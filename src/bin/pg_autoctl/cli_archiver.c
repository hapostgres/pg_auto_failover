/*
 * src/bin/pg_autoctl/cli_archiver.c
 *     Implementation of the pg_autoctl create archiver CLI for the
 *     pg_auto_failover archiver nodes.
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

#include "archiver.h"
#include "archiver_config.h"
#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "ini_file.h"
#include "ipaddr.h"
#include "pidfile.h"

/* #include "service_archiver.h" */
/* #include "service_archiver_init.h" */
#include "string_utils.h"

/*
 * pg_autoctl archiver CLI:
 *
 *   pg_autoctl create archiver
 *
 *   pg_autoctl archive list nodes
 *   pg_autoctl archive add node --name
 *   pg_autoctl archive drop node --name
 *
 *   # policy
 *   pg_autoctl archive get policy --apply-delay --backup-interval ...
 *   pg_autoctl archive set policy --apply-delay --backup-interval ...
 *
 *   pg_autoctl archive list schedule
 *   pg_autoctl archive list backups --node ...
 *   pg_autoctl archive list wal --node
 *   pg_autoctl archive list timelines --node
 *
 *   pg_autoctl archive create backup --node ...
 *   pg_autoctl archive drop backup --node ...
 */

ArchiverConfig archiverOptions = { 0 };
CreateArchiverNodeOpts createArchiveNodeOptions = { 0 };
AddArchiverNodeOpts addArchiverNodeOptions = { 0 };

static int cli_create_archiver_getopts(int argc, char **argv);
static bool cli_create_archiver_config(Archiver *archiver);
static void cli_create_archiver(int argc, char **argv);

static int cli_archiver_node_getopts(int argc, char **argv);
static int cli_archiver_add_node_getopts(int argc, char **argv);

static void cli_archiver_list_nodes(int argc, char **argv);
static void cli_archiver_add_node(int argc, char **argv);
static void cli_archiver_drop_node(int argc, char **argv);

static void cli_archiver_get_policy(int argc, char **argv);
static void cli_archiver_set_policy(int argc, char **argv);

/*
 * static void cli_archiver_list_schedule(int argc, char **argv);
 * static void cli_archiver_list_backups(int argc, char **argv);
 * static void cli_archiver_list_wal(int argc, char **argv);
 * static void cli_archiver_list_timelines(int argc, char **argv);
 *
 * static void cli_archiver_create_backup(int argc, char **argv);
 * static void cli_archiver_drop_backup(int argc, char **argv);
 */

CommandLine create_archiver_command =
	make_command(
		"archiver",
		"Initialize a pg_auto_failover archiver node",
		" [ --directory --hostname --name ] ",
		"  --directory       top-level directory where to handle archives\n"
		"  --monitor         pg_auto_failover Monitor Postgres URL\n"
		"  --hostname        hostname by which postgres is reachable\n"
		"  --name            name of this archiver\n",
		cli_create_archiver_getopts,
		cli_create_archiver);

CommandLine archive_list_nodes_command =
	make_command(
		"nodes",
		"List archiver nodes managed by this pg_auto_failover archiver",
		" [ --name ]",
		"  --name            archiver node name\n",
		cli_archiver_node_getopts,
		cli_archiver_list_nodes);

CommandLine archive_add_node_command =
	make_command(
		"node",
		"Add a pg_auto_failover node to this archiver",
		"",
		"  --formation       pg_auto_failover formation\n"
		"  --group           pg_auto_failover group Id\n"
		"  --name            pg_auto_failover archiver node name\n",
		cli_archiver_add_node_getopts,
		cli_archiver_add_node);

CommandLine archive_drop_node_command =
	make_command(
		"node",
		"Drop a pg_auto_failover node from this archiver",
		"",
		"  --name            pg_auto_failover archiver node name\n",
		cli_archiver_node_getopts,
		cli_archiver_drop_node);

CommandLine archive_get_policy_command =
	make_command(
		"policy",
		"get the archiver policy for a given formation",
		"",
		"  --formation       pg_auto_failover formation\n",
		cli_archiver_node_getopts,
		cli_archiver_get_policy);

