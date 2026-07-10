/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C
 *
 * Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
 * Free Software Foundation, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
 * part or all of the Bison parser skeleton and distribute that work
 * under terms of your choice, so long as that work isn't itself a
 * parser generator using the skeleton or a modified version thereof
 * as a parser skeleton.  Alternatively, if you modify or redistribute
 * the parser skeleton itself, you may (at your option) remove this
 * special exception, which will cause the skeleton and the resulting
 * Bison output files to be licensed under the GNU General Public
 * License without this special exception.
 *
 * This special exception was added by the Free Software Foundation in
 * version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
 * simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
 * infringing on user name space.  This should be done even for local
 * variables, as they might otherwise be expanded by user macros.
 * There are some unavoidable exceptions within include files to
 * define necessary library symbols; they are noted "INFRINGES ON
 * USER NAME SPACE" below.  */

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
 * know about them.  */
enum yytokentype
{
	T_CLUSTER = 258,
	T_SCENARIO = 259,
	T_MONITOR = 260,
	T_NODE = 261,
	T_CITUS_COORDINATOR = 262,
	T_CITUS_WORKER = 263,
	T_SETUP = 264,
	T_TEARDOWN = 265,
	T_STEP = 266,
	T_SEQUENCE = 267,
	T_EQUALS = 268,
	T_IMAGE = 269,
	T_IMAGE_TARGET = 270,
	T_SSL = 271,
	T_AUTH = 272,
	T_AUTH_METHOD = 273,
	T_FORMATION = 274,
	T_NUM_SYNC = 275,
	T_COORDINATOR = 276,
	T_WORKER = 277,
	T_ASYNC = 278,
	T_NO_MONITOR = 279,
	T_LAUNCH = 280,
	T_DEFERRED = 281,
	T_IMMEDIATE = 282,
	T_INITIALLY = 283,
	T_VOLUME = 284,
	T_LISTEN = 285,
	T_CITUS_SECONDARY = 286,
	T_CANDIDATE_PRIORITY = 287,
	T_PORT = 288,
	T_PASSWORD = 289,
	T_MONITOR_PASSWORD = 290,
	T_CITUS_CLUSTER_NAME = 291,
	T_DEBIAN_CLUSTER = 292,
	T_REPLICATION_QUORUM = 293,
	T_REPLICATION_PASSWORD = 294,
	T_EXTENSION_VERSION = 295,
	T_BIND_SOURCE = 296,
	T_FS_INIT = 297,
	T_FS_SINGLE = 298,
	T_FS_PRIMARY = 299,
	T_FS_WAIT_PRIMARY = 300,
	T_FS_WAIT_STANDBY = 301,
	T_FS_DEMOTED = 302,
	T_FS_DEMOTE_TIMEOUT = 303,
	T_FS_DRAINING = 304,
	T_FS_SECONDARY = 305,
	T_FS_CATCHINGUP = 306,
	T_FS_PREP_PROMOTION = 307,
	T_FS_STOP_REPLICATION = 308,
	T_FS_MAINTENANCE = 309,
	T_FS_JOIN_PRIMARY = 310,
	T_FS_APPLY_SETTINGS = 311,
	T_FS_PREPARE_MAINTENANCE = 312,
	T_FS_WAIT_MAINTENANCE = 313,
	T_FS_REPORT_LSN = 314,
	T_FS_FAST_FORWARD = 315,
	T_FS_JOIN_SECONDARY = 316,
	T_FS_DROPPED = 317,
	T_EXEC = 318,
	T_EXEC_FAILS = 319,
	T_PG_AUTOCTL = 320,
	T_WAIT = 321,
	T_UNTIL = 322,
	T_TIMEOUT = 323,
	T_AND = 324,
	T_IS = 325,
	T_WITH = 326,
	T_ASSERT = 327,
	T_SQL = 328,
	T_EXPECT = 329,
	T_ERROR = 330,
	T_PROMOTE = 331,
	T_NETWORK = 332,
	T_DISCONNECT = 333,
	T_CONNECT = 334,
	T_SLEEP = 335,
	T_COMPOSE = 336,
	T_DOWN = 337,
	T_START = 338,
	T_STOP = 339,
	T_STOPPED = 340,
	T_KILL = 341,
	T_INJECT = 342,
	T_STATE = 343,
	T_ASSIGNED_STATE = 344,
	T_IN = 345,
	T_GROUP = 346,
	T_LBRACE = 347,
	T_RBRACE = 348,
	T_COMMA = 349,
	T_POSTGRES = 350,
	T_STAYS = 351,
	T_WHILE = 352,
	T_THROUGH = 353,
	T_SET = 354,
	T_LOGS = 355,
	T_NOT = 356,
	T_CONTAINS = 357,
	T_MATCHES = 358,
	T_NODE_ACTIVE = 359,
	T_MARK = 360,
	T_HEALTHY = 361,
	T_UNHEALTHY = 362,
	T_STATES = 363,
	T_INTEGER = 364,
	T_IDENT = 365,
	T_STRING = 366,
	T_BLOCK = 367,
	T_SHELL_ARGS = 368
};
#endif

/* Tokens.  */
#define T_CLUSTER 258
#define T_SCENARIO 259
#define T_MONITOR 260
#define T_NODE 261
#define T_CITUS_COORDINATOR 262
#define T_CITUS_WORKER 263
#define T_SETUP 264
#define T_TEARDOWN 265
#define T_STEP 266
#define T_SEQUENCE 267
#define T_EQUALS 268
#define T_IMAGE 269
#define T_IMAGE_TARGET 270
#define T_SSL 271
#define T_AUTH 272
#define T_AUTH_METHOD 273
#define T_FORMATION 274
#define T_NUM_SYNC 275
#define T_COORDINATOR 276
#define T_WORKER 277
#define T_ASYNC 278
#define T_NO_MONITOR 279
#define T_LAUNCH 280
#define T_DEFERRED 281
#define T_IMMEDIATE 282
#define T_INITIALLY 283
#define T_VOLUME 284
#define T_LISTEN 285
#define T_CITUS_SECONDARY 286
#define T_CANDIDATE_PRIORITY 287
#define T_PORT 288
#define T_PASSWORD 289
#define T_MONITOR_PASSWORD 290
#define T_CITUS_CLUSTER_NAME 291
#define T_DEBIAN_CLUSTER 292
#define T_REPLICATION_QUORUM 293
#define T_REPLICATION_PASSWORD 294
#define T_EXTENSION_VERSION 295
#define T_BIND_SOURCE 296
#define T_FS_INIT 297
#define T_FS_SINGLE 298
#define T_FS_PRIMARY 299
#define T_FS_WAIT_PRIMARY 300
#define T_FS_WAIT_STANDBY 301
#define T_FS_DEMOTED 302
#define T_FS_DEMOTE_TIMEOUT 303
#define T_FS_DRAINING 304
#define T_FS_SECONDARY 305
#define T_FS_CATCHINGUP 306
#define T_FS_PREP_PROMOTION 307
#define T_FS_STOP_REPLICATION 308
#define T_FS_MAINTENANCE 309
#define T_FS_JOIN_PRIMARY 310
#define T_FS_APPLY_SETTINGS 311
#define T_FS_PREPARE_MAINTENANCE 312
#define T_FS_WAIT_MAINTENANCE 313
#define T_FS_REPORT_LSN 314
#define T_FS_FAST_FORWARD 315
#define T_FS_JOIN_SECONDARY 316
#define T_FS_DROPPED 317
#define T_EXEC 318
#define T_EXEC_FAILS 319
#define T_PG_AUTOCTL 320
#define T_WAIT 321
#define T_UNTIL 322
#define T_TIMEOUT 323
#define T_AND 324
#define T_IS 325
#define T_WITH 326
#define T_ASSERT 327
#define T_SQL 328
#define T_EXPECT 329
#define T_ERROR 330
#define T_PROMOTE 331
#define T_NETWORK 332
#define T_DISCONNECT 333
#define T_CONNECT 334
#define T_SLEEP 335
#define T_COMPOSE 336
#define T_DOWN 337
#define T_START 338
#define T_STOP 339
#define T_STOPPED 340
#define T_KILL 341
#define T_INJECT 342
#define T_STATE 343
#define T_ASSIGNED_STATE 344
#define T_IN 345
#define T_GROUP 346
#define T_LBRACE 347
#define T_RBRACE 348
#define T_COMMA 349
#define T_POSTGRES 350
#define T_STAYS 351
#define T_WHILE 352
#define T_THROUGH 353
#define T_SET 354
#define T_LOGS 355
#define T_NOT 356
#define T_CONTAINS 357
#define T_MATCHES 358
#define T_NODE_ACTIVE 359
#define T_MARK 360
#define T_HEALTHY 361
#define T_UNHEALTHY 362
#define T_STATES 363
#define T_INTEGER 364
#define T_IDENT 365
#define T_STRING 366
#define T_BLOCK 367
#define T_SHELL_ARGS 368


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
extern int yylex(void);
extern int pgaf_line_number;
extern FILE *yyin;
extern int pgaf_next_brace_is_while;  /* set before T_LBRACE for while body */

/* the spec we're building */
static TestSpec *current_spec = NULL;

static void
yyerror(const char *msg)
{
	fprintf(stderr, "pgaftest: parse error at line %d: %s\n",
			pgaf_line_number, msg);
	exit(1);
}


/* helpers */
static void
append_cmd(TestStep *step, TestCmd *cmd)
{
	if (!step->commands)
	{
		step->commands = cmd;
	}
	else
	{
		TestCmd *c = step->commands;
		while (c->next)
		{
			c = c->next;
		}
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
	while (*p == ' ' || *p == '\t')
	{
		p++;
	}

	if (p[0] != '{' || (p[1] != ' ' && p[1] != '\t'))
	{
		return;
	}

	char tmp[4096] = { 0 };
	int pos = 0;
	bool first = true;

	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n')
		{
			p++;
		}
		if (*p == '\0')
		{
			break;
		}
		if (*p != '{')
		{
			break;
		}

		p++;
		while (*p == ' ' || *p == '\t')
		{
			p++;
		}

		char row[1024] = { 0 };
		int ri = 0;
		int depth = 1;
		while (*p && depth > 0)
		{
			if (*p == '{')
			{
				depth++;
			}
			else if (*p == '}')
			{
				if (--depth == 0)
				{
					break;
				}
			}
			if (depth > 0 && ri < (int) sizeof(row) - 1)
			{
				row[ri++] = *p;
			}
			p++;
		}
		if (*p == '}')
		{
			p++;
		}

		while (ri > 0 && (row[ri - 1] == ' ' || row[ri - 1] == '\t'))
		{
			ri--;
		}
		row[ri] = '\0';

		if (!first && pos < (int) sizeof(tmp) - 1)
		{
			tmp[pos++] = '\n';
		}
		int l = ri;
		if (pos + l >= (int) sizeof(tmp))
		{
			l = (int) sizeof(tmp) - pos - 1;
		}
		if (l > 0)
		{
			memcpy(tmp + pos, row, l);
			pos += l;
		}
		tmp[pos] = '\0';
		first = false;
	}

	if (!first)
	{
		strncpy(buf, tmp, buflen - 1);
	}
}


