/*
 * src/bin/pg_walserver/repl_command.c
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
#include "cmd_archive_file.h"
#include "cmd_base_backup.h"
#include "cmd_check_file.h"
#include "cmd_fetch_file.h"
#include "cmd_identify_system.h"
#include "cmd_replication_slot.h"
#include "cmd_show.h"
#include "cmd_start_replication.h"
#include "cmd_timeline_history.h"
#include "framing.h"


/*
 * ws_dispatch_command routes an already-parsed WsCommand to its own
 * per-command handler -- the parse-tree-to-handler step that stayed
 * separate from parsing even after repl_command_parse() itself moved into
 * repl_gram.y's own semantic actions (see this file's own header comment).
 * cmd->type is one of the WsCommandType members repl_gram.y can actually
 * build; anything else (a command this project never implemented, or
 * deliberately dropped, see repl_command.h) falls through to a clean
 * 42601 ErrorResponse rather than a crash, leaving the connection usable
 * for the next command.
 */
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

		case WS_CMD_CHECK_FILE:
		{
			cmd_check_file(sock, route, cmd->filename,
						   cmd->checkFileSize, cmd->checkFileCrc32c);
			break;
		}

		case WS_CMD_ARCHIVE_FILE:
		{
			cmd_archive_file(sock, route, cmd->filename);
			break;
		}

		default:
		{
			ws_send_error_response(sock, "42601", "unsupported replication command");
			break;
		}
	}
}
