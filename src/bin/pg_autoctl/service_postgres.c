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
#include "primary_standby.h"
#include "service.h"
#include "service_postgres.h"
#include "signals.h"
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
			log_error("Failed to fork the postgres supervisor process");
			return false;
		}

		case 0:
		{
			(void) set_ps_title("postgres");

			/* exec the postgres binary directly, as a sub-process */
			(void) pg_ctl_postgres(pgSetup->pg_ctl,
								   pgSetup->pgdata,
								   pgSetup->pgport,
								   pgSetup->listen_addresses);

			if (asked_to_stop || asked_to_stop_fast)
			{
				exit(EXIT_CODE_QUIT);
			}
			else
			{
				/*
				 * Postgres was stopped by someone else, maybe an admin doing
				 * pg_ctl stop to test our software, or maybe something else.
				 */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		default:
		{
			int timeout = 10;   /* wait for Postgres for 10s */
			int logLevel = ++countPostgresStart == 1 ? LOG_INFO : LOG_DEBUG;

			log_debug("pg_autoctl started postgres in subprocess %d", fpid);
			*pid = fpid;

			/* we're starting postgres, reset the cached value for the pid */
			pgSetup->pidFile.pid = 0;

			return pg_setup_wait_until_is_ready(pgSetup, timeout, logLevel);
		}
	}
}


/*
 * service_postgres_stop stops the postgres service, using pg_ctl stop.
 */
bool
service_postgres_stop(void *context)
{
	Service *service = (Service *) context;
	PostgresSetup *pgSetup = (PostgresSetup *) service->context;

	log_info("Stopping pg_autoctl postgres service");

	if (kill(service->pid, SIGTERM) != 0)
	{
		log_error("Failed to send SIGTERM to pid %d for service %s",
				  service->pid, service->name);
		return false;
	}

	if (!pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to stop Postgres, see above for details");
		return false;
	}

	return true;
}
