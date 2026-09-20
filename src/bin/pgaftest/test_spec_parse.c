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
#define YYLAST   684

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  128
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  72
/* YYNRULES -- Number of rules.  */
#define YYNRULES  236
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  419

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
     344,   348,   364,   368,   375,   382,   388,   395,   402,   409,
     422,   428,   438,   444,   454,   464,   470,   481,   480,   497,
     499,   508,   509,   510,   511,   512,   516,   521,   525,   531,
     533,   552,   553,   562,   579,   578,   586,   585,   593,   595,
     599,   604,   609,   613,   617,   621,   625,   631,   636,   640,
     645,   649,   653,   657,   661,   665,   670,   675,   679,   683,
     689,   695,   700,   705,   710,   714,   718,   724,   730,   744,
     765,   772,   783,   801,   816,   819,   827,   828,   829,   830,
     831,   832,   833,   834,   835,   836,   837,   838,   839,   840,
     841,   842,   843,   857,   864,   870,   877,   883,   890,   896,
     904,   910,   937,   937,   948,   963,   981,   982,   997,   999,
    1003,  1011,  1019,  1026,  1038,  1037,  1049,  1048,  1059,  1068,
    1077,  1091,  1099,  1113,  1128,  1145,  1169,  1198,  1228,  1234,
    1241,  1247,  1260,  1262,  1266,  1271,  1279,  1280,  1281,  1292,
    1300,  1308,  1316,  1334,  1352,  1368,  1375,  1379,  1385,  1398,
    1406,  1414,  1435,  1442,  1449,  1457,  1473,  1479,  1500,  1508,
    1523,  1537,  1541,  1547,  1553,  1579,  1613,  1619,  1640,  1657,
    1657,  1662,  1681,  1706,  1715,  1724,  1733,  1749,  1752,  1754,
    1776,  1777,  1778,  1779,  1780,  1781,  1782,  1783,  1784,  1785,
    1786,  1787,  1788,  1789,  1790,  1791,  1792,  1793,  1794,  1795,
    1796,  1804,  1805,  1816,  1817,  1825,  1826
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

