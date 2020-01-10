/*
 * src/bin/pg_autoctl/cli_do_monitor.c
 *     Implementation of a CLI which lets you interact with a pg_auto_failover
 *     monitor.
 *
 * The monitor API only makes sense given a local pg_auto_failover keeper
 * setup: we need the formation and group, or the nodename and port, and at
 * registration time we want to create a state file, then at node_active time
 * we need many information obtained in both the configuration and the current
 * state.
 *
 * The `pg_autctl do monitor ...` commands are meant for testing the keeper use
 * of the monitor's API, not just the monitor API itself, so to make use of
 * those commands you need both a running monitor instance and a valid
 * configuration for a local keeper.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "pgctl.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

static void cli_do_monitor_get_primary_node(int argc, char **argv);
static void cli_do_monitor_get_other_node(int argc, char **argv);
static void cli_do_monitor_get_coordinator(int argc, char **argv);
static void cli_do_monitor_register_node(int argc, char **argv);
static void cli_do_monitor_node_active(int argc, char **argv);
static void cli_do_monitor_version(int argc, char **argv);


static CommandLine monitor_get_primary_command =
	make_command("primary",
				 "Get the primary node from pg_auto_failover in given formation/group",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_primary_node);

static CommandLine monitor_get_other_node_command =
	make_command("other",
				 "Get the other node from the pg_auto_failover group of nodename/port",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_other_node);

static CommandLine monitor_get_coordinator_command =
	make_command("coordinator",
				 "Get the coordinator node from the pg_auto_failover formation",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_coordinator);

static CommandLine *monitor_get_commands[] = {
	&monitor_get_primary_command,
	&monitor_get_other_node_command,
	&monitor_get_coordinator_command,
	NULL
};

static CommandLine monitor_get_command =
	make_command_set("get",
					 "Get information from the monitor", NULL, NULL,
					 NULL, monitor_get_commands);

static CommandLine monitor_register_command =
	make_command("register",
				 "Register the current node with the monitor",
				 CLI_PGDATA_USAGE "<initial state>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_register_node);

static CommandLine monitor_node_active_command =
	make_command("active",
				 "Call in the pg_auto_failover Node Active protocol",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_node_active);

static CommandLine monitor_version_command =
	make_command("version",
				 "Check that monitor version is "
				 PG_AUTOCTL_EXTENSION_VERSION
				 "; alter extension update if not",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_version);

static CommandLine *monitor_subcommands[] = {
	&monitor_get_command,
	&monitor_register_command,
	&monitor_node_active_command,
	&monitor_version_command,
	NULL
};

CommandLine do_monitor_commands =
	make_command_set("monitor",
					 "Query a pg_auto_failover monitor", NULL, NULL,
					 NULL, monitor_subcommands);


/*
 * cli_do_monitor_get_primary_node contacts the pg_auto_failover monitor and
 * retrieves the primary node information for given formation and group.
 */
static void
cli_do_monitor_get_primary_node(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	NodeAddress primaryNode;

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_primary(&monitor,
							 config.formation,
							 config.groupId,
							 &primaryNode))
	{
		log_fatal("Failed to get the primary node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_number(root, "groupId", (double) config.groupId);
		json_object_set_string(root, "host", primaryNode.host);
		json_object_set_number(root, "port", (double) primaryNode.port);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout,
				"%s/%d %s:%d\n",
				config.formation, config.groupId,
				primaryNode.host, primaryNode.port);
	}
}


/*
 * cli_do_monitor_get_other_node contacts the pg_auto_failover monitor and
 * retrieves the "other node" information for given nodename and port.
 */
static void
cli_do_monitor_get_other_node(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	Monitor monitor = { 0 };
	NodeAddress otherNode = { 0 };

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_other_node(&monitor,
								config.nodename,
								config.pgSetup.pgport,
								&otherNode))
	{
		log_fatal("Failed to get the other node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_number(root, "groupId", (double) config.groupId);
		json_object_set_string(root, "host", otherNode.host);
		json_object_set_number(root, "port", (double) otherNode.port);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout,
				"%s/%d %s:%d\n",
				config.formation, config.groupId,
				otherNode.host, otherNode.port);
	}
}


/*
 * cli_do_monitor_get_coordinator contacts the pg_auto_failover monitor and
 * retrieves the "coordinator" information for given formation.
 */
static void
cli_do_monitor_get_coordinator(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	NodeAddress coordinatorNode = { 0 };

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor, config.formation, &coordinatorNode))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (IS_EMPTY_STRING_BUFFER(coordinatorNode.host))
	{
		fprintf(stdout, "%s has no coordinator ready yet\n", config.formation);
		exit(EXIT_CODE_QUIT);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_number(root, "groupId", (double) config.groupId);
		json_object_set_string(root, "host", coordinatorNode.host);
		json_object_set_number(root, "port", (double) coordinatorNode.port);

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout,
				"%s %s:%d\n",
				config.formation, coordinatorNode.host, coordinatorNode.port);
	}
}


