/*
 * src/bin/pg_walserver/wal_dir_scan.h
 *   Finds the newest fully-captured (non-.partial) WAL segment in an
 *   archiver's WAL cache directory and derives its boundary LSNs from the
 *   segment filename alone (standard 24-hex-digit XLogFileName format,
 *   using the cluster's own WAL segment size, see
 *   ws_cluster_wal_segment_size()).
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

#include "clusters.h"
#include "wal_segment.h"

uint64_t ws_cluster_wal_segment_size(const WsCluster *cluster);

bool wal_dir_find_latest(const WsCluster *cluster, uint32_t *timeline,
						 char *endLsn, size_t endLsnSize);

bool wal_dir_has_any_segment(const WsCluster *cluster);

bool wal_position_cache_read(const char *path, uint32_t *timeline,
							 char *lsn, size_t lsnSize);

/*
 * WS_RECEIVEWAL_PROGRESS_FILENAME is "<cluster path>/receivewal-progress" --
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

bool ws_receivewal_progress_write(const char *path, const char *lsn,
								  uint32_t timeline);

bool ws_receivewal_progress_read(const char *path, char *lsn, size_t lsnSize,
								 uint32_t *timeline, time_t *observedAt);

/*
 * WsWalFileKind classifies one directory entry's filename shape -- exported
 * for "pg_walserver list wal" (cli_list.c) to reuse this file's own
 * filename-parsing rather than re-deriving it a second time (cli_archive_
 * cleanup.c's own wal_segment_name_extract_prefix() call, src/bin/common/
 * wal_segment.h, recognizes almost the same shapes, for a different
 * purpose -- retention cutoff comparison, not inventory).
 */
typedef enum
{
	WS_WAL_FILE_OTHER = 0,     /* not one of the shapes below */
	WS_WAL_FILE_SEGMENT,       /* 24 hex digits, complete */
	WS_WAL_FILE_PARTIAL,       /* 24 hex digits + ".partial" */
	WS_WAL_FILE_BACKUP,        /* "<24hex>.<8hex>.backup" */
	WS_WAL_FILE_HISTORY        /* "<8hex>.history" */
} WsWalFileKind;

WsWalFileKind ws_wal_dir_classify_filename(const char *name, char *segmentOut);

#endif /* WS_WAL_DIR_SCAN_H */
