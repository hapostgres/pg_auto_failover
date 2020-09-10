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

typedef struct TmuxOptions
{
	char root[MAXPGPATH];
	int firstPort;
	int nodes;
} TmuxOptions;

static TmuxOptions tmuxOptions = { 0 };


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
	int connlimit = 1;

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
									  PG_AUTOCTL_HEALTH_USERNAME,
									  PG_AUTOCTL_HEALTH_PASSWORD,
									  monitorHostname,
									  "trust",
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
 * keeper_cli_pgsetup_discover implements the CLI to discover a PostgreSQL
 * setup thanks to PGDATA and other environment variables.
 */
void
keeper_cli_pgsetup_discover(int argc, char **argv)
{
	bool missingPgdataOk = true;
	PostgresSetup pgSetup = { 0 };

	if (!pg_setup_init(&pgSetup, &keeperOptions.pgSetup, true, true))
	{
		exit(EXIT_CODE_PGCTL);
	}

	if (!pg_controldata(&pgSetup, missingPgdataOk))
	{
		exit(EXIT_CODE_PGCTL);
	}

	if (!IS_EMPTY_STRING_BUFFER(keeperOptions.hostname))
	{
		fformat(stdout, "Node Name:          %s\n", keeperOptions.hostname);
	}

	fprintf_pg_setup(stdout, &pgSetup);
}


/*
 * keeper_cli_pgsetup_is_ready returns success when the local PostgreSQL setup
 * belongs to a server that is "ready".
 */
void
keeper_cli_pgsetup_is_ready(int argc, char **argv)
{
	PostgresSetup pgSetup = { 0 };
	bool pgIsReady = false;
	bool pgIsNotRunningIsOk = false;

	if (!pg_setup_init(&pgSetup, &keeperOptions.pgSetup, true, true))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_debug("Initialized pgSetup, now calling pg_setup_is_ready()");

	pgIsReady = pg_setup_is_ready(&pgSetup, pgIsNotRunningIsOk);

	log_info("Postgres status is: \"%s\"", pmStatusToString(pgSetup.pm_status));

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
	PostgresSetup pgSetup = { 0 };
	bool pgIsReady = false;

	if (!pg_setup_init(&pgSetup, &keeperOptions.pgSetup, true, true))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_debug("Initialized pgSetup, now calling pg_setup_wait_until_is_ready()");

	pgIsReady = pg_setup_wait_until_is_ready(&pgSetup, timeout, LOG_INFO);

	log_info("Postgres status is: \"%s\"", pmStatusToString(pgSetup.pm_status));

	if (pgIsReady)
	{
		exit(EXIT_CODE_QUIT);
	}
	exit(EXIT_CODE_PGSQL);
}


/*
 * keeper_cli_pgsetup_startup_logs logs the Postgre startup logs.
 */
