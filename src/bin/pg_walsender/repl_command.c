/*
 * src/bin/pg_walsender/repl_command.c
 *   See repl_command.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "postgres_fe.h"

#include "string_utils.h"

#include "repl_command.h"
#include "cmd_base_backup.h"
#include "cmd_fetch_file.h"
#include "cmd_identify_system.h"
#include "cmd_replication_slot.h"
#include "cmd_show.h"
#include "cmd_start_replication.h"
#include "cmd_timeline_history.h"
#include "wal_dir_scan.h"
#include "framing.h"


static const char *
skip_whitespace(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	{
		p++;
	}

	return p;
}


static void
rtrim(char *s)
{
	size_t n = strlen(s);

	while (n > 0 &&
		   (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' ||
			s[n - 1] == '\r' || s[n - 1] == ';'))
	{
		s[--n] = '\0';
	}
}


/*
 * match_keyword: does p start with the command keyword, as a whole word?
 * "IDENTIFY_SYSTEMxyz" must not be taken for IDENTIFY_SYSTEM. On a match
 * *rest points right after the keyword.
 */
static bool
match_keyword(const char *p, const char *keyword, const char **rest)
{
	size_t len = strlen(keyword);

	if (strncasecmp(p, keyword, len) != 0)
	{
		return false;
	}

	char next = p[len];

	if (next != '\0' && !isspace((unsigned char) next) && next != ';' &&
		next != '(')
	{
		return false;
	}

	*rest = p + len;

	return true;
}


bool
repl_command_parse(const char *query, WsCommand *cmd)
{
	memset(cmd, 0, sizeof(WsCommand));

	const char *p = skip_whitespace(query);
	const char *rest = NULL;

	if (match_keyword(p, "IDENTIFY_SYSTEM", &rest))
	{
		cmd->type = WS_CMD_IDENTIFY_SYSTEM;
		return true;
	}

	if (match_keyword(p, "SHOW", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->showName, p, sizeof(cmd->showName));
		rtrim(cmd->showName);
		cmd->type = WS_CMD_SHOW;
		return true;
	}

	if (match_keyword(p, "BASE_BACKUP", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->rawOptions, p, sizeof(cmd->rawOptions));
		rtrim(cmd->rawOptions);
		cmd->type = WS_CMD_BASE_BACKUP;
		return true;
	}

	if (match_keyword(p, "TIMELINE_HISTORY", &rest))
	{
		p = skip_whitespace(rest);

		if (!stringToInt(p, &(cmd->timeline)) || cmd->timeline <= 0)
		{
			return false;
		}

		cmd->type = WS_CMD_TIMELINE_HISTORY;
		return true;
	}

	if (match_keyword(p, "CREATE_REPLICATION_SLOT", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
		return true;
	}

	if (match_keyword(p, "DROP_REPLICATION_SLOT", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_DROP_REPLICATION_SLOT;
		return true;
	}

	if (match_keyword(p, "READ_REPLICATION_SLOT", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_READ_REPLICATION_SLOT;
		return true;
	}

	/*
	 * FETCH_FILE '<name>': not part of PostgreSQL's replication grammar, ours
	 * (see cmd_fetch_file.h). The name is a single-quoted literal, or bare.
	 */
	if (match_keyword(p, "FETCH_FILE", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->filename, p, sizeof(cmd->filename));
		rtrim(cmd->filename);

		size_t n = strlen(cmd->filename);

		if (n >= 2 && cmd->filename[0] == '\'' && cmd->filename[n - 1] == '\'')
		{
			memmove(cmd->filename, cmd->filename + 1, n - 2); /* IGNORE-BANNED */
			cmd->filename[n - 2] = '\0';
		}

		cmd->type = WS_CMD_FETCH_FILE;
		return true;
	}

	if (match_keyword(p, "START_REPLICATION", &rest))
	{
		p = skip_whitespace(rest);
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_START_REPLICATION;
		return true;
	}

	cmd->type = WS_CMD_UNKNOWN;
	return false;
}


void
ws_dispatch_command(int sock, const WsCommand *cmd,
					const WsRoute *route, const char *dbname)
{
	/*
	 * Everything that does segment arithmetic needs the WAL segment size,
	 * which comes from the monitor: refuse rather than guess 16MB.
	 */
	if ((cmd->type == WS_CMD_START_REPLICATION ||
		 cmd->type == WS_CMD_BASE_BACKUP ||
		 (cmd->type == WS_CMD_SHOW &&
		  strcasecmp(cmd->showName, "wal_segment_size") == 0)) &&
		ws_route_wal_segment_size(route) == 0)
	{
		ws_send_error_response(sock, "55000",
							   "the WAL segment size of this archive is not "
							   "known yet, try again shortly");
		return;
	}

	switch (cmd->type)
	{
		case WS_CMD_IDENTIFY_SYSTEM:
		{
			cmd_identify_system(sock, route, dbname);
			break;
		}

		case WS_CMD_SHOW:
		{
			cmd_show(sock, route, cmd->showName);
			break;
		}

		case WS_CMD_BASE_BACKUP:
		{
			cmd_base_backup(sock, route, cmd->rawOptions);
			break;
		}

		case WS_CMD_TIMELINE_HISTORY:
		{
			cmd_timeline_history(sock, route, cmd->timeline);
			break;
		}

		case WS_CMD_CREATE_REPLICATION_SLOT:
		{
			cmd_create_replication_slot(sock, route, cmd->rawArgs);
			break;
		}

		case WS_CMD_READ_REPLICATION_SLOT:
		{
			cmd_read_replication_slot(sock, route, cmd->rawArgs);
			break;
		}

		case WS_CMD_DROP_REPLICATION_SLOT:
		{
			cmd_drop_replication_slot(sock, route, cmd->rawArgs);
			break;
		}

		case WS_CMD_START_REPLICATION:
		{
			cmd_start_replication(sock, route, cmd->rawArgs);
			break;
		}

		case WS_CMD_FETCH_FILE:
		{
			cmd_fetch_file(sock, route, cmd->filename);
			break;
		}

		default:
		{
			ws_send_error_response(sock, "42601", "unsupported replication command");
			break;
		}
	}
}
