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
#include "service_archiver_serve.h"
#include "signals.h"
#include "supervisor.h"


/*
 * service_archiver_capture_start forks a child that runs
 * service_archiver_loop() (service_archiver.c) -- the outbound WAL-capture
 * half, supervising pg_receivewal against the group's primary. No exec():
 * this project's own binary already implements the loop, matching
 * service_keeper_start()'s sibling shape for an ordinary node minus the
 * execv() re-exec (that one replaces the process image to get a fresh
 * "node-active"-titled process; forking straight into the loop function is
 * simpler and just as correct here).
 */
static bool
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
 * service_archiver_serve_start_service forks a child that runs
 * service_archiver_serve_loop() (service_archiver_serve.c) -- the inbound
 * serving half, exec'ing and supervising pg_walsender. Named with a
 * "_service" suffix to avoid colliding with service_archiver_serve.c's own
 * service_archiver_serve_start_walsender(), a different function one level
 * down (that one starts pg_walsender itself; this one starts the loop that
 * in turn starts and monitors pg_walsender).
 */
static bool
service_archiver_serve_start_service(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the archiver serve process");
			return false;
		}

		case 0:
		{
			(void) set_signal_handlers(false);
			(void) set_ps_title("pg_autoctl: archiver serve");

			/* see service_archiver_capture_start()'s own comment on why
			 * each supervised child re-connects independently */
			if (!monitor_init(&(keeper->monitor), keeper->config.monitor_pguri))
			{
				log_fatal("Failed to contact the monitor, see above for details");
				exit(EXIT_CODE_MONITOR);
			}

			if (!service_archiver_serve_loop(keeper))
			{
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			exit(EXIT_CODE_QUIT);
		}

		default:
		{
			log_debug("pg_autoctl archiver serve process started in "
					  "subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}


bool
start_archiver(Keeper *keeper)
{
	const char *pidfile = keeper->config.pathnames.pid;

	Service subprocesses[] = {
		{
			SERVICE_NAME_ARCHIVER_CAPTURE,
			RP_PERMANENT,
			-1,
			&service_archiver_capture_start,
			(void *) keeper
		},
		{
			SERVICE_NAME_ARCHIVER_SERVE,
			RP_PERMANENT,
			-1,
			&service_archiver_serve_start_service,
			(void *) keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	return supervisor_start(subprocesses, subprocessesCount, pidfile);
}
