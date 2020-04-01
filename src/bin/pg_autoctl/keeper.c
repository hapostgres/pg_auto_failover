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
#include "state.h"


static bool keeper_init_state_write(Keeper *keeper);


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
			return postgres->pgIsRunning
				&& !IS_EMPTY_STRING_BUFFER(postgres->currentLSN)
				&& !IS_EMPTY_STRING_BUFFER(postgres->pgsrSyncState);
		}

		case SECONDARY_STATE:
		case CATCHINGUP_STATE:
		{
			/* pg_stat_replication.sync_state is only available upstream */
			return postgres->pgIsRunning
				&& !IS_EMPTY_STRING_BUFFER(postgres->currentLSN);
		}

		default:
			/* we don't need to check replication state in those states */
			break;
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
	if (!keeper_init_state_create(keeper))
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
	if (!keeper_init_state_create(keeper))
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
 * keeper_init_state_write create our pg_autoctl.init file.
 *
 * This file is created when entering keeper init and deleted only when the
 * init has been successful. This allows the code to take smarter decisions and
 * decipher in between a previous init having failed halfway through or
 * initializing from scratch in conditions not supported (pre-existing and
 * running cluster, etc).
 */
bool
keeper_init_state_create(Keeper *keeper)
{
	KeeperStateInit *initState = &(keeper->initState);

	initState->initStage = INIT_STAGE_1;

	if (!keeper_init_state_discover(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Writing keeper init state file at \"%s\"",
			 keeper->config.pathnames.init);
	log_debug("keeper_init_state_create: version = %d",
			  initState->pg_autoctl_state_version);
	log_debug("keeper_init_state_create: pgInitState = %s",
			  PreInitPostgreInstanceStateToString(initState->pgInitState));

	return keeper_init_state_write(keeper);
}


/*
 * keeper_init_state_update updates our pg_autoctl init file to the given
 * stage.
 */
bool
keeper_init_state_update(Keeper *keeper, InitStage initStage)
{
	char tempFileName[MAXPGPATH] = { 0 };
	char *filename = keeper->config.pathnames.init;
	InitStage previousStage = keeper->initState.initStage;

	if (keeper->initState.initStage == initStage)
	{
		log_debug("keeper_init_state_update: keeper is already at stage %d",
				  keeper->initState.initStage);
		return true;
	}

	keeper->initState.initStage = initStage;

	log_info("Updating keeper init state file to stage %d at \"%s\"",
			 keeper->initState.initStage, keeper->config.pathnames.init);

	/* backup our current init file into init.1 (for stage 1) */
	sformat(tempFileName, MAXPGPATH, "%s.%d", filename, previousStage);

	if (rename(filename, tempFileName) != 0)
	{
		log_fatal("Failed to rename \"%s\" to \"%s\": %m",
				  filename, tempFileName);
		return false;
	}

	if (!keeper_init_state_write(keeper))
	{
		return false;
	}

	/* we don't need the previous stage init file around anymore */
	return unlink_file(tempFileName);
}


/*
 * keeper_init_state_write writes our pg_autoctl.init file.
 */
static bool
keeper_init_state_write(Keeper *keeper)
{
	int fd;
	char buffer[PG_AUTOCTL_KEEPER_STATE_FILE_SIZE] = { 0 };
	KeeperStateInit *initState = &(keeper->initState);

	memset(buffer, 0, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE);

	/*
	 * Explanation of IGNORE-BANNED:
	 * memcpy is safe to use here.
	 * we have a static assert that sizeof(KeeperStateInit) is always
	 * less than the buffer length PG_AUTOCTL_KEEPER_STATE_FILE_SIZE.
	 * also KeeperStateData is a plain struct that does not contain
	 * any pointers in it. Necessary comment about not using pointers
	 * is added to the struct definition.
	 */
	memcpy(buffer, initState, sizeof(KeeperStateInit)); /* IGNORE-BANNED */

	fd = open(keeper->config.pathnames.init,
			  O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		log_fatal("Failed to create keeper init state file \"%s\": %m",
				  keeper->config.pathnames.init);
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
		log_fatal("Failed to write keeper state file \"%s\": %m",
				  keeper->config.pathnames.init);
		return false;
	}

	if (fsync(fd) != 0)
	{
		log_fatal("fsync error: %m");
		return false;
	}

	close(fd);

	return true;
}


/*
 * keeper_init_state_discover discovers the current KeeperStateInit from the
 * command line options, by checking everything we can about the possibly
 * existing Postgres instance.
 */
bool
keeper_init_state_discover(Keeper *keeper)
{
	KeeperStateInit *initState = &(keeper->initState);
	PostgresSetup pgSetup = { 0 };
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;

	initState->pg_autoctl_state_version = PG_AUTOCTL_STATE_VERSION;

	if (!pg_setup_init(&pgSetup, &(keeper->config.pgSetup),
					   missingPgdataIsOk, pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialise the keeper init state, "
				  "see above for details");
		return false;
	}

	if (pg_setup_is_running(&pgSetup) && pg_setup_is_primary(&pgSetup))
	{
		initState->pgInitState = PRE_INIT_STATE_PRIMARY;
	}
	else if (pg_setup_is_running(&pgSetup))
	{
		initState->pgInitState = PRE_INIT_STATE_RUNNING;
	}
	else if (pg_setup_pgdata_exists(&pgSetup))
	{
		initState->pgInitState = PRE_INIT_STATE_EXISTS;
	}
	else
	{
		initState->pgInitState = PRE_INIT_STATE_EMPTY;
	}

	return true;
}


/*
 * keeper_init_state_read reads the information kept in the keeper init file.
 */
bool
keeper_init_state_read(Keeper *keeper)
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

	if (fileSize >= sizeof(KeeperStateInit)
		&& pg_autoctl_state_version == PG_AUTOCTL_STATE_VERSION)
	{
		keeper->initState = *(KeeperStateInit*) content;
		free(content);
		return true;
	}

	free(content);

	/* Looks like it's a mess. */
	log_error("Keeper init state file \"%s\" exists but "
			  "is broken or wrong version (%d)",
			  filename, pg_autoctl_state_version);
	return false;
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
