/*
 * src/bin/pg_autoctl/service_keeper_init.c
 *     Keeper initialisation service.
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
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "state.h"
#include "service.h"
#include "service_keeper.h"
#include "service_keeper_init.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "string_utils.h"


/*
 * service_keeper_init defines and start services needed during the
 * keeper initialisation when doing `pg_autoctl create postgres`. We already
 * need to have our Postgres service supervisor sub-process started and ready
 * to start postgres when reaching initialization stage 2.
 */
bool
service_keeper_init(Keeper *keeper)
{
	const char *pidfile = keeper->config.pathnames.pid;

	Service subprocesses[] = {
		{
			"postgres ctl",
			0,
			&service_postgres_ctl_start,
			&service_postgres_ctl_stop,
			(void *) keeper
		},
		{
			"keeper init",
			0,
			&service_keeper_init_start,
			&service_keeper_init_stop,
			(void *) keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	if (!service_start(subprocesses, subprocessesCount, pidfile))
	{
		/* errors have already been logged */
		return false;
	}

	/* we only get there when the supervisor exited successfully (SIGTERM) */
	return true;
}


/*
 * service_keeper_init_start is a subprocess that runs the installation of the
 * pg_autoctl keeper and its Postgres service, including initdb or
 * pg_basebackup.
 */
bool
service_keeper_init_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;
	KeeperConfig *config = &(keeper->config);

	pid_t fpid = -1;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the keeper init process");
			return false;
		}

		case 0:
		{
			(void) set_ps_title("keeper init");

			if (!keeper_pg_init_and_register(keeper))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_QUIT);
			}

			if (keeperInitWarnings)
			{
				log_info("Keeper has been successfully initialized, "
						 "please fix above warnings to complete installation.");
			}
			else
			{
				log_info("%s has been successfully initialized.", config->role);

				if (createAndRun)
				{
					(void) service_keeper_runprogram(keeper);
				}
				else
				{
					exit(EXIT_CODE_QUIT);
				}
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
			/* fork succeeded, in parent */
			log_debug("pg_autoctl init process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}


/*
 * service_keeper_init_stop stops the postgres service.
 */
bool
service_keeper_init_stop(void *context)
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
