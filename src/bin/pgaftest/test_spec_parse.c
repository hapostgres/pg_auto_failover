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


#line 215 "test_spec_parse.c"

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
  YYSYMBOL_T_PROMOTE = 85,                 /* T_PROMOTE  */
  YYSYMBOL_T_PERFORM = 86,                 /* T_PERFORM  */
  YYSYMBOL_T_FAILOVER = 87,                /* T_FAILOVER  */
  YYSYMBOL_T_NETWORK = 88,                 /* T_NETWORK  */
  YYSYMBOL_T_DISCONNECT = 89,              /* T_DISCONNECT  */
  YYSYMBOL_T_CONNECT = 90,                 /* T_CONNECT  */
  YYSYMBOL_T_SLEEP = 91,                   /* T_SLEEP  */
  YYSYMBOL_T_COMPOSE = 92,                 /* T_COMPOSE  */
  YYSYMBOL_T_DOWN = 93,                    /* T_DOWN  */
  YYSYMBOL_T_START = 94,                   /* T_START  */
  YYSYMBOL_T_STOP = 95,                    /* T_STOP  */
  YYSYMBOL_T_STOPPED = 96,                 /* T_STOPPED  */
  YYSYMBOL_T_KILL = 97,                    /* T_KILL  */
  YYSYMBOL_T_INJECT = 98,                  /* T_INJECT  */
  YYSYMBOL_T_STATE = 99,                   /* T_STATE  */
  YYSYMBOL_T_ASSIGNED_STATE = 100,         /* T_ASSIGNED_STATE  */
  YYSYMBOL_T_IN = 101,                     /* T_IN  */
  YYSYMBOL_T_GROUP = 102,                  /* T_GROUP  */
  YYSYMBOL_T_LBRACE = 103,                 /* T_LBRACE  */
  YYSYMBOL_T_RBRACE = 104,                 /* T_RBRACE  */
  YYSYMBOL_T_COMMA = 105,                  /* T_COMMA  */
  YYSYMBOL_T_POSTGRES = 106,               /* T_POSTGRES  */
  YYSYMBOL_T_STAYS = 107,                  /* T_STAYS  */
  YYSYMBOL_T_WHILE = 108,                  /* T_WHILE  */
  YYSYMBOL_T_THROUGH = 109,                /* T_THROUGH  */
  YYSYMBOL_T_SET = 110,                    /* T_SET  */
  YYSYMBOL_T_GET = 111,                    /* T_GET  */
  YYSYMBOL_T_FSM = 112,                    /* T_FSM  */
  YYSYMBOL_T_LOGS = 113,                   /* T_LOGS  */
  YYSYMBOL_T_NOT = 114,                    /* T_NOT  */
  YYSYMBOL_T_CONTAINS = 115,               /* T_CONTAINS  */
  YYSYMBOL_T_MATCHES = 116,                /* T_MATCHES  */
  YYSYMBOL_T_INTEGER = 117,                /* T_INTEGER  */
  YYSYMBOL_T_IDENT = 118,                  /* T_IDENT  */
  YYSYMBOL_T_STRING = 119,                 /* T_STRING  */
  YYSYMBOL_T_BLOCK = 120,                  /* T_BLOCK  */
  YYSYMBOL_T_SHELL_ARGS = 121,             /* T_SHELL_ARGS  */
  YYSYMBOL_YYACCEPT = 122,                 /* $accept  */
  YYSYMBOL_spec = 123,                     /* spec  */
  YYSYMBOL_spec_item = 124,                /* spec_item  */
  YYSYMBOL_cluster_block = 125,            /* cluster_block  */
  YYSYMBOL_126_1 = 126,                    /* $@1  */
  YYSYMBOL_cluster_item_list = 127,        /* cluster_item_list  */
  YYSYMBOL_cluster_item = 128,             /* cluster_item  */
  YYSYMBOL_monitor_line = 129,             /* monitor_line  */
  YYSYMBOL_image_line = 130,               /* image_line  */
  YYSYMBOL_extension_version_line = 131,   /* extension_version_line  */
  YYSYMBOL_ssl_line = 132,                 /* ssl_line  */
  YYSYMBOL_auth_line = 133,                /* auth_line  */
  YYSYMBOL_formation_block = 134,          /* formation_block  */
  YYSYMBOL_135_2 = 135,                    /* $@2  */
  YYSYMBOL_formation_opt_list = 136,       /* formation_opt_list  */
  YYSYMBOL_bare_name = 137,                /* bare_name  */
  YYSYMBOL_formation_opt = 138,            /* formation_opt  */
  YYSYMBOL_node_list = 139,                /* node_list  */
  YYSYMBOL_node_name = 140,                /* node_name  */
  YYSYMBOL_init_node_slot = 141,           /* init_node_slot  */
  YYSYMBOL_node_line = 142,                /* node_line  */
  YYSYMBOL_143_3 = 143,                    /* $@3  */
  YYSYMBOL_144_4 = 144,                    /* $@4  */
  YYSYMBOL_node_opt_list = 145,            /* node_opt_list  */
  YYSYMBOL_node_opt = 146,                 /* node_opt  */
  YYSYMBOL_setup_block = 147,              /* setup_block  */
  YYSYMBOL_teardown_block = 148,           /* teardown_block  */
  YYSYMBOL_named_step = 149,               /* named_step  */
  YYSYMBOL_cmd_block = 150,                /* cmd_block  */
  YYSYMBOL_cmd_list = 151,                 /* cmd_list  */
  YYSYMBOL_step_cmd = 152,                 /* step_cmd  */
  YYSYMBOL_exec_cmd = 153,                 /* exec_cmd  */
  YYSYMBOL_state_op = 154,                 /* state_op  */
  YYSYMBOL_wait_multi_condition = 155,     /* wait_multi_condition  */
  YYSYMBOL_wait_multi_condition_list = 156, /* wait_multi_condition_list  */
  YYSYMBOL_opt_passing_through = 157,      /* opt_passing_through  */
  YYSYMBOL_pass_state_list = 158,          /* pass_state_list  */
  YYSYMBOL_wait_cmd = 159,                 /* wait_cmd  */
  YYSYMBOL_160_5 = 160,                    /* $@5  */
  YYSYMBOL_161_6 = 161,                    /* $@6  */
  YYSYMBOL_state_name_list = 162,          /* state_name_list  */
  YYSYMBOL_opt_in_group = 163,             /* opt_in_group  */
  YYSYMBOL_group_items = 164,              /* group_items  */
  YYSYMBOL_opt_timeout = 165,              /* opt_timeout  */
  YYSYMBOL_assert_cmd = 166,               /* assert_cmd  */
  YYSYMBOL_sql_cmd = 167,                  /* sql_cmd  */
  YYSYMBOL_expect_cmd = 168,               /* expect_cmd  */
  YYSYMBOL_promote_cmd = 169,              /* promote_cmd  */
  YYSYMBOL_promote_list = 170,             /* promote_list  */
  YYSYMBOL_perform_cmd = 171,              /* perform_cmd  */
  YYSYMBOL_network_cmd = 172,              /* network_cmd  */
  YYSYMBOL_nodeini_cmd = 173,              /* nodeini_cmd  */
  YYSYMBOL_sleep_cmd = 174,                /* sleep_cmd  */
  YYSYMBOL_compose_cmd = 175,              /* compose_cmd  */
  YYSYMBOL_postgres_ctl_cmd = 176,         /* postgres_ctl_cmd  */
  YYSYMBOL_fsm_step_cmd = 177,             /* fsm_step_cmd  */
  YYSYMBOL_while_body = 178,               /* while_body  */
  YYSYMBOL_179_7 = 179,                    /* $@7  */
  YYSYMBOL_stays_while_cmd = 180,          /* stays_while_cmd  */
  YYSYMBOL_set_monitor_cmd = 181,          /* set_monitor_cmd  */
  YYSYMBOL_logs_cmd = 182,                 /* logs_cmd  */
  YYSYMBOL_sequence_block = 183,           /* sequence_block  */
  YYSYMBOL_sequence_names = 184,           /* sequence_names  */
  YYSYMBOL_fsm_state = 185,                /* fsm_state  */
  YYSYMBOL_ident_or_string = 186           /* ident_or_string  */
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
#define YYLAST   633

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  122
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  65
/* YYNRULES -- Number of rules.  */
#define YYNRULES  215
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  357

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   376


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
     115,   116,   117,   118,   119,   120,   121
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   216,   216,   217,   221,   222,   223,   224,   225,   238,
     237,   247,   249,   253,   254,   255,   256,   257,   258,   259,
     260,   273,   277,   284,   291,   297,   304,   311,   318,   331,
     337,   347,   353,   363,   373,   379,   390,   389,   406,   408,
     417,   418,   419,   420,   421,   425,   430,   434,   440,   442,
     461,   462,   471,   488,   487,   495,   494,   502,   504,   508,
     513,   518,   522,   526,   530,   536,   541,   545,   550,   554,
     558,   562,   566,   570,   575,   580,   584,   588,   594,   600,
     605,   610,   615,   619,   623,   629,   637,   643,   657,   678,
     685,   696,   714,   729,   732,   740,   741,   742,   743,   744,
     745,   746,   747,   748,   749,   750,   751,   752,   753,   754,
     755,   769,   776,   782,   789,   795,   802,   808,   816,   822,
     849,   849,   860,   875,   893,   894,   909,   911,   915,   923,
     931,   938,   950,   949,   961,   960,   971,   980,   989,  1003,
    1011,  1025,  1040,  1046,  1053,  1059,  1072,  1074,  1078,  1083,
    1091,  1092,  1093,  1104,  1112,  1120,  1128,  1146,  1161,  1168,
    1172,  1178,  1191,  1199,  1207,  1228,  1235,  1242,  1250,  1266,
    1272,  1293,  1301,  1316,  1330,  1334,  1340,  1346,  1372,  1406,
    1412,  1433,  1450,  1450,  1455,  1474,  1499,  1508,  1517,  1526,
    1542,  1545,  1547,  1569,  1570,  1571,  1572,  1573,  1574,  1575,
    1576,  1577,  1578,  1579,  1580,  1581,  1582,  1583,  1584,  1585,
    1586,  1587,  1588,  1589,  1597,  1598
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
  "T_NODEINI", "T_FS_INIT", "T_FS_SINGLE", "T_FS_PRIMARY",
  "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
  "T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
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
  "cluster_item", "monitor_line", "image_line", "extension_version_line",
  "ssl_line", "auth_line", "formation_block", "$@2", "formation_opt_list",
  "bare_name", "formation_opt", "node_list", "node_name", "init_node_slot",
  "node_line", "$@3", "$@4", "node_opt_list", "node_opt", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "$@5", "$@6", "state_name_list", "opt_in_group",
  "group_items", "opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd",
  "promote_cmd", "promote_list", "perform_cmd", "network_cmd",
  "nodeini_cmd", "sleep_cmd", "compose_cmd", "postgres_ctl_cmd",
  "fsm_step_cmd", "while_body", "$@7", "stays_while_cmd",
  "set_monitor_cmd", "logs_cmd", "sequence_block", "sequence_names",
  "fsm_state", "ident_or_string", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-179)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-124)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     123,   -68,   -61,   -61,  -102,  -179,    85,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,   -61,
    -102,  -179,  -179,  -179,   454,  -179,  -179,     8,   -43,   -79,
     -21,   -18,    -9,   -25,    -1,    -7,   -64,    -2,   -33,   -12,
       7,   -22,    22,    33,  -179,    36,   108,    37,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,    -3,   -72,    38,    44,    64,
    -179,   -32,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,    65,    66,     4,    69,    75,    77,   152,  -179,
     -10,    90,   102,   -26,  -179,  -179,   118,     0,   106,   107,
    -179,  -179,   110,   133,   134,   135,     5,     5,   136,     5,
     -57,   138,   109,   139,   141,    30,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,   142,   143,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,   -51,   180,   -73,  -179,     6,     6,   564,  -179,  -179,
    -179,   144,   245,   147,  -179,  -179,  -179,  -179,  -179,   145,
    -179,  -179,  -179,  -179,  -179,    -8,   146,   148,  -179,  -179,
    -179,  -179,   241,   173,     3,   174,   175,   176,   -49,     6,
       6,   177,   194,   181,   -49,  -179,  -179,   222,   251,   189,
    -179,   203,  -179,  -179,   179,   204,  -179,  -179,   285,  -179,
    -179,  -179,  -179,   207,   296,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,   -49,   209,   252,  -179,   292,   321,   228,  -179,
     -14,   212,   225,  -179,  -179,  -179,   -49,   -49,   -49,   -49,
    -179,  -179,   229,  -179,  -179,   213,  -179,  -179,     1,  -179,
    -179,   216,   257,   258,   -49,   -49,     6,   177,  -179,  -179,
     234,  -179,  -179,  -179,  -179,   235,   220,  -179,   221,  -179,
    -179,  -179,  -179,   253,   253,  -179,  -179,   362,  -179,   246,
    -179,  -179,  -179,  -179,   391,   -49,   -49,  -179,  -179,  -179,
     499,  -179,  -179,  -179,   259,  -179,  -179,  -179,  -179,   262,
     153,   432,  -179,   248,   249,   250,  -179,  -179,  -179,  -179,
    -179,    76,   -13,  -179,  -179,   273,  -179,  -179,   275,   276,
     277,   279,   280,    84,   281,    17,   282,   278,  -179,  -179,
    -179,   125,  -179,  -179,  -179,  -179,  -179,  -179,   369,    19,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,   372,  -179,  -179,  -179
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   191,     0,     2,     4,     5,
       6,     7,     8,     9,    93,    89,    90,   214,   215,     0,
     190,     1,     3,    11,     0,    91,   192,     0,     0,     0,
       0,     0,   119,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    92,     0,     0,     0,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   110,   103,   104,
     105,   106,   107,   108,   109,    21,     0,     0,     0,     0,
      36,     0,    19,    20,    10,    12,    13,    14,    17,    15,
      16,    18,     0,     0,   112,   114,   116,   118,     0,    51,
      50,     0,     0,   159,   158,   163,   162,   165,     0,     0,
     173,   174,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    30,    29,    33,    34,
      35,    38,    31,    32,     0,     0,   111,   113,   115,   117,
     193,   194,   195,   196,   197,   198,   199,   200,   201,   202,
     203,   204,   205,   206,   207,   208,   209,   210,   211,   212,
     213,   143,     0,   146,   142,     0,     0,     0,   157,   161,
     160,     0,     0,     0,   169,   170,   175,   176,   177,     0,
      50,   180,   179,   185,   181,     0,     0,     0,    23,    24,
      25,    22,     0,     0,     0,     0,     0,     0,   150,     0,
       0,     0,     0,     0,   150,   120,   121,     0,     0,     0,
     164,     0,   166,   178,     0,     0,   186,   188,    26,    27,
      43,    44,    42,     0,     0,    48,    40,    41,    45,    39,
     171,   172,   150,     0,     0,   138,     0,     0,     0,   124,
     150,     0,   147,   145,   144,   140,   150,   150,   150,   150,
     182,   184,   167,   187,   189,     0,    46,    47,     0,   139,
     151,     0,   134,   132,   150,   150,     0,     0,   141,   148,
       0,   154,   153,   156,   155,     0,     0,    28,     0,    37,
      52,    49,   152,   126,   126,   137,   136,     0,   125,     0,
      93,   168,    52,    53,     0,   150,   150,   123,   122,   149,
       0,    55,    57,   129,   127,   128,   135,   133,   183,     0,
      54,     0,    57,     0,     0,     0,    59,    60,    61,    62,
      63,     0,     0,    64,    69,     0,    70,    71,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    58,   131,
     130,     0,    79,    80,    81,    65,    68,    66,     0,     0,
      72,    76,    86,    77,    78,    83,    82,    84,    73,    74,
      85,    75,    56,     0,    87,    88,    67
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -179,  -179,   396,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -105,   121,
    -179,  -179,  -179,   103,  -179,  -179,  -179,  -179,    12,   124,
    -179,  -179,  -146,  -178,  -179,   132,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -156,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,  -179,
    -179,  -179,  -179,  -157,   387
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     6,     7,     8,    23,    27,    75,    76,    77,    78,
      79,    80,    81,   121,   184,   218,   219,   248,    91,   283,
     271,   292,   299,   300,   328,     9,    10,    11,    15,    24,
      48,    49,   197,   152,   230,   285,   294,    50,   274,   273,
     153,   194,   232,   225,    51,    52,    53,    54,    96,    55,
      56,    57,    58,    59,    60,    61,   241,   265,    62,    63,
      64,    12,    20,   154,    19
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     199,   171,   172,    89,   174,    89,   268,   210,   211,    89,
     198,   111,    65,   229,   337,    16,    17,    18,   195,   212,
      93,    66,   213,    67,    68,    69,    70,   223,   192,   187,
     224,    25,   193,   112,   113,    13,   234,   114,   235,    84,
     237,   239,    14,   226,   227,   188,   116,   117,   189,   190,
      88,    71,    72,    73,    97,   182,    94,   175,   176,   177,
     214,   183,   223,   257,   338,   224,   249,    82,    83,   253,
     255,   101,   102,   103,   258,   104,   105,    98,    99,   278,
     261,   262,   263,   264,   196,    21,   122,   123,     1,   155,
     156,   159,   160,     2,     3,     4,     5,    85,   275,   276,
      86,   162,   163,   335,   336,   269,   215,   204,   205,    87,
     277,    92,    74,   345,   346,   115,    95,    90,   109,   170,
     288,   216,   217,   170,   100,   126,     1,   295,   106,   296,
     297,     2,     3,     4,     5,   348,   349,   354,   355,   107,
     303,   304,   305,   270,   330,   306,   307,   308,   309,   310,
     311,   312,   313,   314,   108,   110,   118,   315,   316,   317,
     318,   319,   119,   320,   321,   322,   323,   324,   303,   304,
     305,   325,   326,   306,   307,   308,   309,   310,   311,   312,
     313,   314,   120,   124,   125,   315,   316,   317,   318,   319,
     127,   320,   321,   322,   323,   324,   128,   157,   129,   325,
     326,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   158,   161,   164,   165,   179,   327,   166,   352,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   167,   168,   169,   173,   327,   178,   191,   180,   181,
     185,   186,   200,   201,   202,   206,   203,   207,   208,   209,
     151,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   220,   221,   222,   228,   231,   240,   243,   233,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   242,   245,   244,   246,   247,   250,   256,   251,   259,
     260,   266,   267,   272,  -123,  -122,   279,   281,   280,   282,
     236,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   284,   289,   301,   302,   332,   333,   334,   238,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   339,   340,   341,   353,   351,   342,   343,   344,   356,
     347,   350,    22,   291,   290,   331,   286,    26,     0,     0,
     252,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,     0,     0,     0,     0,     0,     0,     0,   254,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     287,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,    28,     0,     0,     0,     0,     0,     0,   293,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    29,    30,    31,    32,    33,     0,
       0,     0,     0,     0,     0,    34,    35,    36,     0,    37,
      38,     0,    39,     0,     0,    40,    41,    28,    42,    43,
     329,     0,     0,     0,     0,     0,     0,     0,    44,     0,
       0,     0,     0,     0,    45,     0,    46,    47,     0,    29,
      30,    31,    32,    33,     0,     0,     0,     0,     0,     0,
      34,    35,    36,     0,    37,    38,     0,    39,     0,     0,
      40,    41,     0,    42,    43,     0,     0,     0,     0,     0,
       0,     0,     0,   298,     0,     0,     0,     0,     0,    45,
       0,    46,    47,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150
};

