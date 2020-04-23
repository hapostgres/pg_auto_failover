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
#include "monitor_pg_init.h"
#include "monitor_service.h"
#include "service.h"
#include "signals.h"
#include "string_utils.h"


static void reload_configuration(Monitor *monitor);
static bool monitor_ensure_configuration(Monitor *monitor);


/*
 * ensure_monitor_pg_running checks if monitor is running, attempts to restart
 * if it is not. The function verifies if the extension version is the same as
 * expected. It returns true if the monitor is up and running.
 */
bool
ensure_monitor_pg_running(Monitor *monitor)
{
	MonitorConfig *mconfig = &(monitor->config);
	MonitorExtensionVersion version = { 0 };

	if (!pg_is_running(mconfig->pgSetup.pg_ctl, mconfig->pgSetup.pgdata))
	{
		log_info("Postgres is not running, starting postgres");
		pgsql_finish(&(monitor->pgsql));

		if (!pg_ctl_start(mconfig->pgSetup.pg_ctl,
						  mconfig->pgSetup.pgdata,
						  mconfig->pgSetup.pgport,
						  mconfig->pgSetup.listen_addresses))
		{
			log_error("Failed to start PostgreSQL, see above for details");
			return false;
		}

		/*
		 * Check version compatibility.
		 *
		 * The function terminates any existing connection during clenaup
		 * therefore it is not called when PG is found to be running to not
		 * to intervene pgsql_listen call.
		 */
		if (!monitor_ensure_extension_version(monitor, &version))
		{
			/* errors have already been logged */
			return false;
		}
	}

	return true;
}


/*
 * monitor_service_run watches over monitor process, restarts if it is necessary,
 * also loops over a LISTEN command that is notified at every
 * change of state on the monitor, and prints the change on stdout.
 */
bool
monitor_service_run(Monitor *monitor, pid_t start_pid)
{
	MonitorConfig *mconfig = &(monitor->config);
	char *channels[] = { "log", "state", NULL };
	char postgresUri[MAXCONNINFO];

	if (!monitor_ensure_configuration(monitor))
	{
		log_fatal("Failed to apply the current monitor configuration, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	/* We exit the loop if we can't get monitor to be running during the start */
	if (!ensure_monitor_pg_running(monitor))
	{
		/* errors were already logged */
		log_warn("Failed to ensure PostgreSQL is running, exiting the service");
		return false;
	}

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
		if (asked_to_reload)
		{
			(void) reload_configuration(monitor);
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(mconfig->pathnames.pid, start_pid);

		if (!ensure_monitor_pg_running(monitor))
		{
			log_warn("Failed to ensure PostgreSQL is running, "
					 "retrying in %d seconds",
					 PG_AUTOCTL_MONITOR_SLEEP_TIME);
			sleep(PG_AUTOCTL_MONITOR_SLEEP_TIME);
			continue;
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
	char configFilePath[MAXPGPATH] = { 0 };

	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	if (!monitor_add_postgres_default_settings(monitor))
	{
		log_error("Failed to initialize our Postgres settings, "
				  "see above for details");
		return false;
	}

	return true;
}
