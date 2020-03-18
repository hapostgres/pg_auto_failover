/*
 * src/bin/pg_autoctl/primary_standby.c
 *     API to manage a local postgres database cluster
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <time.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsql.h"
#include "primary_standby.h"


static void local_postgres_update_pg_failures_tracking(
	LocalPostgresServer *postgres,
	bool pgIsRunning);

/*
 * Default settings for postgres databases managed by pg_auto_failover.
 * These settings primarily ensure that streaming replication is
 * possible and synchronous replication is the default.
 *
 * listen_addresses and port are placeholder values in this array and are
 * replaced with dynamic values from the setup when used.
 */
#define DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER		\
	{ "listen_addresses", "'*'" },						\
	{ "port", "5432" },									\
	{ "max_wal_senders", "4" },							\
	{ "max_replication_slots", "4" },					\
	{ "wal_level", "'replica'" },						\
	{ "wal_log_hints", "on" },							\
	{ "wal_keep_segments", "64" },						\
	{ "wal_sender_timeout", "'30s'" },					\
	{ "hot_standby_feedback", "on" },					\
	{ "hot_standby", "on" },							\
	{ "synchronous_commit", "on" },						\
	{ "logging_collector", "on" },						\
	{ "log_destination", "stderr"},						\
	{ "logging_collector", "on"},						\
	{ "log_directory", "log"},							\
	{ "log_min_messages", "info"},						\
	{ "log_connections", "on"},							\
	{ "log_disconnections", "on"},						\
	{ "log_lock_waits", "on"},							\
	{ "ssl", "off" },									\
	{ "ssl_ca_file", "" },								\
	{ "ssl_crl_file", "" },								\
	{ "ssl_cert_file", "" },							\
	{ "ssl_key_file", "" },								\
	{ "ssl_ciphers", "'TLSv1.2+HIGH:!aNULL:!eNULL'" }

GUC postgres_default_settings[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER,
	{ NULL, NULL }
};

GUC citus_default_settings[] = {
	DEFAULT_GUC_SETTINGS_FOR_PG_AUTO_FAILOVER,
	{ "shared_preload_libraries", "'citus'" },
	{ "citus.node_conninfo", "'sslmode=prefer'" },
	{ NULL, NULL }
};


/*
 * local_postgres_init initialises an interface for managing a local
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
 * ensure_local_postgres_is_running starts postgres if it is not already
 * running and updates the setup such that we can connect to it.
 */
bool
ensure_local_postgres_is_running(LocalPostgresServer *postgres)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	log_trace("ensure_local_postgres_is_running");

	if (!pg_is_running(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_info("Postgres is not running, starting postgres");

		if (!pg_ctl_start(pgSetup->pg_ctl,
						  pgSetup->pgdata,
						  pgSetup->pgport,
						  pgSetup->listen_addresses))
		{
			/* errors have already been logged */
			bool pgIsRunning = false;
			local_postgres_update_pg_failures_tracking(postgres, pgIsRunning);
			return false;
		}
		else
		{
			/* we expect postgres to be running now */
			PostgresSetup newPgSetup = { 0 };
			bool missingPgdataIsOk = false;
			bool postgresNotRunningIsOk = false;
			bool pgIsRunning = false;

			/*
			 * We know that pg_ctl start --wait was successfull. Still Postgres
			 * might not have updated its postmaster.pid file yet. Have a
			 * couple attempts at
			 */
			int maxAttempts = 5;
			int attempts = 0;

			for (attempts = 0; attempts < maxAttempts; attempts++)
			{
				bool pgIsRunning = pg_setup_is_running(pgSetup);

				log_trace("waiting for pg_setup_is_running() [%s], attempt %d/%d",
						  pgIsRunning ? "true" : "false",
						  attempts+1,
						  maxAttempts);

				if (pgIsRunning)
				{
					break;
				}

				/* wait for 100 ms and try again */
				pg_usleep(100 * 1000);
			}

			/* update settings from running database */
			if (!pg_setup_init(&newPgSetup, pgSetup, missingPgdataIsOk,
							   postgresNotRunningIsOk))
			{
				/* errors have already been logged */
				pgIsRunning = false;
				local_postgres_update_pg_failures_tracking(postgres, pgIsRunning);
				return false;
			}

			/* update connection string for connection to postgres */
			local_postgres_init(postgres, &newPgSetup);

			/* update PostgreSQL restart failure tracking */
			pgIsRunning = true;
			local_postgres_update_pg_failures_tracking(postgres, pgIsRunning);
		}
	}

	return true;
}


/*
 * primary_has_replica returns whether the local postgres server has a
 * replica that is connecting using the given user name.
 */
