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
     T_ARCHIVER = 277,
     T_ASYNC = 278,
     T_NO_MONITOR = 279,
     T_SUSPENDED = 280,
     T_LAUNCH = 281,
     T_CREATE = 282,
     T_DEFERRED = 283,
     T_IMMEDIATE = 284,
     T_FALSE = 285,
     T_TRUE = 286,
     T_INITIALLY = 287,
     T_VOLUME = 288,
     T_LISTEN = 289,
     T_CITUS_SECONDARY = 290,
     T_CANDIDATE_PRIORITY = 291,
     T_PORT = 292,
     T_PASSWORD = 293,
     T_MONITOR_PASSWORD = 294,
     T_CITUS_CLUSTER_NAME = 295,
     T_DEBIAN_CLUSTER = 296,
     T_REPLICATION_QUORUM = 297,
     T_REPLICATION_PASSWORD = 298,
     T_EXTENSION_VERSION = 299,
     T_BIND_SOURCE = 300,
     T_LEGACY_STARTUP = 301,
     T_REGION = 302,
     T_NODEINI = 303,
     T_FS_INIT = 304,
     T_FS_SINGLE = 305,
     T_FS_PRIMARY = 306,
     T_FS_WAIT_PRIMARY = 307,
     T_FS_WAIT_STANDBY = 308,
     T_FS_DEMOTED = 309,
     T_FS_DEMOTE_TIMEOUT = 310,
     T_FS_DRAINING = 311,
     T_FS_SECONDARY = 312,
     T_FS_CATCHINGUP = 313,
     T_FS_PREP_PROMOTION = 314,
     T_FS_STOP_REPLICATION = 315,
     T_FS_MAINTENANCE = 316,
     T_FS_JOIN_PRIMARY = 317,
     T_FS_APPLY_SETTINGS = 318,
     T_FS_PREPARE_MAINTENANCE = 319,
     T_FS_WAIT_MAINTENANCE = 320,
     T_FS_REPORT_LSN = 321,
     T_FS_FAST_FORWARD = 322,
     T_FS_JOIN_SECONDARY = 323,
     T_FS_DROPPED = 324,
     T_EXEC = 325,
     T_EXEC_FAILS = 326,
     T_RUN = 327,
     T_PG_AUTOCTL = 328,
     T_WAIT = 329,
     T_UNTIL = 330,
     T_TIMEOUT = 331,
     T_AND = 332,
     T_IS = 333,
     T_WITH = 334,
     T_REPLAYS = 335,
     T_ASSERT = 336,
     T_SQL = 337,
     T_EXPECT = 338,
     T_ERROR = 339,
     T_PROMOTE = 340,
     T_PERFORM = 341,
     T_FAILOVER = 342,
     T_NETWORK = 343,
     T_DISCONNECT = 344,
     T_CONNECT = 345,
     T_SLEEP = 346,
     T_COMPOSE = 347,
     T_DOWN = 348,
     T_START = 349,
     T_STOP = 350,
     T_STOPPED = 351,
     T_KILL = 352,
     T_INJECT = 353,
     T_STATE = 354,
     T_ASSIGNED_STATE = 355,
     T_IN = 356,
     T_GROUP = 357,
     T_LBRACE = 358,
     T_RBRACE = 359,
     T_COMMA = 360,
     T_POSTGRES = 361,
     T_STAYS = 362,
     T_WHILE = 363,
     T_THROUGH = 364,
     T_SET = 365,
     T_GET = 366,
     T_FSM = 367,
     T_LOGS = 368,
     T_NOT = 369,
     T_CONTAINS = 370,
     T_MATCHES = 371,
     T_WAL = 372,
     T_SEGMENT = 373,
     T_ARCHIVED = 374,
     T_BASEBACKUP = 375,
     T_SLASH = 376,
     T_INTEGER = 377,
     T_IDENT = 378,
     T_STRING = 379,
     T_BLOCK = 380,
     T_SHELL_ARGS = 381
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
#define T_ARCHIVER 277
#define T_ASYNC 278
#define T_NO_MONITOR 279
#define T_SUSPENDED 280
#define T_LAUNCH 281
#define T_CREATE 282
#define T_DEFERRED 283
#define T_IMMEDIATE 284
#define T_FALSE 285
#define T_TRUE 286
#define T_INITIALLY 287
#define T_VOLUME 288
#define T_LISTEN 289
#define T_CITUS_SECONDARY 290
#define T_CANDIDATE_PRIORITY 291
#define T_PORT 292
#define T_PASSWORD 293
#define T_MONITOR_PASSWORD 294
#define T_CITUS_CLUSTER_NAME 295
#define T_DEBIAN_CLUSTER 296
#define T_REPLICATION_QUORUM 297
#define T_REPLICATION_PASSWORD 298
#define T_EXTENSION_VERSION 299
#define T_BIND_SOURCE 300
#define T_LEGACY_STARTUP 301
#define T_REGION 302
#define T_NODEINI 303
#define T_FS_INIT 304
#define T_FS_SINGLE 305
#define T_FS_PRIMARY 306
#define T_FS_WAIT_PRIMARY 307
#define T_FS_WAIT_STANDBY 308
#define T_FS_DEMOTED 309
#define T_FS_DEMOTE_TIMEOUT 310
#define T_FS_DRAINING 311
#define T_FS_SECONDARY 312
#define T_FS_CATCHINGUP 313
#define T_FS_PREP_PROMOTION 314
#define T_FS_STOP_REPLICATION 315
#define T_FS_MAINTENANCE 316
#define T_FS_JOIN_PRIMARY 317
#define T_FS_APPLY_SETTINGS 318
#define T_FS_PREPARE_MAINTENANCE 319
#define T_FS_WAIT_MAINTENANCE 320
#define T_FS_REPORT_LSN 321
#define T_FS_FAST_FORWARD 322
#define T_FS_JOIN_SECONDARY 323
#define T_FS_DROPPED 324
#define T_EXEC 325
#define T_EXEC_FAILS 326
#define T_RUN 327
#define T_PG_AUTOCTL 328
#define T_WAIT 329
#define T_UNTIL 330
#define T_TIMEOUT 331
#define T_AND 332
#define T_IS 333
#define T_WITH 334
#define T_REPLAYS 335
#define T_ASSERT 336
#define T_SQL 337
#define T_EXPECT 338
#define T_ERROR 339
#define T_PROMOTE 340
#define T_PERFORM 341
#define T_FAILOVER 342
#define T_NETWORK 343
#define T_DISCONNECT 344
#define T_CONNECT 345
#define T_SLEEP 346
#define T_COMPOSE 347
#define T_DOWN 348
#define T_START 349
#define T_STOP 350
#define T_STOPPED 351
#define T_KILL 352
#define T_INJECT 353
#define T_STATE 354
#define T_ASSIGNED_STATE 355
#define T_IN 356
#define T_GROUP 357
#define T_LBRACE 358
#define T_RBRACE 359
#define T_COMMA 360
#define T_POSTGRES 361
#define T_STAYS 362
#define T_WHILE 363
#define T_THROUGH 364
#define T_SET 365
#define T_GET 366
#define T_FSM 367
#define T_LOGS 368
#define T_NOT 369
#define T_CONTAINS 370
#define T_MATCHES 371
#define T_WAL 372
#define T_SEGMENT 373
#define T_ARCHIVED 374
#define T_BASEBACKUP 375
#define T_SLASH 376
#define T_INTEGER 377
#define T_IDENT 378
#define T_STRING 379
#define T_BLOCK 380
#define T_SHELL_ARGS 381




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
static TestArchiverNode *current_archiver = NULL;



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
#line 146 "test_spec_parse.y"
{
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}
/* Line 193 of yacc.c.  */
#line 500 "test_spec_parse.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 513 "test_spec_parse.c"

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
#define YYLAST   683

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  127
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  71
/* YYNRULES -- Number of rules.  */
#define YYNRULES  234
/* YYNRULES -- Number of states.  */
#define YYNSTATES  412

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   381

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
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     8,    10,    12,    14,    16,    18,
      19,    25,    26,    29,    31,    33,    35,    37,    39,    41,
      43,    45,    47,    48,    55,    56,    59,    62,    65,    68,
      73,    76,    79,    81,    85,    89,    93,    97,   102,   107,
     114,   117,   120,   123,   126,   129,   132,   135,   136,   143,
     144,   147,   149,   151,   153,   155,   157,   159,   162,   165,
     166,   169,   171,   173,   174,   175,   180,   181,   189,   190,
     193,   195,   197,   199,   201,   203,   205,   207,   210,   213,
     218,   221,   223,   225,   227,   230,   233,   236,   239,   242,
     245,   248,   251,   254,   257,   260,   263,   266,   269,   273,
     277,   280,   283,   287,   291,   292,   295,   297,   299,   301,
     303,   305,   307,   309,   311,   313,   315,   317,   319,   321,
     323,   325,   327,   331,   334,   338,   341,   345,   348,   352,
     355,   357,   359,   361,   366,   371,   373,   377,   378,   381,
     383,   385,   389,   393,   394,   404,   405,   415,   423,   431,
     437,   444,   450,   457,   466,   478,   489,   501,   503,   505,
     509,   513,   514,   517,   520,   525,   526,   529,   533,   540,
     547,   554,   561,   565,   568,   571,   575,   579,   582,   584,
     588,   591,   596,   602,   610,   614,   618,   624,   630,   633,
     636,   640,   644,   648,   653,   657,   661,   665,   666,   672,
     678,   682,   687,   693,   698,   704,   707,   708,   711,   713,
     715,   717,   719,   721,   723,   725,   727,   729,   731,   733,
     735,   737,   739,   741,   743,   745,   747,   749,   751,   753,
     755,   757,   759,   761,   762
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     128,     0,    -1,   129,    -1,   128,   129,    -1,   130,    -1,
     156,    -1,   157,    -1,   158,    -1,   192,    -1,    -1,     3,
     103,   131,   132,   104,    -1,    -1,   132,   133,    -1,   138,
      -1,   139,    -1,   141,    -1,   142,    -1,   140,    -1,   143,
      -1,   134,    -1,    45,    -1,    46,    -1,    -1,    22,   123,
     135,   103,   136,   104,    -1,    -1,   136,   137,    -1,    18,
     123,    -1,    47,   123,    -1,    47,   124,    -1,    27,    77,
      26,    28,    -1,    26,    28,    -1,    27,    28,    -1,     4,
      -1,     4,    41,   123,    -1,     4,    14,   123,    -1,     4,
      37,   122,    -1,     4,    38,   124,    -1,     4,   123,    26,
      28,    -1,     4,   123,    32,    96,    -1,     4,   123,    26,
      28,    38,   124,    -1,    13,   124,    -1,    13,   123,    -1,
      44,   123,    -1,    44,   124,    -1,    15,   123,    -1,    16,
     123,    -1,    17,   123,    -1,    -1,    18,   144,   145,   103,
     148,   104,    -1,    -1,   145,   147,    -1,   123,    -1,   124,
      -1,    16,    -1,     4,    -1,     5,    -1,   146,    -1,    19,
     122,    -1,    57,    30,    -1,    -1,   148,   151,    -1,   123,
      -1,     4,    -1,    -1,    -1,   149,   150,   152,   154,    -1,
      -1,     5,   123,   150,   153,   103,   154,   104,    -1,    -1,
     154,   155,    -1,    20,    -1,    21,    -1,    22,    -1,    23,
      -1,    24,    -1,    25,    -1,    28,    -1,    26,    28,    -1,
      27,    28,    -1,    27,    77,    26,    28,    -1,    26,    29,
      -1,    29,    -1,    34,    -1,    35,    -1,    36,   122,    -1,
      47,   123,    -1,    47,   124,    -1,   102,   122,    -1,    37,
     122,    -1,    40,   123,    -1,    41,   123,    -1,    15,   123,
      -1,    16,   123,    -1,    17,   123,    -1,    42,    31,    -1,
      42,    30,    -1,    43,   124,    -1,    39,   124,    -1,    33,
     123,   123,    -1,    33,   123,   124,    -1,     8,   159,    -1,
       9,   159,    -1,    10,   195,   159,    -1,   103,   160,   104,
      -1,    -1,   160,   161,    -1,   162,    -1,   168,    -1,   175,
      -1,   176,    -1,   177,    -1,   178,    -1,   180,    -1,   181,
      -1,   183,    -1,   184,    -1,   185,    -1,   186,    -1,   189,
      -1,   190,    -1,   191,    -1,   182,    -1,    70,   123,   126,
      -1,    70,   123,    -1,    71,   123,   126,    -1,    71,   123,
      -1,    72,   123,   126,    -1,    72,   123,    -1,    73,   123,
     126,    -1,    73,   123,    -1,    73,    -1,    12,    -1,    78,
      -1,   123,    99,   163,   194,    -1,   123,    99,   163,   123,
      -1,   164,    -1,   165,    77,   164,    -1,    -1,   109,   167,
      -1,   194,    -1,   123,    -1,   167,   105,   194,    -1,   167,
     105,   123,    -1,    -1,    74,    75,   123,    99,   163,   194,
     169,   166,   174,    -1,    -1,    74,    75,   123,    99,   163,
     123,   170,   166,   174,    -1,    74,    75,   123,   100,   163,
     194,   174,    -1,    74,    75,   123,   100,   163,   123,   174,
      -1,    74,    75,   123,    96,   174,    -1,    74,    75,   123,
      80,   123,   174,    -1,    74,    75,   171,   172,   174,    -1,
      74,    75,   164,    77,   165,   174,    -1,    74,    75,    82,
     123,   125,    78,   125,   174,    -1,    74,    75,   117,   118,
     124,   119,   101,   123,   121,   122,   174,    -1,    74,    75,
      22,    99,   163,   196,   101,   123,   197,   174,    -1,    74,
      75,   120,   123,    78,   123,   101,   123,   121,   122,   174,
      -1,   194,    -1,   123,    -1,   171,   105,   194,    -1,   171,
     105,   123,    -1,    -1,   101,   173,    -1,   102,   122,    -1,
     173,   105,   102,   122,    -1,    -1,    76,   122,    -1,    79,
      76,   122,    -1,    81,   123,    99,   163,   194,   174,    -1,
      81,   123,    99,   163,   123,   174,    -1,    81,   123,   100,
     163,   194,   174,    -1,    81,   123,   100,   163,   123,   174,
      -1,    82,   123,   125,    -1,    83,   125,    -1,    83,    84,
      -1,    83,    84,   123,    -1,    83,    84,   122,    -1,    85,
     179,    -1,   123,    -1,   179,   105,   123,    -1,    86,    87,
      -1,    86,    87,   102,   122,    -1,    86,    87,   101,    18,
     123,    -1,    86,    87,   101,    18,   123,   102,   122,    -1,
      88,    89,   123,    -1,    88,    90,   123,    -1,    48,   110,
     123,   123,   123,    -1,    48,   111,   123,   123,   123,    -1,
      91,   122,    -1,    92,    93,    -1,    92,    94,   123,    -1,
      92,    95,   123,    -1,    92,    97,   123,    -1,    92,    98,
     123,   126,    -1,    95,   106,   149,    -1,    94,   106,   149,
      -1,   112,    10,   149,    -1,    -1,   108,   188,   103,   160,
     104,    -1,    81,   149,   107,   194,   187,    -1,   110,   123,
     123,    -1,   113,   123,   115,   124,    -1,   113,   123,   114,
     115,   124,    -1,   113,   123,   116,   124,    -1,   113,   123,
     114,   116,   124,    -1,    11,   193,    -1,    -1,   193,   195,
      -1,    49,    -1,    50,    -1,    51,    -1,    52,    -1,    53,
      -1,    54,    -1,    55,    -1,    56,    -1,    57,    -1,    58,
      -1,    59,    -1,    60,    -1,    61,    -1,    62,    -1,    63,
      -1,    64,    -1,    65,    -1,    66,    -1,    67,    -1,    68,
      -1,    69,    -1,   123,    -1,   124,    -1,   194,    -1,   123,
      -1,    -1,   121,   122,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   220,   220,   221,   225,   226,   227,   228,   229,   242,
     241,   251,   253,   257,   258,   259,   260,   261,   262,   263,
     264,   265,   288,   287,   305,   307,   311,   325,   330,   335,
     342,   346,   362,   366,   373,   380,   386,   393,   400,   407,
     420,   426,   436,   442,   452,   462,   468,   479,   478,   495,
     497,   506,   507,   508,   509,   510,   514,   519,   523,   529,
     531,   550,   551,   560,   577,   576,   584,   583,   591,   593,
     597,   602,   607,   611,   615,   619,   623,   629,   634,   638,
     643,   647,   651,   655,   659,   663,   668,   673,   677,   681,
     687,   693,   698,   703,   708,   712,   716,   722,   728,   742,
     763,   770,   781,   799,   814,   817,   825,   826,   827,   828,
     829,   830,   831,   832,   833,   834,   835,   836,   837,   838,
     839,   840,   854,   861,   867,   874,   880,   887,   893,   901,
     907,   934,   934,   945,   960,   978,   979,   994,   996,  1000,
    1008,  1016,  1023,  1035,  1034,  1046,  1045,  1056,  1065,  1074,
    1088,  1096,  1110,  1125,  1142,  1166,  1195,  1225,  1231,  1238,
    1244,  1257,  1259,  1263,  1268,  1276,  1277,  1278,  1289,  1297,
    1305,  1313,  1331,  1346,  1353,  1357,  1363,  1376,  1384,  1392,
    1413,  1420,  1427,  1435,  1451,  1457,  1478,  1486,  1501,  1515,
    1519,  1525,  1531,  1557,  1591,  1597,  1618,  1635,  1635,  1640,
    1659,  1684,  1693,  1702,  1711,  1727,  1730,  1732,  1754,  1755,
    1756,  1757,  1758,  1759,  1760,  1761,  1762,  1763,  1764,  1765,
    1766,  1767,  1768,  1769,  1770,  1771,  1772,  1773,  1774,  1782,
    1783,  1794,  1795,  1803,  1804
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
  "T_WORKER", "T_ARCHIVER", "T_ASYNC", "T_NO_MONITOR", "T_SUSPENDED",
  "T_LAUNCH", "T_CREATE", "T_DEFERRED", "T_IMMEDIATE", "T_FALSE", "T_TRUE",
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
  "T_GET", "T_FSM", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_WAL",
  "T_SEGMENT", "T_ARCHIVED", "T_BASEBACKUP", "T_SLASH", "T_INTEGER",
  "T_IDENT", "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec",
  "spec_item", "cluster_block", "@1", "cluster_item_list", "cluster_item",
  "archiver_block", "@2", "archiver_opt_list", "archiver_opt",
  "monitor_line", "image_line", "extension_version_line", "ssl_line",
  "auth_line", "formation_block", "@3", "formation_opt_list", "bare_name",
  "formation_opt", "node_list", "node_name", "init_node_slot", "node_line",
  "@4", "@5", "node_opt_list", "node_opt", "setup_block", "teardown_block",
  "named_step", "cmd_block", "cmd_list", "step_cmd", "exec_cmd",
  "state_op", "wait_multi_condition", "wait_multi_condition_list",
  "opt_passing_through", "pass_state_list", "wait_cmd", "@6", "@7",
  "state_name_list", "opt_in_group", "group_items", "opt_timeout",
  "assert_cmd", "sql_cmd", "expect_cmd", "promote_cmd", "promote_list",
  "perform_cmd", "network_cmd", "nodeini_cmd", "sleep_cmd", "compose_cmd",
  "postgres_ctl_cmd", "fsm_step_cmd", "while_body", "@8",
  "stays_while_cmd", "set_monitor_cmd", "logs_cmd", "sequence_block",
  "sequence_names", "fsm_state", "ident_or_string", "wait_state_name",
  "opt_wait_group", 0
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
     375,   376,   377,   378,   379,   380,   381
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   127,   128,   128,   129,   129,   129,   129,   129,   131,
     130,   132,   132,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   135,   134,   136,   136,   137,   137,   137,   137,
     137,   137,   138,   138,   138,   138,   138,   138,   138,   138,
     139,   139,   140,   140,   141,   142,   142,   144,   143,   145,
     145,   146,   146,   146,   146,   146,   147,   147,   147,   148,
     148,   149,   149,   150,   152,   151,   153,   151,   154,   154,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     155,   155,   155,   155,   155,   155,   155,   155,   155,   155,
     156,   157,   158,   159,   160,   160,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   163,   163,   164,   164,   165,   165,   166,   166,   167,
     167,   167,   167,   169,   168,   170,   168,   168,   168,   168,
     168,   168,   168,   168,   168,   168,   168,   171,   171,   171,
     171,   172,   172,   173,   173,   174,   174,   174,   175,   175,
     175,   175,   176,   177,   177,   177,   177,   178,   179,   179,
     180,   180,   180,   180,   181,   181,   182,   182,   183,   184,
     184,   184,   184,   184,   185,   185,   186,   188,   187,   189,
     190,   191,   191,   191,   191,   192,   193,   193,   194,   194,
     194,   194,   194,   194,   194,   194,   194,   194,   194,   194,
     194,   194,   194,   194,   194,   194,   194,   194,   194,   195,
     195,   196,   196,   197,   197
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
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
       1,     1,     3,     2,     3,     2,     3,     2,     3,     2,
       1,     1,     1,     4,     4,     1,     3,     0,     2,     1,
       1,     3,     3,     0,     9,     0,     9,     7,     7,     5,
       6,     5,     6,     8,    11,    10,    11,     1,     1,     3,
       3,     0,     2,     2,     4,     0,     2,     3,     6,     6,
       6,     6,     3,     2,     2,     3,     3,     2,     1,     3,
       2,     4,     5,     7,     3,     3,     5,     5,     2,     2,
       3,     3,     3,     4,     3,     3,     3,     0,     5,     5,
       3,     4,     5,     4,     5,     2,     0,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     0,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   206,     0,     2,     4,     5,
       6,     7,     8,     9,   104,   100,   101,   229,   230,     0,
     205,     1,     3,    11,     0,   102,   207,     0,     0,     0,
       0,     0,   130,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   103,     0,     0,     0,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   121,   114,   115,
     116,   117,   118,   119,   120,    32,     0,     0,     0,     0,
      47,     0,     0,    20,    21,    10,    12,    19,    13,    14,
      17,    15,    16,    18,     0,     0,   123,   125,   127,   129,
       0,    62,    61,     0,     0,   174,   173,   178,   177,   180,
       0,     0,   188,   189,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    41,    40,
      44,    45,    46,    49,    22,    42,    43,     0,     0,   122,
     124,   126,   128,     0,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,     0,     0,     0,   158,     0,
     161,   157,     0,     0,     0,   172,   176,   175,     0,     0,
       0,   184,   185,   190,   191,   192,     0,    61,   195,   194,
     200,   196,     0,     0,     0,    34,    35,    36,    33,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     165,     0,     0,     0,     0,     0,   165,   131,   132,     0,
       0,     0,   179,     0,   181,   193,     0,     0,   201,   203,
      37,    38,    54,    55,    53,     0,     0,    59,    51,    52,
      56,    50,    24,   186,   187,     0,     0,     0,     0,   165,
       0,     0,   149,     0,     0,     0,   135,   165,     0,   162,
     160,   159,   151,   165,   165,   165,   165,   197,   199,   182,
     202,   204,     0,    57,    58,     0,     0,   232,   231,     0,
       0,     0,     0,   150,   166,     0,   145,   143,   165,   165,
       0,     0,   152,   163,     0,   169,   168,   171,   170,     0,
       0,    39,     0,    48,    63,    60,     0,     0,     0,     0,
      23,    25,     0,   165,     0,     0,   167,   137,   137,   148,
     147,     0,   136,     0,   104,   183,    63,    64,    26,    30,
      31,     0,    27,    28,   233,   153,     0,     0,     0,   165,
     165,   134,   133,   164,     0,    66,    68,     0,     0,   165,
       0,     0,   140,   138,   139,   146,   144,   198,     0,    65,
      29,   234,   155,   165,   165,     0,    68,     0,     0,     0,
      70,    71,    72,    73,    74,    75,     0,     0,    76,    81,
       0,    82,    83,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    69,   154,   156,   142,   141,     0,    91,    92,
      93,    77,    80,    78,     0,     0,    84,    88,    97,    89,
      90,    95,    94,    96,    85,    86,    87,    67,     0,    98,
      99,    79
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,    23,    27,    76,    77,   192,   266,
     301,    78,    79,    80,    81,    82,    83,   123,   191,   230,
     231,   265,    93,   317,   295,   336,   348,   349,   382,     9,
      10,    11,    15,    24,    48,    49,   209,   159,   247,   329,
     343,    50,   308,   307,   160,   206,   249,   242,    51,    52,
      53,    54,    98,    55,    56,    57,    58,    59,    60,    61,
     258,   289,    62,    63,    64,    12,    20,   161,    19,   269,
     339
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -204
static const yytype_int16 yypact[] =
{
     102,   -70,   -56,   -56,   -98,  -204,    82,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,   -56,
     -98,  -204,  -204,  -204,   504,  -204,  -204,    52,   -33,   -74,
     -63,   -57,   -51,   -18,     7,   -20,   -66,    -2,    21,    -6,
      10,    25,    11,    23,  -204,    14,   144,    32,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,    -7,   -29,    34,    36,    37,
    -204,    38,   -22,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,    39,    40,    60,    61,    62,    63,
     116,  -204,    15,    83,    67,   -16,  -204,  -204,    88,    33,
      74,    86,  -204,  -204,    87,    94,   100,   101,     8,     8,
     104,     8,   -27,   105,    89,   106,   108,    -3,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,   109,   111,  -204,
    -204,  -204,  -204,   126,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,   112,   119,   120,   -61,   152,
     -73,  -204,     3,     3,   614,  -204,  -204,  -204,   121,   220,
     133,  -204,  -204,  -204,  -204,  -204,   130,  -204,  -204,  -204,
    -204,  -204,    26,   139,   145,  -204,  -204,  -204,  -204,   229,
     174,     1,   168,   150,   151,     3,   153,   155,   197,   154,
     -39,     3,     3,   157,   180,   235,   -39,  -204,  -204,   256,
     279,   218,  -204,   226,  -204,  -204,   227,   228,  -204,  -204,
     238,  -204,  -204,  -204,  -204,   231,   320,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,   331,   276,   236,   233,   -39,
     237,   281,  -204,   354,   375,   261,  -204,   -15,   239,   257,
    -204,  -204,  -204,   -39,   -39,   -39,   -39,  -204,  -204,   262,
    -204,  -204,   241,  -204,  -204,     5,    -5,  -204,  -204,   265,
     242,   267,   268,  -204,  -204,   248,   286,   294,   -39,   -39,
       3,   157,  -204,  -204,   270,  -204,  -204,  -204,  -204,   271,
     251,  -204,   252,  -204,  -204,  -204,   253,   349,   -14,    16,
    -204,  -204,   255,   -39,   278,   322,  -204,   337,   337,  -204,
    -204,   406,  -204,   325,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,   422,  -204,  -204,   328,  -204,   329,   330,   450,   -39,
     -39,  -204,  -204,  -204,   549,  -204,  -204,   424,   356,   -39,
     357,   358,  -204,   348,  -204,  -204,  -204,  -204,   373,   225,
    -204,  -204,  -204,   -39,   -39,   481,  -204,   359,   360,   361,
    -204,  -204,  -204,  -204,  -204,  -204,   115,    -4,  -204,  -204,
     362,  -204,  -204,   364,   365,   366,   368,   369,   118,   370,
      22,   367,  -204,  -204,  -204,  -204,  -204,   179,  -204,  -204,
    -204,  -204,  -204,  -204,   455,    29,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,   460,  -204,
    -204,  -204
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -204,  -204,   487,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -107,   181,  -204,  -204,  -204,   140,  -204,  -204,
    -204,  -204,    24,   206,  -204,  -204,  -147,  -195,  -204,   187,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -203,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,  -204,
    -204,  -204,  -204,  -204,  -204,  -204,  -204,  -164,   501,  -204,
    -204
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -135
static const yytype_int16 yytable[] =
{
     211,   178,   179,   252,   181,   222,   223,   113,   246,    91,
     292,    91,    91,   296,   320,   207,   210,   224,    95,   199,
     225,   297,   298,   189,   393,    17,    18,    16,   204,   190,
     114,   115,   205,    13,   116,   200,   273,   240,   201,   202,
     241,   251,   299,    25,   282,   254,   256,    14,   235,    86,
     285,   286,   287,   288,   243,   244,    65,    90,   226,    96,
      87,   240,   281,   321,   241,    66,    88,    67,    68,    69,
      70,   268,    89,   394,    71,   309,   310,    84,    85,   277,
     279,   208,    21,   100,   101,     1,   312,   182,   183,   184,
       2,     3,     4,     5,   118,   119,    72,    73,    74,   300,
     325,   125,   126,    94,   227,     1,   166,   167,    99,   293,
       2,     3,     4,     5,   162,   163,   117,   108,   103,   104,
     105,    97,   106,   107,   228,   229,   345,   346,   177,   109,
      92,   177,   102,   311,   169,   170,   352,   110,   133,   322,
     323,   216,   217,   391,   392,   404,   405,   332,   401,   402,
     383,   384,   409,   410,   111,   112,    75,   120,   294,   121,
     122,   124,   127,   128,   344,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   129,   130,   131,   132,
     164,   386,   165,   168,   357,   358,   359,   171,   155,   360,
     361,   362,   363,   364,   365,   366,   367,   368,   369,   172,
     173,   186,   370,   371,   372,   373,   374,   174,   375,   376,
     377,   378,   379,   175,   176,   195,   380,   180,   185,   203,
     187,   188,   193,   156,   194,   196,   157,   197,   213,   158,
     357,   358,   359,   198,   212,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   214,   215,   220,   370,   371,
     372,   373,   374,   218,   375,   376,   377,   378,   379,   219,
     221,   232,   380,   233,   234,   238,   262,   239,   236,   237,
     245,   381,   248,   407,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   257,   381,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   259,
     264,   260,   261,   263,   270,   271,   272,   275,   250,   274,
     280,   283,   284,  -134,   290,   291,   302,   303,   304,   305,
     306,  -133,   313,   315,   314,   316,   318,   319,   324,   253,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   326,   255,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   327,   328,   333,   337,   338,
     340,   341,   350,   355,   267,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   356,   276,   351,   353,
     354,   408,   388,   389,   390,   395,   396,   397,   411,   406,
     398,   399,   400,    22,   403,   330,   387,   335,   278,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     334,    26,     0,     0,     0,     0,     0,     0,     0,   331,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,     0,    28,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   342,    29,    30,    31,    32,    33,     0,
       0,     0,     0,     0,     0,    34,    35,    36,     0,    37,
      38,     0,    39,     0,     0,    40,    41,    28,    42,    43,
       0,     0,     0,     0,   385,     0,     0,     0,    44,     0,
       0,     0,     0,     0,    45,     0,    46,    47,     0,    29,
      30,    31,    32,    33,     0,     0,     0,     0,     0,     0,
      34,    35,    36,     0,    37,    38,     0,    39,     0,     0,
      40,    41,     0,    42,    43,     0,     0,     0,     0,     0,
       0,     0,     0,   347,     0,     0,     0,     0,     0,    45,
       0,    46,    47,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154
};

static const yytype_int16 yycheck[] =
{
     164,   108,   109,   206,   111,     4,     5,    14,   203,     4,
       5,     4,     4,    18,    28,    12,   163,    16,    84,    80,
      19,    26,    27,    26,    28,   123,   124,     3,   101,    32,
      37,    38,   105,   103,    41,    96,   239,    76,    99,   100,
      79,   205,    47,    19,   247,   209,   210,   103,   195,   123,
     253,   254,   255,   256,   201,   202,     4,    75,    57,   125,
     123,    76,    77,    77,    79,    13,   123,    15,    16,    17,
      18,   235,   123,    77,    22,   278,   279,   110,   111,   243,
     244,    78,     0,    89,    90,     3,   281,   114,   115,   116,
       8,     9,    10,    11,   123,   124,    44,    45,    46,   104,
     303,   123,   124,   123,   103,     3,   122,   123,    87,   104,
       8,     9,    10,    11,    99,   100,   123,   106,    93,    94,
      95,   123,    97,    98,   123,   124,   329,   330,   123,   106,
     123,   123,   122,   280,   101,   102,   339,   123,    22,   123,
     124,   115,   116,    28,    29,   123,   124,   311,    30,    31,
     353,   354,   123,   124,    10,   123,   104,   123,   265,   123,
     123,   123,   123,   123,   328,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,   126,   126,   126,   126,
     107,   355,   125,   105,    15,    16,    17,   123,    82,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,   123,
     123,   122,    33,    34,    35,    36,    37,   123,    39,    40,
      41,    42,    43,   123,   123,    99,    47,   123,   123,    77,
     124,   123,   123,   117,   123,   123,   120,   118,    18,   123,
      15,    16,    17,   123,   123,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,   122,   126,    28,    33,    34,
      35,    36,    37,   124,    39,    40,    41,    42,    43,   124,
      96,   103,    47,   123,   123,    78,    38,   123,   125,   124,
     123,   102,   102,   104,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,   108,   102,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,   123,
      30,   124,   124,   122,    78,   119,   123,    76,   123,   122,
      99,   122,   105,    77,   102,   124,   101,   125,   101,   101,
     122,    77,   102,   122,   103,   123,   123,    28,   123,   123,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,   123,   123,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,   123,   109,   122,    26,   121,
     121,   121,    28,   105,   123,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,   103,   123,   122,   122,
     122,    26,   123,   123,   123,   123,   122,   122,    28,   122,
     124,   123,   123,     6,   124,   308,   356,   316,   123,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
     314,    20,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   123,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   123,    70,    71,    72,    73,    74,    -1,
      -1,    -1,    -1,    -1,    -1,    81,    82,    83,    -1,    85,
      86,    -1,    88,    -1,    -1,    91,    92,    48,    94,    95,
      -1,    -1,    -1,    -1,   123,    -1,    -1,    -1,   104,    -1,
      -1,    -1,    -1,    -1,   110,    -1,   112,   113,    -1,    70,
      71,    72,    73,    74,    -1,    -1,    -1,    -1,    -1,    -1,
      81,    82,    83,    -1,    85,    86,    -1,    88,    -1,    -1,
      91,    92,    -1,    94,    95,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   104,    -1,    -1,    -1,    -1,    -1,   110,
      -1,   112,   113,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   128,   129,   130,   156,
     157,   158,   192,   103,   103,   159,   159,   123,   124,   195,
     193,     0,   129,   131,   160,   159,   195,   132,    48,    70,
      71,    72,    73,    74,    81,    82,    83,    85,    86,    88,
      91,    92,    94,    95,   104,   110,   112,   113,   161,   162,
     168,   175,   176,   177,   178,   180,   181,   182,   183,   184,
     185,   186,   189,   190,   191,     4,    13,    15,    16,    17,
      18,    22,    44,    45,    46,   104,   133,   134,   138,   139,
     140,   141,   142,   143,   110,   111,   123,   123,   123,   123,
      75,     4,   123,   149,   123,    84,   125,   123,   179,    87,
      89,    90,   122,    93,    94,    95,    97,    98,   106,   106,
     123,    10,   123,    14,    37,    38,    41,   123,   123,   124,
     123,   123,   123,   144,   123,   123,   124,   123,   123,   126,
     126,   126,   126,    22,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    82,   117,   120,   123,   164,
     171,   194,    99,   100,   107,   125,   122,   123,   105,   101,
     102,   123,   123,   123,   123,   123,   123,   123,   149,   149,
     123,   149,   114,   115,   116,   123,   122,   124,   123,    26,
      32,   145,   135,   123,   123,    99,   123,   118,   123,    80,
      96,    99,   100,    77,   101,   105,   172,    12,    78,   163,
     163,   194,   123,    18,   122,   126,   115,   116,   124,   124,
      28,    96,     4,     5,    16,    19,    57,   103,   123,   124,
     146,   147,   103,   123,   123,   163,   125,   124,    78,   123,
      76,    79,   174,   163,   163,   123,   164,   165,   102,   173,
     123,   194,   174,   123,   194,   123,   194,   108,   187,   123,
     124,   124,    38,   122,    30,   148,   136,   123,   194,   196,
      78,   119,   123,   174,   122,    76,   123,   194,   123,   194,
      99,    77,   174,   122,   105,   174,   174,   174,   174,   188,
     102,   124,     5,   104,   149,   151,    18,    26,    27,    47,
     104,   137,   101,   125,   101,   101,   122,   170,   169,   174,
     174,   163,   164,   102,   103,   122,   123,   150,   123,    28,
      28,    77,   123,   124,   123,   174,   123,   123,   109,   166,
     166,   123,   194,   122,   160,   150,   152,    26,   121,   197,
     121,   121,   123,   167,   194,   174,   174,   104,   153,   154,
      28,   122,   174,   122,   122,   105,   103,    15,    16,    17,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      33,    34,    35,    36,    37,    39,    40,    41,    42,    43,
      47,   102,   155,   174,   174,   123,   194,   154,   123,   123,
     123,    28,    29,    28,    77,   123,   122,   122,   124,   123,
     123,    30,    31,   124,   123,   124,   122,   104,    26,   123,
     124,    28
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
#line 242 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	;}
    break;

  case 20:
#line 264 "test_spec_parse.y"
    { current_spec->cluster.bindSource = true; ;}
    break;

  case 21:
#line 265 "test_spec_parse.y"
    { current_spec->cluster.legacyStartup = true; ;}
    break;

  case 22:
#line 288 "test_spec_parse.y"
    {
		TestCluster *cl = &current_spec->cluster;

		if (cl->archiverCount >= PGAF_MAX_ARCHIVERS)
		{
			fprintf(stderr, "pgaftest: too many archivers (max %d)\n",
			        PGAF_MAX_ARCHIVERS);
			exit(1);
		}

		current_archiver = &cl->archivers[cl->archiverCount++];
		strlcpy(current_archiver->name, (yyvsp[(2) - (2)].str), sizeof(current_archiver->name));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 26:
#line 312 "test_spec_parse.y"
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
		        (yyvsp[(2) - (2)].str), sizeof(current_archiver->formations[0]));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 27:
#line 326 "test_spec_parse.y"
    {
		strlcpy(current_archiver->region, (yyvsp[(2) - (2)].str), sizeof(current_archiver->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 28:
#line 331 "test_spec_parse.y"
    {
		strlcpy(current_archiver->region, (yyvsp[(2) - (2)].str), sizeof(current_archiver->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 29:
#line 336 "test_spec_parse.y"
    {
		/* bare "create and launch deferred" = both gates, matching
		 * node_opt's own identical form */
		current_archiver->createDeferred = true;
		current_archiver->launchDeferred = true;
	;}
    break;

  case 30:
#line 343 "test_spec_parse.y"
    {
		current_archiver->launchDeferred = true;
	;}
    break;

  case 31:
#line 347 "test_spec_parse.y"
    {
		current_archiver->createDeferred = true;
	;}
    break;

  case 32:
#line 363 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
	;}
    break;

  case 33:
#line 367 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 34:
#line 374 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 35:
#line 381 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[(3) - (3)].ival);
	;}
    break;

  case 36:
#line 387 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 37:
#line 394 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 38:
#line 401 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 39:
#line 408 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (6)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (6)].str));
		/* password for second monitor not yet stored */
		free((yyvsp[(6) - (6)].str));
	;}
    break;

  case 40:
#line 421 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 41:
#line 427 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 42:
#line 437 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 43:
#line 443 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 44:
#line 453 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 45:
#line 463 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 46:
#line 469 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 47:
#line 479 "test_spec_parse.y"
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

  case 51:
#line 506 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 52:
#line 507 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 53:
#line 508 "test_spec_parse.y"
    { (yyval.str) = strdup("auth"); ;}
    break;

  case 54:
#line 509 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 55:
#line 510 "test_spec_parse.y"
    { (yyval.str) = strdup("node"); ;}
    break;

  case 56:
#line 515 "test_spec_parse.y"
    {
		strlcpy(current_formation->name, (yyvsp[(1) - (1)].str), sizeof(current_formation->name));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 57:
#line 520 "test_spec_parse.y"
    {
		current_formation->numSync = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 58:
#line 524 "test_spec_parse.y"
    {
		current_formation->disableSecondary = true;
	;}
    break;

  case 61:
#line 550 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 62:
#line 551 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 63:
#line 560 "test_spec_parse.y"
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

  case 64:
#line 577 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(1) - (2)].str), sizeof(current_node->name));
		free((yyvsp[(1) - (2)].str));
	;}
    break;

  case 66:
#line 584 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(2) - (3)].str), sizeof(current_node->name));
		free((yyvsp[(2) - (3)].str));
	;}
    break;

  case 70:
#line 598 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 71:
#line 603 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 72:
#line 608 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_ARCHIVER;
	;}
    break;

  case 73:
#line 612 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 74:
#line 616 "test_spec_parse.y"
    {
		current_node->noMonitor = true;
	;}
    break;

  case 75:
#line 620 "test_spec_parse.y"
    {
		current_node->suspended = true;
	;}
    break;

  case 76:
#line 624 "test_spec_parse.y"
    {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 77:
#line 630 "test_spec_parse.y"
    {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	;}
    break;

  case 78:
#line 635 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
	;}
    break;

  case 79:
#line 639 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 80:
#line 644 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 81:
#line 648 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 82:
#line 652 "test_spec_parse.y"
    {
		current_node->listen = true;
	;}
    break;

  case 83:
#line 656 "test_spec_parse.y"
    {
		current_node->citusSecondary = true;
	;}
    break;

  case 84:
#line 660 "test_spec_parse.y"
    {
		current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 85:
#line 664 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 86:
#line 669 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 87:
#line 674 "test_spec_parse.y"
    {
		current_node->group = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 88:
#line 678 "test_spec_parse.y"
    {
		current_node->pgPort = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 89:
#line 682 "test_spec_parse.y"
    {
		strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 90:
#line 688 "test_spec_parse.y"
    {
		strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 91:
#line 694 "test_spec_parse.y"
    {
		strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str), sizeof(current_node->ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 92:
#line 699 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 93:
#line 704 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 94:
#line 709 "test_spec_parse.y"
    {
		current_node->replicationQuorum = true;
	;}
    break;

  case 95:
#line 713 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 96:
#line 717 "test_spec_parse.y"
    {
		strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 97:
#line 723 "test_spec_parse.y"
    {
		strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 98:
#line 729 "test_spec_parse.y"
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

  case 99:
#line 743 "test_spec_parse.y"
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

  case 100:
#line 764 "test_spec_parse.y"
    {
		current_spec->setup = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 101:
#line 771 "test_spec_parse.y"
    {
		current_spec->teardown = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 102:
#line 782 "test_spec_parse.y"
    {
		TestStep *s = (yyvsp[(3) - (3)].step);
		strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
		free((yyvsp[(2) - (3)].str));
		register_step(current_spec, s);
	;}
    break;

  case 103:
#line 800 "test_spec_parse.y"
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

  case 104:
#line 814 "test_spec_parse.y"
    {
		(yyval.step) = make_step("");
	;}
    break;

  case 105:
#line 818 "test_spec_parse.y"
    {
		if ((yyvsp[(2) - (2)].cmd)) append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
		(yyval.step) = (yyvsp[(1) - (2)].step);
	;}
    break;

  case 106:
#line 825 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 107:
#line 826 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 108:
#line 827 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 109:
#line 828 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 110:
#line 829 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 111:
#line 830 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 112:
#line 831 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 113:
#line 832 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 114:
#line 833 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 115:
#line 834 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 116:
#line 835 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 117:
#line 836 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 118:
#line 837 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 119:
#line 838 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 120:
#line 839 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 121:
#line 840 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 122:
#line 855 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 123:
#line 862 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 124:
#line 868 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 125:
#line 875 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 126:
#line 881 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 127:
#line 888 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 128:
#line 894 "test_spec_parse.y"
    {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 129:
#line 902 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 130:
#line 908 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	;}
    break;

  case 133:
#line 946 "test_spec_parse.y"
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

  case 134:
#line 961 "test_spec_parse.y"
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

  case 139:
#line 1001 "test_spec_parse.y"
    {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 140:
#line 1009 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 141:
#line 1017 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 142:
#line 1024 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 143:
#line 1035 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); ;}
    break;

  case 144:
#line 1040 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 145:
#line 1046 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); free((yyvsp[(6) - (6)].str)); ;}
    break;

  case 146:
#line 1051 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 147:
#line 1057 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str));
	;}
    break;

  case 148:
#line 1066 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str)); free((yyvsp[(6) - (7)].str));
	;}
    break;

  case 149:
#line 1075 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		free((yyvsp[(3) - (5)].str));
	;}
    break;

  case 150:
#line 1089 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(3) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 151:
#line 1097 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 152:
#line 1111 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 153:
#line 1126 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service,  (yyvsp[(4) - (8)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,     (yyvsp[(5) - (8)].str), sizeof((yyval.cmd)->args));
		strlcpy((yyval.cmd)->expected, (yyvsp[(7) - (8)].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(8) - (8)].ival);
		free((yyvsp[(4) - (8)].str)); free((yyvsp[(5) - (8)].str)); free((yyvsp[(7) - (8)].str));
	;}
    break;

  case 154:
#line 1143 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
		        "SELECT pgautofailover.wal_archived('%s', %d, '%s')",
		        (yyvsp[(8) - (11)].str), (yyvsp[(10) - (11)].ival), (yyvsp[(5) - (11)].str));
		strlcpy((yyval.cmd)->expected, "t", sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(11) - (11)].ival);
		free((yyvsp[(5) - (11)].str)); free((yyvsp[(8) - (11)].str));
	;}
    break;

  case 155:
#line 1167 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		if ((yyvsp[(9) - (10)].ival) >= 0)
		{
			sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
			        "SELECT reportedstate::text FROM pgautofailover.node"
			        " WHERE nodename LIKE 'archiver-%%' AND formationid = '%s'"
			        " AND groupid = %d", (yyvsp[(8) - (10)].str), (yyvsp[(9) - (10)].ival));
		}
		else
		{
			sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
			        "SELECT reportedstate::text FROM pgautofailover.node"
			        " WHERE nodename LIKE 'archiver-%%' AND formationid = '%s'", (yyvsp[(8) - (10)].str));
		}
		strlcpy((yyval.cmd)->expected, (yyvsp[(6) - (10)].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(10) - (10)].ival);
		free((yyvsp[(6) - (10)].str)); free((yyvsp[(8) - (10)].str));
	;}
    break;

  case 156:
#line 1196 "test_spec_parse.y"
    {
		if (strcmp((yyvsp[(4) - (11)].str), "source") != 0 &&
		    strcmp((yyvsp[(4) - (11)].str), "status") != 0 &&
		    strcmp((yyvsp[(4) - (11)].str), "replaymode") != 0)
		{
			fprintf(stderr,
			        "pgaftest: line %d: \"wait until basebackup %s ...\" -- "
			        "unknown property (expected source, status, or replaymode)\n",
			        pgaf_line_number, (yyvsp[(4) - (11)].str));
			exit(1);
		}
		(yyval.cmd) = make_cmd(CMD_WAIT_SQL);
		strlcpy((yyval.cmd)->service, "monitor", sizeof((yyval.cmd)->service));
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args),
		        "SELECT %s::text FROM pgautofailover.get_latest_basebackup('%s', %d)",
		        (yyvsp[(4) - (11)].str), (yyvsp[(8) - (11)].str), (yyvsp[(10) - (11)].ival));
		strlcpy((yyval.cmd)->expected, (yyvsp[(6) - (11)].str), sizeof((yyval.cmd)->expected));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(11) - (11)].ival);
		free((yyvsp[(4) - (11)].str)); free((yyvsp[(6) - (11)].str)); free((yyvsp[(8) - (11)].str));
	;}
    break;

  case 157:
#line 1226 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 158:
#line 1232 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 159:
#line 1239 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 160:
#line 1245 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 163:
#line 1264 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 164:
#line 1269 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(4) - (4)].ival);
	;}
    break;

  case 165:
#line 1276 "test_spec_parse.y"
    { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; ;}
    break;

  case 166:
#line 1277 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;

  case 167:
#line 1278 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(3) - (3)].ival); ;}
    break;

  case 168:
#line 1290 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 169:
#line 1298 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 170:
#line 1306 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 171:
#line 1314 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 172:
#line 1332 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 173:
#line 1347 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 174:
#line 1354 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	;}
    break;

  case 175:
#line 1358 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->state));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 176:
#line 1364 "test_spec_parse.y"
    {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[(3) - (3)].ival));
	;}
    break;

  case 177:
