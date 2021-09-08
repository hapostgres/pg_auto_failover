/*
 * src/bin/pg_autoctl/keeper_init.c
 *     Keeper initialisation.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>

#include "cli_common.h"
#include "debian.h"
#include "defaults.h"
#include "env_utils.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "parsing.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "service_keeper_init.h"
#include "signals.h"
#include "state.h"


/*
 * We keep track of the fact that we had non-fatal warnings during `pg_autoctl
 * keeper init`: in that case the init step is considered successful, yet users
 * have extra actions to take care of.
 *
 * The only such case supported as of now is failure to `master_activate_node`.
 * In that case the `pg_autoctl create` job is done: we have registered the
 * node to the monitor and the coordinator. The operator should now take action
 * to make it possible to activate the node, and those actions require a
 * running PostgreSQL instance.
 */
bool keeperInitWarnings = false;

static bool keeper_pg_init_and_register_primary(Keeper *keeper);
static bool reach_initial_state(Keeper *keeper);
static bool exit_if_dropped(Keeper *keeper);
static bool wait_until_primary_is_ready(Keeper *config,
										MonitorAssignedState *assignedState);
static bool wait_until_primary_has_created_our_replication_slot(Keeper *keeper,
																MonitorAssignedState *
																assignedState);
static bool keeper_pg_init_node_active(Keeper *keeper);

/*
 * keeper_pg_init initializes a pg_autoctl keeper and its local PostgreSQL.
 *
 * Depending on whether we have a monitor or not in the config (see
 * --without-monitor), then we call into keeper_pg_init_and_register or
 * keeper_pg_init_fsm.
 */
bool
keeper_pg_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	log_trace("keeper_pg_init: monitor is %s",
			  config->monitorDisabled ? "disabled" : "enabled");

	return service_keeper_init(keeper);
}


/*
 * keeper_pg_init_and_register initializes a pg_autoctl keeper and its local
 * PostgreSQL instance. Registering a PostgreSQL instance to the monitor is a 3
 * states story:
 *
 * - register as INIT, the monitor decides your role (primary or secondary),
 *   and the keeper only does that when the local PostgreSQL instance does not
 *   exist yet.
 *
 * - register as SINGLE, when a PostgreSQL instance exists and is not in
 *   recovery.
 *
 * - register as INIT then being assigned WAIT_STANDBY, then the keeper should
 *   busy loop (every 1s or something) until the Primary state is WAIT_STANDBY,
 *   so that we can pg_basebackup and move through the CATCHINGUP state.
 *
 * In any case, the Keeper implements the first transition after registration
 * directly, within the `pg_autoctl create` command itself, not waiting until
 * the first loop when the keeper service starts. Once `pg_autoctl create` is
 * done, PostgreSQL is known to be running in the proper state.
 */
