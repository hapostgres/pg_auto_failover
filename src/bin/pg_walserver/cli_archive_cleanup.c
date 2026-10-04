/*
 * src/bin/pg_walserver/cli_archive_cleanup.c
 *   See cli_archive_cleanup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <getopt.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_archive_cleanup.h"
#include "cli_common.h"
#include "cli_root.h"
#include "cmd_base_backup.h"
#include "cmd_replication_slot.h"
#include "file_utils.h"
#include "log.h"
#include "routes.h"
#include "string_utils.h"
#include "wal_dir_scan.h"
#include "wal_segment.h"

#define WS_WAL_FNAME_LEN 24
#define WS_BACKUPS_SUBDIR "basebackups"
#define WS_LATEST_FILENAME "basebackups/.latest"

/* WsBackupInfo (formerly a private WsCleanupBackup) is now declared in
 * cli_archive_cleanup.h, exported for "pg_walserver list backups"
 * (cli_list.c) to reuse -- see that header's own comment. */

/*
 * WsContinuityProblem carries one WAL-continuity problem's detail string
 * back out of check_wal_range()/ws_check_wal_continuity() below -- declared
 * here, ahead of those functions' own forward declarations, since both take
 * a pointer to it.
 */
typedef struct WsContinuityProblem
{
	bool hasProblem;
	char detail[512];
} WsContinuityProblem;

/* local helpers */
static time_t ws_retention_age_cutoff(const RetentionAge *age, time_t now);
static int backup_cmp(const void *a, const void *b);
static bool parse_backup_label_time(const char *label, time_t *takenAt);
static bool read_last_history_line(const char *routePath, uint32_t timeline,
								   uint32_t *parentTliOut, char *lsnOut,
								   size_t lsnOutSize);
static void check_wal_range(const char *routePath, uint64_t segSize,
							uint32_t startTli, uint64_t startSegno,
							uint32_t endTli, uint64_t endSegno,
							WsContinuityProblem *problem);
static bool ws_check_wal_continuity(const char *routePath, const WsRoute *route,
									uint64_t segSize, WsBackupInfo *backups,
									int backupCount, const bool *kept);

static int cli_archive_cleanup_getopt(int argc, char **argv);
static void cli_archive_cleanup_command_run(int argc, char **argv);


/* -----------------------------------------------------------------------
 * pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path <dir>
 *                               [--keep-count <N>] [--keep-age <interval>]
 *                               [--dry-run]
 * ----------------------------------------------------------------------- */

static char archiveCleanupPgdata[MAXPGPATH] = { 0 };
static char archiveCleanupConfigFile[MAXPGPATH] = { 0 };
static char archiveCleanupRoute[NAMEDATALEN + 16] = { 0 };
static char archiveCleanupPath[MAXPGPATH] = { 0 };
static bool archiveCleanupHaveKeepCount = false;
static int archiveCleanupKeepCount = 0;
static bool archiveCleanupHaveKeepAge = false;
static RetentionAge archiveCleanupKeepAge = { 0 };
static bool archiveCleanupDryRun = false;
static bool archiveCleanupForce = false;

