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
     T_INTEGER = 372,
     T_IDENT = 373,
     T_STRING = 374,
     T_BLOCK = 375,
     T_SHELL_ARGS = 376
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
#define T_INTEGER 372
#define T_IDENT 373
#define T_STRING 374
#define T_BLOCK 375
#define T_SHELL_ARGS 376




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
#line 490 "test_spec_parse.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 503 "test_spec_parse.c"

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
#define YYLAST   609

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  122
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  69
/* YYNRULES -- Number of rules.  */
#define YYNRULES  226
/* YYNRULES -- Number of states.  */
#define YYNSTATES  376

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   376

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
     115,   116,   117,   118,   119,   120,   121
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
     437,   444,   450,   457,   459,   461,   465,   469,   470,   473,
     476,   481,   482,   485,   489,   496,   503,   510,   517,   521,
     524,   527,   531,   535,   538,   540,   544,   547,   552,   558,
     566,   570,   574,   580,   586,   589,   592,   596,   600,   604,
     609,   613,   617,   621,   622,   628,   634,   638,   643,   649,
     654,   660,   663,   664,   667,   669,   671,   673,   675,   677,
     679,   681,   683,   685,   687,   689,   691,   693,   695,   697,
     699,   701,   703,   705,   707,   709,   711
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     123,     0,    -1,   124,    -1,   123,   124,    -1,   125,    -1,
     151,    -1,   152,    -1,   153,    -1,   187,    -1,    -1,     3,
     103,   126,   127,   104,    -1,    -1,   127,   128,    -1,   133,
      -1,   134,    -1,   136,    -1,   137,    -1,   135,    -1,   138,
      -1,   129,    -1,    45,    -1,    46,    -1,    -1,    22,   118,
     130,   103,   131,   104,    -1,    -1,   131,   132,    -1,    18,
     118,    -1,    47,   118,    -1,    47,   119,    -1,    27,    77,
      26,    28,    -1,    26,    28,    -1,    27,    28,    -1,     4,
      -1,     4,    41,   118,    -1,     4,    14,   118,    -1,     4,
      37,   117,    -1,     4,    38,   119,    -1,     4,   118,    26,
      28,    -1,     4,   118,    32,    96,    -1,     4,   118,    26,
      28,    38,   119,    -1,    13,   119,    -1,    13,   118,    -1,
      44,   118,    -1,    44,   119,    -1,    15,   118,    -1,    16,
     118,    -1,    17,   118,    -1,    -1,    18,   139,   140,   103,
     143,   104,    -1,    -1,   140,   142,    -1,   118,    -1,   119,
      -1,    16,    -1,     4,    -1,     5,    -1,   141,    -1,    19,
     117,    -1,    57,    30,    -1,    -1,   143,   146,    -1,   118,
      -1,     4,    -1,    -1,    -1,   144,   145,   147,   149,    -1,
      -1,     5,   118,   145,   148,   103,   149,   104,    -1,    -1,
     149,   150,    -1,    20,    -1,    21,    -1,    22,    -1,    23,
      -1,    24,    -1,    25,    -1,    28,    -1,    26,    28,    -1,
      27,    28,    -1,    27,    77,    26,    28,    -1,    26,    29,
      -1,    29,    -1,    34,    -1,    35,    -1,    36,   117,    -1,
      47,   118,    -1,    47,   119,    -1,   102,   117,    -1,    37,
     117,    -1,    40,   118,    -1,    41,   118,    -1,    15,   118,
      -1,    16,   118,    -1,    17,   118,    -1,    42,    31,    -1,
      42,    30,    -1,    43,   119,    -1,    39,   119,    -1,    33,
     118,   118,    -1,    33,   118,   119,    -1,     8,   154,    -1,
       9,   154,    -1,    10,   190,   154,    -1,   103,   155,   104,
      -1,    -1,   155,   156,    -1,   157,    -1,   163,    -1,   170,
      -1,   171,    -1,   172,    -1,   173,    -1,   175,    -1,   176,
      -1,   178,    -1,   179,    -1,   180,    -1,   181,    -1,   184,
      -1,   185,    -1,   186,    -1,   177,    -1,    70,   118,   121,
      -1,    70,   118,    -1,    71,   118,   121,    -1,    71,   118,
      -1,    72,   118,   121,    -1,    72,   118,    -1,    73,   118,
     121,    -1,    73,   118,    -1,    73,    -1,    12,    -1,    78,
      -1,   118,    99,   158,   189,    -1,   118,    99,   158,   118,
      -1,   159,    -1,   160,    77,   159,    -1,    -1,   109,   162,
      -1,   189,    -1,   118,    -1,   162,   105,   189,    -1,   162,
     105,   118,    -1,    -1,    74,    75,   118,    99,   158,   189,
     164,   161,   169,    -1,    -1,    74,    75,   118,    99,   158,
     118,   165,   161,   169,    -1,    74,    75,   118,   100,   158,
     189,   169,    -1,    74,    75,   118,   100,   158,   118,   169,
      -1,    74,    75,   118,    96,   169,    -1,    74,    75,   118,
      80,   118,   169,    -1,    74,    75,   166,   167,   169,    -1,
      74,    75,   159,    77,   160,   169,    -1,   189,    -1,   118,
      -1,   166,   105,   189,    -1,   166,   105,   118,    -1,    -1,
     101,   168,    -1,   102,   117,    -1,   168,   105,   102,   117,
      -1,    -1,    76,   117,    -1,    79,    76,   117,    -1,    81,
     118,    99,   158,   189,   169,    -1,    81,   118,    99,   158,
     118,   169,    -1,    81,   118,   100,   158,   189,   169,    -1,
      81,   118,   100,   158,   118,   169,    -1,    82,   118,   120,
      -1,    83,   120,    -1,    83,    84,    -1,    83,    84,   118,
      -1,    83,    84,   117,    -1,    85,   174,    -1,   118,    -1,
     174,   105,   118,    -1,    86,    87,    -1,    86,    87,   102,
     117,    -1,    86,    87,   101,    18,   118,    -1,    86,    87,
     101,    18,   118,   102,   117,    -1,    88,    89,   118,    -1,
      88,    90,   118,    -1,    48,   110,   118,   118,   118,    -1,
      48,   111,   118,   118,   118,    -1,    91,   117,    -1,    92,
      93,    -1,    92,    94,   118,    -1,    92,    95,   118,    -1,
      92,    97,   118,    -1,    92,    98,   118,   121,    -1,    95,
     106,   144,    -1,    94,   106,   144,    -1,   112,    10,   144,
      -1,    -1,   108,   183,   103,   155,   104,    -1,    81,   144,
     107,   189,   182,    -1,   110,   118,   118,    -1,   113,   118,
     115,   119,    -1,   113,   118,   114,   115,   119,    -1,   113,
     118,   116,   119,    -1,   113,   118,   114,   116,   119,    -1,
      11,   188,    -1,    -1,   188,   190,    -1,    49,    -1,    50,
      -1,    51,    -1,    52,    -1,    53,    -1,    54,    -1,    55,
      -1,    56,    -1,    57,    -1,    58,    -1,    59,    -1,    60,
      -1,    61,    -1,    62,    -1,    63,    -1,    64,    -1,    65,
      -1,    66,    -1,    67,    -1,    68,    -1,    69,    -1,   118,
      -1,   119,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   217,   217,   218,   222,   223,   224,   225,   226,   239,
     238,   248,   250,   254,   255,   256,   257,   258,   259,   260,
     261,   262,   285,   284,   302,   304,   308,   322,   327,   332,
     339,   343,   359,   363,   370,   377,   383,   390,   397,   404,
     417,   423,   433,   439,   449,   459,   465,   476,   475,   492,
     494,   503,   504,   505,   506,   507,   511,   516,   520,   526,
     528,   547,   548,   557,   574,   573,   581,   580,   588,   590,
     594,   599,   604,   608,   612,   616,   620,   626,   631,   635,
     640,   644,   648,   652,   656,   660,   665,   670,   674,   678,
     684,   690,   695,   700,   705,   709,   713,   719,   725,   739,
     760,   767,   778,   796,   811,   814,   822,   823,   824,   825,
     826,   827,   828,   829,   830,   831,   832,   833,   834,   835,
     836,   837,   851,   858,   864,   871,   877,   884,   890,   898,
     904,   931,   931,   942,   957,   975,   976,   991,   993,   997,
    1005,  1013,  1020,  1032,  1031,  1043,  1042,  1053,  1062,  1071,
    1085,  1093,  1107,  1122,  1128,  1135,  1141,  1154,  1156,  1160,
    1165,  1173,  1174,  1175,  1186,  1194,  1202,  1210,  1228,  1243,
    1250,  1254,  1260,  1273,  1281,  1289,  1310,  1317,  1324,  1332,
    1348,  1354,  1375,  1383,  1398,  1412,  1416,  1422,  1428,  1454,
    1488,  1494,  1515,  1532,  1532,  1537,  1556,  1581,  1590,  1599,
    1608,  1624,  1627,  1629,  1651,  1652,  1653,  1654,  1655,  1656,
    1657,  1658,  1659,  1660,  1661,  1662,  1663,  1664,  1665,  1666,
    1667,  1668,  1669,  1670,  1671,  1679,  1680
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
  "T_GET", "T_FSM", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES",
  "T_INTEGER", "T_IDENT", "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept",
  "spec", "spec_item", "cluster_block", "@1", "cluster_item_list",
  "cluster_item", "archiver_block", "@2", "archiver_opt_list",
  "archiver_opt", "monitor_line", "image_line", "extension_version_line",
  "ssl_line", "auth_line", "formation_block", "@3", "formation_opt_list",
  "bare_name", "formation_opt", "node_list", "node_name", "init_node_slot",
  "node_line", "@4", "@5", "node_opt_list", "node_opt", "setup_block",
  "teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
  "exec_cmd", "state_op", "wait_multi_condition",
  "wait_multi_condition_list", "opt_passing_through", "pass_state_list",
  "wait_cmd", "@6", "@7", "state_name_list", "opt_in_group", "group_items",
  "opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd", "promote_cmd",
  "promote_list", "perform_cmd", "network_cmd", "nodeini_cmd", "sleep_cmd",
  "compose_cmd", "postgres_ctl_cmd", "fsm_step_cmd", "while_body", "@8",
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
     375,   376
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   122,   123,   123,   124,   124,   124,   124,   124,   126,
     125,   127,   127,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   130,   129,   131,   131,   132,   132,   132,   132,
     132,   132,   133,   133,   133,   133,   133,   133,   133,   133,
     134,   134,   135,   135,   136,   137,   137,   139,   138,   140,
     140,   141,   141,   141,   141,   141,   142,   142,   142,   143,
     143,   144,   144,   145,   147,   146,   148,   146,   149,   149,
     150,   150,   150,   150,   150,   150,   150,   150,   150,   150,
     150,   150,   150,   150,   150,   150,   150,   150,   150,   150,
     150,   150,   150,   150,   150,   150,   150,   150,   150,   150,
     151,   152,   153,   154,   155,   155,   156,   156,   156,   156,
     156,   156,   156,   156,   156,   156,   156,   156,   156,   156,
     156,   156,   157,   157,   157,   157,   157,   157,   157,   157,
     157,   158,   158,   159,   159,   160,   160,   161,   161,   162,
     162,   162,   162,   164,   163,   165,   163,   163,   163,   163,
     163,   163,   163,   166,   166,   166,   166,   167,   167,   168,
     168,   169,   169,   169,   170,   170,   170,   170,   171,   172,
     172,   172,   172,   173,   174,   174,   175,   175,   175,   175,
     176,   176,   177,   177,   178,   179,   179,   179,   179,   179,
     180,   180,   181,   183,   182,   184,   185,   186,   186,   186,
     186,   187,   188,   188,   189,   189,   189,   189,   189,   189,
     189,   189,   189,   189,   189,   189,   189,   189,   189,   189,
     189,   189,   189,   189,   189,   190,   190
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
       6,     5,     6,     1,     1,     3,     3,     0,     2,     2,
       4,     0,     2,     3,     6,     6,     6,     6,     3,     2,
       2,     3,     3,     2,     1,     3,     2,     4,     5,     7,
       3,     3,     5,     5,     2,     2,     3,     3,     3,     4,
       3,     3,     3,     0,     5,     5,     3,     4,     5,     4,
       5,     2,     0,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   202,     0,     2,     4,     5,
       6,     7,     8,     9,   104,   100,   101,   225,   226,     0,
     201,     1,     3,    11,     0,   102,   203,     0,     0,     0,
       0,     0,   130,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   103,     0,     0,     0,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   121,   114,   115,
     116,   117,   118,   119,   120,    32,     0,     0,     0,     0,
      47,     0,     0,    20,    21,    10,    12,    19,    13,    14,
      17,    15,    16,    18,     0,     0,   123,   125,   127,   129,
       0,    62,    61,     0,     0,   170,   169,   174,   173,   176,
       0,     0,   184,   185,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    41,    40,
      44,    45,    46,    49,    22,    42,    43,     0,     0,   122,
     124,   126,   128,   204,   205,   206,   207,   208,   209,   210,
     211,   212,   213,   214,   215,   216,   217,   218,   219,   220,
     221,   222,   223,   224,   154,     0,   157,   153,     0,     0,
       0,   168,   172,   171,     0,     0,     0,   180,   181,   186,
     187,   188,     0,    61,   191,   190,   196,   192,     0,     0,
       0,    34,    35,    36,    33,     0,     0,     0,     0,     0,
       0,     0,   161,     0,     0,     0,     0,     0,   161,   131,
     132,     0,     0,     0,   175,     0,   177,   189,     0,     0,
     197,   199,    37,    38,    54,    55,    53,     0,     0,    59,
      51,    52,    56,    50,    24,   182,   183,   161,     0,     0,
     149,     0,     0,     0,   135,   161,     0,   158,   156,   155,
     151,   161,   161,   161,   161,   193,   195,   178,   198,   200,
       0,    57,    58,     0,     0,   150,   162,     0,   145,   143,
     161,   161,     0,     0,   152,   159,     0,   165,   164,   167,
     166,     0,     0,    39,     0,    48,    63,    60,     0,     0,
       0,     0,    23,    25,   163,   137,   137,   148,   147,     0,
     136,     0,   104,   179,    63,    64,    26,    30,    31,     0,
      27,    28,     0,   161,   161,   134,   133,   160,     0,    66,
      68,     0,   140,   138,   139,   146,   144,   194,     0,    65,
      29,     0,    68,     0,     0,     0,    70,    71,    72,    73,
      74,    75,     0,     0,    76,    81,     0,    82,    83,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    69,   142,
     141,     0,    91,    92,    93,    77,    80,    78,     0,     0,
      84,    88,    97,    89,    90,    95,    94,    96,    85,    86,
      87,    67,     0,    98,    99,    79
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,    23,    27,    76,    77,   188,   254,
     283,    78,    79,    80,    81,    82,    83,   123,   187,   222,
     223,   253,    93,   295,   277,   310,   318,   319,   348,     9,
      10,    11,    15,    24,    48,    49,   201,   155,   235,   303,
     313,    50,   286,   285,   156,   198,   237,   230,    51,    52,
      53,    54,    98,    55,    56,    57,    58,    59,    60,    61,
     246,   271,    62,    63,    64,    12,    20,   157,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -180
