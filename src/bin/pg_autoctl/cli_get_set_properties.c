/*
 * src/bin/pg_autoctl/cli_get_set_properties.c
 *     Implementation of a CLI to get and set properties managed by the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include "parson.h"

#include "cli_common.h"
#include "parsing.h"
#include "string_utils.h"

static void cli_ensure_node_name(Keeper *keeper);
static int cli_get_set_properties_getopts(int argc, char **argv);

static bool get_node_replication_settings(NodeReplicationSettings *settings);
static void cli_get_node_replication_quorum(int argc, char **argv);
static void cli_get_node_candidate_priority(int argc, char **argv);
static void cli_get_formation_settings(int argc, char **argv);
static void cli_get_formation_number_sync_standbys(int argc, char **argv);

static void cli_set_node_replication_quorum(int argc, char **argv);
static void cli_set_node_candidate_priority(int argc, char **argv);
static void cli_set_node_metadata(int argc, char **argv);
static void cli_set_formation_number_sync_standbys(int arc, char **argv);

static bool set_node_candidate_priority(Keeper *keeper, int candidatePriority);
static bool set_node_replication_quorum(Keeper *keeper, bool replicationQuorum);
static bool set_formation_number_sync_standbys(Monitor *monitor,
											   char *formation,
											   int groupId,
											   int numberSyncStandbys);

CommandLine get_node_replication_quorum =
	make_command("replication-quorum",
				 "get replication-quorum property from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] [ --name ]",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_set_properties_getopts,
				 cli_get_node_replication_quorum);

CommandLine get_node_candidate_priority =
	make_command("candidate-priority",
				 "get candidate property from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] [ --name ]",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_set_properties_getopts,
				 cli_get_node_candidate_priority);


static CommandLine *get_node_subcommands[] = {
	&get_node_replication_quorum,
	&get_node_candidate_priority,
	NULL
};

static CommandLine get_node_command =
	make_command_set("node",
					 "get a node property from the pg_auto_failover monitor",
					 NULL, NULL, NULL,
					 get_node_subcommands);

static CommandLine get_formation_settings =
	make_command("settings",
				 "get replication settings for a formation from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] ",
				 "  --pgdata      path to data directory\n"
				 "  --json        output data in the JSON format\n"
				 "  --formation   pg_auto_failover formation\n",
				 cli_get_set_properties_getopts,
				 cli_get_formation_settings);

static CommandLine get_formation_number_sync_standbys =
	make_command("number-sync-standbys",
				 "get number_sync_standbys for a formation from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] ",
				 "  --pgdata      path to data directory\n"
				 "  --json        output data in the JSON format\n"
				 "  --formation   pg_auto_failover formation\n",
				 cli_get_set_properties_getopts,
				 cli_get_formation_number_sync_standbys);

static CommandLine *get_formation_subcommands[] = {
	&get_formation_settings,
	&get_formation_number_sync_standbys,
	NULL
};

static CommandLine get_formation_command =
	make_command_set("formation",
					 "get a formation property from the pg_auto_failover monitor",
					 NULL, NULL, NULL,
					 get_formation_subcommands);

static CommandLine *get_subcommands[] = {
	&get_node_command,
	&get_formation_command,
	NULL
};

CommandLine get_commands =
	make_command_set("get",
					 "Get a pg_auto_failover node, or formation setting",
					 NULL, NULL, NULL, get_subcommands);

/* set commands */
static CommandLine set_node_replication_quorum_command =
	make_command("replication-quorum",
				 "set replication-quorum property on the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] [ --name ] "
				 "<true|false>",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_set_properties_getopts,
				 cli_set_node_replication_quorum);

static CommandLine set_node_candidate_priority_command =
	make_command("candidate-priority",
				 "set candidate property on the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] [ --name ] "
				 "<priority: 0..100>",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_set_properties_getopts,
				 cli_set_node_candidate_priority);

static CommandLine set_node_metadata_command =
	make_command("metadata",
				 "set metadata on the monitor",
				 " [ --pgdata --name --hostname --pgport ] ",
				 "  --pgdata      path to data directory\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --hostname    hostname used to connect from other nodes\n"
				 "  --pgport      PostgreSQL's port number\n",
				 cli_node_metadata_getopts,
				 cli_set_node_metadata);


