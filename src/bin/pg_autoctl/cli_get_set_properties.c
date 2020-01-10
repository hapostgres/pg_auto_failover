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

static bool get_node_replication_settings(NodeReplicationSettings *settings);
static void cli_get_node_replication_quorum(int argc, char **argv);
static void cli_get_node_candidate_priority(int argc, char **argv);
static void cli_get_formation_number_sync_standbys(int argc, char **argv);

static void cli_set_node_property(int arc, char **argv);
static void cli_set_formation_property(int arc, char **argv);

CommandLine get_node_replication_quorum =
	make_command("replication-quorum",
				 "get replication-quorum property for a node from the pg_auto_failover monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_get_node_replication_quorum);

CommandLine get_node_candidate_priority =
	make_command("candidate-priority",
				 "get candidate property for a node from the pg_auto_failover monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_get_node_candidate_priority);


static CommandLine *get_node_subcommands[] = {
	&get_node_replication_quorum,
	&get_node_candidate_priority,
	NULL
};

CommandLine get_node_command =
	make_command_set("node",
				 "get a node property from the pg_auto_failover monitor",
				 NULL, NULL, NULL,
				 get_node_subcommands);

CommandLine get_formation_number_sync_standbys =
	make_command("number-sync-standbys",
				 "get number_sync_standbys for a formation from the pg_auto_failover monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_get_formation_number_sync_standbys);

static CommandLine *get_formation_subcommands[] = {
	&get_formation_number_sync_standbys,
	NULL
};

CommandLine get_formation_command =
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

CommandLine set_node_command =
	make_command("node",
				 "set a property for a node at the pg_auto_failover monitor",
				 CLI_PGDATA_USAGE "{ candidate-priority | replication-quorum } value",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_set_node_property);

CommandLine set_formation_command =
	make_command("formation",
				 "set a property for a formation the pg_auto_failover monitor",
				 CLI_PGDATA_USAGE "{ number-sync-standbys }  value",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_set_formation_property);

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
 * get_node_replication_settings retrieves candidate priority and
 * replication quorum settings for this node from the monitor
 */
static bool
get_node_replication_settings(NodeReplicationSettings *settings)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (keeper.config.monitorDisabled)
	{
		log_error("This node has disabled monitor, "
				  "pg_autoctl get and set commands are not available.");
		return false;
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	return monitor_get_node_replication_settings(&(keeper.monitor),
			keeper.state.current_node_id, settings);
}


/*
 * cli_get_node_replication_quorum function prints
 * replication quorum property of this node to standard output.
 */
static void
cli_get_node_replication_quorum(int argc, char **argv)
{
	NodeReplicationSettings settings = { 0, false};

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get replication quorum value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_boolean(jsObj,
								"replication-quorum",
								settings.replicationQuorum);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout, "%s\n", boolToString(settings.replicationQuorum));
	}
}


/*
 * cli_get_node_candidate_priority function prints
 * candidate priority property of this node to standard output.
 */
static void
cli_get_node_candidate_priority(int argc, char **argv)
{
	NodeReplicationSettings settings = { 0, false};

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get candidate priority value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
								"candidate-priority",
							   (double) settings.candidatePriority);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout, "%d\n", settings.candidatePriority);
	}
}


/*
 * cli_get_formation_number_sync_standbys function prints
 * number sync standbys property of this node to standard output.
 */
static void
cli_get_formation_number_sync_standbys(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	int numberSyncStandbys = 0;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (config.monitorDisabled)
	{
		log_error("This node has disabled monitor, "
				  "pg_autoctl get and set commands are not available.");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_get_formation_number_sync_standbys(&monitor,
													config.formation,
													&numberSyncStandbys))
	{
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
							   "number-sync-standbys",
							   (double) numberSyncStandbys);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout, "%d\n", numberSyncStandbys);
	}
}


/*
 * cli_set_node_property sets a node property on the monitor
 * for to current keeper node
 */
