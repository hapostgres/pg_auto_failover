/*
 * src/bin/pg_autoctl/cli_do_coordinator.c
 *     Implementation of a CLI which lets you interact with a Citus coordinator.
 */
#include <inttypes.h>
#include <getopt.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "commandline.h"
#include "coordinator.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "pgctl.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

static void cli_do_coordinator_add_inactive_node(int argc, char **argv);
static void cli_do_coordinator_activate_node(int argc, char **argv);
static void cli_do_coordinator_remove_node(int argc, char **argv);

static void cli_do_coordinator_update_node_prepare(int argc, char **argv);
static void cli_do_coordinator_update_node_commit(int argc, char **argv);
static void cli_do_coordinator_update_node_rollback(int argc, char **argv);


static CommandLine coordinator_add_inactive_node_command =
	make_command("add",
				 "Add this pg_auto_failover node to its formation's coordinator.",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_add_inactive_node);


static CommandLine coordinator_add_activate_command =
	make_command("activate",
				 "Activate this pg_auto_failover node to its formation's coordinator.",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_activate_node);


static CommandLine coordinator_remove_node_command =
	make_command("remove",
				 "Remove this pg_auto_failover node to its formation's coordinator.",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_remove_node);


static CommandLine coordinator_update_prepare_command =
	make_command("prepare",
				 "Prepare transaction for master_update_node on the coordinator",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_update_node_prepare);


static CommandLine coordinator_update_commit_command =
	make_command("commit",
				 "Commit prepared transaction for master_update_node on the coordinator",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_update_node_commit);


static CommandLine coordinator_update_rollback_command =
	make_command("rollback",
				 "Rollback prepared transaction for master_update_node on the coordinator",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_coordinator_update_node_rollback);


static CommandLine *coordinator_update_subcommands[] = {
	&coordinator_update_prepare_command,
	&coordinator_update_commit_command,
	&coordinator_update_rollback_command,
	NULL
};


static CommandLine coordinator_update_commands =
	make_command_set("update",
					 "Update current node's host:port on the coordinator",
					 NULL, NULL,
					 NULL, coordinator_update_subcommands);


static CommandLine *coordinator_commands[] = {
	&coordinator_add_inactive_node_command,
	&coordinator_add_activate_command,
	&coordinator_remove_node_command,
	&coordinator_update_commands,
	NULL
};

CommandLine do_coordinator_commands =
	make_command_set("coordinator",
					 "Query a Citus coordinator", NULL, NULL,
					 NULL, coordinator_commands);


/*
 * cli_do_coordinator_add_inactive_node contacts the Citus coordinator and
 * calls master_add_inactive_node() there.
 */
static void
cli_do_coordinator_add_inactive_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };
	int nodeid = -1;

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_add_inactive_node(&coordinator, &keeper, &nodeid))
	{
		pgsql_finish(&coordinator.pgsql);
		log_fatal("Failed to add current node to the Citus coordinator, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	log_info("Added node %s:%d in formation's %s coordinator %s:%d",
			 keeper.config.hostname, keeper.config.pgSetup.pgport,
			 keeper.config.formation,
			 coordinator.node.host, coordinator.node.port);

	pgsql_finish(&coordinator.pgsql);

	/* output something easy to parse by another program */
	fformat(stdout,
			"%s %s:%d\n",
			keeper.config.formation,
			keeper.config.hostname, keeper.config.pgSetup.pgport);
}


/*
 * cli_do_coordinator_activate_node contacts the Citus coordinator and
 * calls master_activate_node() there.
 */
static void
cli_do_coordinator_activate_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };
	int nodeid = -1;

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_activate_node(&coordinator, &keeper, &nodeid))
	{
		log_fatal("Failed to add current node to the Citus coordinator, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator.pgsql);

	log_info("Activated node %s:%d in formation's %s coordinator %s:%d",
			 keeper.config.hostname, keeper.config.pgSetup.pgport,
			 keeper.config.formation,
			 coordinator.node.host, coordinator.node.port);

	/* output something easy to parse by another program */
	fformat(stdout,
			"%s %s:%d\n",
			keeper.config.formation,
			keeper.config.hostname, keeper.config.pgSetup.pgport);
}


/*
 * cli_do_coordinator_remove_node contacts the Citus coordinator and
 * calls master_remove_node() there.
 */
static void
cli_do_coordinator_remove_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };

	if (!keeper_config_read_file(&(keeper.config),
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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_remove_node(&coordinator, &keeper))
	{
		log_fatal("Failed to remove current node to the Citus coordinator, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	log_info("Removed node %s:%d from the formation %s coordinator %s:%d",
			 keeper.config.hostname, keeper.config.pgSetup.pgport,
			 keeper.config.formation,
			 coordinator.node.host, coordinator.node.port);
}


/*
 * cli_do_coordinator_update_node_prepare contacts the Citus coordinator and
 * calls master_update_node() there in a prepared transaction.
 */
static void
cli_do_coordinator_update_node_prepare(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_update_node_prepare(&coordinator, &keeper))
	{
		log_error("Failed to call master_update_node, see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	log_info("Coordinator is now blocking writes for node %d with a "
			 "prepared transaction calling master_update_node(%d, %s, %d)",
			 keeper.state.current_node_id, keeper.state.current_node_id,
			 keeper.config.hostname, keeper.config.pgSetup.pgport);
}


/*
 * cli_do_coordinator_update_node_commit contacts the Citus coordinator and
 * commits the prepared transaction.
 */
static void
cli_do_coordinator_update_node_commit(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };

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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_update_node_commit(&coordinator, &keeper))
	{
		char transactionName[PREPARED_TRANSACTION_NAMELEN];
		int groupId = keeper.state.current_group;

		GetPreparedTransactionName(groupId, transactionName);

		log_error("Failed to commit prepared transaction '%s', "
				  "see above for details", transactionName);
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!keeper_store_state(&keeper))
	{
		log_error("Failed to save keeper's state in \"%s\"",
				  keeper.config.pathnames.state);
		exit(EXIT_CODE_BAD_STATE);
	}

	log_info("Coordinator has now updated node id %d to %s:%d",
			 keeper.state.current_node_id,
			 keeper.config.hostname, keeper.config.pgSetup.pgport);
}


/*
 * cli_do_coordinator_update_node_commit contacts the Citus coordinator and
 * rolls back the prepared transaction.
 */
static void
cli_do_coordinator_update_node_rollback(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	Monitor monitor = { 0 };
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };

	if (!keeper_config_read_file(&(keeper.config),
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

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!monitor_get_coordinator(&monitor,
								 config.formation,
								 &coordinatorNodeAddress))
	{
		log_fatal("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), &keeper))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!coordinator_update_node_rollback(&coordinator, &keeper))
	{
		char transactionName[PREPARED_TRANSACTION_NAMELEN];
		int groupId = keeper.state.current_group;

		GetPreparedTransactionName(groupId, transactionName);

		log_error("Failed to rollback prepared transaction '%s', "
				  "see above for details", transactionName);
		exit(EXIT_CODE_COORDINATOR);
	}

	if (!keeper_store_state(&keeper))
	{
		log_error("Failed to save keeper's state in \"%s\"",
				  keeper.config.pathnames.state);
		exit(EXIT_CODE_BAD_STATE);
	}

	log_info("Coordinator has now rolled back updating node id %d to %s:%d",
			 keeper.state.current_node_id,
			 keeper.config.hostname, keeper.config.pgSetup.pgport);
}
