/*
 * src/bin/pg_autoctl/service_archiver_pgreceivewal_ctl.c
 *   See service_archiver_pgreceivewal_ctl.h.
 *
 * Mirrors this project's own postgres-controller shape (service_postgres_
 * ctl.c) for pg_receivewal: a dedicated, permanently-supervised process
 * per membership, forked directly by service_archiver_reconciler.c
 * alongside that membership's own "archiver-capture-<formation>-<group>"
 * process (siblings, not parent/child) rather than pg_receivewal being a
 * direct child of the FSM tick loop the way it used to be.
 *
 * Why this matters: supervisor.c's own central reap loop
 * (service_supervisor(), supervisor.c) calls waitpid(-1, WNOHANG) --
 * a wildcard wait that reaps *any* child of the calling process. When
 * pg_receivewal was forked directly from inside "archiver capture" (a
 * process supervisor.c itself started and therefore also wildcard-reaps),
 * that wildcard wait could win the race and silently consume pg_
 * receivewal's exit status before this file's own targeted waitpid() ever
 * saw it -- corrupting the FSM tick's own "is it still running" check and
 * logging a surprise "unexpected child" supervisor.c never asked to
 * track. Giving pg_receivewal its own dedicated controller process fixes
 * this structurally, not by convention: a targeted waitpid(pid, ...) in
 * *this* process can only ever observe pg_receivewal, this process's own
 * one and only child, so there is no second reaper left to race against.
 *
 * Control path: the FSM tick ("archiver capture", service_archiver.c)
 * doesn't fork pg_receivewal itself anymore -- it writes a small "desired
 * state" file (service_archiver_pgreceivewal_state.h) whenever the target
 * primary changes or pg_receivewal should start/stop, exactly the same
 * shape as service_postgres_ctl.c's own KeeperStatePostgres file (state.h)
 * for the ordinary Postgres case. This process polls that file and
 * reconciles reality to match, including restarting pg_receivewal on its
 * own if it dies -- the FSM tick no longer needs its own "is it running,
 * restart if not" check at all (supervisor.c's RP_PERMANENT restart
 * policy on *this* controller process, plus this loop's own
 * reconciliation, cover it together, the same two-layer guarantee
 * ordinary Postgres already gets).
 *
 * Compilation-unit boundary, deliberate: the actual pg_receivewal binary
 * this file forks and runs is vendored in full (vendor/pg_receivewal/,
 * ~3300 lines plus three extra static libraries) -- kept out of service_
 * archiver_pgreceivewal_state.c/.h on purpose, so a caller that only
 * needs to read or write the desired-state file (service_archiver.c's
 * FSM tick, fsm_transition.c, and anything sharing their real logic for
 * tests -- pgaftest's own Makefile) never has to link any of it. Only
 * service_archiver_reconciler.c, the one caller that actually starts this
 * controller as a supervised service, includes this file.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "access/xlog_internal.h"

#include "service_archiver_pgreceivewal_ctl.h"

#include "archiver_systemid.h"
#include "archiver_wal_notify.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "pg_receivewal_entry.h"
#include "pgctl.h"
#include "pgsql.h"
#include "service_archiver_pgreceivewal_state.h"
#include "signals.h"
#include "string_utils.h"

/*
 * archiverWalNotifySocketPath is set once, in the parent, before fork()ing
 * pg_receivewal's own child -- read back by pgaf_hook_wal_segment_closed()
 * below, which runs *inside* that child (a real fork(), so this global's
 * value at fork time is all it ever needs; no cross-process sharing).
 */
static char archiverWalNotifySocketPath[MAXPGPATH] = { 0 };

/*
 * archiverSystemIdPath, cached the same way and for the same reason as
 * archiverWalNotifySocketPath above: computed once in the parent, before
 * fork()ing pg_receivewal's own child, so pgaf_hook_wal_segment_closed()
 * (running inside that child) can read the current system identifier
 * without needing a KeeperConfig* of its own.
 */
static char archiverSystemIdPath[MAXPGPATH] = { 0 };


