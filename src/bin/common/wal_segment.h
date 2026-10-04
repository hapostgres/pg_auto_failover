/*
 * src/bin/common/wal_segment.h
 *   WAL segment filename arithmetic and filename-shape predicates, shared
 *   between pg_walserver (wal_dir_scan.c, cmd_base_backup.c,
 *   cli_archive_cleanup.c, cmd_check_file.c, cmd_replication_slot.c,
 *   cmd_start_replication.c) and any future pg_autoctl caller. Every
 *   function here is a straight port of upstream PostgreSQL's own
 *   src/include/access/xlog_internal.h macros/inline functions, kept in
 *   this project's own snake_case convention -- see each function's own
 *   comment (in wal_segment.c) for the exact upstream name and file:line
 *   it mirrors (as of REL_19_BETA1).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WAL_SEGMENT_H
#define WS_WAL_SEGMENT_H

#include <stdbool.h>
#include <stdint.h>

uint64_t wal_segments_per_xlogid(uint64_t segSize);

bool wal_segment_name_is_valid(const char *name);

bool wal_segment_name_is_partial(const char *name);

bool wal_backup_history_name_is_valid(const char *name);

void wal_segment_name_format(uint32_t timeline, uint64_t segno,
							 uint64_t segSize, char *dest, size_t destSize);

void wal_segment_name_parse(const char *name, uint64_t segSize,
							uint32_t *timelineOut, uint64_t *segnoOut);

bool wal_segment_name_extract_prefix(const char *name, char *prefixOut);

bool wal_lsn_to_segno(const char *lsn, uint64_t segSize, uint64_t *segnoOut);

bool wal_lsn_to_segment_name(const char *lsn, uint32_t timeline,
							 uint64_t segSize, char *segmentOut,
							 size_t segmentOutSize);

void wal_segment_size_string(uint64_t segSize, char *dest, size_t destSize);

#endif /* WS_WAL_SEGMENT_H */
