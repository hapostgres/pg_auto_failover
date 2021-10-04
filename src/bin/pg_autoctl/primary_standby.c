/*
 * src/bin/pg_autoctl/primary_standby.c
 *     API to manage a local postgres database cluster
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "config.h"
#include "file_utils.h"
#include "keeper.h"
#include "log.h"
#include "parsing.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsql.h"
#include "primary_standby.h"
#include "signals.h"
#include "state.h"


static bool local_postgres_wait_until_ready(LocalPostgresServer *postgres);

static void local_postgres_update_pg_failures_tracking(LocalPostgresServer *postgres,
													   bool pgIsRunning);

/*
 * Default settings for postgres databases managed by pg_auto_failover.
 * These settings primarily ensure that streaming replication is
 * possible and synchronous replication is the default.
 *
 * listen_addresses and port are placeholder values in this array and are
 * replaced with dynamic values from the setup when used.
 */
#define DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER \
	{ "shared_preload_libraries", "pg_stat_statements" }, \
	{ "listen_addresses", "'*'" }, \
	{ "port", "5432" }, \
	{ "max_wal_senders", "12" }, \
	{ "max_replication_slots", "12" }, \
	{ "wal_level", "'replica'" }, \
	{ "wal_log_hints", "on" }, \
	{ "wal_sender_timeout", "'30s'" }, \
	{ "hot_standby_feedback", "on" }, \
	{ "hot_standby", "on" }, \
	{ "synchronous_commit", "on" }, \
	{ "logging_collector", "on" }, \
	{ "log_destination", "stderr" }, \
	{ "log_directory", "log" }, \
	{ "log_min_messages", "info" }, \
	{ "log_connections", "off" }, \
	{ "log_disconnections", "off" }, \
	{ "log_lock_waits", "on" }, \
	{ "password_encryption", "md5" }, \
	{ "ssl", "off" }, \
	{ "ssl_ca_file", "" }, \
	{ "ssl_crl_file", "" }, \
	{ "ssl_cert_file", "" }, \
	{ "ssl_key_file", "" }, \
	{ "ssl_ciphers", "'" DEFAULT_SSL_CIPHERS "'" }

#define DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_PRE_13 \
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER, \
	{ "wal_keep_segments", "512" }

#define DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_13 \
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER, \
	{ "wal_keep_size", "'8 GB'" }

GUC postgres_default_settings_pre_13[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_PRE_13,
	{ NULL, NULL }
};

GUC postgres_default_settings_13[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_13,
	{ NULL, NULL }
};

GUC citus_default_settings_pre_13[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_PRE_13,
	{ "shared_preload_libraries", "'citus,pg_stat_statements'" },
	{ "citus.node_conninfo", "'sslmode=prefer'" },
	{ "citus.cluster_name", "'default'" },
	{ "citus.use_secondary_nodes", "'never'" },
	{ "citus.local_hostname", "'localhost'" },
	{ NULL, NULL }
};

GUC citus_default_settings_13[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER_13,
	{ "shared_preload_libraries", "'citus,pg_stat_statements'" },
	{ "citus.node_conninfo", "'sslmode=prefer'" },
	{ "citus.cluster_name", "'default'" },
	{ "citus.use_secondary_nodes", "'never'" },
	{ "citus.local_hostname", "'localhost'" },
	{ NULL, NULL }
};


/*
 * local_postgres_init initializes an interface for managing a local
 * postgres server with the given setup.
 */
void
local_postgres_init(LocalPostgresServer *postgres, PostgresSetup *pgSetup)
{
	char connInfo[MAXCONNINFO];

	pg_setup_get_local_connection_string(pgSetup, connInfo);
	pgsql_init(&postgres->sqlClient, connInfo, PGSQL_CONN_LOCAL);

	postgres->postgresSetup = *pgSetup;

	/* reset PostgreSQL restart failures tracking */
	postgres->pgFirstStartFailureTs = 0;
	postgres->pgStartRetries = 0;

	/* set the local instance kind from the configuration. */
	postgres->pgKind = pgSetup->pgKind;

	if (!local_postgres_set_status_path(postgres, true))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}
}


/*
 * local_postgres_set_status_path sets the file pathname to the pg_autoctl.pg
 * file that we use to signal the Postgres controller if Postgres is expected
 * to be running or not.
 *
 * When the file does not exist, the controller do nothing, so it's safe to
 * always remove the file at startup.
 */
