/*
 * src/bin/pg_autoctl/service_archiver.c
 *   Archiving & Disaster Recovery: supervision of the pg_receivewal child
 *   process an ARCHIVING node keeps running against its group's current
 *   primary.
 *
 * Milestone 2's own scope, per the Build order in
 * ~/dev/temp/archiving-disaster-recovery.md: the colocated fast path only.
 * pg_receivewal is a real, unmodified Postgres client talking straight to
 * the real primary's own walsender -- no new wire protocol needed here at
 * all. This file only launches and tracks that one child process; it does
 * not yet integrate with supervisor.c's Service/RestartPolicy machinery
 * (a liveness check happens on each FSM tick instead, via
 * service_archiver_pgreceivewal_is_running(), the same "is it alive"
 * check the design doc's own ARCHIVING FSM section describes for
 * keeper_ensure_current_state) -- and does not yet use a replication slot
 * (WAL retention across a pg_receivewal restart is a follow-up).
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

#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "service_archiver_basebackup.h"
#include "signals.h"

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
 * Last WAL filename already reported to the monitor, so each tick only
 * reports newly-appeared segments instead of re-scanning and re-reporting
 * the whole cache directory every time (the monitor-side insert is
 * idempotent, ON CONFLICT DO NOTHING, but that's a fallback for restarts,
 * not meant to be relied on every tick).
 */
static char lastReportedWalFileName[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };

/*
 * One pg_receivewal child per archiver process, matching milestone 2's own
 * single-membership scope (see this file's own comment) -- a future
 * milestone generalizing to several (formation, group) memberships per
 * archiver will need one pid per membership instead of this one global.
 */
static pid_t pgReceivewalPid = -1;

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
 * service_archiver_pgreceivewal_is_running returns true iff the tracked
 * pg_receivewal child is still alive. waitpid(WNOHANG) both checks and
 * reaps: called on every FSM tick, so a child that exited between ticks is
 * reaped promptly rather than lingering as a zombie.
 */
bool
service_archiver_pgreceivewal_is_running(void)
{
	if (pgReceivewalPid <= 0)
	{
		return false;
	}

	int status = 0;
	pid_t ret = waitpid(pgReceivewalPid, &status, WNOHANG);

	if (ret == 0)
	{
		/* still running */
		return true;
	}

	if (ret == pgReceivewalPid)
	{
		log_warn("pg_receivewal (pid %d) exited with status %d",
				 pgReceivewalPid, status);
	}
	else
	{
		log_warn("Failed to check on pg_receivewal (pid %d): %m",
				 pgReceivewalPid);
	}

	pgReceivewalPid = -1;
	return false;
}


/*
 * service_archiver_stop_pgreceivewal stops the tracked pg_receivewal child,
 * if any. Idempotent: a no-op when nothing is tracked or the child has
 * already exited on its own.
 */
bool
service_archiver_stop_pgreceivewal(void)
{
	if (!service_archiver_pgreceivewal_is_running())
	{
		return true;
	}

	log_info("Stopping pg_receivewal (pid %d)", pgReceivewalPid);

	if (kill(pgReceivewalPid, SIGTERM) != 0 && errno != ESRCH)
	{
		log_error("Failed to send SIGTERM to pg_receivewal (pid %d): %m",
				  pgReceivewalPid);
		return false;
	}

	int status = 0;

	if (waitpid(pgReceivewalPid, &status, 0) == -1 && errno != ECHILD)
	{
		log_error("Failed to wait for pg_receivewal (pid %d) to stop: %m",
				  pgReceivewalPid);
		pgReceivewalPid = -1;
		return false;
	}

	pgReceivewalPid = -1;
	return true;
}


