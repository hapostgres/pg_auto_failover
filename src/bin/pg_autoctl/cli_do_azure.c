/*
 * src/bin/pg_autoctl/cli_do_azure.c
 *     Implementation of a CLI which lets you call `az` cli commands to prepare
 *     a pg_auto_failover demo or QA environment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "azure.h"
#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

typedef struct AzureOptions
{
	char group[NAMEDATALEN];
	char name[NAMEDATALEN];
	char location[NAMEDATALEN];

	char config[MAXPGPATH];

	int nodes;
	int cidr;
	bool monitor;
	bool all;
	bool watch;
} AzureOptions;

static AzureOptions azureOptions = { 0 };

bool dryRun = false;
PQExpBuffer azureScript = NULL;

static void outputAzureScript(void);


/*
 * cli_print_version_getopts parses the CLI options for the pg_autoctl version
 * command, which are the usual suspects.
 */
int
cli_do_azure_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	AzureOptions options = { 0 };

	static struct option long_options[] = {
		{ "group", required_argument, NULL, 'g' },
		{ "name", required_argument, NULL, 'n' },
		{ "location", required_argument, NULL, 'l' },
		{ "nodes", required_argument, NULL, 'N' },
		{ "monitor", no_argument, NULL, 'M' },
		{ "all", no_argument, NULL, 'A' },
		{ "script", no_argument, NULL, 'S' },
		{ "watch", no_argument, NULL, 'T' },
		{ "az", no_argument, NULL, 'Z' },
		{ "cidr", no_argument, NULL, 'c' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set our defaults */
	options.cidr = 11;          /* 10.11.0.0/16 and 10.11.11.0/24 */
	options.nodes = 2;
	options.monitor = false;
	options.all = false;
	options.watch = false;

	strlcpy(options.group, "ha-demo", sizeof(options.group));

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "p:n:l:N:MAWSTVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			/* { "group", required_argument, NULL, 'g' }, */
			case 'g':
			{
				strlcpy(options.group, optarg, NAMEDATALEN);
				log_trace("--group %s", options.group);
				break;
			}

			/* { "name", required_argument, NULL, 'n' }, */
			case 'n':
			{
				strlcpy(options.name, optarg, NAMEDATALEN);
				log_trace("--name %s", options.name);
				break;
			}

			/* { "location", required_argument, NULL, 'l' }, */
			case 'l':
			{
				strlcpy(options.location, optarg, NAMEDATALEN);
				log_trace("--location %s", options.location);
				break;
			}

			/* { "az", no_argument, NULL, 'Z' }, */
			case 'Z':
			{
				strlcpy(azureCLI, optarg, NAMEDATALEN);
				log_trace("--az %s", azureCLI);
				break;
			}

			/* { "cidr", no_argument, NULL, 'c' }, */
			case 'c':
			{
				if (!stringToInt(optarg, &options.cidr))
				{
					log_error("Failed to parse --cidr number \"%s\"", optarg);
					errors++;
				}
				else if (options.cidr < 1 || options.cidr > 254)
				{
					log_error("Failed to parse --cidr number \"%s\"", optarg);
					errors++;
				}
				else
				{
					log_trace("--cidr %d", options.cidr);
				}
				break;
			}

			/* { "nodes", required_argument, NULL, 'N' }, */
			case 'N':
			{
				if (!stringToInt(optarg, &options.nodes))
				{
					log_error("Failed to parse --nodes number \"%s\"", optarg);
					errors++;
				}
				log_trace("--nodes %d", options.nodes);
				break;
			}

			/* { "monitor", no_argument, NULL, 'M' }, */
			case 'M':
			{
				options.monitor = true;
				log_trace("--monitor");
				break;
			}

			/* { "all", no_argument, NULL, 'A' }, */
			case 'A':
			{
				options.all = true;
				log_trace("--monitor");
				break;
			}

			/* { "script", no_argument, NULL, 'S' }, */
			case 'S':
			{
				dryRun = true;
				log_trace("--script");
				break;
			}

			/* { "watch", no_argument, NULL, 'T' }, */
			case 'T':
			{
				options.watch = true;
				log_trace("--watch");
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

	if (IS_EMPTY_STRING_BUFFER(options.group))
	{
		++errors;
		log_fatal("--group is a mandatory option");
	}

	if (IS_EMPTY_STRING_BUFFER(options.location))
	{
		++errors;
		log_fatal("--location is a mandatory option");
	}

	if (IS_EMPTY_STRING_BUFFER(azureCLI))
	{
		if (!search_path_first("az", azureCLI))
		{
			++errors;
			log_fatal("Failed to find program \"%s\" in PATH", "az");
		}
	}
	else
	{
		if (!file_exists(azureCLI))
		{
			++errors;
			log_fatal("No such file or directory: \"%s\"", azureCLI);
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/*
	 * In --script mode (or dry run) we generate a script with the commands we
	 * would run instead of actually running them.
	 */
	if (dryRun)
	{
		azureScript = createPQExpBuffer();

		if (azureScript == NULL)
		{
			log_error("Failed to allocate memory");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		appendPQExpBuffer(azureScript,
						  "# azure commands for pg_auto_failover demo");
	}

	/* publish parsed options */
	azureOptions = options;

	return optind;
}


/*
 * outputAzureScript writes the azure script to stdout.
 */
static void
outputAzureScript()
{
	if (dryRun)
	{
		fformat(stdout, "%s\n", azureScript->data);
		destroyPQExpBuffer(azureScript);
	}
}


/*
 * cli_do_azure_create_region creates an Azure region with some nodes and
 * network rules for a demo or QA context of pg_auto_failover.
 */
void
cli_do_azure_create_region(int argc, char **argv)
{
	AzureOptions options = azureOptions;

	if (!azure_create_region(options.group,
							 options.name,
							 options.location,
							 options.cidr,
							 options.nodes))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* that's for testing only */
	if (false && !azure_psleep(options.nodes, true))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();
}