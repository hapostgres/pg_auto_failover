/*
 * src/bin/pg_autoctl/cli_show.c
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

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "monitor.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

static int eventCount = 10;

static int cli_show_state_getopts(int argc, char **argv);
static void cli_show_state(int argc, char **argv);
static void cli_show_events(int argc, char **argv);

static int cli_show_nodes_getopts(int argc, char **argv);
static void cli_show_nodes(int argc, char **argv);

static void cli_show_standby_names(int argc, char **argv);

static int cli_show_file_getopts(int argc, char **argv);
static void cli_show_file(int argc, char **argv);
static bool fprint_file_contents(const char *filename);

static int cli_show_uri_getopts(int argc, char **argv);
static void cli_show_uri(int argc, char **argv);
static void cli_show_all_uri(int argc, char **argv);
static void cli_show_formation_uri(int argc, char **argv);

static void print_monitor_and_formation_uri(KeeperConfig *config,
											Monitor *monitor,
											FILE *stream);

CommandLine show_uri_command =
	make_command("uri",
				 "Show the postgres uri to use to connect to pg_auto_failover nodes",
				 " [ --pgdata --formation --json ] ",
				 "  --pgdata      path to data directory\n"
				 "  --formation   show the coordinator uri of given formation\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_uri_getopts,
				 cli_show_uri);

CommandLine show_events_command =
	make_command("events",
				 "Prints monitor's state of nodes in a given formation and group",
				 " [ --pgdata --formation --group --count ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --count       how many events to fetch, defaults to 10 \n"
				 "  --json        output data in the JSON format\n",
				 cli_show_state_getopts,
				 cli_show_events);

CommandLine show_state_command =
	make_command("state",
				 "Prints monitor's state of nodes in a given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --json        output data in the JSON format\n",
				 cli_show_state_getopts,
				 cli_show_state);

CommandLine show_nodes_command =
	make_command("nodes",
				 "Prints monitor nodes of nodes in a given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --json        output data in the JSON format\n",
				 cli_show_nodes_getopts,
				 cli_show_nodes);

CommandLine show_sync_standby_names_command =
	make_command("synchronous_standby_names",
				 "Prints synchronous_standby_names for a given group",
				 " [ --pgdata ] --formation --group",
				 "  --pgdata      path to data directory	 \n"
				 "  --formation   formation to query, defaults to 'default'\n"
				 "  --group       group to query formation, defaults to all\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_nodes_getopts,
				 cli_show_standby_names);

CommandLine show_standby_names_command =
	make_command("standby-names",
				 "Prints synchronous_standby_names for a given group",
				 " [ --pgdata ] --formation --group",
				 "  --pgdata      path to data directory	 \n"
				 "  --formation   formation to query, defaults to 'default'\n"
				 "  --group       group to query formation, defaults to all\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_nodes_getopts,
				 cli_show_standby_names);

CommandLine show_file_command =
	make_command("file",
				 "List pg_autoctl internal files (config, state, pid)",
				 " [ --pgdata --all --config | --state | --init | --pid --contents ]",
				 "  --pgdata      path to data directory \n"
				 "  --all         show all pg_autoctl files \n"
				 "  --config      show pg_autoctl configuration file \n"
				 "  --state       show pg_autoctl state file \n"
				 "  --init        show pg_autoctl initialisation state file \n"
				 "  --pid         show pg_autoctl PID file \n"
				 "  --contents    show selected file contents \n",
				 cli_show_file_getopts,
				 cli_show_file);

typedef enum
{
	SHOW_FILE_UNKNOWN = 0,		/* no option selected yet */
	SHOW_FILE_ALL,				/* --all, or no option at all */
	SHOW_FILE_CONFIG,
	SHOW_FILE_STATE,
	SHOW_FILE_INIT,
	SHOW_FILE_PID
} ShowFileSelection;

typedef struct ShowFileOptions
{
	bool showFileContents;
	ShowFileSelection selection;
} ShowFileOptions;

static ShowFileOptions showFileOptions;


/*
 * keeper_cli_monitor_state_getopts parses the command line options for the
 * command `pg_autoctl show state`.
 */
