/*
 * src/bin/pg_walserver/cli_archive_cleanup.c
 *   See cli_archive_cleanup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_archive_cleanup.h"
#include "cmd_base_backup.h"
#include "file_utils.h"
#include "log.h"
#include "routes.h"
#include "string_utils.h"
#include "wal_dir_scan.h"

#define WS_WAL_FNAME_LEN 24
#define WS_BACKUPS_SUBDIR "basebackups"
#define WS_LATEST_FILENAME "basebackups/.latest"

/* one enumerated base backup directory under <path>/basebackups/ */
typedef struct WsCleanupBackup
{
	char label[NAMEDATALEN];        /* "basebackup-20260101T000000Z" */
	char dirPath[MAXPGPATH];
	time_t takenAt;                 /* parsed out of the label itself */
	bool haveStart;                 /* backup_label parsed successfully */
	char startSegment[WS_WAL_FNAME_LEN + 1]; /* this backup's own required
	                                          * starting WAL segment */
} WsCleanupBackup;


/* ---------------------------------------------------------------------
 * --keep-age parsing
 * --------------------------------------------------------------------- */
bool
ws_parse_retention_age(const char *str, WsRetentionAge *age)
{
	size_t len = str == NULL ? 0 : strlen(str);

	if (len < 2)
	{
		log_error("Invalid --keep-age value \"%s\": expected a number "
				  "followed by one of \"h\" (hours), \"d\" (days), \"w\" "
				  "(weeks), or \"m\" (calendar months), e.g. \"72h\", "
				  "\"30d\", \"4w\", \"3m\" -- an explicit suffix is "
				  "required, there is no bare-number default",
				  str == NULL ? "" : str);
		return false;
	}

	char unit = str[len - 1];

	if (unit != 'h' && unit != 'd' && unit != 'w' && unit != 'm')
	{
		log_error("Invalid --keep-age value \"%s\": unrecognized suffix "
				  "\"%c\" -- accepted suffixes are \"h\" (hours), \"d\" "
				  "(days), \"w\" (weeks), and \"m\" (calendar months)",
				  str, unit);
		return false;
	}

	char numberPart[32] = { 0 };

	if (len - 1 >= sizeof(numberPart))
	{
		log_error("Invalid --keep-age value \"%s\": number is too long", str);
		return false;
	}

	memcpy(numberPart, str, len - 1); /* IGNORE-BANNED */
	numberPart[len - 1] = '\0';

	int64_t value = 0;

	if (!stringToInt64(numberPart, &value) || value <= 0 || value > 100000)
	{
		log_error("Invalid --keep-age value \"%s\": expected a positive "
				  "whole number before the \"%c\" suffix", str, unit);
		return false;
	}

	age->value = (long) value;
	age->unit = unit;

	return true;
}


/*
 * ws_retention_age_cutoff computes the timestamp before which a base
 * backup counts as expired under --keep-age. 'h'/'d'/'w' are plain fixed
 * durations; 'm' is real calendar-month arithmetic on UTC struct tm
 * fields plus timegm() -- deliberately not "value * 30 days", since month
 * lengths vary. A day-of-month that doesn't exist in the target month
 * (e.g. going back one month from March 31st, where February 31st doesn't
 * exist) normalizes forward the same way mktime()/timegm() always
 * normalizes an out-of-range struct tm -- ordinary, documented behavior,
 * not a bug.
 */
static time_t
ws_retention_age_cutoff(const WsRetentionAge *age, time_t now)
{
	if (age->unit == 'm')
	{
		struct tm tmNow = { 0 };

		gmtime_r(&now, &tmNow);
		tmNow.tm_mon -= (int) age->value;

		return timegm(&tmNow);
	}

	long secondsPerUnit;

	switch (age->unit)
	{
		case 'h':
		{
			secondsPerUnit = 3600L;
			break;
		}

		case 'd':
		{
			secondsPerUnit = 86400L;
			break;
		}

		case 'w':
		default:
		{
			secondsPerUnit = 604800L;
			break;
		}
	}

	return now - (age->value * secondsPerUnit);
}


/* ---------------------------------------------------------------------
 * WAL/.partial/.backup filename helpers -- same shapes wal_dir_scan.c's
 * own is_wal_segment_filename() (not exported) recognizes, plus the
 * ".backup" and ".history" forms it has no need to.
 * --------------------------------------------------------------------- */
