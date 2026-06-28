%{
/*
 * src/bin/pgaftest/test_spec_scan.l
 *   Flex lexer for .pgaf test specification files.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <string.h>
#include "test_spec.h"
#include "test_spec_parse.h"

/* track line number for error messages */
int pgaf_line_number = 1;

static char *pgaf_strdup(const char *s)
{
	char *r = strdup(s);
	if (!r) { fprintf(stderr, "out of memory\n"); exit(1); }
	return r;
}
%}

%option noyywrap
%option nounput
%option never-interactive

/* multi-line SQL/text block: { ... } */
%x BLOCK

%%

"#"[^\n]*           { /* single-line comment, skip */ }

"cluster"           { return T_CLUSTER; }
"monitor"           { return T_MONITOR; }
"node"              { return T_NODE; }
"citus-coordinator" { return T_CITUS_COORDINATOR; }
"citus-worker"      { return T_CITUS_WORKER; }

"setup"             { return T_SETUP; }
"teardown"          { return T_TEARDOWN; }
"step"              { return T_STEP; }
"sequence"          { return T_SEQUENCE; }

"exec"              { return T_EXEC; }
"wait"              { return T_WAIT; }
"until"             { return T_UNTIL; }
"timeout"           { return T_TIMEOUT; }
"assert"            { return T_ASSERT; }
"sql"               { return T_SQL; }
"expect"            { return T_EXPECT; }
"network"           { return T_NETWORK; }
"disconnect"        { return T_DISCONNECT; }
"connect"           { return T_CONNECT; }
"sleep"             { return T_SLEEP; }
"compose"           { return T_COMPOSE; }
"down"              { return T_DOWN; }

"state"             { return T_STATE; }
"assigned-state"    { return T_ASSIGNED_STATE; }
"candidate-priority" { return T_CANDIDATE_PRIORITY; }
"group"             { return T_GROUP; }
"async"             { return T_ASYNC; }

"="                 { return T_EQUALS; }

[0-9]+[sS]?        {
	/* integer, optionally followed by 's' for seconds */
	yylval.ival = atoi(yytext);
	return T_INTEGER;
}

[A-Za-z_][A-Za-z0-9_-]*  {
	yylval.str = pgaf_strdup(yytext);
	return T_IDENT;
}

\"[^\"]*\"  {
	/* quoted string: strip surrounding quotes */
	yytext[yyleng-1] = '\0';
	yylval.str = pgaf_strdup(yytext + 1);
	return T_STRING;
}

"{"         {
	/* start of a brace block — collect everything until matching } */
	int depth = 1;
	int c;
	char buf[65536];
	int  pos = 0;

	while (depth > 0 && (c = input()) != EOF)
	{
		if (c == '{') depth++;
		else if (c == '}') { depth--; if (depth == 0) break; }
		else if (c == '\n') pgaf_line_number++;
		if (depth > 0)
		{
			if (pos < (int)sizeof(buf) - 1)
				buf[pos++] = (char)c;
		}
	}
	buf[pos] = '\0';

	/* trim leading/trailing whitespace */
	char *start = buf;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
		start++;
	char *end = start + strlen(start);
	while (end > start && (end[-1]==' ' || end[-1]=='\t' ||
	       end[-1]=='\n' || end[-1]=='\r'))
		end--;
	*end = '\0';

	yylval.str = pgaf_strdup(start);
	return T_BLOCK;
}

\n          { pgaf_line_number++; }
[ \t\r]+    { /* whitespace */ }

.           {
	fprintf(stderr, "pgaftest: unexpected character '%c' at line %d\n",
	        yytext[0], pgaf_line_number);
}

%%