static void
cli_set_node_property(int argc, char **argv)
{
	Keeper keeper = { 0 };
	char *name = NULL;
	char *value = NULL;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (argc != 2)
	{
		log_error("Expected 2 but found %d arguments", argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (keeper.config.monitorDisabled)
	{
		log_error("This node has disabled monitor, "
				  "pg_autoctl get and set commands are not available.");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	name = argv[0];
	value = argv[1];

	if (strcmp(name, "candidate-priority") == 0)
	{
		int candidatePriority = strtol(value, NULL, 10);
		if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
		{
			log_error("candidate-priority value %s is not valid."
					  " Valid values are integers from 0 to 100. ", value);
			exit(EXIT_CODE_BAD_ARGS);
		}

		if (!monitor_set_node_candidate_priority(&(keeper.monitor),
					keeper.state.current_node_id, keeper.config.nodename,
					keeper.config.pgSetup.pgport, candidatePriority))
		{
			log_error("Failed to set \"candidate-priority\" to \"%d\".",
					  candidatePriority);
			exit(EXIT_CODE_MONITOR);
		}

		fprintf(stdout, "%d\n", candidatePriority);
	}
	else if (strcmp(name, "replication-quorum") == 0)
	{
		bool replicationQuorum = false;

		if (!parse_bool(value, &replicationQuorum))
		{
			log_error("replication-quorum value %s is not valid."
					  " Valid values are \"true\" or \"false.", value);

			exit(EXIT_CODE_BAD_ARGS);
		}

		if (!monitor_set_node_replication_quorum(&(keeper.monitor),
					keeper.state.current_node_id, keeper.config.nodename,
					keeper.config.pgSetup.pgport, replicationQuorum))
		{
			log_error("Failed to set \"replication-quorum\" to \"%s\".",
					  boolToString(replicationQuorum));
			exit(EXIT_CODE_MONITOR);
		}

		if (outputJSON)
		{
			char *serialized_string = NULL;
			JSON_Value *js = json_value_init_object();
			JSON_Object *jsObj = json_value_get_object(js);

			json_object_set_boolean(jsObj,
									"replication-quorum",
									replicationQuorum);

			serialized_string = json_serialize_to_string_pretty(js);

			fprintf(stdout, "%s\n", serialized_string);

			json_free_serialized_string(serialized_string);
			json_value_free(js);
		}
		else
		{
			fprintf(stdout, "%s\n", boolToString(replicationQuorum));
		}
	}
	else
	{
		log_error("Unknown node property %s", name);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}
}


/*
 * cli_set_formation_property sets a formation property on the monitor
 * for a formation the current keeper node belongs to.
 */
static void
cli_set_formation_property(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	char *name = NULL;
	char *value = NULL;
	int numberSyncStandbys = -1;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (argc != 2)
	{
		log_error("Expected 2 but found %d arguments", argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	name = argv[0];
	value = argv[1];

	if (strcmp(name, "number-sync-standbys") == 0)
	{
		numberSyncStandbys = strtol(value, NULL, 10);
		if (errno == EINVAL || numberSyncStandbys < 0)
		{
			log_error("number-sync-standbys value %s is not valid."
					  " Expected a non-negative integer value. ", value);
			exit(EXIT_CODE_BAD_ARGS);
		}

	}
	else
	{
		log_error("Unknown formation property %s", name);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (config.monitorDisabled)
	{
		log_error("This node has disabled monitor, "
				  "pg_autoctl get and set commands are not available.");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_set_formation_number_sync_standbys(&monitor,
													config.formation,
													numberSyncStandbys))
	{
		log_error("Failed to set \"number-sync-standbys\" to \"%d\".",
				  numberSyncStandbys);
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_number(jsObj,
							   "number-sync-standbys",
							   (double) numberSyncStandbys);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout, "%d\n", numberSyncStandbys);
	}
}
