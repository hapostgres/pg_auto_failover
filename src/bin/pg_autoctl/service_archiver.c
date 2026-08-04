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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "service_archiver.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "signals.h"

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
