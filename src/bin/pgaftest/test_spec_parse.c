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

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

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
static TestArchiverNode *current_archiver = NULL;


#line 218 "test_spec_parse.c"

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
  YYSYMBOL_T_ARCHIVER = 22,                /* T_ARCHIVER  */
  YYSYMBOL_T_ASYNC = 23,                   /* T_ASYNC  */
  YYSYMBOL_T_NO_MONITOR = 24,              /* T_NO_MONITOR  */
  YYSYMBOL_T_SUSPENDED = 25,               /* T_SUSPENDED  */
  YYSYMBOL_T_LAUNCH = 26,                  /* T_LAUNCH  */
  YYSYMBOL_T_CREATE = 27,                  /* T_CREATE  */
  YYSYMBOL_T_DEFERRED = 28,                /* T_DEFERRED  */
  YYSYMBOL_T_IMMEDIATE = 29,               /* T_IMMEDIATE  */
  YYSYMBOL_T_FALSE = 30,                   /* T_FALSE  */
  YYSYMBOL_T_TRUE = 31,                    /* T_TRUE  */
  YYSYMBOL_T_INITIALLY = 32,               /* T_INITIALLY  */
  YYSYMBOL_T_VOLUME = 33,                  /* T_VOLUME  */
  YYSYMBOL_T_LISTEN = 34,                  /* T_LISTEN  */
  YYSYMBOL_T_CITUS_SECONDARY = 35,         /* T_CITUS_SECONDARY  */
  YYSYMBOL_T_CANDIDATE_PRIORITY = 36,      /* T_CANDIDATE_PRIORITY  */
  YYSYMBOL_T_PORT = 37,                    /* T_PORT  */
  YYSYMBOL_T_PASSWORD = 38,                /* T_PASSWORD  */
  YYSYMBOL_T_MONITOR_PASSWORD = 39,        /* T_MONITOR_PASSWORD  */
  YYSYMBOL_T_CITUS_CLUSTER_NAME = 40,      /* T_CITUS_CLUSTER_NAME  */
  YYSYMBOL_T_DEBIAN_CLUSTER = 41,          /* T_DEBIAN_CLUSTER  */
  YYSYMBOL_T_REPLICATION_QUORUM = 42,      /* T_REPLICATION_QUORUM  */
  YYSYMBOL_T_REPLICATION_PASSWORD = 43,    /* T_REPLICATION_PASSWORD  */
  YYSYMBOL_T_EXTENSION_VERSION = 44,       /* T_EXTENSION_VERSION  */
  YYSYMBOL_T_BIND_SOURCE = 45,             /* T_BIND_SOURCE  */
  YYSYMBOL_T_LEGACY_STARTUP = 46,          /* T_LEGACY_STARTUP  */
  YYSYMBOL_T_REGION = 47,                  /* T_REGION  */
  YYSYMBOL_T_NODEINI = 48,                 /* T_NODEINI  */
  YYSYMBOL_T_FS_INIT = 49,                 /* T_FS_INIT  */
  YYSYMBOL_T_FS_SINGLE = 50,               /* T_FS_SINGLE  */
  YYSYMBOL_T_FS_PRIMARY = 51,              /* T_FS_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_PRIMARY = 52,         /* T_FS_WAIT_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_STANDBY = 53,         /* T_FS_WAIT_STANDBY  */
  YYSYMBOL_T_FS_DEMOTED = 54,              /* T_FS_DEMOTED  */
  YYSYMBOL_T_FS_DEMOTE_TIMEOUT = 55,       /* T_FS_DEMOTE_TIMEOUT  */
  YYSYMBOL_T_FS_DRAINING = 56,             /* T_FS_DRAINING  */
  YYSYMBOL_T_FS_SECONDARY = 57,            /* T_FS_SECONDARY  */
  YYSYMBOL_T_FS_CATCHINGUP = 58,           /* T_FS_CATCHINGUP  */
  YYSYMBOL_T_FS_PREP_PROMOTION = 59,       /* T_FS_PREP_PROMOTION  */
  YYSYMBOL_T_FS_STOP_REPLICATION = 60,     /* T_FS_STOP_REPLICATION  */
  YYSYMBOL_T_FS_MAINTENANCE = 61,          /* T_FS_MAINTENANCE  */
  YYSYMBOL_T_FS_JOIN_PRIMARY = 62,         /* T_FS_JOIN_PRIMARY  */
  YYSYMBOL_T_FS_APPLY_SETTINGS = 63,       /* T_FS_APPLY_SETTINGS  */
  YYSYMBOL_T_FS_PREPARE_MAINTENANCE = 64,  /* T_FS_PREPARE_MAINTENANCE  */
  YYSYMBOL_T_FS_WAIT_MAINTENANCE = 65,     /* T_FS_WAIT_MAINTENANCE  */
  YYSYMBOL_T_FS_REPORT_LSN = 66,           /* T_FS_REPORT_LSN  */
  YYSYMBOL_T_FS_FAST_FORWARD = 67,         /* T_FS_FAST_FORWARD  */
  YYSYMBOL_T_FS_JOIN_SECONDARY = 68,       /* T_FS_JOIN_SECONDARY  */
  YYSYMBOL_T_FS_DROPPED = 69,              /* T_FS_DROPPED  */
  YYSYMBOL_T_EXEC = 70,                    /* T_EXEC  */
  YYSYMBOL_T_EXEC_FAILS = 71,              /* T_EXEC_FAILS  */
  YYSYMBOL_T_RUN = 72,                     /* T_RUN  */
  YYSYMBOL_T_PG_AUTOCTL = 73,              /* T_PG_AUTOCTL  */
  YYSYMBOL_T_WAIT = 74,                    /* T_WAIT  */
  YYSYMBOL_T_UNTIL = 75,                   /* T_UNTIL  */
  YYSYMBOL_T_TIMEOUT = 76,                 /* T_TIMEOUT  */
  YYSYMBOL_T_AND = 77,                     /* T_AND  */
  YYSYMBOL_T_IS = 78,                      /* T_IS  */
  YYSYMBOL_T_WITH = 79,                    /* T_WITH  */
  YYSYMBOL_T_REPLAYS = 80,                 /* T_REPLAYS  */
  YYSYMBOL_T_ASSERT = 81,                  /* T_ASSERT  */
  YYSYMBOL_T_SQL = 82,                     /* T_SQL  */
  YYSYMBOL_T_EXPECT = 83,                  /* T_EXPECT  */
  YYSYMBOL_T_ERROR = 84,                   /* T_ERROR  */
  YYSYMBOL_T_LET = 85,                     /* T_LET  */
  YYSYMBOL_T_PROMOTE = 86,                 /* T_PROMOTE  */
  YYSYMBOL_T_PERFORM = 87,                 /* T_PERFORM  */
  YYSYMBOL_T_FAILOVER = 88,                /* T_FAILOVER  */
  YYSYMBOL_T_NETWORK = 89,                 /* T_NETWORK  */
  YYSYMBOL_T_DISCONNECT = 90,              /* T_DISCONNECT  */
  YYSYMBOL_T_CONNECT = 91,                 /* T_CONNECT  */
  YYSYMBOL_T_SLEEP = 92,                   /* T_SLEEP  */
  YYSYMBOL_T_COMPOSE = 93,                 /* T_COMPOSE  */
  YYSYMBOL_T_DOWN = 94,                    /* T_DOWN  */
  YYSYMBOL_T_START = 95,                   /* T_START  */
  YYSYMBOL_T_STOP = 96,                    /* T_STOP  */
  YYSYMBOL_T_STOPPED = 97,                 /* T_STOPPED  */
  YYSYMBOL_T_KILL = 98,                    /* T_KILL  */
  YYSYMBOL_T_INJECT = 99,                  /* T_INJECT  */
  YYSYMBOL_T_STATE = 100,                  /* T_STATE  */
  YYSYMBOL_T_ASSIGNED_STATE = 101,         /* T_ASSIGNED_STATE  */
  YYSYMBOL_T_IN = 102,                     /* T_IN  */
  YYSYMBOL_T_GROUP = 103,                  /* T_GROUP  */
  YYSYMBOL_T_LBRACE = 104,                 /* T_LBRACE  */
  YYSYMBOL_T_RBRACE = 105,                 /* T_RBRACE  */
  YYSYMBOL_T_COMMA = 106,                  /* T_COMMA  */
  YYSYMBOL_T_POSTGRES = 107,               /* T_POSTGRES  */
  YYSYMBOL_T_STAYS = 108,                  /* T_STAYS  */
  YYSYMBOL_T_WHILE = 109,                  /* T_WHILE  */
  YYSYMBOL_T_THROUGH = 110,                /* T_THROUGH  */
  YYSYMBOL_T_SET = 111,                    /* T_SET  */
  YYSYMBOL_T_GET = 112,                    /* T_GET  */
  YYSYMBOL_T_FSM = 113,                    /* T_FSM  */
  YYSYMBOL_T_LOGS = 114,                   /* T_LOGS  */
  YYSYMBOL_T_NOT = 115,                    /* T_NOT  */
  YYSYMBOL_T_CONTAINS = 116,               /* T_CONTAINS  */
  YYSYMBOL_T_MATCHES = 117,                /* T_MATCHES  */
  YYSYMBOL_T_WAL = 118,                    /* T_WAL  */
  YYSYMBOL_T_SEGMENT = 119,                /* T_SEGMENT  */
  YYSYMBOL_T_ARCHIVED = 120,               /* T_ARCHIVED  */
  YYSYMBOL_T_BASEBACKUP = 121,             /* T_BASEBACKUP  */
  YYSYMBOL_T_SLASH = 122,                  /* T_SLASH  */
  YYSYMBOL_T_INTEGER = 123,                /* T_INTEGER  */
  YYSYMBOL_T_IDENT = 124,                  /* T_IDENT  */
  YYSYMBOL_T_STRING = 125,                 /* T_STRING  */
  YYSYMBOL_T_BLOCK = 126,                  /* T_BLOCK  */
  YYSYMBOL_T_SHELL_ARGS = 127,             /* T_SHELL_ARGS  */
  YYSYMBOL_YYACCEPT = 128,                 /* $accept  */
  YYSYMBOL_spec = 129,                     /* spec  */
  YYSYMBOL_spec_item = 130,                /* spec_item  */
  YYSYMBOL_cluster_block = 131,            /* cluster_block  */
  YYSYMBOL_132_1 = 132,                    /* $@1  */
  YYSYMBOL_cluster_item_list = 133,        /* cluster_item_list  */
  YYSYMBOL_cluster_item = 134,             /* cluster_item  */
  YYSYMBOL_archiver_block = 135,           /* archiver_block  */
  YYSYMBOL_136_2 = 136,                    /* $@2  */
  YYSYMBOL_archiver_opt_list = 137,        /* archiver_opt_list  */
  YYSYMBOL_archiver_opt = 138,             /* archiver_opt  */
  YYSYMBOL_monitor_line = 139,             /* monitor_line  */
  YYSYMBOL_image_line = 140,               /* image_line  */
  YYSYMBOL_extension_version_line = 141,   /* extension_version_line  */
  YYSYMBOL_ssl_line = 142,                 /* ssl_line  */
  YYSYMBOL_auth_line = 143,                /* auth_line  */
  YYSYMBOL_formation_block = 144,          /* formation_block  */
  YYSYMBOL_145_3 = 145,                    /* $@3  */
  YYSYMBOL_formation_opt_list = 146,       /* formation_opt_list  */
  YYSYMBOL_bare_name = 147,                /* bare_name  */
  YYSYMBOL_formation_opt = 148,            /* formation_opt  */
  YYSYMBOL_node_list = 149,                /* node_list  */
  YYSYMBOL_node_name = 150,                /* node_name  */
  YYSYMBOL_init_node_slot = 151,           /* init_node_slot  */
  YYSYMBOL_node_line = 152,                /* node_line  */
  YYSYMBOL_153_4 = 153,                    /* $@4  */
  YYSYMBOL_154_5 = 154,                    /* $@5  */
  YYSYMBOL_node_opt_list = 155,            /* node_opt_list  */
  YYSYMBOL_node_opt = 156,                 /* node_opt  */
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
  YYSYMBOL_170_6 = 170,                    /* $@6  */
  YYSYMBOL_171_7 = 171,                    /* $@7  */
  YYSYMBOL_state_name_list = 172,          /* state_name_list  */
  YYSYMBOL_opt_in_group = 173,             /* opt_in_group  */
  YYSYMBOL_group_items = 174,              /* group_items  */
  YYSYMBOL_opt_timeout = 175,              /* opt_timeout  */
  YYSYMBOL_assert_cmd = 176,               /* assert_cmd  */
  YYSYMBOL_sql_cmd = 177,                  /* sql_cmd  */
  YYSYMBOL_let_cmd = 178,                  /* let_cmd  */
  YYSYMBOL_expect_cmd = 179,               /* expect_cmd  */
  YYSYMBOL_promote_cmd = 180,              /* promote_cmd  */
  YYSYMBOL_promote_list = 181,             /* promote_list  */
  YYSYMBOL_perform_cmd = 182,              /* perform_cmd  */
  YYSYMBOL_network_cmd = 183,              /* network_cmd  */
  YYSYMBOL_nodeini_cmd = 184,              /* nodeini_cmd  */
  YYSYMBOL_sleep_cmd = 185,                /* sleep_cmd  */
  YYSYMBOL_compose_cmd = 186,              /* compose_cmd  */
  YYSYMBOL_postgres_ctl_cmd = 187,         /* postgres_ctl_cmd  */
  YYSYMBOL_fsm_step_cmd = 188,             /* fsm_step_cmd  */
  YYSYMBOL_while_body = 189,               /* while_body  */
  YYSYMBOL_190_8 = 190,                    /* $@8  */
  YYSYMBOL_stays_while_cmd = 191,          /* stays_while_cmd  */
  YYSYMBOL_set_monitor_cmd = 192,          /* set_monitor_cmd  */
  YYSYMBOL_logs_cmd = 193,                 /* logs_cmd  */
  YYSYMBOL_sequence_block = 194,           /* sequence_block  */
  YYSYMBOL_sequence_names = 195,           /* sequence_names  */
  YYSYMBOL_fsm_state = 196,                /* fsm_state  */
  YYSYMBOL_ident_or_string = 197,          /* ident_or_string  */
  YYSYMBOL_wait_state_name = 198,          /* wait_state_name  */
  YYSYMBOL_opt_wait_group = 199            /* opt_wait_group  */
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
#define YYLAST   686

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  128
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  72
/* YYNRULES -- Number of rules.  */
#define YYNRULES  237
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  421

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   382


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
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   222,   222,   223,   227,   228,   229,   230,   231,   244,
     243,   253,   255,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   290,   289,   307,   309,   313,   327,   332,   337,
     343,   350,   354,   370,   374,   381,   388,   394,   401,   408,
     415,   428,   434,   444,   450,   460,   470,   476,   487,   486,
     503,   505,   514,   515,   516,   517,   518,   522,   527,   531,
     537,   539,   558,   559,   568,   585,   584,   592,   591,   599,
     601,   605,   610,   615,   619,   623,   627,   631,   637,   642,
     646,   651,   655,   659,   663,   667,   671,   676,   681,   685,
     689,   695,   701,   706,   711,   716,   720,   724,   730,   736,
     750,   771,   778,   789,   807,   822,   825,   833,   834,   835,
     836,   837,   838,   839,   840,   841,   842,   843,   844,   845,
     846,   847,   848,   849,   863,   870,   876,   883,   889,   896,
     902,   910,   916,   943,   943,   954,   969,   987,   988,  1003,
    1005,  1009,  1017,  1025,  1032,  1044,  1043,  1055,  1054,  1065,
    1074,  1083,  1097,  1105,  1119,  1134,  1151,  1175,  1204,  1234,
    1240,  1247,  1253,  1266,  1268,  1272,  1277,  1285,  1286,  1287,
    1298,  1306,  1314,  1322,  1340,  1358,  1374,  1381,  1385,  1391,
    1404,  1412,  1420,  1441,  1448,  1455,  1463,  1479,  1485,  1506,
    1514,  1529,  1543,  1547,  1553,  1559,  1585,  1619,  1625,  1646,
    1663,  1663,  1668,  1687,  1712,  1721,  1730,  1739,  1755,  1758,
    1760,  1782,  1783,  1784,  1785,  1786,  1787,  1788,  1789,  1790,
    1791,  1792,  1793,  1794,  1795,  1796,  1797,  1798,  1799,  1800,
    1801,  1802,  1810,  1811,  1822,  1823,  1831,  1832
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
  "T_NUM_SYNC", "T_COORDINATOR", "T_WORKER", "T_ARCHIVER", "T_ASYNC",
  "T_NO_MONITOR", "T_SUSPENDED", "T_LAUNCH", "T_CREATE", "T_DEFERRED",
  "T_IMMEDIATE", "T_FALSE", "T_TRUE", "T_INITIALLY", "T_VOLUME",
  "T_LISTEN", "T_CITUS_SECONDARY", "T_CANDIDATE_PRIORITY", "T_PORT",
  "T_PASSWORD", "T_MONITOR_PASSWORD", "T_CITUS_CLUSTER_NAME",
  "T_DEBIAN_CLUSTER", "T_REPLICATION_QUORUM", "T_REPLICATION_PASSWORD",
  "T_EXTENSION_VERSION", "T_BIND_SOURCE", "T_LEGACY_STARTUP", "T_REGION",
  "T_NODEINI", "T_FS_INIT", "T_FS_SINGLE", "T_FS_PRIMARY",
  "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
  "T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
  "T_FS_CATCHINGUP", "T_FS_PREP_PROMOTION", "T_FS_STOP_REPLICATION",
  "T_FS_MAINTENANCE", "T_FS_JOIN_PRIMARY", "T_FS_APPLY_SETTINGS",
  "T_FS_PREPARE_MAINTENANCE", "T_FS_WAIT_MAINTENANCE", "T_FS_REPORT_LSN",
  "T_FS_FAST_FORWARD", "T_FS_JOIN_SECONDARY", "T_FS_DROPPED", "T_EXEC",
  "T_EXEC_FAILS", "T_RUN", "T_PG_AUTOCTL", "T_WAIT", "T_UNTIL",
  "T_TIMEOUT", "T_AND", "T_IS", "T_WITH", "T_REPLAYS", "T_ASSERT", "T_SQL",
  "T_EXPECT", "T_ERROR", "T_LET", "T_PROMOTE", "T_PERFORM", "T_FAILOVER",
  "T_NETWORK", "T_DISCONNECT", "T_CONNECT", "T_SLEEP", "T_COMPOSE",
  "T_DOWN", "T_START", "T_STOP", "T_STOPPED", "T_KILL", "T_INJECT",
  "T_STATE", "T_ASSIGNED_STATE", "T_IN", "T_GROUP", "T_LBRACE", "T_RBRACE",
  "T_COMMA", "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH", "T_SET",
  "T_GET", "T_FSM", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_WAL",
  "T_SEGMENT", "T_ARCHIVED", "T_BASEBACKUP", "T_SLASH", "T_INTEGER",
  "T_IDENT", "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec",
  "spec_item", "cluster_block", "$@1", "cluster_item_list", "cluster_item",
  "archiver_block", "$@2", "archiver_opt_list", "archiver_opt",
  "monitor_line", "image_line", "extension_version_line", "ssl_line",
  "auth_line", "formation_block", "$@3", "formation_opt_list", "bare_name",
  "formation_opt", "node_list", "node_name", "init_node_slot", "node_line",
  "$@4", "$@5", "node_opt_list", "node_opt", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "$@6", "$@7", "state_name_list", "opt_in_group",
  "group_items", "opt_timeout", "assert_cmd", "sql_cmd", "let_cmd",
  "expect_cmd", "promote_cmd", "promote_list", "perform_cmd",
  "network_cmd", "nodeini_cmd", "sleep_cmd", "compose_cmd",
  "postgres_ctl_cmd", "fsm_step_cmd", "while_body", "$@8",
  "stays_while_cmd", "set_monitor_cmd", "logs_cmd", "sequence_block",
  "sequence_names", "fsm_state", "ident_or_string", "wait_state_name",
  "opt_wait_group", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-208)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-137)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      90,   -38,   -32,   -32,  -102,  -208,    86,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   -32,
    -102,  -208,  -208,  -208,   515,  -208,  -208,    11,   -36,   -45,
     -41,   -39,   -37,    40,     5,     4,   -66,    16,    21,    24,
       0,    28,    15,    46,    47,  -208,    33,   114,    34,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,    -3,   -22,    35,
      36,    38,  -208,    39,   -18,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,    42,    43,    65,    66,
      67,    68,   122,  -208,    17,    56,    70,    -4,  -208,   153,
    -208,    62,    20,    45,    74,  -208,  -208,    75,    79,    91,
      92,     9,     9,    93,     9,   -85,    99,   106,   105,   107,
      37,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
     109,   111,  -208,  -208,  -208,  -208,   134,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   113,   117,
     115,   -61,   161,   -58,  -208,     2,     2,   617,  -208,  -208,
    -208,   159,   118,   226,   127,  -208,  -208,  -208,  -208,  -208,
     124,  -208,  -208,  -208,  -208,  -208,    18,   120,   137,  -208,
    -208,  -208,  -208,   235,   167,     1,   166,   152,   154,     2,
     151,   155,   203,   158,    -5,     2,     2,   160,   180,   242,
      -5,  -208,  -208,   263,   287,   176,   162,  -208,   163,  -208,
    -208,   164,   208,  -208,  -208,   296,  -208,  -208,  -208,  -208,
     234,   328,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
     339,   281,   240,   237,    -5,   239,   288,  -208,   363,   384,
     265,  -208,   -15,   244,   257,  -208,  -208,  -208,    -5,    -5,
      -5,    -5,  -208,  -208,   243,   267,  -208,  -208,   246,  -208,
    -208,     3,    41,  -208,  -208,   266,   247,   270,   272,  -208,
    -208,   252,   299,   300,    -5,    -5,     2,   160,  -208,  -208,
     275,  -208,  -208,  -208,  -208,   276,  -208,   256,  -208,   258,
    -208,  -208,  -208,   259,   353,   -12,   260,    12,  -208,  -208,
     262,    -5,   285,   286,  -208,   274,   274,  -208,  -208,   415,
    -208,   331,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   429,
    -208,  -208,  -208,   334,  -208,   335,   336,   460,    -5,    -5,
    -208,  -208,  -208,   551,  -208,  -208,   431,   337,    -5,   338,
     362,  -208,   356,  -208,  -208,  -208,  -208,   382,   232,  -208,
    -208,  -208,    -5,    -5,   491,  -208,   364,   365,   366,  -208,
    -208,  -208,  -208,  -208,  -208,   110,    -7,  -208,  -208,   367,
    -208,  -208,   369,   370,   371,   373,   374,   112,   375,    23,
     372,  -208,  -208,  -208,  -208,  -208,   185,  -208,  -208,  -208,
    -208,  -208,  -208,   468,    25,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,   471,  -208,  -208,
    -208
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   209,     0,     2,     4,     5,
       6,     7,     8,     9,   105,   101,   102,   232,   233,     0,
     208,     1,     3,    11,     0,   103,   210,     0,     0,     0,
       0,     0,   132,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   104,     0,     0,     0,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   123,
     116,   117,   118,   119,   120,   121,   122,    33,     0,     0,
       0,     0,    48,     0,     0,    20,    21,    10,    12,    19,
      13,    14,    17,    15,    16,    18,     0,     0,   125,   127,
     129,   131,     0,    63,    62,     0,     0,   177,   176,     0,
     181,   180,   183,     0,     0,   191,   192,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    42,    41,    45,    46,    47,    50,    22,    43,    44,
       0,     0,   124,   126,   128,   130,     0,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,     0,     0,
       0,   160,     0,   163,   159,     0,     0,     0,   174,   179,
     178,     0,     0,     0,     0,   187,   188,   193,   194,   195,
       0,    62,   198,   197,   203,   199,     0,     0,     0,    35,
      36,    37,    34,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   167,     0,     0,     0,     0,     0,
     167,   133,   134,     0,     0,     0,     0,   182,     0,   184,
     196,     0,     0,   204,   206,    38,    39,    55,    56,    54,
       0,     0,    60,    52,    53,    57,    51,    24,   189,   190,
       0,     0,     0,     0,   167,     0,     0,   151,     0,     0,
       0,   137,   167,     0,   164,   162,   161,   153,   167,   167,
     167,   167,   200,   202,     0,   185,   205,   207,     0,    58,
      59,     0,     0,   235,   234,     0,     0,     0,     0,   152,
     168,     0,   147,   145,   167,   167,     0,     0,   154,   165,
       0,   171,   170,   173,   172,     0,   175,     0,    40,     0,
      49,    64,    61,     0,     0,     0,     0,     0,    23,    25,
       0,   167,     0,     0,   169,   139,   139,   150,   149,     0,
     138,     0,   105,   186,    64,    65,    26,    31,    32,     0,
      29,    27,    28,   236,   155,     0,     0,     0,   167,   167,
     136,   135,   166,     0,    67,    69,     0,     0,   167,     0,
       0,   142,   140,   141,   148,   146,   201,     0,    66,    30,
     237,   157,   167,   167,     0,    69,     0,     0,     0,    71,
      72,    73,    74,    75,    76,     0,     0,    77,    82,     0,
      83,    84,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    70,   156,   158,   144,   143,     0,    92,    93,    94,
      78,    81,    79,     0,     0,    85,    89,    98,    90,    91,
      96,    95,    97,    86,    87,    88,    68,     0,    99,   100,
      80
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -208,  -208,   495,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -110,   178,  -208,  -208,  -208,   138,  -208,  -208,
    -208,  -208,    22,   182,  -208,  -208,  -156,  -195,  -208,   189,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -207,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -167,   486,
    -208,  -208
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     6,     7,     8,    23,    27,    78,    79,   196,   272,
     309,    80,    81,    82,    83,    84,    85,   126,   195,   235,
     236,   271,    95,   325,   302,   345,   357,   358,   391,     9,
      10,    11,    15,    24,    49,    50,   213,   162,   252,   338,
     352,    51,   316,   315,   163,   210,   254,   247,    52,    53,
      54,    55,    56,   101,    57,    58,    59,    60,    61,    62,
      63,   263,   295,    64,    65,    66,    12,    20,   164,    19,
     275,   348
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     215,   182,   183,   257,   185,   227,   228,    93,   299,    93,
     214,   116,   251,    93,   211,    67,   328,   229,    97,   203,
     230,   402,    17,    18,    68,    16,    69,    70,    71,    72,
     186,   187,   188,    73,   117,   118,   204,   279,   119,   205,
     206,    25,   256,   240,   208,   288,   259,   261,   209,   248,
     249,   291,   292,   293,   294,    74,    75,    76,   231,   303,
      98,   245,   287,   193,   246,   329,    13,   304,   305,   194,
     403,   245,    14,   274,   246,    86,    87,   317,   318,    88,
     212,   283,   285,    89,   306,    90,    21,    91,   307,     1,
     103,   104,   320,     1,     2,     3,     4,     5,     2,     3,
       4,     5,   121,   122,   334,   232,   128,   129,   300,   106,
     107,   108,   102,   109,   110,    92,    77,   165,   166,   169,
     170,   120,   173,   174,   114,   233,   234,   181,    96,    94,
     319,   354,   355,   181,   221,   222,   331,   332,   400,   401,
      99,   361,   410,   411,   136,   100,   308,   413,   414,   418,
     419,   105,   341,   111,   112,   392,   393,   113,   115,   123,
     124,   301,   125,   127,   167,   171,   130,   131,   172,   175,
     353,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   132,   133,   134,   135,   168,   395,   176,   177,
     366,   367,   368,   178,   158,   369,   370,   371,   372,   373,
     374,   375,   376,   377,   378,   179,   180,   184,   379,   380,
     381,   382,   383,   189,   384,   385,   386,   387,   388,   190,
     191,   192,   389,   197,   199,   198,   201,   200,   207,   202,
     159,   216,   217,   160,   218,   223,   161,   366,   367,   368,
     219,   220,   369,   370,   371,   372,   373,   374,   375,   376,
     377,   378,   224,   225,   226,   379,   380,   381,   382,   383,
     237,   384,   385,   386,   387,   388,   238,   241,   239,   389,
     242,   243,   244,   253,   250,   262,   264,   265,   390,   266,
     416,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   267,   268,   390,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   269,   270,   276,
     277,   278,   280,   290,   281,   286,   255,   289,   310,   296,
     297,   298,   312,   311,   313,   314,  -136,  -135,   321,   323,
     322,   327,   324,   326,   337,   330,   333,   258,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   335,
     336,   260,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   342,   346,   347,   349,   350,   359,
     360,   362,   364,   273,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   363,   365,   282,   397,   398,
     399,   404,   405,   406,   417,   415,   407,   408,   409,   420,
     412,    22,   344,   396,   343,   339,    26,     0,   284,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   340,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,     0,     0,    28,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   351,    29,    30,    31,    32,    33,
       0,     0,     0,     0,     0,     0,    34,    35,    36,    28,
      37,    38,    39,     0,    40,     0,     0,    41,    42,     0,
      43,    44,     0,     0,     0,   394,     0,     0,     0,     0,
      45,    29,    30,    31,    32,    33,    46,     0,    47,    48,
       0,     0,    34,    35,    36,     0,    37,    38,    39,     0,
      40,     0,     0,    41,    42,     0,    43,    44,     0,     0,
       0,     0,     0,     0,     0,     0,   356,     0,     0,     0,
       0,     0,    46,     0,    47,    48,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157
};

