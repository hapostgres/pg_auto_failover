%{
/*
 * src/bin/pgaftest/test_spec_parse.y
 *   Bison grammar for .pgaf test specification files.
 *
 * The outer structure (cluster, setup, teardown, step, sequence) is
 * described here as bison rules.  Inside step/setup/teardown bodies
 * the flex lexer enters the STEP_BODY exclusive state and returns
 * individual tokens for every keyword, identifier, integer, and
 * punctuation character — no more hand-written strstr/strtok parsing.
 *
 * The cluster { } block is now also fully parsed by this grammar.
 * The flex lexer enters the CLUSTER_BODY exclusive state when it sees
 * the opening '{' after "cluster", returning proper tokens for every
 * keyword, value, and punctuation inside.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_spec.h"
#include "pgsetup.h"
#include "file_utils.h"

/* provided by test_spec_scan.l */
extern int  yylex(void);
extern int  pgaf_line_number;
extern FILE *yyin;
extern int  pgaf_next_brace_is_while; /* set before T_LBRACE for while body */

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
 * expand_tuple_expect — convert `{ r1 } { r2 }` tuple syntax into
 * the newline-separated form that psql --tuples-only --no-align produces.
 */
static void
expand_tuple_expect(char *buf, int buflen)
{
	const char *p = buf;
	while (*p == ' ' || *p == '\t') p++;

	if (p[0] != '{' || (p[1] != ' ' && p[1] != '\t'))
		return;

	char tmp[4096] = { 0 };
	int  pos = 0;
	bool first = true;

	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		if (*p == '\0') break;
		if (*p != '{') break;

		p++;
		while (*p == ' ' || *p == '\t') p++;

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
 * Static state used by multi-element grammar rules.
 *
 * The parser is single-threaded; these are only live during the reduction
 * of a single rule so there is no re-entrancy concern.
 * ----------------------------------------------------------------------- */

static TestCmd       *current_wait_cmd    = NULL;
static TestCmd       *current_promote_cmd = NULL;
static TestCmd       *current_pass_cmd    = NULL;  /* for opt_passing_through */
static TestFormation *current_formation   = NULL;
static TestNode      *current_node        = NULL;

%}

%union {
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}

/* ---- Outer-structure tokens (used in INITIAL lex state) ---- */
%token T_CLUSTER T_MONITOR T_NODE T_CITUS_COORDINATOR T_CITUS_WORKER
%token T_SETUP T_TEARDOWN T_STEP T_SEQUENCE
%token T_EQUALS

/* ---- Cluster-body tokens ---- */
%token T_IMAGE T_IMAGE_TARGET T_SSL T_AUTH T_AUTH_METHOD T_FORMATION T_NUM_SYNC
%token T_COORDINATOR T_WORKER T_ASYNC T_NO_MONITOR
%token T_LAUNCH T_DEFERRED T_IMMEDIATE T_INITIALLY T_VOLUME
%token T_LISTEN T_CITUS_SECONDARY T_CANDIDATE_PRIORITY T_PORT T_PASSWORD T_MONITOR_PASSWORD
%token T_CITUS_CLUSTER_NAME T_DEBIAN_CLUSTER T_REPLICATION_QUORUM T_REPLICATION_PASSWORD
%token T_EXTENSION_VERSION T_BIND_SOURCE

/* ---- FSM state tokens (used in CLUSTER_BODY and STEP_BODY) ---- */
%token T_FS_INIT T_FS_SINGLE T_FS_PRIMARY
%token T_FS_WAIT_PRIMARY T_FS_WAIT_STANDBY
%token T_FS_DEMOTED T_FS_DEMOTE_TIMEOUT T_FS_DRAINING
%token T_FS_SECONDARY T_FS_CATCHINGUP
%token T_FS_PREP_PROMOTION T_FS_STOP_REPLICATION
%token T_FS_MAINTENANCE T_FS_JOIN_PRIMARY T_FS_APPLY_SETTINGS
%token T_FS_PREPARE_MAINTENANCE T_FS_WAIT_MAINTENANCE
%token T_FS_REPORT_LSN T_FS_FAST_FORWARD T_FS_JOIN_SECONDARY
%token T_FS_DROPPED

/* ---- Step-body tokens (used in STEP_BODY lex state) ---- */
%token T_EXEC T_EXEC_FAILS T_PG_AUTOCTL
%token T_WAIT T_UNTIL T_TIMEOUT T_AND T_IS T_WITH
%token T_ASSERT
%token T_SQL T_EXPECT T_ERROR
%token T_PROMOTE
%token T_NETWORK T_DISCONNECT T_CONNECT
%token T_SLEEP
%token T_COMPOSE T_DOWN T_START T_STOP T_STOPPED T_KILL T_INJECT
%token T_STATE T_ASSIGNED_STATE
%token T_IN T_GROUP
%token T_LBRACE T_RBRACE T_COMMA
%token T_POSTGRES T_STAYS T_WHILE T_THROUGH T_SET
%token T_LOGS T_NOT T_CONTAINS T_MATCHES

/* ---- Tokens with values ---- */
%token <ival> T_INTEGER
%token <str>  T_IDENT T_STRING T_BLOCK T_SHELL_ARGS

/* ---- Non-terminal types ---- */
%type <str>   ident_or_string
%type <str>   bare_name
%type <str>   fsm_state
%type <str>   node_name
%type <step>  cmd_block cmd_list
%type <cmd>   step_cmd
%type <cmd>   exec_cmd wait_cmd assert_cmd sql_cmd expect_cmd
%type <cmd>   promote_cmd network_cmd sleep_cmd compose_cmd
%type <cmd>   postgres_ctl_cmd stays_while_cmd set_monitor_cmd logs_cmd
%type <ival>  opt_timeout
%type <step>  while_body

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
 * The flex lexer enters CLUSTER_BODY on the opening '{' and returns to
 * INITIAL on the outermost closing '}'.  Every keyword and value inside
 * the cluster block is a proper token — no hand-written string parsing.
 * ----------------------------------------------------------------------- */

cluster_block:
	T_CLUSTER T_LBRACE
	{
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	}
	cluster_item_list T_RBRACE
	;

cluster_item_list:
	  /* empty */
	| cluster_item_list cluster_item
	;

cluster_item:
	  monitor_line
	| image_line
	| ssl_line
	| auth_line
	| extension_version_line
	| formation_block
	| T_BIND_SOURCE { current_spec->cluster.bindSource = true; }
	;

/*
 * monitor [port N]
 *
 * The monitor keyword may optionally be followed by "port N".  The ssl and
 * auth settings for the monitor are handled by the top-level ssl_line and
 * auth_line rules (they write to the same TestCluster fields), so we do not
 * duplicate them here.  Keeping only T_PORT avoids shift/reduce conflicts
 * with ssl_line and auth_line in cluster_item_list.
 */
monitor_line:
	  T_MONITOR
	{
		current_spec->cluster.withMonitor = true;
	}
	| T_MONITOR T_DEBIAN_CLUSTER T_IDENT
	{
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, $3,
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free($3);
	}
	| T_MONITOR T_IMAGE_TARGET T_IDENT
	{
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, $3,
		        sizeof(current_spec->cluster.monitorImageTarget));
		free($3);
	}
	| T_MONITOR T_PORT T_INTEGER
	{
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) $3;
	}
	| T_MONITOR T_PASSWORD T_STRING
	{
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, $3,
		        sizeof(current_spec->cluster.monitorPassword));
		free($3);
	}
	| T_MONITOR T_IDENT T_LAUNCH T_DEFERRED
	{
		strlcpy(current_spec->cluster.secondMonitorName, $2,
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free($2);
	}
	| T_MONITOR T_IDENT T_INITIALLY T_STOPPED
	{
		strlcpy(current_spec->cluster.secondMonitorName, $2,
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free($2);
	}
	| T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING
	{
		strlcpy(current_spec->cluster.secondMonitorName, $2,
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free($2);
		/* password for second monitor not yet stored */
		free($6);
	}
	;

/* image "tag" | image tag */
image_line:
	  T_IMAGE T_STRING
	{
		strlcpy(current_spec->cluster.image, $2,
		        sizeof(current_spec->cluster.image));
		free($2);
	}
	| T_IMAGE T_IDENT
	{
		strlcpy(current_spec->cluster.image, $2,
		        sizeof(current_spec->cluster.image));
		free($2);
	}
	;

/* extension-version VALUE — sets PG_AUTOCTL_EXTENSION_VERSION on the monitor */
extension_version_line:
	T_EXTENSION_VERSION T_IDENT
	{
		strlcpy(current_spec->cluster.extensionVersion, $2,
		        sizeof(current_spec->cluster.extensionVersion));
		free($2);
	}
	| T_EXTENSION_VERSION T_STRING
	{
		strlcpy(current_spec->cluster.extensionVersion, $2,
		        sizeof(current_spec->cluster.extensionVersion));
		free($2);
	}
	;

/* ssl VALUE */
ssl_line:
	T_SSL T_IDENT
	{
		strlcpy(current_spec->cluster.ssl, $2,
		        sizeof(current_spec->cluster.ssl));
		free($2);
	}
	;

/* auth VALUE | auth-method VALUE */
auth_line:
	  T_AUTH T_IDENT
	{
		strlcpy(current_spec->cluster.auth, $2,
		        sizeof(current_spec->cluster.auth));
		free($2);
	}
	| T_AUTH_METHOD T_IDENT
	{
		strlcpy(current_spec->cluster.auth, $2,
		        sizeof(current_spec->cluster.auth));
		free($2);
	}
	;

/* formation [name] [num-sync N] { node_list } */
formation_block:
	T_FORMATION
	{
		TestCluster *cl = &current_spec->cluster;
		if (cl->formationCount >= PGAF_MAX_FORMATIONS)
		{
			fprintf(stderr, "pgaftest: too many formations (max %d)\n",
			        PGAF_MAX_FORMATIONS);
			exit(1);
		}
		current_formation = &cl->formations[cl->formationCount++];
		strlcpy(current_formation->name, "default",
		        sizeof(current_formation->name));
		current_formation->numSync = -1;
	}
	formation_opt_list T_LBRACE node_list T_RBRACE
	;

formation_opt_list:
	  /* empty */
	| formation_opt_list formation_opt
	;

/*
 * bare_name allows any identifier or quoted string as a name, plus keywords
 * that are likely to be used as formation/node names (e.g. "auth", "node",
 * "monitor").  Using a keyword as a name is a common source of parse errors.
 */
bare_name:
	  T_IDENT   { $$ = $1; }
	| T_STRING  { $$ = $1; }
	| T_AUTH    { $$ = strdup("auth"); }
	| T_MONITOR { $$ = strdup("monitor"); }
	| T_NODE    { $$ = strdup("node"); }
	;

formation_opt:
	  bare_name
	{
		strlcpy(current_formation->name, $1, sizeof(current_formation->name));
		free($1);
	}
	| T_NUM_SYNC T_INTEGER
	{
		current_formation->numSync = $2;
	}
	;

node_list:
	  /* empty */
	| node_list node_line
	;

/*
 * node_name — the first token on a node line.
 *
 * Node names are usually plain identifiers like "node1", "coord", "w1".
 * They can also collide with reserved cluster keywords (e.g. a node named
 * "monitor" or "node").  We allow T_MONITOR and T_NODE here so the grammar
 * doesn't choke on such names.  The FSM-state catch-all handles any state
 * name used as a node name (unlikely but defensive).
 */
/*
 * node_name covers bare-identifier node names used in the flat syntax.
 * T_NODE is intentionally excluded: "node" is reserved for the block syntax
 * "node foo { ... }" and must not be reduced here to avoid a shift-reduce
 * conflict with T_NODE T_IDENT T_LBRACE.
 */
node_name:
	  T_IDENT    { $$ = $1; }
	| T_MONITOR  { $$ = strdup("monitor"); }
	;

/*
 * init_node_slot — shared mid-rule action that allocates a node slot and
 * sets defaults.  Used by both node_line productions.
 */
init_node_slot:
	/* empty */
	{
		if (current_formation->nodeCount >= PGAF_MAX_NODES)
		{
			fprintf(stderr, "pgaftest: too many nodes in formation (max %d)\n",
			        PGAF_MAX_NODES);
			exit(1);
		}
		current_node = &current_formation->nodes[current_formation->nodeCount++];
		current_node->kind = NODE_KIND_STANDALONE;
		current_node->candidatePriority = 50;
		current_node->replicationQuorum = true;
	}
	;

node_line:
	/* flat syntax: node1 listen port 5432 ... */
	node_name init_node_slot
	{
		strlcpy(current_node->name, $1, sizeof(current_node->name));
		free($1);
	}
	node_opt_list
	/* block syntax: node foo { listen \n port 5432 \n ... } */
	| T_NODE T_IDENT init_node_slot
	{
		strlcpy(current_node->name, $2, sizeof(current_node->name));
		free($2);
	}
	T_LBRACE node_opt_list T_RBRACE
	;

node_opt_list:
	  /* empty */
	| node_opt_list node_opt
	;

node_opt:
	  T_COORDINATOR
	{
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
	| T_WORKER
	{
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
	| T_ASYNC
	{
		current_node->replicationQuorum = false;
	}
	| T_NO_MONITOR
	{
		current_node->noMonitor = true;
	}
	| T_DEFERRED
	{
		current_node->launchDeferred = true;
	}
	| T_LAUNCH T_DEFERRED
	{
		current_node->launchDeferred = true;
	}
	| T_LAUNCH T_IMMEDIATE
	{
		current_node->launchDeferred = false;
	}
	| T_IMMEDIATE
	{
		current_node->launchDeferred = false;
	}
	| T_LISTEN
	{
		current_node->listen = true;
	}
	| T_CITUS_SECONDARY
	{
		current_node->citusSecondary = true;
	}
	| T_CANDIDATE_PRIORITY T_INTEGER
	{
		current_node->candidatePriority = $2;
	}
	| T_GROUP T_INTEGER
	{
		current_node->group = $2;
	}
	| T_PORT T_INTEGER
	{
		current_node->pgPort = $2;
	}
	| T_CITUS_CLUSTER_NAME T_IDENT
	{
		strlcpy(current_node->citusClusterName, $2,
		        sizeof(current_node->citusClusterName));
		free($2);
	}
	| T_DEBIAN_CLUSTER T_IDENT
	{
		strlcpy(current_node->debianCluster, $2,
		        sizeof(current_node->debianCluster));
		free($2);
	}
	| T_SSL T_IDENT
	{
		strlcpy(current_node->ssl, $2, sizeof(current_node->ssl));
		free($2);
	}
	| T_AUTH T_IDENT
	{
		strlcpy(current_node->auth, $2, sizeof(current_node->auth));
		free($2);
	}
	| T_AUTH_METHOD T_IDENT
	{
		strlcpy(current_node->auth, $2, sizeof(current_node->auth));
		free($2);
	}
	| T_REPLICATION_QUORUM T_IDENT
	{
		if (strcmp($2, "false") == 0 || strcmp($2, "0") == 0)
			current_node->replicationQuorum = false;
		else
			current_node->replicationQuorum = true;
		free($2);
	}
	| T_REPLICATION_PASSWORD T_STRING
	{
		strlcpy(current_node->replicationPassword, $2,
		        sizeof(current_node->replicationPassword));
		free($2);
	}
	| T_MONITOR_PASSWORD T_STRING
	{
		strlcpy(current_node->monitorPassword, $2,
		        sizeof(current_node->monitorPassword));
		free($2);
	}
	| T_VOLUME T_IDENT T_IDENT
	{
		/* volume <name> <containerPath> — adds a named Docker volume */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, $2,
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, $3,
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free($2); free($3);
	}
	| T_VOLUME T_IDENT T_STRING
	{
		/* volume <name> "/path/with spaces" */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, $2,
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, $3,
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free($2); free($3);
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

/* -----------------------------------------------------------------------
 * Step body: T_LBRACE ... T_RBRACE
 *
 * The flex lexer enters STEP_BODY state when it sees the opening T_LBRACE
 * and returns to INITIAL state after the closing T_RBRACE.  Inside, every
 * keyword and value is a distinct token — no strstr/strtok parsing.
 * ----------------------------------------------------------------------- */

cmd_block:
	T_LBRACE cmd_list T_RBRACE
	{
		/* post-process: CMD_SQL immediately before CMD_EXPECT_ERROR */
		for (TestCmd *c = $2->commands; c; c = c->next)
		{
			if (c->kind == CMD_SQL && c->next &&
			    c->next->kind == CMD_EXPECT_ERROR)
				c->allowError = true;
		}
		$$ = $2;
	}
	;

cmd_list:
	  /* empty */
	{
		$$ = make_step("");
	}
	| cmd_list step_cmd
	{
		if ($2) append_cmd($1, $2);
		$$ = $1;
	}
	;

step_cmd:
	  exec_cmd          { $$ = $1; }
	| wait_cmd          { $$ = $1; }
	| assert_cmd        { $$ = $1; }
	| sql_cmd           { $$ = $1; }
	| expect_cmd        { $$ = $1; }
	| promote_cmd       { $$ = $1; }
	| network_cmd       { $$ = $1; }
	| sleep_cmd         { $$ = $1; }
	| compose_cmd       { $$ = $1; }
	| postgres_ctl_cmd  { $$ = $1; }
	| stays_while_cmd   { $$ = $1; }
	| set_monitor_cmd   { $$ = $1; }
	| logs_cmd          { $$ = $1; }
	;

/* -----------------------------------------------------------------------
 * exec <svc> <shell-args>
 * exec-fails <svc> <shell-args>
 *
 * The flex EXEC_ARGS / EXEC_ARGS_REST states return:
 *   T_EXEC or T_EXEC_FAILS
 *   T_IDENT           (the service name)
 *   T_SHELL_ARGS      (rest of line, trimmed; omitted when line is empty)
 * ----------------------------------------------------------------------- */

exec_cmd:
	  T_EXEC T_IDENT T_SHELL_ARGS
	{
		$$ = make_cmd(CMD_EXEC);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args,    $3, sizeof($$->args));
		free($2); free($3);
	}
	| T_EXEC T_IDENT
	{
		$$ = make_cmd(CMD_EXEC);
		strlcpy($$->service, $2, sizeof($$->service));
		free($2);
	}
	| T_EXEC_FAILS T_IDENT T_SHELL_ARGS
	{
		$$ = make_cmd(CMD_EXEC_FAILS);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args,    $3, sizeof($$->args));
		free($2); free($3);
	}
	| T_EXEC_FAILS T_IDENT
	{
		$$ = make_cmd(CMD_EXEC_FAILS);
		strlcpy($$->service, $2, sizeof($$->service));
		free($2);
	}
	| T_PG_AUTOCTL T_IDENT T_SHELL_ARGS
	{
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		$$ = make_cmd(CMD_PG_AUTOCTL);
		sformat($$->args, sizeof($$->args), "%s %s", $2, $3);
		free($2); free($3);
	}
	| T_PG_AUTOCTL T_IDENT
	{
		$$ = make_cmd(CMD_PG_AUTOCTL);
		strlcpy($$->args, $2, sizeof($$->args));
		free($2);
	}
	| T_PG_AUTOCTL
	{
		$$ = make_cmd(CMD_PG_AUTOCTL);
	}
	;

/* -----------------------------------------------------------------------
 * wait until ...
 *
 * Four forms, disambiguated cleanly by the token immediately after the
 * first T_IDENT (one token of LALR lookahead is sufficient):
 *
 *   wait until <node> state = <state> [timeout Ns]
 *   wait until <node> assigned-state = <state> [timeout Ns]
 *   wait until <node> stopped [timeout Ns]
 *   wait until <state>[, <state2>...] [in group N[, group M]] [timeout Ns]
 *
 * FSM state names (T_FS_*) are accepted wherever a state string is expected.
 * T_IDENT covers node names (e.g. "node1") and any future unknown states.
 * ----------------------------------------------------------------------- */

/*
 * state_op: accept '=' or 'is' as state comparison operator.
 * This lets writers use either form:
 *   wait until node1 state = primary
 *   wait until node1 state is primary
 */
state_op: T_EQUALS | T_IS ;

/*
 * wait_multi_condition_list: one or more "node state [is|=] state" conditions
 * joined by 'and'.  Builds a CMD_WAIT_MULTI command via the current_wait_cmd
 * global.
 *
 *   wait until node2 state is primary and node1 state is secondary timeout 90s
 *   wait until node2 state is primary and node1 state is secondary with timeout 90s
 */
wait_multi_condition:
	  T_IDENT T_STATE state_op fsm_state
	{
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  $1,
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], $4,
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free($1);
	}
	| T_IDENT T_STATE state_op T_IDENT
	{
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  $1,
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], $4,
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free($1); free($4);
	}
	;

wait_multi_condition_list:
	  wait_multi_condition
	| wait_multi_condition_list T_AND wait_multi_condition
	;

/*
 * opt_passing_through — optional "passing through s1, s2, ..." clause.
 *
 * These are intermediate FSM states that the node is expected to transit
 * through on the way to the target state.  The runner verifies them via
 * LISTEN/NOTIFY on the monitor's state-change notifications.  Missing any
 * listed intermediate state is a test failure.
 *
 * Syntax (after the target state, before timeout):
 *   wait until node1 state is primary passing through wait_primary timeout 90s
 */

opt_passing_through:
	  /* empty */
	| T_THROUGH pass_state_list
	;

pass_state_list:
	  fsm_state
	{
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        $1, sizeof(current_pass_cmd->passThroughStates[0]));
	}
	| T_IDENT
	{
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        $1, sizeof(current_pass_cmd->passThroughStates[0]));
		free($1);
	}
	| pass_state_list T_COMMA fsm_state
	{
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        $3, sizeof(current_pass_cmd->passThroughStates[0]));
	}
	| pass_state_list T_COMMA T_IDENT
	{
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        $3, sizeof(current_pass_cmd->passThroughStates[0]));
		free($3);
	}
	;

