/*
 * src/bin/pg_walserver/wal_dir_scan.c
 *   See wal_dir_scan.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "wal_dir_scan.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"
#include "ws_util.h"

/* default WAL segment size (16MB) when the route has no pg_walserver_walsegsize */
#define WS_DEFAULT_WAL_SEGMENT_SIZE UINT64CONST(0x1000000)
#define WS_MIN_WAL_SEGMENT_SIZE UINT64CONST(0x100000)
#define WS_MAX_WAL_SEGMENT_SIZE UINT64CONST(0x40000000)

#define WS_WAL_FNAME_LEN 24


/*
 * is_wal_segment_filename returns true when name looks like a complete WAL
 * segment filename: exactly WS_WAL_FNAME_LEN (24) hexadecimal digits, no
 * more, no less (so ".partial"/".gz"/etc suffixed names are excluded).
 */
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


/*
 * ws_route_wal_segment_size returns the route's own configured WAL segment
 * size, read from its "pg_walserver_walsegsize" file (a bare decimal byte count,
 * written once by pg_autoctl when the archiver first learns it from the
 * group's real primary). Falls back to WS_DEFAULT_WAL_SEGMENT_SIZE (16MB)
 * when route is NULL/has no path, the file is absent, or its content isn't a
 * valid power-of-two size in [WS_MIN_WAL_SEGMENT_SIZE,
 * WS_MAX_WAL_SEGMENT_SIZE] (logged as an error in that last case).
 */
uint64_t
ws_route_wal_segment_size(const WsRoute *route)
{
	if (route == NULL || route->path[0] == '\0')
	{
		return WS_DEFAULT_WAL_SEGMENT_SIZE;
	}

	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/pg_walserver_walsegsize", route->path);

	char *contents = NULL;
	size_t size = 0;

	/* absent: the archiver has not recorded one, the default applies */
	if (!ws_read_file_capped(path, 64, true, &contents, &size, NULL))
	{
		return WS_DEFAULT_WAL_SEGMENT_SIZE;
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
		log_error("Ignoring an invalid WAL segment size in \"%s\": using "
				  "the default", path);
		return WS_DEFAULT_WAL_SEGMENT_SIZE;
	}

	return value;
}


/*
 * ws_wal_segment_size_string formats segSize (a byte count) the way the
 * wal_segment_size GUC prints it ("16MB", "1GB"), the format
 * pg_receivewal/pg_basebackup's own RetrieveWalSegSize() expects to parse
 * back out of a SHOW wal_segment_size reply.
 */
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


void
ws_wal_segment_prefix_to_position(const char *segmentPrefix, uint64_t segSize,
								  uint32_t *timelineOut, uint64_t *segnoOut)
{
	uint64_t perXLogId = UINT64CONST(0x100000000) / segSize;

	char tliHex[9] = { 0 };
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(tliHex, segmentPrefix, 8); /* IGNORE-BANNED */
	memcpy(logIdHex, segmentPrefix + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, segmentPrefix + 16, 8); /* IGNORE-BANNED */

	uint32_t tli = (uint32_t) strtoul(tliHex, NULL, 16);
	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	*timelineOut = tli;
	*segnoOut = (uint64_t) logId * perXLogId + seg;
}


bool
ws_wal_lsn_to_segno(const char *lsn, uint64_t segSize, uint64_t *segnoOut)
{
	uint32_t hi, lo;

	if (sscanf(lsn, "%X/%X", &hi, &lo) != 2) /* IGNORE-BANNED */
	{
		return false;
	}

	uint64_t lsnValue = ((uint64_t) hi << 32) | lo;

	*segnoOut = lsnValue / segSize;

	return true;
}


bool
wal_dir_find_latest(const WsRoute *route, uint32_t *timeline,
					char *endLsn, size_t endLsnSize)
{
	uint64_t segSize = ws_route_wal_segment_size(route);
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

	uint32_t tli;
	uint64_t segno;

	ws_wal_segment_prefix_to_position(best, segSize, &tli, &segno);

	uint64_t endOfSegment = (segno + 1) * segSize;

	*timeline = tli;
	sformat(endLsn, endLsnSize, "%X/%08X",
			(uint32_t) (endOfSegment >> 32), (uint32_t) (endOfSegment & 0xFFFFFFFF));

	return true;
}


