/*
 * src/bin/pg_autoctl/cli_do_service.c
 *     Implementation of a CLI for controlling the pg_autoctl service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "service.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "supervisor.h"


static void cli_do_service_postgres(int argc, char **argv);
static void cli_do_service_pgcontroler(int argc, char **argv);
static void cli_do_service_postgresctl_on(int argc, char **argv);
static void cli_do_service_postgresctl_off(int argc, char **argv);

CommandLine service_pgcontroler =
	make_command("pgcontroler",
				 "pg_autoctl supervised postgres controler",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_pgcontroler);

CommandLine service_postgres =
	make_command("postgres",
				 "pg_autoctl service that start/stop postgres when asked",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgres);

static CommandLine *service[] = {
	&service_pgcontroler,
	&service_postgres,
	NULL
};

CommandLine do_service_commands =
	make_command_set("service",
					 "Run pg_autoctl sub-processes (services)", NULL, NULL,
					 NULL, service);


CommandLine service_postgres_ctl_on =
	make_command("on",
				 "Signal pg_autoctl postgres service to ensure Postgres is running",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgresctl_on);


CommandLine service_postgres_ctl_off =
	make_command("off",
				 "Signal pg_autoctl postgres service to ensure Postgres is stopped",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgresctl_off);

static CommandLine *pgctl[] = {
	&service_postgres_ctl_on,
	&service_postgres_ctl_off,
	NULL
};

CommandLine do_service_postgres_ctl_commands =
	make_command_set("pgctl",
					 "Signal the pg_autoctl postgres service", NULL, NULL,
					 NULL, pgctl);


/*
 * cli_do_pgcontroler starts the process controller service within a
 * supervision tree. It is used for debug purposes only. When using this entry
 * point we have a supervisor process that is responsible for only one service:
 *
 *  pg_autoctl do service pgcontroler
 *   - pg_autoctl do service postgres
 *     - postgres
 */
static void
cli_do_service_pgcontroler(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };

	Service subprocesses[] = {
		"postgres",
		RP_PERMANENT,
		-1,
		&service_postgres_ctl_start,
		(void *) &(postgres.postgresSetup)
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	bool exitOnQuit = true;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!supervisor_start(subprocesses, subprocessesCount, pathnames.pid))
	{
		log_fatal("Failed to start the supervisor, see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_service_postgres starts the process service. This is intended to be
 * used from the supervisor process tree itself. Then we have a main process
 * that supervises two sub-processes, one of them is cli_do_service_postgres:
 *
 *  pg_autoctl
 *   - pg_autoctl do service postgres
 *     - postgres
 *   - pg_autoctl do service keeper|monitor
 */
static void
cli_do_service_postgres(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };

	bool exitOnQuit = true;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: start/stop postgres");

	(void) service_postgres_ctl_loop(&postgres);
}


/*
 * cli_do_service_postgresctl_on asks the pg_autoctl Postgres controler service
 * to ensure that Postgres is running.
 */
static void
cli_do_service_postgresctl_on(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	(void) local_postgres_init(&postgres, pgSetup);

	if (!ensure_postgres_service_is_running(&postgres))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_info("Postgres is serving PGDATA \"%s\" on port %d with pid %d",
			 pgSetup->pgdata, pgSetup->pgport, pgSetup->pidFile.pid);

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();

		if (!pg_setup_as_json(pgSetup, js))
		{
			/* can't happen */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		(void) cli_pprint_json(js);
	}
}


/*
 * cli_do_service_postgresctl_on asks the pg_autoctl Postgres controler service
 * to ensure that Postgres is stopped.
 */
static void
cli_do_service_postgresctl_off(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	(void) local_postgres_init(&postgres, pgSetup);

	if (!ensure_postgres_service_is_stopped(&postgres))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_info("Postgres has been stopped for PGDATA \"%s\"", pgSetup->pgdata);
}