wait_cmd:
	  T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state
	    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, $3, sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   $6, sizeof(current_pass_cmd->state));
	      free($3); }
	  opt_passing_through opt_timeout
	{
		current_pass_cmd->timeoutSeconds = $9;
		$$ = current_pass_cmd;
		current_pass_cmd = NULL;
	}
	| T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT
	    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, $3, sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   $6, sizeof(current_pass_cmd->state));
	      free($3); free($6); }
	  opt_passing_through opt_timeout
	{
		current_pass_cmd->timeoutSeconds = $9;
		$$ = current_pass_cmd;
		current_pass_cmd = NULL;
	}
	| T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout
	{
		$$ = make_cmd(CMD_WAIT_STATE);
		$$->kind = CMD_ASSERT_ASSIGNED;
		strlcpy($$->service, $3, sizeof($$->service));
		strlcpy($$->state,   $6, sizeof($$->state));
		$$->timeoutSeconds = $7;
		free($3);
	}
	| T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout
	{
		$$ = make_cmd(CMD_WAIT_STATE);
		$$->kind = CMD_ASSERT_ASSIGNED;
		strlcpy($$->service, $3, sizeof($$->service));
		strlcpy($$->state,   $6, sizeof($$->state));
		$$->timeoutSeconds = $7;
		free($3); free($6);
	}
	| T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout
	{
		$$ = make_cmd(CMD_WAIT_STOPPED);
		strlcpy($$->service, $3, sizeof($$->service));
		$$->timeoutSeconds = $5;
		free($3);
	}
	| T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout
	{
		$$ = current_wait_cmd;
		$$->timeoutSeconds = $5;
		current_wait_cmd = NULL;
	}
	/*
	 * Multi-condition form:
	 *   wait until node2 state is primary and node1 state is secondary timeout 90s
	 *   wait until node2 state is primary and node1 state is secondary with timeout 90s
	 *
	 * Parsed via wait_multi_condition_list which populates current_wait_cmd.
	 * Requires at least two conditions (single-condition uses the forms above).
	 */
	| T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout
	{
		$$ = current_wait_cmd;
		$$->timeoutSeconds = $6;
		current_wait_cmd = NULL;
	}
	;