static const yytype_int16 yycheck[] =
{
     167,   111,   112,   210,   114,     4,     5,     4,     5,     4,
     166,    14,   207,     4,    12,     4,    28,    16,    84,    80,
      19,    28,   124,   125,    13,     3,    15,    16,    17,    18,
     115,   116,   117,    22,    37,    38,    97,   244,    41,   100,
     101,    19,   209,   199,   102,   252,   213,   214,   106,   205,
     206,   258,   259,   260,   261,    44,    45,    46,    57,    18,
     126,    76,    77,    26,    79,    77,   104,    26,    27,    32,
      77,    76,   104,   240,    79,   111,   112,   284,   285,   124,
      78,   248,   249,   124,    43,   124,     0,   124,    47,     3,
      90,    91,   287,     3,     8,     9,    10,    11,     8,     9,
      10,    11,   124,   125,   311,   104,   124,   125,   105,    94,
      95,    96,    88,    98,    99,    75,   105,   100,   101,   123,
     124,   124,   102,   103,    10,   124,   125,   124,   124,   124,
     286,   338,   339,   124,   116,   117,   124,   125,    28,    29,
     124,   348,    30,    31,    22,   124,   105,   124,   125,   124,
     125,   123,   319,   107,   107,   362,   363,   124,   124,   124,
     124,   271,   124,   124,   108,    12,   124,   124,   106,   124,
     337,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,   127,   127,   127,   127,   126,   364,   124,   124,
      15,    16,    17,   124,    82,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,   124,   124,   124,    33,    34,
      35,    36,    37,   124,    39,    40,    41,    42,    43,   123,
     125,   124,    47,   124,   100,   124,   119,   124,    77,   124,
     118,    82,   124,   121,    18,   125,   124,    15,    16,    17,
     123,   127,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,   125,    28,    97,    33,    34,    35,    36,    37,
     104,    39,    40,    41,    42,    43,   124,   126,   124,    47,
     125,    78,   124,   103,   124,   109,   124,   124,   103,   125,
     105,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,   125,    38,   103,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,   123,    30,    78,
     120,   124,   123,   106,    76,   100,   124,   123,   102,   126,
     103,   125,   102,   126,   102,   123,    77,    77,   103,   123,
     104,    28,   124,   124,   110,   125,   124,   124,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,   124,
     124,   124,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,   123,    26,   122,   122,   122,    28,
     123,   123,   106,   124,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,   123,   104,   124,   124,   124,
     124,   124,   123,   123,    26,   123,   125,   124,   124,    28,
     125,     6,   324,   365,   322,   316,    20,    -1,   124,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   124,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    -1,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   124,    70,    71,    72,    73,    74,
      -1,    -1,    -1,    -1,    -1,    -1,    81,    82,    83,    48,
      85,    86,    87,    -1,    89,    -1,    -1,    92,    93,    -1,
      95,    96,    -1,    -1,    -1,   124,    -1,    -1,    -1,    -1,
     105,    70,    71,    72,    73,    74,   111,    -1,   113,   114,
      -1,    -1,    81,    82,    83,    -1,    85,    86,    87,    -1,
      89,    -1,    -1,    92,    93,    -1,    95,    96,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   105,    -1,    -1,    -1,
      -1,    -1,   111,    -1,   113,   114,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   129,   130,   131,   157,
     158,   159,   194,   104,   104,   160,   160,   124,   125,   197,
     195,     0,   130,   132,   161,   160,   197,   133,    48,    70,
      71,    72,    73,    74,    81,    82,    83,    85,    86,    87,
      89,    92,    93,    95,    96,   105,   111,   113,   114,   162,
     163,   169,   176,   177,   178,   179,   180,   182,   183,   184,
     185,   186,   187,   188,   191,   192,   193,     4,    13,    15,
      16,    17,    18,    22,    44,    45,    46,   105,   134,   135,
     139,   140,   141,   142,   143,   144,   111,   112,   124,   124,
     124,   124,    75,     4,   124,   150,   124,    84,   126,   124,
     124,   181,    88,    90,    91,   123,    94,    95,    96,    98,
      99,   107,   107,   124,    10,   124,    14,    37,    38,    41,
     124,   124,   125,   124,   124,   124,   145,   124,   124,   125,
     124,   124,   127,   127,   127,   127,    22,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    82,   118,
     121,   124,   165,   172,   196,   100,   101,   108,   126,   123,
     124,    12,   106,   102,   103,   124,   124,   124,   124,   124,
     124,   124,   150,   150,   124,   150,   115,   116,   117,   124,
     123,   125,   124,    26,    32,   146,   136,   124,   124,   100,
     124,   119,   124,    80,    97,   100,   101,    77,   102,   106,
     173,    12,    78,   164,   164,   196,    82,   124,    18,   123,
     127,   116,   117,   125,   125,    28,    97,     4,     5,    16,
      19,    57,   104,   124,   125,   147,   148,   104,   124,   124,
     164,   126,   125,    78,   124,    76,    79,   175,   164,   164,
     124,   165,   166,   103,   174,   124,   196,   175,   124,   196,
     124,   196,   109,   189,   124,   124,   125,   125,    38,   123,
      30,   149,   137,   124,   196,   198,    78,   120,   124,   175,
     123,    76,   124,   196,   124,   196,   100,    77,   175,   123,
     106,   175,   175,   175,   175,   190,   126,   103,   125,     5,
     105,   150,   152,    18,    26,    27,    43,    47,   105,   138,
     102,   126,   102,   102,   123,   171,   170,   175,   175,   164,
     165,   103,   104,   123,   124,   151,   124,    28,    28,    77,
     125,   124,   125,   124,   175,   124,   124,   110,   167,   167,
     124,   196,   123,   161,   151,   153,    26,   122,   199,   122,
     122,   124,   168,   196,   175,   175,   105,   154,   155,    28,
     123,   175,   123,   123,   106,   104,    15,    16,    17,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    33,
      34,    35,    36,    37,    39,    40,    41,    42,    43,    47,
     103,   156,   175,   175,   124,   196,   155,   124,   124,   124,
      28,    29,    28,    77,   124,   123,   123,   125,   124,   124,
      30,    31,   125,   124,   125,   123,   105,    26,   124,   125,
      28
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   128,   129,   129,   130,   130,   130,   130,   130,   132,
     131,   133,   133,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   136,   135,   137,   137,   138,   138,   138,   138,
     138,   138,   138,   139,   139,   139,   139,   139,   139,   139,
     139,   140,   140,   141,   141,   142,   143,   143,   145,   144,
     146,   146,   147,   147,   147,   147,   147,   148,   148,   148,
     149,   149,   150,   150,   151,   153,   152,   154,   152,   155,
     155,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   157,   158,   159,   160,   161,   161,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   163,   163,   163,   163,   163,   163,
     163,   163,   163,   164,   164,   165,   165,   166,   166,   167,
     167,   168,   168,   168,   168,   170,   169,   171,   169,   169,
     169,   169,   169,   169,   169,   169,   169,   169,   169,   172,
     172,   172,   172,   173,   173,   174,   174,   175,   175,   175,
     176,   176,   176,   176,   177,   178,   179,   179,   179,   179,
     180,   181,   181,   182,   182,   182,   182,   183,   183,   184,
     184,   185,   186,   186,   186,   186,   186,   187,   187,   188,
     190,   189,   191,   192,   193,   193,   193,   193,   194,   195,
     195,   196,   196,   196,   196,   196,   196,   196,   196,   196,
     196,   196,   196,   196,   196,   196,   196,   196,   196,   196,
     196,   196,   197,   197,   198,   198,   199,   199
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     0,     6,     0,     2,     2,     2,     2,     2,
       4,     2,     2,     1,     3,     3,     3,     3,     4,     4,
       6,     2,     2,     2,     2,     2,     2,     2,     0,     6,
       0,     2,     1,     1,     1,     1,     1,     1,     2,     2,
       0,     2,     1,     1,     0,     0,     4,     0,     7,     0,
       2,     1,     1,     1,     1,     1,     1,     1,     2,     2,
       4,     2,     1,     1,     1,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     3,
       3,     2,     2,     3,     3,     0,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     2,     3,     2,     3,     2,
       3,     2,     1,     1,     1,     4,     4,     1,     3,     0,
       2,     1,     1,     3,     3,     0,     9,     0,     9,     7,
       7,     5,     6,     5,     6,     8,    11,    10,    11,     1,
       1,     3,     3,     0,     2,     2,     4,     0,     2,     3,
       6,     6,     6,     6,     3,     6,     2,     2,     3,     3,
       2,     1,     3,     2,     4,     5,     7,     3,     3,     5,
       5,     2,     2,     3,     3,     3,     4,     3,     3,     3,
       0,     5,     5,     3,     4,     5,     4,     5,     2,     0,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     0,     2
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
#line 244 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	}
#line 1796 "test_spec_parse.c"
    break;

  case 20: /* cluster_item: T_BIND_SOURCE  */
#line 266 "test_spec_parse.y"
                        { current_spec->cluster.bindSource = true; }
#line 1802 "test_spec_parse.c"
    break;

  case 21: /* cluster_item: T_LEGACY_STARTUP  */
#line 267 "test_spec_parse.y"
                           { current_spec->cluster.legacyStartup = true; }
#line 1808 "test_spec_parse.c"
    break;

  case 22: /* $@2: %empty  */
#line 290 "test_spec_parse.y"
        {
		TestCluster *cl = &current_spec->cluster;

		if (cl->archiverCount >= PGAF_MAX_ARCHIVERS)
		{
			fprintf(stderr, "pgaftest: too many archivers (max %d)\n",
			        PGAF_MAX_ARCHIVERS);
			exit(1);
		}

		current_archiver = &cl->archivers[cl->archiverCount++];
		strlcpy(current_archiver->name, (yyvsp[0].str), sizeof(current_archiver->name));
		free((yyvsp[0].str));
	}
#line 1827 "test_spec_parse.c"
    break;

  case 26: /* archiver_opt: T_FORMATION T_IDENT  */
#line 314 "test_spec_parse.y"
        {
		if (current_archiver->formationCount >= PGAF_MAX_ARCHIVER_FORMATIONS)
		{
			fprintf(stderr,
			        "pgaftest: too many --formation entries for archiver "
			        "\"%s\" (max %d)\n",
			        current_archiver->name, PGAF_MAX_ARCHIVER_FORMATIONS);
			exit(1);
		}
		strlcpy(current_archiver->formations[current_archiver->formationCount++],
		        (yyvsp[0].str), sizeof(current_archiver->formations[0]));
		free((yyvsp[0].str));
	}
#line 1845 "test_spec_parse.c"
    break;

  case 27: /* archiver_opt: T_REGION T_IDENT  */
#line 328 "test_spec_parse.y"
        {
		strlcpy(current_archiver->region, (yyvsp[0].str), sizeof(current_archiver->region));
		free((yyvsp[0].str));
	}
#line 1854 "test_spec_parse.c"
    break;

  case 28: /* archiver_opt: T_REGION T_STRING  */
#line 333 "test_spec_parse.y"
        {
		strlcpy(current_archiver->region, (yyvsp[0].str), sizeof(current_archiver->region));
		free((yyvsp[0].str));
	}
#line 1863 "test_spec_parse.c"
    break;

  case 29: /* archiver_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 338 "test_spec_parse.y"
        {
		strlcpy(current_archiver->replicationPassword, (yyvsp[0].str),
		        sizeof(current_archiver->replicationPassword));
		free((yyvsp[0].str));
	}
#line 1873 "test_spec_parse.c"
    break;

  case 30: /* archiver_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 344 "test_spec_parse.y"
        {
		/* bare "create and launch deferred" = both gates, matching
		 * node_opt's own identical form */
		current_archiver->createDeferred = true;
		current_archiver->launchDeferred = true;
	}