bool
primary_has_replica(LocalPostgresServer *postgres, char *userName, bool *hasStandby)
{
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_has_replica");

	result = pgsql_has_replica(pgsql, userName, hasStandby);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_create_replication_slot (re)creates a replication slot. The replication
 * slot will not have its LSN initialised until first use. The return value indicates
 * whether the operation was successful.
 */
bool
primary_create_replication_slot(LocalPostgresServer *postgres, char *replicationSlotName)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	bool verbose = false;
	bool result = false;

	log_trace("primary_create_replication_slot");

	/*
	 * Try dropping the replication slot in case it already exists and
	 * is stuck at some arbitrary point in the timeline.
	 *
	 * We ignore failures because all we really care about is creating
	 * the slot.
	 */
	pgsql_drop_replication_slot(pgsql, replicationSlotName, verbose);

	result = pgsql_create_replication_slot(pgsql, replicationSlotName);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_drop_replication_slot drops a replication slot if it exists. The return
 * value indicates whether the operation was successful.
 */
bool
primary_drop_replication_slot(LocalPostgresServer *postgres, char *replicationSlotName)
{
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_drop_replication_slot");

	result = pgsql_drop_replication_slot(pgsql, replicationSlotName, true);

	pgsql_finish(pgsql);
	return result;
}


/*
 * primary_enable_synchronous_replication enables synchronous replication
 * on a primary postgres node.
 */
bool
primary_enable_synchronous_replication(LocalPostgresServer *postgres)
{
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_enable_synchronous_replication");

	result = pgsql_enable_synchronous_replication(pgsql);

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
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);

	log_trace("primary_disable_synchronous_replication");

	result = pgsql_disable_synchronous_replication(pgsql);

	pgsql_finish(pgsql);
	return result;
}


/*
 * postgres_add_default_settings ensures that postgresql.conf includes a
 * postgresql-auto-failover.conf file that sets a number of good defaults for
 * settings related to streaming replication and running pg_auto_failover.
 */
bool
postgres_add_default_settings(LocalPostgresServer *postgres)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	char configFilePath[MAXPGPATH];
	GUC *default_settings = postgres_default_settings;

	log_trace("primary_add_default_postgres_settings");

	/* configFilePath = $PGDATA/postgresql.conf */
	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	/* in case of errors, pgsql_ functions finish the connection */
	pgsql_finish(pgsql);

	/* default settings are different when dealing with a Citus node */
	if (IS_CITUS_INSTANCE_KIND(postgres->pgKind))
	{
		default_settings = citus_default_settings;
	}

	if (!pg_add_auto_failover_default_settings(pgSetup,
											   configFilePath,
											   default_settings))
	{
		log_error("Failed to add default settings to postgres.conf: couldn't "
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
							 char *password, char *hostname, char *authMethod)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	bool login = true;
	bool superuser = false;
	bool replication = false;
	char hbaFilePath[MAXPGPATH];

	log_trace("primary_create_user_with_hba");

	if (!pgsql_create_user(pgsql, userName, password,
						   login, superuser, replication))
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
									   authMethod))
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
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);
	bool login = true;
	bool superuser = true;
	bool replication = true;

	log_trace("primary_create_replication_user");

	result = pgsql_create_user(pgsql, replicationUsername, replicationPassword,
							   login, superuser, replication);

	pgsql_finish(pgsql);

	return result;
}


/*
 * primary_add_standby_to_hba ensures the current standby (if any) is added
 * to pg_hba.conf on the primary.
 */
bool
primary_add_standby_to_hba(LocalPostgresServer *postgres,
						   char *standbyHostname,
						   const char *replicationPassword)
{
	PGSQL *pgsql = &(postgres->sqlClient);
	PostgresSetup *postgresSetup = &(postgres->postgresSetup);
	char hbaFilePath[MAXPGPATH];
	char *authMethod = pg_setup_get_auth_method(postgresSetup);

	if (replicationPassword == NULL)
	{
		/* most authentication methods require a password */
		if (strcmp(authMethod, "trust") != 0
			|| strcmp(authMethod, SKIP_HBA_AUTH_METHOD) != 0)
		{
			log_warn("Granting replication connection for \"%s\" "
					 "using authentication method \"%s\" although no "
					 "replication password has been set",
					 standbyHostname, authMethod);
			log_info("HINT: see `pg_autoctl config get replication.password`");
		}
	}

	log_trace("primary_add_standby_to_hba");

	if (!pgsql_get_hba_file_path(pgsql, hbaFilePath, MAXPGPATH))
	{
		log_error("Failed to add the standby node to PostgreSQL HBA file: "
				  "couldn't get the standby pg_hba file location from the local "
				  "postgres server.");
		return false;
	}

	if (!pghba_ensure_host_rule_exists(hbaFilePath,
									   postgresSetup->ssl.active,
									   HBA_DATABASE_REPLICATION, NULL,
									   PG_AUTOCTL_REPLICA_USERNAME,
									   standbyHostname, authMethod))
	{
		log_error("Failed to add the standby node to PostgreSQL HBA file: "
				  "couldn't modify the pg_hba file");
		return false;
	}

	if (!pghba_ensure_host_rule_exists(hbaFilePath,
									   postgresSetup->ssl.active,
									   HBA_DATABASE_DBNAME,
									   postgresSetup->dbname,
									   PG_AUTOCTL_REPLICA_USERNAME,
									   standbyHostname, authMethod))
	{
		log_error("Failed to add the standby node to PostgreSQL HBA file: "
				  "couldn't modify the pg_hba file");
		return false;
	}

	if (!pgsql_reload_conf(pgsql))
	{
		log_error("Failed to reload the postgres configuration after adding "
				  "the standby user to pg_hba");
		return false;
	}

	pgsql_finish(pgsql);

	return true;
}


