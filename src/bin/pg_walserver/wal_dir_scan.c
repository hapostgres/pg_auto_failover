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
#include "wal_segment.h"

/* default WAL segment size (16MB) when the cluster has no pg_walserver_walsegsize */
#define WS_DEFAULT_WAL_SEGMENT_SIZE UINT64CONST(0x1000000)
#define WS_MIN_WAL_SEGMENT_SIZE UINT64CONST(0x100000)
#define WS_MAX_WAL_SEGMENT_SIZE UINT64CONST(0x40000000)

#define WS_WAL_FNAME_LEN 24


/*
 * ws_cluster_wal_segment_size returns the cluster's own configured WAL segment
 * size, read from its "pg_walserver_walsegsize" file (a bare decimal byte count,
 * written once by pg_autoctl when the archiver first learns it from the
 * group's real primary). Falls back to WS_DEFAULT_WAL_SEGMENT_SIZE (16MB)
 * when cluster is NULL/has no path, the file is absent, or its content isn't a
 * valid power-of-two size in [WS_MIN_WAL_SEGMENT_SIZE,
 * WS_MAX_WAL_SEGMENT_SIZE] (logged as an error in that last case). Every
 * segment number/name/LSN computation and "SHOW wal_segment_size" derive
 * from it.
 */
uint64_t
ws_cluster_wal_segment_size(const WsCluster *cluster)
{
	if (cluster == NULL || cluster->path[0] == '\0')
	{
		return WS_DEFAULT_WAL_SEGMENT_SIZE;
	}

	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/pg_walserver_walsegsize", cluster->path);

	char *contents = NULL;
	size_t size = 0;

	/* absent: the archiver has not recorded one, the default applies */
	if (!read_file_capped(path, 64, true, &contents, &size, NULL))
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
 * wal_dir_find_latest scans the cluster's directory for the highest-numbered complete
 * WAL segment (24 hex chars, no ".partial" suffix). On success, returns
 * true with *timeline set and endLsn filled with that segment's end-of-
 * segment LSN (formatted "%X/%08X", matching pg_lsn's own text form) --
 * the natural "resume from here" position once this segment is fully
 * captured. Returns false (not an error, *timeline and *endLsn untouched)
 * if the directory has no WAL segments yet.
 */
bool
wal_dir_find_latest(const WsCluster *cluster, uint32_t *timeline,
					char *endLsn, size_t endLsnSize)
{
	uint64_t segSize = ws_cluster_wal_segment_size(cluster);
	DIR *dir = opendir(cluster->path);

	if (dir == NULL)
	{
		return false;
	}

	char best[WS_WAL_FNAME_LEN + 1] = { 0 };
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (!wal_segment_name_is_valid(entry->d_name))
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

	wal_segment_name_parse(best, segSize, &tli, &segno);

	uint64_t endOfSegment = (segno + 1) * segSize;

	*timeline = tli;
	sformat(endLsn, endLsnSize, "%X/%08X",
			(uint32_t) (endOfSegment >> 32), (uint32_t) (endOfSegment & 0xFFFFFFFF));

	return true;
}


/*
 * wal_dir_has_any_segment returns true as soon as cluster->path holds at
 * least one WAL segment file, complete OR still ".partial" -- unlike wal_
 * dir_find_latest() above (complete segments only, the right conservative
 * choice for a "resume from here" position), this is a plain "has a
 * receivewal worker connected and begun streaming into this cluster at all yet"
 * check: a receivewal worker whose only activity so far is its very first, still-
 * growing ".partial" segment (the common case moments after it starts)
 * must count as "yes" here, or a caller polling for readiness would spin
 * until an entire segment happens to fill, which may never even happen
 * during a short-lived caller's own bounded wait. See cli_setup.c's own
 * "prime the embedded receivewal worker before taking the first base backup" use.
 */
bool
wal_dir_has_any_segment(const WsCluster *cluster)
{
	if (cluster == NULL || cluster->path[0] == '\0')
	{
		return false;
	}

	DIR *dir = opendir(cluster->path);

	if (dir == NULL)
	{
		return false;
	}

	bool found = false;
	struct dirent *entry;

	while (!found && (entry = readdir(dir)) != NULL)
	{
		if (wal_segment_name_is_valid(entry->d_name) ||
			wal_segment_name_is_partial(entry->d_name))
		{
			found = true;
			break;
		}
	}

	closedir(dir);

	return found;
}


/*
 * ws_wal_dir_classify_filename classifies name into one of WsWalFileKind's
 * five shapes. For WS_WAL_FILE_SEGMENT/PARTIAL/BACKUP, segmentOut (when not
 * NULL, at least 25 bytes) receives the 24-hex WAL segment prefix.
 */
WsWalFileKind
ws_wal_dir_classify_filename(const char *name, char *segmentOut)
{
	size_t len = strlen(name);

	if (wal_segment_name_is_valid(name))
	{
		if (segmentOut != NULL)
		{
			memcpy(segmentOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
			segmentOut[WS_WAL_FNAME_LEN] = '\0';
		}
		return WS_WAL_FILE_SEGMENT;
	}

	if (wal_segment_name_is_partial(name))
	{
		if (segmentOut != NULL)
		{
			memcpy(segmentOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
			segmentOut[WS_WAL_FNAME_LEN] = '\0';
		}
		return WS_WAL_FILE_PARTIAL;
	}

	/* "<24hex>.<8hex>.backup" */
	const char *backupSuffix = ".backup";

	if (len == WS_WAL_FNAME_LEN + 1 + 8 + strlen(backupSuffix) &&
		name[WS_WAL_FNAME_LEN] == '.' &&
		wal_backup_history_name_is_valid(name))
	{
		if (segmentOut != NULL)
		{
			memcpy(segmentOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
			segmentOut[WS_WAL_FNAME_LEN] = '\0';
		}
		return WS_WAL_FILE_BACKUP;
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


/*
 * ws_receivewal_progress_read reads it back: lsn/timeline/observedAt are
 * only set on success. Returns false (untouched) when the cluster has no such
 * file yet (its receivewal worker has never ticked, isn't running, or the
 * cluster isn't "receivewal = pull" at all) or it fails to parse -- callers
 * must treat that as "no live reading available", never as an error.
 */
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
