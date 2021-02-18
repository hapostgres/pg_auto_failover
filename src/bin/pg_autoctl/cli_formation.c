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
#include "env_utils.h"
#include "formation_config.h"
#include "log.h"
#include "pgsetup.h"

static FormationConfig formationOptions;

static bool cli_formation_use_monitor_option(FormationConfig *options);
static int keeper_cli_formation_getopts(int argc, char **argv);
static int keeper_cli_formation_create_getopts(int argc, char **argv);

static void keeper_cli_formation_create(int argc, char **argv);
static void keeper_cli_formation_drop(int argc, char **argv);


CommandLine create_formation_command =
	make_command("formation",
				 "Create a new formation on the pg_auto_failover monitor",
				 " [ --pgdata --monitor --formation --kind --dbname "
				 " --with-secondary --without-secondary ] ",
				 "  --pgdata      path to data directory\n"
				 "  --monitor     pg_auto_failover Monitor Postgres URL\n"
				 "  --formation   name of the formation to create \n"
				 "  --kind        formation kind, either \"pgsql\" or \"citus\"\n"
				 "  --dbname      name for postgres database to use in this formation \n"
				 "  --enable-secondary     create a formation that has multiple nodes that can be \n"
				 "                         used for fail over when others have issues \n"
				 "  --disable-secondary    create a citus formation without nodes to fail over to \n"
				 "  --number-sync-standbys minimum number of standbys to confirm write \n",
				 keeper_cli_formation_create_getopts,
				 keeper_cli_formation_create);

CommandLine drop_formation_command =
	make_command("formation",
				 "Drop a formation on the pg_auto_failover monitor",
				 " [ --pgdata --formation ]",
				 "  --pgdata      path to data directory	 \n" \
				 "  --monitor     pg_auto_failover Monitor Postgres URL\n"
				 "  --formation   name of the formation to drop \n",
				 keeper_cli_formation_getopts,
				 keeper_cli_formation_drop);


/*
 * cli_formation_use_monitor_option returns true when the --monitor option
 * should be used, or when PG_AUTOCTL_MONITOR has been set in the environment.
 * In that case the options->monitor_pguri is also set to the value found in
 * the environment.
 *
 * See cli_use_monitor_option() for the general KeeperConfig version of the
 * same function.
 */
static bool
cli_formation_use_monitor_option(FormationConfig *options)
{
	/* if --monitor is used, then use it */
	if (!IS_EMPTY_STRING_BUFFER(options->monitor_pguri))
	{
		return true;
	}

	/* otherwise, have a look at the PG_AUTOCTL_MONITOR environment variable */
	if (env_exists(PG_AUTOCTL_MONITOR) &&
		get_env_copy(PG_AUTOCTL_MONITOR,
					 options->monitor_pguri,
					 sizeof(options->monitor_pguri)))
	{
		log_debug("Using environment PG_AUTOCTL_MONITOR \"%s\"",
				  options->monitor_pguri);
		return true;
	}

	/*
	 * Still nothing? well don't use --monitor then.
	 *
	 * Now, on commands that are compatible with using just a monitor and no
	 * local pg_autoctl node, we want to include an error message about the
	 * lack of a --monitor when we also lack --pgdata.
	 */
	if (IS_EMPTY_STRING_BUFFER(options->pgSetup.pgdata) &&
		!env_exists("PGDATA"))
	{
		log_error("Failed to get value for environment variable '%s', "
				  "which is unset", PG_AUTOCTL_MONITOR);
		log_warn("This command also supports the --monitor option, which "
				 "is not used here");
	}

	return false;
}


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
		{ "monitor", required_argument, NULL, 'm' },
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
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_formation_use_monitor_option(&options))
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
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
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
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "kind", required_argument, NULL, 'k' },
		{ "dbname", required_argument, NULL, 'd' },
		{ "enable-secondary", no_argument, NULL, 's' },
		{ "disable-secondary", no_argument, NULL, 'S' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ "number-sync-standbys", required_argument, NULL, 'n' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set defaults for formations */
	options.formationHasSecondary = true;

	while ((c = getopt_long(argc, argv, "D:f:k:sSVvqhn:",
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

			case 'n':
			{
				/* { "number-sync-standbys", required_argument, NULL, 'n'} */
				int numberSyncStandbys = strtol(optarg, NULL, 10);

				if (errno == EINVAL || numberSyncStandbys < 0)
				{
					log_fatal("--number-sync-standbys argument is not valid."
							  " Use a non-negative integer value.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				options.numberSyncStandbys = numberSyncStandbys;
				log_trace("--number-sync-standbys %d", numberSyncStandbys);
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

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_formation_use_monitor_option(&options))
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
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
	}

	if (IS_EMPTY_STRING_BUFFER(options.formation) ||
		IS_EMPTY_STRING_BUFFER(options.formationKind))
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

	if (IS_EMPTY_STRING_BUFFER(config.monitor_pguri))
	{
		if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
	else
	{
		if (!monitor_init(&monitor, config.monitor_pguri))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (!monitor_create_formation(&monitor,
								  config.formation,
								  config.formationKind,
								  config.dbname,
								  config.formationHasSecondary,
								  config.numberSyncStandbys))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Created formation \"%s\" of kind \"%s\" on the monitor, with secondary %s.",
			 config.formation, config.formationKind, config.formationHasSecondary ?
			 "enabled" : "disabled");
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

	if (IS_EMPTY_STRING_BUFFER(config.monitor_pguri))
	{
		if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
	else
	{
		if (!monitor_init(&monitor, config.monitor_pguri))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (!monitor_drop_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Dropped formation \"%s\" on the monitor", config.formation);
}