static struct option archiveCleanupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'F' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "keep-count", required_argument, NULL, 'k' },
	{ "keep-age", required_argument, NULL, 'a' },
	{ "dry-run", no_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

CommandLine archive_cleanup_command =
	make_command("archive-cleanup",
				 "Remove WAL/base backups this route no longer needs to "
				 "keep (operator/cron-driven, never automatic)",
				 "--cluster <name> --pgdata <path> [--config <path>] "
				 "| --path <dir> "
				 "[--keep-count <N>] [--keep-age <interval>] [--dry-run] "
				 "[--force]",
				 "  --pgdata      this instance's own data root (defaults "
				 "to PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "                <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster     the cluster name to clean up (looked up "
				 "in the config file)\n"
				 "  --path        the route's own directory (overrides "
				 "the route's own \"path\")\n"
				 "  --keep-count  keep at least this many of the most "
				 "recent base backups\n"
				 "  --keep-age    keep anything from the last <N><unit> "
				 "(h/d/w/m -- hours,\n"
				 "                days, weeks, calendar months); at "
				 "least one of --keep-count/\n"
				 "                --keep-age is required, retention is "
				 "infinite otherwise\n"
				 "  --dry-run, -n print what would be removed without "
				 "removing anything -- still runs\n"
				 "                and reports the WAL-continuity check "
				 "below, pass or fail\n"
				 "  --force, -f   before deleting anything, a pre-flight "
				 "check refuses the whole\n"
				 "                operation if any kept backup would be "
				 "left with a WAL gap, or\n"
				 "                if removing a backup would leave a "
				 "time range with no gap-free\n"
				 "                newer backup to cover it; --force "
				 "bypasses that refusal only (it\n"
				 "                does not change what --keep-count/"
				 "--keep-age decide to remove) --\n"
				 "                a default, unattended cron job should "
				 "NEVER blindly pass this;\n"
				 "                only use it once you've independently "
				 "verified proceeding is\n"
				 "                safe (e.g. an independent backup, or "
				 "an accepted/expected gap)\n",
				 cli_archive_cleanup_getopt, cli_archive_cleanup_command_run);


/*
 * RetentionAge/stringToRetentionAge() -- --keep-age parsing -- have
 * moved to src/bin/common/string_utils.h/.c, shared with cli_basebackup.c.
 *
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
ws_retention_age_cutoff(const RetentionAge *age, time_t now)
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


/*
 * Base backup enumeration
 */
static int
backup_cmp(const void *a, const void *b)
{
	const WsBackupInfo *ba = (const WsBackupInfo *) a;
	const WsBackupInfo *bb = (const WsBackupInfo *) b;

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
 * load_backups scans <routePath>/basebackups/ for backup directories,
 * parses each one's own label timestamp and (via read_backup_label(),
 * cmd_base_backup.c) its own required starting WAL segment, and returns
 * them sorted oldest-first (label strings sort chronologically). Returns
 * true even when there are zero backups (an empty, not-yet-used route);
 * false only on a directory that cannot be opened at all.
 */
bool
ws_backup_list_load(const char *routePath, uint64_t segSize,
					WsBackupInfo **backupsOut, int *countOut)
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
	WsBackupInfo *backups = (WsBackupInfo *)
							malloc(capacity * sizeof(WsBackupInfo));
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
			backups = (WsBackupInfo *)
					  realloc(backups, capacity * sizeof(WsBackupInfo));
		}

		WsBackupInfo *backup = &(backups[count]);

		memset(backup, 0, sizeof(WsBackupInfo));
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
			backup->haveStart = wal_lsn_to_segment_name(lsn, (uint32_t) timeline,
														segSize,
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

	qsort(backups, count, sizeof(WsBackupInfo), backup_cmp); /* IGNORE-BANNED */

	*backupsOut = backups;
	*countOut = count;

	return true;
}


/*
 * WAL-continuity pre-flight check
 *
 * Before any deletion happens, verify that every *kept* backup's own
 * required starting WAL segment can still walk forward, with no missing
 * segment, to wherever it needs to reach: the next newer kept backup's own
 * start segment, or (for the newest kept backup) the newest WAL segment
 * actually present on disk. This is independent of, and additional to,
 * the count/age retention math above -- it catches a WAL gap that has
 * nothing to do with this run's own retention cutoff at all (an
 * archive_command outage, a disk problem, manual tampering, or even a
 * previous archive-cleanup run under different flags).
 *
 * A timeline switch between two kept segments is not by itself a gap: a
 * "%08X.history" file (real PostgreSQL's own TLHistoryFileName() shape,
 * see cmd_timeline_history.c) records, for the timeline it belongs to,
 * the parent timeline and the exact LSN the switch happened at, and this
 * project's server already writes/serves that same file. We walk that
 * ancestry chain from the newer boundary's timeline down to the older
 * one, split the segment-number range at each recorded switchpoint, and
 * require every segment number to be present under whichever timeline
 * owned it at that point in the chain -- never flagging a gap merely
 * because two adjacent kept segments' timeline bytes differ.
 */

#define WS_MAX_TIMELINE_CHAIN 64

/*
 * read_last_history_line reads "<routePath>/%08X.history" (timeline) and
 * returns, in *parentTliOut/lsnOut, the parent timeline and switchpoint LSN
 * from its last non-blank, non-comment line -- the entry that records where
 * *this* timeline itself branched off from *parentTliOut* (a history file
 * may carry more than one line, one per ancestor further back, but the last
 * line is always the immediate parent, exactly how real Postgres's own
 * readTimeLineHistory()/tliOfPointInHistory() reasoning works). Returns
 * false if the file is missing, empty, or has no parseable line.
 */
static bool
read_last_history_line(const char *routePath, uint32_t timeline,
					   uint32_t *parentTliOut, char *lsnOut, size_t lsnOutSize)
{
	char path[MAXPGPATH] = { 0 };

	sformat(path, sizeof(path), "%s/%08X.history", routePath, timeline);

	char *contents = NULL;
	long size = 0;

	if (!read_file_if_exists(path, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool found = false;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		char *p = line;

		while (*p == ' ' || *p == '\t')
		{
			p++;
		}

		if (*p != '\0' && *p != '#')
		{
			unsigned int tli;
			char lsn[64] = { 0 };

			if (sscanf(p, "%u\t%63s", &tli, lsn) == 2 || /* IGNORE-BANNED */
				sscanf(p, "%u %63s", &tli, lsn) == 2) /* IGNORE-BANNED */
			{
				*parentTliOut = (uint32_t) tli;
				strlcpy(lsnOut, lsn, lsnOutSize);
				found = true;
			}
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return found;
}


/*
 * check_wal_range verifies that every WAL segment number from startSegno
 * (on startTli) through endSegno (on endTli, inclusive) is present on
 * disk under routePath, resolving any intervening timeline switch(es) via
 * "%08X.history" files. On the first missing segment, or the first
 * ancestry fact that can't be established, fills *problem and returns --
 * callers only need to check problem->hasProblem.
 */
static void
check_wal_range(const char *routePath, uint64_t segSize,
				uint32_t startTli, uint64_t startSegno,
				uint32_t endTli, uint64_t endSegno,
				WsContinuityProblem *problem)
{
	problem->hasProblem = false;
	problem->detail[0] = '\0';

	if (endTli < startTli)
	{
		sformat(problem->detail, sizeof(problem->detail),
				"cannot verify WAL continuity: the newer boundary is on "
				"timeline %u, older than the earlier boundary's timeline "
				"%u -- this should never happen",
				endTli, startTli);
		problem->hasProblem = true;
		return;
	}

	uint32_t chainTli[WS_MAX_TIMELINE_CHAIN];
	uint64_t chainLower[WS_MAX_TIMELINE_CHAIN];
	int chainLen = 1;

	chainTli[0] = endTli;

	uint32_t cur = endTli;

	while (cur != startTli)
	{
		uint32_t parentTli = 0;
		char lsn[64] = { 0 };

		if (!read_last_history_line(routePath, cur, &parentTli, lsn, sizeof(lsn)))
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity across a timeline switch: "
					"\"%08X.history\" is missing or unreadable under \"%s\", "
					"needed to confirm timeline %u's own ancestry back to "
					"timeline %u", cur, routePath, cur, startTli);
			problem->hasProblem = true;
			return;
		}

		uint64_t switchSegno;

		if (!wal_lsn_to_segno(lsn, segSize, &switchSegno))
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: \"%08X.history\" under "
					"\"%s\" has an unparseable switchpoint LSN (\"%s\")",
					cur, routePath, lsn);
			problem->hasProblem = true;
			return;
		}

		chainLower[chainLen - 1] = switchSegno;

		if (parentTli >= cur || parentTli < startTli)
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: \"%08X.history\" under "
					"\"%s\" names an implausible parent timeline %u",
					cur, routePath, parentTli);
			problem->hasProblem = true;
			return;
		}

		cur = parentTli;

		if (chainLen >= WS_MAX_TIMELINE_CHAIN)
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: more than %d timeline "
					"switches between timeline %u and timeline %u",
					WS_MAX_TIMELINE_CHAIN, startTli, endTli);
			problem->hasProblem = true;
			return;
		}

		chainTli[chainLen] = cur;
		chainLen++;
	}

	chainLower[chainLen - 1] = startSegno;

	/* walk oldest (startTli) to newest (endTli), each timeline owning the
	 * segment-number range [chainLower[k], next boundary) within the
	 * overall [startSegno, endSegno] range being checked */
	for (int k = chainLen - 1; k >= 0; k--)
	{
		uint64_t lower = chainLower[k];

		/* an older timeline whose immediate successor switched away at
		 * segno 0 owns nothing at all within this range -- guard the
		 * subtraction below rather than underflow an unsigned bound */
		if (k > 0 && chainLower[k - 1] == 0)
		{
			continue;
		}

		uint64_t upper = (k == 0) ? endSegno : (chainLower[k - 1] - 1);

		if (lower > upper)
		{
			continue;
		}

		for (uint64_t segno = lower; segno <= upper; segno++)
		{
			char segName[WS_WAL_FNAME_LEN + 1] = { 0 };

			wal_segment_name_format(chainTli[k], segno, segSize,
									segName, sizeof(segName));

			char segPath[MAXPGPATH] = { 0 };

			sformat(segPath, sizeof(segPath), "%s/%s", routePath, segName);

			if (!file_exists(segPath))
			{
				sformat(problem->detail, sizeof(problem->detail),
						"missing WAL segment \"%s\" (needed between "
						"\"%08X%08X%08X\" and \"%08X%08X%08X\")",
						segName,
						startTli, (uint32_t) (startSegno >> 32),
						(uint32_t) startSegno,
						endTli, (uint32_t) (endSegno >> 32),
						(uint32_t) endSegno);
				problem->hasProblem = true;
				return;
			}
		}
	}
}


