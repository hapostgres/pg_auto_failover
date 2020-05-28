/*
 * src/bin/pg_autoctl/keeper.c
 *     Keeper state functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "parson.h"

#include "file_utils.h"
#include "keeper.h"
#include "keeper_config.h"
#include "pgsetup.h"
#include "primary_standby.h"
#include "state.h"


/*
 * keeper_init initialises the keeper logic according to the given keeper
 * configuration. It also reads the state file from disk. The state file
 * must be generated before calling keeper_init.
 */
bool
keeper_init(Keeper *keeper, KeeperConfig *config)
{
	PostgresSetup *pgSetup = &(config->pgSetup);

	keeper->config = *config;

	local_postgres_init(&keeper->postgres, pgSetup);

	if (!config->monitorDisabled)
	{
		if (!monitor_init(&keeper->monitor, config->monitor_pguri))
		{
			return false;
		}
	}

	if (!keeper_load_state(keeper))
	{
		/* errors logged in keeper_state_read */
		return false;
	}

	return true;
}


/*
 * keeper_load_state loads the current state of the keeper from the
 * configured state file.
 */
bool
keeper_load_state(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperConfig *config = &(keeper->config);

	return keeper_state_read(keeperState, config->pathnames.state);
}


/*
 * keeper_store_state stores the current state of the keeper in the configured
 * state file.
 */
bool
keeper_store_state(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperConfig *config = &(keeper->config);

	return keeper_state_write(keeperState, config->pathnames.state);
}


/*
 * keeper_update_state updates the keeper state and immediately writes
 * it to disk.
 */
bool
keeper_update_state(Keeper *keeper, int node_id, int group_id,
					NodeState state, bool update_last_monitor_contact)
{
	KeeperStateData *keeperState = &(keeper->state);
	uint64_t now = time(NULL);

	if (update_last_monitor_contact)
	{
		keeperState->last_monitor_contact = now;
	}
	keeperState->current_node_id = node_id;
	keeperState->current_group = group_id;
	keeperState->assigned_role = state;

	if (!keeper_store_state(keeper))
	{
		/* keeper_state_write logs errors */
		return false;
	}

	log_keeper_state(keeperState);

	return true;
}


/*
 * keeper_should_ensure_current_state returns true when pg_autoctl should
 * ensure that Postgres is running, or not running, depending on the current
 * FSM state, before calling the transition function to the next state.
 *
 * At the moment, the only cases when we DON'T want to ensure the current state
 * are when either the current state or the goal state are one of the following:
 *
 *  - DRAINING
 *  - DEMOTED
 *  - DEMOTE TIMEOUT
 *
 * That's because we would then stop Postgres first when going from DEMOTED to
 * SINGLE, or ensure Postgres is running when going from PRIMARY to DEMOTED.
 * This last example is a split-brain hazard, too.
 */
bool
keeper_should_ensure_current_state_before_transition(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);

	if (keeperState->assigned_role == keeperState->current_role)
	{
		/* this function should not be called in that case */
		log_debug("BUG: keeper_should_ensure_current_state_before_transition "
				  "called with assigned role == current role == %s",
				  NodeStateToString(keeperState->assigned_role));
		return false;
	}

	if (keeperState->assigned_role == DRAINING_STATE ||
		keeperState->assigned_role == DEMOTE_TIMEOUT_STATE ||
		keeperState->assigned_role == DEMOTED_STATE)
	{
		/* don't ensure Postgres is running before shutting it down */
		return false;
	}

	if (keeperState->current_role == DRAINING_STATE ||
		keeperState->current_role == DEMOTE_TIMEOUT_STATE ||
		keeperState->current_role == DEMOTED_STATE)
	{
		/* don't ensure Postgres is down before starting it again */
		return false;
	}

	/* in all other cases, yes please ensure the current state */
	return true;
}


/*
 * keeper_ensure_current_state ensures that the current keeper's state is met
 * with the current PostgreSQL status, at minimum that PostgreSQL is running
 * when it's expected to be, etc.
 */
