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
     T_REGION = 300,
     T_NODEINI = 301,
     T_FS_INIT = 302,
     T_FS_SINGLE = 303,
     T_FS_PRIMARY = 304,
     T_FS_WAIT_PRIMARY = 305,
     T_FS_WAIT_STANDBY = 306,
     T_FS_DEMOTED = 307,
     T_FS_DEMOTE_TIMEOUT = 308,
     T_FS_DRAINING = 309,
     T_FS_SECONDARY = 310,
     T_FS_CATCHINGUP = 311,
     T_FS_PREP_PROMOTION = 312,
     T_FS_STOP_REPLICATION = 313,
     T_FS_MAINTENANCE = 314,
     T_FS_JOIN_PRIMARY = 315,
     T_FS_APPLY_SETTINGS = 316,
     T_FS_PREPARE_MAINTENANCE = 317,
     T_FS_WAIT_MAINTENANCE = 318,
     T_FS_REPORT_LSN = 319,
     T_FS_FAST_FORWARD = 320,
     T_FS_JOIN_SECONDARY = 321,
     T_FS_DROPPED = 322,
     T_EXEC = 323,
     T_EXEC_FAILS = 324,
     T_RUN = 325,
     T_PG_AUTOCTL = 326,
     T_WAIT = 327,
     T_UNTIL = 328,
     T_TIMEOUT = 329,
     T_AND = 330,
     T_IS = 331,
     T_WITH = 332,
     T_ASSERT = 333,
     T_SQL = 334,
     T_EXPECT = 335,
     T_ERROR = 336,
     T_PROMOTE = 337,
     T_PERFORM = 338,
     T_FAILOVER = 339,
     T_NETWORK = 340,
     T_DISCONNECT = 341,
     T_CONNECT = 342,
     T_SLEEP = 343,
     T_COMPOSE = 344,
     T_DOWN = 345,
     T_START = 346,
     T_STOP = 347,
     T_STOPPED = 348,
     T_KILL = 349,
     T_INJECT = 350,
     T_STATE = 351,
     T_ASSIGNED_STATE = 352,
     T_IN = 353,
     T_GROUP = 354,
     T_LBRACE = 355,
     T_RBRACE = 356,
     T_COMMA = 357,
     T_POSTGRES = 358,
     T_STAYS = 359,
     T_WHILE = 360,
     T_THROUGH = 361,
     T_SET = 362,
     T_GET = 363,
     T_LOGS = 364,
     T_NOT = 365,
     T_CONTAINS = 366,
     T_MATCHES = 367,
     T_INTEGER = 368,
     T_IDENT = 369,
     T_STRING = 370,
     T_BLOCK = 371,
     T_SHELL_ARGS = 372
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
#define T_REGION 300
#define T_NODEINI 301
#define T_FS_INIT 302
#define T_FS_SINGLE 303
#define T_FS_PRIMARY 304
#define T_FS_WAIT_PRIMARY 305
#define T_FS_WAIT_STANDBY 306
#define T_FS_DEMOTED 307
#define T_FS_DEMOTE_TIMEOUT 308
#define T_FS_DRAINING 309
#define T_FS_SECONDARY 310
#define T_FS_CATCHINGUP 311
#define T_FS_PREP_PROMOTION 312
#define T_FS_STOP_REPLICATION 313
#define T_FS_MAINTENANCE 314
#define T_FS_JOIN_PRIMARY 315
#define T_FS_APPLY_SETTINGS 316
#define T_FS_PREPARE_MAINTENANCE 317
#define T_FS_WAIT_MAINTENANCE 318
#define T_FS_REPORT_LSN 319
#define T_FS_FAST_FORWARD 320
#define T_FS_JOIN_SECONDARY 321
#define T_FS_DROPPED 322
#define T_EXEC 323
#define T_EXEC_FAILS 324
#define T_RUN 325
#define T_PG_AUTOCTL 326
#define T_WAIT 327
#define T_UNTIL 328
#define T_TIMEOUT 329
#define T_AND 330
#define T_IS 331
#define T_WITH 332
#define T_ASSERT 333
#define T_SQL 334
#define T_EXPECT 335
#define T_ERROR 336
#define T_PROMOTE 337
#define T_PERFORM 338
#define T_FAILOVER 339
#define T_NETWORK 340
#define T_DISCONNECT 341
#define T_CONNECT 342
#define T_SLEEP 343
#define T_COMPOSE 344
#define T_DOWN 345
#define T_START 346
#define T_STOP 347
#define T_STOPPED 348
#define T_KILL 349
#define T_INJECT 350
#define T_STATE 351
#define T_ASSIGNED_STATE 352
#define T_IN 353
#define T_GROUP 354
#define T_LBRACE 355
#define T_RBRACE 356
#define T_COMMA 357
#define T_POSTGRES 358
#define T_STAYS 359
#define T_WHILE 360
#define T_THROUGH 361
#define T_SET 362
#define T_GET 363
#define T_LOGS 364
#define T_NOT 365
#define T_CONTAINS 366
#define T_MATCHES 367
#define T_INTEGER 368
#define T_IDENT 369
#define T_STRING 370
#define T_BLOCK 371
#define T_SHELL_ARGS 372




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
#line 481 "test_spec_parse.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 494 "test_spec_parse.c"

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
#define YYNTOKENS  118
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  64
/* YYNRULES -- Number of rules.  */
#define YYNRULES  210
/* YYNRULES -- Number of states.  */
#define YYNSTATES  347

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   372

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
     115,   116,   117
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
     216,   219,   222,   225,   228,   231,   235,   239,   242,   245,
     249,   253,   254,   257,   259,   261,   263,   265,   267,   269,
     271,   273,   275,   277,   279,   281,   283,   285,   287,   291,
     294,   298,   301,   305,   308,   312,   315,   317,   319,   321,
     326,   331,   333,   337,   338,   341,   343,   345,   349,   353,
     354,   364,   365,   375,   383,   391,   397,   403,   410,   412,
     414,   418,   422,   423,   426,   429,   434,   435,   438,   442,
     449,   456,   463,   470,   474,   477,   480,   484,   488,   491,
     493,   497,   500,   505,   511,   519,   523,   527,   533,   539,
     542,   545,   549,   553,   557,   562,   566,   570,   571,   577,
     583,   587,   592,   598,   603,   609,   612,   613,   616,   618,
     620,   622,   624,   626,   628,   630,   632,   634,   636,   638,
     640,   642,   644,   646,   648,   650,   652,   654,   656,   658,
     660
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     119,     0,    -1,   120,    -1,   119,   120,    -1,   121,    -1,
     143,    -1,   144,    -1,   145,    -1,   178,    -1,    -1,     3,
     100,   122,   123,   101,    -1,    -1,   123,   124,    -1,   125,
      -1,   126,    -1,   128,    -1,   129,    -1,   127,    -1,   130,
      -1,    43,    -1,    44,    -1,     4,    -1,     4,    39,   114,
      -1,     4,    14,   114,    -1,     4,    35,   113,    -1,     4,
      36,   115,    -1,     4,   114,    24,    26,    -1,     4,   114,
      30,    93,    -1,     4,   114,    24,    26,    36,   115,    -1,
      13,   115,    -1,    13,   114,    -1,    42,   114,    -1,    42,
     115,    -1,    15,   114,    -1,    16,   114,    -1,    17,   114,
      -1,    -1,    18,   131,   132,   100,   135,   101,    -1,    -1,
     132,   134,    -1,   114,    -1,   115,    -1,    16,    -1,     4,
      -1,     5,    -1,   133,    -1,    19,   113,    -1,    55,    28,
      -1,    -1,   135,   138,    -1,   114,    -1,     4,    -1,    -1,
      -1,   136,   137,   139,   141,    -1,    -1,     5,   114,   137,
     140,   100,   141,   101,    -1,    -1,   141,   142,    -1,    20,
      -1,    21,    -1,    22,    -1,    23,    -1,    26,    -1,    24,
      26,    -1,    25,    26,    -1,    25,    75,    24,    26,    -1,
      24,    27,    -1,    27,    -1,    32,    -1,    33,    -1,    34,
     113,    -1,    45,   114,    -1,    45,   115,    -1,    99,   113,
      -1,    35,   113,    -1,    38,   114,    -1,    39,   114,    -1,
      15,   114,    -1,    16,   114,    -1,    17,   114,    -1,    40,
      29,    -1,    40,    28,    -1,    41,   115,    -1,    37,   115,
      -1,    31,   114,   114,    -1,    31,   114,   115,    -1,     8,
     146,    -1,     9,   146,    -1,    10,   181,   146,    -1,   100,
     147,   101,    -1,    -1,   147,   148,    -1,   149,    -1,   155,
      -1,   162,    -1,   163,    -1,   164,    -1,   165,    -1,   167,
      -1,   168,    -1,   170,    -1,   171,    -1,   172,    -1,   175,
      -1,   176,    -1,   177,    -1,   169,    -1,    68,   114,   117,
      -1,    68,   114,    -1,    69,   114,   117,    -1,    69,   114,
      -1,    70,   114,   117,    -1,    70,   114,    -1,    71,   114,
     117,    -1,    71,   114,    -1,    71,    -1,    12,    -1,    76,
      -1,   114,    96,   150,   180,    -1,   114,    96,   150,   114,
      -1,   151,    -1,   152,    75,   151,    -1,    -1,   106,   154,
      -1,   180,    -1,   114,    -1,   154,   102,   180,    -1,   154,
     102,   114,    -1,    -1,    72,    73,   114,    96,   150,   180,
     156,   153,   161,    -1,    -1,    72,    73,   114,    96,   150,
     114,   157,   153,   161,    -1,    72,    73,   114,    97,   150,
     180,   161,    -1,    72,    73,   114,    97,   150,   114,   161,
      -1,    72,    73,   114,    93,   161,    -1,    72,    73,   158,
     159,   161,    -1,    72,    73,   151,    75,   152,   161,    -1,
     180,    -1,   114,    -1,   158,   102,   180,    -1,   158,   102,
     114,    -1,    -1,    98,   160,    -1,    99,   113,    -1,   160,
     102,    99,   113,    -1,    -1,    74,   113,    -1,    77,    74,
     113,    -1,    78,   114,    96,   150,   180,   161,    -1,    78,
     114,    96,   150,   114,   161,    -1,    78,   114,    97,   150,
     180,   161,    -1,    78,   114,    97,   150,   114,   161,    -1,
      79,   114,   116,    -1,    80,   116,    -1,    80,    81,    -1,
      80,    81,   114,    -1,    80,    81,   113,    -1,    82,   166,
      -1,   114,    -1,   166,   102,   114,    -1,    83,    84,    -1,
      83,    84,    99,   113,    -1,    83,    84,    98,    18,   114,
      -1,    83,    84,    98,    18,   114,    99,   113,    -1,    85,
      86,   114,    -1,    85,    87,   114,    -1,    46,   107,   114,
     114,   114,    -1,    46,   108,   114,   114,   114,    -1,    88,
     113,    -1,    89,    90,    -1,    89,    91,   114,    -1,    89,
      92,   114,    -1,    89,    94,   114,    -1,    89,    95,   114,
     117,    -1,    92,   103,   136,    -1,    91,   103,   136,    -1,
      -1,   105,   174,   100,   147,   101,    -1,    78,   136,   104,
     180,   173,    -1,   107,   114,   114,    -1,   109,   114,   111,
     115,    -1,   109,   114,   110,   111,   115,    -1,   109,   114,
     112,   115,    -1,   109,   114,   110,   112,   115,    -1,    11,
     179,    -1,    -1,   179,   181,    -1,    47,    -1,    48,    -1,
      49,    -1,    50,    -1,    51,    -1,    52,    -1,    53,    -1,
      54,    -1,    55,    -1,    56,    -1,    57,    -1,    58,    -1,
      59,    -1,    60,    -1,    61,    -1,    62,    -1,    63,    -1,
      64,    -1,    65,    -1,    66,    -1,    67,    -1,   114,    -1,
     115,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   214,   214,   215,   219,   220,   221,   222,   223,   236,
     235,   245,   247,   251,   252,   253,   254,   255,   256,   257,
     258,   271,   275,   282,   289,   295,   302,   309,   316,   329,
     335,   345,   351,   361,   371,   377,   388,   387,   404,   406,
     415,   416,   417,   418,   419,   423,   428,   432,   438,   440,
     459,   460,   469,   486,   485,   493,   492,   500,   502,   506,
     511,   516,   520,   524,   530,   535,   539,   544,   548,   552,
     556,   560,   564,   569,   574,   578,   582,   588,   594,   599,
     604,   609,   613,   617,   623,   629,   643,   664,   671,   682,
     700,   715,   718,   726,   727,   728,   729,   730,   731,   732,
     733,   734,   735,   736,   737,   738,   739,   740,   754,   761,
     767,   774,   780,   787,   793,   801,   807,   834,   834,   845,
     860,   878,   879,   894,   896,   900,   908,   916,   923,   935,
     934,   946,   945,   956,   965,   974,   981,   995,  1010,  1016,
    1023,  1029,  1042,  1044,  1048,  1053,  1061,  1062,  1063,  1074,
    1082,  1090,  1098,  1116,  1131,  1138,  1142,  1148,  1161,  1169,
    1177,  1198,  1205,  1212,  1220,  1236,  1242,  1263,  1271,  1286,
    1300,  1304,  1310,  1316,  1342,  1376,  1382,  1399,  1399,  1404,
    1423,  1448,  1457,  1466,  1475,  1491,  1494,  1496,  1518,  1519,
    1520,  1521,  1522,  1523,  1524,  1525,  1526,  1527,  1528,  1529,
    1530,  1531,  1532,  1533,  1534,  1535,  1536,  1537,  1538,  1546,
    1547
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
  "T_EXTENSION_VERSION", "T_BIND_SOURCE", "T_LEGACY_STARTUP", "T_REGION",
  "T_NODEINI", "T_FS_INIT", "T_FS_SINGLE", "T_FS_PRIMARY",
  "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
  "T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
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
  "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH", "T_SET", "T_GET",
  "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_INTEGER", "T_IDENT",
  "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec", "spec_item",
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
  "perform_cmd", "network_cmd", "nodeini_cmd", "sleep_cmd", "compose_cmd",
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
     365,   366,   367,   368,   369,   370,   371,   372
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   118,   119,   119,   120,   120,   120,   120,   120,   122,
     121,   123,   123,   124,   124,   124,   124,   124,   124,   124,
     124,   125,   125,   125,   125,   125,   125,   125,   125,   126,
     126,   127,   127,   128,   129,   129,   131,   130,   132,   132,
     133,   133,   133,   133,   133,   134,   134,   134,   135,   135,
     136,   136,   137,   139,   138,   140,   138,   141,   141,   142,
     142,   142,   142,   142,   142,   142,   142,   142,   142,   142,
     142,   142,   142,   142,   142,   142,   142,   142,   142,   142,
     142,   142,   142,   142,   142,   142,   142,   143,   144,   145,
     146,   147,   147,   148,   148,   148,   148,   148,   148,   148,
     148,   148,   148,   148,   148,   148,   148,   148,   149,   149,
     149,   149,   149,   149,   149,   149,   149,   150,   150,   151,
     151,   152,   152,   153,   153,   154,   154,   154,   154,   156,
     155,   157,   155,   155,   155,   155,   155,   155,   158,   158,
     158,   158,   159,   159,   160,   160,   161,   161,   161,   162,
     162,   162,   162,   163,   164,   164,   164,   164,   165,   166,
     166,   167,   167,   167,   167,   168,   168,   169,   169,   170,
     171,   171,   171,   171,   171,   172,   172,   174,   173,   175,
     176,   177,   177,   177,   177,   178,   179,   179,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   181,
     181
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
       2,     2,     2,     2,     2,     3,     3,     2,     2,     3,
       3,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     2,
       3,     2,     3,     2,     3,     2,     1,     1,     1,     4,
       4,     1,     3,     0,     2,     1,     1,     3,     3,     0,
       9,     0,     9,     7,     7,     5,     5,     6,     1,     1,
       3,     3,     0,     2,     2,     4,     0,     2,     3,     6,
       6,     6,     6,     3,     2,     2,     3,     3,     2,     1,
       3,     2,     4,     5,     7,     3,     3,     5,     5,     2,
       2,     3,     3,     3,     4,     3,     3,     0,     5,     5,
       3,     4,     5,     4,     5,     2,     0,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,   186,     0,     2,     4,     5,
       6,     7,     8,     9,    91,    87,    88,   209,   210,     0,
     185,     1,     3,    11,     0,    89,   187,     0,     0,     0,
       0,     0,   116,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    90,     0,     0,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   107,   101,   102,   103,
     104,   105,   106,    21,     0,     0,     0,     0,    36,     0,
      19,    20,    10,    12,    13,    14,    17,    15,    16,    18,
       0,     0,   109,   111,   113,   115,     0,    51,    50,     0,
       0,   155,   154,   159,   158,   161,     0,     0,   169,   170,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    30,    29,    33,    34,    35,    38,    31,
      32,     0,     0,   108,   110,   112,   114,   188,   189,   190,
     191,   192,   193,   194,   195,   196,   197,   198,   199,   200,
     201,   202,   203,   204,   205,   206,   207,   208,   139,     0,
     142,   138,     0,     0,     0,   153,   157,   156,     0,     0,
       0,   165,   166,   171,   172,   173,     0,    50,   176,   175,
     180,     0,     0,     0,    23,    24,    25,    22,     0,     0,
       0,     0,     0,   146,     0,     0,     0,     0,     0,   146,
     117,   118,     0,     0,     0,   160,     0,   162,   174,     0,
       0,   181,   183,    26,    27,    43,    44,    42,     0,     0,
      48,    40,    41,    45,    39,   167,   168,     0,     0,   135,
       0,     0,     0,   121,   146,     0,   143,   141,   140,   136,
     146,   146,   146,   146,   177,   179,   163,   182,   184,     0,
      46,    47,     0,   147,     0,   131,   129,   146,   146,     0,
       0,   137,   144,     0,   150,   149,   152,   151,     0,     0,
      28,     0,    37,    52,    49,   148,   123,   123,   134,   133,
       0,   122,     0,    91,   164,    52,    53,     0,   146,   146,
     120,   119,   145,     0,    55,    57,   126,   124,   125,   132,
     130,   178,     0,    54,     0,    57,     0,     0,     0,    59,
      60,    61,    62,     0,     0,    63,    68,     0,    69,    70,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    58,
     128,   127,     0,    78,    79,    80,    64,    67,    65,     0,
       0,    71,    75,    84,    76,    77,    82,    81,    83,    72,
      73,    74,    56,     0,    85,    86,    66
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,    23,    27,    73,    74,    75,    76,
      77,    78,    79,   118,   180,   213,   214,   242,    89,   276,
     264,   285,   292,   293,   319,     9,    10,    11,    15,    24,
      47,    48,   192,   149,   224,   278,   287,    49,   267,   266,
     150,   189,   226,   219,    50,    51,    52,    53,    94,    54,
      55,    56,    57,    58,    59,   235,   258,    60,    61,    62,
      12,    20,   151,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -174
