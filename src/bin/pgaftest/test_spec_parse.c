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
#line 1 "src/bin/pgaftest/test_spec_parse.y"

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


#line 215 "src/bin/pgaftest/test_spec_parse.c"

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
  YYSYMBOL_T_LAUNCH = 24,                  /* T_LAUNCH  */
  YYSYMBOL_T_CREATE = 25,                  /* T_CREATE  */
  YYSYMBOL_T_DEFERRED = 26,                /* T_DEFERRED  */
  YYSYMBOL_T_IMMEDIATE = 27,               /* T_IMMEDIATE  */
  YYSYMBOL_T_FALSE = 28,                   /* T_FALSE  */
  YYSYMBOL_T_TRUE = 29,                    /* T_TRUE  */
  YYSYMBOL_T_INITIALLY = 30,               /* T_INITIALLY  */
  YYSYMBOL_T_VOLUME = 31,                  /* T_VOLUME  */
  YYSYMBOL_T_LISTEN = 32,                  /* T_LISTEN  */
  YYSYMBOL_T_CITUS_SECONDARY = 33,         /* T_CITUS_SECONDARY  */
  YYSYMBOL_T_CANDIDATE_PRIORITY = 34,      /* T_CANDIDATE_PRIORITY  */
  YYSYMBOL_T_PORT = 35,                    /* T_PORT  */
  YYSYMBOL_T_PASSWORD = 36,                /* T_PASSWORD  */
  YYSYMBOL_T_MONITOR_PASSWORD = 37,        /* T_MONITOR_PASSWORD  */
  YYSYMBOL_T_CITUS_CLUSTER_NAME = 38,      /* T_CITUS_CLUSTER_NAME  */
  YYSYMBOL_T_DEBIAN_CLUSTER = 39,          /* T_DEBIAN_CLUSTER  */
  YYSYMBOL_T_REPLICATION_QUORUM = 40,      /* T_REPLICATION_QUORUM  */
  YYSYMBOL_T_REPLICATION_PASSWORD = 41,    /* T_REPLICATION_PASSWORD  */
  YYSYMBOL_T_EXTENSION_VERSION = 42,       /* T_EXTENSION_VERSION  */
  YYSYMBOL_T_BIND_SOURCE = 43,             /* T_BIND_SOURCE  */
  YYSYMBOL_T_FS_INIT = 44,                 /* T_FS_INIT  */
  YYSYMBOL_T_FS_SINGLE = 45,               /* T_FS_SINGLE  */
  YYSYMBOL_T_FS_PRIMARY = 46,              /* T_FS_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_PRIMARY = 47,         /* T_FS_WAIT_PRIMARY  */
  YYSYMBOL_T_FS_WAIT_STANDBY = 48,         /* T_FS_WAIT_STANDBY  */
  YYSYMBOL_T_FS_DEMOTED = 49,              /* T_FS_DEMOTED  */
  YYSYMBOL_T_FS_DEMOTE_TIMEOUT = 50,       /* T_FS_DEMOTE_TIMEOUT  */
  YYSYMBOL_T_FS_DRAINING = 51,             /* T_FS_DRAINING  */
  YYSYMBOL_T_FS_SECONDARY = 52,            /* T_FS_SECONDARY  */
  YYSYMBOL_T_FS_CATCHINGUP = 53,           /* T_FS_CATCHINGUP  */
  YYSYMBOL_T_FS_PREP_PROMOTION = 54,       /* T_FS_PREP_PROMOTION  */
  YYSYMBOL_T_FS_STOP_REPLICATION = 55,     /* T_FS_STOP_REPLICATION  */
  YYSYMBOL_T_FS_MAINTENANCE = 56,          /* T_FS_MAINTENANCE  */
  YYSYMBOL_T_FS_JOIN_PRIMARY = 57,         /* T_FS_JOIN_PRIMARY  */
  YYSYMBOL_T_FS_APPLY_SETTINGS = 58,       /* T_FS_APPLY_SETTINGS  */
  YYSYMBOL_T_FS_PREPARE_MAINTENANCE = 59,  /* T_FS_PREPARE_MAINTENANCE  */
  YYSYMBOL_T_FS_WAIT_MAINTENANCE = 60,     /* T_FS_WAIT_MAINTENANCE  */
  YYSYMBOL_T_FS_REPORT_LSN = 61,           /* T_FS_REPORT_LSN  */
  YYSYMBOL_T_FS_FAST_FORWARD = 62,         /* T_FS_FAST_FORWARD  */
  YYSYMBOL_T_FS_JOIN_SECONDARY = 63,       /* T_FS_JOIN_SECONDARY  */
  YYSYMBOL_T_FS_DROPPED = 64,              /* T_FS_DROPPED  */
  YYSYMBOL_T_EXEC = 65,                    /* T_EXEC  */
  YYSYMBOL_T_EXEC_FAILS = 66,              /* T_EXEC_FAILS  */
  YYSYMBOL_T_PG_AUTOCTL = 67,              /* T_PG_AUTOCTL  */
  YYSYMBOL_T_WAIT = 68,                    /* T_WAIT  */
  YYSYMBOL_T_UNTIL = 69,                   /* T_UNTIL  */
  YYSYMBOL_T_TIMEOUT = 70,                 /* T_TIMEOUT  */
  YYSYMBOL_T_AND = 71,                     /* T_AND  */
  YYSYMBOL_T_IS = 72,                      /* T_IS  */
  YYSYMBOL_T_WITH = 73,                    /* T_WITH  */
  YYSYMBOL_T_ASSERT = 74,                  /* T_ASSERT  */
  YYSYMBOL_T_SQL = 75,                     /* T_SQL  */
  YYSYMBOL_T_EXPECT = 76,                  /* T_EXPECT  */
  YYSYMBOL_T_ERROR = 77,                   /* T_ERROR  */
  YYSYMBOL_T_PROMOTE = 78,                 /* T_PROMOTE  */
  YYSYMBOL_T_NETWORK = 79,                 /* T_NETWORK  */
  YYSYMBOL_T_DISCONNECT = 80,              /* T_DISCONNECT  */
  YYSYMBOL_T_CONNECT = 81,                 /* T_CONNECT  */
  YYSYMBOL_T_SLEEP = 82,                   /* T_SLEEP  */
  YYSYMBOL_T_COMPOSE = 83,                 /* T_COMPOSE  */
  YYSYMBOL_T_DOWN = 84,                    /* T_DOWN  */
  YYSYMBOL_T_START = 85,                   /* T_START  */
  YYSYMBOL_T_STOP = 86,                    /* T_STOP  */
  YYSYMBOL_T_STOPPED = 87,                 /* T_STOPPED  */
  YYSYMBOL_T_KILL = 88,                    /* T_KILL  */
  YYSYMBOL_T_INJECT = 89,                  /* T_INJECT  */
  YYSYMBOL_T_STATE = 90,                   /* T_STATE  */
  YYSYMBOL_T_ASSIGNED_STATE = 91,          /* T_ASSIGNED_STATE  */
  YYSYMBOL_T_IN = 92,                      /* T_IN  */
  YYSYMBOL_T_GROUP = 93,                   /* T_GROUP  */
  YYSYMBOL_T_LBRACE = 94,                  /* T_LBRACE  */
  YYSYMBOL_T_RBRACE = 95,                  /* T_RBRACE  */
  YYSYMBOL_T_COMMA = 96,                   /* T_COMMA  */
  YYSYMBOL_T_POSTGRES = 97,                /* T_POSTGRES  */
  YYSYMBOL_T_STAYS = 98,                   /* T_STAYS  */
  YYSYMBOL_T_WHILE = 99,                   /* T_WHILE  */
  YYSYMBOL_T_THROUGH = 100,                /* T_THROUGH  */
  YYSYMBOL_T_SET = 101,                    /* T_SET  */
  YYSYMBOL_T_LOGS = 102,                   /* T_LOGS  */
  YYSYMBOL_T_NOT = 103,                    /* T_NOT  */
  YYSYMBOL_T_CONTAINS = 104,               /* T_CONTAINS  */
  YYSYMBOL_T_MATCHES = 105,                /* T_MATCHES  */
  YYSYMBOL_T_INTEGER = 106,                /* T_INTEGER  */
  YYSYMBOL_T_IDENT = 107,                  /* T_IDENT  */
  YYSYMBOL_T_STRING = 108,                 /* T_STRING  */
  YYSYMBOL_T_BLOCK = 109,                  /* T_BLOCK  */
  YYSYMBOL_T_SHELL_ARGS = 110,             /* T_SHELL_ARGS  */
  YYSYMBOL_YYACCEPT = 111,                 /* $accept  */
  YYSYMBOL_spec = 112,                     /* spec  */
  YYSYMBOL_spec_item = 113,                /* spec_item  */
  YYSYMBOL_cluster_block = 114,            /* cluster_block  */
  YYSYMBOL_115_1 = 115,                    /* $@1  */
  YYSYMBOL_cluster_item_list = 116,        /* cluster_item_list  */
  YYSYMBOL_cluster_item = 117,             /* cluster_item  */
  YYSYMBOL_monitor_line = 118,             /* monitor_line  */
  YYSYMBOL_image_line = 119,               /* image_line  */
  YYSYMBOL_extension_version_line = 120,   /* extension_version_line  */
  YYSYMBOL_ssl_line = 121,                 /* ssl_line  */
  YYSYMBOL_auth_line = 122,                /* auth_line  */
  YYSYMBOL_formation_block = 123,          /* formation_block  */
  YYSYMBOL_124_2 = 124,                    /* $@2  */
  YYSYMBOL_formation_opt_list = 125,       /* formation_opt_list  */
  YYSYMBOL_bare_name = 126,                /* bare_name  */
  YYSYMBOL_formation_opt = 127,            /* formation_opt  */
  YYSYMBOL_node_list = 128,                /* node_list  */
  YYSYMBOL_node_name = 129,                /* node_name  */
  YYSYMBOL_init_node_slot = 130,           /* init_node_slot  */
  YYSYMBOL_node_line = 131,                /* node_line  */
  YYSYMBOL_132_3 = 132,                    /* $@3  */
  YYSYMBOL_133_4 = 133,                    /* $@4  */
  YYSYMBOL_node_opt_list = 134,            /* node_opt_list  */
  YYSYMBOL_node_opt = 135,                 /* node_opt  */
  YYSYMBOL_setup_block = 136,              /* setup_block  */
  YYSYMBOL_teardown_block = 137,           /* teardown_block  */
  YYSYMBOL_named_step = 138,               /* named_step  */
  YYSYMBOL_cmd_block = 139,                /* cmd_block  */
  YYSYMBOL_cmd_list = 140,                 /* cmd_list  */
  YYSYMBOL_step_cmd = 141,                 /* step_cmd  */
  YYSYMBOL_exec_cmd = 142,                 /* exec_cmd  */
  YYSYMBOL_state_op = 143,                 /* state_op  */
  YYSYMBOL_wait_multi_condition = 144,     /* wait_multi_condition  */
  YYSYMBOL_wait_multi_condition_list = 145, /* wait_multi_condition_list  */
  YYSYMBOL_opt_passing_through = 146,      /* opt_passing_through  */
  YYSYMBOL_pass_state_list = 147,          /* pass_state_list  */
  YYSYMBOL_wait_cmd = 148,                 /* wait_cmd  */
  YYSYMBOL_149_5 = 149,                    /* $@5  */
  YYSYMBOL_150_6 = 150,                    /* $@6  */
  YYSYMBOL_state_name_list = 151,          /* state_name_list  */
  YYSYMBOL_opt_in_group = 152,             /* opt_in_group  */
  YYSYMBOL_group_items = 153,              /* group_items  */
  YYSYMBOL_opt_timeout = 154,              /* opt_timeout  */
  YYSYMBOL_assert_cmd = 155,               /* assert_cmd  */
  YYSYMBOL_sql_cmd = 156,                  /* sql_cmd  */
  YYSYMBOL_expect_cmd = 157,               /* expect_cmd  */
  YYSYMBOL_promote_cmd = 158,              /* promote_cmd  */
  YYSYMBOL_promote_list = 159,             /* promote_list  */
  YYSYMBOL_network_cmd = 160,              /* network_cmd  */
  YYSYMBOL_sleep_cmd = 161,                /* sleep_cmd  */
  YYSYMBOL_compose_cmd = 162,              /* compose_cmd  */
  YYSYMBOL_postgres_ctl_cmd = 163,         /* postgres_ctl_cmd  */
  YYSYMBOL_while_body = 164,               /* while_body  */
  YYSYMBOL_165_7 = 165,                    /* $@7  */
  YYSYMBOL_stays_while_cmd = 166,          /* stays_while_cmd  */
  YYSYMBOL_set_monitor_cmd = 167,          /* set_monitor_cmd  */
  YYSYMBOL_logs_cmd = 168,                 /* logs_cmd  */
  YYSYMBOL_sequence_block = 169,           /* sequence_block  */
  YYSYMBOL_sequence_names = 170,           /* sequence_names  */
  YYSYMBOL_fsm_state = 171,                /* fsm_state  */
  YYSYMBOL_ident_or_string = 172           /* ident_or_string  */
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
#define YYLAST   562

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  111
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  62
/* YYNRULES -- Number of rules.  */
#define YYNRULES  197
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  320

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   365


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
     105,   106,   107,   108,   109,   110
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   211,   211,   212,   216,   217,   218,   219,   220,   233,
     232,   242,   244,   248,   249,   250,   251,   252,   253,   254,
     267,   271,   278,   285,   291,   298,   305,   312,   325,   331,
     341,   347,   357,   367,   373,   384,   383,   400,   402,   411,
     412,   413,   414,   415,   419,   424,   428,   434,   436,   455,
     456,   465,   482,   481,   489,   488,   496,   498,   502,   507,
     512,   516,   520,   526,   531,   535,   540,   544,   548,   552,
     556,   560,   564,   568,   574,   580,   585,   590,   595,   599,
     603,   609,   615,   629,   650,   657,   668,   686,   701,   704,
     712,   713,   714,   715,   716,   717,   718,   719,   720,   721,
     722,   723,   724,   738,   745,   751,   758,   764,   772,   778,
     805,   805,   816,   831,   849,   850,   865,   867,   871,   879,
     887,   894,   906,   905,   917,   916,   927,   936,   945,   952,
     966,   981,   987,   994,  1000,  1013,  1015,  1019,  1024,  1032,
    1033,  1034,  1045,  1053,  1061,  1069,  1087,  1102,  1109,  1113,
    1119,  1132,  1140,  1148,  1163,  1169,  1182,  1196,  1200,  1206,
    1212,  1238,  1272,  1278,  1295,  1295,  1300,  1319,  1344,  1353,
    1362,  1371,  1387,  1390,  1392,  1414,  1415,  1416,  1417,  1418,
    1419,  1420,  1421,  1422,  1423,  1424,  1425,  1426,  1427,  1428,
    1429,  1430,  1431,  1432,  1433,  1434,  1442,  1443
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
  "T_LAUNCH", "T_CREATE", "T_DEFERRED", "T_IMMEDIATE", "T_FALSE", "T_TRUE",
  "T_INITIALLY", "T_VOLUME", "T_LISTEN", "T_CITUS_SECONDARY",
  "T_CANDIDATE_PRIORITY", "T_PORT", "T_PASSWORD", "T_MONITOR_PASSWORD",
  "T_CITUS_CLUSTER_NAME", "T_DEBIAN_CLUSTER", "T_REPLICATION_QUORUM",
  "T_REPLICATION_PASSWORD", "T_EXTENSION_VERSION", "T_BIND_SOURCE",
  "T_FS_INIT", "T_FS_SINGLE", "T_FS_PRIMARY", "T_FS_WAIT_PRIMARY",
  "T_FS_WAIT_STANDBY", "T_FS_DEMOTED", "T_FS_DEMOTE_TIMEOUT",
  "T_FS_DRAINING", "T_FS_SECONDARY", "T_FS_CATCHINGUP",
  "T_FS_PREP_PROMOTION", "T_FS_STOP_REPLICATION", "T_FS_MAINTENANCE",
  "T_FS_JOIN_PRIMARY", "T_FS_APPLY_SETTINGS", "T_FS_PREPARE_MAINTENANCE",
  "T_FS_WAIT_MAINTENANCE", "T_FS_REPORT_LSN", "T_FS_FAST_FORWARD",
  "T_FS_JOIN_SECONDARY", "T_FS_DROPPED", "T_EXEC", "T_EXEC_FAILS",
  "T_PG_AUTOCTL", "T_WAIT", "T_UNTIL", "T_TIMEOUT", "T_AND", "T_IS",
  "T_WITH", "T_ASSERT", "T_SQL", "T_EXPECT", "T_ERROR", "T_PROMOTE",
  "T_NETWORK", "T_DISCONNECT", "T_CONNECT", "T_SLEEP", "T_COMPOSE",
  "T_DOWN", "T_START", "T_STOP", "T_STOPPED", "T_KILL", "T_INJECT",
  "T_STATE", "T_ASSIGNED_STATE", "T_IN", "T_GROUP", "T_LBRACE", "T_RBRACE",
  "T_COMMA", "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH", "T_SET",
  "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_INTEGER", "T_IDENT",
  "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec", "spec_item",
  "cluster_block", "$@1", "cluster_item_list", "cluster_item",
  "monitor_line", "image_line", "extension_version_line", "ssl_line",
  "auth_line", "formation_block", "$@2", "formation_opt_list", "bare_name",
  "formation_opt", "node_list", "node_name", "init_node_slot", "node_line",
  "$@3", "$@4", "node_opt_list", "node_opt", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "$@5", "$@6", "state_name_list", "opt_in_group",
  "group_items", "opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd",
  "promote_cmd", "promote_list", "network_cmd", "sleep_cmd", "compose_cmd",
  "postgres_ctl_cmd", "while_body", "$@7", "stays_while_cmd",
  "set_monitor_cmd", "logs_cmd", "sequence_block", "sequence_names",
  "fsm_state", "ident_or_string", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-155)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-114)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      33,   -54,   -49,   -49,   -32,  -155,    79,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,   -49,
     -32,  -155,  -155,  -155,   397,  -155,  -155,     5,   -52,   -45,
     -30,    11,     4,   -12,   -63,    -8,     3,   -13,   -35,    21,
      29,  -155,    -2,    16,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,   -11,   -16,
      36,    37,    38,  -155,    -6,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,    18,    41,    50,   140,  -155,    13,
      12,    52,    10,  -155,  -155,    74,    64,    65,  -155,  -155,
      71,    98,    99,   100,     6,     6,   102,   -37,   125,   127,
     126,   129,     7,  -155,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,   -26,   166,   -36,  -155,     1,
       1,   498,  -155,  -155,  -155,   131,  -155,  -155,  -155,  -155,
    -155,   130,  -155,  -155,  -155,  -155,    15,   133,   134,  -155,
    -155,  -155,  -155,   213,   156,     0,     8,     1,     1,   137,
     152,   167,     8,  -155,  -155,   204,   231,   147,  -155,  -155,
     161,   162,  -155,  -155,   235,  -155,  -155,  -155,  -155,   190,
     244,  -155,  -155,  -155,  -155,  -155,   191,   203,  -155,   268,
     295,   208,  -155,   -44,   193,   205,  -155,  -155,  -155,     8,
       8,     8,     8,  -155,  -155,  -155,  -155,   192,  -155,  -155,
       2,  -155,   196,   232,   233,     8,     8,     1,   137,  -155,
    -155,   212,  -155,  -155,  -155,  -155,   214,  -155,   199,  -155,
    -155,  -155,  -155,   207,   207,  -155,  -155,   332,  -155,   227,
    -155,  -155,  -155,   359,     8,     8,  -155,  -155,  -155,   439,
    -155,  -155,  -155,   238,  -155,  -155,  -155,  -155,   215,   142,
     396,  -155,   228,   229,   230,  -155,  -155,  -155,  -155,    95,
     -14,  -155,  -155,   253,  -155,  -155,   255,   256,   202,   257,
     258,    96,   259,   260,  -155,  -155,  -155,   115,  -155,  -155,
    -155,  -155,  -155,  -155,   339,    26,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,   342,  -155,  -155,  -155
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   173,     0,     2,     4,     5,
       6,     7,     8,     9,    88,    84,    85,   196,   197,     0,
     172,     1,     3,    11,     0,    86,   174,     0,     0,     0,
     109,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    87,     0,     0,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,    20,     0,
       0,     0,     0,    35,     0,    19,    10,    12,    13,    14,
      17,    15,    16,    18,   104,   106,   108,     0,    50,    49,
       0,     0,   148,   147,   152,   151,     0,     0,   156,   157,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    29,    28,    32,    33,    34,    37,    30,
      31,   103,   105,   107,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   193,   194,   195,   132,     0,   135,   131,     0,
       0,     0,   146,   150,   149,     0,   154,   155,   158,   159,
     160,     0,    49,   163,   162,   167,     0,     0,     0,    22,
      23,    24,    21,     0,     0,     0,   139,     0,     0,     0,
       0,     0,   139,   110,   111,     0,     0,     0,   153,   161,
       0,     0,   168,   170,    25,    26,    42,    43,    41,     0,
       0,    47,    39,    40,    44,    38,     0,     0,   128,     0,
       0,     0,   114,   139,     0,   136,   134,   133,   129,   139,
     139,   139,   139,   164,   166,   169,   171,     0,    45,    46,
       0,   140,     0,   124,   122,   139,   139,     0,     0,   130,
     137,     0,   143,   142,   145,   144,     0,    27,     0,    36,
      51,    48,   141,   116,   116,   127,   126,     0,   115,     0,
      88,    51,    52,     0,   139,   139,   113,   112,   138,     0,
      54,    56,   119,   117,   118,   125,   123,   165,     0,    53,
       0,    56,     0,     0,     0,    58,    59,    60,    61,     0,
       0,    62,    67,     0,    68,    69,     0,     0,     0,     0,
       0,     0,     0,     0,    57,   121,   120,     0,    75,    76,
      77,    63,    66,    64,     0,     0,    70,    72,    81,    73,
      74,    79,    78,    80,    71,    55,     0,    82,    83,    65
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -155,  -155,   363,  -155,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,   -93,   119,
    -155,  -155,  -155,   101,  -155,  -155,  -155,  -155,    14,   121,
    -155,  -155,  -129,  -154,  -155,   153,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -140,  -155,  -155,  -155,  -155,  -155,  -155,
    -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,  -155,
    -141,   353
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     6,     7,     8,    23,    27,    67,    68,    69,    70,
      71,    72,    73,   108,   165,   194,   195,   220,    80,   252,
     241,   261,   268,   269,   294,     9,    10,    11,    15,    24,
      44,    45,   175,   136,   203,   254,   263,    46,   244,   243,
     137,   172,   205,   198,    47,    48,    49,    50,    85,    51,
      52,    53,    54,   214,   236,    55,    56,    57,    12,    20,
     138,    19
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     177,   153,   154,    98,   186,   187,    78,   238,    78,    58,
      78,   176,   303,   173,    82,   202,   188,    16,    59,   189,
      60,    61,    62,    63,    99,   100,   196,   228,   101,   197,
     207,   163,   208,    25,   210,   212,     1,   164,   199,   200,
      13,     2,     3,     4,     5,    14,    83,    64,    65,    89,
      90,    91,   190,    92,    93,    74,   170,   304,   224,   226,
     171,   166,    75,   229,   167,   168,   156,   157,   158,   232,
     233,   234,   235,   174,   248,    17,    18,    76,   196,    21,
      77,   197,     1,    86,    87,   245,   246,     2,     3,     4,
       5,   103,   104,    88,   191,    81,   102,   239,   247,    84,
      66,   109,   110,   139,   140,    96,   257,   192,   193,   152,
     141,    79,   264,   152,   265,   266,   143,   144,    94,   180,
     181,   301,   302,    97,   311,   312,    95,   240,   111,   296,
     272,   273,   274,   317,   318,   275,   276,   277,   278,   279,
     280,   281,   282,   105,   106,   107,   283,   284,   285,   286,
     287,   112,   288,   289,   290,   291,   292,   272,   273,   274,
     113,   142,   275,   276,   277,   278,   279,   280,   281,   282,
     145,   146,   147,   283,   284,   285,   286,   287,   148,   288,
     289,   290,   291,   292,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   149,   150,   151,   293,   155,
     315,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   159,   160,   161,   293,   162,   169,   178,   184,
     179,   182,   183,   185,   201,   204,   213,   135,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   215,
     216,   217,   219,   222,   206,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   218,   221,   227,   230,
     237,   231,   242,  -113,  -112,   249,   251,   253,   250,   271,
     308,   209,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   258,   270,   298,   299,   300,   211,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     305,   306,   307,   316,   309,   310,   314,   313,   319,    22,
     260,   259,   297,    26,     0,   223,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   255,     0,     0,
       0,     0,   225,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   256,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,     0,    28,    29,    30,    31,   262,     0,     0,     0,
       0,    32,    33,    34,     0,    35,    36,     0,     0,    37,
      38,     0,    39,    40,     0,     0,     0,     0,     0,     0,
       0,     0,    41,     0,     0,     0,     0,     0,    42,    43,
       0,     0,     0,   295,    28,    29,    30,    31,     0,     0,
       0,     0,     0,    32,    33,    34,     0,    35,    36,     0,
       0,    37,    38,     0,    39,    40,     0,     0,     0,     0,
       0,     0,     0,     0,   267,     0,     0,     0,     0,     0,
      42,    43,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134
};