bool
local_postgres_set_status_path(LocalPostgresServer *postgres, bool unlink)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	/* normalize our PGDATA path when it exists on-disk already */
	if (directory_exists(pgSetup->pgdata))
	{
		/* normalize the existing path to PGDATA */
		if (!normalize_filename(pgSetup->pgdata, pgSetup->pgdata, MAXPGPATH))
		{
			/* errors have already been logged */
			return false;
		}
	}

	log_trace("local_postgres_set_status_path: %s", pgSetup->pgdata);

	/* initialize our Postgres state file path */
	if (!build_xdg_path(pgStatus->pgStatusPath,
						XDG_RUNTIME,
						pgSetup->pgdata,
						KEEPER_POSTGRES_STATE_FILENAME))
	{
		/* highly unexpected */
		log_error("Failed to build pg_autoctl postgres state file pathname, "
				  "see above for details.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_trace("local_postgres_set_status_path: %s", pgStatus->pgStatusPath);

	/* local_postgres_init removes any stale pg_autoctl.pg file */
	if (unlink && !local_postgres_unlink_status_file(postgres))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * local_postgres_unlink_status_file unlinks the file we use to communicate
 * with the Postgres controller, so that this process won't interfere with
 * whatever the user is doing durning maintenance (such as stop Postgres).
 */
bool
local_postgres_unlink_status_file(LocalPostgresServer *postgres)
{
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	log_trace("local_postgres_unlink_status_file: %s", pgStatus->pgStatusPath);

	return unlink_file(pgStatus->pgStatusPath);
}


/*
 * local_postgres_UpdatePgFailuresTracking updates our tracking of PostgreSQL
 * restart failures.
 */
static void
local_postgres_update_pg_failures_tracking(LocalPostgresServer *postgres,
										   bool pgIsRunning)
{
	if (pgIsRunning)
	{
		/* reset PostgreSQL restart failures tracking */
		postgres->pgFirstStartFailureTs = 0;
		postgres->pgStartRetries = 0;
		postgres->pgIsRunning = true;
	}
	else
	{
		uint64_t now = time(NULL);

		/* update PostgreSQL restart failure tracking */
		if (postgres->pgFirstStartFailureTs == 0)
		{
			postgres->pgFirstStartFailureTs = now;
		}
		++postgres->pgStartRetries;
	}
}


/*
 * local_postgres_finish closes our connection to the local PostgreSQL
 * server, if needs be.
 */
void
local_postgres_finish(LocalPostgresServer *postgres)
{
	pgsql_finish(&postgres->sqlClient);
}


/*
 * local_postgres_update updates the LocalPostgresServer pgSetup information
 * with what we discover from the newly created Postgres instance. Typically
 * used just after a pg_basebackup.
 */
bool
local_postgres_update(LocalPostgresServer *postgres, bool postgresNotRunningIsOk)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	PostgresSetup newPgSetup = { 0 };
	bool missingPgdataIsOk = true;

	/* in case a connection is still established, now is time to close */
	(void) local_postgres_finish(postgres);

	if (!pg_setup_init(&newPgSetup, pgSetup,
					   missingPgdataIsOk,
					   postgresNotRunningIsOk))
	{
		/* errors have already been logged */
		return false;
	}

	(void) local_postgres_init(postgres, &newPgSetup);

	return true;
}


/*
 * local_postgres_wait_until_ready waits until Postgres is running and updates
 * our failure tracking counters for the Postgres service accordingly.
 */
static bool
local_postgres_wait_until_ready(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	int timeout = 10;       /* wait for Postgres for 10s */
	bool pgIsRunning = pg_is_running(pgSetup->pg_ctl, pgSetup->pgdata);

	log_trace("local_postgres_wait_until_ready: Postgres %s in \"%s\"",
			  pgIsRunning ? "is running" : "is not running", pgSetup->pgdata);

	if (!pgIsRunning)
	{
		/* main logging is done in the Postgres controller sub-process */
		pgIsRunning = pg_setup_wait_until_is_ready(pgSetup, timeout, LOG_DEBUG);

		/* update connection string for connection to postgres */
		(void)
		local_postgres_update_pg_failures_tracking(postgres, pgIsRunning);

		if (pgIsRunning)
		{
			/* update pgSetup cache with new Postgres pid and all */
			local_postgres_init(postgres, pgSetup);

			log_debug("local_postgres_wait_until_ready: Postgres is running "
					  "with pid %d", pgSetup->pidFile.pid);
		}
		else
		{
			log_error("Failed to ensure that Postgres is running in \"%s\"",
					  pgSetup->pgdata);
		}
	}

	return pgIsRunning;
}


/*
 * ensure_postgres_service_is_running signals the Postgres controller service
 * that Postgres is expected to be running, by updating the expectedPgStatus
 * file to the proper values, and then wait until Postgres is running before
 * returning true in case of success.
 */
bool
ensure_postgres_service_is_running(LocalPostgresServer *postgres)
{
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	/* update our data structure in-memory, then on-disk */
	if (!keeper_set_postgres_state_running(&(pgStatus->state),
										   pgStatus->pgStatusPath))
	{
		/* errors have already been logged */
		return false;
	}

	return local_postgres_wait_until_ready(postgres);
}


/*
 * ensure_postgres_service_is_running_as_subprocess signals the Postgres
 * controller service that Postgres is expected to be running as a subprocess
 * of pg_autoctl, by updating the expectedPgStatus file to the proper values,
 * and then wait until Postgres is running before returning true in case of
 * success.
 */
bool
ensure_postgres_service_is_running_as_subprocess(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	bool pgIsRunning = pg_is_running(pgSetup->pg_ctl, pgSetup->pgdata);

	/* update our data structure in-memory, then on-disk */
	if (!keeper_set_postgres_state_running_as_subprocess(&(pgStatus->state),
														 pgStatus->pgStatusPath))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * If Postgres was already running before we wrote a new expected status
	 * file, then the Postgres controller might be up to stop and then restart
	 * Postgres. This happens when the already running Postgres is not a
	 * subprocess of this pg_autoctl process, and only the controller has the
	 * right information to check that (child process pid for "postgres").
	 *
	 * Because we are lacking information, we just wait for some time before
	 * checking if Postgres is running (again)
	 */
	if (pgIsRunning)
	{
		sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
	}

	return local_postgres_wait_until_ready(postgres);
}


/*
 * ensure_postgres_service_is_running signals the Postgres controller service
 * that Postgres is expected to not be running, by updating the
 * expectedPgStatus file to the proper values, and then wait until Postgres is
 * stopped before returning true in case of success.
 */
bool
ensure_postgres_service_is_stopped(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	int timeout = 10;       /* wait for Postgres for 10s */

	log_trace("keeper_ensure_postgres_is_stopped");

	/* update our data structure in-memory, then on-disk */
	if (!keeper_set_postgres_state_stopped(&(pgStatus->state),
										   pgStatus->pgStatusPath))
	{
		/* errors have already been logged */
		return false;
	}

	return pg_setup_wait_until_is_stopped(pgSetup, timeout, LOG_DEBUG);
}


/*
 * primary_has_replica returns whether the local postgres server has a
 * replica that is connecting using the given user name.
 */
bool
primary_has_replica(LocalPostgresServer *postgres, char *userName, bool *hasStandby)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_has_replica");

	bool result = pgsql_has_replica(pgsql, userName, hasStandby);

	pgsql_finish(pgsql);
	return result;
}


/*
 * upstream_has_replication_slot checks whether the upstream server already has
 * created our replication slot.
 */
