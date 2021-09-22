/*
 * src/bin/pg_autoctl/service_monitor_init.c
 *     Monitor initialisation service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cli_common.h"
#include "cli_root.h"
#include "defaults.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "monitor_pg_init.h"
#include "pgctl.h"
#include "pidfile.h"
#include "state.h"
#include "service_monitor.h"
#include "service_monitor_init.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "string_utils.h"
#include "supervisor.h"


static bool service_monitor_init_start(void *context, pid_t *pid);


/*
 * monitor_pg_finish_init starts the Postgres instance that we need running to
 * finish our installation, and finished the installation of the pgautofailover
 * monitor extension in the Postgres instance.
 */
bool
service_monitor_init(Monitor *monitor)
{
	MonitorConfig *config = &monitor->config;
	PostgresSetup *pgSetup = &config->pgSetup;
	LocalPostgresServer postgres = { 0 };

	Service subprocesses[] = {
		{
			SERVICE_NAME_POSTGRES,
			RP_PERMANENT,
			-1,
			&service_postgres_ctl_start
		},
		{
			SERVICE_NAME_MONITOR_INIT,
			createAndRun ? RP_PERMANENT : RP_TRANSIENT,
			-1,
			&service_monitor_init_start,
			(void *) monitor
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	/* when using pg_autoctl create monitor --run, use "listener" */
	if (createAndRun)
	{
		strlcpy(subprocesses[1].name, SERVICE_NAME_MONITOR, NAMEDATALEN);
	}

	/* We didn't create our target username/dbname yet */
	strlcpy(pgSetup->username, "", NAMEDATALEN);
	strlcpy(pgSetup->dbname, "", NAMEDATALEN);

	/* initialize our local Postgres instance representation */
	(void) local_postgres_init(&postgres, pgSetup);

	if (!supervisor_start(subprocesses,
						  subprocessesCount,
						  config->pathnames.pid))
	{
		/* errors have already been logged */
		return false;
	}

	/* we only get there when the supervisor exited successfully (SIGTERM) */
	return true;
}


/*
 * service_monitor_init_start is a subprocess that finishes the installation of
 * the monitor extension for pgautofailover.
 */
static bool
service_monitor_init_start(void *context, pid_t *pid)
{
	Monitor *monitor = (Monitor *) context;
	MonitorConfig *config = &monitor->config;
	PostgresSetup *pgSetup = &config->pgSetup;


	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the monitor install process");
			return false;
		}

		case 0:
		{
			/*
			 * We are in a sub-process and didn't call exec() on our pg_autoctl
			 * do service listener program yet we do not want to clean-up the
			 * semaphore just yet. Publish that we are a sub-process and only
			 * then quit, avoiding to call the atexit() semaphore clean-up
			 * function.
			 */

			const char *serviceName = createAndRun ?
									  "pg_autoctl: monitor listener" :
									  "pg_autoctl: monitor installer";

			(void) set_ps_title(serviceName);

			/* finish the install if necessary */
			if (!monitor_install(config->hostname, *pgSetup, false))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			log_info("Monitor has been successfully initialized.");

			if (createAndRun)
			{
				/* here we call execv() so we never get back */
				(void) service_monitor_runprogram(monitor);

				/* unexpected */
				log_fatal("BUG: returned from service_monitor_runprogram()");
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			else
			{
				exit(EXIT_CODE_QUIT);
			}
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl installer process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}
