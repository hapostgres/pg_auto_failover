/*
 * src/bin/pg_walsender/repl_command.h
 *   Parses the Query-message command strings real replication clients send
 *   (e.g. "IDENTIFY_SYSTEM", "SHOW wal_segment_size") and dispatches to the
 *   matching cmd_*.c handler. This is repl_gram.y/repl_scanner.l's
 *   equivalent, hand-rolled: the real grammar is backend-locked (bison
 *   output building backend Node types via palloc, see the design
 *   research), and the fixed ~7-command surface this project needs doesn't
 *   justify vendoring bison/flex infrastructure for it -- plain C
 *   tokenizing is enough.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_REPL_COMMAND_H
#define WS_REPL_COMMAND_H

#include <stdbool.h>

#include "routes.h"

typedef enum WsCommandType
{
	WS_CMD_IDENTIFY_SYSTEM,
	WS_CMD_SHOW,
	WS_CMD_BASE_BACKUP,
	WS_CMD_TIMELINE_HISTORY,
	WS_CMD_CREATE_REPLICATION_SLOT,
	WS_CMD_READ_REPLICATION_SLOT,
	WS_CMD_START_REPLICATION,
	WS_CMD_UNKNOWN
} WsCommandType;

typedef struct WsCommand
{
	WsCommandType type;
	char showName[NAMEDATALEN];   /* WS_CMD_SHOW only */
	char rawOptions[1024];        /* WS_CMD_BASE_BACKUP only: the "(...)" or
	                               * trailing-token option list verbatim,
	                               * parsed by cmd_base_backup.c itself */
	int timeline;                  /* WS_CMD_TIMELINE_HISTORY only */
	char rawArgs[512];              /* WS_CMD_{CREATE,READ}_REPLICATION_SLOT /
	                                 * WS_CMD_START_REPLICATION: everything
	                                 * after the keyword, verbatim, parsed by
	                                 * each command's own cmd_*.c */
} WsCommand;

/*
 * repl_command_parse fills *cmd from the given Query-message string.
 * Returns false (cmd->type == WS_CMD_UNKNOWN) for anything not yet
 * recognized -- the caller sends the ErrorResponse, this function doesn't
 * touch the socket.
 */
bool repl_command_parse(const char *query, WsCommand *cmd);

/*
 * ws_dispatch_command runs cmd against the connection's resolved route
 * (NULL in manual-testing mode, see auth.h) and the dbname the client
 * originally requested (always set, even without a route -- see
 * startup.c), sending whatever RowDescription/DataRow/CommandComplete or
 * ErrorResponse the command produces. Never sends ReadyForQuery -- the
 * caller's command loop does that once per Query message, uniformly.
 */
void ws_dispatch_command(int sock, const WsCommand *cmd,
						 const WsRoute *route, const char *dbname);

#endif /* WS_REPL_COMMAND_H */
