/*
 * src/bin/pg_walserver/ws_util.h
 *   Small hardening helpers shared by pg_walserver: size-capped file reads
 *   (a configuration file is never trusted to be small), atomic file
 *   replacement (temp file + fsync + rename, the way PostgreSQL's
 *   durable_rename() users and the historical global/pg_pwd flat files
 *   did it: one writer, readers only ever see a complete old or new file),
 *   log sanitizing of client supplied strings, and the connection's
 *   absolute authentication deadline.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_UTIL_H
#define WS_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* hba/passwd/nodes/routes/uri files: nothing legitimate is larger */
#define WS_MAX_CONFIG_FILE_SIZE (1024 * 1024)

/*
 * ws_read_file_capped reads the whole regular file at path into a freshly
 * malloc'ed, NUL-terminated buffer. A file larger than maxSize is an error
 * (logged, false). A missing file returns false quietly when missingOk.
 * When st is not NULL it receives the fstat() of the file actually read.
 */
struct stat;
bool ws_read_file_capped(const char *path, size_t maxSize, bool missingOk,
						 char **contents, size_t *size, struct stat *st);

/* the same with explicit open() flags (O_RDONLY | O_CLOEXEC | O_NOFOLLOW) */
bool ws_read_file_flags(const char *path, int openFlags, size_t maxSize,
						bool missingOk, char **contents, size_t *size,
						struct stat *st);

/*
 * ws_open_served_file opens a file the server is about to send to a client:
 * O_NOFOLLOW (a symlink planted in a route directory is never followed) and
 * a regular-file check on the descriptor itself (fstat). -1 with errno set
 * (ENOENT when missing) on failure.
 */
int ws_open_served_file(const char *path);

/*
 * ws_sanitize_for_log copies a client supplied string into out, replacing
 * control characters (< 0x20, 0x7f) with '?' and truncating it to fit
 * (marking the truncation with "..."), so it can be logged or shown in a
 * process title without letting a client forge log lines.
 */
void ws_sanitize_for_log(const char *in, char *out, size_t outSize);

/*
 * The absolute authentication deadline of the current connection, see
 * accept_loop.c.
 */
void ws_auth_deadline_set(int seconds);
void ws_auth_deadline_clear(void);

/* CLOCK_MONOTONIC in milliseconds */
int64_t ws_monotonic_ms(void);

/*
 * ws_file_crc32c reads the whole regular file at path (a plain sequential
 * read, in chunks -- never loading a whole WAL segment into memory) and
 * computes its size and CRC32C, using the same INIT_CRC32C/COMP_CRC32C/
 * FIN_CRC32C facility (port/pg_crc32c.h) real Postgres uses for its own
 * backup manifests and pg_autoctl's own nodespec.c file-change detection --
 * see CHECK_FILE/ARCHIVE_FILE's own design (DESIGN-standalone-archiving.md)
 * for why CRC32C specifically. Returns false, with errno left exactly as
 * open()/read() set it (ENOENT for "does not exist" is the case both
 * cmd_check_file.c and cmd_archive_file.c actually branch on), on any
 * failure to open or read the file; sizeOut/crcOut are left untouched in
 * that case.
 */
bool ws_file_crc32c(const char *path, uint64_t *sizeOut, uint32_t *crcOut);

#endif /* WS_UTIL_H */