bool
keeper_ensure_current_state(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);

	log_debug("Ensuring current state: %s",
			  NodeStateToString(keeperState->current_role));

	switch (keeperState->current_role)
	{
		/*
		 * When in primary state, publishing that PostgreSQL is down might
		 * trigger a failover. This is the best solution only when we tried
		 * everything else. So first, retry starting PostgreSQL a couple more
		 * times.
		 *
		 * See configuration parameters:
		 *
		 *   timeout.postgresql_fails_to_start_timeout (default 20s)
		 *   timeout.postgresql_fails_to_start_retries (default 3 times)
		 */
		case PRIMARY_STATE:
		{
			if (!keeper_ensure_postgres_is_running(keeper, true))
			{
				/* errors have already been logged */
				return false;
			}

			/* when a standby has been removed, remove its replication slot */
			return keeper_drop_replication_slots_for_removed_nodes(keeper);
		}

		case SINGLE_STATE:
		{
			/* a single node does not need to maintain retries attempts */
			if (!keeper_ensure_postgres_is_running(keeper, false))
			{
				/* errors have already been logged */
				return false;
			}

			/* when a standby has been removed, remove its replication slot */
			return keeper_drop_replication_slots_for_removed_nodes(keeper);
		}

		/*
		 * In the following states, we don't want to maintain local replication
		 * slots, either because we're a primary and the replication protocol
		 * is taking care of that, or because we're in the middle of changing
		 * the replication upstream node.
		 */
		case WAIT_PRIMARY_STATE:
		case PREP_PROMOTION_STATE:
		case STOP_REPLICATION_STATE:
		{
			return keeper_ensure_postgres_is_running(keeper, false);
		}

		case SECONDARY_STATE:
		{
			bool updateRetries = false;

			if (!keeper_ensure_postgres_is_running(keeper, updateRetries))
			{
				/* errors have already been logged */
				return false;
			}

			/* now ensure progress is made on the replication slots */
			return keeper_maintain_replication_slots(keeper);
		}

		/*
		 * We don't maintain replication slots in CATCHINGUP state. We might
		 * not be in a position to pg_replication_slot_advance() the slot to
		 * the position required by the other standby nodes. Typically we would
		 * get a Postgres error such as the following:
		 *
		 *   cannot advance replication slot to 0/5000060, minimum is 0/6000028
		 */
		case CATCHINGUP_STATE:
		{
			bool updateRetries = false;

			return keeper_ensure_postgres_is_running(keeper, updateRetries);
		}

		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		case DRAINING_STATE:
		{
			if (postgres->pgIsRunning)
			{
				log_warn("PostgreSQL is running while in state \"%s\", "
						 "stopping PostgreSQL.",
						 NodeStateToString(keeperState->current_role));

				return pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata);
			}
			return true;
		}

		case MAINTENANCE_STATE:
		default:

			/* nothing to be done here */
			return true;
	}

	/* should never happen */
	return false;
}


/*
 * reportPgIsRunning returns the boolean that we should use to report
 * pgIsRunning to the monitor. When the local PostgreSQL isn't running, we
 * continue reporting that it is for some time, depending on the following
 * configuration parameters:
 *
 *   timeout.postgresql_restart_failure_timeout (default 20s)
 *   timeout.postgresql_restart_failure_max_retries (default 3 times)
 */
bool
ReportPgIsRunning(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);

	int retries = config->postgresql_restart_failure_max_retries;
	int timeout = config->postgresql_restart_failure_timeout;
	uint64_t now = time(NULL);

	if (keeperState->current_role != PRIMARY_STATE)
	{
		/*
		 * Only when in the PRIMARY_STATE is the monitor going to consider a
		 * failover to another node. That's when we should be careful about
		 * having attempted all we could before resigning.
		 *
		 * When we're not in PRIMARY_STATE, then it's ok to immediately report
		 * that PostgreSQL is not running, for immediate decision making on the
		 * monitor's side.
		 */
		return postgres->pgIsRunning;
	}

	/*
	 * Now we know the current state is PRIMARY_STATE. If PostgreSQL is
	 * running, then we simply report that, easy.
	 */
	if (postgres->pgIsRunning)
	{
		return postgres->pgIsRunning;
	}
	else if (postgres->pgFirstStartFailureTs == 0)
	{
		/*
		 * Oh, that's quite strange. It means we just fell in a code path where
		 * pgIsRunning is set to false, and didn't call
		 * ensure_local_postgres_is_running() to restart it.
		 */
		log_debug("ReportPgIsRunning: PostgreSQL is not running, "
				  "and has not been restarted.");

		return postgres->pgIsRunning;
	}
	else if ((now - postgres->pgFirstStartFailureTs) > timeout ||
			 postgres->pgStartRetries >= retries)
	{
		/*
		 * If we fail to restart PostgreSQL 3 times in a row within the last 20
		 * seconds (default values), then report the failure to the monitor for
		 * immediate action (failover, depending on the secondary health &
		 * reporting).
		 */
		log_error("Failed to restart PostgreSQL %d times in the "
				  "last %" PRIu64 "s, reporting PostgreSQL not running to "
								  "the pg_auto_failover monitor.",
				  postgres->pgStartRetries,
				  now - postgres->pgFirstStartFailureTs);

		return false;
	}
	else
	{
		/*
		 * Don't tell the monitor yet, pretend PostgreSQL is running: we might
		 * be able to get the service back running, it's too early for a
		 * failover to be our best option yet.
		 */
		log_warn("PostgreSQL failed to start %d/%d times before "
				 "reporting to the monitor, trying again",
				 postgres->pgStartRetries, retries);

		return true;
	}

	/* we never reach this point. */
}


