/*
 * src/bin/pg_walsender/cmd_identify_system.c
 *   See cmd_identify_system.h.
 *
 *   systemid is read straight from route->path's own "archiver-systemid"
 *   file, written once (never refreshed -- a system identifier is
 *   immutable for a cluster's lifetime) by pg_autoctl's archiver-capture
 *   loop the first time it learns the group's real primary has one (see
 *   service_archiver_maybe_persist_systemid(), service_archiver.c).
 *   timeline/xlogpos prefer the newest fully-captured WAL segment's own
 *   boundary (wal_dir_scan.h, filename-derived, not a parsed WAL record
 *   position) when the WAL cache has one, falling back to timeline 1 and
 *   "0/0" when it doesn't (a brand new archiver with nothing captured
 *   yet).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cmd_identify_system.h"
#include "file_utils.h"
#include "framing.h"
#include "wal_dir_scan.h"


/*
 * read_systemid reads route->path's own "archiver-systemid" file (see this
 * file's own header comment) into idOut, trimmed of its trailing newline.
 * Returns false (idOut untouched) when the file doesn't exist yet -- the
 * group's real primary hasn't been discovered to have one yet.
 */
static bool
read_systemid(const char *path, char *idOut, size_t idOutSize)
{
	char sysidPath[MAXPGPATH] = { 0 };

	sformat(sysidPath, sizeof(sysidPath), "%s/archiver-systemid", path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(sysidPath, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	char *nl = strchr(contents, '\n');

	if (nl != NULL)
	{
		*nl = '\0';
	}

	strlcpy(idOut, contents, idOutSize);
	free(contents);

	return idOut[0] != '\0';
}


void
cmd_identify_system(int sock, const WsRoute *route, const char *dbname)
{
	WsColumn columns[] = {
		{ "systemid", WS_TEXTOID, -1 },
		{ "timeline", WS_INT4OID, 4 },
		{ "xlogpos", WS_TEXTOID, -1 },
		{ "dbname", WS_TEXTOID, -1 },
	};

	char timelineStr[16];
	char xlogpos[32] = "0/0";
	char systemIdBuf[32] = "0";
	int timeline = 1;

	if (route != NULL && route->path[0] != '\0')
	{
		(void) read_systemid(route->path, systemIdBuf, sizeof(systemIdBuf));

		uint32_t foundTimeline;

		if (wal_dir_find_latest(route->path, &foundTimeline,
								xlogpos, sizeof(xlogpos)))
		{
			timeline = (int) foundTimeline;
		}
	}

	const char *systemId = systemIdBuf;

	sformat(timelineStr, sizeof(timelineStr), "%d", timeline);

	const char *values[] = {
		systemId,
		timelineStr,
		xlogpos,
		dbname,
	};

	if (!ws_send_row_description(sock, columns, 4) ||
		!ws_send_data_row(sock, values, 4) ||
		!ws_send_command_complete(sock, "IDENTIFY_SYSTEM"))
	{
		/* the connection is likely dead at this point; the command loop's
		 * next ws_read_message() will notice and close it */
		return;
	}
}
