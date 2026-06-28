%{
/*
 * src/bin/pgaftest/test_spec_parse.y
 *   Bison grammar for .pgaf test specification files.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_spec.h"
#include "pgsetup.h"

/* provided by test_spec_scan.l */
extern int  yylex(void);
extern int  pgaf_line_number;
extern FILE *yyin;

/* the spec we're building */
static TestSpec *current_spec = NULL;

static void yyerror(const char *msg)
{
	fprintf(stderr, "pgaftest: parse error at line %d: %s\n",
	        pgaf_line_number, msg);
	exit(1);
}

/* helpers */
static void append_cmd(TestStep *step, TestCmd *cmd)
{
	if (!step->commands)
	{
		step->commands = cmd;
	}
	else
	{
		TestCmd *c = step->commands;
		while (c->next) c = c->next;
		c->next = cmd;
	}
}

/*
 * collect_block_content — extract content from a `{ ... }` block.
 *
 * src points to the opening `{`.  Two forms are supported:
 *
 * Inline:    { content }         or   { { r1 } { r2 } }
 *   The matching closing `}` is found with depth counting (brace-aware).
 *   The content between the outermost `{}` is captured.
 *
 * Multi-line: {
 *               line1
 *               line2
 *             }
 *   Subsequent lines are read via strtok(NULL, "\n") until a line that
 *   is just `}`.
 *
 * The captured content (without surrounding braces) is written into out.
 */
static void
collect_block_content(const char *src, char *out, int outlen)
{
	/* skip '{' and leading whitespace */
	src++;
	while (*src == ' ' || *src == '\t') src++;

	/*
	 * Inline form: look for the MATCHING closing brace on this line,
	 * counting nested braces.  Stop at '\n' so we fall through to the
	 * multi-line path when the block spans multiple lines.
	 */
	{
		int depth = 1;
		const char *p = src;
		const char *close = NULL;
		while (*p && *p != '\n')
		{
			if (*p == '{') depth++;
			else if (*p == '}') { if (--depth == 0) { close = p; break; } }
			p++;
		}
		if (close)
		{
			int len = (int)(close - src);
			while (len > 0 && (src[len-1] == ' ' || src[len-1] == '\t'))
				len--;
			if (len >= outlen) len = outlen - 1;
			memcpy(out, src, len);
			out[len] = '\0';
			return;
		}
	}

	/*
	 * Multi-line form: content starts on the remainder of the current line
	 * (may be empty) then continues on subsequent lines until a line that
	 * is just `}`.
	 */
	int pos = 0;

	if (*src)
	{
		int l = strlen(src);
		while (l > 0 && (src[l-1] == '\r' || src[l-1] == '\n' ||
		                  src[l-1] == ' '  || src[l-1] == '\t'))
			l--;
		if (l > 0)
		{
			if (l >= outlen - 1) l = outlen - 1;
			memcpy(out, src, l);
			pos = l;
			out[pos] = '\0';
		}
	}

	char *nxtline;
	while ((nxtline = strtok(NULL, "\n")) != NULL)
	{
		char *p = nxtline;
		while (*p == ' ' || *p == '\t') p++;

		if (*p == '}' && (p[1] == '\0' || p[1] == ' ' || p[1] == '\t'))
			break;

		if (pos > 0 && pos < outlen - 1)
			out[pos++] = '\n';

		int l = strlen(p);
		while (l > 0 && (p[l-1] == '\r' || p[l-1] == '\n')) l--;

		if (pos + l >= outlen)
			l = outlen - pos - 1;
		if (l > 0)
		{
			memcpy(out + pos, p, l);
			pos += l;
		}
		out[pos] = '\0';
	}
}

/*
 * expand_tuple_expect — convert `{ r1 } { r2 }` tuple syntax into
 * the newline-separated form that psql --tuples-only --no-align produces.
 *
 * When the expect content starts with `{ ` (brace + space, to avoid
 * colliding with PostgreSQL array literals like `{1,2}`), each `{ val }`
 * group is extracted and joined with '\n'.
 *
 * Examples:
 *   "{ 1 } { 2 }"         →  "1\n2"
 *   "{ node1\tprimary }"  →  "node1\tprimary"
 *   "2"                   →  "2" (unchanged — no leading brace-space)
 */
static void
expand_tuple_expect(char *buf, int buflen)
{
	const char *p = buf;
	while (*p == ' ' || *p == '\t') p++;

	/* Tuple format requires "{ " (brace followed by space/content, not "{x") */
	if (p[0] != '{' || (p[1] != ' ' && p[1] != '\t'))
		return;  /* not tuple format */

	char tmp[4096] = { 0 };
	int  pos = 0;
	bool first = true;

	while (*p)
	{
		/* skip whitespace between tuples */
		while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		if (*p == '\0') break;
		if (*p != '{') break;   /* unexpected char */

		p++;  /* skip '{' */
		while (*p == ' ' || *p == '\t') p++;

		/* collect until matching '}' */
		char row[1024] = { 0 };
		int  ri = 0;
		int  depth = 1;
		while (*p && depth > 0)
		{
			if (*p == '{') depth++;
			else if (*p == '}') { if (--depth == 0) break; }
			if (depth > 0 && ri < (int)sizeof(row) - 1)
				row[ri++] = *p;
			p++;
		}
		if (*p == '}') p++;

		/* trim trailing whitespace from row */
		while (ri > 0 && (row[ri-1] == ' ' || row[ri-1] == '\t')) ri--;
		row[ri] = '\0';

		if (!first && pos < (int)sizeof(tmp) - 1)
			tmp[pos++] = '\n';
		int l = ri;
		if (pos + l >= (int)sizeof(tmp)) l = (int)sizeof(tmp) - pos - 1;
		if (l > 0) { memcpy(tmp + pos, row, l); pos += l; }
		tmp[pos] = '\0';
		first = false;
	}

	if (!first)
		strncpy(buf, tmp, buflen - 1);
}

static void register_step(TestSpec *spec, TestStep *step)
{
	if (!spec->steps)
	{
		spec->steps = step;
	}
	else
	{
		TestStep *s = spec->steps;
		while (s->next) s = s->next;
		s->next = step;
	}
	spec->stepCount++;
}

/* -----------------------------------------------------------------------
 * Cluster block mini-parser
 *
 * parse_node_line, parse_formation_block, and parse_cluster_block live in
 * the %{ %} prologue so that bison sees them as C code, not grammar rules.
 * ----------------------------------------------------------------------- */

static void parse_formation_block(const char **pp, TestFormation *f,
                                   TestCluster *cl);

/*
 * parse_node_line — parse one node line inside a formation block.
 *
 *   name [kind] [option...]
 *
 * Supported kinds:   postgres (default), coordinator, worker
 * Supported options: async
 *                    candidate-priority N   or  candidate-priority=N
 *                    group N                or  group=N
 *                    no-monitor
 *                    listen
 *                    citus-secondary
 *                    citus-cluster-name N   or  citus-cluster-name=NAME
 *                    port N                 or  port=N
 *                    debian-cluster NAME    or  debian-cluster=NAME
 *                    ssl MODE               or  ssl=MODE
 *                    auth-method METHOD     or  auth-method=METHOD
 *
 * Advances *pp past the line (stops at '\n' or '\0').
 */
static void
parse_node_line(const char **pp, TestFormation *f, TestCluster *cl)
{
	const char *p = *pp;

	/* skip leading whitespace */
	while (*p == ' ' || *p == '\t') p++;

	/* empty or comment */
	if (*p == '\0' || *p == '#' || *p == '\n') {
		while (*p && *p != '\n') p++;
		*pp = p;
		return;
	}

	if (f->nodeCount >= PGAF_MAX_NODES) {
		fprintf(stderr, "pgaftest: too many nodes in formation (max %d)\n",
		        PGAF_MAX_NODES);
		exit(1);
	}

	TestNode *n = &f->nodes[f->nodeCount++];
	n->kind = NODE_KIND_STANDALONE;
	n->candidatePriority = 50;
	n->replicationQuorum = true;

	/* first token: node name */
	char tok[128];
	int i = 0;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 127)
		tok[i++] = *p++;
	tok[i] = '\0';
	strlcpy(n->name, tok, sizeof(n->name));

	/* remaining tokens on the line */
	while (*p && *p != '\n') {
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '\n' || *p == '#') break;

		/* read next token (may be "key=value") */
		i = 0;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 127)
			tok[i++] = *p++;
		tok[i] = '\0';

		/* split "key=value" tokens into key + embedded value */
		char key[128], val[128];
		char *eq = strchr(tok, '=');
		if (eq) {
			int kl = (int)(eq - tok);
			memcpy(key, tok, kl); key[kl] = '\0';
			strlcpy(val, eq + 1, sizeof(val));
		} else {
			strlcpy(key, tok, sizeof(key));
			val[0] = '\0';
		}

		/* helper: read next whitespace-separated token into val */
