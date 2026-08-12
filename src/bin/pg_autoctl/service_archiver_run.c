/*
 * src/bin/pg_autoctl/service_archiver_run.c
 *   See service_archiver_run.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <unistd.h>

#include "service_archiver_run.h"

#include "cli_root.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "service_archiver.h"
#include "service_archiver_reconciler.h"
#include "service_archiver_serve.h"
#include "signals.h"
#include "supervisor.h"


/*
 * service_archiver_capture_start forks a child that runs
 * service_archiver_loop() (service_archiver.c) -- the outbound WAL-capture
 * half, supervising pg_receivewal against one (formation, group)
 * membership's own primary. No exec(): this project's own binary already
 * implements the loop, matching service_keeper_start()'s sibling shape for
 * an ordinary node minus the execv() re-exec (that one replaces the
 * process image to get a fresh "node-active"-titled process; forking
 * straight into the loop function is simpler and just as correct here).
 *
 * Exported (not static): start_archiver() below no longer calls this
 * directly -- it's service_archiver_reconciler.c that does, once per
 * membership this archiver holds, since an archiver can hold more than
 * one at once. See that file's own header comment for the full design.
 */
bool
service_archiver_capture_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the archiver capture process");
			return false;
		}

		case 0:
		{
			(void) set_signal_handlers(false);
			(void) set_ps_title("pg_autoctl: archiver capture");

			/*
			 * Re-connect: the parent's own keeper->monitor connection is
			 * not fork-safe to share, and may already have been closed by
			 * the caller (cli_service.c's cli_keeper_run finishes its own
			 * connection before starting services) -- each supervised
			 * child establishes its own, exactly like a freshly exec'd
			 * process would.
			 */
			if (!monitor_init(&(keeper->monitor), keeper->config.monitor_pguri))
			{
				log_fatal("Failed to contact the monitor, see above for details");
				exit(EXIT_CODE_MONITOR);
			}

			if (!service_archiver_loop(keeper))
			{
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			exit(EXIT_CODE_QUIT);
		}

		default:
		{
			log_debug("pg_autoctl archiver capture process started in "
					  "subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}


/*
 * start_archiver supervises exactly two top-level children: "archiver-
 * serve" (pg_walsender itself, exec'd directly by service_archiver_
 * walsender_start() -- service_archiver_serve.c) and "archiver-reconciler"
 * (service_archiver_reconciler.c), which in turn keeps one WAL-capture
 * child per membership running, added and removed as this archiver's own
 * attachments change. Both use the plain, unmodified supervisor_start() --
 * a fixed two-element array like every other node kind's own top-level
 * supervisor -- so a bug in the reconciler's own, genuinely new dynamic-
 * membership logic can only crash and restart the reconciler itself;
 * "archiver-serve" is never affected. No intermediate wrapper process or
 * hand-rolled liveness loop sits between this registration and pg_
 * walsender itself anymore -- supervisor.c's own generic tick loop notices
 * pg_walsender's death and restarts it (RP_PERMANENT) the same way it
 * already does for every other permanent service in this project.
 */
bool
start_archiver(Keeper *keeper)
{
	const char *pidfile = keeper->config.pathnames.pid;

	Service subprocesses[] = {
		{
			SERVICE_NAME_ARCHIVER_SERVE,
			RP_PERMANENT,
			-1,
			&service_archiver_walsender_start,
			(void *) keeper
		},
		{
			SERVICE_NAME_ARCHIVER_RECONCILER,
			RP_PERMANENT,
			-1,
			&service_archiver_reconciler_start,
			(void *) keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	return supervisor_start(subprocesses, subprocessesCount, pidfile);
}