static void
register_step(TestSpec *spec, TestStep *step)
{
	if (!spec->steps)
	{
		spec->steps = step;
	}
	else
	{
		TestStep *s = spec->steps;
		while (s->next)
		{
			s = s->next;
		}
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

static TestCmd *current_wait_cmd = NULL;
static TestCmd *current_promote_cmd = NULL;
static TestCmd *current_pass_cmd = NULL;           /* for opt_passing_through */
static TestFormation *current_formation = NULL;
static TestNode *current_node = NULL;


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

#if !defined YYSTYPE && !defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 145 "test_spec_parse.y"
{
	int ival;
	char *str;
	TestStep *step;
	TestCmd *cmd;
}

/* Line 193 of yacc.c.  */
#line 473 "test_spec_parse.c"
YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 486 "test_spec_parse.c"

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
# elif !defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
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
#   define YY_(msgid) dgettext("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if !defined lint || defined __GNUC__
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
static int YYID(int i)
#else
static int YYID(i)
int i;
#endif
{
	return i;
}
#endif

#if !defined yyoverflow || YYERROR_VERBOSE

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
#    if !defined _ALLOCA_H && !defined _STDLIB_H && (defined __STDC__ || \
													 defined __C99__FUNC__ \
													 || defined __cplusplus || \
													 defined _MSC_VER)
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
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID(0))
#  ifndef YYSTACK_ALLOC_MAXIMUM

/* The OS might guarantee only one guard page at the bottom of the stack,
 * and a page size can be as small as 4096 bytes.  So we cannot safely
 * invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
 * to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && !defined _STDLIB_H \
	   && !((defined YYMALLOC || defined malloc) \
	&& (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if !defined malloc && !defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
												 || defined __cplusplus || \
												 defined _MSC_VER)
void * malloc(YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if !defined free && !defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
											   || defined __cplusplus || defined _MSC_VER)
void free(void *);  /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (!defined yyoverflow \
	 && (!defined __cplusplus \
		 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
	yytype_int16 yyss;
	YYSTYPE yyvs;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof(union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
 * N elements.  */