static int
cli_show_state_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "count", required_argument, NULL, 'n' },
		{ "json", no_argument, NULL, 'J' },
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

	strlcpy(options.formation, "default", NAMEDATALEN);

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

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'g':
			{
				int scanResult = sscanf(optarg, "%d", &options.groupId);
				if (scanResult == 0)
				{
					log_fatal("--group argument is not a valid group ID: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--group %d", options.groupId);
				break;
			}

			case 'n':
			{
				int scanResult = sscanf(optarg, "%d", &eventCount);
				if (scanResult == 0)
				{
					log_fatal("--count argument is not a valid count: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--count %d", eventCount);
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

	keeperOptions = options;

	return optind;
}


/*
 * keeper_cli_monitor_print_events prints the list of the most recent events
 * known to the monitor.
 */
static void
cli_show_events(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (outputJSON)
	{
		if (!monitor_print_last_events_as_json(&monitor,
											   config.formation,
											   config.groupId,
											   eventCount,
											   stdout))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_last_events(&monitor,
									   config.formation,
									   config.groupId,
									   eventCount))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
}


/*
 * keeper_cli_monitor_print_state prints the current state of given formation
 * and port from the monitor's point of view.
 */
static void
cli_show_state(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (outputJSON)
	{
		if (!monitor_print_state_as_json(&monitor,
										 config.formation, config.groupId))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_state(&monitor, config.formation, config.groupId))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
}


/*
 * cli_show_nodes_getopts parses the command line options for the
 * command `pg_autoctl show nodes`.
 */
static int
cli_show_nodes_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "json", no_argument, NULL, 'J' },
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

	strlcpy(options.formation, "default", NAMEDATALEN);

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

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'g':
			{
				int scanResult = sscanf(optarg, "%d", &options.groupId);
				if (scanResult == 0)
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

	keeperOptions = options;

	return optind;
}


/*
 * keeper_cli_monitor_print_state prints the current state of given formation
 * and port from the monitor's point of view.
 */
static void
cli_show_nodes(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (outputJSON)
	{
		if (!monitor_print_nodes_as_json(&monitor,
										 config.formation,
										 config.groupId))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_nodes(&monitor,
								 config.formation,
								 config.groupId))
		{
			log_fatal("Failed to get the other nodes from the monitor, "
					  "see above for details");
			exit(EXIT_CODE_MONITOR);
		}
	}
}


/*
 * cli_show_standby_names prints the synchronous_standby_names setting value
 * for a given group (in a known formation).
 */
static void
cli_show_standby_names(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	char synchronous_standby_names[BUFSIZE] = { 0 };

	/* change the default group when it's not been given on the command */
	if (config.groupId == -1)
	{
		config.groupId = 0;
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_synchronous_standby_names(
			&monitor,
			config.formation,
			config.groupId,
			synchronous_standby_names,
			BUFSIZE))
	{
		log_fatal("Failed to get the synchronous_standby_names setting value "
				  " from the monitor, see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj,
							   "synchronous_standby_names",
							   synchronous_standby_names);

		(void) cli_pprint_json(js);
	}
	else
	{
		(void) fprintf(stdout, "%s\n", synchronous_standby_names);
	}
}


/*
 * keeper_show_uri_getopts parses the command line options for the
 * command `pg_autoctl show uri`.
 */
static int
cli_show_uri_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "json", no_argument, NULL, 'J' },
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

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			default:
			{
				log_error("Failed to parse command line, see above for details.");
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
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

	keeperOptions = options;

	return optind;
}


/*
 * cli_show_uri prints the URI to connect to with psql.
 */
static void
cli_show_uri(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!IS_EMPTY_STRING_BUFFER(config.formation))
	{
		(void) cli_show_formation_uri(argc, argv);
	}
	else
	{
		(void) cli_show_all_uri(argc, argv);
	}
}


/*
 * keeper_cli_formation_uri lists the connection string to connect to a formation
 */
static void
cli_show_formation_uri(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	char postgresUri[MAXCONNINFO];

	/* when --formation is missing, use the default value */
	if (IS_EMPTY_STRING_BUFFER(config.formation))
	{
		strlcpy(config.formation, FORMATION_DEFAULT, NAMEDATALEN);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) print_monitor_and_formation_uri(&config, &monitor, stdout);
}


/*
 * keeper_cli_monitor_uri shows the postgres uri to use for connecting to the
 * monitor
 */
