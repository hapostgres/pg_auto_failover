/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "test_spec_parse.y"

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

/*
 * create_standalone_node — shared helper behind postgres_line and
 * pg_walserver_line (see their own comment below): allocates a brand new,
 * single-node formation named after the node itself (guaranteed unique,
 * so it can't collide with a real formation{} block elsewhere in the same
 * spec) and returns its one no-monitor TestNode, ready for node_opt_list to
 * apply any further modifiers (including "command <string>" and the new
 * "alias ..." clause) exactly as it already does for an ordinary node_line.
 *
 * This is pure syntactic sugar: it produces the exact same TestFormation/
 * TestNode shape a hand-written "formation { <name> no-monitor }" block
 * already produces, so every existing container-generation code path in
 * compose_gen.c (ini writer, compose service writer, IP allocation,
 * extra_hosts) picks it up completely unchanged.
 */
static TestNode *
create_standalone_node(TestCluster *cl, const char *name)
{
	if (cl->formationCount >= PGAF_MAX_FORMATIONS)
	{
		fprintf(stderr, "pgaftest: too many formations (max %d)\n",
		        PGAF_MAX_FORMATIONS);
		exit(1);
	}

	TestFormation *form = &cl->formations[cl->formationCount++];
	strlcpy(form->name, name, sizeof(form->name));
	form->numSync = -1;

	TestNode *node = &form->nodes[form->nodeCount++];
	node->kind = NODE_KIND_STANDALONE;
	node->candidatePriority = 50;
	node->replicationQuorum = true;
	node->noMonitor = true;
	strlcpy(node->name, name, sizeof(node->name));

	return node;
}