# define YYSTACK_BYTES(N) \
	((N) *(sizeof(yytype_int16) + sizeof(YYSTYPE)) \
	 + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
 * not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
	__builtin_memcpy(To, From, (Count) * sizeof(*(From)))
#  else
#   define YYCOPY(To, From, Count) \
	do \
	{ \
		YYSIZE_T yyi; \
		for (yyi = 0; yyi < (Count); yyi++) { \
			(To)[yyi] = (From)[yyi]; } \
	} \
	while (YYID(0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
 * local variables YYSIZE and YYSTACKSIZE give the old and new number of
 * elements in the stack, and YYPTR gives the new location of the
 * stack.  Advance YYPTR to a properly aligned location for the next
 * stack.  */
# define YYSTACK_RELOCATE(Stack) \
	do \
	{ \
		YYSIZE_T yynewbytes; \
		YYCOPY(&yyptr->Stack, Stack, yysize); \
		Stack = &yyptr->Stack; \
		yynewbytes = yystacksize * sizeof(*Stack) + YYSTACK_GAP_MAXIMUM; \
		yyptr += yynewbytes / sizeof(*yyptr); \
	} \
	while (YYID(0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL 24

/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST 587

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS 115

/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS 68

/* YYNRULES -- Number of rules.  */
#define YYNRULES 208

/* YYNRULES -- Number of states.  */
#define YYNSTATES 338

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK 2
#define YYMAXUTOK 368

#define YYTRANSLATE(YYX) \
	((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
	0, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 114, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 1, 2, 3, 4,
	5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
	35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
	65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
	75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
	85, 86, 87, 88, 89, 90, 91, 92, 93, 94,
	95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113
};

#if YYDEBUG

/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
 * YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
	0, 0, 3, 5, 8, 10, 12, 14, 16, 18,
	20, 21, 27, 28, 34, 35, 38, 40, 42, 44,
	46, 47, 50, 52, 54, 56, 58, 60, 62, 64,
	66, 70, 74, 78, 82, 87, 92, 99, 102, 105,
	108, 111, 114, 117, 120, 121, 128, 129, 132, 134,
	136, 138, 140, 142, 144, 147, 148, 151, 153, 155,
	156, 157, 162, 163, 171, 172, 175, 177, 179, 181,
	183, 185, 188, 191, 193, 195, 197, 200, 203, 206,
	209, 212, 215, 218, 221, 224, 227, 230, 234, 238,
	241, 244, 248, 252, 253, 256, 258, 260, 262, 264,
	266, 268, 270, 272, 274, 276, 278, 280, 282, 284,
	286, 290, 293, 297, 300, 304, 307, 309, 311, 313,
	318, 323, 325, 329, 330, 333, 335, 337, 341, 345,
	346, 356, 357, 367, 375, 383, 389, 395, 402, 404,
	406, 410, 414, 415, 418, 421, 426, 427, 430, 434,
	441, 448, 455, 462, 466, 470, 473, 476, 480, 484,
	487, 489, 493, 497, 501, 504, 507, 511, 515, 519,
	524, 528, 532, 533, 539, 545, 549, 554, 560, 565,
	571, 576, 581, 586, 589, 590, 593, 595, 597, 599,
	601, 603, 605, 607, 609, 611, 613, 615, 617, 619,
	621, 623, 625, 627, 629, 631, 633, 635, 637
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
	116, 0, -1, 117, -1, 116, 117, -1, 118, -1,
	120, -1, 144, -1, 145, -1, 146, -1, 179, -1,
	-1, 3, 92, 119, 124, 93, -1, -1, 4, 92,
	121, 122, 93, -1, -1, 122, 123, -1, 131, -1,
	127, -1, 129, -1, 130, -1, -1, 124, 125, -1,
	126, -1, 127, -1, 129, -1, 130, -1, 128, -1,
	131, -1, 41, -1, 5, -1, 5, 37, 110, -1,
	5, 15, 110, -1, 5, 33, 109, -1, 5, 34,
	111, -1, 5, 110, 25, 26, -1, 5, 110, 28,
	85, -1, 5, 110, 25, 26, 34, 111, -1, 14,
	111, -1, 14, 110, -1, 40, 110, -1, 40, 111,
	-1, 16, 110, -1, 17, 110, -1, 18, 110, -1,
	-1, 19, 132, 133, 92, 136, 93, -1, -1, 133,
	135, -1, 110, -1, 111, -1, 17, -1, 5, -1,
	6, -1, 134, -1, 20, 109, -1, -1, 136, 139,
	-1, 110, -1, 5, -1, -1, -1, 137, 138, 140,
	142, -1, -1, 6, 110, 138, 141, 92, 142, 93,
	-1, -1, 142, 143, -1, 21, -1, 22, -1, 23,
	-1, 24, -1, 26, -1, 25, 26, -1, 25, 27,
	-1, 27, -1, 30, -1, 31, -1, 32, 109, -1,
	91, 109, -1, 33, 109, -1, 36, 110, -1, 37,
	110, -1, 16, 110, -1, 17, 110, -1, 18, 110,
	-1, 38, 110, -1, 39, 111, -1, 35, 111, -1,
	29, 110, 110, -1, 29, 110, 111, -1, 9, 147,
	-1, 10, 147, -1, 11, 182, 147, -1, 92, 148,
	93, -1, -1, 148, 149, -1, 150, -1, 156, -1,
	163, -1, 164, -1, 165, -1, 166, -1, 168, -1,
	169, -1, 170, -1, 171, -1, 174, -1, 175, -1,
	176, -1, 177, -1, 178, -1, 63, 110, 113, -1,
	63, 110, -1, 64, 110, 113, -1, 64, 110, -1,
	65, 110, 113, -1, 65, 110, -1, 65, -1, 13,
	-1, 70, -1, 110, 88, 151, 181, -1, 110, 88,
	151, 110, -1, 152, -1, 153, 69, 152, -1, -1,
	98, 155, -1, 181, -1, 110, -1, 155, 94, 181,
	-1, 155, 94, 110, -1, -1, 66, 67, 110, 88,
	151, 181, 157, 154, 162, -1, -1, 66, 67, 110,
	88, 151, 110, 158, 154, 162, -1, 66, 67, 110,
	89, 151, 181, 162, -1, 66, 67, 110, 89, 151,
	110, 162, -1, 66, 67, 110, 85, 162, -1, 66,
	67, 159, 160, 162, -1, 66, 67, 152, 69, 153,
	162, -1, 181, -1, 110, -1, 159, 94, 181, -1,
	159, 94, 110, -1, -1, 90, 161, -1, 91, 109,
	-1, 161, 94, 91, 109, -1, -1, 68, 109, -1,
	71, 68, 109, -1, 72, 110, 88, 151, 181, 162,
	-1, 72, 110, 88, 151, 110, 162, -1, 72, 110,
	89, 151, 181, 162, -1, 72, 110, 89, 151, 110,
	162, -1, 72, 108, 112, -1, 73, 110, 112, -1,
	74, 112, -1, 74, 75, -1, 74, 75, 110, -1,
	74, 75, 109, -1, 76, 167, -1, 110, -1, 167,
	94, 110, -1, 77, 78, 110, -1, 77, 79, 110,
	-1, 80, 109, -1, 81, 82, -1, 81, 83, 110,
	-1, 81, 84, 110, -1, 81, 86, 110, -1, 81,
	87, 110, 113, -1, 84, 95, 137, -1, 83, 95,
	137, -1, -1, 97, 173, 92, 148, 93, -1, 72,
	137, 96, 181, 172, -1, 99, 110, 110, -1, 100,
	110, 102, 111, -1, 100, 110, 101, 102, 111, -1,
	100, 110, 103, 111, -1, 100, 110, 101, 103, 111,
	-1, 104, 112, 74, 112, -1, 105, 106, 114, 110,
	-1, 105, 107, 114, 110, -1, 12, 180, -1, -1,
	180, 182, -1, 42, -1, 43, -1, 44, -1, 45,
	-1, 46, -1, 47, -1, 48, -1, 49, -1, 50,
	-1, 51, -1, 52, -1, 53, -1, 54, -1, 55,
	-1, 56, -1, 57, -1, 58, -1, 59, -1, 60,
	-1, 61, -1, 62, -1, 110, -1, 111, -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
	0, 213, 213, 214, 218, 219, 220, 221, 222, 223,
	236, 235, 259, 258, 270, 272, 276, 277, 278, 279,
	282, 284, 288, 289, 290, 291, 292, 293, 294, 307,
	311, 318, 325, 331, 338, 345, 352, 365, 371, 381,
	387, 397, 407, 413, 424, 423, 440, 442, 451, 452,
	453, 454, 455, 459, 464, 470, 472, 491, 492, 501,
	518, 517, 525, 524, 532, 534, 538, 543, 548, 552,
	556, 560, 564, 568, 572, 576, 580, 584, 588, 592,
	598, 604, 609, 614, 619, 627, 633, 639, 653, 674,
	681, 692, 710, 725, 728, 736, 737, 738, 739, 740,
	741, 742, 743, 744, 745, 746, 747, 748, 749, 750,
	764, 771, 777, 784, 790, 798, 804, 831, 831, 842,
	857, 875, 876, 891, 893, 897, 905, 913, 920, 932,
	931, 943, 942, 953, 962, 971, 978, 992, 1007, 1013,
	1020, 1026, 1039, 1041, 1045, 1050, 1058, 1059, 1060, 1071,
	1079, 1087, 1095, 1103, 1155, 1170, 1177, 1181, 1187, 1200,
	1208, 1216, 1231, 1237, 1250, 1264, 1268, 1274, 1280, 1306,
	1340, 1346, 1363, 1363, 1368, 1387, 1412, 1421, 1430, 1439,
	1460, 1482, 1494, 1513, 1516, 1518, 1540, 1541, 1542, 1543,
	1544, 1545, 1546, 1547, 1548, 1549, 1550, 1551, 1552, 1553,
	1554, 1555, 1556, 1557, 1558, 1559, 1560, 1568, 1569
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
 * First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
	"$end", "error", "$undefined", "T_CLUSTER", "T_SCENARIO", "T_MONITOR",
	"T_NODE", "T_CITUS_COORDINATOR", "T_CITUS_WORKER", "T_SETUP",
	"T_TEARDOWN", "T_STEP", "T_SEQUENCE", "T_EQUALS", "T_IMAGE",
	"T_IMAGE_TARGET", "T_SSL", "T_AUTH", "T_AUTH_METHOD", "T_FORMATION",
	"T_NUM_SYNC", "T_COORDINATOR", "T_WORKER", "T_ASYNC", "T_NO_MONITOR",
	"T_LAUNCH", "T_DEFERRED", "T_IMMEDIATE", "T_INITIALLY", "T_VOLUME",
	"T_LISTEN", "T_CITUS_SECONDARY", "T_CANDIDATE_PRIORITY", "T_PORT",
	"T_PASSWORD", "T_MONITOR_PASSWORD", "T_CITUS_CLUSTER_NAME",
	"T_DEBIAN_CLUSTER", "T_REPLICATION_QUORUM", "T_REPLICATION_PASSWORD",
	"T_EXTENSION_VERSION", "T_BIND_SOURCE", "T_FS_INIT", "T_FS_SINGLE",
	"T_FS_PRIMARY", "T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
	"T_FS_DEMOTE_TIMEOUT", "T_FS_DRAINING", "T_FS_SECONDARY",
	"T_FS_CATCHINGUP", "T_FS_PREP_PROMOTION", "T_FS_STOP_REPLICATION",
	"T_FS_MAINTENANCE", "T_FS_JOIN_PRIMARY", "T_FS_APPLY_SETTINGS",
	"T_FS_PREPARE_MAINTENANCE", "T_FS_WAIT_MAINTENANCE", "T_FS_REPORT_LSN",
	"T_FS_FAST_FORWARD", "T_FS_JOIN_SECONDARY", "T_FS_DROPPED", "T_EXEC",
	"T_EXEC_FAILS", "T_PG_AUTOCTL", "T_WAIT", "T_UNTIL", "T_TIMEOUT",
	"T_AND", "T_IS", "T_WITH", "T_ASSERT", "T_SQL", "T_EXPECT", "T_ERROR",
	"T_PROMOTE", "T_NETWORK", "T_DISCONNECT", "T_CONNECT", "T_SLEEP",
	"T_COMPOSE", "T_DOWN", "T_START", "T_STOP", "T_STOPPED", "T_KILL",
	"T_INJECT", "T_STATE", "T_ASSIGNED_STATE", "T_IN", "T_GROUP", "T_LBRACE",
	"T_RBRACE", "T_COMMA", "T_POSTGRES", "T_STAYS", "T_WHILE", "T_THROUGH",
	"T_SET", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_NODE_ACTIVE",
	"T_MARK", "T_HEALTHY", "T_UNHEALTHY", "T_STATES", "T_INTEGER", "T_IDENT",
	"T_STRING", "T_BLOCK", "T_SHELL_ARGS", "':'", "$accept", "spec",
	"spec_item", "cluster_block", "@1", "scenario_block", "@2",
	"scenario_item_list", "scenario_item", "cluster_item_list",
	"cluster_item", "monitor_line", "image_line", "extension_version_line",
	"ssl_line", "auth_line", "formation_block", "@3", "formation_opt_list",
	"bare_name", "formation_opt", "node_list", "node_name", "init_node_slot",
	"node_line", "@4", "@5", "node_opt_list", "node_opt", "setup_block",
	"teardown_block", "named_step", "cmd_block", "cmd_list", "step_cmd",
	"exec_cmd", "state_op", "wait_multi_condition",
	"wait_multi_condition_list", "opt_passing_through", "pass_state_list",
	"wait_cmd", "@6", "@7", "state_name_list", "opt_in_group", "group_items",
	"opt_timeout", "assert_cmd", "sql_cmd", "expect_cmd", "promote_cmd",
	"promote_list", "network_cmd", "sleep_cmd", "compose_cmd",
	"postgres_ctl_cmd", "while_body", "@8", "stays_while_cmd",
	"set_monitor_cmd", "logs_cmd", "node_active_cmd", "mark_health_cmd",
	"sequence_block", "sequence_names", "fsm_state", "ident_or_string", 0
};
#endif

# ifdef YYPRINT

/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
 * token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
	0, 256, 257, 258, 259, 260, 261, 262, 263, 264,
	265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
	275, 276, 277, 278, 279, 280, 281, 282, 283, 284,
	285, 286, 287, 288, 289, 290, 291, 292, 293, 294,
	295, 296, 297, 298, 299, 300, 301, 302, 303, 304,
	305, 306, 307, 308, 309, 310, 311, 312, 313, 314,
	315, 316, 317, 318, 319, 320, 321, 322, 323, 324,
	325, 326, 327, 328, 329, 330, 331, 332, 333, 334,
	335, 336, 337, 338, 339, 340, 341, 342, 343, 344,
	345, 346, 347, 348, 349, 350, 351, 352, 353, 354,
	355, 356, 357, 358, 359, 360, 361, 362, 363, 364,
	365, 366, 367, 368, 58
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
	0, 115, 116, 116, 117, 117, 117, 117, 117, 117,
	119, 118, 121, 120, 122, 122, 123, 123, 123, 123,
	124, 124, 125, 125, 125, 125, 125, 125, 125, 126,
	126, 126, 126, 126, 126, 126, 126, 127, 127, 128,
	128, 129, 130, 130, 132, 131, 133, 133, 134, 134,
	134, 134, 134, 135, 135, 136, 136, 137, 137, 138,
	140, 139, 141, 139, 142, 142, 143, 143, 143, 143,
	143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
	143, 143, 143, 143, 143, 143, 143, 143, 143, 144,
	145, 146, 147, 148, 148, 149, 149, 149, 149, 149,
	149, 149, 149, 149, 149, 149, 149, 149, 149, 149,
	150, 150, 150, 150, 150, 150, 150, 151, 151, 152,
	152, 153, 153, 154, 154, 155, 155, 155, 155, 157,
	156, 158, 156, 156, 156, 156, 156, 156, 159, 159,
	159, 159, 160, 160, 161, 161, 162, 162, 162, 163,
	163, 163, 163, 163, 164, 165, 165, 165, 165, 166,
	167, 167, 168, 168, 169, 170, 170, 170, 170, 170,
	171, 171, 173, 172, 174, 175, 176, 176, 176, 176,
	177, 178, 178, 179, 180, 180, 181, 181, 181, 181,
	181, 181, 181, 181, 181, 181, 181, 181, 181, 181,
	181, 181, 181, 181, 181, 181, 181, 182, 182
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
	0, 2, 1, 2, 1, 1, 1, 1, 1, 1,
	0, 5, 0, 5, 0, 2, 1, 1, 1, 1,
	0, 2, 1, 1, 1, 1, 1, 1, 1, 1,
	3, 3, 3, 3, 4, 4, 6, 2, 2, 2,
	2, 2, 2, 2, 0, 6, 0, 2, 1, 1,
	1, 1, 1, 1, 2, 0, 2, 1, 1, 0,
	0, 4, 0, 7, 0, 2, 1, 1, 1, 1,
	1, 2, 2, 1, 1, 1, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 3, 3, 2,
	2, 3, 3, 0, 2, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	3, 2, 3, 2, 3, 2, 1, 1, 1, 4,
	4, 1, 3, 0, 2, 1, 1, 3, 3, 0,
	9, 0, 9, 7, 7, 5, 5, 6, 1, 1,
	3, 3, 0, 2, 2, 4, 0, 2, 3, 6,
	6, 6, 6, 3, 3, 2, 2, 3, 3, 2,
	1, 3, 3, 3, 2, 2, 3, 3, 3, 4,
	3, 3, 0, 5, 5, 3, 4, 5, 4, 5,
	4, 4, 4, 2, 0, 2, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
 * STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
 * means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
	0, 0, 0, 0, 0, 0, 184, 0, 2, 4,
	5, 6, 7, 8, 9, 10, 12, 93, 89, 90,
	207, 208, 0, 183, 1, 3, 20, 14, 0, 91,
	185, 0, 0, 0, 0, 116, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 92, 0, 0, 0,
	0, 94, 95, 96, 97, 98, 99, 100, 101, 102,
	103, 104, 105, 106, 107, 108, 109, 29, 0, 0,
	0, 0, 44, 0, 28, 11, 21, 22, 23, 26,
	24, 25, 27, 13, 15, 17, 18, 19, 16, 111,
	113, 115, 0, 58, 0, 57, 0, 0, 156, 155,
	160, 159, 0, 0, 164, 165, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 38, 37, 41, 42, 43, 46, 39, 40,
	110, 112, 114, 186, 187, 188, 189, 190, 191, 192,
	193, 194, 195, 196, 197, 198, 199, 200, 201, 202,
	203, 204, 205, 206, 139, 0, 142, 138, 153, 0,
	0, 0, 154, 158, 157, 0, 162, 163, 166, 167,
	168, 0, 57, 171, 170, 175, 0, 0, 0, 0,
	0, 0, 31, 32, 33, 30, 0, 0, 0, 146,
	0, 0, 0, 0, 0, 146, 117, 118, 0, 0,
	0, 161, 169, 0, 0, 176, 178, 180, 181, 182,
	34, 35, 51, 52, 50, 0, 55, 48, 49, 53,
	47, 0, 0, 135, 0, 0, 0, 121, 146, 0,
	143, 141, 140, 136, 146, 146, 146, 146, 172, 174,
	177, 179, 0, 54, 0, 147, 0, 131, 129, 146,
	146, 0, 0, 137, 144, 0, 150, 149, 152, 151,
	0, 36, 0, 45, 59, 56, 148, 123, 123, 134,
	133, 0, 122, 0, 93, 59, 60, 0, 146, 146,
	120, 119, 145, 0, 62, 64, 126, 124, 125, 132,
	130, 173, 0, 61, 0, 64, 0, 0, 0, 66,
	67, 68, 69, 0, 70, 73, 0, 74, 75, 0,
	0, 0, 0, 0, 0, 0, 0, 65, 128, 127,
	0, 81, 82, 83, 71, 72, 0, 76, 78, 86,
	79, 80, 84, 85, 77, 63, 87, 88
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
	-1, 7, 8, 9, 26, 10, 27, 32, 84, 31,
	76, 77, 78, 79, 80, 81, 82, 127, 188, 219,
	220, 244, 96, 276, 265, 285, 292, 293, 317, 11,
	12, 13, 18, 28, 51, 52, 198, 155, 228, 278,
	287, 53, 268, 267, 156, 195, 230, 223, 54, 55,
	56, 57, 101, 58, 59, 60, 61, 239, 260, 62,
	63, 64, 65, 66, 14, 23, 157, 22
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
 * STATE-NUM.  */
#define YYPACT_NINF -180
static const yytype_int16 yypact[] =
{
	69, -65, -41, -29, -29, -74, -180, 50, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -29, -74, -180, -180, -180, -180, 414, -180,
	-180, 5, 14, -28, -9, 20, 2, 3, 39, -64,
	57, -3, -23, 1, 28, 78, -180, 90, 91, 31,
	-1, -180, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -8, 6, 92,
	93, 94, -180, 9, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180, -180, 118,
	119, 120, 137, -180, 95, 33, -7, 122, 15, -180,
	-180, 10, 125, 126, -180, -180, 127, 128, 129, 130,
	4, 4, 131, -6, 41, 132, 155, 133, 96, 134,
	160, 13, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -21, 173, -77, -180, -180, 7,
	7, 525, -180, -180, -180, 161, -180, -180, -180, -180,
	-180, 159, -180, -180, -180, -180, 24, 162, 163, 164,
	165, 189, -180, -180, -180, -180, 218, 215, -2, -24,
	7, 7, 191, 211, 167, -24, -180, -180, 206, 236,
	207, -180, -180, 192, 194, -180, -180, -180, -180, -180,
	272, -180, -180, -180, -180, 198, -180, -180, -180, -180,
	-180, 199, 241, -180, 275, 305, 222, -180, 23, 202,
	219, -180, -180, -180, -24, -24, -24, -24, -180, -180,
	-180, -180, 201, -180, 0, -180, 205, 246, 269, -24,
	-24, 7, 191, -180, -180, 248, -180, -180, -180, -180,
	249, -180, 230, -180, -180, -180, -180, 244, 244, -180,
	-180, 344, -180, 234, -180, -180, -180, 374, -24, -24,
	-180, -180, -180, 461, -180, -180, -180, 250, -180, -180,
	-180, -180, 253, 139, 413, -180, 258, 259, 260, -180,
	-180, -180, -180, 102, -180, -180, 261, -180, -180, 263,
	264, 265, 267, 268, 270, 271, 266, -180, -180, -180,
	115, -180, -180, -180, -180, -180, 48, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
	-180, -180, 367, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, 347, -180, 349, 351, 352, -180, -180, -180,
	-180, -180, -110, 135, -180, -180, -180, 112, -180, -180,
	-180, -180, 30, 138, -180, -180, -148, -178, -180, 140,
	-180, -180, -180, -180, -180, -180, -180, -179, -180, -180,
	-180, -180, -180, -180, -180, -180, -180, -180, -180, -180,
	-180, -180, -180, -180, -180, -180, -159, 386
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
 * positive, shift that token.  If negative, reduce the rule which
 * number is the opposite.  If zero, do what YYDEFACT says.
 * If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -121
static const yytype_int16 yytable[] =
{
	173, 174, 200, 212, 213, 93, 262, 117, 93, 93,
	67, 98, 199, 193, 227, 214, 233, 194, 215, 68,
	196, 69, 70, 71, 72, 118, 119, 15, 68, 120,
	69, 70, 71, 72, 19, 232, 20, 21, 186, 235,
	237, 187, 224, 225, 221, 73, 74, 222, 99, 253,
	24, 16, 29, 1, 2, 256, 257, 258, 259, 3,
	4, 5, 6, 17, 189, 248, 250, 190, 191, 92,
	269, 270, 1, 2, 272, 102, 103, 197, 3, 4,
	5, 6, 89, 105, 106, 107, 104, 108, 109, 161,
	216, 221, 252, 263, 222, 176, 177, 178, 75, 289,
	290, 90, 121, 271, 165, 115, 116, 83, 217, 218,
	172, 94, 281, 95, 172, 179, 122, 123, 288, 128,
	129, 159, 160, 110, 163, 164, 203, 204, 324, 325,
	91, 296, 297, 298, 264, 319, 299, 300, 301, 302,
	303, 304, 305, 114, 306, 307, 308, 309, 310, 97,
	311, 312, 313, 314, 315, 296, 297, 298, 336, 337,
	299, 300, 301, 302, 303, 304, 305, 100, 306, 307,
	308, 309, 310, 111, 311, 312, 313, 314, 315, 133,
	134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
	144, 145, 146, 147, 148, 149, 150, 151, 152, 153,
	112, 113, 124, 125, 126, 183, 316, 158, 335, 133,
	134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
	144, 145, 146, 147, 148, 149, 150, 151, 152, 153,
	316, 130, 131, 132, 162, 166, 167, 168, 169, 170,
	171, 175, 192, 182, 210, 184, 180, 154, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
	145, 146, 147, 148, 149, 150, 151, 152, 153, 181,
	185, 201, 202, 205, 206, 208, 207, 231, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
	145, 146, 147, 148, 149, 150, 151, 152, 153, 209,
	211, 226, 229, 240, 238, 241, 242, 243, 245, 246,
	251, 254, 261, 255, 266, -120, 234, 133, 134, 135,
	136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
	146, 147, 148, 149, 150, 151, 152, 153, -119, 273,
	275, 274, 277, 282, 294, 295, 236, 133, 134, 135,
	136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
	146, 147, 148, 149, 150, 151, 152, 153, 321, 322,
	323, 326, 327, 328, 25, 334, 329, 330, 331, 85,
	332, 86, 333, 87, 88, 247, 133, 134, 135, 136,
	137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
	147, 148, 149, 150, 151, 152, 153, 320, 279, 30,
	284, 0, 283, 0, 0, 249, 133, 134, 135, 136,
	137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
	147, 148, 149, 150, 151, 152, 153, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 280, 133, 134, 135, 136, 137,
	138, 139, 140, 141, 142, 143, 144, 145, 146, 147,
	148, 149, 150, 151, 152, 153, 0, 33, 34, 35,
	36, 0, 0, 0, 286, 0, 37, 38, 39, 0,
	40, 41, 0, 0, 42, 43, 0, 44, 45, 0,
	0, 0, 0, 0, 0, 0, 0, 46, 0, 0,
	0, 0, 0, 47, 48, 0, 0, 0, 49, 50,
	0, 0, 0, 318, 33, 34, 35, 36, 0, 0,
	0, 0, 0, 37, 38, 39, 0, 40, 41, 0,
	0, 42, 43, 0, 44, 45, 0, 0, 0, 0,
	0, 0, 0, 0, 291, 0, 0, 0, 0, 0,
	47, 48, 0, 0, 0, 49, 50, 133, 134, 135,
	136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
	146, 147, 148, 149, 150, 151, 152, 153
};

static const yytype_int16 yycheck[] =
{
	110, 111, 161, 5, 6, 5, 6, 15, 5, 5,
	5, 75, 160, 90, 192, 17, 195, 94, 20, 14,
	13, 16, 17, 18, 19, 33, 34, 92, 14, 37,
	16, 17, 18, 19, 4, 194, 110, 111, 25, 198,
	199, 28, 190, 191, 68, 40, 41, 71, 112, 228,
	0, 92, 22, 3, 4, 234, 235, 236, 237, 9,
	10, 11, 12, 92, 85, 224, 225, 88, 89, 67,
	249, 250, 3, 4, 252, 78, 79, 70, 9, 10,
	11, 12, 110, 82, 83, 84, 109, 86, 87, 96,
	92, 68, 69, 93, 71, 101, 102, 103, 93, 278,
	279, 110, 110, 251, 94, 106, 107, 93, 110, 111,
	110, 108, 271, 110, 110, 74, 110, 111, 277, 110,
	111, 88, 89, 95, 109, 110, 102, 103, 26, 27,
	110, 16, 17, 18, 244, 294, 21, 22, 23, 24,
	25, 26, 27, 112, 29, 30, 31, 32, 33, 110,
	35, 36, 37, 38, 39, 16, 17, 18, 110, 111,
	21, 22, 23, 24, 25, 26, 27, 110, 29, 30,
	31, 32, 33, 95, 35, 36, 37, 38, 39, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
	110, 110, 110, 110, 110, 109, 91, 112, 93, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
	91, 113, 113, 113, 112, 110, 110, 110, 110, 110,
	110, 110, 69, 110, 26, 111, 114, 110, 42, 43,
	44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	54, 55, 56, 57, 58, 59, 60, 61, 62, 114,
	110, 110, 113, 111, 111, 110, 112, 110, 42, 43,
	44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	54, 55, 56, 57, 58, 59, 60, 61, 62, 110,
	85, 110, 91, 111, 97, 111, 34, 109, 109, 68,
	88, 109, 111, 94, 109, 69, 110, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62, 69, 91,
	110, 92, 98, 109, 94, 92, 110, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62, 110, 110,
	110, 110, 109, 109, 7, 109, 111, 110, 110, 32,
	110, 32, 111, 32, 32, 110, 42, 43, 44, 45,
	46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 295, 268, 23,
	275, -1, 274, -1, -1, 110, 42, 43, 44, 45,
	46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 62, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, 110, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 62, -1, 63, 64, 65,
	66, -1, -1, -1, 110, -1, 72, 73, 74, -1,
	76, 77, -1, -1, 80, 81, -1, 83, 84, -1,
	-1, -1, -1, -1, -1, -1, -1, 93, -1, -1,
	-1, -1, -1, 99, 100, -1, -1, -1, 104, 105,
	-1, -1, -1, 110, 63, 64, 65, 66, -1, -1,
	-1, -1, -1, 72, 73, 74, -1, 76, 77, -1,
	-1, 80, 81, -1, 83, 84, -1, -1, -1, -1,
	-1, -1, -1, -1, 93, -1, -1, -1, -1, -1,
	99, 100, -1, -1, -1, 104, 105, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
 * symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
	0, 3, 4, 9, 10, 11, 12, 116, 117, 118,
	120, 144, 145, 146, 179, 92, 92, 92, 147, 147,
	110, 111, 182, 180, 0, 117, 119, 121, 148, 147,
	182, 124, 122, 63, 64, 65, 66, 72, 73, 74,
	76, 77, 80, 81, 83, 84, 93, 99, 100, 104,
	105, 149, 150, 156, 163, 164, 165, 166, 168, 169,
	170, 171, 174, 175, 176, 177, 178, 5, 14, 16,
	17, 18, 19, 40, 41, 93, 125, 126, 127, 128,
	129, 130, 131, 93, 123, 127, 129, 130, 131, 110,
	110, 110, 67, 5, 108, 110, 137, 110, 75, 112,
	110, 167, 78, 79, 109, 82, 83, 84, 86, 87,
	95, 95, 110, 110, 112, 106, 107, 15, 33, 34,
	37, 110, 110, 111, 110, 110, 110, 132, 110, 111,
	113, 113, 113, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 110, 152, 159, 181, 112, 88,
	89, 96, 112, 109, 110, 94, 110, 110, 110, 110,
	110, 110, 110, 137, 137, 110, 101, 102, 103, 74,
	114, 114, 110, 109, 111, 110, 25, 28, 133, 85,
	88, 89, 69, 90, 94, 160, 13, 70, 151, 151,
	181, 110, 113, 102, 103, 111, 111, 112, 110, 110,
	26, 85, 5, 6, 17, 20, 92, 110, 111, 134,
	135, 68, 71, 162, 151, 151, 110, 152, 153, 91,
	161, 110, 181, 162, 110, 181, 110, 181, 97, 172,
	111, 111, 34, 109, 136, 109, 68, 110, 181, 110,
	181, 88, 69, 162, 109, 94, 162, 162, 162, 162,
	173, 111, 6, 93, 137, 139, 109, 158, 157, 162,
	162, 151, 152, 91, 92, 110, 138, 98, 154, 154,
	110, 181, 109, 148, 138, 140, 110, 155, 181, 162,
	162, 93, 141, 142, 94, 92, 16, 17, 18, 21,
	22, 23, 24, 25, 26, 27, 29, 30, 31, 32,
	33, 35, 36, 37, 38, 39, 91, 143, 110, 181,
	142, 110, 110, 110, 26, 27, 110, 109, 109, 111,
	110, 110, 110, 111, 109, 93, 110, 111
};

#define yyerrok (yyerrstatus = 0)
#define yyclearin (yychar = YYEMPTY)
#define YYEMPTY (-2)
#define YYEOF 0

#define YYACCEPT goto yyacceptlab
#define YYABORT goto yyabortlab
#define YYERROR goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
 * to ease the transition to the new meaning of YYERROR, for GCC.
 * Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL goto yyerrlab

#define YYRECOVERING() (!!yyerrstatus)

#define YYBACKUP(Token, Value) \
	do \
		if (yychar == YYEMPTY && yylen == 1) \
		{ \
			yychar = (Token); \
			yylval = (Value); \
			yytoken = YYTRANSLATE(yychar); \
			YYPOPSTACK(1); \
			goto yybackup; \
		} \
		else \
		{ \
			yyerror(YY_("syntax error: cannot back up")); \
			YYERROR; \
		} \
	while (YYID(0))


#define YYTERROR 1
#define YYERRCODE 256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
 * If N is 0, then set CURRENT to the empty location which ends
 * the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N) \
	do \
		if (YYID(N)) \
		{ \
			(Current).first_line = YYRHSLOC(Rhs, 1).first_line; \
			(Current).first_column = YYRHSLOC(Rhs, 1).first_column; \
			(Current).last_line = YYRHSLOC(Rhs, N).last_line; \
			(Current).last_column = YYRHSLOC(Rhs, N).last_column; \
		} \
		else \
		{ \
			(Current).first_line = (Current).last_line = \
				YYRHSLOC(Rhs, 0).last_line; \
			(Current).first_column = (Current).last_column = \
				YYRHSLOC(Rhs, 0).last_column; \
		} \
	while (YYID(0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
 * This macro was not mandated originally: define only if we know
 * we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc) \
	fprintf(File, "%d.%d-%d.%d", \
			(Loc).first_line, (Loc).first_column, \
			(Loc).last_line, (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex(YYLEX_PARAM)
#else
# define YYLEX yylex()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args) \
	do { \
		if (yydebug) { \
			YYFPRINTF Args; } \
	} while (YYID(0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location) \
	do { \
		if (yydebug) \
		{ \
			YYFPRINTF(stderr, "%s ", Title); \
			yy_symbol_print(stderr, \
							Type, Value); \
			YYFPRINTF(stderr, "\n"); \
		} \
	} while (YYID(0))


/*--------------------------------.
 | Print this symbol on YYOUTPUT.  |
 |  `--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static void yy_symbol_value_print(FILE *yyoutput, int yytype, YYSTYPE const *const
								  yyvaluep)
#else
static void yy_symbol_value_print(yyoutput, yytype, yyvaluep)
FILE *yyoutput;
int yytype;
YYSTYPE const *const yyvaluep;
#endif
{
	if (!yyvaluep)
	{
		return;
	}
# ifdef YYPRINT
	if (yytype < YYNTOKENS)
	{
		YYPRINT(yyoutput, yytoknum[yytype], *yyvaluep);
	}
# else
	YYUSE(yyoutput);
# endif
	switch (yytype)
	{
		default:
		{
			break;
		}
	}
}


/*--------------------------------.
 | Print this symbol on YYOUTPUT.  |
 |  `--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static void yy_symbol_print(FILE *yyoutput, int yytype, YYSTYPE const *const yyvaluep)
#else
static void yy_symbol_print(yyoutput, yytype, yyvaluep)
FILE *yyoutput;
int yytype;
YYSTYPE const *const yyvaluep;
#endif
{
	if (yytype < YYNTOKENS)
	{
		YYFPRINTF(yyoutput, "token %s (", yytname[yytype]);
	}
	else
	{
		YYFPRINTF(yyoutput, "nterm %s (", yytname[yytype]);
	}

	yy_symbol_value_print(yyoutput, yytype, yyvaluep);
	YYFPRINTF(yyoutput, ")");
}

/*------------------------------------------------------------------.
 | yy_stack_print -- Print the state stack from its BOTTOM up to its |
 | TOP (included).                                                   |
 |  `------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static void yy_stack_print(yytype_int16 *bottom, yytype_int16 *top)
#else
static void yy_stack_print(bottom, top)
yytype_int16 *bottom;
yytype_int16 *top;
#endif
{
	YYFPRINTF(stderr, "Stack now");
	for (; bottom <= top; ++bottom)
	{
		YYFPRINTF(stderr, " %d", *bottom);
	}
	YYFPRINTF(stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top) \
	do { \
		if (yydebug) { \
			yy_stack_print((Bottom), (Top)); } \
	} while (YYID(0))


/*------------------------------------------------.
 | Report that the YYRULE is going to be reduced.  |
 |  `------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static void yy_reduce_print(YYSTYPE *yyvsp, int yyrule)
#else
static void yy_reduce_print(yyvsp, yyrule)
YYSTYPE *yyvsp;
int yyrule;
#endif
{
	int yynrhs = yyr2[yyrule];
	int yyi;
	unsigned long int yylno = yyrline[yyrule];
	YYFPRINTF(stderr, "Reducing stack by rule %d (line %lu):\n",
			  yyrule - 1, yylno);

	/* The symbols being reduced.  */
	for (yyi = 0; yyi < yynrhs; yyi++)
	{
		fprintf(stderr, "   $%d = ", yyi + 1);
		yy_symbol_print(stderr, yyrhs[yyprhs[yyrule] + yyi],
						&(yyvsp[(yyi + 1) - (yynrhs)])
						);
		fprintf(stderr, "\n");
	}
}

# define YY_REDUCE_PRINT(Rule) \
	do { \
		if (yydebug) { \
			yy_reduce_print(yyvsp, Rule); } \
	} while (YYID(0))

/* Nonzero means print parse trace.  It is left uninitialized so that
 * multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
 * if the built-in stack extension method is used).
 *
 * Do not make this value too large; the results are undefined if
 * YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
 * evaluated with infinite-precision integer arithmetic.  */

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
static YYSIZE_T yystrlen(const char *yystr)
#else
static YYSIZE_T yystrlen(yystr)
const char *yystr;
#endif
{
	YYSIZE_T yylen;
	for (yylen = 0; yystr[yylen]; yylen++)
	{
		continue;
	}
	return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else

/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
 * YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static char * yystpcpy(char *yydest, const char *yysrc)
#else
static char * yystpcpy(yydest, yysrc)
char *yydest;
const char *yysrc;
#endif
{
	char *yyd = yydest;
	const char *yys = yysrc;

	while ((*yyd++ = *yys++) != '\0')
	{
		continue;
	}

	return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr

/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
 * quotes and backslashes, so that it's suitable for yyerror.  The
 * heuristic is that double-quoting is unnecessary unless the string
 * contains an apostrophe, a comma, or backslash (other than
 * backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
 * null, do not copy; instead, return the length of what the result
 * would have been.  */
static YYSIZE_T
yytnamerr(char *yyres, const char *yystr)
{
	if (*yystr == '"')
	{
		YYSIZE_T yyn = 0;
		char const *yyp = yystr;

		for (;;)
		{
			switch (*++yyp)
			{
				case '\'':
				case ',':
				{
					goto do_not_strip_quotes;
				}

				case '\\':
				{
					if (*++yyp != '\\')
					{
						goto do_not_strip_quotes;
					}

					/* Fall through.  */
				}

				default:
				{
					if (yyres)
					{
						yyres[yyn] = *yyp;
					}
					yyn++;
					break;
				}

				case '"':
					if (yyres)
					{
						yyres[yyn] = '\0';
					}
					return yyn;
			}
		}
do_not_strip_quotes:;
	}

	if (!yyres)
	{
		return yystrlen(yystr);
	}

	return yystpcpy(yyres, yystr) - yyres;
}


# endif

/* Copy into YYRESULT an error message about the unexpected token
 * YYCHAR while in state YYSTATE.  Return the number of bytes copied,
 * including the terminating null byte.  If YYRESULT is null, do not
 * copy anything; just return the number of bytes that would be
 * copied.  As a special case, return 0 if an ordinary "syntax error"
 * message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
 * size calculation.  */
static YYSIZE_T
yysyntax_error(char *yyresult, int yystate, int yychar)
{
	int yyn = yypact[yystate];

	if (!(YYPACT_NINF < yyn && yyn <= YYLAST))
	{
		return 0;
	}
	else
	{
		int yytype = YYTRANSLATE(yychar);
		YYSIZE_T yysize0 = yytnamerr(0, yytname[yytype]);
		YYSIZE_T yysize = yysize0;
		YYSIZE_T yysize1;
		int yysize_overflow = 0;
		enum
		{
			YYERROR_VERBOSE_ARGS_MAXIMUM = 5
		};
		char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
		int yyx;

# if 0

		/* This is so xgettext sees the translatable formats that are
		 * constructed on the fly.  */
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
		char yyformat[sizeof yyunexpected +
					  sizeof yyexpecting - 1 +
					  ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2) *
					   (sizeof yyor - 1))];
		char const *yyprefix = yyexpecting;

		/* Start YYX at -YYN if negative to avoid negative indexes in
		 * YYCHECK.  */
		int yyxbegin = yyn < 0 ? -yyn : 0;

		/* Stay within bounds of both yycheck and yytname.  */
		int yychecklim = YYLAST - yyn + 1;
		int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
		int yycount = 1;

		yyarg[0] = yytname[yytype];
		yyfmt = yystpcpy(yyformat, yyunexpected);

		for (yyx = yyxbegin; yyx < yyxend; ++yyx)
		{
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
				yysize1 = yysize + yytnamerr(0, yytname[yyx]);
				yysize_overflow |= (yysize1 < yysize);
				yysize = yysize1;
				yyfmt = yystpcpy(yyfmt, yyprefix);
				yyprefix = yyor;
			}
		}

		yyf = YY_(yyformat);
		yysize1 = yysize + yystrlen(yyf);
		yysize_overflow |= (yysize1 < yysize);
		yysize = yysize1;

		if (yysize_overflow)
		{
			return YYSIZE_MAXIMUM;
		}

		if (yyresult)
		{
			/* Avoid sprintf, as that infringes on the user's name space.
			 * Don't have undefined behavior even if the translation
			 * produced a string with the wrong number of "%s"s.  */
			char *yyp = yyresult;
			int yyi = 0;
			while ((*yyp = *yyf) != '\0')
			{
				if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
				{
					yyp += yytnamerr(yyp, yyarg[yyi++]);
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
 |  `-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
static void yydestruct(const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void yydestruct(yymsg, yytype, yyvaluep)
const char *yymsg;
int yytype;
YYSTYPE *yyvaluep;
#endif
{
	YYUSE(yyvaluep);

	if (!yymsg)
	{
		yymsg = "Deleting";
	}
	YY_SYMBOL_PRINT(yymsg, yytype, yyvaluep, yylocationp);

	switch (yytype)
	{
		default:
		{
			break;
		}
	}
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse(void *YYPARSE_PARAM);
#else
int yyparse();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse(void);
#else
int yyparse();
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
 |  `----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
int yyparse(void *YYPARSE_PARAM)
#else
int yyparse(YYPARSE_PARAM)
void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
	 || defined __cplusplus || defined _MSC_VER)
int
yyparse(void)
#else
int
yyparse()

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
	 * `yyss': related to states,
	 * `yyvs': related to semantic values,
	 * `yyls': related to locations.
	 *
	 * Refer to the stacks thru separate pointers, to allow yyoverflow
	 * to reallocate them elsewhere.  */

	/* The state stack.  */
	yytype_int16 yyssa[YYINITDEPTH];
	yytype_int16 *yyss = yyssa;
	yytype_int16 *yyssp;

	/* The semantic value stack.  */
	YYSTYPE yyvsa[YYINITDEPTH];
	YYSTYPE *yyvs = yyvsa;
	YYSTYPE *yyvsp;


#define YYPOPSTACK(N) (yyvsp -= (N), yyssp -= (N))

	YYSIZE_T yystacksize = YYINITDEPTH;

	/* The variables used to return semantic value and location from the
	 * action routines.  */
	YYSTYPE yyval;


	/* The number of symbols on the RHS of the reduced rule.
	 * Keep to zero when no symbol should be popped.  */
	int yylen = 0;

	YYDPRINTF((stderr, "Starting parse\n"));

	yystate = 0;
	yyerrstatus = 0;
	yynerrs = 0;
	yychar = YYEMPTY;   /* Cause a token to be read.  */

	/* Initialize stack pointers.
	 * Waste one element of value and location stack
	 * so that they stay on the same level as the state stack.
	 * The wasted elements are never initialized.  */

	yyssp = yyss;
	yyvsp = yyvs;

	goto yysetstate;

/*------------------------------------------------------------.
 | yynewstate -- Push a new state, which is found in yystate.  |
 |  `------------------------------------------------------------*/
yynewstate:

	/* In all cases, when you get here, the value and location stacks
	 * have just been pushed.  So pushing a state here evens the stacks.  */
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
			 * these so that the &'s don't force the real ones into
			 * memory.  */
			YYSTYPE *yyvs1 = yyvs;
			yytype_int16 *yyss1 = yyss;


			/* Each stack pointer address is followed by the size of the
			 * data in use in that stack, in bytes.  This used to be a
			 * conditional around just the two extra args, but that might
			 * be undefined if yyoverflow is a macro.  */
			yyoverflow(YY_("memory exhausted"),
					   &yyss1, yysize * sizeof(*yyssp),
					   &yyvs1, yysize * sizeof(*yyvsp),

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
		{
			goto yyexhaustedlab;
		}
		yystacksize *= 2;
		if (YYMAXDEPTH < yystacksize)
		{
			yystacksize = YYMAXDEPTH;
		}

		{
			yytype_int16 *yyss1 = yyss;
			union yyalloc *yyptr =
				(union yyalloc *) YYSTACK_ALLOC(YYSTACK_BYTES(yystacksize));
			if (!yyptr)
			{
				goto yyexhaustedlab;
			}
			YYSTACK_RELOCATE(yyss);
			YYSTACK_RELOCATE(yyvs);

#  undef YYSTACK_RELOCATE
			if (yyss1 != yyssa)
			{
				YYSTACK_FREE(yyss1);
			}
		}
# endif
#endif /* no yyoverflow */

		yyssp = yyss + yysize - 1;
		yyvsp = yyvs + yysize - 1;


		YYDPRINTF((stderr, "Stack size increased to %lu\n",
				   (unsigned long int) yystacksize));

		if (yyss + yystacksize - 1 <= yyssp)
		{
			YYABORT;
		}
	}

	YYDPRINTF((stderr, "Entering state %d\n", yystate));

	goto yybackup;

/*-----------.
 | yybackup.  |
 |  `-----------*/
yybackup:

	/* Do appropriate processing given the current state.  Read a
	 * look-ahead token if we need one and don't already have one.  */

	/* First try to decide what to do without reference to look-ahead token.  */
	yyn = yypact[yystate];
	if (yyn == YYPACT_NINF)
	{
		goto yydefault;
	}

	/* Not known => get a look-ahead token if don't already have one.  */

	/* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
	if (yychar == YYEMPTY)
	{
		YYDPRINTF((stderr, "Reading a token: "));
		yychar = YYLEX;
	}

	if (yychar <= YYEOF)
	{
		yychar = yytoken = YYEOF;
		YYDPRINTF((stderr, "Now at end of input.\n"));
	}
	else
	{
		yytoken = YYTRANSLATE(yychar);
		YY_SYMBOL_PRINT("Next token is", yytoken, &yylval, &yylloc);
	}

	/* If the proper action on seeing token YYTOKEN is to reduce or to
	 * detect an error, take that action.  */
	yyn += yytoken;
	if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
	{
		goto yydefault;
	}
	yyn = yytable[yyn];
	if (yyn <= 0)
	{
		if (yyn == 0 || yyn == YYTABLE_NINF)
		{
			goto yyerrlab;
		}
		yyn = -yyn;
		goto yyreduce;
	}

	if (yyn == YYFINAL)
	{
		YYACCEPT;
	}

	/* Count tokens shifted since error; after three, turn off error
	 * status.  */
	if (yyerrstatus)
	{
		yyerrstatus--;
	}

	/* Shift the look-ahead token.  */
	YY_SYMBOL_PRINT("Shifting", yytoken, &yylval, &yylloc);

	/* Discard the shifted token unless it is eof.  */
	if (yychar != YYEOF)
	{
		yychar = YYEMPTY;
	}

	yystate = yyn;
	*++yyvsp = yylval;

	goto yynewstate;


/*-----------------------------------------------------------.
 | yydefault -- do the default action for the current state.  |
 |  `-----------------------------------------------------------*/
yydefault:
	yyn = yydefact[yystate];
	if (yyn == 0)
	{
		goto yyerrlab;
	}
	goto yyreduce;


/*-----------------------------.
 | yyreduce -- Do a reduction.  |
 |  `-----------------------------*/
yyreduce:

	/* yyn is the number of a rule to reduce with.  */
	yylen = yyr2[yyn];

	/* If YYLEN is nonzero, implement the default value of the action:
	 * `$$ = $1'.
	 *
	 * Otherwise, the following line sets YYVAL to garbage.
	 * This behavior is undocumented and Bison
	 * users should not rely upon it.  Assigning to YYVAL
	 * unconditionally makes the parser a bit smaller, and it avoids a
	 * GCC warning that YYVAL may be used uninitialized.  */
	yyval = yyvsp[1 - yylen];


	YY_REDUCE_PRINT(yyn);
	switch (yyn)
	{
		case 10:
#line 236 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, "self-signed",
						sizeof(current_spec->cluster.ssl));
				strlcpy(current_spec->cluster.auth, "trust",
						sizeof(current_spec->cluster.auth));
			}
			break;

		case 12:
#line 259 "test_spec_parse.y"
			{
				current_spec->cluster.monitorApiOnly = true;
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.ssl, "self-signed",
						sizeof(current_spec->cluster.ssl));
				strlcpy(current_spec->cluster.auth, "trust",
						sizeof(current_spec->cluster.auth));
			}
			break;

		case 28:
#line 294 "test_spec_parse.y"
			{
				current_spec->cluster.bindSource = true;
			}
			break;

		case 29:
#line 308 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
			}
			break;

		case 30:
#line 312 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) -
																		   (3)].str),
						sizeof(current_spec->cluster.monitorDebianCluster));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 31:
#line 319 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorImageTarget));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 32:
#line 326 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;

				/* monitor port not stored in TestCluster yet; ignore */
				(void) (yyvsp[(3) - (3)].ival);
			}
			break;

		case 33:
#line 332 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorPassword));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 34:
#line 339 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 35:
#line 346 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 36:
#line 353 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (6)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (6)].str));

				/* password for second monitor not yet stored */
				free((yyvsp[(6) - (6)].str));
			}
			break;

		case 37:
#line 366 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 38:
#line 372 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 39:
#line 382 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 40:
#line 388 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 41:
#line 398 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 42:
#line 408 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 43:
#line 414 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 44:
#line 424 "test_spec_parse.y"
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
			break;

		case 48:
#line 451 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 49:
#line 452 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 50:
#line 453 "test_spec_parse.y"
			{
				(yyval.str) = strdup("auth");
			}
			break;

		case 51:
#line 454 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 52:
#line 455 "test_spec_parse.y"
			{
				(yyval.str) = strdup("node");
			}
			break;

		case 53:
#line 460 "test_spec_parse.y"
			{
				strlcpy(current_formation->name, (yyvsp[(1) - (1)].str),
						sizeof(current_formation->name));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 54:
#line 465 "test_spec_parse.y"
			{
				current_formation->numSync = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 57:
#line 491 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 58:
#line 492 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 59:
#line 501 "test_spec_parse.y"
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
			break;

		case 60:
#line 518 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(1) - (2)].str),
						sizeof(current_node->name));
				free((yyvsp[(1) - (2)].str));
			}
			break;

		case 62:
#line 525 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(2) - (3)].str),
						sizeof(current_node->name));
				free((yyvsp[(2) - (3)].str));
			}
			break;

		case 66:
#line 539 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_COORDINATOR;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 67:
#line 544 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_WORKER;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 68:
#line 549 "test_spec_parse.y"
			{
				current_node->replicationQuorum = false;
			}
			break;

		case 69:
#line 553 "test_spec_parse.y"
			{
				current_node->noMonitor = true;
			}
			break;

		case 70:
#line 557 "test_spec_parse.y"
			{
				current_node->launchDeferred = true;
			}
			break;

		case 71:
#line 561 "test_spec_parse.y"
			{
				current_node->launchDeferred = true;
			}
			break;

		case 72:
#line 565 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 73:
#line 569 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 74:
#line 573 "test_spec_parse.y"
			{
				current_node->listen = true;
			}
			break;

		case 75:
#line 577 "test_spec_parse.y"
			{
				current_node->citusSecondary = true;
			}
			break;

		case 76:
#line 581 "test_spec_parse.y"
			{
				current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 77:
#line 585 "test_spec_parse.y"
			{
				current_node->group = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 78:
#line 589 "test_spec_parse.y"
			{
				current_node->pgPort = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 79:
#line 593 "test_spec_parse.y"
			{
				strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
						sizeof(current_node->citusClusterName));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 80:
#line 599 "test_spec_parse.y"
			{
				strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
						sizeof(current_node->debianCluster));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 81:
#line 605 "test_spec_parse.y"
			{
				strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_node->ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 82:
#line 610 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 83:
#line 615 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 84:
#line 620 "test_spec_parse.y"
			{
				if (strcmp((yyvsp[(2) - (2)].str), "false") == 0 || strcmp((yyvsp[(2) -
																				  (2)].str),
																		   "0") == 0)
				{
					current_node->replicationQuorum = false;
				}
				else
				{
					current_node->replicationQuorum = true;
				}
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 85:
#line 628 "test_spec_parse.y"
			{
				strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->replicationPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 86:
#line 634 "test_spec_parse.y"
			{
				strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->monitorPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 87:
#line 640 "test_spec_parse.y"
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
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 88:
#line 654 "test_spec_parse.y"
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
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 89:
#line 675 "test_spec_parse.y"
			{
				current_spec->setup = (yyvsp[(2) - (2)].step);
			}
			break;

		case 90:
#line 682 "test_spec_parse.y"
			{
				current_spec->teardown = (yyvsp[(2) - (2)].step);
			}
			break;

		case 91:
#line 693 "test_spec_parse.y"
			{
				TestStep *s = (yyvsp[(3) - (3)].step);
				strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
				free((yyvsp[(2) - (3)].str));
				register_step(current_spec, s);
			}
			break;

		case 92:
#line 711 "test_spec_parse.y"
			{
				/* post-process: CMD_SQL immediately before CMD_EXPECT_ERROR */
				for (TestCmd *c = (yyvsp[(2) - (3)].step)->commands; c; c = c->next)
				{
					if (c->kind == CMD_SQL && c->next &&
						c->next->kind == CMD_EXPECT_ERROR)
					{
						c->allowError = true;
					}
				}
				(yyval.step) = (yyvsp[(2) - (3)].step);
			}
			break;

		case 93:
#line 725 "test_spec_parse.y"
			{
				(yyval.step) = make_step("");
			}
			break;

		case 94:
#line 729 "test_spec_parse.y"
			{
				if ((yyvsp[(2) - (2)].cmd))
				{
					append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
				}
				(yyval.step) = (yyvsp[(1) - (2)].step);
			}
			break;

		case 95:
#line 736 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 96:
#line 737 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 97:
#line 738 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 98:
#line 739 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 99:
#line 740 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 100:
#line 741 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 101:
#line 742 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 102:
#line 743 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 103:
#line 744 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 104:
#line 745 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 105:
#line 746 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 106:
#line 747 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 107:
#line 748 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 108:
#line 749 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 109:
#line 750 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 110:
#line 765 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 111:
#line 772 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 112:
#line 778 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 113:
#line 785 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 114:
#line 791 "test_spec_parse.y"
			{
				/* "pg_autoctl perform failover --formation auth"
				 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
				sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s",
						(yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 115:
#line 799 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
				strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 116:
#line 805 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			}
			break;

		case 119:
#line 843 "test_spec_parse.y"
			{
				if (!current_wait_cmd)
				{
					current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
				}
				int i = current_wait_cmd->waitStateCount;
				if (i < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(current_wait_cmd->waitNodes[i], (yyvsp[(1) - (4)].str),
							sizeof(current_wait_cmd->waitNodes[0]));
					strlcpy(current_wait_cmd->waitStates[i], (yyvsp[(4) - (4)].str),
							sizeof(current_wait_cmd->waitStates[0]));
					current_wait_cmd->waitStateCount++;
				}
				free((yyvsp[(1) - (4)].str));
			}
			break;

		case 120:
#line 858 "test_spec_parse.y"
			{
				if (!current_wait_cmd)
				{
					current_wait_cmd = make_cmd(CMD_WAIT_MULTI);
				}
				int i = current_wait_cmd->waitStateCount;
				if (i < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(current_wait_cmd->waitNodes[i], (yyvsp[(1) - (4)].str),
							sizeof(current_wait_cmd->waitNodes[0]));
					strlcpy(current_wait_cmd->waitStates[i], (yyvsp[(4) - (4)].str),
							sizeof(current_wait_cmd->waitStates[0]));
					current_wait_cmd->waitStateCount++;
				}
				free((yyvsp[(1) - (4)].str));
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 125:
#line 898 "test_spec_parse.y"
			{
				/* current_pass_cmd set by the enclosing wait_cmd rule */
				if (current_pass_cmd &&
					current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_pass_cmd->passThroughStates[current_pass_cmd->
															passThroughCount++],
						(yyvsp[(1) - (1)].str),
						sizeof(current_pass_cmd->passThroughStates[0]));
				}
			}
			break;

		case 126:
#line 906 "test_spec_parse.y"
			{
				if (current_pass_cmd &&
					current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_pass_cmd->passThroughStates[current_pass_cmd->
															passThroughCount++],
						(yyvsp[(1) - (1)].str),
						sizeof(current_pass_cmd->passThroughStates[0]));
				}
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 127:
#line 914 "test_spec_parse.y"
			{
				if (current_pass_cmd &&
					current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_pass_cmd->passThroughStates[current_pass_cmd->
															passThroughCount++],
						(yyvsp[(3) - (3)].str),
						sizeof(current_pass_cmd->passThroughStates[0]));
				}
			}
			break;

		case 128:
#line 921 "test_spec_parse.y"
			{
				if (current_pass_cmd &&
					current_pass_cmd->passThroughCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_pass_cmd->passThroughStates[current_pass_cmd->
															passThroughCount++],
						(yyvsp[(3) - (3)].str),
						sizeof(current_pass_cmd->passThroughStates[0]));
				}
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 129:
#line 932 "test_spec_parse.y"
			{
				current_pass_cmd = make_cmd(CMD_WAIT_STATE);
				strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
						sizeof(current_pass_cmd->service));
				strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
						sizeof(current_pass_cmd->state));
				free((yyvsp[(3) - (6)].str));
			}
			break;

		case 130:
#line 937 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 131:
#line 943 "test_spec_parse.y"
			{
				current_pass_cmd = make_cmd(CMD_WAIT_STATE);
				strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
						sizeof(current_pass_cmd->service));
				strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
						sizeof(current_pass_cmd->state));
				free((yyvsp[(3) - (6)].str));
				free((yyvsp[(6) - (6)].str));
			}
			break;

		case 132:
#line 948 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 133:
#line 954 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
				(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(6) - (7)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
				free((yyvsp[(3) - (7)].str));
			}
			break;

		case 134:
#line 963 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
				(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(6) - (7)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
				free((yyvsp[(3) - (7)].str));
				free((yyvsp[(6) - (7)].str));
			}
			break;

		case 135:
#line 972 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				free((yyvsp[(3) - (5)].str));
			}
			break;

		case 136:
#line 979 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 137:
#line 993 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 138:
#line 1008 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
			}
			break;

		case 139:
#line 1014 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 140:
#line 1021 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(3) - (3)].str),
						sizeof(current_wait_cmd->waitStates[0]));
				}
			}
			break;

		case 141:
#line 1027 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
				{
					strlcpy(
						current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(3) - (3)].str),
						sizeof(current_wait_cmd->waitStates[0]));
				}
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 144:
#line 1046 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(2) - (2)].ival);
				}
			}
			break;

		case 145:
#line 1051 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(4) - (4)].ival);
				}
			}
			break;

		case 146:
#line 1058 "test_spec_parse.y"
			{
				(yyval.ival) = PGAF_TIMEOUT_DEFAULT;
			}
			break;

		case 147:
#line 1059 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 148:
#line 1060 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(3) - (3)].ival);
			}
			break;

		case 149:
#line 1072 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE :
									   CMD_ASSERT_STATE);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				free((yyvsp[(2) - (6)].str));
			}
			break;

		case 150:
#line 1080 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE :
									   CMD_ASSERT_STATE);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				free((yyvsp[(2) - (6)].str));
				free((yyvsp[(5) - (6)].str));
			}
			break;

		case 151:
#line 1088 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				free((yyvsp[(2) - (6)].str));
			}
			break;

		case 152:
#line 1096 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				free((yyvsp[(2) - (6)].str));
				free((yyvsp[(5) - (6)].str));
			}
			break;

		case 153:
#line 1104 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_ASSERT_STATES);

				/* T_BLOCK contains "node1: draining  node2: prepare_promotion" */
				/* Parse name:state pairs inline */
				char *p = (yyvsp[(3) - (3)].str);
				while (*p)
				{
					while (*p == ' ' || *p == '\t' || *p == '\n')
					{
						p++;
					}
					if (*p == '\0')
					{
						break;
					}

					/* name */
					char name[64] = { 0 };
					int ni = 0;
					while (*p && *p != ':' && *p != ' ' && *p != '\t')
					{
						if (ni < 63)
						{
							name[ni++] = *p;
						}
						p++;
					}
					while (*p == ' ' || *p == '\t')
					{
						p++;
					}
					if (*p == ':')
					{
						p++;
					}
					while (*p == ' ' || *p == '\t')
					{
						p++;
					}

					/* state */
					char state[64] = { 0 };
					int si = 0;
					while (*p && *p != ' ' && *p != '\t' && *p != '\n')
					{
						if (si < 63)
						{
							state[si++] = *p;
						}
						p++;
					}
					if (name[0] && state[0])
					{
						int idx = (yyval.cmd)->waitStateCount;
						if (idx < PGAF_MAX_WAIT_STATES)
						{
							strlcpy((yyval.cmd)->waitNodes[idx], name,
									sizeof((yyval.cmd)->waitNodes[0]));
							strlcpy((yyval.cmd)->waitStates[idx], state,
									sizeof((yyval.cmd)->waitStates[0]));
							(yyval.cmd)->waitStateCount++;
						}
					}
				}
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 154:
#line 1156 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_SQL);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 155:
#line 1171 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT);
				strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->expected));
				expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 156:
#line 1178 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			}
			break;

		case 157:
#line 1182 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->state));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 158:
#line 1188 "test_spec_parse.y"
			{
				/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d",
						 (yyvsp[(3) - (3)].ival));
			}
			break;

		case 159:
#line 1201 "test_spec_parse.y"
			{
				(yyval.cmd) = current_promote_cmd;
				current_promote_cmd = NULL;
			}
			break;

		case 160:
#line 1209 "test_spec_parse.y"
			{
				current_promote_cmd = make_cmd(CMD_PROMOTE);
				current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
				strlcpy(
					current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
					(yyvsp[(1) - (1)].str),
					sizeof(current_promote_cmd->promoteNodes[0]));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 161:
#line 1217 "test_spec_parse.y"
			{
				if (current_promote_cmd->promoteCount < PGAF_MAX_PROMOTE_NODES)
				{
					strlcpy(
						current_promote_cmd->promoteNodes[current_promote_cmd->
														  promoteCount++],
						(yyvsp[(3) - (3)].str),
						sizeof(current_promote_cmd->promoteNodes[0]));
				}
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 162:
#line 1232 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 163:
#line 1238 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 164:
#line 1251 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_SLEEP);
				(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 165:
#line 1265 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
			}
			break;

		case 166:
#line 1269 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 167:
#line 1275 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 168:
#line 1281 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 169:
#line 1307 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_INJECT);
				strlcpy((yyval.cmd)->expected, (yyvsp[(3) - (4)].str),
						sizeof((yyval.cmd)->expected));                                         /* image */

				/* Split T_SHELL_ARGS: "<src-path> <svc>:<dst-path>" */
				char tmp[4096];
				strlcpy(tmp, (yyvsp[(4) - (4)].str), sizeof(tmp));
				char *src = tmp;
				char *p = tmp;
				while (*p && *p != ' ' && *p != '\t')
				{
					p++;
				}
				if (*p)
				{
					*p++ = '\0';
					while (*p == ' ' || *p == '\t')
					{
						p++;
					}
				}
				char *svcdst = p;
				char *colon = (*svcdst) ? strchr(svcdst, ':') : NULL;
				strlcpy((yyval.cmd)->args, src, sizeof((yyval.cmd)->args));
				if (colon)
				{
					*colon = '\0';
					strlcpy((yyval.cmd)->service, svcdst, sizeof((yyval.cmd)->service)); /* dst svc  */
					strlcpy((yyval.cmd)->state, colon + 1, sizeof((yyval.cmd)->state)); /* dst path */
				}
				free((yyvsp[(3) - (4)].str));
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 170:
#line 1341 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 171:
#line 1347 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 172:
#line 1363 "test_spec_parse.y"
			{
				pgaf_next_brace_is_while = 1;
			}
			break;

		case 173:
#line 1364 "test_spec_parse.y"
			{
				(yyval.step) = (yyvsp[(4) - (5)].step);
			}
			break;

		case 174:
#line 1369 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_STAYS_WHILE);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->state, (yyvsp[(4) - (5)].str),
						sizeof((yyval.cmd)->state));
				(yyval.cmd)->body = ((yyvsp[(5) - (5)].step)) ? (yyvsp[(5) -
																	   (5)].step)->
									commands : NULL;
				free((yyvsp[(2) - (5)].str));
			}
			break;

		case 175:
#line 1388 "test_spec_parse.y"
			{
				/* only "set monitor <svc>" is supported; $2 must be "monitor" */
				if (strcmp((yyvsp[(2) - (3)].str), "monitor") != 0)
				{
					fprintf(stderr,
							"pgaftest: unknown 'set' target '%s' (expected 'monitor')\n",
							(yyvsp[(2) -
								   (
									   3)].str));
					free((yyvsp[(2) - (3)].str));
					free((yyvsp[(3) - (3)].str));
					YYERROR;
				}
				(yyval.cmd) = make_cmd(CMD_SET_MONITOR);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (3)].str));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 176:
#line 1413 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->args));
				(yyval.cmd)->logsNegate = false;
				(yyval.cmd)->allowError = false; /* false = fixed string, true = PCRE */
				free((yyvsp[(2) - (4)].str));
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 177:
#line 1422 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str),
						sizeof((yyval.cmd)->args));
				(yyval.cmd)->logsNegate = true;
				(yyval.cmd)->allowError = false;
				free((yyvsp[(2) - (5)].str));
				free((yyvsp[(5) - (5)].str));
			}
			break;

		case 178:
#line 1431 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->args));
				(yyval.cmd)->logsNegate = false;
				(yyval.cmd)->allowError = true; /* true = PCRE (-P) */
				free((yyvsp[(2) - (4)].str));
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 179:
#line 1440 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (5)].str),
						sizeof((yyval.cmd)->service));
				strlcpy((yyval.cmd)->args, (yyvsp[(5) - (5)].str),
						sizeof((yyval.cmd)->args));
				(yyval.cmd)->logsNegate = true;
				(yyval.cmd)->allowError = true;
				free((yyvsp[(2) - (5)].str));
				free((yyvsp[(5) - (5)].str));
			}
			break;

		case 180:
