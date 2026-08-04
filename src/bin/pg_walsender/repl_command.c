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

#include "repl_command.h"
#include "cmd_base_backup.h"
#include "cmd_identify_system.h"
#include "cmd_replication_slot.h"
#include "cmd_show.h"
#include "cmd_start_replication.h"
#include "cmd_timeline_history.h"
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


bool
repl_command_parse(const char *query, WsCommand *cmd)
{
	memset(cmd, 0, sizeof(WsCommand));

	const char *p = skip_whitespace(query);

	if (strncasecmp(p, "IDENTIFY_SYSTEM", strlen("IDENTIFY_SYSTEM")) == 0)
	{
		cmd->type = WS_CMD_IDENTIFY_SYSTEM;
		return true;
	}

	if (strncasecmp(p, "SHOW", strlen("SHOW")) == 0 && isspace((unsigned char) p[4]))
	{
		p = skip_whitespace(p + 4);
		strlcpy(cmd->showName, p, sizeof(cmd->showName));
		rtrim(cmd->showName);
		cmd->type = WS_CMD_SHOW;
		return true;
	}

	if (strncasecmp(p, "BASE_BACKUP", strlen("BASE_BACKUP")) == 0)
	{
		p = skip_whitespace(p + strlen("BASE_BACKUP"));
		strlcpy(cmd->rawOptions, p, sizeof(cmd->rawOptions));
		rtrim(cmd->rawOptions);
		cmd->type = WS_CMD_BASE_BACKUP;
		return true;
	}

	if (strncasecmp(p, "TIMELINE_HISTORY", strlen("TIMELINE_HISTORY")) == 0)
	{
		p = skip_whitespace(p + strlen("TIMELINE_HISTORY"));
		cmd->timeline = atoi(p);
		cmd->type = WS_CMD_TIMELINE_HISTORY;
		return true;
	}

	if (strncasecmp(p, "CREATE_REPLICATION_SLOT",
					strlen("CREATE_REPLICATION_SLOT")) == 0)
	{
		p = skip_whitespace(p + strlen("CREATE_REPLICATION_SLOT"));
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
		return true;
	}

	if (strncasecmp(p, "READ_REPLICATION_SLOT",
					strlen("READ_REPLICATION_SLOT")) == 0)
	{
		p = skip_whitespace(p + strlen("READ_REPLICATION_SLOT"));
		strlcpy(cmd->rawArgs, p, sizeof(cmd->rawArgs));
		rtrim(cmd->rawArgs);
		cmd->type = WS_CMD_READ_REPLICATION_SLOT;
		return true;
	}

	if (strncasecmp(p, "START_REPLICATION", strlen("START_REPLICATION")) == 0)
	{
		p = skip_whitespace(p + strlen("START_REPLICATION"));
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
	switch (cmd->type)
	{
		case WS_CMD_IDENTIFY_SYSTEM:
		{
			cmd_identify_system(sock, route, dbname);
			break;
		}

		case WS_CMD_SHOW:
		{
			cmd_show(sock, cmd->showName);
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

		case WS_CMD_START_REPLICATION:
		{
			cmd_start_replication(sock, route, cmd->rawArgs);
			break;
		}

		default:
		{
			ws_send_error_response(sock, "42601", "unsupported replication command");
			break;
		}
	}
}
