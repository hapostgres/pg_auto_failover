/*
 * src/bin/pg_autoctl/cli_do_service.c
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
#include <sys/types.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pidfile.h"
#include "service_keeper.h"
#include "service_monitor.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "supervisor.h"

static void cli_do_service_postgres(int argc, char **argv);
static void cli_do_service_pgcontroller(int argc, char **argv);
static void cli_do_service_postgresctl_on(int argc, char **argv);
static void cli_do_service_postgresctl_off(int argc, char **argv);

static void cli_do_service_getpid(const char *serviceName);
static void cli_do_service_getpid_postgres(int argc, char **argv);
static void cli_do_service_getpid_listener(int argc, char **argv);
static void cli_do_service_getpid_node_active(int argc, char **argv);

static void cli_do_service_restart(const char *serviceName);
static void cli_do_service_restart_postgres(int argc, char **argv);
static void cli_do_service_restart_listener(int argc, char **argv);
static void cli_do_service_restart_node_active(int argc, char **argv);

static void cli_do_service_monitor_listener(int argc, char **argv);
static void cli_do_service_node_active(int argc, char **argv);

CommandLine service_pgcontroller =
	make_command("pgcontroller",
				 "pg_autoctl supervised postgres controller",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_pgcontroller);

CommandLine service_postgres =
	make_command("postgres",
				 "pg_autoctl service that start/stop postgres when asked",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgres);

CommandLine service_monitor_listener =
	make_command("listener",
				 "pg_autoctl service that listens to the monitor notifications",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_monitor_listener);

CommandLine service_node_active =
	make_command("node-active",
				 "pg_autoctl service that implements the node active protocol",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_node_active);

CommandLine service_getpid_postgres =
	make_command("postgres",
				 "Get the pid of the pg_autoctl postgres controller service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_getpid_postgres);

CommandLine service_getpid_listener =
	make_command("listener",
				 "Get the pid of the pg_autoctl monitor listener service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_getpid_listener);

CommandLine service_getpid_node_active =
	make_command("node-active",
				 "Get the pid of the pg_autoctl keeper node-active service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_getpid_node_active);

static CommandLine *service_getpid[] = {
	&service_getpid_postgres,
	&service_getpid_listener,
	&service_getpid_node_active,
	NULL
};

CommandLine do_service_getpid_commands =
	make_command_set("getpid",
					 "Get the pid of pg_autoctl sub-processes (services)",
					 NULL, NULL,
					 NULL, service_getpid);

CommandLine service_restart_postgres =
	make_command("postgres",
				 "Restart the pg_autoctl postgres controller service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_restart_postgres);

CommandLine service_restart_listener =
	make_command("listener",
				 "Restart the pg_autoctl monitor listener service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_restart_listener);

CommandLine service_restart_node_active =
	make_command("node-active",
				 "Restart the pg_autoctl keeper node-active service",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_restart_node_active);

static CommandLine *service_restart[] = {
	&service_restart_postgres,
	&service_restart_listener,
	&service_restart_node_active,
	NULL
};

CommandLine do_service_restart_commands =
	make_command_set("restart",
					 "Restart pg_autoctl sub-processes (services)", NULL, NULL,
					 NULL, service_restart);

static CommandLine *service[] = {
	&do_service_getpid_commands,
	&do_service_restart_commands,
	&service_pgcontroller,
	&service_postgres,
	&service_monitor_listener,
	&service_node_active,
	NULL
};

CommandLine do_service_commands =
	make_command_set("service",
					 "Run pg_autoctl sub-processes (services)", NULL, NULL,
					 NULL, service);


CommandLine service_postgres_ctl_on =
	make_command("on",
				 "Signal pg_autoctl postgres service to ensure Postgres is running",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgresctl_on);


CommandLine service_postgres_ctl_off =
	make_command("off",
				 "Signal pg_autoctl postgres service to ensure Postgres is stopped",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_service_postgresctl_off);

static CommandLine *pgctl[] = {
	&service_postgres_ctl_on,
	&service_postgres_ctl_off,
	NULL
};

CommandLine do_service_postgres_ctl_commands =
	make_command_set("pgctl",
					 "Signal the pg_autoctl postgres service", NULL, NULL,
					 NULL, pgctl);


/*
 * cli_do_service_getpid retrieves the PID of a service running within the
 * pg_autoctl supervision.
 */
