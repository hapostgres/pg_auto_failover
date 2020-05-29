/*
 * src/bin/pg_autoctl/main.c
 *    Main entry point for the pg_autoctl command-line tool
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_root.h"
#include "env_utils.h"
#include "keeper.h"
#include "keeper_config.h"
#include "lock_utils.h"

char pg_autoctl_argv0[MAXPGPATH];
char pg_autoctl_program[MAXPGPATH];

char *ps_buffer;                /* will point to argv area */
size_t ps_buffer_size;          /* space determined at run time */
size_t last_status_len;         /* use to minimize length of clobber */

Semaphore log_semaphore;        /* allows inter-process locking */


static void set_logger(void);


/*
 * set_logger creates our log semaphore, sets the logging utility aspects such
 * as using colors in an interactive terminal and the default log level.
 */
static void
set_logger()
{
	/* we're verbose by default */
	log_set_level(LOG_INFO);

	/*
	 * Log messages go to stderr. We use colours when stderr is being shown
	 * directly to the user to make it easier to spot warnings and errors.
	 */
	log_use_colors(isatty(fileno(stderr)));

	/* initialise the semaphore used for locking log output */
	if (!semaphore_init(&log_semaphore))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* set our logging facility to use our semaphore as a lock mechanism */
	(void) log_set_udata(&log_semaphore);
	(void) log_set_lock(&semaphore_log_lock_function);
}


/*
 * Main entry point for the binary.
 */
int
main(int argc, char **argv)
{
	CommandLine command = root;

	/* allows changing process title in ps/top/ptree etc */
	(void) init_ps_buffer(argc, argv);

	/* set our logging infrastructure */
	(void) set_logger();

	/*
	 * When PG_AUTOCTL_DEBUG is set in the environment, provide the user
	 * commands available to debug a pg_autoctl instance.
	 */
	if (env_exists(PG_AUTOCTL_DEBUG))
	{
		command = root_with_debug;
	}

	/*
	 * We need to follow POSIX specifications for argument parsing, in
	 * particular we want getopt() to stop as soon as it reaches a non option
	 * in the command line.
	 *
	 * GNU and modern getopt() implementation will reorder the command
	 * arguments, making a mess of our nice subcommands facility.
	 *
	 * Note that we call unsetenv("POSIXLY_CORRECT"); before parsing options
	 * for commands that are the final sub-command of their chain and when we
	 * might mix options and arguments.
	 */
	setenv("POSIXLY_CORRECT", "1", 1);

	/*
	 * Stash away the argv[0] used to run this program and compute the realpath
	 * of the program invoked, which we need at several places including when
	 * preparing the systemd unit files.
	 *
	 * Note that we're using log_debug() in get_program_absolute_path and we
	 * have not set the log level from the command line option parsing yet. We
	 * hard-coded LOG_INFO as our log level. For now we won't see the log_debug
	 * output, but as a developer you could always change the LOG_INFO to
	 * LOG_DEBUG above and then see the message.
	 */
	strlcpy(pg_autoctl_argv0, argv[0], MAXPGPATH);

	if (!set_program_absolute_path(pg_autoctl_program, MAXPGPATH))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!commandline_run(&command, argc, argv))
	{
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!semaphore_finish(&log_semaphore))
	{
		/* error have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	return 0;
}