bool
wal_dir_has_any_segment(const WsRoute *route)
{
	if (route == NULL || route->path[0] == '\0')
	{
		return false;
	}

	DIR *dir = opendir(route->path);

	if (dir == NULL)
	{
		return false;
	}

	bool found = false;
	struct dirent *entry;

	while (!found && (entry = readdir(dir)) != NULL)
	{
		if (is_wal_segment_filename(entry->d_name))
		{
			found = true;
			break;
		}

		const char *partialSuffix = ".partial";
		size_t nameLen = strlen(entry->d_name);
		size_t suffixLen = strlen(partialSuffix);

		if (nameLen == WS_WAL_FNAME_LEN + suffixLen &&
			strcmp(entry->d_name + WS_WAL_FNAME_LEN, partialSuffix) == 0)
		{
			char segPart[WS_WAL_FNAME_LEN + 1] = { 0 };

			memcpy(segPart, entry->d_name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */

			if (is_wal_segment_filename(segPart))
			{
				found = true;
			}
		}
	}

	closedir(dir);

	return found;
}


/*
 * ws_wal_dir_classify_filename -- see wal_dir_scan.h.
 */
WsWalFileKind
ws_wal_dir_classify_filename(const char *name, char *segmentOut)
{
	size_t len = strlen(name);

	if (is_wal_segment_filename(name))
	{
		if (segmentOut != NULL)
		{
			memcpy(segmentOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
			segmentOut[WS_WAL_FNAME_LEN] = '\0';
		}
		return WS_WAL_FILE_SEGMENT;
	}

	const char *partialSuffix = ".partial";
	size_t partialLen = strlen(partialSuffix);

	if (len == WS_WAL_FNAME_LEN + partialLen &&
		strcmp(name + WS_WAL_FNAME_LEN, partialSuffix) == 0)
	{
		char prefix[WS_WAL_FNAME_LEN + 1] = { 0 };

		memcpy(prefix, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */

		if (is_wal_segment_filename(prefix))
		{
			if (segmentOut != NULL)
			{
				strlcpy(segmentOut, prefix, WS_WAL_FNAME_LEN + 1);
			}
			return WS_WAL_FILE_PARTIAL;
		}
	}

	/* "<24hex>.<8hex>.backup" */
	const char *backupSuffix = ".backup";

	if (len == WS_WAL_FNAME_LEN + 1 + 8 + strlen(backupSuffix) &&
		name[WS_WAL_FNAME_LEN] == '.' &&
		strcmp(name + WS_WAL_FNAME_LEN + 1 + 8, backupSuffix) == 0)
	{
		char prefix[WS_WAL_FNAME_LEN + 1] = { 0 };

		memcpy(prefix, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */

		if (is_wal_segment_filename(prefix))
		{
			if (segmentOut != NULL)
			{
				strlcpy(segmentOut, prefix, WS_WAL_FNAME_LEN + 1);
			}
			return WS_WAL_FILE_BACKUP;
		}
	}

	/* "<8hex>.history" */
	const char *historySuffix = ".history";

	if (len == 8 + strlen(historySuffix) &&
		strcmp(name + 8, historySuffix) == 0)
	{
		bool hexOk = true;

		for (int i = 0; i < 8; i++)
		{
			if (!isxdigit((unsigned char) name[i]))
			{
				hexOk = false;
				break;
			}
		}

		if (hexOk)
		{
			return WS_WAL_FILE_HISTORY;
		}
	}

	return WS_WAL_FILE_OTHER;
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


bool
ws_receivewal_progress_write(const char *path, const char *lsn, uint32_t timeline)
{
	if (path == NULL || path[0] == '\0' || lsn == NULL || lsn[0] == '\0')
	{
		return false;
	}

	char filePath[MAXPGPATH] = { 0 };

	sformat(filePath, sizeof(filePath), "%s/" WS_RECEIVEWAL_PROGRESS_FILENAME, path);

	char content[128] = { 0 };

	sformat(content, sizeof(content), "lsn = %s\ntimeline = %u\nobserved = %lld\n",
			lsn, timeline, (long long) time(NULL));

	return write_file_atomic(content, strlen(content), filePath);
}


bool
ws_receivewal_progress_read(const char *path, char *lsn, size_t lsnSize,
							uint32_t *timeline, time_t *observedAt)
{
	if (path == NULL || path[0] == '\0')
	{
		return false;
	}

	char filePath[MAXPGPATH] = { 0 };

	sformat(filePath, sizeof(filePath), "%s/" WS_RECEIVEWAL_PROGRESS_FILENAME, path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(filePath, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool foundLsn = false;
	bool foundTimeline = false;
	bool foundObserved = false;
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
		const char *obsPrefix = "observed = ";

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
		else if (strncmp(line, obsPrefix, strlen(obsPrefix)) == 0)
		{
			*observedAt = (time_t) atoll(line + strlen(obsPrefix)); /* IGNORE-BANNED */
			foundObserved = *observedAt > 0;
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return foundLsn && foundTimeline && foundObserved;
}