static const yytype_int16 yypact[] =
{
      80,   -81,   -72,   -72,   -52,  -174,    71,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,   -72,
     -52,  -174,  -174,  -174,   432,  -174,  -174,     7,   -15,   -71,
     -59,   -57,   -50,   -40,     3,   -27,   -69,   -16,    25,     8,
      11,   -22,    16,    19,  -174,    24,    32,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,    -4,   -18,    42,    53,    59,  -174,   -14,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
      67,    94,    93,   116,   117,   118,   136,  -174,    15,   133,
     122,    52,  -174,  -174,   138,     5,   125,   127,  -174,  -174,
     128,   129,   130,   131,     4,     4,   132,   -26,   134,   159,
     158,   135,     6,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,   160,   161,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,   -51,   172,
     -54,  -174,     2,     2,   542,  -174,  -174,  -174,   162,   259,
     188,  -174,  -174,  -174,  -174,  -174,   185,  -174,  -174,  -174,
    -174,     9,   163,   189,  -174,  -174,  -174,  -174,   277,   212,
      -1,   192,   193,   -21,     2,     2,   194,   210,   165,   -21,
    -174,  -174,   204,   233,   205,  -174,   197,  -174,  -174,   198,
     199,  -174,  -174,   276,  -174,  -174,  -174,  -174,   202,   288,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,   227,   243,  -174,
     272,   301,   245,  -174,   -48,   229,   241,  -174,  -174,  -174,
     -21,   -21,   -21,   -21,  -174,  -174,   246,  -174,  -174,   231,
    -174,  -174,     1,  -174,   256,   269,   295,   -21,   -21,     2,
     194,  -174,  -174,   273,  -174,  -174,  -174,  -174,   271,   260,
    -174,   261,  -174,  -174,  -174,  -174,   268,   268,  -174,  -174,
     340,  -174,   263,  -174,  -174,  -174,  -174,   369,   -21,   -21,
    -174,  -174,  -174,   479,  -174,  -174,  -174,   275,  -174,  -174,
    -174,  -174,   278,   137,   408,  -174,   265,   266,   267,  -174,
    -174,  -174,  -174,   102,   -10,  -174,  -174,   270,  -174,  -174,
     296,   297,   293,   298,   299,   151,   322,    90,   325,  -174,
    -174,  -174,   110,  -174,  -174,  -174,  -174,  -174,  -174,   358,
      92,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,   357,  -174,  -174,  -174
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -174,  -174,   379,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -103,   139,
    -174,  -174,  -174,   144,  -174,  -174,  -174,  -174,    18,   167,
    -174,  -174,  -144,  -173,  -174,   174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,  -172,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,  -174,
    -174,  -174,  -154,   391
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -121
static const yytype_int16 yytable[] =
{
     194,   168,   169,   205,   206,    87,   261,    87,    87,   193,
     108,    63,    91,   223,   190,   207,   328,   229,   208,    13,
      64,    16,    65,    66,    67,    68,   217,   250,    14,   218,
     178,   109,   110,    86,   228,   111,   179,    25,   231,   233,
     220,   221,   183,    82,   187,   184,   185,    92,   188,    69,
      70,    71,   251,   217,   209,    83,   218,    84,   254,   255,
     256,   257,    17,    18,    85,   329,   246,   248,    99,   100,
     101,    21,   102,   103,     1,   268,   269,   271,   191,     2,
       3,     4,     5,     1,   171,   172,   173,    90,     2,     3,
       4,     5,    80,    81,    96,    97,   113,   114,    93,   210,
     119,   120,   262,   159,   160,   270,   289,   290,    72,    95,
     112,   152,   153,   211,   212,   167,   281,    88,   167,   104,
     199,   200,   105,   288,    98,   296,   297,   298,   326,   327,
     299,   300,   301,   302,   303,   304,   305,   306,   106,   263,
     321,   307,   308,   309,   310,   311,   107,   312,   313,   314,
     315,   316,   296,   297,   298,   317,   115,   299,   300,   301,
     302,   303,   304,   305,   306,   156,   157,   116,   307,   308,
     309,   310,   311,   117,   312,   313,   314,   315,   316,   336,
     337,   121,   317,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   339,   340,   344,   345,   122,   318,
     123,   342,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   124,   125,   126,   318,   154,   155,   161,
     158,   162,   163,   164,   165,   166,   170,   186,   174,   177,
     148,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   175,   176,   181,   182,   195,   196,   201,   227,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   197,   198,   203,   202,   204,   215,   216,   222,   225,
     234,   236,   239,   237,   238,   240,   241,   244,   230,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     243,   249,   252,   253,  -120,   259,   260,   232,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   265,
    -119,   273,   272,   274,   277,   275,   282,   294,   295,   323,
     324,   325,   343,   346,   330,    22,   245,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   333,   331,
     332,    26,   334,   335,   284,   247,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   338,   341,   322,
     283,   279,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   280,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,     0,     0,    28,     0,
       0,     0,     0,   286,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      29,    30,    31,    32,    33,     0,     0,     0,     0,     0,
      34,    35,    36,     0,    37,    38,     0,    39,     0,     0,
      40,    41,   320,    42,    43,    28,     0,     0,     0,     0,
       0,     0,     0,    44,     0,     0,     0,     0,     0,    45,
       0,    46,     0,     0,     0,     0,     0,    29,    30,    31,
      32,    33,     0,     0,     0,     0,     0,    34,    35,    36,
       0,    37,    38,     0,    39,     0,     0,    40,    41,     0,
      42,    43,     0,     0,     0,     0,     0,     0,     0,     0,
     291,     0,     0,     0,     0,     0,    45,     0,    46,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147
};

static const yytype_int16 yycheck[] =
{
     154,   104,   105,     4,     5,     4,     5,     4,     4,   153,
      14,     4,    81,   186,    12,    16,    26,   189,    19,   100,
      13,     3,    15,    16,    17,    18,    74,    75,   100,    77,
      24,    35,    36,    73,   188,    39,    30,    19,   192,   193,
     184,   185,    93,   114,    98,    96,    97,   116,   102,    42,
      43,    44,   224,    74,    55,   114,    77,   114,   230,   231,
     232,   233,   114,   115,   114,    75,   220,   221,    90,    91,
      92,     0,    94,    95,     3,   247,   248,   250,    76,     8,
       9,    10,    11,     3,   110,   111,   112,   114,     8,     9,
      10,    11,   107,   108,    86,    87,   114,   115,   114,   100,
     114,   115,   101,    98,    99,   249,   278,   279,   101,    84,
     114,    96,    97,   114,   115,   114,   270,   114,   114,   103,
     111,   112,   103,   277,   113,    15,    16,    17,    26,    27,
      20,    21,    22,    23,    24,    25,    26,    27,   114,   242,
     294,    31,    32,    33,    34,    35,   114,    37,    38,    39,
      40,    41,    15,    16,    17,    45,   114,    20,    21,    22,
      23,    24,    25,    26,    27,   113,   114,   114,    31,    32,
      33,    34,    35,   114,    37,    38,    39,    40,    41,    28,
      29,   114,    45,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,   114,   115,   114,   115,   114,    99,
     117,   101,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,   117,   117,   117,    99,   104,   116,   114,
     102,   114,   114,   114,   114,   114,   114,    75,   114,   114,
     114,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,   113,   115,   114,   114,   114,    18,   115,   114,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,   113,   117,    26,   115,    93,   114,   114,   114,    99,
     105,   114,    36,   115,   115,   113,    28,    74,   114,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
     113,    96,   113,   102,    75,    99,   115,   114,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,   113,
      75,   100,    99,   113,   106,   114,   113,   102,   100,   114,
     114,   114,    24,    26,   114,     6,   114,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,   115,   113,
     113,    20,   114,   114,   275,   114,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,   115,   113,   295,
     273,   267,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   114,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    -1,    -1,    46,    -1,
      -1,    -1,    -1,   114,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      68,    69,    70,    71,    72,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    -1,    82,    83,    -1,    85,    -1,    -1,
      88,    89,   114,    91,    92,    46,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   101,    -1,    -1,    -1,    -1,    -1,   107,
      -1,   109,    -1,    -1,    -1,    -1,    -1,    68,    69,    70,
      71,    72,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,
      -1,    82,    83,    -1,    85,    -1,    -1,    88,    89,    -1,
      91,    92,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     101,    -1,    -1,    -1,    -1,    -1,   107,    -1,   109,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     8,     9,    10,    11,   119,   120,   121,   143,
     144,   145,   178,   100,   100,   146,   146,   114,   115,   181,
     179,     0,   120,   122,   147,   146,   181,   123,    46,    68,
      69,    70,    71,    72,    78,    79,    80,    82,    83,    85,
      88,    89,    91,    92,   101,   107,   109,   148,   149,   155,
     162,   163,   164,   165,   167,   168,   169,   170,   171,   172,
     175,   176,   177,     4,    13,    15,    16,    17,    18,    42,
      43,    44,   101,   124,   125,   126,   127,   128,   129,   130,
     107,   108,   114,   114,   114,   114,    73,     4,   114,   136,
     114,    81,   116,   114,   166,    84,    86,    87,   113,    90,
      91,    92,    94,    95,   103,   103,   114,   114,    14,    35,
      36,    39,   114,   114,   115,   114,   114,   114,   131,   114,
     115,   114,   114,   117,   117,   117,   117,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,   114,   151,
     158,   180,    96,    97,   104,   116,   113,   114,   102,    98,
      99,   114,   114,   114,   114,   114,   114,   114,   136,   136,
     114,   110,   111,   112,   114,   113,   115,   114,    24,    30,
     132,   114,   114,    93,    96,    97,    75,    98,   102,   159,
      12,    76,   150,   150,   180,   114,    18,   113,   117,   111,
     112,   115,   115,    26,    93,     4,     5,    16,    19,    55,
     100,   114,   115,   133,   134,   114,   114,    74,    77,   161,
     150,   150,   114,   151,   152,    99,   160,   114,   180,   161,
     114,   180,   114,   180,   105,   173,   114,   115,   115,    36,
     113,    28,   135,   113,    74,   114,   180,   114,   180,    96,
      75,   161,   113,   102,   161,   161,   161,   161,   174,    99,
     115,     5,   101,   136,   138,   113,   157,   156,   161,   161,
     150,   151,    99,   100,   113,   114,   137,   106,   153,   153,
     114,   180,   113,   147,   137,   139,   114,   154,   180,   161,
     161,   101,   140,   141,   102,   100,    15,    16,    17,    20,
      21,    22,    23,    24,    25,    26,    27,    31,    32,    33,
      34,    35,    37,    38,    39,    40,    41,    45,    99,   142,
     114,   180,   141,   114,   114,   114,    26,    27,    26,    75,
     114,   113,   113,   115,   114,   114,    28,    29,   115,   114,
     115,   113,   101,    24,   114,   115,    26
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
#line 236 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl,  "self-signed",
		        sizeof(current_spec->cluster.ssl));
		strlcpy(current_spec->cluster.auth, "trust",
		        sizeof(current_spec->cluster.auth));
	;}
    break;

  case 19:
#line 257 "test_spec_parse.y"
    { current_spec->cluster.bindSource = true; ;}
    break;

  case 20:
#line 258 "test_spec_parse.y"
    { current_spec->cluster.legacyStartup = true; ;}
    break;

  case 21:
#line 272 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
	;}
    break;

  case 22:
#line 276 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorDebianCluster));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 23:
#line 283 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorImageTarget));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 24:
#line 290 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		/* monitor port not stored in TestCluster yet; ignore */
		(void) (yyvsp[(3) - (3)].ival);
	;}
    break;

  case 25:
#line 296 "test_spec_parse.y"
    {
		current_spec->cluster.withMonitor = true;
		strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
		        sizeof(current_spec->cluster.monitorPassword));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 26:
#line 303 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 27:
#line 310 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
		        sizeof(current_spec->cluster.secondMonitorName));
		current_spec->cluster.secondMonitorStopped = true;
		free((yyvsp[(2) - (4)].str));
	;}
    break;

  case 28:
#line 317 "test_spec_parse.y"
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
#line 330 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 30:
#line 336 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.image));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 31:
#line 346 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 32:
#line 352 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.extensionVersion));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 33:
#line 362 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 34:
#line 372 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 35:
#line 378 "test_spec_parse.y"
    {
		strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
		        sizeof(current_spec->cluster.auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 36:
#line 388 "test_spec_parse.y"
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
#line 415 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 41:
#line 416 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 42:
#line 417 "test_spec_parse.y"
    { (yyval.str) = strdup("auth"); ;}
    break;

  case 43:
#line 418 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 44:
#line 419 "test_spec_parse.y"
    { (yyval.str) = strdup("node"); ;}
    break;

  case 45:
#line 424 "test_spec_parse.y"
    {
		strlcpy(current_formation->name, (yyvsp[(1) - (1)].str), sizeof(current_formation->name));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 46:
#line 429 "test_spec_parse.y"
    {
		current_formation->numSync = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 47:
#line 433 "test_spec_parse.y"
    {
		current_formation->disableSecondary = true;
	;}
    break;

  case 50:
#line 459 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 51:
#line 460 "test_spec_parse.y"
    { (yyval.str) = strdup("monitor"); ;}
    break;

  case 52:
#line 469 "test_spec_parse.y"
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
#line 486 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(1) - (2)].str), sizeof(current_node->name));
		free((yyvsp[(1) - (2)].str));
	;}
    break;

  case 55:
#line 493 "test_spec_parse.y"
    {
		strlcpy(current_node->name, (yyvsp[(2) - (3)].str), sizeof(current_node->name));
		free((yyvsp[(2) - (3)].str));
	;}
    break;

  case 59:
#line 507 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_COORDINATOR;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 60:
#line 512 "test_spec_parse.y"
    {
		current_node->kind = NODE_KIND_CITUS_WORKER;
		current_spec->cluster.withCitus = true;
	;}
    break;

  case 61:
#line 517 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 62:
#line 521 "test_spec_parse.y"
    {
		current_node->noMonitor = true;
	;}
    break;

  case 63:
#line 525 "test_spec_parse.y"
    {
		/* bare "deferred" = create and launch deferred (both gates) */
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 64:
#line 531 "test_spec_parse.y"
    {
		/* "launch deferred" alone = run-deferred only, create immediate */
		current_node->launchDeferred = true;
	;}
    break;

  case 65:
#line 536 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
	;}
    break;

  case 66:
#line 540 "test_spec_parse.y"
    {
		current_node->createDeferred = true;
		current_node->launchDeferred = true;
	;}
    break;

  case 67:
#line 545 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 68:
#line 549 "test_spec_parse.y"
    {
		current_node->launchDeferred = false;
	;}
    break;

  case 69:
#line 553 "test_spec_parse.y"
    {
		current_node->listen = true;
	;}
    break;

  case 70:
#line 557 "test_spec_parse.y"
    {
		current_node->citusSecondary = true;
	;}
    break;

  case 71:
#line 561 "test_spec_parse.y"
    {
		current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 72:
#line 565 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 73:
#line 570 "test_spec_parse.y"
    {
		strlcpy(current_node->region, (yyvsp[(2) - (2)].str), sizeof(current_node->region));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 74:
#line 575 "test_spec_parse.y"
    {
		current_node->group = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 75:
#line 579 "test_spec_parse.y"
    {
		current_node->pgPort = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 76:
#line 583 "test_spec_parse.y"
    {
		strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->citusClusterName));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 77:
#line 589 "test_spec_parse.y"
    {
		strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->debianCluster));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 78:
#line 595 "test_spec_parse.y"
    {
		strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str), sizeof(current_node->ssl));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 79:
#line 600 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 80:
#line 605 "test_spec_parse.y"
    {
		strlcpy(current_node->auth, (yyvsp[(2) - (2)].str), sizeof(current_node->auth));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 81:
#line 610 "test_spec_parse.y"
    {
		current_node->replicationQuorum = true;
	;}
    break;

  case 82:
#line 614 "test_spec_parse.y"
    {
		current_node->replicationQuorum = false;
	;}
    break;

  case 83:
#line 618 "test_spec_parse.y"
    {
		strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->replicationPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 84:
#line 624 "test_spec_parse.y"
    {
		strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
		        sizeof(current_node->monitorPassword));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 85:
#line 630 "test_spec_parse.y"
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

  case 86:
#line 644 "test_spec_parse.y"
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

  case 87:
#line 665 "test_spec_parse.y"
    {
		current_spec->setup = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 88:
#line 672 "test_spec_parse.y"
    {
		current_spec->teardown = (yyvsp[(2) - (2)].step);
	;}
    break;

  case 89:
#line 683 "test_spec_parse.y"
    {
		TestStep *s = (yyvsp[(3) - (3)].step);
		strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
		free((yyvsp[(2) - (3)].str));
		register_step(current_spec, s);
	;}
    break;

  case 90:
#line 701 "test_spec_parse.y"
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

  case 91:
#line 715 "test_spec_parse.y"
    {
		(yyval.step) = make_step("");
	;}
    break;

  case 92:
#line 719 "test_spec_parse.y"
    {
		if ((yyvsp[(2) - (2)].cmd)) append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
		(yyval.step) = (yyvsp[(1) - (2)].step);
	;}
    break;

  case 93:
#line 726 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 94:
#line 727 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 95:
#line 728 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 96:
#line 729 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 97:
#line 730 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 98:
#line 731 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 99:
#line 732 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 100:
#line 733 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 101:
#line 734 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 102:
#line 735 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 103:
#line 736 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 104:
#line 737 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 105:
#line 738 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 106:
#line 739 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 107:
#line 740 "test_spec_parse.y"
    { (yyval.cmd) = (yyvsp[(1) - (1)].cmd); ;}
    break;

  case 108:
#line 755 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 109:
#line 762 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 110:
#line 768 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 111:
#line 775 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 112:
#line 781 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 113:
#line 788 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_RUN);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 114:
#line 794 "test_spec_parse.y"
    {
		/* "pg_autoctl perform failover --formation auth"
		 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s", (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 115:
#line 802 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
		strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 116:
#line 808 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
	;}
    break;

  case 119:
#line 846 "test_spec_parse.y"
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

  case 120:
#line 861 "test_spec_parse.y"
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

  case 125:
#line 901 "test_spec_parse.y"
    {
		/* current_pass_cmd set by the enclosing wait_cmd rule */
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 126:
#line 909 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(1) - (1)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 127:
#line 917 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
	;}
    break;

  case 128:
#line 924 "test_spec_parse.y"
    {
		if (current_pass_cmd &&
		    current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_pass_cmd->passThroughStates[current_pass_cmd->passThroughCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_pass_cmd->passThroughStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 129:
#line 935 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); ;}
    break;

  case 130:
#line 940 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 131:
#line 946 "test_spec_parse.y"
    { current_pass_cmd = make_cmd(CMD_WAIT_STATE);
	      strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str), sizeof(current_pass_cmd->service));
	      strlcpy(current_pass_cmd->state,   (yyvsp[(6) - (6)].str), sizeof(current_pass_cmd->state));
	      free((yyvsp[(3) - (6)].str)); free((yyvsp[(6) - (6)].str)); ;}
    break;

  case 132:
#line 951 "test_spec_parse.y"
    {
		current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
		(yyval.cmd) = current_pass_cmd;
		current_pass_cmd = NULL;
	;}
    break;

  case 133:
#line 957 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str));
	;}
    break;

  case 134:
#line 966 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
		(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(6) - (7)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
		free((yyvsp[(3) - (7)].str)); free((yyvsp[(6) - (7)].str));
	;}
    break;

  case 135:
#line 975 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		free((yyvsp[(3) - (5)].str));
	;}
    break;

  case 136:
#line 982 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 137:
#line 996 "test_spec_parse.y"
    {
		(yyval.cmd) = current_wait_cmd;
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		current_wait_cmd = NULL;
	;}
    break;

  case 138:
#line 1011 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 139:
#line 1017 "test_spec_parse.y"
    {
		current_wait_cmd = make_cmd(CMD_WAIT_STATES);
		strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 140:
#line 1024 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
	;}
    break;

  case 141:
#line 1030 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_wait_cmd->waitStates[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 144:
#line 1049 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 145:
#line 1054 "test_spec_parse.y"
    {
		if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] = (yyvsp[(4) - (4)].ival);
	;}
    break;

  case 146:
#line 1061 "test_spec_parse.y"
    { (yyval.ival) = PGAF_TIMEOUT_DEFAULT; ;}
    break;

  case 147:
#line 1062 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(2) - (2)].ival); ;}
    break;

  case 148:
#line 1063 "test_spec_parse.y"
    { (yyval.ival) = (yyvsp[(3) - (3)].ival); ;}
    break;

  case 149:
#line 1075 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 150:
#line 1083 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE : CMD_ASSERT_STATE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 151:
#line 1091 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str));
	;}
    break;

  case 152:
#line 1099 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(5) - (6)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
		free((yyvsp[(2) - (6)].str)); free((yyvsp[(5) - (6)].str));
	;}
    break;

  case 153:
#line 1117 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SQL);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args,    (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(2) - (3)].str)); free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 154:
#line 1132 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT);
		strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str), sizeof((yyval.cmd)->expected));
		expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
		free((yyvsp[(2) - (2)].str));
	;}
    break;

  case 155:
