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

static void cli_set_node_replication_quorum(int argc, char **argv);
static void cli_set_node_candidate_priority(int argc, char **argv);
static void cli_set_node_nodename(int argc, char **argv);
static void cli_set_formation_number_sync_standbys(int arc, char **argv);

static bool set_node_candidate_priority(Keeper *keeper, int candidatePriority);
static bool set_node_replication_quorum(Keeper *keeper, bool replicationQuorum);
static bool set_node_nodename(Keeper *keeper, const char *nodename);
static bool set_formation_number_sync_standbys(Monitor *monitor,
											   char *formation,
											   int groupId,
											   int numberSyncStandbys);

CommandLine get_node_replication_quorum =
	make_command("replication-quorum",
				 "get replication-quorum property from the monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_get_node_replication_quorum);

CommandLine get_node_candidate_priority =
	make_command("candidate-priority",
				 "get candidate property from the monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
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

static CommandLine get_formation_number_sync_standbys =
	make_command("number-sync-standbys",
				 "get number_sync_standbys for a formation from the monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_get_formation_number_sync_standbys);

static CommandLine *get_formation_subcommands[] = {
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
				 CLI_PGDATA_USAGE "<true|false>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_set_node_replication_quorum);

static CommandLine set_node_candidate_priority_command =
	make_command("candidate-priority",
				 "set candidate property on the monitor",
				 CLI_PGDATA_USAGE "<priority: 0..100>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_set_node_candidate_priority);

static CommandLine set_node_nodename_command =
	make_command("nodename",
				 "set nodename on the monitor",
				 CLI_PGDATA_USAGE "<hostname|ipaddr>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_set_node_nodename);


static CommandLine *set_node_subcommands[] = {
	&set_node_replication_quorum_command,
	&set_node_candidate_priority_command,
	&set_node_nodename_command,
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
				 CLI_PGDATA_USAGE "<number_sync_standbys>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
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
	NodeReplicationSettings settings = { 0, false };

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get replication quorum value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

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
	NodeReplicationSettings settings = { 0, false };

	if (!get_node_replication_settings(&settings))
	{
		log_error("Unable to get candidate priority value from monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

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

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
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

	if (!parse_bool(argv[0], &replicationQuorum))
	{
		log_error("replication-quorum value %s is not valid."
				  " Valid values are \"true\" or \"false.", argv[0]);

		exit(EXIT_CODE_BAD_ARGS);
	}

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

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
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

	candidatePriority = strtol(argv[0], NULL, 10);

	if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
	{
		log_error("candidate-priority value %s is not valid."
				  " Valid values are integers from 0 to 100. ", argv[0]);
		exit(EXIT_CODE_BAD_ARGS);
	}

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
 * cli_set_node_nodename sets this pg_autoctl "nodename" on the monitor. That's
 * the hostname that is used by every other node in the system to contact the
 * local node, so it might as well be an IP address.
 */
static void
cli_set_node_nodename(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
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

	if (!set_node_nodename(&keeper, argv[0]))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(js);

		json_object_set_string(jsObj, "nodename", argv[0]);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "%s\n", argv[0]);
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

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

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

	if (!monitor_set_node_candidate_priority(
			&(keeper->monitor),
			keeper->state.current_node_id,
			config->hostname,
			config->pgSetup.pgport,
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

	if (!monitor_set_node_replication_quorum(
			&(keeper->monitor),
			keeper->state.current_node_id,
			config->hostname,
			config->pgSetup.pgport,
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
 * set_node_nodename sets a new nodename for the current pg_autoctl node on the
 * monitor. This node might be in an environment where you might get a new IP
 * at reboot, such as in Kubernetes.
 */
static bool
set_node_nodename(Keeper *keeper, const char *nodename)
{
	KeeperStateData keeperState = { 0 };
	int nodeId = -1;

	if (!keeper_state_read(&keeperState, keeper->config.pathnames.state))
	{
		/* errors have already been logged */
		return false;
	}

	nodeId = keeperState.current_node_id;

	if (!monitor_set_nodename(&(keeper->monitor), nodeId, nodename))
	{
		/* errors have already been logged */
		return false;
	}

	strlcpy(keeper->config.nodename, nodename, _POSIX_HOST_NAME_MAX);

	if (!keeper_config_write_file(&(keeper->config)))
	{
		log_warn("This node nodename has been updated to \"%s\" on the monitor "
				 "but could not be update in the local configuration file!",
				 nodename);
		return false;
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