static const yytype_int16 yycheck[] =
{
     157,   106,   107,     4,   109,     4,     5,     4,     5,     4,
     156,    14,     4,   191,    27,     3,   118,   119,    12,    16,
      84,    13,    19,    15,    16,    17,    18,    76,   101,    80,
      79,    19,   105,    36,    37,   103,   193,    40,   194,   118,
     197,   198,   103,   189,   190,    96,   118,   119,    99,   100,
      75,    43,    44,    45,    87,    25,   120,   114,   115,   116,
      57,    31,    76,    77,    77,    79,   222,   110,   111,   226,
     227,    93,    94,    95,   230,    97,    98,    89,    90,   257,
     236,   237,   238,   239,    78,     0,   118,   119,     3,    99,
     100,   117,   118,     8,     9,    10,    11,   118,   254,   255,
     118,   101,   102,    27,    28,   104,   103,   115,   116,   118,
     256,   118,   104,    29,    30,   118,   118,   118,    10,   118,
     277,   118,   119,   118,   117,   121,     3,   284,   106,   285,
     286,     8,     9,    10,    11,   118,   119,   118,   119,   106,
      15,    16,    17,   248,   301,    20,    21,    22,    23,    24,
      25,    26,    27,    28,   118,   118,   118,    32,    33,    34,
      35,    36,   118,    38,    39,    40,    41,    42,    15,    16,
      17,    46,    47,    20,    21,    22,    23,    24,    25,    26,
      27,    28,   118,   118,   118,    32,    33,    34,    35,    36,
     121,    38,    39,    40,    41,    42,   121,   107,   121,    46,
      47,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,   120,   105,   118,   118,   117,   102,   118,   104,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   118,   118,   118,   118,   102,   118,    77,   119,   118,
     118,   118,   118,    18,   117,   119,   121,   119,    27,    96,
     118,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,   118,   118,   118,   118,   102,   108,   119,   118,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   118,    37,   119,   117,    29,   117,    99,    76,   117,
     105,   102,   119,   117,    77,    77,   102,   117,   103,   118,
     118,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,   109,   117,   105,   103,   118,   118,   118,   118,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   118,   117,   117,    25,   117,   119,   118,   118,    27,
     119,   119,     6,   282,   280,   302,   274,    20,    -1,    -1,
     118,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   118,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     118,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    48,    -1,    -1,    -1,    -1,    -1,    -1,   118,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    70,    71,    72,    73,    74,    -1,
      -1,    -1,    -1,    -1,    -1,    81,    82,    83,    -1,    85,
      86,    -1,    88,    -1,    -1,    91,    92,    48,    94,    95,
     118,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   104,    -1,
      -1,    -1,    -1,    -1,   110,    -1,   112,   113,    -1,    70,
      71,    72,    73,    74,    -1,    -1,    -1,    -1,    -1,    -1,
      81,    82,    83,    -1,    85,    86,    -1,    88,    -1,    -1,
      91,    92,    -1,    94,    95,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   104,    -1,    -1,    -1,    -1,    -1,   110,
      -1,   112,   113,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   123,   124,   125,   147,
     148,   149,   183,   103,   103,   150,   150,   118,   119,   186,
     184,     0,   124,   126,   151,   150,   186,   127,    48,    70,
      71,    72,    73,    74,    81,    82,    83,    85,    86,    88,
      91,    92,    94,    95,   104,   110,   112,   113,   152,   153,
     159,   166,   167,   168,   169,   171,   172,   173,   174,   175,
     176,   177,   180,   181,   182,     4,    13,    15,    16,    17,
      18,    43,    44,    45,   104,   128,   129,   130,   131,   132,
     133,   134,   110,   111,   118,   118,   118,   118,    75,     4,
     118,   140,   118,    84,   120,   118,   170,    87,    89,    90,
     117,    93,    94,    95,    97,    98,   106,   106,   118,    10,
     118,    14,    36,    37,    40,   118,   118,   119,   118,   118,
     118,   135,   118,   119,   118,   118,   121,   121,   121,   121,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   118,   155,   162,   185,    99,   100,   107,   120,   117,
     118,   105,   101,   102,   118,   118,   118,   118,   118,   118,
     118,   140,   140,   118,   140,   114,   115,   116,   118,   117,
     119,   118,    25,    31,   136,   118,   118,    80,    96,    99,
     100,    77,   101,   105,   163,    12,    78,   154,   154,   185,
     118,    18,   117,   121,   115,   116,   119,   119,    27,    96,
       4,     5,    16,    19,    57,   103,   118,   119,   137,   138,
     118,   118,   118,    76,    79,   165,   154,   154,   118,   155,
     156,   102,   164,   118,   185,   165,   118,   185,   118,   185,
     108,   178,   118,   119,   119,    37,   117,    29,   139,   165,
     117,    76,   118,   185,   118,   185,    99,    77,   165,   117,
     105,   165,   165,   165,   165,   179,   102,   119,     5,   104,
     140,   142,   117,   161,   160,   165,   165,   154,   155,   102,
     103,   117,   118,   141,   109,   157,   157,   118,   185,   117,
     151,   141,   143,   118,   158,   185,   165,   165,   104,   144,
     145,   105,   103,    15,    16,    17,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    32,    33,    34,    35,    36,
      38,    39,    40,    41,    42,    46,    47,   102,   146,   118,
     185,   145,   118,   118,   118,    27,    28,    27,    77,   118,
     117,   117,   119,   118,   118,    29,    30,   119,   118,   119,
     119,   117,   104,    25,   118,   119,    27
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   122,   123,   123,   124,   124,   124,   124,   124,   126,
     125,   127,   127,   128,   128,   128,   128,   128,   128,   128,
     128,   129,   129,   129,   129,   129,   129,   129,   129,   130,
     130,   131,   131,   132,   133,   133,   135,   134,   136,   136,
     137,   137,   137,   137,   137,   138,   138,   138,   139,   139,
     140,   140,   141,   143,   142,   144,   142,   145,   145,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   147,
     148,   149,   150,   151,   151,   152,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   152,
     152,   153,   153,   153,   153,   153,   153,   153,   153,   153,
     154,   154,   155,   155,   156,   156,   157,   157,   158,   158,
     158,   158,   160,   159,   161,   159,   159,   159,   159,   159,
     159,   159,   162,   162,   162,   162,   163,   163,   164,   164,
     165,   165,   165,   166,   166,   166,   166,   167,   168,   168,
     168,   168,   169,   170,   170,   171,   171,   171,   171,   172,
     172,   173,   173,   174,   175,   175,   175,   175,   175,   176,
     176,   177,   179,   178,   180,   181,   182,   182,   182,   182,
     183,   184,   184,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,   185,   185,   185,   185,   185,   185,   185,
     185,   185,   185,   185,   186,   186
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     3,     3,     4,     4,     6,     2,
       2,     2,     2,     2,     2,     2,     0,     6,     0,     2,
       1,     1,     1,     1,     1,     1,     2,     2,     0,     2,
       1,     1,     0,     0,     4,     0,     7,     0,     2,     1,
       1,     1,     1,     1,     1,     2,     2,     4,     2,     1,
       1,     1,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     3,     3,     2,
       2,     3,     3,     0,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     2,     3,     2,     3,     2,     3,     2,     1,
       1,     1,     4,     4,     1,     3,     0,     2,     1,     1,
       3,     3,     0,     9,     0,     9,     7,     7,     5,     6,
       5,     6,     1,     1,     3,     3,     0,     2,     2,     4,
       0,     2,     3,     6,     6,     6,     6,     3,     2,     2,
       3,     3,     2,     1,     3,     2,     4,     5,     7,     3,
       3,     5,     5,     2,     2,     3,     3,     3,     4,     3,
       3,     3,     0,     5,     5,     3,     4,     5,     4,     5,
       2,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1
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
#line 238 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	}
#line 1737 "test_spec_parse.c"
    break;

  case 19: /* cluster_item: T_BIND_SOURCE  */
#line 259 "test_spec_parse.y"
                        { current_spec->cluster.bindSource = true; }
#line 1743 "test_spec_parse.c"
    break;

  case 20: /* cluster_item: T_LEGACY_STARTUP  */
#line 260 "test_spec_parse.y"
                           { current_spec->cluster.legacyStartup = true; }
#line 1749 "test_spec_parse.c"
    break;

  case 21: /* monitor_line: T_MONITOR  */
#line 274 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
	}
