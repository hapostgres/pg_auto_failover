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
#include "pqexpbuffer.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgctl.h"
#include "pgtuning.h"
#include "primary_standby.h"
#include "string_utils.h"


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
 * keeper_cli_drop_replication_slot implements the CLI to drop a replication
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
 * keeper_cli_add_defaults implements the CLI to add pg_auto_failover default
 * settings to postgresql.conf
 */
void
keeper_cli_add_default_settings(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	local_postgres_init(&postgres, &(config.pgSetup));

	if (!postgres_add_default_settings(&postgres, config.hostname))
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
	char monitorHostname[_POSIX_HOST_NAME_MAX];
	int monitorPort = 0;
	int connlimit = 1;

	keeper_config_init(&config, missingPgdataOk, postgresNotRunningOk);
	local_postgres_init(&postgres, &(config.pgSetup));

	int urlLength = strlcpy(config.monitor_pguri, argv[0], MAXCONNINFO);
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
									  PG_AUTOCTL_HEALTH_USERNAME,
									  PG_AUTOCTL_HEALTH_PASSWORD,
									  monitorHostname,
									  "trust",
									  HBA_EDIT_MINIMAL,
									  connlimit))
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
 * keeper_cli_pgsetup_pg_ctl implements the CLI to find a suitable pg_ctl entry
 * from either the PG_CONFIG environment variable, or the PATH, then either
 * finding a single pg_ctl entry or falling back to a single pg_config entry
 * that we then use with pg_config --bindir.
 */