#define YYTABLE_NINF (-136)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     101,   -60,   -54,   -54,   -98,  -208,    76,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   -54,
     -98,  -208,  -208,  -208,   513,  -208,  -208,    44,   -10,   -49,
     -41,   -32,   -28,   -12,     7,     0,   -61,     4,    11,    55,
     -56,    27,   -27,    37,    45,  -208,    31,   146,    33,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,    -5,   -18,    34,
      35,    36,  -208,    38,   -11,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,    39,    40,    63,    64,
      65,    66,   120,  -208,    15,    57,    41,    -3,  -208,   154,
    -208,    88,    20,    72,    73,  -208,  -208,    77,    90,    91,
      97,     8,     8,   103,     8,   -17,   104,   106,   107,   109,
      -2,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
     111,   112,  -208,  -208,  -208,  -208,   113,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   115,   118,
     116,    -6,   157,   -77,  -208,     2,     2,   615,  -208,  -208,
    -208,   149,   119,   224,   125,  -208,  -208,  -208,  -208,  -208,
     122,  -208,  -208,  -208,  -208,  -208,    17,   135,   136,  -208,
    -208,  -208,  -208,   234,   171,     1,   170,   151,   152,     2,
     153,   155,   200,   158,   -48,     2,     2,   159,   178,   240,
     -48,  -208,  -208,   261,   285,   175,   161,  -208,   163,  -208,
    -208,   206,   207,  -208,  -208,   317,  -208,  -208,  -208,  -208,
     233,   327,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
     337,   280,   239,   236,   -48,   238,   286,  -208,   361,   382,
     263,  -208,   -36,   242,   260,  -208,  -208,  -208,   -48,   -48,
     -48,   -48,  -208,  -208,   241,   265,  -208,  -208,   244,  -208,
    -208,     3,    -8,  -208,  -208,   268,   245,   270,   271,  -208,
    -208,   251,   298,   299,   -48,   -48,     2,   159,  -208,  -208,
     274,  -208,  -208,  -208,  -208,   275,  -208,   255,  -208,   256,
    -208,  -208,  -208,   257,   354,   -13,    13,  -208,  -208,   259,
     -48,   283,   284,  -208,   342,   342,  -208,  -208,   413,  -208,
     330,  -208,  -208,  -208,  -208,  -208,  -208,  -208,   358,  -208,
    -208,   332,  -208,   333,   334,   458,   -48,   -48,  -208,  -208,
    -208,   549,  -208,  -208,   429,   335,   -48,   336,   360,  -208,
     378,  -208,  -208,  -208,  -208,   356,   230,  -208,  -208,  -208,
     -48,   -48,   489,  -208,   362,   363,   364,  -208,  -208,  -208,
    -208,  -208,  -208,    89,    -7,  -208,  -208,   365,  -208,  -208,
     367,   368,   369,   371,   372,   110,   373,    21,   370,  -208,
    -208,  -208,  -208,  -208,   183,  -208,  -208,  -208,  -208,  -208,
    -208,   466,    23,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,   469,  -208,  -208,  -208
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   208,     0,     2,     4,     5,
       6,     7,     8,     9,   104,   100,   101,   231,   232,     0,
     207,     1,     3,    11,     0,   102,   209,     0,     0,     0,
       0,     0,   131,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   103,     0,     0,     0,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   122,
     115,   116,   117,   118,   119,   120,   121,    32,     0,     0,
       0,     0,    47,     0,     0,    20,    21,    10,    12,    19,
      13,    14,    17,    15,    16,    18,     0,     0,   124,   126,
     128,   130,     0,    62,    61,     0,     0,   176,   175,     0,
     180,   179,   182,     0,     0,   190,   191,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    41,    40,    44,    45,    46,    49,    22,    42,    43,
       0,     0,   123,   125,   127,   129,     0,   210,   211,   212,
     213,   214,   215,   216,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,     0,     0,
       0,   159,     0,   162,   158,     0,     0,     0,   173,   178,
     177,     0,     0,     0,     0,   186,   187,   192,   193,   194,
       0,    61,   197,   196,   202,   198,     0,     0,     0,    34,
      35,    36,    33,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   166,     0,     0,     0,     0,     0,
     166,   132,   133,     0,     0,     0,     0,   181,     0,   183,
     195,     0,     0,   203,   205,    37,    38,    54,    55,    53,
       0,     0,    59,    51,    52,    56,    50,    24,   188,   189,
       0,     0,     0,     0,   166,     0,     0,   150,     0,     0,
       0,   136,   166,     0,   163,   161,   160,   152,   166,   166,
     166,   166,   199,   201,     0,   184,   204,   206,     0,    57,
      58,     0,     0,   234,   233,     0,     0,     0,     0,   151,
     167,     0,   146,   144,   166,   166,     0,     0,   153,   164,
       0,   170,   169,   172,   171,     0,   174,     0,    39,     0,
      48,    63,    60,     0,     0,     0,     0,    23,    25,     0,
     166,     0,     0,   168,   138,   138,   149,   148,     0,   137,
       0,   104,   185,    63,    64,    26,    30,    31,     0,    27,
      28,   235,   154,     0,     0,     0,   166,   166,   135,   134,
     165,     0,    66,    68,     0,     0,   166,     0,     0,   141,
     139,   140,   147,   145,   200,     0,    65,    29,   236,   156,
     166,   166,     0,    68,     0,     0,     0,    70,    71,    72,
      73,    74,    75,     0,     0,    76,    81,     0,    82,    83,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    69,
     155,   157,   143,   142,     0,    91,    92,    93,    77,    80,
      78,     0,     0,    84,    88,    97,    89,    90,    95,    94,
      96,    85,    86,    87,    67,     0,    98,    99,    79
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -208,  -208,   493,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -110,   177,  -208,  -208,  -208,   138,  -208,  -208,
    -208,  -208,    19,   181,  -208,  -208,  -150,  -194,  -208,   188,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -207,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,
    -208,  -208,  -208,  -208,  -208,  -208,  -208,  -208,  -167,   484,
    -208,  -208
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     6,     7,     8,    23,    27,    78,    79,   196,   272,
     308,    80,    81,    82,    83,    84,    85,   126,   195,   235,
     236,   271,    95,   324,   302,   343,   355,   356,   389,     9,
      10,    11,    15,    24,    49,    50,   213,   162,   252,   336,
     350,    51,   315,   314,   163,   210,   254,   247,    52,    53,
      54,    55,    56,   101,    57,    58,    59,    60,    61,    62,
      63,   263,   295,    64,    65,    66,    12,    20,   164,    19,
     275,   346
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     215,   182,   183,   257,   185,   227,   228,    93,   299,   116,
     303,    93,    93,   251,   211,   327,   214,   229,   304,   305,
     230,   400,    16,    97,   193,   208,    17,    18,   245,   209,
     194,   246,   117,   118,   103,   104,   119,   279,    25,   306,
     245,   287,   256,   246,    13,   288,   259,   261,    67,   240,
      14,   291,   292,   293,   294,   248,   249,    68,   231,    69,
      70,    71,    72,    92,   328,    98,    73,   106,   107,   108,
     401,   109,   110,   274,   203,    88,    21,   316,   317,     1,
     212,   283,   285,    89,     2,     3,     4,     5,    74,    75,
      76,   204,    90,   319,   205,   206,    91,   307,   186,   187,
     188,    86,    87,   332,     1,   232,   121,   122,   300,     2,
       3,     4,     5,   128,   129,   165,   166,   398,   399,   120,
     169,   170,   173,   174,    96,   233,   234,   181,    99,   352,
     353,    94,   181,   221,   222,   100,   318,   329,   330,   359,
     408,   409,   136,   102,   111,   411,   412,   416,   417,    77,
     105,   339,   112,   390,   391,   113,   114,   115,   123,   124,
     125,   301,   127,   130,   131,   167,   171,   168,   351,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     132,   133,   134,   135,   172,   393,   175,   176,   364,   365,
     366,   177,   158,   367,   368,   369,   370,   371,   372,   373,
     374,   375,   376,   199,   178,   179,   377,   378,   379,   380,
     381,   180,   382,   383,   384,   385,   386,   184,   189,   190,
     387,   216,   191,   192,   207,   197,   198,   201,   159,   200,
     202,   160,   218,   217,   161,   364,   365,   366,   219,   220,
     367,   368,   369,   370,   371,   372,   373,   374,   375,   376,
     223,   224,   225,   377,   378,   379,   380,   381,   226,   382,
     383,   384,   385,   386,   237,   238,   239,   387,   243,   241,
     242,   253,   244,   250,   262,   264,   388,   265,   414,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   266,   267,   388,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   268,   269,   270,   276,   277,
     278,   280,   281,   286,   255,   289,   290,   296,   297,   298,
     309,   310,   311,   312,   313,  -135,  -134,   320,   322,   321,
     323,   325,   326,   331,   344,   258,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   333,   334,   260,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   335,   340,   345,   347,   348,   357,   358,   360,
     363,   273,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   361,   362,   282,   395,   396,   397,   402,
     403,   404,   415,   413,   405,   406,   407,   418,   410,    22,
     342,   394,   341,   337,    26,     0,   284,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   338,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,     0,
       0,    28,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   349,    29,    30,    31,    32,    33,     0,     0,
       0,     0,     0,     0,    34,    35,    36,    28,    37,    38,
      39,     0,    40,     0,     0,    41,    42,     0,    43,    44,
       0,     0,     0,   392,     0,     0,     0,     0,    45,    29,
      30,    31,    32,    33,    46,     0,    47,    48,     0,     0,
      34,    35,    36,     0,    37,    38,    39,     0,    40,     0,
       0,    41,    42,     0,    43,    44,     0,     0,     0,     0,
       0,     0,     0,     0,   354,     0,     0,     0,     0,     0,
      46,     0,    47,    48,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157
};

