/*
 * cli_formation.c
 *     Implementation of a CLI to manage a pg_auto_failover formation.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "formation_config.h"
#include "log.h"
#include "pgsetup.h"

static FormationConfig formationOptions;

static int keeper_cli_formation_getopts(int argc, char **argv);
static int keeper_cli_formation_create_getopts(int argc, char **argv);

static void keeper_cli_formation_create(int argc, char **argv);
static void keeper_cli_formation_drop(int argc, char **argv);

CommandLine create_formation_command =
	make_command("formation",
				 "Create a new formation on the pg_auto_failover monitor",
				 " [ --pgdata --formation --kind --dbname --with-secondary " \
				 "--without-secondary ] ",
				 "  --pgdata            path to data directory	 \n" \
				 "  --formation         name of the formation to create \n" \
				 "  --kind              formation kind, either \"pgsql\" or \"citus\" \n" \
				 "  --dbname            name for postgres database to use in this formation \n" \
				 "  --enable-secondary  create a formation that has multiple nodes that can be \n" \
				 "                      used for fail over when others have issues \n" \
				 "  --disable-secondary create a citus formation without nodes to fail over to \n",
				 keeper_cli_formation_create_getopts,
				 keeper_cli_formation_create);

CommandLine drop_formation_command =
	make_command("formation",
				 "Drop a formation on the pg_auto_failover monitor",
				 " [ --pgdata --formation ]",
				 "  --pgdata      path to data directory	 \n" \
				 "  --formation   name of the formation to drop \n",
				 keeper_cli_formation_getopts,
				 keeper_cli_formation_drop);


/*
 * keeper_cli_formation_getopts parses the command line options
 * necessary to describe an already existing formation
 */
int
keeper_cli_formation_getopts(int argc, char **argv)
{
	FormationConfig options = { 0 };
	int c = 0, option_index = 0, errors = 0;
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
			log_fatal("Failed to get PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	/* publish our option parsing in the global variable */
	formationOptions = options;

	return optind;
}


/*
 * keeper_cli_formation_create_getopts parses the command line options
 * necessary to create a new formation.
 */
int
keeper_cli_formation_create_getopts(int argc, char **argv)
{
	FormationConfig options = { 0 };

	int c = 0, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "kind", required_argument, NULL, 'k' },
		{ "dbname", required_argument, NULL, 'd' },
		{ "enable-secondary", no_argument, NULL, 's' },
		{ "disable-secondary", no_argument, NULL, 'S' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set defaults for formations */
	options.formationHasSecondary = true;

	while ((c = getopt_long(argc, argv, "D:f:k:sSVvqh",
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

			case 'k':
			{
				strlcpy(options.formationKind, optarg, NAMEDATALEN);
				log_trace("--kind %s", options.formationKind);
				break;
			}

			case 'd':
			{
				strlcpy(options.dbname, optarg, NAMEDATALEN);
				log_trace("--dbname %s", options.dbname);
				break;
			}

			case 's':
			{
				options.formationHasSecondary = true;
				log_trace("--enable-secondary");
				break;
			}

			case 'S':
			{
				options.formationHasSecondary = false;
				log_trace("--disable-secondary");
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

	if (IS_EMPTY_STRING_BUFFER(options.formation)
		|| IS_EMPTY_STRING_BUFFER(options.formationKind))
	{
		log_error("Options --formation and --kind are mandatory");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* --dbname is not provided, use default */
	if (IS_EMPTY_STRING_BUFFER(options.dbname))
	{
		log_debug("--dbname not provided, setting to \"%s\"",
				  DEFAULT_DATABASE_NAME);
		strlcpy(options.dbname, DEFAULT_DATABASE_NAME, NAMEDATALEN);
	}

	/* publish our option parsing in the global variable */
	formationOptions = options;

	return optind;
}


/*
 * keeper_cli_formation_create creates a new formation of a given kind in the
 * pg_auto_failover monitor.
 */
static void
keeper_cli_formation_create(int argc, char **argv)
{
	FormationConfig config = formationOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_create_formation(&monitor,
								  config.formation,
								  config.formationKind,
								  config.dbname,
								  config.formationHasSecondary))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Created formation \"%s\" of kind \"%s\" on the monitor, with secondary %s.",
			 config.formation, config.formationKind, config.formationHasSecondary ? "enabled" : "disabled");
}


/*
 * keeper_cli_formation_drop removes a formation in the pg_auto_failover monitor.
 */
static void
keeper_cli_formation_drop(int argc, char **argv)
{
	FormationConfig config = formationOptions;
	Monitor monitor = { 0 };

	if (IS_EMPTY_STRING_BUFFER(config.formation))
	{
		log_error("Options --formation is mandatory");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_drop_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Dropped formation \"%s\" on the monitor", config.formation);
}