static const yytype_int16 yycheck[] =
{
     141,    94,    95,    14,     4,     5,     4,     5,     4,     4,
       4,   140,    26,    12,    77,   169,    16,     3,    13,    19,
      15,    16,    17,    18,    35,    36,    70,    71,    39,    73,
     171,    24,   172,    19,   175,   176,     3,    30,   167,   168,
      94,     8,     9,    10,    11,    94,   109,    42,    43,    84,
      85,    86,    52,    88,    89,   107,    92,    71,   199,   200,
      96,    87,   107,   203,    90,    91,   103,   104,   105,   209,
     210,   211,   212,    72,   228,   107,   108,   107,    70,     0,
      69,    73,     3,    80,    81,   225,   226,     8,     9,    10,
      11,   107,   108,   106,    94,   107,   107,    95,   227,   107,
      95,   107,   108,    90,    91,   107,   247,   107,   108,   107,
      98,   107,   253,   107,   254,   255,   106,   107,    97,   104,
     105,    26,    27,   107,    28,    29,    97,   220,   110,   270,
      15,    16,    17,   107,   108,    20,    21,    22,    23,    24,
      25,    26,    27,   107,   107,   107,    31,    32,    33,    34,
      35,   110,    37,    38,    39,    40,    41,    15,    16,    17,
     110,   109,    20,    21,    22,    23,    24,    25,    26,    27,
      96,   107,   107,    31,    32,    33,    34,    35,   107,    37,
      38,    39,    40,    41,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,   107,   107,   107,    93,   107,
      95,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,   107,   106,   108,    93,   107,    71,   107,    26,
     110,   108,   108,    87,   107,    93,    99,   107,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,   108,
     108,    36,    28,    70,   107,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,   106,   106,    90,   106,
     108,    96,   106,    71,    71,    93,   107,   100,    94,    94,
     108,   107,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,   106,    96,   107,   107,   107,   107,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
     107,   106,   106,    24,   107,   107,   106,   108,    26,     6,
     251,   250,   271,    20,    -1,   107,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,   244,    -1,    -1,
      -1,    -1,   107,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   107,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    -1,    65,    66,    67,    68,   107,    -1,    -1,    -1,
      -1,    74,    75,    76,    -1,    78,    79,    -1,    -1,    82,
      83,    -1,    85,    86,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    95,    -1,    -1,    -1,    -1,    -1,   101,   102,
      -1,    -1,    -1,   107,    65,    66,    67,    68,    -1,    -1,
      -1,    -1,    -1,    74,    75,    76,    -1,    78,    79,    -1,
      -1,    82,    83,    -1,    85,    86,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    95,    -1,    -1,    -1,    -1,    -1,
     101,   102,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   112,   113,   114,   136,
     137,   138,   169,    94,    94,   139,   139,   107,   108,   172,
     170,     0,   113,   115,   140,   139,   172,   116,    65,    66,
      67,    68,    74,    75,    76,    78,    79,    82,    83,    85,
      86,    95,   101,   102,   141,   142,   148,   155,   156,   157,
     158,   160,   161,   162,   163,   166,   167,   168,     4,    13,
      15,    16,    17,    18,    42,    43,    95,   117,   118,   119,
     120,   121,   122,   123,   107,   107,   107,    69,     4,   107,
     129,   107,    77,   109,   107,   159,    80,    81,   106,    84,
      85,    86,    88,    89,    97,    97,   107,   107,    14,    35,
      36,    39,   107,   107,   108,   107,   107,   107,   124,   107,
     108,   110,   110,   110,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,   107,   144,   151,   171,    90,
      91,    98,   109,   106,   107,    96,   107,   107,   107,   107,
     107,   107,   107,   129,   129,   107,   103,   104,   105,   107,
     106,   108,   107,    24,    30,   125,    87,    90,    91,    71,
      92,    96,   152,    12,    72,   143,   143,   171,   107,   110,
     104,   105,   108,   108,    26,    87,     4,     5,    16,    19,
      52,    94,   107,   108,   126,   127,    70,    73,   154,   143,
     143,   107,   144,   145,    93,   153,   107,   171,   154,   107,
     171,   107,   171,    99,   164,   108,   108,    36,   106,    28,
     128,   106,    70,   107,   171,   107,   171,    90,    71,   154,
     106,    96,   154,   154,   154,   154,   165,   108,     5,    95,
     129,   131,   106,   150,   149,   154,   154,   143,   144,    93,
      94,   107,   130,   100,   146,   146,   107,   171,   106,   140,
     130,   132,   107,   147,   171,   154,   154,    95,   133,   134,
      96,    94,    15,    16,    17,    20,    21,    22,    23,    24,
      25,    26,    27,    31,    32,    33,    34,    35,    37,    38,
      39,    40,    41,    93,   135,   107,   171,   134,   107,   107,
     107,    26,    27,    26,    71,   107,   106,   106,   108,   107,
     107,    28,    29,   108,   106,    95,    24,   107,   108,    26
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   111,   112,   112,   113,   113,   113,   113,   113,   115,
     114,   116,   116,   117,   117,   117,   117,   117,   117,   117,
     118,   118,   118,   118,   118,   118,   118,   118,   119,   119,
     120,   120,   121,   122,   122,   124,   123,   125,   125,   126,
     126,   126,   126,   126,   127,   127,   127,   128,   128,   129,
     129,   130,   132,   131,   133,   131,   134,   134,   135,   135,
     135,   135,   135,   135,   135,   135,   135,   135,   135,   135,
     135,   135,   135,   135,   135,   135,   135,   135,   135,   135,
     135,   135,   135,   135,   136,   137,   138,   139,   140,   140,
     141,   141,   141,   141,   141,   141,   141,   141,   141,   141,
     141,   141,   141,   142,   142,   142,   142,   142,   142,   142,
     143,   143,   144,   144,   145,   145,   146,   146,   147,   147,
     147,   147,   149,   148,   150,   148,   148,   148,   148,   148,
     148,   151,   151,   151,   151,   152,   152,   153,   153,   154,
     154,   154,   155,   155,   155,   155,   156,   157,   157,   157,
     157,   158,   159,   159,   160,   160,   161,   162,   162,   162,
     162,   162,   163,   163,   165,   164,   166,   167,   168,   168,
     168,   168,   169,   170,   170,   171,   171,   171,   171,   171,
     171,   171,   171,   171,   171,   171,   171,   171,   171,   171,
     171,   171,   171,   171,   171,   171,   172,   172
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     3,     3,     3,     4,     4,     6,     2,     2,
       2,     2,     2,     2,     2,     0,     6,     0,     2,     1,
       1,     1,     1,     1,     1,     2,     2,     0,     2,     1,
       1,     0,     0,     4,     0,     7,     0,     2,     1,     1,
       1,     1,     1,     2,     2,     4,     2,     1,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     3,     3,     2,     2,     3,     3,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     2,     3,     2,     3,     2,     1,
       1,     1,     4,     4,     1,     3,     0,     2,     1,     1,
       3,     3,     0,     9,     0,     9,     7,     7,     5,     5,
       6,     1,     1,     3,     3,     0,     2,     2,     4,     0,
       2,     3,     6,     6,     6,     6,     3,     2,     2,     3,
       3,     2,     1,     3,     3,     3,     2,     2,     3,     3,
       3,     4,     3,     3,     0,     5,     5,     3,     4,     5,
       4,     5,     2,     0,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1
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
#line 233 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	}
#line 1687 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 19: /* cluster_item: T_BIND_SOURCE  */
#line 254 "src/bin/pgaftest/test_spec_parse.y"
                        { current_spec->cluster.bindSource = true; }
#line 1693 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 20: /* monitor_line: T_MONITOR  */
#line 268 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
	}
