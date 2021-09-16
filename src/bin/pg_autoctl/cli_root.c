/*
 * src/bin/pg_autoctl/cli_root.c
 *     Implementation of a CLI which lets you run individual keeper routines
 *     directly
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "cli_common.h"
#include "cli_root.h"
#include "commandline.h"

/* local bindings for all the commands */
CommandLine help =
	make_command("help", "print help message", "", "", NULL, keeper_cli_help);

CommandLine version =
	make_command("version", "print pg_autoctl version", "", "",
				 cli_print_version_getopts,
				 keeper_cli_print_version);

/* non-local to be able to patch it from other files */
CommandLine *create_subcommands[] = {
	&create_monitor_command,
	&create_postgres_command,
	&create_formation_command,
	NULL
};

CommandLine create_commands =
	make_command_set("create",
					 "Create a pg_auto_failover node, or formation", NULL, NULL,
					 NULL, create_subcommands);

CommandLine *show_subcommands_with_debug[] = {
	&show_uri_command,
	&show_events_command,
	&show_state_command,
	&show_settings_command,
	&show_standby_names_command,
	&show_file_command,
	&systemd_cat_service_file_command,
	NULL
};

CommandLine show_commands_with_debug =
	make_command_set("show",
					 "Show pg_auto_failover information", NULL, NULL,
					 NULL, show_subcommands_with_debug);

CommandLine *show_subcommands[] = {
	&show_uri_command,
	&show_events_command,
	&show_state_command,
	&show_settings_command,
	&show_standby_names_command,
	&show_file_command,
	&systemd_cat_service_file_command,
	NULL
};

CommandLine show_commands =
	make_command_set("show",
					 "Show pg_auto_failover information", NULL, NULL,
					 NULL, show_subcommands);

CommandLine *drop_subcommands[] = {
	&drop_monitor_command,
	&drop_node_command,
	&drop_formation_command,
	NULL
};

CommandLine drop_commands =
	make_command_set("drop",
					 "Drop a pg_auto_failover node, or formation", NULL, NULL,
					 NULL, drop_subcommands);

/*
 * Binding them all into the top-level command:
 */
CommandLine *root_subcommands_with_debug[] = {
	&create_commands,
	&drop_commands,
	&config_commands,
	&show_commands_with_debug,
	&enable_commands,
	&disable_commands,
	&get_commands,
	&set_commands,
	&perform_commands,
	&do_commands,
	&service_run_command,
	&watch_command,
	&service_stop_command,
	&service_reload_command,
	&service_status_command,
	&help,
	&version,
	NULL
};

CommandLine root_with_debug =
	make_command_set("pg_autoctl",
					 "pg_auto_failover control tools and service",
					 "[ --debug|verbose|quiet ]", NULL,
					 root_options, root_subcommands_with_debug);


CommandLine *root_subcommands[] = {
	&create_commands,
	&drop_commands,
	&config_commands,
	&show_commands,
	&enable_commands,
	&disable_commands,
	&get_commands,
	&set_commands,
	&perform_commands,
	&service_run_command,
	&watch_command,
	&service_stop_command,
	&service_reload_command,
	&service_status_command,
	&help,
	&version,
	NULL
};

CommandLine root =
	make_command_set("pg_autoctl",
					 "pg_auto_failover control tools and service",
					 "[ --verbose --quiet ]", NULL,
					 root_options, root_subcommands);


/*
 * root_options parses flags from the list of arguments that are common to all
 * commands.
 */
int
root_options(int argc, char **argv)
{
	int verboseCount = 0;
	bool printVersion = false;

	static struct option long_options[] = {
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "json", no_argument, NULL, 'J' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int c, option_index, errors = 0;

	optind = 0;

	while ((c = getopt_long(argc, argv, "JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			case 'V':
			{
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

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
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

	return optind;
}
