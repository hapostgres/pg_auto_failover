/*
 * src/bin/pg_walsender/cmd_identify_system.c
 *   See cmd_identify_system.h.
 *
 *   systemid comes straight from the route (written by pg_autoctl's
 *   archiver-serve supervisor from the monitor's own tracked values -- see
 *   routes.h). timeline/xlogpos prefer the newest fully-captured WAL
 *   segment's own boundary (wal_dir_scan.h, filename-derived, not a
 *   parsed WAL record position) when the WAL cache has one, falling back
 *   to the route's static timeline and "0/0" when it doesn't (a brand
 *   new archiver with nothing captured yet).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cmd_identify_system.h"
#include "framing.h"
#include "wal_dir_scan.h"


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
	const char *systemId = (route != NULL && route->systemId[0] != '\0')
						   ? route->systemId
						   : "0";
	int timeline = (route != NULL && route->timeline > 0) ? route->timeline : 1;

	if (route != NULL && route->walcacheDir[0] != '\0')
	{
		uint32_t foundTimeline;

		if (wal_dir_find_latest(route->walcacheDir, &foundTimeline,
								xlogpos, sizeof(xlogpos)))
		{
			timeline = (int) foundTimeline;
		}
	}

	snprintf(timelineStr, sizeof(timelineStr), "%d", timeline);

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
