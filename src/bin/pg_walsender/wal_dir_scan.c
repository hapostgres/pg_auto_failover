/*
 * src/bin/pg_walsender/wal_dir_scan.c
 *   See wal_dir_scan.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <string.h>

#include "postgres_fe.h"

#include "wal_dir_scan.h"
#include "file_utils.h"
#include "string_utils.h"

/* default WAL segment size (16MB), matching cmd_show.c's own
 * "SHOW wal_segment_size" -> "16MB" answer */
#define WS_WAL_SEGMENT_SIZE UINT64CONST(0x1000000)
#define WS_XLOG_SEGMENTS_PER_XLOGID (UINT64CONST(0x100000000) / WS_WAL_SEGMENT_SIZE)

#define WS_WAL_FNAME_LEN 24


static bool
is_wal_segment_filename(const char *name)
{
	size_t len = strlen(name);

	if (len != WS_WAL_FNAME_LEN)
	{
		return false;
	}

	for (size_t i = 0; i < len; i++)
	{
		if (!isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


void
wal_segment_filename(uint32_t timeline, uint64_t segno, char *dest, size_t destSize)
{
	uint32_t logId = (uint32_t) (segno / WS_XLOG_SEGMENTS_PER_XLOGID);
	uint32_t seg = (uint32_t) (segno % WS_XLOG_SEGMENTS_PER_XLOGID);

	sformat(dest, destSize, "%08X%08X%08X", timeline, logId, seg);
}


bool
wal_dir_find_latest(const char *walcacheDir, uint32_t *timeline,
					char *endLsn, size_t endLsnSize)
{
	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		return false;
	}

	char best[WS_WAL_FNAME_LEN + 1] = { 0 };
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (!is_wal_segment_filename(entry->d_name))
		{
			continue;
		}

		if (best[0] == '\0' || strcmp(entry->d_name, best) > 0)
		{
			strlcpy(best, entry->d_name, sizeof(best));
		}
	}

	closedir(dir);

	if (best[0] == '\0')
	{
		return false;
	}

	char tliHex[9] = { 0 };
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(tliHex, best, 8); /* IGNORE-BANNED */
	memcpy(logIdHex, best + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, best + 16, 8); /* IGNORE-BANNED */

	uint32_t tli = (uint32_t) strtoul(tliHex, NULL, 16);
	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * WS_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t endOfSegment = (segno + 1) * WS_WAL_SEGMENT_SIZE;

	*timeline = tli;
	sformat(endLsn, endLsnSize, "%X/%08X",
			(uint32_t) (endOfSegment >> 32), (uint32_t) (endOfSegment & 0xFFFFFFFF));

	return true;
}


bool
wal_position_cache_read(const char *path, uint32_t *timeline,
						char *lsn, size_t lsnSize)
{
	char cachePath[MAXPGPATH] = { 0 };

	sformat(cachePath, sizeof(cachePath), "%s/archiver-position", path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(cachePath, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool foundLsn = false;
	bool foundTimeline = false;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		const char *lsnPrefix = "lsn = ";
		const char *tliPrefix = "timeline = ";

		if (strncmp(line, lsnPrefix, strlen(lsnPrefix)) == 0)
		{
			strlcpy(lsn, line + strlen(lsnPrefix), lsnSize);
			foundLsn = lsn[0] != '\0';
		}
		else if (strncmp(line, tliPrefix, strlen(tliPrefix)) == 0)
		{
			foundTimeline =
				stringToUInt32(line + strlen(tliPrefix), timeline) &&
				*timeline > 0;
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return foundLsn && foundTimeline;
}