static CommandLine *set_node_subcommands[] = {
	&set_node_metadata_command,
	&set_node_replication_quorum_command,
	&set_node_candidate_priority_command,
	NULL
};

CommandLine set_node_command =
	make_command_set("node",
					 "set a node property on the monitor",
					 NULL, NULL, NULL,
					 set_node_subcommands);

static CommandLine set_formation_number_sync_standby_command =
	make_command("number-sync-standbys",
				 "set number-sync-standbys for a formation on the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] "
				 "<number_sync_standbys>",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_set_properties_getopts,
				 cli_set_formation_number_sync_standbys);

static CommandLine *set_formation_subcommands[] = {
	&set_formation_number_sync_standby_command,
	NULL
};

static CommandLine set_formation_command =
	make_command_set("formation",
					 "set a formation property on the monitor",
					 NULL, NULL, NULL,
					 set_formation_subcommands);


static CommandLine *set_subcommands[] = {
	&set_node_command,
	&set_formation_command,
	NULL
};

CommandLine set_commands =
	make_command_set("set",
					 "Set a pg_auto_failover node, or formation setting",
					 NULL, NULL, NULL, set_subcommands);


/*
 * cli_get_set_properties_getopts parses the command line options for the
 * command `pg_autoctl get|set` commands.
 */
static int
cli_get_set_properties_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "name", required_argument, NULL, 'a' },
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

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'a':
			{
				/* { "name", required_argument, NULL, 'a' }, */
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

	/* now that we have the command line parameters, prepare the options */
	(void) prepare_keeper_options(&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * get_node_replication_settings retrieves candidate priority and
 * replication quorum settings for this node from the monitor
 */
static bool
get_node_replication_settings(NodeReplicationSettings *settings)
{
	Keeper keeper = { 0 };

	keeper.config = keeperOptions;

	if (!monitor_init_from_pgsetup(&keeper.monitor, &keeper.config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* grab --name from either the command options or the configuration file */
	(void) cli_ensure_node_name(&keeper);

	/* copy the target name */
	strlcpy(settings->name, keeper.config.name, _POSIX_HOST_NAME_MAX);

	return monitor_get_node_replication_settings(&(keeper.monitor), settings);
}


/*
 * cli_get_node_replication_quorum function prints
 * replication quorum property of this node to standard output.
 */
static void
cli_get_node_replication_quorum(int argc, char **argv)
{
	NodeReplicationSettings settings = { { 0 }, 0, false };

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get replication quorum value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj, "name", settings.name);

		json_object_set_boolean(jsObj,
								"replication-quorum",
								settings.replicationQuorum);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%s\n", boolToString(settings.replicationQuorum));
	}
}


/*
 * cli_get_node_candidate_priority function prints
 * candidate priority property of this node to standard output.
 */
static void
cli_get_node_candidate_priority(int argc, char **argv)
{
	NodeReplicationSettings settings = { { 0 }, 0, false };

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get candidate priority value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj, "name", settings.name);

		json_object_set_number(jsObj,
							   "candidate-priority",
							   (double) settings.candidatePriority);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%d\n", settings.candidatePriority);
	}
}


typedef struct FormationReplicationSettings
{
	char context[BUFSIZE];
	char setting[BUFSIZE];
	char value[BUFSIZE];
} FormationReplicationSettings;


/*
 * set_formation_replication_setting sets a replication setting entry.
 */
static void
set_formation_replication_setting(FormationReplicationSettings *settings,
								  char *context, char *setting, char *value)
{
	strlcpy(settings->context, context, BUFSIZE);
	strlcpy(settings->setting, setting, BUFSIZE);
	strlcpy(settings->value, value, BUFSIZE);
}


/*
 * print_formation_replication_settings prints an array of replication settings.
 */
