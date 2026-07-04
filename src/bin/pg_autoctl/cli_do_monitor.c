/*
 * src/bin/pg_autoctl/cli_do_monitor.c
 *     Implementation of a CLI which lets you interact with a pg_auto_failover
 *     monitor.
 *
 * The monitor API only makes sense given a local pg_auto_failover keeper
 * setup: we need the formation and group, or the hostname and port, and at
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

#include "parson.h"
#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "nodestate_utils.h"
#include "parsing.h"
#include "pgctl.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pqexpbuffer.h"
#include "state.h"
#include "string_utils.h"

static void cli_do_monitor_get_primary_node(int argc, char **argv);
static void cli_do_monitor_get_other_nodes(int argc, char **argv);
static void cli_do_monitor_get_candidate_count(int argc, char **argv);
static void cli_do_monitor_get_coordinator(int argc, char **argv);
static void cli_do_monitor_register_node(int argc, char **argv);
static void cli_do_monitor_node_active(int argc, char **argv);
static void cli_do_monitor_version(int argc, char **argv);
static void cli_do_monitor_parse_notification(int argc, char **argv);
static int cli_do_monitor_node_state_getopts(int argc, char **argv);
static void cli_do_monitor_node_state(int argc, char **argv);
static int cli_do_monitor_formation_states_getopts(int argc, char **argv);
static void cli_do_monitor_formation_states(int argc, char **argv);


static CommandLine monitor_get_primary_command =
	make_command("primary",
				 "Get the primary node from pg_auto_failover in given formation/group",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_primary_node);

static CommandLine monitor_get_other_nodes_command =
	make_command("others",
				 "Get the other nodes from the pg_auto_failover group of hostname/port",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_other_nodes);

static CommandLine monitor_get_candidate_count_command =
	make_command("candidate-count",
				 "Get the failover candidate count in the group",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_candidate_count);

static CommandLine monitor_get_coordinator_command =
	make_command("coordinator",
				 "Get the coordinator node from the pg_auto_failover formation",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_get_coordinator);

static CommandLine *monitor_get_commands[] = {
	&monitor_get_primary_command,
	&monitor_get_other_nodes_command,
	&monitor_get_candidate_count_command,
	&monitor_get_coordinator_command,
	NULL
};

CommandLine monitor_get_command =
	make_command_set("get",
					 "Get information from the monitor", NULL, NULL,
					 NULL, monitor_get_commands);

CommandLine monitor_register_command =
	make_command("register",
				 "Register the current node with the monitor",
				 CLI_PGDATA_USAGE "<initial state>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_register_node);

CommandLine monitor_node_active_command =
	make_command("active",
				 "Call in the pg_auto_failover Node Active protocol",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_node_active);

CommandLine monitor_version_command =
	make_command("version",
				 "Check that monitor version is "
				 PG_AUTOCTL_EXTENSION_VERSION
				 "; alter extension update if not",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_monitor_version);

CommandLine monitor_parse_notification_command =
	make_command("parse-notification",
				 "parse a raw notification message",
				 " <notification> ",
				 "",
				 NULL,
				 cli_do_monitor_parse_notification);

CommandLine monitor_node_state_command =
	make_command("node-state",
				 "Print reported|assigned state for a named node (for test runners)",
				 "--name <node> [--state <s>] [--monitor <uri>] [--timeout N]",
				 "  --name <node>     node name to query\n"
				 "  --state <s>       wait until reportedstate matches (requires --timeout)\n"
				 "  --monitor <uri>   monitor URI (default: local socket)\n"
				 "  --timeout N       retry for up to N seconds (default: no retry)\n",
				 cli_do_monitor_node_state_getopts,
				 cli_do_monitor_node_state);

CommandLine monitor_formation_states_command =
	make_command("formation-states",
				 "Exit 0 when every listed state has at least one node (for test runners)",
				 "[--group N] [--timeout N] <state1> [<state2>...]",
				 "  --group N         restrict to this group id\n"
				 "  --monitor <uri>   monitor URI (default: local socket)\n"
				 "  --timeout N       retry for up to N seconds (default: no retry)\n",
				 cli_do_monitor_formation_states_getopts,
				 cli_do_monitor_formation_states);

static CommandLine *monitor_subcommands[] = {
	&monitor_get_command,
	&monitor_register_command,
	&monitor_node_active_command,
	&monitor_version_command,
	&monitor_parse_notification_command,
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
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_number(root, "groupId", (double) config.groupId);
		json_object_set_number(root, "nodeId", (double) primaryNode.nodeId);
		json_object_set_string(root, "name", primaryNode.name);
		json_object_set_string(root, "host", primaryNode.host);
		json_object_set_number(root, "port", (double) primaryNode.port);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout,
				"%s/%d %s:%d\n",
				config.formation, config.groupId,
				primaryNode.host, primaryNode.port);
	}
}


/*
 * cli_do_monitor_get_other_nodes contacts the pg_auto_failover monitor and
 * retrieves the "other node" information for given hostname and port.
 */
