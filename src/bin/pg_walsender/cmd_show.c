/*
 * src/bin/pg_walsender/cmd_show.c
 *   See cmd_show.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <strings.h>

#include "postgres_fe.h"

#include "cmd_show.h"
#include "framing.h"


void
cmd_show(int sock, const char *name)
{
	const char *value = NULL;

	if (strcasecmp(name, "wal_segment_size") == 0)
	{
		/* matches the real default; a non-default segment size would need
		 * to come from the archived group's own tracked configuration --
		 * not wired in yet, see the identify_system placeholder note */
		value = "16MB";
	}
	else if (strcasecmp(name, "data_directory_mode") == 0)
	{
		value = "0700";
	}

	if (value == NULL)
	{
		ws_send_error_response(sock, "42704", "unrecognized configuration parameter");
		return;
	}

	WsColumn columns[] = {
		{ name, WS_TEXTOID, -1 },
	};

	const char *values[] = { value };

	if (!ws_send_row_description(sock, columns, 1) ||
		!ws_send_data_row(sock, values, 1) ||
		!ws_send_command_complete(sock, "SHOW"))
	{
		return;
	}
}