#line 254 "test_spec_parse.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "test_spec_parse.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_CLUSTER = 3,                  /* T_CLUSTER  */
  YYSYMBOL_T_MONITOR = 4,                  /* T_MONITOR  */
  YYSYMBOL_T_NODE = 5,                     /* T_NODE  */
  YYSYMBOL_T_CITUS_COORDINATOR = 6,        /* T_CITUS_COORDINATOR  */
  YYSYMBOL_T_CITUS_WORKER = 7,             /* T_CITUS_WORKER  */
  YYSYMBOL_T_SETUP = 8,                    /* T_SETUP  */
  YYSYMBOL_T_TEARDOWN = 9,                 /* T_TEARDOWN  */
  YYSYMBOL_T_STEP = 10,                    /* T_STEP  */
  YYSYMBOL_T_SEQUENCE = 11,                /* T_SEQUENCE  */
  YYSYMBOL_T_EQUALS = 12,                  /* T_EQUALS  */
  YYSYMBOL_T_IMAGE = 13,                   /* T_IMAGE  */
  YYSYMBOL_T_IMAGE_TARGET = 14,            /* T_IMAGE_TARGET  */
  YYSYMBOL_T_SSL = 15,                     /* T_SSL  */
  YYSYMBOL_T_AUTH = 16,                    /* T_AUTH  */
  YYSYMBOL_T_AUTH_METHOD = 17,             /* T_AUTH_METHOD  */
  YYSYMBOL_T_FORMATION = 18,               /* T_FORMATION  */
  YYSYMBOL_T_NUM_SYNC = 19,                /* T_NUM_SYNC  */
  YYSYMBOL_T_COORDINATOR = 20,             /* T_COORDINATOR  */
  YYSYMBOL_T_WORKER = 21,                  /* T_WORKER  */
  YYSYMBOL_T_ASYNC = 22,                   /* T_ASYNC  */
  YYSYMBOL_T_NO_MONITOR = 23,              /* T_NO_MONITOR  */
  YYSYMBOL_T_SUSPENDED = 24,               /* T_SUSPENDED  */
  YYSYMBOL_T_LAUNCH = 25,                  /* T_LAUNCH  */
  YYSYMBOL_T_CREATE = 26,                  /* T_CREATE  */
  YYSYMBOL_T_DEFERRED = 27,                /* T_DEFERRED  */
  YYSYMBOL_T_IMMEDIATE = 28,               /* T_IMMEDIATE  */
  YYSYMBOL_T_FALSE = 29,                   /* T_FALSE  */
  YYSYMBOL_T_TRUE = 30,                    /* T_TRUE  */
  YYSYMBOL_T_INITIALLY = 31,               /* T_INITIALLY  */
  YYSYMBOL_T_VOLUME = 32,                  /* T_VOLUME  */
  YYSYMBOL_T_LISTEN = 33,                  /* T_LISTEN  */
  YYSYMBOL_T_CITUS_SECONDARY = 34,         /* T_CITUS_SECONDARY  */
  YYSYMBOL_T_CANDIDATE_PRIORITY = 35,      /* T_CANDIDATE_PRIORITY  */
  YYSYMBOL_T_PORT = 36,                    /* T_PORT  */
  YYSYMBOL_T_PASSWORD = 37,                /* T_PASSWORD  */
  YYSYMBOL_T_MONITOR_PASSWORD = 38,        /* T_MONITOR_PASSWORD  */
  YYSYMBOL_T_CITUS_CLUSTER_NAME = 39,      /* T_CITUS_CLUSTER_NAME  */
  YYSYMBOL_T_DEBIAN_CLUSTER = 40,          /* T_DEBIAN_CLUSTER  */
  YYSYMBOL_T_REPLICATION_QUORUM = 41,      /* T_REPLICATION_QUORUM  */
  YYSYMBOL_T_REPLICATION_PASSWORD = 42,    /* T_REPLICATION_PASSWORD  */
  YYSYMBOL_T_EXTENSION_VERSION = 43,       /* T_EXTENSION_VERSION  */
  YYSYMBOL_T_BIND_SOURCE = 44,             /* T_BIND_SOURCE  */
  YYSYMBOL_T_LEGACY_STARTUP = 45,          /* T_LEGACY_STARTUP  */
  YYSYMBOL_T_REGION = 46,                  /* T_REGION  */
  YYSYMBOL_T_COMMAND = 47,                 /* T_COMMAND  */
  YYSYMBOL_T_NODEINI = 48,                 /* T_NODEINI  */
  YYSYMBOL_T_PG_WALSERVER = 49,            /* T_PG_WALSERVER  */
  YYSYMBOL_T_ALIAS = 50,                   /* T_ALIAS  */
  YYSYMBOL_T_DOCKER_INIT = 51,             /* T_DOCKER_INIT  */
  YYSYMBOL_T_FS_INIT = 52,                 /* T_FS_INIT  */
  YYSYMBOL_T_FS_SINGLE = 53,               /* T_FS_SINGLE  */
  YYSYMBOL_T_FS_PRIMARY = 54,              /* T_FS_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_PRIMARY = 55,         /* T_FS_WAIT_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_STANDBY = 56,         /* T_FS_WAIT_STANDBY  */
  YYSYMBOL_T_FS_DEMOTED = 57,              /* T_FS_DEMOTED  */
  YYSYMBOL_T_FS_DEMOTE_TIMEOUT = 58,       /* T_FS_DEMOTE_TIMEOUT  */
  YYSYMBOL_T_FS_DRAINING = 59,             /* T_FS_DRAINING  */
  YYSYMBOL_T_FS_SECONDARY = 60,            /* T_FS_SECONDARY  */
  YYSYMBOL_T_FS_CATCHINGUP = 61,           /* T_FS_CATCHINGUP  */
  YYSYMBOL_T_FS_PREP_PROMOTION = 62,       /* T_FS_PREP_PROMOTION  */
  YYSYMBOL_T_FS_STOP_REPLICATION = 63,     /* T_FS_STOP_REPLICATION  */
  YYSYMBOL_T_FS_MAINTENANCE = 64,          /* T_FS_MAINTENANCE  */
  YYSYMBOL_T_FS_JOIN_PRIMARY = 65,         /* T_FS_JOIN_PRIMARY  */
  YYSYMBOL_T_FS_APPLY_SETTINGS = 66,       /* T_FS_APPLY_SETTINGS  */
  YYSYMBOL_T_FS_PREPARE_MAINTENANCE = 67,  /* T_FS_PREPARE_MAINTENANCE  */
  YYSYMBOL_T_FS_WAIT_MAINTENANCE = 68,     /* T_FS_WAIT_MAINTENANCE  */
  YYSYMBOL_T_FS_REPORT_LSN = 69,           /* T_FS_REPORT_LSN  */
  YYSYMBOL_T_FS_FAST_FORWARD = 70,         /* T_FS_FAST_FORWARD  */
  YYSYMBOL_T_FS_JOIN_SECONDARY = 71,       /* T_FS_JOIN_SECONDARY  */
  YYSYMBOL_T_FS_DROPPED = 72,              /* T_FS_DROPPED  */
  YYSYMBOL_T_EXEC = 73,                    /* T_EXEC  */
  YYSYMBOL_T_EXEC_FAILS = 74,              /* T_EXEC_FAILS  */
  YYSYMBOL_T_RUN = 75,                     /* T_RUN  */
  YYSYMBOL_T_PG_AUTOCTL = 76,              /* T_PG_AUTOCTL  */
  YYSYMBOL_T_WAIT = 77,                    /* T_WAIT  */
  YYSYMBOL_T_UNTIL = 78,                   /* T_UNTIL  */
  YYSYMBOL_T_TIMEOUT = 79,                 /* T_TIMEOUT  */
  YYSYMBOL_T_AND = 80,                     /* T_AND  */
  YYSYMBOL_T_IS = 81,                      /* T_IS  */
  YYSYMBOL_T_WITH = 82,                    /* T_WITH  */
  YYSYMBOL_T_REPLAYS = 83,                 /* T_REPLAYS  */
  YYSYMBOL_T_ASSERT = 84,                  /* T_ASSERT  */
  YYSYMBOL_T_SQL = 85,                     /* T_SQL  */
  YYSYMBOL_T_EXPECT = 86,                  /* T_EXPECT  */
  YYSYMBOL_T_ERROR = 87,                   /* T_ERROR  */
  YYSYMBOL_T_PROMOTE = 88,                 /* T_PROMOTE  */
  YYSYMBOL_T_PERFORM = 89,                 /* T_PERFORM  */
  YYSYMBOL_T_FAILOVER = 90,                /* T_FAILOVER  */
  YYSYMBOL_T_NETWORK = 91,                 /* T_NETWORK  */
  YYSYMBOL_T_DISCONNECT = 92,              /* T_DISCONNECT  */
  YYSYMBOL_T_CONNECT = 93,                 /* T_CONNECT  */
  YYSYMBOL_T_SLEEP = 94,                   /* T_SLEEP  */
  YYSYMBOL_T_COMPOSE = 95,                 /* T_COMPOSE  */
  YYSYMBOL_T_DOWN = 96,                    /* T_DOWN  */
  YYSYMBOL_T_START = 97,                   /* T_START  */
  YYSYMBOL_T_STOP = 98,                    /* T_STOP  */
  YYSYMBOL_T_STOPPED = 99,                 /* T_STOPPED  */
  YYSYMBOL_T_KILL = 100,                   /* T_KILL  */
  YYSYMBOL_T_INJECT = 101,                 /* T_INJECT  */
  YYSYMBOL_T_STATE = 102,                  /* T_STATE  */
  YYSYMBOL_T_ASSIGNED_STATE = 103,         /* T_ASSIGNED_STATE  */
  YYSYMBOL_T_IN = 104,                     /* T_IN  */
  YYSYMBOL_T_GROUP = 105,                  /* T_GROUP  */
  YYSYMBOL_T_LBRACE = 106,                 /* T_LBRACE  */
  YYSYMBOL_T_RBRACE = 107,                 /* T_RBRACE  */
  YYSYMBOL_T_COMMA = 108,                  /* T_COMMA  */
  YYSYMBOL_T_POSTGRES = 109,               /* T_POSTGRES  */
  YYSYMBOL_T_STAYS = 110,                  /* T_STAYS  */
  YYSYMBOL_T_WHILE = 111,                  /* T_WHILE  */
  YYSYMBOL_T_THROUGH = 112,                /* T_THROUGH  */
  YYSYMBOL_T_SET = 113,                    /* T_SET  */
  YYSYMBOL_T_GET = 114,                    /* T_GET  */
  YYSYMBOL_T_FSM = 115,                    /* T_FSM  */
  YYSYMBOL_T_LOGS = 116,                   /* T_LOGS  */
  YYSYMBOL_T_NOT = 117,                    /* T_NOT  */
  YYSYMBOL_T_CONTAINS = 118,               /* T_CONTAINS  */
  YYSYMBOL_T_MATCHES = 119,                /* T_MATCHES  */
  YYSYMBOL_T_INTEGER = 120,                /* T_INTEGER  */
  YYSYMBOL_T_IDENT = 121,                  /* T_IDENT  */
  YYSYMBOL_T_STRING = 122,                 /* T_STRING  */
  YYSYMBOL_T_BLOCK = 123,                  /* T_BLOCK  */
  YYSYMBOL_T_SHELL_ARGS = 124,             /* T_SHELL_ARGS  */
  YYSYMBOL_YYACCEPT = 125,                 /* $accept  */
  YYSYMBOL_spec = 126,                     /* spec  */
  YYSYMBOL_spec_item = 127,                /* spec_item  */
  YYSYMBOL_cluster_block = 128,            /* cluster_block  */
  YYSYMBOL_129_1 = 129,                    /* $@1  */
  YYSYMBOL_cluster_item_list = 130,        /* cluster_item_list  */
  YYSYMBOL_cluster_item = 131,             /* cluster_item  */
  YYSYMBOL_monitor_line = 132,             /* monitor_line  */
  YYSYMBOL_postgres_line = 133,            /* postgres_line  */
  YYSYMBOL_134_2 = 134,                    /* $@2  */
  YYSYMBOL_pg_walserver_line = 135,        /* pg_walserver_line  */
  YYSYMBOL_136_3 = 136,                    /* $@3  */
  YYSYMBOL_aux_opt_list = 137,             /* aux_opt_list  */
  YYSYMBOL_aux_opt = 138,                  /* aux_opt  */
  YYSYMBOL_image_line = 139,               /* image_line  */
  YYSYMBOL_extension_version_line = 140,   /* extension_version_line  */
  YYSYMBOL_ssl_line = 141,                 /* ssl_line  */
  YYSYMBOL_auth_line = 142,                /* auth_line  */
  YYSYMBOL_formation_block = 143,          /* formation_block  */
  YYSYMBOL_144_4 = 144,                    /* $@4  */
  YYSYMBOL_formation_opt_list = 145,       /* formation_opt_list  */
  YYSYMBOL_bare_name = 146,                /* bare_name  */
  YYSYMBOL_formation_opt = 147,            /* formation_opt  */
  YYSYMBOL_node_list = 148,                /* node_list  */
  YYSYMBOL_node_name = 149,                /* node_name  */
  YYSYMBOL_init_node_slot = 150,           /* init_node_slot  */
  YYSYMBOL_node_line = 151,                /* node_line  */
  YYSYMBOL_152_5 = 152,                    /* $@5  */
  YYSYMBOL_153_6 = 153,                    /* $@6  */
  YYSYMBOL_node_opt_list = 154,            /* node_opt_list  */
  YYSYMBOL_node_opt = 155,                 /* node_opt  */
  YYSYMBOL_alias_list = 156,               /* alias_list  */
  YYSYMBOL_setup_block = 157,              /* setup_block  */
  YYSYMBOL_teardown_block = 158,           /* teardown_block  */
  YYSYMBOL_named_step = 159,               /* named_step  */
  YYSYMBOL_cmd_block = 160,                /* cmd_block  */
  YYSYMBOL_cmd_list = 161,                 /* cmd_list  */
  YYSYMBOL_step_cmd = 162,                 /* step_cmd  */
  YYSYMBOL_exec_cmd = 163,                 /* exec_cmd  */
  YYSYMBOL_state_op = 164,                 /* state_op  */
  YYSYMBOL_wait_multi_condition = 165,     /* wait_multi_condition  */
  YYSYMBOL_wait_multi_condition_list = 166, /* wait_multi_condition_list  */
  YYSYMBOL_opt_passing_through = 167,      /* opt_passing_through  */
  YYSYMBOL_pass_state_list = 168,          /* pass_state_list  */
  YYSYMBOL_wait_cmd = 169,                 /* wait_cmd  */
  YYSYMBOL_170_7 = 170,                    /* $@7  */
  YYSYMBOL_171_8 = 171,                    /* $@8  */
  YYSYMBOL_state_name_list = 172,          /* state_name_list  */
  YYSYMBOL_opt_in_group = 173,             /* opt_in_group  */
  YYSYMBOL_group_items = 174,              /* group_items  */
  YYSYMBOL_opt_timeout = 175,              /* opt_timeout  */
  YYSYMBOL_assert_cmd = 176,               /* assert_cmd  */
  YYSYMBOL_sql_cmd = 177,                  /* sql_cmd  */
  YYSYMBOL_expect_cmd = 178,               /* expect_cmd  */
  YYSYMBOL_promote_cmd = 179,              /* promote_cmd  */
  YYSYMBOL_promote_list = 180,             /* promote_list  */
  YYSYMBOL_perform_cmd = 181,              /* perform_cmd  */
  YYSYMBOL_network_cmd = 182,              /* network_cmd  */
  YYSYMBOL_nodeini_cmd = 183,              /* nodeini_cmd  */
  YYSYMBOL_sleep_cmd = 184,                /* sleep_cmd  */
  YYSYMBOL_compose_cmd = 185,              /* compose_cmd  */
  YYSYMBOL_postgres_ctl_cmd = 186,         /* postgres_ctl_cmd  */
  YYSYMBOL_fsm_step_cmd = 187,             /* fsm_step_cmd  */
  YYSYMBOL_while_body = 188,               /* while_body  */
  YYSYMBOL_189_9 = 189,                    /* $@9  */
  YYSYMBOL_stays_while_cmd = 190,          /* stays_while_cmd  */
  YYSYMBOL_set_monitor_cmd = 191,          /* set_monitor_cmd  */
  YYSYMBOL_logs_cmd = 192,                 /* logs_cmd  */
  YYSYMBOL_sequence_block = 193,           /* sequence_block  */
  YYSYMBOL_sequence_names = 194,           /* sequence_names  */
  YYSYMBOL_fsm_state = 195,                /* fsm_state  */
  YYSYMBOL_ident_or_string = 196           /* ident_or_string  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  21
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   653

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  125
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  72
/* YYNRULES -- Number of rules.  */
#define YYNRULES  230
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  379

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   379


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   256,   256,   257,   261,   262,   263,   264,   265,   278,
     277,   287,   289,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   315,   319,   326,   333,   339,   346,   353,
     360,   427,   426,   436,   435,   482,   484,   488,   494,   495,
     503,   509,   519,   525,   535,   545,   551,   562,   561,   578,
     580,   589,   590,   591,   592,   593,   597,   602,   606,   612,
     614,   633,   634,   643,   660,   659,   667,   666,   674,   676,
     680,   685,   690,   694,   698,   702,   708,   713,   717,   722,
     726,   730,   734,   738,   742,   747,   752,   756,   760,   766,
     772,   777,   782,   787,   791,   795,   801,   809,   815,   829,
     843,   844,   857,   866,   882,   889,   900,   918,   933,   936,
     944,   945,   946,   947,   948,   949,   950,   951,   952,   953,
     954,   955,   956,   957,   958,   959,   973,   980,   986,   993,
     999,  1006,  1012,  1020,  1026,  1053,  1053,  1064,  1079,  1097,
    1098,  1113,  1115,  1119,  1127,  1135,  1142,  1154,  1153,  1165,
    1164,  1175,  1184,  1193,  1207,  1215,  1229,  1244,  1250,  1257,
    1263,  1276,  1278,  1282,  1287,  1295,  1296,  1297,  1308,  1316,
    1324,  1332,  1350,  1365,  1372,  1376,  1382,  1395,  1403,  1411,
    1432,  1439,  1446,  1454,  1470,  1476,  1497,  1505,  1520,  1534,
    1538,  1544,  1550,  1576,  1610,  1616,  1637,  1654,  1654,  1659,
    1678,  1703,  1712,  1721,  1730,  1746,  1749,  1751,  1773,  1774,
    1775,  1776,  1777,  1778,  1779,  1780,  1781,  1782,  1783,  1784,
    1785,  1786,  1787,  1788,  1789,  1790,  1791,  1792,  1793,  1801,
    1802
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "T_CLUSTER",
  "T_MONITOR", "T_NODE", "T_CITUS_COORDINATOR", "T_CITUS_WORKER",
  "T_SETUP", "T_TEARDOWN", "T_STEP", "T_SEQUENCE", "T_EQUALS", "T_IMAGE",
  "T_IMAGE_TARGET", "T_SSL", "T_AUTH", "T_AUTH_METHOD", "T_FORMATION",
  "T_NUM_SYNC", "T_COORDINATOR", "T_WORKER", "T_ASYNC", "T_NO_MONITOR",
  "T_SUSPENDED", "T_LAUNCH", "T_CREATE", "T_DEFERRED", "T_IMMEDIATE",
  "T_FALSE", "T_TRUE", "T_INITIALLY", "T_VOLUME", "T_LISTEN",
  "T_CITUS_SECONDARY", "T_CANDIDATE_PRIORITY", "T_PORT", "T_PASSWORD",
  "T_MONITOR_PASSWORD", "T_CITUS_CLUSTER_NAME", "T_DEBIAN_CLUSTER",
  "T_REPLICATION_QUORUM", "T_REPLICATION_PASSWORD", "T_EXTENSION_VERSION",
  "T_BIND_SOURCE", "T_LEGACY_STARTUP", "T_REGION", "T_COMMAND",
  "T_NODEINI", "T_PG_WALSERVER", "T_ALIAS", "T_DOCKER_INIT", "T_FS_INIT",
  "T_FS_SINGLE", "T_FS_PRIMARY", "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY",
  "T_FS_DEMOTED", "T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
  "T_FS_CATCHINGUP", "T_FS_PREP_PROMOTION", "T_FS_STOP_REPLICATION",
  "T_FS_MAINTENANCE", "T_FS_JOIN_PRIMARY", "T_FS_APPLY_SETTINGS",
  "T_FS_PREPARE_MAINTENANCE", "T_FS_WAIT_MAINTENANCE", "T_FS_REPORT_LSN",
  "T_FS_FAST_FORWARD", "T_FS_JOIN_SECONDARY", "T_FS_DROPPED", "T_EXEC",
  "T_EXEC_FAILS", "T_RUN", "T_PG_AUTOCTL", "T_WAIT", "T_UNTIL",
  "T_TIMEOUT", "T_AND", "T_IS", "T_WITH", "T_REPLAYS", "T_ASSERT", "T_SQL",
  "T_EXPECT", "T_ERROR", "T_PROMOTE", "T_PERFORM", "T_FAILOVER",
  "T_NETWORK", "T_DISCONNECT", "T_CONNECT", "T_SLEEP", "T_COMPOSE",
  "T_DOWN", "T_START", "T_STOP", "T_STOPPED", "T_KILL", "T_INJECT",
  "T_STATE", "T_ASSIGNED_STATE", "T_IN", "T_GROUP", "T_LBRACE", "T_RBRACE",
  "T_COMMA", "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH", "T_SET",
  "T_GET", "T_FSM", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES",
  "T_INTEGER", "T_IDENT", "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept",
  "spec", "spec_item", "cluster_block", "$@1", "cluster_item_list",
  "cluster_item", "monitor_line", "postgres_line", "$@2",
  "pg_walserver_line", "$@3", "aux_opt_list", "aux_opt", "image_line",
  "extension_version_line", "ssl_line", "auth_line", "formation_block",
  "$@4", "formation_opt_list", "bare_name", "formation_opt", "node_list",
  "node_name", "init_node_slot", "node_line", "$@5", "$@6",
  "node_opt_list", "node_opt", "alias_list", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "$@7", "$@8", "state_name_list", "opt_in_group",
  "group_items", "opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd",
  "promote_cmd", "promote_list", "perform_cmd", "network_cmd",
  "nodeini_cmd", "sleep_cmd", "compose_cmd", "postgres_ctl_cmd",
  "fsm_step_cmd", "while_body", "$@9", "stays_while_cmd",
  "set_monitor_cmd", "logs_cmd", "sequence_block", "sequence_names",
  "fsm_state", "ident_or_string", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-189)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-139)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      85,   -67,   -65,   -65,   -92,  -189,    81,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,   -65,
     -92,  -189,  -189,  -189,   481,  -189,  -189,     8,   -42,   -72,
     -66,   -63,   -52,    -3,    -1,   -41,   -69,   -34,    21,    11,
     -23,   -33,    16,    23,  -189,    -7,   106,    -2,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,     0,   -16,    17,    44,    67,
    -189,   -13,  -189,  -189,    73,  -189,    79,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,    80,    84,    78,    82,
      86,    87,   176,  -189,    10,    99,    89,     7,  -189,  -189,
     105,    46,    93,    94,  -189,  -189,    95,    96,    97,    98,
       5,     5,   100,     5,   -17,   101,   103,   104,   128,     2,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
     129,   130,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,   -68,   140,   -22,
    -189,     4,     4,   581,  -189,  -189,  -189,   131,   206,   133,
    -189,  -189,  -189,  -189,  -189,   132,  -189,  -189,  -189,  -189,
    -189,    45,   135,   136,  -189,  -189,  -189,  -189,   227,   156,
       1,  -189,  -189,   138,   163,   164,   -60,     4,     4,   165,
     155,   211,   -60,  -189,  -189,   246,   281,   150,  -189,   166,
    -189,  -189,   167,   168,  -189,  -189,   251,  -189,  -189,  -189,
    -189,   171,   263,  -189,  -189,  -189,  -189,  -189,     9,     9,
    -189,  -189,   -60,   173,   215,  -189,   316,   351,   193,  -189,
     -32,   199,   188,  -189,  -189,  -189,   -60,   -60,   -60,   -60,
    -189,  -189,   216,  -189,  -189,   198,  -189,  -189,     3,   200,
     201,  -189,  -189,  -189,  -189,   204,   245,   247,   -60,   -60,
       4,   165,  -189,  -189,   221,  -189,  -189,  -189,  -189,   222,
     209,  -189,   210,  -189,  -189,  -189,  -189,  -189,   248,  -189,
     218,   218,  -189,  -189,   386,  -189,   234,  -189,  -189,  -189,
    -189,   233,   421,   -60,   -60,  -189,  -189,  -189,   516,  -189,
    -189,  -189,  -189,   249,  -189,  -189,  -189,  -189,   252,   157,
     456,  -189,   238,   239,   240,  -189,  -189,  -189,  -189,  -189,
     102,   -14,  -189,  -189,   241,  -189,  -189,   243,   244,   267,
     269,   270,   139,   271,    54,   272,   201,  -189,   275,  -189,
    -189,  -189,   120,  -189,  -189,  -189,  -189,  -189,  -189,   340,
      65,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,   248,  -189,  -189,   339,  -189,  -189,  -189
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   206,     0,     2,     4,     5,
       6,     7,     8,     9,   108,   104,   105,   229,   230,     0,
     205,     1,     3,    11,     0,   106,   207,     0,     0,     0,
       0,     0,   134,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   107,     0,     0,     0,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   125,   118,   119,
     120,   121,   122,   123,   124,    23,     0,     0,     0,     0,
      47,     0,    21,    22,     0,    10,     0,    12,    13,    19,
      20,    14,    17,    15,    16,    18,     0,     0,   127,   129,
     131,   133,     0,    62,    61,     0,     0,   174,   173,   178,
     177,   180,     0,     0,   188,   189,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      41,    40,    44,    45,    46,    49,    42,    43,    33,    31,
       0,     0,   126,   128,   130,   132,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   158,     0,   161,
     157,     0,     0,     0,   172,   176,   175,     0,     0,     0,
     184,   185,   190,   191,   192,     0,    61,   195,   194,   200,
     196,     0,     0,     0,    25,    26,    27,    24,     0,     0,
       0,    35,    35,     0,     0,     0,   165,     0,     0,     0,
       0,     0,   165,   135,   136,     0,     0,     0,   179,     0,
     181,   193,     0,     0,   201,   203,    28,    29,    54,    55,
      53,     0,     0,    59,    51,    52,    56,    50,    34,    32,
     186,   187,   165,     0,     0,   153,     0,     0,     0,   139,
     165,     0,   162,   160,   159,   155,   165,   165,   165,   165,
     197,   199,   182,   202,   204,     0,    57,    58,     0,     0,
       0,    39,    36,   154,   166,     0,   149,   147,   165,   165,
       0,     0,   156,   163,     0,   169,   168,   171,   170,     0,
       0,    30,     0,    48,    63,    60,    37,   102,    38,   167,
     141,   141,   152,   151,     0,   140,     0,   108,   183,    63,
      64,     0,     0,   165,   165,   138,   137,   164,     0,    66,
      68,   103,   144,   142,   143,   150,   148,   198,     0,    65,
       0,    68,     0,     0,     0,    70,    71,    72,    73,    74,
       0,     0,    75,    80,     0,    81,    82,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   101,     0,    69,
     146,   145,     0,    90,    91,    92,    76,    79,    77,     0,
       0,    83,    87,    97,    88,    89,    94,    93,    95,    84,
      85,    96,   100,    86,    67,     0,    98,    99,    78
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -189,  -189,   390,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,   205,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -109,   125,  -189,  -189,  -189,    71,
    -189,    52,  -189,  -189,  -189,    25,   134,  -189,  -189,  -152,
    -188,  -189,   108,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -170,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,  -189,
    -163,   380
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     6,     7,     8,    23,    27,    77,    78,    79,   192,
      80,   191,   228,   262,    81,    82,    83,    84,    85,   125,
     190,   226,   227,   258,    95,   300,   285,   310,   318,   319,
     349,   288,     9,    10,    11,    15,    24,    48,    49,   205,
     158,   240,   303,   313,    50,   291,   290,   159,   202,   242,
     235,    51,    52,    53,    54,   100,    55,    56,    57,    58,
      59,    60,    61,   251,   279,    62,    63,    64,    12,    20,
     160,    19
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     207,   177,   178,    93,   180,   218,   219,    93,   282,    93,
     206,   239,    65,   358,   115,   195,   203,   220,    97,   233,
     221,    66,   234,    67,    68,    69,    70,   188,    16,    17,
      18,   196,   245,   189,   197,   198,   116,   117,   244,    13,
     118,    14,   247,   249,    25,   236,   237,   233,   271,    88,
     234,    71,    72,    73,    98,    89,   259,    74,    90,   260,
     261,   222,   263,   105,   106,   107,   359,   108,   109,    91,
     272,    86,    87,   267,   269,    92,   275,   276,   277,   278,
      96,    21,   200,   295,     1,   204,   201,    99,     1,     2,
       3,     4,     5,     2,     3,     4,     5,   104,   292,   293,
     181,   182,   183,   102,   103,   120,   121,   223,   126,   127,
     283,   101,   161,   162,   112,    75,   113,    76,   294,   114,
      94,   119,   224,   225,   176,   110,   176,   165,   166,   356,
     357,   306,   111,   315,   316,   322,   323,   324,   122,   314,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   284,
     168,   169,   334,   335,   336,   337,   338,   351,   339,   340,
     341,   342,   343,   212,   213,   123,   344,   345,   366,   367,
     346,   347,   322,   323,   324,   369,   370,   325,   326,   327,
     328,   329,   330,   331,   332,   333,   376,   377,   124,   334,
     335,   336,   337,   338,   128,   339,   340,   341,   342,   343,
     129,   130,   132,   344,   345,   131,   133,   346,   347,   163,
     134,   135,   164,   167,   170,   171,   172,   173,   174,   175,
     199,   179,   184,   185,   209,   348,   186,   374,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   187,
     193,   194,   208,   210,   216,   217,   211,   214,   215,   230,
     241,   250,   348,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   231,   232,   238,   252,   255,   253,
     254,   256,   257,   264,   265,   270,   274,   157,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   273,
     281,   280,   286,   287,   289,  -138,   296,  -137,   297,   298,
     302,   299,   243,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   307,   311,   301,   320,   321,   353,
     354,   355,   360,   361,   362,   375,   378,   246,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   363,
     364,   365,   352,   368,   371,   373,    22,   229,   372,   304,
      26,     0,   248,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   309,     0,     0,     0,     0,     0,
       0,   308,     0,     0,     0,     0,     0,   266,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   268,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   305,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,    28,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   312,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    29,    30,    31,    32,    33,     0,
       0,     0,     0,     0,    28,    34,    35,    36,     0,    37,
      38,     0,    39,     0,     0,    40,    41,   350,    42,    43,
       0,     0,     0,     0,     0,     0,     0,     0,    44,    29,
      30,    31,    32,    33,    45,     0,    46,    47,     0,     0,
      34,    35,    36,     0,    37,    38,     0,    39,     0,     0,
      40,    41,     0,    42,    43,     0,     0,     0,     0,     0,
       0,     0,     0,   317,     0,     0,     0,     0,     0,    45,
       0,    46,    47,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156
};

