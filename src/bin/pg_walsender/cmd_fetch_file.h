/*
 * src/bin/pg_walsender/cmd_fetch_file.h
 *   FETCH_FILE: a non-standard side-channel, not a replication-protocol
 *   command, for restore_command-style single-WAL-file fetch (see the
 *   design doc's own reasoning: restore_command spawns a fresh subprocess
 *   once per segment, with no persistent session to reuse -- riding the
 *   replication grammar would add protocol surface no real client ever
 *   exercises). Reuses the same connection's startup-packet + auth
 *   machinery (accept_loop.c routes a dbname of the form
 *   "fetch/<formation>/<group>" here instead of into the normal
 *   replication command loop), so it's gated by the same trust/
 *   allowed_hosts check, no new auth surface.
 *
 *   Wire shape, deliberately minimal since the only caller is
 *   fetch_client.c (this project's own code, not a real Postgres tool):
 *   after AuthenticationOk, the client sends the bare filename as a single
 *   '\n'-terminated line (ws_read_line, not a real protocol message), and
 *   the server replies with exactly one message: CopyData carrying the
 *   raw file bytes on success, or ErrorResponse on failure. Then the
 *   connection closes -- no CopyOutResponse/CopyDone, this isn't a real
 *   COPY sub-protocol, just reusing CopyData as a convenient length-
 *   prefixed binary envelope.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_FETCH_FILE_H
#define WS_CMD_FETCH_FILE_H

#include "routes.h"

void cmd_fetch_file(int sock, const WsRoute *route);

#endif /* WS_CMD_FETCH_FILE_H */
