/*
 * src/bin/pg_autoctl/postgres_service_ctl.c
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
#include "service_postgres_ctl.h"
#include "signals.h"
#include "supervisor.h"
#include "state.h"
#include "string_utils.h"

#include "runprogram.h"

static bool shutdownSequenceInProgress = false;

static bool ensure_postgres_status(LocalPostgresServer *postgres,
								   Service *service);

static bool ensure_postgres_status_stopped(LocalPostgresServer *postgres,
										   Service *service);

static bool ensure_postgres_status_running(LocalPostgresServer *postgres,
										   Service *service,
										   bool ensurePostgresSubprocess);


/*
 * service_postgres_ctl_start starts a subprocess that implements the postgres
 * service depending on the current assigned and goal state of the keeper.
 */
bool
service_postgres_ctl_start(void *context, pid_t *pid)
{
	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the postgres controller process");
			return false;
		}

		case 0:
		{
			/* here we call execv() so we never get back */
			(void) service_postgres_ctl_runprogram();

			/* unexpected */
			log_fatal("BUG: returned from service_keeper_runprogram()");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			log_debug(
				"pg_autoctl started postgres controller in subprocess %d",
				fpid);
			*pid = fpid;

			return true;
		}
	}
}


/*
 * service_postgres_ctl_runprogram runs the postgres controller service:
 *
 *   $ pg_autoctl do service postgres --pgdata ...
 */
void
service_postgres_ctl_runprogram()
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
	 *
	 * The pg_autoctl postgres controller is used both in the monitor context
	 * and in the keeper context; which means it gets started from one of the
	 * following top-level commands:
	 *
	 *  - pg_autoctl create monitor
	 *  - pg_autoctl create postgres
	 *  - pg_autoctl run
	 *
	 * The monitor specific commands set monitorOptions, the generic and keeper
	 * specific commands set keeperOptions.
	 */
	char *pgdata =
		IS_EMPTY_STRING_BUFFER(monitorOptions.pgSetup.pgdata)
		? keeperOptions.pgSetup.pgdata
		: monitorOptions.pgSetup.pgdata;

	setenv(PG_AUTOCTL_DEBUG, "1", 1);

	args[argsIndex++] = (char *) pg_autoctl_program;
	args[argsIndex++] = "do";
	args[argsIndex++] = "service";
	args[argsIndex++] = "postgres";
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
 * service_postgres_ctl_loop loops over the current CTL state file and ensure
 * that Postgres is running when that's expected, or that Postgres is not
 * running when in a state where we should maintain Postgres down to avoid
 * split-brain situations.
 */