static const yytype_int16 yycheck[] =
{
     163,   110,   111,     4,   113,     4,     5,     4,     5,     4,
     162,   199,     4,    27,    14,    83,    12,    16,    87,    79,
      19,    13,    82,    15,    16,    17,    18,    25,     3,   121,
     122,    99,   202,    31,   102,   103,    36,    37,   201,   106,
      40,   106,   205,   206,    19,   197,   198,    79,    80,   121,
      82,    43,    44,    45,   123,   121,    47,    49,   121,    50,
      51,    60,   232,    96,    97,    98,    80,   100,   101,   121,
     240,   113,   114,   236,   237,    78,   246,   247,   248,   249,
     121,     0,   104,   271,     3,    81,   108,   121,     3,     8,
       9,    10,    11,     8,     9,    10,    11,   120,   268,   269,
     117,   118,   119,    92,    93,   121,   122,   106,   121,   122,
     107,    90,   102,   103,   121,   107,    10,   109,   270,   121,
     121,   121,   121,   122,   121,   109,   121,   120,   121,    27,
      28,   294,   109,   303,   304,    15,    16,    17,   121,   302,
      20,    21,    22,    23,    24,    25,    26,    27,    28,   258,
     104,   105,    32,    33,    34,    35,    36,   320,    38,    39,
      40,    41,    42,   118,   119,   121,    46,    47,    29,    30,
      50,    51,    15,    16,    17,   121,   122,    20,    21,    22,
      23,    24,    25,    26,    27,    28,   121,   122,   121,    32,
      33,    34,    35,    36,   121,    38,    39,    40,    41,    42,
     121,   121,   124,    46,    47,   121,   124,    50,    51,   110,
     124,   124,   123,   108,   121,   121,   121,   121,   121,   121,
      80,   121,   121,   120,    18,   105,   122,   107,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,   121,
     121,   121,   121,   120,    27,    99,   124,   122,   122,   121,
     105,   111,   105,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,   121,   121,   121,   121,    37,   122,
     122,   120,    29,   120,    79,   102,   108,   121,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,   120,
     122,   105,   122,   122,   120,    80,   105,    80,   106,   120,
     112,   121,   121,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,   120,   122,   108,   108,   106,   121,
     121,   121,   121,   120,   120,    25,    27,   121,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,   122,
     121,   121,   321,   122,   122,   120,     6,   192,   346,   291,
      20,    -1,   121,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,   299,    -1,    -1,    -1,    -1,    -1,
      -1,   297,    -1,    -1,    -1,    -1,    -1,   121,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   121,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   121,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    48,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   121,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    73,    74,    75,    76,    77,    -1,
      -1,    -1,    -1,    -1,    48,    84,    85,    86,    -1,    88,
      89,    -1,    91,    -1,    -1,    94,    95,   121,    97,    98,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   107,    73,
      74,    75,    76,    77,   113,    -1,   115,   116,    -1,    -1,
      84,    85,    86,    -1,    88,    89,    -1,    91,    -1,    -1,
      94,    95,    -1,    97,    98,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   107,    -1,    -1,    -1,    -1,    -1,   113,
      -1,   115,   116,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   126,   127,   128,   157,
     158,   159,   193,   106,   106,   160,   160,   121,   122,   196,
     194,     0,   127,   129,   161,   160,   196,   130,    48,    73,
      74,    75,    76,    77,    84,    85,    86,    88,    89,    91,
      94,    95,    97,    98,   107,   113,   115,   116,   162,   163,
     169,   176,   177,   178,   179,   181,   182,   183,   184,   185,
     186,   187,   190,   191,   192,     4,    13,    15,    16,    17,
      18,    43,    44,    45,    49,   107,   109,   131,   132,   133,
     135,   139,   140,   141,   142,   143,   113,   114,   121,   121,
     121,   121,    78,     4,   121,   149,   121,    87,   123,   121,
     180,    90,    92,    93,   120,    96,    97,    98,   100,   101,
     109,   109,   121,    10,   121,    14,    36,    37,    40,   121,
     121,   122,   121,   121,   121,   144,   121,   122,   121,   121,
     121,   121,   124,   124,   124,   124,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,   121,   165,   172,
     195,   102,   103,   110,   123,   120,   121,   108,   104,   105,
     121,   121,   121,   121,   121,   121,   121,   149,   149,   121,
     149,   117,   118,   119,   121,   120,   122,   121,    25,    31,
     145,   136,   134,   121,   121,    83,    99,   102,   103,    80,
     104,   108,   173,    12,    81,   164,   164,   195,   121,    18,
     120,   124,   118,   119,   122,   122,    27,    99,     4,     5,
      16,    19,    60,   106,   121,   122,   146,   147,   137,   137,
     121,   121,   121,    79,    82,   175,   164,   164,   121,   165,
     166,   105,   174,   121,   195,   175,   121,   195,   121,   195,
     111,   188,   121,   122,   122,    37,   120,    29,   148,    47,
      50,    51,   138,   175,   120,    79,   121,   195,   121,   195,
     102,    80,   175,   120,   108,   175,   175,   175,   175,   189,
     105,   122,     5,   107,   149,   151,   122,   122,   156,   120,
     171,   170,   175,   175,   164,   165,   105,   106,   120,   121,
     150,   108,   112,   167,   167,   121,   195,   120,   161,   150,
     152,   122,   121,   168,   195,   175,   175,   107,   153,   154,
     108,   106,    15,    16,    17,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    32,    33,    34,    35,    36,    38,
      39,    40,    41,    42,    46,    47,    50,    51,   105,   155,
     121,   195,   154,   121,   121,   121,    27,    28,    27,    80,
     121,   120,   120,   122,   121,   121,    29,    30,   122,   121,
     122,   122,   156,   120,   107,    25,   121,   122,    27
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   125,   126,   126,   127,   127,   127,   127,   127,   129,
     128,   130,   130,   131,   131,   131,   131,   131,   131,   131,
     131,   131,   131,   132,   132,   132,   132,   132,   132,   132,
     132,   134,   133,   136,   135,   137,   137,   138,   138,   138,
     139,   139,   140,   140,   141,   142,   142,   144,   143,   145,
     145,   146,   146,   146,   146,   146,   147,   147,   147,   148,
     148,   149,   149,   150,   152,   151,   153,   151,   154,   154,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   156,   156,   157,   158,   159,   160,   161,   161,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   163,   163,   163,   163,
     163,   163,   163,   163,   163,   164,   164,   165,   165,   166,
     166,   167,   167,   168,   168,   168,   168,   170,   169,   171,
     169,   169,   169,   169,   169,   169,   169,   172,   172,   172,
     172,   173,   173,   174,   174,   175,   175,   175,   176,   176,
     176,   176,   177,   178,   178,   178,   178,   179,   180,   180,
     181,   181,   181,   181,   182,   182,   183,   183,   184,   185,
     185,   185,   185,   185,   186,   186,   187,   189,   188,   190,
     191,   192,   192,   192,   192,   193,   194,   194,   195,   195,
     195,   195,   195,   195,   195,   195,   195,   195,   195,   195,
     195,   195,   195,   195,   195,   195,   195,   195,   195,   196,
     196
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     3,     3,     3,     4,     4,
       6,     0,     4,     0,     4,     0,     2,     2,     2,     1,
       2,     2,     2,     2,     2,     2,     2,     0,     6,     0,
       2,     1,     1,     1,     1,     1,     1,     2,     2,     0,
       2,     1,     1,     0,     0,     4,     0,     7,     0,     2,
       1,     1,     1,     1,     1,     1,     2,     2,     4,     2,
       1,     1,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     3,     3,
       2,     1,     1,     3,     2,     2,     3,     3,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     2,     3,     2,
       3,     2,     3,     2,     1,     1,     1,     4,     4,     1,
       3,     0,     2,     1,     1,     3,     3,     0,     9,     0,
       9,     7,     7,     5,     6,     5,     6,     1,     1,     3,
       3,     0,     2,     2,     4,     0,     2,     3,     6,     6,
       6,     6,     3,     2,     2,     3,     3,     2,     1,     3,
       2,     4,     5,     7,     3,     3,     5,     5,     2,     2,
       3,     3,     3,     4,     3,     3,     3,     0,     5,     5,
       3,     4,     5,     4,     5,     2,     0,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 9: /* $@1: %empty  */
#line 278 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	}
#line 1806 "test_spec_parse.c"
    break;

  case 21: /* cluster_item: T_BIND_SOURCE  */
#line 301 "test_spec_parse.y"
                        { current_spec->cluster.bindSource = true; }
#line 1812 "test_spec_parse.c"
    break;

  case 22: /* cluster_item: T_LEGACY_STARTUP  */
#line 302 "test_spec_parse.y"
                           { current_spec->cluster.legacyStartup = true; }
#line 1818 "test_spec_parse.c"
    break;

  case 23: /* monitor_line: T_MONITOR  */
#line 316 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
	}
