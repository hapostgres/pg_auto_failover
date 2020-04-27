/*
 * src/bin/pg_autoctl/fsm_transition.c
 *   Implementation of transitions in the keeper state machine
 *
 * To move from a current state to a goal state, the pg_autoctl state machine
 * will call the functions defined in this file, which are referenced from
 * fsm.c
 *
 * Every transition must be idempotent such that it can safely be repeated
 * until it succeeds.
 *
 * As the keeper could fail or be interrupted in-flight, it's important that
 * every transition can be tried again (is idempotent). When interrupted (by
 * a bug or a signal, user interrupt or system reboot), the current and
 * assigned roles have not changed and on the next keeper's start the FSM
 * will kick in a call the transition that failed again. The transition might
 * have successfully implemented the first parts of its duties... and we must
 * not fail because of that. Idempotency is achieved by only calling
 * idempotent subroutines or checking whether the goal of the subroutine
 * (e.g. "postgres is promoted") has been achieved already.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "env_utils.h"
#include "pgctl.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pghba.h"
#include "primary_standby.h"
#include "state.h"

static bool prepare_replication(Keeper *keeper, NodeState otherNodeState);


/*
 * fsm_init_primary initialises the postgres server as primary.
 *
 * This function actually covers the transition from INIT to SINGLE.
 *
 *    pg_ctl initdb (if necessary)
 * && create database + create extension (if necessary)
 * && start_postgres
 * && promote_standby (if applicable)
 * && add_default_settings
 * && create_monitor_user
 * && create_replication_user
 */
