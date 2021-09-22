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
#include "pidfile.h"
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
			SERVICE_NAME_POSTGRES,
			RP_PERMANENT,
			-1,
			&service_postgres_ctl_start
		},
		{
			SERVICE_NAME_MONITOR,
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

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the monitor listener process");
			return false;
		}

		case 0:
		{
			/* here we call execv() so we never get back */
			(void) service_monitor_runprogram(monitor);

			/* unexpected */
			log_fatal("BUG: returned from service_monitor_runprogram()");
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
 * execve() and will never return.
 */
void
service_monitor_runprogram(Monitor *monitor)
{
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
	args[argsIndex++] = "listener";
	args[argsIndex++] = "--pgdata";
	args[argsIndex++] = pgdata;
	args[argsIndex++] = logLevelToString(log_get_level());
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	Program program = { 0 };
	(void) initialize_program(&program, args, false);

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
	char postgresUri[MAXCONNINFO] = { 0 };

	bool loggedAboutListening = false;
	bool firstLoop = true;
	LocalPostgresServer postgres = { 0 };

	/* Initialize our local connection to the monitor */
	if (!monitor_local_init(monitor))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	/* Now get the Monitor URI to display it to the user, and move along */
	if (monitor_config_get_postgres_uri(mconfig, postgresUri, MAXCONNINFO))
	{
		log_info("Managing the monitor at %s", postgresUri);
	}

	(void) local_postgres_init(&postgres, &(monitor->config.pgSetup));

	/*
	 * Main loop for notifications.
	 */
	for (;; firstLoop = false)
	{
		bool pgIsNotRunningIsOk = true;
		PostgresSetup *pgSetup = &(postgres.postgresSetup);

		if (asked_to_reload || firstLoop)
		{
			(void) reload_configuration(monitor);
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			log_info("Listener service received signal %s, terminating",
					 signal_to_string(get_current_signal(SIGTERM)));
			break;
		}

		/*
		 * On the first loop we don't expect Postgres to be running, and on
		 * following loops it should be all fine. That said, at any point in
		 * time, if Postgres is not running now is a good time to make sure
		 * it's running.
		 *
		 * Also, whenever Postgres has been restarted, we should check about
		 * the version in the shared object library and maybe upgrade the
		 * extension SQL definitions to match.
		 */
		if (firstLoop || !pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk))
		{
			MonitorExtensionVersion version = { 0 };

			if (!ensure_postgres_service_is_running_as_subprocess(&postgres))
			{
				log_error("Failed to ensure Postgres is running "
						  "as a pg_autoctl subprocess, "
						  "see above for details.");
				return false;
			}

			/* Check version compatibility. */
			if (!monitor_ensure_extension_version(monitor, &postgres, &version))
			{
				/* maybe we failed to connect to the monitor */
				if (monitor->pgsql.status != PG_CONNECTION_OK)
				{
					/* leave some time to the monitor before we try again */
					sleep(PG_AUTOCTL_MONITOR_RETRY_TIME);
					continue;
				}

				/* or maybe we failed to update the extension altogether */
				return false;
			}
		}

		if (!loggedAboutListening)
		{
			log_info("Contacting the monitor to LISTEN to its events.");
			loggedAboutListening = true;
		}

		if (!monitor_get_notifications(monitor,

		                               /* we want the time in milliseconds */
									   PG_AUTOCTL_MONITOR_SLEEP_TIME * 1000))
		{
			log_warn("Re-establishing connection. We might miss notifications.");
			pgsql_finish(&(monitor->pgsql));
			pgsql_finish(&(monitor->notificationClient));

			continue;
		}
	}

	pgsql_finish(&(monitor->pgsql));
	pgsql_finish(&(monitor->notificationClient));

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
		log_warn("Configuration file \"%s\" does not exist, "
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
	bool pgIsNotRunningIsOk = true;

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
		log_fatal("Failed to initialize a monitor node, see above for details");
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

	if (pg_setup_is_ready(&(postgres.postgresSetup), pgIsNotRunningIsOk))
	{
		if (!pgsql_reload_conf(&(postgres.sqlClient)))
		{
			log_warn("Failed to reload Postgres configuration after "
					 "reloading pg_autoctl configuration, "
					 "see above for details");
			return false;
		}

		pgsql_finish(&(postgres.sqlClient));
	}

	return true;
}