static void
cli_show_all_uri(int argc, char **argv)
{
	KeeperConfig kconfig = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&kconfig.pathnames,
												 kconfig.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(kconfig.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			Monitor monitor = { 0 };
			MonitorConfig mconfig = { 0 };
			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;
			char connInfo[MAXCONNINFO];

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &kconfig.pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_PGCTL);
			}

			pg_setup_get_local_connection_string(&(mconfig.pgSetup), connInfo);
			monitor_init(&monitor, connInfo);

			if (outputJSON)
			{
				if (!monitor_print_every_formation_uri_as_json(&monitor,
															   stdout))
				{
					log_fatal("Failed to get the list of formation URIs");
					exit(EXIT_CODE_MONITOR);
				}
			}
			else
			{
				if (!monitor_print_every_formation_uri(&monitor))
				{
					log_fatal("Failed to get the list of formation URIs");
					exit(EXIT_CODE_MONITOR);
				}
			}

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			Monitor monitor = { 0 };
			bool monitorDisabledIsOk = false;
			char value[BUFSIZE];

			if (!keeper_config_read_file_skip_pgsetup(
					&kconfig,
					monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!monitor_init(&monitor, kconfig.monitor_pguri))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			(void) print_monitor_and_formation_uri(&kconfig, &monitor, stdout);

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  kconfig.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * print_monitor_and_formation_uri connects to given monitor to fetch the
 * keeper configuration formation's URI, and prints it out on given stream. It
 * is printed in JSON format when outputJSON is true (--json options).
 */
static void
print_monitor_and_formation_uri(KeeperConfig *config,
								Monitor *monitor,
								FILE *stream)
{
	char postgresUri[MAXCONNINFO];

	if (!monitor_formation_uri(monitor,
							   config->formation,
							   postgresUri,
							   MAXCONNINFO))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj,
							   "monitor",
							   monitor->pgsql.connectionString);

		json_object_set_string(jsObj, config->formation, postgresUri);

		(void) cli_pprint_json(js);
	}
	else
	{
		fprintf(stdout, "%s\n", postgresUri);
	}
}


/*
 * cli_show_file_getopts parses the command line options for the
 * command `pg_autoctl show file`.
 */
