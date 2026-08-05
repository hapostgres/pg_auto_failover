/*
 * src/bin/pg_walsender/cmd_timeline_history.c
 *   See cmd_timeline_history.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cmd_timeline_history.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"

/* matches xlog_internal.h's own MAXFNAMELEN (backend-only header, not
 * pulled in here) -- "%08X.history" is always exactly 17 bytes + NUL */
#define WS_MAXFNAMELEN 64


void
cmd_timeline_history(int sock, const WsRoute *route, int timeline)
{
	if (route == NULL || route->walcacheDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	/* matches real Postgres's TLHistoryFileName() macro exactly */
	char filename[WS_MAXFNAMELEN];

	sformat(filename, sizeof(filename), "%08X.history", timeline);

	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/%s", route->walcacheDir, filename);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		/* matches real walsender.c: no history file for this timeline is
		 * an ERROR there too, not a soft "empty" fallback */
		log_info("TIMELINE_HISTORY: \"%s\" not found under \"%s\"",
				 filename, route->walcacheDir);
		ws_send_error_response(sock, "58P01",
							   "requested timeline history file not found");
		return;
	}

	WsColumn columns[] = {
		{ "filename", WS_TEXTOID, -1 },
		{ "content", WS_TEXTOID, -1 },
	};

	const char *values[] = { filename, contents };

	if (ws_send_row_description(sock, columns, 2) &&
		ws_send_data_row(sock, values, 2))
	{
		ws_send_command_complete(sock, "TIMELINE_HISTORY");
	}

	free(contents);
}