/*
 * keeper_update_pg_state updates our internal reflection of the PostgreSQL
 * state.
 *
 * It returns true when we could successfully update the PostgreSQL state and
 * everything makes sense, and false when either we failed to update the state,
 * or when there's a serious problem with PostgreSQL and our expections are not
 * met. Examples of returning false include:
 *  - Postgres is running on a different port than configured
 *  - Postgres system identifier has changed from our keeper state
 *  - We failed to obtain the replication state from pg_stat_replication
 */
bool
keeper_update_pg_state(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *pgsql = &(postgres->sqlClient);

	bool pg_is_not_running_is_ok = true;

	log_debug("Update local PostgreSQL state");

	*pgSetup = config->pgSetup;

	/* reinitialize the replication state values each time we update */
	postgres->pgIsRunning = false;
	memset(postgres->pgsrSyncState, 0, PGSR_SYNC_STATE_MAXLENGTH);
	strlcpy(postgres->currentLSN, "0/0", sizeof(postgres->currentLSN));

	/*
	 * In some states, it's ok to not have a PostgreSQL data directory at all.
	 */
	switch (keeperState->current_role)
	{
		case NO_STATE:
		case INIT_STATE:
		case WAIT_STANDBY_STATE:
		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		case STOP_REPLICATION_STATE:
		{
			bool missing_ok = true;

			/*
			 * Given missing_ok, pg_controldata returns true even when the
			 * directory doesn't exists, so we take care of that situation
			 * ourselves here.
			 */
			if (directory_exists(config->pgSetup.pgdata))
			{
				if (!pg_controldata(pgSetup, missing_ok))
				{
					/*
					 * In case of corrupted files in PGDATA, avoid spurious log
					 * messages later when checking everything is fine: we
					 * already know that things are not fine, don't bother
					 * checking system_identifier etc.
					 */
					return false;
				}
			}
			else
			{
				/* If there's no PGDATA, just stop here, and it's ok */
				return true;
			}
			break;
		}

		default:
		{
			bool missing_ok = false;

			if (!pg_controldata(pgSetup, missing_ok))
			{
				/* If there's no PGDATA, just stop here: we have a problem */
				return false;
			}
		}
	}

	if (keeperState->system_identifier != pgSetup->control.system_identifier)
	{
		if (keeperState->system_identifier == 0)
		{
			keeperState->system_identifier =
				pgSetup->control.system_identifier;
		}
		else
		{
			/*
			 * This is a physical replication deal breaker, so it's mighty
			 * confusing to get that here. In the least, the keeper should get
			 * initialized from scratch again, but basically, we don't know
			 * what we are doing anymore.
			 */
			log_error("Unknown PostgreSQL system identifier: %" PRIu64 ", "
																	   "expected %" PRIu64,
					  pgSetup->control.system_identifier,
					  keeperState->system_identifier);
			return false;
		}
	}

	if (keeperState->pg_control_version != pgSetup->control.pg_control_version)
	{
		if (keeperState->pg_control_version == 0)
		{
			keeperState->pg_control_version =
				pgSetup->control.pg_control_version;
		}
		else
		{
			log_warn("PostgreSQL version changed from %u to %u",
					 keeperState->pg_control_version,
					 pgSetup->control.pg_control_version);
		}
	}

	if (keeperState->catalog_version_no != pgSetup->control.catalog_version_no)
	{
		if (keeperState->catalog_version_no == 0)
		{
			keeperState->catalog_version_no =
				pgSetup->control.catalog_version_no;
		}
		else
		{
			log_warn("PostgreSQL catalog version changed from %u to %u",
					 keeperState->catalog_version_no,
					 pgSetup->control.catalog_version_no);
		}
	}

	/*
	 * When PostgreSQL is running, do some extra checks that are going to be
	 * helpful to drive the keeper's FSM decision making.
	 */
	if (pg_setup_is_ready(pgSetup, pg_is_not_running_is_ok))
	{
		char connInfo[MAXCONNINFO];

		if (pgSetup->pidFile.port != config->pgSetup.pgport)
		{
			log_fatal("PostgreSQL is expected to run on port %d, "
					  "found to be running on port %d",
					  config->pgSetup.pgport, pgSetup->pidFile.port);
			return false;
		}

		/* we know now that Postgres is running (and ready) */
		postgres->pgIsRunning = true;

		/*
		 * Reinitialise connection string in case host changed or was first
		 * discovered.
		 */
		pg_setup_get_local_connection_string(pgSetup, connInfo);
		pgsql_init(pgsql, connInfo, PGSQL_CONN_LOCAL);

		/*
		 * Update our Postgres metadata now.
		 *
		 * First, update our cache of file path locations for Postgres
		 * configuration files (including HBA), in case it's been moved to
		 * somewhere else. This could happen when using the debian/ubuntu
		 * pg_createcluster command on an already existing cluster, for
		 * instance.
		 *
		 * Also update our view of pg_is_in_recovery, the replication sync
		 * state when we are a primary with a standby currently using our
		 * replication slot, and our current LSN position.
		 *
		 */
		if (!pgsql_get_postgres_metadata(pgsql,
										 config->replication_slot_name,
										 &pgSetup->is_in_recovery,
										 postgres->pgsrSyncState,
										 postgres->currentLSN))
		{
			log_error("Failed to update the local Postgres metadata");
			return false;
		}
	}
	else
	{
		/* Postgres is not running. */
		postgres->pgIsRunning = false;
	}

	/*
	 * In some states, PostgreSQL isn't expected to be running, or not expected
	 * to have a streaming replication to monitor at all.
	 */
	switch (keeperState->current_role)
	{
		case WAIT_PRIMARY_STATE:
		{
			/* we don't expect to have a streaming replica */
			return postgres->pgIsRunning;
		}

		case PRIMARY_STATE:
		{
			/*
			 * We expect to be able to read the current LSN, as always when
			 * Postgres is running, and we also expect replication to be in
			 * place when in PRIMARY state.
			 *
			 * On the primary, we use pg_stat_replication.sync_state to have an
			 * idea of how the replication is going. The query we use in
			 * pgsql_get_postgres_metadata should always return a non-empty
			 * string when we are a PRIMARY and our standby is connected.
			 */

			if (IS_EMPTY_STRING_BUFFER(postgres->pgsrSyncState))
			{
				log_error("Failed to fetch current replication properties "
						  "from standby node: no standby connected in "
						  "pg_stat_replication.");
				log_warn("HINT: check pg_autoctl and Postgres logs on "
						 "standby nodes");
			}

			return postgres->pgIsRunning &&
				   !IS_EMPTY_STRING_BUFFER(postgres->currentLSN) &&
				   !IS_EMPTY_STRING_BUFFER(postgres->pgsrSyncState);
		}

		case SECONDARY_STATE:
		case CATCHINGUP_STATE:
		{
			/* pg_stat_replication.sync_state is only available upstream */
			return postgres->pgIsRunning &&
				   !IS_EMPTY_STRING_BUFFER(postgres->currentLSN);
		}

		default:
		{
			/* we don't need to check replication state in those states */
			break;
		}
	}

	return true;
}