#line 1826 "test_spec_parse.c"
    break;

  case 24: /* monitor_line: T_MONITOR T_DEBIAN_CLUSTER T_IDENT  */
#line 320 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[0].str));
	}
#line 1837 "test_spec_parse.c"
    break;

  case 25: /* monitor_line: T_MONITOR T_IMAGE_TARGET T_IDENT  */
#line 327 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[0].str));
	}
#line 1848 "test_spec_parse.c"
    break;

  case 26: /* monitor_line: T_MONITOR T_PORT T_INTEGER  */
#line 334 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[0].ival);
	}
#line 1858 "test_spec_parse.c"
    break;

  case 27: /* monitor_line: T_MONITOR T_PASSWORD T_STRING  */
#line 340 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[0].str));
	}
#line 1869 "test_spec_parse.c"
    break;

  case 28: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED  */
#line 347 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1880 "test_spec_parse.c"
    break;

  case 29: /* monitor_line: T_MONITOR T_IDENT T_INITIALLY T_STOPPED  */
#line 354 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1891 "test_spec_parse.c"
    break;

  case 30: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING  */
#line 361 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-4].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-4].str));
		/* password for second monitor not yet stored */
		free((yyvsp[0].str));
	}
#line 1904 "test_spec_parse.c"
    break;

  case 31: /* $@2: %empty  */
