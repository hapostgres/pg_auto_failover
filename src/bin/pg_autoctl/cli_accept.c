/*
 * src/bin/pg_autoctl/cli_accept.c
 *     Implementation of the pg_autoctl accept CLI, used to resolve a
 *     detected timeline fork by pinning which lineage is ground truth.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "string_utils.h"

static int cli_accept_timeline_getopts(int argc, char **argv);
static void cli_accept_timeline(int argc, char **argv);

static int acceptTimelineTLI = -1;
static char acceptTimelineDecidedBy[BUFSIZE] = { 0 };

CommandLine accept_timeline_command =
	make_command("timeline",
				 "Accept a timeline as the ground truth after a detected fork",
				 " [ --pgdata --formation --group ] --tli <tli>",
				 "  --pgdata      path to data directory\n"
				 "  --formation   formation to target, defaults to 'default'\n"
				 "  --group       group to target, defaults to 0\n"
				 "  --tli         timeline to accept, as shown by "
				 "`pg_autoctl show timeline`\n"
				 "  --reason      free-text note explaining the decision\n",
				 cli_accept_timeline_getopts,
				 cli_accept_timeline);

CommandLine *accept_subcommands[] = {
	&accept_timeline_command,
	NULL,
};

CommandLine accept_commands =
	make_command_set("accept",
					 "Accept an operator decision orchestrated by the monitor",
					 NULL, NULL, NULL, accept_subcommands);


/*
 * cli_accept_timeline_getopts parses the command line options for the
 * command `pg_autoctl accept timeline`.
 */
static int
cli_accept_timeline_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "tli", required_argument, NULL, 't' },
		{ "reason", required_argument, NULL, 'r' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	options.groupId = -1;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:g:t:r:Vvqh",
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

			case 't':
			{
				if (!stringToInt(optarg, &acceptTimelineTLI) ||
					acceptTimelineTLI <= 0)
				{
					log_fatal("--tli argument is not a valid timeline id: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--tli %d", acceptTimelineTLI);
				break;
			}

			case 'r':
			{
				strlcpy(acceptTimelineDecidedBy, optarg, BUFSIZE);
				log_trace("--reason %s", acceptTimelineDecidedBy);
				break;
			}

			case 'V':
			{
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
				errors++;
			}
		}
	}

	if (acceptTimelineTLI <= 0)
	{
		log_fatal("Option --tli is mandatory");
		errors++;
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_connecting_to_monitor(options.monitor_pguri);
		}

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

	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_accept_timeline calls the SQL function pgautofailover.accept_timeline()
 * on the monitor.
 */
static void
cli_accept_timeline(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);
	(void) cli_set_groupId(&monitor, &config);

	if (!monitor_accept_timeline(&monitor, config.formation, config.groupId,
								 acceptTimelineTLI, acceptTimelineDecidedBy))
	{
		log_fatal("Failed to accept timeline %d for formation \"%s\" group %d, "
				  "see above for details",
				  acceptTimelineTLI, config.formation, config.groupId);
		exit(EXIT_CODE_MONITOR);
	}

	fformat(stdout,
			"Timeline %d accepted as ground truth for formation \"%s\" "
			"group %d. The election will now only consider nodes on that "
			"lineage; other nodes need pg_rewind before rejoining.\n",
			acceptTimelineTLI, config.formation, config.groupId);
}
