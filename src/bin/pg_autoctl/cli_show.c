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
#include "nodestate_utils.h"
#include "parsing.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pidfile.h"
#include "state.h"
#include "string_utils.h"
#include "watch.h"

static int eventCount = 10;
static bool localState = false;
static bool watch = false;

static int cli_show_state_getopts(int argc, char **argv);
static void cli_show_state(int argc, char **argv);
static void cli_show_local_state(void);
static void cli_show_events(int argc, char **argv);

static int cli_show_standby_names_getopts(int argc, char **argv);
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
								const char *citusClusterName,
								FILE *stream);
static void print_all_uri(SSLOptions *ssl,
						  Monitor *monitor,
						  FILE *stream);


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
				 "  --monitor     pg_auto_failover Monitor Postgres URL\n" \
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --count       how many events to fetch, defaults to 10 \n"
				 "  --watch       display an auto-updating dashboard\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_state_getopts,
				 cli_show_events);

CommandLine show_state_command =
	make_command("state",
				 "Prints monitor's state of nodes in a given formation and group",
				 " [ --pgdata --formation --group ] ",
				 "  --pgdata      path to data directory	 \n"
				 "  --monitor     show the monitor uri\n"
				 "  --formation   formation to query, defaults to 'default' \n"
				 "  --group       group to query formation, defaults to all \n"
				 "  --local       show local data, do not connect to the monitor\n"
				 "  --watch       display an auto-updating dashboard\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_state_getopts,
				 cli_show_state);

CommandLine show_settings_command =
	make_command("settings",
				 "Print replication settings for a formation from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] ",
				 "  --pgdata      path to data directory\n"
				 "  --monitor     pg_auto_failover Monitor Postgres URL\n"
				 "  --json        output data in the JSON format\n"
				 "  --formation   pg_auto_failover formation\n",
				 cli_get_name_getopts,
				 cli_get_formation_settings);

