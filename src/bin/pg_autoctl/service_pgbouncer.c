/*
 * src/bin/pg_autoctl/service_pgbouncer.c
 *     Service that manages a pgbouncer instance.
 *
 * Copyright (c) XXX
 */

#include <sys/types.h>
#include <unistd.h>

#include "cli_common.h"
#include "cli_root.h"
#include "monitor.h"
#include "pgbouncer_config.h"
#include "runprogram.h"
#include "service_pgbouncer.h"
#include "signals.h"
#include "string_utils.h"


/*
 * We are called from the process tree something like this
 *
 * \_ pg_autoctl create postgres --pgdata ./data ---
 *     \_ pg_autoctl: start/stop postgres
 *     |   \_ /postgres/build/bin/postgres -D ./data -h *
 *     |       \_ postgres: logger
 *     |       \_ postgres: startup recovering 000000030000000000000003
 *     |       \_ postgres: checkpointer
 *     |       \_ postgres: background writer
 *     |       \_ postgres: stats collector
 *     |       \_ postgres: walreceiver streaming 0/305FB48
 *     \_ pg_autoctl: node active
 *
 * We want to launch our service_pgbouncer_manager process which will listen for
 * notifications and a pgbouncer as an indipendent child, like this:
 * \_ pg_autoctl create postgres --pgdata ./data ---
 *     \_ pg_autoctl: start/stop postgres
 *     |   \_ /postgres -D ./data -h *
 *     |       \_ postgres: logger
 *     |       \_ postgres: startup recovering 000000030000000000000003
 *     |       \_ postgres: checkpointer
 *     |       \_ postgres: background writer
 *     |       \_ postgres: stats collector
 *     |       \_ postgres: walreceiver streaming 0/305FB48
 *     \_ pg_autoctl: node active
 *     \_ pg_autoctl: pgbouncer manager
 *         \_ pgbouncer config.ini
 *
 * The pgbouncer manager will be be responsible for starting/stopping/reloading
 * its child process, pgbouncer.
 * Starting happens only once during startup, right after fork
 * Stopping happens only once during asked to stop from supervisor
 * Reloading happens when required:
 *     asked to reload from supervisor
 *     got notified from the monitor
 * The pgbouncer manager process will waitpid() on the child and if the child is
 * not running (SIGCONT?) then the manager simply exits and lets its supervisor
 * decide what to do. Its supervisor (pg_autoctl) might decide to call start
 * again or it might do nothing.
 *
 * The pgbouncer manager service when starts it should
 *		Connect to the monitor and issue a LISTEN command
 *      Set up the run time configuration for pgbouncer (cache reset)
 *		Launch the child pgbouncer process via runprogram
 *
 * The pgbouncer manager service when it loops it should
 *			cache invalidation: process notifications for state change
 *			check that the child pgbouncer is still running,
 *				if not exit the	process
 *
 * The whole service runs under the supervision protocol. It subscribes itself
 * with a 'soft' restart policy. I.e. Best efford. If the supervisor fails to
 * maintain the child running after MAX_RESTART_ATTEMPTS, then it simply
 * deactivates the service. The 'hard' restart policy, shuts down the whole
 * pg_autoctl process tree and is not desirable to loose the database simply
 * because pgbouncer burfs.
 */

static pid_t service_pgbouncer_launch(void *context);
static void service_pgbouncer_manager_loop(void *context, pid_t ppid);