#line 1701 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 21: /* monitor_line: T_MONITOR T_DEBIAN_CLUSTER T_IDENT  */
#line 272 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[0].str));
	}
#line 1712 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 22: /* monitor_line: T_MONITOR T_IMAGE_TARGET T_IDENT  */
#line 279 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[0].str));
	}
#line 1723 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 23: /* monitor_line: T_MONITOR T_PORT T_INTEGER  */
#line 286 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[0].ival);
	}
#line 1733 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 24: /* monitor_line: T_MONITOR T_PASSWORD T_STRING  */
#line 292 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[0].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[0].str));
	}
#line 1744 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 25: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED  */
#line 299 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1755 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 26: /* monitor_line: T_MONITOR T_IDENT T_INITIALLY T_STOPPED  */
#line 306 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-2].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-2].str));
	}
#line 1766 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 27: /* monitor_line: T_MONITOR T_IDENT T_LAUNCH T_DEFERRED T_PASSWORD T_STRING  */
#line 313 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[-4].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[-4].str));
		/* password for second monitor not yet stored */
		free((yyvsp[0].str));
	}
#line 1779 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 28: /* image_line: T_IMAGE T_STRING  */
#line 326 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1789 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 29: /* image_line: T_IMAGE T_IDENT  */
#line 332 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.image, (yyvsp[0].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[0].str));
	}