#line 1884 "test_spec_parse.c"
    break;

  case 31: /* archiver_opt: T_LAUNCH T_DEFERRED  */
#line 351 "test_spec_parse.y"
        {
		current_archiver->launchDeferred = true;
	}
#line 1892 "test_spec_parse.c"
    break;

  case 32: /* archiver_opt: T_CREATE T_DEFERRED  */
#line 355 "test_spec_parse.y"
        {
		current_archiver->createDeferred = true;
	}
#line 1900 "test_spec_parse.c"
    break;

  case 33: /* monitor_line: T_MONITOR  */
#line 371 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
	}
#line 1908 "test_spec_parse.c"
    break;

  case 34: /* monitor_line: T_MONITOR T_DEBIAN_CLUSTER T_IDENT  */
#line 375 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[0].str));
	}
#line 1919 "test_spec_parse.c"
    break;

  case 35: /* monitor_line: T_MONITOR T_IMAGE_TARGET T_IDENT  */
#line 382 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[0].str));
	}
#line 1930 "test_spec_parse.c"
    break;

  case 36: /* monitor_line: T_MONITOR T_PORT T_INTEGER  */
#line 389 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[0].ival);
	}
#line 1940 "test_spec_parse.c"
    break;

  case 37: /* monitor_line: T_MONITOR T_PASSWORD T_STRING  */