/*
 * state_name_list — one or more FSM state names separated by commas.
 *
 * Accepts both T_FS_* tokens (proper FSM state names) and T_IDENT
 * (for forward-compatibility with unknown states).
 */
state_name_list:
	  fsm_state
	{
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        $1, sizeof(current_wait_cmd->waitStates[0]));
	}
	| T_IDENT
	{
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        $1, sizeof(current_wait_cmd->waitStates[0]));
		free($1);
	}
	| state_name_list T_COMMA fsm_state
	{
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        $3, sizeof(current_wait_cmd->waitStates[0]));
	}
	| state_name_list T_COMMA T_IDENT
	{
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        $3, sizeof(current_wait_cmd->waitStates[0]));
		free($3);
	}
	;

/*
 * opt_in_group — optional "in group N [, group M ...]" clause.
 * When absent, waitGroupCount stays 0 which means "all groups".
 */
opt_in_group:
	  /* empty */
	| T_IN group_items
	;

group_items:
	  T_GROUP T_INTEGER
	{
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = $2;
	}
	| group_items T_COMMA T_GROUP T_INTEGER
	{
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = $4;
	}
	;

opt_timeout:
	  /* empty */                  { $$ = PGAF_TIMEOUT_DEFAULT; }
	| T_TIMEOUT T_INTEGER          { $$ = $2; }
	| T_WITH T_TIMEOUT T_INTEGER   { $$ = $3; }
	;