bool
keeper_pg_init_and_register(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	/*
	 * The initial state we may register in depend on the current PostgreSQL
	 * instance that might exist or not at PGDATA.
	 */
	PostgresSetup *pgSetup = &(config->pgSetup);
	bool postgresInstanceExists = pg_setup_pgdata_exists(pgSetup);
	bool postgresInstanceIsRunning = pg_setup_is_running(pgSetup);
	PostgresRole postgresRole = pg_setup_role(pgSetup);
	bool postgresInstanceIsPrimary = postgresRole == POSTGRES_ROLE_PRIMARY;

	if (postgresInstanceExists)
	{
		if (!keeper_ensure_pg_configuration_files_in_pgdata(pgSetup))
		{
			log_fatal("Failed to setup your Postgres instance "
					  "the PostgreSQL way, see above for details");
			return false;
		}
	}

	/*
	 * If we don't have a state file, we consider that we're initializing from
	 * scratch and can move on, nothing to do here.
	 */
	if (file_exists(config->pathnames.init))
	{
		return keeper_pg_init_continue(keeper);
	}

	/*
	 * If we have a state file, we're either running the same command again
	 * (such as pg_autoctl create postgres --run ...) or maybe the user has
	 * changed their mind after having done a pg_autoctl drop node.
	 */
	if (file_exists(config->pathnames.state))
	{
		bool dropped = false;

		/* initialize our local Postgres instance representation */
		LocalPostgresServer *postgres = &(keeper->postgres);

		(void) local_postgres_init(postgres, pgSetup);

		if (!keeper_ensure_node_has_been_dropped(keeper, &dropped))
		{
			log_fatal("Failed to determine if node %d with current state \"%s\""
					  " in formation \"%s\" and group %d"
					  " has been dropped from the monitor, see above for details",
					  keeper->state.current_node_id,
					  NodeStateToString(keeper->state.current_role),
					  keeper->config.formation,
					  keeper->config.groupId);
			return false;
		}

		if (dropped)
		{
			log_info("This node had been dropped previously, now trying to "
					 "register it again");
		}

		/*
		 * If the node has not been dropped previously, then the state file
		 * indicates a second run of pg_autoctl create postgres command, and
		 * when given --run we start the service normally.
		 *
		 * If dropped is true, the node has been dropped in the past and the
		 * user is trying to cancel the pg_autoctl drop node command by doing a
		 * pg_autoctl create postgres command again. Just continue then.
		 */
		if (!dropped)
		{
			if (createAndRun)
			{
				if (!keeper_init(keeper, config))
				{
					return false;
				}
			}
			else
			{
				log_fatal("The state file \"%s\" exists and "
						  "there's no init in progress",
						  config->pathnames.state);
				log_info("HINT: use `pg_autoctl run` to start the service.");
				exit(EXIT_CODE_QUIT);
			}
			return createAndRun;
		}
	}

	/*
	 * When the monitor is disabled, we're almost done. All that is left is
	 * creating a state file with our nodeId as from the --node-id parameter.
	 * The value is found in the global variable monitorDisabledNodeId.
	 */
	if (config->monitorDisabled)
	{
		return keeper_init_fsm(keeper);
	}

	char scrubbedConnectionString[MAXCONNINFO] = { 0 };
	if (!parse_and_scrub_connection_string(config->monitor_pguri,
										   scrubbedConnectionString))
	{
		log_error("Failed to parse the monitor connection string");
		return false;
	}

	/*
	 * If the local Postgres instance does not exist, we have two possible
	 * choices: either we're the only one in our group, or we are joining a
	 * group that already exists.
	 *
	 * The situation is decided by the Monitor, which implements transaction
	 * semantics and safe concurrency approach, needed here in case other
	 * keeper are concurrently registering other nodes.
	 *
	 * So our strategy is to ask the monitor to pick a state for us and then
	 * implement whatever was decided. After all PGDATA does not exist yet so
	 * we can decide to either pg_ctl initdb or pg_basebackup to create it.
	 */
	if (!postgresInstanceExists)
	{
		if (!keeper_register_and_init(keeper, INIT_STATE))
		{
			log_error("Failed to register the existing local Postgres node "
					  "\"%s:%d\" running at \"%s\""
					  "to the pg_auto_failover monitor at %s, "
					  "see above for details",
					  config->hostname, config->pgSetup.pgport,
					  config->pgSetup.pgdata, scrubbedConnectionString);
			return false;
		}

		log_info("Successfully registered as \"%s\" to the monitor.",
				 NodeStateToString(keeper->state.assigned_role));

		return reach_initial_state(keeper);
	}

	/*
	 * Ok so there's already a Postgres instance that exists in $PGDATA.
	 *
	 * If it's running and is a primary, we can register it as it is and expect
	 * a SINGLE state from the monitor.
	 *
	 * If it's running and is not a primary, we don't know how to handle the
	 * situation yet: the already existing secondary is using its own
	 * replication slot and primary conninfo string (with username, password,
	 * SSL setup, etc).
	 */
	if (postgresInstanceIsRunning)
	{
		if (postgresInstanceIsPrimary)
		{
			log_info("Registering Postgres system %" PRIu64
					 " running on port %d with pid %d found at \"%s\"",
					 pgSetup->control.system_identifier,
					 pgSetup->pidFile.port,
					 pgSetup->pidFile.pid,
					 pgSetup->pgdata);

			return keeper_pg_init_and_register_primary(keeper);
		}
		else
		{
			log_error("pg_autoctl doesn't know how to register an already "
					  "existing standby server at the moment");
			return false;
		}
	}

	/*
	 * Ok so there's a Postgres instance that exists in $PGDATA and it's not
	 * running at the moment. We have run pg_controldata on the instance and we
	 * do have its system_identifier. Using it to register, we have two cases:
	 *
	 * - either we are the first node in our group and all is good, we can
	 *   register the current PGDATA as a SINGLE, maybe promoting it to being a
	 *   primary,
	 *
	 * - or a primary node already is registered in our group, and we are going
	 *   to join it as a secondary: that is only possible when the
	 *   system_identifier of the other nodes in the group are all the same,
	 *   which the monitor checks for us in a way that registration fails when
	 *   that's not the case.
	 */
	if (postgresInstanceExists && !postgresInstanceIsRunning)
	{
		log_info("Registering Postgres system %" PRIu64 " found at \"%s\"",
				 pgSetup->control.system_identifier,
				 pgSetup->pgdata);

		if (!keeper_register_and_init(keeper, INIT_STATE))
		{
			log_error("Failed to register the existing local Postgres node "
					  "\"%s:%d\" running at \"%s\""
					  "to the pg_auto_failover monitor at %s, "
					  "see above for details",
					  config->hostname, config->pgSetup.pgport,
					  config->pgSetup.pgdata, scrubbedConnectionString);
			return false;
		}

		log_info("Successfully registered as \"%s\" to the monitor.",
				 NodeStateToString(keeper->state.assigned_role));

		return reach_initial_state(keeper);
	}

	/* unknown case, the logic above is faulty, at least admit we're defeated */
	log_error("Failed to recognise the current initialisation environment");

	log_debug("pg exists: %s", postgresInstanceExists ? "yes" : "no");
	log_debug("pg is primary: %s", postgresInstanceIsPrimary ? "yes" : "no");

	return false;
}