#line 395 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[0].str));
	}
#line 1951 "test_spec_parse.c"
    break;

  case 38: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED  */
#line 402 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1962 "test_spec_parse.c"
    break;

  case 39: /* monitor_line: T_MONITOR T_IDENT T_INITIALLY T_STOPPED  */
#line 409 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1973 "test_spec_parse.c"
    break;

  case 40: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING  */
#line 416 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-4].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-4].str));
		/* password for second monitor not yet stored */
		free((yyvsp[0].str));
	}
#line 1986 "test_spec_parse.c"
    break;

  case 41: /* image_line: T_IMAGE T_STRING  */
#line 429 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1996 "test_spec_parse.c"
    break;

  case 42: /* image_line: T_IMAGE T_IDENT  */
#line 435 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 2006 "test_spec_parse.c"
    break;

  case 43: /* extension_version_line: T_EXTENSION_VERSION T_IDENT  */
#line 445 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2016 "test_spec_parse.c"
    break;

  case 44: /* extension_version_line: T_EXTENSION_VERSION T_STRING  */
#line 451 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2026 "test_spec_parse.c"
    break;

  case 45: /* ssl_line: T_SSL T_IDENT  */
#line 461 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl, (yyvsp[0].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[0].str));
	}
#line 2036 "test_spec_parse.c"
    break;

  case 46: /* auth_line: T_AUTH T_IDENT  */