static const yytype_int16 yycheck[] =
{
     167,   111,   112,   210,   114,     4,     5,     4,     5,    14,
      18,     4,     4,   207,    12,    28,   166,    16,    26,    27,
      19,    28,     3,    84,    26,   102,   124,   125,    76,   106,
      32,    79,    37,    38,    90,    91,    41,   244,    19,    47,
      76,    77,   209,    79,   104,   252,   213,   214,     4,   199,
     104,   258,   259,   260,   261,   205,   206,    13,    57,    15,
      16,    17,    18,    75,    77,   126,    22,    94,    95,    96,
      77,    98,    99,   240,    80,   124,     0,   284,   285,     3,
      78,   248,   249,   124,     8,     9,    10,    11,    44,    45,
      46,    97,   124,   287,   100,   101,   124,   105,   115,   116,
     117,   111,   112,   310,     3,   104,   124,   125,   105,     8,
       9,    10,    11,   124,   125,   100,   101,    28,    29,   124,
     123,   124,   102,   103,   124,   124,   125,   124,   124,   336,
     337,   124,   124,   116,   117,   124,   286,   124,   125,   346,
      30,    31,    22,    88,   107,   124,   125,   124,   125,   105,
     123,   318,   107,   360,   361,   124,    10,   124,   124,   124,
     124,   271,   124,   124,   124,   108,    12,   126,   335,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
     127,   127,   127,   127,   106,   362,   124,   124,    15,    16,
      17,   124,    82,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,   100,   124,   124,    33,    34,    35,    36,
      37,   124,    39,    40,    41,    42,    43,   124,   124,   123,
      47,    82,   125,   124,    77,   124,   124,   119,   118,   124,
     124,   121,    18,   124,   124,    15,    16,    17,   123,   127,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
     125,   125,    28,    33,    34,    35,    36,    37,    97,    39,
      40,    41,    42,    43,   104,   124,   124,    47,    78,   126,
     125,   103,   124,   124,   109,   124,   103,   124,   105,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   125,   125,   103,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    38,   123,    30,    78,   120,
     124,   123,    76,   100,   124,   123,   106,   126,   103,   125,
     102,   126,   102,   102,   123,    77,    77,   103,   123,   104,
     124,   124,    28,   124,    26,   124,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,   124,   124,   124,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,   110,   123,   122,   122,   122,    28,   123,   123,
     104,   124,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,   123,   106,   124,   124,   124,   124,   124,
     123,   123,    26,   123,   125,   124,   124,    28,   125,     6,
     323,   363,   321,   315,    20,    -1,   124,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   124,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    -1,
      -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   124,    70,    71,    72,    73,    74,    -1,    -1,
      -1,    -1,    -1,    -1,    81,    82,    83,    48,    85,    86,
      87,    -1,    89,    -1,    -1,    92,    93,    -1,    95,    96,
      -1,    -1,    -1,   124,    -1,    -1,    -1,    -1,   105,    70,
      71,    72,    73,    74,   111,    -1,   113,   114,    -1,    -1,
      81,    82,    83,    -1,    85,    86,    87,    -1,    89,    -1,
      -1,    92,    93,    -1,    95,    96,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   105,    -1,    -1,    -1,    -1,    -1,
     111,    -1,   113,   114,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69
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
     105,   150,   152,    18,    26,    27,    47,   105,   138,   102,
     126,   102,   102,   123,   171,   170,   175,   175,   164,   165,
     103,   104,   123,   124,   151,   124,    28,    28,    77,   124,
     125,   124,   175,   124,   124,   110,   167,   167,   124,   196,
     123,   161,   151,   153,    26,   122,   199,   122,   122,   124,
     168,   196,   175,   175,   105,   154,   155,    28,   123,   175,
     123,   123,   106,   104,    15,    16,    17,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    33,    34,    35,
      36,    37,    39,    40,    41,    42,    43,    47,   103,   156,
     175,   175,   124,   196,   155,   124,   124,   124,    28,    29,
      28,    77,   124,   123,   123,   125,   124,   124,    30,    31,
     125,   124,   125,   123,   105,    26,   124,   125,    28
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   128,   129,   129,   130,   130,   130,   130,   130,   132,
     131,   133,   133,   134,   134,   134,   134,   134,   134,   134,
     134,   134,   136,   135,   137,   137,   138,   138,   138,   138,
     138,   138,   139,   139,   139,   139,   139,   139,   139,   139,
     140,   140,   141,   141,   142,   143,   143,   145,   144,   146,
     146,   147,   147,   147,   147,   147,   148,   148,   148,   149,
     149,   150,   150,   151,   153,   152,   154,   152,   155,   155,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     157,   158,   159,   160,   161,   161,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   163,   163,   163,   163,   163,   163,   163,
     163,   163,   164,   164,   165,   165,   166,   166,   167,   167,
     168,   168,   168,   168,   170,   169,   171,   169,   169,   169,
     169,   169,   169,   169,   169,   169,   169,   169,   172,   172,
     172,   172,   173,   173,   174,   174,   175,   175,   175,   176,
     176,   176,   176,   177,   178,   179,   179,   179,   179,   180,
     181,   181,   182,   182,   182,   182,   183,   183,   184,   184,
     185,   186,   186,   186,   186,   186,   187,   187,   188,   190,
     189,   191,   192,   193,   193,   193,   193,   194,   195,   195,
     196,   196,   196,   196,   196,   196,   196,   196,   196,   196,
     196,   196,   196,   196,   196,   196,   196,   196,   196,   196,
     196,   197,   197,   198,   198,   199,   199
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     0,     6,     0,     2,     2,     2,     2,     4,
       2,     2,     1,     3,     3,     3,     3,     4,     4,     6,
       2,     2,     2,     2,     2,     2,     2,     0,     6,     0,
       2,     1,     1,     1,     1,     1,     1,     2,     2,     0,
       2,     1,     1,     0,     0,     4,     0,     7,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     2,     2,     4,
       2,     1,     1,     1,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     3,     3,
       2,     2,     3,     3,     0,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     2,     3,     2,     3,     2,     3,
       2,     1,     1,     1,     4,     4,     1,     3,     0,     2,
       1,     1,     3,     3,     0,     9,     0,     9,     7,     7,
       5,     6,     5,     6,     8,    11,    10,    11,     1,     1,
       3,     3,     0,     2,     2,     4,     0,     2,     3,     6,
       6,     6,     6,     3,     6,     2,     2,     3,     3,     2,
       1,     3,     2,     4,     5,     7,     3,     3,     5,     5,
       2,     2,     3,     3,     3,     4,     3,     3,     3,     0,
       5,     5,     3,     4,     5,     4,     5,     2,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     0,     2
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
#line 1793 "test_spec_parse.c"
    break;

  case 20: /* cluster_item: T_BIND_SOURCE  */
#line 266 "test_spec_parse.y"
                        { current_spec->cluster.bindSource = true; }
#line 1799 "test_spec_parse.c"
    break;

  case 21: /* cluster_item: T_LEGACY_STARTUP  */
#line 267 "test_spec_parse.y"
                           { current_spec->cluster.legacyStartup = true; }
#line 1805 "test_spec_parse.c"
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
#line 1824 "test_spec_parse.c"
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
#line 1842 "test_spec_parse.c"
    break;

  case 27: /* archiver_opt: T_REGION T_IDENT  */
#line 328 "test_spec_parse.y"
        {
		strlcpy(current_archiver->region, (yyvsp[0].str), sizeof(current_archiver->region));
		free((yyvsp[0].str));
	}
#line 1851 "test_spec_parse.c"
    break;

  case 28: /* archiver_opt: T_REGION T_STRING  */
#line 333 "test_spec_parse.y"
        {
		strlcpy(current_archiver->region, (yyvsp[0].str), sizeof(current_archiver->region));
		free((yyvsp[0].str));
	}
#line 1860 "test_spec_parse.c"
    break;

  case 29: /* archiver_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 338 "test_spec_parse.y"
        {
		/* bare "create and launch deferred" = both gates, matching
		 * node_opt's own identical form */
		current_archiver->createDeferred = true;
		current_archiver->launchDeferred = true;
	}