/*
 * pgaf_hook_wal_segment_closed is pg_receivewal's own WalSegmentClosedHook
 * (vendor/pg_receivewal/pg_receivewal_entry.h) -- formats xlogpos as the
 * same "%X/%X" text form every other LSN in this project already uses
 * (pg_lsn's own input syntax) and hands it to archiver_wal_notify_send()
 * along with this membership's own system identifier (archiver_systemid_
 * read_from_path() -- best-effort: a segment closing before the system
 * identifier is known yet is simply not reported, same as every other gap
 * this best-effort socket already tolerates, see archiver_wal_notify.c's
 * own header comment).
 */
static void
pgaf_hook_wal_segment_closed(XLogRecPtr xlogpos, uint32 timeline)
{
	uint64_t systemIdentifier = 0;

	if (!archiver_systemid_read_from_path(archiverSystemIdPath,
										  &systemIdentifier))
	{
		return;
	}

	char lsn[PG_LSN_MAXLENGTH] = { 0 };

	sformat(lsn, sizeof(lsn), "%X/%X",
			(uint32) (xlogpos >> 32), (uint32) xlogpos);

	/*
	 * The completed segment's own filename -- real Postgres's own
	 * XLogFileName() (access/xlog_internal.h), the same helper pg_
	 * receivewal.c itself uses internally, rather than hand-rolled segno
	 * arithmetic duplicating it. 16MB is this project's own fixed WAL
	 * segment size assumption, matching ARCHIVER_WAL_SEGMENT_SIZE
	 * (service_archiver.c) and pg_receivewal's own lack of a
	 * --wal-segsize flag here (only relevant for initdb-time sizing,
	 * never varied per this project's own captures).
	 */
	char walFileName[MAXPGPATH] = { 0 };
	XLogSegNo segno;

	XLByteToSeg(xlogpos, segno, (1024 * 1024 * 16));
	XLogFileName(walFileName, timeline, segno, (1024 * 1024 * 16));

	(void) archiver_wal_notify_send_segment(archiverWalNotifySocketPath,
											walFileName, lsn, systemIdentifier);
}


/*
 * How often pgaf_hook_wal_progress() actually sends a PROGRESS message, in
 * seconds -- stop_streaming() (pg_receivewal.c) can invoke this hook far
 * more often than that under a busy primary (once per received message
 * chunk, not just once per --status-interval), so this throttle is what
 * keeps PROGRESS traffic a low-volume, best-effort observability signal
 * rather than flooding the socket at line rate.
 */
#define ARCHIVER_WAL_PROGRESS_MIN_INTERVAL_SECONDS 5


/*
 * pgaf_hook_wal_progress is pg_receivewal's own WalProgressHook (vendor/
 * pg_receivewal/pg_receivewal_entry.h) -- same LSN formatting and system-
 * identifier lookup as pgaf_hook_wal_segment_closed() above, throttled to
 * ARCHIVER_WAL_PROGRESS_MIN_INTERVAL_SECONDS and, unlike that hook, with no
 * segment filename at all (see archiver_wal_notify_send_progress()'s own
 * header comment for why).
 */
static void
pgaf_hook_wal_progress(XLogRecPtr xlogpos, uint32 timeline)
{
	static time_t lastSentAt = 0;
	time_t now = time(NULL);

	if (lastSentAt != 0 &&
		(now - lastSentAt) < ARCHIVER_WAL_PROGRESS_MIN_INTERVAL_SECONDS)
	{
		return;
	}

	uint64_t systemIdentifier = 0;

	if (!archiver_systemid_read_from_path(archiverSystemIdPath,
										  &systemIdentifier))
	{
		return;
	}

	char lsn[PG_LSN_MAXLENGTH] = { 0 };

	sformat(lsn, sizeof(lsn), "%X/%X",
			(uint32) (xlogpos >> 32), (uint32) xlogpos);

	if (archiver_wal_notify_send_progress(archiverWalNotifySocketPath,
										  lsn, systemIdentifier))
	{
		lastSentAt = now;
	}
}


static bool ensure_pgreceivewal_matches(KeeperConfig *config,
										ArchiverPgReceivewalDesiredState *desired,
										ArchiverPgReceivewalDesiredState *running,
										pid_t *childPid);
static bool start_pgreceivewal_child(KeeperConfig *config,
									 ArchiverPgReceivewalDesiredState *desired,
									 pid_t *childPid);