#line 471 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2046 "test_spec_parse.c"
    break;

  case 47: /* auth_line: T_AUTH_METHOD T_IDENT  */
#line 477 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2056 "test_spec_parse.c"
    break;

  case 48: /* $@3: %empty  */
#line 487 "test_spec_parse.y"
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
#line 2074 "test_spec_parse.c"
    break;

  case 52: /* bare_name: T_IDENT  */
#line 514 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2080 "test_spec_parse.c"
    break;

  case 53: /* bare_name: T_STRING  */
#line 515 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2086 "test_spec_parse.c"
    break;

  case 54: /* bare_name: T_AUTH  */
#line 516 "test_spec_parse.y"
                    { (yyval.str) = strdup("auth"); }
#line 2092 "test_spec_parse.c"
    break;

  case 55: /* bare_name: T_MONITOR  */
#line 517 "test_spec_parse.y"
                    { (yyval.str) = strdup("monitor"); }
#line 2098 "test_spec_parse.c"
    break;

  case 56: /* bare_name: T_NODE  */
#line 518 "test_spec_parse.y"
                    { (yyval.str) = strdup("node"); }
#line 2104 "test_spec_parse.c"
    break;

  case 57: /* formation_opt: bare_name  */
#line 523 "test_spec_parse.y"
        {
		strlcpy(current_formation->name, (yyvsp[0].str), sizeof(current_formation->name));
		free((yyvsp[0].str));
	}
#line 2113 "test_spec_parse.c"
    break;

  case 58: /* formation_opt: T_NUM_SYNC T_INTEGER  */
#line 528 "test_spec_parse.y"
        {
		current_formation->numSync = (yyvsp[0].ival);
	}
#line 2121 "test_spec_parse.c"
    break;

  case 59: /* formation_opt: T_FS_SECONDARY T_FALSE  */
#line 532 "test_spec_parse.y"
        {
		current_formation->disableSecondary = true;
	}
#line 2129 "test_spec_parse.c"
    break;

  case 62: /* node_name: T_IDENT  */
#line 558 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 2135 "test_spec_parse.c"
    break;

  case 63: /* node_name: T_MONITOR  */
#line 559 "test_spec_parse.y"
                     { (yyval.str) = strdup("monitor"); }
#line 2141 "test_spec_parse.c"
    break;

  case 64: /* init_node_slot: %empty  */
#line 568 "test_spec_parse.y"
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
#line 2158 "test_spec_parse.c"
    break;

  case 65: /* $@4: %empty  */
#line 585 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2167 "test_spec_parse.c"
    break;

  case 67: /* $@5: %empty  */
#line 592 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2176 "test_spec_parse.c"
    break;

  case 71: /* node_opt: T_COORDINATOR  */
#line 606 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
#line 2185 "test_spec_parse.c"
    break;

  case 72: /* node_opt: T_WORKER  */
#line 611 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
#line 2194 "test_spec_parse.c"
    break;

  case 73: /* node_opt: T_ARCHIVER  */
#line 616 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_ARCHIVER;
	}
#line 2202 "test_spec_parse.c"
    break;

  case 74: /* node_opt: T_ASYNC  */
#line 620 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2210 "test_spec_parse.c"
    break;

  case 75: /* node_opt: T_NO_MONITOR  */
#line 624 "test_spec_parse.y"
        {
		current_node->noMonitor = true;
	}
#line 2218 "test_spec_parse.c"
    break;

  case 76: /* node_opt: T_SUSPENDED  */
#line 628 "test_spec_parse.y"
        {
		current_node->suspended = true;
	}
#line 2226 "test_spec_parse.c"
    break;

  case 77: /* node_opt: T_DEFERRED  */
#line 632 "test_spec_parse.y"
        {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2236 "test_spec_parse.c"
    break;

  case 78: /* node_opt: T_LAUNCH T_DEFERRED  */
#line 638 "test_spec_parse.y"
        {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	}
#line 2245 "test_spec_parse.c"
    break;

  case 79: /* node_opt: T_CREATE T_DEFERRED  */
#line 643 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
	}
#line 2253 "test_spec_parse.c"
    break;

  case 80: /* node_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 647 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2262 "test_spec_parse.c"
    break;

  case 81: /* node_opt: T_LAUNCH T_IMMEDIATE  */
#line 652 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2270 "test_spec_parse.c"
    break;

  case 82: /* node_opt: T_IMMEDIATE  */
#line 656 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2278 "test_spec_parse.c"
    break;

  case 83: /* node_opt: T_LISTEN  */
#line 660 "test_spec_parse.y"
        {
		current_node->listen = true;
	}
#line 2286 "test_spec_parse.c"
    break;

  case 84: /* node_opt: T_CITUS_SECONDARY  */
#line 664 "test_spec_parse.y"
        {
		current_node->citusSecondary = true;
	}
#line 2294 "test_spec_parse.c"
    break;

  case 85: /* node_opt: T_CANDIDATE_PRIORITY T_INTEGER  */
#line 668 "test_spec_parse.y"
        {
		current_node->candidatePriority = (yyvsp[0].ival);
	}
#line 2302 "test_spec_parse.c"
    break;

  case 86: /* node_opt: T_REGION T_IDENT  */
#line 672 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2311 "test_spec_parse.c"
    break;

  case 87: /* node_opt: T_REGION T_STRING  */
