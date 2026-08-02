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
     T_NO_AUTOPILOT = 279,
     T_LAUNCH = 280,
     T_CREATE = 281,
     T_DEFERRED = 282,
     T_IMMEDIATE = 283,
     T_FALSE = 284,
     T_TRUE = 285,
     T_INITIALLY = 286,
     T_VOLUME = 287,
     T_LISTEN = 288,
     T_CITUS_SECONDARY = 289,
     T_CANDIDATE_PRIORITY = 290,
     T_PORT = 291,
     T_PASSWORD = 292,
     T_MONITOR_PASSWORD = 293,
     T_CITUS_CLUSTER_NAME = 294,
     T_DEBIAN_CLUSTER = 295,
     T_REPLICATION_QUORUM = 296,
     T_REPLICATION_PASSWORD = 297,
     T_EXTENSION_VERSION = 298,
     T_BIND_SOURCE = 299,
     T_LEGACY_STARTUP = 300,
     T_REGION = 301,
     T_NODEINI = 302,
     T_FS_INIT = 303,
     T_FS_SINGLE = 304,
     T_FS_PRIMARY = 305,
     T_FS_WAIT_PRIMARY = 306,
     T_FS_WAIT_STANDBY = 307,
     T_FS_DEMOTED = 308,
     T_FS_DEMOTE_TIMEOUT = 309,
     T_FS_DRAINING = 310,
     T_FS_SECONDARY = 311,
     T_FS_CATCHINGUP = 312,
     T_FS_PREP_PROMOTION = 313,
     T_FS_STOP_REPLICATION = 314,
     T_FS_MAINTENANCE = 315,
     T_FS_JOIN_PRIMARY = 316,
     T_FS_APPLY_SETTINGS = 317,
     T_FS_PREPARE_MAINTENANCE = 318,
     T_FS_WAIT_MAINTENANCE = 319,
     T_FS_REPORT_LSN = 320,
     T_FS_FAST_FORWARD = 321,
     T_FS_JOIN_SECONDARY = 322,
     T_FS_DROPPED = 323,
     T_EXEC = 324,
     T_EXEC_FAILS = 325,
     T_RUN = 326,
     T_PG_AUTOCTL = 327,
     T_WAIT = 328,
     T_UNTIL = 329,
     T_TIMEOUT = 330,
     T_AND = 331,
     T_IS = 332,
     T_WITH = 333,
     T_REPLAYS = 334,
     T_ASSERT = 335,
     T_SQL = 336,
     T_EXPECT = 337,
     T_ERROR = 338,
     T_PROMOTE = 339,
     T_PERFORM = 340,
     T_FAILOVER = 341,
     T_NETWORK = 342,
     T_DISCONNECT = 343,
     T_CONNECT = 344,
     T_SLEEP = 345,
     T_COMPOSE = 346,
     T_DOWN = 347,
     T_START = 348,
     T_STOP = 349,
     T_STOPPED = 350,
     T_KILL = 351,
     T_INJECT = 352,
     T_STATE = 353,
     T_ASSIGNED_STATE = 354,
     T_IN = 355,
     T_GROUP = 356,
     T_LBRACE = 357,
     T_RBRACE = 358,
     T_COMMA = 359,
     T_POSTGRES = 360,
     T_STAYS = 361,
     T_WHILE = 362,
     T_THROUGH = 363,
     T_SET = 364,
     T_GET = 365,
     T_FSM = 366,
     T_LOGS = 367,
     T_NOT = 368,
     T_CONTAINS = 369,
     T_MATCHES = 370,
     T_INTEGER = 371,
     T_IDENT = 372,
     T_STRING = 373,
     T_BLOCK = 374,
     T_SHELL_ARGS = 375
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
#define T_NO_AUTOPILOT 279
#define T_LAUNCH 280
#define T_CREATE 281
#define T_DEFERRED 282
#define T_IMMEDIATE 283
#define T_FALSE 284
#define T_TRUE 285
#define T_INITIALLY 286
#define T_VOLUME 287
#define T_LISTEN 288
#define T_CITUS_SECONDARY 289
#define T_CANDIDATE_PRIORITY 290
#define T_PORT 291
#define T_PASSWORD 292
#define T_MONITOR_PASSWORD 293
#define T_CITUS_CLUSTER_NAME 294
#define T_DEBIAN_CLUSTER 295
#define T_REPLICATION_QUORUM 296
#define T_REPLICATION_PASSWORD 297
#define T_EXTENSION_VERSION 298
#define T_BIND_SOURCE 299
#define T_LEGACY_STARTUP 300
#define T_REGION 301
#define T_NODEINI 302
#define T_FS_INIT 303
#define T_FS_SINGLE 304
#define T_FS_PRIMARY 305
#define T_FS_WAIT_PRIMARY 306
#define T_FS_WAIT_STANDBY 307
#define T_FS_DEMOTED 308
#define T_FS_DEMOTE_TIMEOUT 309
#define T_FS_DRAINING 310
#define T_FS_SECONDARY 311
#define T_FS_CATCHINGUP 312
#define T_FS_PREP_PROMOTION 313
#define T_FS_STOP_REPLICATION 314
#define T_FS_MAINTENANCE 315
#define T_FS_JOIN_PRIMARY 316
#define T_FS_APPLY_SETTINGS 317
#define T_FS_PREPARE_MAINTENANCE 318
#define T_FS_WAIT_MAINTENANCE 319
#define T_FS_REPORT_LSN 320
#define T_FS_FAST_FORWARD 321
#define T_FS_JOIN_SECONDARY 322
#define T_FS_DROPPED 323
#define T_EXEC 324
#define T_EXEC_FAILS 325
#define T_RUN 326
#define T_PG_AUTOCTL 327
#define T_WAIT 328
#define T_UNTIL 329
#define T_TIMEOUT 330
#define T_AND 331
#define T_IS 332
#define T_WITH 333
#define T_REPLAYS 334
#define T_ASSERT 335
#define T_SQL 336
#define T_EXPECT 337
#define T_ERROR 338
#define T_PROMOTE 339
#define T_PERFORM 340
#define T_FAILOVER 341
#define T_NETWORK 342
#define T_DISCONNECT 343
#define T_CONNECT 344
#define T_SLEEP 345
#define T_COMPOSE 346
#define T_DOWN 347
#define T_START 348
#define T_STOP 349
#define T_STOPPED 350
#define T_KILL 351
#define T_INJECT 352
#define T_STATE 353
#define T_ASSIGNED_STATE 354
#define T_IN 355
#define T_GROUP 356
#define T_LBRACE 357
#define T_RBRACE 358
#define T_COMMA 359
#define T_POSTGRES 360
#define T_STAYS 361
#define T_WHILE 362
#define T_THROUGH 363
#define T_SET 364
#define T_GET 365
#define T_FSM 366
#define T_LOGS 367
#define T_NOT 368
#define T_CONTAINS 369
#define T_MATCHES 370
#define T_INTEGER 371
#define T_IDENT 372
#define T_STRING 373
#define T_BLOCK 374
#define T_SHELL_ARGS 375




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
#line 487 "test_spec_parse.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 500 "test_spec_parse.c"

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
#define YYLAST   620

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  121
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  65
/* YYNRULES -- Number of rules.  */
#define YYNRULES  214
/* YYNRULES -- Number of states.  */
#define YYNSTATES  355

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   375

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
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120
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
     161,   163,   165,   167,   169,   171,   174,   177,   182,   185,
     187,   189,   191,   194,   197,   200,   203,   206,   209,   212,
     215,   218,   221,   224,   227,   230,   233,   237,   241,   244,
     247,   251,   255,   256,   259,   261,   263,   265,   267,   269,
     271,   273,   275,   277,   279,   281,   283,   285,   287,   289,
     291,   295,   298,   302,   305,   309,   312,   316,   319,   321,
     323,   325,   330,   335,   337,   341,   342,   345,   347,   349,
     353,   357,   358,   368,   369,   379,   387,   395,   401,   408,
     414,   421,   423,   425,   429,   433,   434,   437,   440,   445,
     446,   449,   453,   460,   467,   474,   481,   485,   488,   491,
     495,   499,   502,   504,   508,   511,   516,   522,   530,   534,
     538,   544,   550,   553,   556,   560,   564,   568,   573,   577,
     581,   585,   586,   592,   598,   602,   607,   613,   618,   624,
     627,   628,   631,   633,   635,   637,   639,   641,   643,   645,
     647,   649,   651,   653,   655,   657,   659,   661,   663,   665,
     667,   669,   671,   673,   675
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     122,     0,    -1,   123,    -1,   122,   123,    -1,   124,    -1,
     146,    -1,   147,    -1,   148,    -1,   182,    -1,    -1,     3,
     102,   125,   126,   103,    -1,    -1,   126,   127,    -1,   128,
      -1,   129,    -1,   131,    -1,   132,    -1,   130,    -1,   133,
      -1,    44,    -1,    45,    -1,     4,    -1,     4,    40,   117,
      -1,     4,    14,   117,    -1,     4,    36,   116,    -1,     4,
      37,   118,    -1,     4,   117,    25,    27,    -1,     4,   117,
      31,    95,    -1,     4,   117,    25,    27,    37,   118,    -1,
      13,   118,    -1,    13,   117,    -1,    43,   117,    -1,    43,
     118,    -1,    15,   117,    -1,    16,   117,    -1,    17,   117,
      -1,    -1,    18,   134,   135,   102,   138,   103,    -1,    -1,
     135,   137,    -1,   117,    -1,   118,    -1,    16,    -1,     4,
      -1,     5,    -1,   136,    -1,    19,   116,    -1,    56,    29,
      -1,    -1,   138,   141,    -1,   117,    -1,     4,    -1,    -1,
      -1,   139,   140,   142,   144,    -1,    -1,     5,   117,   140,
     143,   102,   144,   103,    -1,    -1,   144,   145,    -1,    20,
      -1,    21,    -1,    22,    -1,    23,    -1,    24,    -1,    27,
      -1,    25,    27,    -1,    26,    27,    -1,    26,    76,    25,
      27,    -1,    25,    28,    -1,    28,    -1,    33,    -1,    34,
      -1,    35,   116,    -1,    46,   117,    -1,    46,   118,    -1,
     101,   116,    -1,    36,   116,    -1,    39,   117,    -1,    40,
     117,    -1,    15,   117,    -1,    16,   117,    -1,    17,   117,
      -1,    41,    30,    -1,    41,    29,    -1,    42,   118,    -1,
      38,   118,    -1,    32,   117,   117,    -1,    32,   117,   118,
      -1,     8,   149,    -1,     9,   149,    -1,    10,   185,   149,
      -1,   102,   150,   103,    -1,    -1,   150,   151,    -1,   152,
      -1,   158,    -1,   165,    -1,   166,    -1,   167,    -1,   168,
      -1,   170,    -1,   171,    -1,   173,    -1,   174,    -1,   175,
      -1,   176,    -1,   179,    -1,   180,    -1,   181,    -1,   172,
      -1,    69,   117,   120,    -1,    69,   117,    -1,    70,   117,
     120,    -1,    70,   117,    -1,    71,   117,   120,    -1,    71,
     117,    -1,    72,   117,   120,    -1,    72,   117,    -1,    72,
      -1,    12,    -1,    77,    -1,   117,    98,   153,   184,    -1,
     117,    98,   153,   117,    -1,   154,    -1,   155,    76,   154,
      -1,    -1,   108,   157,    -1,   184,    -1,   117,    -1,   157,
     104,   184,    -1,   157,   104,   117,    -1,    -1,    73,    74,
     117,    98,   153,   184,   159,   156,   164,    -1,    -1,    73,
      74,   117,    98,   153,   117,   160,   156,   164,    -1,    73,
      74,   117,    99,   153,   184,   164,    -1,    73,    74,   117,
      99,   153,   117,   164,    -1,    73,    74,   117,    95,   164,
      -1,    73,    74,   117,    79,   117,   164,    -1,    73,    74,
     161,   162,   164,    -1,    73,    74,   154,    76,   155,   164,
      -1,   184,    -1,   117,    -1,   161,   104,   184,    -1,   161,
     104,   117,    -1,    -1,   100,   163,    -1,   101,   116,    -1,
     163,   104,   101,   116,    -1,    -1,    75,   116,    -1,    78,
      75,   116,    -1,    80,   117,    98,   153,   184,   164,    -1,
      80,   117,    98,   153,   117,   164,    -1,    80,   117,    99,
     153,   184,   164,    -1,    80,   117,    99,   153,   117,   164,
      -1,    81,   117,   119,    -1,    82,   119,    -1,    82,    83,
      -1,    82,    83,   117,    -1,    82,    83,   116,    -1,    84,
     169,    -1,   117,    -1,   169,   104,   117,    -1,    85,    86,
      -1,    85,    86,   101,   116,    -1,    85,    86,   100,    18,
     117,    -1,    85,    86,   100,    18,   117,   101,   116,    -1,
      87,    88,   117,    -1,    87,    89,   117,    -1,    47,   109,
     117,   117,   117,    -1,    47,   110,   117,   117,   117,    -1,
      90,   116,    -1,    91,    92,    -1,    91,    93,   117,    -1,
      91,    94,   117,    -1,    91,    96,   117,    -1,    91,    97,
     117,   120,    -1,    94,   105,   139,    -1,    93,   105,   139,
      -1,   111,    10,   139,    -1,    -1,   107,   178,   102,   150,
     103,    -1,    80,   139,   106,   184,   177,    -1,   109,   117,
     117,    -1,   112,   117,   114,   118,    -1,   112,   117,   113,
     114,   118,    -1,   112,   117,   115,   118,    -1,   112,   117,
     113,   115,   118,    -1,    11,   183,    -1,    -1,   183,   185,
      -1,    48,    -1,    49,    -1,    50,    -1,    51,    -1,    52,
      -1,    53,    -1,    54,    -1,    55,    -1,    56,    -1,    57,
      -1,    58,    -1,    59,    -1,    60,    -1,    61,    -1,    62,
      -1,    63,    -1,    64,    -1,    65,    -1,    66,    -1,    67,
      -1,    68,    -1,   117,    -1,   118,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   216,   216,   217,   221,   222,   223,   224,   225,   238,
     237,   247,   249,   253,   254,   255,   256,   257,   258,   259,
     260,   273,   277,   284,   291,   297,   304,   311,   318,   331,
     337,   347,   353,   363,   373,   379,   390,   389,   406,   408,
     417,   418,   419,   420,   421,   425,   430,   434,   440,   442,
     461,   462,   471,   488,   487,   495,   494,   502,   504,   508,
     513,   518,   522,   526,   530,   536,   541,   545,   550,   554,
     558,   562,   566,   570,   575,   580,   584,   588,   594,   600,
     605,   610,   615,   619,   623,   629,   635,   649,   670,   677,
     688,   706,   721,   724,   732,   733,   734,   735,   736,   737,
     738,   739,   740,   741,   742,   743,   744,   745,   746,   747,
     761,   768,   774,   781,   787,   794,   800,   808,   814,   841,
     841,   852,   867,   885,   886,   901,   903,   907,   915,   923,
     930,   942,   941,   953,   952,   963,   972,   981,   995,  1003,
    1017,  1032,  1038,  1045,  1051,  1064,  1066,  1070,  1075,  1083,
    1084,  1085,  1096,  1104,  1112,  1120,  1138,  1153,  1160,  1164,
    1170,  1183,  1191,  1199,  1220,  1227,  1234,  1242,  1258,  1264,
    1285,  1293,  1308,  1322,  1326,  1332,  1338,  1364,  1398,  1404,
    1425,  1442,  1442,  1447,  1466,  1491,  1500,  1509,  1518,  1534,
    1537,  1539,  1561,  1562,  1563,  1564,  1565,  1566,  1567,  1568,
    1569,  1570,  1571,  1572,  1573,  1574,  1575,  1576,  1577,  1578,
    1579,  1580,  1581,  1589,  1590
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
  "T_WORKER", "T_ASYNC", "T_NO_MONITOR", "T_NO_AUTOPILOT", "T_LAUNCH",
  "T_CREATE", "T_DEFERRED", "T_IMMEDIATE", "T_FALSE", "T_TRUE",
  "T_INITIALLY", "T_VOLUME", "T_LISTEN", "T_CITUS_SECONDARY",
  "T_CANDIDATE_PRIORITY", "T_PORT", "T_PASSWORD", "T_MONITOR_PASSWORD",
  "T_CITUS_CLUSTER_NAME", "T_DEBIAN_CLUSTER", "T_REPLICATION_QUORUM",
  "T_REPLICATION_PASSWORD", "T_EXTENSION_VERSION", "T_BIND_SOURCE",
  "T_LEGACY_STARTUP", "T_REGION", "T_NODEINI", "T_FS_INIT", "T_FS_SINGLE",
  "T_FS_PRIMARY", "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
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
  "spec", "spec_item", "cluster_block", "@1", "cluster_item_list",
  "cluster_item", "monitor_line", "image_line", "extension_version_line",
  "ssl_line", "auth_line", "formation_block", "@2", "formation_opt_list",
  "bare_name", "formation_opt", "node_list", "node_name", "init_node_slot",
  "node_line", "@3", "@4", "node_opt_list", "node_opt", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "@5", "@6", "state_name_list", "opt_in_group", "group_items",
  "opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd", "promote_cmd",
  "promote_list", "perform_cmd", "network_cmd", "nodeini_cmd", "sleep_cmd",
  "compose_cmd", "postgres_ctl_cmd", "fsm_step_cmd", "while_body", "@7",
  "stays_while_cmd", "set_monitor_cmd", "logs_cmd", "sequence_block",
  "sequence_names", "fsm_state", "ident_or_string", 0
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
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   121,   122,   122,   123,   123,   123,   123,   123,   125,
     124,   126,   126,   127,   127,   127,   127,   127,   127,   127,
     127,   128,   128,   128,   128,   128,   128,   128,   128,   129,
     129,   130,   130,   131,   132,   132,   134,   133,   135,   135,
     136,   136,   136,   136,   136,   137,   137,   137,   138,   138,
     139,   139,   140,   142,   141,   143,   141,   144,   144,   145,
     145,   145,   145,   145,   145,   145,   145,   145,   145,   145,
     145,   145,   145,   145,   145,   145,   145,   145,   145,   145,
     145,   145,   145,   145,   145,   145,   145,   145,   146,   147,
     148,   149,   150,   150,   151,   151,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   151,   151,   151,   151,   151,
     152,   152,   152,   152,   152,   152,   152,   152,   152,   153,
     153,   154,   154,   155,   155,   156,   156,   157,   157,   157,
     157,   159,   158,   160,   158,   158,   158,   158,   158,   158,
     158,   161,   161,   161,   161,   162,   162,   163,   163,   164,
     164,   164,   165,   165,   165,   165,   166,   167,   167,   167,
     167,   168,   169,   169,   170,   170,   170,   170,   171,   171,
     172,   172,   173,   174,   174,   174,   174,   174,   175,   175,
     176,   178,   177,   179,   180,   181,   181,   181,   181,   182,
     183,   183,   184,   184,   184,   184,   184,   184,   184,   184,
     184,   184,   184,   184,   184,   184,   184,   184,   184,   184,
     184,   184,   184,   185,   185
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
       1,     1,     1,     1,     1,     2,     2,     4,     2,     1,
       1,     1,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     3,     3,     2,     2,
       3,     3,     0,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     2,     3,     2,     3,     2,     3,     2,     1,     1,
       1,     4,     4,     1,     3,     0,     2,     1,     1,     3,
       3,     0,     9,     0,     9,     7,     7,     5,     6,     5,
       6,     1,     1,     3,     3,     0,     2,     2,     4,     0,
       2,     3,     6,     6,     6,     6,     3,     2,     2,     3,
       3,     2,     1,     3,     2,     4,     5,     7,     3,     3,
       5,     5,     2,     2,     3,     3,     3,     4,     3,     3,
       3,     0,     5,     5,     3,     4,     5,     4,     5,     2,
       0,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   190,     0,     2,     4,     5,
       6,     7,     8,     9,    92,    88,    89,   213,   214,     0,
     189,     1,     3,    11,     0,    90,   191,     0,     0,     0,
       0,     0,   118,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    91,     0,     0,     0,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   109,   102,   103,
     104,   105,   106,   107,   108,    21,     0,     0,     0,     0,
      36,     0,    19,    20,    10,    12,    13,    14,    17,    15,
      16,    18,     0,     0,   111,   113,   115,   117,     0,    51,
      50,     0,     0,   158,   157,   162,   161,   164,     0,     0,
     172,   173,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    30,    29,    33,    34,
      35,    38,    31,    32,     0,     0,   110,   112,   114,   116,
     192,   193,   194,   195,   196,   197,   198,   199,   200,   201,
     202,   203,   204,   205,   206,   207,   208,   209,   210,   211,
     212,   142,     0,   145,   141,     0,     0,     0,   156,   160,
     159,     0,     0,     0,   168,   169,   174,   175,   176,     0,
      50,   179,   178,   184,   180,     0,     0,     0,    23,    24,
      25,    22,     0,     0,     0,     0,     0,     0,   149,     0,
       0,     0,     0,     0,   149,   119,   120,     0,     0,     0,
     163,     0,   165,   177,     0,     0,   185,   187,    26,    27,
      43,    44,    42,     0,     0,    48,    40,    41,    45,    39,
     170,   171,   149,     0,     0,   137,     0,     0,     0,   123,
     149,     0,   146,   144,   143,   139,   149,   149,   149,   149,
     181,   183,   166,   186,   188,     0,    46,    47,     0,   138,
     150,     0,   133,   131,   149,   149,     0,     0,   140,   147,
       0,   153,   152,   155,   154,     0,     0,    28,     0,    37,
      52,    49,   151,   125,   125,   136,   135,     0,   124,     0,
      92,   167,    52,    53,     0,   149,   149,   122,   121,   148,
       0,    55,    57,   128,   126,   127,   134,   132,   182,     0,
      54,     0,    57,     0,     0,     0,    59,    60,    61,    62,
      63,     0,     0,    64,    69,     0,    70,    71,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    58,   130,   129,
       0,    79,    80,    81,    65,    68,    66,     0,     0,    72,
      76,    85,    77,    78,    83,    82,    84,    73,    74,    75,
      56,     0,    86,    87,    67
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,    23,    27,    75,    76,    77,    78,
      79,    80,    81,   121,   184,   218,   219,   248,    91,   283,
     271,   292,   299,   300,   327,     9,    10,    11,    15,    24,
      48,    49,   197,   152,   230,   285,   294,    50,   274,   273,
     153,   194,   232,   225,    51,    52,    53,    54,    96,    55,
      56,    57,    58,    59,    60,    61,   241,   265,    62,    63,
      64,    12,    20,   154,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -180