static void
print_formation_replication_settings(FormationReplicationSettings *settingsArray,
									 int count)
{
	int contextLen = 7;         /* "Context" */
	int settingLen = 7;         /* "Setting" */
	int valueLen = 5;           /* "Value" */

	char contextSeparator[BUFSIZE] = { 0 };
	char settingSeparator[BUFSIZE] = { 0 };
	char valueSeparator[BUFSIZE] = { 0 };

	for (int i = 0; i < count; i++)
	{
		FormationReplicationSettings *entry = &(settingsArray[i]);

		if (strlen(entry->context) > contextLen)
		{
			contextLen = strlen(entry->context);
		}

		if (strlen(entry->setting) > settingLen)
		{
			settingLen = strlen(entry->setting);
		}

		if (strlen(entry->value) > valueLen)
		{
			valueLen = strlen(entry->value);
		}
	}

	for (int i = 0; i < contextLen; i++)
	{
		contextSeparator[i] = '-';
	}

	for (int i = 0; i < settingLen; i++)
	{
		settingSeparator[i] = '-';
	}

	for (int i = 0; i < valueLen; i++)
	{
		valueSeparator[i] = '-';
	}

	fformat(stdout, "%*s | %*s | %*s\n",
			contextLen, "Context",
			settingLen, "Setting",
			valueLen, "Value");

	fformat(stdout, "%*s-+-%*s-+-%*s\n",
			contextLen, contextSeparator,
			settingLen, settingSeparator,
			valueLen, valueSeparator);

	for (int i = 0; i < count; i++)
	{
		FormationReplicationSettings *entry = &(settingsArray[i]);

		fformat(stdout, "%*s | %*s | %*s\n",
				contextLen, entry->context,
				settingLen, entry->setting,
				valueLen, entry->value);
	}

	fformat(stdout, "\n");
}


/*
 * cli_get_formation_settings function prints the replication settings for a
 * given formation.
 */
static void
cli_get_formation_settings(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	int numberSyncStandbys = 0;
	char synchronous_standby_names[BUFSIZE];
	NodeAddressArray nodesArray = { 0 };

	FormationReplicationSettings settingsArray[NODE_ARRAY_MAX_COUNT + 2] = { 0 };
	int settingsIndex = 0;

	JSON_Value *jsNodes = json_value_init_array();
	JSON_Array *jsNodesArray = json_value_get_array(jsNodes);

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_formation_number_sync_standbys(&monitor,
													config.formation,
													&numberSyncStandbys))
	{
		exit(EXIT_CODE_MONITOR);
	}

	set_formation_replication_setting(&(settingsArray[settingsIndex++]),
									  "formation",
									  "number_sync_standbys",
									  intToString(numberSyncStandbys).strValue);

	if (monitor_synchronous_standby_names(
			&monitor,
			config.formation,
			config.groupId,
			synchronous_standby_names,
			BUFSIZE))
	{
		char quotedStandbyNames[BUFSIZE] = { 0 };

		sformat(quotedStandbyNames, BUFSIZE, "'%s'", synchronous_standby_names);

		set_formation_replication_setting(&(settingsArray[settingsIndex++]),
										  "primary",
										  "synchronous_standby_names",
										  quotedStandbyNames);
	}
	else
	{
		log_warn("Failed to get synchronous_standby_names on the monitor");
	}

	if (!monitor_get_nodes(&monitor,
						   config.formation,
						   config.groupId,
						   &nodesArray))
	{
		log_warn("Failed to get_nodes() on the monitor");
	}

	for (int i = 0; i < nodesArray.count; i++)
	{
		NodeAddress *node = &(nodesArray.nodes[i]);
		NodeReplicationSettings settings = { 0 };

		char prefixedName[BUFSIZE] = { 0 };

		/* copy the target name */
		strlcpy(settings.name, node->name, _POSIX_HOST_NAME_MAX);

		sformat(prefixedName, sizeof(prefixedName), "node %d: \"%s\"",
				node->nodeId, node->name);

		if (!monitor_get_node_replication_settings(&monitor, &settings))
		{
			log_warn("Failed to get replication settings for node %d \"%s\" "
					 "from the monitor",
					 node->nodeId, node->name);
		}

		set_formation_replication_setting(
			&(settingsArray[settingsIndex++]),
			prefixedName,
			"Replication Quorum",
			settings.replicationQuorum ? "true" : "false");

		set_formation_replication_setting(
			&(settingsArray[settingsIndex++]),
			prefixedName,
			"Candidate Priority",
			intToString(settings.candidatePriority).strValue);

		if (outputJSON)
		{
			JSON_Value *jsNode = json_value_init_object();
			JSON_Object *jsNodeObj = json_value_get_object(jsNode);

			json_object_set_number(jsNodeObj, "nodeId", (double) node->nodeId);
			json_object_set_string(jsNodeObj, "name", node->name);
			json_object_set_boolean(jsNodeObj,
									"replicationQuorum",
									settings.replicationQuorum);
			json_object_set_number(jsNodeObj,
								   "candidatePriority",
								   (double) settings.candidatePriority);

			json_array_append_value(jsNodesArray, jsNode);
		}
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		JSON_Value *jsFormation = json_value_init_object();
		JSON_Object *jsFormationObj = json_value_get_object(jsFormation);

		JSON_Value *jsPrimary = json_value_init_object();
		JSON_Object *jsPrimaryObj = json_value_get_object(jsPrimary);

		json_object_set_number(jsFormationObj,
							   "number-sync-standbys",
							   (double) numberSyncStandbys);

		json_object_set_string(jsPrimaryObj,
							   "synchronous_standby_names",
							   synchronous_standby_names);

		json_object_set_value(jsObj, "formation", jsFormation);
		json_object_set_value(jsObj, "primary", jsPrimary);
		json_object_set_value(jsObj, "nodes", jsNodes);

		(void) cli_pprint_json(js);
	}
	else
	{
		print_formation_replication_settings(settingsArray, settingsIndex);
	}
}


