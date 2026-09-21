/*
 * src/bin/pg_walsender/cmd_fetch_file.h
 *   FETCH_FILE '<name>': our own replication-connection command (not in
 *   PostgreSQL's grammar) to fetch one file of the route's WAL cache, one
 *   at a time, for a restore_command -- which spawns a fresh process per
 *   segment, with no session to reuse. It is served like any other command
 *   on an ordinary connection: the client (a plain libpq connection, see
 *   fetch_client.c) gets TLS, SCRAM and every libpq connection option for
 *   free, the same HBA rules apply, and the reply is a regular COPY OUT
 *   (CopyOutResponse, CopyData ..., CopyDone, CommandComplete).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_FETCH_FILE_H
#define WS_CMD_FETCH_FILE_H

#include "routes.h"

void cmd_fetch_file(int sock, const WsRoute *route, const char *filename);

#endif /* WS_CMD_FETCH_FILE_H */