/*
 * ws_check_wal_continuity runs check_wal_range() (above) for every kept
 * backup in the final kept set: from its own required starting segment
 * through to the next newer kept backup's own start segment, or, for the
 * newest kept backup, through to the newest WAL segment actually present
 * on disk. Logs a specific log_error (naming the backup and the missing
 * segment/range) for every problem found and returns false if any were --
 * callers decide what to do about that (refuse outright, or proceed
 * anyway under --force).
 */
static bool
ws_check_wal_continuity(const char *routePath, const WsRoute *route,
						uint64_t segSize, WsBackupInfo *backups,
						int backupCount, const bool *kept)
{
	bool ok = true;

	int *keptIdx = (int *) malloc(sizeof(int) * backupCount);
	int keptLen = 0;

	for (int i = 0; i < backupCount; i++)
	{
		if (kept[i] && backups[i].haveStart)
		{
			keptIdx[keptLen++] = i;
		}
	}

	for (int k = 0; k < keptLen; k++)
	{
		WsBackupInfo *backup = &(backups[keptIdx[k]]);
		uint32_t startTli;
		uint64_t startSegno;

		if (!wal_segment_name_is_valid(backup->startSegment))
		{
			/* can't happen: startSegment was produced by our own
			 * wal_segment_name_format() when this backup was loaded */
			continue;
		}

		wal_segment_name_parse(backup->startSegment, segSize,
							   &startTli, &startSegno);

		uint32_t endTli = 0;
		uint64_t endSegno = 0;
		bool haveEnd = false;

		if (k + 1 < keptLen)
		{
			WsBackupInfo *next = &(backups[keptIdx[k + 1]]);

			haveEnd = wal_segment_name_is_valid(next->startSegment);

			if (haveEnd)
			{
				wal_segment_name_parse(next->startSegment, segSize,
									   &endTli, &endSegno);
			}
		}
		else
		{
			uint32_t latestTli = 0;
			char latestEndLsn[64] = { 0 };

			if (wal_dir_find_latest(route, &latestTli, latestEndLsn,
									sizeof(latestEndLsn)))
			{
				uint64_t oneAfterSegno;

				if (wal_lsn_to_segno(latestEndLsn, segSize, &oneAfterSegno) &&
					oneAfterSegno > 0)
				{
					endTli = latestTli;
					endSegno = oneAfterSegno - 1;
					haveEnd = true;
				}
			}
		}

		if (!haveEnd)
		{
			/* nothing on disk to compare against at all (a brand new
			 * route, or every recognizable complete segment is gone) --
			 * the least we can require is that this backup's own
			 * required starting segment is itself still present */
			char segPath[MAXPGPATH] = { 0 };

			sformat(segPath, sizeof(segPath), "%s/%s", routePath,
					backup->startSegment);

			if (!file_exists(segPath))
			{
				log_error("archive-cleanup: WAL continuity check failed for "
						  "kept backup \"%s\": its own required starting "
						  "WAL segment \"%s\" is missing, and no WAL "
						  "segment at all is present under \"%s\" to "
						  "compare against", backup->dirPath,
						  backup->startSegment, routePath);
				ok = false;
			}

			continue;
		}

		WsContinuityProblem problem = { 0 };

		check_wal_range(routePath, segSize, startTli, startSegno,
						endTli, endSegno, &problem);

		if (problem.hasProblem)
		{
			log_error("archive-cleanup: WAL continuity check failed for "
					  "kept backup \"%s\" (requires WAL from \"%s\" "
					  "onward): %s", backup->dirPath, backup->startSegment,
					  problem.detail);
			ok = false;
		}
	}

	free(keptIdx);

	return ok;
}


