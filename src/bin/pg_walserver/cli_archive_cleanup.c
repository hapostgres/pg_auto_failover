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

#include "backup_list.h"
#include "cli_archive_cleanup.h"
#include "cli_common.h"
#include "cli_root.h"
#include "cmd_replication_slot.h"
#include "file_utils.h"
#include "log.h"
#include "clusters.h"
#include "string_utils.h"
#include "wal_dir_scan.h"
#include "wal_segment.h"

#define WS_WAL_FNAME_LEN 24
#define WS_BACKUPS_SUBDIR "basebackups"
#define WS_LATEST_FILENAME "basebackups/.latest"

/* local helpers */
static bool ws_check_wal_continuity(const char *clusterPath, const WsCluster *cluster,
									uint64_t segSize, WsBackupInfo *backups,
									int backupCount, const bool *kept);

static int cli_archive_cleanup_getopt(int argc, char **argv);
static void cli_archive_cleanup_command_run(int argc, char **argv);


/*
 * pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path <dir>
 *                               [--keep-count <N>] [--keep-age <interval>]
 *                               [--dry-run]
 */

static char archiveCleanupPgdata[MAXPGPATH] = { 0 };
static char archiveCleanupConfigFile[MAXPGPATH] = { 0 };
static char archiveCleanupCluster[NAMEDATALEN + 16] = { 0 };
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
				 "Remove WAL/base backups this cluster no longer needs to "
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
				 "  --path        the cluster's own directory (overrides "
				 "the cluster's own \"path\")\n"
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
 * RetentionAge/stringToRetentionAge() -- --keep-age parsing -- and
 * retentionAgeCutoff() -- the timestamp before which a base backup counts
 * as expired under --keep-age -- have moved to src/bin/common/
 * string_utils.h/.c, shared with cli_basebackup.c.
 */

/*
 * Base backup enumeration: WsBackupInfo/ws_backup_list_load()/backup_cmp()
 * have moved to backup_list.h/.c, shared with "pg_walserver list backups"
 * (cli_list.c).
 */

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
 *
 * The generic chasing-the-chain-of-segments logic itself (timeline-history
 * parsing, the segment-range walk) lives in src/bin/common/wal_segment.c's
 * own wal_check_range()/wal_check_continuity(), free of any pg_walserver
 * type: ws_check_wal_continuity() below is the thin, pg_walserver-specific
 * wrapper around it, resolving this project's own WsCluster/WsBackupInfo
 * into the plain tli/segno positions wal_check_continuity() needs (via
 * wal_dir_find_latest(), wal_dir_scan.c, for the newest kept backup's own
 * end boundary), then reporting each problem it hands back with this
 * project's own log_error() wording, naming the specific backup.
 */

/*
 * ws_check_wal_continuity runs wal_check_continuity() (src/bin/common/
 * wal_segment.c) for every kept backup in the final kept set: from its own
 * required starting segment through to the next newer kept backup's own
 * start segment, or, for the newest kept backup, through to the newest WAL
 * segment actually present on disk (wal_dir_find_latest(), this project's
 * own cluster/WAL-directory knowledge, resolved here and handed down as
 * plain scalars). Logs a specific log_error (naming the backup and the
 * missing segment/range) for every problem found and returns false if any
 * were -- callers decide what to do about that (refuse outright, or
 * proceed anyway under --force).
 */
static bool
ws_check_wal_continuity(const char *clusterPath, const WsCluster *cluster,
						uint64_t segSize, WsBackupInfo *backups,
						int backupCount, const bool *kept)
{
	int *keptIdx = (int *) malloc(sizeof(int) * backupCount);
	int keptLen = 0;

	for (int i = 0; i < backupCount; i++)
	{
		if (kept[i] && backups[i].haveStart)
		{
			keptIdx[keptLen++] = i;
		}
	}

	WalContinuityEntry *entries = (WalContinuityEntry *)
								  malloc(sizeof(WalContinuityEntry) * keptLen);
	int *entryBackupIdx = (int *) malloc(sizeof(int) * keptLen);
	int entryLen = 0;

	for (int k = 0; k < keptLen; k++)
	{
		WsBackupInfo *backup = &(backups[keptIdx[k]]);

		if (!wal_segment_name_is_valid(backup->startSegment))
		{
			/* can't happen: startSegment was produced by our own
			 * wal_segment_name_format() when this backup was loaded */
			continue;
		}

		wal_segment_name_parse(backup->startSegment, segSize,
							   &(entries[entryLen].startTli),
							   &(entries[entryLen].startSegno));
		entryBackupIdx[entryLen] = keptIdx[k];
		entryLen++;
	}

	bool haveLatest = false;
	uint32_t latestTli = 0;
	uint64_t latestEndSegno = 0;

	{
		char latestEndLsn[64] = { 0 };

		if (wal_dir_find_latest(cluster, &latestTli, latestEndLsn,
								sizeof(latestEndLsn)))
		{
			uint64_t oneAfterSegno;

			if (wal_lsn_to_segno(latestEndLsn, segSize, &oneAfterSegno) &&
				oneAfterSegno > 0)
			{
				latestEndSegno = oneAfterSegno - 1;
				haveLatest = true;
			}
		}
	}

	WalContinuityProblem *problems = (WalContinuityProblem *)
									 calloc(entryLen > 0 ? entryLen : 1,
											sizeof(WalContinuityProblem));

	bool ok = wal_check_continuity(clusterPath, segSize, entries, entryLen,
								   haveLatest, latestTli, latestEndSegno,
								   problems);

	for (int e = 0; e < entryLen; e++)
	{
		if (!problems[e].hasProblem)
		{
			continue;
		}

		WsBackupInfo *backup = &(backups[entryBackupIdx[e]]);
		bool lastWithNoReference = (e == entryLen - 1) && !haveLatest;

		if (lastWithNoReference)
		{
			/* nothing on disk to compare against at all (a brand new
			 * cluster, or every recognizable complete segment is gone) --
			 * the least that could still be required (its own required
			 * starting segment being present) already failed */
			log_error("archive-cleanup: WAL continuity check failed for "
					  "kept backup \"%s\": its own required starting "
					  "WAL segment \"%s\" is missing, and no WAL "
					  "segment at all is present under \"%s\" to "
					  "compare against", backup->dirPath,
					  backup->startSegment, clusterPath);
		}
		else
		{
			log_error("archive-cleanup: WAL continuity check failed for "
					  "kept backup \"%s\" (requires WAL from \"%s\" "
					  "onward): %s", backup->dirPath, backup->startSegment,
					  problems[e].detail);
		}
	}

	free(keptIdx);
	free(entries);
	free(entryBackupIdx);
	free(problems);

	return ok;
}