CommandLine show_standby_names_command =
	make_command("standby-names",
				 "Prints synchronous_standby_names for a given group",
				 " [ --pgdata ] --formation --group",
				 "  --pgdata      path to data directory	 \n"
				 "  --monitor     show the monitor uri\n"
				 "  --formation   formation to query, defaults to 'default'\n"
				 "  --group       group to query formation, defaults to all\n"
				 "  --json        output data in the JSON format\n",
				 cli_show_standby_names_getopts,
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
	char citusClusterName[NAMEDATALEN];
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
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "group", required_argument, NULL, 'g' },
		{ "count", required_argument, NULL, 'n' },
		{ "local", no_argument, NULL, 'L' },
		{ "watch", no_argument, NULL, 'W' },
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

			case 'L':
			{
				localState = true;
				log_trace("--local");
				break;
			}

			case 'W':
			{
				watch = true;
				log_trace("--watch");
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

	if (watch && localState)
	{
		log_error("Please use either --local or --watch, but not both");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (watch && outputJSON)
	{
		log_error("Please use either --json or --watch, but not both");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (localState)
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
	}
	else
	{
		/* when we have a monitor URI we don't need PGDATA */
		if (cli_use_monitor_option(&options))
		{
			if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
			{
				log_warn("Given --monitor URI, the --pgdata option is ignored");
				log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);
			}
		}
		else
		{
			cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
		}
	}

	/* when --pgdata is given, still initialise our pathnames */
	if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
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

	if (watch)
	{
		WatchContext context = { 0 };

		(void) cli_monitor_init_from_option_or_config(&(context.monitor), &config);

		strlcpy(context.formation, config.formation, sizeof(context.formation));
		context.groupId = config.groupId;

		(void) cli_watch_main_loop(&context);

		exit(EXIT_CODE_QUIT);
	}

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

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

	if (localState)
	{
		(void) cli_show_local_state();
		exit(EXIT_CODE_QUIT);
	}

	/*
	 * When dealing with a keeper node with a disabled monitor, we force the
	 * --local option.
	 */
	if (!IS_EMPTY_STRING_BUFFER(config.pgSetup.pgdata) &&
		ProbeConfigurationFileRole(config.pathnames.config) == PG_AUTOCTL_ROLE_KEEPER)
	{
		bool missingPgdataIsOk = true;
		bool pgIsNotRunningIsOk = true;
		bool monitorDisabledIsOk = true;

		if (!keeper_config_read_file(&config,
									 missingPgdataIsOk,
									 pgIsNotRunningIsOk,
									 monitorDisabledIsOk))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		if (config.monitorDisabled)
		{
			log_info("Monitor is disabled, showing --local state");
			(void) cli_show_local_state();
			exit(EXIT_CODE_QUIT);
		}
	}

	if (watch)
	{
		WatchContext context = { 0 };

		(void) cli_monitor_init_from_option_or_config(&(context.monitor), &config);

		strlcpy(context.formation, config.formation, sizeof(context.formation));
		context.groupId = config.groupId;

		(void) cli_watch_main_loop(&context);

		exit(EXIT_CODE_QUIT);
	}

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

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
 * cli_show_local_state implements pg_autoctl show state --local, which
 * composes the state from what we have in the configuration file and the state
 * file for the local (keeper) node.
 */
static void
cli_show_local_state()
{
	KeeperConfig config = keeperOptions;
	int optionGroupId = keeperOptions.groupId;

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			log_error("pg_autoctl show state --local is not supported "
					  "on a monitor");
			exit(EXIT_CODE_MONITOR);
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;
			bool monitorDisabledIsOk = true;

			Keeper keeper = { 0 };
			CurrentNodeState nodeState = { 0 };

			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!keeper_init(&keeper, &config))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			/* ensure that --group makes sense then */
			if (optionGroupId != -1 && config.groupId != optionGroupId)
			{
				log_error("--group %d does not match this node's group: %d",
						  optionGroupId, config.groupId);
				exit(EXIT_CODE_BAD_CONFIG);
			}

			/* build the CurrentNodeState from pieces */
			nodeState.node.nodeId = keeper.state.current_node_id;
			strlcpy(nodeState.node.name, config.name, _POSIX_HOST_NAME_MAX);
			strlcpy(nodeState.node.host, config.hostname, _POSIX_HOST_NAME_MAX);
			nodeState.node.port = config.pgSetup.pgport;

			strlcpy(nodeState.formation, config.formation, NAMEDATALEN);
			nodeState.groupId = config.groupId;

			nodeState.reportedState = keeper.state.current_role;
			nodeState.goalState = keeper.state.assigned_role;

			if (pg_setup_is_ready(&(config.pgSetup), pgIsNotRunningIsOk))
			{
				if (!pgsql_get_postgres_metadata(
						&(keeper.postgres.sqlClient),
						&(keeper.postgres.postgresSetup.is_in_recovery),
						keeper.postgres.pgsrSyncState,
						keeper.postgres.currentLSN,
						&(keeper.postgres.postgresSetup.control)))
				{
					log_warn("Failed to update the local Postgres metadata");

					strlcpy(nodeState.node.lsn, "0/0", PG_LSN_MAXLENGTH);
				}

				nodeState.node.tli =
					keeper.postgres.postgresSetup.control.timeline_id;

				strlcpy(nodeState.node.lsn,
						keeper.postgres.currentLSN,
						PG_LSN_MAXLENGTH);
			}
			else
			{
				/* also grab the minimum recovery LSN if that's possible */
				if (!pg_controldata(&(config.pgSetup), missingPgdataIsOk))
				{
					/* errors have already been logged, just continue */
				}

				nodeState.node.tli = config.pgSetup.control.timeline_id;
				strlcpy(nodeState.node.lsn,
						config.pgSetup.control.latestCheckpointLSN,
						PG_LSN_MAXLENGTH);
			}

			/* we have no idea, only the monitor knows, so report "unknown" */
			nodeState.health = -1;

			if (outputJSON)
			{
				JSON_Value *js = json_value_init_object();

				if (!nodestateAsJSON(&nodeState, js))
				{
					/* can't happen */
					exit(EXIT_CODE_INTERNAL_ERROR);
				}
				(void) cli_pprint_json(js);
			}
			else
			{
				NodeAddressHeaders headers = { 0 };

				headers.nodeKind = keeper.config.pgSetup.pgKind;

				(void) nodestateAdjustHeaders(&headers,
											  &(nodeState.node),
											  nodeState.groupId);

				(void) prepareHeaderSeparators(&headers);

				(void) nodestatePrintHeader(&headers);

				(void) nodestatePrintNodeState(&headers, &nodeState);

				fformat(stdout, "\n");
			}

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
}


/*
 * cli_show_nodes_getopts parses the command line options for the
 * command `pg_autoctl show nodes`.
 */