void
keeper_cli_pgsetup_startup_logs(int argc, char **argv)
{
	PostgresSetup pgSetup = { 0 };

	if (!pg_setup_init(&pgSetup, &keeperOptions.pgSetup, true, true))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_debug("Initialized pgSetup, now calling pg_log_startup()");

	if (!pg_log_startup(pgSetup.pgdata, LOG_INFO))
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
	int hostLength = 0;

	if (argc != 2)
	{
		commandline_print_usage(&do_standby_init, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	hostLength = strlcpy(postgres.replicationSource.primaryNode.host, argv[0],
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
	int hostLength = 0;

	KeeperConfig config = keeperOptions;
	LocalPostgresServer postgres = { 0 };

	if (argc < 1 || argc > 2)
	{
		commandline_print_usage(&do_standby_rewind, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);
	local_postgres_init(&postgres, &(config.pgSetup));

	hostLength = strlcpy(postgres.replicationSource.primaryNode.host, argv[0],
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
	int hostLength = 0;

	if (argc != 2)
	{
		commandline_print_usage(&do_primary_identify_system, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeper_config_init(&config, missing_pgdata_is_ok, pg_not_running_is_ok);

	hostLength = strlcpy(replicationSource.primaryNode.host, argv[0],
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
}


/*
 * cli_print_version_getopts parses the CLI options for the pg_autoctl version
 * command, which are the usual suspects.
 */
int
cli_do_tmux_script_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	TmuxOptions options = { 0 };

	static struct option long_options[] = {
		{ "root", required_argument, NULL, 'D' },
		{ "first-port", required_argument, NULL, 'p' },
		{ "nodes", required_argument, NULL, 'n' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set our defaults */
	options.nodes = 2;
	options.firstPort = 5500;
	strlcpy(options.root, "/tmp/pgaf/tmux", MAXPGPATH);

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.root, optarg, MAXPGPATH);
				log_trace("--root %s", options.root);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &options.firstPort))
				{
					log_error("Failed to parse --first-port number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--first-port %d", options.firstPort);
				break;
			}

			case 'n':
			{
				if (!stringToInt(optarg, &options.nodes))
				{
					log_error("Failed to parse --nodes number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--nodes %d", options.nodes);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				printVersion = true;
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/* publish parsed options */
	tmuxOptions = options;

	return optind;
}


/*
 * tmux_add_command appends a tmux command to the given script buffer.
 */
static void
tmux_add_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[BUFSIZE] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "%s\n", buffer);
}


/*
 * tmux_add_send_keys_command appends a tmux send-keys command to the given
 * script buffer, with an additional Enter command.
 */
static void
tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[BUFSIZE] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "send-keys '%s' Enter\n", buffer);
}


/*
 * tmux_add_xdg_environment appends the XDG environment that makes the test
 * target self-contained, as a series of tmux send-keys commands, to the given
 * script buffer.
 */
static void
tmux_add_xdg_environment(PQExpBuffer script, const char *root)
{
	char *xdg[][3] = {
		{ "XDG_DATA_HOME", "share" },
		{ "XDG_CONFIG_HOME", "config" },
		{ "XDG_RUNTIME_DIR", "run" },
	};

	/*
	 * For demo/tests purposes, arrange a self-contained setup where everything
	 * is to be found in the given options.root directory.
	 */
	for (int i = 0; i < 3; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];

		tmux_add_send_keys_command(script,
								   "export %s=\"%s/%s\"", var, root, dir);
	}
}


/*
 * tmux_pg_autoctl_create appends a pg_autoctl create command to the given
 * script buffer, and also the commands to set PGDATA and PGPORT.
 */
static void
tmux_pg_autoctl_create(PQExpBuffer script,
					   const char *root,
					   int pgport,
					   const char *role,
					   const char *name)
{
	char *pg_ctl_opts = "--hostname localhost --ssl-self-signed --auth trust";

	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "export PGPORT=%d", pgport);

	/* the monitor is always named monitor, and does not need --monitor */
	if (strcmp(role, "monitor") == 0)
	{
		tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

		tmux_add_send_keys_command(script,
								   "%s create %s %s --run",
								   pg_autoctl_argv0,
								   role,
								   pg_ctl_opts);
	}
	else
	{
		char monitor[BUFSIZE] = { 0 };

		sformat(monitor, sizeof(monitor),
				"$(%s show uri --pgdata %s/monitor --monitor)",
				pg_autoctl_argv0,
				root);

		tmux_add_send_keys_command(script,
								   "export PGDATA=\"%s/%s\"",
								   root,
								   name);

		tmux_add_send_keys_command(script,
								   "%s create %s %s --monitor %s --run",
								   pg_autoctl_argv0,
								   role,
								   pg_ctl_opts,
								   monitor);
	}
}


/*
 * keeper_cli_tmux_script generates a tmux script to run a test case or a demo
 * for pg_auto_failover easily.
 */
void
cli_do_tmux_script(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;

	char *root = options.root;
	PQExpBuffer script = createPQExpBuffer();

	int pgport = options.firstPort;

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	tmux_add_command(script, "set-option -g default-shell /bin/bash");
	tmux_add_command(script, "new -s pgautofailover");

	/* start a monitor */
	tmux_pg_autoctl_create(script, root, pgport++, "monitor", "monitor");

	/* start the Postgres nodes, using the monitor URI */
	for (int i = 0; i < options.nodes; i++)
	{
		char name[NAMEDATALEN] = { 0 };

		sformat(name, sizeof(name), "node%d", i + 1);

		tmux_add_command(script, "split-window -v");
		tmux_add_command(script, "select-layout even-vertical");

		/* allow some time for each previous node to be ready */
		tmux_add_send_keys_command(script, "sleep %d", 3 * (i + 1));
		tmux_pg_autoctl_create(script, root, pgport++, "postgres", name);
		tmux_add_send_keys_command(script, "pg_autoctl run");
	}

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);
	tmux_add_send_keys_command(script,
							   "watch -n 0.2 %s show state",
							   pg_autoctl_argv0,
							   options.root);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s\n", script->data);
	destroyPQExpBuffer(script);
}