#line 677 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2320 "test_spec_parse.c"
    break;

  case 88: /* node_opt: T_GROUP T_INTEGER  */
#line 682 "test_spec_parse.y"
        {
		current_node->group = (yyvsp[0].ival);
	}
#line 2328 "test_spec_parse.c"
    break;

  case 89: /* node_opt: T_PORT T_INTEGER  */
#line 686 "test_spec_parse.y"
        {
		current_node->pgPort = (yyvsp[0].ival);
	}
#line 2336 "test_spec_parse.c"
    break;

  case 90: /* node_opt: T_CITUS_CLUSTER_NAME T_IDENT  */
#line 690 "test_spec_parse.y"
        {
		strlcpy(current_node->citusClusterName, (yyvsp[0].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[0].str));
	}
#line 2346 "test_spec_parse.c"
    break;

  case 91: /* node_opt: T_DEBIAN_CLUSTER T_IDENT  */
#line 696 "test_spec_parse.y"
        {
		strlcpy(current_node->debianCluster, (yyvsp[0].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[0].str));
	}
#line 2356 "test_spec_parse.c"
    break;

  case 92: /* node_opt: T_SSL T_IDENT  */
#line 702 "test_spec_parse.y"
        {
		strlcpy(current_node->ssl, (yyvsp[0].str), sizeof(current_node->ssl));
		free((yyvsp[0].str));
	}
#line 2365 "test_spec_parse.c"
    break;

  case 93: /* node_opt: T_AUTH T_IDENT  */
#line 707 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2374 "test_spec_parse.c"
    break;

  case 94: /* node_opt: T_AUTH_METHOD T_IDENT  */
#line 712 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2383 "test_spec_parse.c"
    break;

  case 95: /* node_opt: T_REPLICATION_QUORUM T_TRUE  */
#line 717 "test_spec_parse.y"
        {
		current_node->replicationQuorum = true;
	}
#line 2391 "test_spec_parse.c"
    break;

  case 96: /* node_opt: T_REPLICATION_QUORUM T_FALSE  */
#line 721 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2399 "test_spec_parse.c"
    break;

  case 97: /* node_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 725 "test_spec_parse.y"
        {
		strlcpy(current_node->replicationPassword, (yyvsp[0].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[0].str));
	}
#line 2409 "test_spec_parse.c"
    break;

  case 98: /* node_opt: T_MONITOR_PASSWORD T_STRING  */
#line 731 "test_spec_parse.y"
        {
		strlcpy(current_node->monitorPassword, (yyvsp[0].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[0].str));
	}
#line 2419 "test_spec_parse.c"
    break;

  case 99: /* node_opt: T_VOLUME T_IDENT T_IDENT  */
#line 737 "test_spec_parse.y"
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
#line 2437 "test_spec_parse.c"
    break;

  case 100: /* node_opt: T_VOLUME T_IDENT T_STRING  */
#line 751 "test_spec_parse.y"
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
#line 2455 "test_spec_parse.c"
    break;

  case 101: /* setup_block: T_SETUP cmd_block  */
#line 772 "test_spec_parse.y"
        {
		current_spec->setup = (yyvsp[0].step);
	}
#line 2463 "test_spec_parse.c"
    break;

  case 102: /* teardown_block: T_TEARDOWN cmd_block  */
#line 779 "test_spec_parse.y"
        {
		current_spec->teardown = (yyvsp[0].step);
	}
#line 2471 "test_spec_parse.c"
    break;

  case 103: /* named_step: T_STEP ident_or_string cmd_block  */
#line 790 "test_spec_parse.y"
        {
		TestStep *s = (yyvsp[0].step);
		strncpy(s->name, (yyvsp[-1].str), sizeof(s->name) - 1);
		free((yyvsp[-1].str));
		register_step(current_spec, s);
	}
#line 2482 "test_spec_parse.c"
    break;

  case 104: /* cmd_block: T_LBRACE cmd_list T_RBRACE  */
#line 808 "test_spec_parse.y"
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
#line 2497 "test_spec_parse.c"
    break;

  case 105: /* cmd_list: %empty  */
#line 822 "test_spec_parse.y"
        {
		(yyval.step) = make_step("");
	}
#line 2505 "test_spec_parse.c"
    break;

  case 106: /* cmd_list: cmd_list step_cmd  */
#line 826 "test_spec_parse.y"
        {
		if ((yyvsp[0].cmd)) append_cmd((yyvsp[-1].step), (yyvsp[0].cmd));
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2514 "test_spec_parse.c"
    break;

  case 107: /* step_cmd: exec_cmd  */
#line 833 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2520 "test_spec_parse.c"
    break;

  case 108: /* step_cmd: wait_cmd  */
#line 834 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2526 "test_spec_parse.c"
    break;

  case 109: /* step_cmd: assert_cmd  */
#line 835 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2532 "test_spec_parse.c"
    break;

  case 110: /* step_cmd: sql_cmd  */
#line 836 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2538 "test_spec_parse.c"
    break;

  case 111: /* step_cmd: let_cmd  */
#line 837 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2544 "test_spec_parse.c"
    break;

  case 112: /* step_cmd: expect_cmd  */
#line 838 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2550 "test_spec_parse.c"
    break;

  case 113: /* step_cmd: promote_cmd  */
#line 839 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2556 "test_spec_parse.c"
    break;

  case 114: /* step_cmd: perform_cmd  */
#line 840 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2562 "test_spec_parse.c"
    break;

  case 115: /* step_cmd: network_cmd  */
#line 841 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2568 "test_spec_parse.c"
    break;

  case 116: /* step_cmd: sleep_cmd  */
#line 842 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2574 "test_spec_parse.c"
    break;

  case 117: /* step_cmd: compose_cmd  */
#line 843 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2580 "test_spec_parse.c"
    break;

  case 118: /* step_cmd: postgres_ctl_cmd  */
#line 844 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2586 "test_spec_parse.c"
    break;

  case 119: /* step_cmd: fsm_step_cmd  */
#line 845 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2592 "test_spec_parse.c"
    break;

  case 120: /* step_cmd: stays_while_cmd  */
#line 846 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2598 "test_spec_parse.c"
    break;

  case 121: /* step_cmd: set_monitor_cmd  */
#line 847 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2604 "test_spec_parse.c"
    break;

  case 122: /* step_cmd: logs_cmd  */
#line 848 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2610 "test_spec_parse.c"
    break;

  case 123: /* step_cmd: nodeini_cmd  */
#line 849 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2616 "test_spec_parse.c"
    break;

  case 124: /* exec_cmd: T_EXEC T_IDENT T_SHELL_ARGS  */
#line 864 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2627 "test_spec_parse.c"
    break;

  case 125: /* exec_cmd: T_EXEC T_IDENT  */
#line 871 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2637 "test_spec_parse.c"
    break;

  case 126: /* exec_cmd: T_EXEC_FAILS T_IDENT T_SHELL_ARGS  */
#line 877 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2648 "test_spec_parse.c"
    break;

  case 127: /* exec_cmd: T_EXEC_FAILS T_IDENT  */
#line 884 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2658 "test_spec_parse.c"
    break;

  case 128: /* exec_cmd: T_RUN T_IDENT T_SHELL_ARGS  */
#line 890 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2669 "test_spec_parse.c"
    break;

  case 129: /* exec_cmd: T_RUN T_IDENT  */
#line 897 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2679 "test_spec_parse.c"
    break;

  case 130: /* exec_cmd: T_PG_AUTOCTL T_IDENT T_SHELL_ARGS  */
#line 903 "test_spec_parse.y"
        {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2691 "test_spec_parse.c"
    break;

  case 131: /* exec_cmd: T_PG_AUTOCTL T_IDENT  */
#line 911 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[0].str));
	}
#line 2701 "test_spec_parse.c"
    break;

  case 132: /* exec_cmd: T_PG_AUTOCTL  */
#line 917 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	}
#line 2709 "test_spec_parse.c"
    break;

  case 135: /* wait_multi_condition: T_IDENT T_STATE state_op fsm_state  */
#line 955 "test_spec_parse.y"
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
#line 2728 "test_spec_parse.c"
    break;

  case 136: /* wait_multi_condition: T_IDENT T_STATE state_op T_IDENT  */
#line 970 "test_spec_parse.y"
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
#line 2747 "test_spec_parse.c"
    break;

  case 141: /* pass_state_list: fsm_state  */
#line 1010 "test_spec_parse.y"
        {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2759 "test_spec_parse.c"
    break;

  case 142: /* pass_state_list: T_IDENT  */
#line 1018 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2771 "test_spec_parse.c"
    break;

  case 143: /* pass_state_list: pass_state_list T_COMMA fsm_state  */
#line 1026 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2782 "test_spec_parse.c"
    break;

  case 144: /* pass_state_list: pass_state_list T_COMMA T_IDENT  */
#line 1033 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2794 "test_spec_parse.c"
    break;

  case 145: /* $@6: %empty  */
#line 1044 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); }
#line 2803 "test_spec_parse.c"
    break;

  case 146: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state $@6 opt_passing_through opt_timeout  */
#line 1049 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2813 "test_spec_parse.c"
    break;

  case 147: /* $@7: %empty  */
#line 1055 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); free((yyvsp[0].str)); }
#line 2822 "test_spec_parse.c"
    break;

  case 148: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT $@7 opt_passing_through opt_timeout  */
#line 1060 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2832 "test_spec_parse.c"
    break;

  case 149: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1066 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2845 "test_spec_parse.c"
    break;

  case 150: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1075 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2858 "test_spec_parse.c"
    break;

  case 151: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout  */
#line 1084 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-2].str));
	}
#line 2869 "test_spec_parse.c"
    break;

  case 152: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_REPLAYS T_IDENT opt_timeout  */
#line 1098 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2881 "test_spec_parse.c"
    break;

  case 153: /* wait_cmd: T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout  */
#line 1106 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2891 "test_spec_parse.c"
    break;

  case 154: /* wait_cmd: T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout  */
#line 1120 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2901 "test_spec_parse.c"
    break;

  case 155: /* wait_cmd: T_WAIT T_UNTIL T_SQL T_IDENT T_BLOCK T_IS T_BLOCK opt_timeout  */
#line 1135 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service,  (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,     (yyvsp[-3].str), sizeof((yyval.cmd)->args));
		strlcpy((yyval.cmd)->expected, (yyvsp[-1].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2914 "test_spec_parse.c"
    break;

  case 156: /* wait_cmd: T_WAIT T_UNTIL T_WAL T_SEGMENT T_STRING T_ARCHIVED T_IN T_IDENT T_SLASH T_INTEGER opt_timeout  */
#line 1152 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
		        "SELECT pgautofailover.wal_archived('%s', %d, '%s')",
		        (yyvsp[-3].str), (yyvsp[-1].ival), (yyvsp[-6].str));
		strlcpy((yyval.cmd)->expected, "t", sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-6].str)); free((yyvsp[-3].str));
	}