#line 1139 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
	;}
    break;

  case 156:
#line 1143 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->state));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 157:
#line 1149 "test_spec_parse.y"
    {
		/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
		(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
		snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d", (yyvsp[(3) - (3)].ival));
	;}
    break;

  case 158:
#line 1162 "test_spec_parse.y"
    {
		(yyval.cmd) = current_promote_cmd;
		current_promote_cmd = NULL;
	;}
    break;

  case 159:
#line 1170 "test_spec_parse.y"
    {
		current_promote_cmd = make_cmd(CMD_PROMOTE);
		current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
		strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
		        (yyvsp[(1) - (1)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(1) - (1)].str));
	;}
    break;

  case 160:
#line 1178 "test_spec_parse.y"
    {
		if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
			strlcpy(current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
			        (yyvsp[(3) - (3)].str), sizeof(current_promote_cmd->promoteNodes[0]));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 161:
#line 1199 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 162:
#line 1206 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, "default", sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(4) - (4)].ival);
		(yyval.cmd)->waitGroupCount = 1;
	;}
    break;

  case 163:
#line 1213 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = 0;
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 164:
#line 1221 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_FAILOVER);
		strlcpy((yyval.cmd)->service, (yyvsp[(5) - (7)].str), sizeof((yyval.cmd)->service));
		(yyval.cmd)->waitGroups[0] = (yyvsp[(7) - (7)].ival);
		(yyval.cmd)->waitGroupCount = 1;
		free((yyvsp[(5) - (7)].str));
	;}
    break;

  case 165:
