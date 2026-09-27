%{
/*
 * src/bin/pg_walsender/repl_gram.y
 *   Grammar for the replication command language pg_walsender accepts on
 *   Query messages, adapted from PostgreSQL's own
 *   src/backend/replication/repl_gram.y -- same command alternatives, same
 *   generic_option_list shape for BASE_BACKUP/CREATE_REPLICATION_SLOT
 *   options, same START_REPLICATION/CREATE_REPLICATION_SLOT/
 *   DROP_REPLICATION_SLOT/READ_REPLICATION_SLOT/SHOW/TIMELINE_HISTORY/
 *   IDENTIFY_SYSTEM rules. What's different: the real grammar's semantic
 *   actions build a backend Node / DefElem / List parse tree via
 *   makeNode()/palloc(), none of which exist in this frontend project --
 *   here every action instead fills in the single WsCommand this file's
 *   caller (repl_command.c) already allocated, directly, field by field,
 *   with a plain fixed-size WsCommandOption array standing in for a
 *   DefElem List. ALTER_REPLICATION_SLOT, UPLOAD_MANIFEST and logical
 *   START_REPLICATION are dropped: pg_walsender never implements them
 *   (WsCommandType has no matching member), so there is nothing for their
 *   productions to build. FETCH_FILE is this project's own extension,
 *   with no Postgres equivalent (see cmd_fetch_file.h).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "log.h"

#include "repl_command.h"

/* the command being filled in by the current parse; set by repl_command_parse() */
static WsCommand *ws_parse_cmd = NULL;

/* set by yyerror() or the scanner on a lexical error; checked by repl_command_parse() */
static bool ws_parse_failed = false;

/* the raw command string currently being parsed, kept only for error messages */
char *ws_parse_query = NULL;

extern int yylex(void);

static void yyerror(const char *message);
static char *dotted_name(const char *a, const char *b);
static void add_option(const char *name, const char *value, bool hasValue);

%}

%union
{
	char	   *str;
	bool		boolval;
	uint32_t	uintval;
	uint64_t	recptr;
}

/* Non-keyword tokens */
%token <str> SCONST IDENT
%token <uintval> UCONST
%token <recptr> RECPTR

/* Keyword tokens -- same names as PostgreSQL's own repl_gram.y */
%token K_BASE_BACKUP
%token K_IDENTIFY_SYSTEM
%token K_READ_REPLICATION_SLOT
%token K_SHOW
%token K_START_REPLICATION
%token K_CREATE_REPLICATION_SLOT
%token K_DROP_REPLICATION_SLOT
%token K_TIMELINE_HISTORY
%token K_WAIT
%token K_TIMELINE
%token K_PHYSICAL
%token K_LOGICAL
%token K_SLOT
%token K_RESERVE_WAL
%token K_TEMPORARY
%token K_TWO_PHASE
%token K_EXPORT_SNAPSHOT
%token K_NOEXPORT_SNAPSHOT
%token K_USE_SNAPSHOT
%token K_FETCH_FILE

%type <str>		var_name ident_or_keyword opt_slot
%type <boolval>	opt_temporary
%type <uintval>	opt_timeline

%%

firstcmd: command opt_semicolon
				{
					(void) yynerrs; /* suppress compiler warning */
				}
			;

opt_semicolon:	';'
				| /* EMPTY */
				;

command:
			identify_system
			| base_backup
			| start_replication
			| create_replication_slot
			| drop_replication_slot
			| read_replication_slot
			| timeline_history
			| show
			| fetch_file
			;

/*
 * IDENTIFY_SYSTEM
 */
identify_system:
			K_IDENTIFY_SYSTEM
				{
					ws_parse_cmd->type = WS_CMD_IDENTIFY_SYSTEM;
				}
			;

/*
 * FETCH_FILE '<name>' -- this project's own extension, no Postgres
 * equivalent. fetch_client.c always sends the name single-quoted (a
 * filename can contain '.'/'/' which aren't valid in a bare IDENT), so
 * only the SCONST form is needed; the real scanner's <xq> state already
 * gives us '' escaping for free.
 */
fetch_file:
			K_FETCH_FILE SCONST
				{
					ws_parse_cmd->type = WS_CMD_FETCH_FILE;
					strlcpy(ws_parse_cmd->filename, $2, sizeof(ws_parse_cmd->filename));
				}
			;

/*
 * READ_REPLICATION_SLOT slot
 */
read_replication_slot:
			K_READ_REPLICATION_SLOT IDENT
				{
					ws_parse_cmd->type = WS_CMD_READ_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
				}
			;

/*
 * SHOW setting
 */
show:
			K_SHOW var_name
				{
					ws_parse_cmd->type = WS_CMD_SHOW;
					strlcpy(ws_parse_cmd->showName, $2, sizeof(ws_parse_cmd->showName));
				}
			;

var_name:	IDENT	{ $$ = $1; }
			| var_name '.' IDENT
				{ $$ = dotted_name($1, $3); }
		;

/*
 * BASE_BACKUP [ ( option ['value'] [, ...] ) ]
 */
