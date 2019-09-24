/*
 * src/bin/pg_autoctl/keeper_listener.c
 *	 The keeper listener is a process that reads commands from a PIPE and then
 *	 synchronoulsy writes to the same PIPE the result of running given
 *	 commands. This process is used to implement FSM transitions when running
 *	 in --disable-monitor mode.
 *
 *   One reason to do things that way is that we don't want the postgres
 *   processes to inherit from the HTTPd server socket and other pg_autoctl
 *   context; so the clean way is to have a process hierarchy where the HTTPd
 *   service is not the parent of the Postgres related activity.
 *
 *   pg_autoctl run
 *    - httpd server
 *    - listener
 *      - pg_autoctl do fsm assign  (or other commands)
 *        - postgres -p 5432 -h localhost -k /tmp
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cli_root.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_listener.h"
#include "log.h"
#include "signals.h"
#include "state.h"


static bool keeper_listener_read_commands(FILE *input, FILE *output);


/*
 * keeper_listener_start starts a subprocess that listens on a given PIPE for
 * commands to run. The commands it implements are the PG_AUTOCTL_DEBUG=1
 * commands.
 */
bool
keeper_listener_start(const char *pgdata, int port)
{
	pid_t pid;
	int log_level = logLevel;
	int listenerPipe[2] = { 0, 0 };
	FILE *listenerInput;
	FILE *listenerOutput;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* create the communication pipe to the listener */
	if (pipe(listenerPipe) < 0)
	{
		log_error("Failed to create a pipe with the listener process: %s",
				  strerror(errno));
		return false;
	}

	listenerInput = fdopen(listenerPipe[0], "r");

	if (listenerInput == NULL)
	{
		log_error("Failed to open the input to the listener process: %s",
				  strerror(errno));
		return false;
	}

	listenerOutput = fdopen(listenerPipe[1], "w");

	if (listenerOutput == NULL)
	{
		log_error("Failed to open the output to the listener process: %s",
				  strerror(errno));
		return false;
	}

	/* time to create the listener sub-process, that receives the commands */
	pid = fork();

	switch (pid)
	{
		case -1:
		{
			log_error("Failed to fork the HTTPd process");
			return false;
		}

		case 0:
		{
			/* fork succeeded, in child */

			/* we execute commands through the pg_autoctl do command line */
			setenv(PG_AUTOCTL_DEBUG, "1", 1);

			return keeper_listener_read_commands(listenerInput, listenerOutput);
		}

		default:
		{
			/* fork succeeded, in parent */
			log_warn("HTTP service started in subprocess %d", pid);
			return true;
		}
	}
}


/*
 * keeper_listener_read_commands reads from the listener PIPE for commands to
 * execute. Commands are expected to be in the form of
 *
 *  fsm assign single
 *
 * And then the listener executes the following command:
 *
 *   pg_autoctl do fsm assign single
 */
static bool
keeper_listener_read_commands(FILE *input, FILE *output)
{
	char buffer[BUFSIZE] = { 0 };
	char *command = NULL;

	while ((command = fgets(buffer, BUFSIZE, input)) != NULL)
	{
		log_warn("keeper_listener_read_commands: \"%s\"", command);

		/* TODO: implement the command via a sub-program call */
	}

	return true;
}
