/*
 * src/bin/pg_walsender/tar_stream.h
 *   Walks a directory tree and emits it as a ustar-format byte stream via a
 *   callback, chunked for CopyData framing. Reproduces the shape of
 *   basebackup.c's sendDir()/sendFile()/_tarWriteHeader() pattern -- not
 *   linked (backend-only, tied to the bbsink sink-chain and palloc/
 *   ereport), but the tar-header math itself comes straight from the
 *   vendored vendor/tar.c (tarCreateHeader(), the real Postgres source
 *   both basebackup.c and pg_basebackup itself build on).
 *
 *   Deliberately simpler than basebackup.c's own sendDir(): this walks an
 *   already-complete, static backup directory (produced by a real
 *   pg_basebackup run against a live server -- see the "Base backup
 *   generation" milestone, not yet implemented), so none of basebackup.c's
 *   live-PGDATA special-casing (skipping pg_wal/pg_stat_tmp/postmaster
 *   files, injecting a synthesized backup_label, tracking WAL positions
 *   mid-walk) applies -- the directory is tarred up exactly as it sits on
 *   disk.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_TAR_STREAM_H
#define WS_TAR_STREAM_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Called with successive chunks of the tar byte stream (header blocks, file
 * content, and padding all flow through this same callback) -- return
 * false to abort the walk early (e.g. the client disconnected mid-stream).
 */
typedef bool (*TarChunkCallback) (void *context, const char *data, size_t len);

/*
 * tar_stream_directory walks rootDir recursively and invokes callback with
 * the resulting ustar byte stream. Tar member names are rootDir-relative,
 * with no leading "./" (matching real Postgres's own convention -- see
 * basebackup.c's sendDir()).
 *
 * Deliberately omits the standalone-tar-file convention's trailing two-
 * zero-block end-of-archive marker: real Postgres's own perform_base_
 * backup() (basebackup.c) never puts one on the wire for a client-
 * streamed, WAL-not-included backup either (the only mode this project
 * serves, see cmd_base_backup.c's own "WAL-inclusive BASE_BACKUP is not
 * supported yet" check) -- it sends CopyDone right after the last file's
 * content/padding, relying on that alone to signal "no more files". A pre-
 * 15 pg_basebackup client's own receiving state machine (ReceiveAndUnpack
 * TarFile(), pg_basebackup.c) takes this literally: once it's between
 * files, it treats the *next* CopyData chunk as a new tar header and
 * requires it to be exactly TAR_BLOCK_SIZE bytes, erroring out ("invalid
 * tar block header size") on anything else -- including this marker, which
 * this project used to send as one extra 2*TAR_BLOCK_SIZE-byte chunk after
 * the real content. Never needed, and actively broke pre-15 clients.
 */
bool tar_stream_directory(const char *rootDir, TarChunkCallback callback, void *context);

#endif /* WS_TAR_STREAM_H */