/*
 * keeper_pg_init_and_register_primary registers a local Postgres instance that
 * is known to be a primary: Postgres is running and SELECT pg_is_in_recovery()
 * returns false.
 */
static bool
keeper_pg_init_and_register_primary(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	char absolutePgdata[PATH_MAX];
	char scrubbedConnectionString[MAXCONNINFO] = { 0 };
	if (!parse_and_scrub_connection_string(config->monitor_pguri,
										   scrubbedConnectionString))
	{
		log_error("Failed to parse the monitor connection string");
		return false;
	}

	log_info("A postgres directory already exists at \"%s\", registering "
			 "as a single node",
			 realpath(pgSetup->pgdata, absolutePgdata));

	/* register to the monitor in the expected state directly */
	if (!keeper_register_and_init(keeper, SINGLE_STATE))
	{
		log_error("Failed to register the existing local Postgres node "
				  "\"%s:%d\" running at \"%s\""
				  "to the pg_auto_failover monitor at %s, "
				  "see above for details",
				  config->hostname, config->pgSetup.pgport,
				  config->pgSetup.pgdata, scrubbedConnectionString);
	}

	log_info("Successfully registered as \"%s\" to the monitor.",
			 NodeStateToString(keeper->state.assigned_role));

	return reach_initial_state(keeper);
}


/*
 * keeper_pg_init_continue attempts to continue a `pg_autoctl create` that
 * failed through in the middle. A particular case of interest is trying to
 * init with a stale file lying around.
 *
 * When we initialize and register to the monitor, we create two files: the
 * init file and the state file. When the init is done, we remove the init file
 * and never create it again. Which means that when the init file exists, we
 * know we were interrupted in the middle of the init step, after having
 * registered to the monitor: that's when we create the init file.
 */
bool
keeper_pg_init_continue(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperStateInit *initState = &(keeper->initState);
	KeeperConfig *config = &(keeper->config);

	/* initialize our keeper state and read the state file */
	if (!keeper_init(keeper, config))
	{
		/* errors have already been logged */
		return false;
	}

	/* also read the init state file */
	if (!keeper_init_state_read(initState, config->pathnames.init))
	{
		log_fatal("Failed to restart from previous keeper init attempt");
		log_info("HINT: use `pg_autoctl drop node` to retry in a clean state");
		return false;
	}

	log_info("Continuing from a previous `pg_autoctl create` failed attempt");
	log_info("PostgreSQL state at registration time was: %s",
			 PreInitPostgreInstanceStateToString(initState->pgInitState));

	/*
	 * TODO: verify the information in the state file against the information
	 * in the monitor and decide if it's stale or not.
	 */

	/*
	 * Also update the groupId and replication slot name in the configuration
	 * file, from the keeper state file: we might not have reached a point
	 * where the configuration changes have been saved to disk in the previous
	 * attempt.
	 */
	if (!keeper_config_update(&(keeper->config),
							  keeperState->current_node_id,
							  keeperState->current_group))
	{
		log_error("Failed to update the configuration file with the groupId %d "
				  "and the nodeId %d",
				  keeperState->current_group,
				  keeperState->current_node_id);
		return false;
	}

	/*
	 * If we have an init file and the state file looks good, then the
	 * operation that failed was removing the init state file.
	 */
	if (keeper->state.current_role == keeper->state.assigned_role &&
		(keeper->state.current_role == SINGLE_STATE ||
		 keeper->state.current_role == CATCHINGUP_STATE))
	{
		return unlink_file(config->pathnames.init);
	}

	if (config->monitorDisabled)
	{
		return true;
	}
	else
	{
		return reach_initial_state(keeper);
	}
}


