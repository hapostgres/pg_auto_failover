/*
 * src/bin/pg_walsender/accept_loop.h
 *   The bare accept loop: socket()/bind()/listen()/accept(), fork()
 *   per connection with no exec() (matching real Postgres's
 *   BackendStartup()/BackendMain() model for cheap concurrency -- see
 *   the design doc's "Process model" section), each forked child running
 *   the full startup/auth/command-loop for exactly one connection.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_ACCEPT_LOOP_H
#define WS_ACCEPT_LOOP_H

#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsServerConfig
{
	int port;
	char routesPath[MAXPGPATH];   /* empty: no routing, manual-testing mode */
} WsServerConfig;

bool ws_accept_loop(const WsServerConfig *config);

#endif /* WS_ACCEPT_LOOP_H */