/*
 * keeper_cli_monitor_register_node registers the current node to the monitor.
 */
static void
cli_do_monitor_register_node(int argc, char **argv)
{
	NodeState initialState = NO_STATE;
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	if (argc != 1)
	{
		log_error("Missing argument: <initial state>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	initialState = NodeStateFromString(argv[0]);

	/*
	 * On the keeper's side we should only accept to register a local node to
	 * the monitor in a state that matches what we have found. A SINGLE node
	 * shoud certainly have a PostgreSQL running already, for instance.
	 *
	 * Then again, we are not overly protective here because we also need this
	 * command to test the monitor's side of handling different kinds of
	 * situations.
	 */
	switch (initialState)
	{
		case NO_STATE:
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}

		case INIT_STATE:
		{
			missingPgdataIsOk = true;
			pgIsNotRunningIsOk = true;
			break;
		}

		case SINGLE_STATE:
		{
			missingPgdataIsOk = false;
			pgIsNotRunningIsOk = true;
			break;
		}

		case WAIT_STANDBY_STATE:
		{
			missingPgdataIsOk = false;
			pgIsNotRunningIsOk = false;
			break;
		}

		default:
		{
			/* let the monitor decide if the situation is supported or not */
			missingPgdataIsOk = true;
			pgIsNotRunningIsOk = true;
			break;
		}
	}

	/* The processing of the --pgdata option has set keeperConfigFilePath. */
	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_register_and_init(&keeper, &config, initialState))
	{
		exit(EXIT_CODE_BAD_STATE);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_string(root, "host", config.nodename);
		json_object_set_number(root, "port", (double) config.pgSetup.pgport);
		json_object_set_number(root, "nodeId",
							   (double) keeper.state.current_node_id);
		json_object_set_number(root, "groupId",
							   (double) keeper.state.current_group);
		json_object_set_string(root, "assigned_role",
							   NodeStateToString(keeper.state.assigned_role));

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout,
				"%s/%d %s:%d %d:%d %s\n",
				config.formation,
				config.groupId,
				config.nodename,
				config.pgSetup.pgport,
				keeper.state.current_node_id,
				keeper.state.current_group,
				NodeStateToString(keeper.state.assigned_role));
	}
}


/*
 * keeper_cli_monitor_node_active contacts the monitor with the current state
 * of the keeper and get an assigned state from there.
 */
static void
cli_do_monitor_node_active(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	MonitorAssignedState assignedState = { 0 };

	/* The processing of the --pgdata option has set keeperConfigFilePath. */
	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * Update our in-memory representation of PostgreSQL state, ignore errors
	 * as in the main loop: we continue with default WAL lag of -1 and an empty
	 * string for pgsrSyncState.
	 */
	(void) keeper_update_pg_state(&keeper);

	if (!monitor_node_active(&keeper.monitor,
							 config.formation,
							 config.nodename,
							 config.pgSetup.pgport,
							 keeper.state.current_node_id,
							 keeper.state.current_group,
							 keeper.state.current_role,
							 keeper.postgres.pgIsRunning,
							 keeper.postgres.currentLSN,
							 keeper.postgres.pgsrSyncState,
							 &assignedState))
	{
		log_fatal("Failed to get the goal state from the node with the monitor, "
				  "see above for details");
		exit(EXIT_CODE_PGSQL);
	}

	if (!keeper_update_state(&keeper, assignedState.nodeId, assignedState.groupId,
							 assignedState.state, true))
	{
		/* log and error but continue, giving more information to the user */
		log_error("Failed to update keepers's state");
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		char *serialized_string = NULL;
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_string(root, "host", config.nodename);
		json_object_set_number(root, "port", (double) config.pgSetup.pgport);
		json_object_set_number(root, "nodeId", (double) assignedState.nodeId);
		json_object_set_number(root, "groupId", (double) assignedState.groupId);
		json_object_set_string(root,
							   "assigned_role",
							   NodeStateToString(assignedState.state));

		serialized_string = json_serialize_to_string_pretty(js);

		fprintf(stdout, "%s\n", serialized_string);

		json_free_serialized_string(serialized_string);
		json_value_free(js);
	}
	else
	{
		fprintf(stdout,
				"%s/%d %s:%d %d:%d %s\n",
				config.formation,
				config.groupId,
				config.nodename,
				config.pgSetup.pgport,
				assignedState.nodeId,
				assignedState.groupId,
				NodeStateToString(assignedState.state));
	}
}


/*
 * cli_monitor_version ensures that the version of the monitor is the one that
 * is expected by pg_autoctl too. When that's not the case, the command issues
 * an ALTER EXTENSION ... UPDATE TO ... to ensure that the monitor is now
 * running the expected version number.
 */
static void
cli_do_monitor_version(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };
	MonitorExtensionVersion version = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* Check version compatibility */
	if (!monitor_ensure_extension_version(&monitor, &version))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		log_warn("This command does not support JSON output at the moment");
	}
	fprintf(stdout, "%s\n", version.installedVersion);
}
