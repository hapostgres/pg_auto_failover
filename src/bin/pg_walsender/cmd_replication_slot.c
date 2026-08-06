/*
 * src/bin/pg_walsender/cmd_replication_slot.c
 *   See cmd_replication_slot.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <string.h>

#include "postgres_fe.h"

#include "cmd_replication_slot.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "wal_dir_scan.h"

#define WS_SLOT_NAME_MAX 64


/*
 * parse_slot_name reads a possibly-quoted identifier (matching real
 * Postgres's AppendQuotedIdentifier on the client side -- unquoted for a
 * simple lowercase name, double-quoted otherwise) from the front of *p,
 * advancing *p past it.
 */
static bool
parse_slot_name(const char **p, char *nameOut, size_t nameOutSize)
{
	const char *s = *p;

	while (isspace((unsigned char) *s))
	{
		s++;
	}

	if (*s == '"')
	{
		s++;

		char *out = nameOut;
		char *outEnd = nameOut + nameOutSize - 1;

		while (*s && *s != '"')
		{
			if (out < outEnd)
			{
				*out++ = *s;
			}
			s++;
		}

		if (*s != '"')
		{
			return false;
		}

		*out = '\0';
		s++;
	}
	else
	{
		const char *start = s;

		while (*s && !isspace((unsigned char) *s))
		{
			s++;
		}

		size_t len = Min((size_t) (s - start), nameOutSize - 1);

		memcpy(nameOut, start, len); /* IGNORE-BANNED */
		nameOut[len] = '\0';
	}

	*p = s;

	return nameOut[0] != '\0';
}


static bool
slot_name_is_safe(const char *name)
{
	if (name[0] == '\0')
	{
		return false;
	}

	for (const char *p = name; *p; p++)
	{
		if (!(isalnum((unsigned char) *p) || *p == '_' || *p == '-'))
		{
			return false;
		}
	}

	return true;
}


static void
slot_marker_path(const WsRoute *route, const char *slotName, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/.slot_%s", route->walcacheDir, slotName);
}


void
cmd_create_replication_slot(int sock, const WsRoute *route, const char *rawArgs)
{
	if (route == NULL || route->walcacheDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	const char *p = rawArgs;
	char slotName[WS_SLOT_NAME_MAX];

	if (!parse_slot_name(&p, slotName, sizeof(slotName)) || !slot_name_is_safe(slotName))
	{
		ws_send_error_response(sock, "22023", "invalid or missing slot name");
		return;
	}

	bool sawPhysical = false;
	bool sawLogical = false;
	char word[64];

	while (*p)
	{
		while (*p && (isspace((unsigned char) *p) || *p == ',' || *p == '(' || *p == ')'))
		{
			p++;
		}

		if (!*p)
		{
			break;
		}

		const char *start = p;

		while (*p && !isspace((unsigned char) *p) && *p != ',' &&
			   *p != '(' && *p != ')')
		{
			p++;
		}

		size_t len = Min((size_t) (p - start), sizeof(word) - 1);

		memcpy(word, start, len); /* IGNORE-BANNED */
		word[len] = '\0';

		if (strcasecmp(word, "PHYSICAL") == 0)
		{
			sawPhysical = true;
		}
		else if (strcasecmp(word, "LOGICAL") == 0)
		{
			sawLogical = true;
		}

		/* TEMPORARY and RESERVE_WAL are accepted but not enforced yet --
		 * see this file's own header comment on retention */
	}

	if (sawLogical || !sawPhysical)
	{
		ws_send_error_response(sock, "0A000",
							   "only physical replication slots are supported");
		return;
	}

	char consistentPoint[32] = "0/0";
	uint32_t timeline;

	(void) wal_dir_find_latest(route->walcacheDir, &timeline, consistentPoint,
							   sizeof(consistentPoint));

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	char contents[128];

	sformat(contents, sizeof(contents), "restart_lsn=%s\n", consistentPoint);

	if (!write_file(contents, strlen(contents), path))
	{
		log_error("Failed to write replication slot marker \"%s\"", path);
		ws_send_error_response(sock, "58030", "failed to persist the replication slot");
		return;
	}

	WsColumn columns[] = {
		{ "slot_name", WS_TEXTOID, -1 },
		{ "consistent_point", WS_TEXTOID, -1 },
		{ "snapshot_name", WS_TEXTOID, -1 },
		{ "output_plugin", WS_TEXTOID, -1 },
	};

	const char *values[] = { slotName, consistentPoint, NULL, NULL };

	if (ws_send_row_description(sock, columns, 4) &&
		ws_send_data_row(sock, values, 4))
	{
		ws_send_command_complete(sock, "CREATE_REPLICATION_SLOT");
	}
}


void
cmd_read_replication_slot(int sock, const WsRoute *route, const char *rawArgs)
{
	if (route == NULL || route->walcacheDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	const char *p = rawArgs;
	char slotName[WS_SLOT_NAME_MAX];

	if (!parse_slot_name(&p, slotName, sizeof(slotName)) || !slot_name_is_safe(slotName))
	{
		ws_send_error_response(sock, "22023", "invalid or missing slot name");
		return;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	char *contents = NULL;
	long fileSize = 0;

	WsColumn columns[] = {
		{ "slot_type", WS_TEXTOID, -1 },
		{ "restart_lsn", WS_TEXTOID, -1 },
		{ "restart_tli", WS_INT8OID, 8 },
	};

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		/* matches real Postgres: slot doesn't exist -> one all-NULL row,
		 * not an ErrorResponse -- the client checks PQgetisnull() itself */
		const char *nullValues[] = { NULL, NULL, NULL };

		if (ws_send_row_description(sock, columns, 3) &&
			ws_send_data_row(sock, nullValues, 3))
		{
			ws_send_command_complete(sock, "READ_REPLICATION_SLOT");
		}

		return;
	}

	char restartLsn[32] = "0/0";
	const char *prefix = "restart_lsn=";
	char *line = strstr(contents, prefix);

	if (line != NULL)
	{
		line += strlen(prefix);

		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		strlcpy(restartLsn, line, sizeof(restartLsn));
	}

	free(contents);

	uint32_t timeline = (route->timeline > 0) ? (uint32_t) route->timeline : 1;
	char timelineStr[16];

	sformat(timelineStr, sizeof(timelineStr), "%u", timeline);

	const char *values[] = { "physical", restartLsn, timelineStr };

	if (ws_send_row_description(sock, columns, 3) &&
		ws_send_data_row(sock, values, 3))
	{
		ws_send_command_complete(sock, "READ_REPLICATION_SLOT");
	}
}
