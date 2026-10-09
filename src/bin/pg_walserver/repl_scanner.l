%{
/*
 * src/bin/pg_walserver/repl_scanner.l
 *   Lexer for the replication command language pg_walserver accepts on
 *   Query messages, adapted from PostgreSQL's own
 *   src/backend/replication/repl_scanner.l -- same token set, same
 *   IDENT/SCONST quoting states (xq single-quoted strings, xd
 *   double-quoted identifiers), same UCONST/RECPTR patterns as the real
 *   scanner. What's swapped out is only the backend-only plumbing: the
 *   original's ereport()-based error path becomes log_error() plus a
 *   parse-failed flag (repl_gram.y's ws_parse_failed), StringInfo becomes
 *   PQExpBuffer, and there is no %option reentrant/bison-bridge (see
 *   repl_command.h's own header comment: pg_walserver forks one child per
 *   connection, so plain global scanner/parser state is safe here, exactly
 *   like this project's own pgaftest test_spec_scan.l).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <string.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "log.h"

#include "repl_gram.h"

/* accumulates a quoted literal's content across xq/xd states */
static PQExpBuffer litbuf = NULL;

static void startlit(void);
static char *litbufdup(void);
static void addlit(const char *ytext, int yleng);
static void addlitchar(unsigned char ychar);
static char *truncate_ident(char *ident);
static char *downcase_truncate(const char *ident);

/* called from repl_gram.y's repl_command_parse(); declared here, not
 * static, so they need a prototype ahead of their own definitions below */
void ws_scanner_init(const char *str);
void ws_scanner_finish(void);

%}

%option noyywrap
%option nounput
%option noinput
%option never-interactive

/*
 * Exclusive states, same as real Postgres:
 *  <xd> delimited identifiers (double-quoted identifiers)
 *  <xq> standard single-quoted strings
 *
 * <fname> is this project's own addition, entered right after the
 * FETCH_FILE keyword: a WAL segment name is a mix of digits and letters
 * that starts with a digit (e.g. "0000000100000000000000B"), so it matches
 * neither {digit}+ (UCONST) nor {identifier} (IDENT, which also folds case
 * -- wrong for a filename) -- unlike everywhere else in this grammar, this
 * one token has to be scanned as an opaque, case-preserving filename
 * rather than a SQL-like token. A quoted name (fetch_client.c always sends
 * one, single-quoted via PQescapeLiteral) still goes through the normal
 * <xq> single-quote handling below.
 */
%x xd
%x xq
%x fname

space			[ \t\n\r\f\v]

quote			'
quotestop		{quote}

