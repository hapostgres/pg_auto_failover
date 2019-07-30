/*
 * src/bin/pg_autoctl/monitor_pg_init.c
 *     Monitor initialisation.
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
#include "ipaddr.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "pgctl.h"
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "primary_standby.h"


/*
 * Default settings for PostgreSQL instance when running the pg_auto_failover
 * monitor.
 */
GUC monitor_default_settings[] = {
	{ "shared_preload_libraries", "'pgautofailover'" },
	{ "listen_addresses", "'*'" },
	{ "port", "5432" },
#ifdef TEST
	{ "unix_socket_directories", "''" },
#endif
	{ NULL, NULL }
};


static bool monitor_install(const char *nodename,
							PostgresSetup pgSetupOption, bool checkSettings);
static bool check_monitor_settings(PostgresSetup pgSetup);


/*
 * monitor_pg_init initialises a pg_auto_failover monitor PostgreSQL cluster,
 * either from scratch using `pg_ctl initdb`, or creating a new database in an
 * existing cluster.
 */
bool
monitor_pg_init(Monitor *monitor, MonitorConfig *config)
{
	char configFilePath[MAXPGPATH];
	char postgresUri[MAXCONNINFO];
	PostgresSetup pgSetup = config->pgSetup;

	if (directory_exists(pgSetup.pgdata))
	{
		PostgresSetup existingPgSetup = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		if (!pg_setup_init(&existingPgSetup, &pgSetup,
						   missing_pgdata_is_ok,
						   pg_is_not_running_is_ok))
		{
			log_fatal("Failed to initialise a monitor node, "
					  "see above for details");
			return false;
		}

		if (pg_setup_is_running(&existingPgSetup))
		{
			log_info("Installing pg_auto_failover monitor in existing "
					 "PostgreSQL instance at \"%s\" running on port %d",
					 pgSetup.pgdata, existingPgSetup.pidFile.port);

			if (!monitor_install(config->nodename, existingPgSetup, true))
			{
				log_fatal("Failed to install pg_auto_failover monitor, "
						  "see above for details");
				return false;
			}

			/* and we're done now! */
			return true;
		}

		if (pg_setup_pgdata_exists(&existingPgSetup))
		{
			log_fatal("PGDATA directory \"%s\" already exists, skipping",
					  pgSetup.pgdata);
			return false;
		}
	}

	if (!pg_ctl_initdb(pgSetup.pg_ctl, pgSetup.pgdata))
	{
		log_fatal("Failed to initialise a PostgreSQL instance at \"%s\", "
				  "see above for details", pgSetup.pgdata);
		return false;
	}

	/*
	 * We managed to initdb, refresh our configuration file location with
	 * the realpath(3): we might have been given a relative pathname.
	 */
	if (!monitor_config_update_with_absolute_pgdata(config))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * We just did the initdb ourselves, so we know where the configuration
	 * file is to be found Also, we didn't start PostgreSQL yet.
	 */
	join_path_components(configFilePath, pgSetup.pgdata, "postgresql.conf");

	if (!pg_add_auto_failover_default_settings(&pgSetup, configFilePath,
											   monitor_default_settings))
	{
		log_error("Failed to add default settings to \"%s\".conf: couldn't "
				  "write the new postgresql.conf, see above for details",
				  configFilePath);
		return false;
	}

	if (!pg_ctl_start(pgSetup.pg_ctl,
					  pgSetup.pgdata, pgSetup.pgport, pgSetup.listen_addresses))
	{
		log_error("Failed to start postgres, see above");
		return false;
	}

	if (!monitor_install(config->nodename, pgSetup, false))
	{
		return false;
	}

	if (monitor_config_get_postgres_uri(config, postgresUri, MAXCONNINFO))
	{
		log_info("pg_auto_failover monitor is ready at %s", postgresUri);
	}

	return true;
}


/*
 * Install pg_auto_failover monitor in some existing PostgreSQL instance:
 *
 *  - add postgresql-auto-failover.conf to postgresql.conf
 *  - create user autoctl with createdb login;
 *  - create database pg_auto_failover with owner autoctl;
 *  - create extension pgautofailover;
 */