#line 1461 "test_spec_parse.y"
			{
				if (!current_spec->cluster.monitorApiOnly)
				{
					yyerror("node_active is only allowed inside a scenario { } block; "
							"use a cluster { } block for full docker compose tests");
				}
				(yyval.cmd) = make_cmd(CMD_NODE_ACTIVE);

				/* $2 = "node2  reported: secondary  lsn: 0/5A0  ..." */
				strlcpy((yyval.cmd)->args, (yyvsp[(2) - (4)].str),
						sizeof((yyval.cmd)->args));

				/* $4 = "assigned: secondary" */
				strlcpy((yyval.cmd)->nodeActiveExpected, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->nodeActiveExpected));
				free((yyvsp[(2) - (4)].str));
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 181:
#line 1483 "test_spec_parse.y"
			{
				if (!current_spec->cluster.monitorApiOnly)
				{
					yyerror(
						"mark healthy/unhealthy is only allowed inside a scenario { } "
						"block; use a cluster { } block for full docker compose tests");
				}
				(yyval.cmd) = make_cmd(CMD_MARK_HEALTH);
				strlcpy((yyval.cmd)->service, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->markHealthy = true;
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 182:
#line 1495 "test_spec_parse.y"
			{
				if (!current_spec->cluster.monitorApiOnly)
				{
					yyerror(
						"mark healthy/unhealthy is only allowed inside a scenario { } "
						"block; use a cluster { } block for full docker compose tests");
				}
				(yyval.cmd) = make_cmd(CMD_MARK_HEALTH);
				strlcpy((yyval.cmd)->service, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->markHealthy = false;
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 185:
#line 1519 "test_spec_parse.y"
			{
				int i = current_spec->sequenceLength;
				if (i < PGAF_MAX_SEQ)
				{
					current_spec->sequence[current_spec->sequenceLength++] = (yyvsp[(2) -
																					(2)].
																			  str);
				}
				else
				{
					fprintf(stderr, "pgaftest: too many steps in sequence (max %d)\n",
							PGAF_MAX_SEQ);
					exit(1);
				}
			}
			break;

		case 186:
#line 1540 "test_spec_parse.y"
			{
				(yyval.str) = "init";
			}
			break;

		case 187:
#line 1541 "test_spec_parse.y"
			{
				(yyval.str) = "single";
			}
			break;

		case 188:
#line 1542 "test_spec_parse.y"
			{
				(yyval.str) = "primary";
			}
			break;

		case 189:
#line 1543 "test_spec_parse.y"
			{
				(yyval.str) = "wait_primary";
			}
			break;

		case 190:
#line 1544 "test_spec_parse.y"
			{
				(yyval.str) = "wait_standby";
			}
			break;

		case 191:
#line 1545 "test_spec_parse.y"
			{
				(yyval.str) = "demoted";
			}
			break;

		case 192:
#line 1546 "test_spec_parse.y"
			{
				(yyval.str) = "demote_timeout";
			}
			break;

		case 193:
#line 1547 "test_spec_parse.y"
			{
				(yyval.str) = "draining";
			}
			break;

		case 194:
#line 1548 "test_spec_parse.y"
			{
				(yyval.str) = "secondary";
			}
			break;

		case 195:
#line 1549 "test_spec_parse.y"
			{
				(yyval.str) = "catchingup";
			}
			break;

		case 196:
#line 1550 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_promotion";
			}
			break;

		case 197:
#line 1551 "test_spec_parse.y"
			{
				(yyval.str) = "stop_replication";
			}
			break;

		case 198:
#line 1552 "test_spec_parse.y"
			{
				(yyval.str) = "maintenance";
			}
			break;

		case 199:
#line 1553 "test_spec_parse.y"
			{
				(yyval.str) = "join_primary";
			}
			break;

		case 200:
#line 1554 "test_spec_parse.y"
			{
				(yyval.str) = "apply_settings";
			}
			break;

		case 201:
#line 1555 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_maintenance";
			}
			break;

		case 202:
#line 1556 "test_spec_parse.y"
			{
				(yyval.str) = "wait_maintenance";
			}
			break;

		case 203:
#line 1557 "test_spec_parse.y"
			{
				(yyval.str) = "report_lsn";
			}
			break;

		case 204:
#line 1558 "test_spec_parse.y"
			{
				(yyval.str) = "fast_forward";
			}
			break;

		case 205:
#line 1559 "test_spec_parse.y"
			{
				(yyval.str) = "join_secondary";
			}
			break;

		case 206:
#line 1560 "test_spec_parse.y"
			{
				(yyval.str) = "dropped";
			}
			break;

		case 207:
#line 1568 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 208:
#line 1569 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;


/* Line 1267 of yacc.c.  */
#line 3516 "test_spec_parse.c"
		default:
		{ }
		break;
	}
	YY_SYMBOL_PRINT("-> $$ =", yyr1[yyn], &yyval, &yyloc);

	YYPOPSTACK(yylen);
	yylen = 0;
	YY_STACK_PRINT(yyss, yyssp);

	*++yyvsp = yyval;


	/* Now `shift' the result of the reduction.  Determine what state
	 * that goes to, based on the state we popped back to and the rule
	 * number reduced by.  */

	yyn = yyr1[yyn];

	yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
	if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
	{
		yystate = yytable[yystate];
	}
	else
	{
		yystate = yydefgoto[yyn - YYNTOKENS];
	}

	goto yynewstate;


/*------------------------------------.
 | yyerrlab -- here on detecting error |
 |  `------------------------------------*/
yyerrlab:

	/* If not already recovering from an error, report this error.  */
	if (!yyerrstatus)
	{
		++yynerrs;
#if !YYERROR_VERBOSE
		yyerror(YY_("syntax error"));
#else
		{
			YYSIZE_T yysize = yysyntax_error(0, yystate, yychar);
			if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
			{
				YYSIZE_T yyalloc = 2 * yysize;
				if (!(yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
				{
					yyalloc = YYSTACK_ALLOC_MAXIMUM;
				}
				if (yymsg != yymsgbuf)
				{
					YYSTACK_FREE(yymsg);
				}
				yymsg = (char *) YYSTACK_ALLOC(yyalloc);
				if (yymsg)
				{
					yymsg_alloc = yyalloc;
				}
				else
				{
					yymsg = yymsgbuf;
					yymsg_alloc = sizeof yymsgbuf;
				}
			}

			if (0 < yysize && yysize <= yymsg_alloc)
			{
				(void) yysyntax_error(yymsg, yystate, yychar);
				yyerror(yymsg);
			}
			else
			{
				yyerror(YY_("syntax error"));
				if (yysize != 0)
				{
					goto yyexhaustedlab;
				}
			}
		}
#endif
	}


	if (yyerrstatus == 3)
	{
		/* If just tried and failed to reuse look-ahead token after an
		 * error, discard it.  */

		if (yychar <= YYEOF)
		{
			/* Return failure if at end of input.  */
			if (yychar == YYEOF)
			{
				YYABORT;
			}
		}
		else
		{
			yydestruct("Error: discarding",
					   yytoken, &yylval);
			yychar = YYEMPTY;
		}
	}

	/* Else will try to reuse look-ahead token after shifting the error
	 * token.  */
	goto yyerrlab1;


/*---------------------------------------------------.
 | yyerrorlab -- error raised explicitly by YYERROR.  |
 |  `---------------------------------------------------*/
yyerrorlab:

	/* Pacify compilers like GCC when the user code never invokes
	 * YYERROR and the label yyerrorlab therefore never appears in user
	 * code.  */
	if (/*CONSTCOND*/ 0)
	{
		goto yyerrorlab;
	}

	/* Do not reclaim the symbols of the rule which action triggered
	 * this YYERROR.  */
	YYPOPSTACK(yylen);
	yylen = 0;
	YY_STACK_PRINT(yyss, yyssp);
	yystate = *yyssp;
	goto yyerrlab1;


/*-------------------------------------------------------------.
 | yyerrlab1 -- common code for both syntax error and YYERROR.  |
 |  `-------------------------------------------------------------*/
yyerrlab1:
	yyerrstatus = 3; /* Each real token shifted decrements this.  */

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
				{
					break;
				}
			}
		}

		/* Pop the current state because it cannot handle the error token.  */
		if (yyssp == yyss)
		{
			YYABORT;
		}


		yydestruct("Error: popping",
				   yystos[yystate], yyvsp);
		YYPOPSTACK(1);
		yystate = *yyssp;
		YY_STACK_PRINT(yyss, yyssp);
	}

	if (yyn == YYFINAL)
	{
		YYACCEPT;
	}

	*++yyvsp = yylval;


	/* Shift the error token.  */
	YY_SYMBOL_PRINT("Shifting", yystos[yyn], yyvsp, yylsp);

	yystate = yyn;
	goto yynewstate;


