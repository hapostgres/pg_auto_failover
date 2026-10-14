/*
 * src/bin/pg_walserver/cmd_archive_file.h
 *   ARCHIVE_FILE '<name>': this project's own extension (not in
 *   PostgreSQL's grammar), a CopyIn (client to server) push of one file --
 *   a WAL segment or a base backup's own ".backup" history file -- into a
 *   cluster's own directory. See README.md's "The archive push side" section
 *   for the full design this implements.
 *
 *   The server never trusts a client's own CHECK_FILE checksum (a lying
 *   client could otherwise talk its way past the overwrite-safety check
 *   entirely) -- once the whole CopyIn has been received, this file
 *   re-derives the same size/CRC32C comparison CHECK_FILE reports
 *   (cmd_check_file.h) directly from the real bytes on disk vs. the real
 *   bytes just received: identical -> success (an idempotent retry, exactly
 *   matching PostgreSQL's own archive_command contract, which explicitly
 *   requires this), different -> a clean rejection, nothing there yet ->
 *   the bytes are written for real, via a same-directory temp file plus
 *   atomic rename (never a partial file visible under the final name).
 *
 *   Same filename allow-list as FETCH_FILE's read side
 *   (ws_fetch_filename_is_servable(), cmd_fetch_file.h, extended to also
 *   accept ".backup" files -- see that function's own comment), and a hard
 *   size cap at the cluster's own wal_segment_size (ws_cluster_wal_segment_size(),
 *   wal_dir_scan.h) plus a fixed slack, checked as bytes arrive so an
 *   oversized push is rejected without ever writing the whole thing to disk.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_ARCHIVE_FILE_H
#define WS_CMD_ARCHIVE_FILE_H

#include "clusters.h"

/*
 * A WAL segment is exactly the cluster's own configured wal_segment_size; a
 * ".backup" history file is tiny (a few hundred bytes of text). This slack
 * only needs to comfortably exceed the latter -- 1 MiB is generous on both
 * counts, while still catching a genuinely oversized/corrupt push well
 * before it could exhaust disk space.
 */
#define WS_ARCHIVE_FILE_SIZE_SLACK (1024 * 1024)

void cmd_archive_file(int sock, const WsCluster *cluster, const char *filename);

#endif /* WS_CMD_ARCHIVE_FILE_H */
