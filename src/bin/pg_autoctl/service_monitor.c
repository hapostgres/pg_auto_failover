/*
 * src/bin/pg_autoctl/monitor_service.c
 *   Utilities to start/stop the pg_autoctl service on a monitor node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <limits.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "service.h"
#include "service_monitor.h"
#include "service_postgres.h"
#include "signals.h"
#include "string_utils.h"


/*
 * monitor_service_start starts the monitor processes: the Postgres instance
 * and the user-facing LISTEN client that displays notifications.
 */
bool
start_monitor(Monitor *monitor)
{
	MonitorConfig *config = &monitor->config;
	PostgresSetup *pgSetup = &config->pgSetup;

	Service subprocesses[] = {
		{
			"postgres",
			0,
			&service_postgres_start,
			&service_postgres_stop,
			(void *) pgSetup
		},
		{
			"listener",
			0,
			&service_monitor_start,
			&service_monitor_stop,
			(void *) monitor
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	if (!service_start(subprocesses, subprocessesCount, config->pathnames.pid))
	{
		/* errors have already been logged */
		return false;
	}

	/* we only get there when the supervisor exited successfully (SIGTERM) */
	return true;
}


/*
 * service_monitor_start starts a sub-process that listens to the monitor
 * notifications and outputs them for the user.
 */
bool
service_monitor_start(void *context, pid_t *pid)
{
	Monitor *monitor = (Monitor *) context;
	pid_t fpid;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the node_active process");
			return false;
		}

		case 0:
		{
			/* fork succeeded, in child */
			(void) set_ps_title("listener");
			(void) monitor_service_run(monitor);

			/*
			 * When the "main" function for the child process is over, it's the
			 * end of our execution thread. Don't get back to the caller.
			 */
			if (asked_to_stop || asked_to_stop_fast)
			{
				exit(EXIT_CODE_QUIT);
			}
			else
			{
				/* something went wrong (e.g. broken pipe) */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl listen process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}


/*
 * monitor_service_run watches over monitor process, restarts if it is
 * necessary, also loops over a LISTEN command that is notified at every change
 * of state on the monitor, and prints the change on stdout.
 */
bool
monitor_service_run(Monitor *monitor)
{
	MonitorConfig *mconfig = &monitor->config;
	char *channels[] = { "log", "state", NULL };
	char postgresUri[MAXCONNINFO];

	/* Now get the the Monitor URI to display it to the user, and move along */
	if (monitor_config_get_postgres_uri(mconfig, postgresUri, MAXCONNINFO))
	{
		log_info("pg_auto_failover monitor is ready at %s", postgresUri);
	}

	log_info("Contacting the monitor to LISTEN to its events.");
	pgsql_listen(&(monitor->pgsql), channels);

	/*
	 * Main loop for notifications.
	 */
	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		if (!monitor_get_notifications(monitor))
		{
			log_warn("Re-establishing connection. We might miss notifications.");
			pgsql_finish(&(monitor->pgsql));

			pgsql_listen(&(monitor->pgsql), channels);

			/* skip sleeping */
			continue;
		}

		sleep(PG_AUTOCTL_MONITOR_SLEEP_TIME);
	}

	pgsql_finish(&(monitor->pgsql));

	return true;
}


/*
 * service_monitor_stop stops the pg_autoctl monitor listener service.
 */
bool
service_monitor_stop(void *context)
{
	Service *service = (Service *) context;

	if (kill(service->pid, SIGQUIT) != 0)
	{
		log_error("Failed to send SIGQUIT to pid %d for service %s",
				  service->pid, service->name);
		return false;
	}
	return true;
}
