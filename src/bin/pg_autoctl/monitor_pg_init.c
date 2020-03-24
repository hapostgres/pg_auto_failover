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
#include "service.h"
#include "service_monitor.h"
#include "service_postgres.h"
#include "signals.h"


/*
 * Default settings for PostgreSQL instance when running the pg_auto_failover
 * monitor.
 */
GUC monitor_default_settings[] = {
	{ "shared_preload_libraries", "'pgautofailover'" },
	{ "listen_addresses", "'*'" },
	{ "port", "5432" },
	{ "log_destination", "stderr"},
	{ "logging_collector", "on"},
	{ "log_directory", "log"},
	{ "log_min_messages", "info"},
	{ "log_connections", "on"},
	{ "log_disconnections", "on"},
	{ "log_lock_waits", "on"},
	{ "ssl", "off" },
	{ "ssl_ca_file", "" },
	{ "ssl_crl_file", "" },
	{ "ssl_cert_file", "" },
	{ "ssl_key_file", "" },
	{ "ssl_ciphers", "'TLSv1.2+HIGH:!aNULL:!eNULL'" },
#ifdef TEST
	{ "unix_socket_directories", "''" },
#endif
	{ NULL, NULL }
};


static bool service_monitor_init_start(void *context, pid_t *pid);
static bool monitor_install(const char *nodename,
							PostgresSetup pgSetupOption, bool checkSettings);
static bool check_monitor_settings(PostgresSetup pgSetup);


/*
 * monitor_pg_init initialises a pg_auto_failover monitor PostgreSQL cluster,
 * either from scratch using `pg_ctl initdb`, or creating a new database in an
 * existing cluster.
 */
bool
monitor_pg_init(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);
	char configFilePath[MAXPGPATH];
	PostgresSetup *pgSetup = &(config->pgSetup);

	if (directory_exists(pgSetup->pgdata))
	{
		PostgresSetup existingPgSetup = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		if (!pg_setup_init(&existingPgSetup, pgSetup,
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
					 pgSetup->pgdata, existingPgSetup.pidFile.port);

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
					  pgSetup->pgdata);
			return false;
		}
	}

	if (!pg_ctl_initdb(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_fatal("Failed to initialise a PostgreSQL instance at \"%s\", "
				  "see above for details", pgSetup->pgdata);
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
	join_path_components(configFilePath, pgSetup->pgdata, "postgresql.conf");

	/*
	 * When --ssl-self-signed has been used, now is the time to build a
	 * self-signed certificate for the server. We place the certificate and
	 * private key in $PGDATA/server.key and $PGDATA/server.crt
	 */
	if (pgSetup->ssl.createSelfSignedCert)
	{
		if (!pg_create_self_signed_cert(pgSetup, config->nodename))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}
	}

	if (!pg_add_auto_failover_default_settings(pgSetup, configFilePath,
											   monitor_default_settings))
	{
		log_error("Failed to add default settings to \"%s\": couldn't "
				  "write the new postgresql.conf, see above for details",
				  configFilePath);
		return false;
	}

	return true;
}


/*
 * monitor_pg_finish_init starts the Postgres instance that we need running to
 * finish our installation, and finished the installation of the pgautofailover
 * monitor extension in the Postgres instance.
 */
bool
monitor_pg_init_finish(Monitor *monitor)
{
	MonitorConfig *config = &monitor->config;
	PostgresSetup *pgSetup = &config->pgSetup;

	Service subprocesses[] = {
		{
			"postgres",
			0,
			&service_postgres_start,
			&service_postgres_stop,
			(void *) pgSetup
		},
		{
			"installer",
			0,
			&service_monitor_init_start,
			&service_monitor_stop,
			(void *) monitor
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	/* We didn't create our target username/dbname yet */
	strlcpy(pgSetup->username, "", NAMEDATALEN);
	strlcpy(pgSetup->dbname, "", NAMEDATALEN);

	if (!service_start(subprocesses, subprocessesCount, config->pathnames.pid))
	{
		/* errors have already been logged */
		return false;
	}

	/* we only get there when the supervisor exited successfully (SIGTERM) */
	return true;
}


/*
 * service_monitor_init_start is a subprocess that finishes the installation of
 * the monitor extension for pgautofailover.
 */
static bool
service_monitor_init_start(void *context, pid_t *pid)
{
	Monitor *monitor = (Monitor *) context;
	MonitorConfig *config = &monitor->config;
	PostgresSetup *pgSetup = &config->pgSetup;

	pid_t fpid;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the monitor install process");
			return false;
		}

		case 0:
		{
			/* finish the install */
			bool missingPgdataIsOk = false;
			bool postgresNotRunningIsOk = false;

			(void) set_ps_title("installer");

			if (!monitor_install(config->nodename, *pgSetup, false))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			log_info("Monitor has been succesfully initialized.");

			if (createAndRun)
			{
				(void) set_ps_title("listener");

				/* reset pgSetup username, dbname, and Postgres information */
				if (!monitor_config_read_file(config,
											  missingPgdataIsOk,
											  postgresNotRunningIsOk))
				{
					log_fatal("Failed to read configuration file \"%s\"",
							  config->pathnames.config);
					exit(EXIT_CODE_BAD_CONFIG);
				}

				(void) monitor_service_run(monitor);
			}
			else
			{
				exit(EXIT_CODE_QUIT);
			}

			/*
			 * When the "main" function for the child process is over, it's the
			 * end of our execution thread. Don't get back to the caller.
			 */
			if (asked_to_stop || asked_to_stop_fast)
			{
				exit(EXIT_CODE_QUIT);
			}
			else
			{
				/* something went wrong (e.g. broken pipe) */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl installer process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
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
	pgsql_init(&postgres.sqlClient, connInfo, PGSQL_CONN_LOCAL);

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
							   pgSetup.ssl.active,
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