#line 1799 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 30: /* extension_version_line: T_EXTENSION_VERSION T_IDENT  */
#line 342 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 1809 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 31: /* extension_version_line: T_EXTENSION_VERSION T_STRING  */
#line 348 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[0].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[0].str));
	}
#line 1819 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 32: /* ssl_line: T_SSL T_IDENT  */
#line 358 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.ssl, (yyvsp[0].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[0].str));
	}
#line 1829 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 33: /* auth_line: T_AUTH T_IDENT  */
#line 368 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 1839 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 34: /* auth_line: T_AUTH_METHOD T_IDENT  */
#line 374 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_spec->cluster.auth, (yyvsp[0].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[0].str));
	}
#line 1849 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 35: /* $@2: %empty  */
#line 384 "src/bin/pgaftest/test_spec_parse.y"
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
#line 1867 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 39: /* bare_name: T_IDENT  */
#line 411 "src/bin/pgaftest/test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 1873 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 40: /* bare_name: T_STRING  */
#line 412 "src/bin/pgaftest/test_spec_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 1879 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 41: /* bare_name: T_AUTH  */
#line 413 "src/bin/pgaftest/test_spec_parse.y"
                    { (yyval.str) = strdup("auth"); }
#line 1885 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 42: /* bare_name: T_MONITOR  */
#line 414 "src/bin/pgaftest/test_spec_parse.y"
                    { (yyval.str) = strdup("monitor"); }
