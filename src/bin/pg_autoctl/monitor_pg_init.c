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
#include "debian.h"
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
#include "pidfile.h"
#include "primary_standby.h"
#include "service_monitor.h"
#include "service_monitor_init.h"
#include "service_postgres.h"
#include "signals.h"

/*
 * Default settings for PostgreSQL instance when running the pg_auto_failover
 * monitor.
 */
GUC monitor_default_settings[] = {
	{ "shared_preload_libraries", "'pgautofailover'" },
	{ "cluster_name", "'pg_auto_failover monitor'" },
	{ "listen_addresses", "'*'" },
	{ "port", "5432" },
	{ "log_destination", "stderr" },
	{ "logging_collector", "on" },
	{ "log_directory", "log" },
	{ "log_min_messages", "info" },
	{ "log_connections", "off" },
	{ "log_disconnections", "off" },
	{ "log_lock_waits", "on" },
	{ "log_statement", "ddl" },
	{ "password_encryption", "md5" },
	{ "ssl", "off" },
	{ "ssl_ca_file", "" },
	{ "ssl_crl_file", "" },
	{ "ssl_cert_file", "" },
	{ "ssl_key_file", "" },
	{ "ssl_ciphers", "'" DEFAULT_SSL_CIPHERS "'" },
#ifdef TEST
	{ "unix_socket_directories", "''" },
#endif
	{ NULL, NULL }
};


static bool check_monitor_settings(PostgresSetup pgSetup);


/*
 * monitor_pg_init initializes a pg_auto_failover monitor PostgreSQL cluster
 * from scratch using `pg_ctl initdb`.
 */
bool
monitor_pg_init(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	if (pg_setup_pgdata_exists(pgSetup))
	{
		PostgresSetup existingPgSetup = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		if (!pg_setup_init(&existingPgSetup, pgSetup,
						   missing_pgdata_is_ok,
						   pg_is_not_running_is_ok))
		{
			log_fatal("Failed to initialize a monitor node, "
					  "see above for details");
			return false;
		}

		if (pg_setup_is_running(&existingPgSetup))
		{
			log_error("Installing pg_auto_failover monitor in existing "
					  "PostgreSQL instance at \"%s\" running on port %d "
					  "is not supported.",
					  pgSetup->pgdata, existingPgSetup.pidFile.port);

			return false;
		}

		/* if we have a debian cluster, re-own the configuration files */
		if (!keeper_ensure_pg_configuration_files_in_pgdata(&existingPgSetup))
		{
			log_fatal("Failed to setup your Postgres instance "
					  "the PostgreSQL way, see above for details");
			return false;
		}
	}
	else
	{
		if (!pg_ctl_initdb(pgSetup->pg_ctl, pgSetup->pgdata))
		{
			log_fatal("Failed to initialize a PostgreSQL instance at \"%s\", "
					  "see above for details", pgSetup->pgdata);
			return false;
		}
	}

	if (!monitor_add_postgres_default_settings(monitor))
	{
		log_fatal("Failed to initialize our Postgres settings, "
				  "see above for details");
		return false;
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
monitor_install(const char *hostname,
				PostgresSetup pgSetupOption, bool checkSettings)
{
	PostgresSetup pgSetup = { 0 };
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;
	LocalPostgresServer postgres = { 0 };
	char connInfo[MAXCONNINFO];

	/* We didn't create our target username/dbname yet */
	strlcpy(pgSetupOption.username, "", NAMEDATALEN);
	strlcpy(pgSetupOption.dbname, "", NAMEDATALEN);

	/*
	 * We might have just started a PostgreSQL instance, so we want to recheck
	 * the PostgreSQL setup.
	 */
	if (!pg_setup_init(&pgSetup, &pgSetupOption,
					   missingPgdataIsOk, pgIsNotRunningIsOk))
	{
		log_fatal("Failed to initialize a monitor node, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	(void) local_postgres_init(&postgres, &pgSetup);

	if (!ensure_postgres_service_is_running(&postgres))
	{
		log_error("Failed to install pg_auto_failover in the monitor's "
				  "Postgres database, see above for details");
		return false;
	}

	if (!pgsql_create_user(&postgres.sqlClient, PG_AUTOCTL_MONITOR_DBOWNER,

	                       /* password, login, superuser, replication, connlimit */
						   NULL, true, false, false, -1))
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

	/* now, connect to the newly created database to create our extension */
	strlcpy(pgSetup.dbname, PG_AUTOCTL_MONITOR_DBNAME, NAMEDATALEN);
	pg_setup_get_local_connection_string(&pgSetup, connInfo);
	pgsql_init(&postgres.sqlClient, connInfo, PGSQL_CONN_LOCAL);

	/*
	 * Ensure our extension "pgautofailvover" is available in the server
	 * extension dir used to create the Postgres instance. We only search for
	 * the control file to offer better diagnostics in the logs in case the
	 * following CREATE EXTENSION fails.
	 */
	if (!find_extension_control_file(pgSetup.pg_ctl,
									 PG_AUTOCTL_MONITOR_EXTENSION_NAME))
	{
		log_warn("Failed to find extension control file for \"%s\"",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME);
	}

	if (!pgsql_create_extension(&postgres.sqlClient,
								PG_AUTOCTL_MONITOR_EXTENSION_NAME))
	{
		log_error("Failed to create extension %s",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
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

	/*
	 * Now make sure we allow nodes on the same network to connect to
	 * pg_auto_failover database.
	 */
	if (!pghba_enable_lan_cidr(&postgres.sqlClient,
							   pgSetup.ssl.active,
							   HBA_DATABASE_DBNAME,
							   PG_AUTOCTL_MONITOR_DBNAME,
							   hostname,
							   PG_AUTOCTL_MONITOR_USERNAME,
							   pg_setup_get_auth_method(&pgSetup),
							   pgSetup.hbaLevel,
							   NULL))
	{
		log_warn("Failed to grant connection to local network.");
		return false;
	}

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
	pgsql_init(&postgres.sqlClient, connInfo, PGSQL_CONN_LOCAL);

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


/*
 * monitor_add_postgres_default_settings adds the monitor Postgres setup.
 */
bool
monitor_add_postgres_default_settings(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);
	PostgresSetup *pgSetup = &(config->pgSetup);
	char configFilePath[MAXPGPATH] = { 0 };

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
	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	/*
	 * When --ssl-self-signed has been used, now is the time to build a
	 * self-signed certificate for the server. We place the certificate and
	 * private key in $PGDATA/server.key and $PGDATA/server.crt
	 */
	if (pgSetup->ssl.createSelfSignedCert)
	{
		if (!pg_create_self_signed_cert(&(config->pgSetup), config->hostname))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}

		/* update our configuration with ssl server.{key,cert} */
		if (!monitor_config_write_file(config))
		{
			/* errors have already been logged */
			return false;
		}
	}

	if (!pg_add_auto_failover_default_settings(pgSetup,
											   config->hostname,
											   configFilePath,
											   monitor_default_settings))
	{
		log_error("Failed to add default settings to \"%s\": couldn't "
				  "write the new postgresql.conf, see above for details",
				  configFilePath);
		return false;
	}

	return true;
}
