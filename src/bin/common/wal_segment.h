/*
 * src/bin/common/wal_segment.h
 *   WAL segment filename arithmetic and filename-shape predicates, shared
 *   between pg_walserver (wal_dir_scan.c, cmd_base_backup.c,
 *   cli_archive_cleanup.c, cmd_check_file.c, cmd_replication_slot.c,
 *   cmd_start_replication.c) and any future pg_autoctl caller. Every
 *   function here is a straight port of upstream PostgreSQL's own
 *   src/include/access/xlog_internal.h macros/inline functions, kept in
 *   this project's own snake_case convention -- see each function's own
 *   comment for the exact upstream name and file:line it mirrors (as of
 *   REL_19_BETA1).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WAL_SEGMENT_H
#define WS_WAL_SEGMENT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * wal_segments_per_xlogid mirrors XLogSegmentsPerXLogId(wal_segsz_bytes)
 * (xlog_internal.h:99-100): how many segments make up one "logId" --
 * the high 32 bits of a segment number, in the traditional
 * "%08X%08X%08X" (tli/logId/seg) WAL filename split.
 */
uint64_t wal_segments_per_xlogid(uint64_t segSize);

/*
 * wal_segment_name_is_valid mirrors IsXLogFileName (xlog_internal.h:
 * 178-183): true when name is exactly 24 hexadecimal digits, no more, no
 * less (so a ".partial"/".backup"/etc suffixed name is excluded).
 */
bool wal_segment_name_is_valid(const char *name);

/*
 * wal_segment_name_is_partial mirrors IsPartialXLogFileName
 * (xlog_internal.h:190-196): true when name is a complete WAL segment
 * name (wal_segment_name_is_valid()) immediately followed by ".partial" --
 * the shape pg_receivewal (and this project's own embedded receivewal
 * worker) uses for a segment still being written.
 */
bool wal_segment_name_is_partial(const char *name);

/*
 * wal_backup_history_name_is_valid mirrors IsBackupHistoryFileName
 * (xlog_internal.h:251-257): true when name has the
 * "<24hex><8hex>.backup" shape pg_basebackup's own backup history file
 * uses. Deliberately matches upstream's own lenient suffix-only check
 * (anything of length > 24 hex digits ending in ".backup", not a fixed
 * total length) -- see this function's own callers for which shape each
 * one actually needs.
 */
bool wal_backup_history_name_is_valid(const char *name);

/*
 * wal_segment_name_format mirrors XLogFileName (xlog_internal.h:164-170):
 * formats the 24-hex-digit WAL segment filename for timeline/segno/segSize
 * into dest (at least 25 bytes). Argument order is timeline, segno,
 * segSize, dest, destSize -- dest/destSize last, matching this project's
 * other WAL-segment functions (unlike sformat(), which takes dest first).
 */
void wal_segment_name_format(uint32_t timeline, uint64_t segno,
							 uint64_t segSize, char *dest, size_t destSize);

/*
 * wal_segment_name_parse mirrors XLogFromFileName (xlog_internal.h:
 * 198-206): decodes a 24-hex WAL segment name (or the leading 24-hex
 * prefix of a ".partial"/".backup" name) back into its own timeline and
 * 0-based segment number. Does not itself validate name's shape -- callers
 * that need that check first via wal_segment_name_is_valid()/_is_partial()/
 * wal_backup_history_name_is_valid(), exactly as upstream's own
 * XLogFromFileName() callers already validate via IsXLogFileName() first.
 */
void wal_segment_name_parse(const char *name, uint64_t segSize,
							uint32_t *timelineOut, uint64_t *segnoOut);

/*
 * wal_segment_name_extract_prefix extracts the 24-hex WAL-segment prefix a
 * filename's retention/inventory decision is keyed on, recognizing the
 * three shapes real pg_archivecleanup's own
 * SetWALFileNameForCleanup()/CleanupPriorWALFiles()
 * (src/bin/pg_archivecleanup/pg_archivecleanup.c) recognize: a plain
 * segment (wal_segment_name_is_valid()), a ".partial" segment
 * (wal_segment_name_is_partial()), or a "<24hex>.<8hex>.backup" backup
 * history file (wal_backup_history_name_is_valid(), with the stricter
 * exact-length shape this project's callers need -- see that function's
 * own comment). prefixOut (at least 25 bytes) receives the 24-hex prefix
 * on success. Returns false (not one of these three shapes) for anything
 * else, including a "<8hex>.history" timeline history file, which carries
 * no WAL-segment prefix of this kind at all.
 */
bool wal_segment_name_extract_prefix(const char *name, char *prefixOut);

/*
 * wal_lsn_to_segno mirrors XLByteToSeg (xlog_internal.h:116-117) composed
 * with real Postgres's own pg_parse_lsn validation rule (this project's
 * own parseLSN(), src/bin/common/parsing.c): parses lsn ("%X/%X") and
 * divides it by segSize to get its 0-based segment number. Returns false
 * (segnoOut untouched) when lsn fails to parse -- parseLSN()'s own
 * validation (1-8 hex digits per component, exactly one '/', no trailing
 * junk) is strictly more careful than a bare "%X/%X" scanf-style parse
 * would be.
 */
bool wal_lsn_to_segno(const char *lsn, uint64_t segSize, uint64_t *segnoOut);

/*
 * wal_lsn_to_segment_name composes wal_lsn_to_segno() and
 * wal_segment_name_format(): parses lsn, divides by segSize, and formats
 * the resulting segment number (on timeline) into segmentOut (at least 25
 * bytes). Returns false (segmentOut untouched) exactly when wal_lsn_to_
 * segno() itself would.
 */
bool wal_lsn_to_segment_name(const char *lsn, uint32_t timeline,
							 uint64_t segSize, char *segmentOut,
							 size_t segmentOutSize);

#endif /* WS_WAL_SEGMENT_H */