#line 1757 "test_spec_parse.c"
    break;

  case 22: /* monitor_line: T_MONITOR T_DEBIAN_CLUSTER T_IDENT  */
#line 278 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[0].str));
	}
#line 1768 "test_spec_parse.c"
    break;

  case 23: /* monitor_line: T_MONITOR T_IMAGE_TARGET T_IDENT  */
#line 285 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[0].str));
	}
#line 1779 "test_spec_parse.c"
    break;

  case 24: /* monitor_line: T_MONITOR T_PORT T_INTEGER  */
#line 292 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[0].ival);
	}
#line 1789 "test_spec_parse.c"
    break;

  case 25: /* monitor_line: T_MONITOR T_PASSWORD T_STRING  */
#line 298 "test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[0].str));
	}
#line 1800 "test_spec_parse.c"
    break;

  case 26: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED  */
#line 305 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1811 "test_spec_parse.c"
    break;

  case 27: /* monitor_line: T_MONITOR T_IDENT T_INITIALLY T_STOPPED  */
#line 312 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1822 "test_spec_parse.c"
    break;

  case 28: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING  */
#line 319 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-4].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-4].str));
		/* password for second monitor not yet stored */
		free((yyvsp[0].str));
	}
#line 1835 "test_spec_parse.c"
    break;

  case 29: /* image_line: T_IMAGE T_STRING  */