static bool stop_pgreceivewal_child(pid_t *childPid);
static bool wait_for_primary_and_slot_ready(const char *primaryConnInfo,
											const char *slotName);

/*
 * How long wait_for_primary_and_slot_ready() polls for before giving up and
 * forking pg_receivewal anyway -- a bound, not a guarantee: it exists so a
 * primary that genuinely never creates the slot (a real misconfiguration,
 * not just a startup race) doesn't wedge this controller's own poll loop
 * forever. pg_receivewal's own retry behavior remains the backstop past
 * this point, same as before this function existed.
 */
#define ARCHIVER_PGRECEIVEWAL_SLOT_WAIT_SECONDS 20

/*
 * How often wait_for_primary_and_slot_ready() actually re-checks, in
 * milliseconds -- deliberately much finer than the 20s wall-clock bound
 * above: each check is one lightweight query (pgsql_replication_slot_
 * exists()), and the common case this function exists for (a plain
 * archiver restart, where the slot has already existed for a while) only
 * ever needs its first attempt to succeed. A coarse 1s-per-attempt
 * interval cost this preflight check up to a full extra second of pure
 * waiting even when readiness was already true, directly eating into
 * callers' own downstream timeouts (e.g. archiver_wal_capture.pgaf's own
 * restart-liveness test) for no benefit.
 */
#define ARCHIVER_PGRECEIVEWAL_SLOT_POLL_MS 250


/*
 * wait_for_primary_and_slot_ready polls the primary at primaryConnInfo,
 * connecting fresh each attempt, until it accepts a connection AND its
 * replication slot slotName exists -- or ARCHIVER_PGRECEIVEWAL_SLOT_WAIT_
 * SECONDS elapses, or a stop is requested. Best-effort: a timeout here
 * doesn't stop start_pgreceivewal_child() from starting pg_receivewal
 * anyway, it just means this preflight check didn't get to close the
 * startup race for it.
 *
 * The race this closes: the slot is created by the primary's own keeper,
 * asynchronously, once it discovers this archiver as another node to
 * maintain a slot for (keeper_create_and_drop_replication_slots(),
 * primary_standby.c) -- there is no synchronous handshake guaranteeing it
 * exists by the time this controller is ready to start streaming.
 * pg_receivewal itself is not a reliable way to wait this out: it treats
 * "replication slot does not exist" as a retryable disconnect, but that
 * internal retry has been observed to occasionally never recover once the
 * slot subsequently appears (a genuine bug in the vendored client's own
 * reconnect path, still being tracked down) -- polling for readiness
 * before ever starting pg_receivewal avoids relying on that retry path
 * for this specific, common, entirely expected race at all.
 */
static bool
wait_for_primary_and_slot_ready(const char *primaryConnInfo, const char *slotName)
{
	int maxAttempts =
		(ARCHIVER_PGRECEIVEWAL_SLOT_WAIT_SECONDS * 1000) /
		ARCHIVER_PGRECEIVEWAL_SLOT_POLL_MS;

	for (int attempt = 0; attempt < maxAttempts; attempt++)
	{
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			return false;
		}

		PGSQL pgsql = { 0 };

		if (pgsql_init(&pgsql, (char *) primaryConnInfo, PGSQL_CONN_UPSTREAM))
		{
			bool slotExists = false;

			if (pgsql_replication_slot_exists(&pgsql, slotName, &slotExists) &&
				slotExists)
			{
				pgsql_finish(&pgsql);
				return true;
			}

			pgsql_finish(&pgsql);
		}

		pg_usleep(ARCHIVER_PGRECEIVEWAL_SLOT_POLL_MS * 1000);
	}

	log_warn("Timed out after %ds waiting for the primary and replication "
			 "slot \"%s\" to be ready; starting pg_receivewal anyway",
			 ARCHIVER_PGRECEIVEWAL_SLOT_WAIT_SECONDS, slotName);

	return false;
}