bool
upstream_has_replication_slot(ReplicationSource *upstream,
							  PostgresSetup *pgSetup,
							  bool *hasReplicationSlot)
{
	NodeAddress *primaryNode = &(upstream->primaryNode);

	PostgresSetup upstreamSetup = { 0 };
	PGSQL upstreamClient = { 0 };
	char connectionString[MAXCONNINFO] = { 0 };

	/* prepare a PostgresSetup that allows preparing a connection string */
	strlcpy(upstreamSetup.username, PG_AUTOCTL_REPLICA_USERNAME, NAMEDATALEN);
	strlcpy(upstreamSetup.dbname, pgSetup->dbname, NAMEDATALEN);
	strlcpy(upstreamSetup.pghost, primaryNode->host, _POSIX_HOST_NAME_MAX);
	upstreamSetup.pgport = primaryNode->port;
	upstreamSetup.ssl = pgSetup->ssl;

	/*
	 * Build the connection string as if to a local node, but we tweaked the
	 * pgsetup to target the primary node by changing its pghost and pgport.
	 */
	pg_setup_get_local_connection_string(&upstreamSetup, connectionString);

	if (!pgsql_init(&upstreamClient, connectionString, PGSQL_CONN_UPSTREAM))
	{
		/* errors have already been logged */
		return false;
	}

	if (!pgsql_replication_slot_exists(&upstreamClient,
									   upstream->slotName,
									   hasReplicationSlot))
	{
		/* errors have already been logged */
		PQfinish(upstreamClient.connection);
		return false;
	}

	PQfinish(upstreamClient.connection);
	return true;
}


/*
 * primary_create_replication_slot (re)creates a replication slot. The
 * replication slot will not have its LSN initialized until first use. The
 * return value indicates whether the operation was successful.
 */
bool
primary_create_replication_slot(LocalPostgresServer *postgres,
								char *replicationSlotName)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_create_replication_slot(%s)", replicationSlotName);

	bool result = pgsql_create_replication_slot(pgsql, replicationSlotName);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_drop_replication_slot drops a replication slot if it exists. The
 * return value indicates whether the operation was successful.
 */
bool
primary_drop_replication_slot(LocalPostgresServer *postgres,
							  char *replicationSlotName)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_drop_replication_slot");

	bool result = pgsql_drop_replication_slot(pgsql, replicationSlotName);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_drop_all_replication_slots drops all the replication slots found on
 * a node.
 *
 * When a node has been demoted, the replication slots that used to be
 * maintained by the streaming replication protocol are now going to be
 * maintained "manually" by pg_autoctl using pg_replication_slot_advance().
 *
 * There is a problem in pg_replication_slot_advance() in that it only
 * maintains the restart_lsn property of a replication slot, it does not
 * maintain the xmin of it. When re-using the pre-existing replication slots,
 * we want to have a NULL xmin, so we drop the slots, and then create them
 * again.
 */
bool
primary_drop_all_replication_slots(LocalPostgresServer *postgres)
{
	NodeAddressArray otherNodesArray = { 0 };

	log_info("Dropping replication slots (to reset their xmin)");

	if (!postgres_replication_slot_create_and_drop(postgres, &otherNodesArray))
	{
		log_error("Failed to drop replication slots on the local Postgres "
				  "instance, see above for details");
		return false;
	}

	return true;
}


/*
 * postgres_replication_slot_create_and_drop drops the replication slots that
 * belong to dropped nodes on a primary server, and creates replication slots
 * for newly created nodes on the monitor.
 */
bool
postgres_replication_slot_create_and_drop(LocalPostgresServer *postgres,
										  NodeAddressArray *nodeArray)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("postgres_replication_slot_drop_removed");

	bool result = pgsql_replication_slot_create_and_drop(pgsql, nodeArray);

	pgsql_finish(pgsql);
	return result;
}


/*
 * postgres_replication_slot_advance advances the current confirmed position of
 * the given replication slot up to the given LSN position.
 */
bool
postgres_replication_slot_maintain(LocalPostgresServer *postgres,
								   NodeAddressArray *nodeArray)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("postgres_replication_slot_maintain");

	bool result = pgsql_replication_slot_maintain(pgsql, nodeArray);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_enable_synchronous_replication enables synchronous replication
 * on a primary postgres node.
 */
bool
primary_set_synchronous_standby_names(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_info("Setting synchronous_standby_names to '%s'",
			 postgres->synchronousStandbyNames);

	bool result =
		pgsql_set_synchronous_standby_names(pgsql,
											postgres->synchronousStandbyNames);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_disable_synchronous_replication disables synchronous replication
 * on a primary postgres node.
 */
bool
primary_disable_synchronous_replication(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_disable_synchronous_replication");

	bool result = pgsql_disable_synchronous_replication(pgsql);

	pgsql_finish(pgsql);
	return result;
}


/*
 * postgres_add_default_settings ensures that postgresql.conf includes a
 * postgresql-auto-failover.conf file that sets a number of good defaults for
 * settings related to streaming replication and running pg_auto_failover.
 */
bool
postgres_add_default_settings(LocalPostgresServer *postgres,
							  const char *hostname)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	char configFilePath[MAXPGPATH];
	GUC *default_settings = NULL;

	log_trace("postgres_add_default_settings (%s) [%d]",
			  nodeKindToString(postgres->pgKind),
			  pgSetup->control.pg_control_version);

	/* configFilePath = $PGDATA/postgresql.conf */
	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	/* in case of errors, pgsql_ functions finish the connection */
	pgsql_finish(pgsql);

	/*
	 * default settings are different depending on Postgres version and Citus
	 * usage, so fetch the curent pg_control_version and make a decision
	 * depending on that.
	 *
	 * Note that many calls to postgres_add_default_settings happen before we
	 * have had the opportunity to call pg_controldata, so now is a good time
	 * to do that.
	 */
	if (pgSetup->control.pg_control_version == 0)
	{
		bool missingPgdataIsOk = false;

		if (!pg_controldata(pgSetup, missingPgdataIsOk))
		{
			/* errors have already been logged */
			return false;
		}
	}

	if (pgSetup->control.pg_control_version < 1300)
	{
		if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
		{
			default_settings = citus_default_settings_pre_13;
		}
		else
		{
			default_settings = postgres_default_settings_pre_13;
		}
	}
	else
	{
		if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
		{
			default_settings = citus_default_settings_13;
		}
		else
		{
			default_settings = postgres_default_settings_13;
		}
	}

	if (!pg_add_auto_failover_default_settings(pgSetup,
											   hostname,
											   configFilePath,
											   default_settings))
	{
		log_error("Failed to add default settings to postgresql.conf: couldn't "
				  "write the new postgresql.conf, see above for details");
		return false;
	}

	return true;
}