#line 2929 "test_spec_parse.c"
    break;

  case 157: /* wait_cmd: T_WAIT T_UNTIL T_ARCHIVER T_STATE state_op wait_state_name T_IN T_IDENT opt_wait_group opt_timeout  */
#line 1176 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		if ((yyvsp[-1].ival) >= 0)
		{
			sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
			        "SELECT reportedstate::text FROM pgautofailover.node"
			        " WHERE nodename LIKE 'archiver-%%' AND formationid = '%s'"
			        " AND groupid = %d", (yyvsp[-2].str), (yyvsp[-1].ival));
		}
		else
		{
			sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
			        "SELECT reportedstate::text FROM pgautofailover.node"
			        " WHERE nodename LIKE 'archiver-%%' AND formationid = '%s'", (yyvsp[-2].str));
		}
		strlcpy((yyval.cmd)->expected, (yyvsp[-4].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-2].str));
	}
#line 2954 "test_spec_parse.c"
    break;

  case 158: /* wait_cmd: T_WAIT T_UNTIL T_BASEBACKUP T_IDENT T_IS T_IDENT T_IN T_IDENT T_SLASH T_INTEGER opt_timeout  */
#line 1205 "test_spec_parse.y"
        {
		if (!streq((yyvsp[-7].str), "source") &&
		    !streq((yyvsp[-7].str), "status") &&
		    !streq((yyvsp[-7].str), "replaymode"))
		{
			fprintf(stderr,
			        "pgaftest: line %d: \"wait until basebackup %s ...\" -- "
			        "unknown property (expected source, status, or replaymode)\n",
			        pgaf_line_number, (yyvsp[-7].str));
			exit(1);
		}
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
		        "SELECT %s::text FROM pgautofailover.get_latest_basebackup('%s', %d)",
		        (yyvsp[-7].str), (yyvsp[-3].str), (yyvsp[-1].ival));
		strlcpy((yyval.cmd)->expected, (yyvsp[-5].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-7].str)); free((yyvsp[-5].str)); free((yyvsp[-3].str));
	}
#line 2979 "test_spec_parse.c"
    break;

  case 159: /* state_name_list: fsm_state  */
#line 1235 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2989 "test_spec_parse.c"
    break;

  case 160: /* state_name_list: T_IDENT  */
#line 1241 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 3000 "test_spec_parse.c"
    break;

  case 161: /* state_name_list: state_name_list T_COMMA fsm_state  */
#line 1248 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 3010 "test_spec_parse.c"
    break;

  case 162: /* state_name_list: state_name_list T_COMMA T_IDENT  */
#line 1254 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 3021 "test_spec_parse.c"
    break;

  case 165: /* group_items: T_GROUP T_INTEGER  */
#line 1273 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 3030 "test_spec_parse.c"
    break;

  case 166: /* group_items: group_items T_COMMA T_GROUP T_INTEGER  */
#line 1278 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 3039 "test_spec_parse.c"
    break;

  case 167: /* opt_timeout: %empty  */
#line 1285 "test_spec_parse.y"
                                       { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; }
#line 3045 "test_spec_parse.c"
    break;

  case 168: /* opt_timeout: T_TIMEOUT T_INTEGER  */
#line 1286 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 3051 "test_spec_parse.c"
    break;

  case 169: /* opt_timeout: T_WITH T_TIMEOUT T_INTEGER  */
#line 1287 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 3057 "test_spec_parse.c"
    break;

  case 170: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout  */
#line 1299 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3069 "test_spec_parse.c"
    break;

  case 171: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout  */
#line 1307 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3081 "test_spec_parse.c"
    break;

  case 172: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1315 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3093 "test_spec_parse.c"
    break;

  case 173: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1323 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3105 "test_spec_parse.c"
    break;

  case 174: /* sql_cmd: T_SQL T_IDENT T_BLOCK  */
#line 1341 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3116 "test_spec_parse.c"
    break;

  case 175: /* let_cmd: T_LET T_IDENT T_EQUALS T_SQL T_IDENT T_BLOCK  */
#line 1359 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LET);
		strlcpy((yyval.cmd)->state,   (yyvsp[-4].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-4].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3128 "test_spec_parse.c"
    break;

  case 176: /* expect_cmd: T_EXPECT T_BLOCK  */
#line 1375 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[0].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[0].str));
	}
#line 3139 "test_spec_parse.c"
    break;

  case 177: /* expect_cmd: T_EXPECT T_ERROR  */
#line 1382 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	}
#line 3147 "test_spec_parse.c"
    break;

  case 178: /* expect_cmd: T_EXPECT T_ERROR T_IDENT  */
#line 1386 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[0].str), sizeof((yyval.cmd)->state));
		free((yyvsp[0].str));
	}
#line 3157 "test_spec_parse.c"
    break;

  case 179: /* expect_cmd: T_EXPECT T_ERROR T_INTEGER  */
#line 1392 "test_spec_parse.y"
        {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[0].ival));
	}
#line 3167 "test_spec_parse.c"
    break;

  case 180: /* promote_cmd: T_PROMOTE promote_list  */
#line 1405 "test_spec_parse.y"
        {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	}
#line 3176 "test_spec_parse.c"
    break;

  case 181: /* promote_list: T_IDENT  */
#line 1413 "test_spec_parse.y"
        {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3188 "test_spec_parse.c"
    break;

  case 182: /* promote_list: promote_list T_COMMA T_IDENT  */
#line 1421 "test_spec_parse.y"
        {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3199 "test_spec_parse.c"
    break;

  case 183: /* perform_cmd: T_PERFORM T_FAILOVER  */
#line 1442 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3210 "test_spec_parse.c"
    break;

  case 184: /* perform_cmd: T_PERFORM T_FAILOVER T_GROUP T_INTEGER  */
#line 1449 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3221 "test_spec_parse.c"
    break;

  case 185: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT  */
#line 1456 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[0].str));
	}
#line 3233 "test_spec_parse.c"
    break;

  case 186: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT T_GROUP T_INTEGER  */
#line 1464 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[-2].str));
	}
#line 3245 "test_spec_parse.c"
    break;

  case 187: /* network_cmd: T_NETWORK T_DISCONNECT T_IDENT  */
#line 1480 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3255 "test_spec_parse.c"
    break;

  case 188: /* network_cmd: T_NETWORK T_CONNECT T_IDENT  */
#line 1486 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3265 "test_spec_parse.c"
    break;

  case 189: /* nodeini_cmd: T_NODEINI T_SET T_IDENT T_IDENT T_IDENT  */
#line 1507 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3277 "test_spec_parse.c"
    break;

  case 190: /* nodeini_cmd: T_NODEINI T_GET T_IDENT T_IDENT T_IDENT  */
#line 1515 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3289 "test_spec_parse.c"
    break;

  case 191: /* sleep_cmd: T_SLEEP T_INTEGER  */
#line 1530 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
	}
#line 3298 "test_spec_parse.c"
    break;

  case 192: /* compose_cmd: T_COMPOSE T_DOWN  */
#line 1544 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	}
#line 3306 "test_spec_parse.c"
    break;

  case 193: /* compose_cmd: T_COMPOSE T_START T_IDENT  */
#line 1548 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3316 "test_spec_parse.c"
    break;

  case 194: /* compose_cmd: T_COMPOSE T_STOP T_IDENT  */
#line 1554 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3326 "test_spec_parse.c"
    break;

  case 195: /* compose_cmd: T_COMPOSE T_KILL T_IDENT  */
#line 1560 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3336 "test_spec_parse.c"
    break;

  case 196: /* compose_cmd: T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS  */
#line 1586 "test_spec_parse.y"
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
#line 3363 "test_spec_parse.c"
    break;

  case 197: /* postgres_ctl_cmd: T_STOP T_POSTGRES node_name  */
#line 1620 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3373 "test_spec_parse.c"
    break;

  case 198: /* postgres_ctl_cmd: T_START T_POSTGRES node_name  */
#line 1626 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3383 "test_spec_parse.c"
    break;

  case 199: /* fsm_step_cmd: T_FSM T_STEP node_name  */
#line 1647 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3393 "test_spec_parse.c"
    break;

  case 200: /* $@8: %empty  */
#line 1663 "test_spec_parse.y"
                { pgaf_next_brace_is_while = 1; }
#line 3399 "test_spec_parse.c"
    break;

  case 201: /* while_body: T_WHILE $@8 T_LBRACE cmd_list T_RBRACE  */
#line 1664 "test_spec_parse.y"
        { (yyval.step) = (yyvsp[-1].step); }
#line 3405 "test_spec_parse.c"
    break;

  case 202: /* stays_while_cmd: T_ASSERT node_name T_STAYS fsm_state while_body  */
#line 1669 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[0].step)) ? (yyvsp[0].step)->commands : NULL;
		free((yyvsp[-3].str));
	}
#line 3417 "test_spec_parse.c"
    break;

  case 203: /* set_monitor_cmd: T_SET T_IDENT T_IDENT  */
