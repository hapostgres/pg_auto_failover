/*
 * src/bin/pg_walsender/wal_dir_scan.h
 *   Finds the newest fully-captured (non-.partial) WAL segment in an
 *   archiver's WAL cache directory and derives its boundary LSNs from the
 *   segment filename alone (standard 24-hex-digit XLogFileName format,
 *   assuming the fixed 16MB default segment size this project's own SHOW
 *   wal_segment_size already reports -- see cmd_show.c).
 *
 *   This is a segment-boundary approximation, not a real-record-level
 *   position: it doesn't parse WAL contents, just the filename. Good
 *   enough for CREATE_REPLICATION_SLOT's consistent_point and
 *   IDENTIFY_SYSTEM's xlogpos; START_REPLICATION's actual segment
 *   streaming (wal_segment_source.c) reads the real bytes.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WAL_DIR_SCAN_H
#define WS_WAL_DIR_SCAN_H

#include <stdbool.h>
#include <stdint.h>

/*
 * wal_dir_find_latest scans walcacheDir for the highest-numbered complete
 * WAL segment (24 hex chars, no ".partial" suffix). On success, returns
 * true with *timeline set and endLsn filled with that segment's end-of-
 * segment LSN (formatted "%X/%08X", matching pg_lsn's own text form) --
 * the natural "resume from here" position once this segment is fully
 * captured. Returns false (not an error, *timeline and *endLsn untouched)
 * if the directory has no WAL segments yet.
 */
bool wal_dir_find_latest(const char *walcacheDir, uint32_t *timeline,
						 char *endLsn, size_t endLsnSize);

/*
 * wal_segment_filename formats a filename the same way real Postgres does
 * (XLogFileName), for a given timeline and 0-based segment number.
 */
void wal_segment_filename(uint32_t timeline, uint64_t segno,
						  char *dest, size_t destSize);

#endif /* WS_WAL_DIR_SCAN_H */