static void
cli_do_monitor_get_other_nodes(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);
	Monitor *monitor = &(keeper.monitor);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* load the state file to get the node id */
	if (!keeper_init(&keeper, config))
	{
		/* errors are logged in keeper_state_read */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!monitor_init(monitor, config->monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (outputJSON)
	{
		if (!monitor_print_other_nodes_as_json(monitor,
											   keeper.state.current_node_id,
											   ANY_STATE))
		{
			log_fatal("Failed to get the other nodes from the monitor, "
					  "see above for details");
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_other_nodes(monitor,
									   keeper.state.current_node_id,
									   ANY_STATE))
		{
			log_fatal("Failed to get the other nodes from the monitor, "
					  "see above for details");
			exit(EXIT_CODE_MONITOR);
		}
	}
}


/*
 * cli_do_monitor_get_candidate_count contacts the pg_auto_failover monitor and
 * retrieves the current count of failover candidate nodes.
 */
static void
cli_do_monitor_get_candidate_count(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);
	Monitor *monitor = &(keeper.monitor);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* load the state file to get the node id */
	if (!keeper_init(&keeper, config))
	{
		/* errors are logged in keeper_state_read */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!monitor_init(monitor, config->monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	int failoverCandidateCount = 0;

	if (!monitor_count_failover_candidates(monitor,
										   config->formation,
										   config->groupId,
										   &failoverCandidateCount))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config->formation);
		json_object_set_number(root, "groupId", (double) config->groupId);
		json_object_set_number(root, "failoverCandidateCount",
							   (double) failoverCandidateCount);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%d\n", failoverCandidateCount);
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
	CoordinatorNodeAddress coordinatorNode = { 0 };

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

	if (IS_EMPTY_STRING_BUFFER(coordinatorNode.node.host))
	{
		fformat(stdout, "%s has no coordinator ready yet\n", config.formation);
		exit(EXIT_CODE_QUIT);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_number(root, "groupId", (double) config.groupId);
		json_object_set_string(root, "host", coordinatorNode.node.host);
		json_object_set_number(root, "port", (double) coordinatorNode.node.port);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout,
				"%s %s:%d\n",
				config.formation,
				coordinatorNode.node.host,
				coordinatorNode.node.port);
	}
}


/*
 * keeper_cli_monitor_register_node registers the current node to the monitor.
 */
