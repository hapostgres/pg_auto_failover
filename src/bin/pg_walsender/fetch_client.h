/*
 * src/bin/pg_walsender/fetch_client.h
 *   The client side of the FETCH_FILE side-channel (cmd_fetch_file.h) --
 *   the only caller of that protocol, matching its header comment ("the
 *   only caller here is pg_autoctl's own restore_command wrapper, which
 *   this project fully controls end to end"). Exposed as `pg_walsender
 *   fetch-file ...` (see main.c) so pg_autoctl's restore_command can shell
 *   out to it directly, the same way it already shells out to real
 *   pg_receivewal/pg_basebackup elsewhere in this project.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_FETCH_CLIENT_H
#define WS_FETCH_CLIENT_H

/*
 * Connects to host:port, requests filename for routeKey ("<formation>/
 * <group>"), and writes the result to outputPath (via a same-directory
 * temp file + rename, so a killed/interrupted fetch never leaves a
 * partial file at outputPath). Returns 0 on success, 1 on any failure
 * (connection, auth, missing file, short write) -- always with a
 * human-readable message already logged, matching restore_command's own
 * "non-zero means retry me" contract.
 */
int ws_fetch_file_client(const char *host, int port, const char *routeKey,
						 const char *filename, const char *outputPath);

#endif /* WS_FETCH_CLIENT_H */
