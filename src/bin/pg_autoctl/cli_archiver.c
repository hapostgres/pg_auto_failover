/*
 * src/bin/pg_autoctl/cli_archiver.c
 *   See cli_archiver.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>

#include "postgres_fe.h"

#include "cli_archiver.h"
#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "file_utils.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "pidfile.h"
#include "service_archiver_serve.h"
#include "signals.h"
#include "string_utils.h"

static int cli_archiver_serve_getopts(int argc, char **argv);
static void cli_archiver_serve(int argc, char **argv);

/* set by --port; 0 means "use PG_AUTOCTL_ARCHIVER_SERVE_PORT" */
static int archiverServePortOption = 0;


static int
cli_archiver_serve_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "port", required_argument, NULL, 'p' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
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

			case 'p':
			{
				if (!stringToInt(optarg, &archiverServePortOption) ||
					archiverServePortOption <= 0 ||
					archiverServePortOption > 65535)
				{
					log_fatal("Failed to parse --port value \"%s\"", optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
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
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	(void) prepare_keeper_options(&options);

	keeperOptions = options;

	return optind;
}


/*
 * cli_archiver_serve implements `pg_autoctl archiver serve`: loads the
 * archiver's own config/state (already written by `pg_autoctl create
 * archiver`), connects to the monitor, and runs
 * service_archiver_serve_loop() -- exec'ing pg_walsender and keeping its
 * routes file current. See service_archiver_serve.h.
 */
static void
cli_archiver_serve(int argc, char **argv)
{
	Keeper keeper = { 0 };

	keeper.config = keeperOptions;

	/*
	 * An archiver's pgdata is its local WAL-cache root, never a real
	 * Postgres instance (see service_archiver.c's own header comment) --
	 * both flags must tolerate that, matching cli_create_archiver's own
	 * choice not to run pg_setup_init's real-instance checks at all.
	 */
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_fatal("Failed to read the archiver configuration file \"%s\", "
				  "see above for details", keeper.config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (strcmp(keeper.config.nodeKind, "archiver") != 0)
	{
		log_fatal("\"%s\" is not an archiver's configuration file "
				  "(pg_autoctl.nodekind is \"%s\", expected \"archiver\")",
				  keeper.config.pathnames.config, keeper.config.nodeKind);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (keeper.config.archiverId <= 0)
	{
		log_fatal("This archiver's configuration file has no archiver_id "
				  "recorded -- it may predate `pg_autoctl archiver serve` "
				  "support; re-create the archiver with `pg_autoctl create "
				  "archiver` to pick it up");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_load_state(&keeper))
	{
		log_fatal("Failed to read the archiver state file \"%s\", "
				  "see above for details", keeper.config.pathnames.state);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor, see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (archiverServePortOption > 0)
	{
		service_archiver_serve_set_port(archiverServePortOption);
	}

	(void) set_signal_handlers(false);
	(void) set_ps_title("pg_autoctl: archiver serve");

	if (!create_pidfile(keeper.config.pathnames.pid, getpid()))
	{
		log_fatal("Failed to write archiver pid file \"%s\"",
				  keeper.config.pathnames.pid);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!service_archiver_serve_loop(&keeper))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


CommandLine archiver_serve_command =
	make_command(
		"serve",
		"Start serving this archiver's captured WAL and base backups",
		" [ --pgdata --port ] ",
		"  --pgdata          path to the archiver's local data/cache directory\n"
		"  --port            port for pg_walsender to listen on "
		"(default: 6543)\n",
		cli_archiver_serve_getopts,
		cli_archiver_serve);

CommandLine *archiver_subcommands[] = {
	&archiver_serve_command,
	NULL
};

CommandLine archiver_commands =
	make_command_set("archiver",
					 "Manage a pg_auto_failover archiver node", NULL, NULL,
					 NULL, archiver_subcommands);