/*
 * keeper_start_postgres calls pg_ctl_start and then update our local
 * PostgreSQL instance setup and connection string to reflect the new reality.
 */
bool
keeper_start_postgres(Keeper *keeper)
{
	PostgresSetup *pgSetup = &(keeper->config.pgSetup);

	if (!pg_ctl_start(pgSetup->pg_ctl,
					  pgSetup->pgdata,
					  pgSetup->pgport,
					  pgSetup->listen_addresses))
	{
		log_error("Failed to start PostgreSQL at \"%s\" on port %d, "
				  "see above for details.",
				  pgSetup->pgdata, pgSetup->pgport);
		return false;
	}
	if (!keeper_update_pg_state(keeper))
	{
		log_error("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above for details.");
		return false;
	}
	return true;
}


/*
 * keeper_restart_postgres calls pg_ctl_restart and then update our local
 * PostgreSQL instance setup and connection string to reflect the new reality.
 */
bool
keeper_restart_postgres(Keeper *keeper)
{
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);

	if (!pg_ctl_restart(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to restart PostgreSQL instance at \"%s\", "
				  "see above for details.", pgSetup->pgdata);
		return false;
	}
	if (!keeper_update_pg_state(keeper))
	{
		log_error("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above for details.");
		return false;
	}
	return true;
}


