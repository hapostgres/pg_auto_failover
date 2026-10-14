/*
 * src/bin/pg_walserver/backup_list.c
 *   See backup_list.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "backup_list.h"
#include "cmd_base_backup.h"
#include "file_utils.h"
#include "log.h"
#include "wal_segment.h"

#define WS_BACKUPS_SUBDIR "basebackups"

static int backup_cmp(const void *a, const void *b);


/*
 * backup_cmp is qsort's own comparator for ws_backup_list_load() below,
 * sorting by label string, which sorts chronologically for this project's
 * own "basebackup-<UTC timestamp>Z" naming scheme.
 */
static int
backup_cmp(const void *a, const void *b)
{
	const WsBackupInfo *ba = (const WsBackupInfo *) a;
	const WsBackupInfo *bb = (const WsBackupInfo *) b;

	return strcmp(ba->label, bb->label);
}


/*
 * ws_backup_list_load scans <clusterPath>/basebackups/ for backup directories,
 * parses each one's own label timestamp and (via read_backup_label(),
 * cmd_base_backup.c) its own required starting WAL segment, and returns
 * them sorted oldest-first (label strings sort chronologically). Returns
 * true even when there are zero backups (an empty, not-yet-used cluster);
 * false only on a directory that cannot be opened at all.
 */
bool
ws_backup_list_load(const char *clusterPath, uint64_t segSize,
					WsBackupInfo **backupsOut, int *countOut)
{
	char backupsDir[MAXPGPATH] = { 0 };

	sformat(backupsDir, sizeof(backupsDir), "%s/%s", clusterPath, WS_BACKUPS_SUBDIR);

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

		if (!wal_backup_label_parse_time(backup->label, &(backup->takenAt)))
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
