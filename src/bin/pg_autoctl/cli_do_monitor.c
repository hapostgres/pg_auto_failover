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
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "pgctl.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

static void keeper_cli_monitor_get_primary_node(int argc, char **argv);
static void keeper_cli_monitor_get_other_node(int argc, char **argv);
static void keeper_cli_monitor_get_coordinator(int argc, char **argv);
static void keeper_cli_monitor_register_node(int argc, char **argv);
static void keeper_cli_monitor_node_active(int argc, char **argv);
static void cli_monitor_pause_node(int argc, char **argv);
static void cli_monitor_resume_node(int argc, char **argv);
static void cli_monitor_version(int argc, char **argv);


static CommandLine monitor_get_primary_command =
	make_command("primary",
				 "Get the primary node from pg_auto_failover in given formation/group",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_monitor_get_primary_node);

static CommandLine monitor_get_other_node_command =
	make_command("other",
				 "Get the other node from the pg_auto_failover group of nodename/port",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_monitor_get_other_node);

static CommandLine monitor_get_coordinator_command =
	make_command("coordinator",
				 "Get the coordinator node from the pg_auto_failover formation",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_monitor_get_coordinator);

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
				 " [ --pgdata ] <initial state>",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_monitor_register_node);

static CommandLine monitor_node_active_command =
	make_command("active",
				 "Call in the pg_auto_failover Node Active protocol",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_monitor_node_active);

static CommandLine monitor_pause_node_command =
	make_command("pause",
				 "Pause pg_auto_failover on this node",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_monitor_pause_node);

static CommandLine monitor_resume_node_command =
	make_command("resume",
				 "Resume pg_auto_failover on this node",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_monitor_resume_node);

static CommandLine monitor_version_command =
	make_command("version",
				 "Check that monitor version is "
				 PG_AUTOCTL_EXTENSION_VERSION
				 "; alter extension update if not",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_monitor_version);

static CommandLine *monitor_subcommands[] = {
	&monitor_get_command,
	&monitor_register_command,
	&monitor_node_active_command,
	&monitor_pause_node_command,
	&monitor_resume_node_command,
	&monitor_version_command,
	NULL
};

CommandLine do_monitor_commands =
	make_command_set("monitor",
					 "Query a pg_auto_failover monitor", NULL, NULL,
					 NULL, monitor_subcommands);


/*
 * keeper_cli_monitor_get_primary_node contacts the pg_auto_failover monitor and
 * retrieves the primary node information for given formation and group.
 */
static void
keeper_cli_monitor_get_primary_node(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	Monitor monitor = { 0 };
	NodeAddress primaryNode;

	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
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
	fprintf(stdout,
			"%s/%d %s:%d\n",
			config.formation, config.groupId,
			primaryNode.host, primaryNode.port);
}


/*
 * keeper_cli_monitor_get_other_node contacts the pg_auto_failover monitor and
 * retrieves the "other node" information for given nodename and port.
 */
static void
keeper_cli_monitor_get_other_node(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	Monitor monitor = { 0 };
	NodeAddress otherNode = { 0 };

	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
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
	fprintf(stdout,
			"%s/%d %s:%d\n",
			config.formation, config.groupId,
			otherNode.host, otherNode.port);
}


/*
 * keeper_cli_monitor_get_coordinator contacts the pg_auto_failover monitor and
 * retrieves the "coordinator" information for given formation.
 */
static void
keeper_cli_monitor_get_coordinator(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	Monitor monitor = { 0 };
	NodeAddress coordinatorNode = { 0 };

	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
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

	/* output something easy to parse by another program */
	if (IS_EMPTY_STRING_BUFFER(coordinatorNode.host))
	{
		fprintf(stdout, "%s has no coordinator ready yet\n", config.formation);
	}
	else
	{
		fprintf(stdout,
				"%s %s:%d\n",
				config.formation, coordinatorNode.host, coordinatorNode.port);
	}
}


