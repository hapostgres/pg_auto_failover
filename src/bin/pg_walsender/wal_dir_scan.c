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
#include "log.h"
#include "string_utils.h"
#include "ws_util.h"

#define WS_MIN_WAL_SEGMENT_SIZE UINT64CONST(0x100000)
#define WS_MAX_WAL_SEGMENT_SIZE UINT64CONST(0x40000000)

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


uint64_t
ws_route_wal_segment_size(const WsRoute *route)
{
	if (route == NULL || route->path[0] == '\0')
	{
		return 0;
	}

	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/archiver-walsegsize", route->path);

	char *contents = NULL;
	size_t size = 0;

	/* absent: the monitor has not told the archiver yet, unknown */
	if (!ws_read_file_capped(path, 64, true, &contents, &size, NULL))
	{
		return 0;
	}

	uint64_t value = 0;
	bool ok = size > 0;

	for (size_t i = 0; ok && i < size; i++)
	{
		if (contents[i] >= '0' && contents[i] <= '9')
		{
			value = value * 10 + (uint64_t) (contents[i] - '0');
		}
		else if (contents[i] == '\n' && i == size - 1)
		{
			break;
		}
		else
		{
			ok = false;
		}

		if (value > WS_MAX_WAL_SEGMENT_SIZE)
		{
			ok = false;
		}
	}

	free(contents);

	if (!ok || value < WS_MIN_WAL_SEGMENT_SIZE ||
		value > WS_MAX_WAL_SEGMENT_SIZE || (value & (value - 1)) != 0)
	{
		log_error("Ignoring an invalid WAL segment size in \"%s\"", path);
		return 0;
	}

	return value;
}


void
ws_wal_segment_size_string(uint64_t segSize, char *dest, size_t destSize)
{
	/* the format of the wal_segment_size GUC, which pg_receivewal and
	 * pg_basebackup parse (RetrieveWalSegSize): "16MB", "1GB" */
	if (segSize >= UINT64CONST(0x40000000))
	{
		sformat(dest, destSize, "%" PRIu64 "GB", segSize >> 30);
	}
	else
	{
		sformat(dest, destSize, "%" PRIu64 "MB", segSize >> 20);
	}
}


void
wal_segment_filename(uint32_t timeline, uint64_t segno, uint64_t segSize,
					 char *dest, size_t destSize)
{
	uint64_t perXLogId = UINT64CONST(0x100000000) / segSize;
	uint32_t logId = (uint32_t) (segno / perXLogId);
	uint32_t seg = (uint32_t) (segno % perXLogId);

	sformat(dest, destSize, "%08X%08X%08X", timeline, logId, seg);
}


bool
wal_dir_find_latest(const WsRoute *route, uint32_t *timeline,
					char *endLsn, size_t endLsnSize)
{
	uint64_t segSize = ws_route_wal_segment_size(route);
	uint64_t perXLogId = UINT64CONST(0x100000000) / segSize;
	DIR *dir = opendir(route->path);

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

	uint64_t segno = (uint64_t) logId * perXLogId + seg;
	uint64_t endOfSegment = (segno + 1) * segSize;

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
