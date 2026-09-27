/*
 * src/bin/pg_walsender/repl_command.c
 *   See repl_command.h. repl_command_parse() itself now lives in
 *   repl_gram.y (the bison action that fills in a WsCommand as it
 *   reduces) -- this file only keeps ws_dispatch_command(), the
 *   parse-tree-to-handler dispatch that was always separate from parsing.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "repl_command.h"
#include "cmd_base_backup.h"
#include "cmd_fetch_file.h"
#include "cmd_identify_system.h"
#include "cmd_replication_slot.h"
#include "cmd_show.h"
#include "cmd_start_replication.h"
#include "cmd_timeline_history.h"
#include "framing.h"


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
			cmd_show(sock, route, cmd->showName);
			break;
		}

		case WS_CMD_BASE_BACKUP:
		{
			cmd_base_backup(sock, route, cmd->options, cmd->nOptions);
			break;
		}

		case WS_CMD_TIMELINE_HISTORY:
		{
			cmd_timeline_history(sock, route, cmd->timeline);
			break;
		}

		case WS_CMD_CREATE_REPLICATION_SLOT:
		{
			cmd_create_replication_slot(sock, route, cmd->slotName,
										cmd->temporary, cmd->isLogical);
			break;
		}

		case WS_CMD_READ_REPLICATION_SLOT:
		{
			cmd_read_replication_slot(sock, route, cmd->slotName);
			break;
		}

		case WS_CMD_DROP_REPLICATION_SLOT:
		{
			cmd_drop_replication_slot(sock, route, cmd->slotName, cmd->dropWait);
			break;
		}

		case WS_CMD_START_REPLICATION:
		{
			cmd_start_replication(sock, route, cmd->slotName, cmd->startLsn,
								  cmd->haveTimeline, (uint32_t) cmd->timeline);
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