void
service_postgres_ctl_loop(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	LocalExpectedPostgresStatus *localStatus = &(postgres->expectedPgStatus);
	KeeperStatePostgres *pgStatus = &(localStatus->state);

	/*
	 * We re-use a service definition because that's handy for our code here,
	 * but we implement our own policy for handling the service: the keeper
	 * process might want Postgres to not be running at times, to avoid
	 * split-brain situations.
	 */
	Service postgresService = {
		SERVICE_NAME_POSTGRES,
		RP_PERMANENT,           /* actually micro-managed in this loop */
		-1,
		&service_postgres_start,
		(void *) pgSetup
	};

	bool pgStatusPathIsReady = false;

	/* make sure to initialize the expected Postgres status to unknown */
	pgStatus->pgExpectedStatus = PG_EXPECTED_STATUS_UNKNOWN;

	for (;;)
	{
		int status;

		/* we might have to reload, pass the signal down */
		if (asked_to_reload)
		{
			(void) service_postgres_reload((void *) &postgresService);

			asked_to_reload = 0;
		}

		/* that's expected the shutdown sequence from the supervisor */
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			if (!shutdownSequenceInProgress)
			{
				shutdownSequenceInProgress = true;
				log_info("Postgres controller service received signal %s, "
						 "terminating",
						 signal_to_string(get_current_signal(SIGTERM)));
			}

			if (!ensure_postgres_status_stopped(postgres, &postgresService))
			{
				log_error("Failed to stop Postgres, see above for details");
				pg_usleep(100 * 1000);  /* 100ms */
				continue;
			}
			exit(EXIT_CODE_QUIT);
		}

		/*
		 * This postgres controller process is running Postgres as a child
		 * process and thus is responsible for calling waitpid() from time to
		 * time.
		 */
		pid_t pid = waitpid(-1, &status, WNOHANG);

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
				if (pid != postgresService.pid)
				{
					/* might be one of our pg_controldata... */
					char *verb = WIFEXITED(status) ? "exited" : "failed";
					log_debug("waitpid(): process %d has %s", pid, verb);
				}

				/*
				 * Postgres is not running anymore, the rest of the code will
				 * handle that situation, just continue.
				 */
				break;
			}
		}

		if (pg_setup_pgdata_exists(pgSetup))
		{
			/*
			 * If we have a PGDATA directory, now is a good time to initialize
			 * our LocalPostgresServer structure and its file paths to point at
			 * the right place: we need to normalize PGDATA to its realpath
			 * location.
			 */
			if (!pgStatusPathIsReady)
			{
				/* initialize our Postgres state file path */
				if (!local_postgres_set_status_path(postgres, false))
				{
					/* highly unexpected */
					log_error("Failed to build postgres state file pathname, "
							  "see above for details.");

					/* maybe next round will have better luck? */
					pg_usleep(100 * 1000);  /* 100ms */
					continue;
				}

				pgStatusPathIsReady = true;

				log_trace("Reading current postgres expected status from \"%s\"",
						  localStatus->pgStatusPath);
			}
		}
		else if (!pgStatusPathIsReady)
		{
			/*
			 * If PGDATA doesn't exists yet, we didn't have a chance to
			 * normalize its filename and we might be reading the wrong file
			 * for the Postgres expected status. So we first check if our
			 * pgSetup reflects an existing on-disk instance and if not, update
			 * it until it does.
			 *
			 * The keeper init process is reponsible for running pg_ctl initdb.
			 *
			 * Given that we have two processes working concurrently and
			 * deciding at the same time what's next, we need to be cautious
			 * about race conditions. We add extra checks around existence of
			 * files to make sure we don't get started too early.
			 */
			PostgresSetup newPgSetup = { 0 };
			bool missingPgdataIsOk = true;
			bool postgresNotRunningIsOk = true;

			if (pg_setup_init(&newPgSetup,
							  pgSetup,
							  missingPgdataIsOk,
							  postgresNotRunningIsOk) &&
				pg_setup_pgdata_exists(&newPgSetup) &&
				pg_auto_failover_default_settings_file_exists(&newPgSetup))
			{
				*pgSetup = newPgSetup;
			}

			pg_usleep(100 * 1000);  /* 100ms */
			continue;
		}

		/*
		 * Maintain a Postgres service as a sub-process.
		 *
		 * Depending on the current state of the keeper, we need to either
		 * ensure that Postgres is running, or that it is NOT running. To avoid
		 * split-brain situations, we need to ensure Postgres is not running in
		 * the DEMOTED state, for instance.
		 *
		 * Adding to that, during the `pg_autoctl create postgres` phase we
		 * also need to start Postgres and sometimes even restart it.
		 */
		if (pgStatusPathIsReady && file_exists(localStatus->pgStatusPath))
		{
			const char *filename = localStatus->pgStatusPath;

			if (!keeper_postgres_state_read(pgStatus, filename))
			{
				/* errors have already been logged, will try again */
				pg_usleep(100 * 1000);  /* 100ms */
				continue;
			}

			log_trace("service_postgres_ctl_loop: %s in %s",
					  ExpectedPostgresStatusToString(pgStatus->pgExpectedStatus),
					  filename);

			if (!ensure_postgres_status(postgres, &postgresService))
			{
				pgStatusPathIsReady = false;
			}
		}

		pg_usleep(100 * 1000);  /* 100ms */
	}
}


