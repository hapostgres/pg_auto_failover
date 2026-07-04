/*
 * src/bin/pg_autoctl/cli_get_set_properties.c
 *     Implementation of a CLI to get and set properties managed by the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <termios.h>
#include <unistd.h>

#include "parson.h"

#include "cli_common.h"
#include "monitor_pg_init.h"
#include "parsing.h"
#include "pgsql.h"
#include "pgsetup.h"
#include "string_utils.h"

static bool get_node_replication_settings(NodeReplicationSettings *settings);
static void cli_get_node_replication_quorum(int argc, char **argv);
static void cli_get_node_candidate_priority(int argc, char **argv);
static void cli_get_formation_number_sync_standbys(int argc, char **argv);

static void cli_set_node_replication_quorum(int argc, char **argv);
static void cli_set_node_candidate_priority(int argc, char **argv);
static void cli_set_node_metadata(int argc, char **argv);
static void cli_set_formation_number_sync_standbys(int arc, char **argv);
static int cli_set_password_getopts(int argc, char **argv);
static void cli_set_password(int argc, char **argv);

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
				 cli_get_name_getopts,
				 cli_get_node_replication_quorum);

CommandLine get_node_candidate_priority =
	make_command("candidate-priority",
				 "get candidate property from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] [ --name ]",
				 "  --pgdata      path to data directory\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --name        pg_auto_failover node name\n"
				 "  --json        output data in the JSON format\n",
				 cli_get_name_getopts,
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
				 cli_get_name_getopts,
				 cli_get_formation_settings);

static CommandLine get_formation_number_sync_standbys =
	make_command("number-sync-standbys",
				 "get number_sync_standbys for a formation from the monitor",
				 " [ --pgdata ] [ --json ] [ --formation ] ",
				 "  --pgdata      path to data directory\n"
				 "  --json        output data in the JSON format\n"
				 "  --formation   pg_auto_failover formation\n",
				 cli_get_name_getopts,
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
				 cli_get_name_getopts,
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
				 cli_get_name_getopts,
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
				 cli_get_name_getopts,
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


static CommandLine set_password_command =
	make_command("password",
				 "set the password for a pg_auto_failover role",
				 " [ --pgdata ] --role <role> [ --password <password> ]",
				 "  --pgdata      path to data directory\n"
				 "  --role        one of: autoctl_node, pgautofailover_monitor,"
				 " pgautofailover_replicator\n"
				 "  --password    new password (prompted if omitted)\n",
				 cli_set_password_getopts,
				 cli_set_password);

static CommandLine *set_subcommands[] = {
	&set_node_command,
	&set_formation_command,
	&set_password_command,
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
	Monitor *monitor = &(keeper.monitor);

	keeper.config = keeperOptions;

	(void) cli_monitor_init_from_option_or_config(monitor, &(keeper.config));

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


/*
 * cli_get_formation_settings function prints the replication settings for a
 * given formation.
 */