bool
fsm_init_primary(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *pgsql = &(postgres->sqlClient);
	bool inRecovery = false;

	KeeperStateInit *initState = &(keeper->initState);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	bool postgresInstanceExists = pg_setup_pgdata_exists(pgSetup);
	bool pgInstanceIsOurs = false;

	log_info("Initialising postgres as a primary");

	/*
	 * When initialializing the local node on-top of an empty (or non-existing)
	 * PGDATA directory, now is the time to `pg_ctl initdb`.
	 */
	if (!keeper_init_state_read(initState, config->pathnames.init))
	{
		log_error("Failed to read init state file \"%s\", which is required "
				  "for the transition from INIT to SINGLE.",
				  config->pathnames.init);
		return false;
	}

	/*
	 * When initState is PRE_INIT_STATE_RUNNING, double check that Postgres is
	 * still running. After all the end-user could just stop Postgres and then
	 * give the install to us. We ought to support that.
	 */
	if (initState->pgInitState >= PRE_INIT_STATE_RUNNING)
	{
		if (!keeper_init_state_discover(initState,
										pgSetup,
										keeper->config.pathnames.init))
		{
			/* errors have already been logged */
			return false;
		}

		/* did the user try again after having stopped Postgres maybe? */
		if (initState->pgInitState < PRE_INIT_STATE_RUNNING)
		{
			log_info("PostgreSQL state has changed since registration time: %s",
					 PreInitPostgreInstanceStateToString(initState->pgInitState));
		}
	}

	pgInstanceIsOurs =
		initState->pgInitState == PRE_INIT_STATE_EMPTY ||
		initState->pgInitState == PRE_INIT_STATE_EXISTS;

	if (initState->pgInitState == PRE_INIT_STATE_EMPTY &&
		!postgresInstanceExists)
	{
		if (!pg_ctl_initdb(pgSetup->pg_ctl, pgSetup->pgdata))
		{
			log_fatal("Failed to initialise a PostgreSQL instance at \"%s\""
					  ", see above for details", pgSetup->pgdata);

			return false;
		}

		/*
		 * We managed to initdb, refresh our configuration file location with
		 * the realpath(3) from pg_setup_update_config_with_absolute_pgdata:
		 *  we might have been given a relative pathname.
		 */
		if (!keeper_config_update_with_absolute_pgdata(&(keeper->config)))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else if (initState->pgInitState >= PRE_INIT_STATE_RUNNING)
	{
		log_error("PostgreSQL is already running at \"%s\", refusing to "
				  "initialize a new cluster on-top of the current one.",
				  pgSetup->pgdata);

		return false;
	}

	/*
	 * When the PostgreSQL instance either did not exist, or did exist but was
	 * not running when creating the pg_autoctl node the first time, then we
	 * can restart the instance without fear of disturbing the service.
	 */
	if (pgInstanceIsOurs)
	{
		/* create the target database and install our extension there */
		if (!create_database_and_extension(keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/*
	 * Now is the time to make sure Postgres is running, as our next steps to
	 * prepare a SINGLE from INIT are depending on being able to connect to the
	 * local Postgres service.
	 */
	if (!ensure_local_postgres_is_running(postgres))
	{
		log_error("Failed to initialise postgres as primary because "
				  "starting postgres failed, see above for details");
		return false;
	}

	/*
	 * FIXME: In the current FSM, I am not sure this can happen anymore. That
	 * said we might want to remain compatible with initializing a SINGLE from
	 * an pre-existing standby. I wonder why/how it would come to that though.
	 */
	if (pgsql_is_in_recovery(pgsql, &inRecovery) && inRecovery)
	{
		log_info("Initialising a postgres server in recovery mode as the primary, "
				 "promoting");

		if (!standby_promote(postgres))
		{
			log_error("Failed to initialise postgres as primary because promoting "
					  "postgres failed, see above for details");
			return false;
		}
	}

	/*
	 * We just created the local Postgres cluster, make sure it has our minimum
	 * configuration deployed.
	 *
	 * When --ssl-self-signed has been used, now is the time to build a
	 * self-signed certificate for the server. We place the certificate and
	 * private key in $PGDATA/server.key and $PGDATA/server.crt
	 */
	if (!keeper_create_self_signed_cert(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	if (!postgres_add_default_settings(postgres))
	{
		log_error("Failed to initialise postgres as primary because "
				  "adding default settings failed, see above for details");
		return false;
	}

	/*
	 * Now add the role and HBA entries necessary for the monitor to run health
	 * checks on the local Postgres node.
	 */
	if (!config->monitorDisabled)
	{
		char monitorHostname[_POSIX_HOST_NAME_MAX];
		int monitorPort = 0;

		if (!hostname_from_uri(config->monitor_pguri,
							   monitorHostname, _POSIX_HOST_NAME_MAX,
							   &monitorPort))
		{
			/* developer error, this should never happen */
			log_fatal("BUG: monitor_pguri should be validated before calling "
					  "fsm_init_primary");
			return false;
		}
	}

	/*
	 * This node is intended to be used as a primary later in the setup, when
	 * we have a standby node to register, so prepare the replication user now.
	 */
	if (!primary_create_replication_user(postgres, PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password))
	{
		log_error("Failed to initialise postgres as primary because creating the "
				  "replication user for the standby failed, see above for details");
		return false;
	}

	/*
	 * What remains to be done is either opening the HBA for a test setup, or
	 * when we are initializing pg_auto_failover on an existing PostgreSQL
	 * primary server instance, making sure that the parameters are all set.
	 */
	if (pgInstanceIsOurs)
	{
		if (env_found_empty("PG_REGRESS_SOCK_DIR"))
		{
			/*
			 * In test environements allow nodes from the same network to
			 * connect. The network is discovered automatically.
			 */
			if (!pghba_enable_lan_cidr(&keeper->postgres.sqlClient,
									   keeper->config.pgSetup.ssl.active,
									   HBA_DATABASE_ALL, NULL,
									   keeper->config.hostname,
									   NULL, DEFAULT_AUTH_METHOD, NULL))
			{
				log_error("Failed to grant local network connections in HBA");
				return false;
			}
		}
	}
	else
	{
		/*
		 * As we are registering a previsouly existing PostgreSQL
		 * instance, we now check that our mininum configuration
		 * requirements for pg_auto_failover are in place. If not, tell
		 * the user they must restart PostgreSQL at their next
		 * maintenance window to fully enable pg_auto_failover.
		 */
		bool settings_are_ok = false;

		if (!check_postgresql_settings(&(keeper->postgres),
									   &settings_are_ok))
		{
			log_fatal("Failed to check local PostgreSQL settings "
					  "compliance with pg_auto_failover, "
					  "see above for details");
			return false;
		}
		else if (!settings_are_ok)
		{
			log_fatal("Current PostgreSQL settings are not compliant "
					  "with pg_auto_failover requirements, "
					  "please restart PostgreSQL at the next "
					  "opportunity to enable pg_auto_failover changes, "
					  "and redo `pg_autoctl create`");
			return false;
		}
	}

	/* and we're done with this connection. */
	pgsql_finish(pgsql);

	return true;
}


/*
 * fsm_disable_replication is used when other node was forcibly removed, now
 * single.
 *
 *    disable_synchronous_replication
 * && keeper_drop_replication_slots_for_removed_nodes
 *
 * TODO: We currently use a separate session for each step. We should use
 * a single connection.
 */
bool
fsm_disable_replication(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!primary_disable_synchronous_replication(postgres))
	{
		log_error("Failed to disable replication because disabling synchronous "
				  "failed, see above for details");
		return false;
	}

	/* when a standby has been removed, remove its replication slot */
	return keeper_drop_replication_slots_for_removed_nodes(keeper);
}


/*
 * fsm_resume_as_primary is used when the local node was demoted after a
 * failure, but standby was forcibly removed.
 *
 *    start_postgres
 * && disable_synchronous_replication
 * && keeper_drop_replication_slots_for_removed_nodes
 *
 * So we reuse fsm_disable_replication() here, rather than copy/pasting the same
 * bits code in the fsm_resume_as_primary() function body. If the definition of
 * the fsm_resume_as_primary transition ever came to diverge from whatever
 * fsm_disable_replication() is doing, we'd have to copy/paste and maintain
 * separate code path.
 */
bool
fsm_resume_as_primary(Keeper *keeper)
{
	if (!keeper_start_postgres(keeper))
	{
		return false;
	}

	if (!fsm_disable_replication(keeper))
	{
		log_error("Failed to disable synchronous replication in order to "
				  "resume as a primary, see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_prepare_replication is used when a new standby was added.
 *
 * add_standby_to_hba && create_replication_slot
 */
bool
fsm_prepare_replication(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	if (IS_EMPTY_STRING_BUFFER(config->hostname))
	{
		/* developer error, this should never happen */
		log_fatal(
			"BUG: hostname should be set before calling fsm_prepare_replication");
		return false;
	}

	return prepare_replication(keeper, WAIT_STANDBY_STATE);
}


/*
 * prepare_replication is the work-horse for fsm_prepare_replication and used
 * in fsm_promote_standby too, where we could have to accept the fact that
 * there's no other node at the moment: we're doing a secondary ➜ single
 * transition after all.
 */
static bool
prepare_replication(Keeper *keeper, NodeState otherNodeState)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	int nodeIndex = 0;

	/*
	 * When using the monitor, now is the time to fetch the otherNode hostname
	 * and port from it. When the monitor is disabled, it is expected that the
	 * information has been already filled in keeper->otherNode; see
	 * `keeper_cli_fsm_assign' for an example of that.
	 */
	if (!config->monitorDisabled)
	{
		char *host = config->hostname;
		int port = pgSetup->pgport;

		if (!monitor_get_other_nodes(monitor, host, port,
									 otherNodeState,
									 &(keeper->otherNodes)))
		{
			/* errors have already been logged */
			return false;
		}

		if (keeper->otherNodes.count == 0)
		{
			if (otherNodeState == ANY_STATE)
			{
				log_warn("There's no other node for %s:%d", host, port);
			}
			else
			{
				/*
				 * Should we warn about it really? it might be a replication
				 * setting change that will impact synchronous_standby_names
				 * and that's all.
				 */
				log_warn("There's no other node in state \"%s\" "
						 "for node %s:%d",
						 NodeStateToString(otherNodeState),
						 host, port);
			}
		}
	}

	/*
	 * Should we fail somewhere in this loop, we return false and fail the
	 * whole transition. The transition is going to be tried again, and we are
	 * going to try and add HBA entries and create replication slots again.
	 * Both operations succeed when their target entry already exists.
	 *
	 */
	for (nodeIndex = 0; nodeIndex < keeper->otherNodes.count; nodeIndex++)
	{
		NodeAddress *otherNode = &(keeper->otherNodes.nodes[nodeIndex]);
		char replicationSlotName[BUFSIZE] = { 0 };

		log_info("Preparing replication for standby node %d (%s:%d)",
				 otherNode->nodeId, otherNode->host, otherNode->port);

		if (!primary_add_standby_to_hba(postgres,
										otherNode->host,
										config->replication_password))
		{
			log_error("Failed to grant access to the standby %d (%s:%d) "
					  "by adding relevant lines to pg_hba.conf for the standby "
					  "hostname and user, see above for details",
					  otherNode->nodeId, otherNode->host, otherNode->port);
			return false;
		}

		if (!postgres_sprintf_replicationSlotName(otherNode->nodeId,
												  replicationSlotName, BUFSIZE))
		{
			/* that's highly unlikely... */
			log_error("Failed to snprintf replication slot name for node %d",
					  otherNode->nodeId);
			return false;
		}

		if (!primary_create_replication_slot(postgres, replicationSlotName))
		{
			log_error(
				"Failed to enable replication from the primary server because "
				"creating the replication slot failed, see above for details");
			return false;
		}
	}

	return true;
}


/*
 * fsm_stop_replication is used to forcefully stop replication, in case the
 * primary is on the other side of a network split.
 */
bool
fsm_stop_replication(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *client = &(postgres->sqlClient);

	/*
	 * We can't control if the client is still sending writes to our PostgreSQL
	 * instance or not. To avoid split-brains situation, we need to make some
	 * efforts:
	 *
	 * - set default_transaction_read_only to 'on' on this server (a
	 *   standby being promoted) so that it can't be the target of
	 *   connection strings requiring target_session_attrs=read-write yet
	 *
	 * - shut down the replication stream (here by promoting the replica)
	 *
	 * - have the primary server realize it's alone on the network: can't
	 *   communicate with the monitor (which triggered the failover), can't
	 *   communicate with the standby (now absent from pg_stat_replication)
	 *
	 * When the keeper on the primary realizes they are alone in the dark,
	 * it will go to DEMOTE state on its own and shut down PostgreSQL,
	 * protecting againts split brain.
	 */

	log_info("Prevent writes to the promoted standby while the primary "
			 "is not demoted yet, by making the service incompatible with "
			 "target_session_attrs = read-write");

	if (!pgsql_set_default_transaction_mode_read_only(client))
	{
		log_error("Failed to switch to read-only mode");
		return false;
	}

	return fsm_promote_standby(keeper);
}


/*
 * fsm_disable_sync_rep is used when standby became unhealthy.
 */
bool
fsm_disable_sync_rep(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	return primary_disable_synchronous_replication(postgres);
}


/*
 * fsm_promote_standby_to_primary is used when the standby should become the
 * new primary. It also prepares for the old primary to become the new standby.
 *
 * The promotion of the standby has already happened in the previous
 * transition:
 *
 *  1.         secondary ➜ prepare_promotion : block writes
 *  2. prepare_promotion ➜ stop_replication  : promote
 *  3.  stop_replication ➜ wait_primary      : resume writes
 *
 * Resuming writes is done by setting default_transaction_read_only to off,
 * thus allowing libpq to establish connections when target_session_attrs is
 * read-write.
 */
bool
fsm_promote_standby_to_primary(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *client = &(postgres->sqlClient);

	if (!pgsql_set_default_transaction_mode_read_write(client))
	{
		log_error("Failed to set default_transaction_read_only to off "
				  "which is needed to accept libpq connections with "
				  "target_session_attrs read-write");
		return false;
	}
	return true;
}


/*
 * fsm_enable_sync_rep is used when a healthy standby appeared.
 */
bool
fsm_enable_sync_rep(Keeper *keeper)
{
	/*
	 * We need to fetch and apply the synchronous_standby_names setting value
	 * from the monitor... and that's about it really.
	 */
	return fsm_apply_settings(keeper);
}


/*
 * fsm_apply_settings is used when a pg_auto_failover setting has changed, such
 * as number_sync_standbys or node priorities and replication quorum
 * properties.
 *
 * So we have to fetch the current synchronous_standby_names setting value from
 * the monitor and apply it (reload) to the current node.
 */
bool
fsm_apply_settings(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);

	char synchronous_standby_names[BUFSIZE] = { 0 };

	/* get synchronous_standby_names value from the monitor */
	if (!config->monitorDisabled)
	{
		if (!monitor_synchronous_standby_names(
				monitor,
				config->formation,
				keeper->state.current_group,
				synchronous_standby_names,
				BUFSIZE))
		{
			log_error("Failed to enable synchronous replication because "
					  "we failed to get the synchronous_standby_names value "
					  "from the monitor, see above for details");
			return false;
		}
	}
	else
	{
		/* no monitor: use the generic value '*' */
		strlcpy(synchronous_standby_names, "*", BUFSIZE);
	}

	return primary_set_synchronous_standby_names(
		postgres,
		synchronous_standby_names);
}


/*
 * fsm_start_postgres is used when we detected a network partition, but monitor
 * didn't do failover.
 */
bool
fsm_start_postgres(Keeper *keeper)
{
	return keeper_start_postgres(keeper);
}


/*
 * fsm_stop_postgres is used when local node was demoted, need to be dead now.
 */
bool
fsm_stop_postgres(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	return pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata);
}


/*
 * fsm_init_standby is used when the primary is now ready to accept a standby,
 * we're the standby.
 */
bool
fsm_init_standby(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);

	int groupId = keeper->state.current_group;
	NodeAddress *primaryNode = NULL;

	/* get the primary node to follow */
	if (!config->monitorDisabled)
	{
		if (!monitor_get_primary(monitor, config->formation, groupId,
								 &(postgres->replicationSource.primaryNode)))
		{
			log_error("Failed to initialise standby because get the primary node "
					  "from the monitor failed, see above for details");
			return false;
		}
	}
	else
	{
		/* copy information from keeper->otherNodes into replicationSource */
		primaryNode = &(keeper->otherNodes.nodes[0]);
	}

	if (!standby_init_replication_source(postgres,
										 primaryNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	if (!standby_init_database(postgres, config->hostname))
	{
		log_error("Failed initialise standby server, see above for details");
		return false;
	}

	/* ensure the SSL setup is synced with the keeper config */
	if (!keeper_create_self_signed_cert(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/* now, in case we have an init state file around, remove it */
	return unlink_file(config->pathnames.init);
}


/*
 * fsm_rewind_or_init is used when a new primary is available. First, try to
 * rewind. If that fails, do a pg_basebackup.
 */
bool
fsm_rewind_or_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);

	int groupId = keeper->state.current_group;
	NodeAddress *primaryNode = NULL;

	/* get the primary node to follow */
	if (!config->monitorDisabled)
	{
		if (!monitor_get_primary(monitor, config->formation, groupId,
								 &(postgres->replicationSource.primaryNode)))
		{
			log_error("Failed to initialise standby because get the primary node "
					  "from the monitor failed, see above for details");
			return false;
		}
	}
	else
	{
		/* copy information from keeper->otherNodes into replicationSource */
		primaryNode = &(keeper->otherNodes.nodes[0]);
	}

	if (!standby_init_replication_source(postgres,
										 primaryNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	if (!primary_rewind_to_standby(postgres))
	{
		log_warn("Failed to rewind demoted primary to standby, "
				 "trying pg_basebackup instead");

		if (!standby_init_database(postgres, config->hostname))
		{
			log_error("Failed to become standby server, see above for details");
			return false;
		}

		/* ensure the SSL setup is synced with the keeper config */
		if (!keeper_create_self_signed_cert(keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}

	return true;
}


/*
 * fsm_maintain_replication_slots is used when going from CATCHINGUP to
 * SECONDARY, to create missing replication slots. We want to maintain a
 * replication slot for each of the other nodes in the system, so that we make
 * sure we have the WAL bytes around when a standby nodes has to follow a new
 * primary, after failover.
 */
bool
fsm_maintain_replication_slots(Keeper *keeper)
{
	return keeper_maintain_replication_slots(keeper);
}


/*
 * fsm_prepare_standby_for_promotion used when the standby is asked to prepare
 * its own promotion.
 *
 * TODO: implement the prepare_promotion_walreceiver_timeout as follows:
 *
 *   We need to loop over the `ready_to_promote' until the standby is ready.
 *   This routine compare the time spent waiting to the setup:
 *
 *   prepare_promotion_walreceiver_timeout
 *
 *   The `ready_to_promote' routine eventually returns true.
 *
 *   Currently the keeper only supports Synchronous Replication so this timeout
 *   isn't necessary, that's why it's not implemented yet. The implementation
 *   needs to happen for async rep support.
 */
bool
fsm_prepare_standby_for_promotion(Keeper *keeper)
{
	log_debug("No support for async replication means we don't wait until "
			  "prepare_promotion_walreceiver_timeout (%ds)",
			  keeper->config.prepare_promotion_walreceiver);

	return true;
}


/*
 * fsm_suspend_standby is used when putting the standby in maintenance mode
 * (kernel upgrades, change of hardware, etc). Maintenance means that the user
 * now is driving the service, refrain from doing anything ourselves.
 */
bool
fsm_start_maintenance_on_standby(Keeper *keeper)
{
	return true;
}


/*
 * fsm_restart_standby is used when restarting standby after manual maintenance
 * is done.
 */
bool
fsm_restart_standby(Keeper *keeper)
{
	return fsm_start_postgres(keeper);
}


/*
 * fsm_promote_standby is used in several situations in the FSM transitions and
 * the following actions are needed to promote a standby:
 *
 *    start_postgres
 * && promote_standby
 * && add_standby_to_hba
 * && create_replication_slot
 * && disable_synchronous_replication
 * && keeper_drop_replication_slots_for_removed_nodes
 *
 * So we reuse fsm_disable_replication() here, rather than copy/pasting the same
 * bits code in the fsm_promote_standby() function body. If the definition of
 * the fsm_promote_standby transition ever came to diverge from whatever
 * fsm_disable_replication() is doing, we'd have to copy/paste and maintain
 * separate code path.
 */
bool
fsm_promote_standby(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!ensure_local_postgres_is_running(postgres))
	{
		log_error("Failed to promote postgres because the server could not "
				  "be started before promotion, see above for details");
		return false;
	}

	/*
	 * If postgres is no longer in recovery mode, standby_promote returns true
	 * immediately and therefore this function is idempotent.
	 */
	if (!standby_promote(postgres))
	{
		log_error("Failed to promote the local postgres server from standby "
				  "to single state, see above for details");
		return false;
	}

	/*
	 * The old primary will (hopefully) come back as a secondary and should
	 * be able to open a replication connection. We therefore need to do the
	 * same steps we take when going from single to wait_primary, namely to
	 * create a replication slot and add the other node to pg_hba.conf. These
	 * steps are implemented in fsm_prepare_replication.
	 */
	if (!prepare_replication(keeper, DEMOTE_TIMEOUT_STATE))
	{
		/* prepare_replication logs relevant errors */
		return false;
	}

	if (!fsm_disable_replication(keeper))
	{
		log_error("Failed to disable synchronous replication after promotion, "
				  "see above for details");
		return false;
	}

	return true;
}
