/*
 * src/bin/pg_autoctl/postgres_service.c
 *   Utilities to start/stop the pg_autoctl service.
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
#include "pgsetup.h"
#include "pidfile.h"
#include "primary_standby.h"
#include "service_postgres.h"
#include "signals.h"
#include "supervisor.h"
#include "state.h"
#include "string_utils.h"

#include "runprogram.h"

int countPostgresStart = 0;

/*
 * service_postgres_start starts "postgres" in a sub-process. Rather than using
 * pg_ctl start, which forks off a deamon, we want to control the sub-process
 * and maintain it as a process child of pg_autoctl.
 */
bool
service_postgres_start(void *context, pid_t *pid)
{
	PostgresSetup *pgSetup = (PostgresSetup *) context;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the postgres supervisor process");
			return false;
		}

		case 0:
		{
			(void) set_ps_title("postgres");

			log_trace("service_postgres_start: EXEC postgres");

			bool listen = true;

			/* execv() the postgres binary directly, as a sub-process */
			(void) pg_ctl_postgres(pgSetup->pg_ctl,
								   pgSetup->pgdata,
								   pgSetup->pgport,
								   pgSetup->listen_addresses,
								   listen);

			/* unexpected */
			log_fatal("BUG: returned from service_keeper_runprogram()");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			int timeout = 10;   /* wait for Postgres for 10s */
			int logLevel = ++countPostgresStart == 1 ? LOG_INFO : LOG_DEBUG;

			log_debug("pg_autoctl started postgres in subprocess %d", fpid);
			*pid = fpid;

			/* we're starting postgres, reset the cached value for the pid */
			pgSetup->pidFile.pid = 0;

			bool pgIsReady =
				pg_setup_wait_until_is_ready(pgSetup, timeout, logLevel);

			/*
			 * If Postgres failed to start the least we can do is log the
			 * "startup.log" file prominently to the user now.
			 */
			if (!pgIsReady)
			{
				(void) pg_log_startup(pgSetup->pgdata, LOG_ERROR);
			}
			else if (log_get_level() <= LOG_DEBUG)
			{
				/*
				 * If postgres started successfully we only log startup
				 * messages in DEBUG or TRACE loglevel. Otherwise we get might
				 * see this confusing error, but harmless error message:
				 * ERROR:  database "postgres" already exists
				 */
				(void) pg_log_startup(pgSetup->pgdata, LOG_DEBUG);
			}
			return pgIsReady;
		}
	}
}


/*
 * service_postgres_stop stops the postgres service, using pg_ctl stop.
 */
bool
service_postgres_stop(Service *service)
{
	PostgresSetup *pgSetup = (PostgresSetup *) service->context;

	log_info("Stopping pg_autoctl postgres service");

	if (!pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to stop Postgres, see above for details");
		return false;
	}

	/* cache invalidation */
	service->pid = 0;

	return true;
}


/*
 * service_postgres_reload signal Postgres with a SIGHUP
 */
void
service_postgres_reload(Service *service)
{
	log_info("Reloading pg_autoctl postgres service [%d]", service->pid);

	if (kill(service->pid, SIGHUP) != 0)
	{
		log_error("Failed to send SIGHUP to Postgres pid %d: %m", service->pid);
	}
}
