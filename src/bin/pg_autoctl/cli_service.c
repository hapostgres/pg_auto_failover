/*
 * src/bin/pg_autoctl/cli_service.c
 *     Implementation of a CLI for controlling the pg_autoctl service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "signals.h"

static int stop_signal = SIGTERM;

static void cli_service_run(int argc, char **argv);
static void cli_keeper_run(int argc, char **argv);
static void cli_monitor_run(int argc, char **argv);

static int cli_getopt_pgdata_and_mode(int argc, char **argv);

static void cli_service_reload(int argc, char **argv);
static void cli_service_stop(int argc, char **argv);

CommandLine service_run_command =
	make_command("run",
				 "Run the pg_autoctl service (monitor or keeper)",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_service_run);

CommandLine service_stop_command =
	make_command("stop",
				 "signal the pg_autoctl service for it to stop",
				 " [ --pgdata --fast --immediate ]",
				 "  --pgdata      path to data director \n"
				 "  --fast        fast shutdown mode for the keeper \n"
				 "  --immediate   immediate shutdown mode for the keeper \n",
				 cli_getopt_pgdata_and_mode,
				 cli_service_stop);

CommandLine service_reload_command =
	make_command("reload",
				 "signal the pg_autoctl for it to reload its configuration",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_service_reload);


/*
 * cli_service_run starts the local pg_auto_failover service, either the
 * monitor or the keeper, depending on the configuration file associated with
 * the current PGDATA, or the --pgdata argument.
 */
static void
cli_service_run(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
												 config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
			(void) cli_monitor_run(argc, argv);
			break;

		case PG_AUTOCTL_ROLE_KEEPER:
			(void) cli_keeper_run(argc, argv);
			break;

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * keeper_cli_fsm_run runs the keeper state machine in an infinite
 * loop.
 */
static void
cli_keeper_run(int argc, char **argv)
{
	Keeper keeper = { 0 };
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	pid_t pid = 0;

	keeper.config = keeperOptions;

	/*
	 * keeper_config_read_file() calls into pg_setup_init() which uses
	 * pg_setup_is_ready(), and this function might loop until Postgres is
	 * ready, as per its name.
	 *
	 * In order to make it possible to interrupt the pg_autctl service while in
	 * that loop, we need to install our signal handlers and pidfile prior to
	 * getting there.
	 */
	if (!keeper_service_init(&keeper, &pid))
	{
		log_fatal("Failed to initialize pg_auto_failover service, "
				  "see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	keeper_config_read_file(&(keeper.config),
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	if (!keeper_check_monitor_extension_version(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	keeper_service_run(&keeper, &pid);
}


/*
 * cli_monitor_run ensures PostgreSQL is running and then listens for state
 * changes from the monitor, logging them as INFO messages. Also listens for
 * log messages from the monitor, and outputs them as DEBUG messages.
 */
static void
cli_monitor_run(int argc, char **argv)
{
	KeeperConfig kconfig = keeperOptions;
	MonitorConfig mconfig = { 0 };
	Monitor monitor = { 0 };
	MonitorExtensionVersion version = { 0 };
	char postgresUri[MAXCONNINFO];
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;
	char connInfo[MAXCONNINFO];
	char *channels[] = { "log", "state", NULL };

	if (!monitor_config_init_from_pgsetup(&monitor,
										  &mconfig,
										  &kconfig.pgSetup,
										  missingPgdataIsOk,
										  pgIsNotRunningIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGCTL);
	}

	pg_setup_get_local_connection_string(&(mconfig.pgSetup), connInfo);
	monitor_init(&monitor, connInfo);

	if (!pg_is_running(mconfig.pgSetup.pg_ctl, mconfig.pgSetup.pgdata))
	{
		log_info("Postgres is not running, starting postgres");

		if (!pg_ctl_start(mconfig.pgSetup.pg_ctl,
						  mconfig.pgSetup.pgdata,
						  mconfig.pgSetup.pgport,
						  mconfig.pgSetup.listen_addresses))
		{
			log_error("Failed to start PostgreSQL, see above for details");
			exit(EXIT_CODE_PGCTL);
		}
	}

	log_info("PostgreSQL is running in \"%s\" on port %d",
			 mconfig.pgSetup.pgdata, mconfig.pgSetup.pgport);

	/* Check version compatibility */
	if (!monitor_ensure_extension_version(&monitor, &version))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	/* Now get the the Monitor URI to display it to the user, and move along */
	if (monitor_config_get_postgres_uri(&mconfig, postgresUri, MAXCONNINFO))
	{
		log_info("pg_auto_failover monitor is ready at %s", postgresUri);
	}

	log_info("Contacting the monitor to LISTEN to its events.");
 	pgsql_listen(&(monitor.pgsql), channels);

	/*
	 * Main loop for notifications.
	 */
	for (;;)
	{
		if (!monitor_get_notifications(&monitor))
		{
			log_warn("Re-establishing connection. We might miss notifications.");
			pgsql_finish(&(monitor.pgsql));

			pgsql_listen(&(monitor.pgsql), channels);

			/* skip sleeping */
			continue;
		}

		sleep(PG_AUTOCTL_MONITOR_SLEEP_TIME);
	}
	pgsql_finish(&(monitor.pgsql));

	return;
}


/*
 * service_cli_reload sends a SIGHUP signal to the keeper.
 */
static void
cli_service_reload(int argc, char **argv)
{
	pid_t pid;
	Keeper keeper = { 0 };

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		if (kill(pid, SIGHUP) != 0)
		{
			log_error("Failed to send SIGHUP to the keeper's pid %d: %s",
					  pid, strerror(errno));
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * cli_getopt_pgdata_and_mode gets both the --pgdata and the stopping mode
 * options (either --fast or --immediate) from the command line.
 */
static int
cli_getopt_pgdata_and_mode(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "fast", no_argument, NULL, 'f' },
		{ "immediate", no_argument, NULL, 'i' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:fi",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'f':
			{
				/* change the signal to send from SIGTERM to SIGINT. */
				if (stop_signal != SIGTERM)
				{
					log_fatal("Please use either --fast or --immediate, not both");
					exit(EXIT_CODE_BAD_ARGS);
				}
				stop_signal = SIGINT;
				break;
			}

			case 'i':
			{
				/* change the signal to send from SIGTERM to SIGQUIT. */
				if (stop_signal != SIGTERM)
				{
					log_fatal("Please use either --fast or --immediate, not both");
					exit(EXIT_CODE_BAD_ARGS);
				}
				stop_signal = SIGQUIT;
				break;
			}

			default:
			{
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to get PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	log_info("Managing PostgreSQL installation at \"%s\"",
			 options.pgSetup.pgdata);

	if (!keeper_config_set_pathnames_from_pgdata(&options.pathnames, options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_service_stop sends a SIGTERM signal to the keeper.
 */
static void
cli_service_stop(int argc, char **argv)
{
	pid_t pid;
	Keeper keeper = { 0 };

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		if (kill(pid, stop_signal) != 0)
		{
			log_error("Failed to send %s to the keeper's pid %d: %s",
					  strsignal(stop_signal), pid, strerror(errno));
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		log_fatal("Failed to read the keeper's PID at \"%s\"",
				  keeper.config.pathnames.pid);
	}
}
