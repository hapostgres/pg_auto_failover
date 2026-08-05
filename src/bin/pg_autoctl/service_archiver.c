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
	 * A plain key/value conninfo string: trust/no-password authentication,
	 * matching every other pgaftest docker environment this milestone is
	 * validated against. A real deployment's --ssl/password handling is a
	 * follow-up, mirroring pg_basebackup()'s own PGPASSWORD-env dance
	 * (pgctl.c) once an archiver config carries a replication password.
	 */
	char primaryConnInfo[MAXCONNINFO] = { 0 };

	sformat(primaryConnInfo, sizeof(primaryConnInfo),
			"host=%s port=%d user=%s application_name=%s",
			primaryNode->host, primaryNode->port,
			PG_AUTOCTL_REPLICA_USERNAME, config->name);

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
 * wal_filename_compare is a qsort() comparator over an array of char*,
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

	memcpy(logIdHex, walFileName + 8, 8);
	memcpy(segHex, walFileName + 16, 8);

	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * ARCHIVER_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t position = segno * ARCHIVER_WAL_SEGMENT_SIZE + offsetInSegment;

	snprintf(lsn, lsnSize, "%X/%08X",
			 (uint32_t) (position >> 32),
			 (uint32_t) (position & 0xFFFFFFFF));
}


static void
wal_segment_end_lsn(const char *walFileName, char *lsn, size_t lsnSize)
{
	wal_segment_position_lsn(walFileName, ARCHIVER_WAL_SEGMENT_SIZE, lsn, lsnSize);
}


/*
 * partial_segment_real_length reads a ".partial" WAL segment file (pre-
 * allocated to its full ARCHIVER_WAL_SEGMENT_SIZE by pg_receivewal the
 * moment it's created, matching real Postgres's own WAL file pre-
 * allocation, XLogFileInitInternal) and returns the length of its real
 * content, trimming the zero-padded unwritten tail -- same technique
 * pg_walsender/cmd_start_replication.c's own trim_trailing_zeros() already
 * applies when actually serving one of these files.
 *
 * Trusting a trailing zero run to mean "unwritten" isn't safe in the
 * general case -- a real primary's own WAL segments get recycled (renamed
 * and reused rather than freshly zero-filled, so old content can linger
 * past the real write position) -- but pg_receivewal itself never
 * recycles; every ".partial" file it ever creates is fresh, so this holds
 * here specifically.
 */
