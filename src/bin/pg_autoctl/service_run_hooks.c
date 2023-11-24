/*
 * src/bin/pg_autoctl/service_run_hooks.c
 *   The main loop of the pg_autoctl run-hooks service
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

#include "parson.h"

#include "cli_common.h"
#include "cli_root.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "pidfile.h"
#include "service_run_hooks.h"
#include "signals.h"
#include "state.h"
#include "string_utils.h"
#include "supervisor.h"

#include "runprogram.h"


static void reload_configuration(Keeper *keeper);
static bool service_run_hook(Keeper *keeper, NodeAddress *primary);
static bool service_run_hooks_start_service(Keeper *keeper, pid_t *pid);
static bool service_run_hooks_check_service(Keeper *keeper, pid_t *hookServicePid);


/*
 * service_run_hooks_start starts a sub-process that listens to the monitor
 * notifications and outputs them for the user.
 */
bool
service_run_hooks_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the run-hooks process");
			return false;
		}

		case 0:
		{
			/* here we call execv() so we never get back */
			(void) service_run_hooks_runprogram(keeper);

			/* unexpected */
			log_fatal("BUG: returned from service_run_hooks_runprogram()");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl run-hooks process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}


/*
 * service_run_hooks_runprogram runs the node_active protocol service:
 *
 *   $ pg_autoctl do service run-hooks --pgdata ...
 *
 * This function is intended to be called from the child process after a fork()
 * has been successfully done at the parent process level: it's calling
 * execve() and will never return.
 */
void
service_run_hooks_runprogram(Keeper *keeper)
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
	 */
	char *pgdata = keeperOptions.pgSetup.pgdata;

	setenv(PG_AUTOCTL_DEBUG, "1", 1);

	args[argsIndex++] = (char *) pg_autoctl_program;
	args[argsIndex++] = "do";
	args[argsIndex++] = "service";
	args[argsIndex++] = "run-hooks";
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
 * service_run_hooks_init initializes the pg_autoctl service for the run-hooks
 * implementation.
 */
bool
service_run_hooks_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	/* wait until the config file exists on-disk */
	ConnectionRetryPolicy retryPolicy = { 0 };

	/* retry until we have a configuration file ready (create --run) */
	(void) pgsql_set_main_loop_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		if (file_exists(config->pathnames.config))
		{
			/* success: break out of the retry loop */
			break;
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			return true;
		}

		int sleepTimeMs =
			pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_debug("Checking if config file \"%s\" exists again in %dms",
				  config->pathnames.config,
				  sleepTimeMs);

		(void) pg_usleep(sleepTimeMs * 1000);
	}

	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file_skip_pgsetup(config, monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!config->monitorDisabled)
	{
		if (!monitor_init(&keeper->monitor, config->monitor_pguri))
		{
			log_fatal("Failed to initialize monitor, see above for details");
			return false;
		}
	}

	return true;
}


/*
 * run_hooks_loop runs the main loop of the run-hooks service.
 */