/*
 * primary_create_user_with_hba creates a user and updates pg_hba.conf
 * to allow the user to connect from the given hostname.
 */
bool
primary_create_user_with_hba(LocalPostgresServer *postgres, char *userName,
							 char *password, char *hostname,
							 char *authMethod, HBAEditLevel hbaLevel,
							 int connlimit)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	bool login = true;
	bool superuser = false;
	bool replication = false;
	char hbaFilePath[MAXPGPATH];

	log_trace("primary_create_user_with_hba");

	if (!pgsql_create_user(pgsql, userName, password,
						   login, superuser, replication, connlimit))
	{
		log_error("Failed to create user \"%s\" on local postgres server",
				  userName);
		return false;
	}

	if (!pgsql_get_hba_file_path(pgsql, hbaFilePath, MAXPGPATH))
	{
		log_error("Failed to set the pg_hba rule for user \"%s\": couldn't get "
				  "hba_file from local postgres server", userName);
		return false;
	}

	if (!pghba_ensure_host_rule_exists(hbaFilePath,
									   postgres->postgresSetup.ssl.active,
									   HBA_DATABASE_ALL,
									   NULL,
									   userName,
									   hostname,
									   authMethod,
									   hbaLevel))
	{
		log_error("Failed to set the pg_hba rule for user \"%s\"", userName);
		return false;
	}

	if (!pgsql_reload_conf(pgsql))
	{
		log_error("Failed to reload pg_hba settings after updating pg_hba.conf");
		return false;
	}

	pgsql_finish(pgsql);

	return true;
}


/*
 * primary_create_replication_user creates a user that allows the secondary
 * to connect for replication.
 */
bool
primary_create_replication_user(LocalPostgresServer *postgres,
								char *replicationUsername,
								char *replicationPassword)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	bool login = true;
	bool superuser = true;
	bool replication = true;
	int connlimit = -1;

	log_trace("primary_create_replication_user");

	bool result = pgsql_create_user(pgsql, replicationUsername, replicationPassword,
									login, superuser, replication, connlimit);

	pgsql_finish(pgsql);

	return result;
}


/*
 * standby_init_replication_source initializes a replication source structure
 * with given arguments. If the upstreamNode is NULL, then the
 * replicationSource.primary structure slot is not updated.
 *
 * Note that we just store the pointers to all those const char *arguments
 * here, expect for the upstreamNode there's no copying involved.
 */
bool
standby_init_replication_source(LocalPostgresServer *postgres,
								NodeAddress *upstreamNode,
								const char *username,
								const char *password,
								const char *slotName,
								const char *maximumBackupRate,
								const char *backupDirectory,
								const char *targetLSN,
								SSLOptions sslOptions,
								int currentNodeId)
{
	ReplicationSource *upstream = &(postgres->replicationSource);

	if (upstreamNode != NULL)
	{
		upstream->primaryNode.nodeId = upstreamNode->nodeId;

		strlcpy(upstream->primaryNode.name,
				upstreamNode->name, _POSIX_HOST_NAME_MAX);

		strlcpy(upstream->primaryNode.host,
				upstreamNode->host, _POSIX_HOST_NAME_MAX);

		upstream->primaryNode.port = upstreamNode->port;
	}

	strlcpy(upstream->userName, username, NAMEDATALEN);

	if (password != NULL)
	{
		strlcpy(upstream->password, password, MAXCONNINFO);
	}

	strlcpy(upstream->slotName, slotName, MAXCONNINFO);
	strlcpy(upstream->maximumBackupRate,
			maximumBackupRate,
			MAXIMUM_BACKUP_RATE_LEN);
	strlcpy(upstream->backupDir, backupDirectory, MAXCONNINFO);

	if (targetLSN != NULL)
	{
		strlcpy(upstream->targetLSN, targetLSN, PG_LSN_MAXLENGTH);
	}

	upstream->sslOptions = sslOptions;

	/* prepare our application_name */
	sformat(upstream->applicationName, MAXCONNINFO,
			"%s%d",
			REPLICATION_APPLICATION_NAME_PREFIX,
			currentNodeId);

	return true;
}


/*
 * standby_init_database tries to initialize PostgreSQL as a hot standby. It uses
 * pg_basebackup to do so. Returns false on failure.
 */
