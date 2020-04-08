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
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "service.h"
#include "service_keeper.h"
#include "service_monitor.h"
#include "service_postgres.h"
#include "signals.h"

static void cli_do_service_postgres(int argc, char **argv);
static void cli_do_service_node_active(int argc, char **argv);
static void cli_do_service_monitor(int argc, char **argv);

CommandLine service_postgres =
	make_command("postgres",
				 "pg_autoctl service that start/stop postgres when needed",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgres);

CommandLine service_node_active =
	make_command("node-active",
				 "pg_autoctl service that implements the node active protocol",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_node_active);

CommandLine service_monitor =
	make_command("monitor",
				 "pg_autoctl service that listens to the monitor notifications",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_monitor);

static CommandLine *service[] = {
	&service_postgres,
	&service_node_active,
	&service_monitor,
	NULL
};

CommandLine do_service_commands =
	make_command_set("service",
					 "Run pg_autoctl sub-processes (services)", NULL, NULL,
					 NULL, service);


/*
 * cli_do_service_postgres starts the process service.
 */
static void
cli_do_service_postgres(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: start/stop postgres");

	(void) service_postgres_fsm_loop(&keeper);
}


/*
 * cli_do_service_node_active starts the node active service.
 */
static void
cli_do_service_node_active(int argc, char **argv)
{
	Keeper keeper = { 0 };

	pid_t ppid = getppid();

	keeper.config = keeperOptions;

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	if (!service_keeper_node_active_init(&keeper))
	{
		log_fatal("Failed to initialise the node active service, "
				  "see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: node active");

	(void) keeper_node_active_loop(&keeper, ppid);
}


/*
 * cli_do_service_monitor starts the monitor service.
 */
static void
cli_do_service_monitor(int argc, char **argv)
{
	KeeperConfig options = keeperOptions;

	Monitor monitor = { 0 };
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	/* Prepare MonitorConfig from the CLI options fed in options */
	if (!monitor_config_init_from_pgsetup(&(monitor.config),
										  &options.pgSetup,
										  missingPgdataIsOk,
										  pgIsNotRunningIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGCTL);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: monitor listener");

	/* Start the monitor service */
	(void) monitor_service_run(&monitor);
}