/*
 * Main entry point
 */
bool
ws_archive_cleanup_run(const char *clusterPath,
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

	if (!directory_exists(clusterPath))
	{
		log_error("archive-cleanup: \"%s\" is not a directory", clusterPath);
		return false;
	}

	WsCluster cluster = { 0 };

	strlcpy(cluster.path, clusterPath, sizeof(cluster.path));

	uint64_t segSize = ws_cluster_wal_segment_size(&cluster);

	WsBackupInfo *backups = NULL;
	int backupCount = 0;

	if (!ws_backup_list_load(clusterPath, segSize, &backups, &backupCount))
	{
		log_error("archive-cleanup: could not read \"%s/%s\"",
				  clusterPath, WS_BACKUPS_SUBDIR);
		return false;
	}

	if (backupCount == 0)
	{
		log_warn("archive-cleanup: no base backups found under \"%s/%s\"; "
				 "nothing to anchor WAL retention against, leaving \"%s\" "
				 "untouched", clusterPath, WS_BACKUPS_SUBDIR, clusterPath);
		free(backups);
		return true;
	}

	/* which backup does basebackups/.latest currently point to? */
	char latestPath[MAXPGPATH] = { 0 };
	char *latestContents = NULL;
	long latestSize = 0;

	sformat(latestPath, sizeof(latestPath), "%s/%s", clusterPath, WS_LATEST_FILENAME);

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
				  latestPath, latestLabel, clusterPath, WS_BACKUPS_SUBDIR);
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
	time_t ageCutoffTime = haveKeepAge ? retentionAgeCutoff(&keepAge, now) : 0;
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

	if (ws_replication_slot_oldest_restart_lsn(&cluster, segSize, slotName,
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
	bool continuityOk = ws_check_wal_continuity(clusterPath, &cluster, segSize,
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

			sformat(segPath, sizeof(segPath), "%s/%s", clusterPath,
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
	DIR *dir = opendir(clusterPath);

	if (dir == NULL)
	{
		log_error("archive-cleanup: could not open \"%s\"", clusterPath);
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

		sformat(filePath, sizeof(filePath), "%s/%s", clusterPath, entry->d_name);

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
	archiveCleanupCluster[0] = '\0';
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
				strlcpy(archiveCleanupCluster, optarg, sizeof(archiveCleanupCluster));
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

	char clusterPath[MAXPGPATH] = { 0 };

	if (archiveCleanupPath[0] != '\0')
	{
		strlcpy(clusterPath, archiveCleanupPath, sizeof(clusterPath));
	}
	else if (archiveCleanupPgdata[0] != '\0' && archiveCleanupCluster[0] != '\0')
	{
		char clustersPath[MAXPGPATH] = { 0 };
		WsCluster *clusters = NULL;
		int clusterCount = 0;

		config_file_path(archiveCleanupPgdata, archiveCleanupConfigFile,
						 clustersPath, sizeof(clustersPath));

		const WsCluster *cluster = NULL;

		if (clusters_load(clustersPath, &clusters, &clusterCount))
		{
			cluster = clusters_find(clusters, clusterCount, archiveCleanupCluster);
		}

		if (cluster == NULL)
		{
			log_fatal("No cluster \"%s\" in \"%s\"", archiveCleanupCluster,
					  clustersPath);
			clusters_free(clusters);
			exit(1);
		}

		strlcpy(clusterPath, cluster->path, sizeof(clusterPath));
		clusters_free(clusters);
	}
	else
	{
		log_fatal("archive-cleanup requires --path, or --cluster with "
				  "--pgdata pointing at a \"pg_walserver.ini\" that has "
				  "that cluster");
		exit(1);
	}

	exit(ws_archive_cleanup_run(clusterPath,
								archiveCleanupHaveKeepCount, archiveCleanupKeepCount,
								archiveCleanupHaveKeepAge, archiveCleanupKeepAge,
								archiveCleanupDryRun, archiveCleanupForce) ? 0 : 1);
}