/*
 * keeper_ensure_postgres_is_running ensures that Postgres is running.
 */
bool
keeper_ensure_postgres_is_running(Keeper *keeper, bool updateRetries)
{
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (postgres->pgIsRunning)
	{
		if (updateRetries)
		{
			/* reset PostgreSQL restart failures tracking */
			postgres->pgFirstStartFailureTs = 0;
			postgres->pgStartRetries = 0;
		}
		return true;
	}
	else if (ensure_local_postgres_is_running(postgres))
	{
		log_warn("PostgreSQL was not running, restarted with pid %d",
				 pgSetup->pidFile.pid);
		return true;
	}
	else
	{
		log_error("Failed to restart PostgreSQL, "
				  "see PostgreSQL logs for instance at \"%s\".",
				  pgSetup->pgdata);
		return false;
	}
}


/*
 * keeper_create_self_signed_cert creates SSL self-signed certificates if
 * needed within the current configuration, and then makes sure we update our
 * keeper configuration both in-memory and on-disk with the new normalized
 * filenames of the certificate files created.
 */
bool
keeper_create_self_signed_cert(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	if (pgSetup->ssl.createSelfSignedCert &&
		!(file_exists(pgSetup->ssl.serverKey) &&
		  file_exists(pgSetup->ssl.serverCert)))
	{
		if (!pg_create_self_signed_cert(pgSetup, config->nodename))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}
	}

	/* ensure the SSL setup is synced with the keeper config */
	config->pgSetup.ssl = pgSetup->ssl;

	/* update our configuration with ssl server.{key,cert} */
	if (!keeper_config_write_file(config))
	{
		/* errors have already been logged */
		return false;
	}
	return true;
}


/*
 * keeper_ensure_configuration updates the Postgres settings to match the
 * pg_autoctl configuration file, if necessary.
 *
 * This includes making sure that the SSL server.{key,cert} files are used in
 * the Postgres configuration, and on a secondary server, that means updating
 * the primary_conninfo connection string to make sure we use the proper
 * sslmode that is setup.
 *
 * This could change anytime with `pg_autoctl enable|disable ssl`. We cache the
 * primary node information in the LocalPostgresServer with the other
 * replicationSource parameters, and the monitor has the responsiblity to
 * instruct us when this cache needs to be invalidated (new primary, etc).
 */
