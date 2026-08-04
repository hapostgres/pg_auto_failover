/*
 * src/bin/pg_walsender/cmd_identify_system.c
 *   See cmd_identify_system.h.
 *
 *   systemid/timeline come straight from the route (written by pg_autoctl's
 *   archiver-serve supervisor from the monitor's own tracked values -- see
 *   routes.h). xlogpos is reported as "0/0" for now: computing the real
 *   latest-captured position requires scanning the WAL cache directory,
 *   which is wired in alongside START_REPLICATION (milestone 2 step 5),
 *   not required for the protocol handshake itself to be correct.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cmd_identify_system.h"
#include "framing.h"


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
	const char *systemId = (route != NULL && route->systemId[0] != '\0')
						   ? route->systemId
						   : "0";
	int timeline = (route != NULL && route->timeline > 0) ? route->timeline : 1;

	snprintf(timelineStr, sizeof(timelineStr), "%d", timeline);

	const char *values[] = {
		systemId,
		timelineStr,
		"0/0",
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