CommandLine archive_set_policy_command =
	make_command(
		"policy",
		"set the archiver policy for a given formation",
		"",
		"  --formation       pg_auto_failover formation\n",
		cli_archiver_node_getopts,
		cli_archiver_set_policy);


CommandLine *archiver_list[] = {
	&archive_list_nodes_command,
	NULL
};

CommandLine archiver_list_commands =
	make_command_set("list",
					 "list archiver nodes/schedule/resources",
					 NULL, NULL, NULL, archiver_list);

CommandLine *archiver_add[] = {
	&archive_add_node_command,
	NULL
};

CommandLine archiver_add_commands =
	make_command_set("add",
					 "add archiver nodes/schedule/resources",
					 NULL, NULL, NULL, archiver_add);

CommandLine *archiver_drop[] = {
	&archive_drop_node_command,
	NULL
};

CommandLine archiver_drop_commands =
	make_command_set("drop",
					 "drop archiver nodes/resources",
					 NULL, NULL, NULL, archiver_drop);

CommandLine *archiver_get_policy[] = {
	&archive_get_policy_command,
	NULL
};

CommandLine archiver_get_policy_commands =
	make_command_set("policy",
					 "get archiver policy settings",
					 NULL, NULL, NULL, archiver_get_policy);

CommandLine *archiver_get[] = {
	&archiver_get_policy_commands,
	NULL
};

CommandLine archiver_get_commands =
	make_command_set("get",
					 "get archiver settings",
					 NULL, NULL, NULL, archiver_get);

CommandLine *archiver_set_policy[] = {
	&archive_set_policy_command,
	NULL
};

CommandLine archiver_set_policy_commands =
	make_command_set("policy",
					 "set archiver policy settings",
					 NULL, NULL, NULL, archiver_set_policy);

CommandLine *archiver_set[] = {
	&archiver_set_policy_commands,
	NULL
};

CommandLine archiver_set_commands =
	make_command_set("set",
					 "set archiver settings",
					 NULL, NULL, NULL, archiver_set);


CommandLine *archiver[] = {
	&archiver_add_commands,
	&archiver_list_commands,
	&archiver_get_commands,
	&archiver_set_commands,
	NULL
};

CommandLine archiver_commands =
	make_command_set("archive",
					 "manage an archiver node",
					 NULL, NULL, NULL, archiver);


/*
 * cli_archiver_node_getopts parses the command line options necessary for many
 * of the pg_autoctl archive commands. Most of them only support the --node
 * argument.
 */