/*
 * Main entry point
 */
bool
ws_archive_cleanup_run(const char *routePath,
					   bool haveKeepCount, int keepCount,
					   bool haveKeepAge, RetentionAge keepAge,
					   bool dryRun, bool force)
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

	WsBackupInfo *backups = NULL;
	int backupCount = 0;

	if (!ws_backup_list_load(routePath, segSize, &backups, &backupCount))
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

	/*
	 * A still-existing replication slot's own restart_lsn is an
	 * unconditional floor, exactly like a real PostgreSQL slot: there is
	 * no flag here to ignore it short of dropping the slot itself
	 * (DROP_REPLICATION_SLOT/"pg_walserver ps" or similar). Unlike a real
	 * primary, where a forgotten slot can silently grow pg_wal until the
	 * disk fills (max_slot_wal_keep_size, when configured, is the only
	 * guard), this is a WARN every single archive-cleanup run logs
	 * loudly by name whenever the slot is the actual reason less was
	 * removed than --keep-count/--keep-age alone would have allowed --
	 * an operator running this on a schedule cannot miss it the way a
	 * real primary's own slow disk-filling often goes unnoticed until
	 * it's critical.
	 */
	char slotName[NAMEDATALEN] = { 0 };
	char slotLsn[32] = { 0 };

	if (ws_replication_slot_oldest_restart_lsn(&route, segSize, slotName,
											   sizeof(slotName), slotLsn,
											   sizeof(slotLsn)))
	{
		uint64_t slotSegno;

		if (wal_lsn_to_segno(slotLsn, segSize, &slotSegno))
		{
			char slotCutoff[WS_WAL_FNAME_LEN + 1] = { 0 };

			wal_segment_name_format(0, slotSegno, segSize, slotCutoff,
									sizeof(slotCutoff));

			if (strcmp(slotCutoff + 8, combinedCutoff + 8) < 0)
			{
				log_warn("archive-cleanup: replication slot \"%s\" (restart_lsn "
						 "%s) needs WAL from \"%s\" onward, older than "
						 "--keep-count/--keep-age alone would have kept -- "
						 "retaining it too; drop the slot (or let it catch "
						 "up) to allow this WAL to be removed",
						 slotName, slotLsn, slotCutoff);
				strlcpy(combinedCutoff, slotCutoff, sizeof(combinedCutoff));
			}
		}
	}

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

	/* --- pre-flight WAL-continuity check on the final kept set, always
	 * computed and reported (dry-run or not) -- see ws_check_wal_
	 * continuity()'s own header comment. Only a real run's actual
	 * deletion is gated on the outcome (and only without --force): a
	 * dry-run never deletes anything regardless, but must still surface
	 * the same problem a real run would refuse over. */
	bool continuityOk = ws_check_wal_continuity(routePath, &route, segSize,
												backups, backupCount, kept);

	if (!continuityOk)
	{
		if (force)
		{
			log_warn("archive-cleanup: proceeding despite the WAL "
					 "continuity problem(s) above because --force was "
					 "given");
		}
		else if (dryRun)
		{
			log_error("archive-cleanup: [dry run] the WAL continuity "
					  "problem(s) above would refuse this operation "
					  "outright on a real run (pass --force to proceed "
					  "anyway once you've verified that is safe)");
		}
		else
		{
			log_fatal("archive-cleanup: refusing to remove anything: one "
					  "or more kept backups would be left without a "
					  "complete, gap-free WAL sequence -- see the "
					  "specific problem(s) logged above. This is a whole-"
					  "operation refusal, nothing has been deleted. Pass "
					  "--force only once you have independently verified "
					  "it is safe to proceed (e.g. an independent backup, "
					  "or an accepted/expected gap) -- a default, "
					  "unattended cron job should never blindly pass "
					  "--force");
			free(backups);
			free(keptByCount);
			free(keptByAge);
			free(kept);
			return false;
		}
	}

	/* --- remove non-kept, non-superseded-anyway backups --- */
	for (int i = 0; i < backupCount; i++)
	{
		if (i == latestIndex)
		{
			continue;
		}

		WsBackupInfo *backup = &(backups[i]);
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

		if (!wal_segment_name_extract_prefix(entry->d_name, prefix))
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

	/* a dry run that found a continuity problem (and wasn't --force'd)
	 * reports it above and deletes nothing either way, but still signals
	 * the problem via its own exit status, matching what a real run
	 * would have refused to do */
	return continuityOk || force;
}