static PgbouncerConfig *
service_pbouncer_setup_config(Keeper *keeper)
{
	KeeperConfig *keeperConfig = &(keeper->config);
	PgbouncerConfig *pgbouncerConfig;
	Monitor monitor = { 0 };
	NodeAddress primary = { 0 };

	pgbouncerConfig = calloc(1, sizeof(*pgbouncerConfig));
	if (!pgbouncerConfig)
	{
		/* This is not recoverable */
		log_fatal("malloc failed %m");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Make certain that we have the latest configuration and that the postgres
	 * is done been setting up
	 */
	if (!keeper_config_read_file(keeperConfig,
								 true, /* missingPgdataIsOk */
								 true, /* pgIsNotRunningIsOk */
								 true /* monitorDisabledIsOk */))
	{
		/* It has already logged why */
		return NULL;
	}

	/*
	 * Poor mans' synchronisation:
	 *
	 * Currently pgbouncer can only run as a subprocess of a postgres node. Make
	 * certain that the node is running. If not, spin a bit in case this is
	 * still starting, otherwise fail.
	 */
	if (!pg_setup_is_ready(&keeperConfig->pgSetup, true /* postgresNotRunningIsOk */))
	{
		/* Spin a bit otherwise fail */
		int retries = 10;
		do {
			pg_usleep(100000L);
		} while (!pg_setup_is_ready(&keeperConfig->pgSetup, true) && --retries > 0);

		if (!pg_setup_is_ready(&keeperConfig->pgSetup, true))
		{
			log_error("Cannot start pgbouncer service, pg set up is not ready");
			return false;
		}
	}

	/*
	 * Verify as best as possible that we will not fail later in the process
	 * tree. It should not be problem even if we fail, just a bit of waste.
	 */
	if (!pgbouncer_config_init(pgbouncerConfig, keeperConfig->pgSetup.pgdata) ||
		!pgbouncer_config_read_template(pgbouncerConfig))
	{
		/* It has already logged why */
		free(pgbouncerConfig);
		return NULL;
	}

	strlcpy(pgbouncerConfig->monitor_pguri, keeperConfig->monitor_pguri,
			sizeof(keeperConfig->monitor_pguri));
	strlcpy(pgbouncerConfig->formation, keeperConfig->formation,
			sizeof(keeperConfig->formation));
	pgbouncerConfig->groupId = keeperConfig->groupId;

	if (!monitor_init(&monitor, keeperConfig->monitor_pguri))
	{
		/* It has already logged why */
		free(pgbouncerConfig);
		return NULL;
	}

	if (!monitor_get_primary(&monitor, keeperConfig->formation,
							 keeperConfig->groupId, &primary))
	{
		/* It has already logged why */
		pgsql_finish(&(monitor.pgsql));
		free(pgbouncerConfig);
		return NULL;
	}

	pgsql_finish(&(monitor.pgsql));
	pgbouncerConfig->pgSetup = keeperConfig->pgSetup;
	pgbouncerConfig->primary = primary;

	return pgbouncerConfig;
}


/*
 * service_pgbouncer_start starts pgbouncer, our manager, and pgbouncer in a
 * subprocess. We do not want to run pgbouncer as a deamon, because we want to
 * control the subprocess and maintain it as a child of the current process
 * tree. We do not want to run our manager in the parent, because we want it to
 * be a supervised service.
 */
bool
service_pgbouncer_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;
	PgbouncerConfig *pgbouncerConfig;
	IntString semIdString;
	pid_t fpid;
	semIdString = intToString(log_semaphore.semId);
	setenv(PG_AUTOCTL_DEBUG, "1", 1);
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the pgbouncer manager sub-process */
	fpid = fork();
	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the pgbouncer manager process");
			return false;
		}

		case 0:
		{
			pid_t pgbouncerPid;

			pgbouncerConfig = service_pbouncer_setup_config(keeper);
			if (!pgbouncerConfig)
			{
				/* It has already logged why */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			if (!pgbouncer_config_write_runtime(pgbouncerConfig))
			{
				/* It has already logged why */
				free(pgbouncerConfig);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			pgbouncerPid = service_pgbouncer_launch(pgbouncerConfig);

			service_pgbouncer_manager_loop(pgbouncerConfig, pgbouncerPid);
			pgbouncer_config_destroy(pgbouncerConfig);
			free(pgbouncerConfig);

			if (asked_to_stop_fast || asked_to_stop ||
				asked_to_quit)
			{
				log_info("Stopped pgbouncer manager service");
				exit(EXIT_CODE_QUIT);
			}

			log_fatal("BUG: unexpected return from service_pgbouncer_loop()");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			/* fork succeeded, in parent */
			pid_t wpid;
			int status;

			wpid = waitpid(fpid, &status, WNOHANG);

			if (wpid != 0)
			{
				/* Something went wrong with our child */
				log_error("pg_autoctl pgbouncer manager process failed in subprocess %d",
						  fpid);
				return false;
			}

			log_debug("pg_autoctl pgbouncer manager process started in subprocess %d",
					  fpid);
			*pid = fpid;

			return true;
		}
	}

	return false; /* Not reached, keep the compiler happy */
}


/*
 * service_pgbouncer_launch executes pgbouncer in a child process.
 * Returns the child's pid on success or Exits in failure.
 */
