/*
 * src/bin/pg_autoctl/service_archiver.c
 *   Archiving & Disaster Recovery: the FSM tick loop for an ARCHIVING node's
 *   membership, plus WAL-capture bookkeeping (reporting captured segments,
 *   tracking the current LSN frontier).
 *
 * Current scope: the colocated fast path only. pg_receivewal is a real,
 * unmodified Postgres client talking straight to the real primary's own
 * walsender -- no new wire protocol needed here at all. This file no
 * longer forks or tracks that child process directly: service_archiver_
 * start_pgreceivewal()/service_archiver_stop_pgreceivewal() below just
 * write a small desired-state file that a dedicated, permanently-
 * supervised controller process reconciles on its own (service_archiver_
 * pgreceivewal_ctl.c -- see that file's own header comment for why pg_
 * receivewal needed its own sibling process rather than staying a direct
 * child of this FSM tick loop). Uses a replication slot, named after this
 * archiver's own node id (see service_archiver_pgreceivewal_set_desired_
 * state()'s own comment).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include "service_archiver.h"

#include "archiver_systemid.h"
#include "archiver_wal_notify.h"
#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "service_archiver_basebackup.h"
#include "service_archiver_pgreceivewal_state.h"
#include "signals.h"

/*
 * The bounded correctness backstop for whatever the WAL-notify socket
 * missed (nothing listening yet, a dropped connection, ...) is no longer a
 * scan inline in this loop -- it's service_archiver_wal_scanner.c, a
 * separate sibling process (like pg_receivewal's own controller) that
 * feeds the exact same socket at its own cadence, independent of the FSM
 * tick. See that file's own header comment.
 */

static ArchiverWalNotifyListener archiverWalNotifyListener = {
	-1, { 0 }
};

/*
 * WAL segment filename layout, duplicated from pg_walsender/wal_dir_scan.c:
 * pg_autoctl doesn't link that standalone binary's code (see this project's
 * Makefile split), so the ~15-line segno/LSN arithmetic is small enough to
 * repeat here rather than share.
 */
#define ARCHIVER_WAL_FNAME_LEN 24
#define ARCHIVER_WAL_SEGMENT_SIZE ((uint64_t) 0x1000000)
#define ARCHIVER_XLOG_SEGMENTS_PER_XLOGID \
	(((uint64_t) 0x100000000) / ARCHIVER_WAL_SEGMENT_SIZE)

/*
 * How often service_archiver_loop() reports storage usage, in ticks
 * (PG_AUTOCTL_KEEPER_SLEEP_TIME apart, currently 1s each) -- directory_size()
 * walks the archiver's whole pgdata (walcache + basebackups, potentially
 * many GB across several retained backups), real I/O work unlike the other
 * per-tick checks in this loop, so it isn't worth doing every single tick.
 */
#define ARCHIVER_STORAGE_REPORT_TICKS 30

/*
 * The timeline of the WAL segment service_archiver_update_current_lsn()
 * most recently found to be the current capture frontier -- computed
 * alongside keeper->postgres.currentLSN there (same scan, same tick), and
 * persisted next to it by service_archiver_persist_current_lsn() so pg_
 * walsender can read both without re-scanning the WAL cache itself. 0
 * means "nothing captured yet", matching currentLSN's own "0/0" default.
 */
static int currentTimeline = 0;


/*
 * service_archiver_pgreceivewal_is_running returns true iff pg_receivewal
 * is currently running, as last observed by its own dedicated controller
 * process (service_archiver_pgreceivewal_ctl.c) -- a plain pidfile read
 * plus a kill(pid, 0) probe, not a waitpid() this process has no business
 * calling anymore: pg_receivewal isn't this process's own child, the
 * controller's is (see that file's own header comment for why splitting
 * them apart this way fixes a real reaping race, not just tidies the code).
 */
bool
service_archiver_pgreceivewal_is_running(Keeper *keeper)
{
	bool isRunning = false;

	(void) service_archiver_pgreceivewal_ctl_is_running(&(keeper->config),
														&isRunning);

	return isRunning;
}


/*
 * service_archiver_stop_pgreceivewal asks pg_receivewal's own controller
 * process to stop it, by writing "running = false" to the desired-state
 * file that controller polls -- see service_archiver_pgreceivewal_ctl.c's
 * own header comment for the full design. Idempotent: writing the same
 * desired state twice is harmless.
 */