void
keeper_cli_pgsetup_pg_ctl(int argc, char **argv)
{
	bool success = true;

	PostgresSetup pgSetupMonitor = { 0 }; /* find first entry */
	PostgresSetup pgSetupKeeper = { 0 };  /* find non ambiguous entry */

	char PG_CONFIG[MAXPGPATH] = { 0 };

	if (env_exists("PG_CONFIG") &&
		get_env_copy("PG_CONFIG", PG_CONFIG, sizeof(PG_CONFIG)))
	{
		log_info("Environment variable PG_CONFIG is set to \"%s\"", PG_CONFIG);
	}

	if (config_find_pg_ctl(&pgSetupKeeper))
	{
		log_info("`pg_autoctl create postgres` would use \"%s\" for Postgres %s",
				 pgSetupKeeper.pg_ctl, pgSetupKeeper.pg_version);
	}
	else
	{
		log_fatal("pg_autoctl create postgres would fail to find pg_ctl");
		success = false;
	}

	/*
	 * This function EXITs when it's not happy, so we do it last:
	 */
	(void) set_first_pgctl(&pgSetupMonitor);

	log_info("`pg_autoctl create monitor` would use \"%s\" for Postgres %s",
			 pgSetupMonitor.pg_ctl, pgSetupMonitor.pg_version);

	/*
	 * Now check that find_extension_control_file would be happy.
	 */
	if (find_extension_control_file(pgSetupMonitor.pg_ctl,
									PG_AUTOCTL_MONITOR_EXTENSION_NAME))
	{
		log_info("Found the control file for extension \"%s\"",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME);
	}
	else
	{
		log_fatal("pg_autoctl on the monitor would fail "
				  "to find extension \"%s\"",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		success = false;
	}

	if (!success)
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * keeper_cli_pgsetup_discover implements the CLI to discover a PostgreSQL
 * setup thanks to PGDATA and other environment variables.
 */
void
keeper_cli_pgsetup_discover(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	bool missingPgdataOk = true;

	if (!pg_controldata(pgSetup, missingPgdataOk))
	{
		exit(EXIT_CODE_PGCTL);
	}

	if (!IS_EMPTY_STRING_BUFFER(keeperOptions.hostname))
	{
		fformat(stdout, "Node Name:          %s\n", keeperOptions.hostname);
	}

	fprintf_pg_setup(stdout, pgSetup);
}


/*
 * keeper_cli_pgsetup_is_ready returns success when the local PostgreSQL setup
 * belongs to a server that is "ready".
 */
void
keeper_cli_pgsetup_is_ready(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_debug("Initialized pgSetup, now calling pg_setup_is_ready()");

	bool pgIsNotRunningIsOk = false;
	bool pgIsReady = pg_setup_is_ready(pgSetup, pgIsNotRunningIsOk);

	log_info("Postgres status is: \"%s\"", pmStatusToString(pgSetup->pm_status));

	if (pgIsReady)
	{
		exit(EXIT_CODE_QUIT);
	}
	exit(EXIT_CODE_PGSQL);
}


/*
 * keeper_cli_discover_pg_setup implements the CLI to discover a PostgreSQL
 * setup thanks to PGDATA and other environment variables.
 */
void
keeper_cli_pgsetup_wait_until_ready(int argc, char **argv)
{
	int timeout = 30;

	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_debug("Initialized pgSetup, now calling pg_setup_wait_until_is_ready()");

	bool pgIsReady = pg_setup_wait_until_is_ready(pgSetup, timeout, LOG_INFO);

	log_info("Postgres status is: \"%s\"", pmStatusToString(pgSetup->pm_status));

	if (pgIsReady)
	{
		exit(EXIT_CODE_QUIT);
	}
	exit(EXIT_CODE_PGSQL);
}


/*
 * keeper_cli_pgsetup_startup_logs logs the Postgres startup logs.
 */
void
keeper_cli_pgsetup_startup_logs(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_debug("Initialized pgSetup, now calling pg_log_startup()");

	if (!pg_log_startup(pgSetup->pgdata, LOG_INFO))
	{
		exit(EXIT_CODE_PGCTL);
	}
}


/*
 * keeper_cli_pgsetup_tune compute some Postgres tuning for the local system.
 */
void
keeper_cli_pgsetup_tune(int argc, char **argv)
{
	char config[BUFSIZE] = { 0 };

	if (!pgtuning_prepare_guc_settings(postgres_tuning, config, BUFSIZE))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s\n", config);
}


/*
 * keeper_cli_init_standby initializes a standby
 */
void
keeper_cli_init_standby(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = true;
	const bool pg_not_running_is_ok = true;
	const bool skipBaseBackup = false;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	if (argc != 2)
	{
		commandline_print_usage(&do_standby_init, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	int hostLength = strlcpy(postgres.replicationSource.primaryNode.host, argv[0],
							 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!stringToInt(argv[1], &(postgres.replicationSource.primaryNode.port)))
	{
		log_fatal("Argument is not a valid port number: \"%s\"", argv[1]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!standby_init_replication_source(&postgres,
										 NULL, /* primaryNode is done */
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config.replication_password,
										 config.replication_slot_name,
										 config.maximum_backup_rate,
										 config.backupDirectory,
										 NULL, /* no targetLSN */
										 config.pgSetup.ssl,
										 0))
	{
		/* can't happen at the moment */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!standby_init_database(&postgres, config.hostname, skipBaseBackup))
	{
		log_fatal("Failed to grant access to the standby by adding "
				  "relevant lines to pg_hba.conf for the "
				  "standby hostname and user, see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


void
keeper_cli_rewind_old_primary(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = false;
	const bool pg_not_running_is_ok = true;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	if (argc < 1 || argc > 2)
	{
		commandline_print_usage(&do_standby_rewind, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	int hostLength = strlcpy(postgres.replicationSource.primaryNode.host, argv[0],
							 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, MAXCONNINFO - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!stringToInt(argv[1], &(postgres.replicationSource.primaryNode.port)))
	{
		log_fatal("Argument is not a valid port number: \"%s\"", argv[1]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!standby_init_replication_source(&postgres,
										 NULL, /* primaryNode is done */
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config.replication_password,
										 config.replication_slot_name,
										 config.maximum_backup_rate,
										 config.backupDirectory,
										 NULL, /* no targetLSN */
										 config.pgSetup.ssl,
										 0))
	{
		/* can't happen at the moment */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!primary_rewind_to_standby(&postgres))
	{
		log_fatal("Failed to rewind a demoted primary to standby, "
				  "see above for details");
		exit(EXIT_CODE_PGSQL);
	}
}


void
keeper_cli_maybe_do_crash_recovery(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = false;
	const bool pg_not_running_is_ok = true;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	if (!standby_init_replication_source(&postgres,
										 NULL, /* primaryNode is done */
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config.replication_password,
										 config.replication_slot_name,
										 config.maximum_backup_rate,
										 config.backupDirectory,
										 NULL, /* no targetLSN */
										 config.pgSetup.ssl,
										 0))
	{
		/* can't happen at the moment */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!postgres_maybe_do_crash_recovery(&postgres))
	{
		log_fatal("Failed to implement postgres crash recovery, "
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


/*
 * keeper_cli_identify_system connects to a Postgres server using the
 * replication protocol to run the IDENTIFY_SYSTEM command.
 *
 * The IDENTIFY_SYSTEM replication command requests the server to identify
 * itself. We use this command mostly to ensure that we can establish a
 * replication connection to the upstream/primary server, which means that the
 * HBA setup is good to go.
 *
 * See https://www.postgresql.org/docs/12/protocol-replication.html for more
 * information about the replication protocol and commands.
 */
void
keeper_cli_identify_system(int argc, char **argv)
{
	const bool missing_pgdata_is_ok = true;
	const bool pg_not_running_is_ok = true;

	KeeperConfig config = keeperOptions;
	ReplicationSource replicationSource = { 0 };

	if (argc != 2)
	{
		commandline_print_usage(&do_primary_identify_system, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);

	int hostLength = strlcpy(replicationSource.primaryNode.host, argv[0],
							 _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_fatal("Hostname \"%s\" given in command line is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  argv[0], hostLength, _POSIX_HOST_NAME_MAX - 1);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!stringToInt(argv[1], &(replicationSource.primaryNode.port)))
	{
		log_fatal("Argument is not a valid port number: \"%s\"", argv[1]);
		exit(EXIT_CODE_BAD_ARGS);
	}

	strlcpy(replicationSource.applicationName, "pg_autoctl", MAXCONNINFO);
	strlcpy(replicationSource.userName, PG_AUTOCTL_REPLICA_USERNAME, NAMEDATALEN);

	if (!pgctl_identify_system(&replicationSource))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	IdentifySystem *system = &(replicationSource.system);

	fformat(stdout, "Current timeline:  %d\n", system->timeline);
	fformat(stdout, "Current WAL LSN:   %s\n", system->xlogpos);

	for (int index = 0; index < system->timelines.count; index++)
	{
		TimeLineHistoryEntry *entry = &(system->timelines.history[index]);

		char startLSN[PG_LSN_MAXLENGTH] = { 0 };

		sformat(startLSN, sizeof(startLSN), "%X/%X",
				(uint32_t) (entry->begin >> 32),
				(uint32_t) entry->begin);

		fformat(stdout, "Timeline %d:   %18s .. %X/%X\n",
				entry->tli,
				startLSN,
				(uint32_t) (entry->end >> 32),
				(uint32_t) entry->end);
	}
}
