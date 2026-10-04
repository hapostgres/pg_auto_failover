/*
 * src/bin/pg_walserver/tar_stream.h
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

bool tar_stream_directory(const char *rootDir, TarChunkCallback callback,
						  void *context, const char **excludeNames,
						  int excludeCount);

#endif /* WS_TAR_STREAM_H */