#line 1891 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 43: /* bare_name: T_NODE  */
#line 415 "src/bin/pgaftest/test_spec_parse.y"
                    { (yyval.str) = strdup("node"); }
#line 1897 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 44: /* formation_opt: bare_name  */
#line 420 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_formation->name, (yyvsp[0].str), sizeof(current_formation->name));
		free((yyvsp[0].str));
	}
#line 1906 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 45: /* formation_opt: T_NUM_SYNC T_INTEGER  */
#line 425 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_formation->numSync = (yyvsp[0].ival);
	}
#line 1914 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 46: /* formation_opt: T_FS_SECONDARY T_FALSE  */
#line 429 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_formation->disableSecondary = true;
	}
#line 1922 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 49: /* node_name: T_IDENT  */
#line 455 "src/bin/pgaftest/test_spec_parse.y"
                     { (yyval.str) = (yyvsp[0].str); }
#line 1928 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 50: /* node_name: T_MONITOR  */
#line 456 "src/bin/pgaftest/test_spec_parse.y"
                     { (yyval.str) = strdup("monitor"); }
#line 1934 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 51: /* init_node_slot: %empty  */
#line 465 "src/bin/pgaftest/test_spec_parse.y"
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
#line 1951 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 52: /* $@3: %empty  */
#line 482 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 1960 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 54: /* $@4: %empty  */
#line 489 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->name, (yyvsp[-1].str), sizeof(current_node->name));
		free((yyvsp[-1].str));
	}