/*
 * reach_initial_state implements the first FSM transition.
 *
 * When asked by the monitor to reach the WAIT_STANDBY state, we know we are
 * going to then move forward to the CATCHINGUP state, and this is the
 * interesting transition here: we might fail to setup the Streaming
 * Replication.
 *
 * Being nice to the user, we're going to implement that extra step during the
 * `pg_autoctl create` command, so that we can detect and fix any error before
 * sarting as a service.
 */
static bool
reach_initial_state(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	log_trace("reach_initial_state: %s to %s",
			  NodeStateToString(keeper->state.current_role),
			  NodeStateToString(keeper->state.assigned_role));

	/*
	 * To move from current_role to assigned_role, we call in the FSM.
	 */
	if (!keeper_fsm_reach_assigned_state(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * We have extra work to do after the FSM transition is done.
	 *
	 * The goal here is to be as user friendly as possible: make sure that when
	 * the initialization is done, our pg_auto_failover situation is as
	 * expected. So we go the extra mile here.
	 */
	switch (keeper->state.assigned_role)
	{
		case CATCHINGUP_STATE:
		{
			/*
			 * Well we're good then, there's nothing else for us to do.
			 *
			 * This might happen when doing `pg_autoctl create` on an already
			 * initialized cluster, or when running the command for the second
			 * time after fixing a glitch in the setup or the environment.
			 */
			break;
		}

		case WAIT_STANDBY_STATE:
		{
			/*
			 * Now the transition from INIT_STATE to WAIT_STANDBY_STATE consist
			 * of doing nothing on the keeper's side: we are just waiting until
			 * the primary has updated its HBA setup with our hostname.
			 */
			MonitorAssignedState assignedState = { 0 };

			/* busy loop until we are asked to be in CATCHINGUP_STATE */
			if (!wait_until_primary_is_ready(keeper, &assignedState))
			{
				/* the node might have been dropped early */
				return exit_if_dropped(keeper);
			}

			/*
			 * Now that we are asked to catch up, it means the primary is ready
			 * for us to pg_basebackup, which allows the local instance to then
			 * reach goal state SECONDARY:
			 */
			if (!keeper_fsm_reach_assigned_state(keeper))
			{
				/*
				 * One reason why we failed to reach the CATCHING-UP state is
				 * that we've been DROPPED while doing the pg_basebackup or
				 * some other step of that migration. Check about that now.
				 */
				return exit_if_dropped(keeper);
			}

			/*
			 * Because we did contact the monitor, we need to update our
			 * partial local cache of the monitor's state. That updates the
			 * cache both in memory and on-disk.
			 */
			if (!keeper_update_state(keeper,
									 assignedState.nodeId,
									 assignedState.groupId,
									 assignedState.state,
									 true))
			{
				log_error("Failed to update keepers's state");
				return false;
			}

			/*
			 * We insist on using the realpath(3) for PGDATA in the config, and
			 * now is a good time to check this, because we just created the
			 * directory.
			 */
			if (!keeper_config_update_with_absolute_pgdata(&(keeper->config)))
			{
				/* errors have already been logged */
				return false;
			}

			break;
		}

		case SINGLE_STATE:
		{
			/* it's all done in the INIT ➜ SINGLE transition now. */
			break;
		}

		case REPORT_LSN_STATE:
		{
			/* all the work is done in the INIT ➜ REPORT_LSN transition */
			break;
		}

		default:

			/* we don't support any other state at initialization time */
			log_error("reach_initial_state: don't know how to read state %s",
					  NodeStateToString(keeper->state.assigned_role));
			return false;
	}

	/*
	 * The initialization is done, publish the new current state to the
	 * monitor.
	 */
	if (!keeper_pg_init_node_active(keeper))
	{
		/* errors have been logged already */
		return false;
	}

	/* everything went fine, get rid of the init state file */
	return unlink_file(config->pathnames.init);
}


/*
 * exit_if_dropped checks if the node has been dropped during its
 * initialization phase, and if that's the case, finished the DROP protocol and
 * exits with a specific exit code.
 */
static bool
exit_if_dropped(Keeper *keeper)
{
	bool dropped = false;

	if (!keeper_ensure_node_has_been_dropped(keeper, &dropped))
	{
		log_fatal(
			"Failed to determine if node %d with current state \"%s\" "
			" in formation \"%s\" and group %d "
			"has been dropped from the monitor, see above for details",
			keeper->state.current_node_id,
			NodeStateToString(keeper->state.current_role),
			keeper->config.formation,
			keeper->config.groupId);
		return false;
	}

	if (dropped)
	{
		log_fatal("This node has been dropped from the monitor");
		exit(EXIT_CODE_DROPPED);
	}

	return false;
}


/*
 * wait_until_primary_is_ready calls monitor_node_active every second until the
 * monitor tells us that we can move from our current state
 * (WAIT_STANDBY_STATE) to CATCHINGUP_STATE, which only happens when the
 * primary successfully prepared for Streaming Replication.
 */
static bool
wait_until_primary_is_ready(Keeper *keeper,
							MonitorAssignedState *assignedState)
{
	bool pgIsRunning = false;
	int currentTLI = 1;
	char currrentLSN[PG_LSN_MAXLENGTH] = "0/0";
	char *pgsrSyncState = "";
	int errors = 0, tries = 0;
	bool firstLoop = true;

	/* wait until the primary is ready for us to pg_basebackup */
	do {
		bool groupStateHasChanged = false;

		if (firstLoop)
		{
			firstLoop = false;
		}
		else
		{
			Monitor *monitor = &(keeper->monitor);
			KeeperStateData *keeperState = &(keeper->state);
			int timeoutMs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000;

			(void) pgsql_prepare_to_wait(&(monitor->notificationClient));
			(void) monitor_wait_for_state_change(monitor,
												 keeper->config.formation,
												 keeperState->current_group,
												 keeperState->current_node_id,
												 timeoutMs,
												 &groupStateHasChanged);

			/* when no state change has been notified, close the connection */
			if (!groupStateHasChanged &&
				monitor->notificationClient.connectionStatementType ==
				PGSQL_CONNECTION_MULTI_STATEMENT)
			{
				pgsql_finish(&(monitor->notificationClient));
			}
		}

		if (!monitor_node_active(&(keeper->monitor),
								 keeper->config.formation,
								 keeper->state.current_node_id,
								 keeper->state.current_group,
								 keeper->state.current_role,
								 pgIsRunning,
								 currentTLI,
								 currrentLSN,
								 pgsrSyncState,
								 assignedState))
		{
			++errors;

			log_warn("Failed to contact the monitor at \"%s\"",
					 keeper->config.monitor_pguri);

			if (errors > 5)
			{
				log_error("Failed to contact the monitor 5 times in a row now, "
						  "so we stop trying. You can do `pg_autoctl create` "
						  "to retry and finish the local setup");
				return false;
			}
		}

		/* if state has changed, we didn't wait for a full timeout */
		if (!groupStateHasChanged)
		{
			++tries;
		}

		/* if the node has been dropped while trying to init, exit early */
		if (assignedState->state == DROPPED_STATE)
		{
			return false;
		}

		if (tries == 3)
		{
			log_info("Still waiting for the monitor to drive us to state \"%s\"",
					 NodeStateToString(CATCHINGUP_STATE));
			log_warn("Please make sure that the primary node is currently "
					 "running `pg_autoctl run` and contacting the monitor.");
		}

		log_trace("wait_until_primary_is_ready: %s",
				  NodeStateToString(assignedState->state));
	} while (assignedState->state != CATCHINGUP_STATE);

	/*
	 * Update our state with the result from the monitor now.
	 */
	if (!keeper_update_state(keeper,
							 assignedState->nodeId,
							 assignedState->groupId,
							 assignedState->state,
							 true))
	{
		log_error("Failed to update keepers's state");
		return false;
	}

	/* Now make sure the replication slot has been created on the primary */
	return wait_until_primary_has_created_our_replication_slot(keeper,
															   assignedState);
}


/*
 * wait_until_primary_has_created_our_replication_slot loops over querying the
 * primary server until it has created our replication slot.
 *
 * When assigned CATCHINGUP_STATE, in some cases the primary might not be ready
 * yet. That might happen when all the other standby nodes are in maintenance
 * and the primary is already in the WAIT_PRIMARY state.
 */
static bool
wait_until_primary_has_created_our_replication_slot(Keeper *keeper,
													MonitorAssignedState *assignedState)
{
	int errors = 0, tries = 0;
	bool firstLoop = true;

	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource *upstream = &(postgres->replicationSource);
	NodeAddress primaryNode = { 0 };

	bool hasReplicationSlot = false;

	if (!keeper_get_primary(keeper, &primaryNode))
	{
		/* errors have already been logged */
		return false;
	}

	if (!standby_init_replication_source(postgres,
										 &primaryNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 assignedState->nodeId))
	{
		/* can't happen at the moment */
		return false;
	}

	do {
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			return false;
		}

		if (firstLoop)
		{
			firstLoop = false;
		}
		else
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}

		if (!upstream_has_replication_slot(upstream,
										   &(config->pgSetup),
										   &hasReplicationSlot))
		{
			++errors;

			log_warn("Failed to contact the primary node " NODE_FORMAT,
					 primaryNode.nodeId,
					 primaryNode.name,
					 primaryNode.host,
					 primaryNode.port);

			if (errors > 5)
			{
				log_error("Failed to contact the primary 5 times in a row now, "
						  "so we stop trying. You can do `pg_autoctl create` "
						  "to retry and finish the local setup");
				return false;
			}
		}

		++tries;

		if (!hasReplicationSlot && tries == 3)
		{
			log_info("Still waiting for the to create our replication slot");
			log_warn("Please make sure that the primary node is currently "
					 "running `pg_autoctl run` and contacting the monitor.");
		}
	} while (!hasReplicationSlot);

	return true;
}


