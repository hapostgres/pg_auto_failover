/*
 * src/bin/pg_autoctl/cli_config.c
 *     Implementation of pg_autoctl config CLI sub-commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "ini_file.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"


static void cli_config_check(int argc, char **argv);
static void cli_config_check_pgsetup(PostgresSetup *pgSetup);

static void cli_config_get(int argc, char **argv);
static void cli_keeper_config_get(int argc, char **argv);
static void cli_monitor_config_get(int argc, char **argv);

static void cli_config_set(int argc, char **argv);
static void cli_keeper_config_set(int argc, char **argv);
static void cli_monitor_config_set(int argc, char **argv);

static CommandLine config_check =
	make_command("check",
				 "Check pg_autoctl configuration",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_config_check);

static CommandLine config_get =
	make_command("get",
				 "Get the value of a given pg_autoctl configuration variable",
				 "[ section.option ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_config_get);

static CommandLine config_set =
	make_command("set",
				 "Set the value of a given pg_autoctl configuration variable",
				 "section.option [ value ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_config_set);

static CommandLine *config[] = {
	&config_check,
	&config_get,
	&config_set,
	NULL
};

CommandLine config_commands =
	make_command_set("config",
					 "Manages the pg_autoctl configuration", NULL, NULL,
					 NULL, config);


/*
 * cli_config_check reads a configuration file and debug its content as
 * DEBUG messages.
 */