#line 1969 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 58: /* node_opt: T_COORDINATOR  */
#line 503 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	}
#line 1978 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 59: /* node_opt: T_WORKER  */
#line 508 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	}
#line 1987 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 60: /* node_opt: T_ASYNC  */
#line 513 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 1995 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 61: /* node_opt: T_NO_MONITOR  */
#line 517 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->noMonitor = true;
	}
#line 2003 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 62: /* node_opt: T_DEFERRED  */
#line 521 "src/bin/pgaftest/test_spec_parse.y"
        {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2013 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 63: /* node_opt: T_LAUNCH T_DEFERRED  */
#line 527 "src/bin/pgaftest/test_spec_parse.y"
        {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	}
#line 2022 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 64: /* node_opt: T_CREATE T_DEFERRED  */
#line 532 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->createDeferred = true;
	}
#line 2030 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 65: /* node_opt: T_CREATE T_AND T_LAUNCH T_DEFERRED  */
#line 536 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	}
#line 2039 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 66: /* node_opt: T_LAUNCH T_IMMEDIATE  */
#line 541 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2047 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 67: /* node_opt: T_IMMEDIATE  */
#line 545 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->launchDeferred = false;
	}
#line 2055 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 68: /* node_opt: T_LISTEN  */
#line 549 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->listen = true;
	}
#line 2063 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 69: /* node_opt: T_CITUS_SECONDARY  */
#line 553 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->citusSecondary = true;
	}
#line 2071 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 70: /* node_opt: T_CANDIDATE_PRIORITY T_INTEGER  */
#line 557 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->candidatePriority = (yyvsp[0].ival);
	}
#line 2079 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 71: /* node_opt: T_GROUP T_INTEGER  */
#line 561 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->group = (yyvsp[0].ival);
	}
#line 2087 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 72: /* node_opt: T_PORT T_INTEGER  */
#line 565 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->pgPort = (yyvsp[0].ival);
	}
#line 2095 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 73: /* node_opt: T_CITUS_CLUSTER_NAME T_IDENT  */
#line 569 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->citusClusterName, (yyvsp[0].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[0].str));
	}
#line 2105 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 74: /* node_opt: T_DEBIAN_CLUSTER T_IDENT  */
#line 575 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->debianCluster, (yyvsp[0].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[0].str));
	}
#line 2115 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 75: /* node_opt: T_SSL T_IDENT  */
#line 581 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->ssl, (yyvsp[0].str), sizeof(current_node->ssl));
		free((yyvsp[0].str));
	}
#line 2124 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 76: /* node_opt: T_AUTH T_IDENT  */
#line 586 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2133 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 77: /* node_opt: T_AUTH_METHOD T_IDENT  */
#line 591 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->auth, (yyvsp[0].str), sizeof(current_node->auth));
		free((yyvsp[0].str));
	}
#line 2142 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 78: /* node_opt: T_REPLICATION_QUORUM T_TRUE  */
#line 596 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->replicationQuorum = true;
	}
#line 2150 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 79: /* node_opt: T_REPLICATION_QUORUM T_FALSE  */
#line 600 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_node->replicationQuorum = false;
	}
#line 2158 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 80: /* node_opt: T_REPLICATION_PASSWORD T_STRING  */
#line 604 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->replicationPassword, (yyvsp[0].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[0].str));
	}
#line 2168 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 81: /* node_opt: T_MONITOR_PASSWORD T_STRING  */
#line 610 "src/bin/pgaftest/test_spec_parse.y"
        {
		strlcpy(current_node->monitorPassword, (yyvsp[0].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[0].str));
	}
#line 2178 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 82: /* node_opt: T_VOLUME T_IDENT T_IDENT  */
#line 616 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2196 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 83: /* node_opt: T_VOLUME T_IDENT T_STRING  */
#line 630 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2214 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 84: /* setup_block: T_SETUP cmd_block  */
#line 651 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->setup = (yyvsp[0].step);
	}
#line 2222 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 85: /* teardown_block: T_TEARDOWN cmd_block  */
#line 658 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_spec->teardown = (yyvsp[0].step);
	}
#line 2230 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 86: /* named_step: T_STEP ident_or_string cmd_block  */
#line 669 "src/bin/pgaftest/test_spec_parse.y"
        {
		TestStep *s = (yyvsp[0].step);
		strncpy(s->name, (yyvsp[-1].str), sizeof(s->name) - 1);
		free((yyvsp[-1].str));
		register_step(current_spec, s);
	}