#line 1237 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 166:
#line 1243 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 167:
#line 1264 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_SET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 168:
#line 1272 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_NODEINI_GET);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		free((yyvsp[(3) - (5)].str)); free((yyvsp[(4) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 169:
#line 1287 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_SLEEP);
		(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
	;}
    break;

  case 170:
#line 1301 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
	;}
    break;

  case 171:
#line 1305 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 172:
#line 1311 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 173:
#line 1317 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 174:
#line 1343 "test_spec_parse.y"
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

  case 175:
#line 1377 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 176:
#line 1383 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
		strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str), sizeof((yyval.cmd)->service));
		free((yyvsp[(3) - (3)].str));
	;}
    break;

  case 177:
#line 1399 "test_spec_parse.y"
    { pgaf_next_brace_is_while = 1; ;}
    break;

  case 178:
#line 1400 "test_spec_parse.y"
    { (yyval.step) = (yyvsp[(4) - (5)].step); ;}
    break;

  case 179:
#line 1405 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->state,   (yyvsp[(4) - (5)].str), sizeof((yyval.cmd)->state));
		(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) - (5)].step)->commands : NULL;
		free((yyvsp[(2) - (5)].str));
	;}
    break;

  case 180:
#line 1424 "test_spec_parse.y"
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

  case 181:
#line 1449 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = false;  /* false = fixed string, true = PCRE */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 182:
#line 1458 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = false;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 183:
#line 1467 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = false;
		(yyval.cmd)->allowError = true;   /* true = PCRE (-P) */
		free((yyvsp[(2) - (4)].str)); free((yyvsp[(4) - (4)].str));
	;}
    break;

  case 184:
#line 1476 "test_spec_parse.y"
    {
		(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
		strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str), sizeof((yyval.cmd)->service));
		strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str), sizeof((yyval.cmd)->args));
		(yyval.cmd)->logsNegate = true;
		(yyval.cmd)->allowError = true;
		free((yyvsp[(2) - (5)].str)); free((yyvsp[(5) - (5)].str));
	;}
    break;

  case 187:
#line 1497 "test_spec_parse.y"
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

  case 188:
#line 1518 "test_spec_parse.y"
    { (yyval.str) = "init"; ;}
    break;

  case 189:
#line 1519 "test_spec_parse.y"
    { (yyval.str) = "single"; ;}
    break;

  case 190:
#line 1520 "test_spec_parse.y"
    { (yyval.str) = "primary"; ;}
    break;

  case 191:
#line 1521 "test_spec_parse.y"
    { (yyval.str) = "wait_primary"; ;}
    break;

  case 192:
