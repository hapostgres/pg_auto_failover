%{
/*
 * src/bin/pgaftest/test_spec_scan.l
 *   Flex lexer for .pgaf test specification files.
 *
 * The outer structure (cluster, setup, teardown, step, sequence) is
 * tokenised in the INITIAL state.  When a step/setup/teardown body
 * opens, we switch to the STEP_BODY exclusive state where every
 * command keyword and value becomes its own token.
 *
 * Additional exclusive states:
 *   CLUSTER_BODY   - inside a cluster { } block; tokenises the cluster
 *                    mini-language (formations, nodes, options)
 *   EXEC_ARGS      - skip whitespace, then read the service name token
 *   EXEC_ARGS_REST - read the rest of the line as a raw T_SHELL_ARGS string
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <string.h>
#include "test_spec.h"
#include "test_spec_parse.h"

int pgaf_line_number = 1;

/*
 * When the lexer sees "setup", "teardown", or "step", it sets this flag
 * so the next "{" enters STEP_BODY rather than doing the raw block read.
 */
static int pgaf_next_brace_is_step = 0;

/*
 * When the lexer sees "cluster", it sets this flag so the next "{"
 * enters CLUSTER_BODY rather than doing the raw block read.
 */
static int pgaf_next_brace_is_cluster = 0;

/*
 * When 1, the next '{' in STEP_BODY returns T_LBRACE (for a while body)
 * instead of calling pgaf_read_raw_block().  Set by the grammar via a
 * mid-rule action when T_WHILE is seen.  Also used by the setup/teardown
 * blocks that need sub-block parsing.
 */
int pgaf_next_brace_is_while = 0;

/*
 * Nesting depth for '{' tokens returned as T_LBRACE inside STEP_BODY
 * (while bodies).  0 means no nested while bodies are open.  Each T_LBRACE
 * increments this; each T_RBRACE decrements it when depth > 0; when depth
 * reaches 0 the '}' is the step's own closing brace → BEGIN(INITIAL).
 */
static int pgaf_step_brace_depth = 0;

/*
 * Nesting depth inside CLUSTER_BODY - so we know when the outermost "}"
 * closes and we can return to INITIAL.
 */
static int pgaf_cluster_depth = 0;

static char *pgaf_strdup(const char *s)
{
	char *r = strdup(s);
	if (!r) { fprintf(stderr, "out of memory\n"); exit(1); }
	return r;
}

/*
 * pgaf_read_raw_block: called when '{' is seen and we want to collect
 * everything up to the matching '}' as a single string.  Used for
 * sql/expect content inside step bodies.
 * Stores a freshly allocated trimmed string in yylval.str.
 *
 * Defined in the user-code section (after the second %%) so that it can
 * call flex's static input() function.
 */
static void pgaf_read_raw_block(void);
%}

%option noyywrap
%option nounput
%option never-interactive

%x CLUSTER_BODY
%x STEP_BODY
%x EXEC_ARGS
%x EXEC_ARGS_REST

%%

"#"[^\n]*           { /* comment */ }

"cluster"           { pgaf_next_brace_is_cluster = 1; return T_CLUSTER; }
"monitor"           { return T_MONITOR; }
"node"              { return T_NODE; }
"citus-coordinator" { return T_CITUS_COORDINATOR; }
"citus-worker"      { return T_CITUS_WORKER; }

"setup"             { pgaf_next_brace_is_step = 1; return T_SETUP; }
"teardown"          { pgaf_next_brace_is_step = 1; return T_TEARDOWN; }
"step"              { pgaf_next_brace_is_step = 1; return T_STEP; }
"sequence"          { return T_SEQUENCE; }

"="                 { return T_EQUALS; }

[0-9]+[sS]?  {
	yylval.ival = atoi(yytext);
	return T_INTEGER;
}

[A-Za-z_][A-Za-z0-9_-]*  {
	yylval.str = pgaf_strdup(yytext);
	return T_IDENT;
}

\"[^\"]*\"  {
	yytext[yyleng - 1] = '\0';
	yylval.str = pgaf_strdup(yytext + 1);
	return T_STRING;
}

"{"  {
	if (pgaf_next_brace_is_step)
	{
		pgaf_next_brace_is_step = 0;
		BEGIN(STEP_BODY);
		return T_LBRACE;
	}
	if (pgaf_next_brace_is_cluster)
	{
		pgaf_next_brace_is_cluster = 0;
		pgaf_cluster_depth = 1;
		BEGIN(CLUSTER_BODY);
		return T_LBRACE;
	}
	/* fallback: should not happen in a well-formed file */
	fprintf(stderr, "pgaftest: unexpected '{' at line %d\n", pgaf_line_number);
}

