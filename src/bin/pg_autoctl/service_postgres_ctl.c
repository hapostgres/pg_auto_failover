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
#include "primary_standby.h"
#include "service.h"
#include "service_postgres.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "state.h"
#include "string_utils.h"

#include "runprogram.h"

static bool ensure_postgres_status(Keeper *keeper, Service *postgres);
static bool ensure_postgres_status_stopped(Keeper *keeper, Service *postgres);
static bool ensure_postgres_status_running(Keeper *keeper, Service *postgres);


/*
 * service_postgres_ctl_start starts a subprocess that implements the postgres
 * service depending on the current assigned and goal state of the keeper.
 */
bool
service_postgres_ctl_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;
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
			log_error("Failed to fork the postgres controller process");
			return false;
		}

		case 0:
		{
			(void) service_postgres_ctl_runprogram(keeper);
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
 * service_postgres_stop stops the postgres service, using pg_ctl stop.
 */
bool
service_postgres_ctl_stop(void *context)
{
	Service *service = (Service *) context;
	Keeper *keeper = (Keeper *) service->context;
	PostgresSetup *pgSetup = &(keeper->config.pgSetup);

	log_info("Stopping pg_autoctl postgres ctl supervisor service");

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


/*
 * service_postgres_ctl_runprogram runs the node_active protocol service:
 *
 *   $ pg_autoctl do service postgres --pgdata ...
 */
void
service_postgres_ctl_runprogram(Keeper *keeper)
{
	Program program;

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
	args[argsIndex++] = "postgres";
	args[argsIndex++] = "--pgdata";
	args[argsIndex++] = pgdata;
	args[argsIndex++] = logLevelToString(log_get_level());
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	program = initialize_program(args, false);

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
service_postgres_ctl_loop(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	Service postgresService = {
		"postgres",
		-1,
		&service_postgres_start,
		&service_postgres_stop,
		(void *) pgSetup
	};

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	bool pgStatusPathIsReady = false;

	/*
	 * Initialize our keeper struct instance with values from the setup.
	 */
	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(keeper, config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	for (;;)
	{
		pid_t pid;
		int status;

		/* that's expected the shutdown sequence from the supervisor */
		if (asked_to_stop || asked_to_stop_fast)
		{
			exit(EXIT_CODE_QUIT);
		}

		/*
		 * This postgres controller process is running Postgres as a child
		 * process and thus is responsible for calling waitpid() from time to
		 * time.
		 */
		pid = waitpid(-1, &status, WNOHANG);

		switch (pid)
		{
			case -1:
			{
				/* if our PostgresService stopped, just continue */
				if (errno != ECHILD)
				{
					log_error("Oops, waitpid() failed with: %s",
							  strerror(errno));
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
					/* that's quite strange, but we log and continue */
					log_error("BUG: pg_autoctl waitpid() returned %d", pid);
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
				(void) local_postgres_init(postgres, pgSetup);

				pgStatusPathIsReady = true;

				log_trace("Reading current postgres expected status from \"%s\"",
						  pgStatus->pgStatusPath);
			}
		}
		else
		{
			/*
			 * If PGDATA doesn't exists yet, we didn't have a chance to
			 * normalize its filename and we might be reading the wrong file
			 * for the Postgres expected status. So we first check if our
			 * pgSetup reflects an existing on-disk instance and if not, update
			 * it until it does.
			 *
			 * The keeper init process is reponsible for running pg_ctl initdb.
			 */
			PostgresSetup newPgSetup = { 0 };
			bool missingPgdataIsOk = true;
			bool postgresNotRunningIsOk = true;

			if (pg_setup_init(&newPgSetup,
							  pgSetup,
							  missingPgdataIsOk,
							  postgresNotRunningIsOk))
			{
				*pgSetup = newPgSetup;
			}
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
		if (pgStatusPathIsReady && file_exists(pgStatus->pgStatusPath))
		{
			if (!keeper_postgres_state_read(&(pgStatus->state),
											pgStatus->pgStatusPath))
			{
				/* errors have already been logged, will try again */
				continue;
			}

			if (!ensure_postgres_status(keeper, &postgresService))
			{
				/* TODO: review the error message, make sure startup.log is
				 * logged in case of failure to start Postgres
				 */
				pgStatusPathIsReady = false;
				log_warn("Will try again in 100ms");
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
 * The design choice here is in between re-using the state file for the FSM and
 * using a dedicated file or a PIPE to pass direct Postgres service commands
 * (start, stop, restart) to the sub-process.
 *
 * - using a PIPE makes the code complex: setting up the PIPE is ok, but then
 *   we need a reader and writer that talk to each other (so we need a command
 *   parser of sorts), and the reader itself needs to use a loop around
 *   select(2) to be able to also process signals, etc
 *
 * - using a dedicated file means that we now need to handle "stale" file
 *   staying around when one of the processes die and is restarted, and more
 *   generally that the dedicated command file(s) is always in sync with the
 *   FSM
 *
 * - using the state file from the FSM directly makes things overall simpler,
 *   the only drawback is that this is one more place where we need to
 *   implement the FSM semantics, it's not just the transition functions... it
 *   was never "just the transition functions" that said.
 */
static bool
ensure_postgres_status(Keeper *keeper, Service *postgres)
{
	KeeperStatePostgres *pgStatus = &(keeper->postgres.expectedPgStatus.state);

	switch (pgStatus->pgExpectedStatus)
	{
		case PG_EXPECTED_STATUS_UNKNOWN:
		{
			/* do nothing */
			return true;
		}

		case PG_EXPECTED_STATUS_STOPPED:
		{
			return ensure_postgres_status_stopped(keeper, postgres);
		}

		case PG_EXPECTED_STATUS_RUNNING:
		{
			return ensure_postgres_status_running(keeper, postgres);
		}
	}

	/* make compiler happy */
	return false;
}


/*
 * ensure_postgres_status_stopped ensures that Postgres is stopped.
 */
static bool
ensure_postgres_status_stopped(Keeper *keeper, Service *postgres)
{
	PostgresSetup *pgSetup = &(keeper->config.pgSetup);
	KeeperStateData *keeperState = &(keeper->state);

	bool pgIsNotRunningIsOk = true;
	bool pgIsRunning = pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk);

	if (pgIsRunning)
	{
		log_warn("PostgreSQL is running while in state \"%s\", "
				 "stopping PostgreSQL.",
				 NodeStateToString(keeperState->current_role));

		return service_postgres_stop((void *) postgres);
	}
	return true;
}


/*
 * ensure_postgres_status_running ensures that Postgres is running.
 */
static bool
ensure_postgres_status_running(Keeper *keeper, Service *postgres)
{
	PostgresSetup *pgSetup = &(keeper->config.pgSetup);

	/* we might still be starting-up */
	bool pgIsNotRunningIsOk = true;
	bool pgIsRunning = pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk);

	if (pgIsRunning)
	{
		return true;
	}

	if (service_postgres_start(postgres->context, &(postgres->pid)))
	{
		if (countPostgresStart > 1)
		{
			log_warn("PostgreSQL was not running, restarted with pid %d",
					 pgSetup->pidFile.pid);
		}
		return true;
	}
	else
	{
		log_warn("Failed to restart PostgreSQL, "
				 "see PostgreSQL logs for instance at \"%s\".",
				 pgSetup->pgdata);

		return false;
	}
	return true;
}