static void
cli_do_service_getpid(const char *serviceName)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	pid_t pid = -1;

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!supervisor_find_service_pid(pathnames.pid, serviceName, &pid))
	{
		log_fatal("Failed to find pid for service name \"%s\"", serviceName);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%d\n", pid);
}


/*
 * cli_do_service_getpid_postgres gets the postgres service pid.
 */
static void
cli_do_service_getpid_postgres(int argc, char **argv)
{
	(void) cli_do_service_getpid(SERVICE_NAME_POSTGRES);
}


/*
 * cli_do_service_getpid_listener gets the postgres service pid.
 */
static void
cli_do_service_getpid_listener(int argc, char **argv)
{
	(void) cli_do_service_getpid(SERVICE_NAME_MONITOR);
}


/*
 * cli_do_service_getpid_node_active gets the postgres service pid.
 */
static void
cli_do_service_getpid_node_active(int argc, char **argv)
{
	(void) cli_do_service_getpid(SERVICE_NAME_KEEPER);
}


/*
 * cli_do_service_restart sends the TERM signal to the given serviceName, which
 * is known to have the restart policy RP_PERMANENT (that's hard-coded). As a
 * consequence the supervisor will restart the service.
 */
static void
cli_do_service_restart(const char *serviceName)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };

	pid_t pid = -1;
	pid_t newPid = -1;

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!supervisor_find_service_pid(pathnames.pid, serviceName, &pid))
	{
		log_fatal("Failed to find pid for service name \"%s\"", serviceName);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Sending the TERM signal to service \"%s\" with pid %d",
			 serviceName, pid);

	if (kill(pid, SIGTERM) != 0)
	{
		log_error("Failed to send SIGHUP to the pg_autoctl pid %d: %m", pid);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* loop until we have a new pid */
	do {
		if (!supervisor_find_service_pid(pathnames.pid, serviceName, &newPid))
		{
			log_fatal("Failed to find pid for service name \"%s\"", serviceName);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		if (newPid == pid)
		{
			log_trace("pidfile \"%s\" still contains pid %d for service \"%s\"",
					  pathnames.pid, newPid, serviceName);
		}

		pg_usleep(100 * 1000);  /* retry in 100 ms */
	} while (newPid == pid);

	log_info("Service \"%s\" has been restarted with pid %d",
			 serviceName, newPid);

	fformat(stdout, "%d\n", newPid);
}


/*
 * cli_do_service_restart_postgres sends the TERM signal to the postgres
 * service, which is known to have the restart policy RP_PERMANENT (that's
 * hard-coded). As a consequence the supervisor will restart the service.
 */
static void
cli_do_service_restart_postgres(int argc, char **argv)
{
	(void) cli_do_service_restart(SERVICE_NAME_POSTGRES);
}


/*
 * cli_do_service_restart_listener sends the TERM signal to the monitor
 * listener service, which is known to have the restart policy RP_PERMANENT
 * (that's hard-coded). As a consequence the supervisor will restart the
 * service.
 */
static void
cli_do_service_restart_listener(int argc, char **argv)
{
	(void) cli_do_service_restart(SERVICE_NAME_MONITOR);
}


/*
 * cli_do_service_restart_node_active sends the TERM signal to the keeper node
 * active service, which is known to have the restart policy RP_PERMANENT
 * (that's hard-coded). As a consequence the supervisor will restart the
 * service.
 */
static void
cli_do_service_restart_node_active(int argc, char **argv)
{
	(void) cli_do_service_restart(SERVICE_NAME_KEEPER);
}


/*
 * cli_do_pgcontroller starts the process controller service within a supervision
 * tree. It is used for debug purposes only. When using this entry point we
 * have a supervisor process that is responsible for only one service:
 *
 *  pg_autoctl do service pgcontroller
 *   - pg_autoctl do service postgres
 *     - postgres
 */
static void
cli_do_service_pgcontroller(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };

	Service subprocesses[] = {
		"postgres",
		RP_PERMANENT,
		-1,
		&service_postgres_ctl_start
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	bool exitOnQuit = false;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!supervisor_start(subprocesses, subprocessesCount, pathnames.pid))
	{
		log_fatal("Failed to start the supervisor, see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_service_postgres starts the process service. This is intended to be
 * used from the supervisor process tree itself. Then we have a main process
 * that supervises two sub-processes, one of them is cli_do_service_postgres:
 *
 *  pg_autoctl
 *   - pg_autoctl do service postgres
 *     - postgres
 *   - pg_autoctl do service keeper|monitor
 */
static void
cli_do_service_postgres(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };

	bool exitOnQuit = false;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	if (!cli_common_pgsetup_init(&pathnames, &(postgres.postgresSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: start/stop postgres");

	/* create the service pidfile */
	if (!create_service_pidfile(pathnames.pid, SERVICE_NAME_POSTGRES))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) service_postgres_ctl_loop(&postgres);
}


/*
 * cli_do_service_postgresctl_on asks the pg_autoctl Postgres controller service
 * to ensure that Postgres is running.
 */
static void
cli_do_service_postgresctl_on(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	(void) local_postgres_init(&postgres, pgSetup);

	if (!ensure_postgres_service_is_running(&postgres))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_info("Postgres is serving PGDATA \"%s\" on port %d with pid %d",
			 pgSetup->pgdata, pgSetup->pgport, pgSetup->pidFile.pid);

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();

		if (!pg_setup_as_json(pgSetup, js))
		{
			/* can't happen */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		(void) cli_pprint_json(js);
	}
}


/*
 * cli_do_service_postgresctl_on asks the pg_autoctl Postgres controller service
 * to ensure that Postgres is stopped.
 */
static void
cli_do_service_postgresctl_off(int argc, char **argv)
{
	ConfigFilePaths pathnames = { 0 };
	LocalPostgresServer postgres = { 0 };
	PostgresSetup *pgSetup = &(postgres.postgresSetup);

	if (!cli_common_pgsetup_init(&pathnames, pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	(void) local_postgres_init(&postgres, pgSetup);

	if (!ensure_postgres_service_is_stopped(&postgres))
	{
		exit(EXIT_CODE_PGCTL);
	}

	log_info("Postgres has been stopped for PGDATA \"%s\"", pgSetup->pgdata);
}


/*
 * cli_do_service_monitor_listener starts the monitor listener service.
 */
static void
cli_do_service_monitor_listener(int argc, char **argv)
{
	KeeperConfig options = keeperOptions;

	Monitor monitor = { 0 };
	bool missingPgdataIsOk = false;
	bool pgIsNotRunningIsOk = true;

	bool exitOnQuit = true;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	/* Prepare MonitorConfig from the CLI options fed in options */
	if (!monitor_config_init_from_pgsetup(&(monitor.config),
										  &options.pgSetup,
										  missingPgdataIsOk,
										  pgIsNotRunningIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGCTL);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: monitor listener");

	/* create the service pidfile */
	if (!create_service_pidfile(monitor.config.pathnames.pid,
								SERVICE_NAME_MONITOR))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* Start the monitor service */
	(void) monitor_service_run(&monitor);
}


/*
 * cli_do_service_node_active starts the node active service.
 */
static void
cli_do_service_node_active(int argc, char **argv)
{
	Keeper keeper = { 0 };

	pid_t ppid = getppid();

	bool exitOnQuit = true;

	keeper.config = keeperOptions;

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	/* Prepare our Keeper and KeeperConfig from the CLI options */
	if (!service_keeper_node_active_init(&keeper))
	{
		log_fatal("Failed to initialize the node active service, "
				  "see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* display a user-friendly process name */
	(void) set_ps_title("pg_autoctl: node active");

	/* create the service pidfile */
	if (!create_service_pidfile(keeper.config.pathnames.pid,
								SERVICE_NAME_KEEPER))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* Start the node_active() protocol client */
	(void) keeper_node_active_loop(&keeper, ppid);
}
