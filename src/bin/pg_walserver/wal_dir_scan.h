/*
 * src/bin/pg_walserver/wal_dir_scan.h
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
#include <time.h>

#include "routes.h"

/*
 * ws_route_wal_segment_size: the WAL segment size of the route's cluster,
 * in bytes, read from "<route path>/pg_walserver_walsegsize" (decimal bytes,
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
 * ws_wal_segment_prefix_to_position decodes a 24-hex WAL segment prefix
 * (this file's own WS_WAL_FILE_SEGMENT/PARTIAL/BACKUP shape, see ws_wal_
 * dir_classify_filename() below) into its own timeline and 0-based
 * segment number -- the exact 8+8+8 hex split/perXLogId division wal_dir_
 * find_latest() above already applies to the highest segment name it
 * finds by directory scan, exposed here for an arbitrary segment name a
 * caller already has in hand (never touches the filesystem itself).
 */
void ws_wal_segment_prefix_to_position(const char *segmentPrefix,
									   uint64_t segSize,
									   uint32_t *timelineOut,
									   uint64_t *segnoOut);

/*
 * ws_wal_lsn_to_segno converts an "%X/%08X"-formatted LSN plus a route's
 * own WAL segment size into the 0-based segment number it falls in -- the
 * same division wal_dir_find_latest() and cli_archive_cleanup.c's own
 * retention math already use. Returns false (untouched) when lsn doesn't
 * parse as "%X/%X".
 */
bool ws_wal_lsn_to_segno(const char *lsn, uint64_t segSize,
						 uint64_t *segnoOut);

/*
 * wal_dir_has_any_segment returns true as soon as route->path holds at
 * least one WAL segment file, complete OR still ".partial" -- unlike wal_
 * dir_find_latest() above (complete segments only, the right conservative
 * choice for a "resume from here" position), this is a plain "has a
 * receivewal worker connected and begun streaming into this route at all yet"
 * check: a receivewal worker whose only activity so far is its very first, still-
 * growing ".partial" segment (the common case moments after it starts)
 * must count as "yes" here, or a caller polling for readiness would spin
 * until an entire segment happens to fill, which may never even happen
 * during a short-lived caller's own bounded wait. See cli_setup.c's own
 * "prime the embedded receivewal worker before taking the first base backup" use.
 */
bool wal_dir_has_any_segment(const WsRoute *route);

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

/*
 * WS_RECEIVEWAL_PROGRESS_FILENAME is "<route path>/receivewal-progress" --
 * a separate, purely observational LSN cache from "archiver-position"
 * above, written by the embedded receivewal worker's own pgaf_wal_progress_
 * hook/pgaf_wal_segment_closed_hook callbacks (pg_receivewal_entry.h,
 * wired up in cli_internal.c). Deliberately NOT the same file as
 * "archiver-position": pgaf_wal_progress_hook's own xlogpos is raw stream
 * position, not guaranteed to land on a genuine WAL record boundary (see
 * that hook's own comment), so it must never be mistaken for the safe,
 * record-boundary "resume from here" position wal_position_cache_read()
 * hands out to START_REPLICATION/CREATE_REPLICATION_SLOT/base backup code
 * elsewhere in this codebase. This file's "lsn" is for "pg_walserver ps"/
 * "status"/"list clusters" to *display*, nothing else.
 */
#define WS_RECEIVEWAL_PROGRESS_FILENAME "receivewal-progress"

/*
 * ws_receivewal_progress_write overwrites "<path>/receivewal-progress" with
 * lsn/timeline (the receivewal worker's own last-observed position, already
 * formatted "%X/%08X" by the caller -- this file has no reason to depend on
 * <access/xlogdefs.h>/XLogRecPtr) plus the current wall-clock time. Called
 * from the embedded receivewal worker child's own hook callbacks
 * (cli_internal.c), which throttle how often they call this to roughly
 * once a second; this function itself performs no throttling of its own,
 * just one atomic overwrite (write_file_atomic()) per call. Returns false
 * (and logs nothing -- best-effort, must never crash or stall the
 * receivewal worker over a display-only file) on failure.
 */
bool ws_receivewal_progress_write(const char *path, const char *lsn,
								  uint32_t timeline);

/*
 * ws_receivewal_progress_read reads it back: lsn/timeline/observedAt are
 * only set on success. Returns false (untouched) when the route has no such
 * file yet (its receivewal worker has never ticked, isn't running, or the
 * route isn't "receivewal = pull" at all) or it fails to parse -- callers
 * must treat that as "no live reading available", never as an error.
 */
bool ws_receivewal_progress_read(const char *path, char *lsn, size_t lsnSize,
								 uint32_t *timeline, time_t *observedAt);

/*
 * WsWalFileKind classifies one directory entry's filename shape -- exported
 * for "pg_walserver list wal" (cli_list.c) to reuse this file's own
 * filename-parsing rather than re-deriving it a second time (cli_archive_
 * cleanup.c's own wal_prefix_from_name() recognizes almost the same shapes,
 * for a different purpose -- retention cutoff comparison, not inventory --
 * and stays private to that file).
 */
typedef enum
{
	WS_WAL_FILE_OTHER = 0,     /* not one of the shapes below */
	WS_WAL_FILE_SEGMENT,       /* 24 hex digits, complete */
	WS_WAL_FILE_PARTIAL,       /* 24 hex digits + ".partial" */
	WS_WAL_FILE_BACKUP,        /* "<24hex>.<8hex>.backup" */
	WS_WAL_FILE_HISTORY        /* "<8hex>.history" */
} WsWalFileKind;

/*
 * ws_wal_dir_classify_filename classifies name into one of WsWalFileKind's
 * five shapes. For WS_WAL_FILE_SEGMENT/PARTIAL/BACKUP, segmentOut (when not
 * NULL, at least 25 bytes) receives the 24-hex WAL segment prefix.
 */
WsWalFileKind ws_wal_dir_classify_filename(const char *name, char *segmentOut);

#endif /* WS_WAL_DIR_SCAN_H */