#line 427 "test_spec_parse.y"
        {
		current_node = create_standalone_node(&current_spec->cluster, (yyvsp[0].str));
		free((yyvsp[0].str));
	}
#line 1913 "test_spec_parse.c"
    break;

  case 33: /* $@3: %empty  */
#line 436 "test_spec_parse.y"
        {
		current_node = create_standalone_node(&current_spec->cluster, (yyvsp[0].str));
		free((yyvsp[0].str));

		/*
		 * Marks this node as the `pg_walserver <name>` DSL kind (as opposed
		 * to plain `postgres <name>` sugar or an ordinary formation node) --
		 * compose_gen.c's write_pg_walserver_default_hba() uses this to
		 * decide whether to bind-mount a generated, usable default
		 * pg_walserver_hba.conf into this node's container before its own
		 * command (below, or a "command \"...\"" override) ever runs. See
		 * that function's own header comment for the full design.
		 */
		current_node->isPgWalserver = true;

		/*
		 * Default: run pg_walserver's own "serve" mode directly as this
		 * container's PID 1, pointed at a writable directory of its own
		 * under the node's already-provisioned /var/lib/postgres volume.
		 * Zero named routes at startup is a supported, harmless state (see
		 * cli_serve_run in pg_walserver/cli_root.c); a spec that wants
		 * routes configured first overrides this via "command \"...\"".
		 *
		 * Also copies in a real, usable default pg_walserver_hba.conf --
		 * compose_gen.c's write_pg_walserver_default_hba() bind-mounts it
		 * read-only at /etc/pgaf/<name>-pg_walserver_hba.conf; this cp (after
		 * "mkdir -p" has created /var/lib/postgres/ws as this container's
		 * own user, not Docker's auto-created root:root parent directory a
		 * direct bind-mount into it would leave behind) is what actually
		 * puts it where pg_walserver reads it from. See that function's own
		 * header comment for the full design and why a direct bind-mount
		 * into /var/lib/postgres/ws isn't used instead. "|| true": the
		 * source file may not exist for a node whose name collides with
		 * nothing generated (never happens via this grammar rule, but keeps
		 * this command robust rather than failing PID 1 outright over HBA).
		 */
		strlcpy(current_node->commandOverride,
		        "mkdir -p /var/lib/postgres/ws && "
		        "(cp /etc/pgaf/$(hostname)-pg_walserver_hba.conf "
		        "/var/lib/postgres/ws/pg_walserver_hba.conf || true) && "
		        "exec pg_walserver --pgdata /var/lib/postgres/ws --port 5432",
		        sizeof(current_node->commandOverride));
	}
#line 1961 "test_spec_parse.c"
    break;

  case 37: /* aux_opt: T_COMMAND T_STRING  */
#line 489 "test_spec_parse.y"
        {
		strlcpy(current_node->commandOverride, (yyvsp[0].str),
		        sizeof(current_node->commandOverride));
		free((yyvsp[0].str));
	}
#line 1971 "test_spec_parse.c"
    break;

  case 39: /* aux_opt: T_DOCKER_INIT  */
#line 496 "test_spec_parse.y"
        {
		current_node->dockerInit = true;
	}
#line 1979 "test_spec_parse.c"
    break;

  case 40: /* image_line: T_IMAGE T_STRING  */
#line 504 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1989 "test_spec_parse.c"
    break;

  case 41: /* image_line: T_IMAGE T_IDENT  */
#line 510 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1999 "test_spec_parse.c"
    break;

  case 42: /* extension_version_line: T_EXTENSION_VERSION T_IDENT  */
#line 520 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2009 "test_spec_parse.c"
    break;

  case 43: /* extension_version_line: T_EXTENSION_VERSION T_STRING  */
#line 526 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2019 "test_spec_parse.c"
    break;

  case 44: /* ssl_line: T_SSL T_IDENT  */
#line 536 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl, (yyvsp[0].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[0].str));
	}
#line 2029 "test_spec_parse.c"
    break;

  case 45: /* auth_line: T_AUTH T_IDENT  */
#line 546 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2039 "test_spec_parse.c"
    break;

  case 46: /* auth_line: T_AUTH_METHOD T_IDENT  */
#line 552 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2049 "test_spec_parse.c"
    break;

  case 47: /* $@4: %empty  */
#line 562 "test_spec_parse.y"
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
#line 2067 "test_spec_parse.c"
    break;

  case 51: /* bare_name: T_IDENT  */
#line 589 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2073 "test_spec_parse.c"
    break;

  case 52: /* bare_name: T_STRING  */
#line 590 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2079 "test_spec_parse.c"
    break;

  case 53: /* bare_name: T_AUTH  */
#line 591 "test_spec_parse.y"
                    { (yyval.str) = strdup("auth"); }
#line 2085 "test_spec_parse.c"
    break;

  case 54: /* bare_name: T_MONITOR  */
#line 592 "test_spec_parse.y"
                    { (yyval.str) = strdup("monitor"); }
#line 2091 "test_spec_parse.c"
    break;

  case 55: /* bare_name: T_NODE  */
#line 593 "test_spec_parse.y"
                    { (yyval.str) = strdup("node"); }
#line 2097 "test_spec_parse.c"
    break;

  case 56: /* formation_opt: bare_name  */
#line 598 "test_spec_parse.y"
        {
		strlcpy(current_formation->name, (yyvsp[0].str), sizeof(current_formation->name));
		free((yyvsp[0].str));
	}
#line 2106 "test_spec_parse.c"
    break;

  case 57: /* formation_opt: T_NUM_SYNC T_INTEGER  */
#line 603 "test_spec_parse.y"
        {
		current_formation->numSync = (yyvsp[0].ival);
	}
#line 2114 "test_spec_parse.c"
    break;

  case 58: /* formation_opt: T_FS_SECONDARY T_FALSE  */
#line 607 "test_spec_parse.y"
        {
		current_formation->disableSecondary = true;
	}
#line 2122 "test_spec_parse.c"
    break;

  case 61: /* node_name: T_IDENT  */
#line 633 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 2128 "test_spec_parse.c"
    break;

  case 62: /* node_name: T_MONITOR  */
#line 634 "test_spec_parse.y"
                     { (yyval.str) = strdup("monitor"); }
