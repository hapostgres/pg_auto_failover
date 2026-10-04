/*
 * src/bin/common/wal_segment.c
 *   See wal_segment.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "parsing.h"
#include "wal_segment.h"

#define WS_WAL_FNAME_LEN 24


/*
 * wal_segments_per_xlogid -- see wal_segment.h.
 */
uint64_t
wal_segments_per_xlogid(uint64_t segSize)
{
	return UINT64CONST(0x100000000) / segSize;
}


/*
 * wal_segment_name_is_valid -- see wal_segment.h.
 */
bool
wal_segment_name_is_valid(const char *name)
{
	return strlen(name) == WS_WAL_FNAME_LEN &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN;
}


/*
 * wal_segment_name_is_partial -- see wal_segment.h.
 */
bool
wal_segment_name_is_partial(const char *name)
{
	const char *partialSuffix = ".partial";
	size_t partialLen = strlen(partialSuffix);

	return strlen(name) == WS_WAL_FNAME_LEN + partialLen &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		   strcmp(name + WS_WAL_FNAME_LEN, partialSuffix) == 0;
}


/*
 * wal_backup_history_name_is_valid -- see wal_segment.h.
 */
bool
wal_backup_history_name_is_valid(const char *name)
{
	const char *backupSuffix = ".backup";
	size_t len = strlen(name);
	size_t suffixLen = strlen(backupSuffix);

	return len > WS_WAL_FNAME_LEN &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		   len > suffixLen &&
		   strcmp(name + len - suffixLen, backupSuffix) == 0;
}


/*
 * wal_segment_name_format -- see wal_segment.h.
 */
void
wal_segment_name_format(uint32_t timeline, uint64_t segno, uint64_t segSize,
						char *dest, size_t destSize)
{
	uint64_t perXLogId = wal_segments_per_xlogid(segSize);
	uint32_t logId = (uint32_t) (segno / perXLogId);
	uint32_t seg = (uint32_t) (segno % perXLogId);

	sformat(dest, destSize, "%08X%08X%08X", timeline, logId, seg);
}


/*
 * wal_segment_name_parse -- see wal_segment.h.
 */
void
wal_segment_name_parse(const char *name, uint64_t segSize,
					   uint32_t *timelineOut, uint64_t *segnoOut)
{
	uint64_t perXLogId = wal_segments_per_xlogid(segSize);

	char tliHex[9] = { 0 };
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(tliHex, name, 8); /* IGNORE-BANNED */
	memcpy(logIdHex, name + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, name + 16, 8); /* IGNORE-BANNED */

	uint32_t tli = (uint32_t) strtoul(tliHex, NULL, 16);
	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	*timelineOut = tli;
	*segnoOut = (uint64_t) logId * perXLogId + seg;
}


/*
 * wal_segment_name_extract_prefix -- see wal_segment.h.
 */
bool
wal_segment_name_extract_prefix(const char *name, char *prefixOut)
{
	if (wal_segment_name_is_valid(name) || wal_segment_name_is_partial(name))
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	/*
	 * "<24hex>.<8hex>.backup": the exact shape real BackupHistoryFileName()
	 * produces (xlog_internal.h), stricter than wal_backup_history_name_
	 * is_valid()'s own upstream-mirrored, suffix-only check -- this
	 * project's own retention/inventory callers need the exact shape to
	 * safely pull out the 8-hex "offset" component, not merely confirm a
	 * ".backup" suffix.
	 */
	size_t len = strlen(name);
	const char *backupSuffix = ".backup";

	if (len == WS_WAL_FNAME_LEN + 1 + 8 + strlen(backupSuffix) &&
		name[WS_WAL_FNAME_LEN] == '.' &&
		strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		strspn(name + WS_WAL_FNAME_LEN + 1, "0123456789ABCDEF") == 8 &&
		strcmp(name + WS_WAL_FNAME_LEN + 1 + 8, backupSuffix) == 0)
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	return false;
}


/*
 * wal_lsn_to_segno -- see wal_segment.h.
 */
bool
wal_lsn_to_segno(const char *lsn, uint64_t segSize, uint64_t *segnoOut)
{
	uint64_t lsnValue;

	if (!parseLSN(lsn, &lsnValue))
	{
		return false;
	}

	*segnoOut = lsnValue / segSize;

	return true;
}


/*
 * wal_lsn_to_segment_name -- see wal_segment.h.
 */
bool
wal_lsn_to_segment_name(const char *lsn, uint32_t timeline, uint64_t segSize,
						char *segmentOut, size_t segmentOutSize)
{
	uint64_t segno;

	if (!wal_lsn_to_segno(lsn, segSize, &segno))
	{
		return false;
	}

	wal_segment_name_format(timeline, segno, segSize, segmentOut, segmentOutSize);

	return true;
}