\n          { pgaf_line_number++; }
[ \t\r]+    { /* whitespace */ }

.  {
	fprintf(stderr, "pgaftest: unexpected character '%c' at line %d\n",
	        yytext[0], pgaf_line_number);
}

<CLUSTER_BODY>"#"[^\n]*   { /* comment */ }
<CLUSTER_BODY>\n           { pgaf_line_number++; }
<CLUSTER_BODY>[ \t\r]+     { /* whitespace */ }

<CLUSTER_BODY>"monitor"            { return T_MONITOR; }
<CLUSTER_BODY>"image-target"       { return T_IMAGE_TARGET; }
<CLUSTER_BODY>"image"              { return T_IMAGE; }
<CLUSTER_BODY>"ssl"                { return T_SSL; }
<CLUSTER_BODY>"auth-method"        { return T_AUTH_METHOD; }
<CLUSTER_BODY>"auth"               { return T_AUTH; }
<CLUSTER_BODY>"formation"          { return T_FORMATION; }
<CLUSTER_BODY>"num-sync"           { return T_NUM_SYNC; }
<CLUSTER_BODY>"node"               { return T_NODE; }

<CLUSTER_BODY>"coordinator"        { return T_COORDINATOR; }
<CLUSTER_BODY>"worker"             { return T_WORKER; }
<CLUSTER_BODY>"async"              { return T_ASYNC; }
<CLUSTER_BODY>"no-monitor"         { return T_NO_MONITOR; }
<CLUSTER_BODY>"password"           { return T_PASSWORD; }
<CLUSTER_BODY>"monitor-password"   { return T_MONITOR_PASSWORD; }
<CLUSTER_BODY>"launch"             { return T_LAUNCH; }
<CLUSTER_BODY>"create"             { return T_CREATE; }
<CLUSTER_BODY>"deferred"           { return T_DEFERRED; }
<CLUSTER_BODY>"immediate"          { return T_IMMEDIATE; }
<CLUSTER_BODY>"false"              { return T_FALSE; }
<CLUSTER_BODY>"true"               { return T_TRUE; }
<CLUSTER_BODY>"and"                { return T_AND; }
<CLUSTER_BODY>"initially"          { return T_INITIALLY; }
<CLUSTER_BODY>"stopped"            { return T_STOPPED; }
<CLUSTER_BODY>"volume"             { return T_VOLUME; }
<CLUSTER_BODY>"listen"             { return T_LISTEN; }
<CLUSTER_BODY>"citus-secondary"    { return T_CITUS_SECONDARY; }
<CLUSTER_BODY>"candidate-priority" { return T_CANDIDATE_PRIORITY; }
<CLUSTER_BODY>"group"              { return T_GROUP; }
<CLUSTER_BODY>"port"               { return T_PORT; }
<CLUSTER_BODY>"citus-cluster-name" { return T_CITUS_CLUSTER_NAME; }
<CLUSTER_BODY>"debian-cluster"     { return T_DEBIAN_CLUSTER; }
<CLUSTER_BODY>"replication-quorum"    { return T_REPLICATION_QUORUM; }
<CLUSTER_BODY>"replication-password"  { return T_REPLICATION_PASSWORD; }
<CLUSTER_BODY>"extension-version"   { return T_EXTENSION_VERSION; }
<CLUSTER_BODY>"bind-source"         { return T_BIND_SOURCE; }

<CLUSTER_BODY>"="  { return T_EQUALS; }

<CLUSTER_BODY>[0-9]+[sS]?  {
	yylval.ival = atoi(yytext);
	return T_INTEGER;
}

<CLUSTER_BODY>\"[^\"]*\"  {
	yytext[yyleng - 1] = '\0';
	yylval.str = pgaf_strdup(yytext + 1);
	return T_STRING;
}

<CLUSTER_BODY>"{"  {
	pgaf_cluster_depth++;
	return T_LBRACE;
}

<CLUSTER_BODY>"}"  {
	pgaf_cluster_depth--;
	if (pgaf_cluster_depth == 0)
		BEGIN(INITIAL);
	return T_RBRACE;
}