bool
keeper_ensure_configuration(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *state = &(keeper->state);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	bool postgresNotRunningIsOk = false;

	/*
	 * We just reloaded our configuration file from disk. Use the pgSetup from
	 * the new configuration to re-init our local postgres instance
	 * information, including a maybe different SSL setup.
	 */
	postgres->postgresSetup = config->pgSetup;

	if (!local_postgres_update(postgres, postgresNotRunningIsOk))
	{
		log_error("Failed to reload configuration, see above for details");
		return false;
	}

	/*
	 * We might have to deploy a new Postgres configuration, from new SSL
	 * options being found in our pg_autoctl configuration file or for other
	 * reasons.
	 */
	if (!postgres_add_default_settings(postgres))
	{
		log_warn("Failed to edit Postgres configuration after "
				 "reloading pg_autoctl configuration, "
				 "see above for details");
		return false;
	}

	/*
	 * In pg_auto_failover before version 1.3 we would use pg_basebackup with
	 * the --write-recovery-conf option. Starting with Postgres 12, this option
	 * would cause pg_basebackup to edit postgresql.auto.conf rather than
	 * recovery.conf... meaning that our own setup would not have any effect.
	 *
	 * Now is a good time to clean-up, at start-up or reload, and either on a
	 * primary or a secondary, because those parameters should not remain set
	 * on a primary either.
	 */
	if (pgSetup->control.pg_control_version >= 1200)
	{
		/* errors are logged already, and non-fatal to this function */
		(void) pgsql_reset_primary_conninfo(&(postgres->sqlClient));
	}

	if (!pgsql_reload_conf(&(postgres->sqlClient)))
	{
		log_warn("Failed to reload Postgres configuration after "
				 "reloading pg_autoctl configuration, "
				 "see above for details");
		return false;
	}

	if (!monitor_init(&(keeper->monitor), config->monitor_pguri))
	{
		/* we tested already in keeper_config_accept_new, but... */
		log_warn("Failed to contact the monitor because its "
				 "URL is invalid, see above for details");
		return false;
	}

	/*
	 * On a standby server we might have to produce a new recovery settings
	 * file (either recovery.conf or postgresql-auto-failover-standby.conf) and
	 * then restart Postgres.
	 */
	if (state->current_role == CATCHINGUP_STATE ||
		state->current_role == SECONDARY_STATE ||
		state->current_role == MAINTENANCE_STATE)
	{
		ReplicationSource *upstream = &(postgres->replicationSource);

		/* either recovery.conf or AUTOCTL_STANDBY_CONF_FILENAME */
		char *relativeConfPathName =
			pgSetup->control.pg_control_version < 1200
			? "recovery.conf"
			: AUTOCTL_STANDBY_CONF_FILENAME;

		char upstreamConfPath[MAXPGPATH] = { 0 };

		char *currentConfContents = NULL;
		long currentConfSize = 0L;

		char *newConfContents = NULL;
		long newConfSize = 0L;

		/* do we have the primaryNode already? */
		if (IS_EMPTY_STRING_BUFFER(upstream->primaryNode.host))
		{
			log_debug("keeper_update_primary_conninfo: monitor_get_primary()");

			if (!monitor_get_primary(monitor,
									 config->formation,
									 state->current_group,
									 &(upstream->primaryNode)))
			{
				log_error("Failed to update primary_conninfo because getting "
						  "the primary node from the monitor failed, "
						  "see above for details");
				return false;
			}
		}

		/*
		 * Read the contents of the standby configuration file now, so that we
		 * only restart Postgres when it has been changed with the next step.
		 */
		join_path_components(upstreamConfPath,
							 pgSetup->pgdata,
							 relativeConfPathName);

		if (file_exists(upstreamConfPath))
		{
			if (!read_file(upstreamConfPath,
						   &currentConfContents,
						   &currentConfSize))
			{
				/* errors have already been logged */
				return false;
			}
		}

		/* prepare a replicationSource from the primary and our SSL setup */
		if (!standby_init_replication_source(postgres,
											 NULL, /* primaryNode is done */
											 PG_AUTOCTL_REPLICA_USERNAME,
											 config->replication_password,
											 config->replication_slot_name,
											 config->maximum_backup_rate,
											 config->backupDirectory,
											 config->pgSetup.ssl,
											 state->current_node_id))
		{
			/* can't happen at the moment */
			return false;
		}

		/* now setup the replication configuration (primary_conninfo etc) */
		if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
								   pgSetup->pgdata,
								   upstream))
		{
			log_error("Failed to setup Postgres as a standby after primary "
					  "connection settings change");
			return false;
		}

		/* restart Postgres only when the configuration file has changed */
		if (!read_file(upstreamConfPath, &newConfContents, &newConfSize))
		{
			/* errors have already been logged */
			return false;
		}

		if (currentConfContents == NULL ||
			strcmp(newConfContents, currentConfContents) != 0)
		{
			log_info("Replication settings at \"%s\" have changed, "
					 "restarting Postgres", upstreamConfPath);

			if (!pgsql_checkpoint(&(postgres->sqlClient)))
			{
				log_warn("Failed to CHECKPOINT before restart, "
						 "see above for details");
			}

			if (!keeper_restart_postgres(keeper))
			{
				log_error("Failed to restart Postgres to enable new "
						  "replication settings, see above for details");
				return false;
			}
		}
	}

	return true;
}


/*
 * keeper_drop_replication_slots_for_removed_nodes drops replication slots that
 * we have on the local Postgres instance when the node is not registered on
 * the monitor anymore (after a pgautofailover.remove_node() has been issued,
 * maybe with the command `pg_autoctl drop node [ --destroy ]`).
 */