#line 1871 "test_spec_parse.c"
    break;

  case 30: /* archiver_opt: T_LAUNCH T_DEFERRED  */
#line 345 "test_spec_parse.y"
        {
		current_archiver->launchDeferred = true;
	}
#line 1879 "test_spec_parse.c"
    break;

  case 31: /* archiver_opt: T_CREATE T_DEFERRED  */
#line 349 "test_spec_parse.y"
        {
		current_archiver->createDeferred = true;
	}
#line 1887 "test_spec_parse.c"
    break;

  case 32: /* monitor_line: T_MONITOR  */
#line 365 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
	}
#line 1895 "test_spec_parse.c"
    break;

  case 33: /* monitor_line: T_MONITOR T_DEBIAN_CLUSTER T_IDENT  */
#line 369 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[0].str));
	}
#line 1906 "test_spec_parse.c"
    break;

  case 34: /* monitor_line: T_MONITOR T_IMAGE_TARGET T_IDENT  */
#line 376 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[0].str));
	}
#line 1917 "test_spec_parse.c"
    break;

  case 35: /* monitor_line: T_MONITOR T_PORT T_INTEGER  */
#line 383 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[0].ival);
	}
#line 1927 "test_spec_parse.c"
    break;

  case 36: /* monitor_line: T_MONITOR T_PASSWORD T_STRING  */
#line 389 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[0].str));
	}
#line 1938 "test_spec_parse.c"
    break;

  case 37: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED  */
#line 396 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1949 "test_spec_parse.c"
    break;

  case 38: /* monitor_line: T_MONITOR T_IDENT T_INITIALLY T_STOPPED  */
#line 403 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1960 "test_spec_parse.c"
    break;

  case 39: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING  */
#line 410 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-4].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-4].str));
		/* password for second monitor not yet stored */
		free((yyvsp[0].str));
	}
#line 1973 "test_spec_parse.c"
    break;

  case 40: /* image_line: T_IMAGE T_STRING  */
#line 423 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1983 "test_spec_parse.c"
    break;

  case 41: /* image_line: T_IMAGE T_IDENT  */
#line 429 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1993 "test_spec_parse.c"
    break;

  case 42: /* extension_version_line: T_EXTENSION_VERSION T_IDENT  */
#line 439 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2003 "test_spec_parse.c"
    break;

  case 43: /* extension_version_line: T_EXTENSION_VERSION T_STRING  */
#line 445 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 2013 "test_spec_parse.c"
    break;

  case 44: /* ssl_line: T_SSL T_IDENT  */
#line 455 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl, (yyvsp[0].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[0].str));
	}
#line 2023 "test_spec_parse.c"
    break;

  case 45: /* auth_line: T_AUTH T_IDENT  */
#line 465 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2033 "test_spec_parse.c"
    break;

  case 46: /* auth_line: T_AUTH_METHOD T_IDENT  */
#line 471 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 2043 "test_spec_parse.c"
    break;

  case 47: /* $@3: %empty  */
#line 481 "test_spec_parse.y"
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
#line 2061 "test_spec_parse.c"
    break;

  case 51: /* bare_name: T_IDENT  */
#line 508 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2067 "test_spec_parse.c"
    break;

  case 52: /* bare_name: T_STRING  */
#line 509 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 2073 "test_spec_parse.c"
    break;

  case 53: /* bare_name: T_AUTH  */
#line 510 "test_spec_parse.y"
                    { (yyval.str) = strdup("auth"); }
#line 2079 "test_spec_parse.c"
    break;

  case 54: /* bare_name: T_MONITOR  */
#line 511 "test_spec_parse.y"
                    { (yyval.str) = strdup("monitor"); }
#line 2085 "test_spec_parse.c"
    break;

  case 55: /* bare_name: T_NODE  */
#line 512 "test_spec_parse.y"
                    { (yyval.str) = strdup("node"); }
#line 2091 "test_spec_parse.c"
    break;

  case 56: /* formation_opt: bare_name  */
#line 517 "test_spec_parse.y"
        {
		strlcpy(current_formation->name, (yyvsp[0].str), sizeof(current_formation->name));
		free((yyvsp[0].str));
	}
#line 2100 "test_spec_parse.c"
    break;

  case 57: /* formation_opt: T_NUM_SYNC T_INTEGER  */
#line 522 "test_spec_parse.y"
        {
		current_formation->numSync = (yyvsp[0].ival);
	}
#line 2108 "test_spec_parse.c"
    break;

  case 58: /* formation_opt: T_FS_SECONDARY T_FALSE  */
#line 526 "test_spec_parse.y"
        {
		current_formation->disableSecondary = true;
	}
#line 2116 "test_spec_parse.c"
    break;

  case 61: /* node_name: T_IDENT  */
#line 552 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 2122 "test_spec_parse.c"
    break;

  case 62: /* node_name: T_MONITOR  */
#line 553 "test_spec_parse.y"
                     { (yyval.str) = strdup("monitor"); }
#line 2128 "test_spec_parse.c"
    break;

  case 63: /* init_node_slot: %empty  */
#line 562 "test_spec_parse.y"
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
#line 2145 "test_spec_parse.c"
    break;

  case 64: /* $@4: %empty  */
#line 579 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2154 "test_spec_parse.c"
    break;

  case 66: /* $@5: %empty  */
#line 586 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2163 "test_spec_parse.c"
    break;

  case 70: /* node_opt: T_COORDINATOR  */
#line 600 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
#line 2172 "test_spec_parse.c"
    break;

  case 71: /* node_opt: T_WORKER  */
#line 605 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
#line 2181 "test_spec_parse.c"
    break;

  case 72: /* node_opt: T_ARCHIVER  */
#line 610 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_ARCHIVER;
	}
#line 2189 "test_spec_parse.c"
    break;

  case 73: /* node_opt: T_ASYNC  */
#line 614 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2197 "test_spec_parse.c"
    break;

  case 74: /* node_opt: T_NO_MONITOR  */
#line 618 "test_spec_parse.y"
        {
		current_node->noMonitor = true;
	}
#line 2205 "test_spec_parse.c"
    break;

  case 75: /* node_opt: T_SUSPENDED  */
#line 622 "test_spec_parse.y"
        {
		current_node->suspended = true;
	}
#line 2213 "test_spec_parse.c"
    break;

  case 76: /* node_opt: T_DEFERRED  */