<CLUSTER_BODY,STEP_BODY>"init"                { return T_FS_INIT; }
<CLUSTER_BODY,STEP_BODY>"single"              { return T_FS_SINGLE; }
<CLUSTER_BODY,STEP_BODY>"primary"             { return T_FS_PRIMARY; }
<CLUSTER_BODY,STEP_BODY>"wait_primary"        { return T_FS_WAIT_PRIMARY; }
<CLUSTER_BODY,STEP_BODY>"wait-primary"        { return T_FS_WAIT_PRIMARY; }
<CLUSTER_BODY,STEP_BODY>"wait_standby"        { return T_FS_WAIT_STANDBY; }
<CLUSTER_BODY,STEP_BODY>"wait-standby"        { return T_FS_WAIT_STANDBY; }
<CLUSTER_BODY,STEP_BODY>"demoted"             { return T_FS_DEMOTED; }
<CLUSTER_BODY,STEP_BODY>"demote_timeout"      { return T_FS_DEMOTE_TIMEOUT; }
<CLUSTER_BODY,STEP_BODY>"demote-timeout"      { return T_FS_DEMOTE_TIMEOUT; }
<CLUSTER_BODY,STEP_BODY>"draining"            { return T_FS_DRAINING; }
<CLUSTER_BODY,STEP_BODY>"secondary"           { return T_FS_SECONDARY; }
<CLUSTER_BODY,STEP_BODY>"catchingup"          { return T_FS_CATCHINGUP; }
<CLUSTER_BODY,STEP_BODY>"prepare_promotion"   { return T_FS_PREP_PROMOTION; }
<CLUSTER_BODY,STEP_BODY>"prepare-promotion"   { return T_FS_PREP_PROMOTION; }
<CLUSTER_BODY,STEP_BODY>"stop_replication"    { return T_FS_STOP_REPLICATION; }
<CLUSTER_BODY,STEP_BODY>"stop-replication"    { return T_FS_STOP_REPLICATION; }
<CLUSTER_BODY,STEP_BODY>"maintenance"         { return T_FS_MAINTENANCE; }
<CLUSTER_BODY,STEP_BODY>"join_primary"        { return T_FS_JOIN_PRIMARY; }
<CLUSTER_BODY,STEP_BODY>"join-primary"        { return T_FS_JOIN_PRIMARY; }
<CLUSTER_BODY,STEP_BODY>"apply_settings"      { return T_FS_APPLY_SETTINGS; }
<CLUSTER_BODY,STEP_BODY>"apply-settings"      { return T_FS_APPLY_SETTINGS; }
<CLUSTER_BODY,STEP_BODY>"prepare_maintenance" { return T_FS_PREPARE_MAINTENANCE; }
<CLUSTER_BODY,STEP_BODY>"prepare-maintenance" { return T_FS_PREPARE_MAINTENANCE; }
<CLUSTER_BODY,STEP_BODY>"wait_maintenance"    { return T_FS_WAIT_MAINTENANCE; }
<CLUSTER_BODY,STEP_BODY>"wait-maintenance"    { return T_FS_WAIT_MAINTENANCE; }
<CLUSTER_BODY,STEP_BODY>"report_lsn"          { return T_FS_REPORT_LSN; }
<CLUSTER_BODY,STEP_BODY>"report-lsn"          { return T_FS_REPORT_LSN; }
<CLUSTER_BODY,STEP_BODY>"fast_forward"        { return T_FS_FAST_FORWARD; }
<CLUSTER_BODY,STEP_BODY>"fast-forward"        { return T_FS_FAST_FORWARD; }
<CLUSTER_BODY,STEP_BODY>"join_secondary"      { return T_FS_JOIN_SECONDARY; }
<CLUSTER_BODY,STEP_BODY>"join-secondary"      { return T_FS_JOIN_SECONDARY; }
<CLUSTER_BODY,STEP_BODY>"dropped"             { return T_FS_DROPPED; }

<CLUSTER_BODY>[A-Za-z_][A-Za-z0-9_-]*  {
	yylval.str = pgaf_strdup(yytext);
	return T_IDENT;
}

<STEP_BODY>"#"[^\n]*  { /* comment */ }
<STEP_BODY>\n          { pgaf_line_number++; }
<STEP_BODY>[ \t\r]+    { /* whitespace */ }

<STEP_BODY>"exec-fails"    { BEGIN(EXEC_ARGS); return T_EXEC_FAILS; }
<STEP_BODY>"exec"          { BEGIN(EXEC_ARGS); return T_EXEC; }
<STEP_BODY>"run"           { BEGIN(EXEC_ARGS); return T_RUN; }
<STEP_BODY>"pg_autoctl"    { BEGIN(EXEC_ARGS); return T_PG_AUTOCTL; }

