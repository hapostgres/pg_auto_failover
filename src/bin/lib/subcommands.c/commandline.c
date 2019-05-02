/*
 * commandline.h
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "commandline.h"


CommandLine *current_command = NULL;
static void commandline_pretty_print_subcommands(CommandLine *command,
												 FILE *stream);


/*
 * Implementation of the main subcommands entry point.
 *
 * Parses the command line given the Command_t cmd context, and run commands
 * that match with the subcommand definitions.
 */
void
commandline_run(CommandLine *command, int argc, char **argv)
{
	const char *argv0 = NULL;

	if (argc > 0)
	{
		argv0 = argv[0];
	}

	/*
	 * If the user gives the --help option at this point, describe the current
	 * command.
	 */
	if (argc >= 2 && (streq(argv[1], "--help") || streq(argv[1], "-h")))
	{
		commandline_print_usage(command, stderr);
		return;
	}

	/* Otherwise let the command parse any options that occur here. */
	if (command->getopt != NULL)
	{
		int option_count = command->getopt(argc, argv);
		argc -= option_count;
		argv += option_count;
	}
	else
	{
		argc--;
		argv++;
	}

	if (command->run != NULL)
	{
		current_command = command;
		return command->run(argc, argv);
	}
	else if (argc == 0)
	{
		/*
		 * We're at the end of the command line already, and command->run is
		 * not set, which means we expected a subcommand to be used, but none
		 * have been given by the user. Inform him.
		 */
		commandline_print_subcommands(command, stderr);
	}
	else
	{
		if (command->subcommands != NULL)
		{
			CommandLine **subcommand = command->subcommands;

			for (; *subcommand != NULL; subcommand++)
			{
				if (streq(argv[0], (*subcommand)->name))
				{
					commandline_add_breadcrumb(command, *subcommand);

					return commandline_run(*subcommand, argc, argv);
				}
			}

			/* if we reach this code, we didn't find a subcommand */
			{
				const char *breadcrumb =
					command->breadcrumb == NULL ? argv0 : command->breadcrumb;

				fprintf(stderr,
						"%s: %s: unknown command\n", breadcrumb, argv[0]);
			}

			fprintf(stderr, "\n");
			commandline_print_subcommands(command, stderr);
		}
	}
}


/*
 * Print help message for the known currently running command.
 */
void
commandline_help(FILE *stream)
{
	if (current_command != NULL)
	{
		commandline_print_usage(current_command, stream);
	}
}


/*
 * Helper function to print usage and help message for a command.
 */
void
commandline_print_usage(CommandLine *command, FILE *stream)
{
	const char *breadcrumb =
		command->breadcrumb == NULL ? command->name : command->breadcrumb;

	fprintf(stream, "%s:", breadcrumb);

	if (command->shortDescription)
	{
		fprintf(stream, " %s", command->shortDescription);
	}
	fprintf(stream, "\n");

	if (command->usageSuffix)
	{
		fprintf(stream,
				"usage: %s %s\n", breadcrumb, command->usageSuffix);
		fprintf(stream, "\n");
	}

	if (command->help)
	{
		fprintf(stream, "%s\n", command->help);
	}

	if (command->subcommands)
	{
		fprintf(stream, "\n");
		commandline_print_subcommands(command, stream);
	}
	fflush(stream);
}


/*
 * Print the list of subcommands accepted from a command.
 */
void
commandline_print_subcommands(CommandLine *command, FILE *stream)
{
	/* the root command doesn't have a breadcrumb at this point */
	const char *breadcrumb =
		command->breadcrumb == NULL ? command->name : command->breadcrumb;

	fprintf(stream, "Available commands:\n  %s\n", breadcrumb);

	commandline_pretty_print_subcommands(command, stream);
	fprintf(stream, "\n");
}


/*
 * commandline_print_command_tree walks a command tree and prints out its whole
 * set of commands, recursively.
 */
void
commandline_print_command_tree(CommandLine *command, FILE *stream)
{
	if (command != NULL)
	{
		const char *breadcrumb =
			command->breadcrumb == NULL ? command->name : command->breadcrumb;

		if (command->subcommands != NULL)
		{
			CommandLine **subcommand;

			fprintf(stream, "  %s\n", breadcrumb);
			commandline_pretty_print_subcommands(command, stream);
			fprintf(stream, "\n");

			for (subcommand = command->subcommands;
				 *subcommand != NULL;
				 subcommand++)
			{
				commandline_add_breadcrumb(command, *subcommand);
				commandline_print_command_tree(*subcommand, stream);
			}
		}
	}
}


/*
 * commandline_pretty_print_subcommands pretty prints a list of subcommands.
 */
static void
commandline_pretty_print_subcommands(CommandLine *command, FILE *stream)
{
	if (command->subcommands != NULL)
	{
		CommandLine **subcommand;
		int maxLength = 0;

		/* pretty printing: reduce maximum length of subcommand names */
		for (subcommand = command->subcommands; *subcommand != NULL; subcommand++)
		{
			int len = strlen((*subcommand)->name);

			if (maxLength < len)
			{
				maxLength = len;
			}
		}

		for (subcommand = command->subcommands; *subcommand != NULL; subcommand++)
		{
			const char *description = "";

			if ((*subcommand)->shortDescription != NULL)
			{
				description = (*subcommand)->shortDescription;
			}

			fprintf(stream,
					"  %c %*s  %s\n",
					(*subcommand)->subcommands ? '+' : ' ',
					(int) -maxLength,
					(*subcommand)->name,
					description);
		}
	}
}


/*
 * Add command to the breadcrumb of subcommand.
 *
 * The idea is to be able to print the list of subcommands in the help
 * messages, as in the following example:
 *
 *   $ ./foo env get --help
 *   foo env get: short description
 */
void
commandline_add_breadcrumb(CommandLine *command, CommandLine *subcommand)
{
	const char *command_bc =
		command->breadcrumb ? command->breadcrumb : command->name;
	int breadcrumbLength = strlen(command_bc);
	int subcommandLength = strlen(subcommand->name);

	breadcrumbLength += subcommandLength + 2;

	subcommand->breadcrumb = (char *) malloc(breadcrumbLength * sizeof(char));
	sprintf(subcommand->breadcrumb, "%s %s", command_bc, subcommand->name);
}