bool
service_archiver_stop_pgreceivewal(Keeper *keeper)
{
	return service_archiver_pgreceivewal_set_desired_state(keeper, false, NULL);
}


/*
 * service_archiver_start_pgreceivewal asks pg_receivewal's own controller
 * process (service_archiver_pgreceivewal_ctl.c) to run it against the
 * given primary node, by writing that target to the desired-state file the
 * controller polls -- this function itself no longer forks anything.
 * Idempotent: writing an unchanged target is a harmless no-op for the
 * controller (see ensure_pgreceivewal_matches()'s own comment there).
 *
 * Uses a replication slot, named exactly the way keeper_create_and_drop_
 * replication_slots()/pgsql_replication_slot_create_and_drop() (keeper.c,
 * primary_standby.c, pgsql.c) already name it for an ordinary standby --
 * REPLICATION_SLOT_NAME_DEFAULT + "_" + this archiver's own node id. That
 * mechanism runs on every primary-role node regardless of the other node's
 * kind (AutoFailoverOtherNodesList() has no hasPgData filter, node_active_
 * protocol.c's get_other_nodes()), eagerly creating and maintaining this
 * exact slot on whichever node is currently primary the same way it does
 * for every real standby -- nothing on the primary side needs to change for
 * this to work. Without a slot, a pg_receivewal whose first connection
 * attempt loses the startup HBA-propagation race (a real, observed
 * scenario) restarts streaming from the server's then-current position
 * instead of resuming, permanently and silently skipping every WAL segment
 * in between: report_wal_received() never reports them (they were simply
 * never captured), and any consumer later asked to stream from inside that
 * gap (e.g. pg_walsender's own START_REPLICATION, cmd_start_replication.c)
 * would wait forever for a segment that will never exist. A replication
 * slot fixes this the same way it does for a standby: the slot pins a
 * restart_lsn at creation time and the server retains WAL back to it
 * regardless of how many times the consumer disconnects and reconnects.
 */
bool
service_archiver_start_pgreceivewal(Keeper *keeper, NodeAddress *primaryNode)
{
	return service_archiver_pgreceivewal_set_desired_state(keeper, true,
														   primaryNode);
}


/*
 * is_wal_segment_filename returns true iff name has the shape of a real WAL
 * segment file (24 hex digits) -- this also naturally excludes pg_receivewal's
 * own "<segment>.partial" in-progress file, since it's longer than 24 chars.
 */
