/*
 * src/bin/pg_autoctl/keeper_init.c
 *     Keeper initialisation.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdbool.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
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

static KeeperStateInit initState = { 0 };

static bool keeper_pg_init_fsm(Keeper *keeper, KeeperConfig *config);
static bool keeper_pg_init_and_register(Keeper *keeper, KeeperConfig *config);
static bool reach_initial_state(Keeper *keeper);
static bool wait_until_primary_is_ready(Keeper *config,
										MonitorAssignedState *assignedState);
static bool keeper_pg_init_node_active(Keeper *keeper);


/*
 * keeper_pg_init initializes a pg_autoctl keeper and its local PostgreSQL.
 *
 * Depending on whether we have a monitor or not in the config (see
 * --without-monitor), then we call into keeper_pg_init_and_register or
 * keeper_pg_init_fsm.
 */
bool
keeper_pg_init(Keeper *keeper, KeeperConfig *config)
{
	log_trace("keeper_pg_init: monitor is %s",
			  config->monitorDisabled ? "disabled" : "enabled" );

	if (config->monitorDisabled)
	{
		return keeper_pg_init_fsm(keeper, config);
	}
	else
	{
		return keeper_pg_init_and_register(keeper, config);
	}
}


/*
 * keeper_pg_init_fsm initializes the keeper's local FSM and does nothing more.
 * It's only intended to be used when we are not using a monitor, which means
 * we're going to expose our FSM driving as an HTTP API, and sit there waiting
 * for orders from another software.
 */
static bool
keeper_pg_init_fsm(Keeper *keeper, KeeperConfig *config)
{
	return keeper_init_fsm(keeper, config);
}


static bool
keeper_ensure_pg_configuration_files_in_pgdata(KeeperConfig *config)
{
	/* check existence of postgresql.conf, pg_hba.conf, pg_ident.conf in pgdata dir */

	char *pgdata = config->pgSetup.pgdata;
	char hbaConfPath[MAXPGPATH];
	char posgresqlConfPath[MAXPGPATH];
	char versionFilePath[MAXPGPATH];
	char *fileContents;
	char versionString[MAXPGPATH];
	long fileSize;
	char installationPath[MAXPGPATH];
	bool installationPathPresent = false;
	bool postgresqlConfExists = false;
	char installationPosgresqlConfPath[MAXPGPATH];
	char identConfPath[MAXPGPATH];


	/* read version file */


	join_path_components(versionFilePath, pgdata, "PG_VERSION");

	if (!read_file(versionFilePath, &fileContents, &fileSize))
	{
		log_error("Unable to read PG_VERSION from %s", versionFilePath);
		return false;
	}

	if (fileSize > 0)
	{
		int scanResult = sscanf(fileContents, "%s", versionString);
		if (scanResult == 0)
		{
			log_error("Unsupported PG_VERSION content");
			return false;

		}

		log_info("Version = %s", versionString);
	}

	free(fileContents);

	join_path_components(installationPath, "/etc/postgresql", versionString);
	join_path_components(installationPath, installationPath, "main");

	log_warn("installation path : %s", installationPath);


	/* check if postgresql.conf exists in pgdata. there is no separate check for pg_hba and pg_ident  */
	join_path_components(posgresqlConfPath, pgdata, "postgresql.conf");

	postgresqlConfExists = file_exists(posgresqlConfPath);

	if (postgresqlConfExists)
	{
		return true;
	}

	/* check if pgdata is in default path. */
	if (strstr(pgdata, "/var/lib/postgresql") != pgdata)
	{
		log_error("Cannot determine configuration file location for this instance");
		return false;
	}


	/* true if we are working on debian/ubuntu installation */
	installationPathPresent = directory_exists(installationPath);
	if (!installationPathPresent)
	{
		log_error("Cannot find configuration file directory");
		return false;
	}


	{
		char *postgresConfRelativePath = strstr(posgresqlConfPath, "postgresql");

		join_path_components(installationPosgresqlConfPath, "/etc/", postgresConfRelativePath);

		if (file_exists(installationPosgresqlConfPath))
		{
			if (read_file(installationPosgresqlConfPath, &fileContents, &fileSize))
			{
				int charCount = 0;
				write_file(fileContents, fileSize, posgresqlConfPath);
				charCount += sprintf(fileContents, "# DO NOT EDIT\n# Added by pg_autofailoover \n");
				charCount += sprintf(fileContents, "hba_file = 'ConfigDir/pg_hba.conf'	# host-based authentication file added by pg_auto_failover\n");
				charCount += sprintf(fileContents, "ident_file = 'ConfigDir/pg_ident.conf'	# ident configuration file file added by pg_auto_failover\n");
				append_to_file(fileContents, charCount, posgresqlConfPath);

				free(fileContents);
			}
			else
			{
				log_error("error reading %s", installationPosgresqlConfPath);
				return false;
			}
		}
		else
		{
			log_error("could not file postgresql.conf at default path : %s", installationPosgresqlConfPath);
			return false;
		}
	}


	join_path_components(hbaConfPath, pgdata, "pg_hba.conf");

	if(!file_exists(hbaConfPath))
	{
		char installationHbaConfPath[MAXPGPATH];

		log_warn("hba file does not exist : %s, need to copy", hbaConfPath);
		join_path_components(installationHbaConfPath, installationPath, "pg_hba.conf");

		if (file_exists(installationHbaConfPath))
		{
			if (read_file(installationHbaConfPath, &fileContents, &fileSize))
			{
				write_file(fileContents, fileSize, hbaConfPath);
				free(fileContents);
			}
			else
			{
				log_error("error reading %s", installationHbaConfPath);
				return false;
			}
		}
		else
		{
			log_error("could not file hba file at default path : %s",installationHbaConfPath);
			return false;
		}
	}


	join_path_components(identConfPath, pgdata, "pg_ident.conf");

	if(!file_exists(identConfPath))
	{
		char installationIdentConfPath[MAXPGPATH];

		log_warn("pg_ident.conf does not exist : %s, need to copy", identConfPath);
		join_path_components(installationIdentConfPath, installationPath, "pg_ident.conf");

		if (file_exists(installationIdentConfPath))
		{
			if (read_file(installationIdentConfPath, &fileContents, &fileSize))
			{
				write_file(fileContents, fileSize, hbaConfPath);
				free(fileContents);
			}
			else
			{
				log_error("error reading %s", identConfPath);
				return false;
			}
		}
		else
		{
			log_error("could not file pg_ident.conf at default path : %s",installationIdentConfPath);
			return false;
		}
	}


	return true;
}


