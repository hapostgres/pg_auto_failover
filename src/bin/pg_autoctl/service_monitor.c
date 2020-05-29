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
#include "monitor_pg_init.h"
#include "service.h"
#include "service_monitor.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "string_utils.h"
#include "supervisor.h"

#include "runprogram.h"


static void reload_configuration(Monitor *monitor);
static bool monitor_ensure_configuration(Monitor *monitor);


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
			"postgres",
			RP_PERMANENT,
			-1,
			&service_postgres_ctl_start
		},
		{
			"listener",
			RP_PERMANENT,
			-1,
			&service_monitor_start,
			(void *) monitor
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	/* initialize our local Postgres instance representation */
	(void) local_postgres_init(&postgres, pgSetup);

	return supervisor_start(subprocesses,
							subprocessesCount,
							config->pathnames.pid);
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
			/* here we call execv() so we never get back */
			(void) service_monitor_runprogram(monitor);

			/* unexpected */
			log_fatal("BUG: returned from service_keeper_runprogram()");
			exit(EXIT_CODE_INTERNAL_ERROR);
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
 *
 * This function is intended to be called from the child process after a fork()
 * has been successfully done at the parent process level: it's calling
 * execve() and willl never return.
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
	IntString semIdString = intToString(log_semaphore.semId);

	setenv(PG_AUTOCTL_DEBUG, "1", 1);
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

	args[argsIndex++] = (char *) pg_autoctl_program;
	args[argsIndex++] = "do";
	args[argsIndex++] = "service";
	args[argsIndex++] = "listener";
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

	if (!ensure_postgres_service_is_running(&postgres))
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

	if (!monitor_ensure_configuration(monitor))
	{
		log_fatal("Failed to apply the current monitor configuration, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Contacting the monitor to LISTEN to its events.");
	pgsql_listen(&(monitor->pgsql), channels);

	/*
	 * Main loop for notifications.
	 */
	for (;;)
	{
		if (asked_to_reload)
		{
			(void) reload_configuration(monitor);
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		if (!monitor_get_notifications(monitor))
		{
			log_warn("Re-establishing connection. We might miss notifications.");
			pgsql_finish(&(monitor->pgsql));

			/* We got disconnected, ensure that Postgres is running again */
			if (!ensure_postgres_service_is_running(&postgres))
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
 * reload_configuration reads the supposedly new configuration file and
 * integrates accepted new values into the current setup.
 */
static void
reload_configuration(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);

	if (file_exists(config->pathnames.config))
	{
		MonitorConfig newConfig = { 0 };
		bool missingPgdataIsOk = true;
		bool pgIsNotRunningIsOk = true;

		/*
		 * Set the same configuration and state file as the current config.
		 */
		strlcpy(newConfig.pathnames.config, config->pathnames.config, MAXPGPATH);

		if (monitor_config_read_file(&newConfig,
									 missingPgdataIsOk,
									 pgIsNotRunningIsOk) &&
			monitor_config_accept_new(config, &newConfig))
		{
			log_info("Reloaded the new configuration from \"%s\"",
					 config->pathnames.config);

			/*
			 * The new configuration might impact the Postgres setup, such as
			 * when changing the SSL file paths.
			 */
			if (!monitor_ensure_configuration(monitor))
			{
				log_warn("Failed to reload pg_autoctl configuration, "
						 "see above for details");
			}
		}
		else
		{
			log_warn("Failed to read configuration file \"%s\", "
					 "continuing with the same configuration.",
					 config->pathnames.config);
		}
	}
	else
	{
		log_warn("Configuration file \"%s\" does not exists, "
				 "continuing with the same configuration.",
				 config->pathnames.config);
	}

	/* we're done reloading now. */
	asked_to_reload = 0;
}


/*
 * monitor_ensure_configuration updates the Postgres settings to match the
 * pg_autoctl configuration file, if necessary.
 */
static bool
monitor_ensure_configuration(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetupReload = &(postgres.postgresSetup);
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = false;

	if (!monitor_add_postgres_default_settings(monitor))
	{
		log_error("Failed to initialize our Postgres settings, "
				  "see above for details");
		return false;
	}

	if (!pg_setup_init(pgSetupReload,
					   pgSetup,
					   missingPgdataIsOk,
					   pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialise a monitor node, see above for details");
		return false;
	}

	/*
	 * To reload Postgres config, we need to connect as the local system user,
	 * otherwise using the autoctl_node user does not provide us with enough
	 * privileges.
	 */
	strlcpy(pgSetupReload->username, "", NAMEDATALEN);
	strlcpy(pgSetupReload->dbname, "template1", NAMEDATALEN);
	local_postgres_init(&postgres, pgSetupReload);

	if (!pgsql_reload_conf(&(postgres.sqlClient)))
	{
		log_warn("Failed to reload Postgres configuration after "
				 "reloading pg_autoctl configuration, "
				 "see above for details");
		return false;
	}

	pgsql_finish(&(postgres.sqlClient));

	return true;
}