/*
 * service_archiver_start_pgreceivewal starts pg_receivewal against the
 * given primary node, writing captured WAL into the archiver's own local
 * storage directory (config->pgSetup.pgdata -- an ARCHIVING node's config
 * reuses the same field an ordinary node uses for its real PGDATA, see
 * this project's own cli_create_archiver, since it plays the same "this
 * node's local root directory" role here without ever holding a real
 * Postgres cluster). Idempotent: stops any previously-tracked child first,
 * exactly like fsm_init_standby's own upstream reuse pattern.
 *
 * Passes -S/--slot, naming the slot exactly the way keeper_create_and_drop_
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
	KeeperConfig *config = &(keeper->config);

	if (!service_archiver_stop_pgreceivewal())
	{
		/* errors have already been logged */
		return false;
	}

	char pgReceivewalPath[MAXPGPATH] = { 0 };

	path_in_same_directory(config->pgSetup.pg_ctl,
						   "pg_receivewal",
						   pgReceivewalPath);

	if (!file_exists(pgReceivewalPath))
	{
		log_error("Failed to find pg_receivewal at \"%s\"", pgReceivewalPath);
		return false;
	}

	/*
	 * Create-if-missing only -- never ensure_empty_dir(), which rmtree()s
	 * first: this directory holds already-captured WAL across restarts,
	 * the whole point of running an archiver.
	 */
	if (!directory_exists(config->pgSetup.pgdata) &&
		mkdir(config->pgSetup.pgdata, 0700) != 0)
	{
		log_error("Failed to create archiver WAL directory \"%s\": %m",
				  config->pgSetup.pgdata);
		return false;
	}

	/*
	 * Same helper every standby's own primary_conninfo goes through
	 * (pgctl.c): sslmode/sslrootcert/sslcrl from config->pgSetup.ssl (so
	 * cert auth works exactly as it does for any other node -- libpq picks
	 * up the client certificate from ~/.postgresql/ once sslmode requests
	 * SSL, no extra flag needed here), plus password= when config->
	 * replication_password is set (md5/password auth) -- prepare_primary_
	 * conninfo() itself skips that clause when the password is empty, so
	 * this is still a plain trust/no-password conninfo by default,
	 * unchanged from before this now goes through the shared builder.
	 * escape = false: this string is a pg_receivewal `-d` argument, not a
	 * quoted primary_conninfo GUC value.
	 */
	char primaryConnInfo[MAXCONNINFO] = { 0 };

	if (!prepare_primary_conninfo(primaryConnInfo,
								  sizeof(primaryConnInfo),
								  primaryNode->host,
								  primaryNode->port,
								  PG_AUTOCTL_REPLICA_USERNAME,
								  NULL,
								  config->replication_password,
								  config->name,
								  config->pgSetup.ssl,
								  false))
	{
		log_error("Failed to prepare the archiver's connection string to "
				  "the primary, see above for details");
		return false;
	}

	char slotName[MAXCONNINFO] = { 0 };

	sformat(slotName, sizeof(slotName), "%s_%d",
			REPLICATION_SLOT_NAME_DEFAULT, keeper->state.current_node_id);

	log_info("Starting pg_receivewal against %s:%d, writing to \"%s\", "
			 "using replication slot \"%s\"",
			 primaryNode->host, primaryNode->port, config->pgSetup.pgdata,
			 slotName);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork pg_receivewal: %m");
		return false;
	}

	if (pid == 0)
	{
		/* child process: replace ourselves with pg_receivewal */
		char *args[10];
		int argsIndex = 0;

		args[argsIndex++] = pgReceivewalPath;
		args[argsIndex++] = "-w";
		args[argsIndex++] = "-d";
		args[argsIndex++] = primaryConnInfo;
		args[argsIndex++] = "-D";
		args[argsIndex++] = config->pgSetup.pgdata;
		args[argsIndex++] = "--no-sync";
		args[argsIndex++] = "-S";
		args[argsIndex++] = slotName;
		args[argsIndex] = NULL;

		execv(pgReceivewalPath, args);

		/* execv only returns on failure */
		log_fatal("execv(\"%s\"): %m", pgReceivewalPath);
		_exit(127);
	}

	/* parent process: track the child, keep running our own loop */
	pgReceivewalPid = pid;

	return true;
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
 * wal_filename_compare is a pg_qsort() comparator over an array of char*,
 * ordering WAL segment filenames the same way their fixed-width hex names
 * already sort lexicographically (== numerically, oldest to newest).
 */