/*
 * standby_init_database tries to initialize PostgreSQL as a hot standby. It uses
 * pg_basebackup to do so. Returns false on failure.
 */
bool
standby_init_database(LocalPostgresServer *postgres,
					  ReplicationSource *replicationSource,
					  const char *nodename)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	log_trace("standby_init_database");
	log_info("Initialising PostgreSQL as a hot standby");

	if (pg_setup_pgdata_exists(pgSetup))
	{
		log_info("Target directory exists: \"%s\", stopping PostgreSQL",
				 pgSetup->pgdata);

		/* try to stop PostgreSQL, stop here if that fails */
		if (!pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata))
		{
			log_error("Failed to initialise a standby: "
					  "the database directory exists "
					  "and postgres could not be stopped");
			return false;
		}
	}

	/*
	 * Now, we know that pgdata either doesn't exists or belongs to a stopped
	 * PostgreSQL instance. We can safely proceed with pg_basebackup.
	 */
	if (!pg_basebackup(pgSetup->pgdata, pgSetup->pg_ctl, replicationSource))
	{
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
		if (!pg_create_self_signed_cert(pgSetup, nodename))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}
	}

	if (!ensure_local_postgres_is_running(postgres))
	{
		return false;
	}

	log_info("PostgreSQL started on port %d", pgSetup->pgport);

	/*
	 * We might have local edits to implement to the PostgreSQL
	 * configuration, such as a specific listen_addresses.
	 *
	 * Because pg_auto_failover always enforce the listen_addresses and port
	 * settings in pg_ctl_start, we don't actually have to restart PostgreSQL
	 * after having applied the settings here. The reason for doing the effort
	 * is to make the situation cleaner in case an operator was to manually
	 * start/restart PostgreSQL.
	 */
	if (!postgres_add_default_settings(postgres))
	{
		log_error("Failed to add default settings to the secondary, "
				  "see above for details.");
		return false;
	}

	return true;
}


/*
 * primary_rewind_to_standby brings a database directory of a failed primary back
 * into a state where it can become the standby of the new primary.
 */
bool
primary_rewind_to_standby(LocalPostgresServer *postgres,
						  ReplicationSource *replicationSource)
{
	char configFilePath[MAXPGPATH];
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	NodeAddress *primaryNode = &(replicationSource->primaryNode);

	log_trace("primary_rewind_to_standby");
	log_info("Rewinding PostgreSQL to follow new primary %s:%d",
			 primaryNode->host, primaryNode->port);

	/* configFilePath = $PGDATA/postgresql.conf */
	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	if (!pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to stop postgres to do rewind");
		return false;
	}

	if (!pg_rewind(pgSetup->pgdata, pgSetup->pg_ctl, replicationSource))
	{
		log_error("Failed to rewind old data directory");
		return false;
	}

	if (!pg_setup_standby_mode(pgSetup->control.pg_control_version,
							   configFilePath,
							   pgSetup->pgdata,
							   replicationSource))
	{
		log_error("Failed to setup Postgres as a standby, after rewind");
		return false;
	}

	if (!ensure_local_postgres_is_running(postgres))
	{
		log_error("Failed to start postgres after rewind");
		return false;
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

	do
	{
		log_info("Waiting for postgres to promote");
		pg_usleep(AWAIT_PROMOTION_SLEEP_TIME_MS * 1000);

		if (!pgsql_is_in_recovery(pgsql, &inRecovery))
		{
			log_error("Failed to determine whether postgres is in "
					  "recovery mode after promotion");
			return false;
		}
	}
	while (inRecovery);

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

	/*
	 * Starting with Postgres 12, pg_basebackup sets the recovery configuration
	 * parameters in the postgresql.auto.conf file. We need to make sure to
	 * RESET this value so that our own configuration setting takes effect.
	 */
	if (pgSetup->control.pg_control_version >= 1200)
	{
		if (!pgsql_reset_primary_conninfo(pgsql))
		{
			log_error("Failed to RESET primary_conninfo");
			return false;
		}
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
	bool result = false;
	PGSQL *pgsql = &(postgres->sqlClient);
	bool isCitusInstanceKind = IS_CITUS_INSTANCE_KIND(postgres->pgKind);

	result = pgsql_check_postgresql_settings(pgsql,
											 isCitusInstanceKind,
											 settings_are_ok);

	pgsql_finish(pgsql);
	return result;
}