static void
cli_config_check(int argc, char **argv)
{
	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;

	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames, config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			Monitor monitor;
			MonitorConfig mconfig = { 0 };

			if (!monitor_config_init_from_pgsetup(&monitor,
												  &mconfig,
												  &config.pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			(void) cli_config_check_pgsetup(&(mconfig.pgSetup));

			fprintf(stdout, "Current Configuration (includes pgSetup):\n");
			monitor_config_write(stdout, &mconfig);
			fprintf(stdout, "\n");

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			(void) cli_config_check_pgsetup(&(config.pgSetup));

			fprintf(stdout, "Current Configuration (includes pgSetup):\n");
			keeper_config_write(stdout, &config);
			fprintf(stdout, "\n");

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	keeper_config_destroy(&config);
}


/*
 * cli_keeper_config_check checks a keeper configuration file.
 */
static void
cli_config_check_pgsetup(PostgresSetup *pgSetup)
{
	int errors = 0;

	/*
	 * Now check for errors. Rather than using the generic missing_pgdata_is_ok
	 * and pg_not_running_is_ok facility, we do our own error checking here.
	 * One reason is that this command line doesn't provide support for
	 * --pgport and other options, on purpose. Another reason is that we want
	 * to check for everything rather than fail fast.
	 */
	if (pgSetup->control.pg_control_version == 0)
	{
		errors++;
		log_error("postgresql.pgdata does not belong to a PostgreSQL cluster: "
				  "\"%s\"", pgSetup->pgdata);
	}

	/* when PostgreSQL is running, pg_setup_init() has connected to it. */
	if (pgSetup->pidFile.pid == 0)
	{
		errors++;
		log_error("PostgreSQL is not running");
	}

	/* TODO: check formation, group, nodename on the monitor */

	fprintf(stdout, "Discovered PostgreSQL Setup:\n");
	fprintf_pg_setup(stdout, pgSetup);
	fprintf(stdout, "\n");

	if (errors > 0)
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}
}


/*
 * cli_keeper_config_get retrieves the value of a given configuration value,
 * supporting either a Keeper or a Monitor configuration file.
 */
static void
cli_config_get(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames, config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
			(void) cli_monitor_config_get(argc, argv);
			break;

		case PG_AUTOCTL_ROLE_KEEPER:
			(void) cli_keeper_config_get(argc, argv);
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
 * keeper_cli_config_get returns the value of a given section.option, or prints
 * out the whole file to stdout when no argument has been given.
 */
static void
cli_keeper_config_get(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	switch (argc)
	{
		case 0:
		{
			/* no argument, write the config out */
			if (!keeper_config_read_file(&config,
										 missing_pgdata_is_ok,
										 pg_is_not_running_is_ok))
			{
				exit(EXIT_CODE_PGCTL);
			}
			else
			{
				keeper_config_write(stdout, &config);
				fprintf(stdout, "\n");
			}

			keeper_config_destroy(&config);
			break;
		}

		case 1:
		{
			/* single argument, find the option and display its value */
			char *path = argv[0];
			char value[BUFSIZE];

			if (keeper_config_get_setting(&config,
										  path,
										  value,
										  BUFSIZE))
			{
				fprintf(stdout, "%s\n", value);
			}
			else
			{
				log_error("Failed to lookup option %s", path);
				exit(EXIT_CODE_BAD_ARGS);
			}

			keeper_config_destroy(&config);
			break;
		}

		default:
		{
			/* we only support 0 or 1 argument */
			commandline_help(stderr);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}


/*
 * keeper_cli_config_get returns the value of a given section.option, or prints
 * out the whole file to stdout when no argument has been given.
 */
static void
cli_monitor_config_get(int argc, char **argv)
{
	Monitor monitor = { 0 };
	MonitorConfig mconfig = { 0 };
	KeeperConfig kconfig = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	if (!monitor_config_init_from_pgsetup(&monitor,
										  &mconfig,
										  &kconfig.pgSetup,
										  missing_pgdata_is_ok,
										  pg_is_not_running_is_ok))
	{
		exit(EXIT_CODE_PGCTL);
	}

	switch (argc)
	{
		case 0:
		{
			monitor_config_write(stdout, &mconfig);
			fprintf(stdout, "\n");

			keeper_config_destroy(&kconfig);
			break;
		}

		case 1:
		{
			/* single argument, find the option and display its value */
			char *path = argv[0];
			char value[BUFSIZE];

			if (monitor_config_get_setting(&mconfig,
										  path,
										  value,
										  BUFSIZE))
			{
				fprintf(stdout, "%s\n", value);
			}
			else
			{
				log_error("Failed to lookup option %s", path);
				exit(EXIT_CODE_BAD_ARGS);
			}

			keeper_config_destroy(&kconfig);
			break;
		}

		default:
		{
			/* we only support 0 or 1 argument */
			commandline_help(stderr);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}


/*
 * cli_keeper_config_get retrieves the value of a given configuration value,
 * supporting either a Keeper or a Monitor configuration file.
 */
static void
cli_config_set(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames, config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
			(void) cli_monitor_config_set(argc, argv);
			break;

		case PG_AUTOCTL_ROLE_KEEPER:
			(void) cli_keeper_config_set(argc, argv);
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
 * cli_keeper_config_set sets the given option path to the given value.
 */
static void
cli_keeper_config_set(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (argc != 2)
	{
		log_error("Two arguments are expected, found %d", argc);
		exit(EXIT_CODE_BAD_ARGS);
	}
	else
	{
		/* we print out the value that we parsed, as a double-check */
		char value[BUFSIZE];

		if (!keeper_config_set_setting(&config,
									   argv[0],
									   argv[1]))
		{
			/* we already logged about it */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* first write the new configuration settings to file */
		if (!keeper_config_write_file(&config))
		{
			log_fatal("Failed to write the monitor's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* now read the value from just written file */
		if (keeper_config_get_setting(&config,
									  argv[0],
									  value,
									  BUFSIZE))
		{
			fprintf(stdout, "%s\n", value);
		}
		else
		{
			log_error("Failed to lookup option %s", argv[0]);
			exit(EXIT_CODE_BAD_ARGS);
		}

		keeper_config_destroy(&config);
	}
}


/*
 * cli_monitor_config_set sets the given option path to the given value.
 */
static void
cli_monitor_config_set(int argc, char **argv)
{
	KeeperConfig kconfig = keeperOptions;

	if (argc != 2)
	{
		log_error("Two arguments are expected, found %d", argc);
		exit(EXIT_CODE_BAD_ARGS);
	}
	else
	{
		/* we print out the value that we parsed, as a double-check */
		char value[BUFSIZE];
		Monitor monitor = { 0 };
		MonitorConfig mconfig = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		if (!monitor_config_init_from_pgsetup(&monitor,
											  &mconfig,
											  &kconfig.pgSetup,
											  missing_pgdata_is_ok,
											  pg_is_not_running_is_ok))
		{
			exit(EXIT_CODE_PGCTL);
		}

		/* first write the new configuration settings to file */
		if (!monitor_config_set_setting(&mconfig, argv[0], argv[1]))
		{
			/* we already logged about it */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		if (!monitor_config_write_file(&mconfig))
		{
			log_fatal("Failed to write the monitor's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* now read the value from just written file */
		if (monitor_config_get_setting(&mconfig,
									   argv[0],
									   value,
									   BUFSIZE))
		{
			fprintf(stdout, "%s\n", value);
		}
		else
		{
			log_error("Failed to lookup option %s", argv[0]);
			exit(EXIT_CODE_BAD_ARGS);
		}

		keeper_config_destroy(&kconfig);
	}
}