static const yytype_int16 yypact[] =
{
      77,   -81,   -74,   -74,   -36,  -180,    39,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,   -74,
     -36,  -180,  -180,  -180,   442,  -180,  -180,     9,   -17,   -59,
     -43,   -40,   -23,    30,    -1,    -4,   -64,     0,    36,     7,
      26,   -21,    21,    45,  -180,    43,   160,    61,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,    -7,   -20,    69,    94,    95,
    -180,   -18,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,    96,    98,   118,   119,   120,   121,   140,  -180,
       3,   137,   125,   -11,  -180,  -180,   143,     8,   128,   129,
    -180,  -180,   131,   132,   133,   134,     6,     6,   135,     6,
     -24,   136,   138,   161,   139,    31,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,   163,   164,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,   -61,   179,   -69,  -180,     2,     2,   552,  -180,  -180,
    -180,   165,   265,   168,  -180,  -180,  -180,  -180,  -180,   188,
    -180,  -180,  -180,  -180,  -180,    10,   167,   191,  -180,  -180,
    -180,  -180,   283,   216,     1,   195,   196,   197,   -32,     2,
       2,   198,   215,   169,   -32,  -180,  -180,   210,   239,   211,
    -180,   200,  -180,  -180,   201,   202,  -180,  -180,   284,  -180,
    -180,  -180,  -180,   206,   294,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,   -32,   208,   250,  -180,   280,   309,   228,  -180,
     -15,   233,   246,  -180,  -180,  -180,   -32,   -32,   -32,   -32,
    -180,  -180,   251,  -180,  -180,   235,  -180,  -180,     4,  -180,
    -180,   238,   275,   279,   -32,   -32,     2,   198,  -180,  -180,
     277,  -180,  -180,  -180,  -180,   278,   263,  -180,   264,  -180,
    -180,  -180,  -180,   274,   274,  -180,  -180,   350,  -180,   267,
    -180,  -180,  -180,  -180,   379,   -32,   -32,  -180,  -180,  -180,
     487,  -180,  -180,  -180,   281,  -180,  -180,  -180,  -180,   282,
     141,   420,  -180,   269,   270,   271,  -180,  -180,  -180,  -180,
    -180,   104,   -12,  -180,  -180,   272,  -180,  -180,   276,   303,
     273,   304,   305,   142,   302,    67,   307,  -180,  -180,  -180,
     113,  -180,  -180,  -180,  -180,  -180,  -180,   365,    92,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,   366,  -180,  -180,  -180
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -180,  -180,   388,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -105,   114,
    -180,  -180,  -180,    93,  -180,  -180,  -180,  -180,    13,   144,
    -180,  -180,  -145,  -179,  -180,   151,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -171,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -157,   428
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -123
static const yytype_int16 yytable[] =
{
     199,   171,   172,    89,   174,   210,   211,   111,    89,   268,
      89,   198,   229,    65,   195,   336,    16,   212,   187,    93,
     213,    13,    66,   235,    67,    68,    69,    70,    14,   112,
     113,   192,    25,   114,   188,   193,   234,   189,   190,    21,
     237,   239,     1,   223,   226,   227,   224,     2,     3,     4,
       5,   249,    71,    72,    73,    94,   182,   214,    84,   258,
     223,   257,   183,   224,   337,   261,   262,   263,   264,   253,
     255,   101,   102,   103,    85,   104,   105,    86,   278,   196,
       1,    17,    18,   275,   276,     2,     3,     4,     5,   175,
     176,   177,    82,    83,    87,    98,    99,   116,   117,   122,
     123,   155,   156,   215,    88,   159,   160,   269,   162,   163,
     115,   277,    74,    92,   296,   297,    90,    95,   216,   217,
     288,   170,    97,   170,   204,   205,   106,   295,   303,   304,
     305,   334,   335,   306,   307,   308,   309,   310,   311,   312,
     313,   314,   100,   270,   329,   315,   316,   317,   318,   319,
     107,   320,   321,   322,   323,   324,   303,   304,   305,   325,
     108,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     109,   344,   345,   315,   316,   317,   318,   319,   110,   320,
     321,   322,   323,   324,   347,   348,   118,   325,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   352,
     353,   119,   120,   124,   326,   125,   350,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   126,   127,
     128,   129,   326,   157,   158,   164,   165,   161,   166,   167,
     168,   169,   173,   178,   179,   191,   181,   151,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   180,
     185,   186,   200,   201,   202,   206,   233,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   203,   207,
     208,   209,   220,   221,   222,   228,   231,   242,   240,   243,
     244,   245,   246,   247,   250,   251,   256,   236,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   259,
     260,  -122,   266,   267,   272,  -121,   238,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   279,   281,
     280,   282,   284,   289,   302,   301,   331,   332,   333,   338,
     351,   341,   339,   354,    22,   330,   291,   252,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   340,
     346,   342,   343,   349,   290,   286,   254,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,    26,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   287,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,    28,
       0,     0,     0,     0,     0,     0,   293,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    29,    30,    31,    32,    33,     0,     0,     0,     0,
       0,     0,    34,    35,    36,     0,    37,    38,     0,    39,
       0,     0,    40,    41,    28,    42,    43,   328,     0,     0,
       0,     0,     0,     0,     0,    44,     0,     0,     0,     0,
       0,    45,     0,    46,    47,     0,    29,    30,    31,    32,
      33,     0,     0,     0,     0,     0,     0,    34,    35,    36,
       0,    37,    38,     0,    39,     0,     0,    40,    41,     0,
      42,    43,     0,     0,     0,     0,     0,     0,     0,     0,
     298,     0,     0,     0,     0,     0,    45,     0,    46,    47,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150
};

static const yytype_int16 yycheck[] =
{
     157,   106,   107,     4,   109,     4,     5,    14,     4,     5,
       4,   156,   191,     4,    12,    27,     3,    16,    79,    83,
      19,   102,    13,   194,    15,    16,    17,    18,   102,    36,
      37,   100,    19,    40,    95,   104,   193,    98,    99,     0,
     197,   198,     3,    75,   189,   190,    78,     8,     9,    10,
      11,   222,    43,    44,    45,   119,    25,    56,   117,   230,
      75,    76,    31,    78,    76,   236,   237,   238,   239,   226,
     227,    92,    93,    94,   117,    96,    97,   117,   257,    77,
       3,   117,   118,   254,   255,     8,     9,    10,    11,   113,
     114,   115,   109,   110,   117,    88,    89,   117,   118,   117,
     118,    98,    99,   102,    74,   116,   117,   103,   100,   101,
     117,   256,   103,   117,   285,   286,   117,   117,   117,   118,
     277,   117,    86,   117,   114,   115,   105,   284,    15,    16,
      17,    27,    28,    20,    21,    22,    23,    24,    25,    26,
      27,    28,   116,   248,   301,    32,    33,    34,    35,    36,
     105,    38,    39,    40,    41,    42,    15,    16,    17,    46,
     117,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      10,    29,    30,    32,    33,    34,    35,    36,   117,    38,
      39,    40,    41,    42,   117,   118,   117,    46,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,   117,
     118,   117,   117,   117,   101,   117,   103,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,   120,   120,
     120,   120,   101,   106,   119,   117,   117,   104,   117,   117,
     117,   117,   117,   117,   116,    76,   117,   117,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,   118,
     117,   117,   117,    18,   116,   118,   117,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,   120,   118,
      27,    95,   117,   117,   117,   117,   101,   117,   107,   118,
     118,    37,   116,    29,   116,    75,    98,   117,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,   116,
     104,    76,   101,   118,   116,    76,   117,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,   101,   116,
     102,   117,   108,   116,   102,   104,   117,   117,   117,   117,
      25,   118,   116,    27,     6,   302,   282,   117,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,   116,
     118,   117,   117,   116,   280,   274,   117,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    20,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   117,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    47,
      -1,    -1,    -1,    -1,    -1,    -1,   117,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    69,    70,    71,    72,    73,    -1,    -1,    -1,    -1,
      -1,    -1,    80,    81,    82,    -1,    84,    85,    -1,    87,
      -1,    -1,    90,    91,    47,    93,    94,   117,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   103,    -1,    -1,    -1,    -1,
      -1,   109,    -1,   111,   112,    -1,    69,    70,    71,    72,
      73,    -1,    -1,    -1,    -1,    -1,    -1,    80,    81,    82,
      -1,    84,    85,    -1,    87,    -1,    -1,    90,    91,    -1,
      93,    94,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     103,    -1,    -1,    -1,    -1,    -1,   109,    -1,   111,   112,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   122,   123,   124,   146,
     147,   148,   182,   102,   102,   149,   149,   117,   118,   185,
     183,     0,   123,   125,   150,   149,   185,   126,    47,    69,
      70,    71,    72,    73,    80,    81,    82,    84,    85,    87,
      90,    91,    93,    94,   103,   109,   111,   112,   151,   152,
     158,   165,   166,   167,   168,   170,   171,   172,   173,   174,
     175,   176,   179,   180,   181,     4,    13,    15,    16,    17,
      18,    43,    44,    45,   103,   127,   128,   129,   130,   131,
     132,   133,   109,   110,   117,   117,   117,   117,    74,     4,
     117,   139,   117,    83,   119,   117,   169,    86,    88,    89,
     116,    92,    93,    94,    96,    97,   105,   105,   117,    10,
     117,    14,    36,    37,    40,   117,   117,   118,   117,   117,
     117,   134,   117,   118,   117,   117,   120,   120,   120,   120,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,   117,   154,   161,   184,    98,    99,   106,   119,   116,
     117,   104,   100,   101,   117,   117,   117,   117,   117,   117,
     117,   139,   139,   117,   139,   113,   114,   115,   117,   116,
     118,   117,    25,    31,   135,   117,   117,    79,    95,    98,
      99,    76,   100,   104,   162,    12,    77,   153,   153,   184,
     117,    18,   116,   120,   114,   115,   118,   118,    27,    95,
       4,     5,    16,    19,    56,   102,   117,   118,   136,   137,
     117,   117,   117,    75,    78,   164,   153,   153,   117,   154,
     155,   101,   163,   117,   184,   164,   117,   184,   117,   184,
     107,   177,   117,   118,   118,    37,   116,    29,   138,   164,
     116,    75,   117,   184,   117,   184,    98,    76,   164,   116,
     104,   164,   164,   164,   164,   178,   101,   118,     5,   103,
     139,   141,   116,   160,   159,   164,   164,   153,   154,   101,
     102,   116,   117,   140,   108,   156,   156,   117,   184,   116,
     150,   140,   142,   117,   157,   184,   164,   164,   103,   143,
     144,   104,   102,    15,    16,    17,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    32,    33,    34,    35,    36,
      38,    39,    40,    41,    42,    46,   101,   145,   117,   184,
     144,   117,   117,   117,    27,    28,    27,    76,   117,   116,
     116,   118,   117,   117,    29,    30,   118,   117,   118,   116,
     103,    25,   117,   118,    27
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
#line 238 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	;}
    break;

  case 19:
#line 259 "test_spec_parse.y"
    { current_spec->cluster.bindSource = true; ;}
    break;

  case 20:
#line 260 "test_spec_parse.y"
    { current_spec->cluster.legacyStartup = true; ;}
    break;

  case 21:
#line 274 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
	;}
    break;

  case 22:
#line 278 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 23:
#line 285 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 24:
#line 292 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[(3) - (3)].ival);
	;}
    break;

  case 25:
#line 298 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 26:
#line 305 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 27:
#line 312 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 28:
#line 319 "test_spec_parse.y"
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
#line 332 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 30:
#line 338 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 31:
#line 348 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 32:
#line 354 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 33:
#line 364 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 34:
#line 374 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 35:
#line 380 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 36:
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
	;}
    break;

  case 40:
#line 417 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 41:
#line 418 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 42:
#line 419 "test_spec_parse.y"
    { (yyval.str) = strdup("auth"); ;}
    break;

  case 43:
#line 420 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 44:
#line 421 "test_spec_parse.y"
    { (yyval.str) = strdup("node"); ;}
    break;

  case 45:
#line 426 "test_spec_parse.y"
    {
		strlcpy(current_formation->name, (yyvsp[(1) - (1)].str), sizeof(current_formation->name));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 46:
#line 431 "test_spec_parse.y"
    {
		current_formation->numSync = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 47:
#line 435 "test_spec_parse.y"
    {
		current_formation->disableSecondary = true;
	;}
    break;

  case 50:
#line 461 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 51:
#line 462 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 52:
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
	;}
    break;

  case 53:
#line 488 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(1) - (2)].str), sizeof(current_node->name));
		free((yyvsp[(1) - (2)].str));
	;}
    break;

  case 55:
#line 495 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(2) - (3)].str), sizeof(current_node->name));
		free((yyvsp[(2) - (3)].str));
	;}
    break;

  case 59:
#line 509 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 60:
#line 514 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 61:
#line 519 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 62:
#line 523 "test_spec_parse.y"
    {
		current_node->noMonitor = true;
	;}
    break;

  case 63:
#line 527 "test_spec_parse.y"
    {
		current_node->noAutopilot = true;
	;}
    break;

  case 64:
#line 531 "test_spec_parse.y"
    {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 65:
#line 537 "test_spec_parse.y"
    {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	;}
    break;

  case 66:
#line 542 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
	;}
    break;

  case 67:
#line 546 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 68:
#line 551 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 69:
#line 555 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 70:
#line 559 "test_spec_parse.y"
    {
		current_node->listen = true;
	;}
    break;

  case 71:
#line 563 "test_spec_parse.y"
    {
		current_node->citusSecondary = true;
	;}
    break;

  case 72:
#line 567 "test_spec_parse.y"
    {
		current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 73:
#line 571 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 74:
#line 576 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 75:
#line 581 "test_spec_parse.y"
    {
		current_node->group = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 76:
#line 585 "test_spec_parse.y"
    {
		current_node->pgPort = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 77:
#line 589 "test_spec_parse.y"
    {
		strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 78:
#line 595 "test_spec_parse.y"
    {
		strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 79:
#line 601 "test_spec_parse.y"
    {
		strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str), sizeof(current_node->ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 80:
#line 606 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 81:
#line 611 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 82:
#line 616 "test_spec_parse.y"
    {
		current_node->replicationQuorum = true;
	;}
    break;

  case 83:
#line 620 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 84:
#line 624 "test_spec_parse.y"
    {
		strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 85:
#line 630 "test_spec_parse.y"
    {
		strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 86:
#line 636 "test_spec_parse.y"
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

  case 87:
#line 650 "test_spec_parse.y"
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

  case 88:
#line 671 "test_spec_parse.y"
    {
		current_spec->setup = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 89:
#line 678 "test_spec_parse.y"
    {
		current_spec->teardown = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 90:
#line 689 "test_spec_parse.y"
    {
		TestStep *s = (yyvsp[(3) - (3)].step);
		strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
		free((yyvsp[(2) - (3)].str));
		register_step(current_spec, s);
	;}
    break;

  case 91:
#line 707 "test_spec_parse.y"
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

  case 92:
#line 721 "test_spec_parse.y"
    {
		(yyval.step) = make_step("");
	;}
    break;

  case 93:
#line 725 "test_spec_parse.y"
    {
		if ((yyvsp[(2) - (2)].cmd)) append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
		(yyval.step) = (yyvsp[(1) - (2)].step);
	;}
    break;

  case 94:
#line 732 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 95:
#line 733 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 96:
#line 734 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 97:
#line 735 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 98:
#line 736 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 99:
#line 737 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 100:
#line 738 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 101:
#line 739 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 102:
#line 740 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 103:
#line 741 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 104:
#line 742 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 105:
#line 743 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 106:
#line 744 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 107:
#line 745 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 108:
#line 746 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 109:
#line 747 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 110:
#line 762 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 111:
#line 769 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 112:
#line 775 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 113:
#line 782 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 114:
#line 788 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 115:
#line 795 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 116:
#line 801 "test_spec_parse.y"
    {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 117:
#line 809 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 118:
#line 815 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	;}
    break;

  case 121:
#line 853 "test_spec_parse.y"
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

  case 122:
#line 868 "test_spec_parse.y"
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

  case 127:
#line 908 "test_spec_parse.y"
    {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 128:
#line 916 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 129:
#line 924 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 130:
#line 931 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 131:
#line 942 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); ;}
    break;

  case 132:
#line 947 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 133:
#line 953 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); free((yyvsp[(6) - (6)].str)); ;}
    break;

  case 134:
#line 958 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 135:
#line 964 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str));
	;}
    break;

  case 136:
#line 973 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str)); free((yyvsp[(6) - (7)].str));
	;}
    break;

  case 137:
#line 982 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		free((yyvsp[(3) - (5)].str));
	;}
    break;

  case 138:
#line 996 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(3) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 139:
#line 1004 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 140:
#line 1018 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 141:
#line 1033 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 142:
#line 1039 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 143:
#line 1046 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 144:
#line 1052 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 147:
#line 1071 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 148:
#line 1076 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(4) - (4)].ival);
	;}
    break;

  case 149:
#line 1083 "test_spec_parse.y"
    { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; ;}
    break;

  case 150:
#line 1084 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;

  case 151:
#line 1085 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(3) - (3)].ival); ;}
    break;

  case 152:
#line 1097 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 153:
#line 1105 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 154:
#line 1113 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 155:
#line 1121 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 156:
#line 1139 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 157:
#line 1154 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 158:
#line 1161 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	;}
    break;

  case 159:
#line 1165 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->state));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 160:
#line 1171 "test_spec_parse.y"
    {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[(3) - (3)].ival));
	;}
    break;

  case 161:
#line 1184 "test_spec_parse.y"
    {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	;}
    break;

  case 162:
#line 1192 "test_spec_parse.y"
    {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 163:
#line 1200 "test_spec_parse.y"
    {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 164:
#line 1221 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 165:
#line 1228 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(4) - (4)].ival);
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 166:
#line 1235 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 167:
#line 1243 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (7)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(7) - (7)].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (7)].str));
	;}
    break;

  case 168:
#line 1259 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 169:
#line 1265 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 170:
#line 1286 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 171:
#line 1294 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 172:
#line 1309 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 173:
#line 1323 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	;}
    break;

  case 174:
#line 1327 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 175:
#line 1333 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 176:
#line 1339 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 177:
#line 1365 "test_spec_parse.y"
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

  case 178:
#line 1399 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 179:
#line 1405 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 180:
#line 1426 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 181:
#line 1442 "test_spec_parse.y"
    { pgaf_next_brace_is_while = 1; ;}
    break;

  case 182:
#line 1443 "test_spec_parse.y"
    { (yyval.step) = (yyvsp[(4) - (5)].step); ;}
    break;

  case 183:
#line 1448 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) - (5)].step)->commands : NULL;
		free((yyvsp[(2) - (5)].str));
	;}
    break;

  case 184:
#line 1467 "test_spec_parse.y"
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

  case 185:
#line 1492 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 186:
#line 1501 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 187:
#line 1510 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 188:
#line 1519 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 191:
#line 1540 "test_spec_parse.y"
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

  case 192:
#line 1561 "test_spec_parse.y"
    { (yyval.str) = "init"; ;}
    break;

  case 193:
#line 1562 "test_spec_parse.y"
    { (yyval.str) = "single"; ;}
    break;

  case 194:
#line 1563 "test_spec_parse.y"
    { (yyval.str) = "primary"; ;}
    break;

  case 195:
#line 1564 "test_spec_parse.y"
    { (yyval.str) = "wait_primary"; ;}
    break;

  case 196:
#line 1565 "test_spec_parse.y"
    { (yyval.str) = "wait_standby"; ;}
    break;

  case 197:
#line 1566 "test_spec_parse.y"
    { (yyval.str) = "demoted"; ;}
    break;

  case 198:
#line 1567 "test_spec_parse.y"
    { (yyval.str) = "demote_timeout"; ;}
    break;

  case 199:
#line 1568 "test_spec_parse.y"
    { (yyval.str) = "draining"; ;}
    break;

  case 200:
#line 1569 "test_spec_parse.y"
    { (yyval.str) = "secondary"; ;}
    break;

  case 201:
#line 1570 "test_spec_parse.y"
    { (yyval.str) = "catchingup"; ;}
    break;

  case 202:
#line 1571 "test_spec_parse.y"
    { (yyval.str) = "prepare_promotion"; ;}
    break;

  case 203:
#line 1572 "test_spec_parse.y"
    { (yyval.str) = "stop_replication"; ;}
    break;

  case 204:
#line 1573 "test_spec_parse.y"
    { (yyval.str) = "maintenance"; ;}
    break;

  case 205:
#line 1574 "test_spec_parse.y"
    { (yyval.str) = "join_primary"; ;}
    break;

  case 206:
#line 1575 "test_spec_parse.y"
    { (yyval.str) = "apply_settings"; ;}
    break;

  case 207:
#line 1576 "test_spec_parse.y"
    { (yyval.str) = "prepare_maintenance"; ;}
    break;

  case 208:
#line 1577 "test_spec_parse.y"
    { (yyval.str) = "wait_maintenance"; ;}
    break;

  case 209:
#line 1578 "test_spec_parse.y"
    { (yyval.str) = "report_lsn"; ;}
    break;

  case 210:
#line 1579 "test_spec_parse.y"
    { (yyval.str) = "fast_forward"; ;}
    break;

  case 211:
#line 1580 "test_spec_parse.y"
    { (yyval.str) = "join_secondary"; ;}
    break;

  case 212:
#line 1581 "test_spec_parse.y"
    { (yyval.str) = "dropped"; ;}
    break;

  case 213:
#line 1589 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 214:
#line 1590 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3614 "test_spec_parse.c"
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


#line 1593 "test_spec_parse.y"


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