static bool
is_hex_run(const char *name, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		if (!isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


static bool
is_wal_segment_name(const char *name)
{
	return strlen(name) == WS_WAL_FNAME_LEN && is_hex_run(name, WS_WAL_FNAME_LEN);
}


/*
 * wal_prefix_from_name extracts the 24-hex WAL-segment prefix a filename's
 * retention decision is keyed on, the same three shapes real
 * pg_archivecleanup's own SetWALFileNameForCleanup()/CleanupPriorWALFiles()
 * recognize (src/bin/pg_archivecleanup/pg_archivecleanup.c): a plain
 * segment, a ".partial" segment, or a "<24hex>.<8hex>.backup" backup
 * history file. Returns false (not one of these three shapes) for
 * anything else, including a "<8hex>.history" timeline history file --
 * see cli_archive_cleanup_run()'s own comment on why those are never
 * removed by this function at all.
 */
static bool
wal_prefix_from_name(const char *name, char *prefixOut)
{
	size_t len = strlen(name);

	if (is_wal_segment_name(name))
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	const char *partialSuffix = ".partial";
	size_t partialLen = strlen(partialSuffix);

	if (len == WS_WAL_FNAME_LEN + partialLen &&
		strcmp(name + WS_WAL_FNAME_LEN, partialSuffix) == 0 &&
		is_hex_run(name, WS_WAL_FNAME_LEN))
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	/* "<24hex>.<8hex>.backup", exactly IsBackupHistoryFileName()'s shape */
	if (len == WS_WAL_FNAME_LEN + 1 + 8 + strlen(".backup") &&
		name[WS_WAL_FNAME_LEN] == '.' &&
		is_hex_run(name, WS_WAL_FNAME_LEN) &&
		is_hex_run(name + WS_WAL_FNAME_LEN + 1, 8) &&
		strcmp(name + WS_WAL_FNAME_LEN + 1 + 8, ".backup") == 0)
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	return false;
}


/* ---------------------------------------------------------------------
 * Base backup enumeration
 * --------------------------------------------------------------------- */
static int
backup_cmp(const void *a, const void *b)
{
	const WsCleanupBackup *ba = (const WsCleanupBackup *) a;
	const WsCleanupBackup *bb = (const WsCleanupBackup *) b;

	return strcmp(ba->label, bb->label);
}


/*
 * parse_backup_label_time parses this project's own base backup directory
 * naming scheme, "basebackup-<UTC timestamp>Z" (cli_basebackup.c's own
 * strftime("basebackup-%Y%m%dT%H%M%SZ", ...)), back into a time_t, entirely
 * with a fixed-format scan (IGNORE-BANNED below) and timegm() -- the same
 * fixed-format-string style this project's own backup_label parsing
 * (cmd_base_backup.c's read_backup_label()) already uses, rather than
 * introducing strptime().
 */
static bool
parse_backup_label_time(const char *label, time_t *takenAt)
{
	const char *prefix = "basebackup-";
	size_t prefixLen = strlen(prefix);

	if (strncmp(label, prefix, prefixLen) != 0)
	{
		return false;
	}

	int y, mo, d, h, mi, s;

	if (sscanf(label + prefixLen, "%4d%2d%2dT%2d%2d%2dZ", /* IGNORE-BANNED */
			   &y, &mo, &d, &h, &mi, &s) != 6)
	{
		return false;
	}

	struct tm tm = { 0 };

	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;

	*takenAt = timegm(&tm);

	return *takenAt != (time_t) -1;
}


/*
 * lsn_and_segsize_to_segment converts an "%X/%08X"-formatted LSN plus a
 * route's own WAL segment size into the 24-hex segment filename that LSN
 * falls in, using the exact same math wal_dir_scan.c's own wal_segment_
 * filename() and wal_dir_find_latest() already use.
 */
static bool
lsn_to_segment(const char *lsn, uint32_t timeline, uint64_t segSize,
			   char *segmentOut, size_t segmentOutSize)
{
	uint32_t hi, lo;

	if (sscanf(lsn, "%X/%X", &hi, &lo) != 2) /* IGNORE-BANNED */
	{
		return false;
	}

	uint64_t lsnValue = ((uint64_t) hi << 32) | lo;
	uint64_t segno = lsnValue / segSize;

	wal_segment_filename(timeline, segno, segSize, segmentOut, segmentOutSize);

	return true;
}


/*
 * load_backups scans <routePath>/basebackups/ for backup directories,
 * parses each one's own label timestamp and (via read_backup_label(),
 * cmd_base_backup.c) its own required starting WAL segment, and returns
 * them sorted oldest-first (label strings sort chronologically). Returns
 * true even when there are zero backups (an empty, not-yet-used route);
 * false only on a directory that cannot be opened at all.
 */
static bool
load_backups(const char *routePath, uint64_t segSize,
			 WsCleanupBackup **backupsOut, int *countOut)
{
	char backupsDir[MAXPGPATH] = { 0 };

	sformat(backupsDir, sizeof(backupsDir), "%s/%s", routePath, WS_BACKUPS_SUBDIR);

	*backupsOut = NULL;
	*countOut = 0;

	DIR *dir = opendir(backupsDir);

	if (dir == NULL)
	{
		/* no basebackups/ directory yet at all: nothing to enumerate */
		return true;
	}

	int capacity = 16;
	WsCleanupBackup *backups = (WsCleanupBackup *)
							   malloc(capacity * sizeof(WsCleanupBackup));
	int count = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
			strcmp(entry->d_name, ".latest") == 0)
		{
			continue;
		}

		char entryPath[MAXPGPATH] = { 0 };

		sformat(entryPath, sizeof(entryPath), "%s/%s", backupsDir, entry->d_name);

		if (!directory_exists(entryPath))
		{
			continue;
		}

		if (count == capacity)
		{
			capacity *= 2;
			backups = (WsCleanupBackup *)
					  realloc(backups, capacity * sizeof(WsCleanupBackup));
		}

		WsCleanupBackup *backup = &(backups[count]);

		memset(backup, 0, sizeof(WsCleanupBackup));
		strlcpy(backup->label, entry->d_name, sizeof(backup->label));
		strlcpy(backup->dirPath, entryPath, sizeof(backup->dirPath));

		if (!parse_backup_label_time(backup->label, &(backup->takenAt)))
		{
			log_warn("archive-cleanup: \"%s\" does not look like a "
					 "pg_walserver base backup directory name; leaving it "
					 "alone", backup->dirPath);
			backup->takenAt = 0;
		}

		char lsn[MAXPGPATH] = { 0 };
		int timeline = 0;

		if (read_backup_label(backup->dirPath, lsn, sizeof(lsn), &timeline))
		{
			backup->haveStart = lsn_to_segment(lsn, (uint32_t) timeline, segSize,
											   backup->startSegment,
											   sizeof(backup->startSegment));
		}

		if (!backup->haveStart)
		{
			log_warn("archive-cleanup: could not determine \"%s\"'s own "
					 "required starting WAL segment (missing or "
					 "unparseable backup_label); leaving this backup "
					 "alone this run", backup->dirPath);
		}

		count++;
	}

	closedir(dir);

	qsort(backups, count, sizeof(WsCleanupBackup), backup_cmp); /* IGNORE-BANNED */

	*backupsOut = backups;
	*countOut = count;

	return true;
}