static void
cli_do_monitor_register_node(int argc, char **argv)
{
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

	NodeState initialState = NodeStateFromString(argv[0]);

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

	if (!keeper_register_and_init(&keeper, initialState))
	{
		exit(EXIT_CODE_BAD_STATE);
	}

	/* output something easy to parse by another program */
	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_string(root, "host", config.hostname);
		json_object_set_number(root, "port", (double) config.pgSetup.pgport);
		json_object_set_number(root, "nodeId",
							   (double) keeper.state.current_node_id);
		json_object_set_number(root, "groupId",
							   (double) keeper.state.current_group);
		json_object_set_string(root, "assigned_role",
							   NodeStateToString(keeper.state.assigned_role));

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout,
				"%s/%d %s:%d %d:%d %s\n",
				config.formation,
				config.groupId,
				config.hostname,
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
	(void) keeper_update_pg_state(&keeper, LOG_WARN);

	if (!monitor_node_active(&keeper.monitor,
							 config.formation,
							 keeper.state.current_node_id,
							 keeper.state.current_group,
							 keeper.state.current_role,
							 keeper.postgres.pgIsRunning,
							 keeper.postgres.postgresSetup.control.timeline_id,
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
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "formation", config.formation);
		json_object_set_string(root, "host", config.hostname);
		json_object_set_number(root, "port", (double) config.pgSetup.pgport);
		json_object_set_number(root, "nodeId", (double) assignedState.nodeId);
		json_object_set_number(root, "groupId", (double) assignedState.groupId);
		json_object_set_string(root,
							   "assigned_role",
							   NodeStateToString(assignedState.state));

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout,
				"%s/%d %s:%d %" PRId64 ":%d %s\n",
				config.formation,
				config.groupId,
				config.hostname,
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
	LocalPostgresServer postgres = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) local_postgres_init(&postgres, &(monitor.config.pgSetup));

	/* Check version compatibility */
	if (!monitor_ensure_extension_version(&monitor, &postgres, &version))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		log_warn("This command does not support JSON output at the moment");
	}
	fformat(stdout, "%s\n", version.installedVersion);
}


/*
 * cli_do_monitor_parse_notification parses a raw notification message as given
 * by the monitor LISTEN/NOTIFY protocol on the state channel, such as:
 *
 *   {
 *     "type": "state", "formation": "default", "groupId": 0, "nodeId": 1,
 *     "name": "node_1", "host": "localhost", "port": 5001,
 *     "reportedState": "maintenance", "goalState": "maintenance"
 *   }
 */
