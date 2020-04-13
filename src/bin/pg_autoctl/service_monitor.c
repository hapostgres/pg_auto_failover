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

#include "cli_common.h"
#include "cli_root.h"
#include "defaults.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "service.h"
#include "service_monitor.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

/*
 * monitor_service_start starts the monitor processes: the Postgres instance
 * and the user-facing LISTEN client that displays notifications.
 */
bool
start_monitor(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	LocalPostgresServer postgres = { 0 };

	Service subprocesses[] = {
		{
			"postgres ctl",
			0,
			&service_postgres_ctl_start,
			&service_postgres_ctl_stop,
			(void *) &postgres
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

	/* initialize our local Postgres instance representation */
	(void) local_postgres_init(&postgres, pgSetup);

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
			(void) service_monitor_runprogram(monitor);

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
 * service_monitor_runprogram runs the node_active protocol service:
 *
 *   $ pg_autoctl do service monitor --pgdata ...
 */
void
service_monitor_runprogram(Monitor *monitor)
{
	Program program;

	char *args[12];
	int argsIndex = 0;

	char command[BUFSIZE];

	/*
	 * use --pgdata option rather than the config.
	 *
	 * On macOS when using /tmp, the file path is then redirected to being
	 * /private/tmp when using realpath(2) as we do in normalize_filename(). So
	 * for that case to be supported, we explicitely re-use whatever PGDATA or
	 * --pgdata was parsed from the main command line to start our sub-process.
	 *
	 * The pg_autoctl monitor listener can get started from one of the
	 * following top-level commands:
	 *
	 *  - pg_autoctl create monitor --run
	 *  - pg_autoctl run
	 *
	 * The monitor specific commands set monitorOptions, the generic command
	 * set keeperOptions.
	 */
	char *pgdata =
		IS_EMPTY_STRING_BUFFER(monitorOptions.pgSetup.pgdata)
		? keeperOptions.pgSetup.pgdata
		: monitorOptions.pgSetup.pgdata;

	setenv(PG_AUTOCTL_DEBUG, "1", 1);

	args[argsIndex++] = (char *) pg_autoctl_program;
	args[argsIndex++] = "do";
	args[argsIndex++] = "service";
	args[argsIndex++] = "monitor";
	args[argsIndex++] = "--pgdata";
	args[argsIndex++] = pgdata;
	args[argsIndex++] = logLevelToString(log_get_level());
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	program = initialize_program(args, false);

	program.capture = false;    /* redirect output, don't capture */
	program.stdOutFd = STDOUT_FILENO;
	program.stdErrFd = STDERR_FILENO;

	/* log the exact command line we're using */
	(void) snprintf_program_command_line(&program, command, BUFSIZE);

	log_info("%s", command);

	(void) execute_program(&program);
}


/*
 * monitor_service_run watches over monitor process, restarts if it is
 * necessary, also loops over a LISTEN command that is notified at every change
 * of state on the monitor, and prints the change on stdout.
 */
bool
monitor_service_run(Monitor *monitor)
{
	MonitorConfig *mconfig = &(monitor->config);
	MonitorExtensionVersion version = { 0 };
	char *channels[] = { "log", "state", NULL };
	char postgresUri[MAXCONNINFO];

	LocalPostgresServer postgres = { 0 };

	/* Initialize our local connection to the monitor */
	if (!monitor_local_init(monitor))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	/* Now get the the Monitor URI to display it to the user, and move along */
	if (monitor_config_get_postgres_uri(mconfig, postgresUri, MAXCONNINFO))
	{
		log_info("pg_auto_failover monitor is ready at %s", postgresUri);
	}

	(void) local_postgres_init(&postgres, &(monitor->config.pgSetup));

	if (!ensure_local_postgres_is_running(&postgres))
	{
		log_error("Failed to ensure Postgres is running, "
				  "see above for details.");
		return false;
	}

	/* Check version compatibility. */
	if (!monitor_ensure_extension_version(monitor, &version))
	{
		/* errors have already been logged */
		return false;
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

			/* We got disconnected, ensure that Postgres is running again */
			if (!ensure_local_postgres_is_running(&postgres))
			{
				log_error("Failed to ensure Postgres is running, "
						  "see above for details.");
				return false;
			}

			/* Check version compatibility. */
			if (!monitor_ensure_extension_version(monitor, &version))
			{
				/* errors have already been logged */
				return false;
			}

			/* Get back to our infinite LISTEN loop */
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
