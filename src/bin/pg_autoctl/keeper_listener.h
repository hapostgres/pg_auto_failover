/*
 * src/bin/pg_autoctl/listener.h
 *	 Internal process that listens to commands on a pipe and executes them.
 *	 This allows the HTTPd server to only care about HTTP communication and
 *	 defer the real work to the listener workers.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_LISTENER_H
#define KEEPER_LISTENER_H

#include <stdio.h>
#include <string.h>

typedef struct CommandPipe
{
	int cmdPipe[2];				/* a Unix pipe to send commands to */
	int resPipe[2];				/* a Unix pipe to retrieve results from */
} CommandPipe;

extern CommandPipe listenerCommandPipe;

bool keeper_listener_start(const char *pgdata, pid_t *listenerPid);
bool keeper_listener_send_command(const char *command, char *output, int size);


#endif	/* KEEPER_LISTENER_H */