static void
cli_do_monitor_parse_notification(int argc, char **argv)
{
	CurrentNodeState nodeState = { 0 };

	JSON_Value *js = json_value_init_object();
	JSON_Object *root = json_value_get_object(js);

	if (argc != 1)
	{
		commandline_print_usage(&monitor_parse_notification_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* errors are logged by parse_state_notification_message */
	if (!parse_state_notification_message(&nodeState, argv[0]))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* log the notification just parsed */
	(void) nodestate_log(&nodeState, LOG_INFO, 0);

	json_object_set_string(root, "name", nodeState.node.name);
	json_object_set_string(root, "hostname", nodeState.node.host);
	json_object_set_number(root, "port", (double) nodeState.node.port);
	json_object_set_string(root, "formationid", nodeState.formation);
	json_object_set_string(root, "reportedState",
						   NodeStateToString(nodeState.reportedState));
	json_object_set_string(root, "goalState",
						   NodeStateToString(nodeState.goalState));

	(void) cli_pprint_json(js);
}


/* -----------------------------------------------------------------------
 * pg_autoctl inspect monitor node-state --name <node>
 * pg_autoctl inspect monitor formation-states [--group N] <s1> [<s2>...]
 *
 * These two commands run ON the monitor container.  They connect to the
 * local PostgreSQL socket and return structured output consumed by the
 * pgaftest test runner via `docker compose exec -T monitor pg_autoctl …`.
 * Using dedicated subcommands (rather than shelling to psql) avoids all
 * shell quoting issues.
 * ----------------------------------------------------------------------- */

#define MONITOR_LOCAL_URI \
	"postgresql://autoctl_node@localhost/pg_auto_failover?sslmode=prefer"

/* Shared option state for both new commands */
static struct
{
	char monitorUri[MAXCONNINFO];
	char nodeName[_POSIX_HOST_NAME_MAX];
	char targetState[64];   /* --state: wait until reportedstate matches */
	char formation[NAMEDATALEN]; /* --formation: filter by formation */
	int groupId;     /* -1 = all groups */
	int timeout;     /* 0 = no retry loop, N = retry for N seconds */
}
inspectOpts;

static int
cli_do_monitor_node_state_getopts(int argc, char **argv)
{
	/* defaults */
	strlcpy(inspectOpts.monitorUri, MONITOR_LOCAL_URI, sizeof(inspectOpts.monitorUri));
	inspectOpts.nodeName[0] = '\0';
	inspectOpts.targetState[0] = '\0';
	inspectOpts.groupId = -1;
	inspectOpts.timeout = 0;

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "name", required_argument, NULL, 'n' },
		{ "state", required_argument, NULL, 's' },
		{ "timeout", required_argument, NULL, 't' },
		{ NULL, 0, NULL, 0 }
	};

	int c;
	optind = 0;
	while ((c = getopt_long(argc, argv, "m:n:s:t:", long_options, NULL)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				strlcpy(inspectOpts.monitorUri, optarg,
						sizeof(inspectOpts.monitorUri));
				break;
			}

			case 'n':
			{
				strlcpy(inspectOpts.nodeName, optarg,
						sizeof(inspectOpts.nodeName));
				break;
			}

			case 's':
			{
				strlcpy(inspectOpts.targetState, optarg,
						sizeof(inspectOpts.targetState));
				break;
			}

			case 't':
			{
				inspectOpts.timeout = atoi(optarg) /* IGNORE-BANNED */;
				break;
			}

			default:
			{
				commandline_print_usage(&monitor_node_state_command, stderr);
				exit(EXIT_CODE_BAD_ARGS);
			}
		}
	}

	if (inspectOpts.nodeName[0] == '\0')
	{
		log_error("--name <node> is required");
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


/*
 * cli_do_monitor_node_state connects to the local monitor and prints:
 *   <reportedstate>|<goalstate>
 * for the named node, then exits 0.  Exits non-zero if the node is not
 * found or the monitor is unreachable.
 *
 * When --timeout N is given the command retries for up to N seconds using the
 * same exponential-backoff-with-jitter policy used elsewhere in pg_autoctl.
 */
static void
cli_do_monitor_node_state(int argc, char **argv)
{
	const char *sql =
		"SELECT reportedstate::text || '|' || goalstate::text || '|' || health::text "
		"FROM pgautofailover.node WHERE nodename = $1";

	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { inspectOpts.nodeName };

	ConnectionRetryPolicy retryPolicy = { 0 };

	if (inspectOpts.timeout > 0)
	{
		pgsql_set_retry_policy(&retryPolicy,
							   inspectOpts.timeout,
							   -1,    /* unbounded attempts within timeout */
							   2000,  /* cap at 2 s between attempts */
							   500);  /* start at 500 ms */
	}
	else
	{
		pgsql_set_retry_policy(&retryPolicy, 0, 0, 0, 0); /* no retry */
	}
	do {
		Monitor monitor = { 0 };

		if (!monitor_init(&monitor, inspectOpts.monitorUri))
		{
			/* connection failure: retry if policy allows */
			goto next_attempt;
		}

		SingleValueResultContext ctx = { { 0 }, PGSQL_RESULT_STRING, false };

		if (!pgsql_execute_with_params(&monitor.pgsql, sql,
									   paramCount, paramTypes, paramValues,
									   &ctx, &parseSingleValueResult))
		{
			pgsql_finish(&monitor.pgsql);
			goto next_attempt;
		}

		pgsql_finish(&monitor.pgsql);

		if (!ctx.parsedOk || ctx.strVal == NULL)
		{
			/* node not found yet */
			if (ctx.strVal)
			{
				free(ctx.strVal);
			}
			goto next_attempt;
		}

		/*
		 * When --state was given, check that reportedstate (the part
		 * before the first '|') matches the target.
		 * Output format: reportedstate|goalstate|health
		 */
		if (inspectOpts.targetState[0] != '\0')
		{
			char reported[64] = "";
			char *pipe = strchr(ctx.strVal, '|');
			if (pipe)
			{
				int len = pipe - ctx.strVal;
				if (len >= (int) sizeof(reported))
				{
					len = sizeof(reported) - 1;
				}
				memcpy(reported, ctx.strVal, len); /* IGNORE-BANNED */
				reported[len] = '\0';
			}
			if (strcmp(reported, inspectOpts.targetState) != 0)
			{
				free(ctx.strVal);
				goto next_attempt;
			}
		}

		fformat(stdout, "%s\n", ctx.strVal);
		free(ctx.strVal);
		exit(0);

next_attempt:
		if (pgsql_retry_policy_expired(&retryPolicy))
		{
			break;
		}

		int sleepMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);
		log_debug("node-state: node \"%s\" not found yet, retrying in %d ms",
				  inspectOpts.nodeName, sleepMs);
		pg_usleep((long) sleepMs * 1000);
	} while (!pgsql_retry_policy_expired(&retryPolicy));

	log_error("Node \"%s\" not found on the monitor", inspectOpts.nodeName);
	exit(EXIT_CODE_MONITOR);
}