xqstart			{quote}
xqdouble		{quote}{quote}
xqinside		[^']+

dquote			\"
xdstart			{dquote}
xdstop			{dquote}
xddouble		{dquote}{dquote}
xdinside		[^"]+

digit			[0-9]
hexdigit		[0-9A-Fa-f]

ident_start		[A-Za-z\200-\377_]
ident_cont		[A-Za-z\200-\377_0-9\$]

identifier		{ident_start}{ident_cont}*

%%

BASE_BACKUP				{ return K_BASE_BACKUP; }
IDENTIFY_SYSTEM			{ return K_IDENTIFY_SYSTEM; }
READ_REPLICATION_SLOT	{ return K_READ_REPLICATION_SLOT; }
SHOW					{ return K_SHOW; }
TIMELINE				{ return K_TIMELINE; }
START_REPLICATION		{ return K_START_REPLICATION; }
CREATE_REPLICATION_SLOT	{ return K_CREATE_REPLICATION_SLOT; }
DROP_REPLICATION_SLOT	{ return K_DROP_REPLICATION_SLOT; }
TIMELINE_HISTORY		{ return K_TIMELINE_HISTORY; }
PHYSICAL				{ return K_PHYSICAL; }
RESERVE_WAL				{ return K_RESERVE_WAL; }
LOGICAL					{ return K_LOGICAL; }
SLOT					{ return K_SLOT; }
TEMPORARY				{ return K_TEMPORARY; }
TWO_PHASE				{ return K_TWO_PHASE; }
EXPORT_SNAPSHOT			{ return K_EXPORT_SNAPSHOT; }
NOEXPORT_SNAPSHOT		{ return K_NOEXPORT_SNAPSHOT; }
USE_SNAPSHOT			{ return K_USE_SNAPSHOT; }
WAIT					{ return K_WAIT; }

	/*
	 * FETCH_FILE: not part of PostgreSQL's replication grammar, this
	 * project's own extension (see cmd_fetch_file.h). BEGIN(fname) so the
	 * one argument that follows is scanned as an opaque filename -- see
	 * this file's own header comment on the <fname> state.
	 */
FETCH_FILE				{ BEGIN(fname); return K_FETCH_FILE; }

	/*
	 * CHECK_FILE / ARCHIVE_FILE: not part of PostgreSQL's replication
	 * grammar either, this project's own extensions (see cmd_check_file.h,
	 * cmd_archive_file.h). Same reasoning as FETCH_FILE above: the filename
	 * argument that follows is scanned as an opaque <fname> token.
	 */
CHECK_FILE				{ BEGIN(fname); return K_CHECK_FILE; }
ARCHIVE_FILE			{ BEGIN(fname); return K_ARCHIVE_FILE; }

{space}+		{ /* ignore */ }

<fname>{space}+	{ /* ignore */ }

<fname>{xqstart}	{
					BEGIN(xq);
					startlit();
				}

<fname>[^ \t\n\r\f\v'";]+	{
					/* a bare filename: returned byte-for-byte, unlike
					 * {identifier} this is never downcased or truncated */
					yylval.str = strdup(yytext); /* IGNORE-BANNED */
					BEGIN(INITIAL);
					return SCONST;
				}

<fname><<EOF>>	{
					log_error("missing filename after FETCH_FILE");
					return 0;
				}

{digit}+		{
					yylval.uintval = strtoul(yytext, NULL, 10);
					return UCONST;
				}

	/*
	 * CHECK_FILE's own "crc32c:<hex>" argument (see cmd_check_file.h):
	 * scanned as a single token, not "crc32c" IDENT + ':' + a hex number,
	 * because a hex CRC32C can start with a digit ("89abcdef..."), which
	 * the ordinary {identifier} rule below cannot match at all (it requires
	 * a letter/underscore first character) -- the same problem FETCH_FILE's
	 * own <fname> state exists to solve for WAL segment names, solved here
	 * instead with one combined pattern since the "crc32c:" prefix is fixed
	 * and unambiguous. Case is preserved (crc32c hex digits compare
	 * case-insensitively at the C level, see cmd_check_file.c).
	 */
crc32c:{hexdigit}+	{
					yylval.str = strdup(yytext + strlen("crc32c:")); /* IGNORE-BANNED */
					return CRC32C_HEX;
				}

{hexdigit}+\/{hexdigit}+		{
					uint32_t hi, lo;

					if (sscanf(yytext, "%X/%08X", &hi, &lo) != 2)
					{
						log_error("invalid streaming start location: \"%s\"", yytext);
						return 0;
					}

					yylval.recptr = ((uint64_t) hi) << 32 | lo;
					return RECPTR;
				}

{xqstart}		{
					BEGIN(xq);
					startlit();
				}

<xq>{quotestop}	{
					yyless(1);
					BEGIN(INITIAL);
					yylval.str = litbufdup();
					return SCONST;
				}

<xq>{xqdouble}	{
					addlitchar('\'');
				}

<xq>{xqinside}  {
					addlit(yytext, yyleng);
				}

{xdstart}		{
					BEGIN(xd);
					startlit();
				}

<xd>{xdstop}	{
					yyless(1);
					BEGIN(INITIAL);
					yylval.str = truncate_ident(litbufdup());
					return IDENT;
				}

<xd>{xddouble}	{
					addlitchar('"');
				}

<xd>{xdinside}  {
					addlit(yytext, yyleng);
				}

{identifier}	{
					yylval.str = downcase_truncate(yytext);
					return IDENT;
				}

.				{
					/* any char not recognized above is returned as itself,
					 * e.g. '(', ')', ',' -- the grammar matches on these */
					return yytext[0];
				}

<xq,xd><<EOF>>	{
					log_error("unterminated quoted string in replication command");
					return 0;
				}

<<EOF>>			{ yyterminate(); }

%%

/* the single scan buffer for the command string currently being parsed */
static YY_BUFFER_STATE ws_scan_buffer = NULL;


/*
 * ws_scanner_init points the (global, one-per-forked-connection) scanner
 * at a fresh command string, ready for yylex()/yyparse() to consume. Called
 * once per repl_command_parse() -- see repl_gram.y.
 */
void
ws_scanner_init(const char *str)
{
	ws_scan_buffer = yy_scan_string(str);
}


/*
 * ws_scanner_finish releases the scan buffer set up by ws_scanner_init,
 * once repl_command_parse() is done with it.
 */
void
ws_scanner_finish(void)
{
	if (ws_scan_buffer != NULL)
	{
		yy_delete_buffer(ws_scan_buffer);
		ws_scan_buffer = NULL;
	}
}


/*
 * startlit resets the literal accumulation buffer for a fresh xq/xd token,
 * allocating it once and reusing it across tokens (one scanner per
 * connection, so this never needs to be freed before process exit).
 */
static void
startlit(void)
{
	if (litbuf == NULL)
	{
		litbuf = createPQExpBuffer();
	}
	else
	{
		resetPQExpBuffer(litbuf);
	}
}


/* litbufdup hands back a malloc'd copy of the literal buffer collected so far. */
static char *
litbufdup(void)
{
	return strdup(litbuf->data); /* IGNORE-BANNED */
}


/* addlit appends yyleng raw bytes from the current match onto the literal buffer. */
static void
addlit(const char *ytext, int yleng)
{
	appendBinaryPQExpBuffer(litbuf, ytext, yleng);
}


/* addlitchar appends one already-unescaped character (a doubled quote) onto the literal buffer. */
static void
addlitchar(unsigned char ychar)
{
	appendPQExpBufferChar(litbuf, (char) ychar);
}


/*
 * truncate_ident mirrors PostgreSQL's own truncate_identifier(): silently
 * cut an over-long identifier down to NAMEDATALEN - 1 bytes rather than
 * rejecting it outright.
 */
static char *
truncate_ident(char *ident)
{
	if (strlen(ident) >= NAMEDATALEN)
	{
		ident[NAMEDATALEN - 1] = '\0';
	}

	return ident;
}


/*
 * downcase_truncate mirrors PostgreSQL's own downcase_truncate_identifier()
 * for an unquoted identifier: SQL identifiers are case-insensitive unless
 * double-quoted, folded to lower case here exactly like the real scanner.
 */
static char *
downcase_truncate(const char *ident)
{
	char *out = strdup(ident); /* IGNORE-BANNED */

	for (char *p = out; *p != '\0'; p++)
	{
		*p = (char) tolower((unsigned char) *p);
	}

	return truncate_ident(out);
}