base_backup:
			K_BASE_BACKUP '(' generic_option_list ')'
				{
					ws_parse_cmd->type = WS_CMD_BASE_BACKUP;
				}
			| K_BASE_BACKUP
				{
					ws_parse_cmd->type = WS_CMD_BASE_BACKUP;
				}
			;

/*
 * CREATE_REPLICATION_SLOT slot [TEMPORARY] { PHYSICAL | LOGICAL plugin } [options]
 *
 * pg_walsender only ever implements physical slots; a LOGICAL request
 * parses fine here (matching the real grammar's shape) and is rejected at
 * dispatch time in cmd_replication_slot.c, exactly as the previous
 * hand-rolled parser already did.
 */
create_replication_slot:
			K_CREATE_REPLICATION_SLOT IDENT opt_temporary K_PHYSICAL create_slot_options
				{
					ws_parse_cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->temporary = $3;
					ws_parse_cmd->isLogical = false;
				}
			| K_CREATE_REPLICATION_SLOT IDENT opt_temporary K_LOGICAL IDENT create_slot_options
				{
					ws_parse_cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->temporary = $3;
					ws_parse_cmd->isLogical = true;
				}
			;

create_slot_options:
			'(' generic_option_list ')'
			| create_slot_legacy_opt_list
			;

create_slot_legacy_opt_list:
			create_slot_legacy_opt_list create_slot_legacy_opt
			| /* EMPTY */
			;

/*
 * The pre-9.4-era legacy option keywords: turned into the very same
 * "snapshot"/"reserve_wal"/"two_phase" options generic_option_list would
 * have produced, same as PostgreSQL's own makeDefElem() calls here.
 */
create_slot_legacy_opt:
			K_EXPORT_SNAPSHOT		{ add_option("snapshot", "export", true); }
			| K_NOEXPORT_SNAPSHOT	{ add_option("snapshot", "nothing", true); }
			| K_USE_SNAPSHOT		{ add_option("snapshot", "use", true); }
			| K_RESERVE_WAL			{ add_option("reserve_wal", "true", true); }
			| K_TWO_PHASE			{ add_option("two_phase", "true", true); }
			;

/* DROP_REPLICATION_SLOT slot [WAIT] */
drop_replication_slot:
			K_DROP_REPLICATION_SLOT IDENT
				{
					ws_parse_cmd->type = WS_CMD_DROP_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->dropWait = false;
				}
			| K_DROP_REPLICATION_SLOT IDENT K_WAIT
				{
					ws_parse_cmd->type = WS_CMD_DROP_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->dropWait = true;
				}
			;

/*
 * START_REPLICATION [SLOT slot] [PHYSICAL] %X/%08X [TIMELINE %u]
 *
 * Logical replication (START_REPLICATION SLOT slot LOGICAL ...) is not
 * implemented by pg_walsender at all, so unlike PostgreSQL's own grammar
 * there is no separate start_logical_replication production for it.
 */
start_replication:
			K_START_REPLICATION opt_slot opt_physical RECPTR opt_timeline
				{
					ws_parse_cmd->type = WS_CMD_START_REPLICATION;

					if ($2 != NULL)
					{
						strlcpy(ws_parse_cmd->slotName, $2, sizeof(ws_parse_cmd->slotName));
					}

					ws_parse_cmd->startLsn = $4;
					ws_parse_cmd->timeline = (int) $5;
					ws_parse_cmd->haveTimeline = ($5 != 0);
				}
			;

/*
 * TIMELINE_HISTORY %u
 */
timeline_history:
			K_TIMELINE_HISTORY UCONST
				{
					if ($2 == 0)
					{
						yyerror("invalid timeline 0");
						YYERROR;
					}

					ws_parse_cmd->type = WS_CMD_TIMELINE_HISTORY;
					ws_parse_cmd->timeline = (int) $2;
				}
			;

opt_physical:
			K_PHYSICAL
			| /* EMPTY */
			;

opt_temporary:
			K_TEMPORARY			{ $$ = true; }
			| /* EMPTY */		{ $$ = false; }
			;

opt_slot:
			K_SLOT IDENT
				{ $$ = $2; }
			| /* EMPTY */
				{ $$ = NULL; }
			;

opt_timeline:
			K_TIMELINE UCONST
				{
					if ($2 == 0)
					{
						yyerror("invalid timeline 0");
						YYERROR;
					}

					$$ = $2;
				}
			| /* EMPTY */		{ $$ = 0; }
			;

generic_option_list:
			generic_option_list ',' generic_option
			| generic_option
			;

generic_option:
			ident_or_keyword
				{ add_option($1, NULL, false); }
			| ident_or_keyword IDENT
				{ add_option($1, $2, true); }
			| ident_or_keyword SCONST
				{ add_option($1, $2, true); }
			| ident_or_keyword UCONST
				{
					char numbuf[32];

					snprintf(numbuf, sizeof(numbuf), "%u", $2);
					add_option($1, numbuf, true);
				}
			;

