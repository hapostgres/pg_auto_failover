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
#include <sys/wait.h>
#include <unistd.h>

#include "service_archiver.h"

#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
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

	log_info("Starting pg_receivewal against %s:%d, writing to \"%s\"",
			 primaryNode->host, primaryNode->port, config->pgSetup.pgdata);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork pg_receivewal: %m");
		return false;
	}

	if (pid == 0)
	{
		/* child process: replace ourselves with pg_receivewal */
		char *args[8];
		int argsIndex = 0;

		args[argsIndex++] = pgReceivewalPath;
		args[argsIndex++] = "-w";
		args[argsIndex++] = "-d";
		args[argsIndex++] = primaryConnInfo;
		args[argsIndex++] = "-D";
		args[argsIndex++] = config->pgSetup.pgdata;
		args[argsIndex++] = "--no-sync";
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
wal_segment_end_lsn(const char *walFileName, char *lsn, size_t lsnSize)
{
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(logIdHex, walFileName + 8, 8);
	memcpy(segHex, walFileName + 16, 8);

	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * ARCHIVER_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t endOfSegment = (segno + 1) * ARCHIVER_WAL_SEGMENT_SIZE;

	snprintf(lsn, lsnSize, "%X/%08X",
			 (uint32_t) (endOfSegment >> 32),
			 (uint32_t) (endOfSegment & 0xFFFFFFFF));
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
	 * so keeper->postgres.currentLSN is otherwise left at its zero-valued
	 * empty string for the lifetime of this process. keeper_node_active()
	 * always sends it as one of node_active()'s own parameters, and the
	 * monitor-side pg_lsn column rejects an empty string outright ("invalid
	 * input syntax for type pg_lsn"). "0/0" is the same placeholder
	 * keeper_update_pg_state() itself defaults to before it has a real
	 * reading; an archiver's own WAL-capture progress is tracked
	 * separately via archiver_wal, not through this per-node report.
	 */
	strlcpy(keeper->postgres.currentLSN, "0/0", sizeof(keeper->postgres.currentLSN));

	while (!asked_to_stop && !asked_to_stop_fast && !asked_to_quit)
	{
		MonitorAssignedState assignedState = { 0 };

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
	}

	(void) service_archiver_stop_pgreceivewal();

	log_info("pg_autoctl archiver service is stopping");

	return true;
}
