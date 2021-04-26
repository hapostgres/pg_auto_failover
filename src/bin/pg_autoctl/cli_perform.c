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

static int cli_perform_promotion_getopts(int argc, char **argv);
static void cli_perform_promotion(int argc, char **argv);

CommandLine perform_failover_command =
	make_command("failover",
				 "Perform a failover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory\n"
				 "  --formation   formation to target, defaults to 'default'\n"
				 "  --group       group to target, defaults to 0\n"
				 "  --wait        how many seconds to wait, default to 60 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine perform_switchover_command =
	make_command("switchover",
				 "Perform a switchover for given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory\n"
				 "  --formation   formation to target, defaults to 'default'\n"
				 "  --group       group to target, defaults to 0\n"
				 "  --wait        how many seconds to wait, default to 60 \n",
				 cli_perform_failover_getopts,
				 cli_perform_failover);

CommandLine perform_promotion_command =
	make_command("promotion",
				 "Perform a failover that promotes a target node",
				 " [ --pgdata --formation --group ] --name <node name>",
				 "  --pgdata      path to data directory\n"
				 "  --formation   formation to target, defaults to 'default' \n"
				 "  --name        node name to target, defaults to current node\n"
				 "  --wait        how many seconds to wait, default to 60 \n",
				 cli_perform_promotion_getopts,
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
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "wait", required_argument, NULL, 'w' },
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
	options.listen_notifications_timeout =
		PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT;

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

			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
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

			case 'w':
			{
				if (!stringToInt(optarg, &options.listen_notifications_timeout))
				{
					log_fatal("--wait argument is not a valid timeout: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--wait %d", options.listen_notifications_timeout);
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

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);
		}

		/* the rest of the program needs pgdata actually empty */
		bzero((void *) options.pgSetup.pgdata, sizeof(options.pgSetup.pgdata));
	}
	else
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
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

	char *channels[] = { "state", NULL };

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

	(void) cli_set_groupId(&monitor, &config);

	/* start listening to the state changes before we call perform_failover */
	if (!pgsql_listen(&(monitor.notificationClient), channels))
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
	if (!monitor_wait_until_some_node_reported_state(
			&monitor,
			config.formation,
			config.groupId,
			config.pgSetup.pgKind,
			PRIMARY_STATE,
			config.listen_notifications_timeout))
	{
		log_error("Failed to wait until a new primary has been notified");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_perform_promotion_getopts parses the command line options for the
 * command `pg_autoctl perform promotion` command.
 */
static int
cli_perform_promotion_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "name", required_argument, NULL, 'a' },
		{ "wait", required_argument, NULL, 'w' },
		{ "json", no_argument, NULL, 'J' },
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
	options.listen_notifications_timeout =
		PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT;

	optind = 0;

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

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

			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'a':
			{
				/* { "name", required_argument, NULL, 'a' }, */
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
				break;
			}

			case 'w':
			{
				/* { "wait", required_argument, NULL, 'w' }, */
				if (!stringToInt(optarg, &options.listen_notifications_timeout))
				{
					log_fatal("--wait argument is not a valid timeout: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--wait %d", options.listen_notifications_timeout);
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

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
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

	/* now that we have the command line parameters, prepare the options */
	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);

			/* the rest of the program needs pgdata actually empty */
			bzero((void *) options.pgSetup.pgdata,
				  sizeof(options.pgSetup.pgdata));
		}
	}
	else
	{
		(void) prepare_keeper_options(&options);
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
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

	(void) cli_monitor_init_from_option_or_config(monitor, config);

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
	if (!pgsql_listen(&(monitor->notificationClient), channels))
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
		if (!monitor_wait_until_some_node_reported_state(
				monitor,
				config->formation,
				groupId,
				nodeKind,
				PRIMARY_STATE,
				config->listen_notifications_timeout))
		{
			log_error("Failed to wait until a new primary has been notified");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}