/*
 * cli_get_formation_number_sync_standbys function prints
 * number sync standbys property of this formation to standard output.
 */
static void
cli_get_formation_number_sync_standbys(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	int numberSyncStandbys = 0;

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_formation_number_sync_standbys(&monitor,
													config.formation,
													&numberSyncStandbys))
	{
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
							   "number-sync-standbys",
							   (double) numberSyncStandbys);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%d\n", numberSyncStandbys);
	}
}


/*
 * cli_set_node_replication_quorum sets the replication quorum property on the
 * monitor for current pg_autoctl node.
 */
static void
cli_set_node_replication_quorum(int argc, char **argv)
{
	Keeper keeper = { 0 };
	bool replicationQuorum = false;

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!parse_bool(argv[0], &replicationQuorum))
	{
		log_error("replication-quorum value %s is not valid."
				  " Valid values are \"true\" or \"false.", argv[0]);

		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_init_from_pgsetup(&keeper.monitor, &keeper.config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* grab --name from either the command options or the configuration file */
	(void) cli_ensure_node_name(&keeper);

	if (!set_node_replication_quorum(&keeper, replicationQuorum))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_boolean(jsObj,
								"replication-quorum",
								replicationQuorum);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%s\n", boolToString(replicationQuorum));
	}
}


/*
 * cli_set_node_candidate_priority sets the candidate priority property on the
 * monitor for current pg_autoctl node.
 */