#line 2134 "test_spec_parse.c"
    break;

  case 63: /* init_node_slot: %empty  */
#line 643 "test_spec_parse.y"
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
#line 2151 "test_spec_parse.c"
    break;

  case 64: /* $@5: %empty  */
#line 660 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2160 "test_spec_parse.c"
    break;

  case 66: /* $@6: %empty  */
#line 667 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2169 "test_spec_parse.c"
    break;

  case 70: /* node_opt: T_COORDINATOR  */
#line 681 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
#line 2178 "test_spec_parse.c"
    break;

  case 71: /* node_opt: T_WORKER  */
#line 686 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
#line 2187 "test_spec_parse.c"
    break;

  case 72: /* node_opt: T_ASYNC  */
#line 691 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2195 "test_spec_parse.c"
    break;

  case 73: /* node_opt: T_NO_MONITOR  */
#line 695 "test_spec_parse.y"
        {
		current_node->noMonitor = true;
	}
#line 2203 "test_spec_parse.c"
    break;

  case 74: /* node_opt: T_SUSPENDED  */
#line 699 "test_spec_parse.y"
        {
		current_node->suspended = true;
	}
#line 2211 "test_spec_parse.c"
    break;

  case 75: /* node_opt: T_DEFERRED  */
#line 703 "test_spec_parse.y"
        {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2221 "test_spec_parse.c"
    break;

  case 76: /* node_opt: T_LAUNCH T_DEFERRED  */
#line 709 "test_spec_parse.y"
        {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	}
#line 2230 "test_spec_parse.c"
    break;

  case 77: /* node_opt: T_CREATE T_DEFERRED  */
#line 714 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
	}
#line 2238 "test_spec_parse.c"
    break;

  case 78: /* node_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 718 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2247 "test_spec_parse.c"
    break;

  case 79: /* node_opt: T_LAUNCH T_IMMEDIATE  */
#line 723 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2255 "test_spec_parse.c"
    break;

  case 80: /* node_opt: T_IMMEDIATE  */
#line 727 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2263 "test_spec_parse.c"
    break;

  case 81: /* node_opt: T_LISTEN  */
#line 731 "test_spec_parse.y"
        {
		current_node->listen = true;
	}
#line 2271 "test_spec_parse.c"
    break;

  case 82: /* node_opt: T_CITUS_SECONDARY  */
#line 735 "test_spec_parse.y"
        {
		current_node->citusSecondary = true;
	}
#line 2279 "test_spec_parse.c"
    break;

  case 83: /* node_opt: T_CANDIDATE_PRIORITY T_INTEGER  */
#line 739 "test_spec_parse.y"
        {
		current_node->candidatePriority = (yyvsp[0].ival);
	}
#line 2287 "test_spec_parse.c"
    break;

  case 84: /* node_opt: T_REGION T_IDENT  */
#line 743 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2296 "test_spec_parse.c"
    break;

  case 85: /* node_opt: T_REGION T_STRING  */
#line 748 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2305 "test_spec_parse.c"
    break;

  case 86: /* node_opt: T_GROUP T_INTEGER  */
#line 753 "test_spec_parse.y"
        {
		current_node->group = (yyvsp[0].ival);
	}
#line 2313 "test_spec_parse.c"
    break;

  case 87: /* node_opt: T_PORT T_INTEGER  */
#line 757 "test_spec_parse.y"
        {
		current_node->pgPort = (yyvsp[0].ival);
	}
#line 2321 "test_spec_parse.c"
    break;

  case 88: /* node_opt: T_CITUS_CLUSTER_NAME T_IDENT  */
#line 761 "test_spec_parse.y"
        {
		strlcpy(current_node->citusClusterName, (yyvsp[0].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[0].str));
	}
#line 2331 "test_spec_parse.c"
    break;

  case 89: /* node_opt: T_DEBIAN_CLUSTER T_IDENT  */
#line 767 "test_spec_parse.y"
        {
		strlcpy(current_node->debianCluster, (yyvsp[0].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[0].str));
	}
#line 2341 "test_spec_parse.c"
    break;

  case 90: /* node_opt: T_SSL T_IDENT  */
#line 773 "test_spec_parse.y"
        {
		strlcpy(current_node->ssl, (yyvsp[0].str), sizeof(current_node->ssl));
		free((yyvsp[0].str));
	}
#line 2350 "test_spec_parse.c"
    break;

  case 91: /* node_opt: T_AUTH T_IDENT  */
#line 778 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2359 "test_spec_parse.c"
    break;

  case 92: /* node_opt: T_AUTH_METHOD T_IDENT  */
#line 783 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2368 "test_spec_parse.c"
    break;

  case 93: /* node_opt: T_REPLICATION_QUORUM T_TRUE  */
#line 788 "test_spec_parse.y"
        {
		current_node->replicationQuorum = true;
	}
#line 2376 "test_spec_parse.c"
    break;

  case 94: /* node_opt: T_REPLICATION_QUORUM T_FALSE  */
#line 792 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2384 "test_spec_parse.c"
    break;

  case 95: /* node_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 796 "test_spec_parse.y"
        {
		strlcpy(current_node->replicationPassword, (yyvsp[0].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[0].str));
	}
#line 2394 "test_spec_parse.c"
    break;

  case 96: /* node_opt: T_COMMAND T_STRING  */
#line 802 "test_spec_parse.y"
        {
		/* replaces this node's own container command entirely, see
		 * test_spec.h's own commandOverride comment */
		strlcpy(current_node->commandOverride, (yyvsp[0].str),
		        sizeof(current_node->commandOverride));
		free((yyvsp[0].str));
	}
#line 2406 "test_spec_parse.c"
    break;

  case 97: /* node_opt: T_MONITOR_PASSWORD T_STRING  */
#line 810 "test_spec_parse.y"
        {
		strlcpy(current_node->monitorPassword, (yyvsp[0].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[0].str));
	}
#line 2416 "test_spec_parse.c"
    break;

  case 98: /* node_opt: T_VOLUME T_IDENT T_IDENT  */