#line 626 "test_spec_parse.y"
        {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2223 "test_spec_parse.c"
    break;

  case 77: /* node_opt: T_LAUNCH T_DEFERRED  */
#line 632 "test_spec_parse.y"
        {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	}
#line 2232 "test_spec_parse.c"
    break;

  case 78: /* node_opt: T_CREATE T_DEFERRED  */
#line 637 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
	}
#line 2240 "test_spec_parse.c"
    break;

  case 79: /* node_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 641 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2249 "test_spec_parse.c"
    break;

  case 80: /* node_opt: T_LAUNCH T_IMMEDIATE  */
#line 646 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2257 "test_spec_parse.c"
    break;

  case 81: /* node_opt: T_IMMEDIATE  */
#line 650 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2265 "test_spec_parse.c"
    break;

  case 82: /* node_opt: T_LISTEN  */
#line 654 "test_spec_parse.y"
        {
		current_node->listen = true;
	}
#line 2273 "test_spec_parse.c"
    break;

  case 83: /* node_opt: T_CITUS_SECONDARY  */
#line 658 "test_spec_parse.y"
        {
		current_node->citusSecondary = true;
	}
#line 2281 "test_spec_parse.c"
    break;

  case 84: /* node_opt: T_CANDIDATE_PRIORITY T_INTEGER  */
#line 662 "test_spec_parse.y"
        {
		current_node->candidatePriority = (yyvsp[0].ival);
	}
#line 2289 "test_spec_parse.c"
    break;

  case 85: /* node_opt: T_REGION T_IDENT  */
#line 666 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2298 "test_spec_parse.c"
    break;

  case 86: /* node_opt: T_REGION T_STRING  */
#line 671 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2307 "test_spec_parse.c"
    break;

  case 87: /* node_opt: T_GROUP T_INTEGER  */
#line 676 "test_spec_parse.y"
        {
		current_node->group = (yyvsp[0].ival);
	}
#line 2315 "test_spec_parse.c"
    break;

  case 88: /* node_opt: T_PORT T_INTEGER  */
#line 680 "test_spec_parse.y"
        {
		current_node->pgPort = (yyvsp[0].ival);
	}
#line 2323 "test_spec_parse.c"
    break;

  case 89: /* node_opt: T_CITUS_CLUSTER_NAME T_IDENT  */
#line 684 "test_spec_parse.y"
        {
		strlcpy(current_node->citusClusterName, (yyvsp[0].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[0].str));
	}
#line 2333 "test_spec_parse.c"
    break;

  case 90: /* node_opt: T_DEBIAN_CLUSTER T_IDENT  */
#line 690 "test_spec_parse.y"
        {
		strlcpy(current_node->debianCluster, (yyvsp[0].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[0].str));
	}
#line 2343 "test_spec_parse.c"
    break;

  case 91: /* node_opt: T_SSL T_IDENT  */
#line 696 "test_spec_parse.y"
        {
		strlcpy(current_node->ssl, (yyvsp[0].str), sizeof(current_node->ssl));
		free((yyvsp[0].str));
	}
#line 2352 "test_spec_parse.c"
    break;

  case 92: /* node_opt: T_AUTH T_IDENT  */
#line 701 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2361 "test_spec_parse.c"
    break;

  case 93: /* node_opt: T_AUTH_METHOD T_IDENT  */
#line 706 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2370 "test_spec_parse.c"
    break;

  case 94: /* node_opt: T_REPLICATION_QUORUM T_TRUE  */
#line 711 "test_spec_parse.y"
        {
		current_node->replicationQuorum = true;
	}
#line 2378 "test_spec_parse.c"
    break;

  case 95: /* node_opt: T_REPLICATION_QUORUM T_FALSE  */
#line 715 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2386 "test_spec_parse.c"
    break;

  case 96: /* node_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 719 "test_spec_parse.y"
        {
		strlcpy(current_node->replicationPassword, (yyvsp[0].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[0].str));
	}
#line 2396 "test_spec_parse.c"
    break;

  case 97: /* node_opt: T_MONITOR_PASSWORD T_STRING  */
#line 725 "test_spec_parse.y"
        {
		strlcpy(current_node->monitorPassword, (yyvsp[0].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[0].str));
	}
#line 2406 "test_spec_parse.c"
    break;

  case 98: /* node_opt: T_VOLUME T_IDENT T_IDENT  */
#line 731 "test_spec_parse.y"
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
#line 2424 "test_spec_parse.c"
    break;

  case 99: /* node_opt: T_VOLUME T_IDENT T_STRING  */
#line 745 "test_spec_parse.y"
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
#line 2442 "test_spec_parse.c"
    break;

  case 100: /* setup_block: T_SETUP cmd_block  */
#line 766 "test_spec_parse.y"
        {
		current_spec->setup = (yyvsp[0].step);
	}
#line 2450 "test_spec_parse.c"
    break;

  case 101: /* teardown_block: T_TEARDOWN cmd_block  */
#line 773 "test_spec_parse.y"
        {
		current_spec->teardown = (yyvsp[0].step);
	}
#line 2458 "test_spec_parse.c"
    break;

  case 102: /* named_step: T_STEP ident_or_string cmd_block  */
#line 784 "test_spec_parse.y"
        {
		TestStep *s = (yyvsp[0].step);
		strncpy(s->name, (yyvsp[-1].str), sizeof(s->name) - 1);
		free((yyvsp[-1].str));
		register_step(current_spec, s);
	}
#line 2469 "test_spec_parse.c"
    break;

  case 103: /* cmd_block: T_LBRACE cmd_list T_RBRACE  */
#line 802 "test_spec_parse.y"
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
#line 2484 "test_spec_parse.c"
    break;

  case 104: /* cmd_list: %empty  */
#line 816 "test_spec_parse.y"
        {
		(yyval.step) = make_step("");
	}
#line 2492 "test_spec_parse.c"
    break;

  case 105: /* cmd_list: cmd_list step_cmd  */