/*-------------------------------------.
 | yyacceptlab -- YYACCEPT comes here.  |
 |  `-------------------------------------*/
yyacceptlab:
	yyresult = 0;
	goto yyreturn;

/*-----------------------------------.
 | yyabortlab -- YYABORT comes here.  |
 |  `-----------------------------------*/
yyabortlab:
	yyresult = 1;
	goto yyreturn;

#ifndef yyoverflow

/*-------------------------------------------------.
 | yyexhaustedlab -- memory exhaustion comes here.  |
 |  `-------------------------------------------------*/
yyexhaustedlab:
	yyerror(YY_("memory exhausted"));
	yyresult = 2;

	/* Fall through.  */
#endif

yyreturn:
	if (yychar != YYEOF && yychar != YYEMPTY)
	{
		yydestruct("Cleanup: discarding lookahead",
				   yytoken, &yylval);
	}

	/* Do not reclaim the symbols of the rule which action triggered
	 * this YYABORT or YYACCEPT.  */
	YYPOPSTACK(yylen);
	YY_STACK_PRINT(yyss, yyssp);
	while (yyssp != yyss)
	{
		yydestruct("Cleanup: popping",
				   yystos[*yyssp], yyvsp);
		YYPOPSTACK(1);
	}
#ifndef yyoverflow
	if (yyss != yyssa)
	{
		YYSTACK_FREE(yyss);
	}
#endif
#if YYERROR_VERBOSE
	if (yymsg != yymsgbuf)
	{
		YYSTACK_FREE(yymsg);
	}
#endif

	/* Make sure YYID is used.  */
	return YYID(yyresult);
}


#line 1572 "test_spec_parse.y"


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
	if (!spec)
	{
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	strncpy(spec->filename, filename, sizeof(spec->filename) - 1);

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
	if (!c)
	{
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	c->kind = kind;
	c->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
	return c;
}


TestStep *
make_step(const char *name)
{
	TestStep *s = (TestStep *) calloc(1, sizeof(TestStep));
	if (!s)
	{
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	if (name)
	{
		strncpy(s->name, name, sizeof(s->name) - 1);
	}
	return s;
}


TestStep *
spec_find_step(TestSpec *spec, const char *name)
{
	for (TestStep *s = spec->steps; s; s = s->next)
	{
		if (strcmp(s->name, name) == 0)
		{
			return s;
		}
	}
	return NULL;
}
