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
#include <unistd.h>

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
#include "pghba.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pgtuning.h"
#include "primary_standby.h"
#include "string_utils.h"

/* Options specific to "pg_autoctl inspect pgsetup wait" */
static bool pgsetupWaitReadWrite = false;
static int pgsetupWaitTimeout = 30;


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
 * keeper_cli_pgsetup_wait_getopts parses options specific to
 * "pg_autoctl inspect pgsetup wait": --read-write, --timeout, plus the
 * standard Postgres connection options inherited from the keeper setup.
 */
int
keeper_cli_pgsetup_wait_getopts(int argc, char **argv)
{
	/*
	 * cli_common_keeper_getopts (called by keeper_cli_keeper_setup_getopts)
	 * exits with BAD_ARGS when it encounters unknown options.  Since --timeout
	 * and --read-write are not in its options list, we must remove them from
	 * argv before delegating.
	 *
	 * Strategy:
	 *   1. Scan argv with opterr=0 to capture --timeout / --read-write.
	 *   2. Build a filtered argv that omits those two options.
	 *   3. Pass the filtered argv to keeper_cli_keeper_setup_getopts.
	 */

	/* Reset module-level wait options */
	pgsetupWaitReadWrite = false;
	pgsetupWaitTimeout = 30;

	static struct option wait_options[] = {
		{ "read-write", no_argument, NULL, 'W' },
		{ "timeout", required_argument, NULL, 'T' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 1;
	opterr = 0;

	int c;
	int option_index = 0;

	while ((c = getopt_long(argc, argv, "WT:", wait_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'W':
			{
				pgsetupWaitReadWrite = true;
				break;
			}

			case 'T':
			{
				int t = strtol(optarg, NULL, 10);
				if (t <= 0)
				{
					log_error("--timeout must be a positive integer");
					exit(EXIT_CODE_BAD_ARGS);
				}
				pgsetupWaitTimeout = t;
				break;
			}

			default:
			{
				/* standard keeper options; handled by the delegated call below */
				break;
			}
		}
	}

	opterr = 1;

	/*
	 * Build a filtered argv that strips --timeout/--read-write (and their
	 * arguments) so that keeper_cli_keeper_setup_getopts does not see them.
	 */
	char **filtered_argv = (char **) palloc((argc + 1) * sizeof(char *));
	int filtered_argc = 0;

	for (int i = 0; i < argc; i++)
	{
		if (strcmp(argv[i], "--read-write") == 0 || strcmp(argv[i], "-W") == 0)
		{
			continue;
		}

		if ((strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-T") == 0) &&
			i + 1 < argc)
		{
			/* skip both the flag and its argument */
			i++;
			continue;
		}

		filtered_argv[filtered_argc++] = argv[i];
	}
	filtered_argv[filtered_argc] = NULL;

	int rc = keeper_cli_keeper_setup_getopts(filtered_argc, filtered_argv);

	pfree(filtered_argv);

	return rc;
}


/*
 * keeper_cli_pgsetup_wait_until_ready waits for the local Postgres server to
 * become ready.  When --read-write is given, it additionally waits until the
 * server is accepting read-write connections (not in recovery and not set to
 * default_transaction_read_only).
 *
 * The --timeout value (default 30s) is a single deadline shared by both
 * phases: the pg_is_ready poll and the subsequent read-write connection
 * attempt.  Time spent waiting for Postgres to start counts against the
 * budget for the read-write phase.
 */
void
keeper_cli_pgsetup_wait_until_ready(int argc, char **argv)
{
	int timeout = pgsetupWaitTimeout;

	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	/* Record wall-clock start so all phases share one deadline. */
	time_t startTime = time(NULL);

	/* Wait up to `timeout` seconds for the config file to be created.
	 * In no-monitor mode, pg_autoctl create postgres runs first and writes the
	 * config; pgsetup wait may be called before that completes. */
	{
		KeeperConfig kconfig = keeperOptions;
		if (keeper_config_set_pathnames_from_pgdata(&(kconfig.pathnames),
													kconfig.pgSetup.pgdata))
		{
			time_t deadline = startTime + timeout;
			while (!file_exists(kconfig.pathnames.config) &&
				   time(NULL) < deadline)
			{
				log_debug("Waiting for config file \"%s\" to appear",
						  kconfig.pathnames.config);
				pg_usleep(500 * 1000);
			}
		}
	}

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_debug("Initialized pgSetup, now calling pg_setup_wait_until_is_ready()");

	/*
	 * Phase 1: wait for postmaster to signal "ready" in postmaster.pid.
	 * Pass the remaining timeout so the two phases together stay within the
	 * single user-visible deadline.
	 */
	int remainingAfterConfig = timeout - (int) (time(NULL) - startTime);
	if (remainingAfterConfig <= 0)
	{
		log_error("Timed out waiting for Postgres config file to appear");
		exit(EXIT_CODE_PGSQL);
	}

	bool pgIsReady =
		pg_setup_wait_until_is_ready(pgSetup, remainingAfterConfig, LOG_INFO);

	log_info("Postgres status is: \"%s\"", pmStatusToString(pgSetup->pm_status));

	if (!pgIsReady)
	{
		exit(EXIT_CODE_PGSQL);
	}

	if (!pgsetupWaitReadWrite)
	{
		/* Plain "ready" check — we're done. */
		exit(EXIT_CODE_QUIT);
	}

	/*
	 * Phase 2: wait until the server accepts read-write connections.
	 *
	 * Postgres is up (phase 1 passed) but may still be in recovery, finishing
	 * pg_rewind, or in standby mode.  We poll with a libpq connection that
	 * checks pg_is_in_recovery() until it returns false or the deadline fires.
	 *
	 * We use the local connection string from pgSetup (Unix socket when
	 * available, matching whatever auth the node was created with) so that the
	 * check works regardless of the cluster's auth method.
	 */
	char connstr[MAXCONNINFO];
	if (!pg_setup_get_local_connection_string(pgSetup, connstr))
	{
		log_error("Failed to build local connection string for read-write check");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_info("Waiting for Postgres to accept read-write connections "
			 "(timeout %ds)", timeout);

	bool isReadWrite = false;
	int attempts = 0;

	while (!isReadWrite)
	{
		int elapsed = (int) (time(NULL) - startTime);
		int remaining = timeout - elapsed;

		if (remaining <= 0)
		{
			log_error("Timed out after %ds waiting for Postgres "
					  "to accept read-write connections", timeout);
			exit(EXIT_CODE_PGSQL);
		}

		/* Use a short per-attempt connect_timeout so we retry briskly. */
		char attemptConnstr[MAXCONNINFO];
		sformat(attemptConnstr, sizeof(attemptConnstr),
				"%s connect_timeout=1", connstr);

		PGSQL pgsql = { 0 };
		pgsql_init(&pgsql, attemptConnstr, PGSQL_CONN_LOCAL);

		bool inRecovery = true;   /* assume standby until proven otherwise */
		bool queryOk = pgsql_is_in_recovery(&pgsql, &inRecovery);
		pgsql_finish(&pgsql);

		if (queryOk && !inRecovery)
		{
			isReadWrite = true;
			break;
		}

		/* let's not be THAT verbose about it */
		if (attempts % 10 == 0)
		{
			log_debug("pgsetup wait --read-write: attempt %d, "
					  "in_recovery=%s, after %ds",
					  attempts + 1,
					  inRecovery ? "true" : "false",
					  elapsed);
		}

		++attempts;
		pg_usleep(100 * 1000);   /* 100 ms between probes */
	}

	log_info("Postgres is now accepting read-write connections on port %d",
			 pgSetup->pgport);
	exit(EXIT_CODE_QUIT);
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
 * keeper_cli_pgsetup_hba_lan appends LAN CIDR trust rules to pg_hba.conf
 * without connecting to Postgres (safe when TCP HBA hasn't been set up yet),
 * then reloads the configuration via pg_ctl.
 *
 * Intended for --skip-pg-hba setups where the operator needs to seed the
 * initial trust rules before pg_autoctl or replication can connect via TCP.
 *
 * Sequence:
 *   1. Wait up to 60 s for $PGDATA/pg_hba.conf to appear.
 *   2. Append "host all all <LAN-CIDR> trust" and
 *      "host replication all <LAN-CIDR> trust" directly to the file.
 *   3. Wait for Postgres to be running (reads postmaster.pid, no TCP).
 *   4. pg_ctl reload -D $PGDATA.
 */
void
keeper_cli_pgsetup_hba_lan(int argc, char **argv)
{
	/*
	 * Derive pgdata from command-line options directly, without calling
	 * cli_common_pgsetup_init().  cli_common_pgsetup_init() calls
	 * ProbeConfigurationFileRole() which fatals when the keeper config file
	 * does not exist yet (e.g. node still initializing).  We do not need the
	 * full pgSetup here — only pgdata, hostname, auth method, and ssl.
	 */
	const char *pgdata = keeperOptions.pgSetup.pgdata;

	if (IS_EMPTY_STRING_BUFFER(pgdata))
	{
		log_fatal("Please provide --pgdata or set PGDATA");
		exit(EXIT_CODE_BAD_ARGS);
	}

	char hbaFile[MAXPGPATH];
	sformat(hbaFile, MAXPGPATH, "%s/pg_hba.conf", pgdata);

	/* Step 1: wait for pg_hba.conf to appear (up to 60 s) */
	{
		int timeout = 60;
		time_t deadline = time(NULL) + timeout;
		while (!file_exists(hbaFile) && time(NULL) < deadline)
		{
			log_debug("Waiting for \"%s\" to appear", hbaFile);
			pg_usleep(500 * 1000);
		}
		if (!file_exists(hbaFile))
		{
			log_error("Timed out waiting for \"%s\" to appear", hbaFile);
			exit(EXIT_CODE_PGCTL);
		}
	}

	/*
	 * Step 2: determine the hostname for LAN CIDR lookups.
	 *
	 * Preference order:
	 *   a) keeper config file hostname (written early during pg_autoctl init)
	 *   b) OS hostname via gethostname() — inside a Docker container this is
	 *      the service name (e.g. "node2") which resolves correctly on the LAN
	 *   c) keeperOptions.pgSetup.pghost (last resort; may be a Unix socket
	 *      path like /var/run/postgresql that does not resolve as a hostname)
	 *
	 * The keeper config file is created before postgres initialises, so it is
	 * normally available by the time pg_hba.conf appears.  All three options
	 * are tried so the command works even when called very early.
	 */
	char hostname[_POSIX_HOST_NAME_MAX] = "";

	/* option a: keeper config */
	{
		KeeperConfig hbaConfig = keeperOptions;
		if (keeper_config_set_pathnames_from_pgdata(&hbaConfig.pathnames,
													pgdata) &&
			keeper_config_read_file(&hbaConfig,
									false /* missingPgdataIsOk */,
									true /* pgIsNotRunningIsOk */,
									true /* monitorDisabledIsOk */) &&
			!IS_EMPTY_STRING_BUFFER(hbaConfig.hostname))
		{
			strlcpy(hostname, hbaConfig.hostname, sizeof(hostname));
		}
	}

	/* option b: OS hostname */
	if (IS_EMPTY_STRING_BUFFER(hostname))
	{
		if (gethostname(hostname, sizeof(hostname)) == 0)
		{
			log_debug("hba-lan: using OS hostname \"%s\"", hostname);
		}
		else
		{
			hostname[0] = '\0';
		}
	}

	/* option c: pghost (may be a socket path — last resort) */
	if (IS_EMPTY_STRING_BUFFER(hostname))
	{
		strlcpy(hostname, keeperOptions.pgSetup.pghost, sizeof(hostname));
	}

	/* --auth <method> defaults to "trust"; --ssl enables hostssl rules */
	const char *authMethod = keeperOptions.pgSetup.authMethod;
	if (IS_EMPTY_STRING_BUFFER(authMethod))
	{
		authMethod = "trust";
	}
	bool useSSL = keeperOptions.pgSetup.ssl.active;

	/* cert auth for replication needs an ident map */
	bool isCert = (strcmp(authMethod, "cert") == 0);
	const char *replAuth = isCert ? "cert map=pgautofailover" : authMethod;

	if (!pghba_enable_lan_cidr(NULL, useSSL,
							   HBA_DATABASE_ALL, NULL,
							   hostname, NULL, authMethod,
							   HBA_EDIT_MINIMAL,
							   pgdata))
	{
		log_error("Failed to add LAN CIDR HBA rule for \"all\" databases");
		exit(EXIT_CODE_PGCTL);
	}

	if (!pghba_enable_lan_cidr(NULL, useSSL,
							   HBA_DATABASE_REPLICATION, NULL,
							   hostname, NULL, replAuth,
							   HBA_EDIT_MINIMAL,
							   pgdata))
	{
		log_error("Failed to add LAN CIDR HBA rule for replication");
		exit(EXIT_CODE_PGCTL);
	}

	/* cert auth requires an ident map entry in pg_ident.conf */
	if (isCert)
	{
		if (!pghba_ensure_ident_map_entry(pgdata,
										  "pgautofailover",
										  PG_AUTOCTL_MONITOR_USERNAME,
										  PG_AUTOCTL_REPLICA_USERNAME))
		{
			log_error("Failed to add cert ident map entry to pg_ident.conf");
			exit(EXIT_CODE_PGCTL);
		}
	}

	/* Step 3: wait for Postgres to be running (PID file, no TCP needed) */
	if (!pg_setup_wait_until_is_ready(&keeperOptions.pgSetup, 60, LOG_INFO))
	{
		log_error("Postgres did not become ready within 60 s");
		exit(EXIT_CODE_PGCTL);
	}

	/* Step 4: reload so the new HBA rules take effect */
	log_info("Reloading Postgres configuration in \"%s\"", pgdata);
	if (!pg_ctl_reload(keeperOptions.pgSetup.pg_ctl, pgdata))
	{
		exit(EXIT_CODE_PGCTL);
	}
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