/* -----------------------------------------------------------------------
 * assert <node> state = <state>          (instant check, no timeout)
 * assert <node> state is <state>         (same, alternate keyword)
 * assert <node> state is <s> timeout Ns  (polling wait, alias for wait until)
 * assert <node> assigned-state = <state>
 * ----------------------------------------------------------------------- */

assert_cmd:
	  T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout
	{
		$$ = make_cmd($6 > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->state,   $5, sizeof($$->state));
		$$->timeoutSeconds = $6;
		free($2);
	}
	| T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout
	{
		$$ = make_cmd($6 > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->state,   $5, sizeof($$->state));
		$$->timeoutSeconds = $6;
		free($2); free($5);
	}
	| T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout
	{
		$$ = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->state,   $5, sizeof($$->state));
		$$->timeoutSeconds = $6;
		free($2);
	}
	| T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout
	{
		$$ = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->state,   $5, sizeof($$->state));
		$$->timeoutSeconds = $6;
		free($2); free($5);
	}
	;

/* -----------------------------------------------------------------------
 * sql <svc> { SQL text }
 *
 * Inside STEP_BODY, '{' triggers the raw block reader which returns
 * T_BLOCK with the SQL content already trimmed (braces not included).
 * ----------------------------------------------------------------------- */

sql_cmd:
	T_SQL T_IDENT T_BLOCK
	{
		$$ = make_cmd(CMD_SQL);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args,    $3, sizeof($$->args));
		free($2); free($3);
	}
	;