/* ---------------------------------------------------------------------
 * Main entry point
 * --------------------------------------------------------------------- */
bool
ws_archive_cleanup_run(const char *routePath,
					   bool haveKeepCount, int keepCount,
					   bool haveKeepAge, WsRetentionAge keepAge,
					   bool dryRun)
{
	if (!haveKeepCount && !haveKeepAge)
	{
		log_error("archive-cleanup requires --keep-count and/or --keep-age "
				  "-- retention is infinite by default, and running with "
				  "neither would mean \"delete everything\", which this "
				  "tool refuses to do implicitly");
		return false;
	}

	if (!directory_exists(routePath))
	{
		log_error("archive-cleanup: \"%s\" is not a directory", routePath);
		return false;
	}

	WsRoute route = { 0 };

	strlcpy(route.path, routePath, sizeof(route.path));

	uint64_t segSize = ws_route_wal_segment_size(&route);

	WsCleanupBackup *backups = NULL;
	int backupCount = 0;

	if (!load_backups(routePath, segSize, &backups, &backupCount))
	{
		log_error("archive-cleanup: could not read \"%s/%s\"",
				  routePath, WS_BACKUPS_SUBDIR);
		return false;
	}

	if (backupCount == 0)
	{
		log_warn("archive-cleanup: no base backups found under \"%s/%s\"; "
				 "nothing to anchor WAL retention against, leaving \"%s\" "
				 "untouched", routePath, WS_BACKUPS_SUBDIR, routePath);
		free(backups);
		return true;
	}

	/* which backup does basebackups/.latest currently point to? */
	char latestPath[MAXPGPATH] = { 0 };
	char *latestContents = NULL;
	long latestSize = 0;

	sformat(latestPath, sizeof(latestPath), "%s/%s", routePath, WS_LATEST_FILENAME);

	if (!read_file_if_exists(latestPath, &latestContents, &latestSize) ||
		latestContents == NULL || latestSize == 0)
	{
		log_error("archive-cleanup: \"%s\" is missing or empty -- refusing "
				  "to run without a known \"latest\" backup to protect",
				  latestPath);
		free(backups);
		return false;
	}

	char latestLabel[NAMEDATALEN] = { 0 };

	strlcpy(latestLabel, latestContents, sizeof(latestLabel));
	free(latestContents);

	/* trim a trailing newline, the same way cli_basebackup_run() writes it */
	size_t latestLen = strlen(latestLabel);

	if (latestLen > 0 && latestLabel[latestLen - 1] == '\n')
	{
		latestLabel[latestLen - 1] = '\0';
	}

	int latestIndex = -1;

	for (int i = 0; i < backupCount; i++)
	{
		if (strcmp(backups[i].label, latestLabel) == 0)
		{
			latestIndex = i;
			break;
		}
	}

	if (latestIndex == -1)
	{
		log_error("archive-cleanup: the backup named by \"%s\" (\"%s\") "
				  "does not exist under \"%s/%s\" -- refusing to run "
				  "without a known \"latest\" backup to protect",
				  latestPath, latestLabel, routePath, WS_BACKUPS_SUBDIR);
		free(backups);
		return false;
	}

	if (!backups[latestIndex].haveStart)
	{
		log_error("archive-cleanup: the latest backup (\"%s\") has no "
				  "readable starting WAL position -- refusing to run "
				  "without knowing what WAL it requires",
				  backups[latestIndex].dirPath);
		free(backups);
		return false;
	}

	/* --- decide which backups are kept, oldest to newest --- */
	time_t now = time(NULL);
	time_t ageCutoffTime = haveKeepAge ? ws_retention_age_cutoff(&keepAge, now) : 0;
	int countCutoffIndex = haveKeepCount ? (backupCount - keepCount) : 0;

	bool *keptByCount = (bool *) calloc(backupCount, sizeof(bool));
	bool *keptByAge = (bool *) calloc(backupCount, sizeof(bool));
	bool *kept = (bool *) calloc(backupCount, sizeof(bool));

	for (int i = 0; i < backupCount; i++)
	{
		keptByCount[i] = i == latestIndex ||
						 (haveKeepCount && i >= countCutoffIndex);
		keptByAge[i] = i == latestIndex ||
					   (haveKeepAge && backups[i].takenAt >= ageCutoffTime);

		if (haveKeepCount && haveKeepAge)
		{
			/* more conservative wins: kept if EITHER rule wants it kept,
			 * i.e. only removed when BOTH rules independently agree it
			 * may go -- never delete something either flag alone would
			 * still want kept */
			kept[i] = keptByCount[i] || keptByAge[i];
		}
		else if (haveKeepCount)
		{
			kept[i] = keptByCount[i];
		}
		else
		{
			kept[i] = keptByAge[i];
		}
	}

	/* the combined WAL retention cutoff: the required starting segment of
	 * the oldest still-kept backup -- always <= the latest backup's own
	 * starting segment, since the latest backup is always kept */
	int cutoffIndex = latestIndex;

	for (int i = 0; i < backupCount; i++)
	{
		if (kept[i] && backups[i].haveStart)
		{
			cutoffIndex = i;
			break;
		}
	}

	char combinedCutoff[WS_WAL_FNAME_LEN + 1] = { 0 };

	strlcpy(combinedCutoff, backups[cutoffIndex].startSegment,
			sizeof(combinedCutoff));

	if (haveKeepCount && haveKeepAge)
	{
		log_info("archive-cleanup: --keep-count %d and --keep-age %ld%c "
				 "both given; the more conservative (keeps more) of the "
				 "two wins -- retaining WAL from \"%s\" onward",
				 keepCount, keepAge.value, keepAge.unit, combinedCutoff);
	}
	else if (haveKeepCount)
	{
		log_info("archive-cleanup: --keep-count %d -- retaining WAL from "
				 "\"%s\" onward", keepCount, combinedCutoff);
	}
	else
	{
		log_info("archive-cleanup: --keep-age %ld%c -- retaining WAL from "
				 "\"%s\" onward", keepAge.value, keepAge.unit, combinedCutoff);
	}

	/* --- remove non-kept, non-superseded-anyway backups --- */
	for (int i = 0; i < backupCount; i++)
	{
		if (i == latestIndex)
		{
			continue;
		}

		WsCleanupBackup *backup = &(backups[i]);
		bool supersededByMissingWal = false;

		if (backup->haveStart)
		{
			char segPath[MAXPGPATH] = { 0 };

			sformat(segPath, sizeof(segPath), "%s/%s", routePath,
					backup->startSegment);
			supersededByMissingWal = !file_exists(segPath);
		}

		if (supersededByMissingWal)
		{
			if (dryRun)
			{
				log_info("archive-cleanup: [dry run] would remove backup "
						 "\"%s\": superseded (its own required starting "
						 "WAL segment \"%s\" is already missing)",
						 backup->dirPath, backup->startSegment);
			}
			else
			{
				log_info("archive-cleanup: removing backup \"%s\": "
						 "superseded (its own required starting WAL "
						 "segment \"%s\" is already missing)",
						 backup->dirPath, backup->startSegment);
				(void) rmtree(backup->dirPath, true);
			}
			continue;
		}

		if (!kept[i])
		{
			const char *reason;

			if (haveKeepCount && haveKeepAge)
			{
				reason = "both --keep-count and --keep-age agree it may "
						 "be removed";
			}
			else if (haveKeepCount)
			{
				reason = "past the --keep-count cutoff";
			}
			else
			{
				reason = "past the --keep-age cutoff";
			}

			if (dryRun)
			{
				log_info("archive-cleanup: [dry run] would remove backup "
						 "\"%s\": %s", backup->dirPath, reason);
			}
			else
			{
				log_info("archive-cleanup: removing backup \"%s\": %s",
						 backup->dirPath, reason);
				(void) rmtree(backup->dirPath, true);
			}
		}
	}

	/* --- remove WAL/.partial/.backup files older than the cutoff --- */
	DIR *dir = opendir(routePath);

	if (dir == NULL)
	{
		log_error("archive-cleanup: could not open \"%s\"", routePath);
		free(backups);
		free(keptByCount);
		free(keptByAge);
		free(kept);
		return false;
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		char prefix[WS_WAL_FNAME_LEN + 1] = { 0 };

		if (!wal_prefix_from_name(entry->d_name, prefix))
		{
			/* not a WAL/.partial/.backup shaped name -- includes
			 * "<8hex>.history" timeline history files, deliberately never
			 * touched here: unlike a WAL segment or a backup history file,
			 * a timeline history file's own filename carries no WAL
			 * position to compare against a retention cutoff at all (the
			 * branch point it records is inside the file, not in its
			 * name), and it is tiny -- not worth inventing a position for
			 * it just to make it eligible for removal */
			continue;
		}

		/* ignore the timeline byte range, exactly like real
		 * pg_archivecleanup's own CleanupPriorWALFiles() does, so a
		 * segment is never pruned prematurely just because it belongs to
		 * a different (e.g. parent) timeline than the cutoff's own */
		if (strcmp(prefix + 8, combinedCutoff + 8) >= 0)
		{
			continue;
		}

		char filePath[MAXPGPATH] = { 0 };

		sformat(filePath, sizeof(filePath), "%s/%s", routePath, entry->d_name);

		if (dryRun)
		{
			log_info("archive-cleanup: [dry run] would remove \"%s\": "
					 "older than the retention cutoff (\"%s\")",
					 filePath, combinedCutoff);
		}
		else
		{
			log_info("archive-cleanup: removing \"%s\": older than the "
					 "retention cutoff (\"%s\")", filePath, combinedCutoff);
			(void) unlink_file(filePath);
		}
	}

	closedir(dir);

	free(backups);
	free(keptByCount);
	free(keptByAge);
	free(kept);

	return true;
}