#line 332 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1845 "test_spec_parse.c"
    break;

  case 30: /* image_line: T_IMAGE T_IDENT  */
#line 338 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1855 "test_spec_parse.c"
    break;

  case 31: /* extension_version_line: T_EXTENSION_VERSION T_IDENT  */
#line 348 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 1865 "test_spec_parse.c"
    break;

  case 32: /* extension_version_line: T_EXTENSION_VERSION T_STRING  */
#line 354 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 1875 "test_spec_parse.c"
    break;

  case 33: /* ssl_line: T_SSL T_IDENT  */
#line 364 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl, (yyvsp[0].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[0].str));
	}
#line 1885 "test_spec_parse.c"
    break;

  case 34: /* auth_line: T_AUTH T_IDENT  */
#line 374 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 1895 "test_spec_parse.c"
    break;

  case 35: /* auth_line: T_AUTH_METHOD T_IDENT  */
#line 380 "test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 1905 "test_spec_parse.c"
    break;

  case 36: /* $@2: %empty  */
#line 390 "test_spec_parse.y"
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
#line 1923 "test_spec_parse.c"
    break;

  case 40: /* bare_name: T_IDENT  */
#line 417 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 1929 "test_spec_parse.c"
    break;

  case 41: /* bare_name: T_STRING  */