/* -----------------------------------------------------------------------
 * expect { text }
 * expect error [SQLSTATE]
 * ----------------------------------------------------------------------- */

expect_cmd:
	  T_EXPECT T_BLOCK
	{
		$$ = make_cmd(CMD_EXPECT);
		strlcpy($$->expected, $2, sizeof($$->expected));
		expand_tuple_expect($$->expected, sizeof($$->expected));
		free($2);
	}
	| T_EXPECT T_ERROR
	{
		$$ = make_cmd(CMD_EXPECT_ERROR);
	}
	| T_EXPECT T_ERROR T_IDENT
	{
		$$ = make_cmd(CMD_EXPECT_ERROR);
		strlcpy($$->state, $3, sizeof($$->state));
		free($3);
	}
	| T_EXPECT T_ERROR T_INTEGER
	{
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		$$ = make_cmd(CMD_EXPECT_ERROR);
		snprintf($$->state, sizeof($$->state), "%d", $3);
	}
	;

/* -----------------------------------------------------------------------
 * promote node1 [, node2, ...]
 * ----------------------------------------------------------------------- */

promote_cmd:
	T_PROMOTE promote_list
	{
		$$ = current_promote_cmd;
		current_promote_cmd = NULL;
	}
	;

promote_list:
	  T_IDENT
	{
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        $1, sizeof(current_promote_cmd->promoteNodes[0]));
		free($1);
	}
	| promote_list T_COMMA T_IDENT
	{
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        $3, sizeof(current_promote_cmd->promoteNodes[0]));
		free($3);
	}
	;