#line 816 "test_spec_parse.y"
        {
		/* volume <name> <containerPath> — adds a named Docker volume */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, (yyvsp[-1].str),
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, (yyvsp[0].str),
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2434 "test_spec_parse.c"
    break;

  case 99: /* node_opt: T_VOLUME T_IDENT T_STRING  */
#line 830 "test_spec_parse.y"
        {
		/* volume <name> "/path/with spaces" */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, (yyvsp[-1].str),
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, (yyvsp[0].str),
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2452 "test_spec_parse.c"
    break;

  case 101: /* node_opt: T_DOCKER_INIT  */
#line 845 "test_spec_parse.y"
        {
		current_node->dockerInit = true;
	}
#line 2460 "test_spec_parse.c"
    break;

  case 102: /* alias_list: T_STRING  */
#line 858 "test_spec_parse.y"
        {
		if (current_node->aliasCount < PGAF_MAX_NODE_ALIASES)
		{
			strlcpy(current_node->aliases[current_node->aliasCount++], (yyvsp[0].str),
			        sizeof(current_node->aliases[0]));
		}
		free((yyvsp[0].str));
	}
#line 2473 "test_spec_parse.c"
    break;

  case 103: /* alias_list: alias_list T_COMMA T_STRING  */
#line 867 "test_spec_parse.y"
        {
		if (current_node->aliasCount < PGAF_MAX_NODE_ALIASES)
		{
			strlcpy(current_node->aliases[current_node->aliasCount++], (yyvsp[0].str),
			        sizeof(current_node->aliases[0]));
		}
		free((yyvsp[0].str));
	}
#line 2486 "test_spec_parse.c"
    break;

  case 104: /* setup_block: T_SETUP cmd_block  */
#line 883 "test_spec_parse.y"
        {
		current_spec->setup = (yyvsp[0].step);
	}
#line 2494 "test_spec_parse.c"
    break;

  case 105: /* teardown_block: T_TEARDOWN cmd_block  */
#line 890 "test_spec_parse.y"
        {
		current_spec->teardown = (yyvsp[0].step);
	}
#line 2502 "test_spec_parse.c"
    break;

  case 106: /* named_step: T_STEP ident_or_string cmd_block  */
#line 901 "test_spec_parse.y"
        {
		TestStep *s = (yyvsp[0].step);
		strncpy(s->name, (yyvsp[-1].str), sizeof(s->name) - 1);
		free((yyvsp[-1].str));
		register_step(current_spec, s);
	}
#line 2513 "test_spec_parse.c"
    break;

  case 107: /* cmd_block: T_LBRACE cmd_list T_RBRACE  */
#line 919 "test_spec_parse.y"
        {
		/* post-process: CMD_SQL immediately before CMD_EXPECT_ERROR */
		for (TestCmd *c = (yyvsp[-1].step)->commands; c; c = c->next)
		{
			if (c->kind == CMD_SQL && c->next &&
			    c->next->kind == CMD_EXPECT_ERROR)
				c->allowError = true;
		}
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2528 "test_spec_parse.c"
    break;

  case 108: /* cmd_list: %empty  */
#line 933 "test_spec_parse.y"
        {
		(yyval.step) = make_step("");
	}
#line 2536 "test_spec_parse.c"
    break;

  case 109: /* cmd_list: cmd_list step_cmd  */
#line 937 "test_spec_parse.y"
        {
		if ((yyvsp[0].cmd)) append_cmd((yyvsp[-1].step), (yyvsp[0].cmd));
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2545 "test_spec_parse.c"
    break;

  case 110: /* step_cmd: exec_cmd  */
#line 944 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2551 "test_spec_parse.c"
    break;

  case 111: /* step_cmd: wait_cmd  */
#line 945 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2557 "test_spec_parse.c"
    break;

  case 112: /* step_cmd: assert_cmd  */
#line 946 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2563 "test_spec_parse.c"
    break;

  case 113: /* step_cmd: sql_cmd  */
#line 947 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2569 "test_spec_parse.c"
    break;

  case 114: /* step_cmd: expect_cmd  */
#line 948 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2575 "test_spec_parse.c"
    break;

  case 115: /* step_cmd: promote_cmd  */
#line 949 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2581 "test_spec_parse.c"
    break;

  case 116: /* step_cmd: perform_cmd  */
#line 950 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2587 "test_spec_parse.c"
    break;

  case 117: /* step_cmd: network_cmd  */
#line 951 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2593 "test_spec_parse.c"
    break;

  case 118: /* step_cmd: sleep_cmd  */
#line 952 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2599 "test_spec_parse.c"
    break;

  case 119: /* step_cmd: compose_cmd  */
#line 953 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2605 "test_spec_parse.c"
    break;

  case 120: /* step_cmd: postgres_ctl_cmd  */
#line 954 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2611 "test_spec_parse.c"
    break;

  case 121: /* step_cmd: fsm_step_cmd  */
#line 955 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2617 "test_spec_parse.c"
    break;

  case 122: /* step_cmd: stays_while_cmd  */
#line 956 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2623 "test_spec_parse.c"
    break;

  case 123: /* step_cmd: set_monitor_cmd  */
#line 957 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2629 "test_spec_parse.c"
    break;

  case 124: /* step_cmd: logs_cmd  */
#line 958 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2635 "test_spec_parse.c"
    break;

  case 125: /* step_cmd: nodeini_cmd  */
#line 959 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2641 "test_spec_parse.c"
    break;

  case 126: /* exec_cmd: T_EXEC T_IDENT T_SHELL_ARGS  */
#line 974 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2652 "test_spec_parse.c"
    break;

  case 127: /* exec_cmd: T_EXEC T_IDENT  */
#line 981 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2662 "test_spec_parse.c"
    break;

  case 128: /* exec_cmd: T_EXEC_FAILS T_IDENT T_SHELL_ARGS  */
#line 987 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2673 "test_spec_parse.c"
    break;

  case 129: /* exec_cmd: T_EXEC_FAILS T_IDENT  */
#line 994 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2683 "test_spec_parse.c"
    break;

  case 130: /* exec_cmd: T_RUN T_IDENT T_SHELL_ARGS  */
#line 1000 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2694 "test_spec_parse.c"
    break;

  case 131: /* exec_cmd: T_RUN T_IDENT  */
#line 1007 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2704 "test_spec_parse.c"
    break;

  case 132: /* exec_cmd: T_PG_AUTOCTL T_IDENT T_SHELL_ARGS  */
#line 1013 "test_spec_parse.y"
        {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2716 "test_spec_parse.c"
    break;

  case 133: /* exec_cmd: T_PG_AUTOCTL T_IDENT  */
#line 1021 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[0].str));
	}
#line 2726 "test_spec_parse.c"
    break;

  case 134: /* exec_cmd: T_PG_AUTOCTL  */
#line 1027 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	}
#line 2734 "test_spec_parse.c"
    break;

  case 137: /* wait_multi_condition: T_IDENT T_STATE state_op fsm_state  */
#line 1065 "test_spec_parse.y"
        {
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  (yyvsp[-3].str),
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], (yyvsp[0].str),
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free((yyvsp[-3].str));
	}
#line 2753 "test_spec_parse.c"
    break;

  case 138: /* wait_multi_condition: T_IDENT T_STATE state_op T_IDENT  */
#line 1080 "test_spec_parse.y"
        {
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  (yyvsp[-3].str),
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], (yyvsp[0].str),
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 2772 "test_spec_parse.c"
    break;

  case 143: /* pass_state_list: fsm_state  */
#line 1120 "test_spec_parse.y"
        {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2784 "test_spec_parse.c"
    break;

  case 144: /* pass_state_list: T_IDENT  */
#line 1128 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2796 "test_spec_parse.c"
    break;

  case 145: /* pass_state_list: pass_state_list T_COMMA fsm_state  */
#line 1136 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2807 "test_spec_parse.c"
    break;

  case 146: /* pass_state_list: pass_state_list T_COMMA T_IDENT  */
#line 1143 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2819 "test_spec_parse.c"
    break;

  case 147: /* $@7: %empty  */
#line 1154 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); }
#line 2828 "test_spec_parse.c"
    break;

  case 148: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state $@7 opt_passing_through opt_timeout  */
#line 1159 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2838 "test_spec_parse.c"
    break;

  case 149: /* $@8: %empty  */
#line 1165 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); free((yyvsp[0].str)); }
#line 2847 "test_spec_parse.c"
    break;

  case 150: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT $@8 opt_passing_through opt_timeout  */
#line 1170 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2857 "test_spec_parse.c"
    break;

  case 151: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1176 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2870 "test_spec_parse.c"
    break;

  case 152: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1185 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2883 "test_spec_parse.c"
    break;

  case 153: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout  */
#line 1194 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-2].str));
	}
#line 2894 "test_spec_parse.c"
    break;

  case 154: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_REPLAYS T_IDENT opt_timeout  */
#line 1208 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2906 "test_spec_parse.c"
    break;

  case 155: /* wait_cmd: T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout  */
#line 1216 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2916 "test_spec_parse.c"
    break;

  case 156: /* wait_cmd: T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout  */
#line 1230 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2926 "test_spec_parse.c"
    break;

  case 157: /* state_name_list: fsm_state  */
#line 1245 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2936 "test_spec_parse.c"
    break;

  case 158: /* state_name_list: T_IDENT  */
#line 1251 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2947 "test_spec_parse.c"
    break;

  case 159: /* state_name_list: state_name_list T_COMMA fsm_state  */
#line 1258 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2957 "test_spec_parse.c"
    break;

  case 160: /* state_name_list: state_name_list T_COMMA T_IDENT  */
#line 1264 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2968 "test_spec_parse.c"
    break;

  case 163: /* group_items: T_GROUP T_INTEGER  */
#line 1283 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2977 "test_spec_parse.c"
    break;

  case 164: /* group_items: group_items T_COMMA T_GROUP T_INTEGER  */
#line 1288 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2986 "test_spec_parse.c"
    break;

  case 165: /* opt_timeout: %empty  */
#line 1295 "test_spec_parse.y"
                                       { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; }
#line 2992 "test_spec_parse.c"
    break;

  case 166: /* opt_timeout: T_TIMEOUT T_INTEGER  */
#line 1296 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 2998 "test_spec_parse.c"
    break;

  case 167: /* opt_timeout: T_WITH T_TIMEOUT T_INTEGER  */
#line 1297 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 3004 "test_spec_parse.c"
    break;

  case 168: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout  */
#line 1309 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3016 "test_spec_parse.c"
    break;

  case 169: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout  */
#line 1317 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3028 "test_spec_parse.c"
    break;

  case 170: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1325 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3040 "test_spec_parse.c"
    break;

  case 171: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1333 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3052 "test_spec_parse.c"
    break;

  case 172: /* sql_cmd: T_SQL T_IDENT T_BLOCK  */
#line 1351 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3063 "test_spec_parse.c"
    break;

  case 173: /* expect_cmd: T_EXPECT T_BLOCK  */
#line 1366 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[0].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[0].str));
	}
#line 3074 "test_spec_parse.c"
    break;

  case 174: /* expect_cmd: T_EXPECT T_ERROR  */
#line 1373 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	}
#line 3082 "test_spec_parse.c"
    break;

  case 175: /* expect_cmd: T_EXPECT T_ERROR T_IDENT  */
#line 1377 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[0].str), sizeof((yyval.cmd)->state));
		free((yyvsp[0].str));
	}
#line 3092 "test_spec_parse.c"
    break;

  case 176: /* expect_cmd: T_EXPECT T_ERROR T_INTEGER  */
#line 1383 "test_spec_parse.y"
        {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[0].ival));
	}
#line 3102 "test_spec_parse.c"
    break;

  case 177: /* promote_cmd: T_PROMOTE promote_list  */
#line 1396 "test_spec_parse.y"
        {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	}
#line 3111 "test_spec_parse.c"
    break;

  case 178: /* promote_list: T_IDENT  */
#line 1404 "test_spec_parse.y"
        {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3123 "test_spec_parse.c"
    break;

  case 179: /* promote_list: promote_list T_COMMA T_IDENT  */
#line 1412 "test_spec_parse.y"
        {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3134 "test_spec_parse.c"
    break;

  case 180: /* perform_cmd: T_PERFORM T_FAILOVER  */
#line 1433 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3145 "test_spec_parse.c"
    break;

  case 181: /* perform_cmd: T_PERFORM T_FAILOVER T_GROUP T_INTEGER  */
#line 1440 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3156 "test_spec_parse.c"
    break;

  case 182: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT  */
#line 1447 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[0].str));
	}
#line 3168 "test_spec_parse.c"
    break;

  case 183: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT T_GROUP T_INTEGER  */
#line 1455 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[-2].str));
	}
#line 3180 "test_spec_parse.c"
    break;

  case 184: /* network_cmd: T_NETWORK T_DISCONNECT T_IDENT  */
#line 1471 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3190 "test_spec_parse.c"
    break;

  case 185: /* network_cmd: T_NETWORK T_CONNECT T_IDENT  */
#line 1477 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3200 "test_spec_parse.c"
    break;

  case 186: /* nodeini_cmd: T_NODEINI T_SET T_IDENT T_IDENT T_IDENT  */
#line 1498 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3212 "test_spec_parse.c"
    break;

  case 187: /* nodeini_cmd: T_NODEINI T_GET T_IDENT T_IDENT T_IDENT  */
#line 1506 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3224 "test_spec_parse.c"
    break;

  case 188: /* sleep_cmd: T_SLEEP T_INTEGER  */
#line 1521 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
	}
#line 3233 "test_spec_parse.c"
    break;

  case 189: /* compose_cmd: T_COMPOSE T_DOWN  */
#line 1535 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	}
#line 3241 "test_spec_parse.c"
    break;

  case 190: /* compose_cmd: T_COMPOSE T_START T_IDENT  */
#line 1539 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3251 "test_spec_parse.c"
    break;

  case 191: /* compose_cmd: T_COMPOSE T_STOP T_IDENT  */
