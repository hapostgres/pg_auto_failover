/*
 * src/bin/pg_autoctl/cli_do_demoapp.c
 *     Implementation of a demo application that shows how to handle automatic
 *     reconnection when a failover happened, and uses a single URI.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include "postgres_fe.h"
#include "portability/instr_time.h"

#include "cli_common.h"
#include "cli_do_demoapp.h"
#include "cli_do_root.h"
#include "commandline.h"
#include "defaults.h"
#include "demoapp.h"
#include "file_utils.h"
#include "ipaddr.h"
#include "monitor.h"
#include "pgctl.h"

DemoAppOptions demoAppOptions = { 0 };

static int cli_do_demoapp_getopts(int argc, char **argv);

static void cli_demo_run(int argc, char **argv);
static void cli_demo_uri(int argc, char **argv);
static void cli_demo_ping(int argc, char **argv);

static CommandLine do_demo_run_command =
	make_command("run",
				 "Run the pg_auto_failover demo application",
				 "[option ...]",
				 "  --monitor   Postgres URI of the pg_auto_failover monitor\n"
				 "  --formation Formation to use (default)\n"
				 "  --username  PostgreSQL's username\n"
				 "  --clients   How many client processes to use (1)\n"
				 "  --duration  Duration of the demo app, in seconds (30)\n",
				 cli_do_demoapp_getopts, cli_demo_run);

static CommandLine do_demo_uri_command =
	make_command("uri",
				 "Grab the application connection string from the monitor",
				 "[option ...]",
				 "  --monitor   Postgres URI of the pg_auto_failover monitor\n"
				 "  --formation Formation to use (default)\n"
				 "  --username  PostgreSQL's username\n"
				 "  --clients   How many client processes to use (1)\n"
				 "  --duration  Duration of the demo app, in seconds (30)\n",
				 cli_do_demoapp_getopts, cli_demo_uri);

static CommandLine do_demo_ping_command =
	make_command("ping",
				 "Attempt to connect to the application URI",
				 "[option ...]",
				 "  --monitor   Postgres URI of the pg_auto_failover monitor\n"
				 "  --formation Formation to use (default)\n"
				 "  --username  PostgreSQL's username\n"
				 "  --clients   How many client processes to use (1)\n"
				 "  --duration  Duration of the demo app, in seconds (30)\n",
				 cli_do_demoapp_getopts, cli_demo_ping);

CommandLine *do_demo_subcommands[] = {
	&do_demo_run_command,
	&do_demo_uri_command,
	&do_demo_ping_command,
	NULL
};

CommandLine do_demo_commands =
	make_command_set("demo",
					 "Use a demo application for pg_auto_failover", NULL, NULL,
					 NULL, do_demo_subcommands);


/*
 * cli_do_demoapp_getopts parses the command line options for the demo
 * sub-commands.
 */
static int
cli_do_demoapp_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	DemoAppOptions options = { 0 };

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "username", required_argument, NULL, 'U' },
		{ "clients", required_argument, NULL, 'c' },
		{ "duration", required_argument, NULL, 't' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set our defaults */
	options.clientsCount = 1;
	options.duration = 30;
	strlcpy(options.formation, "default", sizeof(options.formation));

	/*
	 * The only command lines that are using cli_do_demoapp_getopts are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				/* { "monitor", required_argument, NULL, 'm' } */
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
				/* { "formation", required_argument, NULL, 'f' } */
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'U':
			{
				/* { "username", required_argument, NULL, 'U' } */
				strlcpy(options.username, optarg, NAMEDATALEN);
				log_trace("--username %s", options.username);
				break;
			}

			case 'c':
			{
				/* { "clients", required_argument, NULL, 'c' }, */
				if (!stringToInt(optarg, &options.clientsCount))
				{
					log_error("Failed to parse --clients number \"%s\"",
							  optarg);
					errors++;
				}

				if (options.clientsCount < 1 ||
					options.clientsCount > MAX_CLIENTS_COUNT)
				{
					log_error("Unsupported value for --clients: %d must be "
							  "at least 1 and maximum %d",
							  options.clientsCount,
							  MAX_CLIENTS_COUNT);
				}

				log_trace("--clients %d", options.clientsCount);
				break;
			}

			case 't':
			{
				/* { "duration", required_argument, NULL, 't' }, */
				if (!stringToInt(optarg, &options.duration))
				{
					log_error("Failed to parse --duration number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--duration %d", options.duration);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				printVersion = true;
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

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (IS_EMPTY_STRING_BUFFER(options.monitor_pguri))
	{
		log_fatal("Please provide --monitor");
		errors++;
	}

	if (IS_EMPTY_STRING_BUFFER(options.username))
	{
		PostgresSetup pgSetup = { 0 };
		char *username = pg_setup_get_username(&pgSetup);

		strlcpy(options.username, username, sizeof(options.username));
	}

	/* set our Postgres username as the PGUSER environment variable now */
	setenv("PGUSER", options.username, 1);

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/* publish parsed options */
	demoAppOptions = options;

	return optind;
}


/*
 * cli_demo_run runs a demo application.
 */
static void
cli_demo_run(int argc, char **argv)
{
	char pguri[MAXCONNINFO] = { 0 };

	if (!demoapp_grab_formation_uri(&demoAppOptions, pguri, sizeof(pguri)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Using application connection string \"%s\"", pguri);
	log_info("Using Postgres user PGUSER \"%s\"", demoAppOptions.username);

	if (!demoapp_prepare_schema(pguri))
	{
		log_fatal("Failed to install the demo application schema");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!demoapp_run(pguri,
					 demoAppOptions.clientsCount,
					 demoAppOptions.duration))
	{
		log_fatal("Failed to run the demo application");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) demoapp_print_summary(pguri,
								 demoAppOptions.clientsCount,
								 demoAppOptions.duration);
}


/*
 * cli_demo_uri returns the Postgres connection string (URI) to use in the demo
 * application, grabbed from a running monitor node by using the SQL API.
 */
static void
cli_demo_uri(int argc, char **argv)
{
	char pguri[MAXCONNINFO] = { 0 };

	if (!demoapp_grab_formation_uri(&demoAppOptions, pguri, sizeof(pguri)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s\n", pguri);
}


/*
 * cli_demo_ping connects to the application connection string retrieved from
 * the monitor, and outputs some statistics about the connection attempt(s) and
 * its success or failure.
 */
static void
cli_demo_ping(int argc, char **argv)
{
	PGSQL pgsql = { 0 };
	char pguri[MAXCONNINFO] = { 0 };

	bool is_in_recovery = false;

	if (!demoapp_grab_formation_uri(&demoAppOptions, pguri, sizeof(pguri)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Using application connection string \"%s\"", pguri);
	log_info("Using Postgres user PGUSER \"%s\"", demoAppOptions.username);

	pgsql_init(&pgsql, pguri, PGSQL_CONN_LOCAL);

	if (!pgsql_is_in_recovery(&pgsql, &is_in_recovery))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGSQL);
	}

	instr_time duration;

	INSTR_TIME_SET_CURRENT(duration);
	INSTR_TIME_SUBTRACT(duration, pgsql.retryPolicy.startTime);

	log_info("Connected after %d attempt(s) in %g ms",
			 pgsql.retryPolicy.attempts + 1,
			 INSTR_TIME_GET_MILLISEC(duration));

	if (is_in_recovery)
	{
		log_error("Failed to connect to a primary node: "
				  "Postgres is in recovery");
		exit(EXIT_CODE_PGSQL);
	}

	log_info("Target Postgres is not in recovery, "
			 "as expected from a primary node");
}
