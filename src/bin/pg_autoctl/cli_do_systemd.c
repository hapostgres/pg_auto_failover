/*
 * src/bin/pg_autoctl/cli_do_systemd.c
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
#include "keeper_config.h"
#include "keeper.h"
#include "systemd_config.h"

static SystemdServiceConfig systemdOptions;


static int cli_systemd_getopt(int argc, char **argv);
static void cli_systemd_enable_service(int argc, char **argv);
static void cli_systemd_cat_service_file(int argc, char **argv);
static void cli_systemd_enable_linger(int argc, char **argv);
static void cli_systemd_disable_linger(int argc, char **argv);

static CommandLine do_systemd_enable_command =
	make_command("enable",
				 "Enable systemd service for this node", "", "",
				 cli_systemd_getopt, cli_systemd_enable_service);

static CommandLine do_systemd_cat_service_file_command =
	make_command("service",
				 "Print systemd service file for this node", "", "",
				 cli_systemd_getopt, cli_systemd_cat_service_file);

static CommandLine do_systemd_enable_linger_command =
	make_command("enable",
				 "loginctl enable-linger", "", "",
				 keeper_cli_getopt_pgdata, cli_systemd_enable_linger);

static CommandLine do_systemd_disable_linger_command =
	make_command("disable",
				 "loginctl disable-linger", "", "",
				 keeper_cli_getopt_pgdata, cli_systemd_disable_linger);

static CommandLine *do_systemd_linger_subcommands[] = {
 	&do_systemd_enable_linger_command,
 	&do_systemd_disable_linger_command,
	NULL
};

CommandLine do_systemd_linger_commands =
	make_command_set("linger",
					 "Systemd integration for pg_autoctl", NULL, NULL,
					 NULL, do_systemd_linger_subcommands);

static CommandLine *do_systemd_subcommands[] = {
	&do_systemd_enable_command,
 	&do_systemd_cat_service_file_command,
 	&do_systemd_linger_commands,
	NULL
};

CommandLine do_systemd_commands =
	make_command_set("systemd",
					 "Systemd integration for pg_autoctl", NULL, NULL,
					 NULL, do_systemd_subcommands);


/*
 * cli_systemd_getopt parses the command line options necessary to handle
 * systemd integration for the pg_autoctl keeper service.
 */
int
cli_systemd_getopt(int argc, char **argv)
{
	SystemdServiceConfig options = { 0 };

	int c = 0, option_index = 0, errors = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:h",
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

		}
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to set PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	if (!keeper_config_set_pathnames_from_pgdata(&options.pathnames,
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* publish our option parsing in the global variable */
	systemdOptions = options;

	return optind;
}


/*
 * cli_systemd_enable_service starts pgautofailover as a user-level systemd
 * unit:
 *
 *  - writes ~/.config/systemd/user/pgautofailover.service
 *  - systemctl --user daemon-reload
 *  - systemctl --user start worker
 */
static void
cli_systemd_enable_service(int argc, char **argv)
{
	SystemdServiceConfig config = systemdOptions;
	PostgresSetup pgSetup = { 0 };

	(void) systemd_config_init(&config, pgSetup.pgdata);

	if (!systemd_config_write_file(&config))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!systemd_user_daemon_reload())
	{
		log_fatal("Failed to reload systemd user-level configuration, "
				  "see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (! systemd_user_start_pgautofailover())
	{
		log_fatal("Failed to start systemd user-level service \"%s\", "
				  "see above for details", KEEPER_SYSTEMD_SERVICE);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_systemd_cat_service_file prints the systemd service file for this
 * pg_autoctl node.
 */
static void
cli_systemd_cat_service_file(int argc, char **argv)
{
	SystemdServiceConfig config = systemdOptions;
	PostgresSetup pgSetup = { 0 };

	(void) systemd_config_init(&config, pgSetup.pgdata);

	if (!systemd_config_write(stdout, &config))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_systemd_enable_linger calls `loginctl enable-linger`
 */
static void
cli_systemd_enable_linger(int argc, char **argv)
{
	if (!systemd_enable_linger())
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_systemd_enable_linger calls `loginctl enable-linger`
 */
static void
cli_systemd_disable_linger(int argc, char **argv)
{
	if (!systemd_disable_linger())
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}
