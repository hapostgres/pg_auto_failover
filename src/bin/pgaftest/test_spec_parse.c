/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

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

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     T_CLUSTER = 258,
     T_MONITOR = 259,
     T_NODE = 260,
     T_CITUS_COORDINATOR = 261,
     T_CITUS_WORKER = 262,
     T_SETUP = 263,
     T_TEARDOWN = 264,
     T_STEP = 265,
     T_SEQUENCE = 266,
     T_EQUALS = 267,
     T_IMAGE = 268,
     T_IMAGE_TARGET = 269,
     T_SSL = 270,
     T_AUTH = 271,
     T_AUTH_METHOD = 272,
     T_FORMATION = 273,
     T_NUM_SYNC = 274,
     T_COORDINATOR = 275,
     T_WORKER = 276,
     T_ASYNC = 277,
     T_NO_MONITOR = 278,
     T_LAUNCH = 279,
     T_CREATE = 280,
     T_DEFERRED = 281,
     T_IMMEDIATE = 282,
     T_FALSE = 283,
     T_TRUE = 284,
     T_INITIALLY = 285,
     T_VOLUME = 286,
     T_LISTEN = 287,
     T_CITUS_SECONDARY = 288,
     T_CANDIDATE_PRIORITY = 289,
     T_PORT = 290,
     T_PASSWORD = 291,
     T_MONITOR_PASSWORD = 292,
     T_CITUS_CLUSTER_NAME = 293,
     T_DEBIAN_CLUSTER = 294,
     T_REPLICATION_QUORUM = 295,
     T_REPLICATION_PASSWORD = 296,
     T_EXTENSION_VERSION = 297,
     T_BIND_SOURCE = 298,
     T_LEGACY_STARTUP = 299,
     T_FS_INIT = 300,
     T_FS_SINGLE = 301,
     T_FS_PRIMARY = 302,
     T_FS_WAIT_PRIMARY = 303,
     T_FS_WAIT_STANDBY = 304,
     T_FS_DEMOTED = 305,
     T_FS_DEMOTE_TIMEOUT = 306,
     T_FS_DRAINING = 307,
     T_FS_SECONDARY = 308,
     T_FS_CATCHINGUP = 309,
     T_FS_PREP_PROMOTION = 310,
     T_FS_STOP_REPLICATION = 311,
     T_FS_MAINTENANCE = 312,
     T_FS_JOIN_PRIMARY = 313,
     T_FS_APPLY_SETTINGS = 314,
     T_FS_PREPARE_MAINTENANCE = 315,
     T_FS_WAIT_MAINTENANCE = 316,
     T_FS_REPORT_LSN = 317,
     T_FS_FAST_FORWARD = 318,
     T_FS_JOIN_SECONDARY = 319,
     T_FS_DROPPED = 320,
     T_EXEC = 321,
     T_EXEC_FAILS = 322,
     T_RUN = 323,
     T_PG_AUTOCTL = 324,
     T_WAIT = 325,
     T_UNTIL = 326,
     T_TIMEOUT = 327,
     T_AND = 328,
     T_IS = 329,
     T_WITH = 330,
     T_ASSERT = 331,
     T_SQL = 332,
     T_EXPECT = 333,
     T_ERROR = 334,
     T_PROMOTE = 335,
     T_PERFORM = 336,
     T_FAILOVER = 337,
     T_NETWORK = 338,
     T_DISCONNECT = 339,
     T_CONNECT = 340,
     T_SLEEP = 341,
     T_COMPOSE = 342,
     T_DOWN = 343,
     T_START = 344,
     T_STOP = 345,
     T_STOPPED = 346,
     T_KILL = 347,
     T_INJECT = 348,
     T_STATE = 349,
     T_ASSIGNED_STATE = 350,
     T_IN = 351,
     T_GROUP = 352,
     T_LBRACE = 353,
     T_RBRACE = 354,
     T_COMMA = 355,
     T_POSTGRES = 356,
     T_STAYS = 357,
     T_WHILE = 358,
     T_THROUGH = 359,
     T_SET = 360,
     T_LOGS = 361,
     T_NOT = 362,
     T_CONTAINS = 363,
     T_MATCHES = 364,
     T_INTEGER = 365,
     T_IDENT = 366,
     T_STRING = 367,
     T_BLOCK = 368,
     T_SHELL_ARGS = 369
   };
#endif
/* Tokens.  */
#define T_CLUSTER 258
#define T_MONITOR 259
#define T_NODE 260
#define T_CITUS_COORDINATOR 261
#define T_CITUS_WORKER 262
#define T_SETUP 263
#define T_TEARDOWN 264
#define T_STEP 265
#define T_SEQUENCE 266
#define T_EQUALS 267
#define T_IMAGE 268
#define T_IMAGE_TARGET 269
#define T_SSL 270
#define T_AUTH 271
#define T_AUTH_METHOD 272
#define T_FORMATION 273
#define T_NUM_SYNC 274
#define T_COORDINATOR 275
#define T_WORKER 276
#define T_ASYNC 277
#define T_NO_MONITOR 278
#define T_LAUNCH 279
#define T_CREATE 280
#define T_DEFERRED 281
#define T_IMMEDIATE 282
#define T_FALSE 283
#define T_TRUE 284
#define T_INITIALLY 285
#define T_VOLUME 286
#define T_LISTEN 287
#define T_CITUS_SECONDARY 288
#define T_CANDIDATE_PRIORITY 289
#define T_PORT 290
#define T_PASSWORD 291
#define T_MONITOR_PASSWORD 292
#define T_CITUS_CLUSTER_NAME 293
#define T_DEBIAN_CLUSTER 294
#define T_REPLICATION_QUORUM 295
#define T_REPLICATION_PASSWORD 296
#define T_EXTENSION_VERSION 297
#define T_BIND_SOURCE 298
#define T_LEGACY_STARTUP 299
#define T_FS_INIT 300
#define T_FS_SINGLE 301
#define T_FS_PRIMARY 302
#define T_FS_WAIT_PRIMARY 303
#define T_FS_WAIT_STANDBY 304
#define T_FS_DEMOTED 305
#define T_FS_DEMOTE_TIMEOUT 306
#define T_FS_DRAINING 307
#define T_FS_SECONDARY 308
#define T_FS_CATCHINGUP 309
#define T_FS_PREP_PROMOTION 310
#define T_FS_STOP_REPLICATION 311
#define T_FS_MAINTENANCE 312
#define T_FS_JOIN_PRIMARY 313
#define T_FS_APPLY_SETTINGS 314
#define T_FS_PREPARE_MAINTENANCE 315
#define T_FS_WAIT_MAINTENANCE 316
#define T_FS_REPORT_LSN 317
#define T_FS_FAST_FORWARD 318
#define T_FS_JOIN_SECONDARY 319
#define T_FS_DROPPED 320
#define T_EXEC 321
#define T_EXEC_FAILS 322
#define T_RUN 323
#define T_PG_AUTOCTL 324
#define T_WAIT 325
#define T_UNTIL 326
#define T_TIMEOUT 327
#define T_AND 328
#define T_IS 329
#define T_WITH 330
#define T_ASSERT 331
#define T_SQL 332
#define T_EXPECT 333
#define T_ERROR 334
#define T_PROMOTE 335
#define T_PERFORM 336
#define T_FAILOVER 337
#define T_NETWORK 338
#define T_DISCONNECT 339
#define T_CONNECT 340
#define T_SLEEP 341
#define T_COMPOSE 342
#define T_DOWN 343
#define T_START 344
#define T_STOP 345
#define T_STOPPED 346
#define T_KILL 347
#define T_INJECT 348
#define T_STATE 349
#define T_ASSIGNED_STATE 350
#define T_IN 351
#define T_GROUP 352
#define T_LBRACE 353
#define T_RBRACE 354
#define T_COMMA 355
#define T_POSTGRES 356
#define T_STAYS 357
#define T_WHILE 358
#define T_THROUGH 359
#define T_SET 360
#define T_LOGS 361
#define T_NOT 362
#define T_CONTAINS 363
#define T_MATCHES 364
#define T_INTEGER 365
#define T_IDENT 366
#define T_STRING 367
#define T_BLOCK 368
#define T_SHELL_ARGS 369