/*
 * cli_archive_cleanup_getopt parses "pg_walserver archive-cleanup"'s own flags into the
 * file-scope statics above.
 */
static int
cli_archive_cleanup_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(archiveCleanupPgdata);
	archiveCleanupConfigFile[0] = '\0';
	archiveCleanupRoute[0] = '\0';
	archiveCleanupPath[0] = '\0';
	archiveCleanupHaveKeepCount = false;
	archiveCleanupKeepCount = 0;
	archiveCleanupHaveKeepAge = false;
	archiveCleanupKeepAge = (RetentionAge) {
		0
	};
	archiveCleanupDryRun = false;
	archiveCleanupForce = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:F:c:P:k:a:nf",
							archiveCleanupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(archiveCleanupPgdata, optarg, sizeof(archiveCleanupPgdata));
				break;
			}

			case 'F':
			{
				strlcpy(archiveCleanupConfigFile, optarg,
						sizeof(archiveCleanupConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(archiveCleanupRoute, optarg, sizeof(archiveCleanupRoute));
				break;
			}

			case 'P':
			{
				strlcpy(archiveCleanupPath, optarg, sizeof(archiveCleanupPath));
				break;
			}

			case 'k':
			{
				if (!stringToInt(optarg, &archiveCleanupKeepCount) ||
					archiveCleanupKeepCount <= 0)
				{
					log_fatal("Invalid --keep-count value \"%s\": expected "
							  "a positive whole number", optarg);
					exit(1);
				}
				archiveCleanupHaveKeepCount = true;
				break;
			}

			case 'a':
			{
				if (!stringToRetentionAge(optarg, &archiveCleanupKeepAge))
				{
					/* error already logged */
					exit(1);
				}
				archiveCleanupHaveKeepAge = true;
				break;
			}

			case 'n':
			{
				archiveCleanupDryRun = true;
				break;
			}

			case 'f':
			{
				archiveCleanupForce = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_archive_cleanup_command_run runs "pg_walserver archive-cleanup" against
 * the options cli_archive_cleanup_getopt parsed above, then exit()s with its
 * own result.
 */
static void
cli_archive_cleanup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	char routePath[MAXPGPATH] = { 0 };

	if (archiveCleanupPath[0] != '\0')
	{
		strlcpy(routePath, archiveCleanupPath, sizeof(routePath));
	}
	else if (archiveCleanupPgdata[0] != '\0' && archiveCleanupRoute[0] != '\0')
	{
		char routesPath[MAXPGPATH] = { 0 };
		WsRoute *routes = NULL;
		int routeCount = 0;

		config_file_path(archiveCleanupPgdata, archiveCleanupConfigFile,
						 routesPath, sizeof(routesPath));

		const WsRoute *route = NULL;

		if (routes_load(routesPath, &routes, &routeCount))
		{
			route = routes_find(routes, routeCount, archiveCleanupRoute);
		}

		if (route == NULL)
		{
			log_fatal("No route \"%s\" in \"%s\"", archiveCleanupRoute,
					  routesPath);
			routes_free(routes);
			exit(1);
		}

		strlcpy(routePath, route->path, sizeof(routePath));
		routes_free(routes);
	}
	else
	{
		log_fatal("archive-cleanup requires --path, or --cluster with "
				  "--pgdata pointing at a \"pg_walserver.ini\" that has "
				  "that route");
		exit(1);
	}

	exit(ws_archive_cleanup_run(routePath,
								archiveCleanupHaveKeepCount, archiveCleanupKeepCount,
								archiveCleanupHaveKeepAge, archiveCleanupKeepAge,
								archiveCleanupDryRun, archiveCleanupForce) ? 0 : 1);
}