ident_or_keyword:
			IDENT						{ $$ = $1; }
			| K_BASE_BACKUP				{ $$ = "base_backup"; }
			| K_IDENTIFY_SYSTEM			{ $$ = "identify_system"; }
			| K_SHOW					{ $$ = "show"; }
			| K_START_REPLICATION		{ $$ = "start_replication"; }
			| K_CREATE_REPLICATION_SLOT	{ $$ = "create_replication_slot"; }
			| K_DROP_REPLICATION_SLOT	{ $$ = "drop_replication_slot"; }
			| K_TIMELINE_HISTORY		{ $$ = "timeline_history"; }
			| K_WAIT					{ $$ = "wait"; }
			| K_TIMELINE				{ $$ = "timeline"; }
			| K_PHYSICAL				{ $$ = "physical"; }
			| K_LOGICAL					{ $$ = "logical"; }
			| K_SLOT					{ $$ = "slot"; }
			| K_RESERVE_WAL				{ $$ = "reserve_wal"; }
			| K_TEMPORARY				{ $$ = "temporary"; }
			| K_TWO_PHASE				{ $$ = "two_phase"; }
			| K_EXPORT_SNAPSHOT			{ $$ = "export_snapshot"; }
			| K_NOEXPORT_SNAPSHOT		{ $$ = "noexport_snapshot"; }
			| K_USE_SNAPSHOT			{ $$ = "use_snapshot"; }
		;

%%

/*
 * yyerror is bison's required error hook: called on a syntax error (and by
 * this file's own timeline-validation actions via YYERROR above). It
 * cannot touch the socket -- repl_command_parse() below reports the
 * failure to its caller, which sends the ErrorResponse -- so it only logs
 * and records that this parse failed.
 */
static void
yyerror(const char *message)
{
	ws_parse_failed = true;
	log_error("Syntax error in replication command \"%s\": %s",
			  ws_parse_query != NULL ? ws_parse_query : "", message);
}


/* dotted_name builds "a.b" for a var_name like "wal_segment_size" that
 * SHOW never actually needs multi-part, but real Postgres's grammar
 * supports it, so this does too. */
static char *
dotted_name(const char *a, const char *b)
{
	size_t len = strlen(a) + strlen(b) + 2;
	char *out = malloc(len); /* IGNORE-BANNED */

	if (out != NULL)
	{
		snprintf(out, len, "%s.%s", a, b);
	}

	return out;
}


/*
 * add_option appends one BASE_BACKUP/CREATE_REPLICATION_SLOT option onto
 * the command being parsed -- this project's plain-array stand-in for
 * lappend()'ing a DefElem onto a List. Silently drops an option beyond
 * WS_MAX_COMMAND_OPTIONS rather than erroring: real clients never send
 * anywhere near that many.
 */
static void
add_option(const char *name, const char *value, bool hasValue)
{
	if (ws_parse_cmd->nOptions >= WS_MAX_COMMAND_OPTIONS)
	{
		log_warn("Ignoring replication command option \"%s\": too many "
				 "options in one command (max %d)",
				 name, WS_MAX_COMMAND_OPTIONS);
		return;
	}

	WsCommandOption *opt = &ws_parse_cmd->options[ws_parse_cmd->nOptions++];

	strlcpy(opt->name, name, sizeof(opt->name));
	opt->hasValue = hasValue;

	if (hasValue && value != NULL)
	{
		strlcpy(opt->value, value, sizeof(opt->value));
	}
	else
	{
		opt->value[0] = '\0';
	}
}


/*
 * repl_command_parse fills *cmd from the given Query-message string,
 * running the flex/bison parser generated from repl_scanner.l/repl_gram.y
 * above (see repl_command.h). Not reentrant (global yylex()/yyparse()
 * state, same as this project's own pgaftest test_spec_scan.l/
 * test_spec_parse.y), which is safe here because pg_walsender forks one
 * child per connection (accept_loop.c) and never parses two commands
 * concurrently in the same process.
 */
extern void ws_scanner_init(const char *str);
extern void ws_scanner_finish(void);

bool
repl_command_parse(const char *query, WsCommand *cmd)
{
	memset(cmd, 0, sizeof(WsCommand));
	cmd->type = WS_CMD_UNKNOWN;

	ws_parse_cmd = cmd;
	ws_parse_failed = false;
	ws_parse_query = (char *) query;

	ws_scanner_init(query);

	int parseResult = yyparse();

	ws_scanner_finish();

	if (parseResult != 0 || ws_parse_failed || cmd->type == WS_CMD_UNKNOWN)
	{
		cmd->type = WS_CMD_UNKNOWN;
		return false;
	}

	return true;
}


/*
 * ws_command_find_option looks up a previously parsed BASE_BACKUP/
 * CREATE_REPLICATION_SLOT option by name (case-insensitive, matching
 * ident_or_keyword's own case folding via the scanner's IDENT rule).
 */
const WsCommandOption *
ws_command_find_option(const WsCommand *cmd, const char *name)
{
	for (int i = 0; i < cmd->nOptions; i++)
	{
		if (strcasecmp(cmd->options[i].name, name) == 0)
		{
			return &cmd->options[i];
		}
	}

	return NULL;
}