/* Copy the first part of user declarations.  */
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



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 145 "test_spec_parse.y"
{
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}
/* Line 193 of yacc.c.  */
#line 475 "test_spec_parse.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 488 "test_spec_parse.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

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
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
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
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  21
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   587

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  115
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  63
/* YYNRULES -- Number of rules.  */
#define YYNRULES  205
/* YYNRULES -- Number of states.  */
#define YYNSTATES  334

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   369

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
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
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     8,    10,    12,    14,    16,    18,
      19,    25,    26,    29,    31,    33,    35,    37,    39,    41,
      43,    45,    47,    51,    55,    59,    63,    68,    73,    80,
      83,    86,    89,    92,    95,    98,   101,   102,   109,   110,
     113,   115,   117,   119,   121,   123,   125,   128,   131,   132,
     135,   137,   139,   140,   141,   146,   147,   155,   156,   159,
     161,   163,   165,   167,   169,   172,   175,   180,   183,   185,
     187,   189,   192,   195,   198,   201,   204,   207,   210,   213,
     216,   219,   222,   225,   229,   233,   236,   239,   243,   247,
     248,   251,   253,   255,   257,   259,   261,   263,   265,   267,
     269,   271,   273,   275,   277,   279,   283,   286,   290,   293,
     297,   300,   304,   307,   309,   311,   313,   318,   323,   325,
     329,   330,   333,   335,   337,   341,   345,   346,   356,   357,
     367,   375,   383,   389,   395,   402,   404,   406,   410,   414,
     415,   418,   421,   426,   427,   430,   434,   441,   448,   455,
     462,   466,   469,   472,   476,   480,   483,   485,   489,   492,
     497,   503,   511,   515,   519,   522,   525,   529,   533,   537,
     542,   546,   550,   551,   557,   563,   567,   572,   578,   583,
     589,   592,   593,   596,   598,   600,   602,   604,   606,   608,
     610,   612,   614,   616,   618,   620,   622,   624,   626,   628,
     630,   632,   634,   636,   638,   640
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     116,     0,    -1,   117,    -1,   116,   117,    -1,   118,    -1,
     140,    -1,   141,    -1,   142,    -1,   174,    -1,    -1,     3,
      98,   119,   120,    99,    -1,    -1,   120,   121,    -1,   122,
      -1,   123,    -1,   125,    -1,   126,    -1,   124,    -1,   127,
      -1,    43,    -1,    44,    -1,     4,    -1,     4,    39,   111,
      -1,     4,    14,   111,    -1,     4,    35,   110,    -1,     4,
      36,   112,    -1,     4,   111,    24,    26,    -1,     4,   111,
      30,    91,    -1,     4,   111,    24,    26,    36,   112,    -1,
      13,   112,    -1,    13,   111,    -1,    42,   111,    -1,    42,
     112,    -1,    15,   111,    -1,    16,   111,    -1,    17,   111,
      -1,    -1,    18,   128,   129,    98,   132,    99,    -1,    -1,
     129,   131,    -1,   111,    -1,   112,    -1,    16,    -1,     4,
      -1,     5,    -1,   130,    -1,    19,   110,    -1,    53,    28,
      -1,    -1,   132,   135,    -1,   111,    -1,     4,    -1,    -1,
      -1,   133,   134,   136,   138,    -1,    -1,     5,   111,   134,
     137,    98,   138,    99,    -1,    -1,   138,   139,    -1,    20,
      -1,    21,    -1,    22,    -1,    23,    -1,    26,    -1,    24,
      26,    -1,    25,    26,    -1,    25,    73,    24,    26,    -1,
      24,    27,    -1,    27,    -1,    32,    -1,    33,    -1,    34,
     110,    -1,    97,   110,    -1,    35,   110,    -1,    38,   111,
      -1,    39,   111,    -1,    15,   111,    -1,    16,   111,    -1,
      17,   111,    -1,    40,    29,    -1,    40,    28,    -1,    41,
     112,    -1,    37,   112,    -1,    31,   111,   111,    -1,    31,
     111,   112,    -1,     8,   143,    -1,     9,   143,    -1,    10,
     177,   143,    -1,    98,   144,    99,    -1,    -1,   144,   145,
      -1,   146,    -1,   152,    -1,   159,    -1,   160,    -1,   161,
      -1,   162,    -1,   164,    -1,   165,    -1,   166,    -1,   167,
      -1,   168,    -1,   171,    -1,   172,    -1,   173,    -1,    66,
     111,   114,    -1,    66,   111,    -1,    67,   111,   114,    -1,
      67,   111,    -1,    68,   111,   114,    -1,    68,   111,    -1,
      69,   111,   114,    -1,    69,   111,    -1,    69,    -1,    12,
      -1,    74,    -1,   111,    94,   147,   176,    -1,   111,    94,
     147,   111,    -1,   148,    -1,   149,    73,   148,    -1,    -1,
     104,   151,    -1,   176,    -1,   111,    -1,   151,   100,   176,
      -1,   151,   100,   111,    -1,    -1,    70,    71,   111,    94,
     147,   176,   153,   150,   158,    -1,    -1,    70,    71,   111,
      94,   147,   111,   154,   150,   158,    -1,    70,    71,   111,
      95,   147,   176,   158,    -1,    70,    71,   111,    95,   147,
     111,   158,    -1,    70,    71,   111,    91,   158,    -1,    70,
      71,   155,   156,   158,    -1,    70,    71,   148,    73,   149,
     158,    -1,   176,    -1,   111,    -1,   155,   100,   176,    -1,
     155,   100,   111,    -1,    -1,    96,   157,    -1,    97,   110,
      -1,   157,   100,    97,   110,    -1,    -1,    72,   110,    -1,
      75,    72,   110,    -1,    76,   111,    94,   147,   176,   158,
      -1,    76,   111,    94,   147,   111,   158,    -1,    76,   111,
      95,   147,   176,   158,    -1,    76,   111,    95,   147,   111,
     158,    -1,    77,   111,   113,    -1,    78,   113,    -1,    78,
      79,    -1,    78,    79,   111,    -1,    78,    79,   110,    -1,
      80,   163,    -1,   111,    -1,   163,   100,   111,    -1,    81,
      82,    -1,    81,    82,    97,   110,    -1,    81,    82,    96,
      18,   111,    -1,    81,    82,    96,    18,   111,    97,   110,
      -1,    83,    84,   111,    -1,    83,    85,   111,    -1,    86,
     110,    -1,    87,    88,    -1,    87,    89,   111,    -1,    87,
      90,   111,    -1,    87,    92,   111,    -1,    87,    93,   111,
     114,    -1,    90,   101,   133,    -1,    89,   101,   133,    -1,
      -1,   103,   170,    98,   144,    99,    -1,    76,   133,   102,
     176,   169,    -1,   105,   111,   111,    -1,   106,   111,   108,
     112,    -1,   106,   111,   107,   108,   112,    -1,   106,   111,
     109,   112,    -1,   106,   111,   107,   109,   112,    -1,    11,
     175,    -1,    -1,   175,   177,    -1,    45,    -1,    46,    -1,
      47,    -1,    48,    -1,    49,    -1,    50,    -1,    51,    -1,
      52,    -1,    53,    -1,    54,    -1,    55,    -1,    56,    -1,
      57,    -1,    58,    -1,    59,    -1,    60,    -1,    61,    -1,
      62,    -1,    63,    -1,    64,    -1,    65,    -1,   111,    -1,
     112,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   212,   212,   213,   217,   218,   219,   220,   221,   234,
     233,   243,   245,   249,   250,   251,   252,   253,   254,   255,
     256,   269,   273,   280,   287,   293,   300,   307,   314,   327,
     333,   343,   349,   359,   369,   375,   386,   385,   402,   404,
     413,   414,   415,   416,   417,   421,   426,   430,   436,   438,
     457,   458,   467,   484,   483,   491,   490,   498,   500,   504,
     509,   514,   518,   522,   528,   533,   537,   542,   546,   550,
     554,   558,   562,   566,   570,   576,   582,   587,   592,   597,
     601,   605,   611,   617,   631,   652,   659,   670,   688,   703,
     706,   714,   715,   716,   717,   718,   719,   720,   721,   722,
     723,   724,   725,   726,   727,   741,   748,   754,   761,   767,
     774,   780,   788,   794,   821,   821,   832,   847,   865,   866,
     881,   883,   887,   895,   903,   910,   922,   921,   933,   932,
     943,   952,   961,   968,   982,   997,  1003,  1010,  1016,  1029,
    1031,  1035,  1040,  1048,  1049,  1050,  1061,  1069,  1077,  1085,
    1103,  1118,  1125,  1129,  1135,  1148,  1156,  1164,  1185,  1192,
    1199,  1207,  1223,  1229,  1242,  1256,  1260,  1266,  1272,  1298,
    1332,  1338,  1355,  1355,  1360,  1379,  1404,  1413,  1422,  1431,
    1447,  1450,  1452,  1474,  1475,  1476,  1477,  1478,  1479,  1480,
    1481,  1482,  1483,  1484,  1485,  1486,  1487,  1488,  1489,  1490,
    1491,  1492,  1493,  1494,  1502,  1503
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "T_CLUSTER", "T_MONITOR", "T_NODE",
  "T_CITUS_COORDINATOR", "T_CITUS_WORKER", "T_SETUP", "T_TEARDOWN",
  "T_STEP", "T_SEQUENCE", "T_EQUALS", "T_IMAGE", "T_IMAGE_TARGET", "T_SSL",
  "T_AUTH", "T_AUTH_METHOD", "T_FORMATION", "T_NUM_SYNC", "T_COORDINATOR",
  "T_WORKER", "T_ASYNC", "T_NO_MONITOR", "T_LAUNCH", "T_CREATE",
  "T_DEFERRED", "T_IMMEDIATE", "T_FALSE", "T_TRUE", "T_INITIALLY",
  "T_VOLUME", "T_LISTEN", "T_CITUS_SECONDARY", "T_CANDIDATE_PRIORITY",
  "T_PORT", "T_PASSWORD", "T_MONITOR_PASSWORD", "T_CITUS_CLUSTER_NAME",
  "T_DEBIAN_CLUSTER", "T_REPLICATION_QUORUM", "T_REPLICATION_PASSWORD",
  "T_EXTENSION_VERSION", "T_BIND_SOURCE", "T_LEGACY_STARTUP", "T_FS_INIT",
  "T_FS_SINGLE", "T_FS_PRIMARY", "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY",
  "T_FS_DEMOTED", "T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
  "T_FS_CATCHINGUP", "T_FS_PREP_PROMOTION", "T_FS_STOP_REPLICATION",
  "T_FS_MAINTENANCE", "T_FS_JOIN_PRIMARY", "T_FS_APPLY_SETTINGS",
  "T_FS_PREPARE_MAINTENANCE", "T_FS_WAIT_MAINTENANCE", "T_FS_REPORT_LSN",
  "T_FS_FAST_FORWARD", "T_FS_JOIN_SECONDARY", "T_FS_DROPPED", "T_EXEC",
  "T_EXEC_FAILS", "T_RUN", "T_PG_AUTOCTL", "T_WAIT", "T_UNTIL",
  "T_TIMEOUT", "T_AND", "T_IS", "T_WITH", "T_ASSERT", "T_SQL", "T_EXPECT",
  "T_ERROR", "T_PROMOTE", "T_PERFORM", "T_FAILOVER", "T_NETWORK",
  "T_DISCONNECT", "T_CONNECT", "T_SLEEP", "T_COMPOSE", "T_DOWN", "T_START",
  "T_STOP", "T_STOPPED", "T_KILL", "T_INJECT", "T_STATE",
  "T_ASSIGNED_STATE", "T_IN", "T_GROUP", "T_LBRACE", "T_RBRACE", "T_COMMA",
  "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH", "T_SET", "T_LOGS",
  "T_NOT", "T_CONTAINS", "T_MATCHES", "T_INTEGER", "T_IDENT", "T_STRING",
  "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec", "spec_item",
  "cluster_block", "@1", "cluster_item_list", "cluster_item",
  "monitor_line", "image_line", "extension_version_line", "ssl_line",
  "auth_line", "formation_block", "@2", "formation_opt_list", "bare_name",
  "formation_opt", "node_list", "node_name", "init_node_slot", "node_line",
  "@3", "@4", "node_opt_list", "node_opt", "setup_block", "teardown_block",
  "named_step", "cmd_block", "cmd_list", "step_cmd", "exec_cmd",
  "state_op", "wait_multi_condition", "wait_multi_condition_list",
  "opt_passing_through", "pass_state_list", "wait_cmd", "@5", "@6",
  "state_name_list", "opt_in_group", "group_items", "opt_timeout",
  "assert_cmd", "sql_cmd", "expect_cmd", "promote_cmd", "promote_list",
  "perform_cmd", "network_cmd", "sleep_cmd", "compose_cmd",
  "postgres_ctl_cmd", "while_body", "@7", "stays_while_cmd",
  "set_monitor_cmd", "logs_cmd", "sequence_block", "sequence_names",
  "fsm_state", "ident_or_string", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   115,   116,   116,   117,   117,   117,   117,   117,   119,
     118,   120,   120,   121,   121,   121,   121,   121,   121,   121,
     121,   122,   122,   122,   122,   122,   122,   122,   122,   123,
     123,   124,   124,   125,   126,   126,   128,   127,   129,   129,
     130,   130,   130,   130,   130,   131,   131,   131,   132,   132,
     133,   133,   134,   136,   135,   137,   135,   138,   138,   139,
     139,   139,   139,   139,   139,   139,   139,   139,   139,   139,
     139,   139,   139,   139,   139,   139,   139,   139,   139,   139,
     139,   139,   139,   139,   139,   140,   141,   142,   143,   144,
     144,   145,   145,   145,   145,   145,   145,   145,   145,   145,
     145,   145,   145,   145,   145,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   147,   147,   148,   148,   149,   149,
     150,   150,   151,   151,   151,   151,   153,   152,   154,   152,
     152,   152,   152,   152,   152,   155,   155,   155,   155,   156,
     156,   157,   157,   158,   158,   158,   159,   159,   159,   159,
     160,   161,   161,   161,   161,   162,   163,   163,   164,   164,
     164,   164,   165,   165,   166,   167,   167,   167,   167,   167,
     168,   168,   170,   169,   171,   172,   173,   173,   173,   173,
     174,   175,   175,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   177,   177
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     1,     1,     0,
       5,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     3,     3,     4,     4,     6,     2,
       2,     2,     2,     2,     2,     2,     0,     6,     0,     2,
       1,     1,     1,     1,     1,     1,     2,     2,     0,     2,
       1,     1,     0,     0,     4,     0,     7,     0,     2,     1,
       1,     1,     1,     1,     2,     2,     4,     2,     1,     1,
       1,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     3,     3,     2,     2,     3,     3,     0,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     2,     3,     2,     3,
       2,     3,     2,     1,     1,     1,     4,     4,     1,     3,
       0,     2,     1,     1,     3,     3,     0,     9,     0,     9,
       7,     7,     5,     5,     6,     1,     1,     3,     3,     0,
       2,     2,     4,     0,     2,     3,     6,     6,     6,     6,
       3,     2,     2,     3,     3,     2,     1,     3,     2,     4,
       5,     7,     3,     3,     2,     2,     3,     3,     3,     4,
       3,     3,     0,     5,     5,     3,     4,     5,     4,     5,
       2,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   181,     0,     2,     4,     5,
       6,     7,     8,     9,    89,    85,    86,   204,   205,     0,
     180,     1,     3,    11,     0,    87,   182,     0,     0,     0,
       0,   113,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    88,     0,     0,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,    21,     0,     0,     0,     0,    36,     0,    19,    20,
      10,    12,    13,    14,    17,    15,    16,    18,   106,   108,
     110,   112,     0,    51,    50,     0,     0,   152,   151,   156,
     155,   158,     0,     0,   164,   165,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    30,
      29,    33,    34,    35,    38,    31,    32,   105,   107,   109,
     111,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   194,   195,   196,   197,   198,   199,   200,   201,
     202,   203,   136,     0,   139,   135,     0,     0,     0,   150,
     154,   153,     0,     0,     0,   162,   163,   166,   167,   168,
       0,    50,   171,   170,   175,     0,     0,     0,    23,    24,
      25,    22,     0,     0,     0,   143,     0,     0,     0,     0,
       0,   143,   114,   115,     0,     0,     0,   157,     0,   159,
     169,     0,     0,   176,   178,    26,    27,    43,    44,    42,
       0,     0,    48,    40,    41,    45,    39,     0,     0,   132,
       0,     0,     0,   118,   143,     0,   140,   138,   137,   133,
     143,   143,   143,   143,   172,   174,   160,   177,   179,     0,
      46,    47,     0,   144,     0,   128,   126,   143,   143,     0,
       0,   134,   141,     0,   147,   146,   149,   148,     0,     0,
      28,     0,    37,    52,    49,   145,   120,   120,   131,   130,
       0,   119,     0,    89,   161,    52,    53,     0,   143,   143,
     117,   116,   142,     0,    55,    57,   123,   121,   122,   129,
     127,   173,     0,    54,     0,    57,     0,     0,     0,    59,
      60,    61,    62,     0,     0,    63,    68,     0,    69,    70,
       0,     0,     0,     0,     0,     0,     0,     0,    58,   125,
     124,     0,    76,    77,    78,    64,    67,    65,     0,     0,
      71,    73,    82,    74,    75,    80,    79,    81,    72,    56,
       0,    83,    84,    66
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,    23,    27,    71,    72,    73,    74,
      75,    76,    77,   114,   174,   205,   206,   232,    85,   266,
     254,   275,   282,   283,   308,     9,    10,    11,    15,    24,
      46,    47,   184,   143,   214,   268,   277,    48,   257,   256,
     144,   181,   216,   209,    49,    50,    51,    52,    90,    53,
      54,    55,    56,    57,   225,   248,    58,    59,    60,    12,
      20,   145,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -163