static int
cli_show_standby_names_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
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

	/* do not set a default formation, it should be found in the config file */

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

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);
		}
	}
	else
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));
	}

	/* when --pgdata is given, still initialise our pathnames */
	if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
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

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

	(void) cli_set_groupId(&monitor, &config);

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

		json_object_set_string(jsObj, "formation", config.formation);
		json_object_set_number(jsObj, "group", (double) config.groupId);
		json_object_set_string(jsObj,
							   "synchronous_standby_names",
							   synchronous_standby_names);

		(void) cli_pprint_json(js);
	}
	else
	{
		/* current synchronous_standby_names might be an empty string */
		(void) fformat(stdout, "'%s'\n", synchronous_standby_names);
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
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "citus-cluster", required_argument, NULL, 'Z' },
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
				strlcpy(showUriOptions.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", showUriOptions.formation);

				if (strcmp(showUriOptions.formation, "monitor") == 0)
				{
					showUriOptions.monitorOnly = true;
				}

				break;
			}

			case 'Z':
			{
				strlcpy(showUriOptions.citusClusterName, optarg, NAMEDATALEN);
				log_trace("--citus-cluster %s", showUriOptions.citusClusterName);
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

	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);
		}
	}
	else
	{
		cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

		if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
													 options.pgSetup.pgdata))
		{
			if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
														 options.pgSetup.pgdata))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}
		}
	}

	/*
	 * When --citus-cluster is used, but not --formation, then we assume
	 * --formation default
	 */
	if (!IS_EMPTY_STRING_BUFFER(showUriOptions.citusClusterName) &&
		IS_EMPTY_STRING_BUFFER(showUriOptions.formation))
	{
		strlcpy(showUriOptions.formation, FORMATION_DEFAULT, NAMEDATALEN);
	}

	/* use "default" citus cluster name when user didn't provide it */
	if (IS_EMPTY_STRING_BUFFER(showUriOptions.citusClusterName))
	{
		strlcpy(showUriOptions.citusClusterName,
				DEFAULT_CITUS_CLUSTER_NAME, NAMEDATALEN);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_show_uri_monitor_init_from_config initialises a Monitor instance so that
 * we can connect to the monitor and grab information from there. The
 * KeeperConfig instance might belong to a monitor node or to a keeper role.
 *
 * The SSLOptions are read from the configuration file and used to compute the
 * target connection strings.
 */
static void
cli_show_uri_monitor_init_from_config(KeeperConfig *kconfig,
									  Monitor *monitor, SSLOptions *ssl)
{
	switch (ProbeConfigurationFileRole(kconfig->pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			MonitorConfig mconfig = { 0 };

			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;
			char connInfo[MAXCONNINFO];

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &(kconfig->pgSetup),
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

			if (!monitor_init(monitor, connInfo))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			*ssl = mconfig.pgSetup.ssl;

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool monitorDisabledIsOk = false;

			if (!keeper_config_read_file_skip_pgsetup(kconfig,
													  monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!monitor_init(monitor, kconfig->monitor_pguri))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			*ssl = kconfig->pgSetup.ssl;
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  kconfig->pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * cli_show_uri prints the URI to connect to with psql.
 */
static void
cli_show_uri(int argc, char **argv)
{
	KeeperConfig kconfig = keeperOptions;
	Monitor monitor = { 0 };
	SSLOptions ssl = { 0 };

	/*
	 * We are given either --monitor postgres://uri or --pgdata; in the first
	 * case we just connect to that URI, in the second case we read the monitor
	 * URI's from the local configuration file.
	 */
	if (!IS_EMPTY_STRING_BUFFER(kconfig.monitor_pguri))
	{
		if (!monitor_init(&monitor, kconfig.monitor_pguri))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}

		if (!parse_pguri_ssl_settings(kconfig.monitor_pguri, &ssl))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
	else
	{
		/* read the monitor URI from the configuration file */
		(void) cli_show_uri_monitor_init_from_config(&kconfig, &monitor, &ssl);
	}

	if (showUriOptions.monitorOnly)
	{
		(void) print_monitor_uri(&monitor, stdout);
	}
	else if (!IS_EMPTY_STRING_BUFFER(showUriOptions.formation))
	{
		(void) print_formation_uri(&ssl,
								   &monitor,
								   showUriOptions.formation,
								   showUriOptions.citusClusterName,
								   stdout);
	}
	else
	{
		(void) print_all_uri(&ssl, &monitor, stdout);
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
					const char *citusClusterName,
					FILE *stream)
{
	char postgresUri[MAXCONNINFO];

	if (!monitor_formation_uri(monitor,
							   formation,
							   citusClusterName,
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

	pgAutoCtlNodeRole role = ProbeConfigurationFileRole(config.pathnames.config);

	switch (showFileOptions.selection)
	{
		case SHOW_FILE_ALL:
		{
			if (outputJSON)
			{
				JSON_Value *js = json_value_init_object();
				JSON_Object *root = json_value_get_object(js);

				json_object_set_string(root, "config", config.pathnames.config);

				if (role == PG_AUTOCTL_ROLE_KEEPER)
				{
					json_object_set_string(root, "state", config.pathnames.state);
					json_object_set_string(root, "init", config.pathnames.init);
				}

				json_object_set_string(root, "pid", config.pathnames.pid);

				char *serialized_string = json_serialize_to_string_pretty(js);

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
					JSON_Value *js = json_value_init_object();
					bool includeStatus = false;

					(void) pidfile_as_json(js,
										   config.pathnames.pid,
										   includeStatus);

					(void) cli_pprint_json(js);
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
		free(contents);

		return true;
	}
	else
	{
		/* errors have already been logged */
		return false;
	}
}
