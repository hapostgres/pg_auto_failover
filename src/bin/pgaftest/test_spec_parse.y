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
%type <step>  step_block cmd_block
%type <cmd>   command command_list

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
 * ----------------------------------------------------------------------- */

cluster_block:
	T_CLUSTER T_BLOCK
	{
		/*
		 * Re-parse the block text as a mini node-list.
		 * We do this with a simple line-by-line parser here rather than
		 * a recursive grammar to keep things simple.
		 */
		char *text = $2;
		char *line = strtok(text, "\n");

		current_spec->cluster.withMonitor = true;
		current_spec->cluster.numSync = -1;

		while (line)
		{
			/* skip leading whitespace */
			while (*line == ' ' || *line == '\t') line++;

			if (strncmp(line, "monitor", 7) == 0)
			{
				current_spec->cluster.withMonitor = true;
			}
			else if (strncmp(line, "citus-coordinator", 17) == 0)
			{
				TestNode *n = &current_spec->cluster.nodes[
				              current_spec->cluster.nodeCount++];
				sscanf(line + 18, "%63s", n->name);
				n->kind = NODE_KIND_CITUS_COORDINATOR;
				n->group = 0;
				n->candidatePriority = 50;
				n->replicationQuorum = true;
				current_spec->cluster.withCitus = true;
			}
			else if (strncmp(line, "citus-worker", 12) == 0)
			{
				TestNode *n = &current_spec->cluster.nodes[
				              current_spec->cluster.nodeCount++];
				char rest[256];
				sscanf(line + 13, "%63s %255[^\n]", n->name, rest);
				n->kind = NODE_KIND_CITUS_WORKER;
				n->candidatePriority = 50;
				n->replicationQuorum = true;
				/* parse group=N from rest */
				char *gp = strstr(rest, "group=");
				if (gp) n->group = atoi(gp + 6);
				current_spec->cluster.withCitus = true;
			}
			else if (strncmp(line, "node", 4) == 0)
			{
				TestNode *n = &current_spec->cluster.nodes[
				              current_spec->cluster.nodeCount++];
				n->kind = NODE_KIND_STANDALONE;
				n->candidatePriority = 50;
				n->replicationQuorum = true;

				char rest[256] = { 0 };
				sscanf(line + 5, "%63s %255[^\n]", n->name, rest);

				/* parse options: candidate-priority=N  async */
				char *cp = strstr(rest, "candidate-priority=");
				if (cp) n->candidatePriority = atoi(cp + 19);

				if (strstr(rest, "async"))
					n->replicationQuorum = false;
			}

			line = strtok(NULL, "\n");
		}
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

			if (strncmp(line, "exec ", 5) == 0)
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
				/* sql <svc> { SQL } — but the { } was already consumed by the
				 * outer block parser; here we just have the inline form
				 * sql <svc> <single-line SQL> or sql <svc> { multi-line }
				 * For simplicity we treat everything after svc as the query. */
				char *rest = line + 4;
				while (*rest == ' ' || *rest == '\t') rest++;
				sscanf(rest, "%63s", cmd->service);
				char *q = rest + strlen(cmd->service);
				while (*q == ' ' || *q == '\t') q++;
				strncpy(cmd->args, q, sizeof(cmd->args)-1);
			}
			else if (strncmp(line, "expect ", 7) == 0)
			{
				cmd = make_cmd(CMD_EXPECT);
				strncpy(cmd->expected, line + 7, sizeof(cmd->expected)-1);
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
	spec->cluster.numSync = -1;

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