#line 2241 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 87: /* cmd_block: T_LBRACE cmd_list T_RBRACE  */
#line 687 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2256 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 88: /* cmd_list: %empty  */
#line 701 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.step) = make_step("");
	}
#line 2264 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 89: /* cmd_list: cmd_list step_cmd  */
#line 705 "src/bin/pgaftest/test_spec_parse.y"
        {
		if ((yyvsp[0].cmd)) append_cmd((yyvsp[-1].step), (yyvsp[0].cmd));
		(yyval.step) = (yyvsp[-1].step);
	}
#line 2273 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 90: /* step_cmd: exec_cmd  */
#line 712 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2279 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 91: /* step_cmd: wait_cmd  */
#line 713 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2285 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 92: /* step_cmd: assert_cmd  */
#line 714 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2291 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 93: /* step_cmd: sql_cmd  */
#line 715 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2297 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 94: /* step_cmd: expect_cmd  */
#line 716 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2303 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 95: /* step_cmd: promote_cmd  */
#line 717 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2309 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 96: /* step_cmd: network_cmd  */
#line 718 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2315 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 97: /* step_cmd: sleep_cmd  */
#line 719 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2321 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 98: /* step_cmd: compose_cmd  */
#line 720 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2327 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 99: /* step_cmd: postgres_ctl_cmd  */
#line 721 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2333 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 100: /* step_cmd: stays_while_cmd  */
#line 722 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2339 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 101: /* step_cmd: set_monitor_cmd  */
#line 723 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2345 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 102: /* step_cmd: logs_cmd  */
#line 724 "src/bin/pgaftest/test_spec_parse.y"
                            { (yyval.cmd) = (yyvsp[0].cmd); }
#line 2351 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 103: /* exec_cmd: T_EXEC T_IDENT T_SHELL_ARGS  */
#line 739 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2362 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 104: /* exec_cmd: T_EXEC T_IDENT  */
#line 746 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2372 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 105: /* exec_cmd: T_EXEC_FAILS T_IDENT T_SHELL_ARGS  */
#line 752 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2383 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 106: /* exec_cmd: T_EXEC_FAILS T_IDENT  */
#line 759 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2393 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 107: /* exec_cmd: T_PG_AUTOCTL T_IDENT T_SHELL_ARGS  */
#line 765 "src/bin/pgaftest/test_spec_parse.y"
        {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2405 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 108: /* exec_cmd: T_PG_AUTOCTL T_IDENT  */
#line 773 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[0].str));
	}
#line 2415 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 109: /* exec_cmd: T_PG_AUTOCTL  */
#line 779 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	}
#line 2423 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 112: /* wait_multi_condition: T_IDENT T_STATE state_op fsm_state  */
#line 817 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2442 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 113: /* wait_multi_condition: T_IDENT T_STATE state_op T_IDENT  */
#line 832 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2461 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 118: /* pass_state_list: fsm_state  */
#line 872 "src/bin/pgaftest/test_spec_parse.y"
        {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2473 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 119: /* pass_state_list: T_IDENT  */
#line 880 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2485 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 120: /* pass_state_list: pass_state_list T_COMMA fsm_state  */
#line 888 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
	}
#line 2496 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 121: /* pass_state_list: pass_state_list T_COMMA T_IDENT  */
#line 895 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[0].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[0].str));
	}
#line 2508 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 122: /* $@5: %empty  */
#line 906 "src/bin/pgaftest/test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); }
#line 2517 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 123: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op fsm_state $@5 opt_passing_through opt_timeout  */
#line 911 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2527 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 124: /* $@6: %empty  */
#line 917 "src/bin/pgaftest/test_spec_parse.y"
            { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[-3].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[0].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[-3].str)); free((yyvsp[0].str)); }
#line 2536 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 125: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STATE state_op T_IDENT $@6 opt_passing_through opt_timeout  */
#line 922 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_pass_cmd->timeoutSeconds = (yyvsp[0].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	}
#line 2546 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 126: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 928 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2559 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 127: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 937 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2572 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 128: /* wait_cmd: T_WAIT T_UNTIL T_IDENT T_STOPPED opt_timeout  */
#line 946 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-2].str));
	}
#line 2583 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 129: /* wait_cmd: T_WAIT T_UNTIL state_name_list opt_in_group opt_timeout  */
#line 953 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2593 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 130: /* wait_cmd: T_WAIT T_UNTIL wait_multi_condition T_AND wait_multi_condition_list opt_timeout  */
#line 967 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		current_wait_cmd = NULL;
	}
#line 2603 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 131: /* state_name_list: fsm_state  */
#line 982 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2613 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 132: /* state_name_list: T_IDENT  */
#line 988 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2624 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 133: /* state_name_list: state_name_list T_COMMA fsm_state  */
#line 995 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
	}
#line 2634 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 134: /* state_name_list: state_name_list T_COMMA T_IDENT  */
#line 1001 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[0].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[0].str));
	}
#line 2645 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 137: /* group_items: T_GROUP T_INTEGER  */
#line 1020 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2654 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 138: /* group_items: group_items T_COMMA T_GROUP T_INTEGER  */
#line 1025 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[0].ival);
	}
#line 2663 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 139: /* opt_timeout: %empty  */
#line 1032 "src/bin/pgaftest/test_spec_parse.y"
                                       { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; }
#line 2669 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 140: /* opt_timeout: T_TIMEOUT T_INTEGER  */
#line 1033 "src/bin/pgaftest/test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 2675 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 141: /* opt_timeout: T_WITH T_TIMEOUT T_INTEGER  */
#line 1034 "src/bin/pgaftest/test_spec_parse.y"
                                       { (yyval.ival) = (yyvsp[0].ival); }
#line 2681 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 142: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op fsm_state opt_timeout  */
#line 1046 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2693 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 143: /* assert_cmd: T_ASSERT T_IDENT T_STATE state_op T_IDENT opt_timeout  */
#line 1054 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd((yyvsp[0].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2705 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 144: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op fsm_state opt_timeout  */
#line 1062 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str));
	}
#line 2717 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 145: /* assert_cmd: T_ASSERT T_IDENT T_ASSIGNED_STATE state_op T_IDENT opt_timeout  */
#line 1070 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[-4].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
		free((yyvsp[-4].str)); free((yyvsp[-1].str));
	}
#line 2729 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 146: /* sql_cmd: T_SQL T_IDENT T_BLOCK  */
#line 1088 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[-1].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[0].str), sizeof((yyval.cmd)->args));
		free((yyvsp[-1].str)); free((yyvsp[0].str));
	}
#line 2740 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 147: /* expect_cmd: T_EXPECT T_BLOCK  */
#line 1103 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[0].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[0].str));
	}
#line 2751 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 148: /* expect_cmd: T_EXPECT T_ERROR  */
#line 1110 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	}
#line 2759 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 149: /* expect_cmd: T_EXPECT T_ERROR T_IDENT  */
#line 1114 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[0].str), sizeof((yyval.cmd)->state));
		free((yyvsp[0].str));
	}