static pid_t
service_pgbouncer_launch(void *context)
{
	PgbouncerConfig *config = (PgbouncerConfig *) context;
	pid_t fpid;

	IntString semIdString = intToString(log_semaphore.semId);

	setenv(PG_AUTOCTL_DEBUG, "1", 1);
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

	fpid = fork();
	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the pgbouncer process");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		case 0:
		{
			/* We are the child process that actually runs pgbouncer */
			Program program;
			char *args[] = {
				config->pgbouncerProg,
				"-q",   /* quiet output to logfile */
				config->pathnames.pgbouncerRunTime,
				NULL
			};

			/* We do not want to setsid() */
			program = initialize_program(args, false);

			program.capture = false; /* redirect output */
			program.stdOutFd = STDOUT_FILENO;
			program.stdErrFd = STDERR_FILENO;

			/* It calls execv and should not return */
			(void) execute_program(&program);

			exit(EXIT_CODE_QUIT);
		}

		default:
		{
			/* Everything is ok */
			break;
		}
	}

	return fpid;
}


static bool
pgbouncer_manager_ensure_child(pid_t pgbouncerPid)
{
	int status = 0;
	int w;

	w = waitpid(pgbouncerPid, &status, WNOHANG);
	if (w == -1)
	{
		/* Cannot recover from this cleanly */
		log_fatal("Failed to waitpid");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (w == pgbouncerPid)
	{
		if (WIFEXITED(status))
		{
			log_info("Child %d exited normaly", pgbouncerPid);
		}
		else if (WIFSIGNALED(status))
		{
			log_info("Child %d got signaled", pgbouncerPid);
		}
		else if (WIFSTOPPED(status))
		{
			log_info("Child %d stopped", pgbouncerPid);
		}
		else
		{
			log_fatal("Child %d exited abnormaly", pgbouncerPid);
		}

		return false;
	}

	return w == 0;
}


/*
 * service_pgbouncer_loop is the manager process.
 * It has has three tasks
 *      Checks if the pgbouncer process is still running
 *			handled by pgbouncer_manager_ensure_child()
 *		Listens the monitor for notifications
 *		Signals the pgbouncer process if asked
 */
static void
service_pgbouncer_manager_loop(void *context, pid_t ppid)
{
	PgbouncerConfig *config = context;
	Monitor monitor = { 0 };
	int retries = 0;

	(void) set_ps_title("pg_autoctl: manage pgbouncer");

	if (!monitor_init(&monitor, config->monitor_pguri))
	{
		log_error("Failed to initialize monitor");
		kill(ppid, SIGQUIT);
		return;
	}

	/* setup our monitor client connection with our notification handler */
	(void) monitor_setup_notifications(&monitor,
									   config->groupId,
									   config->primary.nodeId);

	while (true)
	{
		bool groupStateHasChanged;
		char *channels[] = { "state", NULL };

		if (!pgbouncer_manager_ensure_child(ppid))
		{
			/* It has already logged why */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		if (asked_to_stop_fast || asked_to_stop || asked_to_quit)
		{
			kill(ppid, SIGQUIT);
			break;
		}

		if (!pgsql_listen(&(monitor.pgsql), channels) ||
			!monitor_wait_for_state_change(&monitor,
										   config->formation,
										   config->groupId,
										   config->primary.nodeId,
										   1000 /* Ms */,
										   &groupStateHasChanged))
		{
			log_error("Failed to receive details from monitor %d", retries);
			if (++retries < 10)
			{
				pg_usleep(100 * retries);
				continue;
			}
			else
			{
				kill(ppid, SIGQUIT);
				break;
			}
		}

		/* Cache invalidation is needed */
		if (groupStateHasChanged)
		{
			if (!pgbouncer_manager_ensure_child(ppid))
			{
				/* It has already logged why */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			if (asked_to_stop_fast || asked_to_stop || asked_to_quit)
			{
				kill(ppid, SIGQUIT);
				break;
			}

			log_info("Primary changed pausing pgbouncer until a new primary elected");
			kill(ppid, SIGUSR1 /* Pause */);

			monitor_wait_until_some_node_reported_state(&monitor,
														config->formation,
														config->groupId,
														NODE_KIND_UNKNOWN,
														PRIMARY_STATE);


			config->primary.isPrimary = false;
			monitor_get_primary(&monitor,
								config->formation,
								config->groupId,
								&config->primary);

			if (!config->primary.isPrimary)
			{
				log_info("Failed to get primary");
				kill(ppid, SIGINT);
				pgsql_finish(&monitor.pgsql);
				break;
			}

			(void) pgbouncer_config_write_runtime(config);
			kill(ppid, SIGHUP /* Reload */);
			kill(ppid, SIGUSR2 /* Continue */);
		}

		pgsql_finish(&monitor.pgsql);
	}
}