static int
cli_do_monitor_formation_states_getopts(int argc, char **argv)
{
	strlcpy(inspectOpts.monitorUri, MONITOR_LOCAL_URI, sizeof(inspectOpts.monitorUri));
	inspectOpts.formation[0] = '\0';
	inspectOpts.groupId = -1;
	inspectOpts.timeout = 0;

	static struct option long_options[] = {
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'F' },
		{ "group", required_argument, NULL, 'g' },
		{ "timeout", required_argument, NULL, 't' },
		{ NULL, 0, NULL, 0 }
	};

	int c;
	optind = 0;
	while ((c = getopt_long(argc, argv, "m:F:g:t:", long_options, NULL)) != -1)
	{
		switch (c)
		{
			case 'm':
			{
				strlcpy(inspectOpts.monitorUri, optarg,
						sizeof(inspectOpts.monitorUri));
				break;
			}

			case 'F':
			{
				strlcpy(inspectOpts.formation, optarg,
						sizeof(inspectOpts.formation));
				break;
			}

			case 'g':
			{
				inspectOpts.groupId = atoi(optarg) /* IGNORE-BANNED */;
				break;
			}

			case 't':
			{
				inspectOpts.timeout = atoi(optarg) /* IGNORE-BANNED */;
				break;
			}

			default:
			{
				commandline_print_usage(&monitor_formation_states_command, stderr);
				exit(EXIT_CODE_BAD_ARGS);
			}
		}
	}

	return optind;
}


/*
 * cli_do_monitor_formation_states checks that every state listed on the
 * command line has at least one node currently in that reportedstate.
 *
 * Exits 0 when all states are satisfied.  When --timeout N is given the
 * command retries for up to N seconds using the same exponential-backoff-
 * with-jitter policy used elsewhere in pg_autoctl; without it the command
 * exits 1 immediately when the condition is not met.
 */