void
cli_get_formation_settings(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

	if (outputJSON)
	{
		if (!monitor_print_formation_settings_as_json(&monitor, config.formation))
		{
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		if (!monitor_print_formation_settings(&monitor, config.formation))
		{
			exit(EXIT_CODE_MONITOR);
		}
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

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

	if (!monitor_get_formation_number_sync_standbys(&monitor,
													config.formation,
													&numberSyncStandbys))
	{
		log_error("Failed to get number-sync-standbys for formation \"%s\"",
				  config.formation);
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
	Monitor *monitor = &(keeper.monitor);
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

	(void) cli_monitor_init_from_option_or_config(monitor, &(keeper.config));

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
	Monitor *monitor = &(keeper.monitor);

	keeper.config = keeperOptions;

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	int candidatePriority = strtol(argv[0], NULL, 10);

	if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
	{
		log_error("candidate-priority value %s is not valid."
				  " Valid values are integers from 0 to 100. ", argv[0]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) cli_monitor_init_from_option_or_config(monitor, &(keeper.config));

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

	char synchronous_standby_names[BUFSIZE] = { 0 };

	if (argc != 1)
	{
		log_error("Failed to parse command line arguments: "
				  "got %d when 1 is expected",
				  argc);
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	int numberSyncStandbys = strtol(argv[0], NULL, 10);
	if (errno == EINVAL || numberSyncStandbys < 0)
	{
		log_error("number-sync-standbys value %s is not valid."
				  " Expected a non-negative integer value. ", argv[0]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) cli_monitor_init_from_option_or_config(&monitor, &config);

	/* change the default group when it is still unknown */
	if (config.groupId == -1)
	{
		config.groupId = 0;
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
	CurrentNodeStateArray nodesArray = { 0 };

	/*
	 * There might be some race conditions here, but it's all to be
	 * user-friendly so in the worst case we're going to be less friendly that
	 * we could have.
	 */
	if (!monitor_get_current_state(&(keeper->monitor),
								   config->formation,
								   config->groupId,
								   &nodesArray))
	{
		/* ignore the error, just don't wait in that case */
		log_warn("Failed to get the list of all the nodes in formation \"%s\" "
				 "from the monitor, see above for details",
				 keeper->config.formation);
	}

	/* ignore the result of the filtering, worst case we don't wait */
	(void) nodestateFilterArrayGroup(&nodesArray, config->name);

	/* listen for state changes BEFORE we apply new settings */
	if (nodesArray.count > 1)
	{
		char *channels[] = { "state", NULL };

		if (!pgsql_listen(&(keeper->monitor.notificationClient), channels))
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

		if (!pgsql_listen(&(keeper->monitor.notificationClient), channels))
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

		if (!pgsql_listen(&(monitor->notificationClient), channels))
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
 * Options for `pg_autoctl set password`.
 */
typedef struct SetPasswordOptions
{
	char pgdata[MAXPGPATH];
	char role[NAMEDATALEN];
	char password[MAXCONNINFO];
} SetPasswordOptions;

static SetPasswordOptions setPasswordOptions = {
	{
		0
	}, {
		0
	}, {
		0
	}
};

static int
cli_set_password_getopts(int argc, char **argv)
{
	static struct option longOptions[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "role", required_argument, NULL, 'r' },
		{ "password", required_argument, NULL, 'p' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int c, option_index = 0;

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:r:p:h",
							longOptions, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(setPasswordOptions.pgdata, optarg, MAXPGPATH);
				strlcpy(keeperOptions.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", setPasswordOptions.pgdata);
				break;
			}

			case 'r':
			{
				strlcpy(setPasswordOptions.role, optarg, NAMEDATALEN);
				log_trace("--role %s", setPasswordOptions.role);
				break;
			}

			case 'p':
			{
				strlcpy(setPasswordOptions.password, optarg, MAXCONNINFO);
				log_trace("--password ****");
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
			}

			default:
			{
				log_error("Unrecognized option: %c", c);
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
			}
		}
	}

	if (IS_EMPTY_STRING_BUFFER(setPasswordOptions.role))
	{
		log_error("Please provide --role { autoctl_node | "
				  "pgautofailover_monitor | pgautofailover_replicator }");
		exit(EXIT_CODE_BAD_ARGS);
	}

	return optind;
}


/*
 * prompt_password reads a password from the terminal without echoing it.
 * Returns true on success, false on error.
 */
static bool
prompt_password(const char *prompt, char *buf, size_t buflen)
{
	struct termios old, noecho;
	bool ok = false;

	if (tcgetattr(fileno(stdin), &old) != 0)
	{
		/* stdin is not a tty; just read normally */
		fformat(stderr, "%s", prompt);
		fflush(stderr);

		if (fgets(buf, buflen, stdin) == NULL)
		{
			return false;
		}

		/* strip trailing newline */
		buf[strcspn(buf, "\n")] = '\0';
		return true;
	}

	noecho = old;
	noecho.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);

	if (tcsetattr(fileno(stdin), TCSAFLUSH, &noecho) != 0)
	{
		return false;
	}

	fformat(stderr, "%s", prompt);
	fflush(stderr);

	if (fgets(buf, buflen, stdin) != NULL)
	{
		buf[strcspn(buf, "\n")] = '\0';
		ok = true;
	}

	(void) tcsetattr(fileno(stdin), TCSAFLUSH, &old);
	fformat(stderr, "\n");

	return ok;
}


/*
 * cli_set_password implements `pg_autoctl set password`.
 *
 * Supported roles:
 *   autoctl_node            — lives on the monitor; ALTER ROLE there and
 *                             update monitor config autoctl_node_password
 *   pgautofailover_monitor  — lives on each data node; ALTER ROLE there and
 *                             update keeper config monitor_password
 *   pgautofailover_replicator — lives on each data node; ALTER ROLE there
 *                             and update keeper config replication.password
 */
static void
cli_set_password(int argc, char **argv)
{
	const char *role = setPasswordOptions.role;
	char password[MAXCONNINFO] = { 0 };

	/* validate role name */
	bool isAutoctlNode = (strcmp(role, PG_AUTOCTL_MONITOR_USERNAME) == 0);
	bool isHealthUser = (strcmp(role, PG_AUTOCTL_HEALTH_USERNAME) == 0);
	bool isReplicaUser = (strcmp(role, PG_AUTOCTL_REPLICA_USERNAME) == 0);

	if (!isAutoctlNode && !isHealthUser && !isReplicaUser)
	{
		log_error("Unknown role \"%s\". Valid roles: %s, %s, %s",
				  role,
				  PG_AUTOCTL_MONITOR_USERNAME,
				  PG_AUTOCTL_HEALTH_USERNAME,
				  PG_AUTOCTL_REPLICA_USERNAME);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* obtain the password */
	if (!IS_EMPTY_STRING_BUFFER(setPasswordOptions.password))
	{
		strlcpy(password, setPasswordOptions.password, sizeof(password));
	}
	else
	{
		char prompt[256];
		sformat(prompt, sizeof(prompt), "Password for role %s: ", role);

		if (!prompt_password(prompt, password, sizeof(password)))
		{
			log_error("Failed to read password from terminal");
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (IS_EMPTY_STRING_BUFFER(password))
	{
		log_error("Password may not be empty");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * autoctl_node lives on the MONITOR.  We connect to the monitor's local
	 * Postgres (via MonitorConfig) and ALTER the role there.
	 */
	if (isAutoctlNode)
	{
		MonitorConfig mconfig = { 0 };

		/* resolve pgdata */
		if (!IS_EMPTY_STRING_BUFFER(setPasswordOptions.pgdata))
		{
			strlcpy(mconfig.pgSetup.pgdata,
					setPasswordOptions.pgdata,
					MAXPGPATH);
		}

		if (!monitor_config_set_pathnames_from_pgdata(&mconfig))
		{
			log_fatal("Failed to set monitor config pathnames");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		if (!monitor_config_read_file(&mconfig, false, true))
		{
			log_fatal("Failed to read monitor configuration file");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* connect to the monitor's local postgres */
		PGSQL pgsql = { 0 };
		char connInfo[MAXCONNINFO];
		pg_setup_get_local_connection_string(&mconfig.pgSetup, connInfo);
		pgsql_init(&pgsql, connInfo, PGSQL_CONN_LOCAL);

		if (!pgsql_alter_role_password(&pgsql, role, password))
		{
			log_error("Failed to ALTER ROLE \"%s\" PASSWORD on the monitor", role);
			exit(EXIT_CODE_PGSQL);
		}

		/* persist password into monitor ini */
		strlcpy(mconfig.autoctl_node_password, password,
				sizeof(mconfig.autoctl_node_password));

		if (!monitor_config_write_file(&mconfig))
		{
			log_error("Failed to update monitor configuration file");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		log_info("Password for role \"%s\" updated on the monitor and "
				 "saved to the monitor configuration file", role);
		return;
	}

	/*
	 * pgautofailover_monitor and pgautofailover_replicator both live on the
	 * local data node.  We need a KeeperConfig for the local Postgres
	 * connection details.
	 */
	{
		KeeperConfig config = keeperOptions;
		bool missingPgdataIsOk = false;
		bool pgIsNotRunningIsOk = true;
		bool monitorDisabledIsOk = true;

		if (!IS_EMPTY_STRING_BUFFER(setPasswordOptions.pgdata))
		{
			strlcpy(config.pgSetup.pgdata,
					setPasswordOptions.pgdata,
					MAXPGPATH);
		}

		if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
													 config.pgSetup.pgdata))
		{
			log_fatal("Failed to set keeper config pathnames");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		if (!keeper_config_read_file(&config,
									 missingPgdataIsOk,
									 pgIsNotRunningIsOk,
									 monitorDisabledIsOk))
		{
			log_fatal("Failed to read keeper configuration file");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* connect to the local data node's Postgres */
		PGSQL pgsql = { 0 };
		char connInfo[MAXCONNINFO];
		pg_setup_get_local_connection_string(&config.pgSetup, connInfo);
		pgsql_init(&pgsql, connInfo, PGSQL_CONN_LOCAL);

		if (!pgsql_alter_role_password(&pgsql, role, password))
		{
			log_error("Failed to ALTER ROLE \"%s\" PASSWORD on the local node",
					  role);
			exit(EXIT_CODE_PGSQL);
		}

		/* persist password into keeper ini */
		if (isHealthUser)
		{
			strlcpy(config.monitor_password, password,
					sizeof(config.monitor_password));
		}
		else
		{
			/* isReplicaUser */
			strlcpy(config.replication_password, password,
					sizeof(config.replication_password));
		}

		if (!keeper_config_write_file(&config))
		{
			log_error("Failed to update keeper configuration file");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		log_info("Password for role \"%s\" updated on the local node and "
				 "saved to the keeper configuration file", role);
	}
}