#line 2769 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 150: /* expect_cmd: T_EXPECT T_ERROR T_INTEGER  */
#line 1120 "src/bin/pgaftest/test_spec_parse.y"
        {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[0].ival));
	}
#line 2779 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 151: /* promote_cmd: T_PROMOTE promote_list  */
#line 1133 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	}
#line 2788 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 152: /* promote_list: T_IDENT  */
#line 1141 "src/bin/pgaftest/test_spec_parse.y"
        {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 2800 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 153: /* promote_list: promote_list T_COMMA T_IDENT  */
#line 1149 "src/bin/pgaftest/test_spec_parse.y"
        {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[0].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[0].str));
	}
#line 2811 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 154: /* network_cmd: T_NETWORK T_DISCONNECT T_IDENT  */
#line 1164 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2821 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 155: /* network_cmd: T_NETWORK T_CONNECT T_IDENT  */
#line 1170 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2831 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 156: /* sleep_cmd: T_SLEEP T_INTEGER  */
#line 1183 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[0].ival);
	}
#line 2840 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 157: /* compose_cmd: T_COMPOSE T_DOWN  */
#line 1197 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	}
#line 2848 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 158: /* compose_cmd: T_COMPOSE T_START T_IDENT  */
#line 1201 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2858 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 159: /* compose_cmd: T_COMPOSE T_STOP T_IDENT  */
#line 1207 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2868 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 160: /* compose_cmd: T_COMPOSE T_KILL T_IDENT  */
#line 1213 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2878 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 161: /* compose_cmd: T_COMPOSE T_INJECT T_IDENT T_SHELL_ARGS  */
#line 1239 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2905 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 162: /* postgres_ctl_cmd: T_STOP T_POSTGRES node_name  */
#line 1273 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2915 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 163: /* postgres_ctl_cmd: T_START T_POSTGRES node_name  */
#line 1279 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[0].str), sizeof((yyval.cmd)->service));
		free((yyvsp[0].str));
	}
#line 2925 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 164: /* $@7: %empty  */
#line 1295 "src/bin/pgaftest/test_spec_parse.y"
                { pgaf_next_brace_is_while = 1; }
#line 2931 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 165: /* while_body: T_WHILE $@7 T_LBRACE cmd_list T_RBRACE  */
#line 1296 "src/bin/pgaftest/test_spec_parse.y"
        { (yyval.step) = (yyvsp[-1].step); }
#line 2937 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 166: /* stays_while_cmd: T_ASSERT node_name T_STAYS fsm_state while_body  */
#line 1301 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[-1].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[0].step)) ? (yyvsp[0].step)->commands : NULL;
		free((yyvsp[-3].str));
	}
#line 2949 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 167: /* set_monitor_cmd: T_SET T_IDENT T_IDENT  */
#line 1320 "src/bin/pgaftest/test_spec_parse.y"
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
#line 2966 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 168: /* logs_cmd: T_LOGS T_IDENT T_CONTAINS T_STRING  */
#line 1345 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 2979 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 169: /* logs_cmd: T_LOGS T_IDENT T_NOT T_CONTAINS T_STRING  */
#line 1354 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 2992 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 170: /* logs_cmd: T_LOGS T_IDENT T_MATCHES T_STRING  */
#line 1363 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-2].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[-2].str)); free((yyvsp[0].str));
	}
#line 3005 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 171: /* logs_cmd: T_LOGS T_IDENT T_NOT T_MATCHES T_STRING  */
#line 1372 "src/bin/pgaftest/test_spec_parse.y"
        {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[-3].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[0].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[-3].str)); free((yyvsp[0].str));
	}
#line 3018 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 174: /* sequence_names: sequence_names ident_or_string  */
#line 1393 "src/bin/pgaftest/test_spec_parse.y"
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
#line 3034 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 175: /* fsm_state: T_FS_INIT  */
#line 1414 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "init"; }
#line 3040 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 176: /* fsm_state: T_FS_SINGLE  */
#line 1415 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "single"; }
#line 3046 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 177: /* fsm_state: T_FS_PRIMARY  */
#line 1416 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "primary"; }
#line 3052 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 178: /* fsm_state: T_FS_WAIT_PRIMARY  */
#line 1417 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "wait_primary"; }
#line 3058 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 179: /* fsm_state: T_FS_WAIT_STANDBY  */
#line 1418 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "wait_standby"; }
#line 3064 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 180: /* fsm_state: T_FS_DEMOTED  */
#line 1419 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "demoted"; }
#line 3070 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 181: /* fsm_state: T_FS_DEMOTE_TIMEOUT  */
#line 1420 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "demote_timeout"; }
#line 3076 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 182: /* fsm_state: T_FS_DRAINING  */
#line 1421 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "draining"; }
#line 3082 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 183: /* fsm_state: T_FS_SECONDARY  */
#line 1422 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "secondary"; }
#line 3088 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 184: /* fsm_state: T_FS_CATCHINGUP  */
#line 1423 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "catchingup"; }
#line 3094 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 185: /* fsm_state: T_FS_PREP_PROMOTION  */
#line 1424 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "prepare_promotion"; }
#line 3100 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 186: /* fsm_state: T_FS_STOP_REPLICATION  */
#line 1425 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "stop_replication"; }
#line 3106 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 187: /* fsm_state: T_FS_MAINTENANCE  */
#line 1426 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "maintenance"; }
#line 3112 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 188: /* fsm_state: T_FS_JOIN_PRIMARY  */
#line 1427 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "join_primary"; }
#line 3118 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 189: /* fsm_state: T_FS_APPLY_SETTINGS  */
#line 1428 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "apply_settings"; }
#line 3124 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 190: /* fsm_state: T_FS_PREPARE_MAINTENANCE  */
#line 1429 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "prepare_maintenance"; }
#line 3130 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 191: /* fsm_state: T_FS_WAIT_MAINTENANCE  */
#line 1430 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "wait_maintenance"; }
#line 3136 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 192: /* fsm_state: T_FS_REPORT_LSN  */
#line 1431 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "report_lsn"; }
#line 3142 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 193: /* fsm_state: T_FS_FAST_FORWARD  */
#line 1432 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "fast_forward"; }
#line 3148 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 194: /* fsm_state: T_FS_JOIN_SECONDARY  */
#line 1433 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "join_secondary"; }
#line 3154 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 195: /* fsm_state: T_FS_DROPPED  */
#line 1434 "src/bin/pgaftest/test_spec_parse.y"
                                   { (yyval.str) = "dropped"; }
#line 3160 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 196: /* ident_or_string: T_IDENT  */
#line 1442 "src/bin/pgaftest/test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3166 "src/bin/pgaftest/test_spec_parse.c"
    break;

  case 197: /* ident_or_string: T_STRING  */
#line 1443 "src/bin/pgaftest/test_spec_parse.y"
                   { (yyval.str) = (yyvsp[0].str); }
#line 3172 "src/bin/pgaftest/test_spec_parse.c"
    break;


#line 3176 "src/bin/pgaftest/test_spec_parse.c"

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

#line 1446 "src/bin/pgaftest/test_spec_parse.y"


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