<STEP_BODY>"wait"          { return T_WAIT; }
<STEP_BODY>"until"         { return T_UNTIL; }
<STEP_BODY>"timeout"       { return T_TIMEOUT; }
<STEP_BODY>"assert"        { return T_ASSERT; }
<STEP_BODY>"sql"           { return T_SQL; }
<STEP_BODY>"expect"        { return T_EXPECT; }
<STEP_BODY>"error"         { return T_ERROR; }
<STEP_BODY>"promote"       { return T_PROMOTE; }
<STEP_BODY>"network"       { return T_NETWORK; }
<STEP_BODY>"disconnect"    { return T_DISCONNECT; }
<STEP_BODY>"connect"       { return T_CONNECT; }
<STEP_BODY>"sleep"         { return T_SLEEP; }
<STEP_BODY>"compose"       { return T_COMPOSE; }
<STEP_BODY>"down"          { return T_DOWN; }
<STEP_BODY>"start"         { return T_START; }
<STEP_BODY>"stop"           { return T_STOP; }
<STEP_BODY>"stopped"       { return T_STOPPED; }
<STEP_BODY>"kill"          { return T_KILL; }
<STEP_BODY>"in"            { return T_IN; }
<STEP_BODY>"state"         { return T_STATE; }
<STEP_BODY>"assigned-state"      { return T_ASSIGNED_STATE; }
<STEP_BODY>"candidate-priority"  { return T_CANDIDATE_PRIORITY; }
<STEP_BODY>"group"         { return T_GROUP; }
<STEP_BODY>"and"           { return T_AND; }
<STEP_BODY>"is"            { return T_IS; }
<STEP_BODY>"with"          { return T_WITH; }
<STEP_BODY>"="             { return T_EQUALS; }
<STEP_BODY>","             { return T_COMMA; }
<STEP_BODY>"postgres"      { return T_POSTGRES; }
<STEP_BODY>"stays"         { return T_STAYS; }
<STEP_BODY>"while"         { return T_WHILE; }
<STEP_BODY>"through"       { return T_THROUGH; }
<STEP_BODY>"set"           { return T_SET; }
<STEP_BODY>"inject"        { BEGIN(EXEC_ARGS); return T_INJECT; }
<STEP_BODY>"logs"          { return T_LOGS; }
<STEP_BODY>"not"           { return T_NOT; }
<STEP_BODY>"contains"      { return T_CONTAINS; }
<STEP_BODY>"matches"       { return T_MATCHES; }

<STEP_BODY>[0-9]+[sS]?  {
	yylval.ival = atoi(yytext);
	return T_INTEGER;
}

<STEP_BODY>\"[^\"]*\"  {
	yytext[yyleng - 1] = '\0';
	yylval.str = pgaf_strdup(yytext + 1);
	return T_STRING;
}

<STEP_BODY>"{"  {
	if (pgaf_next_brace_is_while) {
		pgaf_next_brace_is_while = 0;
		pgaf_step_brace_depth++;
		return T_LBRACE;
	}
	pgaf_read_raw_block();
	return T_BLOCK;
}

<STEP_BODY>"}"  {
	if (pgaf_step_brace_depth > 0) {
		pgaf_step_brace_depth--;
		return T_RBRACE;
	}
	pgaf_step_brace_depth = 0;
	BEGIN(INITIAL);
	return T_RBRACE;
}

<STEP_BODY>[A-Za-z_][A-Za-z0-9_-]*  {
	yylval.str = pgaf_strdup(yytext);
	return T_IDENT;
}

<EXEC_ARGS>[ \t]+    { /* skip whitespace before service name */ }

<EXEC_ARGS>[^ \t\n]+  {
	yylval.str = pgaf_strdup(yytext);
	BEGIN(EXEC_ARGS_REST);
	return T_IDENT;
}

<EXEC_ARGS>\n  {
	pgaf_line_number++;
	BEGIN(STEP_BODY);
}

<EXEC_ARGS_REST>[^\n]+  {
	char *p = yytext;
	while (*p == ' ' || *p == '\t') p++;
	yylval.str = pgaf_strdup(p);
	BEGIN(STEP_BODY);
	return T_SHELL_ARGS;
}

<EXEC_ARGS_REST>\n  {
	pgaf_line_number++;
	BEGIN(STEP_BODY);
}

%%

static void
pgaf_read_raw_block(void)
{
	int  depth = 1;
	int  c;
	char buf[65536];
	int  pos = 0;

	while (depth > 0 && (c = input()) != EOF)
	{
		if      (c == '{') depth++;
		else if (c == '}') { depth--; if (depth == 0) break; }
		else if (c == '\n') pgaf_line_number++;
		if (depth > 0 && pos < (int)sizeof(buf) - 1)
			buf[pos++] = (char) c;
	}
	buf[pos] = '\0';

	char *start = buf;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
		start++;
	char *end = start + strlen(start);
	while (end > start &&
	       (end[-1] == ' ' || end[-1] == '\t' ||
	        end[-1] == '\n' || end[-1] == '\r'))
		end--;
	*end = '\0';

	yylval.str = pgaf_strdup(start);
}