static void
cli_set_node_candidate_priority(int argc, char **argv)
{
	Keeper keeper = { 0 };

	int candidatePriority = -1;

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	candidatePriority = strtol(argv[0], NULL, 10);

	if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
	{
		log_error("candidate-priority value %s is not valid."
				  " Valid values are integers from 0 to 100. ", argv[0]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_init_from_pgsetup(&keeper.monitor, &keeper.config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* grab --name from either the command options or the configuration file */
	(void) cli_ensure_node_name(&keeper);

	if (!set_node_candidate_priority(&keeper, candidatePriority))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
							   "candidate-priority",
							   (double) candidatePriority);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%d\n", candidatePriority);
	}
}


/*
 * cli_set_node_metadata sets this pg_autoctl node name, hostname, and port on
 * the monitor. That's the hostname that is used by every other node in the
 * system to contact the local node, so it can be an IP address as well.
 */
static void
cli_set_node_metadata(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);
	Monitor *monitor = &(keeper.monitor);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	KeeperConfig oldConfig = { 0 };

	/* initialize from the command lines options */
	*config = keeperOptions;

	if (IS_EMPTY_STRING_BUFFER(keeperOptions.name) &&
		IS_EMPTY_STRING_BUFFER(keeperOptions.hostname) &&
		keeperOptions.pgSetup.pgport == 0)
	{
		log_error("Please use at least one of "
				  "--nodename, --hostname, or --pgport");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!file_exists(config->pathnames.config))
	{
		log_error("Failed to read configuration file \"%s\"",
				  config->pathnames.config);
	}

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_fatal("Failed to read configuration file \"%s\"",
				  config->pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (config->monitorDisabled)
	{
		log_error("This node has disabled monitor");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* keep a copy */
	oldConfig = *config;

	/*
	 * Now that we have loaded the configuration file, apply the command
	 * line options on top of it, giving them priority over the config.
	 */
	if (!keeper_config_merge_options(config, &keeperOptions))
	{
		/* errors have been logged already */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_init(monitor, config->monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!keeper_set_node_metadata(&keeper, &oldConfig))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (file_exists(config->pathnames.pid))
	{
		if (!cli_pg_autoctl_reload(config->pathnames.pid))
		{
			log_error("Failed to reload the pg_autoctl service, consider "
					  "restarting it to implement the metadata changes");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();

		if (!keeper_config_to_json(config, js))
		{
			log_fatal("Failed to serialize configuration to JSON");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		(void) cli_pprint_json(js);
	}
}


/*
 * cli_set_formation_property sets a formation property on the monitor
 * for a formation the current keeper node belongs to.
 */
static void
cli_set_formation_number_sync_standbys(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	int numberSyncStandbys = -1;

	char synchronous_standby_names[BUFSIZE] = { 0 };

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	numberSyncStandbys = strtol(argv[0], NULL, 10);
	if (errno == EINVAL || numberSyncStandbys < 0)
	{
		log_error("number-sync-standbys value %s is not valid."
				  " Expected a non-negative integer value. ", argv[0]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!set_formation_number_sync_standbys(&monitor,
											config.formation,
											config.groupId,
											numberSyncStandbys))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (monitor_synchronous_standby_names(
			&monitor,
			config.formation,
			config.groupId,
			synchronous_standby_names,
			BUFSIZE))
	{
		log_info("primary node has now set synchronous_standby_names = '%s'",
				 synchronous_standby_names);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
							   "number-sync-standbys",
							   (double) numberSyncStandbys);

		if (!IS_EMPTY_STRING_BUFFER(synchronous_standby_names))
		{
			json_object_set_string(jsObj,
								   "synchronous_standby_names",
								   synchronous_standby_names);
		}

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%d\n", numberSyncStandbys);
	}
}


/*
 * set_node_candidate_priority sets the candidate priority on the monitor, and
 * if we have more than one node registered, waits until the primary has
 * applied the settings.
 */
static bool
set_node_candidate_priority(Keeper *keeper, int candidatePriority)
{
	KeeperConfig *config = &(keeper->config);
	NodeAddressArray nodesArray = { 0 };

	/*
	 * There might be some race conditions here, but it's all to be
	 * user-friendly so in the worst case we're going to be less friendly that
	 * we could have.
	 */
	if (!monitor_get_nodes(&(keeper->monitor),
						   config->formation,
						   config->groupId,
						   &nodesArray))
	{
		/* ignore the error, just don't wait in that case */
		log_warn("Failed to get_nodes() on the monitor");
	}

	/* listen for state changes BEFORE we apply new settings */
	if (nodesArray.count > 1)
	{
		char *channels[] = { "state", NULL };

		if (!pgsql_listen(&(keeper->monitor.pgsql), channels))
		{
			log_error("Failed to listen to state changes from the monitor");
			return false;
		}
	}

	if (!monitor_set_node_candidate_priority(&(keeper->monitor),
											 config->formation,
											 config->name,
											 candidatePriority))
	{
		log_error("Failed to set \"candidate-priority\" to \"%d\".",
				  candidatePriority);
		return false;
	}

	/* now wait until the primary actually applied the new setting */
	if (nodesArray.count > 1)
	{
		if (!monitor_wait_until_primary_applied_settings(
				&(keeper->monitor),
				config->formation))
		{
			log_error("Failed to wait until the new setting has been applied");
			return false;
		}
	}

	return true;
}


/*
 * set_node_replication_quorum sets the replication quorum on the monitor, and
 * if we have more than one node registered, waits until the primary has
 * applied the settings.
 */
static bool
set_node_replication_quorum(Keeper *keeper, bool replicationQuorum)
{
	KeeperConfig *config = &(keeper->config);
	NodeAddressArray nodesArray = { 0 };

	/*
	 * There might be some race conditions here, but it's all to be
	 * user-friendly so in the worst case we're going to be less friendly that
	 * we could have.
	 */
	if (!monitor_get_nodes(&(keeper->monitor),
						   config->formation,
						   config->groupId,
						   &nodesArray))
	{
		/* ignore the error, just don't wait in that case */
		log_warn("Failed to get_nodes() on the monitor");
	}

	/* listen for state changes BEFORE we apply new settings */
	if (nodesArray.count > 1)
	{
		char *channels[] = { "state", NULL };

		if (!pgsql_listen(&(keeper->monitor.pgsql), channels))
		{
			log_error("Failed to listen to state changes from the monitor");
			return false;
		}
	}

	if (!monitor_set_node_replication_quorum(&(keeper->monitor),
											 config->formation,
											 config->name,
											 replicationQuorum))
	{
		log_error("Failed to set \"replication-quorum\" to \"%s\".",
				  boolToString(replicationQuorum));
		return false;
	}

	/* now wait until the primary actually applied the new setting */
	if (nodesArray.count > 1)
	{
		if (!monitor_wait_until_primary_applied_settings(
				&(keeper->monitor),
				config->formation))
		{
			log_error("Failed to wait until the new setting has been applied");
			return false;
		}
	}

	return true;
}


/*
 * set_node_replication_quorum sets the number_sync_standbys on the monitor,
 * and if we have more than one node registered in the target formation, waits
 * until the primary has applied the settings.
 */
static bool
set_formation_number_sync_standbys(Monitor *monitor,
								   char *formation,
								   int groupId,
								   int numberSyncStandbys)
{
	NodeAddressArray nodesArray = { 0 };

	/*
	 * There might be some race conditions here, but it's all to be
	 * user-friendly so in the worst case we're going to be less friendly that
	 * we could have.
	 */
	if (!monitor_get_nodes(monitor, formation, groupId, &nodesArray))
	{
		/* ignore the error, just don't wait in that case */
		log_warn("Failed to get_nodes() on the monitor");
	}

	/* listen for state changes BEFORE we apply new settings */
	if (nodesArray.count > 1)
	{
		char *channels[] = { "state", NULL };

		if (!pgsql_listen(&(monitor->pgsql), channels))
		{
			log_error("Failed to listen to state changes from the monitor");
			return false;
		}
	}

	/* set the new number_sync_standbys value */
	if (!monitor_set_formation_number_sync_standbys(
			monitor,
			formation,
			numberSyncStandbys))
	{
		log_error("Failed to set \"number-sync-standbys\" to \"%d\".",
				  numberSyncStandbys);
		return false;
	}


	/* now wait until the primary actually applied the new setting */
	if (nodesArray.count > 1)
	{
		if (!monitor_wait_until_primary_applied_settings(
				monitor,
				formation))
		{
			log_error("Failed to wait until the new setting has been applied");
			return false;
		}
	}

	return true;
}


/*
 * cli_ensure_node_name ensures that we have a node name to continue with,
 * either from the command line itself, or from the configuration file when
 * we're dealing with a keeper node.
 */
static void
cli_ensure_node_name(Keeper *keeper)
{
	switch (ProbeConfigurationFileRole(keeper->config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			if (IS_EMPTY_STRING_BUFFER(keeper->config.name))
			{
				log_fatal("Please use --name to target a specific node");
				exit(EXIT_CODE_BAD_ARGS);
			}
			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			/* when --name has not been used, fetch it from the config */
			if (IS_EMPTY_STRING_BUFFER(keeper->config.name))
			{
				bool monitorDisabledIsOk = false;

				if (!keeper_config_read_file_skip_pgsetup(&(keeper->config),
														  monitorDisabledIsOk))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_CONFIG);
				}
			}
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  keeper->config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}
