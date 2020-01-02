/*
 * src/bin/pg_autoctl/cli_do_misc.c
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "commandline.h"
#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgctl.h"
#include "primary_standby.h"


/*
 * keeper_cli_create_replication_slot implements the CLI to create a replication
 * slot on the primary.
 */
void
keeper_cli_create_replication_slot(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool pgNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, pgNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!primary_create_replication_slot(&postgres, config.replication_slot_name))
	{
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_cli_create_replication_slot implements the CLI to drop a replication
 * slot on the primary.
 */
void
keeper_cli_drop_replication_slot(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool pgNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, pgNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!primary_drop_replication_slot(&postgres, config.replication_slot_name))
	{
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_cli_enable_synchronous_replication implements the CLI to enable
 * synchronous replication on the primary.
 */
void
keeper_cli_enable_synchronous_replication(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool pgNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, pgNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!primary_enable_synchronous_replication(&postgres))
	{
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_cli_disable_synchronous_replication implements the CLI to disable
 * synchronous replication on the primary.
 */
void
keeper_cli_disable_synchronous_replication(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool pgNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, pgNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!primary_disable_synchronous_replication(&postgres))
	{
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_cli_add_defaults implements the CLI to add pg_auto_failover default
 * settings to postgresql.conf
 */
void
keeper_cli_add_default_settings(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool postgresNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, postgresNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!postgres_add_default_settings(&postgres))
	{
		log_fatal("Failed to add the default settings for streaming replication "
				  "used by pg_auto_failover to postgresql.conf, "
				  "see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_create_monitor_user implements the CLI to add a user for the
 * pg_auto_failover monitor.
 */
void
keeper_cli_create_monitor_user(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool postgresNotRunningOk = false;
	int urlLength = 0;
	char monitorHostname[_POSIX_HOST_NAME_MAX];
	int monitorPort = 0;

	/*
	 * Monitor does not use a password, we expect it to login and immediately
	 * disconnect.
	 */
	char *password = NULL;

	keeper_config_init(&config, missingPgdataOk, postgresNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	urlLength = strlcpy(config.monitor_pguri, argv[0], MAXCONNINFO);
	if (urlLength >= MAXCONNINFO)
	{
		log_fatal("Monitor URL \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], urlLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!hostname_from_uri(config.monitor_pguri,
						   monitorHostname, _POSIX_HOST_NAME_MAX,
						   &monitorPort))
	{
		log_fatal("Failed to determine monitor hostname");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!primary_create_user_with_hba(&postgres,
									  PG_AUTOCTL_HEALTH_USERNAME, password,
									  monitorHostname,
									  pg_setup_get_auth_method(&(config.pgSetup))))
	{
		log_fatal("Failed to create the database user that the pg_auto_failover "
				  " monitor uses for health checks, see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_create_replication_user implements the CLI to add a user for the
 * secondary.
 */
void
keeper_cli_create_replication_user(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	bool missingPgdataOk = false;
	bool postgresNotRunningOk = false;

	keeper_config_init(&config, missingPgdataOk, postgresNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!primary_create_replication_user(&postgres, PG_AUTOCTL_REPLICA_USERNAME,
										 config.replication_password))
	{
		log_fatal("Failed to create the database user that a pg_auto_failover "
				  " standby uses for replication, see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_add_standby_to_hba implements the CLI to add the pg_auto_failover
 * replication user to pg_hba.
 */
void
keeper_cli_add_standby_to_hba(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	char standbyHostname[_POSIX_HOST_NAME_MAX];
	bool missingPgdataOk = false;
	bool postgresNotRunningOk = false;
	int hostLength = 0;

	keeper_config_init(&config, missingPgdataOk, postgresNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (argc != 1)
	{
		log_error("a standby hostname is required");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	hostLength = strlcpy(standbyHostname, argv[0],
						 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!primary_add_standby_to_hba(&postgres, standbyHostname, config.replication_password))
	{
		log_fatal("Failed to grant access to the standby by adding relevant lines to "
				  "pg_hba.conf for the standby hostname and user, see above for "
				  "details");
		exit(EXIT_CODE_PGSQL);
	}
}


/*
 * keeper_cli_discover_pg_setup implements the CLI to discover a PostgreSQL
 * setup thanks to PGDATA and other environment variables.
 */
void
keeper_cli_discover_pg_setup(int argc, char **argv)
{
	PostgresSetup pgSetup = { 0 };

	if (!pg_setup_init(&pgSetup, &keeperOptions.pgSetup, true, true))
	{
		exit(EXIT_CODE_PGCTL);
	}

	if (!IS_EMPTY_STRING_BUFFER(keeperOptions.nodename))
	{
		fprintf(stdout, "Node Name:          %s\n", keeperOptions.nodename);
	}

	fprintf_pg_setup(stdout, &pgSetup);
}


void
keeper_cli_init_standby(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = true;
	const bool pg_not_running_is_ok = true;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	ReplicationSource replicationSource = { 0 };

	int hostLength = 0;

	if (argc != 2)
	{
		commandline_print_usage(&do_standby_init, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	hostLength = strlcpy(replicationSource.primaryNode.host, argv[0],
						 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (sscanf(argv[1], "%d", &replicationSource.primaryNode.port) == 0)
	{
		log_fatal("Argument is not a valid port number: \"%s\"", argv[1]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	replicationSource.userName = PG_AUTOCTL_REPLICA_USERNAME;
	replicationSource.password = config.replication_password;
	replicationSource.slotName = config.replication_slot_name;
	replicationSource.maximumBackupRate = MAXIMUM_BACKUP_RATE;
	replicationSource.backupDir = config.backupDirectory;

	if (!standby_init_database(&postgres, &replicationSource))
	{
		log_fatal("Failed to grant access to the standby by adding relevant lines to "
				  "pg_hba.conf for the standby hostname and user, see above for "
				  "details");
		exit(EXIT_CODE_PGSQL);
	}
}


void
keeper_cli_rewind_old_primary(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = false;
	const bool pg_not_running_is_ok = true;
	int hostLength = 0;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };
	ReplicationSource replicationSource = { 0 };

	if (argc < 1 || argc > 2)
	{
		commandline_print_usage(&do_standby_rewind, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	hostLength = strlcpy(replicationSource.primaryNode.host, argv[0],
						 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (sscanf(argv[1], "%d", &replicationSource.primaryNode.port) == 0)
	{
		log_fatal("Argument is not a valid port number: \"%s\"", argv[1]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	replicationSource.userName = PG_AUTOCTL_REPLICA_USERNAME;
	replicationSource.password = config.replication_password;
	replicationSource.slotName = config.replication_slot_name;
	replicationSource.maximumBackupRate = MAXIMUM_BACKUP_RATE;

	if (!primary_rewind_to_standby(&postgres, &replicationSource))
	{
		log_fatal("Failed to rewind a demoted primary to standby, "
				  "see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


void
keeper_cli_promote_standby(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = false;
	const bool pg_not_running_is_ok = false;
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!standby_promote(&postgres))
	{
		log_fatal("Failed to promote a standby to primary, see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}