bool
monitor_install(const char *nodename,
				PostgresSetup pgSetupOption, bool checkSettings)
{
	PostgresSetup pgSetup = { 0 };
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = false;
	LocalPostgresServer postgres = { 0 };
	char connInfo[MAXCONNINFO];

	/* We didn't create our target username/dbname yet */
	strlcpy(pgSetupOption.username, "", NAMEDATALEN);
	strlcpy(pgSetupOption.dbname, "", NAMEDATALEN);

	/*
	 * We might have just started a PostgreSQL instance, so we want to recheck
	 * the PostgreSQL setup. Also this time we make sure PostgreSQL is running,
	 * which the rest of this function assumes.
	 */
	if (!pg_setup_init(&pgSetup, &pgSetupOption,
					   missingPgdataIsOk, pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialise a monitor node, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	local_postgres_init(&postgres, &pgSetup);

	if (!pgsql_create_user(&postgres.sqlClient, PG_AUTOCTL_MONITOR_DBOWNER,
	                       /* password, login, superuser, replication */
						   NULL, true, false, false))
	{
		log_error("Failed to create user \"%s\" on local postgres server",
				  PG_AUTOCTL_MONITOR_DBOWNER);
		return false;
	}

	if (!pgsql_create_database(&postgres.sqlClient,
							   PG_AUTOCTL_MONITOR_DBNAME,
							   PG_AUTOCTL_MONITOR_DBOWNER))
	{
		log_error("Failed to create database %s with owner %s",
				  PG_AUTOCTL_MONITOR_DBNAME, PG_AUTOCTL_MONITOR_DBOWNER);
		return false;
	}

	/* we're done with that connection to "postgres" database */
	pgsql_finish(&postgres.sqlClient);

	/* now, connect to the newly created database to create our extension */
	strlcpy(pgSetup.dbname, PG_AUTOCTL_MONITOR_DBNAME, NAMEDATALEN);
	pg_setup_get_local_connection_string(&pgSetup, connInfo);
	pgsql_init(&postgres.sqlClient, connInfo);

	if (!pgsql_create_extension(&postgres.sqlClient,
								PG_AUTOCTL_MONITOR_EXTENSION_NAME))
	{
		log_error("Failed to create extension %s",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		return false;
	}

	/*
	 * Now allow nodes on the same network to connect to pg_auto_failover
	 * database.
	 */
	if (!pghba_enable_lan_cidr(&postgres.sqlClient,
							   HBA_DATABASE_DBNAME,
							   PG_AUTOCTL_MONITOR_DBNAME,
							   nodename,
							   PG_AUTOCTL_MONITOR_USERNAME,
							   pg_setup_get_auth_method(&pgSetup),
							   NULL))
	{
		log_warn("Failed to grant connection to local network.");
		return false;
	}

	/*
	 * When installing the monitor on-top of an already running PostgreSQL, we
	 * want to check that our settings have been applied already, and warn the
	 * user to restart their instance otherwise.
	 */
	if (checkSettings)
	{
		if (!check_monitor_settings(pgSetup))
		{
			/* that's highly unexpected */
			log_fatal("Failed to check pg_auto_failover monitor settings");
			return false;
		}
	}

	pgsql_finish(&postgres.sqlClient);

	log_info("Your pg_auto_failover monitor instance is now ready on port %d.",
			 pgSetup.pgport);

	return true;
}


/*
 * check_monitor_settings returns true if the pgautofailover extension is
 * already part of the shared_preload_libraries GUC.
 */
static bool
check_monitor_settings(PostgresSetup pgSetup)
{
	LocalPostgresServer postgres = { 0 };
	char connInfo[MAXCONNINFO];
	bool settingsAreOk = false;

	pg_setup_get_local_connection_string(&pgSetup, connInfo);
	pgsql_init(&postgres.sqlClient, connInfo);

	if (!pgsql_check_monitor_settings(&(postgres.sqlClient), &settingsAreOk))
	{
		/* errors have already been logged */
		return false;
	}

	if (settingsAreOk)
	{
		log_info("PostgreSQL shared_preload_libraries already includes \"%s\"",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME);
	}
	else
	{
		log_warn("PostgreSQL shared_preload_libraries doesn't include \"%s\"",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		log_fatal("Current PostgreSQL settings are not compliant "
				  "with pg_auto_failover monitor requirements, please restart "
				  "PostgreSQL at the next opportunity to enable "
				  "pg_auto_failover monitor changes");
	}

	return settingsAreOk;
}
