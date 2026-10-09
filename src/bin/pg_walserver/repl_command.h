/*
 * src/bin/pg_walserver/repl_command.h
 *   Parses the Query-message command strings real replication clients send
 *   (e.g. "IDENTIFY_SYSTEM", "SHOW wal_segment_size") and dispatches to the
 *   matching cmd_*.c handler.
 *
 *   The parser itself is real Postgres's own replication grammar, ported:
 *   repl_scanner.l/repl_gram.y are adapted from PostgreSQL's
 *   src/backend/replication/{repl_scanner.l,repl_gram.y} (same token set,
 *   same xq/xd quoting states, same grammar shape for every command this
 *   project implements), except the bison semantic actions build a plain
 *   WsCommand below directly -- no Node / DefElem / List, no palloc, none of
 *   which exist in this frontend project -- instead of a backend parse
 *   tree. FETCH_FILE (see cmd_fetch_file.h) is this project's own
 *   extension, added as one more grammar rule; it has no Postgres
 *   equivalent. Because each connection already runs in its own forked
 *   child (accept_loop.c), the generated scanner/parser don't need to be
 *   reentrant -- unlike the backend's, which is embedded in one long-lived
 *   process -- so plain global yylex()/yyparse() state (as this project's
 *   own pgaftest test_spec_scan.l/test_spec_parse.y already do) is safe.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_REPL_COMMAND_H
#define WS_REPL_COMMAND_H

#include <stdbool.h>
#include <stdint.h>

#include "clusters.h"

typedef enum WsCommandType
{
	WS_CMD_IDENTIFY_SYSTEM,
	WS_CMD_SHOW,
	WS_CMD_BASE_BACKUP,
	WS_CMD_TIMELINE_HISTORY,
	WS_CMD_CREATE_REPLICATION_SLOT,
	WS_CMD_READ_REPLICATION_SLOT,
	WS_CMD_DROP_REPLICATION_SLOT,
	WS_CMD_START_REPLICATION,
	WS_CMD_FETCH_FILE,
	WS_CMD_CHECK_FILE,
	WS_CMD_ARCHIVE_FILE,
	WS_CMD_UNKNOWN
} WsCommandType;

/*
 * WsCommandOption: one BASE_BACKUP/CREATE_REPLICATION_SLOT option, as the
 * grammar's generic_option/generic_option_list rules build it -- this
 * project's own stand-in for a backend DefElem, without the List/Node
 * machinery: a plain fixed-size array of these on WsCommand instead of a
 * linked List of DefElem*.
 */
#define WS_MAX_COMMAND_OPTIONS 16

typedef struct WsCommandOption
{
	char name[64];
	char value[256];
	bool hasValue;      /* false for a bare keyword option, e.g. "wal" */
} WsCommandOption;

typedef struct WsCommand
{
	WsCommandType type;

	char showName[NAMEDATALEN];    /* WS_CMD_SHOW */

	WsCommandOption options[WS_MAX_COMMAND_OPTIONS];  /* WS_CMD_BASE_BACKUP,
	                                                   * WS_CMD_CREATE_REPLICATION_SLOT */
	int nOptions;

	int timeline;                  /* WS_CMD_TIMELINE_HISTORY (mandatory),
	                                * WS_CMD_START_REPLICATION (optional,
	                                * see haveTimeline) */
	bool haveTimeline;

	char filename[256];            /* WS_CMD_FETCH_FILE, WS_CMD_CHECK_FILE,
	                                * WS_CMD_ARCHIVE_FILE */

	uint64_t checkFileSize;         /* WS_CMD_CHECK_FILE: the client's local
	                                 * file size, to compare against what is
	                                 * on disk here -- see cmd_check_file.h */
	char checkFileCrc32c[16];       /* WS_CMD_CHECK_FILE: the client's local
	                                 * file CRC32C, as an uppercase hex
	                                 * string (no "crc32c:" prefix, already
	                                 * stripped by the scanner) */

	char slotName[NAMEDATALEN];    /* WS_CMD_{CREATE,READ,DROP}_REPLICATION_SLOT,
	                                * WS_CMD_START_REPLICATION (SLOT clause,
	                                * empty when omitted) */
	bool temporary;                 /* WS_CMD_CREATE_REPLICATION_SLOT */
	bool isLogical;                  /* WS_CMD_CREATE_REPLICATION_SLOT:
	                                  * PHYSICAL vs LOGICAL -- only PHYSICAL
	                                  * is actually supported, rejected at
	                                  * dispatch time like before */
	bool dropWait;                    /* WS_CMD_DROP_REPLICATION_SLOT ... WAIT */

	uint64_t startLsn;                 /* WS_CMD_START_REPLICATION */
} WsCommand;

/*
 * repl_command_parse fills *cmd from the given Query-message string, using
 * the generated repl_gram.y/repl_scanner.l parser. Returns false
 * (cmd->type == WS_CMD_UNKNOWN) for anything not recognized or that fails
 * to parse -- the caller sends the ErrorResponse, this function doesn't
 * touch the socket.
 */
bool repl_command_parse(const char *query, WsCommand *cmd);

/*
 * ws_command_find_option looks up a BASE_BACKUP/CREATE_REPLICATION_SLOT
 * option by name (case-insensitive, matching generic_option's own
 * ident_or_keyword). Returns NULL if not present.
 */
const WsCommandOption * ws_command_find_option(const WsCommand *cmd, const char *name);

/*
 * ws_dispatch_command runs cmd against the connection's resolved cluster
 * (NULL in manual-testing mode, see auth.h) and the dbname the client
 * originally requested (always set, even without a cluster -- see
 * startup.c), sending whatever RowDescription/DataRow/CommandComplete or
 * ErrorResponse the command produces. Never sends ReadyForQuery -- the
 * caller's command loop does that once per Query message, uniformly.
 */
void ws_dispatch_command(int sock, const WsCommand *cmd,
						 const WsCluster *cluster, const char *dbname);

#endif /* WS_REPL_COMMAND_H */