#line 418 "test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 1935 "test_spec_parse.c"
    break;

  case 42: /* bare_name: T_AUTH  */
#line 419 "test_spec_parse.y"
                    { (yyval.str) = strdup("auth"); }
#line 1941 "test_spec_parse.c"
    break;

  case 43: /* bare_name: T_MONITOR  */
#line 420 "test_spec_parse.y"
                    { (yyval.str) = strdup("monitor"); }
#line 1947 "test_spec_parse.c"
    break;

  case 44: /* bare_name: T_NODE  */
#line 421 "test_spec_parse.y"
                    { (yyval.str) = strdup("node"); }
#line 1953 "test_spec_parse.c"
    break;

  case 45: /* formation_opt: bare_name  */
#line 426 "test_spec_parse.y"
        {
		strlcpy(current_formation->name, (yyvsp[0].str), sizeof(current_formation->name));
		free((yyvsp[0].str));
	}
#line 1962 "test_spec_parse.c"
    break;

  case 46: /* formation_opt: T_NUM_SYNC T_INTEGER  */
#line 431 "test_spec_parse.y"
        {
		current_formation->numSync = (yyvsp[0].ival);
	}
#line 1970 "test_spec_parse.c"
    break;

  case 47: /* formation_opt: T_FS_SECONDARY T_FALSE  */
#line 435 "test_spec_parse.y"
        {
		current_formation->disableSecondary = true;
	}
#line 1978 "test_spec_parse.c"
    break;

  case 50: /* node_name: T_IDENT  */
#line 461 "test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 1984 "test_spec_parse.c"
    break;

  case 51: /* node_name: T_MONITOR  */
#line 462 "test_spec_parse.y"
                     { (yyval.str) = strdup("monitor"); }
#line 1990 "test_spec_parse.c"
    break;

  case 52: /* init_node_slot: %empty  */
#line 471 "test_spec_parse.y"
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
#line 2007 "test_spec_parse.c"
    break;

  case 53: /* $@3: %empty  */
#line 488 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2016 "test_spec_parse.c"
    break;

  case 55: /* $@4: %empty  */
#line 495 "test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 2025 "test_spec_parse.c"
    break;

  case 59: /* node_opt: T_COORDINATOR  */
#line 509 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
#line 2034 "test_spec_parse.c"
    break;

  case 60: /* node_opt: T_WORKER  */
#line 514 "test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
#line 2043 "test_spec_parse.c"
    break;

  case 61: /* node_opt: T_ASYNC  */
#line 519 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2051 "test_spec_parse.c"
    break;

  case 62: /* node_opt: T_NO_MONITOR  */
#line 523 "test_spec_parse.y"
        {
		current_node->noMonitor = true;
	}
#line 2059 "test_spec_parse.c"
    break;

  case 63: /* node_opt: T_SUSPENDED  */
#line 527 "test_spec_parse.y"
        {
		current_node->suspended = true;
	}
#line 2067 "test_spec_parse.c"
    break;

  case 64: /* node_opt: T_DEFERRED  */
#line 531 "test_spec_parse.y"
        {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2077 "test_spec_parse.c"
    break;

  case 65: /* node_opt: T_LAUNCH T_DEFERRED  */
#line 537 "test_spec_parse.y"
        {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	}
#line 2086 "test_spec_parse.c"
    break;

  case 66: /* node_opt: T_CREATE T_DEFERRED  */
#line 542 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
	}
#line 2094 "test_spec_parse.c"
    break;

  case 67: /* node_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 546 "test_spec_parse.y"
        {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2103 "test_spec_parse.c"
    break;

  case 68: /* node_opt: T_LAUNCH T_IMMEDIATE  */
#line 551 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2111 "test_spec_parse.c"
    break;

  case 69: /* node_opt: T_IMMEDIATE  */
#line 555 "test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2119 "test_spec_parse.c"
    break;

  case 70: /* node_opt: T_LISTEN  */
#line 559 "test_spec_parse.y"
        {
		current_node->listen = true;
	}
#line 2127 "test_spec_parse.c"
    break;

  case 71: /* node_opt: T_CITUS_SECONDARY  */
#line 563 "test_spec_parse.y"
        {
		current_node->citusSecondary = true;
	}
#line 2135 "test_spec_parse.c"
    break;

  case 72: /* node_opt: T_CANDIDATE_PRIORITY T_INTEGER  */
#line 567 "test_spec_parse.y"
        {
		current_node->candidatePriority = (yyvsp[0].ival);
	}
#line 2143 "test_spec_parse.c"
    break;

  case 73: /* node_opt: T_REGION T_IDENT  */
#line 571 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2152 "test_spec_parse.c"
    break;

  case 74: /* node_opt: T_REGION T_STRING  */
#line 576 "test_spec_parse.y"
        {
		strlcpy(current_node->region, (yyvsp[0].str), sizeof(current_node->region));
		free((yyvsp[0].str));
	}
#line 2161 "test_spec_parse.c"
    break;

  case 75: /* node_opt: T_GROUP T_INTEGER  */
#line 581 "test_spec_parse.y"
        {
		current_node->group = (yyvsp[0].ival);
	}
#line 2169 "test_spec_parse.c"
    break;

  case 76: /* node_opt: T_PORT T_INTEGER  */
#line 585 "test_spec_parse.y"
        {
		current_node->pgPort = (yyvsp[0].ival);
	}
#line 2177 "test_spec_parse.c"
    break;

  case 77: /* node_opt: T_CITUS_CLUSTER_NAME T_IDENT  */
#line 589 "test_spec_parse.y"
        {
		strlcpy(current_node->citusClusterName, (yyvsp[0].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[0].str));
	}
#line 2187 "test_spec_parse.c"
    break;

  case 78: /* node_opt: T_DEBIAN_CLUSTER T_IDENT  */
#line 595 "test_spec_parse.y"
        {
		strlcpy(current_node->debianCluster, (yyvsp[0].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[0].str));
	}