#define READ_VAL() do { \
	while (*p == ' ' || *p == '\t') p++; \
	int _vi = 0; \
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && _vi < 127) \
		val[_vi++] = *p++; \
	val[_vi] = '\0'; \
} while (0)

		if (strcmp(key, "postgres") == 0)
			n->kind = NODE_KIND_STANDALONE;
		else if (strcmp(key, "coordinator") == 0) {
			n->kind = NODE_KIND_CITUS_COORDINATOR;
			cl->withCitus = true;
		}
		else if (strcmp(key, "worker") == 0) {
			n->kind = NODE_KIND_CITUS_WORKER;
			cl->withCitus = true;
		}
		else if (strcmp(key, "async") == 0)
			n->replicationQuorum = false;
		else if (strcmp(key, "replication-quorum") == 0) {
			/* replication-quorum=false is equivalent to async */
			if (!val[0]) READ_VAL();
			if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0)
				n->replicationQuorum = false;
			else
				n->replicationQuorum = true;
		}
		else if (strcmp(key, "no-monitor") == 0)
			n->noMonitor = true;
		else if (strcmp(key, "listen") == 0)
			n->listen = true;
		else if (strcmp(key, "citus-secondary") == 0)
			n->citusSecondary = true;
		else if (strcmp(key, "candidate-priority") == 0) {
			if (!val[0]) READ_VAL();
			n->candidatePriority = atoi(val);
		}
		else if (strcmp(key, "group") == 0) {
			if (!val[0]) READ_VAL();
			n->group = atoi(val);
		}
		else if (strcmp(key, "port") == 0) {
			if (!val[0]) READ_VAL();
			n->pgPort = atoi(val);
		}
		else if (strcmp(key, "citus-cluster-name") == 0) {
			if (!val[0]) READ_VAL();
			strlcpy(n->citusClusterName, val, sizeof(n->citusClusterName));
		}
		else if (strcmp(key, "debian-cluster") == 0) {
			if (!val[0]) READ_VAL();
			strlcpy(n->debianCluster, val, sizeof(n->debianCluster));
		}
		else if (strcmp(key, "ssl") == 0) {
			if (!val[0]) READ_VAL();
			strlcpy(n->ssl, val, sizeof(n->ssl));
		}
		else if (strcmp(key, "auth-method") == 0 || strcmp(key, "auth") == 0) {
			if (!val[0]) READ_VAL();
			strlcpy(n->auth, val, sizeof(n->auth));
		}
		/* unknown tokens silently ignored */

