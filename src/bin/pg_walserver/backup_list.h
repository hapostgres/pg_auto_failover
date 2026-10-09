/*
 * src/bin/pg_walserver/backup_list.h
 *   Enumerates one cluster's own base backup directories under
 *   "<cluster path>/basebackups/" -- shared by "pg_walserver archive-
 *   cleanup" (cli_archive_cleanup.c) and "pg_walserver list backups"
 *   (cli_list.c), so neither re-derives the same backup_label parsing/
 *   label-timestamp-parsing logic a second time.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_BACKUP_LIST_H
#define WS_BACKUP_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "postgres_fe.h"

/*
 * WsBackupInfo is one enumerated base backup directory under
 * "<cluster path>/basebackups/".
 */
typedef struct WsBackupInfo
{
	char label[NAMEDATALEN];        /* "basebackup-20260101T000000Z" */
	char dirPath[MAXPGPATH];
	time_t takenAt;                 /* parsed out of the label itself */
	bool haveStart;                 /* backup_label parsed successfully */
	char startSegment[25];          /* this backup's own required starting
	                                * WAL segment (24 hex digits + NUL) */
} WsBackupInfo;

bool ws_backup_list_load(const char *clusterPath, uint64_t segSize,
						 WsBackupInfo **backupsOut, int *countOut);

#endif /* WS_BACKUP_LIST_H */
