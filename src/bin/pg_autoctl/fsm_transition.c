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

#include <inttypes.h>
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


static bool fsm_init_standby_from_upstream(Keeper *keeper);


/*
 * fsm_init_primary initializes the postgres server as primary.
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

	bool pgInstanceIsOurs =
		initState->pgInitState == PRE_INIT_STATE_EMPTY ||
		initState->pgInitState == PRE_INIT_STATE_EXISTS;

	if (initState->pgInitState == PRE_INIT_STATE_EMPTY &&
		!postgresInstanceExists)
	{
		Monitor *monitor = &(keeper->monitor);
		PostgresSetup newPgSetup = { 0 };
		bool missingPgdataIsOk = false;
		bool postgresNotRunningIsOk = true;

		if (!pg_ctl_initdb(pgSetup->pg_ctl, pgSetup->pgdata))
		{
			log_fatal("Failed to initialize a PostgreSQL instance at \"%s\""
					  ", see above for details", pgSetup->pgdata);

			return false;
		}

		if (!pg_setup_init(&newPgSetup,
						   pgSetup,
						   missingPgdataIsOk,
						   postgresNotRunningIsOk))
		{
			/* errors have already been logged */
			log_error("pg_setup_wait_until_is_ready: pg_setup_init is false");
			return false;
		}

		*pgSetup = newPgSetup;

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

		if (!config->monitorDisabled)
		{
			/*
			 * We have a new system_identifier, we need to publish it now.
			 */
			if (!monitor_set_node_system_identifier(
					monitor,
					keeper->state.current_node_id,
					pgSetup->control.system_identifier))
			{
				log_error("Failed to update the new node system_identifier");
				return false;
			}
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
	if (!ensure_postgres_service_is_running(postgres))
	{
		log_error("Failed to initialize postgres as primary because "
				  "starting postgres failed, see above for details");
		return false;
	}

	/*
	 * When dealing with a pg_autoctl create postgres command with a
	 * pre-existing PGDATA directory, make sure we can start the cluster
	 * without being in sync-rep already. The target state here is SINGLE
	 * after all.
	 */
	if (!fsm_disable_replication(keeper))
	{
		log_error("Failed to disable synchronous replication in order to "
				  "initialize as a primary, see above for details");
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
			log_error("Failed to initialize postgres as primary because promoting "
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

	if (!postgres_add_default_settings(postgres, config->hostname))
	{
		log_error("Failed to initialize postgres as primary because "
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
		int connlimit = 1;

		if (!hostname_from_uri(config->monitor_pguri,
							   monitorHostname, _POSIX_HOST_NAME_MAX,
							   &monitorPort))
		{
			/* developer error, this should never happen */
			log_fatal("BUG: monitor_pguri should be validated before calling "
					  "fsm_init_primary");
			return false;
		}

		/*
		 * We need to add the monitor host:port in the HBA settings for the
		 * node to enable the health checks.
		 *
		 * Node that we forcibly use the authentication method "trust" for the
		 * pgautofailover_monitor user, which from the monitor also uses the
		 * hard-coded password PG_AUTOCTL_HEALTH_PASSWORD. The idea is to avoid
		 * leaking information from the passfile, environment variable, or
		 * other places.
		 */
		if (!primary_create_user_with_hba(postgres,
										  PG_AUTOCTL_HEALTH_USERNAME,
										  PG_AUTOCTL_HEALTH_PASSWORD,
										  monitorHostname,
										  "trust",
										  pgSetup->hbaLevel,
										  connlimit))
		{
			log_error(
				"Failed to initialise postgres as primary because "
				"creating the database user that the pg_auto_failover monitor "
				"uses for health checks failed, see above for details");
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
		log_error("Failed to initialize postgres as primary because creating the "
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
									   NULL,
									   DEFAULT_AUTH_METHOD,
									   HBA_EDIT_MINIMAL,
									   NULL))
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
 * && keeper_create_and_drop_replication_slots
 *
 * TODO: We currently use a separate session for each step. We should use
 * a single connection.
 */
bool
fsm_disable_replication(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!ensure_postgres_service_is_running(postgres))
	{
		/* errors have already been logged */
		return false;
	}

	if (!primary_disable_synchronous_replication(postgres))
	{
		log_error("Failed to disable replication because disabling synchronous "
				  "failed, see above for details");
		return false;
	}

	/* cache invalidation in case we're doing WAIT_PRIMARY to SINGLE */
	bzero((void *) postgres->standbyTargetLSN, PG_LSN_MAXLENGTH);

	/* when a standby has been removed, remove its replication slot */
	return keeper_create_and_drop_replication_slots(keeper);
}


/*
 * fsm_resume_as_primary is used when the local node was demoted after a
 * failure, but standby was forcibly removed.
 *
 *    start_postgres
 * && disable_synchronous_replication
 * && keeper_create_and_drop_replication_slots
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
 *
 * Those operations are now done eagerly rather than just in time. So it's been
 * taken care of aready, nothing to do within this state transition.
 */
bool
fsm_prepare_replication(Keeper *keeper)
{
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
	bool forceCacheInvalidation = true;

	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *client = &(postgres->sqlClient);

	if (!pgsql_set_default_transaction_mode_read_write(client))
	{
		log_error("Failed to set default_transaction_read_only to off "
				  "which is needed to accept libpq connections with "
				  "target_session_attrs read-write");
		return false;
	}

	/* now is a good time to make sure we invalidate other nodes cache */
	if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
	{
		log_error("Failed to update HBA rules after resuming writes");
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
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	PGSQL *pgsql = &(postgres->sqlClient);

	/*
	 * First, we need to fetch and apply the synchronous_standby_names setting
	 * value from the monitor...
	 */
	if (!fsm_apply_settings(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * If we don't have any standby with replication-quorum true, then we don't
	 * actually enable sync rep here. In that case don't bother making sure the
	 * standbys have reached a meaningful LSN target before continuing.
	 */
	if (streq(postgres->synchronousStandbyNames, ""))
	{
		return true;
	}

	/* first time in that state, fetch most recent metadata */
	if (IS_EMPTY_STRING_BUFFER(postgres->standbyTargetLSN))
	{
		if (!pgsql_get_postgres_metadata(pgsql,
										 &pgSetup->is_in_recovery,
										 postgres->pgsrSyncState,
										 postgres->currentLSN,
										 &(postgres->postgresSetup.control)))
		{
			log_error("Failed to update the local Postgres metadata");
			return false;
		}

		/*
		 * Our standbyTargetLSN needs to be set once we have at least one
		 * standby that's known to participate in the synchronous replication
		 * quorum.
		 */
		if (!(streq(postgres->pgsrSyncState, "quorum") ||
			  streq(postgres->pgsrSyncState, "sync")))
		{
			/* it's an expected situation here, don't fill-up the logs */
			log_warn("Failed to set the standby Target LSN because we don't "
					 "have a quorum candidate yet");
			return false;
		}

		strlcpy(postgres->standbyTargetLSN,
				postgres->currentLSN,
				PG_LSN_MAXLENGTH);

		log_info("Waiting until standby node has caught-up to LSN %s",
				 postgres->standbyTargetLSN);
	}

	/*
	 * Now, we have set synchronous_standby_names and have one standby that's
	 * expected to be caught-up. Make sure that is the case by checking the LSN
	 * positions in much the same way as Postgres does when committing a
	 * transaction on the primary: get the current LSN, and wait until the
	 * reported LSN from the secondary has advanced past the current point.
	 */
	return primary_standby_has_caught_up(postgres);
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

	/* get synchronous_standby_names value from the monitor */
	if (!config->monitorDisabled)
	{
		if (!monitor_synchronous_standby_names(
				monitor,
				config->formation,
				keeper->state.current_group,
				postgres->synchronousStandbyNames,
				sizeof(postgres->synchronousStandbyNames)))
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
		strlcpy(postgres->synchronousStandbyNames, "*",
				sizeof(postgres->synchronousStandbyNames));
	}

	return primary_set_synchronous_standby_names(postgres);
}


/*
 * fsm_start_postgres is used when we detected a network partition, but monitor
 * didn't do failover.
 */
bool
fsm_start_postgres(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!ensure_postgres_service_is_running(postgres))
	{
		log_error("Failed to promote postgres because the server could not "
				  "be started before promotion, see above for details");
		return false;
	}

	/* fetch synchronous_standby_names setting from the monitor */
	if (!fsm_apply_settings(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * fsm_stop_postgres is used when local node was demoted, need to be dead now.
 */
bool
fsm_stop_postgres(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	return ensure_postgres_service_is_stopped(postgres);
}


/*
 * fsm_stop_postgres_for_primary_maintenance is used when pg_autoctl enable
 * maintenance has been used on the primary server, we do a couple CHECKPOINT
 * before stopping Postgres to ensure a smooth transition.
 */
bool
fsm_stop_postgres_for_primary_maintenance(Keeper *keeper)
{
	return fsm_checkpoint_and_stop_postgres(keeper);
}


/*
 * fsm_stop_postgres_and_setup_standby is used when the primary is put to
 * maintenance. Not only do we stop Postgres, we also prepare a setup as a
 * secondary.
 */
bool
fsm_stop_postgres_and_setup_standby(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource *upstream = &(postgres->replicationSource);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	KeeperConfig *config = &(keeper->config);

	NodeAddress upstreamNode = { 0 };

	if (!ensure_postgres_service_is_stopped(postgres))
	{
		/* errors have already been logged */
		return false;
	}

	/* Move the Postgres controller out of the way */
	if (!local_postgres_unlink_status_file(postgres))
	{
		/* highly unexpected */
		log_error("Failed to remove our Postgres status file "
				  "see above for details");
		return false;
	}

	/* prepare a standby setup */
	if (!standby_init_replication_source(postgres,
										 &upstreamNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	/* make the Postgres setup for a standby node before reaching maintenance */
	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   pgSetup->pgdata,
							   pgSetup->pg_ctl,
							   upstream))
	{
		log_error("Failed to setup Postgres as a standby to go to maintenance");
		return false;
	}

	return true;
}


/*
 * fsm_checkpoint_and_stop_postgres is used when shutting down Postgres as part
 * of some FSM step when we have a controlled situation. We do a couple
 * CHECKPOINT before stopping Postgres to ensure a smooth transition.
 */
bool
fsm_checkpoint_and_stop_postgres(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	PGSQL *pgsql = &(postgres->sqlClient);

	if (pg_setup_is_running(pgSetup))
	{
		/*
		 * Starting with Postgres 12, pg_basebackup sets the recovery
		 * configuration parameters in the postgresql.auto.conf file. We need
		 * to make sure to RESET this value so that our own configuration
		 * setting takes effect.
		 */
		if (pgSetup->control.pg_control_version >= 1200)
		{
			if (!pgsql_reset_primary_conninfo(pgsql))
			{
				log_error("Failed to RESET primary_conninfo");
				return false;
			}
		}

		/*
		 * PostgreSQL shutdown sequence includes a CHECKPOINT, that is issued
		 * by the checkpointer process one every query backend has stopped
		 * already. During this final CHECKPOINT no work can be done, so it's
		 * best to reduce the amount of work needed there. To reduce the
		 * checkpointer shutdown activity, we perform a manual shutdown while
		 * still having concurrent activity.
		 *
		 * The first checkpoint writes all the in-memory buffers, the second
		 * checkpoint writes everything that was added during the first one.
		 */
		log_info("Preparing Postgres shutdown: CHECKPOINT;");

		for (int i = 0; i < 2; i++)
		{
			if (!pgsql_checkpoint(pgsql))
			{
				log_warn("Failed to checkpoint before stopping Postgres");
			}
		}
	}

	log_info("Stopping Postgres at \"%s\"", pgSetup->pgdata);

	return ensure_postgres_service_is_stopped(postgres);
}


/*
 * fsm_init_standby_from_upstream is the work horse for both fsm_init_standby
 * and fsm_init_from_standby. The replication source must have been setup
 * already.
 */
static bool
fsm_init_standby_from_upstream(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);

	/*
	 * At pg_autoctl create time when PGDATA already exists and we were
	 * successful in registering the node, then we can proceed without a
	 * pg_basebackup: we already have a copy of PGDATA on-disk.
	 *
	 * The existence of PGDATA at pg_autoctl create time is tracked in our init
	 * state as the PRE_INIT_STATE_EXISTS enum value. Once init is finished, we
	 * remove our init file: then we need to pg_basebackup again to init a
	 * standby.
	 */
	bool skipBaseBackup = file_exists(keeper->config.pathnames.init) &&
						  keeper->initState.pgInitState == PRE_INIT_STATE_EXISTS;

	if (!standby_init_database(postgres, config->hostname, skipBaseBackup))
	{
		log_error("Failed to initialize standby server, see above for details");
		return false;
	}

	if (!skipBaseBackup)
	{
		bool forceCacheInvalidation = true;

		/* write our own HBA rules, pg_basebackup copies pg_hba.conf too */
		if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
		{
			log_error("Failed to update HBA rules after a base backup");
			return false;
		}
	}

	/*
	 * Publish our possibly new system_identifier now.
	 */
	if (!config->monitorDisabled)
	{
		if (!monitor_set_node_system_identifier(
				monitor,
				keeper->state.current_node_id,
				postgres->postgresSetup.control.system_identifier))
		{
			log_error("Failed to update the new node system_identifier");
			return false;
		}
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
 * fsm_init_standby is used when the primary is now ready to accept a standby,
 * we're the standby.
 */
bool
fsm_init_standby(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);

	NodeAddress *primaryNode = NULL;


	/* get the primary node to follow */
	if (!keeper_get_primary(keeper, &(postgres->replicationSource.primaryNode)))
	{
		log_error("Failed to initialize standby for lack of a primary node, "
				  "see above for details");
		return false;
	}

	if (!standby_init_replication_source(postgres,
										 primaryNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	return fsm_init_standby_from_upstream(keeper);
}


/*
 * fsm_rewind_or_init is used when a new primary is available. First, try to
 * rewind. If that fails, do a pg_basebackup.
 */
bool
fsm_rewind_or_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource *upstream = &(postgres->replicationSource);

	NodeAddress *primaryNode = NULL;

	/* get the primary node to follow */
	if (!keeper_get_primary(keeper, &(postgres->replicationSource.primaryNode)))
	{
		log_error("Failed to initialize standby for lack of a primary node, "
				  "see above for details");
		return false;
	}

	if (!standby_init_replication_source(postgres,
										 primaryNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	/* first, make sure we can connect with "replication" */
	if (!pgctl_identify_system(upstream))
	{
		log_error("Failed to connect to the primary node " NODE_FORMAT
				  "with a replication connection string. "
				  "See above for details",
				  upstream->primaryNode.nodeId,
				  upstream->primaryNode.name,
				  upstream->primaryNode.host,
				  upstream->primaryNode.port);
		return false;
	}

	if (!primary_rewind_to_standby(postgres))
	{
		bool skipBaseBackup = false;
		bool forceCacheInvalidation = true;

		log_warn("Failed to rewind demoted primary to standby, "
				 "trying pg_basebackup instead");

		if (!standby_init_database(postgres, config->hostname, skipBaseBackup))
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

		/* write our own HBA rules, pg_basebackup copies pg_hba.conf too */
		if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
		{
			log_error("Failed to update HBA rules after a base backup");
			return false;
		}
	}

	/*
	 * This node is now demoted: it used to be a primary node, it's not
	 * anymore. The replication slots that used to be maintained by the
	 * streaming replication protocol are now going to be maintained "manually"
	 * by pg_autoctl using pg_replication_slot_advance().
	 *
	 * There is a problem in pg_replication_slot_advance() in that it only
	 * maintains the restart_lsn property of a replication slot, it does not
	 * maintain the xmin of it. When re-using the pre-existing replication
	 * slots, we want to have a NULL xmin, so we drop the slots, and then
	 * create them again.
	 */
	if (!primary_drop_all_replication_slots(postgres))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * fsm_prepare_for_secondary is used when going from CATCHINGUP to SECONDARY,
 * to create missing replication slots. We want to maintain a replication slot
 * for each of the other nodes in the system, so that we make sure we have the
 * WAL bytes around when a standby nodes has to follow a new primary, after
 * failover.
 */
bool
fsm_prepare_for_secondary(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	/* first. check that we're on the same timeline as the new primary */
	if (!standby_check_timeline_with_upstream(postgres))
	{
		/* errors have already been logged */
		return false;
	}

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
 * fsm_start_maintenance_on_standby is used when putting the standby in
 * maintenance mode (kernel upgrades, change of hardware, etc). Maintenance
 * means that the user now is driving the service, refrain from doing anything
 * ourselves.
 */
bool
fsm_start_maintenance_on_standby(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	/* Move the Postgres controller out of the way */
	if (!local_postgres_unlink_status_file(postgres))
	{
		/* highly unexpected */
		log_error("Failed to remove our Postgres status file "
				  "see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_restart_standby is used when restarting a node after manual maintenance
 * is done. In case that changed we get the current primary from the monitor
 * and reset the standby setup (primary_conninfo) to target it, then restart
 * Postgres.
 *
 * We don't know what happened during the maintenance of the node, so we use
 * pg_rewind to make sure we're in a position to be a standby to the current
 * primary.
 *
 * So we're back to doing the exact same thing as fsm_rewind_or_init() now, and
 * that's why we just call that function.
 */
bool
fsm_restart_standby(Keeper *keeper)
{
	return fsm_rewind_or_init(keeper);
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
 * && keeper_create_and_drop_replication_slots
 *
 * Note that the HBA and slot maintenance are done eagerly in the main keeper
 * loop as soon as a new node is added to the group, so we don't need to handle
 * those operations in the context of a the FSM transitions anymore.
 *
 * So we reuse fsm_disable_replication() here, rather than copy/pasting the same
 * bits code in the fsm_promote_standby() function body. If the definition of
 * the fsm_promote_standby transition ever came to diverge from whatever
 * fsm_disable_replication() is doing, we'd have to copy/paste and maintain
 * separate code path.
 *
 * We open the HBA connections for the other node as found per given state,
 * most often a DEMOTE_TIMEOUT_STATE, sometimes though MAINTENANCE_STATE.
 */
bool
fsm_promote_standby(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!ensure_postgres_service_is_running(postgres))
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

	if (!standby_cleanup_as_primary(postgres))
	{
		log_error("Failed to cleanup replication settings, "
				  "see above for details");
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


/*
 * When more than one secondary is available for failover we need to pick one.
 * We want to pick the secondary that received the most WAL, so the monitor
 * asks every secondary to report its current LSN position.
 *
 * secondary ➜ report_lsn
 */
bool
fsm_report_lsn(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *pgsql = &(postgres->sqlClient);

	/*
	 * Forcibly disconnect from the primary node, for two reasons:
	 *
	 *  1. when the primary node can't connect to the monitor, and if there's
	 *     no replica currently connected, it will then proceed to DEMOTE
	 *     itself
	 *
	 *  2. that way we ensure that the current LSN we report can't change
	 *     anymore, because we are a standby without a primary_conninfo, and
	 *     without a restore_command either
	 *
	 * To disconnect the current node from its primary, we write a recovery
	 * setup where there is no primary_conninfo and otherwise use the same
	 * parameters as for streaming replication.
	 */
	NodeAddress upstreamNode = { 0 };

	if (!standby_init_replication_source(postgres,
										 &upstreamNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	log_info("Restarting standby node to disconnect replication "
			 "from failed primary node, to prepare failover");

	if (!standby_restart_with_current_replication_source(postgres))
	{
		log_error("Failed to disconnect from failed primary node, "
				  "see above for details");

		return false;
	}

	/*
	 * Fetch most recent metadata, that will be sent in the next node_active()
	 * call.
	 */
	if (!pgsql_get_postgres_metadata(pgsql,
									 &pgSetup->is_in_recovery,
									 postgres->pgsrSyncState,
									 postgres->currentLSN,
									 &(postgres->postgresSetup.control)))
	{
		log_error("Failed to update the local Postgres metadata");
		return false;
	}

	return true;
}


/*
 * fsm_report_lsn_and_drop_replication_slots is used when a former primary node
 * has been demoted and gets back online during the secondary election.
 *
 * As Postgres pg_replication_slot_advance() function does not maintain the
 * xmin property of the slot, we want to create new inactive slots now rather
 * than continue using previously-active (streaming replication) slots.
 */
bool
fsm_report_lsn_and_drop_replication_slots(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!fsm_report_lsn(keeper))
	{
		/* errors have already been reported */
		return false;
	}

	return primary_drop_all_replication_slots(postgres);
}


/*
 * When the selected failover candidate does not have the latest received WAL,
 * it fetches them from another standby, the first one with the most LSN
 * available.
 */
bool
fsm_fast_forward(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *upstream = &(postgres->replicationSource);

	NodeAddress upstreamNode = { 0 };

	char slotName[MAXCONNINFO] = { 0 };

	/* get the primary node to follow */
	if (!keeper_get_most_advanced_standby(keeper, &upstreamNode))
	{
		log_error("Failed to fast forward from the most advanced standby node, "
				  "see above for details");
		return false;
	}

	/*
	 * Postgres 10 does not have pg_replication_slot_advance(), so we don't
	 * support replication slots on standby nodes there.
	 */
	if (pgSetup->control.pg_control_version >= 1100)
	{
		strlcpy(slotName, config->replication_slot_name, MAXCONNINFO);
	}

	if (!standby_init_replication_source(postgres,
										 &upstreamNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 slotName,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 upstreamNode.lsn,
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	if (!standby_fetch_missing_wal(postgres))
	{
		log_error("Failed to fetch WAL bytes from standby node " NODE_FORMAT
				  ", see above for details",
				  upstream->primaryNode.nodeId,
				  upstream->primaryNode.name,
				  upstream->primaryNode.host,
				  upstream->primaryNode.port);
		return false;
	}

	return true;
}


/*
 * fsm_cleanup_as_primary cleans-up the replication setting. It's called after
 * a fast-forward operation.
 */
bool
fsm_cleanup_as_primary(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!standby_cleanup_as_primary(postgres))
	{
		log_error("Failed to cleanup replication settings and restart Postgres "
				  "to continue as a primary, see above for details");
		return false;
	}

	return true;
}


/*
 * When the failover is done we need to follow the new primary. We should be
 * able to do that directly, by changing our primary_conninfo, thanks to our
 * candidate selection where we make it so that the failover candidate always
 * has the most advanced LSN, and also thanks to our use of replication slots
 * on every standby.
 */
bool
fsm_follow_new_primary(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource *replicationSource = &(postgres->replicationSource);

	/* get the primary node to follow */
	if (!keeper_get_primary(keeper, &(postgres->replicationSource.primaryNode)))
	{
		log_error("Failed to initialize standby for lack of a primary node, "
				  "see above for details");
		return false;
	}

	if (!standby_init_replication_source(postgres,
										 NULL,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	if (!standby_follow_new_primary(postgres))
	{
		log_error("Failed to change standby setup to follow new primary "
				  "node " NODE_FORMAT ", see above for details",
				  replicationSource->primaryNode.nodeId,
				  replicationSource->primaryNode.name,
				  replicationSource->primaryNode.host,
				  replicationSource->primaryNode.port);
		return false;
	}

	/* now, in case we have an init state file around, remove it */
	if (!unlink_file(config->pathnames.init))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Finally, check that we're on the same timeline as the new primary when
	 * assigned secondary as a goal state. This transition function is also
	 * used when going from secondary to catchingup, as the primary might have
	 * changed also in that situation.
	 */
	if (keeper->state.assigned_role == SECONDARY_STATE)
	{
		return standby_check_timeline_with_upstream(postgres);
	}

	return true;
}


/*
 * fsm_init_from_standby creates a new node from existing nodes that are still
 * available but not setup to be a candidate for promotion.
 */
bool
fsm_init_from_standby(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);

	NodeAddress upstreamNode = { 0 };

	/* get the primary node to follow */
	if (!keeper_get_most_advanced_standby(keeper, &upstreamNode))
	{
		log_error("Failed to initialise from the most advanced standby node, "
				  "see above for details");
		return false;
	}

	if (!standby_init_replication_source(postgres,
										 &upstreamNode,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 "", /* no replication slot */
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 upstreamNode.lsn,
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	return fsm_init_standby_from_upstream(keeper);
}


/*
 * fsm_drop_node is called to finish dropping a node on the client side.
 *
 * This stops postgres and updates the postgres state file to say that postgres
 * should be stopped. It also cleans up any existing init file. Not doing these
 * two things can confuse a possible future re-init of the node.
 */
bool
fsm_drop_node(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	if (!fsm_stop_postgres(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	return unlink_file(config->pathnames.init);
}