static bool
is_wal_segment_filename(const char *name)
{
	size_t len = strlen(name);

	if (len != ARCHIVER_WAL_FNAME_LEN)
	{
		return false;
	}

	for (size_t i = 0; i < len; i++)
	{
		if (!isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


/*
 * wal_segment_end_lsn computes the LSN just past the end of the WAL segment
 * named walFileName -- what report_wal_received() records as "captured up
 * to", matching pg_walsender/wal_dir_scan.c's own wal_dir_find_latest()
 * arithmetic for the same filename layout.
 */
static void
wal_segment_position_lsn(const char *walFileName, uint64_t offsetInSegment,
						 char *lsn, size_t lsnSize)
{
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(logIdHex, walFileName + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, walFileName + 16, 8); /* IGNORE-BANNED */

	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * ARCHIVER_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t position = segno * ARCHIVER_WAL_SEGMENT_SIZE + offsetInSegment;

	sformat(lsn, lsnSize, "%X/%08X",
			(uint32_t) (position >> 32),
			(uint32_t) (position & 0xFFFFFFFF));
}


static void
wal_segment_end_lsn(const char *walFileName, char *lsn, size_t lsnSize)
{
	wal_segment_position_lsn(walFileName, ARCHIVER_WAL_SEGMENT_SIZE, lsn, lsnSize);
}


/*
 * WalReportBatch accumulates every message drained from the WAL-notify
 * socket in a single call to service_archiver_report_captured_wal(), so
 * they can all be reported to the monitor in one bulk round trip
 * (monitor_report_wal_received_bulk()) instead of one round trip per
 * message -- the whole point of batching the drain in the first place.
 * Grown with realloc() the same way the old directory-scan code used to,
 * since a tick's own drain count has no fixed bound.
 */
typedef struct WalReportBatch
{
	char **walFileNames;
	char **lsns;
	int count;
	int capacity;
	uint64_t systemIdentifier;
} WalReportBatch;


/*
 * batch_wal_notify_callback adapts WalReportBatch to ArchiverWalNotify
 * Callback's own signature (archiver_wal_notify.h) -- context is the
 * WalReportBatch* the drain call below was made with. Adds every message
 * drained, unconditionally: an earlier version of this function skipped
 * anything <= a remembered in-process high-water mark as a pure
 * optimization (avoid padding the batch with already-known segments) --
 * but that's only safe if notifications are guaranteed to arrive in
 * strictly increasing filename order, which they are not. pg_receivewal's
 * own live hook (archiver_wal_notify_send() is best-effort: a segment
 * closing while nothing is listening yet is silently dropped, see that
 * function's own header comment) and service_archiver_wal_scanner.c's
 * periodic re-scan are two independent producers feeding the same socket
 * -- a live notification for an OLDER segment can arrive after a NEWER
 * one already advanced the high-water mark (e.g. the older one's own live
 * notification was dropped because the listener wasn't open yet right
 * after a restart, while the newer one's succeeded moments later), and a
 * high-water-mark gate then permanently, silently drops that older
 * segment the very first time the scanner's own later re-discovery tries
 * to report it too -- a real, observed data-loss bug this fixes, not a
 * hypothetical one. The monitor side is already idempotent (ON CONFLICT
 * DO NOTHING), so unconditionally batching -- occasionally re-sending an
 * already-recorded segment -- costs a few redundant rows compared against
 * on every insert, genuinely cheap next to permanently losing one.
 */
static bool
batch_wal_notify_callback(void *context, const char *walFileName,
						  const char *lsn, uint64_t systemIdentifier)
{
	WalReportBatch *batch = (WalReportBatch *) context;

	if (batch->count == batch->capacity)
	{
		batch->capacity = batch->capacity == 0 ? 16 : batch->capacity * 2;
		batch->walFileNames =
			realloc(batch->walFileNames, batch->capacity * sizeof(char *));
		batch->lsns =
			realloc(batch->lsns, batch->capacity * sizeof(char *));
	}

	batch->walFileNames[batch->count] = strdup(walFileName);
	batch->lsns[batch->count] = strdup(lsn);
	batch->systemIdentifier = systemIdentifier;
	batch->count++;

	return true;
}


/*
 * flush_wal_report_batch reports everything batch_wal_notify_callback()
 * accumulated in one monitor_report_wal_received_bulk() call -- a monitor
 * hiccup fails the whole batch, retried (along with whatever else has
 * accumulated by then) on the next tick, since nothing here is marked
 * "done" until the report actually succeeds. Frees the batch's own
 * storage regardless of outcome.
 */
static bool
flush_wal_report_batch(Keeper *keeper, WalReportBatch *batch)
{
	bool success = true;

	if (batch->count > 0)
	{
		success = monitor_report_wal_received_bulk(
			&(keeper->monitor), keeper->state.current_node_id,
			batch->systemIdentifier, batch->walFileNames, batch->lsns,
			batch->count);

		if (!success)
		{
			log_error("Failed to report %d captured WAL file(s) to the "
					  "monitor", batch->count);
		}

		for (int i = 0; i < batch->count; i++)
		{
			free(batch->walFileNames[i]);
			free(batch->lsns[i]);
		}
	}

	free(batch->walFileNames);
	free(batch->lsns);

	return success;
}


/*
 * service_archiver_report_captured_wal is the FSM tick's own entry point:
 * drains whatever the WAL-notify socket currently has queued -- fed both
 * by pg_receivewal's own live hook and by this membership's own periodic
 * scanner process (service_archiver_wal_scanner.c, the bounded
 * correctness backstop for whatever the socket alone might miss) -- and
 * reports the whole batch to the monitor in one round trip.
 */
bool
service_archiver_report_captured_wal(Keeper *keeper)
{
	if (archiverWalNotifyListener.listenFd < 0)
	{
		/* nothing to drain without a working listener -- the periodic
		 * scanner process is still feeding the monitor independently, this
		 * tick just has nothing of its own to report */
		return true;
	}

	WalReportBatch batch = { 0 };

	bool drained = archiver_wal_notify_listener_drain(
		&archiverWalNotifyListener,
		&batch_wal_notify_callback,
		(void *) &batch);

	bool reported = flush_wal_report_batch(keeper, &batch);

	return drained && reported;
}


/*
 * service_archiver_position_path computes the local file this membership's
 * capture process persists its own currently-captured LSN and timeline
 * to, once a tick. Inside config->pgSetup.pgdata itself (this membership's
 * own walcache root), not sibling of config->pathnames.config the way it
 * used to be -- pg_walsender only ever learns one path per membership
 * (routes.h's own "path" field, written by service_archiver_reconciler.c),
 * and that path is pgdata, so anything pg_walsender needs to read on its
 * own (wal_position_cache_read(), wal_dir_scan.c) has to live under it,
 * matching archiver_systemid_path()'s own placement (archiver_systemid.c).
 */
static void
service_archiver_position_path(KeeperConfig *config, char *dest)
{
	sformat(dest, MAXPGPATH, "%s/archiver-position", config->pgSetup.pgdata);
}


/*
 * service_archiver_persist_current_lsn writes keeper->postgres.currentLSN
 * and currentTimeline (both computed together by service_archiver_update_
 * current_lsn() just before this is called) to the local position file
 * (see service_archiver_position_path's own comment), atomically (write-
 * to-tmp then rename, matching every other file this project writes this
 * way) so a concurrent reader never observes a partial write. pg_
 * walsender reads this directly (wal_position_cache_read(), wal_dir_scan.
 * c) instead of scanning the WAL cache directory itself on every
 * connection.
 */
static bool
service_archiver_persist_current_lsn(Keeper *keeper)
{
	char path[MAXPGPATH] = { 0 };

	service_archiver_position_path(&(keeper->config), path);

	char contents[128] = { 0 };

	int size = sformat(contents, sizeof(contents),
					   "lsn = %s\ntimeline = %d\n",
					   keeper->postgres.currentLSN, currentTimeline);

	return write_file_atomic(contents, size, path);
}


/*
 * service_archiver_maybe_persist_systemid writes this group's system
 * identifier to the local file archiver_systemid.c's own path computes
 * (archiver_systemid_path()), once. Unlike the position file, this never
 * needs refreshing once written: a Postgres cluster's system identifier is
 * set at initdb and never changes for its lifetime, so write-once is not a
 * simplification that trades away correctness, it's the actually-correct
 * behavior -- there is no "stale" system identifier to worry about
 * invalidating. Both pg_receivewal's own WalSegmentClosedHook (service_
 * archiver_pgreceivewal_ctl.c) and the periodic scanner (service_archiver_
 * wal_scanner.c) read this same file (archiver_systemid_read()) to tag
 * every WAL-notify message with it.
 *
 * A no-op once the file already exists. Before that, asks the monitor once
 * per tick (see monitor_get_group_system_identifier()'s own comment,
 * monitor.c, for why: an archiving node has no real pg_control of its own
 * to read this from directly, it can only learn what the group's real
 * primary already self-reported at ordinary node registration) until the
 * value becomes available -- harmless and cheap to keep asking meanwhile,
 * this is best-effort and never blocks the rest of the loop.
 */
static void
service_archiver_maybe_persist_systemid(Keeper *keeper)
{
	char path[MAXPGPATH] = { 0 };

	archiver_systemid_path(&(keeper->config), path, sizeof(path));

	if (file_exists(path))
	{
		return;
	}

	uint64_t systemIdentifier = 0;
	bool found = false;

	if (!monitor_get_group_system_identifier(&(keeper->monitor),
											 keeper->config.formation,
											 keeper->config.groupId,
											 &systemIdentifier, &found) ||
		!found || systemIdentifier == 0)
	{
		/* not known yet, or the monitor couldn't be reached -- retry next
		 * tick, errors (if any) have already been logged */
		return;
	}

	char contents[32] = { 0 };
	int size = sformat(contents, sizeof(contents), "%" PRIu64 "\n", systemIdentifier);

	(void) write_file_atomic(contents, size, path);
}


/*
 * service_archiver_update_current_lsn scans walcacheDir for the newest
 * *complete* WAL segment and updates keeper->postgres.currentLSN to that
 * segment's own end boundary. This is the single, out-of-band-maintained
 * source of truth for "how far has this archiver actually captured" --
 * computed here, once, per tick, and from here alone: both keeper_node_
 * active()'s own per-tick report to the monitor (the same way every other
 * node kind reports its own currentLSN) and the archiver-position cache
 * file pg_walsender reads (service_archiver_persist_current_lsn() below,
 * wal_position_cache_read() on the pg_walsender side) come from this one
 * scan, rather than each independently re-deriving it by scanning WAL file
 * content on their own -- one canonical value, not several that could
 * disagree.
 *
 * Deliberately ignores a still-in-progress ".partial" segment even when
 * it's the real frontier: a complete segment's own end boundary is
 * guaranteed to have been fully, durably received, while an in-progress
 * ".partial" file's raw (zero-tail-trimmed) byte count is not guaranteed
 * to land on a genuine WAL record boundary -- it can be caught mid-record.
 * That distinction matters because this value doubles as a FAST_FORWARD
 * convergence target for another node rebuilding from this archiver
 * (standby_fetch_missing_wal(), primary_standby.c): Postgres's own replay
 * can only ever advance to real record boundaries, so a target that isn't
 * one can leave that poll waiting forever once the original source primary
 * is gone and nothing more will ever arrive to complete the cut-off
 * record. Reporting the last complete segment's boundary instead costs
 * this value a little freshness (up to just under one segment's worth of
 * already-captured-but-not-yet-reported WAL), never correctness: pg_
 * walsender's own streaming loop (cmd_start_replication.c) still serves
 * everything genuinely available, including ".partial" content, past
 * whatever target callers converge on, so replay always has real data to
 * advance through and beyond it.
 *
 * This is also what makes an archiving node a real, rankable candidate for
 * pgautofailover.get_most_advanced_standby() during a failover election:
 * that query already has no kind-based exclusion and already considers any
 * node reporting REPORT_LSN_STATE (an archiving node passes through it
 * during elections, see ARCHIVING_STATE -> REPORT_LSN_STATE in fsm.c) --
 * the only thing that ever kept an archiver from being selected was this
 * value staying "0/0" forever. Falls back to "0/0" itself when nothing has
 * been captured yet, matching keeper_update_pg_state()'s own default
 * before it has a real reading.
 */

/*
 * wal_segment_timeline extracts the timeline (the filename's first 8 hex
 * digits) from a real WAL segment filename.
 */
static int
wal_segment_timeline(const char *walFileName)
{
	char tliHex[9] = { 0 };

	memcpy(tliHex, walFileName, 8); /* IGNORE-BANNED */

	return (int) strtoul(tliHex, NULL, 16);
}


static void
service_archiver_update_current_lsn(Keeper *keeper)
{
	const char *walcacheDir = keeper->config.pgSetup.pgdata;

	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		strlcpy(keeper->postgres.currentLSN, "0/0",
				sizeof(keeper->postgres.currentLSN));
		currentTimeline = 0;
		return;
	}

	char bestComplete[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (is_wal_segment_filename(entry->d_name) &&
			(bestComplete[0] == '\0' || strcmp(entry->d_name, bestComplete) > 0))
		{
			strlcpy(bestComplete, entry->d_name, sizeof(bestComplete));
		}
	}

	closedir(dir);

	if (bestComplete[0] == '\0')
	{
		strlcpy(keeper->postgres.currentLSN, "0/0",
				sizeof(keeper->postgres.currentLSN));
		currentTimeline = 0;
		return;
	}

	wal_segment_end_lsn(bestComplete, keeper->postgres.currentLSN,
						sizeof(keeper->postgres.currentLSN));
	currentTimeline = wal_segment_timeline(bestComplete);
}


/*
 * service_archiver_report_storage reports this archiver's own disk usage
 * (directory_size() over its whole pgdata -- walcache and basebackups
 * share the same root, see service_archiver_serve.c's own header comment)
 * and free space (statvfs's f_bavail, "available to a non-privileged
 * process" -- what actually predicts whether the next base backup or WAL
 * segment fits, not f_bfree's superuser-reserved total) to the monitor.
 *
 * Skips the report outright on a statvfs failure rather than reporting a
 * free space of zero: unlike directory_size()'s own "best effort, this is
 * informational" stance, a wrong zero here would misleadingly read as
 * "completely full" to anything watching (pg_autoctl watch's own archivers
 * section).
 */
static bool
service_archiver_report_storage(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	const char *pgdata = config->pgSetup.pgdata;

	uint64_t usedBytes = directory_size(pgdata);

	struct statvfs fsStats = { 0 };

	if (statvfs(pgdata, &fsStats) != 0)
	{
		log_warn("Failed to statvfs \"%s\": %m, skipping this storage report",
				 pgdata);
		return false;
	}

	uint64_t freeBytes = (uint64_t) fsStats.f_bavail * (uint64_t) fsStats.f_frsize;

	if (!monitor_report_archiver_storage(&(keeper->monitor), config->archiverId,
										 usedBytes, freeBytes))
	{
		log_warn("Failed to report storage usage to the monitor, will retry");
		return false;
	}

	return true;
}


/*
 * service_archiver_loop is the archiver's own node_active() reporting loop
 * -- deliberately not keeper_node_active_loop (service_keeper.c): that
 * function's own per-tick keeper_update_pg_state()/keeper_ensure_current_
 * state() calls assume a real Postgres instance with a real PGDATA to
 * inspect, which an ARCHIVING node never has (see haspgdata's own design
 * comment). This loop reuses everything that IS kind-agnostic --
 * keeper_load_state()/keeper_store_state(), keeper_node_active() (the
 * monitor RPC wrapper itself only ever reads Keeper's in-memory fields,
 * never touches real Postgres), and keeper_fsm_reach_assigned_state()
 * dispatching through the very same KeeperFSM[] table -- while replacing
 * the two Postgres-specific calls with nothing at all: an ARCHIVING row's
 * pg_receivewal liveness is its own dedicated controller process's
 * responsibility now (service_archiver_pgreceivewal_ctl.c), not this
 * loop's -- see that file's own header comment for why.
 *
 * Single-membership scope (see this file's own header
 * comment): one archiver, one (formation, group) row, reported here
 * directly rather than iterating a list the monitor refreshes.
 */
bool
service_archiver_loop(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);

	log_info("pg_autoctl archiver service is starting");

	/*
	 * An archiver never calls keeper_update_pg_state() -- there's no real
	 * Postgres instance to query (see haspgdata's own design comment) --
	 * so keeper->postgres.currentLSN needs its own source of truth here.
	 * keeper_node_active() always sends it as one of node_active()'s own
	 * parameters, and the monitor-side pg_lsn column rejects an empty
	 * string outright ("invalid input syntax for type pg_lsn"), so it must
	 * hold a valid value even before the first tick's own scan runs.
	 */
	strlcpy(keeper->postgres.currentLSN, "0/0", sizeof(keeper->postgres.currentLSN));

	/*
	 * Best-effort: a failure here just means every tick falls back to the
	 * full scan until the next process restart tries again -- logged, not
	 * fatal, matching this loop's own tolerance for every other per-tick
	 * failure below.
	 */
	if (!archiver_wal_notify_listener_open(&(keeper->config),
										   &archiverWalNotifyListener))
	{
		log_warn("Failed to open the WAL-notify listener, falling back to "
				 "scanning the WAL cache directory every tick");
	}

	/*
	 * service_archiver_start_pgreceivewal() (and the desired-state file it
	 * writes for pg_receivewal's own controller, service_archiver_
	 * pgreceivewal_ctl.c) is otherwise only ever called from an FSM
	 * transition *into* ARCHIVING_STATE (fsm_init_archiver()/fsm_archiver_
	 * follow_new_primary(), fsm.c's own MonitorFSM[] rows) -- never on a
	 * plain restart of this process where current_role is already
	 * "archiving" and the monitor keeps assigning the very same state, so
	 * no transition ever fires. Left alone, that's a real bug, not a
	 * theoretical one: this process's own graceful-shutdown path below
	 * (service_archiver_stop_pgreceivewal()) unconditionally tells the
	 * controller to stop pg_receivewal on every exit, so a restart with no
	 * transition would otherwise leave it stopped forever -- confirmed via
	 * a real hang (pg_receivewal never restarting after a container
	 * restart, archiver_wal_capture.pgaf's own test_002). Re-asserting the
	 * desired state here, once, before the first tick, whenever this
	 * membership is already assigned ARCHIVING_STATE closes that gap: fsm_
	 * init_archiver() is idempotent (service_archiver_pgreceivewal_set_
	 * desired_state()'s own comment), so calling it again here is a
	 * harmless no-op on every ordinary cold start, where the WAIT_STANDBY
	 * -> ARCHIVING transition already asserted it moments earlier.
	 */
	if (keeper_load_state(keeper) &&
		keeperState->current_role == ARCHIVING_STATE)
	{
		if (!fsm_init_archiver(keeper))
		{
			log_warn("Failed to restart pg_receivewal after a restart, "
					 "will retry once the FSM tick reaches the monitor");
		}
	}

	int tickCount = 0;

	while (!asked_to_stop && !asked_to_stop_fast && !asked_to_quit)
	{
		MonitorAssignedState assignedState = { 0 };

		(void) service_archiver_update_current_lsn(keeper);
		(void) service_archiver_persist_current_lsn(keeper);
		(void) service_archiver_maybe_persist_systemid(keeper);

		/*
		 * An archiver never sets postgres.pgIsRunning through the usual
		 * keeper_update_pg_state() path (there's no real Postgres to
		 * query, see haspgdata's own design comment) -- it stays at its
		 * zero-initialized false forever otherwise. Reported here as
		 * pg_receivewal's own real liveness (service_archiver_
		 * pgreceivewal_is_running()), the same "is it running" check
		 * service_archiver_pgreceivewal_is_running(Keeper*) already makes
		 * for local use -- giving operators and tests a SQL-visible way to
		 * see it (pgautofailover.node.pgisrunning) instead of only ever
		 * being able to infer it from log lines.
		 *
		 * This is safe for FAST_FORWARD candidate selection (group_state_
		 * machine.c's WalSourceNodesAreAllUnhealthy(), via NodeIsHealthy())
		 * despite pg_receivewal legitimately being stopped exactly when a
		 * FAST_FORWARD candidate needs a WAL source most (the group's
		 * primary just died, fsm_archiver_report_lsn() stops pg_receivewal
		 * against the now-untrustworthy old primary): what actually serves
		 * WAL to a FAST_FORWARD candidate is pg_walsender ("archiver-
		 * serve"), a separate, independently-supervised top-level process
		 * this loop has no bearing on at all -- pg_receivewal only ever
		 * affects how *fresh* the already-captured WAL is, never whether
		 * it can be served. NodeIsHealthy()/NodeIsUnhealthy() (node_
		 * metadata.c) are themselves updated to stop requiring pgIsRunning
		 * for a !hasPgData (archiver) row, for exactly this reason.
		 */
		keeper->postgres.pgIsRunning =
			service_archiver_pgreceivewal_is_running(keeper);

		if (!keeper_load_state(keeper))
		{
			log_error("Failed to read archiver state file, retrying...");
		}
		else if (keeper_node_active(keeper, false, &assignedState))
		{
			keeperState->assigned_role = assignedState.state;

			if (keeperState->current_role != keeperState->assigned_role)
			{
				if (keeper_fsm_reach_assigned_state(keeper))
				{
					(void) keeper_store_state(keeper);
				}
				else
				{
					log_error("Failed to reach assigned state \"%s\", "
							  "retrying...",
							  NodeStateToString(keeperState->assigned_role));
				}
			}

			if (!service_archiver_report_captured_wal(keeper))
			{
				log_warn("Failed to report newly captured WAL segments to "
						 "the monitor, will retry");
			}

			if (!service_archiver_maybe_generate_basebackup(keeper))
			{
				log_warn("Failed to generate a base backup, will retry");
			}

			if (tickCount % ARCHIVER_STORAGE_REPORT_TICKS == 0)
			{
				(void) service_archiver_report_storage(keeper);
			}
		}
		else
		{
			log_warn("Failed to contact the monitor, retrying...");
		}

		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			break;
		}

		sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		++tickCount;
	}

	(void) service_archiver_stop_pgreceivewal(keeper);
	(void) archiver_wal_notify_listener_close(&archiverWalNotifyListener);

	log_info("pg_autoctl archiver service is stopping");

	return true;
}
