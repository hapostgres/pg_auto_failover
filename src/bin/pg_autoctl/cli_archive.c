/*
 * src/bin/pg_autoctl/cli_archive.c
 *     Implementation of the pg_autoctl archive commands (archiving WAL files
 *     and pgdata, aka base backups).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>

#include "postgres_fe.h"

#include "archiving.h"
#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "keeper_pg_init.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "pgctl.h"
#include "pghba.h"
#include "pidfile.h"
#include "primary_standby.h"
#include "service_keeper.h"
#include "service_keeper_init.h"
#include "service_monitor.h"
#include "service_monitor_init.h"
#include "string_utils.h"
#include "wal-g.h"

char configFilename[MAXPGPATH] = { 0 };

static int cli_archive_getopts(int argc, char **argv);

static void cli_archive_wal(int argc, char **argv);
static void cli_archive_pgdata(int argc, char **argv);
static void cli_archive_show(int argc, char **argv);

CommandLine archive_wal_command =
	make_command(
		"wal",
		"Archive a WAL file",
		" [ --pgdata ] [ --config ] [ --json ] filename",
		"  --pgdata      path to data directory\n"
		"  --config      archive command configuration\n"
		"  --json        output data in the JSON format\n",
		cli_archive_getopts,
		cli_archive_wal);

CommandLine archive_pgdata_command =
	make_command(
		"pgdata",
		"Archive a PGDATA directory (a base backup)",
		" [ --pgdata ] [ --config ] [ --json ] filename",
		"  --pgdata      path to data directory\n"
		"  --config      archive command configuration\n"
		"  --json        output data in the JSON format\n",
		cli_archive_getopts,
		cli_archive_pgdata);

CommandLine archive_show_command =
	make_command(
		"show",
		"Show archives (basebackups and WAL files)",
		" [ --pgdata | --monitor ] [ --formation ] [ --json ]",
		"  --pgdata      path to data directory\n"
		"  --monitor     pg_auto_failover Monitor Postgres URL\n"
		"  --formation   archive WAL for given formation\n"
		"  --name        pg_auto_failover node name\n"
		"  --json        output data in the JSON format\n",
		cli_archive_getopts,
		cli_archive_show);


static CommandLine *archive_subcommands[] = {
	&archive_wal_command,
	&archive_pgdata_command,
	&archive_show_command,
	NULL
};


CommandLine archive_commands =
	make_command_set("archive",
					 "Archive WAL files and PGDATA base backups", NULL, NULL,
					 NULL, archive_subcommands);


/*
 * cli_archive_getopts parses command line options for pg_autoctl archive
 * commands.
 */
static int
cli_archive_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "json", no_argument, NULL, 'J' },
		{ "config", required_argument, NULL, 'C' },
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

			case 'C':
			{
				strlcpy(configFilename, optarg, MAXPGPATH);
				log_trace("--config %s", configFilename);
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

	/*
	 * We can use pg_autoctl archive wal with either a configuration file, for
	 * local testing of the command, or as an archive_command integrated in
	 * Postgres. When running as an archive_command, we expect PGDATA to be set
	 * in the environment, but could also work with PG_AUTOCTL_MONITOR.
	 */
	if (!IS_EMPTY_STRING_BUFFER(configFilename))
	{
		if (!file_exists(configFilename))
		{
			log_error("Configuration file \"%s\" does not exist",
					  configFilename);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		(void) prepare_keeper_options(&options);
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata) &&
		IS_EMPTY_STRING_BUFFER(configFilename))
	{
		log_error("Please provide either --pgdata or --config");
		exit(EXIT_CODE_BAD_ARGS);
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
 * cli_archive_wal archives a WAL file. Can be used as the archive_command in
 * the Postgres configuration.
 */
static void
cli_archive_wal(int argc, char **argv)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperConfig *config = &(keeper.config);

	keeper.config = keeperOptions;

	if (argc != 1 || IS_EMPTY_STRING_BUFFER(argv[0]))
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_fatal("Failed to read configuration file \"%s\"",
				  config->pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	MonitorArchiverPolicyArray policiesArray = { 0 };

	if (!monitor_get_archiver_policies(monitor,
									   config->formation,
									   &policiesArray))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	const char *filename = argv[0];

	if (policiesArray.count == 0)
	{
		log_info("Skipping archiving of WAL file \"%s\": no archiving policy "
				 "has been set-up for formation \"%s\"",
				 filename,
				 config->formation);
		log_info("HINT: Create an archiving policy using the command: "
				 "pg_autoctl create archiver-policy");

		exit(EXIT_CODE_QUIT);
	}

	for (int i = 0; i < policiesArray.count; i++)
	{
		MonitorArchiverPolicy *policy = &(policiesArray.policies[i]);

		if (!archive_wal(&keeper, policy, filename))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * cli_archive_pgdata makes a full base-backup and archives it.
 */
static void
cli_archive_pgdata(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * cli_archive_show shows the current backups and associated WAL files that we
 * have in the archive(s).
 */
static void
cli_archive_show(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}