bool
standby_init_database(LocalPostgresServer *postgres,
					  const char *hostname,
					  bool skipBaseBackup)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *upstream = &(postgres->replicationSource);

	log_trace("standby_init_database");
	log_info("Initialising PostgreSQL as a hot standby");

	if (pg_setup_pgdata_exists(pgSetup) && pg_setup_is_running(pgSetup))
	{
		log_info("Target directory exists: \"%s\", stopping PostgreSQL",
				 pgSetup->pgdata);

		/* try to stop PostgreSQL, stop here if that fails */
		if (!ensure_postgres_service_is_stopped(postgres))
		{
			log_error("Failed to initialize a standby: "
					  "the database directory exists "
					  "and postgres could not be stopped");
			return false;
		}
	}

	/*
	 * Now, we know that pgdata either doesn't exists or belongs to a stopped
	 * PostgreSQL instance. We can safely proceed with pg_basebackup.
	 *
	 * We might be asked to skip pg_basebackup when the PGDATA directory has
	 * already been prepared externally: typically we are creating a standby
	 * node and it was faster to install PGDATA from a file system snapshot or
	 * a backup/recovery tooling.
	 */
	if (skipBaseBackup)
	{
		log_info("Skipping base backup to use pre-existing PGDATA at \"%s\"",
				 pgSetup->pgdata);
	}
	else
	{
		/*
		 * pg_basebackup has this bug where it will copy over the whole PGDATA
		 * contents even if the WAL receiver subprocess fails early, typically
		 * when the replication slot does not exist on the target connection.
		 *
		 * We want to protect against this case here, so we manually check that
		 * the replication exists before calling pg_basebackup.
		 */
		bool hasReplicationSlot = false;

		/*
		 * When initialising from another standby (in REPORT_LSN, if there is
		 * currently no primary node and no candidate node either), we don't
		 * require a replication slot on the upstream node.
		 */
		bool needsReplicationSlot = !IS_EMPTY_STRING_BUFFER(upstream->slotName);

		if (needsReplicationSlot &&
			!upstream_has_replication_slot(upstream,
										   pgSetup,
										   &hasReplicationSlot))
		{
			/* errors have already been logged */
			return false;
		}

		if (!needsReplicationSlot || hasReplicationSlot)
		{
			/* first, make sure we can connect with "replication" */
			if (!pgctl_identify_system(upstream))
			{
				log_error("Failed to connect to the primary with a replication "
						  "connection string. See above for details");
				return false;
			}

			/* now pg_basebackup from our upstream node */
			if (!pg_basebackup(pgSetup->pgdata, pgSetup->pg_ctl, upstream))
			{
				return false;
			}
		}
		else
		{
			log_error("The replication slot \"%s\" has not been created yet "
					  "on the primary node " NODE_FORMAT,
					  upstream->slotName,
					  upstream->primaryNode.nodeId,
					  upstream->primaryNode.name,
					  upstream->primaryNode.host,
					  upstream->primaryNode.port);
			return false;
		}
	}

	/* we have a new PGDATA, update our pgSetup information */
	if (!local_postgres_update(postgres, true))
	{
		log_error("Failed to update our internal Postgres representation "
				  "after pg_basebackup, see above for details");
		return false;
	}

	/* now setup the replication configuration (primary_conninfo etc) */
	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   pgSetup->pgdata,
							   pgSetup->pg_ctl,
							   upstream))
	{
		log_error("Failed to setup Postgres as a standby after pg_basebackup");
		return false;
	}

	/*
	 * When --ssl-self-signed has been used, now is the time to build a
	 * self-signed certificate for the server. We place the certificate and
	 * private key in $PGDATA/server.key and $PGDATA/server.crt
	 *
	 * In particular we override the certificates that we might have fetched
	 * from the primary as part of pg_basebackup: we're not a backup, we're a
	 * standby node, we need our own certificate (even if self-signed).
	 */
	if (pgSetup->ssl.createSelfSignedCert)
	{
		if (!pg_create_self_signed_cert(pgSetup, hostname))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}
	}

	/*
	 * We might have local edits to implement to the PostgreSQL
	 * configuration, such as a specific listen_addresses or different TLS
	 * key and cert locations. By changing this before starting postgres these
	 * new settings will automatically be applied.
	 */
	if (!postgres_add_default_settings(postgres, hostname))
	{
		log_error("Failed to add default settings to the secondary, "
				  "see above for details.");
		return false;
	}

	if (!ensure_postgres_service_is_running(postgres))
	{
		return false;
	}

	log_info("PostgreSQL started on port %d", pgSetup->pgport);

	return true;
}


/*
 * primary_rewind_to_standby brings a database directory of a failed primary back
 * into a state where it can become the standby of the new primary.
 */
bool
primary_rewind_to_standby(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *replicationSource = &(postgres->replicationSource);
	NodeAddress *primaryNode = &(replicationSource->primaryNode);

	log_trace("primary_rewind_to_standby");
	log_info("Rewinding PostgreSQL to follow new primary node " NODE_FORMAT,
			 primaryNode->nodeId,
			 primaryNode->name,
			 primaryNode->host,
			 primaryNode->port);

	if (!ensure_postgres_service_is_stopped(postgres))
	{
		log_error("Failed to stop postgres to do rewind");
		return false;
	}

	if (!postgres_maybe_do_crash_recovery(postgres))
	{
		log_error("Failed to implement Postgres crash recovery "
				  "before calling pg_rewind");
		return false;
	}

	/* before pg_rewind, make sure we can connect with "replication" */
	if (!pgctl_identify_system(replicationSource))
	{
		log_error("Failed to connect to the primary node " NODE_FORMAT
				  "with a replication connection string. "
				  "See above for details",
				  primaryNode->nodeId,
				  primaryNode->name,
				  primaryNode->host,
				  primaryNode->port);
	}

	if (!pg_rewind(pgSetup->pgdata, pgSetup->pg_ctl, replicationSource))
	{
		log_error("Failed to rewind old data directory");
		return false;
	}

	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   pgSetup->pgdata,
							   pgSetup->pg_ctl,
							   replicationSource))
	{
		log_error("Failed to setup Postgres as a standby, after rewind");
		return false;
	}

	if (!ensure_postgres_service_is_running(postgres))
	{
		log_error("Failed to start postgres after rewind");
		return false;
	}

	return true;
}


/*
 * postgres_maybe_do_crash_recovery implements a round of Postgres crash
 * recovery for the local instance of Postgres when pg_rewind would otherwise
 * fail because of its internal checks.
 */