#line 1522 "test_spec_parse.y"
    { (yyval.str) = "wait_standby"; ;}
    break;

  case 193:
#line 1523 "test_spec_parse.y"
    { (yyval.str) = "demoted"; ;}
    break;

  case 194:
#line 1524 "test_spec_parse.y"
    { (yyval.str) = "demote_timeout"; ;}
    break;

  case 195:
#line 1525 "test_spec_parse.y"
    { (yyval.str) = "draining"; ;}
    break;

  case 196:
#line 1526 "test_spec_parse.y"
    { (yyval.str) = "secondary"; ;}
    break;

  case 197:
#line 1527 "test_spec_parse.y"
    { (yyval.str) = "catchingup"; ;}
    break;

  case 198:
#line 1528 "test_spec_parse.y"
    { (yyval.str) = "prepare_promotion"; ;}
    break;

  case 199:
#line 1529 "test_spec_parse.y"
    { (yyval.str) = "stop_replication"; ;}
    break;

  case 200:
#line 1530 "test_spec_parse.y"
    { (yyval.str) = "maintenance"; ;}
    break;

  case 201:
#line 1531 "test_spec_parse.y"
    { (yyval.str) = "join_primary"; ;}
    break;

  case 202:
#line 1532 "test_spec_parse.y"
    { (yyval.str) = "apply_settings"; ;}
    break;

  case 203:
#line 1533 "test_spec_parse.y"
    { (yyval.str) = "prepare_maintenance"; ;}
    break;

  case 204:
#line 1534 "test_spec_parse.y"
    { (yyval.str) = "wait_maintenance"; ;}
    break;

  case 205:
#line 1535 "test_spec_parse.y"
    { (yyval.str) = "report_lsn"; ;}
    break;

  case 206:
#line 1536 "test_spec_parse.y"
    { (yyval.str) = "fast_forward"; ;}
    break;

  case 207:
#line 1537 "test_spec_parse.y"
    { (yyval.str) = "join_secondary"; ;}
    break;

  case 208:
#line 1538 "test_spec_parse.y"
    { (yyval.str) = "dropped"; ;}
    break;

  case 209:
#line 1546 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 210:
#line 1547 "test_spec_parse.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3567 "test_spec_parse.c"
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


#line 1550 "test_spec_parse.y"


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

