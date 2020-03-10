/*
 * src/bin/pg_autoctl/cli_perform.c
 *     Implementation of the pg_autoctl perform CLI for the pg_auto_failover
 *     nodes (monitor, coordinator, worker, postgres).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "ini_file.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"

static int cli_perform_failover_getopts(int argc, char **argv);
static void cli_perform_failover(int argc, char **argv);

CommandLine perform_failover_command =
	make_command("failover",
				 "Perform a failover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"		\
				 "  --formation   formation to target, defaults to 'default' \n" \
				 "  --group       group to target, defaults to 0 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine perform_switchover_command =
	make_command("switchover",
				 "Perform a switchover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"		\
				 "  --formation   formation to target, defaults to 'default' \n" \
				 "  --group       group to target, defaults to 0 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine *perform_subcommands[] = {
	&perform_failover_command,
	&perform_switchover_command,
	NULL,
};

CommandLine perform_commands =
	make_command_set("perform", "Perform an action orchestrated by the monitor",
					 NULL, NULL, NULL, perform_subcommands);


/*
 * cli_perform_failover_getopts parses the command line options for the
 * command `pg_autoctl perform failover`.
 */
static int
cli_perform_failover_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* set default values for our options, when we have some */
	options.groupId = 0;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	strlcpy(options.formation, "default", NAMEDATALEN);

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:g:n:Vvqh",
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

			case 'g':
			{
				options.groupId = strtol(optarg, NULL, 10);
				if (errno  != 0)
				{
					log_fatal("--group argument is not a valid group ID: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--group %d", options.groupId);
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
		int pgdatalen = get_env_variable("PGDATA", options.pgSetup.pgdata, MAXPGPATH);
		if (pgdatalen == -1 || pgdatalen >= MAXPGPATH)
		{
			log_fatal("Failed to get PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_perform_failover calls the SQL function
 * pgautofailover.perform_failover() on the monitor.
 */
static void
cli_perform_failover(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_perform_failover(&monitor, config.formation, config.groupId))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}
}
