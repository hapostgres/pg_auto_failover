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
#include "env_utils.h"
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
#include "pidfile.h"
#include "state.h"
#include "string_utils.h"

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

static void print_monitor_uri(Monitor *monitor, FILE *stream);
static void print_formation_uri(SSLOptions *ssl,
								Monitor *monitor,
								const char *formation,
								FILE *stream);
static void print_all_uri(SSLOptions *ssl,
						  Monitor *monitor,
						  FILE *stream);

static void fprint_pidfile_as_json(const char *pidfile);

CommandLine show_uri_command =
	make_command("uri",
				 "Show the postgres uri to use to connect to pg_auto_failover nodes",
				 " [ --pgdata --monitor --formation --json ] ",
				 "  --pgdata      path to data directory\n"
				 "  --monitor     show the monitor uri\n"
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
	SHOW_FILE_UNKNOWN = 0,      /* no option selected yet */
	SHOW_FILE_ALL,              /* --all, or no option at all */
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

typedef struct ShowUriOptions
{
	bool monitorOnly;
	char formation[NAMEDATALEN];
} ShowUriOptions;

static ShowUriOptions showUriOptions = { 0 };


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
		{ "monitor", no_argument, NULL, 'm' },
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
				if (!stringToInt(optarg, &options.groupId))
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
				if (!stringToInt(optarg, &eventCount))
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

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

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

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

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
		(void) fformat(stdout, "%s\n", synchronous_standby_names);
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
		{ "monitor", no_argument, NULL, 'm' },
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

	while ((c = getopt_long(argc, argv, "D:Vvqh",
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
				showUriOptions.monitorOnly = true;
				log_trace("--monitor");
				break;
			}

			case 'f':
			{
				strlcpy(showUriOptions.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", showUriOptions.formation);
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
				log_error("Failed to parse command line, see above for details.");
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (showUriOptions.monitorOnly &&
		!IS_EMPTY_STRING_BUFFER(showUriOptions.formation))
	{
		log_fatal("Please use either --monitor or --formation, not both");
		exit(EXIT_CODE_BAD_ARGS);
	}

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
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
	KeeperConfig kconfig = keeperOptions;
	Monitor monitor = { 0 };
	SSLOptions *ssl = NULL;

	switch (ProbeConfigurationFileRole(kconfig.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
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

			if (!monitor_config_get_postgres_uri(&mconfig, connInfo, MAXCONNINFO))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!monitor_init(&monitor, connInfo))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}
			ssl = &(mconfig.pgSetup.ssl);

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool monitorDisabledIsOk = false;

			if (!keeper_config_read_file_skip_pgsetup(&kconfig,
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
			ssl = &(kconfig.pgSetup.ssl);
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  kconfig.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	if (showUriOptions.monitorOnly)
	{
		(void) print_monitor_uri(&monitor, stdout);
	}
	else if (!IS_EMPTY_STRING_BUFFER(showUriOptions.formation))
	{
		(void) print_formation_uri(ssl, &monitor, showUriOptions.formation, stdout);
	}
	else
	{
		(void) print_all_uri(ssl, &monitor, stdout);
	}
}


/*
 * print_monitor_uri shows the connection strings for the monitor and all
 * formations managed by it
 */
static void
print_monitor_uri(Monitor *monitor,
				  FILE *stream)
{
	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj,
							   "monitor",
							   monitor->pgsql.connectionString);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%s\n", monitor->pgsql.connectionString);
	}
}


/*
 * print_formation_uri connects to given monitor to fetch the
 * keeper configuration formation's URI, and prints it out on given stream. It
 * is printed in JSON format when outputJSON is true (--json options).
 */
static void
print_formation_uri(SSLOptions *ssl,
					Monitor *monitor,
					const char *formation,
					FILE *stream)
{
	char postgresUri[MAXCONNINFO];

	if (!monitor_formation_uri(monitor,
							   formation,
							   ssl,
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

		json_object_set_string(jsObj, formation, postgresUri);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%s\n", postgresUri);
	}
}


/*
 * print_all_uri prints the connection strings for the monitor and all
 * formations managed by it
 */
static void
print_all_uri(SSLOptions *ssl,
			  Monitor *monitor,
			  FILE *stream)
{
	if (outputJSON)
	{
		if (!monitor_print_every_formation_uri_as_json(monitor,
													   ssl,
													   stdout))
		{
			log_fatal("Failed to get the list of formation URIs");
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_every_formation_uri(monitor, ssl))
		{
			log_fatal("Failed to get the list of formation URIs");
			exit(EXIT_CODE_MONITOR);
		}
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
		{ "json", no_argument, NULL, 'J' },
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
				if (fileOptions.selection != SHOW_FILE_UNKNOWN &&
					fileOptions.selection != SHOW_FILE_CONFIG)
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
				if (fileOptions.selection != SHOW_FILE_UNKNOWN &&
					fileOptions.selection != SHOW_FILE_STATE)
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
				if (fileOptions.selection != SHOW_FILE_UNKNOWN &&
					fileOptions.selection != SHOW_FILE_INIT)
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
				if (fileOptions.selection != SHOW_FILE_UNKNOWN &&
					fileOptions.selection != SHOW_FILE_PID)
				{
					log_error(
						"Please use only one of --config --state --init --pid");
					commandline_help(stderr);
				}
				fileOptions.selection = SHOW_FILE_PID;
				log_trace("--pid");
				break;
			}

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
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
				log_error("Failed to parse command line, see above for details.");
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

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
			if (outputJSON)
			{
				char *serialized_string = NULL;
				JSON_Value *js = json_value_init_object();
				JSON_Object *root = json_value_get_object(js);

				json_object_set_string(root, "config", config.pathnames.config);

				if (role == PG_AUTOCTL_ROLE_KEEPER)
				{
					json_object_set_string(root, "state", config.pathnames.state);
					json_object_set_string(root, "init", config.pathnames.init);
				}

				json_object_set_string(root, "pid", config.pathnames.pid);

				serialized_string = json_serialize_to_string_pretty(js);

				fformat(stdout, "%s\n", serialized_string);

				json_free_serialized_string(serialized_string);
				json_value_free(js);
			}
			else
			{
				fformat(stdout, "%7s | %s\n", "File", "Path");
				fformat(stdout, "%7s-+-%15s\n", "-------", "---------------");

				fformat(stdout, "%7s | %s\n", "Config", config.pathnames.config);

				if (role == PG_AUTOCTL_ROLE_KEEPER)
				{
					fformat(stdout, "%7s | %s\n", "State", config.pathnames.state);
					fformat(stdout, "%7s | %s\n", "Init", config.pathnames.init);
				}
				fformat(stdout, "%7s | %s\n", "Pid", config.pathnames.pid);
				fformat(stdout, "\n");
			}
			break;
		}

		case SHOW_FILE_CONFIG:
		{
			if (showFileOptions.showFileContents)
			{
				if (outputJSON)
				{
					JSON_Value *js = json_value_init_object();

					const bool missingPgdataIsOk = true;
					const bool pgIsNotRunningIsOk = true;
					bool monitorDisabledIsOk = true;

					switch (role)
					{
						case PG_AUTOCTL_ROLE_MONITOR:
						{
							MonitorConfig mconfig = { 0 };

							mconfig.pathnames = config.pathnames;

							if (!monitor_config_read_file(&mconfig,
														  missingPgdataIsOk,
														  pgIsNotRunningIsOk))
							{
								/* errors have already been logged */
								exit(EXIT_CODE_BAD_CONFIG);
							}

							if (!monitor_config_to_json(&mconfig, js))
							{
								log_fatal(
									"Failed to serialize configuration to JSON");
								exit(EXIT_CODE_BAD_CONFIG);
							}
							break;
						}

						case PG_AUTOCTL_ROLE_KEEPER:
						{
							if (!keeper_config_read_file(&config,
														 missingPgdataIsOk,
														 pgIsNotRunningIsOk,
														 monitorDisabledIsOk))
							{
								exit(EXIT_CODE_BAD_CONFIG);
							}

							if (!keeper_config_to_json(&config, js))
							{
								log_fatal(
									"Failed to serialize configuration to JSON");
								exit(EXIT_CODE_BAD_CONFIG);
							}
							break;
						}

						case PG_AUTOCTL_ROLE_UNKNOWN:
						{
							log_fatal("Unknown node role %d", role);
							exit(EXIT_CODE_BAD_CONFIG);
						}
					}

					/* we have the config as a JSON object, print it out now */
					(void) cli_pprint_json(js);
				}
				else if (!fprint_file_contents(config.pathnames.config))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_CONFIG);
				}
			}
			else
			{
				fformat(stdout, "%s\n", config.pathnames.config);
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
					if (outputJSON)
					{
						JSON_Value *js = json_value_init_object();

						keeperStateAsJSON(&keeperState, js);
						(void) cli_pprint_json(js);
					}
					else
					{
						(void) print_keeper_state(&keeperState, stdout);
					}
				}
				else
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_STATE);
				}
			}
			else
			{
				fformat(stdout, "%s\n", config.pathnames.state);
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

				keeper.config = config;

				if (keeper_init_state_read(&(keeper.initState),
										   config.pathnames.init))
				{
					(void) print_keeper_init_state(&(keeper.initState), stdout);
				}
				else
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_STATE);
				}
			}
			else
			{
				fformat(stdout, "%s\n", config.pathnames.init);
			}

			break;
		}

		case SHOW_FILE_PID:
		{
			if (showFileOptions.showFileContents)
			{
				if (outputJSON)
				{
					(void) fprint_pidfile_as_json(config.pathnames.pid);
				}
				else
				{
					if (!fprint_file_contents(config.pathnames.pid))
					{
						/* errors have already been logged */
						exit(EXIT_CODE_INTERNAL_ERROR);
					}
				}
			}
			else
			{
				fformat(stdout, "%s\n", config.pathnames.pid);
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
		fformat(stdout, "%s\n", contents);
		return true;
	}
	else
	{
		/* errors have already been logged */
		return false;
	}
}