#line 820 "test_spec_parse.y"
        {
		if ((yyvsp[0].cmd)) append_cmd((yyvsp[-1].step), (yyvsp[0].cmd));
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2501 "test_spec_parse.c"
    break;

  case 106: /* step_cmd: exec_cmd  */
#line 827 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2507 "test_spec_parse.c"
    break;

  case 107: /* step_cmd: wait_cmd  */
#line 828 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2513 "test_spec_parse.c"
    break;

  case 108: /* step_cmd: assert_cmd  */
#line 829 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2519 "test_spec_parse.c"
    break;

  case 109: /* step_cmd: sql_cmd  */
#line 830 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2525 "test_spec_parse.c"
    break;

  case 110: /* step_cmd: let_cmd  */
#line 831 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2531 "test_spec_parse.c"
    break;

  case 111: /* step_cmd: expect_cmd  */
#line 832 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2537 "test_spec_parse.c"
    break;

  case 112: /* step_cmd: promote_cmd  */
#line 833 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2543 "test_spec_parse.c"
    break;

  case 113: /* step_cmd: perform_cmd  */
#line 834 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2549 "test_spec_parse.c"
    break;

  case 114: /* step_cmd: network_cmd  */
#line 835 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2555 "test_spec_parse.c"
    break;

  case 115: /* step_cmd: sleep_cmd  */
#line 836 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2561 "test_spec_parse.c"
    break;

  case 116: /* step_cmd: compose_cmd  */
#line 837 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2567 "test_spec_parse.c"
    break;

  case 117: /* step_cmd: postgres_ctl_cmd  */
#line 838 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2573 "test_spec_parse.c"
    break;

  case 118: /* step_cmd: fsm_step_cmd  */
#line 839 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2579 "test_spec_parse.c"
    break;

  case 119: /* step_cmd: stays_while_cmd  */
#line 840 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2585 "test_spec_parse.c"
    break;

  case 120: /* step_cmd: set_monitor_cmd  */
#line 841 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2591 "test_spec_parse.c"
    break;

  case 121: /* step_cmd: logs_cmd  */
#line 842 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2597 "test_spec_parse.c"
    break;

  case 122: /* step_cmd: nodeini_cmd  */
#line 843 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2603 "test_spec_parse.c"
    break;

  case 123: /* exec_cmd: T_EXEC T_IDENT T_SHELL_ARGS  */
#line 858 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2614 "test_spec_parse.c"
    break;

  case 124: /* exec_cmd: T_EXEC T_IDENT  */
#line 865 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2624 "test_spec_parse.c"
    break;

  case 125: /* exec_cmd: T_EXEC_FAILS T_IDENT T_SHELL_ARGS  */
#line 871 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2635 "test_spec_parse.c"
    break;

  case 126: /* exec_cmd: T_EXEC_FAILS T_IDENT  */
#line 878 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2645 "test_spec_parse.c"
    break;

  case 127: /* exec_cmd: T_RUN T_IDENT T_SHELL_ARGS  */
#line 884 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2656 "test_spec_parse.c"
    break;

  case 128: /* exec_cmd: T_RUN T_IDENT  */
#line 891 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2666 "test_spec_parse.c"
    break;

  case 129: /* exec_cmd: T_PG_AUTOCTL T_IDENT T_SHELL_ARGS  */
#line 897 "test_spec_parse.y"
        {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2678 "test_spec_parse.c"
    break;

  case 130: /* exec_cmd: T_PG_AUTOCTL T_IDENT  */
#line 905 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[0].str));
	}
#line 2688 "test_spec_parse.c"
    break;

  case 131: /* exec_cmd: T_PG_AUTOCTL  */
#line 911 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	}
#line 2696 "test_spec_parse.c"
    break;

  case 134: /* wait_multi_condition: T_IDENT T_STATE state_op fsm_state  */
#line 949 "test_spec_parse.y"
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
#line 2715 "test_spec_parse.c"
    break;

  case 135: /* wait_multi_condition: T_IDENT T_STATE state_op T_IDENT  */
#line 964 "test_spec_parse.y"
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
#line 2734 "test_spec_parse.c"
    break;

  case 140: /* pass_state_list: fsm_state  */
#line 1004 "test_spec_parse.y"
        {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2746 "test_spec_parse.c"
    break;

  case 141: /* pass_state_list: T_IDENT  */
#line 1012 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2758 "test_spec_parse.c"
    break;

  case 142: /* pass_state_list: pass_state_list T_COMMA fsm_state  */
#line 1020 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2769 "test_spec_parse.c"
    break;

  case 143: /* pass_state_list: pass_state_list T_COMMA T_IDENT  */
#line 1027 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2781 "test_spec_parse.c"
    break;

  case 144: /* $@6: %empty  */
#line 1038 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); }
#line 2790 "test_spec_parse.c"
    break;

  case 145: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state $@6 opt_passing_through opt_timeout  */
#line 1043 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2800 "test_spec_parse.c"
    break;

  case 146: /* $@7: %empty  */
#line 1049 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); free((yyvsp[0].str)); }
#line 2809 "test_spec_parse.c"
    break;

  case 147: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT $@7 opt_passing_through opt_timeout  */
#line 1054 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2819 "test_spec_parse.c"
    break;

  case 148: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1060 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2832 "test_spec_parse.c"
    break;

  case 149: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1069 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2845 "test_spec_parse.c"
    break;

  case 150: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout  */
#line 1078 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-2].str));
	}
#line 2856 "test_spec_parse.c"
    break;

  case 151: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_REPLAYS T_IDENT opt_timeout  */
#line 1092 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2868 "test_spec_parse.c"
    break;

  case 152: /* wait_cmd: T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout  */
#line 1100 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2878 "test_spec_parse.c"
    break;

  case 153: /* wait_cmd: T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout  */
#line 1114 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2888 "test_spec_parse.c"
    break;

  case 154: /* wait_cmd: T_WAIT T_UNTIL T_SQL T_IDENT T_BLOCK T_IS T_BLOCK opt_timeout  */
#line 1129 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service,  (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,     (yyvsp[-3].str), sizeof((yyval.cmd)->args));
		strlcpy((yyval.cmd)->expected, (yyvsp[-1].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2901 "test_spec_parse.c"
    break;

  case 155: /* wait_cmd: T_WAIT T_UNTIL T_WAL T_SEGMENT T_STRING T_ARCHIVED T_IN T_IDENT T_SLASH T_INTEGER opt_timeout  */
#line 1146 "test_spec_parse.y"
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
#line 2916 "test_spec_parse.c"
    break;

  case 156: /* wait_cmd: T_WAIT T_UNTIL T_ARCHIVER T_STATE state_op wait_state_name T_IN T_IDENT opt_wait_group opt_timeout  */
#line 1170 "test_spec_parse.y"
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
#line 2941 "test_spec_parse.c"
    break;

  case 157: /* wait_cmd: T_WAIT T_UNTIL T_BASEBACKUP T_IDENT T_IS T_IDENT T_IN T_IDENT T_SLASH T_INTEGER opt_timeout  */
#line 1199 "test_spec_parse.y"
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
#line 2966 "test_spec_parse.c"
    break;

  case 158: /* state_name_list: fsm_state  */
#line 1229 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2976 "test_spec_parse.c"
    break;

  case 159: /* state_name_list: T_IDENT  */
#line 1235 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2987 "test_spec_parse.c"
    break;

  case 160: /* state_name_list: state_name_list T_COMMA fsm_state  */
#line 1242 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2997 "test_spec_parse.c"
    break;

  case 161: /* state_name_list: state_name_list T_COMMA T_IDENT  */
#line 1248 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 3008 "test_spec_parse.c"
    break;

  case 164: /* group_items: T_GROUP T_INTEGER  */
#line 1267 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 3017 "test_spec_parse.c"
    break;

  case 165: /* group_items: group_items T_COMMA T_GROUP T_INTEGER  */
#line 1272 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 3026 "test_spec_parse.c"
    break;

  case 166: /* opt_timeout: %empty  */
#line 1279 "test_spec_parse.y"
                                       { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; }
#line 3032 "test_spec_parse.c"
    break;

  case 167: /* opt_timeout: T_TIMEOUT T_INTEGER  */
#line 1280 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 3038 "test_spec_parse.c"
    break;

  case 168: /* opt_timeout: T_WITH T_TIMEOUT T_INTEGER  */
#line 1281 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 3044 "test_spec_parse.c"
    break;

  case 169: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout  */
#line 1293 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3056 "test_spec_parse.c"
    break;

  case 170: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout  */
#line 1301 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3068 "test_spec_parse.c"
    break;

  case 171: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1309 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 3080 "test_spec_parse.c"
    break;

  case 172: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1317 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 3092 "test_spec_parse.c"
    break;

  case 173: /* sql_cmd: T_SQL T_IDENT T_BLOCK  */
#line 1335 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3103 "test_spec_parse.c"
    break;

  case 174: /* let_cmd: T_LET T_IDENT T_EQUALS T_SQL T_IDENT T_BLOCK  */
#line 1353 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LET);
		strlcpy((yyval.cmd)->state,   (yyvsp[-4].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-4].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3115 "test_spec_parse.c"
    break;

  case 175: /* expect_cmd: T_EXPECT T_BLOCK  */
#line 1369 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[0].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[0].str));
	}
#line 3126 "test_spec_parse.c"
    break;

  case 176: /* expect_cmd: T_EXPECT T_ERROR  */
#line 1376 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	}
#line 3134 "test_spec_parse.c"
    break;

  case 177: /* expect_cmd: T_EXPECT T_ERROR T_IDENT  */
#line 1380 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[0].str), sizeof((yyval.cmd)->state));
		free((yyvsp[0].str));
	}
#line 3144 "test_spec_parse.c"
    break;

  case 178: /* expect_cmd: T_EXPECT T_ERROR T_INTEGER  */
#line 1386 "test_spec_parse.y"
        {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[0].ival));
	}
