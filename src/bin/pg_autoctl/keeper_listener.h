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

#include <string.h>

bool keeper_listener_start(const char *pgdata, int port);

#endif	/* KEEPER_LISTENER_H */