static int
cli_show_file_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	ShowFileOptions fileOptions = { 0 };
	int c, option_index = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "all", no_argument, NULL, 'a' },
		{ "config", no_argument, NULL, 'c' },
		{ "state", no_argument, NULL, 's' },
		{ "init", no_argument, NULL, 'i' },
		{ "pid", no_argument, NULL, 'p' },
		{ "contents", no_argument, NULL, 'C' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:acsipCVvqh",
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
				fileOptions.showFileContents = true;

				if (fileOptions.selection == SHOW_FILE_ALL)
				{
					log_warn("Ignoring option --content with --all");
				}
				break;
			}

			case 'a':
			{
				fileOptions.selection = SHOW_FILE_ALL;

				if (fileOptions.showFileContents)
				{
					log_warn("Ignoring option --content with --all");
				}
				break;
			}

			case 'c':
			{
				if (fileOptions.selection != SHOW_FILE_UNKNOWN
					&& fileOptions.selection != SHOW_FILE_CONFIG)
				{
					log_error(
						"Please use only one of --config --state --init --pid");
					commandline_help(stderr);
				}
				fileOptions.selection = SHOW_FILE_CONFIG;
				log_trace("--config");
				break;
			}

			case 's':
			{
				if (fileOptions.selection != SHOW_FILE_UNKNOWN
					&& fileOptions.selection != SHOW_FILE_STATE)
				{
					log_error(
						"Please use only one of --config --state --init --pid");
					commandline_help(stderr);
				}
				fileOptions.selection = SHOW_FILE_STATE;
				log_trace("--state");
				break;
			}

			case 'i':
			{
				if (fileOptions.selection != SHOW_FILE_UNKNOWN
					&& fileOptions.selection != SHOW_FILE_INIT)
				{
					log_error(
						"Please use only one of --config --state --init --pid");
					commandline_help(stderr);
				}
				fileOptions.selection = SHOW_FILE_INIT;
				log_trace("--init");
				break;
			}

			case 'p':
			{
				if (fileOptions.selection != SHOW_FILE_UNKNOWN
					&& fileOptions.selection != SHOW_FILE_PID)
				{
					log_error(
						"Please use only one of --config --state --init --pid");
					commandline_help(stderr);
				}
				fileOptions.selection = SHOW_FILE_PID;
				log_trace("--pid");
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
				log_error("Failed to parse command line, see above for details.");
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
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

	if (!keeper_config_set_pathnames_from_pgdata(&options.pathnames,
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* default to --all when no option has been selected */
	if (fileOptions.selection == SHOW_FILE_UNKNOWN)
	{
		fileOptions.selection = SHOW_FILE_ALL;
	}

	keeperOptions = options;
	showFileOptions = fileOptions;

	return optind;
}


/*
 * cli_show_files lists the files used by pg_autoctl.
 */
static void
cli_show_file(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	pgAutoCtlNodeRole role = PG_AUTOCTL_ROLE_UNKNOWN;

	role = ProbeConfigurationFileRole(config.pathnames.config);

	switch (showFileOptions.selection)
	{
		case SHOW_FILE_ALL:
		{
			char *serialized_string = NULL;
			JSON_Value *js = json_value_init_object();
			JSON_Object *root = json_value_get_object(js);

			json_object_set_string(root, "config", config.pathnames.config);

			if (role == PG_AUTOCTL_ROLE_KEEPER)
			{
				json_object_set_string(root, "state", config.pathnames.state);
				json_object_set_string(root, "init", config.pathnames.init);
				json_object_set_string(root, "pid", config.pathnames.pid);
			}

			serialized_string = json_serialize_to_string_pretty(js);

			fprintf(stdout, "%s\n", serialized_string);

			json_free_serialized_string(serialized_string);
			json_value_free(js);

			break;
		}

		case SHOW_FILE_CONFIG:
		{
			if (showFileOptions.showFileContents)
			{
				if (!fprint_file_contents(config.pathnames.config))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_CONFIG);
				}
			}
			else
			{
				fprintf(stdout, "%s\n", config.pathnames.config);
			}
			break;
		}

		case SHOW_FILE_STATE:
		{
			if (role == PG_AUTOCTL_ROLE_MONITOR)
			{
				log_error("A monitor has not state file");
				exit(EXIT_CODE_BAD_ARGS);
			}

			if (showFileOptions.showFileContents)
			{
				KeeperStateData keeperState = { 0 };

				if (keeper_state_read(&keeperState, config.pathnames.state))
				{
					(void) print_keeper_state(&keeperState, stdout);
				}
				else
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_STATE);
				}
			}
			else
			{
				fprintf(stdout, "%s\n", config.pathnames.state);
			}

			break;
		}

		case SHOW_FILE_INIT:
		{
			if (role == PG_AUTOCTL_ROLE_MONITOR)
			{
				log_error("A monitor has not init state file");
				exit(EXIT_CODE_BAD_ARGS);
			}

			if (showFileOptions.showFileContents)
			{
				Keeper keeper = { 0 };
				KeeperStateInit initState = { 0 };

				keeper.config = config;

				if (keeper_init_state_read(&keeper, &initState))
				{
					(void) print_keeper_init_state(&initState, stdout);
				}
				else
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_STATE);
				}
			}
			else
			{
				fprintf(stdout, "%s\n", config.pathnames.init);
			}

			break;
		}

		case SHOW_FILE_PID:
		{
			if (role == PG_AUTOCTL_ROLE_MONITOR)
			{
				log_error("A monitor has not init pidfile");
				exit(EXIT_CODE_BAD_ARGS);
			}

			if (showFileOptions.showFileContents)
			{
				if (!fprint_file_contents(config.pathnames.pid))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_INTERNAL_ERROR);
				}
			}
			else
			{
				fprintf(stdout, "%s\n", config.pathnames.pid);
			}

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * fprint_file_contents prints the content of the given filename to stdout.
 */
static bool
fprint_file_contents(const char *filename)
{
	char *contents = NULL;
	long size = 0L;

	if (read_file(filename, &contents, &size))
	{
		fprintf(stdout, "%s\n", contents);
		return true;
	}
	else
	{
		/* errors have already been logged */
		return false;
	}
}