/*
 * fprint_pidfile_as_json prints the content of the pidfile as JSON.
 */
static void
fprint_pidfile_as_json(const char *pidfile)
{
	JSON_Value *js = json_value_init_object();
	JSON_Value *jsServices = json_value_init_array();
	JSON_Array *jsServicesArray = json_value_get_array(jsServices);

	JSON_Object *jsobj = json_value_get_object(js);

	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineCount = 0;
	int lineNumber;

	if (!file_accessible(pidfile))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		int pidLine = lineNumber + 1; /* zero-based, one-based */
		char *separator = NULL;

		/* main pid */
		if (pidLine == PIDFILE_LINE_PID)
		{
			int pidnum = 0;
			stringToInt(fileLines[lineNumber], &pidnum);
			json_object_set_number(jsobj, "pid", (double) pidnum);

			continue;
		}

		/* data directory */
		if (pidLine == PIDFILE_LINE_DATA_DIR)
		{
			json_object_set_string(jsobj, "pgdata", fileLines[lineNumber]);
		}

		/* version string */
		if (pidLine == PIDFILE_LINE_VERSION_STRING)
		{
			json_object_set_string(jsobj, "version", fileLines[lineNumber]);
		}

		/* extension version string */
		if (pidLine == PIDFILE_LINE_EXTENSION_VERSION)
		{
			/* skip it, the supervisor does not connect to the monitor */
			(void) 0;
		}

		/* semId */
		if (pidLine == PIDFILE_LINE_SEM_ID)
		{
			int semId = 0;

			if (stringToInt(fileLines[lineNumber], &semId))
			{
				json_object_set_number(jsobj, "semId", (double) semId);
			}
			else
			{
				log_error("Failed to parse semId \"%s\"", fileLines[lineNumber]);
			}

			continue;
		}

		if (pidLine >= PIDFILE_LINE_FIRST_SERVICE)
		{
			JSON_Value *jsService = json_value_init_object();
			JSON_Object *jsServiceObj = json_value_get_object(jsService);

			if ((separator = strchr(fileLines[lineNumber], ' ')) == NULL)
			{
				log_debug("Failed to find a space separator in line: \"%s\"",
						  fileLines[lineNumber]);
				continue;
			}
			else
			{
				int pidnum = 0;
				char *serviceName = separator + 1;

				char servicePidFile[BUFSIZE] = { 0 };

				char versionString[BUFSIZE] = { 0 };
				char extensionVersionString[BUFSIZE] = { 0 };

				*separator = '\0';
				stringToInt(fileLines[lineNumber], &pidnum);

				json_object_set_string(jsServiceObj, "name", serviceName);
				json_object_set_number(jsServiceObj, "pid", pidnum);

				/* grab version number of the service by parsing its pidfile */
				get_service_pidfile(pidfile, serviceName, servicePidFile);

				if (!read_service_pidfile_version_strings(
						servicePidFile,
						versionString,
						extensionVersionString))
				{
					/* warn about it and continue */
					log_warn("Failed to read version string for "
							 "service \"%s\" in pidfile \"%s\"",
							 serviceName,
							 servicePidFile);
				}
				else
				{
					json_object_set_string(jsServiceObj,
										   "version", versionString);
					json_object_set_string(jsServiceObj,
										   "pgautofailover",
										   extensionVersionString);
				}
			}

			json_array_append_value(jsServicesArray, jsService);
		}
	}

	json_object_set_value(jsobj, "services", jsServices);

	(void) cli_pprint_json(js);
}
