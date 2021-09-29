/*
 * src/bin/pg_autoctl/cli_watch.c
 *     Implementation of a CLI to show events, states, and URI from the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>

#include <ncurses.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "monitor.h"
#include "nodestate_utils.h"
#include "parsing.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pidfile.h"
#include "state.h"
#include "string_utils.h"
#include "watch.h"

static int cli_watch_getopts(int argc, char **argv);
static void cli_watch(int argc, char **argv);

CommandLine watch_command =
	make_command("watch",
				 "Display a dashboard to watch monitor's events and state",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --monitor     show the monitor uri\n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --json        output data in the JSON format\n",
				 cli_watch_getopts,
				 cli_watch);

static int
cli_watch_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
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
	}
	else
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
	}

	/* when --pgdata is given, still initialise our pathnames */
	if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
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
 * cli_watch starts a ncurses dashboard that displays relevant information
 * about a running formation at a given monitor.
 */
static void
cli_watch(int argc, char **argv)
{
	WatchContext context = { 0 };
	KeeperConfig config = keeperOptions;

	(void) cli_monitor_init_from_option_or_config(&(context.monitor), &config);

	strlcpy(context.formation, config.formation, sizeof(context.formation));
	context.groupId = config.groupId;

	(void) cli_watch_main_loop(&context);
}