static int
cli_archiver_node_getopts(int argc, char **argv)
{
	ArchiverConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'a' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "a:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'a':
			{
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
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
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	archiverOptions = options;

	return optind;
}


/*
 * cli_archiver_add_node_getopts parses the command line options necessary for
 * the pg_autoctl archiver add node command.
 */
static int
cli_archiver_add_node_getopts(int argc, char **argv)
{
	AddArchiverNodeOpts options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'a' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "a:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'a':
			{
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
				break;
			}

			case 'f':
			{
				/* { "formation", required_argument, NULL, 'f' } */
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'g':
			{
				/* { "group", required_argument, NULL, 'g' } */
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
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	addArchiverNodeOptions = options;

	return optind;
}


/*
 * cli_create_archiver_getopts parses the command line options necessary to
 * initialize a pg_auto_failover archiver node.
 */
static int
cli_create_archiver_getopts(int argc, char **argv)
{
	ArchiverConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "directory", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "name", required_argument, NULL, 'a' },
		{ "hostname", required_argument, NULL, 'n' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:m:a:n:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.directory, optarg, MAXPGPATH);
				log_trace("--directory %s", options.directory);
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

			case 'a':
			{
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
				break;
			}

			case 'n':
			{
				strlcpy(options.hostname, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--hostname %s", options.hostname);
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
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (IS_EMPTY_STRING_BUFFER(options.directory))
	{
		log_fatal("The option --directory is mandatory");
		++errors;
	}

	if (IS_EMPTY_STRING_BUFFER(options.monitor_pguri))
	{
		log_fatal("The option --monitor is mandatory");
		++errors;
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!archiver_config_set_pathnames_from_directory(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	archiverOptions = options;

	return optind;
}


/*
 * cli_create_archiver_config takes care of the archiver configuration, either
 * creating it from scratch or merging the pg_autoctl create archiver command
 * line arguments and options with the pre-existing configuration file (for
 * when people change their mind or fix an error in the previous command).
 */
static bool
cli_create_archiver_config(Archiver *archiver)
{
	ArchiverConfig *config = &(archiver->config);

	if (file_exists(config->pathnames.config))
	{
		ArchiverConfig options = archiver->config;

		if (!archiver_config_read_file(config))
		{
			log_fatal("Failed to read configuration file \"%s\"",
					  config->pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/*
		 * Now that we have loaded the configuration file, apply the command
		 * line options on top of it, giving them priority over the config.
		 */
		if (!archiver_config_merge_options(config, &options))
		{
			/* errors have been logged already */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	else
	{
		/* take care of the --hostname */
		if (IS_EMPTY_STRING_BUFFER(config->hostname))
		{
			if (!ipaddrGetLocalHostname(config->hostname,
										sizeof(config->hostname)))
			{
				if (!discover_hostname((char *) &(config->hostname),
									   sizeof(config->hostname),
									   DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
									   DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
				{
					log_fatal("Failed to auto-detect the hostname "
							  "of this machine, please provide one "
							  "via --hostname");
					exit(EXIT_CODE_BAD_ARGS);
				}
			}
		}
		else
		{
			/*
			 * When provided with a --hostname option, we run some checks on
			 * the user provided value based on Postgres usage for the hostname
			 * in its HBA setup. Both forward and reverse DNS needs to return
			 * meaningful values for the connections to be granted when using a
			 * hostname.
			 *
			 * That said network setup is something complex and we don't
			 * pretend we are able to avoid any and all false negatives in our
			 * checks, so we only WARN when finding something that might be
			 * fishy, and proceed with the setup of the local node anyway.
			 */
			(void) check_hostname(config->hostname);
		}

		/* set our ArchiverConfig from the command line options now. */
		(void) archiver_config_init(config);

		/* and write our brand new setup to file */
		if (!archiver_config_write_file(config))
		{
			log_fatal("Failed to write the archiver's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	return true;
}


/*
 * cli_create_archiver creates an archiver node and registers it to the
 * monitor.
 */
static void
cli_create_archiver(int argc, char **argv)
{
	pid_t pid = 0;
	Archiver archiver = { 0 };
	Monitor *monitor = &(archiver.monitor);
	ArchiverConfig *config = &(archiver.config);

	NodeAddress node = { 0 };

	archiver.config = archiverOptions;

	if (read_pidfile(config->pathnames.pid, &pid))
	{
		log_fatal("pg_autoctl is already running with pid %d", pid);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!cli_create_archiver_config(&archiver))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!archiver_monitor_init(&archiver))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_register_archiver(monitor, config->name, config->hostname,
								   &node))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}
}


static void
cli_archiver_list_nodes(int argc, char **argv)
{
	log_fatal("Not yet implemented");
	exit(EXIT_CODE_INTERNAL_ERROR);
}


static void
cli_archiver_add_node(int argc, char **argv)
{
	log_fatal("Not yet implemented");
	exit(EXIT_CODE_INTERNAL_ERROR);
}


static void
cli_archiver_drop_node(int argc, char **argv)
{
	log_fatal("Not yet implemented");
	exit(EXIT_CODE_INTERNAL_ERROR);
}


static void
cli_archiver_get_policy(int argc, char **argv)
{
	log_fatal("Not yet implemented");
	exit(EXIT_CODE_INTERNAL_ERROR);
}


static void
cli_archiver_set_policy(int argc, char **argv)
{
	log_fatal("Not yet implemented");
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * static void
 * cli_archiver_list_schedule(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 *
 *
 * static void
 * cli_archiver_list_backups(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 *
 *
 * static void
 * cli_archiver_list_wal(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 *
 *
 * static void
 * cli_archiver_list_timelines(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 *
 *
 * static void
 * cli_archiver_create_backup(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 *
 *
 * static void
 * cli_archiver_drop_backup(int argc, char **argv)
 * {
 *  log_fatal("Not yet implemented");
 *  exit(EXIT_CODE_INTERNAL_ERROR);
 * }
 */