static bool
partial_segment_real_length(const char *path, uint64_t *length)
{
	FILE *file = fopen(path, "rb");

	if (file == NULL)
	{
		return false;
	}

	char *buffer = malloc(ARCHIVER_WAL_SEGMENT_SIZE);

	if (buffer == NULL)
	{
		fclose(file);
		return false;
	}

	size_t got = fread(buffer, 1, ARCHIVER_WAL_SEGMENT_SIZE, file);

	fclose(file);

	while (got > 0 && buffer[got - 1] == 0)
	{
		got--;
	}

	free(buffer);

	*length = (uint64_t) got;

	return true;
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

	qsort(names, count, sizeof(char *), wal_filename_compare);

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
 * service_archiver_position_path computes the local, host-only file both
 * the archiver-capture and archiver-serve processes use to exchange the
 * current captured LSN. The two are separate fork()ed processes (see
 * service_archiver_run.c's own comment on why each gets an independent
 * connection) -- each has its own private copy of the Keeper struct after
 * the fork, so keeper->postgres.currentLSN as updated by this file's own
 * service_archiver_update_current_lsn() is invisible to the archiver-serve
 * process no matter how it's written; only a real, external, re-read-each-
 * time channel like this file makes the value cross that boundary. Built
 * from config->pathnames.config exactly like service_archiver_serve.c's own
 * service_archiver_serve_routes_path(), so both independently-started
 * processes compute the identical path from their own (identically loaded)
 * config, without needing shared memory or IPC.
 */
static void
service_archiver_position_path(KeeperConfig *config, char *dest)
{
	path_in_same_directory(config->pathnames.config,
						   "archiver-position", dest);
}


/*
 * service_archiver_persist_current_lsn writes keeper->postgres.currentLSN to
 * the local position file (see service_archiver_position_path's own
 * comment), atomically (write-to-tmp then rename, matching service_archiver_
 * serve_refresh_routes()'s own pattern) so a concurrent reader never
 * observes a partial write.
 */
static bool
service_archiver_persist_current_lsn(Keeper *keeper)
{
	char path[MAXPGPATH] = { 0 };

	service_archiver_position_path(&(keeper->config), path);

	char tmpPath[MAXPGPATH] = { 0 };

	sformat(tmpPath, sizeof(tmpPath), "%s.tmp", path);

	FILE *fileStream = fopen_with_umask(tmpPath, "w", FOPEN_FLAGS_W, 0644);

	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	fformat(fileStream, "%s\n", keeper->postgres.currentLSN);

	if (fclose(fileStream) == EOF)
	{
		log_warn("Failed to write file \"%s\": %m", tmpPath);
		return false;
	}

	if (rename(tmpPath, path) != 0)
	{
		log_warn("Failed to rename \"%s\" to \"%s\": %m", tmpPath, path);
		return false;
	}

	return true;
}


/*
 * service_archiver_read_current_lsn reads back the position file written by
 * service_archiver_persist_current_lsn(), for use by the (separate process)
 * archiver-serve side. Returns false (lsnOut left untouched) when the file
 * doesn't exist yet -- the archiver-capture process hasn't completed its
 * first tick -- callers should fall back to "0/0" themselves.
 */
bool
service_archiver_read_current_lsn(KeeperConfig *config,
								  char *lsnOut, size_t lsnOutSize)
{
	char path[MAXPGPATH] = { 0 };

	service_archiver_position_path(config, path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	char *nl = strchr(contents, '\n');

	if (nl != NULL)
	{
		*nl = '\0';
	}

	strlcpy(lsnOut, contents, lsnOutSize);
	free(contents);

	return lsnOut[0] != '\0';
}


/*
 * service_archiver_update_current_lsn scans walcacheDir for the newest WAL
 * segment -- complete, or still ".partial" -- and updates keeper->postgres.
 * currentLSN to the real, currently-captured position: the full segment
 * boundary for a complete one, or the real (zero-tail-trimmed) content
 * length within the current ".partial" one when that's the frontier. This
 * is the single, out-of-band-maintained source of truth for "how far has
 * this archiver actually captured" -- computed here, once, per tick, and
 * from here alone: both keeper_node_active()'s own per-tick report to the
 * monitor (the same way every other node kind reports its own currentLSN)
 * and service_archiver_serve_refresh_routes()'s own routes-file "position"
 * key (service_archiver_serve.c) read via service_archiver_read_current_lsn()
 * above, rather than each independently re-deriving it by scanning WAL file
 * content on their own -- one canonical value, not several that could
 * disagree.
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
static void
service_archiver_update_current_lsn(Keeper *keeper)
{
	const char *walcacheDir = keeper->config.pgSetup.pgdata;

	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		strlcpy(keeper->postgres.currentLSN, "0/0",
				sizeof(keeper->postgres.currentLSN));
		return;
	}

	char bestComplete[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };
	char bestPartial[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (is_wal_segment_filename(entry->d_name))
		{
			if (bestComplete[0] == '\0' || strcmp(entry->d_name, bestComplete) > 0)
			{
				strlcpy(bestComplete, entry->d_name, sizeof(bestComplete));
			}

			continue;
		}

		const char *partialSuffix = ".partial";
		size_t nameLen = strlen(entry->d_name);
		size_t suffixLen = strlen(partialSuffix);

		if (nameLen == ARCHIVER_WAL_FNAME_LEN + suffixLen &&
			strcmp(entry->d_name + ARCHIVER_WAL_FNAME_LEN, partialSuffix) == 0)
		{
			char segPart[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };

			memcpy(segPart, entry->d_name, ARCHIVER_WAL_FNAME_LEN);

			if (is_wal_segment_filename(segPart) &&
				(bestPartial[0] == '\0' || strcmp(segPart, bestPartial) > 0))
			{
				strlcpy(bestPartial, segPart, sizeof(bestPartial));
			}
		}
	}

	closedir(dir);

	/*
	 * A ".partial" file only ever exists for the segment actively being
	 * written, always the same as or newer than the newest complete one --
	 * whenever it exists at all, it's the real frontier.
	 */
	if (bestPartial[0] != '\0' &&
		(bestComplete[0] == '\0' || strcmp(bestPartial, bestComplete) >= 0))
	{
		char path[MAXPGPATH];
		uint64_t realLength = 0;

		snprintf(path, sizeof(path), "%s/%s.partial", walcacheDir, bestPartial);

		if (partial_segment_real_length(path, &realLength))
		{
			wal_segment_position_lsn(bestPartial, realLength,
									 keeper->postgres.currentLSN,
									 sizeof(keeper->postgres.currentLSN));
			return;
		}

		/* fall through to the complete segment below on read failure */
	}

	if (bestComplete[0] == '\0')
	{
		strlcpy(keeper->postgres.currentLSN, "0/0",
				sizeof(keeper->postgres.currentLSN));
		return;
	}

	wal_segment_end_lsn(bestComplete, keeper->postgres.currentLSN,
						sizeof(keeper->postgres.currentLSN));
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
