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
#include "azure_config.h"
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

static AzureOptions azOptions = { 0 };
static AzureRegionResources azRegion = { 0 };

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
		{ "prefix", required_argument, NULL, 'p' },
		{ "region", required_argument, NULL, 'r' },
		{ "location", required_argument, NULL, 'l' },
		{ "from-source", no_argument, NULL, 's' },
		{ "nodes", required_argument, NULL, 'N' },
		{ "no-monitor", no_argument, NULL, 'M' },
		{ "no-app", no_argument, NULL, 'n' },
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
	options.fromSource = false;
	options.appNode = true;
	options.monitor = true;
	options.all = false;
	options.watch = false;

	strlcpy(options.prefix, "ha-demo", sizeof(options.prefix));

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
			/* { "prefix", required_argument, NULL, 'p' }, */
			case 'p':
			{
				strlcpy(options.prefix, optarg, NAMEDATALEN);
				log_trace("--prefix %s", options.prefix);
				break;
			}

			/* { "region", required_argument, NULL, 'r' }, */
			case 'r':
			{
				strlcpy(options.region, optarg, NAMEDATALEN);
				log_trace("--region %s", options.region);
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

			/* { "no-monitor", no_argument, NULL, 'M' }, */
			case 'M':
			{
				options.monitor = false;
				log_trace("--no-monitor");
				break;
			}

			/* { "no-app", no_argument, NULL, 'n' }, */
			case 'n':
			{
				options.appNode = false;
				log_trace("--no-app");
				break;
			}

			/* { "from-source", required_argument, NULL, 's' }, */
			case 's':
			{
				options.fromSource = true;
				log_trace("--from-source");
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

	if (IS_EMPTY_STRING_BUFFER(options.prefix))
	{
		++errors;
		log_fatal("--prefix is a mandatory option");
	}

	if (IS_EMPTY_STRING_BUFFER(azureCLI))
	{
		if (!search_path_first("az", azureCLI, LOG_ERROR))
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
	 * From command line options parsing, prepare a AzureRegionResources in our
	 * static place.
	 *
	 * If a configuration file exists already, it takes precendence, because we
	 * have probably already created all the resources on Azure and deployed
	 * things there.
	 *
	 * If no configuration file exists already, we create one filled with the
	 * options given in the command line.
	 */
	(void) azure_config_prepare(&options, &azRegion);

	if (file_exists(azRegion.filename))
	{
		log_info("Reading configuration from \"%s\"", azRegion.filename);

		if (!azure_config_read_file(&azRegion))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* maybe late we will merge new options in the pre-existing file */
		log_warn("Ignoring command line options, "
				 "configuration file takes precedence");

		log_info("Using --prefix \"%s\" --region \"%s\" --location \"%s\"",
				 azRegion.prefix,
				 azRegion.region,
				 azRegion.location);
	}
	else
	{
		if (!azure_config_write_file(&azRegion))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/* when a configuration file already exists, it provides the location */
	if (IS_EMPTY_STRING_BUFFER(azRegion.location))
	{
		log_fatal("--location is a mandatory option");
		exit(EXIT_CODE_BAD_ARGS);
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
	azOptions = options;

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
 * cli_do_azure_create_environment creates an Azure region with some nodes and
 * network rules for a demo or QA context of pg_auto_failover, then provision
 * those VMs with the needed software, and then create pg_auto_failover nodes
 * from that, in a tmux session for interactive QA.
 */
void
cli_do_azure_create_environment(int argc, char **argv)
{
	/*
	 * azure_create_region creates the resources we need (VMs, network, access
	 * rules, etc) and then provision the VMs with the needed software.
	 */
	if (!azure_create_region(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();

	/*
	 * tmux_azure_start_or_attach_session then creates a tmux session with a
	 * shell window for each VM in the Azure resource group, and in each
	 * session in parallel runs the pg_autoctl create commands, and then add
	 * the setup to systemd.
	 *
	 * Another tmux window is created to run pg_autoctl show state in a watch
	 * loop.
	 *
	 * An extra window is created for interactive tinkering with the QA
	 * environment thus provided.
	 */
	if (!tmux_azure_start_or_attach_session(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_create_region creates an Azure region with some nodes and
 * network rules for a demo or QA context of pg_auto_failover.
 */
void
cli_do_azure_create_region(int argc, char **argv)
{
	if (!azure_create_region(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();
}


/*
 * cli_do_azure_drop_region drops the azure resource group that has been
 * created to host the azure resources in use for the environment.
 */
void
cli_do_azure_drop_region(int argc, char **argv)
{
	bool success = true;

	if (!azure_drop_region(&azRegion))
	{
		log_warn("Configuration file \"%s\" has not been deleted",
				 azRegion.filename);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Killing tmux sessions \"%s\"", azRegion.group);

	success = success && tmux_azure_kill_session(&azRegion);

	log_info("Removing azure configuration file \"%s\"", azRegion.filename);

	if (!unlink_file(azRegion.filename))
	{
		log_fatal("Failed to remove azure configuration file");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();
}


/*
 * cli_do_azure_deploy deploys the pg_autoctl services in the target VM, given
 * by name (such as "monitor" or "a" or "b", etc).
 */
void
cli_do_azure_deploy(int argc, char **argv)
{
	if (argc != 1)
	{
		(void) commandline_print_usage(&do_azure_ssh, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!azure_deploy_vm(&azRegion, argv[0]))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();
}


/*
 * cli_do_azure_create_nodes creates the pg_autoctl services in an Azure
 * region that's been created and provisionned before.
 */
void
cli_do_azure_create_nodes(int argc, char **argv)
{
	if (!azure_create_nodes(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) outputAzureScript();
}


/*
 * cli_do_azure_ls lists Azure resources created in the target region.
 */
void
cli_do_azure_ls(int argc, char **argv)
{
	if (!azure_ls(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_show_ips lists Azure ip addresses assigned to created VMs in a
 * specific region.
 */
void
cli_do_azure_show_ips(int argc, char **argv)
{
	if (!azure_show_ips(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_ssh starts an ssh command to the given Azure VM in a specific
 * prefix and region name.
 */
void
cli_do_azure_ssh(int argc, char **argv)
{
	if (argc != 1)
	{
		(void) commandline_print_usage(&do_azure_ssh, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!azure_ssh(&azRegion, argv[0]))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_rsync uses rsync to upload the current sources to all the
 * created VMs in the target region.
 */
void
cli_do_azure_rsync(int argc, char **argv)
{
	if (!azure_sync_source_dir(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_ssh starts an ssh command to the given Azure VM in a specific
 * prefix and region name.
 */
void
cli_do_azure_show_state(int argc, char **argv)
{
	char *pg_autoctl_command =
		azOptions.watch
		? "watch -n 0.2 pg_autoctl show state --pgdata ./monitor"
		: "pg_autoctl show state --pgdata ./monitor";

	if (!azure_ssh_command(&azRegion,
						   "monitor",
						   azOptions.watch, /* tty is needed for watch */
						   pg_autoctl_command))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_tmux_session starts or re-attach to a tmux session from where
 * to control the VMs in the QA environment on Azure.
 */
void
cli_do_azure_tmux_session(int argc, char **argv)
{
	if (!tmux_azure_start_or_attach_session(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_azure_tmux_session starts or re-attach to a tmux session from where
 * to control the VMs in the QA environment on Azure.
 */
void
cli_do_azure_tmux_kill(int argc, char **argv)
{
	if (!tmux_azure_kill_session(&azRegion))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}
