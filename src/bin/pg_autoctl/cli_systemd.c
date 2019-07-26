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

/* pg_autoctl show systemd, see cli_show.c */
CommandLine systemd_cat_service_file_command =
	make_command("systemd",
				 "Print systemd service file for this node", "", "",
				 cli_systemd_getopt, cli_systemd_cat_service_file);


/*
 * cli_systemd_getopt parses the command line options necessary to handle
 * systemd integration for the pg_autoctl keeper service.
 */
int
cli_systemd_getopt(int argc, char **argv)
{
	SystemdServiceConfig options = { 0 };

	int c = 0, option_index = 0;

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
 * cli_systemd_cat_service_file prints the systemd service file for this
 * pg_autoctl node.
 */
static void
cli_systemd_cat_service_file(int argc, char **argv)
{
	SystemdServiceConfig config = systemdOptions;
	PostgresSetup pgSetup = { 0 };

	(void) systemd_config_init(&config, pgSetup.pgdata);

	log_info("HINT: to complete a systemd integration, "
			 "run the following commands:");
	log_info("pg_autoctl -q show systemd --pgdata \"%s\" | sudo tee %s",
			 config.pgSetup.pgdata, config.pathnames.systemd);
	log_info("sudo systemctl daemon-reload");
	log_info("sudo systemctl start pgautofailover");

	if (!systemd_config_write(stdout, &config))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}
