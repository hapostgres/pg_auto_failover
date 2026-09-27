/*
 * src/bin/pg_walsender/wal_dir_scan.h
 *   Finds the newest fully-captured (non-.partial) WAL segment in an
 *   archiver's WAL cache directory and derives its boundary LSNs from the
 *   segment filename alone (standard 24-hex-digit XLogFileName format,
 *   using the route's own WAL segment size, see
 *   ws_route_wal_segment_size()).
 *
 *   This is a segment-boundary approximation, not a real-record-level
 *   position: it doesn't parse WAL contents, just the filename. Good
 *   enough for CREATE_REPLICATION_SLOT's consistent_point and
 *   IDENTIFY_SYSTEM's xlogpos; START_REPLICATION's actual segment
 *   streaming (wal_segment_source.c) reads the real bytes.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WAL_DIR_SCAN_H
#define WS_WAL_DIR_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "routes.h"

/*
 * ws_route_wal_segment_size: the WAL segment size of the route's cluster,
 * in bytes, read from "<route path>/archiver-walsegsize" (decimal bytes,
 * written by the archiver's pg_receivewal child). The file may be absent:
 * the default, 16777216, applies (as it does for a file that is not a
 * power of two between 1 MiB and 1 GiB, which is logged). Every segment
 * number/name/LSN computation and "SHOW wal_segment_size" derive from it.
 */
uint64_t ws_route_wal_segment_size(const WsRoute *route);

/*
 * ws_wal_segment_size_string formats a segment size the way the
 * wal_segment_size GUC shows it ("16MB", "64MB", "1GB"), which is what
 * pg_receivewal and pg_basebackup parse (RetrieveWalSegSize).
 */
void ws_wal_segment_size_string(uint64_t segSize, char *dest, size_t destSize);

/*
 * wal_dir_find_latest scans the route's directory for the highest-numbered complete
 * WAL segment (24 hex chars, no ".partial" suffix). On success, returns
 * true with *timeline set and endLsn filled with that segment's end-of-
 * segment LSN (formatted "%X/%08X", matching pg_lsn's own text form) --
 * the natural "resume from here" position once this segment is fully
 * captured. Returns false (not an error, *timeline and *endLsn untouched)
 * if the directory has no WAL segments yet.
 */
bool wal_dir_find_latest(const WsRoute *route, uint32_t *timeline,
						 char *endLsn, size_t endLsnSize);

/*
 * wal_segment_filename formats a filename the same way real Postgres does
 * (XLogFileName), for a given timeline, 0-based segment number and segment
 * size.
 */
void wal_segment_filename(uint32_t timeline, uint64_t segno, uint64_t segSize,
						  char *dest, size_t destSize);

/*
 * wal_position_cache_read reads "<path>/archiver-position" -- the current
 * captured LSN and timeline, written roughly once a second by pg_autoctl's
 * own archiver-capture process (service_archiver_update_current_lsn(),
 * service_archiver.c) for its own monitor-reporting needs, and reused here
 * as a cache to avoid a full directory scan on every connection. On
 * success, returns true with *timeline and lsn filled in. Returns false
 * (untouched) when the file doesn't exist yet (archiver-capture hasn't
 * completed its first tick) or fails to parse -- callers should fall back
 * to wal_dir_find_latest() (or their own equivalent scan) in that case,
 * not treat it as fatal.
 */
bool wal_position_cache_read(const char *path, uint32_t *timeline,
							 char *lsn, size_t lsnSize);

#endif /* WS_WAL_DIR_SCAN_H */