#line 2197 "test_spec_parse.c"
    break;

  case 79: /* node_opt: T_SSL T_IDENT  */
#line 601 "test_spec_parse.y"
        {
		strlcpy(current_node->ssl, (yyvsp[0].str), sizeof(current_node->ssl));
		free((yyvsp[0].str));
	}
#line 2206 "test_spec_parse.c"
    break;

  case 80: /* node_opt: T_AUTH T_IDENT  */
#line 606 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2215 "test_spec_parse.c"
    break;

  case 81: /* node_opt: T_AUTH_METHOD T_IDENT  */
#line 611 "test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2224 "test_spec_parse.c"
    break;

  case 82: /* node_opt: T_REPLICATION_QUORUM T_TRUE  */
#line 616 "test_spec_parse.y"
        {
		current_node->replicationQuorum = true;
	}
#line 2232 "test_spec_parse.c"
    break;

  case 83: /* node_opt: T_REPLICATION_QUORUM T_FALSE  */
#line 620 "test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2240 "test_spec_parse.c"
    break;

  case 84: /* node_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 624 "test_spec_parse.y"
        {
		strlcpy(current_node->replicationPassword, (yyvsp[0].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[0].str));
	}
#line 2250 "test_spec_parse.c"
    break;

  case 85: /* node_opt: T_COMMAND T_STRING  */
#line 630 "test_spec_parse.y"
        {
		/* replaces this node's own container command entirely, see
		 * test_spec.h's own commandOverride comment */
		strlcpy(current_node->commandOverride, (yyvsp[0].str),
		        sizeof(current_node->commandOverride));
		free((yyvsp[0].str));
	}
#line 2262 "test_spec_parse.c"
    break;

  case 86: /* node_opt: T_MONITOR_PASSWORD T_STRING  */
#line 638 "test_spec_parse.y"
        {
		strlcpy(current_node->monitorPassword, (yyvsp[0].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[0].str));
	}
#line 2272 "test_spec_parse.c"
    break;

  case 87: /* node_opt: T_VOLUME T_IDENT T_IDENT  */
#line 644 "test_spec_parse.y"
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
#line 2290 "test_spec_parse.c"
    break;

  case 88: /* node_opt: T_VOLUME T_IDENT T_STRING  */
#line 658 "test_spec_parse.y"
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
#line 2308 "test_spec_parse.c"
    break;

  case 89: /* setup_block: T_SETUP cmd_block  */
#line 679 "test_spec_parse.y"
        {
		current_spec->setup = (yyvsp[0].step);
	}
#line 2316 "test_spec_parse.c"
    break;

  case 90: /* teardown_block: T_TEARDOWN cmd_block  */
#line 686 "test_spec_parse.y"
        {
		current_spec->teardown = (yyvsp[0].step);
	}
#line 2324 "test_spec_parse.c"
    break;

  case 91: /* named_step: T_STEP ident_or_string cmd_block  */
#line 697 "test_spec_parse.y"
        {
		TestStep *s = (yyvsp[0].step);
		strncpy(s->name, (yyvsp[-1].str), sizeof(s->name) - 1);
		free((yyvsp[-1].str));
		register_step(current_spec, s);
	}
#line 2335 "test_spec_parse.c"
    break;

  case 92: /* cmd_block: T_LBRACE cmd_list T_RBRACE  */
#line 715 "test_spec_parse.y"
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
#line 2350 "test_spec_parse.c"
    break;

  case 93: /* cmd_list: %empty  */
#line 729 "test_spec_parse.y"
        {
		(yyval.step) = make_step("");
	}
#line 2358 "test_spec_parse.c"
    break;

  case 94: /* cmd_list: cmd_list step_cmd  */
#line 733 "test_spec_parse.y"
        {
		if ((yyvsp[0].cmd)) append_cmd((yyvsp[-1].step), (yyvsp[0].cmd));
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2367 "test_spec_parse.c"
    break;

  case 95: /* step_cmd: exec_cmd  */
#line 740 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2373 "test_spec_parse.c"
    break;

  case 96: /* step_cmd: wait_cmd  */
#line 741 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2379 "test_spec_parse.c"
    break;

  case 97: /* step_cmd: assert_cmd  */
#line 742 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2385 "test_spec_parse.c"
    break;

  case 98: /* step_cmd: sql_cmd  */
#line 743 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2391 "test_spec_parse.c"
    break;

  case 99: /* step_cmd: expect_cmd  */
#line 744 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2397 "test_spec_parse.c"
    break;

  case 100: /* step_cmd: promote_cmd  */
#line 745 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2403 "test_spec_parse.c"
    break;

  case 101: /* step_cmd: perform_cmd  */
#line 746 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2409 "test_spec_parse.c"
    break;

  case 102: /* step_cmd: network_cmd  */
#line 747 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2415 "test_spec_parse.c"
    break;

  case 103: /* step_cmd: sleep_cmd  */
#line 748 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2421 "test_spec_parse.c"
    break;

  case 104: /* step_cmd: compose_cmd  */
#line 749 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2427 "test_spec_parse.c"
    break;

  case 105: /* step_cmd: postgres_ctl_cmd  */
#line 750 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2433 "test_spec_parse.c"
    break;

  case 106: /* step_cmd: fsm_step_cmd  */
#line 751 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2439 "test_spec_parse.c"
    break;

  case 107: /* step_cmd: stays_while_cmd  */
#line 752 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2445 "test_spec_parse.c"
    break;

  case 108: /* step_cmd: set_monitor_cmd  */
#line 753 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2451 "test_spec_parse.c"
    break;

  case 109: /* step_cmd: logs_cmd  */
#line 754 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2457 "test_spec_parse.c"
    break;

  case 110: /* step_cmd: nodeini_cmd  */
#line 755 "test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2463 "test_spec_parse.c"
    break;

  case 111: /* exec_cmd: T_EXEC T_IDENT T_SHELL_ARGS  */
#line 770 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2474 "test_spec_parse.c"
    break;

  case 112: /* exec_cmd: T_EXEC T_IDENT  */
#line 777 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2484 "test_spec_parse.c"
    break;

  case 113: /* exec_cmd: T_EXEC_FAILS T_IDENT T_SHELL_ARGS  */
#line 783 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2495 "test_spec_parse.c"
    break;

  case 114: /* exec_cmd: T_EXEC_FAILS T_IDENT  */
#line 790 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2505 "test_spec_parse.c"
    break;

  case 115: /* exec_cmd: T_RUN T_IDENT T_SHELL_ARGS  */
#line 796 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2516 "test_spec_parse.c"
    break;

  case 116: /* exec_cmd: T_RUN T_IDENT  */
#line 803 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2526 "test_spec_parse.c"
    break;

  case 117: /* exec_cmd: T_PG_AUTOCTL T_IDENT T_SHELL_ARGS  */
#line 809 "test_spec_parse.y"
        {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2538 "test_spec_parse.c"
    break;

  case 118: /* exec_cmd: T_PG_AUTOCTL T_IDENT  */
#line 817 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[0].str));
	}
#line 2548 "test_spec_parse.c"
    break;

  case 119: /* exec_cmd: T_PG_AUTOCTL  */
#line 823 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	}
#line 2556 "test_spec_parse.c"
    break;

  case 122: /* wait_multi_condition: T_IDENT T_STATE state_op fsm_state  */
#line 861 "test_spec_parse.y"
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
#line 2575 "test_spec_parse.c"
    break;

  case 123: /* wait_multi_condition: T_IDENT T_STATE state_op T_IDENT  */
#line 876 "test_spec_parse.y"
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
#line 2594 "test_spec_parse.c"
    break;

  case 128: /* pass_state_list: fsm_state  */
#line 916 "test_spec_parse.y"
        {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2606 "test_spec_parse.c"
    break;

  case 129: /* pass_state_list: T_IDENT  */
#line 924 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2618 "test_spec_parse.c"
    break;

  case 130: /* pass_state_list: pass_state_list T_COMMA fsm_state  */
#line 932 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2629 "test_spec_parse.c"
    break;

  case 131: /* pass_state_list: pass_state_list T_COMMA T_IDENT  */
#line 939 "test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2641 "test_spec_parse.c"
    break;

  case 132: /* $@5: %empty  */
#line 950 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); }
#line 2650 "test_spec_parse.c"
    break;

  case 133: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state $@5 opt_passing_through opt_timeout  */
#line 955 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2660 "test_spec_parse.c"
    break;

  case 134: /* $@6: %empty  */
#line 961 "test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); free((yyvsp[0].str)); }
#line 2669 "test_spec_parse.c"
    break;

  case 135: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT $@6 opt_passing_through opt_timeout  */
#line 966 "test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2679 "test_spec_parse.c"
    break;

  case 136: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 972 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2692 "test_spec_parse.c"
    break;

  case 137: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 981 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2705 "test_spec_parse.c"
    break;

  case 138: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout  */
#line 990 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-2].str));
	}
#line 2716 "test_spec_parse.c"
    break;

  case 139: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_REPLAYS T_IDENT opt_timeout  */
#line 1004 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-3].str)); free((yyvsp[-1].str));
	}