#undef READ_VAL
	}

	/* advance past newline */
	if (*p == '\n') p++;
	*pp = p;
}

/*
 * parse_formation_block — parse the body of a formation { } section.
 *
 * *pp points to the first character after the opening '{'.
 * Returns with *pp pointing past the closing '}'.
 */
static void
parse_formation_block(const char **pp, TestFormation *f, TestCluster *cl)
{
	const char *p = *pp;

	while (*p) {
		/* skip whitespace */
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		if (*p == '\0') break;

		/* closing brace ends the formation */
		if (*p == '}') { p++; break; }

		/* comment */
		if (*p == '#') {
			while (*p && *p != '\n') p++;
			continue;
		}

		/* node line */
		parse_node_line(&p, f, cl);
	}

	*pp = p;
}

/*
 * parse_cluster_block — parse the body of the cluster { } block.
 */
static void
parse_cluster_block(const char *text, TestCluster *cl)
{
	const char *p = text;

	while (*p) {
		/* skip whitespace */
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		if (*p == '\0') break;

		/* comment */
		if (*p == '#') {
			while (*p && *p != '\n') p++;
			continue;
		}

		/* read the keyword / first token of the line */
		char kw[64];
		int  i = 0;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 63)
			kw[i++] = *p++;
		kw[i] = '\0';

		if (strcmp(kw, "monitor") == 0) {
			cl->withMonitor = true;
			/* parse inline options: port N, port=N, ssl=MODE, auth-method=METHOD */
			while (*p && *p != '\n') {
				while (*p == ' ' || *p == '\t') p++;
				if (*p == '\0' || *p == '\n' || *p == '#') break;

				char mkey[64], mval[64];
				int mi = 0;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n' && mi < 63)
					mkey[mi++] = *p++;
				mkey[mi] = '\0';

				char *meq = strchr(mkey, '=');
				if (meq) {
					int mkl = (int)(meq - mkey);
					char pure_key[64];
					memcpy(pure_key, mkey, mkl); pure_key[mkl] = '\0';
					strlcpy(mval, meq + 1, sizeof(mval));
					strlcpy(mkey, pure_key, sizeof(mkey));
				} else {
					mval[0] = '\0';
				}

				if (!mval[0]) {
					while (*p == ' ' || *p == '\t') p++;
					mi = 0;
					while (*p && *p != ' ' && *p != '\t' && *p != '\n' && mi < 63)
						mval[mi++] = *p++;
					mval[mi] = '\0';
				}

				if (strcmp(mkey, "port") == 0)
					cl->monitorHostPort = atoi(mval);
				else if (strcmp(mkey, "ssl") == 0)
					strlcpy(cl->ssl, mval, sizeof(cl->ssl));
				else if (strcmp(mkey, "auth-method") == 0 ||
				         strcmp(mkey, "auth") == 0)
					strlcpy(cl->auth, mval, sizeof(cl->auth));
				/* other monitor options silently ignored for now */
			}
			while (*p && *p != '\n') p++;
		}
		else if (strcmp(kw, "image") == 0) {
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '"') {
				p++;
				i = 0;
				while (*p && *p != '"' && i < 255) cl->image[i++] = *p++;
				cl->image[i] = '\0';
				if (*p == '"') p++;
			} else {
				i = 0;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 255)
					cl->image[i++] = *p++;
				cl->image[i] = '\0';
			}
			while (*p && *p != '\n') p++;
		}
		else if (strcmp(kw, "ssl") == 0) {
			while (*p == ' ' || *p == '\t') p++;
			i = 0;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 31)
				cl->ssl[i++] = *p++;
			cl->ssl[i] = '\0';
			while (*p && *p != '\n') p++;
		}
		else if (strcmp(kw, "auth") == 0) {
			while (*p == ' ' || *p == '\t') p++;
			i = 0;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < 31)
				cl->auth[i++] = *p++;
			cl->auth[i] = '\0';
			while (*p && *p != '\n') p++;
		}
		else if (strcmp(kw, "formation") == 0) {
			if (cl->formationCount >= PGAF_MAX_FORMATIONS) {
				fprintf(stderr, "pgaftest: too many formations (max %d)\n",
				        PGAF_MAX_FORMATIONS);
				exit(1);
			}
			TestFormation *f = &cl->formations[cl->formationCount++];
			strlcpy(f->name, "default", sizeof(f->name));
			f->numSync = -1;

			/* optional: name, then formation-level options, then '{' */
			bool seen_brace = false;
			while (*p && !seen_brace) {
				while (*p == ' ' || *p == '\t') p++;
				if (*p == '{') {
					p++;
					seen_brace = true;
					break;
				}
				if (*p == '\0' || *p == '\n') break;

				/* read next token */
				char tok[64];
				i = 0;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
				       *p != '{' && i < 63)
					tok[i++] = *p++;
				tok[i] = '\0';

				if (strcmp(tok, "num-sync") == 0) {
					while (*p == ' ' || *p == '\t') p++;
					f->numSync = atoi(p);
					while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
				}
				else if (tok[0] != '\0') {
					/* treat as formation name */
					strlcpy(f->name, tok, sizeof(f->name));
				}
			}

			if (!seen_brace) {
				/* skip to '{' which may be on a following line */
				while (*p && *p != '{') p++;
				if (*p == '{') p++;
			}

			parse_formation_block(&p, f, cl);
		}
		else {
			/* unknown keyword — skip to end of line */
			while (*p && *p != '\n') p++;
		}

		if (*p == '\n') p++;
	}
}