/*
 * ensure_postgres_status ensures that the current keeper's state is met with
 * the current PostgreSQL status, at minimum that PostgreSQL is running when
 * it's expected to be, etc.
 *
 * The Postgres controller process (the code in this file) takes orders from
 * another process, either the monitor "listener" or the keeper "node active"
 * process. The orders are sent through a shared file containing the expected
 * status of the Postgres service.
 *
 * This process only reads the file, and the "other" process is responsible for
 * writing it: deleting a stale version of it at startup, creating it, updating
 * it.
 */
static bool
ensure_postgres_status(LocalPostgresServer *postgres, Service *service)
{
	KeeperStatePostgres *pgStatus = &(postgres->expectedPgStatus.state);

	log_trace("ensure_postgres_status: %s",
			  ExpectedPostgresStatusToString(pgStatus->pgExpectedStatus));

	switch (pgStatus->pgExpectedStatus)
	{
		case PG_EXPECTED_STATUS_UNKNOWN:
		{
			/* do nothing */
			return true;
		}

		case PG_EXPECTED_STATUS_STOPPED:
		{
			return ensure_postgres_status_stopped(postgres, service);
		}

		case PG_EXPECTED_STATUS_RUNNING:
		{
			return ensure_postgres_status_running(postgres, service, false);
		}

		case PG_EXPECTED_STATUS_RUNNING_AS_SUBPROCESS:
		{
			return ensure_postgres_status_running(postgres, service, true);
		}
	}

	/* make compiler happy */
	return false;
}


/*
 * ensure_postgres_status_stopped ensures that Postgres is stopped.
 */
static bool
ensure_postgres_status_stopped(LocalPostgresServer *postgres, Service *service)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	bool pgIsNotRunningIsOk = true;
	bool pgIsRunning = pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk);

	if (pgIsRunning)
	{
		/* service_postgres_stop() logs about stopping Postgres */
		log_debug("pg_autoctl: stop postgres (pid %d)", service->pid);

		return service_postgres_stop(service);
	}
	return true;
}


/*
 * ensure_postgres_status_running ensures that Postgres is running.
 */
static bool
ensure_postgres_status_running(LocalPostgresServer *postgres, Service *service,
							   bool ensurePostgresSubprocess)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	/* we might still be starting-up */
	bool pgIsNotRunningIsOk = true;
	bool pgIsRunning = pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk);
	bool restartPostgres = false;

	log_trace("ensure_postgres_status_running: %s",
			  pgIsRunning ? "running" : "not running");

	if (pgIsRunning)
	{
		if (ensurePostgresSubprocess && pgSetup->pidFile.pid != service->pid)
		{
			restartPostgres = true;

			log_warn("Postgres is already running with pid %d, "
					 "which is not a sub-process of pg_autoctl, "
					 "restarting Postgres",
					 pgSetup->pidFile.pid);

			if (!service_postgres_stop(service))
			{
				log_fatal("Failed to stop Postgres pid %d, "
						  "see above for details",
						  pgSetup->pidFile.pid);
				return false;
			}
		}
		else
		{
			return true;
		}
	}

	if (service_postgres_start(service->context, &(service->pid)))
	{
		if (countPostgresStart > 1)
		{
			log_warn("PostgreSQL was not running, restarted with pid %d",
					 pgSetup->pidFile.pid);
		}

		if (restartPostgres)
		{
			log_warn("PostgreSQL had to be stopped and restarted, "
					 "it is now running as a subprocess of pg_autoctl, "
					 "with pid %d",
					 pgSetup->pidFile.pid);
		}

		return true;
	}
	else
	{
		log_warn("Failed to start Postgres instance at \"%s\"",
				 pgSetup->pgdata);

		return false;
	}
	return true;
}
