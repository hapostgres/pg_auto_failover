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
#include "pidfile.h"
#include "state.h"
#include "service_keeper.h"
#include "service_keeper_init.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "string_utils.h"
#include "supervisor.h"


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
			"postgres",
			RP_PERMANENT,
			-1,
			&service_postgres_ctl_start,
		},
		{
			"keeper init",
			createAndRun ? RP_PERMANENT : RP_TRANSIENT,
			-1,
			&service_keeper_init_start,
			(void *) keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	/* when using pg_autoctl create monitor --run, use "node active" */
	if (createAndRun)
	{
		strlcpy(subprocesses[1].name, "node active", NAMEDATALEN);
	}

	return supervisor_start(subprocesses, subprocessesCount, pidfile);
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
			const char *serviceName = createAndRun ?
									  "pg_autoctl: node active" :
									  "pg_autoctl: node installer";

			(void) set_ps_title(serviceName);

			if (!keeper_pg_init_and_register(keeper))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			if (keeperInitWarnings)
			{
				log_info("Keeper has been successfully initialized, "
						 "please fix above warnings to complete installation.");
				exit(EXIT_CODE_QUIT);
			}

			log_info("%s has been successfully initialized.", config->role);

			if (createAndRun)
			{
				/* here we call execv() so we never get back */
				(void) service_keeper_runprogram(keeper);

				/* unexpected */
				log_fatal("BUG: returned from service_keeper_runprogram()");
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			else
			{
				exit(EXIT_CODE_QUIT);
			}
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl node installer process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}