bool
keeper_drop_replication_slots_for_removed_nodes(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);

	char *host = keeper->config.nodename;
	int port = pgSetup->pgport;

	log_trace("keeper_drop_replication_slots_for_removed_nodes");

	if (!monitor_get_other_nodes(monitor, host, port,
								 ANY_STATE, &(keeper->otherNodes)))
	{
		/* errors have already been logged */
		return false;
	}

	if (!postgres_replication_slot_drop_removed(postgres, &(keeper->otherNodes)))
	{
		log_error("Failed to maintain replication slots on the local Postgres "
				  "instance, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_advance_replication_slots loops over the other standby nodes and
 * advance their replication slots up to the current LSN value known by the
 * monitor.
 */
bool
keeper_maintain_replication_slots(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);

	char *host = keeper->config.nodename;
	int port = pgSetup->pgport;

	log_trace("keeper_maintain_replication_slots");

	if (pgSetup->control.pg_control_version < 1100)
	{
		/* Postgres 10 does not have pg_replication_slot_advance() */
		return true;
	}

	if (!monitor_get_other_nodes(monitor, host, port,
								 ANY_STATE, &(keeper->otherNodes)))
	{
		/* errors have already been logged */
		return false;
	}

	if (!postgres_replication_slot_maintain(postgres, &(keeper->otherNodes)))
	{
		log_error("Failed to maintain replication slots on the local Postgres "
				  "instance, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_check_monitor_extension_version checks that the monitor we connect to
 * has an extension version compatible with our expectations.
 */
bool
keeper_check_monitor_extension_version(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	MonitorExtensionVersion version = { 0 };

	if (!monitor_get_extension_version(monitor, &version))
	{
		log_fatal("Failed to check version compatibility with the monitor "
				  "extension \"%s\", see above for details",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		return false;
	}

	/* from a member of the cluster, we don't try to upgrade the extension */
	if (strcmp(version.installedVersion, PG_AUTOCTL_EXTENSION_VERSION) != 0)
	{
		log_fatal("The monitor at \"%s\" has extension \"%s\" version \"%s\", "
				  "this pg_autoctl version requires version \"%s\".",
				  keeper->config.monitor_pguri,
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME,
				  PG_AUTOCTL_EXTENSION_VERSION,
				  version.installedVersion);
		log_info("Please connect to the monitor node and restart pg_autoctl.");
		return false;
	}
	else
	{
		log_info("The version of extension \"%s\" is \"%s\" on the monitor",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME, version.installedVersion);
	}

	return true;
}


/*
 * keeper_init_fsm initializes the keeper's local FSM and does nothing more.
 *
 * It's only intended to be used when we are not using a monitor, which means
 * we're going to expose our FSM driving as an HTTP API, and sit there waiting
 * for orders from another software.
 *
 * The function is modeled to look like keeper_register_and_init with the
 * difference that we don't have a monitor to talk to.
 */
bool
keeper_init_fsm(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	/* fake the initial state provided at monitor registration time */
	MonitorAssignedState assignedState = {
		.nodeId = -1,
		.groupId = -1,
		.state = INIT_STATE
	};

	/*
	 * First try to create our state file. The keeper_state_create_file function
	 * may fail if we have no permission to write to the state file directory
	 * or the disk is full. In that case, we stop before having registered the
	 * local PostgreSQL node to the monitor.
	 */
	if (!keeper_state_create_file(config->pathnames.state))
	{
		log_fatal("Failed to create a state file prior to registering the "
				  "node with the monitor, see above for details");
		return false;
	}

	/* now that we have a state on-disk, finish init of the keeper instance */
	if (!keeper_init(keeper, config))
	{
		return false;
	}

	/* initialize FSM state */
	if (!keeper_update_state(keeper,
							 assignedState.nodeId,
							 assignedState.groupId,
							 assignedState.state,
							 false))
	{
		log_error("Failed to update keepers's state");

		/*
		 * Make sure we don't have a corrupted state file around, that could
		 * prevent trying to init again and cause strange errors.
		 */
		unlink_file(config->pathnames.state);

		return false;
	}

	/*
	 * Leave a track record that we're ok to initialize in PGDATA, so that in
	 * case of `pg_autoctl create` being interrupted, we may resume operations
	 * and accept to work on already running PostgreSQL primary instances.
	 */
	if (!keeper_init_state_create(&(keeper->initState),
								  pgSetup,
								  config->pathnames.init))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * keeper_register_and_init registers the local node to the pg_auto_failover
 * Monitor in the given initialState, and then create the state on-disk with
 * the assigned goal from the Monitor.
 */
bool
keeper_register_and_init(Keeper *keeper, NodeState initialState)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	KeeperStateInit *initState = &(keeper->initState);
	Monitor *monitor = &(keeper->monitor);
	MonitorAssignedState assignedState = { 0 };

	/*
	 * First try to create our state file. The keeper_state_create_file function
	 * may fail if we have no permission to write to the state file directory
	 * or the disk is full. In that case, we stop before having registered the
	 * local PostgreSQL node to the monitor.
	 */
	if (!keeper_state_create_file(config->pathnames.state))
	{
		log_fatal("Failed to create a state file prior to registering the "
				  "node with the monitor, see above for details");
		return false;
	}

	/* now that we have a state on-disk, finish init of the keeper instance */
	if (!keeper_init(keeper, config))
	{
		return false;
	}

	if (!monitor_register_node(monitor,
							   config->formation,
							   config->nodename,
							   config->pgSetup.pgport,
							   config->pgSetup.control.system_identifier,
							   config->pgSetup.dbname,
							   config->groupId,
							   initialState,
							   config->pgSetup.pgKind,
							   config->pgSetup.settings.candidatePriority,
							   config->pgSetup.settings.replicationQuorum,
							   &assignedState))
	{
		/* errors have already been logged, remove state file */
		unlink_file(config->pathnames.state);

		return false;
	}

	/* initialize FSM state from monitor's answer */
	log_info("Writing keeper state file at \"%s\"", config->pathnames.state);

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
		unlink_file(config->pathnames.state);

		return false;
	}

	/* also update the groupId in the configuration file. */
	if (!keeper_config_set_groupId_and_slot_name(&(keeper->config),
												 assignedState.nodeId,
												 assignedState.groupId))
	{
		log_error("Failed to update the configuration file with the groupId: %d",
				  assignedState.groupId);
		return false;
	}

	/*
	 * Leave a track record that we're ok to initialize in PGDATA, so that in
	 * case of `pg_autoctl create` being interrupted, we may resume operations
	 * and accept to work on already running PostgreSQL primary instances.
	 */
	if (!keeper_init_state_create(initState,
								  pgSetup,
								  keeper->config.pathnames.init))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * keeper_remove removes the local node from the monitor and then removes the
 * local state file.
 */
bool
keeper_remove(Keeper *keeper, KeeperConfig *config, bool ignore_monitor_errors)
{
	int errors = 0;

	/*
	 * We don't require keeper_init() to have been done before calling
	 * keeper_remove, because then we would fail to finish a remove that was
	 * half-done only: keeper_init loads the state from the state file, which
	 * might not exists anymore.
	 *
	 * That said, we're going to require keeper->config to have been set the
	 * usual way, so do that at least.
	 */
	keeper->config = *config;

	if (!config->monitorDisabled)
	{
		if (!monitor_init(&(keeper->monitor), config->monitor_pguri))
		{
			return false;
		}

		log_info("Removing local node from the pg_auto_failover monitor.");

		/*
		 * If the node was already removed from the monitor, then the
		 * monitor_remove function is going to return true here. It means that
		 * we can call `pg_autoctl drop node` again when we removed the node
		 * from the monitor already, but failed to remove the state file.
		 */
		if (!monitor_remove(&(keeper->monitor),
							config->nodename,
							config->pgSetup.pgport))
		{
			/* we already logged about errors */
			errors++;

			if (!ignore_monitor_errors)
			{
				return false;
			}
		}
	}

	log_info("Removing local node state file: \"%s\"", config->pathnames.state);

	if (!unlink_file(config->pathnames.state))
	{
		/* we already logged about errors */
		errors++;
	}

	log_info("Removing local node init state file: \"%s\"",
			 config->pathnames.init);

	if (!unlink_file(config->pathnames.init))
	{
		/* we already logged about errors */
		errors++;
	}

	return errors == 0;
}


/*
 * keeper_state_as_json prepares the current keeper state as a JSON object and
 * copy the string to the given pre-allocated memory area, of given size.
 */
bool
keeper_state_as_json(Keeper *keeper, char *json, int size)
{
	JSON_Value *js = json_value_init_object();
	JSON_Value *jsPostgres = json_value_init_object();
	JSON_Value *jsKeeperState = json_value_init_object();

	JSON_Object *jsRoot = json_value_get_object(js);

	char *serialized_string = NULL;
	int len;

	pg_setup_as_json(&(keeper->postgres.postgresSetup), jsPostgres);
	keeperStateAsJSON(&(keeper->state), jsKeeperState);

	json_object_set_value(jsRoot, "postgres", jsPostgres);
	json_object_set_value(jsRoot, "state", jsKeeperState);

	serialized_string = json_serialize_to_string_pretty(js);

	len = strlcpy(json, serialized_string, size);

	json_free_serialized_string(serialized_string);
	json_value_free(js);

	/* strlcpy returns how many bytes where necessary */
	return len < size;
}
