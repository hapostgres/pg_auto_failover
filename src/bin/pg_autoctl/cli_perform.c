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
#include "string_utils.h"

static int cli_perform_failover_getopts(int argc, char **argv);
static void cli_perform_failover(int argc, char **argv);
static void cli_perform_promotion(int argc, char **argv);

CommandLine perform_failover_command =
	make_command("failover",
				 "Perform a failover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n" \
				 "  --formation   formation to target, defaults to 'default' \n" \
				 "  --group       group to target, defaults to 0 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine perform_switchover_command =
	make_command("switchover",
				 "Perform a switchover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n" \
				 "  --formation   formation to target, defaults to 'default' \n" \
				 "  --group       group to target, defaults to 0 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine perform_promotion_command =
	make_command("promotion",
				 "Perform a failover that promotes a target node",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n" \
				 "  --formation   formation to target, defaults to 'default' \n" \
				 "  --name        node name to target, defaults to current node \n",
				 cli_get_name_getopts,
				 cli_perform_promotion);

CommandLine *perform_subcommands[] = {
	&perform_failover_command,
	&perform_switchover_command,
	&perform_promotion_command,
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
	options.groupId = -1;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	/* do not set a default formation, it should be found in the config file */

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
				if (!stringToInt(optarg, &options.groupId))
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
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
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

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
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
	int groupsCount = 0;
	PgInstanceKind nodeKind = NODE_KIND_UNKNOWN;

	char *channels[] = { "state", NULL };

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
												 config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_count_groups(&monitor, config.formation, &groupsCount))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (groupsCount == 0)
	{
		/* nothing to be done here */
		log_fatal("The monitor currently has no Postgres nodes "
				  "registered in formation \"%s\"",
				  config.formation);
		exit(EXIT_CODE_BAD_STATE);
	}

	/*
	 * When --group was not given, we may proceed when there is only one
	 * possible target group in the formation, which is the case with Postgres
	 * standalone setups.
	 */
	if (config.groupId == -1)
	{
		/*
		 * When --group is not given and we have a keeper node, we can grab a
		 * default from the configuration file.
		 */
		pgAutoCtlNodeRole role =
			ProbeConfigurationFileRole(config.pathnames.config);

		if (role == PG_AUTOCTL_ROLE_KEEPER)
		{
			const bool missingPgdataIsOk = true;
			const bool pgIsNotRunningIsOk = true;
			const bool monitorDisabledIsOk = false;

			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			log_info("Targetting group %d in formation \"%s\"",
					 config.groupId,
					 config.formation);

			nodeKind = config.pgSetup.pgKind;
		}
		else
		{
			if (groupsCount == 1)
			{
				/* we have only one group, it's group number zero, proceed */
				config.groupId = 0;
				nodeKind = NODE_KIND_STANDALONE;
			}
			else
			{
				log_error("Please use the --group option to target a "
						  "specific group in formation \"%s\"",
						  config.formation);
				exit(EXIT_CODE_BAD_ARGS);
			}
		}
	}

	/* start listening to the state changes before we call perform_failover */
	if (!pgsql_listen(&(monitor.pgsql), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_perform_failover(&monitor, config.formation, config.groupId))
	{
		log_fatal("Failed to perform failover/switchover, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	/* process state changes notification until we have a new primary */
	if (!monitor_wait_until_some_node_reported_state(&monitor,
													 config.formation,
													 config.groupId,
													 nodeKind,
													 PRIMARY_STATE))
	{
		log_error("Failed to wait until a new primary has been notified");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_perform_promotion calls the function pgautofailover.perform_promotion()
 * on the monitor.
 */
static void
cli_perform_promotion(int argc, char **argv)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperConfig *config = &(keeper.config);

	int groupId = 0;

	PgInstanceKind nodeKind = NODE_KIND_UNKNOWN;

	char *channels[] = { "state", NULL };

	keeper.config = keeperOptions;

	if (!monitor_init_from_pgsetup(monitor, &keeper.config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* grab --name from either the command options or the configuration file */
	(void) cli_ensure_node_name(&keeper);

	if (!monitor_get_groupId_from_name(monitor,
									   config->formation,
									   config->name,
									   &groupId))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* start listening to the state changes before we call perform_promotion */
	if (!pgsql_listen(&(monitor->pgsql), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * pgautofailover.perform_promotion returns true when a promotion has been
	 * triggered, and false when none was necessary. When an error occurs, it
	 * reports an error condition, which is logged about already.
	 */
	if (monitor_perform_promotion(monitor, config->formation, config->name))
	{
		/* process state changes notification until we have a new primary */
		if (!monitor_wait_until_some_node_reported_state(monitor,
														 config->formation,
														 groupId,
														 nodeKind,
														 PRIMARY_STATE))
		{
			log_error("Failed to wait until a new primary has been notified");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}