#line 1377 "test_spec_parse.y"
    {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	;}
    break;

  case 178:
#line 1385 "test_spec_parse.y"
    {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 179:
#line 1393 "test_spec_parse.y"
    {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 180:
#line 1414 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 181:
#line 1421 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(4) - (4)].ival);
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 182:
#line 1428 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 183:
#line 1436 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (7)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(7) - (7)].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (7)].str));
	;}
    break;

  case 184:
#line 1452 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 185:
#line 1458 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 186:
#line 1479 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 187:
#line 1487 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 188:
#line 1502 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 189:
#line 1516 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	;}
    break;

  case 190:
#line 1520 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 191:
#line 1526 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 192:
#line 1532 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 193:
#line 1558 "test_spec_parse.y"
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

  case 194:
#line 1592 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 195:
#line 1598 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 196:
#line 1619 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 197:
#line 1635 "test_spec_parse.y"
    { pgaf_next_brace_is_while = 1; ;}
    break;

  case 198:
#line 1636 "test_spec_parse.y"
    { (yyval.step) = (yyvsp[(4) - (5)].step); ;}
    break;

  case 199:
#line 1641 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) - (5)].step)->commands : NULL;
		free((yyvsp[(2) - (5)].str));
	;}
    break;

  case 200:
#line 1660 "test_spec_parse.y"
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

  case 201:
#line 1685 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 202:
#line 1694 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 203:
#line 1703 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 204:
#line 1712 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 207:
#line 1733 "test_spec_parse.y"
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

  case 208:
#line 1754 "test_spec_parse.y"
    { (yyval.str) = "init"; ;}
    break;

  case 209:
#line 1755 "test_spec_parse.y"
    { (yyval.str) = "single"; ;}
    break;

  case 210:
#line 1756 "test_spec_parse.y"
    { (yyval.str) = "primary"; ;}
    break;

  case 211:
#line 1757 "test_spec_parse.y"
    { (yyval.str) = "wait_primary"; ;}
    break;

  case 212:
#line 1758 "test_spec_parse.y"
    { (yyval.str) = "wait_standby"; ;}
    break;

  case 213:
#line 1759 "test_spec_parse.y"
    { (yyval.str) = "demoted"; ;}
    break;

  case 214:
#line 1760 "test_spec_parse.y"
    { (yyval.str) = "demote_timeout"; ;}
    break;

  case 215:
#line 1761 "test_spec_parse.y"
    { (yyval.str) = "draining"; ;}
    break;

  case 216:
#line 1762 "test_spec_parse.y"
    { (yyval.str) = "secondary"; ;}
    break;

  case 217:
#line 1763 "test_spec_parse.y"
    { (yyval.str) = "catchingup"; ;}
    break;

  case 218:
#line 1764 "test_spec_parse.y"
    { (yyval.str) = "prepare_promotion"; ;}
    break;

  case 219:
#line 1765 "test_spec_parse.y"
    { (yyval.str) = "stop_replication"; ;}
    break;

  case 220:
#line 1766 "test_spec_parse.y"
    { (yyval.str) = "maintenance"; ;}
    break;

  case 221:
#line 1767 "test_spec_parse.y"
    { (yyval.str) = "join_primary"; ;}
    break;

  case 222:
#line 1768 "test_spec_parse.y"
    { (yyval.str) = "apply_settings"; ;}
    break;

  case 223:
#line 1769 "test_spec_parse.y"
    { (yyval.str) = "prepare_maintenance"; ;}
    break;

  case 224:
#line 1770 "test_spec_parse.y"
    { (yyval.str) = "wait_maintenance"; ;}
    break;

  case 225:
#line 1771 "test_spec_parse.y"
    { (yyval.str) = "report_lsn"; ;}
    break;

  case 226:
#line 1772 "test_spec_parse.y"
    { (yyval.str) = "fast_forward"; ;}
    break;

  case 227:
#line 1773 "test_spec_parse.y"
    { (yyval.str) = "join_secondary"; ;}
    break;

  case 228:
#line 1774 "test_spec_parse.y"
    { (yyval.str) = "dropped"; ;}
    break;

  case 229:
#line 1782 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 230:
#line 1783 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 231:
#line 1794 "test_spec_parse.y"
    { (yyval.str) = strdup((yyvsp[(1) - (1)].str)); ;}
    break;

  case 232:
#line 1795 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 233:
#line 1803 "test_spec_parse.y"
    { (yyval.ival) = -1; ;}
    break;

  case 234:
#line 1804 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3856 "test_spec_parse.c"
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


#line 1807 "test_spec_parse.y"


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
			if (strcmp(cluster->formations[fi].name, a->formations[0]) == 0)
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