bool
postgres_maybe_do_crash_recovery(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *replicationSource = &(postgres->replicationSource);

	LocalExpectedPostgresStatus *pgStatus = &(postgres->expectedPgStatus);

	/* update our service controller for Postgres to release control */
	if (!keeper_set_postgres_state_unknown(&(pgStatus->state),
										   pgStatus->pgStatusPath))
	{
		/* errors have already been logged */
		return false;
	}

	/* we don't log the output for pg_ctl_status here */
	int status = pg_ctl_status(pgSetup->pg_ctl, pgSetup->pgdata, false);

	if (status != PG_CTL_STATUS_NOT_RUNNING)
	{
		log_error("Failed to prepare for crash recovery: "
				  "Postgres is not stopped");
		return false;
	}

	/*
	 * pg_rewind fails when the target cluster (meaning the local Postgres
	 * instance) is either running or has not been shutdown correctly. Time to
	 * use pg_controldata and see if the DBState there is to pg_rewind liking.
	 */
	const bool missingPgdataIsOk = false;

	if (!pg_controldata(pgSetup, missingPgdataIsOk))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * We know that Postgres is not running thanks to pg_ctl_status, and we
	 * just grabbed the output from pg_controldata. We can now implement the
	 * same pre-condition checks as in Postgres pg_rewind.c.
	 */
	if (pgSetup->control.state != DB_SHUTDOWNED &&
		pgSetup->control.state != DB_SHUTDOWNED_IN_RECOVERY)
	{
		/*
		 * Before calling pg_rewind, attempt crash recovery on the Postgres
		 * instance and then shutdown.
		 */
		ReplicationSource crashRecoveryReplicationSource = { 0 };

		log_info("Postgres needs to enter crash recovery before pg_rewind.");

		crashRecoveryReplicationSource = *replicationSource;

		/* we target the earlier consistent state possible, or 'immediate' */
		strlcpy(crashRecoveryReplicationSource.targetLSN,
				"immediate",
				sizeof(crashRecoveryReplicationSource.targetLSN));

		/* pause when reaching target to avoid creating a new local timeline */
		strlcpy(crashRecoveryReplicationSource.targetAction,
				"pause",
				sizeof(crashRecoveryReplicationSource.targetAction));

		strlcpy(crashRecoveryReplicationSource.targetTimeline,
				"current",
				sizeof(crashRecoveryReplicationSource.targetTimeline));

		if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
								   pgSetup->pgdata,
								   pgSetup->pg_ctl,
								   &crashRecoveryReplicationSource))
		{
			log_error("Failed to setup for crash recovery "
					  "in preparation for pg_rewind");
			return false;
		}

		/*
		 * Now that the configuration file is ready and asks for Postgres
		 * shutdown when reaching crash recovery time, we start postgres as a
		 * sub-process here and wait for it to terminate.
		 */
		fflush(stdout);
		fflush(stderr);

		/* time to create the node_active sub-process */
		pid_t fpid = fork();

		switch (fpid)
		{
			case -1:
			{
				log_error("Failed to fork the postgres supervisor process");
				return false;
			}

			case 0:
			{
				/* execv() the postgres binary directly, as a sub-process */
				(void) pg_ctl_postgres(pgSetup->pg_ctl,
									   pgSetup->pgdata,
									   pgSetup->pgport,
									   pgSetup->listen_addresses,

				                       /* do not open the service just yet */
									   false);

				/* unexpected */
				log_fatal("BUG: returned from service_keeper_runprogram()");
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			default:
			{
				/* wait until postgres crash recovery is done */
				for (int attempts = 0;; attempts++)
				{
					int timeout = 30;

					if (pg_setup_wait_until_is_ready(pgSetup, timeout, LOG_INFO))
					{
						break;
					}
				}

				/* get Postgres current LSN after recovery, might be useful */
				PGSQL *pgsql = &(postgres->sqlClient);

				if (pgsql_get_postgres_metadata(pgsql,
												&pgSetup->is_in_recovery,
												postgres->pgsrSyncState,
												postgres->currentLSN,
												&(pgSetup->control)))
				{
					log_info("Postgres has finished crash recovery at LSN %s",
							 postgres->currentLSN);
				}
				else
				{
					log_error("Failed to get Postgres metadata, continuing");
				}


				/*
				 * Now stop Postgres by just killing our child process, and
				 * wait until the child process has finished with waitpid().
				 */
				int wpid, status;

				do {
					if (kill(fpid, SIGTERM) != 0)
					{
						log_error("Failed to send SIGTERM to "
								  "Postgres pid %d: %m", fpid);
						return false;
					}

					wpid = waitpid(fpid, &status, WNOHANG);

					if (wpid == -1)
					{
						log_warn("Failed to wait until Postgres is done: %m");
					}

					/* waitpid could be WIFSTOPPED, then try again */
				} while (!(WIFEXITED(status) || !WIFSIGNALED(status)));

				if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CODE_QUIT)
				{
					return true;
				}
				else if (WIFEXITED(status))
				{
					int returnCode = WEXITSTATUS(status);

					log_warn("Postgres has finished crash recovery with "
							 "exit code %d",
							 returnCode);

					(void) pg_log_startup(pgSetup->pgdata, LOG_INFO);
				}
				else
				{
					log_error("BUG: can't make sense of waitpid() exit code");
					return false;
				}
			}
		}
	}

	return true;
}


/*
 * standby_promote promotes a standby postgres server to primary.
 */