%}

%union {
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}

%token T_CLUSTER T_MONITOR T_NODE T_CITUS_COORDINATOR T_CITUS_WORKER
%token T_SETUP T_TEARDOWN T_STEP T_SEQUENCE
%token T_EXEC T_WAIT T_UNTIL T_TIMEOUT T_ASSERT T_SQL T_EXPECT
%token T_NETWORK T_DISCONNECT T_CONNECT T_SLEEP T_COMPOSE T_DOWN
%token T_STATE T_ASSIGNED_STATE T_CANDIDATE_PRIORITY T_GROUP T_ASYNC
%token T_EQUALS

%token <ival> T_INTEGER
%token <str>  T_IDENT T_STRING T_BLOCK

%type <str>   ident_or_string
%type <step>  cmd_block

%%

spec:
	  spec_item
	| spec spec_item
	;

spec_item:
	  cluster_block
	| setup_block
	| teardown_block
	| named_step
	| sequence_block
	;

/* -----------------------------------------------------------------------
 * cluster { }
 *
 * The T_BLOCK token contains the raw text between the outer braces,
 * with nested { } blocks preserved verbatim (the lexer counts depth).
 * We re-parse it with parse_cluster_block() defined in the %{ %} prologue.
 * ----------------------------------------------------------------------- */