static void
cli_do_monitor_formation_states(int argc, char **argv)
{
	if (argc < 1)
	{
		log_error("Expected at least one state name as argument");
		commandline_print_usage(&monitor_formation_states_command, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

#define MAX_STATES 16
	if (argc > MAX_STATES)
	{
		log_error("Too many states (max %d)", MAX_STATES);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * Build the SQL once; the params are the same across every retry attempt.
	 *
	 *   SELECT (count(*) FILTER (WHERE reportedstate::text = $1))::text
	 *          || ',' ||
	 *          (count(*) FILTER (WHERE reportedstate::text = $2))::text
	 *          ...
	 *   FROM pgautofailover.node
	 *   [WHERE groupid = $N]
	 */
	PQExpBufferData sqlBuf;
	initPQExpBuffer(&sqlBuf);

	appendPQExpBufferStr(&sqlBuf, "SELECT ");
	for (int i = 0; i < argc; i++)
	{
		if (i > 0)
		{
			appendPQExpBufferStr(&sqlBuf, " || ',' || ");
		}
		appendPQExpBuffer(&sqlBuf,
						  "(count(*) FILTER (WHERE reportedstate::text = $%d))::text",
						  i + 1);
	}
	appendPQExpBufferStr(&sqlBuf, " FROM pgautofailover.node");

	bool hasFormation = inspectOpts.formation[0] != '\0';
	bool hasGroup = inspectOpts.groupId >= 0;
	bool needWhere = hasFormation || hasGroup;
	int extraParams = (hasFormation ? 1 : 0) + (hasGroup ? 1 : 0);

	if (needWhere)
	{
		appendPQExpBufferStr(&sqlBuf, " WHERE");
		if (hasFormation)
		{
			appendPQExpBuffer(&sqlBuf, " formationid = $%d", argc + 1);
		}
		if (hasGroup)
		{
			appendPQExpBuffer(&sqlBuf, "%s groupid = $%d",
							  hasFormation ? " AND" : "",
							  argc + (hasFormation ? 2 : 1));
		}
	}

	int paramCount = argc + extraParams;
	Oid paramTypes[MAX_STATES + 2];
	const char *paramValues[MAX_STATES + 2];
	IntString groupIdStr = { 0 };

	for (int i = 0; i < argc; i++)
	{
		paramTypes[i] = TEXTOID;
		paramValues[i] = argv[i];
	}
	if (extraParams > 0)
	{
		int pi = argc;
		if (hasFormation)
		{
			paramTypes[pi] = TEXTOID;
			paramValues[pi] = inspectOpts.formation;
			pi++;
		}
		if (hasGroup)
		{
			groupIdStr = intToString(inspectOpts.groupId);
			paramTypes[pi] = INT4OID;
			paramValues[pi] = groupIdStr.strValue;
		}
	}

	ConnectionRetryPolicy retryPolicy = { 0 };

	if (inspectOpts.timeout > 0)
	{
		pgsql_set_retry_policy(&retryPolicy,
							   inspectOpts.timeout,
							   -1,    /* unbounded attempts within timeout */
							   2000,  /* cap at 2 s between attempts */
							   500);  /* start at 500 ms */
	}
	else
	{
		pgsql_set_retry_policy(&retryPolicy, 0, 0, 0, 0); /* no retry */
	}
	do {
		Monitor monitor = { 0 };

		if (!monitor_init(&monitor, inspectOpts.monitorUri))
		{
			goto next_attempt;
		}

		SingleValueResultContext ctx = { { 0 }, PGSQL_RESULT_STRING, false };

		if (!pgsql_execute_with_params(&monitor.pgsql, sqlBuf.data,
									   paramCount, paramTypes, paramValues,
									   &ctx, &parseSingleValueResult))
		{
			pgsql_finish(&monitor.pgsql);
			goto next_attempt;
		}

		pgsql_finish(&monitor.pgsql);

		if (!ctx.parsedOk || ctx.strVal == NULL)
		{
			if (ctx.strVal)
			{
				free(ctx.strVal);
			}
			goto next_attempt;
		}

		/* parse "N,N,..." — all must be >= 1 */
		bool allMet = true;
		char *p = ctx.strVal;
		for (int i = 0; i < argc; i++)
		{
			int cnt = atoi(p) /* IGNORE-BANNED */;
			if (cnt < 1)
			{
				allMet = false;
				break;
			}
			p = strchr(p, ',');
			if (p)
			{
				p++;
			}
			else
			{
				break;
			}
		}
		free(ctx.strVal);

		if (allMet)
		{
			termPQExpBuffer(&sqlBuf);
			exit(0);
		}

next_attempt:
		if (pgsql_retry_policy_expired(&retryPolicy))
		{
			break;
		}

		int sleepMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);
		log_debug("formation-states: condition not met yet, retrying in %d ms",
				  sleepMs);
		pg_usleep((long) sleepMs * 1000);
	} while (!pgsql_retry_policy_expired(&retryPolicy));

	termPQExpBuffer(&sqlBuf);
	exit(1);
}