bool
service_run_hooks_loop(Keeper *keeper, pid_t start_pid)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);

	char *formation = config->formation;
	int groupId = config->groupId;

	pid_t hookServicePid = 0;

	/*
	 * At startup, call the registered command line with the current primary
	 * node.
	 */
	if (!IS_EMPTY_STRING_BUFFER(config->onPrimaryCmd))
	{
		NodeAddress primary = { 0 };

		if (!monitor_get_primary(monitor, formation, groupId, &primary))
		{
			/* errors have already been logged */
			return false;
		}

		if (!service_run_hook(keeper, &primary))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/*
	 * At startup, now that we have run the hooks.on_primary command (if any),
	 * now is a good time to run the service (if any).
	 */
	if (!service_run_hooks_start_service(keeper, &hookServicePid))
	{
		/* errors have already been logged */
		return false;
	}

	bool firstLoop = true;

	for (;; firstLoop = false)
	{
		if (asked_to_reload || firstLoop)
		{
			(void) reload_configuration(keeper);
		}
		else if (!firstLoop)
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			log_info("Run-hooks service received signal %s, terminating",
					 signal_to_string(get_current_signal(SIGTERM)));
			break;
		}

		/*
		 * Consider the service disabled unless we have a command to run when a
		 * primary node is promoted.
		 */
		if (!config->enableHooks)
		{
			continue;
		}

		/*
		 * Take care of our hooks.service command, which we restart when it
		 * fails.
		 */
		if (!service_run_hooks_check_service(keeper, &hookServicePid))
		{
			/* errors have already been logged */
			return false;
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

	return true;
}


/*
 * reload_configuration reads the supposedly new configuration file and
 * integrates accepted new values into the current setup.
 */
static void
reload_configuration(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file_skip_pgsetup(config, monitorDisabledIsOk))
	{
		/* errors have already been logged */
		asked_to_reload = 0;
		return;
	}

	/* we are impacted by a monitor configuration change */
	if (!config->monitorDisabled)
	{
		if (!monitor_init(&keeper->monitor, config->monitor_pguri))
		{
			log_fatal("Failed to initialize monitor, see above for details");
			asked_to_reload = 0;
			return;
		}
	}

	/* only take care of the hooks section */
	if (!IS_EMPTY_STRING_BUFFER(config->onPrimaryCmd))
	{
		JSON_Value *json = json_parse_string(config->onPrimaryCmd);

		if (json_type(json) != JSONString &&
			json_type(json) != JSONArray)
		{
			log_error("Failed to parse hooks.on_primary command \"%s\", "
					  "a JSON string or a JSON array is expected",
					  config->onPrimaryCmd);
		}
	}

	if (!IS_EMPTY_STRING_BUFFER(config->serviceStartCmd))
	{
		JSON_Value *json = json_parse_string(config->serviceStartCmd);

		if (json_type(json) != JSONString &&
			json_type(json) != JSONArray)
		{
			log_error("Failed to parse hooks.service command \"%s\", "
					  "a JSON string or a JSON array is expected",
					  config->serviceStartCmd);
		}
	}

	/* we're done reloading now. */
	asked_to_reload = 0;
}


/*
 * service_run_hook runs the hooks.on_primary command.
 */
static bool
service_run_hook(Keeper *keeper, NodeAddress *primary)
{
	KeeperConfig *config = &(keeper->config);

	if (!config->enableHooks || IS_EMPTY_STRING_BUFFER(config->onPrimaryCmd))
	{
		return true;
	}

	log_warn("Running command: %s", config->onPrimaryCmd);

	return false;
}


/*
 * service_run_hooks_start_service starts the service that's been setup with
 * the hooks registration, if any. Could be a pgloader daemon, for instance.
 */
static bool
service_run_hooks_start_service(Keeper *keeper, pid_t *pid)
{
	KeeperConfig *config = &(keeper->config);

	if (!config->enableHooks || IS_EMPTY_STRING_BUFFER(config->serviceStartCmd))
	{
		*pid = 0;
		return true;
	}

	log_warn("Starting service: %s", config->serviceStartCmd);

	return false;
}


/*
 * service_run_hooks_check_service makes sure that the hooks service is still
 * running, and restarts it otherwise.
 */
static bool
service_run_hooks_check_service(Keeper *keeper, pid_t *hookServicePid)
{
	if (*hookServicePid == 0)
	{
		return true;
	}

	int status;
	pid_t pid = waitpid(*hookServicePid, &status, WNOHANG);

	switch (pid)
	{
		case -1:
		{
			/* if our PostgresService stopped, just continue */
			if (errno != ECHILD)
			{
				log_error("Failed to call waitpid(): %m");
			}
			break;
		}

		case 0:
		{
			/*
			 * We're using WNOHANG, 0 means there are no stopped or exited
			 * children, it's all good. It's the expected case when
			 * everything is running smoothly, so enjoy and sleep for
			 * awhile.
			 */
			break;
		}

		default:
		{
			/* we expect that pid is hookServicePid */
			if (pid != *hookServicePid)
			{
				log_error("BUG: service_run_hooks_loop waitpid() got %d, "
						  "expected hookServicePid %d",
						  pid,
						  *hookServicePid);
				return false;
			}

			char *verb = WIFEXITED(status) ? "exited" : "failed";
			log_info("waitpid(): hook service process %d has %s", pid, verb);


			if (!service_run_hooks_start_service(keeper, hookServicePid))
			{
				/* errors have already been logged */
				return false;
			}

			break;
		}
	}

	return true;
}
