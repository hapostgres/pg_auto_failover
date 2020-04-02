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
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgsetup.h"
#include "service.h"
#include "service_postgres.h"
#include "signals.h"
#include "state.h"
#include "string_utils.h"

static int countPostgresStart = 0;

static void service_postgres_fsm_loop(Keeper *keeper);

static bool ensure_postgres_status(Keeper *keeper, Service *postgres);
static bool ensure_postgres_status_stopped(Keeper *keeper, Service *postgres);
static bool ensure_postgres_status_running(Keeper *keeper, Service *postgres);


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


/*
 * service_postgres_fsm_start starts a subprocess that implements the postgres
 * service depending on the current assigned and goal state of the keeper.
 */
bool
service_postgres_fsm_start(void *context, pid_t *pid)
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
			log_error("Failed to fork the postgres FSM supervisor process");
			return false;
		}

		case 0:
		{
			(void) set_ps_title("postgres fsm");
			(void) service_postgres_fsm_loop(keeper);
		}

		default:
		{
			log_debug(
				"pg_autoctl started postgres FSM supervisor in subprocess %d",
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
service_postgres_fsm_stop(void *context)
{
	Service *service = (Service *) context;
	Keeper *keeper = (Keeper *) service->context;
	PostgresSetup *pgSetup = &(keeper->config.pgSetup);

	log_info("Stopping pg_autoctl postgres fsm supervisor service");

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
 * service_postgres_fsm_loop loops over the current FSM state file and ensure
 * that Postgres is running when that's expected, or that Postgres is not
 * running when in a state where we should maintain Postgres down to avoid
 * split-brain situations.
 */
static void
service_postgres_fsm_loop(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	Service postgres = {
		"postgres",
		-1,
		&service_postgres_start,
		&service_postgres_stop,
		(void *) pgSetup
	};

	InitStage currentInitStage = INIT_STAGE_UNKNOW;
	bool monitorDisabledIsOk = false;

	/*
	 * Initialize our keeper struct instance with values from the setup.
	 */
	if (!keeper_config_read_file_skip_pgsetup(config, monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * Keep track of the current init stage so that we don't start Postgres at
	 * every loop iteration, but only the first time we reach INIT_STAGE_2.
	 */
	if (file_exists(keeper->config.pathnames.init))
	{
		if (!keeper_init_state_read(keeper))
		{
			log_fatal("Failed to start our Postgres FSM loop, "
					  "see above for details");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	currentInitStage = keeper->initState.initStage;

	for (;;)
	{
		/* that's expected the shutdown sequence from the supervisor */
		if (asked_to_stop || asked_to_stop_fast)
		{
			exit(EXIT_CODE_QUIT);
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
		if (file_exists(config->pathnames.init))
		{
			if (!keeper_init_state_read(keeper))
			{
				/* errors have already been logged, will try again */
				continue;
			}

			if (keeper->initState.initStage != currentInitStage &&
				keeper->initState.initStage == INIT_STAGE_2)
			{
				currentInitStage = keeper->initState.initStage;

				log_info("pg_autoctl init has reached stage %d, "
						 "starting Postgres", currentInitStage);

				if (!service_postgres_start(postgres.context, &(postgres.pid)))
				{
					log_warn("Will try again in 100ms");
					continue;
				}
			}
		}
		else if (file_exists(config->pathnames.state))
		{
			if (!keeper_load_state(keeper))
			{
				/* errors have already been logged, will try again */
				continue;
			}

			if (!ensure_postgres_status(keeper, &postgres))
			{
				/* TODO: review the error message, make sure startup.log is
				 * logged in case of failure to start Postgres
				 */
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
	KeeperStateData *keeperState = &(keeper->state);

	log_debug("Ensure postgres status(%s,%s)",
			  NodeStateToString(keeperState->current_role),
			  NodeStateToString(keeperState->assigned_role));

	/*
	 * The KeeperFSM instructs us of the expected Postgres status in case of a
	 * transition. We still need to have make a decision when the assigned and
	 * current roles are the same and no transition is needed, which is most of
	 * the time.
	 *
	 * We have 3 states where we don't want Postgres to be running, in order to
	 * avoid split-brains on the (old) primary instance: draining, demoted, and
	 * demoted_timeout. In all the other cases, we want Postgres to be running.
	 *
	 * During the initialisation procedure (pg_autoctl create postgres) we have
	 * two stages. In INIT_STAGE_1 we're not ready to run Postgres yet (we
	 * didn't initdb nor created our setup). In INIT_STAGE_2 Postgres is
	 * expected to be running.
	 */
	if (keeperState->current_role == keeperState->assigned_role)
	{
		switch (keeperState->assigned_role)
		{
			case NO_STATE:
			case INIT_STATE:
			case MAINTENANCE_STATE:
			{
				/* do nothing */
				return true;
			}

			case DRAINING_STATE:
			case DEMOTED_STATE:
			case DEMOTE_TIMEOUT_STATE:
			{
				return ensure_postgres_status_stopped(keeper, postgres);
			}

			default:
			{
				return ensure_postgres_status_running(keeper, postgres);
			}
		}
	}
	else
	{
		ExpectedPostgresStatus pgStatus = keeper_fsm_get_pgstatus(keeper);

		switch (pgStatus)
		{
			case PGSTATUS_UNKNOWN:
			{
				log_debug("Expected Postgres status is unknown");
				return true;
			}

			case PGSTATUS_INIT:
			{
				if (!keeper_init_state_read(keeper))
				{
					log_error("Failed to read expected Postgres service status, "
							  "see above for details");
					return false;
				}

				if (keeper->initState.initStage == INIT_STAGE_2)
				{
					return ensure_postgres_status_running(keeper, postgres);
				}
			}

			case PGSTATUS_STOPPED:
			{
				return ensure_postgres_status_stopped(keeper, postgres);
			}

			case PGSTATUS_RUNNING:
			{
				return ensure_postgres_status_running(keeper, postgres);
			}
		}
	}

	/* should never happen */
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

	log_trace("ensure_postgres_status_running: %s",
			  pgIsRunning ? "pg is running" : "pg is not running");

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