#line 3154 "test_spec_parse.c"
    break;

  case 179: /* promote_cmd: T_PROMOTE promote_list  */
#line 1399 "test_spec_parse.y"
        {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	}
#line 3163 "test_spec_parse.c"
    break;

  case 180: /* promote_list: T_IDENT  */
#line 1407 "test_spec_parse.y"
        {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3175 "test_spec_parse.c"
    break;

  case 181: /* promote_list: promote_list T_COMMA T_IDENT  */
#line 1415 "test_spec_parse.y"
        {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 3186 "test_spec_parse.c"
    break;

  case 182: /* perform_cmd: T_PERFORM T_FAILOVER  */
#line 1436 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3197 "test_spec_parse.c"
    break;

  case 183: /* perform_cmd: T_PERFORM T_FAILOVER T_GROUP T_INTEGER  */
#line 1443 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 3208 "test_spec_parse.c"
    break;

  case 184: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT  */
#line 1450 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[0].str));
	}
#line 3220 "test_spec_parse.c"
    break;

  case 185: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT T_GROUP T_INTEGER  */
#line 1458 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[-2].str));
	}
#line 3232 "test_spec_parse.c"
    break;

  case 186: /* network_cmd: T_NETWORK T_DISCONNECT T_IDENT  */
#line 1474 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3242 "test_spec_parse.c"
    break;

  case 187: /* network_cmd: T_NETWORK T_CONNECT T_IDENT  */
#line 1480 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3252 "test_spec_parse.c"
    break;

  case 188: /* nodeini_cmd: T_NODEINI T_SET T_IDENT T_IDENT T_IDENT  */
#line 1501 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3264 "test_spec_parse.c"
    break;

  case 189: /* nodeini_cmd: T_NODEINI T_GET T_IDENT T_IDENT T_IDENT  */
#line 1509 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3276 "test_spec_parse.c"
    break;

  case 190: /* sleep_cmd: T_SLEEP T_INTEGER  */
#line 1524 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
	}
#line 3285 "test_spec_parse.c"
    break;

  case 191: /* compose_cmd: T_COMPOSE T_DOWN  */
#line 1538 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	}
#line 3293 "test_spec_parse.c"
    break;

  case 192: /* compose_cmd: T_COMPOSE T_START T_IDENT  */
#line 1542 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3303 "test_spec_parse.c"
    break;

  case 193: /* compose_cmd: T_COMPOSE T_STOP T_IDENT  */
#line 1548 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3313 "test_spec_parse.c"
    break;

  case 194: /* compose_cmd: T_COMPOSE T_KILL T_IDENT  */
#line 1554 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3323 "test_spec_parse.c"
    break;

  case 195: /* compose_cmd: T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS  */
#line 1580 "test_spec_parse.y"
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
#line 3350 "test_spec_parse.c"
    break;

  case 196: /* postgres_ctl_cmd: T_STOP T_POSTGRES node_name  */
#line 1614 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3360 "test_spec_parse.c"
    break;

  case 197: /* postgres_ctl_cmd: T_START T_POSTGRES node_name  */
#line 1620 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3370 "test_spec_parse.c"
    break;

  case 198: /* fsm_step_cmd: T_FSM T_STEP node_name  */
#line 1641 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3380 "test_spec_parse.c"
    break;

  case 199: /* $@8: %empty  */
#line 1657 "test_spec_parse.y"
                { pgaf_next_brace_is_while = 1; }
#line 3386 "test_spec_parse.c"
    break;

  case 200: /* while_body: T_WHILE $@8 T_LBRACE cmd_list T_RBRACE  */
#line 1658 "test_spec_parse.y"
        { (yyval.step) = (yyvsp[-1].step); }
#line 3392 "test_spec_parse.c"
    break;

  case 201: /* stays_while_cmd: T_ASSERT node_name T_STAYS fsm_state while_body  */
#line 1663 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[0].step)) ? (yyvsp[0].step)->commands : NULL;
		free((yyvsp[-3].str));
	}
#line 3404 "test_spec_parse.c"
    break;

  case 202: /* set_monitor_cmd: T_SET T_IDENT T_IDENT  */