/*
 * create_database_and_extension does the following:
 *
 *  - ensures PostgreSQL is running
 *  - create the proper role with login
 *  - to be able to fetch pg_hba.conf location and edit it for pg_autoctl
 *  - then createdb pgSetup.dbname, which might not be postgres
 *  - and restart PostgreSQL with the new setup, to make it active/current
 *  - finally when pgKind is Citus, create the citus extension
 *
 * When pgKind is Citus, the setup we install in step 2 contains the
 * shared_preload_libraries = 'citus' entry, so we can proceed with create
 * extension citus after the restart.
 */
bool
create_database_and_extension(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *pgsql = &(postgres->sqlClient);

	LocalPostgresServer initPostgres = { 0 };
	PostgresSetup initPgSetup = { 0 };
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;
	char hbaFilePath[MAXPGPATH];

	log_trace("create_database_and_extension");

	/* we didn't start PostgreSQL yet, also we just ran initdb */
	sformat(hbaFilePath, MAXPGPATH, "%s/pg_hba.conf", pgSetup->pgdata);

	/*
	 * The Postgres URI given to the user by our facility is going to use
	 * --dbname and --hostname, as per the following command:
	 *
	 *   $ pg_autoctl show uri --formation default
	 *
	 * We need to make it so that the user can actually use that connection
	 * string with at least the --username used to create the database.
	 */
	if (!pghba_ensure_host_rule_exists(hbaFilePath,
									   pgSetup->ssl.active,
									   HBA_DATABASE_DBNAME,
									   pgSetup->dbname,
									   pg_setup_get_username(pgSetup),
									   config->hostname,
									   pg_setup_get_auth_method(pgSetup),
									   pgSetup->hbaLevel))
	{
		log_error("Failed to edit \"%s\" to grant connections to \"%s\", "
				  "see above for details", hbaFilePath, config->hostname);
		return false;
	}

	/*
	 * When --pg-hba-lan is used, we also open the local network CIDR
	 * connections for the given --username and --dbname.
	 */
	if (pgSetup->hbaLevel == HBA_EDIT_LAN)
	{
		if (!pghba_enable_lan_cidr(&keeper->postgres.sqlClient,
								   keeper->config.pgSetup.ssl.active,
								   HBA_DATABASE_DBNAME,
								   keeper->config.pgSetup.dbname,
								   keeper->config.hostname,
								   pg_setup_get_username(pgSetup),
								   pg_setup_get_auth_method(pgSetup),
								   pgSetup->hbaLevel,
								   pgSetup->pgdata))
		{
			log_error("Failed to grant local network connections in HBA");
			return false;
		}
	}

	/*
	 * In test environments using PG_REGRESS_SOCK_DIR="" to disable unix socket
	 * directory, we have to connect to the address from pghost.
	 */
	if (env_found_empty("PG_REGRESS_SOCK_DIR"))
	{
		log_info("Granting connection from \"%s\" in \"%s\"",
				 pgSetup->pghost, hbaFilePath);

		/* Intended use is restricted to unit testing, hard-code "trust" here */
		if (!pghba_ensure_host_rule_exists(hbaFilePath,
										   pgSetup->ssl.active,
										   HBA_DATABASE_ALL,
										   NULL, /* all: no database name */
										   NULL, /* no username, "all" */
										   pgSetup->pghost,
										   "trust",
										   HBA_EDIT_MINIMAL))
		{
			log_error("Failed to edit \"%s\" to grant connections to \"%s\", "
					  "see above for details", hbaFilePath, pgSetup->pghost);
			return false;
		}
	}

	/*
	 * Use the "template1" database in the next operations when connecting to
	 * do the initial PostgreSQL configuration, and to create our database. We
	 * certainly can't connect to our database until we've created it.
	 */
	if (!pg_setup_init(&initPgSetup, pgSetup,
					   missingPgdataIsOk, pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialize newly created PostgreSQL instance,"
				  "see above for details");
		return false;
	}
	strlcpy(initPgSetup.username, "", NAMEDATALEN);
	strlcpy(initPgSetup.dbname, "template1", NAMEDATALEN);
	local_postgres_init(&initPostgres, &initPgSetup);

	/*
	 * When --ssl-self-signed has been used, now is the time to build a
	 * self-signed certificate for the server. We place the certificate and
	 * private key in $PGDATA/server.key and $PGDATA/server.crt
	 */
	if (!keeper_create_self_signed_cert(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/* publish our new pgSetup to the caller postgres state too */
	postgres->postgresSetup.ssl = initPostgres.postgresSetup.ssl;

	/*
	 * Ensure pg_stat_statements is available in the server extension dir used
	 * to create the Postgres instance. We only search for the control file to
	 * offer better diagnostics in the logs in case the following CREATE
	 * EXTENSION fails.
	 */
	if (!find_extension_control_file(config->pgSetup.pg_ctl,
									 "pg_stat_statements"))
	{
		log_warn("Failed to find extension control file for "
				 "\"pg_stat_statements\"");
	}

	/*
	 * Ensure citus extension is available in the server extension dir used to
	 * create the Postgres instance. We only search for the control file to
	 * offer better diagnostics in the logs in case the following CREATE
	 * EXTENSION fails.
	 */
	if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
	{
		if (!find_extension_control_file(config->pgSetup.pg_ctl, "citus"))
		{
			log_warn("Failed to find extension control file for \"citus\"");
		}
	}

	/*
	 * Add pg_autoctl PostgreSQL settings, including Citus extension in
	 * shared_preload_libraries when dealing with a Citus worker or coordinator
	 * node.
	 */
	if (!postgres_add_default_settings(&initPostgres, config->hostname))
	{
		log_error("Failed to add default settings to newly initialized "
				  "PostgreSQL instance, see above for details");
		return false;
	}

	/*
	 * Now start the database, we need to create our dbname and maybe the Citus
	 * Extension too.
	 */
	if (!ensure_postgres_service_is_running(&initPostgres))
	{
		log_error("Failed to start PostgreSQL, see above for details");
		return false;
	}

	/*
	 * If username was set in the setup and doesn't exist we need to create it.
	 */
	if (!IS_EMPTY_STRING_BUFFER(pgSetup->username))
	{
		/*
		 * Remove PGUSER from the environment when we want to create that very
		 * user at bootstrap.
		 */
		char pguser[NAMEDATALEN] = { 0 };

		if (!get_env_copy_with_fallback("PGUSER", pguser, NAMEDATALEN, ""))
		{
			/* errors have already been logged */
			return false;
		}

		if (strcmp(pguser, pgSetup->username) == 0)
		{
			unsetenv("PGUSER");
		}

		if (!pgsql_create_user(&initPostgres.sqlClient,
							   pgSetup->username,
							   NULL, /* password */
							   true, /* WITH login */
							   true, /* WITH superuser */
							   false, /* WITH replication */
							   -1))   /* connlimit */
		{
			log_fatal("Failed to create role \"%s\""
					  ", see above for details", pgSetup->username);

			return false;
		}

		/* reinstall the PGUSER value now that the user has been created. */
		if (strcmp(pguser, pgSetup->username) == 0)
		{
			setenv("PGUSER", pguser, 1);
		}
	}

	/*
	 * Now, maybe create the database (if "postgres", it already exists).
	 *
	 * We need to connect to an existing database here, such as "template1",
	 * and create our target database from there.
	 */
	if (!IS_EMPTY_STRING_BUFFER(pgSetup->dbname))
	{
		/* maybe create the database, skipping if it already exists */
		log_info("CREATE DATABASE %s;", pgSetup->dbname);
		if (!pgsql_create_database(&initPostgres.sqlClient,
								   pgSetup->dbname,
								   pg_setup_get_username(pgSetup)))
		{
			log_error("Failed to create database %s with owner %s",
					  pgSetup->dbname, pgSetup->username);
			return false;
		}
	}

	/* close the "template1" connection now */
	pgsql_finish(&initPostgres.sqlClient);

	/*
	 * Connect to Postgres as the system user to create extension: same user as
	 * initdb with superuser privileges.
	 *
	 * Calling keeper_update_state will re-init our sqlClient to now connect
	 * per the configuration settings, cleaning-up the local changes we made
	 * before.
	 */
	if (!keeper_update_pg_state(keeper, LOG_ERROR))
	{
		log_error("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above for details.");
		return false;
	}

	/*
	 * Install the pg_stat_statements extension in that database, skipping if
	 * the extension has already been installed.
	 */
	log_info("CREATE EXTENSION pg_stat_statements;");

	if (!pgsql_create_extension(&(postgres->sqlClient), "pg_stat_statements"))
	{
		log_error("Failed to create extension pg_stat_statements");
		return false;
	}

	/*
	 * When initialiasing a PostgreSQL instance that's going to be used as a
	 * Citus node, either a coordinator or a worker, we have to also create an
	 * extension in a database that can be used by citus.
	 */
	if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
	{
		/*
		 * Now allow nodes on the same network to connect to the coordinator,
		 * and the coordinator to connect to its workers.
		 */
		if (!pghba_enable_lan_cidr(&initPostgres.sqlClient,
								   pgSetup->ssl.active,
								   HBA_DATABASE_DBNAME,
								   pgSetup->dbname,
								   config->hostname,
								   pg_setup_get_username(pgSetup),
								   pg_setup_get_auth_method(pgSetup),
								   pgSetup->hbaLevel,
								   NULL))
		{
			log_error("Failed to grant local network connections in HBA");
			return false;
		}

		/*
		 * Install the citus extension in that database, skipping if the
		 * extension has already been installed.
		 */
		log_info("CREATE EXTENSION %s;", CITUS_EXTENSION_NAME);

		if (!pgsql_create_extension(&(postgres->sqlClient), CITUS_EXTENSION_NAME))
		{
			log_error("Failed to create extension %s", CITUS_EXTENSION_NAME);
			return false;
		}
	}

	/* and we're done with this connection. */
	pgsql_finish(pgsql);

	return true;
}


/*
 * keeper_pg_init_node_active calls node_active() on the monitor, to publish
 * the state reached by the end of the initialization procedure of the node.
 */
static bool
keeper_pg_init_node_active(Keeper *keeper)
{
	MonitorAssignedState assignedState = { 0 };

	/*
	 * Save our local state before reporting it to the monitor. If we fail to
	 * contact the monitor, we can always retry later.
	 */
	if (!keeper_store_state(keeper))
	{
		/*
		 * Errors have already been logged.
		 *
		 * Make sure we don't have a corrupted state file around, that could
		 * prevent trying to init again and cause strange errors.
		 */
		unlink_file(keeper->config.pathnames.state);

		return false;
	}

	(void) keeper_update_pg_state(keeper, LOG_WARN);

	if (!monitor_node_active(&(keeper->monitor),
							 keeper->config.formation,
							 keeper->state.current_node_id,
							 keeper->state.current_group,
							 keeper->state.current_role,
							 ReportPgIsRunning(keeper),
							 keeper->postgres.postgresSetup.control.timeline_id,
							 keeper->postgres.currentLSN,
							 keeper->postgres.pgsrSyncState,
							 &assignedState))
	{
		log_error("Failed to contact the monitor to publish our "
				  "current state \"%s\".",
				  NodeStateToString(keeper->state.current_role));
		return false;
	}

	/*
	 * Now save the monitor's assigned state before being done with the init
	 * step. If a transition is needed to reach that state, that's the job of
	 * `pg_autoctl run` to make it happen now. That said, we should make
	 * sure to record the monitor's answer in our local state before we give
	 * control back to the user.
	 */
	if (!keeper_update_state(keeper,
							 assignedState.nodeId,
							 assignedState.groupId,
							 assignedState.state,
							 true))
	{
		log_error("Failed to update keepers's state");

		/*
		 * Make sure we don't have a corrupted state file around, that could
		 * prevent trying to init again and cause strange errors.
		 */
		unlink_file(keeper->config.pathnames.state);

		return false;
	}

	return true;
}