bool
standby_promote(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	bool inRecovery = false;

	log_trace("standby_promote");

	if (!pgsql_is_in_recovery(pgsql, &inRecovery))
	{
		log_error("Failed to promote standby: couldn't determine whether postgres "
				  "is in recovery mode");
		return false;
	}

	if (!inRecovery)
	{
		log_info("Skipping promotion: postgres is not in recovery mode");

		/*
		 * Ensure idempotency: if in the last run we managed to promote, but
		 * failed to checkpoint, we still need to checkpoint.
		 */
		if (!pgsql_checkpoint(pgsql))
		{
			log_error("Failed to checkpoint after promotion");
			return false;
		}

		return true;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(pgsql);

	log_info("Promoting postgres");

	if (!pg_ctl_promote(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to promote standby: see pg_ctl promote errors above");
		return false;
	}

	do {
		log_info("Waiting for postgres to promote");
		pg_usleep(AWAIT_PROMOTION_SLEEP_TIME_MS * 1000);

		if (asked_to_stop || asked_to_stop_fast)
		{
			log_trace("standby_promote: signaled");
			pgsql_finish(pgsql);

			return false;
		}

		if (!pgsql_is_in_recovery(pgsql, &inRecovery))
		{
			log_error("Failed to determine whether postgres is in "
					  "recovery mode after promotion");
			return false;
		}
	} while (inRecovery);

	/*
	 * It's necessary to do a checkpoint before allowing the old primary to
	 * rewind, since there can be a race condition in which pg_rewind detects
	 * no change in timeline in the pg_control file, but a checkpoint is
	 * already in progress causing the timelines to diverge before replication
	 * starts.
	 */
	if (!pgsql_checkpoint(pgsql))
	{
		log_error("Failed to checkpoint after promotion");
		return false;
	}

	/* cleanup our standby setup */
	if (!pg_cleanup_standby_mode(pgSetup->control.pg_control_version,
								 pgSetup->pg_ctl,
								 pgSetup->pgdata,
								 pgsql))
	{
		log_error("Failed to clean-up Postgres replication settings, "
				  "see above for details");
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(pgsql);

	return true;
}


/*
 * check_postgresql_settings returns true when our minimal set of PostgreSQL
 * settings are correctly setup on the target server.
 */
bool
check_postgresql_settings(LocalPostgresServer *postgres, bool *settings_are_ok)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	bool isCitusInstanceKind = IS_CITUS_INSTANCE_KIND(postgres->pgKind);

	bool result = pgsql_check_postgresql_settings(pgsql,
												  isCitusInstanceKind,
												  settings_are_ok);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_standby_has_caught_up loops over a SQL query on the primary that
 * checks the current reported LSN from the standby's replication slot.
 */
bool
primary_standby_has_caught_up(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);

	char standbyCurrentLSN[PG_LSN_MAXLENGTH] = { 0 };
	bool hasReachedLSN = false;

	/* ensure some WAL level traffic to move things forward */
	if (!pgsql_checkpoint(pgsql))
	{
		log_error("Failed to checkpoint before checking "
				  "if a standby has caught-up to LSN %s",
				  postgres->standbyTargetLSN);
		return false;
	}

	if (!pgsql_one_slot_has_reached_target_lsn(pgsql,
											   postgres->standbyTargetLSN,
											   standbyCurrentLSN,
											   &hasReachedLSN))
	{
		/* errors have already been logged */
		return false;
	}

	if (hasReachedLSN)
	{
		log_info("Standby reached LSN %s, thus advanced past LSN %s",
				 standbyCurrentLSN, postgres->standbyTargetLSN);

		/* cache invalidation */
		bzero((void *) postgres->standbyTargetLSN, PG_LSN_MAXLENGTH);

		return true;
	}
	else
	{
		log_info("Standby reached LSN %s, waiting for LSN %s",
				 standbyCurrentLSN, postgres->standbyTargetLSN);

		return false;
	}
}


/*
 * standby_follow_new_primary rewrites the replication setup to follow the new
 * primary after a failover.
 */
bool
standby_follow_new_primary(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *replicationSource = &(postgres->replicationSource);
	NodeAddress *primaryNode = &(replicationSource->primaryNode);

	log_info("Follow new primary node " NODE_FORMAT,
			 primaryNode->nodeId,
			 primaryNode->name,
			 primaryNode->host,
			 primaryNode->port);

	/* when we have a primary, only proceed if we can reach it */
	if (!IS_EMPTY_STRING_BUFFER(replicationSource->primaryNode.host))
	{
		if (!pgctl_identify_system(replicationSource))
		{
			log_error("Failed to establish a replication connection "
					  "to the new primary, see above for details");
			return false;
		}
	}

	/* cleanup our existing standby setup, including postgresql.auto.conf */
	if (!pg_cleanup_standby_mode(pgSetup->control.pg_control_version,
								 pgSetup->pg_ctl,
								 pgSetup->pgdata,
								 pgsql))
	{
		log_error("Failed to clean-up Postgres replication settings, "
				  "see above for details");
		return false;
	}

	/* we might be back from maintenance and find Postgres is not running */
	if (pg_is_running(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_info("Stopping Postgres at \"%s\"", pgSetup->pgdata);

		if (!ensure_postgres_service_is_stopped(postgres))
		{
			log_error("Failed to stop Postgres at \"%s\"", pgSetup->pgdata);
			return false;
		}
	}

	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   pgSetup->pgdata,
							   pgSetup->pg_ctl,
							   replicationSource))
	{
		log_error("Failed to setup Postgres as a standby");
		return false;
	}

	log_info("Restarting Postgres at \"%s\"", pgSetup->pgdata);

	if (!ensure_postgres_service_is_running(postgres))
	{
		log_error("Failed to restart Postgres after changing its "
				  "primary conninfo, see above for details");
		return false;
	}

	return true;
}


/*
 * standby_fetch_missing_wal sets up replication to fetch up to given
 * recovery_target_lsn (inclusive) with a recovery_target_action set to
 * 'promote' so that as soon as we get our WAL bytes we are promoted to being a
 * primary.
 */
bool
standby_fetch_missing_wal(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	ReplicationSource *replicationSource = &(postgres->replicationSource);
	NodeAddress *upstreamNode = &(replicationSource->primaryNode);

	char currentLSN[PG_LSN_MAXLENGTH] = { 0 };
	bool hasReachedLSN = false;

	log_info("Fetching WAL from upstream node " NODE_FORMAT
			 "up to LSN %s",
			 upstreamNode->nodeId,
			 upstreamNode->name,
			 upstreamNode->host,
			 upstreamNode->port,
			 replicationSource->targetLSN);

	/* apply new replication source to fetch missing WAL bits */
	if (!standby_restart_with_current_replication_source(postgres))
	{
		log_error("Failed to setup replication "
				  "from upstream node " NODE_FORMAT
				  ", see above for details",
				  upstreamNode->nodeId,
				  upstreamNode->name,
				  upstreamNode->host,
				  upstreamNode->port);
	}

	/*
	 * Now loop until replay has reached our targetLSN.
	 */
	while (!hasReachedLSN)
	{
		if (asked_to_stop || asked_to_stop_fast)
		{
			log_trace("standby_fetch_missing_wal_and_promote: signaled");
			break;
		}

		if (!pgsql_has_reached_target_lsn(pgsql,
										  replicationSource->targetLSN,
										  currentLSN,
										  &hasReachedLSN))
		{
			/* errors have already been logged */
			return false;
		}

		if (!hasReachedLSN)
		{
			log_info("Postgres recovery is at LSN %s, waiting for LSN %s",
					 currentLSN, replicationSource->targetLSN);
			pg_usleep(AWAIT_PROMOTION_SLEEP_TIME_MS * 1000);
		}
	}

	/* done with fast-forwarding, keep the value for node_active() call */
	strlcpy(postgres->currentLSN, currentLSN, PG_LSN_MAXLENGTH);

	/* we might have been interrupted before the end */
	if (!hasReachedLSN)
	{
		log_error("Fast-forward reached LSN %s, target LSN is %s",
				  postgres->currentLSN,
				  replicationSource->targetLSN);
		pgsql_finish(pgsql);
		return false;
	}

	log_info("Fast-forward is done, now at LSN %s", postgres->currentLSN);

	/*
	 * It's necessary to do a checkpoint before allowing the old primary to
	 * rewind, since there can be a race condition in which pg_rewind detects
	 * no change in timeline in the pg_control file, but a checkpoint is
	 * already in progress causing the timelines to diverge before replication
	 * starts.
	 */
	if (!pgsql_checkpoint(pgsql))
	{
		log_error("Failed to checkpoint after fast-forward to LSN %s",
				  postgres->currentLSN);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(pgsql);

	return true;
}


/*
 * standby_restart_with_no_primary sets up recovery parameters without a
 * primary_conninfo, so as to force disconnect from the primary and still
 * remain a standby that can report its current LSN position, for instance.
 */
bool
standby_restart_with_current_replication_source(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	ReplicationSource *replicationSource = &(postgres->replicationSource);

	/* when we have a primary, only proceed if we can reach it */
	if (!IS_EMPTY_STRING_BUFFER(replicationSource->primaryNode.host))
	{
		if (!pgctl_identify_system(replicationSource))
		{
			log_error("Failed to establish a replication connection "
					  "to the primary node, see above for details");
			return false;
		}
	}

	/* cleanup our existing standby setup, including postgresql.auto.conf */
	if (!pg_cleanup_standby_mode(pgSetup->control.pg_control_version,
								 pgSetup->pg_ctl,
								 pgSetup->pgdata,
								 pgsql))
	{
		log_error("Failed to clean-up Postgres replication settings, "
				  "see above for details");
		return false;
	}

	log_info("Stopping Postgres at \"%s\"", pgSetup->pgdata);

	if (!ensure_postgres_service_is_stopped(postgres))
	{
		log_error("Failed to stop Postgres at \"%s\"", pgSetup->pgdata);
		return false;
	}

	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   pgSetup->pgdata,
							   pgSetup->pg_ctl,
							   replicationSource))
	{
		log_error("Failed to setup Postgres as a standby, after rewind");
		return false;
	}

	log_info("Restarting Postgres at \"%s\"", pgSetup->pgdata);

	if (!ensure_postgres_service_is_running(postgres))
	{
		log_error("Failed to restart Postgres after changing its "
				  "primary conninfo, see above for details");
		return false;
	}

	return true;
}