static const yytype_int16 yypact[] =
{
      65,   -89,   -87,   -87,   -49,  -180,   130,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,   -87,
     -49,  -180,  -180,  -180,   430,  -180,  -180,     8,   -32,   -96,
     -86,   -82,   -68,   -18,    -1,   -52,   -71,    -4,    31,    21,
      43,    50,    10,    16,  -180,    56,   116,    62,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,    -3,     9,    72,    85,    91,
    -180,    99,    17,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,   127,   154,   153,   155,   156,   157,
      34,  -180,    54,   168,   159,    38,  -180,  -180,   175,    71,
     163,   164,  -180,  -180,   165,   166,   167,   169,     5,     5,
     192,     5,    35,   193,   195,   194,   196,    29,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,   197,   220,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,   -53,   209,   -72,  -180,    27,    27,
     540,  -180,  -180,  -180,   221,   322,   224,  -180,  -180,  -180,
    -180,  -180,   222,  -180,  -180,  -180,  -180,  -180,    86,   223,
     225,  -180,  -180,  -180,  -180,   317,   250,     1,   244,   230,
     231,   232,    30,    27,    27,   233,   251,   170,    30,  -180,
    -180,   198,   240,   246,  -180,   234,  -180,  -180,   236,   237,
    -180,  -180,   319,  -180,  -180,  -180,  -180,   263,   351,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,    30,   265,   307,
    -180,   268,   310,   285,  -180,    55,   291,   280,  -180,  -180,
    -180,    30,    30,    30,    30,  -180,  -180,   308,  -180,  -180,
     290,  -180,  -180,     3,    33,  -180,  -180,   294,   335,   336,
      30,    30,    27,   233,  -180,  -180,   312,  -180,  -180,  -180,
    -180,   313,   298,  -180,   299,  -180,  -180,  -180,   300,   391,
     -10,    97,  -180,  -180,  -180,   311,   311,  -180,  -180,   338,
    -180,   304,  -180,  -180,  -180,  -180,  -180,  -180,  -180,   396,
    -180,  -180,   380,    30,    30,  -180,  -180,  -180,   475,  -180,
    -180,   395,  -180,   320,  -180,  -180,  -180,  -180,   321,   171,
    -180,   408,  -180,   309,   332,   333,  -180,  -180,  -180,  -180,
    -180,  -180,   212,     0,  -180,  -180,   334,  -180,  -180,   337,
     362,   361,   363,   364,   238,   365,   124,   366,  -180,  -180,
    -180,   142,  -180,  -180,  -180,  -180,  -180,  -180,   400,   152,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,   425,  -180,  -180,  -180
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -180,  -180,   449,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -107,   191,  -180,  -180,  -180,   172,  -180,  -180,
    -180,  -180,    12,   199,  -180,  -180,  -149,  -155,  -180,   200,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -179,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,  -180,
    -180,  -180,  -180,  -180,  -180,  -180,  -180,  -160,   467
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -135
static const yytype_int16 yytable[] =
{
     203,   174,   175,    91,   177,   214,   215,    91,   274,    91,
     202,   113,    65,    95,    13,    16,    14,   216,   298,   240,
     217,    66,    86,    67,    68,    69,    70,   191,   357,   196,
      71,    25,    87,   197,   114,   115,    88,   239,   116,   199,
     234,   242,   244,   192,   231,   232,   193,   194,   255,    96,
      89,   278,    72,    73,    74,   185,   264,    90,   218,   279,
     280,   186,   267,   268,   269,   270,    94,   299,     1,    17,
      18,   259,   261,     2,     3,     4,     5,   358,    84,    85,
     281,   287,   288,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   219,   200,   228,   275,   290,   229,
     100,   101,    75,   289,    97,   117,   108,    92,    99,   220,
     221,   173,   109,   173,   315,   316,   111,   118,   119,   306,
      21,   228,   263,     1,   229,   125,   126,   282,     2,     3,
       4,     5,   314,   103,   104,   105,   276,   106,   107,   178,
     179,   180,   154,   158,   159,   162,   163,   323,   324,   325,
     102,   350,   326,   327,   328,   329,   330,   331,   332,   333,
     334,   335,   165,   166,   110,   336,   337,   338,   339,   340,
     112,   341,   342,   343,   344,   345,   323,   324,   325,   346,
     120,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   208,   209,   121,   336,   337,   338,   339,   340,   122,
     341,   342,   343,   344,   345,   300,   301,   124,   346,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     355,   356,   368,   369,   347,   127,   371,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   365,   366,
     373,   374,   128,   347,   129,   160,   130,   131,   132,   161,
     164,   167,   168,   169,   170,   171,   195,   172,   238,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     176,   181,   182,   183,   184,   189,   241,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   190,   204,
     205,   206,   210,   207,   211,   212,   213,   224,   225,   226,
     227,   233,   247,   236,   245,   248,   249,   250,   243,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     251,   252,   256,   257,   262,   266,   258,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   265,   273,
     272,   284,  -134,  -133,   291,   293,   292,   294,   296,   297,
     302,   307,   311,   320,   322,   321,   372,   352,   260,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     353,   354,   359,   375,   360,    22,   305,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,    28,   361,
     362,   363,   364,   370,   367,   309,   304,    26,     0,     0,
       0,   308,     0,     0,   351,     0,     0,     0,   312,     0,
      29,    30,    31,    32,    33,     0,     0,     0,     0,     0,
       0,    34,    35,    36,     0,    37,    38,     0,    39,     0,
       0,    40,    41,    28,    42,    43,   349,     0,     0,     0,
       0,     0,     0,     0,    44,     0,     0,     0,     0,     0,
      45,     0,    46,    47,     0,    29,    30,    31,    32,    33,
       0,     0,     0,     0,     0,     0,    34,    35,    36,     0,
      37,    38,     0,    39,     0,     0,    40,    41,     0,    42,
      43,     0,     0,     0,     0,     0,     0,     0,     0,   317,
       0,     0,     0,     0,     0,    45,     0,    46,    47,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153
};

static const yytype_int16 yycheck[] =
{
     160,   108,   109,     4,   111,     4,     5,     4,     5,     4,
     159,    14,     4,    84,   103,     3,   103,    16,    28,   198,
      19,    13,   118,    15,    16,    17,    18,    80,    28,   101,
      22,    19,   118,   105,    37,    38,   118,   197,    41,    12,
     195,   201,   202,    96,   193,   194,    99,   100,   227,   120,
     118,    18,    44,    45,    46,    26,   235,    75,    57,    26,
      27,    32,   241,   242,   243,   244,   118,    77,     3,   118,
     119,   231,   232,     8,     9,    10,    11,    77,   110,   111,
      47,   260,   261,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,   103,    78,    76,   104,   263,    79,
      89,    90,   104,   262,   118,   118,   106,   118,    87,   118,
     119,   118,   106,   118,   303,   304,    10,   118,   119,   289,
       0,    76,    77,     3,    79,   118,   119,   104,     8,     9,
      10,    11,   302,    93,    94,    95,   253,    97,    98,   114,
     115,   116,   118,    99,   100,   117,   118,    15,    16,    17,
     117,   321,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,   101,   102,   118,    33,    34,    35,    36,    37,
     118,    39,    40,    41,    42,    43,    15,    16,    17,    47,
     118,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,   115,   116,   118,    33,    34,    35,    36,    37,   118,
      39,    40,    41,    42,    43,   118,   119,   118,    47,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      28,    29,   118,   119,   102,   118,   104,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    30,    31,
     118,   119,   118,   102,   121,   107,   121,   121,   121,   120,
     105,   118,   118,   118,   118,   118,    77,   118,   118,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
     118,   118,   117,   119,   118,   118,   118,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,   118,   118,
      18,   117,   119,   121,   119,    28,    96,   103,   118,   118,
     118,   118,   118,   102,   108,   119,   119,    38,   118,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
     117,    30,   117,    76,    99,   105,   118,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,   117,   119,
     102,   117,    77,    77,   102,   117,   103,   118,   118,    28,
     109,   117,    26,    28,   103,   105,    26,   118,   118,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
     118,   118,   118,    28,   117,     6,   118,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    48,   117,
     119,   118,   118,   117,   119,   294,   286,    20,    -1,    -1,
      -1,   292,    -1,    -1,   322,    -1,    -1,    -1,   118,    -1,
      70,    71,    72,    73,    74,    -1,    -1,    -1,    -1,    -1,
      -1,    81,    82,    83,    -1,    85,    86,    -1,    88,    -1,
      -1,    91,    92,    48,    94,    95,   118,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   104,    -1,    -1,    -1,    -1,    -1,
     110,    -1,   112,   113,    -1,    70,    71,    72,    73,    74,
      -1,    -1,    -1,    -1,    -1,    -1,    81,    82,    83,    -1,
      85,    86,    -1,    88,    -1,    -1,    91,    92,    -1,    94,
      95,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   104,
      -1,    -1,    -1,    -1,    -1,   110,    -1,   112,   113,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   123,   124,   125,   151,
     152,   153,   187,   103,   103,   154,   154,   118,   119,   190,
     188,     0,   124,   126,   155,   154,   190,   127,    48,    70,
      71,    72,    73,    74,    81,    82,    83,    85,    86,    88,
      91,    92,    94,    95,   104,   110,   112,   113,   156,   157,
     163,   170,   171,   172,   173,   175,   176,   177,   178,   179,
     180,   181,   184,   185,   186,     4,    13,    15,    16,    17,
      18,    22,    44,    45,    46,   104,   128,   129,   133,   134,
     135,   136,   137,   138,   110,   111,   118,   118,   118,   118,
      75,     4,   118,   144,   118,    84,   120,   118,   174,    87,
      89,    90,   117,    93,    94,    95,    97,    98,   106,   106,
     118,    10,   118,    14,    37,    38,    41,   118,   118,   119,
     118,   118,   118,   139,   118,   118,   119,   118,   118,   121,
     121,   121,   121,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,   118,   159,   166,   189,    99,   100,
     107,   120,   117,   118,   105,   101,   102,   118,   118,   118,
     118,   118,   118,   118,   144,   144,   118,   144,   114,   115,
     116,   118,   117,   119,   118,    26,    32,   140,   130,   118,
     118,    80,    96,    99,   100,    77,   101,   105,   167,    12,
      78,   158,   158,   189,   118,    18,   117,   121,   115,   116,
     119,   119,    28,    96,     4,     5,    16,    19,    57,   103,
     118,   119,   141,   142,   103,   118,   118,   118,    76,    79,
     169,   158,   158,   118,   159,   160,   102,   168,   118,   189,
     169,   118,   189,   118,   189,   108,   182,   118,   119,   119,
      38,   117,    30,   143,   131,   169,   117,    76,   118,   189,
     118,   189,    99,    77,   169,   117,   105,   169,   169,   169,
     169,   183,   102,   119,     5,   104,   144,   146,    18,    26,
      27,    47,   104,   132,   117,   165,   164,   169,   169,   158,
     159,   102,   103,   117,   118,   145,   118,    28,    28,    77,
     118,   119,   109,   161,   161,   118,   189,   117,   155,   145,
     147,    26,   118,   162,   189,   169,   169,   104,   148,   149,
      28,   105,   103,    15,    16,    17,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    33,    34,    35,    36,
      37,    39,    40,    41,    42,    43,    47,   102,   150,   118,
     189,   149,   118,   118,   118,    28,    29,    28,    77,   118,
     117,   117,   119,   118,   118,    30,    31,   119,   118,   119,
     117,   104,    26,   118,   119,    28
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
#line 239 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	;}
    break;

  case 20:
#line 261 "test_spec_parse.y"
    { current_spec->cluster.bindSource = true; ;}
    break;

  case 21:
#line 262 "test_spec_parse.y"
    { current_spec->cluster.legacyStartup = true; ;}
    break;

  case 22:
#line 285 "test_spec_parse.y"
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
#line 309 "test_spec_parse.y"
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
#line 323 "test_spec_parse.y"
    {
		strlcpy(current_archiver->region, (yyvsp[(2) - (2)].str), sizeof(current_archiver->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 28:
#line 328 "test_spec_parse.y"
    {
		strlcpy(current_archiver->region, (yyvsp[(2) - (2)].str), sizeof(current_archiver->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 29:
#line 333 "test_spec_parse.y"
    {
		/* bare "create and launch deferred" = both gates, matching
		 * node_opt's own identical form */
		current_archiver->createDeferred = true;
		current_archiver->launchDeferred = true;
	;}
    break;

  case 30:
#line 340 "test_spec_parse.y"
    {
		current_archiver->launchDeferred = true;
	;}
    break;

  case 31:
#line 344 "test_spec_parse.y"
    {
		current_archiver->createDeferred = true;
	;}
    break;

  case 32:
#line 360 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
	;}
    break;

  case 33:
#line 364 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 34:
#line 371 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 35:
#line 378 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[(3) - (3)].ival);
	;}
    break;

  case 36:
#line 384 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 37:
#line 391 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 38:
#line 398 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 39:
#line 405 "test_spec_parse.y"
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
#line 418 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 41:
#line 424 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 42:
#line 434 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 43:
#line 440 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 44:
#line 450 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 45:
#line 460 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 46:
#line 466 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 47:
#line 476 "test_spec_parse.y"
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
#line 503 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 52:
#line 504 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 53:
#line 505 "test_spec_parse.y"
    { (yyval.str) = strdup("auth"); ;}
    break;

  case 54:
#line 506 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 55:
#line 507 "test_spec_parse.y"
    { (yyval.str) = strdup("node"); ;}
    break;

  case 56:
#line 512 "test_spec_parse.y"
    {
		strlcpy(current_formation->name, (yyvsp[(1) - (1)].str), sizeof(current_formation->name));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 57:
#line 517 "test_spec_parse.y"
    {
		current_formation->numSync = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 58:
#line 521 "test_spec_parse.y"
    {
		current_formation->disableSecondary = true;
	;}
    break;

  case 61:
#line 547 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 62:
#line 548 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 63:
#line 557 "test_spec_parse.y"
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
#line 574 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(1) - (2)].str), sizeof(current_node->name));
		free((yyvsp[(1) - (2)].str));
	;}
    break;

  case 66:
#line 581 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(2) - (3)].str), sizeof(current_node->name));
		free((yyvsp[(2) - (3)].str));
	;}
    break;

  case 70:
#line 595 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 71:
#line 600 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 72:
#line 605 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_ARCHIVER;
	;}
    break;

  case 73:
#line 609 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 74:
#line 613 "test_spec_parse.y"
    {
		current_node->noMonitor = true;
	;}
    break;

  case 75:
#line 617 "test_spec_parse.y"
    {
		current_node->suspended = true;
	;}
    break;

  case 76:
#line 621 "test_spec_parse.y"
    {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 77:
#line 627 "test_spec_parse.y"
    {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	;}
    break;

  case 78:
#line 632 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
	;}
    break;

  case 79:
#line 636 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 80:
#line 641 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 81:
#line 645 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 82:
#line 649 "test_spec_parse.y"
    {
		current_node->listen = true;
	;}
    break;

  case 83:
#line 653 "test_spec_parse.y"
    {
		current_node->citusSecondary = true;
	;}
    break;

  case 84:
#line 657 "test_spec_parse.y"
    {
		current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 85:
#line 661 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 86:
#line 666 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 87:
#line 671 "test_spec_parse.y"
    {
		current_node->group = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 88:
#line 675 "test_spec_parse.y"
    {
		current_node->pgPort = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 89:
#line 679 "test_spec_parse.y"
    {
		strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 90:
#line 685 "test_spec_parse.y"
    {
		strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 91:
#line 691 "test_spec_parse.y"
    {
		strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str), sizeof(current_node->ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 92:
#line 696 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 93:
#line 701 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 94:
#line 706 "test_spec_parse.y"
    {
		current_node->replicationQuorum = true;
	;}
    break;

  case 95:
#line 710 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 96:
#line 714 "test_spec_parse.y"
    {
		strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 97:
#line 720 "test_spec_parse.y"
    {
		strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 98:
#line 726 "test_spec_parse.y"
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
#line 740 "test_spec_parse.y"
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
#line 761 "test_spec_parse.y"
    {
		current_spec->setup = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 101:
#line 768 "test_spec_parse.y"
    {
		current_spec->teardown = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 102:
#line 779 "test_spec_parse.y"
    {
		TestStep *s = (yyvsp[(3) - (3)].step);
		strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
		free((yyvsp[(2) - (3)].str));
		register_step(current_spec, s);
	;}
    break;

  case 103:
#line 797 "test_spec_parse.y"
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
#line 811 "test_spec_parse.y"
    {
		(yyval.step) = make_step("");
	;}
    break;

  case 105:
#line 815 "test_spec_parse.y"
    {
		if ((yyvsp[(2) - (2)].cmd)) append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
		(yyval.step) = (yyvsp[(1) - (2)].step);
	;}
    break;

  case 106:
#line 822 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 107:
#line 823 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 108:
#line 824 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 109:
#line 825 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 110:
#line 826 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 111:
#line 827 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 112:
#line 828 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 113:
#line 829 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 114:
#line 830 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 115:
#line 831 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 116:
#line 832 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 117:
#line 833 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 118:
#line 834 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 119:
#line 835 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 120:
#line 836 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 121:
#line 837 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 122:
#line 852 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 123:
#line 859 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 124:
#line 865 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 125:
#line 872 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 126:
#line 878 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 127:
#line 885 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 128:
#line 891 "test_spec_parse.y"
    {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 129:
#line 899 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 130:
#line 905 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	;}
    break;

  case 133:
#line 943 "test_spec_parse.y"
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
#line 958 "test_spec_parse.y"
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
#line 998 "test_spec_parse.y"
    {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 140:
#line 1006 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 141:
#line 1014 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 142:
#line 1021 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 143:
#line 1032 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); ;}
    break;

  case 144:
#line 1037 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 145:
#line 1043 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); free((yyvsp[(6) - (6)].str)); ;}
    break;

  case 146:
#line 1048 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 147:
#line 1054 "test_spec_parse.y"
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
#line 1063 "test_spec_parse.y"
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
#line 1072 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		free((yyvsp[(3) - (5)].str));
	;}
    break;

  case 150:
#line 1086 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_LSN);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(3) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 151:
#line 1094 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 152:
#line 1108 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 153:
#line 1123 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 154:
#line 1129 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 155:
#line 1136 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 156:
#line 1142 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 159:
#line 1161 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 160:
#line 1166 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(4) - (4)].ival);
	;}
    break;

  case 161:
#line 1173 "test_spec_parse.y"
    { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; ;}
    break;

  case 162:
#line 1174 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;

  case 163:
#line 1175 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(3) - (3)].ival); ;}
    break;

  case 164:
#line 1187 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 165:
#line 1195 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 166:
#line 1203 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 167:
#line 1211 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 168:
#line 1229 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 169:
#line 1244 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 170:
#line 1251 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	;}
    break;

  case 171:
#line 1255 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->state));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 172:
#line 1261 "test_spec_parse.y"
    {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[(3) - (3)].ival));
	;}
    break;

  case 173:
#line 1274 "test_spec_parse.y"
    {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	;}
    break;

  case 174:
#line 1282 "test_spec_parse.y"
    {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 175:
#line 1290 "test_spec_parse.y"
    {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 176:
#line 1311 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 177:
#line 1318 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(4) - (4)].ival);
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 178:
#line 1325 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 179:
#line 1333 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (7)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(7) - (7)].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (7)].str));
	;}
    break;

  case 180:
#line 1349 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 181:
#line 1355 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 182:
#line 1376 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 183:
#line 1384 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 184:
#line 1399 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 185:
#line 1413 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	;}
    break;

  case 186:
#line 1417 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 187:
#line 1423 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 188:
#line 1429 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 189:
#line 1455 "test_spec_parse.y"
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

  case 190:
#line 1489 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 191:
#line 1495 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 192:
#line 1516 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FSM_STEP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 193:
#line 1532 "test_spec_parse.y"
    { pgaf_next_brace_is_while = 1; ;}
    break;

  case 194:
#line 1533 "test_spec_parse.y"
    { (yyval.step) = (yyvsp[(4) - (5)].step); ;}
    break;

  case 195:
#line 1538 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) - (5)].step)->commands : NULL;
		free((yyvsp[(2) - (5)].str));
	;}
    break;

  case 196:
#line 1557 "test_spec_parse.y"
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

  case 197:
#line 1582 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 198:
#line 1591 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 199:
#line 1600 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 200:
#line 1609 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 203:
#line 1630 "test_spec_parse.y"
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

  case 204:
#line 1651 "test_spec_parse.y"
    { (yyval.str) = "init"; ;}
    break;

  case 205:
#line 1652 "test_spec_parse.y"
    { (yyval.str) = "single"; ;}
    break;

  case 206:
#line 1653 "test_spec_parse.y"
    { (yyval.str) = "primary"; ;}
    break;

  case 207:
#line 1654 "test_spec_parse.y"
    { (yyval.str) = "wait_primary"; ;}
    break;

  case 208:
#line 1655 "test_spec_parse.y"
    { (yyval.str) = "wait_standby"; ;}
    break;

  case 209:
#line 1656 "test_spec_parse.y"
    { (yyval.str) = "demoted"; ;}
    break;

  case 210:
#line 1657 "test_spec_parse.y"
    { (yyval.str) = "demote_timeout"; ;}
    break;

  case 211:
#line 1658 "test_spec_parse.y"
    { (yyval.str) = "draining"; ;}
    break;

  case 212:
#line 1659 "test_spec_parse.y"
    { (yyval.str) = "secondary"; ;}
    break;

  case 213:
#line 1660 "test_spec_parse.y"
    { (yyval.str) = "catchingup"; ;}
    break;

  case 214:
#line 1661 "test_spec_parse.y"
    { (yyval.str) = "prepare_promotion"; ;}
    break;

  case 215:
#line 1662 "test_spec_parse.y"
    { (yyval.str) = "stop_replication"; ;}
    break;

  case 216:
#line 1663 "test_spec_parse.y"
    { (yyval.str) = "maintenance"; ;}
    break;

  case 217:
#line 1664 "test_spec_parse.y"
    { (yyval.str) = "join_primary"; ;}
    break;

  case 218:
#line 1665 "test_spec_parse.y"
    { (yyval.str) = "apply_settings"; ;}
    break;

  case 219:
#line 1666 "test_spec_parse.y"
    { (yyval.str) = "prepare_maintenance"; ;}
    break;

  case 220:
#line 1667 "test_spec_parse.y"
    { (yyval.str) = "wait_maintenance"; ;}
    break;

  case 221:
#line 1668 "test_spec_parse.y"
    { (yyval.str) = "report_lsn"; ;}
    break;

  case 222:
#line 1669 "test_spec_parse.y"
    { (yyval.str) = "fast_forward"; ;}
    break;

  case 223:
#line 1670 "test_spec_parse.y"
    { (yyval.str) = "join_secondary"; ;}
    break;

  case 224:
#line 1671 "test_spec_parse.y"
    { (yyval.str) = "dropped"; ;}
    break;

  case 225:
#line 1679 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 226:
#line 1680 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3710 "test_spec_parse.c"
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


#line 1683 "test_spec_parse.y"


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