#line 1688 "test_spec_parse.y"
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
#line 3434 "test_spec_parse.c"
    break;

  case 204: /* logs_cmd: T_LOGS T_IDENT T_CONTAINS T_STRING  */
#line 1713 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3447 "test_spec_parse.c"
    break;

  case 205: /* logs_cmd: T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING  */
#line 1722 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3460 "test_spec_parse.c"
    break;

  case 206: /* logs_cmd: T_LOGS T_IDENT T_MATCHES T_STRING  */
#line 1731 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3473 "test_spec_parse.c"
    break;

  case 207: /* logs_cmd: T_LOGS T_IDENT T_NOT T_MATCHES T_STRING  */
#line 1740 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3486 "test_spec_parse.c"
    break;

  case 210: /* sequence_names: sequence_names ident_or_string  */
#line 1761 "test_spec_parse.y"
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
#line 3502 "test_spec_parse.c"
    break;

  case 211: /* fsm_state: T_FS_INIT  */
#line 1782 "test_spec_parse.y"
                                   { (yyval.str) = "init"; }
#line 3508 "test_spec_parse.c"
    break;

  case 212: /* fsm_state: T_FS_SINGLE  */
#line 1783 "test_spec_parse.y"
                                   { (yyval.str) = "single"; }
#line 3514 "test_spec_parse.c"
    break;

  case 213: /* fsm_state: T_FS_PRIMARY  */
#line 1784 "test_spec_parse.y"
                                   { (yyval.str) = "primary"; }
#line 3520 "test_spec_parse.c"
    break;

  case 214: /* fsm_state: T_FS_WAIT_PRIMARY  */
#line 1785 "test_spec_parse.y"
                                   { (yyval.str) = "wait_primary"; }
#line 3526 "test_spec_parse.c"
    break;

  case 215: /* fsm_state: T_FS_WAIT_STANDBY  */
#line 1786 "test_spec_parse.y"
                                   { (yyval.str) = "wait_standby"; }
#line 3532 "test_spec_parse.c"
    break;

  case 216: /* fsm_state: T_FS_DEMOTED  */
#line 1787 "test_spec_parse.y"
                                   { (yyval.str) = "demoted"; }
#line 3538 "test_spec_parse.c"
    break;

  case 217: /* fsm_state: T_FS_DEMOTE_TIMEOUT  */
#line 1788 "test_spec_parse.y"
                                   { (yyval.str) = "demote_timeout"; }
#line 3544 "test_spec_parse.c"
    break;

  case 218: /* fsm_state: T_FS_DRAINING  */
#line 1789 "test_spec_parse.y"
                                   { (yyval.str) = "draining"; }
#line 3550 "test_spec_parse.c"
    break;

  case 219: /* fsm_state: T_FS_SECONDARY  */
#line 1790 "test_spec_parse.y"
                                   { (yyval.str) = "secondary"; }
#line 3556 "test_spec_parse.c"
    break;

  case 220: /* fsm_state: T_FS_CATCHINGUP  */
#line 1791 "test_spec_parse.y"
                                   { (yyval.str) = "catchingup"; }
#line 3562 "test_spec_parse.c"
    break;

  case 221: /* fsm_state: T_FS_PREP_PROMOTION  */
#line 1792 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_promotion"; }
#line 3568 "test_spec_parse.c"
    break;

  case 222: /* fsm_state: T_FS_STOP_REPLICATION  */
#line 1793 "test_spec_parse.y"
                                   { (yyval.str) = "stop_replication"; }
#line 3574 "test_spec_parse.c"
    break;

  case 223: /* fsm_state: T_FS_MAINTENANCE  */
#line 1794 "test_spec_parse.y"
                                   { (yyval.str) = "maintenance"; }
#line 3580 "test_spec_parse.c"
    break;

  case 224: /* fsm_state: T_FS_JOIN_PRIMARY  */
#line 1795 "test_spec_parse.y"
                                   { (yyval.str) = "join_primary"; }
#line 3586 "test_spec_parse.c"
    break;

  case 225: /* fsm_state: T_FS_APPLY_SETTINGS  */
#line 1796 "test_spec_parse.y"
                                   { (yyval.str) = "apply_settings"; }
#line 3592 "test_spec_parse.c"
    break;

  case 226: /* fsm_state: T_FS_PREPARE_MAINTENANCE  */
#line 1797 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_maintenance"; }
#line 3598 "test_spec_parse.c"
    break;

  case 227: /* fsm_state: T_FS_WAIT_MAINTENANCE  */
#line 1798 "test_spec_parse.y"
                                   { (yyval.str) = "wait_maintenance"; }
#line 3604 "test_spec_parse.c"
    break;

  case 228: /* fsm_state: T_FS_REPORT_LSN  */
#line 1799 "test_spec_parse.y"
                                   { (yyval.str) = "report_lsn"; }
#line 3610 "test_spec_parse.c"
    break;

  case 229: /* fsm_state: T_FS_FAST_FORWARD  */
#line 1800 "test_spec_parse.y"
                                   { (yyval.str) = "fast_forward"; }
#line 3616 "test_spec_parse.c"
    break;

  case 230: /* fsm_state: T_FS_JOIN_SECONDARY  */
#line 1801 "test_spec_parse.y"
                                   { (yyval.str) = "join_secondary"; }
#line 3622 "test_spec_parse.c"
    break;

  case 231: /* fsm_state: T_FS_DROPPED  */
#line 1802 "test_spec_parse.y"
                                   { (yyval.str) = "dropped"; }
#line 3628 "test_spec_parse.c"
    break;

  case 232: /* ident_or_string: T_IDENT  */
#line 1810 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3634 "test_spec_parse.c"
    break;

  case 233: /* ident_or_string: T_STRING  */
#line 1811 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3640 "test_spec_parse.c"
    break;

  case 234: /* wait_state_name: fsm_state  */
#line 1822 "test_spec_parse.y"
                     { (yyval.str) = strdup((yyvsp[0].str)); }
#line 3646 "test_spec_parse.c"
    break;

  case 235: /* wait_state_name: T_IDENT  */
#line 1823 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 3652 "test_spec_parse.c"
    break;

  case 236: /* opt_wait_group: %empty  */
#line 1831 "test_spec_parse.y"
                               { (yyval.ival) = -1; }
#line 3658 "test_spec_parse.c"
    break;

  case 237: /* opt_wait_group: T_SLASH T_INTEGER  */
#line 1832 "test_spec_parse.y"
                               { (yyval.ival) = (yyvsp[0].ival); }
#line 3664 "test_spec_parse.c"
    break;


#line 3668 "test_spec_parse.c"

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

#line 1835 "test_spec_parse.y"


/*
 * fold_archivers_into_formations turns each top-level "archiver { }"
 * declaration (TestArchiverNode, cluster->archivers[]) into an ordinary
 * TestNode of kind NODE_KIND_ARCHIVER, appended to its own declared
 * formation's own node list -- see TestArchiverNode's own comment
 * (test_spec.h) for why the *declaration* still needs to be top-level even
 * though it ends up represented identically to the older, still-supported
 * "archiver nested inside a formation_block" spelling once parsed. Called
 * once, right after yyparse() returns, so every caller downstream of
 * parse_test_spec() (compose_gen.c included) only ever sees ordinary
 * TestNode entries and needs no awareness of TestArchiverNode at all.
 *
 * cluster->archiverCount is reset to 0 once every entry has been folded,
 * so cluster->archivers[] is never a second, stale source of truth for
 * the very same nodes now living in cluster->formations[].nodes[].
 */
static void
fold_archivers_into_formations(TestCluster *cluster)
{
	for (int ai = 0; ai < cluster->archiverCount; ai++)
	{
		TestArchiverNode *a = &cluster->archivers[ai];

		if (a->formationCount == 0)
		{
			fprintf(stderr,
			        "pgaftest: archiver \"%s\" needs at least one "
			        "\"formation <name>\" entry\n", a->name);
			exit(1);
		}

		if (a->formationCount > 1)
		{
			fprintf(stderr,
			        "pgaftest: archiver \"%s\" lists %d formations, but "
			        "pg_autoctl create archiver's own ini-driven bring-up "
			        "only attaches to one at create time -- declare just "
			        "\"formation %s\" here and attach the rest (e.g. "
			        "\"%s\") dynamically once it's running instead, via a "
			        "direct \"sql monitor { SELECT pgautofailover."
			        "archiver_add_formation(...) }\" step -- see "
			        "archiver_multi_formation.pgaf for the pattern\n",
			        a->name, a->formationCount, a->formations[0],
			        a->formations[1]);
			exit(1);
		}

		TestFormation *form = NULL;

		for (int fi = 0; fi < cluster->formationCount; fi++)
		{
			if (streq(cluster->formations[fi].name, a->formations[0]))
			{
				form = &cluster->formations[fi];
				break;
			}
		}

		if (form == NULL)
		{
			fprintf(stderr,
			        "pgaftest: archiver \"%s\" attaches to formation "
			        "\"%s\", which is not declared in this cluster{} "
			        "block\n", a->name, a->formations[0]);
			exit(1);
		}

		if (form->nodeCount >= PGAF_MAX_NODES)
		{
			fprintf(stderr,
			        "pgaftest: too many nodes in formation \"%s\" (max %d)\n",
			        form->name, PGAF_MAX_NODES);
			exit(1);
		}

		TestNode *node = &form->nodes[form->nodeCount++];

		memset(node, 0, sizeof(*node));
		strlcpy(node->name, a->name, sizeof(node->name));
		node->kind = NODE_KIND_ARCHIVER;
		node->candidatePriority = 50;
		node->replicationQuorum = true;
		strlcpy(node->region, a->region, sizeof(node->region));
		strlcpy(node->replicationPassword, a->replicationPassword,
		        sizeof(node->replicationPassword));
		node->createDeferred = a->createDeferred;
		node->launchDeferred = a->launchDeferred;
	}

	cluster->archiverCount = 0;
}


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

	fold_archivers_into_formations(&spec->cluster);

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