static int
wal_filename_compare(const void *a, const void *b)
{
	const char *nameA = *(const char *const *) a;
	const char *nameB = *(const char *const *) b;

	return strcmp(nameA, nameB);
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
 * service_archiver_report_captured_wal scans the archiver's local WAL cache
 * directory for segments pg_receivewal has completed (i.e. no longer
 * ".partial") since the last-reported filename, and reports each one to the
 * monitor via monitor_report_wal_received() -- the mechanism backing
 * archiver_wal/wal_archived(), so archive_command callers elsewhere in the
 * cluster can learn when a segment has landed durably on quorum archivers.
 *
 * Reports oldest-to-newest and only advances lastReportedWalFileName past a
 * segment once its report has actually succeeded, so a monitor hiccup
 * retries that segment (and anything after it) on the next tick instead of
 * silently skipping it.
 */
bool
service_archiver_report_captured_wal(Keeper *keeper)
{
	const char *walcacheDir = keeper->config.pgSetup.pgdata;

	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		/* nothing captured yet -- not an error */
		return true;
	}

	char **names = NULL;
	int count = 0;
	int capacity = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (!is_wal_segment_filename(entry->d_name))
		{
			continue;
		}

		if (strcmp(entry->d_name, lastReportedWalFileName) <= 0)
		{
			continue;
		}

		if (count == capacity)
		{
			capacity = capacity == 0 ? 16 : capacity * 2;
			names = realloc(names, capacity * sizeof(char *));
		}

		names[count++] = strdup(entry->d_name);
	}

	closedir(dir);

	if (count == 0)
	{
		return true;
	}

	pg_qsort(names, count, sizeof(char *), wal_filename_compare);

	bool success = true;

	for (int i = 0; i < count; i++)
	{
		if (success)
		{
			char lsn[PG_LSN_MAXLENGTH] = { 0 };

			wal_segment_end_lsn(names[i], lsn, sizeof(lsn));

			if (monitor_report_wal_received(&(keeper->monitor),
											keeper->state.current_node_id,
											names[i], lsn))
			{
				strlcpy(lastReportedWalFileName, names[i],
						sizeof(lastReportedWalFileName));
			}
			else
			{
				log_error("Failed to report WAL file \"%s\" to the monitor",
						  names[i]);
				success = false;
			}
		}

		free(names[i]);
	}

	free(names);

	return success;
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
 * matching service_archiver_systemid_path()'s own placement below.
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
 * service_archiver_systemid_path computes the local file holding this
 * membership's group's Postgres system identifier -- inside config->
 * pgSetup.pgdata itself (this membership's own walcache root), matching
 * service_archiver_position_path()'s own placement above: pg_walsender
 * only ever learns one path per membership (routes.h's own "path" field,
 * written by service_archiver_reconciler.c), and that path is pgdata, so
 * anything pg_walsender needs to find on its own has to live under it.
 */
static void
service_archiver_systemid_path(KeeperConfig *config, char *dest)
{
	sformat(dest, MAXPGPATH, "%s/archiver-systemid", config->pgSetup.pgdata);
}


/*
 * service_archiver_maybe_persist_systemid writes this group's system
 * identifier to the local file above, once. Unlike the position file, this
 * never needs refreshing once written: a Postgres cluster's system
 * identifier is set at initdb and never changes for its lifetime, so
 * write-once is not a simplification that trades away correctness, it's
 * the actually-correct behavior -- there is no "stale" system identifier to
 * worry about invalidating.
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

	service_archiver_systemid_path(&(keeper->config), path);

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
 * only "is it running" check is service_archiver_pgreceivewal_is_running(),
 * consulted by the FSM transition functions themselves
 * (fsm_init_archiver et al., fsm_transition.c), not by this loop.
 *
 * Milestone 2's own single-membership scope (see this file's own header
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
		 * zero-initialized false forever otherwise. That's not just
		 * cosmetic: the monitor's own NodeIsHealthy() (node_metadata.c)
		 * unconditionally requires pgIsRunning to be true before ever
		 * considering a node healthy, in every one of its branches --
		 * including group_state_machine.c's own FAST_FORWARD candidate
		 * selection, which refuses to assign fast_forward against an
		 * unhealthy WAL source. Without this, an archiver could never
		 * legitimately serve as a FAST_FORWARD WAL source no matter how
		 * caught up it was: the monitor would always see it as unhealthy
		 * and never select it.
		 *
		 * Deliberately NOT tied to service_archiver_pgreceivewal_is_
		 * running(): that reflects a narrower "is WAL actively being
		 * captured from a live primary right now" fact, which is
		 * legitimately false exactly during the window a FAST_FORWARD
		 * candidate needs the archiver most -- pg_receivewal has nothing
		 * to stream from once the primary it was following is dead, but
		 * the WAL this archiver already captured is still there and still
		 * servable via pg_walsender regardless. pgIsRunning here means
		 * "this archiver's own keeper service is alive and reporting",
		 * the same thing a real node's pgIsRunning=true ultimately proves
		 * about itself -- a crashed or partitioned archiver is still
		 * caught by the monitor's own separate report-staleness check
		 * (NodeIsUnhealthy's reportTime/unhealthyTimeoutMs), which
		 * doesn't depend on this flag at all.
		 */
		keeper->postgres.pgIsRunning = true;

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

			/*
			 * Liveness check: a state transition only (re)starts
			 * pg_receivewal at the moment current_role becomes
			 * ARCHIVING_STATE (fsm_init_archiver/fsm_archiver_follow_new_
			 * primary, fsm_transition.c) -- it does not run again on later
			 * ticks where current_role and assigned_role already agree.
			 * Without this check, a pg_receivewal that dies (or an archiver
			 * process that gets restarted while already ARCHIVING) would
			 * stay down forever instead of being noticed and restarted here,
			 * exactly the "is it running" check this loop's own header
			 * comment describes.
			 */
			if (keeperState->current_role == ARCHIVING_STATE &&
				!service_archiver_pgreceivewal_is_running())
			{
				NodeAddress primaryNode = { 0 };

				if (!keeper_get_primary(keeper, &primaryNode) ||
					!service_archiver_start_pgreceivewal(keeper, &primaryNode))
				{
					log_error("Failed to restart pg_receivewal, retrying...");
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

	(void) service_archiver_stop_pgreceivewal();

	log_info("pg_autoctl archiver service is stopping");

	return true;
}