cluster_block:
	T_CLUSTER T_BLOCK
	{
		TestCluster *cl = &current_spec->cluster;

		/* cluster-level defaults */
		cl->withMonitor = true;
		strlcpy(cl->ssl,  "self-signed", sizeof(cl->ssl));
		strlcpy(cl->auth, "trust",       sizeof(cl->auth));
		cl->monitorHostPort = 0;

		parse_cluster_block($2, cl);
		free($2);
	}
	;

/* -----------------------------------------------------------------------
 * setup { }  and  teardown { }
 * ----------------------------------------------------------------------- */

setup_block:
	T_SETUP cmd_block
	{
		current_spec->setup = $2;
	}
	;

teardown_block:
	T_TEARDOWN cmd_block
	{
		current_spec->teardown = $2;
	}
	;

/* -----------------------------------------------------------------------
 * step <name> { }
 * ----------------------------------------------------------------------- */

named_step:
	T_STEP ident_or_string cmd_block
	{
		TestStep *s = $3;
		strncpy(s->name, $2, sizeof(s->name) - 1);
		free($2);
		register_step(current_spec, s);
	}
	;

cmd_block:
	T_BLOCK
	{
		/*
		 * Parse the block text into a linked list of TestCmd.
		 * We do this with a simple recursive descent over lines.
		 */
		TestStep *step = make_step("");
		char *text = strdup($1);
		free($1);

		char *line = strtok(text, "\n");
		while (line)
		{
			while (*line == ' ' || *line == '\t') line++;
			if (*line == '\0' || *line == '#') { line = strtok(NULL,"\n"); continue; }

			TestCmd *cmd = NULL;

			if (strncmp(line, "exec-fails ", 11) == 0)
			{
				cmd = make_cmd(CMD_EXEC_FAILS);
				char *rest = line + 11;
				while (*rest == ' ' || *rest == '\t') rest++;
				sscanf(rest, "%63s", cmd->service);
				char *sp = rest + strlen(cmd->service);
				while (*sp == ' ' || *sp == '\t') sp++;
				strncpy(cmd->args, sp, sizeof(cmd->args) - 1);
			}
			else if (strncmp(line, "exec ", 5) == 0)
			{
				cmd = make_cmd(CMD_EXEC);
				char *rest = line + 5;
				while (*rest == ' ' || *rest == '\t') rest++;
				/* first token = service, rest = args */
				sscanf(rest, "%63s", cmd->service);
				char *sp = rest + strlen(cmd->service);
				while (*sp == ' ' || *sp == '\t') sp++;
				strncpy(cmd->args, sp, sizeof(cmd->args) - 1);
			}
			else if (strncmp(line, "wait until ", 11) == 0)
			{
				cmd = make_cmd(CMD_WAIT_STATE);
				cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
				char node[64], kw1[32], eq[4], state[64];
				char *rest = line + 11;
				int n = sscanf(rest, "%63s %31s %3s %63s", node, kw1, eq, state);
				if (n < 4) { fprintf(stderr,"bad wait until: %s\n",line); exit(1); }
				strncpy(cmd->service, node, sizeof(cmd->service)-1);
				strncpy(cmd->state,   state, sizeof(cmd->state)-1);
				/* check for "timeout Ns" suffix */
				char *tp = strstr(rest, "timeout ");
				if (tp) cmd->timeoutSeconds = atoi(tp + 8);

				/* assigned-state variant */
				if (strcmp(kw1, "assigned-state") == 0)
					cmd->kind = CMD_ASSERT_ASSIGNED;
			}
			else if (strncmp(line, "assert ", 7) == 0)
			{
				char node[64], kw[32], eq[4], state[64];
				char *rest = line + 7;
				sscanf(rest, "%63s %31s %3s %63s", node, kw, eq, state);
				if (strcmp(kw, "assigned-state") == 0)
					cmd = make_cmd(CMD_ASSERT_ASSIGNED);
				else
					cmd = make_cmd(CMD_ASSERT_STATE);
				strncpy(cmd->service, node,  sizeof(cmd->service)-1);
				strncpy(cmd->state,   state, sizeof(cmd->state)-1);
			}
			else if (strncmp(line, "sql ", 4) == 0)
			{
				cmd = make_cmd(CMD_SQL);
				char *rest = line + 4;
				while (*rest == ' ' || *rest == '\t') rest++;
				sscanf(rest, "%63s", cmd->service);
				char *q = rest + strlen(cmd->service);
				while (*q == ' ' || *q == '\t') q++;
				if (*q == '{')
					collect_block_content(q, cmd->args, sizeof(cmd->args));
				else
					strncpy(cmd->args, q, sizeof(cmd->args)-1);
			}
			else if (strncmp(line, "expect error", 12) == 0 &&
			         (line[12] == '\0' || line[12] == ' ' || line[12] == '\t'))
			{
				cmd = make_cmd(CMD_EXPECT_ERROR);
				/* optional SQLSTATE code after "expect error" */
				char *rest = line + 12;
				while (*rest == ' ' || *rest == '\t') rest++;
				if (*rest)
					strncpy(cmd->state, rest, sizeof(cmd->state) - 1);
			}
			else if (strncmp(line, "expect ", 7) == 0)
			{
				cmd = make_cmd(CMD_EXPECT);
				char *rest = line + 7;
				while (*rest == ' ' || *rest == '\t') rest++;
				if (*rest == '{')
					collect_block_content(rest, cmd->expected, sizeof(cmd->expected));
				else
					strncpy(cmd->expected, rest, sizeof(cmd->expected)-1);
				expand_tuple_expect(cmd->expected, sizeof(cmd->expected));
			}
			else if (strncmp(line, "network disconnect ", 19) == 0)
			{
				cmd = make_cmd(CMD_NETWORK_OFF);
				strncpy(cmd->service, line + 19, sizeof(cmd->service)-1);
			}
			else if (strncmp(line, "network connect ", 16) == 0)
			{
				cmd = make_cmd(CMD_NETWORK_ON);
				strncpy(cmd->service, line + 16, sizeof(cmd->service)-1);
			}
			else if (strncmp(line, "sleep ", 6) == 0)
			{
				cmd = make_cmd(CMD_SLEEP);
				cmd->timeoutSeconds = atoi(line + 6);
			}
			else if (strcmp(line, "compose down") == 0 ||
			         strncmp(line, "compose down", 12) == 0)
			{
				cmd = make_cmd(CMD_COMPOSE_DOWN);
			}
			else
			{
				fprintf(stderr, "pgaftest: unknown command: %s\n", line);
				exit(1);
			}

			if (cmd) append_cmd(step, cmd);
			line = strtok(NULL, "\n");
		}
		free(text);

		/*
		 * Post-process: any CMD_SQL immediately followed by CMD_EXPECT_ERROR
		 * must not fail the step when SQL errors — it just records the error
		 * for CMD_EXPECT_ERROR to validate.
		 */
		for (TestCmd *c = step->commands; c; c = c->next)
		{
			if (c->kind == CMD_SQL && c->next &&
			    c->next->kind == CMD_EXPECT_ERROR)
				c->allowError = true;
		}

		$$ = step;
	}
	;