static void
keeper_cli_monitor_register_node(int argc, char **argv)
{
	NodeState initialState = NO_STATE;
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

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
			missing_pgdata_is_ok = true;
			pg_is_not_running_is_ok = true;
			break;
		}

		case SINGLE_STATE:
		{
			missing_pgdata_is_ok = false;
			pg_is_not_running_is_ok = true;
			break;
		}

		case WAIT_STANDBY_STATE:
		{
			missing_pgdata_is_ok = false;
			pg_is_not_running_is_ok = false;
			break;
		}

		default:
		{
			/* let the monitor decide if the situation is supported or not */
			missing_pgdata_is_ok = true;
			pg_is_not_running_is_ok = true;
			break;
		}
	}

	/* The processing of the --pgdata option has set keeperConfigFilePath. */
	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_register_and_init(&keeper, &config, initialState))
	{
		exit(EXIT_CODE_BAD_STATE);
	}

	/* output something easy to parse by another program */
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


/*
 * keeper_cli_monitor_node_active contacts the monitor with the current state
 * of the keeper and get an assigned state from there.
 */
static void
keeper_cli_monitor_node_active(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	MonitorAssignedState assignedState = { 0 };

	/* The processing of the --pgdata option has set keeperConfigFilePath. */
	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
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
							 keeper.postgres.walLag,
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


/*
 * cli_monitor_pause_node calls the pgautofailover.start_maintenance() function
 * on the monitor for the local node.
 */
static void
cli_monitor_pause_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	pid_t pid;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	keeper_config_read_file(&(keeper.config),
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_start_maintenance(&(keeper.monitor),
								   keeper.config.nodename,
								   keeper.config.pgSetup.pgport))
	{
		log_fatal("Failed to start maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		log_error("Failed to read the keeper's PID file at \"%s\": "
				  "is the keeper running?", keeper.config.pathnames.pid);
		exit(EXIT_CODE_KEEPER);
	}

	log_warn("Signaling the keeper process %d with SIGHUP so that  "
			 "it calls pgautofailover.node_active() immediately.",
			 pid);

	if (kill(pid, SIGHUP) != 0)
	{
		log_warn("Failed to send SIGHUP to the keeper's pid %d: %s",
				  pid, strerror(errno));
	}

	log_info("Node %s:%d will reach maintenance state soon",
			 keeper.config.nodename, keeper.config.pgSetup.pgport);
}


/*
 * cli_monitor_resume_node calls pgautofailver.stop_maintenance(name, port) on
 * the monitor.
 */
static void
cli_monitor_resume_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	pid_t pid;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	keeper_config_read_file(&(keeper.config),
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_stop_maintenance(&(keeper.monitor),
								  keeper.config.nodename,
								  keeper.config.pgSetup.pgport))
	{
		log_fatal("Failed to stop maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		log_error("Failed to read the keeper's PID file at \"%s\": "
				  "is the keeper running?", keeper.config.pathnames.pid);
		exit(EXIT_CODE_KEEPER);
	}

	log_warn("Signaling the keeper process %d with SIGHUP so that  "
			 "it calls pgautofailover.node_active() immediately.", pid);

	if (kill(pid, SIGHUP) != 0)
	{
		log_warn("Failed to send SIGHUP to the keeper's pid %d: %s",
				  pid, strerror(errno));
	}

	log_info("Node %s:%d will exit from maintenance state soon",
			 keeper.config.nodename, keeper.config.pgSetup.pgport);
}


/*
 * cli_monitor_version ensures that the version of the monitor is the one that
 * is expected by pg_autoctl too. When that's not the case, the command issues
 * an ALTER EXTENSION ... UPDATE TO ... to ensure that the monitor is now
 * running the expected version number.
 */
static void
cli_monitor_version(int argc, char **argv)
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

	fprintf(stdout, "%s\n", version.installedVersion);
}