#line 1545 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3261 "test_spec_parse.c"
    break;

  case 192: /* compose_cmd: T_COMPOSE T_KILL T_IDENT  */
#line 1551 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3271 "test_spec_parse.c"
    break;

  case 193: /* compose_cmd: T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS  */
#line 1577 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_INJECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[-1].str), sizeof((yyval.cmd)->expected));  /* image */

		/* Split T_SHELL_ARGS: "<src-path> <svc>:<dst-path>" */
		char tmp[4096];
		strlcpy(tmp, (yyvsp[0].str), sizeof(tmp));
		char *src = tmp;
		char *p = tmp;
		while (*p && *p != ' ' && *p != '\t') p++;
		if (*p) { *p++ = '\0'; while (*p == ' ' || *p == '\t') p++; }
		char *svcdst = p;
		char *colon  = (*svcdst) ? strchr(svcdst, ':') : NULL;
		strlcpy((yyval.cmd)->args, src, sizeof((yyval.cmd)->args));
		if (colon)
		{
			*colon = '\0';
			strlcpy((yyval.cmd)->service, svcdst,   sizeof((yyval.cmd)->service)); /* dst svc  */
			strlcpy((yyval.cmd)->state,   colon + 1, sizeof((yyval.cmd)->state));  /* dst path */
		}
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3298 "test_spec_parse.c"
    break;

  case 194: /* postgres_ctl_cmd: T_STOP T_POSTGRES node_name  */
#line 1611 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3308 "test_spec_parse.c"
    break;

  case 195: /* postgres_ctl_cmd: T_START T_POSTGRES node_name  */
#line 1617 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3318 "test_spec_parse.c"
    break;

  case 196: /* fsm_step_cmd: T_FSM T_STEP node_name  */
#line 1638 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3328 "test_spec_parse.c"
    break;

  case 197: /* $@9: %empty  */
#line 1654 "test_spec_parse.y"
                { pgaf_next_brace_is_while = 1; }
#line 3334 "test_spec_parse.c"
    break;

  case 198: /* while_body: T_WHILE $@9 T_LBRACE cmd_list T_RBRACE  */
#line 1655 "test_spec_parse.y"
        { (yyval.step) = (yyvsp[-1].step); }
#line 3340 "test_spec_parse.c"
    break;

  case 199: /* stays_while_cmd: T_ASSERT node_name T_STAYS fsm_state while_body  */
#line 1660 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[0].step)) ? (yyvsp[0].step)->commands : NULL;
		free((yyvsp[-3].str));
	}
#line 3352 "test_spec_parse.c"
    break;

  case 200: /* set_monitor_cmd: T_SET T_IDENT T_IDENT  */
#line 1679 "test_spec_parse.y"
        {
		/* only "set monitor <svc>" is supported; $2 must be "monitor" */
		if (strcmp((yyvsp[-1].str), "monitor") != 0)
		{
			fprintf(stderr, "pgaftest: unknown 'set' target '%s' (expected 'monitor')\n", (yyvsp[-1].str));
			free((yyvsp[-1].str)); free((yyvsp[0].str));
			YYERROR;
		}
		(yyval.cmd) = make_cmd(CMD_SET_MONITOR);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3369 "test_spec_parse.c"
    break;

  case 201: /* logs_cmd: T_LOGS T_IDENT T_CONTAINS T_STRING  */
#line 1704 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3382 "test_spec_parse.c"
    break;

  case 202: /* logs_cmd: T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING  */
#line 1713 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3395 "test_spec_parse.c"
    break;

  case 203: /* logs_cmd: T_LOGS T_IDENT T_MATCHES T_STRING  */
#line 1722 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3408 "test_spec_parse.c"
    break;

  case 204: /* logs_cmd: T_LOGS T_IDENT T_NOT T_MATCHES T_STRING  */
#line 1731 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3421 "test_spec_parse.c"
    break;

  case 207: /* sequence_names: sequence_names ident_or_string  */
#line 1752 "test_spec_parse.y"
        {
		int i = current_spec->sequenceLength;
		if (i < PGAF_MAX_SEQ)
			current_spec->sequence[current_spec->sequenceLength++] = (yyvsp[0].str);
		else
		{
			fprintf(stderr, "pgaftest: too many steps in sequence (max %d)\n",
			        PGAF_MAX_SEQ);
			exit(1);
		}
	}
#line 3437 "test_spec_parse.c"
    break;

  case 208: /* fsm_state: T_FS_INIT  */
#line 1773 "test_spec_parse.y"
                                   { (yyval.str) = "init"; }
#line 3443 "test_spec_parse.c"
    break;

  case 209: /* fsm_state: T_FS_SINGLE  */
#line 1774 "test_spec_parse.y"
                                   { (yyval.str) = "single"; }
#line 3449 "test_spec_parse.c"
    break;

  case 210: /* fsm_state: T_FS_PRIMARY  */
#line 1775 "test_spec_parse.y"
                                   { (yyval.str) = "primary"; }
#line 3455 "test_spec_parse.c"
    break;

  case 211: /* fsm_state: T_FS_WAIT_PRIMARY  */
#line 1776 "test_spec_parse.y"
                                   { (yyval.str) = "wait_primary"; }
#line 3461 "test_spec_parse.c"
    break;

  case 212: /* fsm_state: T_FS_WAIT_STANDBY  */
#line 1777 "test_spec_parse.y"
                                   { (yyval.str) = "wait_standby"; }
#line 3467 "test_spec_parse.c"
    break;

  case 213: /* fsm_state: T_FS_DEMOTED  */
#line 1778 "test_spec_parse.y"
                                   { (yyval.str) = "demoted"; }
#line 3473 "test_spec_parse.c"
    break;

  case 214: /* fsm_state: T_FS_DEMOTE_TIMEOUT  */
#line 1779 "test_spec_parse.y"
                                   { (yyval.str) = "demote_timeout"; }
#line 3479 "test_spec_parse.c"
    break;

  case 215: /* fsm_state: T_FS_DRAINING  */
#line 1780 "test_spec_parse.y"
                                   { (yyval.str) = "draining"; }
#line 3485 "test_spec_parse.c"
    break;

  case 216: /* fsm_state: T_FS_SECONDARY  */
#line 1781 "test_spec_parse.y"
                                   { (yyval.str) = "secondary"; }
#line 3491 "test_spec_parse.c"
    break;

  case 217: /* fsm_state: T_FS_CATCHINGUP  */
#line 1782 "test_spec_parse.y"
                                   { (yyval.str) = "catchingup"; }
#line 3497 "test_spec_parse.c"
    break;

  case 218: /* fsm_state: T_FS_PREP_PROMOTION  */
#line 1783 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_promotion"; }
#line 3503 "test_spec_parse.c"
    break;

  case 219: /* fsm_state: T_FS_STOP_REPLICATION  */
#line 1784 "test_spec_parse.y"
                                   { (yyval.str) = "stop_replication"; }
#line 3509 "test_spec_parse.c"
    break;

  case 220: /* fsm_state: T_FS_MAINTENANCE  */
#line 1785 "test_spec_parse.y"
                                   { (yyval.str) = "maintenance"; }
#line 3515 "test_spec_parse.c"
    break;

  case 221: /* fsm_state: T_FS_JOIN_PRIMARY  */
#line 1786 "test_spec_parse.y"
                                   { (yyval.str) = "join_primary"; }
#line 3521 "test_spec_parse.c"
    break;

  case 222: /* fsm_state: T_FS_APPLY_SETTINGS  */
#line 1787 "test_spec_parse.y"
                                   { (yyval.str) = "apply_settings"; }
#line 3527 "test_spec_parse.c"
    break;

  case 223: /* fsm_state: T_FS_PREPARE_MAINTENANCE  */
#line 1788 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_maintenance"; }
#line 3533 "test_spec_parse.c"
    break;

  case 224: /* fsm_state: T_FS_WAIT_MAINTENANCE  */
#line 1789 "test_spec_parse.y"
                                   { (yyval.str) = "wait_maintenance"; }
#line 3539 "test_spec_parse.c"
    break;

  case 225: /* fsm_state: T_FS_REPORT_LSN  */
#line 1790 "test_spec_parse.y"
                                   { (yyval.str) = "report_lsn"; }
#line 3545 "test_spec_parse.c"
    break;

  case 226: /* fsm_state: T_FS_FAST_FORWARD  */
#line 1791 "test_spec_parse.y"
                                   { (yyval.str) = "fast_forward"; }
#line 3551 "test_spec_parse.c"
    break;

  case 227: /* fsm_state: T_FS_JOIN_SECONDARY  */
#line 1792 "test_spec_parse.y"
                                   { (yyval.str) = "join_secondary"; }
#line 3557 "test_spec_parse.c"
    break;

  case 228: /* fsm_state: T_FS_DROPPED  */
#line 1793 "test_spec_parse.y"
                                   { (yyval.str) = "dropped"; }
#line 3563 "test_spec_parse.c"
    break;

  case 229: /* ident_or_string: T_IDENT  */
#line 1801 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3569 "test_spec_parse.c"
    break;

  case 230: /* ident_or_string: T_STRING  */
#line 1802 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3575 "test_spec_parse.c"
    break;


#line 3579 "test_spec_parse.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 1805 "test_spec_parse.y"


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

	/*
	 * If the file has no explicit sequence{} block, default to running
	 * steps in declaration order.  Populated here (not just in the CI
	 * `pgaftest run` path) so every caller that reads spec->sequence --
	 * `pgaftest step`, `pgaftest show steps`, `pgaftest indent`, and
	 * `pgaftest run` alike -- sees the same default instead of an empty
	 * sequence.
	 */
	if (spec->sequenceLength == 0)
	{
		for (TestStep *s = spec->steps; s; s = s->next)
		{
			if (spec->sequenceLength < PGAF_MAX_SEQ)
			{
				spec->sequence[spec->sequenceLength++] = s->name;
			}
		}
	}

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
