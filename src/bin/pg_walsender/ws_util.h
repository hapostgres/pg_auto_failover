/*
 * src/bin/pg_walsender/ws_util.h
 *   Small hardening helpers shared by pg_walsender: size-capped file reads
 *   (a configuration file is never trusted to be small), atomic file
 *   replacement (temp file + fsync + rename, the way PostgreSQL's
 *   durable_rename() users and the historical global/pg_pwd flat files
 *   did it: one writer, readers only ever see a complete old or new file),
 *   log sanitizing of client supplied strings, and the connection's
 *   absolute authentication deadline.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
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
 * ws_write_file_atomic writes path through "<path>.tmp.<pid>" in the same
 * directory: write, fsync, rename over the target, fsync the directory.
 */
bool ws_write_file_atomic(const char *path, const char *data, size_t len);

/*
 * ws_sanitize_for_log copies a client supplied string into out, replacing
 * control characters (< 0x20, 0x7f) with '?' and truncating it to fit
 * (marking the truncation with "..."), so it can be logged or shown in a
 * process title without letting a client forge log lines.
 */
void ws_sanitize_for_log(const char *in, char *out, size_t outSize);

/*
 * The absolute authentication deadline of the current connection, see
 * accept_loop.c. ws_auth_deadline_remaining_ms() is INT_MAX-ish (1 hour)
 * when no deadline is armed.
 */
void ws_auth_deadline_set(int seconds);
void ws_auth_deadline_clear(void);
int ws_auth_deadline_remaining_ms(void);

/* CLOCK_MONOTONIC in milliseconds */
int64_t ws_monotonic_ms(void);

#endif /* WS_UTIL_H */