/* -----------------------------------------------------------------------
 * sequence
 * ----------------------------------------------------------------------- */

sequence_block:
	T_SEQUENCE sequence_names
	;

sequence_names:
	  /* empty */
	| sequence_names ident_or_string
	{
		int i = current_spec->sequenceLength;
		if (i < PGAF_MAX_SEQ)
			current_spec->sequence[current_spec->sequenceLength++] = $2;
		else
		{
			fprintf(stderr, "pgaftest: too many steps in sequence (max %d)\n",
			        PGAF_MAX_SEQ);
			exit(1);
		}
	}
	;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

ident_or_string:
	  T_IDENT  { $$ = $1; }
	| T_STRING { $$ = $1; }
	;

%%

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

TestSpec *
parse_test_spec(const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f)
	{
		fprintf(stderr, "pgaftest: cannot open spec file \"%s\": %s\n",
		        filename, strerror(errno));
		return NULL;
	}

	TestSpec *spec = (TestSpec *) calloc(1, sizeof(TestSpec));
	if (!spec) { fprintf(stderr, "out of memory\n"); exit(1); }

	strncpy(spec->filename, filename, sizeof(spec->filename)-1);

	current_spec = spec;
	pgaf_line_number = 1;
	yyin = f;
	yyparse();
	fclose(f);

	return spec;
}

/* helpers used by the grammar and runner */

TestCmd *
make_cmd(TestCmdKind kind)
{
	TestCmd *c = (TestCmd *) calloc(1, sizeof(TestCmd));
	if (!c) { fprintf(stderr, "out of memory\n"); exit(1); }
	c->kind = kind;
	c->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
	return c;
}

TestStep *
make_step(const char *name)
{
	TestStep *s = (TestStep *) calloc(1, sizeof(TestStep));
	if (!s) { fprintf(stderr, "out of memory\n"); exit(1); }
	if (name) strncpy(s->name, name, sizeof(s->name)-1);
	return s;
}

TestStep *
spec_find_step(TestSpec *spec, const char *name)
{
	for (TestStep *s = spec->steps; s; s = s->next)
	{
		if (strcmp(s->name, name) == 0)
			return s;
	}
	return NULL;
}
