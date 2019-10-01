/*
 * src/bin/pg_autoctl/service.c
 *   Starts and stop the sub-processes needed for pg_autoctl run. That's the
 *   embedded HTTPd process, the main loop when using a monitor, the internal
 *   sub-command listener, and the postgres main process itself, too.
 *
 *   pg_autoctl run
 *    - keeper run loop   [monitor enabled]
 *    - httpd server      [all cases]
 *    - listener          [all cases] [published API varies]
 *      - pg_autoctl do fsm assign single
 *      - pg_autoctl do fsm assign wait_primary
 *      - pg_autoctl enable maintenance
 *      - pg_autoctl disable maintenance
 *    - postgres -p 5432 -h localhost -k /tmp
 *
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cli_root.h"
#include "defaults.h"
#include "httpd.h"
#include "keeper.h"
#include "keeper_listener.h"
#include "service.h"
#include "signals.h"


static bool service_start_with_monitor(Keeper *keeper);
static bool service_start_without_monitor(Keeper *keeper);


/*
 * service_start starts the sub-processes that collectively implement our
 * pg_autoctl run service. The list of sub-processes is not the same depending
 * if we're running with or without a monitor.
 */
bool
service_start(Keeper *keeper)
{
	if (keeper->config.monitorDisabled)
	{
		return service_start_without_monitor(keeper);
	}
	else
	{
		return service_start_with_monitor(keeper);
	}
}


/*
 * service_start_with_monitor starts all the sub-processes needed when running
 * the keeper service with a monitor. That includes the main loop.
 *
 * TODO: check the signals situation and wait() for sub-processes in the parent
 * process.
 */
static bool
service_start_with_monitor(Keeper *keeper)
{
	pid_t loopPid, httpdPid;

	if (!keeper_check_monitor_extension_version(keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	httpd_start_process(keeper->config.pgSetup.pgdata,
						keeper->config.httpd.listen_address,
						keeper->config.httpd.port,
						&httpdPid);

	return keeper_service_run(keeper, &loopPid);
}


/*
 * service_start_without_monitor starts all the sub-processes needed when
 * running the keeper service without a monitor.
 *
 * TODO: check the signals situation and wait() for sub-processes in the parent
 * process.
 */
static bool
service_start_without_monitor(Keeper *keeper)
{
	pid_t listenerPid, httpdPid;

	/* start the command pipe sub-process */
	if (!keeper_listener_start(keeper->config.pgSetup.pgdata, &listenerPid))
	{
		log_fatal("Failed to start the command listener process");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* TODO: start the HTTPd process under a supervisor */
	httpd_start_process(keeper->config.pgSetup.pgdata,
						keeper->config.httpd.listen_address,
						keeper->config.httpd.port,
						&httpdPid);

	log_debug("keeper subprocesses have started [%d,%d]", listenerPid, httpdPid);

	/* wait until both process are done */
	while (true)
	{
		pid_t pid;
		int status;

		/* ignore errors */
		pid = waitpid(-1, &status, WNOHANG);

		switch (pid)
		{
			case -1:
			{
				if (errno == ECHILD)
				{
					/* no more childrens */
					if (asked_to_stop || asked_to_stop_fast)
					{
						return keeper_service_stop(keeper);
					}
				}
				else
				{
					log_fatal("Oops, waitpid() failed with: %s",
							  strerror(errno));
					return keeper_service_stop(keeper);
				}
			}

			case 0:
			{
				/*
				 * We're using WNOHANG, 0 means there are no stopped or exited
				 * children, it's all good.
				 */
				break;
			}

			default:
			{
				char *verb = WIFEXITED(status) ? "exited" : "failed";
				int returnCode = WEXITSTATUS(status);

				if (pid == listenerPid)
				{
					log_error("Keeper internal listener process %s [%d]",
							  verb, returnCode);

					if (!(asked_to_stop || asked_to_stop_fast))
					{
						kill(httpdPid, SIGQUIT);
					}
					return keeper_service_stop(keeper);
				}
				else if (pid == httpdPid)
				{
					log_error("Keeper HTTPd process %s [%d]",
							  verb, returnCode);

					if (!(asked_to_stop || asked_to_stop_fast))
					{
						kill(listenerPid, SIGQUIT);
					}
					return keeper_service_stop(keeper);
				}
				else
				{
					log_fatal("BUG: waitpid() returned an unknown PID: %d", pid);
				}
				return keeper_service_stop(keeper);
			}
		}

		sleep(1);
	}

	/* wait until the listener has exited too */
	log_warn("service_start_without_monitor: exit");

	return keeper_service_stop(keeper);
}
