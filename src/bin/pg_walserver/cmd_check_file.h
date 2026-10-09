/*
 * src/bin/pg_walserver/cmd_check_file.h
 *   CHECK_FILE '<name>' <size> crc32c:<hex>: this project's own extension
 *   (not in PostgreSQL's grammar), a cheap query with no file transfer at
 *   all -- see README.md's "The archive push side" section for the full
 *   rationale. The client (`pg_walserver archive`, cli_archive.c,
 *   running as an archive_command) computes the size and CRC32C of its own
 *   *local* file and sends both here; the reply is a two-column,
 *   single-row result (RowDescription/DataRow/CommandComplete, the same
 *   shape SHOW already uses, see cmd_show.h):
 *
 *     status    "missing" (nothing on disk here yet), "matches"
 *               (identical), or "differs" (something else is already
 *               there under that name) -- never anything a client should
 *               try to parse structurally beyond those three strings.
 *     fallback  "yes" or "no": whether this cluster's own embedded
 *               receivewal worker has already streamed *past* filename
 *               (its own last-observed position, "<path>/receivewal-
 *               progress", wal_dir_scan.h, is at a later segment or a
 *               later timeline) while filename itself never showed up --
 *               a hole a streaming worker can never retroactively fill
 *               (typically a timeline switch left a segment behind on
 *               the old timeline). "no" whenever status is "matches",
 *               the cluster has no embedded receivewal worker at all, or
 *               there simply isn't a live progress reading yet (nothing
 *               to compare against) -- the safe default, meaning "keep
 *               waiting for the normal archive_command retry loop",
 *               never "yes" on ambiguous information.
 *
 *   This is advisory only: it never writes anything and never trusts
 *   anything it's told beyond deciding what to answer with. The actual
 *   overwrite-safety decision (which ARCHIVE_FILE, cmd_archive_file.h,
 *   alone makes) is re-derived independently from the real bytes received,
 *   never from a CHECK_FILE result a client could lie about. "fallback"
 *   is advisory in the same sense: it only ever *recommends* pushing via
 *   ARCHIVE_FILE sooner rather than waiting, never forces it -- the
 *   client (cli_archive.c's own ws_archive_push_file()) still decides.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_CHECK_FILE_H
#define WS_CMD_CHECK_FILE_H

#include <stdint.h>

#include "clusters.h"

/*
 * cmd_check_file validates filename against the same allow-list
 * ARCHIVE_FILE/FETCH_FILE use (ws_fetch_filename_is_servable(),
 * cmd_fetch_file.h), then compares clientSize/clientCrc32cHex (an uppercase
 * or lowercase hex CRC32C, compared case-insensitively) against what is
 * actually on disk under cluster->path, replying "missing"/"matches"/
 * "differs" accordingly, plus this file's own header comment's "fallback"
 * column.
 */
void cmd_check_file(int sock, const WsCluster *cluster, const char *filename,
					uint64_t clientSize, const char *clientCrc32cHex);

#endif /* WS_CMD_CHECK_FILE_H */
