/*
 * cli_enable_disable.c
 *     Implementation of pg_autoctl enable and disable CLI sub-commands.
 *     Current features that can be enabled and their scope are:
 *      - secondary (scope: formation)
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "cli_common.h"
#include "commandline.h"
#include "fsm.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "pgsetup.h"


static int cli_secondary_getopts(int argc, char **argv);
static void cli_enable_secondary(int argc, char **argv);
static void cli_disable_secondary(int argc, char **argv);

static void cli_enable_maintenance(int argc, char **argv);
static void cli_disable_maintenance(int argc, char **argv);

static CommandLine enable_secondary_command =
	make_command("secondary",
				 "Enable secondary nodes on a formation",
				 " [ --pgdata --formation ] ",
				 "  --pgdata      path to data directory\n" \
				 "  --formation   Formation to enable secondary on\n",
				 cli_secondary_getopts,
				 cli_enable_secondary);

static CommandLine disable_secondary_command =
	make_command("secondary",
				 "Disable secondary nodes on a formation",
				 " [ --pgdata --formation ] ",
				 "  --pgdata      path to data directory\n" \
				 "  --formation   Formation to disable secondary on\n",
				 cli_secondary_getopts,
				 cli_disable_secondary);

static CommandLine enable_maintenance_command =
	make_command("maintenance",
				 "Enable Postgres maintenance mode on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_enable_maintenance);

static CommandLine disable_maintenance_command =
	make_command("maintenance",
				 "Disable Postgres maintenance mode on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_disable_maintenance);

static CommandLine *enable_subcommands[] = {
	&enable_secondary_command,
	&enable_maintenance_command,
	NULL
};

static CommandLine *disable_subcommands[] = {
	&disable_secondary_command,
	&disable_maintenance_command,
	NULL
};


CommandLine enable_commands =
	make_command_set("enable",
					 "Enable a feature on a formation", NULL, NULL,
					 NULL, enable_subcommands);

CommandLine disable_commands =
	make_command_set("disable",
					 "Disable a feature on a formation", NULL, NULL,
					 NULL, disable_subcommands);


/*
 * cli_secondary_getopts parses command line options for the secondary feature,
 * both during enable and disable. Little verification is performed however the
 * function will error when no --pgdata or --formation are provided, existance
 * of either are not verified.
 */
static int
cli_secondary_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
						log_set_level(LOG_INFO);
						break;

					case 2:
						log_set_level(LOG_DEBUG);
						break;

					default:
						log_set_level(LOG_TRACE);
						break;
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to set PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	if (IS_EMPTY_STRING_BUFFER(options.formation))
	{
		log_error("Option --formation is mandatory");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_enable_secondary enables secondaries on the specified formation.
 */
static void
cli_enable_secondary(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_enable_secondary_for_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Enabled secondaries for formation \"%s\", make sure to add "
			 "worker nodes to the formation to have secondaries ready "
			 "for failover.",
			 config.formation);
}


/*
 * cli_disable_secondary disables secondaries on the specified formation.
 */
static void
cli_disable_secondary(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * disabling secondaries on a formation happens on the monitor. When the
	 * formation is still operating with secondaries an error will be logged
	 * and the function will return with a false value. As we will exit the
	 * successful info message below is only printed if secondaries on the
	 * formation have been disabled successfully.
	 */
	if (!monitor_disable_secondary_for_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Disabled secondaries for formation \"%s\".", config.formation);
}


/*
 * cli_enable_maintenance calls the pgautofailover.start_maintenance() function
 * on the monitor for the local node.
 */
static void
cli_enable_maintenance(int argc, char **argv)
{
	Keeper keeper = { 0 };
	pid_t pid;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_start_maintenance(&(keeper.monitor),
								   keeper.config.nodename,
								   keeper.config.pgSetup.pgport))
	{
		log_fatal("Failed to start maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		log_error("Failed to read the keeper's PID file at \"%s\": "
				  "is the keeper running?", keeper.config.pathnames.pid);
		exit(EXIT_CODE_KEEPER);
	}

	log_warn("Signaling the keeper process %d with SIGHUP so that  "
			 "it calls pgautofailover.node_active() immediately.",
			 pid);

	if (kill(pid, SIGHUP) != 0)
	{
		log_warn("Failed to send SIGHUP to the keeper's pid %d: %s",
				  pid, strerror(errno));
	}

	log_info("Node %s:%d will reach maintenance state soon",
			 keeper.config.nodename, keeper.config.pgSetup.pgport);
}


/*
 * cli_disable_maintenance calls pgautofailver.stop_maintenance(name, port) on
 * the monitor.
 */
static void
cli_disable_maintenance(int argc, char **argv)
{
	Keeper keeper = { 0 };
	pid_t pid;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_stop_maintenance(&(keeper.monitor),
								  keeper.config.nodename,
								  keeper.config.pgSetup.pgport))
	{
		log_fatal("Failed to stop maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		log_error("Failed to read the keeper's PID file at \"%s\": "
				  "is the keeper running?", keeper.config.pathnames.pid);
		exit(EXIT_CODE_KEEPER);
	}

	log_warn("Signaling the keeper process %d with SIGHUP so that  "
			 "it calls pgautofailover.node_active() immediately.", pid);

	if (kill(pid, SIGHUP) != 0)
	{
		log_warn("Failed to send SIGHUP to the keeper's pid %d: %s",
				  pid, strerror(errno));
	}

	log_info("Node %s:%d will exit from maintenance state soon",
			 keeper.config.nodename, keeper.config.pgSetup.pgport);
}
