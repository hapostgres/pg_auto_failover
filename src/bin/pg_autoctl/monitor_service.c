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
#include "monitor_service.h"
#include "service.h"
#include "signals.h"
#include "string_utils.h"


/*
 * ensure_monitor_pg_running checks if monitor is running, attempts to restart
 * if it is not. The function verifies if the extension version is the same as
 * expected. It returns true if the monitor is up and running.
 */
bool
ensure_monitor_pg_running(Monitor *monitor, MonitorConfig *mconfig)
{
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
monitor_service_run(Monitor *monitor, MonitorConfig *mconfig, pid_t start_pid)
{
	char *channels[] = { "log", "state", NULL };
	char postgresUri[MAXCONNINFO];

	/* We exit the loop if we can't get monitor to be running during the start */
	if (!ensure_monitor_pg_running(monitor, mconfig))
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
		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(mconfig->pathnames.pid, start_pid);

		if (!ensure_monitor_pg_running(monitor, mconfig))
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