#line 1682 "test_spec_parse.y"
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
#line 3421 "test_spec_parse.c"
    break;

  case 203: /* logs_cmd: T_LOGS T_IDENT T_CONTAINS T_STRING  */
#line 1707 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3434 "test_spec_parse.c"
    break;

  case 204: /* logs_cmd: T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING  */
#line 1716 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3447 "test_spec_parse.c"
    break;

  case 205: /* logs_cmd: T_LOGS T_IDENT T_MATCHES T_STRING  */
#line 1725 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3460 "test_spec_parse.c"
    break;

  case 206: /* logs_cmd: T_LOGS T_IDENT T_NOT T_MATCHES T_STRING  */
#line 1734 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3473 "test_spec_parse.c"
    break;

  case 209: /* sequence_names: sequence_names ident_or_string  */
#line 1755 "test_spec_parse.y"
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
#line 3489 "test_spec_parse.c"
    break;

  case 210: /* fsm_state: T_FS_INIT  */
#line 1776 "test_spec_parse.y"
                                   { (yyval.str) = "init"; }
#line 3495 "test_spec_parse.c"
    break;

  case 211: /* fsm_state: T_FS_SINGLE  */
#line 1777 "test_spec_parse.y"
                                   { (yyval.str) = "single"; }
#line 3501 "test_spec_parse.c"
    break;

  case 212: /* fsm_state: T_FS_PRIMARY  */
#line 1778 "test_spec_parse.y"
                                   { (yyval.str) = "primary"; }
#line 3507 "test_spec_parse.c"
    break;

  case 213: /* fsm_state: T_FS_WAIT_PRIMARY  */
#line 1779 "test_spec_parse.y"
                                   { (yyval.str) = "wait_primary"; }
#line 3513 "test_spec_parse.c"
    break;

  case 214: /* fsm_state: T_FS_WAIT_STANDBY  */
#line 1780 "test_spec_parse.y"
                                   { (yyval.str) = "wait_standby"; }
#line 3519 "test_spec_parse.c"
    break;

  case 215: /* fsm_state: T_FS_DEMOTED  */
#line 1781 "test_spec_parse.y"
                                   { (yyval.str) = "demoted"; }
#line 3525 "test_spec_parse.c"
    break;

  case 216: /* fsm_state: T_FS_DEMOTE_TIMEOUT  */
#line 1782 "test_spec_parse.y"
                                   { (yyval.str) = "demote_timeout"; }
#line 3531 "test_spec_parse.c"
    break;

  case 217: /* fsm_state: T_FS_DRAINING  */
#line 1783 "test_spec_parse.y"
                                   { (yyval.str) = "draining"; }
#line 3537 "test_spec_parse.c"
    break;

  case 218: /* fsm_state: T_FS_SECONDARY  */
#line 1784 "test_spec_parse.y"
                                   { (yyval.str) = "secondary"; }
#line 3543 "test_spec_parse.c"
    break;

  case 219: /* fsm_state: T_FS_CATCHINGUP  */
#line 1785 "test_spec_parse.y"
                                   { (yyval.str) = "catchingup"; }
#line 3549 "test_spec_parse.c"
    break;

  case 220: /* fsm_state: T_FS_PREP_PROMOTION  */
#line 1786 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_promotion"; }
#line 3555 "test_spec_parse.c"
    break;

  case 221: /* fsm_state: T_FS_STOP_REPLICATION  */
#line 1787 "test_spec_parse.y"
                                   { (yyval.str) = "stop_replication"; }
#line 3561 "test_spec_parse.c"
    break;

  case 222: /* fsm_state: T_FS_MAINTENANCE  */
#line 1788 "test_spec_parse.y"
                                   { (yyval.str) = "maintenance"; }
#line 3567 "test_spec_parse.c"
    break;

  case 223: /* fsm_state: T_FS_JOIN_PRIMARY  */
#line 1789 "test_spec_parse.y"
                                   { (yyval.str) = "join_primary"; }
#line 3573 "test_spec_parse.c"
    break;

  case 224: /* fsm_state: T_FS_APPLY_SETTINGS  */
#line 1790 "test_spec_parse.y"
                                   { (yyval.str) = "apply_settings"; }
#line 3579 "test_spec_parse.c"
    break;

  case 225: /* fsm_state: T_FS_PREPARE_MAINTENANCE  */
#line 1791 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_maintenance"; }
#line 3585 "test_spec_parse.c"
    break;

  case 226: /* fsm_state: T_FS_WAIT_MAINTENANCE  */
#line 1792 "test_spec_parse.y"
                                   { (yyval.str) = "wait_maintenance"; }
#line 3591 "test_spec_parse.c"
    break;

  case 227: /* fsm_state: T_FS_REPORT_LSN  */
#line 1793 "test_spec_parse.y"
                                   { (yyval.str) = "report_lsn"; }
#line 3597 "test_spec_parse.c"
    break;

  case 228: /* fsm_state: T_FS_FAST_FORWARD  */
#line 1794 "test_spec_parse.y"
                                   { (yyval.str) = "fast_forward"; }
#line 3603 "test_spec_parse.c"
    break;

  case 229: /* fsm_state: T_FS_JOIN_SECONDARY  */
#line 1795 "test_spec_parse.y"
                                   { (yyval.str) = "join_secondary"; }
#line 3609 "test_spec_parse.c"
    break;

  case 230: /* fsm_state: T_FS_DROPPED  */
#line 1796 "test_spec_parse.y"
                                   { (yyval.str) = "dropped"; }
#line 3615 "test_spec_parse.c"
    break;

  case 231: /* ident_or_string: T_IDENT  */
#line 1804 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3621 "test_spec_parse.c"
    break;

  case 232: /* ident_or_string: T_STRING  */
#line 1805 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3627 "test_spec_parse.c"
    break;

  case 233: /* wait_state_name: fsm_state  */
#line 1816 "test_spec_parse.y"
                     { (yyval.str) = strdup((yyvsp[0].str)); }
#line 3633 "test_spec_parse.c"
    break;

  case 234: /* wait_state_name: T_IDENT  */
#line 1817 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 3639 "test_spec_parse.c"
    break;

  case 235: /* opt_wait_group: %empty  */
#line 1825 "test_spec_parse.y"
                               { (yyval.ival) = -1; }
#line 3645 "test_spec_parse.c"
    break;

  case 236: /* opt_wait_group: T_SLASH T_INTEGER  */
#line 1826 "test_spec_parse.y"
                               { (yyval.ival) = (yyvsp[0].ival); }
#line 3651 "test_spec_parse.c"
    break;


#line 3655 "test_spec_parse.c"

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

#line 1829 "test_spec_parse.y"


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
