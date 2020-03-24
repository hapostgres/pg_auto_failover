/*
 * src/bin/pg_autoctl/postgres_service.c
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
#include "service_postgres.h"
#include "signals.h"
#include "string_utils.h"


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
			log_error("Failed to fork the node_active process");
			return false;
		}

		case 0:
		{
			(void) set_ps_title("postgres");

			if (!pg_ctl_postgres(pgSetup->pg_ctl,
								 pgSetup->pgdata,
								 pgSetup->pgport,
								 pgSetup->listen_addresses))
			{
				log_error("pg_ctl_postgres failure");
				exit(EXIT_CODE_PGSQL);
			}

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
			/*
			 * Update our pgSetup view of Postgres once we have made sure it's
			 * running.
			 */
			PostgresSetup newPgSetup = { 0 };
			bool missingPgdataIsOk = false;
			bool postgresNotRunningIsOk = false;

			int maxAttempts = 50; /* 50 * 100ms = 5s */
			int attempts = 0;

			log_debug("pg_autoctl started postgres in subprocess %d", fpid);
			*pid = fpid;

			/* wait until Postgres has started */
			pgSetup->pidFile.pid = 0;

			for (attempts = 0; attempts < maxAttempts; attempts++)
			{
				bool pgIsRunning = pg_setup_is_running(pgSetup);

				log_debug("waiting for pg_setup_is_running() [%s], attempt %d/%d",
						  pgIsRunning ? "true" : "false",
						  attempts+1,
						  maxAttempts);

				if (pgIsRunning)
				{
					break;
				}

				/* wait for 100 ms and try again */
				pg_usleep(100 * 1000);
			}

			/* update settings from running database */
			if (!pg_setup_init(&newPgSetup,
							   pgSetup,
							   missingPgdataIsOk,
							   postgresNotRunningIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			log_info("Postgres is now serving PGDATA \"%s\" "
					 "on port %d with pid %d",
					 pgSetup->pgdata, pgSetup->pgport, pgSetup->pidFile.pid);

			return true;
		}
	}
}