/*
 * standby_cleanup_as_primary removes the setup for a standby server and
 * restarts as a primary. It's typically called after standby_fetch_missing_wal
 * so we expect Postgres to be running as a standby and be "paused".
 */
bool
standby_cleanup_as_primary(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	log_info("Cleaning-up Postgres replication settings");

	if (!pg_cleanup_standby_mode(pgSetup->control.pg_control_version,
								 pgSetup->pg_ctl,
								 pgSetup->pgdata,
								 pgsql))
	{
		log_error("Failed to clean-up Postgres replication settings, "
				  "see above for details");
		return false;
	}

	return true;
}


/*
 * standby_check_timeline_with_upstream returns true when the current timeline
 * on the local node (a standby) is the same as the timeline fetched on the
 * upstream node setup in its replicationSource.
 */
bool
standby_check_timeline_with_upstream(LocalPostgresServer *postgres)
{
	ReplicationSource *replicationSource = &(postgres->replicationSource);
	NodeAddress *primaryNode = &(replicationSource->primaryNode);

	/* fetch timeline information from the upstream node */
	if (!pgctl_identify_system(replicationSource))
	{
		log_error("Failed to establish a replication connection "
				  "to the new primary, see above for details");
		return false;
	}

	/* fetch most recent local metadata, including the timeline id. */
	if (!pgsql_get_postgres_metadata(&(postgres->sqlClient),
									 &(postgres->postgresSetup.is_in_recovery),
									 postgres->pgsrSyncState,
									 postgres->currentLSN,
									 &(postgres->postgresSetup.control)))
	{
		log_error("Failed to update the local Postgres metadata");
		return false;
	}

	uint32_t upstreamTimeline = replicationSource->system.timeline;
	uint32_t localTimeline = postgres->postgresSetup.control.timeline_id;

	/* we might not be connected to the primary yet */
	if (localTimeline == 0)
	{
		log_warn("Current received timeline is unknown, pg_autoctl will "
				 "retry this transition.");
		return false;
	}

	/*
	 * We only allow this transition when the standby node as caught-up with
	 * the upstream timeline. As streaming replication is supposed to be a
	 * clean history replay (no PITR shenanigans), it is never expected that
	 * the local timeline would be greater than the timeline found on the
	 * upstream node.
	 */
	if (upstreamTimeline < localTimeline)
	{
		log_error("Current timeline on upstream node " NODE_FORMAT
				  " is %d, and current timeline on this standby node is %d",
				  primaryNode->nodeId,
				  primaryNode->name,
				  primaryNode->host,
				  primaryNode->port,
				  upstreamTimeline,
				  localTimeline);

		return false;
	}
	else if (upstreamTimeline > localTimeline)
	{
		log_warn("Current timeline on upstream node " NODE_FORMAT
				 " is %d, and current timeline on this standby node is still %d",
				 primaryNode->nodeId,
				 primaryNode->name,
				 primaryNode->host,
				 primaryNode->port,
				 upstreamTimeline,
				 localTimeline);

		return false;
	}
	else if (upstreamTimeline == localTimeline)
	{
		log_info("Reached timeline %d, same as upstream node " NODE_FORMAT,
				 localTimeline,
				 primaryNode->nodeId,
				 primaryNode->name,
				 primaryNode->host,
				 primaryNode->port);
	}

	return upstreamTimeline == localTimeline;
}
