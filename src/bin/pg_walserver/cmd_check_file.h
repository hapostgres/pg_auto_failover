/*
 * src/bin/pg_walserver/cmd_check_file.h
 *   CHECK_FILE '<name>' <size> crc32c:<hex>: this project's own extension
 *   (not in PostgreSQL's grammar), a cheap query with no file transfer at
 *   all -- see README.md's "The archive push side" section for the full
 *   rationale. The client (`pg_walserver archive`, cli_archive.c,
 *   running as an archive_command) computes the size and CRC32C of its own
 *   *local* file and sends both here; the reply is a single-column,
 *   single-row result (RowDescription/DataRow/CommandComplete, the same
 *   shape SHOW already uses, see cmd_show.h) whose one value is "missing"
 *   (nothing on disk here yet), "matches" (identical), or "differs"
 *   (something else is already there under that name) -- never anything a
 *   client should try to parse structurally beyond those three strings.
 *
 *   This is advisory only: it never writes anything and never trusts
 *   anything it's told beyond deciding what to answer with. The actual
 *   overwrite-safety decision (which ARCHIVE_FILE, cmd_archive_file.h,
 *   alone makes) is re-derived independently from the real bytes received,
 *   never from a CHECK_FILE result a client could lie about.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_CHECK_FILE_H
#define WS_CMD_CHECK_FILE_H

#include <stdint.h>

#include "routes.h"

/*
 * cmd_check_file validates filename against the same allow-list
 * ARCHIVE_FILE/FETCH_FILE use (ws_fetch_filename_is_servable(),
 * cmd_fetch_file.h), then compares clientSize/clientCrc32cHex (an uppercase
 * or lowercase hex CRC32C, compared case-insensitively) against what is
 * actually on disk under route->path, replying "missing"/"matches"/
 * "differs" accordingly.
 */
void cmd_check_file(int sock, const WsRoute *route, const char *filename,
					uint64_t clientSize, const char *clientCrc32cHex);

#endif /* WS_CMD_CHECK_FILE_H */