#line 2728 "test_spec_parse.c"
    break;

  case 140: /* wait_cmd: T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout  */
#line 1012 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2738 "test_spec_parse.c"
    break;

  case 141: /* wait_cmd: T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout  */
#line 1026 "test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2748 "test_spec_parse.c"
    break;

  case 142: /* state_name_list: fsm_state  */
#line 1041 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2758 "test_spec_parse.c"
    break;

  case 143: /* state_name_list: T_IDENT  */
#line 1047 "test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2769 "test_spec_parse.c"
    break;

  case 144: /* state_name_list: state_name_list T_COMMA fsm_state  */
#line 1054 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2779 "test_spec_parse.c"
    break;

  case 145: /* state_name_list: state_name_list T_COMMA T_IDENT  */
#line 1060 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2790 "test_spec_parse.c"
    break;

  case 148: /* group_items: T_GROUP T_INTEGER  */
#line 1079 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2799 "test_spec_parse.c"
    break;

  case 149: /* group_items: group_items T_COMMA T_GROUP T_INTEGER  */
#line 1084 "test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2808 "test_spec_parse.c"
    break;

  case 150: /* opt_timeout: %empty  */
#line 1091 "test_spec_parse.y"
                                       { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; }
#line 2814 "test_spec_parse.c"
    break;

  case 151: /* opt_timeout: T_TIMEOUT T_INTEGER  */
#line 1092 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 2820 "test_spec_parse.c"
    break;

  case 152: /* opt_timeout: T_WITH T_TIMEOUT T_INTEGER  */
#line 1093 "test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 2826 "test_spec_parse.c"
    break;

  case 153: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout  */
#line 1105 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2838 "test_spec_parse.c"
    break;

  case 154: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout  */
#line 1113 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2850 "test_spec_parse.c"
    break;

  case 155: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1121 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2862 "test_spec_parse.c"
    break;

  case 156: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1129 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2874 "test_spec_parse.c"
    break;

  case 157: /* sql_cmd: T_SQL T_IDENT T_BLOCK  */
#line 1147 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2885 "test_spec_parse.c"
    break;

  case 158: /* expect_cmd: T_EXPECT T_BLOCK  */
#line 1162 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[0].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[0].str));
	}
#line 2896 "test_spec_parse.c"
    break;

  case 159: /* expect_cmd: T_EXPECT T_ERROR  */
#line 1169 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	}
#line 2904 "test_spec_parse.c"
    break;

  case 160: /* expect_cmd: T_EXPECT T_ERROR T_IDENT  */
#line 1173 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[0].str), sizeof((yyval.cmd)->state));
		free((yyvsp[0].str));
	}
#line 2914 "test_spec_parse.c"
    break;

  case 161: /* expect_cmd: T_EXPECT T_ERROR T_INTEGER  */
#line 1179 "test_spec_parse.y"
        {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[0].ival));
	}
#line 2924 "test_spec_parse.c"
    break;

  case 162: /* promote_cmd: T_PROMOTE promote_list  */
#line 1192 "test_spec_parse.y"
        {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	}
#line 2933 "test_spec_parse.c"
    break;

  case 163: /* promote_list: T_IDENT  */
#line 1200 "test_spec_parse.y"
        {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 2945 "test_spec_parse.c"
    break;

  case 164: /* promote_list: promote_list T_COMMA T_IDENT  */
#line 1208 "test_spec_parse.y"
        {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 2956 "test_spec_parse.c"
    break;

  case 165: /* perform_cmd: T_PERFORM T_FAILOVER  */
#line 1229 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 2967 "test_spec_parse.c"
    break;

  case 166: /* perform_cmd: T_PERFORM T_FAILOVER T_GROUP T_INTEGER  */
#line 1236 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
	}
#line 2978 "test_spec_parse.c"
    break;

  case 167: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT  */
#line 1243 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[0].str));
	}
#line 2990 "test_spec_parse.c"
    break;

  case 168: /* perform_cmd: T_PERFORM T_FAILOVER T_IN T_FORMATION T_IDENT T_GROUP T_INTEGER  */
#line 1251 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[0].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[-2].str));
	}
#line 3002 "test_spec_parse.c"
    break;

  case 169: /* network_cmd: T_NETWORK T_DISCONNECT T_IDENT  */
#line 1267 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3012 "test_spec_parse.c"
    break;

  case 170: /* network_cmd: T_NETWORK T_CONNECT T_IDENT  */
#line 1273 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3022 "test_spec_parse.c"
    break;

  case 171: /* nodeini_cmd: T_NODEINI T_SET T_IDENT T_IDENT T_IDENT  */
#line 1294 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3034 "test_spec_parse.c"
    break;

  case 172: /* nodeini_cmd: T_NODEINI T_GET T_IDENT T_IDENT T_IDENT  */
#line 1302 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-2].str)); free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 3046 "test_spec_parse.c"
    break;

  case 173: /* sleep_cmd: T_SLEEP T_INTEGER  */
#line 1317 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
	}
#line 3055 "test_spec_parse.c"
    break;

  case 174: /* compose_cmd: T_COMPOSE T_DOWN  */
#line 1331 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	}
#line 3063 "test_spec_parse.c"
    break;

  case 175: /* compose_cmd: T_COMPOSE T_START T_IDENT  */
#line 1335 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3073 "test_spec_parse.c"
    break;

  case 176: /* compose_cmd: T_COMPOSE T_STOP T_IDENT  */
#line 1341 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3083 "test_spec_parse.c"
    break;

  case 177: /* compose_cmd: T_COMPOSE T_KILL T_IDENT  */
#line 1347 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3093 "test_spec_parse.c"
    break;

  case 178: /* compose_cmd: T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS  */