static const yytype_int16 yypact[] =
{
      61,   -84,   -47,   -47,   -83,  -163,    35,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,   -47,
     -83,  -163,  -163,  -163,   415,  -163,  -163,     6,   -50,   -34,
     -32,   -30,    29,     4,   -19,   -66,     3,    22,     9,    10,
     -35,    24,    30,  -163,    19,    23,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,    -9,   -23,    39,    40,    41,  -163,   -16,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,    18,    26,
      27,    44,   146,  -163,    12,    33,    54,   -12,  -163,  -163,
      68,    21,    66,    67,  -163,  -163,    74,   101,   102,   103,
       5,     5,   104,   -22,   105,    69,   106,   109,     1,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,   -11,   144,   -40,  -163,     8,     8,   522,  -163,
    -163,  -163,   132,   226,   135,  -163,  -163,  -163,  -163,  -163,
     133,  -163,  -163,  -163,  -163,     0,   136,   137,  -163,  -163,
    -163,  -163,   224,   160,    -1,   -33,     8,     8,   141,   156,
     177,   -33,  -163,  -163,   213,   244,   151,  -163,   145,  -163,
    -163,   143,   167,  -163,  -163,   245,  -163,  -163,  -163,  -163,
     170,   254,  -163,  -163,  -163,  -163,  -163,   173,   212,  -163,
     280,   311,   191,  -163,    -7,   176,   187,  -163,  -163,  -163,
     -33,   -33,   -33,   -33,  -163,  -163,   214,  -163,  -163,   198,
    -163,  -163,     2,  -163,   202,   240,   241,   -33,   -33,     8,
     141,  -163,  -163,   218,  -163,  -163,  -163,  -163,   219,   206,
    -163,   207,  -163,  -163,  -163,  -163,   215,   215,  -163,  -163,
     347,  -163,   210,  -163,  -163,  -163,  -163,   378,   -33,   -33,
    -163,  -163,  -163,   460,  -163,  -163,  -163,   221,  -163,  -163,
    -163,  -163,   225,   149,   414,  -163,   211,   235,   236,  -163,
    -163,  -163,  -163,    97,   -14,  -163,  -163,   237,  -163,  -163,
     239,   242,   238,   243,   266,    98,   267,   268,  -163,  -163,
    -163,   122,  -163,  -163,  -163,  -163,  -163,  -163,   327,    17,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
     354,  -163,  -163,  -163
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -163,  -163,   375,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,   -99,    88,
    -163,  -163,  -163,    99,  -163,  -163,  -163,  -163,    14,   119,
    -163,  -163,  -136,  -162,  -163,   126,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -147,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,  -163,
    -163,  -148,   365
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -118
static const yytype_int16 yytable[] =
{
     186,   162,   163,   197,   198,   104,    83,   251,    83,    83,
      61,   185,   317,    87,    13,   199,   213,    16,   200,    62,
     182,    63,    64,    65,    66,   172,   105,   106,    17,    18,
     107,   173,   218,    25,   219,    21,   221,   223,     1,   207,
     210,   211,   208,     2,     3,     4,     5,    88,    67,    68,
      69,    14,   201,    95,    96,    97,   179,    98,    99,   318,
     180,    78,   236,   238,     1,   207,   240,   241,   208,     2,
       3,     4,     5,   244,   245,   246,   247,    79,   261,    80,
     175,    81,   183,   176,   177,   165,   166,   167,   109,   110,
     258,   259,    86,    92,    93,   115,   116,   202,   150,   151,
      82,   252,   108,   260,    91,    70,   146,   147,   191,   192,
     203,   204,   271,   161,    89,    84,   161,   153,   154,   278,
      94,   279,   280,   315,   316,   100,   325,   326,   331,   332,
     102,   101,   117,   253,   103,   148,   310,   286,   287,   288,
     118,   119,   289,   290,   291,   292,   293,   294,   295,   296,
     111,   112,   113,   297,   298,   299,   300,   301,   120,   302,
     303,   304,   305,   306,   286,   287,   288,   149,   152,   289,
     290,   291,   292,   293,   294,   295,   296,   155,   156,   169,
     297,   298,   299,   300,   301,   157,   302,   303,   304,   305,
     306,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   158,   159,   160,   164,   168,   178,   170,   307,
     171,   329,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   187,   188,   189,   307,   190,   193,   194,
     195,   196,   212,   215,   224,   227,   226,   142,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   228,
     230,   229,   231,   233,   234,   239,   242,   243,   217,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     250,   249,   255,  -117,  -116,   262,   264,   263,   265,   267,
     272,   284,   312,   285,   220,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   313,   314,   319,   320,
     322,   330,   321,   274,   323,   222,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   324,   328,   327,
     333,    22,   273,   269,   311,    26,     0,     0,     0,     0,
       0,   235,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   237,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   270,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
       0,    28,    29,    30,    31,    32,     0,     0,     0,   276,
       0,    33,    34,    35,     0,    36,    37,     0,    38,     0,
       0,    39,    40,     0,    41,    42,     0,     0,     0,     0,
       0,     0,     0,     0,    43,     0,     0,     0,     0,     0,
      44,    45,     0,     0,     0,   309,    28,    29,    30,    31,
      32,     0,     0,     0,     0,     0,    33,    34,    35,     0,
      36,    37,     0,    38,     0,     0,    39,    40,     0,    41,
      42,     0,     0,     0,     0,     0,     0,     0,     0,   281,
       0,     0,     0,     0,     0,    44,    45,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141
};

static const yytype_int16 yycheck[] =
{
     148,   100,   101,     4,     5,    14,     4,     5,     4,     4,
       4,   147,    26,    79,    98,    16,   178,     3,    19,    13,
      12,    15,    16,    17,    18,    24,    35,    36,   111,   112,
      39,    30,   180,    19,   181,     0,   184,   185,     3,    72,
     176,   177,    75,     8,     9,    10,    11,   113,    42,    43,
      44,    98,    53,    88,    89,    90,    96,    92,    93,    73,
     100,   111,   210,   211,     3,    72,    73,   214,    75,     8,
       9,    10,    11,   220,   221,   222,   223,   111,   240,   111,
      91,   111,    74,    94,    95,   107,   108,   109,   111,   112,
     237,   238,   111,    84,    85,   111,   112,    98,   110,   111,
      71,    99,   111,   239,    82,    99,    94,    95,   108,   109,
     111,   112,   260,   111,   111,   111,   111,    96,    97,   267,
     110,   268,   269,    26,    27,   101,    28,    29,   111,   112,
     111,   101,   114,   232,   111,   102,   284,    15,    16,    17,
     114,   114,    20,    21,    22,    23,    24,    25,    26,    27,
     111,   111,   111,    31,    32,    33,    34,    35,   114,    37,
      38,    39,    40,    41,    15,    16,    17,   113,   100,    20,
      21,    22,    23,    24,    25,    26,    27,   111,   111,   110,
      31,    32,    33,    34,    35,   111,    37,    38,    39,    40,
      41,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,   111,   111,   111,   111,   111,    73,   112,    97,
     111,    99,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,   111,    18,   110,    97,   114,   112,   112,
      26,    91,   111,    97,   103,   112,   111,   111,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,   112,
     110,    36,    28,   110,    72,    94,   110,   100,   111,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
     112,    97,   110,    73,    73,    97,   110,    98,   111,   104,
     110,   100,   111,    98,   111,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,   111,   111,   111,   110,
     112,    24,   110,   265,   111,   111,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,   111,   110,   112,
      26,     6,   263,   257,   285,    20,    -1,    -1,    -1,    -1,
      -1,   111,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   111,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   111,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      -1,    66,    67,    68,    69,    70,    -1,    -1,    -1,   111,
      -1,    76,    77,    78,    -1,    80,    81,    -1,    83,    -1,
      -1,    86,    87,    -1,    89,    90,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    99,    -1,    -1,    -1,    -1,    -1,
     105,   106,    -1,    -1,    -1,   111,    66,    67,    68,    69,
      70,    -1,    -1,    -1,    -1,    -1,    76,    77,    78,    -1,
      80,    81,    -1,    83,    -1,    -1,    86,    87,    -1,    89,
      90,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    99,
      -1,    -1,    -1,    -1,    -1,   105,   106,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   116,   117,   118,   140,
     141,   142,   174,    98,    98,   143,   143,   111,   112,   177,
     175,     0,   117,   119,   144,   143,   177,   120,    66,    67,
      68,    69,    70,    76,    77,    78,    80,    81,    83,    86,
      87,    89,    90,    99,   105,   106,   145,   146,   152,   159,
     160,   161,   162,   164,   165,   166,   167,   168,   171,   172,
     173,     4,    13,    15,    16,    17,    18,    42,    43,    44,
      99,   121,   122,   123,   124,   125,   126,   127,   111,   111,
     111,   111,    71,     4,   111,   133,   111,    79,   113,   111,
     163,    82,    84,    85,   110,    88,    89,    90,    92,    93,
     101,   101,   111,   111,    14,    35,    36,    39,   111,   111,
     112,   111,   111,   111,   128,   111,   112,   114,   114,   114,
     114,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,   111,   148,   155,   176,    94,    95,   102,   113,
     110,   111,   100,    96,    97,   111,   111,   111,   111,   111,
     111,   111,   133,   133,   111,   107,   108,   109,   111,   110,
     112,   111,    24,    30,   129,    91,    94,    95,    73,    96,
     100,   156,    12,    74,   147,   147,   176,   111,    18,   110,
     114,   108,   109,   112,   112,    26,    91,     4,     5,    16,
      19,    53,    98,   111,   112,   130,   131,    72,    75,   158,
     147,   147,   111,   148,   149,    97,   157,   111,   176,   158,
     111,   176,   111,   176,   103,   169,   111,   112,   112,    36,
     110,    28,   132,   110,    72,   111,   176,   111,   176,    94,
      73,   158,   110,   100,   158,   158,   158,   158,   170,    97,
     112,     5,    99,   133,   135,   110,   154,   153,   158,   158,
     147,   148,    97,    98,   110,   111,   134,   104,   150,   150,
     111,   176,   110,   144,   134,   136,   111,   151,   176,   158,
     158,    99,   137,   138,   100,    98,    15,    16,    17,    20,
      21,    22,    23,    24,    25,    26,    27,    31,    32,    33,
      34,    35,    37,    38,    39,    40,    41,    97,   139,   111,
     176,   138,   111,   111,   111,    26,    27,    26,    73,   111,
     110,   110,   112,   111,   111,    28,    29,   112,   110,    99,
      24,   111,   112,    26
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
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



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
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
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

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
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 9:
#line 234 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	;}
    break;

  case 19:
#line 255 "test_spec_parse.y"
    { current_spec->cluster.bindSource = true; ;}
    break;

  case 20:
#line 256 "test_spec_parse.y"
    { current_spec->cluster.legacyStartup = true; ;}
    break;

  case 21:
#line 270 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
	;}
    break;

  case 22:
#line 274 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 23:
#line 281 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 24:
#line 288 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[(3) - (3)].ival);
	;}
    break;

  case 25:
#line 294 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 26:
#line 301 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 27:
#line 308 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 28:
#line 315 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (6)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (6)].str));
		/* password for second monitor not yet stored */
		free((yyvsp[(6) - (6)].str));
	;}
    break;

  case 29:
#line 328 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 30:
#line 334 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 31:
#line 344 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 32:
#line 350 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 33:
#line 360 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 34:
#line 370 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 35:
#line 376 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 36:
#line 386 "test_spec_parse.y"
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
	;}
    break;

  case 40:
#line 413 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 41:
#line 414 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 42:
#line 415 "test_spec_parse.y"
    { (yyval.str) = strdup("auth"); ;}
    break;

  case 43:
#line 416 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 44:
#line 417 "test_spec_parse.y"
    { (yyval.str) = strdup("node"); ;}
    break;

  case 45:
#line 422 "test_spec_parse.y"
    {
		strlcpy(current_formation->name, (yyvsp[(1) - (1)].str), sizeof(current_formation->name));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 46:
#line 427 "test_spec_parse.y"
    {
		current_formation->numSync = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 47:
#line 431 "test_spec_parse.y"
    {
		current_formation->disableSecondary = true;
	;}
    break;

  case 50:
#line 457 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 51:
#line 458 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 52:
#line 467 "test_spec_parse.y"
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
	;}
    break;

  case 53:
#line 484 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(1) - (2)].str), sizeof(current_node->name));
		free((yyvsp[(1) - (2)].str));
	;}
    break;

  case 55:
#line 491 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(2) - (3)].str), sizeof(current_node->name));
		free((yyvsp[(2) - (3)].str));
	;}
    break;

  case 59:
#line 505 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 60:
#line 510 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 61:
#line 515 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 62:
#line 519 "test_spec_parse.y"
    {
		current_node->noMonitor = true;
	;}
    break;

  case 63:
#line 523 "test_spec_parse.y"
    {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 64:
#line 529 "test_spec_parse.y"
    {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	;}
    break;

  case 65:
#line 534 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
	;}
    break;

  case 66:
#line 538 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 67:
#line 543 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 68:
#line 547 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 69:
#line 551 "test_spec_parse.y"
    {
		current_node->listen = true;
	;}
    break;

  case 70:
#line 555 "test_spec_parse.y"
    {
		current_node->citusSecondary = true;
	;}
    break;

  case 71:
#line 559 "test_spec_parse.y"
    {
		current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 72:
#line 563 "test_spec_parse.y"
    {
		current_node->group = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 73:
#line 567 "test_spec_parse.y"
    {
		current_node->pgPort = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 74:
#line 571 "test_spec_parse.y"
    {
		strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 75:
#line 577 "test_spec_parse.y"
    {
		strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 76:
#line 583 "test_spec_parse.y"
    {
		strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str), sizeof(current_node->ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 77:
#line 588 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 78:
#line 593 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 79:
#line 598 "test_spec_parse.y"
    {
		current_node->replicationQuorum = true;
	;}
    break;

  case 80:
#line 602 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 81:
#line 606 "test_spec_parse.y"
    {
		strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 82:
#line 612 "test_spec_parse.y"
    {
		strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 83:
#line 618 "test_spec_parse.y"
    {
		/* volume <name> <containerPath> — adds a named Docker volume */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, (yyvsp[(2) - (3)].str),
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, (yyvsp[(3) - (3)].str),
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 84:
#line 632 "test_spec_parse.y"
    {
		/* volume <name> "/path/with spaces" */
		int vi = current_node->volumeCount;
		if (vi < PGAF_MAX_NODE_VOLUMES)
		{
			strlcpy(current_node->volumes[vi].name, (yyvsp[(2) - (3)].str),
			        sizeof(current_node->volumes[0].name));
			strlcpy(current_node->volumes[vi].path, (yyvsp[(3) - (3)].str),
			        sizeof(current_node->volumes[0].path));
			current_node->volumeCount++;
		}
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 85:
#line 653 "test_spec_parse.y"
    {
		current_spec->setup = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 86:
#line 660 "test_spec_parse.y"
    {
		current_spec->teardown = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 87:
#line 671 "test_spec_parse.y"
    {
		TestStep *s = (yyvsp[(3) - (3)].step);
		strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
		free((yyvsp[(2) - (3)].str));
		register_step(current_spec, s);
	;}
    break;

  case 88:
#line 689 "test_spec_parse.y"
    {
		/* post-process: CMD_SQL immediately before CMD_EXPECT_ERROR */
		for (TestCmd *c = (yyvsp[(2) - (3)].step)->commands; c; c = c->next)
		{
			if (c->kind == CMD_SQL && c->next &&
			    c->next->kind == CMD_EXPECT_ERROR)
				c->allowError = true;
		}
		(yyval.step) = (yyvsp[(2) - (3)].step);
	;}
    break;

  case 89:
#line 703 "test_spec_parse.y"
    {
		(yyval.step) = make_step("");
	;}
    break;

  case 90:
#line 707 "test_spec_parse.y"
    {
		if ((yyvsp[(2) - (2)].cmd)) append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
		(yyval.step) = (yyvsp[(1) - (2)].step);
	;}
    break;

  case 91:
#line 714 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 92:
#line 715 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 93:
#line 716 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 94:
#line 717 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 95:
#line 718 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 96:
#line 719 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 97:
#line 720 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 98:
#line 721 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 99:
#line 722 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 100:
#line 723 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 101:
#line 724 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 102:
#line 725 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 103:
#line 726 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 104:
#line 727 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 105:
#line 742 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 106:
#line 749 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 107:
#line 755 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 108:
#line 762 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 109:
#line 768 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 110:
#line 775 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 111:
#line 781 "test_spec_parse.y"
    {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 112:
#line 789 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 113:
#line 795 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	;}
    break;

  case 116:
#line 833 "test_spec_parse.y"
    {
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  (yyvsp[(1) - (4)].str),
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], (yyvsp[(4) - (4)].str),
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free((yyvsp[(1) - (4)].str));
	;}
    break;

  case 117:
#line 848 "test_spec_parse.y"
    {
		if (!current_wait_cmd)
			current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
		int i = current_wait_cmd->waitStateCount;
		if (i < PGAF_MAX_WAIT_STATES)
		{
			strlcpy(current_wait_cmd->waitNodes[i],  (yyvsp[(1) - (4)].str),
			        sizeof(current_wait_cmd->waitNodes[0]));
			strlcpy(current_wait_cmd->waitStates[i], (yyvsp[(4) - (4)].str),
			        sizeof(current_wait_cmd->waitStates[0]));
			current_wait_cmd->waitStateCount++;
		}
		free((yyvsp[(1) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 122:
#line 888 "test_spec_parse.y"
    {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 123:
#line 896 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 124:
#line 904 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 125:
#line 911 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 126:
#line 922 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); ;}
    break;

  case 127:
#line 927 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 128:
#line 933 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); free((yyvsp[(6) - (6)].str)); ;}
    break;

  case 129:
#line 938 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 130:
#line 944 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str));
	;}
    break;

  case 131:
#line 953 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str)); free((yyvsp[(6) - (7)].str));
	;}
    break;

  case 132:
#line 962 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		free((yyvsp[(3) - (5)].str));
	;}
    break;

  case 133:
#line 969 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 134:
#line 983 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 135:
#line 998 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 136:
#line 1004 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 137:
#line 1011 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 138:
#line 1017 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 141:
#line 1036 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 142:
#line 1041 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(4) - (4)].ival);
	;}
    break;

  case 143:
#line 1048 "test_spec_parse.y"
    { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; ;}
    break;

  case 144:
#line 1049 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;

  case 145:
#line 1050 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(3) - (3)].ival); ;}
    break;

  case 146:
#line 1062 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 147:
#line 1070 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 148:
#line 1078 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 149:
#line 1086 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 150:
#line 1104 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 151:
#line 1119 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 152:
#line 1126 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	;}
    break;

  case 153:
#line 1130 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->state));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 154:
#line 1136 "test_spec_parse.y"
    {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[(3) - (3)].ival));
	;}
    break;

  case 155:
#line 1149 "test_spec_parse.y"
    {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	;}
    break;

  case 156:
#line 1157 "test_spec_parse.y"
    {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 157:
#line 1165 "test_spec_parse.y"
    {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 158:
#line 1186 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 159:
#line 1193 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(4) - (4)].ival);
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 160:
#line 1200 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 161:
#line 1208 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (7)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(7) - (7)].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (7)].str));
	;}
    break;

  case 162:
#line 1224 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 163:
#line 1230 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 164:
#line 1243 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 165:
#line 1257 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	;}
    break;

  case 166:
#line 1261 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 167:
#line 1267 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 168:
#line 1273 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 169:
#line 1299 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_INJECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(3) - (4)].str), sizeof((yyval.cmd)->expected));  /* image */

		/* Split T_SHELL_ARGS: "<src-path> <svc>:<dst-path>" */
		char tmp[4096];
		strlcpy(tmp, (yyvsp[(4) - (4)].str), sizeof(tmp));
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
		free((yyvsp[(3) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 170:
#line 1333 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 171:
#line 1339 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 172:
#line 1355 "test_spec_parse.y"
    { pgaf_next_brace_is_while = 1; ;}
    break;

  case 173:
#line 1356 "test_spec_parse.y"
    { (yyval.step) = (yyvsp[(4) - (5)].step); ;}
    break;

  case 174:
#line 1361 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) - (5)].step)->commands : NULL;
		free((yyvsp[(2) - (5)].str));
	;}
    break;

  case 175:
#line 1380 "test_spec_parse.y"
    {
		/* only "set monitor <svc>" is supported; $2 must be "monitor" */
		if (strcmp((yyvsp[(2) - (3)].str), "monitor") != 0)
		{
			fprintf(stderr, "pgaftest: unknown 'set' target '%s' (expected 'monitor')\n", (yyvsp[(2) - (3)].str));
			free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
			YYERROR;
		}
		(yyval.cmd) = make_cmd(CMD_SET_MONITOR);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 176:
#line 1405 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 177:
#line 1414 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 178:
#line 1423 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 179:
#line 1432 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 182:
#line 1453 "test_spec_parse.y"
    {
		int i = current_spec->sequenceLength;
		if (i < PGAF_MAX_SEQ)
			current_spec->sequence[current_spec->sequenceLength++] = (yyvsp[(2) - (2)].str);
		else
		{
			fprintf(stderr, "pgaftest: too many steps in sequence (max %d)\n",
			        PGAF_MAX_SEQ);
			exit(1);
		}
	;}
    break;

  case 183:
#line 1474 "test_spec_parse.y"
    { (yyval.str) = "init"; ;}
    break;

  case 184:
#line 1475 "test_spec_parse.y"
    { (yyval.str) = "single"; ;}
    break;

  case 185:
#line 1476 "test_spec_parse.y"
    { (yyval.str) = "primary"; ;}
    break;

  case 186:
#line 1477 "test_spec_parse.y"
    { (yyval.str) = "wait_primary"; ;}
    break;

  case 187:
#line 1478 "test_spec_parse.y"
    { (yyval.str) = "wait_standby"; ;}
    break;

  case 188:
#line 1479 "test_spec_parse.y"
    { (yyval.str) = "demoted"; ;}
    break;

  case 189:
#line 1480 "test_spec_parse.y"
    { (yyval.str) = "demote_timeout"; ;}
    break;

  case 190:
#line 1481 "test_spec_parse.y"
    { (yyval.str) = "draining"; ;}
    break;

  case 191:
#line 1482 "test_spec_parse.y"
    { (yyval.str) = "secondary"; ;}
    break;

  case 192:
#line 1483 "test_spec_parse.y"
    { (yyval.str) = "catchingup"; ;}
    break;

  case 193:
#line 1484 "test_spec_parse.y"
    { (yyval.str) = "prepare_promotion"; ;}
    break;

  case 194:
#line 1485 "test_spec_parse.y"
    { (yyval.str) = "stop_replication"; ;}
    break;

  case 195:
#line 1486 "test_spec_parse.y"
    { (yyval.str) = "maintenance"; ;}
    break;

  case 196:
#line 1487 "test_spec_parse.y"
    { (yyval.str) = "join_primary"; ;}
    break;

  case 197:
#line 1488 "test_spec_parse.y"
    { (yyval.str) = "apply_settings"; ;}
    break;

  case 198:
#line 1489 "test_spec_parse.y"
    { (yyval.str) = "prepare_maintenance"; ;}
    break;

  case 199:
#line 1490 "test_spec_parse.y"
    { (yyval.str) = "wait_maintenance"; ;}
    break;

  case 200:
#line 1491 "test_spec_parse.y"
    { (yyval.str) = "report_lsn"; ;}
    break;

  case 201:
#line 1492 "test_spec_parse.y"
    { (yyval.str) = "fast_forward"; ;}
    break;

  case 202:
#line 1493 "test_spec_parse.y"
    { (yyval.str) = "join_secondary"; ;}
    break;

  case 203:
#line 1494 "test_spec_parse.y"
    { (yyval.str) = "dropped"; ;}
    break;

  case 204:
#line 1502 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 205:
#line 1503 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3503 "test_spec_parse.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
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

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
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
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
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
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 1506 "test_spec_parse.y"


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