/*
 * start_pgreceivewal_child forks and runs this project's own in-process
 * pg_receivewal (vendor/pg_receivewal/, see that directory's own header
 * comment) against desired's target, writing this controller's own
 * pidfile once the fork succeeds. A real fork(), not just a function
 * call: pg_receivewal_main() behaves like the CLI tool it's vendored
 * from (installs its own signal handlers, calls exit() on its own
 * completion/error paths, ...) -- exactly what a genuinely separate
 * process needs to be free to do without disturbing this controller's
 * own process-wide state, the same isolation execv()-ing the real binary
 * used to provide.
 */
static bool
start_pgreceivewal_child(KeeperConfig *config,
						 ArchiverPgReceivewalDesiredState *desired,
						 pid_t *childPid)
{
	if (!directory_exists(config->pgSetup.pgdata) &&
		mkdir(config->pgSetup.pgdata, 0700) != 0)
	{
		log_error("Failed to create archiver WAL directory \"%s\": %m",
				  config->pgSetup.pgdata);
		return false;
	}

	char primaryConnInfo[MAXCONNINFO] = { 0 };

	if (!prepare_primary_conninfo(primaryConnInfo,
								  sizeof(primaryConnInfo),
								  desired->host, desired->port,
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

	(void) wait_for_primary_and_slot_ready(primaryConnInfo, desired->slot);

	log_info("Starting pg_receivewal against %s:%d, writing to \"%s\", "
			 "using replication slot \"%s\"",
			 desired->host, desired->port, config->pgSetup.pgdata,
			 desired->slot);

	(void) archiver_wal_notify_socket_path(config, archiverWalNotifySocketPath,
										   sizeof(archiverWalNotifySocketPath));
	(void) archiver_systemid_path(config, archiverSystemIdPath,
								  sizeof(archiverSystemIdPath));

	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork pg_receivewal: %m");
		return false;
	}

	if (pid == 0)
	{
		char *args[10];
		int argsIndex = 0;

		args[argsIndex++] = "pg_receivewal";
		args[argsIndex++] = "-w";
		args[argsIndex++] = "-d";
		args[argsIndex++] = primaryConnInfo;
		args[argsIndex++] = "-D";
		args[argsIndex++] = config->pgSetup.pgdata;
		args[argsIndex++] = "--no-sync";
		args[argsIndex++] = "-S";
		args[argsIndex++] = desired->slot;
		args[argsIndex] = NULL;

		pgaf_wal_segment_closed_hook = &pgaf_hook_wal_segment_closed;
		pgaf_wal_progress_hook = &pgaf_hook_wal_progress;

		/*
		 * pg_receivewal_main() is called in-process rather than exec'd, so
		 * it inherits this process's getopt state. pg_autoctl's own CLI
		 * parsing (before we ever get here) already advanced glibc's
		 * global optind past 1; without resetting it, getopt_long() here
		 * starts scanning args[] from the wrong index and misparses "-d"
		 * as a stray positional argument. optind = 0 is glibc's documented
		 * way to force a full re-initialization (plain optind = 1 is not
		 * enough to reset all of getopt_long()'s internal state).
		 */
		optind = 0;

		int rc = pg_receivewal_main(argsIndex, args);

		_exit(rc);
	}

	*childPid = pid;

	char pidfilePath[MAXPGPATH] = { 0 };

	archiver_pgreceivewal_pidfile_path(config, pidfilePath);

	char pidStr[16] = { 0 };
	int pidStrLen = sformat(pidStr, sizeof(pidStr), "%d", pid);

	(void) write_file_atomic(pidStr, pidStrLen, pidfilePath);

	return true;
}


/*
 * stop_pgreceivewal_child stops the currently-tracked child, if any --
 * idempotent, and removes this controller's own pidfile so a concurrent
 * reader of service_archiver_pgreceivewal_ctl_is_running() never observes
 * a pid that is about to become invalid.
 */
static bool
stop_pgreceivewal_child(pid_t *childPid)
{
	if (*childPid <= 0)
	{
		return true;
	}

	log_info("Stopping pg_receivewal (pid %d)", *childPid);

	if (kill(*childPid, SIGTERM) != 0 && errno != ESRCH)
	{
		log_error("Failed to send SIGTERM to pg_receivewal (pid %d): %m",
				  *childPid);
		return false;
	}

	int status = 0;

	if (waitpid(*childPid, &status, 0) == -1 && errno != ECHILD)
	{
		log_error("Failed to wait for pg_receivewal (pid %d) to stop: %m",
				  *childPid);
	}

	*childPid = -1;

	return true;
}


/*
 * ensure_pgreceivewal_matches reconciles the currently-running child (if
 * any) against the freshly-read desired state: stops a stale/unwanted
 * child, starts a missing/changed-target one, leaves an already-matching
 * one alone. running tracks what this controller believes it last
 * started, kept across calls (this function's own out-parameter doubles
 * as its next call's own "previous" state).
 */
static bool
ensure_pgreceivewal_matches(KeeperConfig *config,
							ArchiverPgReceivewalDesiredState *desired,
							ArchiverPgReceivewalDesiredState *running,
							pid_t *childPid)
{
	bool childAlive = false;

	if (*childPid > 0)
	{
		int status = 0;
		pid_t ret = waitpid(*childPid, &status, WNOHANG);

		if (ret == 0)
		{
			childAlive = true;
		}
		else
		{
			if (ret == *childPid)
			{
				log_warn("pg_receivewal (pid %d) exited with status %d, "
						 "restarting", *childPid, status);
			}
			*childPid = -1;
		}
	}

	bool targetChanged =
		!streq(running->host, desired->host) ||
		running->port != desired->port ||
		!streq(running->slot, desired->slot);

	if (childAlive && (!desired->running || targetChanged))
	{
		if (!stop_pgreceivewal_child(childPid))
		{
			return false;
		}
		childAlive = false;
	}

	if (desired->running && !childAlive)
	{
		if (!start_pgreceivewal_child(config, desired, childPid))
		{
			return false;
		}
	}
	else if (!desired->running)
	{
		char pidfilePath[MAXPGPATH] = { 0 };

		archiver_pgreceivewal_pidfile_path(config, pidfilePath);
		(void) unlink_file(pidfilePath);
	}

	*running = *desired;

	return true;
}


/*
 * service_archiver_pgreceivewal_ctl_loop is this controller's own body:
 * poll the desired-state file, reconcile, sleep, repeat -- until asked to
 * stop, at which point the tracked child (if any) is stopped cleanly
 * before exiting, matching service_postgres_ctl_loop()'s own shutdown
 * handling.
 */
void
service_archiver_pgreceivewal_ctl_loop(KeeperConfig *config)
{
	ArchiverPgReceivewalDesiredState running = { 0 };
	pid_t childPid = -1;
	bool loggedFirstRead = false;

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			(void) stop_pgreceivewal_child(&childPid);
			exit(EXIT_CODE_QUIT);
		}

		ArchiverPgReceivewalDesiredState desired = { 0 };

		bool readOk = archiver_pgreceivewal_read_desired_state(config, &desired);

		if (!loggedFirstRead)
		{
			loggedFirstRead = true;
			log_info("pgreceivewal-ctl: first desired-state read: ok=%d "
					 "running=%d host=\"%s\" port=%d slot=\"%s\"",
					 readOk, desired.running, desired.host, desired.port,
					 desired.slot);
		}

		if (readOk)
		{
			if (!ensure_pgreceivewal_matches(config, &desired, &running,
											 &childPid))
			{
				log_warn("Failed to reconcile pg_receivewal state, will retry");
			}
		}

		pg_usleep(200 * 1000);  /* 200ms */
	}
}


/*
 * service_archiver_pgreceivewal_ctl_start forks this controller process --
 * matching service_archiver_capture_start()/service_archiver_reconciler_
 * start()'s own fork-without-exec shape (this project's own binary already
 * implements the loop, no need to re-exec into a fresh process image):
 * this is what service_archiver_reconciler.c supervises directly, as a
 * sibling of that same membership's own "archiver-capture-<formation>-
 * <group>" process, not its child.
 */
bool
service_archiver_pgreceivewal_ctl_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the archiver pg_receivewal controller "
					  "process");
			return false;
		}

		case 0:
		{
			(void) set_signal_handlers(false);
			(void) set_ps_title("pg_autoctl: archiver pgreceivewal controller");

			(void) service_archiver_pgreceivewal_ctl_loop(&(keeper->config));

			/* unreachable: the loop only ever exit()s directly */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			log_debug("pg_autoctl archiver pgreceivewal controller started "
					  "in subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}