/* -----------------------------------------------------------------------
 * network disconnect <node>
 * network connect <node>
 * ----------------------------------------------------------------------- */

network_cmd:
	  T_NETWORK T_DISCONNECT T_IDENT
	{
		$$ = make_cmd(CMD_NETWORK_OFF);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	| T_NETWORK T_CONNECT T_IDENT
	{
		$$ = make_cmd(CMD_NETWORK_ON);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	;

/* -----------------------------------------------------------------------
 * sleep Ns
 * ----------------------------------------------------------------------- */

sleep_cmd:
	T_SLEEP T_INTEGER
	{
		$$ = make_cmd(CMD_SLEEP);
		$$->timeoutSeconds = $2;
	}
	;

/* -----------------------------------------------------------------------
 * compose down
 * compose start <svc>
 * compose stop <svc>
 * ----------------------------------------------------------------------- */

compose_cmd:
	  T_COMPOSE T_DOWN
	{
		$$ = make_cmd(CMD_COMPOSE_DOWN);
	}
	| T_COMPOSE T_START T_IDENT
	{
		$$ = make_cmd(CMD_COMPOSE_START);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	| T_COMPOSE T_STOP T_IDENT
	{
		$$ = make_cmd(CMD_COMPOSE_STOP);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	| T_COMPOSE T_KILL T_IDENT
	{
		$$ = make_cmd(CMD_COMPOSE_KILL);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	/*
	 * compose inject <image> <src-path> <svc>:<dst-path>
	 *
	 * Injects a file from a Docker image into a running service container
	 * without restarting it.  The sequence:
	 *   docker create --name _pgaf_inject_tmp <image>
	 *   docker cp _pgaf_inject_tmp:<src-path> /tmp/_pgaf_inject_binary
	 *   docker rm _pgaf_inject_tmp
	 *   docker cp /tmp/_pgaf_inject_binary <project>-<svc>-1:<dst-path>
	 *
	 * Fields used:
	 *   expected  → image name (e.g. "pgaf:next")
	 *   args      → source path in the image (e.g. "/usr/local/bin/pg_autoctl")
	 *   service   → destination service name (e.g. "monitor")
	 *   state     → destination path in the container (same as src usually)
	 *
	 * T_INJECT triggers BEGIN(EXEC_ARGS) in the lexer, so the image name is
	 * returned as T_IDENT (matching [^ \t\n]+, which includes ':') and the
	 * remaining "src svc:dst" tokens come back as T_SHELL_ARGS.
	 */
	| T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS
	{
		$$ = make_cmd(CMD_COMPOSE_INJECT);
		strlcpy($$->expected, $3, sizeof($$->expected));  /* image */

		/* Split T_SHELL_ARGS: "<src-path> <svc>:<dst-path>" */
		char tmp[4096];
		strlcpy(tmp, $4, sizeof(tmp));
		char *src = tmp;
		char *p = tmp;
		while (*p && *p != ' ' && *p != '\t') p++;
		if (*p) { *p++ = '\0'; while (*p == ' ' || *p == '\t') p++; }
		char *svcdst = p;
		char *colon  = (*svcdst) ? strchr(svcdst, ':') : NULL;
		strlcpy($$->args, src, sizeof($$->args));
		if (colon)
		{
			*colon = '\0';
			strlcpy($$->service, svcdst,   sizeof($$->service)); /* dst svc  */
			strlcpy($$->state,   colon + 1, sizeof($$->state));  /* dst path */
		}
		free($3); free($4);
	}
	;

/* -----------------------------------------------------------------------
 * stop postgres <node> / start postgres <node>
 *
 * Sugar for `pg_autoctl manual pgctl off/on --pgdata /var/lib/postgres/pgaf`
 * run inside the named node's container.  Much cleaner than the raw pg_ctl
 * loop that was used before.
 * ----------------------------------------------------------------------- */

postgres_ctl_cmd:
	  T_STOP T_POSTGRES node_name
	{
		$$ = make_cmd(CMD_STOP_POSTGRES);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	| T_START T_POSTGRES node_name
	{
		$$ = make_cmd(CMD_START_POSTGRES);
		strlcpy($$->service, $3, sizeof($$->service));
		free($3);
	}
	;

/* -----------------------------------------------------------------------
 * assert <node> stays <state> while { commands }
 *
 * Runs the body commands and checks after each one (and at the end) that
 * the named node's reported state equals <state>.  Fails immediately if the
 * state ever changes.
 * ----------------------------------------------------------------------- */

while_body:
	T_WHILE { pgaf_next_brace_is_while = 1; } T_LBRACE cmd_list T_RBRACE
	{ $$ = $4; }
	;

stays_while_cmd:
	T_ASSERT node_name T_STAYS fsm_state while_body
	{
		$$ = make_cmd(CMD_STAYS_WHILE);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->state,   $4, sizeof($$->state));
		$$->body = ($5) ? $5->commands : NULL;
		free($2);
	}
	;

/* -----------------------------------------------------------------------
 * set monitor <service>
 *
 * Switches the runner's active monitor: LISTEN/NOTIFY reconnects to the
 * new service, and monitor_get_node_state() queries it for subsequent
 * wait-until / implicit post-failover checks.
 * ----------------------------------------------------------------------- */

set_monitor_cmd:
	T_SET T_IDENT T_IDENT
	{
		/* only "set monitor <svc>" is supported; $2 must be "monitor" */
		if (strcmp($2, "monitor") != 0)
		{
			fprintf(stderr, "pgaftest: unknown 'set' target '%s' (expected 'monitor')\n", $2);
			free($2); free($3);
			YYERROR;
		}
		$$ = make_cmd(CMD_SET_MONITOR);
		strlcpy($$->service, $3, sizeof($$->service));
		free($2); free($3);
	}
	;

/* -----------------------------------------------------------------------
 * logs <svc> [not] contains <pattern>
 * logs <svc> [not] matches  <pattern>
 *
 * Grep the container's log output (via docker compose logs) for <pattern>.
 * "contains" uses fixed-string grep; "matches" uses grep -P (PCRE).
 * With "not", the check asserts the pattern is NOT found.
 * ----------------------------------------------------------------------- */

logs_cmd:
	T_LOGS T_IDENT T_CONTAINS T_STRING
	{
		$$ = make_cmd(CMD_LOGS_CHECK);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args, $4, sizeof($$->args));
		$$->logsNegate = false;
		$$->allowError = false;  /* false = fixed string, true = PCRE */
		free($2); free($4);
	}
	| T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING
	{
		$$ = make_cmd(CMD_LOGS_CHECK);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args, $5, sizeof($$->args));
		$$->logsNegate = true;
		$$->allowError = false;
		free($2); free($5);
	}
	| T_LOGS T_IDENT T_MATCHES T_STRING
	{
		$$ = make_cmd(CMD_LOGS_CHECK);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args, $4, sizeof($$->args));
		$$->logsNegate = false;
		$$->allowError = true;   /* true = PCRE (-P) */
		free($2); free($4);
	}
	| T_LOGS T_IDENT T_NOT T_MATCHES T_STRING
	{
		$$ = make_cmd(CMD_LOGS_CHECK);
		strlcpy($$->service, $2, sizeof($$->service));
		strlcpy($$->args, $5, sizeof($$->args));
		$$->logsNegate = true;
		$$->allowError = true;
		free($2); free($5);
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
 * fsm_state — matches any FSM state token and returns its canonical name.
 *
 * Use %type <str> so callers can strlcpy the name into their cmd->state
 * field.  The returned string is a string literal — do NOT free() it.
 * ----------------------------------------------------------------------- */

fsm_state:
	  T_FS_INIT                { $$ = "init"; }
	| T_FS_SINGLE              { $$ = "single"; }
	| T_FS_PRIMARY             { $$ = "primary"; }
	| T_FS_WAIT_PRIMARY        { $$ = "wait_primary"; }
	| T_FS_WAIT_STANDBY        { $$ = "wait_standby"; }
	| T_FS_DEMOTED             { $$ = "demoted"; }
	| T_FS_DEMOTE_TIMEOUT      { $$ = "demote_timeout"; }
	| T_FS_DRAINING            { $$ = "draining"; }
	| T_FS_SECONDARY           { $$ = "secondary"; }
	| T_FS_CATCHINGUP          { $$ = "catchingup"; }
	| T_FS_PREP_PROMOTION      { $$ = "prepare_promotion"; }
	| T_FS_STOP_REPLICATION    { $$ = "stop_replication"; }
	| T_FS_MAINTENANCE         { $$ = "maintenance"; }
	| T_FS_JOIN_PRIMARY        { $$ = "join_primary"; }
	| T_FS_APPLY_SETTINGS      { $$ = "apply_settings"; }
	| T_FS_PREPARE_MAINTENANCE { $$ = "prepare_maintenance"; }
	| T_FS_WAIT_MAINTENANCE    { $$ = "wait_maintenance"; }
	| T_FS_REPORT_LSN          { $$ = "report_lsn"; }
	| T_FS_FAST_FORWARD        { $$ = "fast_forward"; }
	| T_FS_JOIN_SECONDARY      { $$ = "join_secondary"; }
	| T_FS_DROPPED             { $$ = "dropped"; }
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