/*
 * keeper_pg_init_and_register initialises a pg_autoctl keeper and its local
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
static bool
keeper_pg_init_and_register(Keeper *keeper, KeeperConfig *config)
{
	/*
	 * The initial state we may register in depend on the current PostgreSQL
	 * instance that might exist or not at PGDATA.
	 */
	PostgresSetup pgSetup = config->pgSetup;
	bool postgresInstanceExists = pg_setup_pgdata_exists(&pgSetup);
	bool postgresInstanceIsPrimary = pg_setup_is_primary(&pgSetup);

	if (postgresInstanceExists)
	{
		if (!keeper_ensure_pg_configuration_files_in_pgdata(config))
		{
			log_fatal("Existing postgresql instance is not supported");
			return false;
		}
	}

	/*
	 * If we don't have a state file, we consider that we're initializing from
	 * scratch and can move on, nothing to do here.
	 */
	if (file_exists(config->pathnames.init))
	{
		return keeper_pg_init_continue(keeper, config);
	}

	if (file_exists(config->pathnames.state))
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
					  "there's no init in progress", config->pathnames.state);
			log_info("HINT: use `pg_autoctl run` to start the service.");
		}
		return createAndRun;
	}

	if (postgresInstanceExists
		&& postgresInstanceIsPrimary
		&& !allowRemovingPgdata)
	{
		char absolutePgdata[PATH_MAX];

		log_warn("A postgres directory already exists at \"%s\", registering "
				 "as a single node",
				 realpath(pgSetup.pgdata, absolutePgdata));

		/*
		 * The local Postgres instance exists and we are not allowed to remove
		 * it.
		 *
		 * If we're able to register as a single postgres server, that's great.
		 *
		 * If there already is a single postgres server, then we become the
		 * wait_standby and we would have to remove our own database directory,
		 * which the user didn't give us permission for. In that case, we
		 * revert the situation by removing ourselves from the monitor and
		 * removing the state file.
		 */
		if (!keeper_register_and_init(keeper, config, INIT_STATE))
		{
			/* monitor_register_node logs relevant errors */
			return false;
		}

		if (keeper->state.assigned_role != SINGLE_STATE)
		{
			bool ignore_monitor_errors = false;

			log_error("There is already another postgres node, so the monitor "
					  "wants us to be in state %s. However, that would involve "
					  "removing the database directory.",
					  NodeStateToString(keeper->state.assigned_role));

			log_warn("Removing the node from the monitor");

			if (!keeper_remove(keeper, config, ignore_monitor_errors))
			{
				log_fatal("Failed to remove the node from the monitor, run "
						  "`pg_autoctl drop node` to manually remove "
						  "the node from the monitor");
				return false;
			}

			log_warn("HINT: Re-run with --allow-removing-pgdata to allow "
					 "pg_autoctl to remove \"%s\" and join as %s\n\n",
					 absolutePgdata,
					 NodeStateToString(keeper->state.assigned_role));

			return false;
		}

		log_info("Successfully registered as \"%s\" to the monitor.",
				 NodeStateToString(keeper->state.assigned_role));

		return reach_initial_state(keeper);
	}
	else if (postgresInstanceExists && postgresInstanceIsPrimary)
	{
		/*
		 * The local Postgres instance exists, but we are allowed to remove it
		 * in case the monitor assigns the state wait_standby to us. Therefore,
		 * we register in INIT_STATE and let the monitor decide.
		 */

		if (!keeper_register_and_init(keeper, config, INIT_STATE))
		{
			/* monitor_register_node logs relevant errors */
			return false;
		}

		log_info("Successfully registered as \"%s\" to the monitor.",
				 NodeStateToString(keeper->state.assigned_role));

		return reach_initial_state(keeper);
	}
	else if (postgresInstanceExists && !postgresInstanceIsPrimary)
	{
		log_error("pg_autoctl doesn't know how to register an already "
				  "existing standby server at the moment");
		return false;
	}
	else if (!postgresInstanceExists)
	{
		/*
		 * The local Postgres instance does not exist. We have two possible
		 * choices here, either we're the only one in our group, or we are
		 * joining a group that already exists.
		 *
		 * The situation is decided by the Monitor, which implements
		 * transaction semantics and safe concurrency approach, needed here in
		 * case other keeper are concurrently registering other nodes.
		 *
		 * So our strategy is to ask the monitor to pick a state for us and
		 * then implement whatever was decided.
		 */
		if (!keeper_register_and_init(keeper, config, INIT_STATE))
		{
			log_error("Failed to register the existing local Postgres node "
					  "\"%s:%d\" running at \"%s\""
					  "to the pg_auto_failover monitor at %s, "
					  "see above for details",
					  config->nodename, config->pgSetup.pgport,
					  config->pgSetup.pgdata, config->monitor_pguri);
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
keeper_pg_init_continue(Keeper *keeper, KeeperConfig *config)
{
	if (!keeper_init(keeper, config))
	{
		/* errors have already been logged */
		return false;
	}

	if (!keeper_init_state_read(keeper, &initState))
	{
		log_fatal("Failed to restart from previous keeper init attempt");
		log_info("HINT: use `pg_autoctl drop node` to retry in a clean state");
		return false;
	}

	log_info("Continuing from a previous `pg_autoctl create` failed attempt");
	log_info("PostgreSQL state at registration time was: %s",
			 PreInitPostgreInstanceStateToString(initState.pgInitState));

	/*
	 * TODO: verify the information in the state file against the information
	 * in the monitor and decide if it's stale or not.
	 */

	/*
	 * If we have an init file and the state file looks good, then the
	 * operation that failed was removing the init state file.
	 */
	if (keeper->state.current_role == keeper->state.assigned_role
		&& (keeper->state.current_role == SINGLE_STATE
			|| keeper->state.current_role == CATCHINGUP_STATE))
	{
		return unlink_file(config->pathnames.init);
	}

	return reach_initial_state(keeper);
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
	KeeperConfig config = keeper->config;
	bool pgInstanceIsOurs = false;

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
			 * the primary has update its HBA setup with our nodename.
			 */
			MonitorAssignedState assignedState = { 0 };

			/* busy loop until we are asked to be in CATCHINGUP_STATE */
			if (!wait_until_primary_is_ready(keeper, &assignedState))
			{
				/* errors have already been logged */
				return false;
			}

			/*
			 * Now that we are asked to catch up, it means the primary is ready
			 * for us to pg_basebackup, which allows the local instance to then
			 * reach goal state SECONDARY:
			 */
			if (!keeper_fsm_reach_assigned_state(keeper))
			{
				/* errors have already been logged */
				return false;
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
	return unlink_file(config.pathnames.init);
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
	char currrentLSN[PG_LSN_MAXLENGTH] = "0/0";
	char *pgsrSyncState = "";
	int errors = 0, tries = 0;
	bool firstLoop = true;

	/* wait until the primary is ready for us to pg_basebackup */
	do {
		if (firstLoop)
		{
			firstLoop = false;
		}
		else
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}

		if (!monitor_node_active(&(keeper->monitor),
								 keeper->config.formation,
								 keeper->config.nodename,
								 keeper->config.pgSetup.pgport,
								 keeper->state.current_node_id,
								 keeper->state.current_group,
								 keeper->state.current_role,
								 pgIsRunning,
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
		++tries;

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

	char *pg_regress_sock_dir = getenv("PG_REGRESS_SOCK_DIR");

	char hbaFilePath[MAXPGPATH];

	log_trace("create_database_and_extension");

	/* we didn't start PostgreSQL yet, also we just ran initdb */
	snprintf(hbaFilePath, MAXPGPATH, "%s/pg_hba.conf", pgSetup->pgdata);

	/*
	 * The Postgres URI given to the user by our facility is going to use
	 * --dbname and --nodename, as per the following command:
	 *
	 *   $ pg_autoctl show uri --formation default
	 *
	 * We need to make it so that the user can actually use that connection
	 * string with at least the --username used to create the database.
	 */
	if (!pghba_ensure_host_rule_exists(hbaFilePath,
									   HBA_DATABASE_DBNAME,
									   pgSetup->dbname,
									   pg_setup_get_username(pgSetup),
									   config->nodename,
									   "trust"))
	{
		log_error("Failed to edit \"%s\" to grant connections to \"%s\", "
				  "see above for details", hbaFilePath, config->nodename);
		return false;
	}

	/*
	 * In test environments using PG_REGRESS_SOCK_DIR="" to disable unix socket
	 * directory, we have to connect to the address from pghost.
	 */
	if (pg_regress_sock_dir != NULL
		&& strcmp(pg_regress_sock_dir, "") == 0)
	{
		log_info("Granting connection from \"%s\" in \"%s\"",
				 pgSetup->pghost, hbaFilePath);

		if (!pghba_ensure_host_rule_exists(hbaFilePath,
										   HBA_DATABASE_ALL, NULL, NULL,
										   pgSetup->pghost, "trust"))
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
	 * Now start the database, we need to create our dbname and maybe the Citus
	 * Extension too.
	 */
	if (!ensure_local_postgres_is_running(&initPostgres))
	{
		log_error("Failed to start PostgreSQL, see above for details");
		return false;
	}

	/*
	 * If username was set in the setup and doesn't exist we need to create it.
	 */
	if (!IS_EMPTY_STRING_BUFFER(pgSetup->username))
	{
		if (!pgsql_create_user(&initPostgres.sqlClient, pgSetup->username,
							   /* password, login, superuser, replication */
							   NULL, true, true, false))

		{
			log_fatal("Failed to create role \"%s\""
					  ", see above for details", pgSetup->username);

			return false;
		}
	}

	/*
	 * Add pg_autoctl PostgreSQL settings, including Citus extension in
	 * shared_preload_libraries when dealing with a Citus worker or coordinator
	 * node.
	 */
	if (!postgres_add_default_settings(&initPostgres))
	{
		log_error("Failed to add default settings to newly initialized "
				  "PostgreSQL instance, see above for details");
		return false;
	}

	/*
	 * Now allow nodes on the same network to connect to the coordinator, and
	 * the coordinator to connect to its workers.
	 */
	if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
	{
		(void) pghba_enable_lan_cidr(&initPostgres.sqlClient,
									 HBA_DATABASE_DBNAME,
									 pgSetup->dbname,
									 config->nodename,
									 pg_setup_get_username(pgSetup),
									 "trust",
									 NULL);
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
	 * Because we did create the PostgreSQL cluster in this function, we feel
	 * free to restart it to make sure that the defaults we just installed are
	 * actually in place.
	 */

	if (!keeper_restart_postgres(keeper))
	{
		log_fatal("Failed to restart PostgreSQL to enable pg_auto_failover "
				  "configuration");
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
		 * Install the citus extension in that database, skipping if the
		 * extension has already been installed.
		 */
		log_info("CREATE EXTENSION %s;", CITUS_EXTENSION_NAME);

		/*
		 * Connect to pgsql as the system user to create extension: Same
		 * user as initdb with superuser privileges.
		 */

		if (!pgsql_create_extension(&(postgres->sqlClient), CITUS_EXTENSION_NAME))
		{
			log_error("Failed to create extension %s", CITUS_EXTENSION_NAME);
			return false;
		}

		/* and we're done with this connection. */
		pgsql_finish(pgsql);
	}

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

	keeper_update_pg_state(keeper);

	if (!monitor_node_active(&(keeper->monitor),
							 keeper->config.formation,
							 keeper->config.nodename,
							 keeper->config.pgSetup.pgport,
							 keeper->state.current_node_id,
							 keeper->state.current_group,
							 keeper->state.current_role,
							 ReportPgIsRunning(keeper),
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