#line 1373 "test_spec_parse.y"
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
#line 3120 "test_spec_parse.c"
    break;

  case 179: /* postgres_ctl_cmd: T_STOP T_POSTGRES node_name  */
#line 1407 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3130 "test_spec_parse.c"
    break;

  case 180: /* postgres_ctl_cmd: T_START T_POSTGRES node_name  */
#line 1413 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3140 "test_spec_parse.c"
    break;

  case 181: /* fsm_step_cmd: T_FSM T_STEP node_name  */
#line 1434 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 3150 "test_spec_parse.c"
    break;

  case 182: /* $@7: %empty  */
#line 1450 "test_spec_parse.y"
                { pgaf_next_brace_is_while = 1; }
#line 3156 "test_spec_parse.c"
    break;

  case 183: /* while_body: T_WHILE $@7 T_LBRACE cmd_list T_RBRACE  */
#line 1451 "test_spec_parse.y"
        { (yyval.step) = (yyvsp[-1].step); }
#line 3162 "test_spec_parse.c"
    break;

  case 184: /* stays_while_cmd: T_ASSERT node_name T_STAYS fsm_state while_body  */
#line 1456 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[0].step)) ? (yyvsp[0].step)->commands : NULL;
		free((yyvsp[-3].str));
	}
#line 3174 "test_spec_parse.c"
    break;

  case 185: /* set_monitor_cmd: T_SET T_IDENT T_IDENT  */
#line 1475 "test_spec_parse.y"
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
#line 3191 "test_spec_parse.c"
    break;

  case 186: /* logs_cmd: T_LOGS T_IDENT T_CONTAINS T_STRING  */
#line 1500 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3204 "test_spec_parse.c"
    break;

  case 187: /* logs_cmd: T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING  */
#line 1509 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3217 "test_spec_parse.c"
    break;

  case 188: /* logs_cmd: T_LOGS T_IDENT T_MATCHES T_STRING  */
#line 1518 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3230 "test_spec_parse.c"
    break;

  case 189: /* logs_cmd: T_LOGS T_IDENT T_NOT T_MATCHES T_STRING  */
#line 1527 "test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3243 "test_spec_parse.c"
    break;

  case 192: /* sequence_names: sequence_names ident_or_string  */
#line 1548 "test_spec_parse.y"
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
#line 3259 "test_spec_parse.c"
    break;

  case 193: /* fsm_state: T_FS_INIT  */
#line 1569 "test_spec_parse.y"
                                   { (yyval.str) = "init"; }
#line 3265 "test_spec_parse.c"
    break;

  case 194: /* fsm_state: T_FS_SINGLE  */
#line 1570 "test_spec_parse.y"
                                   { (yyval.str) = "single"; }
#line 3271 "test_spec_parse.c"
    break;

  case 195: /* fsm_state: T_FS_PRIMARY  */
#line 1571 "test_spec_parse.y"
                                   { (yyval.str) = "primary"; }
#line 3277 "test_spec_parse.c"
    break;

  case 196: /* fsm_state: T_FS_WAIT_PRIMARY  */
#line 1572 "test_spec_parse.y"
                                   { (yyval.str) = "wait_primary"; }
#line 3283 "test_spec_parse.c"
    break;

  case 197: /* fsm_state: T_FS_WAIT_STANDBY  */
#line 1573 "test_spec_parse.y"
                                   { (yyval.str) = "wait_standby"; }
#line 3289 "test_spec_parse.c"
    break;

  case 198: /* fsm_state: T_FS_DEMOTED  */
#line 1574 "test_spec_parse.y"
                                   { (yyval.str) = "demoted"; }
#line 3295 "test_spec_parse.c"
    break;

  case 199: /* fsm_state: T_FS_DEMOTE_TIMEOUT  */
#line 1575 "test_spec_parse.y"
                                   { (yyval.str) = "demote_timeout"; }
#line 3301 "test_spec_parse.c"
    break;

  case 200: /* fsm_state: T_FS_DRAINING  */
#line 1576 "test_spec_parse.y"
                                   { (yyval.str) = "draining"; }
#line 3307 "test_spec_parse.c"
    break;

  case 201: /* fsm_state: T_FS_SECONDARY  */
#line 1577 "test_spec_parse.y"
                                   { (yyval.str) = "secondary"; }
#line 3313 "test_spec_parse.c"
    break;

  case 202: /* fsm_state: T_FS_CATCHINGUP  */
#line 1578 "test_spec_parse.y"
                                   { (yyval.str) = "catchingup"; }
#line 3319 "test_spec_parse.c"
    break;

  case 203: /* fsm_state: T_FS_PREP_PROMOTION  */
#line 1579 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_promotion"; }
#line 3325 "test_spec_parse.c"
    break;

  case 204: /* fsm_state: T_FS_STOP_REPLICATION  */
#line 1580 "test_spec_parse.y"
                                   { (yyval.str) = "stop_replication"; }
#line 3331 "test_spec_parse.c"
    break;

  case 205: /* fsm_state: T_FS_MAINTENANCE  */
#line 1581 "test_spec_parse.y"
                                   { (yyval.str) = "maintenance"; }
#line 3337 "test_spec_parse.c"
    break;

  case 206: /* fsm_state: T_FS_JOIN_PRIMARY  */
#line 1582 "test_spec_parse.y"
                                   { (yyval.str) = "join_primary"; }
#line 3343 "test_spec_parse.c"
    break;

  case 207: /* fsm_state: T_FS_APPLY_SETTINGS  */
#line 1583 "test_spec_parse.y"
                                   { (yyval.str) = "apply_settings"; }
#line 3349 "test_spec_parse.c"
    break;

  case 208: /* fsm_state: T_FS_PREPARE_MAINTENANCE  */
#line 1584 "test_spec_parse.y"
                                   { (yyval.str) = "prepare_maintenance"; }
#line 3355 "test_spec_parse.c"
    break;

  case 209: /* fsm_state: T_FS_WAIT_MAINTENANCE  */
#line 1585 "test_spec_parse.y"
                                   { (yyval.str) = "wait_maintenance"; }
#line 3361 "test_spec_parse.c"
    break;

  case 210: /* fsm_state: T_FS_REPORT_LSN  */
#line 1586 "test_spec_parse.y"
                                   { (yyval.str) = "report_lsn"; }
#line 3367 "test_spec_parse.c"
    break;

  case 211: /* fsm_state: T_FS_FAST_FORWARD  */
#line 1587 "test_spec_parse.y"
                                   { (yyval.str) = "fast_forward"; }
#line 3373 "test_spec_parse.c"
    break;

  case 212: /* fsm_state: T_FS_JOIN_SECONDARY  */
#line 1588 "test_spec_parse.y"
                                   { (yyval.str) = "join_secondary"; }
#line 3379 "test_spec_parse.c"
    break;

  case 213: /* fsm_state: T_FS_DROPPED  */
#line 1589 "test_spec_parse.y"
                                   { (yyval.str) = "dropped"; }
#line 3385 "test_spec_parse.c"
    break;

  case 214: /* ident_or_string: T_IDENT  */
#line 1597 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3391 "test_spec_parse.c"
    break;

  case 215: /* ident_or_string: T_STRING  */
#line 1598 "test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3397 "test_spec_parse.c"
    break;


#line 3401 "test_spec_parse.c"

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

#line 1601 "test_spec_parse.y"


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
