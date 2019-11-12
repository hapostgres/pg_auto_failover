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


#include "file_utils.h"
#include "keeper.h"
#include "keeper_config.h"
#include "pgsetup.h"
#include "state.h"


static bool keeper_get_replication_state(Keeper *keeper);


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

	if (!monitor_init(&keeper->monitor, config->monitor_pguri))
	{
		return false;
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

	keeperState->last_monitor_contact = now;
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

	log_trace("keeper_ensure_current_state: %s",
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
			if (postgres->pgIsRunning)
			{
				/* reset PostgreSQL restart failures tracking */
				postgres->pgFirstStartFailureTs = 0;
				postgres->pgStartRetries = 0;

				return true;
			}
			else if (ensure_local_postgres_is_running(postgres))
			{
				log_warn("PostgreSQL was not running, restarted with pid %ld",
						 pgSetup->pidFile.pid);

				return true;
			}
			else
			{
				log_warn("Failed to restart PostgreSQL, "
						 "see PostgreSQL logs for instance at \"%s\".",
						 pgSetup->pgdata);

				return false;
			}
			break;
		}

		case SINGLE_STATE:
		case SECONDARY_STATE:
		case WAIT_PRIMARY_STATE:
		case CATCHINGUP_STATE:
		case PREP_PROMOTION_STATE:
		case STOP_REPLICATION_STATE:
		{
			if (postgres->pgIsRunning)
			{
				return true;
			}
			else if (ensure_local_postgres_is_running(postgres))
			{
				log_warn("PostgreSQL was not running, restarted with pid %ld",
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
			break;
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
	else if ((now - postgres->pgFirstStartFailureTs) > timeout
		|| postgres->pgStartRetries >= retries)
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

	memcpy(pgSetup, &(config->pgSetup), sizeof(PostgresSetup));

	/* reinitialize the replication state values each time we update */
	postgres->pgIsRunning = false;
	memset(postgres->pgsrSyncState, 0, PGSR_SYNC_STATE_MAXLENGTH);
	strcpy(postgres->currentLSN, "0/0");

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
		case PRIMARY_STATE:
		case WAIT_PRIMARY_STATE:
		case SECONDARY_STATE:
		case CATCHINGUP_STATE:
		{
			return postgres->pgIsRunning
				&& keeper_get_replication_state(keeper);
		}

		default:
			/* we don't need to check replication state in those states */
			break;
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
 * keeper_get_replication_state connects to the local PostgreSQL instance and
 * fetches replication related information: pg_stat_replication.sync_state and
 * WAL lag.
 */
static bool
keeper_get_replication_state(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(keeper->postgres.postgresSetup);
	LocalPostgresServer *postgres = &(keeper->postgres);

	PGSQL *pgsql = &(postgres->sqlClient);
	bool missingStateOk = keeperState->current_role == WAIT_PRIMARY_STATE;

	bool success = false;

	/* figure out if we are in recovery or not */
	if (!pgsql_is_in_recovery(pgsql, &pgSetup->is_in_recovery))
	{
		/*
		 * errors have been logged already, probably failed to connect.
		 */
		pgsql_finish(pgsql);
		return false;
	}

	if (pg_setup_is_primary(pgSetup))
	{
		success =
			pgsql_get_sync_state_and_current_lsn(
				pgsql,
				config->replication_slot_name,
				postgres->pgsrSyncState,
				postgres->currentLSN,
				PG_LSN_MAXLENGTH,
				missingStateOk);
	}
	else
	{
		success = pgsql_get_received_lsn_from_standby(pgsql, postgres->currentLSN,
													  PG_LSN_MAXLENGTH);
	}
	pgsql_finish(pgsql);

	return success;
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
		log_info("The version of extenstion \"%s\" is \"%s\" on the monitor",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME, version.installedVersion);
	}

	return true;
}


/*
 * keeper_register_and_init registers the local node to the pg_auto_failover
 * Monitor in the given initialState, and then create the state on-disk with
 * the assigned goal from the Monitor.
 */
bool
keeper_register_and_init(Keeper *keeper,
						 KeeperConfig *config, NodeState initialState)
{
	Monitor *monitor = &(keeper->monitor);
	MonitorAssignedState assignedState = { 0 };

	if (!monitor_init(monitor, config->monitor_pguri))
	{
		log_fatal("Failed to contact the monitor because its URL is invalid, "
				  "see above for details");
		return false;
	}

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

	if (!monitor_register_node(monitor,
							   config->formation,
							   config->nodename,
							   config->pgSetup.pgport,
							   config->pgSetup.dbname,
							   config->groupId,
							   initialState,
							   config->pgSetup.pgKind,
							   &assignedState))
	{
		/* errors have already been logged, remove state file */
		unlink_file(config->pathnames.state);

		return false;
	}

	/* now that we have a state on-disk, finish init of the keeper instance */
	if (!keeper_init(keeper, config))
	{
		return false;
	}

	/* initialize FSM state from monitor's answer */
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
	if (!keeper_config_set_groupId(&(keeper->config), assignedState.groupId))
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
	if (!keeper_init_state_write(keeper))
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

	if (!monitor_init(&(keeper->monitor), config->monitor_pguri))
	{
		return false;
	}

	log_info("Removing local node from the pg_auto_failover monitor.");

	/*
	 * If the node was already removed from the monitor, then the
	 * monitor_remove function is going to return true here. It means that we
	 * can call `pg_autoctl drop node` again when we removed the node from the
	 * monitor already, but failed to remove the state file.
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
 * keeper_init_state_write create our pg_autoctl.init file.
 *
 * This file is created when entering keeper init and deleted only when the
 * init has been successful. This allows the code to take smarter decisions and
 * decipher in between a previous init having failed halfway through or
 * initializing from scratch in conditions not supported (pre-existing and
 * running cluster, etc).
 */
bool
keeper_init_state_write(Keeper *keeper)
{
	int fd;
	char buffer[PG_AUTOCTL_KEEPER_STATE_FILE_SIZE];
	KeeperStateInit initState = { 0 };
	PostgresSetup pgSetup = { 0 };
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;

	initState.pg_autoctl_state_version = PG_AUTOCTL_STATE_VERSION;

	if (!pg_setup_init(&pgSetup, &(keeper->config.pgSetup),
					   missingPgdataIsOk, pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialise the keeper init state, "
				  "see above for details");
		return false;
	}

	if (pg_setup_is_running(&pgSetup) && pg_setup_is_primary(&pgSetup))
	{
		initState.pgInitState = PRE_INIT_STATE_PRIMARY;
	}
	else if (pg_setup_is_running(&pgSetup))
	{
		initState.pgInitState = PRE_INIT_STATE_RUNNING;
	}
	else if (pg_setup_pgdata_exists(&pgSetup))
	{
		initState.pgInitState = PRE_INIT_STATE_EXISTS;
	}
	else
	{
		initState.pgInitState = PRE_INIT_STATE_EMTPY;
	}

	log_info("Writing keeper init state file at \"%s\"",
			 keeper->config.pathnames.init);
	log_debug("keeper_init_state_write: version = %d",
			  initState.pg_autoctl_state_version);
	log_debug("keeper_init_state_write: pgInitState = %s",
			  PreInitPostgreInstanceStateToString(initState.pgInitState));

	memset(buffer, 0, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE);
	memcpy(buffer, &initState, sizeof(KeeperStateInit));

	fd = open(keeper->config.pathnames.init,
			  O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		log_fatal("Failed to create keeper init state file \"%s\": %s",
				  keeper->config.pathnames.init, strerror(errno));
		return false;
	}

	errno = 0;
	if (write(fd, buffer, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE) !=
		PG_AUTOCTL_KEEPER_STATE_FILE_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
		{
			errno = ENOSPC;
		}
		log_fatal("Failed to write keeper state file \"%s\": %s",
				  keeper->config.pathnames.init, strerror(errno));
		return false;
	}

	if (fsync(fd) != 0)
	{
		log_fatal("fsync error: %s", strerror(errno));
		return false;
	}

	close(fd);

	return true;
}


/*
 * keeper_init_state_read reads the information kept in the keeper init file.
 */
bool
keeper_init_state_read(Keeper *keeper, KeeperStateInit *initState)
{
	char *filename = keeper->config.pathnames.init;
	char *content = NULL;
	long fileSize;
	int pg_autoctl_state_version = 0;

	log_debug("Reading current init state from \"%s\"", filename);

	if (!read_file(filename, &content, &fileSize))
	{
		log_error("Failed to read Keeper state from file \"%s\"", filename);
		return false;
	}

	pg_autoctl_state_version =
		((KeeperStateInit *) content)->pg_autoctl_state_version;

	if (fileSize >= sizeof(KeeperStateData)
		&& pg_autoctl_state_version == PG_AUTOCTL_STATE_VERSION)
	{
		memcpy(initState, content, sizeof(KeeperStateInit));
		free(content);
		return true;
	}

	free(content);

	/* Looks like it's a mess. */
	log_error("Keeper init state file \"%s\" exists but "
			  "is broken or wrong version",
			  filename);
	return false;
}
