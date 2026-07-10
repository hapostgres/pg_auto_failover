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
	T_DEFERRED = 280,
	T_IMMEDIATE = 281,
	T_INITIALLY = 282,
	T_VOLUME = 283,
	T_LISTEN = 284,
	T_CITUS_SECONDARY = 285,
	T_CANDIDATE_PRIORITY = 286,
	T_PORT = 287,
	T_PASSWORD = 288,
	T_MONITOR_PASSWORD = 289,
	T_CITUS_CLUSTER_NAME = 290,
	T_DEBIAN_CLUSTER = 291,
	T_REPLICATION_QUORUM = 292,
	T_REPLICATION_PASSWORD = 293,
	T_EXTENSION_VERSION = 294,
	T_BIND_SOURCE = 295,
	T_FS_INIT = 296,
	T_FS_SINGLE = 297,
	T_FS_PRIMARY = 298,
	T_FS_WAIT_PRIMARY = 299,
	T_FS_WAIT_STANDBY = 300,
	T_FS_DEMOTED = 301,
	T_FS_DEMOTE_TIMEOUT = 302,
	T_FS_DRAINING = 303,
	T_FS_SECONDARY = 304,
	T_FS_CATCHINGUP = 305,
	T_FS_PREP_PROMOTION = 306,
	T_FS_STOP_REPLICATION = 307,
	T_FS_MAINTENANCE = 308,
	T_FS_JOIN_PRIMARY = 309,
	T_FS_APPLY_SETTINGS = 310,
	T_FS_PREPARE_MAINTENANCE = 311,
	T_FS_WAIT_MAINTENANCE = 312,
	T_FS_REPORT_LSN = 313,
	T_FS_FAST_FORWARD = 314,
	T_FS_JOIN_SECONDARY = 315,
	T_FS_DROPPED = 316,
	T_EXEC = 317,
	T_EXEC_FAILS = 318,
	T_PG_AUTOCTL = 319,
	T_WAIT = 320,
	T_UNTIL = 321,
	T_TIMEOUT = 322,
	T_AND = 323,
	T_IS = 324,
	T_WITH = 325,
	T_ASSERT = 326,
	T_SQL = 327,
	T_EXPECT = 328,
	T_ERROR = 329,
	T_PROMOTE = 330,
	T_NETWORK = 331,
	T_DISCONNECT = 332,
	T_CONNECT = 333,
	T_SLEEP = 334,
	T_COMPOSE = 335,
	T_DOWN = 336,
	T_START = 337,
	T_STOP = 338,
	T_STOPPED = 339,
	T_KILL = 340,
	T_INJECT = 341,
	T_STATE = 342,
	T_ASSIGNED_STATE = 343,
	T_IN = 344,
	T_GROUP = 345,
	T_LBRACE = 346,
	T_RBRACE = 347,
	T_COMMA = 348,
	T_POSTGRES = 349,
	T_STAYS = 350,
	T_WHILE = 351,
	T_THROUGH = 352,
	T_SET = 353,
	T_LOGS = 354,
	T_NOT = 355,
	T_CONTAINS = 356,
	T_MATCHES = 357,
	T_NODE_ACTIVE = 358,
	T_MARK = 359,
	T_HEALTHY = 360,
	T_UNHEALTHY = 361,
	T_INTEGER = 362,
	T_IDENT = 363,
	T_STRING = 364,
	T_BLOCK = 365,
	T_SHELL_ARGS = 366
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
#define T_DEFERRED 280
#define T_IMMEDIATE 281
#define T_INITIALLY 282
#define T_VOLUME 283
#define T_LISTEN 284
#define T_CITUS_SECONDARY 285
#define T_CANDIDATE_PRIORITY 286
#define T_PORT 287
#define T_PASSWORD 288
#define T_MONITOR_PASSWORD 289
#define T_CITUS_CLUSTER_NAME 290
#define T_DEBIAN_CLUSTER 291
#define T_REPLICATION_QUORUM 292
#define T_REPLICATION_PASSWORD 293
#define T_EXTENSION_VERSION 294
#define T_BIND_SOURCE 295
#define T_FS_INIT 296
#define T_FS_SINGLE 297
#define T_FS_PRIMARY 298
#define T_FS_WAIT_PRIMARY 299
#define T_FS_WAIT_STANDBY 300
#define T_FS_DEMOTED 301
#define T_FS_DEMOTE_TIMEOUT 302
#define T_FS_DRAINING 303
#define T_FS_SECONDARY 304
#define T_FS_CATCHINGUP 305
#define T_FS_PREP_PROMOTION 306
#define T_FS_STOP_REPLICATION 307
#define T_FS_MAINTENANCE 308
#define T_FS_JOIN_PRIMARY 309
#define T_FS_APPLY_SETTINGS 310
#define T_FS_PREPARE_MAINTENANCE 311
#define T_FS_WAIT_MAINTENANCE 312
#define T_FS_REPORT_LSN 313
#define T_FS_FAST_FORWARD 314
#define T_FS_JOIN_SECONDARY 315
#define T_FS_DROPPED 316
#define T_EXEC 317
#define T_EXEC_FAILS 318
#define T_PG_AUTOCTL 319
#define T_WAIT 320
#define T_UNTIL 321
#define T_TIMEOUT 322
#define T_AND 323
#define T_IS 324
#define T_WITH 325
#define T_ASSERT 326
#define T_SQL 327
#define T_EXPECT 328
#define T_ERROR 329
#define T_PROMOTE 330
#define T_NETWORK 331
#define T_DISCONNECT 332
#define T_CONNECT 333
#define T_SLEEP 334
#define T_COMPOSE 335
#define T_DOWN 336
#define T_START 337
#define T_STOP 338
#define T_STOPPED 339
#define T_KILL 340
#define T_INJECT 341
#define T_STATE 342
#define T_ASSIGNED_STATE 343
#define T_IN 344
#define T_GROUP 345
#define T_LBRACE 346
#define T_RBRACE 347
#define T_COMMA 348
#define T_POSTGRES 349
#define T_STAYS 350
#define T_WHILE 351
#define T_THROUGH 352
#define T_SET 353
#define T_LOGS 354
#define T_NOT 355
#define T_CONTAINS 356
#define T_MATCHES 357
#define T_NODE_ACTIVE 358
#define T_MARK 359
#define T_HEALTHY 360
#define T_UNHEALTHY 361
#define T_INTEGER 362
#define T_IDENT 363
#define T_STRING 364
#define T_BLOCK 365
#define T_SHELL_ARGS 366


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
#line 469 "test_spec_parse.c"
YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 482 "test_spec_parse.c"

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
#define YYFINAL 21

/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST 581

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS 113

/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS 64

/* YYNRULES -- Number of rules.  */
#define YYNRULES 198

/* YYNRULES -- Number of states.  */
#define YYNSTATES 325

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK 2
#define YYMAXUTOK 366

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
	2, 2, 2, 2, 2, 2, 2, 2, 112, 2,
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
	105, 106, 107, 108, 109, 110, 111
};

#if YYDEBUG

/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
 * YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
	0, 0, 3, 5, 8, 10, 12, 14, 16, 18,
	19, 25, 26, 29, 31, 33, 35, 37, 39, 41,
	43, 45, 49, 53, 57, 61, 66, 71, 78, 81,
	84, 87, 90, 93, 96, 99, 100, 107, 108, 111,
	113, 115, 117, 119, 121, 123, 126, 127, 130, 132,
	134, 135, 136, 141, 142, 150, 151, 154, 156, 158,
	160, 162, 164, 167, 170, 172, 174, 176, 179, 182,
	185, 188, 191, 194, 197, 200, 203, 206, 209, 213,
	217, 220, 223, 227, 231, 232, 235, 237, 239, 241,
	243, 245, 247, 249, 251, 253, 255, 257, 259, 261,
	263, 265, 269, 272, 276, 279, 283, 286, 288, 290,
	292, 297, 302, 304, 308, 309, 312, 314, 316, 320,
	324, 325, 335, 336, 346, 354, 362, 368, 374, 381,
	383, 385, 389, 393, 394, 397, 400, 405, 406, 409,
	413, 420, 427, 434, 441, 445, 448, 451, 455, 459,
	462, 464, 468, 472, 476, 479, 482, 486, 490, 494,
	499, 503, 507, 508, 514, 520, 524, 529, 535, 540,
	546, 551, 556, 561, 564, 565, 568, 570, 572, 574,
	576, 578, 580, 582, 584, 586, 588, 590, 592, 594,
	596, 598, 600, 602, 604, 606, 608, 610, 612
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
	114, 0, -1, 115, -1, 114, 115, -1, 116, -1,
	138, -1, 139, -1, 140, -1, 173, -1, -1, 3,
	91, 117, 118, 92, -1, -1, 118, 119, -1, 120,
	-1, 121, -1, 123, -1, 124, -1, 122, -1, 125,
	-1, 40, -1, 4, -1, 4, 36, 108, -1, 4,
	14, 108, -1, 4, 32, 107, -1, 4, 33, 109,
	-1, 4, 108, 24, 25, -1, 4, 108, 27, 84,
	-1, 4, 108, 24, 25, 33, 109, -1, 13, 109,
	-1, 13, 108, -1, 39, 108, -1, 39, 109, -1,
	15, 108, -1, 16, 108, -1, 17, 108, -1, -1,
	18, 126, 127, 91, 130, 92, -1, -1, 127, 129,
	-1, 108, -1, 109, -1, 16, -1, 4, -1, 5,
	-1, 128, -1, 19, 107, -1, -1, 130, 133, -1,
	108, -1, 4, -1, -1, -1, 131, 132, 134, 136,
	-1, -1, 5, 108, 132, 135, 91, 136, 92, -1,
	-1, 136, 137, -1, 20, -1, 21, -1, 22, -1,
	23, -1, 25, -1, 24, 25, -1, 24, 26, -1,
	26, -1, 29, -1, 30, -1, 31, 107, -1, 90,
	107, -1, 32, 107, -1, 35, 108, -1, 36, 108,
	-1, 15, 108, -1, 16, 108, -1, 17, 108, -1,
	37, 108, -1, 38, 109, -1, 34, 109, -1, 28,
	108, 108, -1, 28, 108, 109, -1, 8, 141, -1,
	9, 141, -1, 10, 176, 141, -1, 91, 142, 92,
	-1, -1, 142, 143, -1, 144, -1, 150, -1, 157,
	-1, 158, -1, 159, -1, 160, -1, 162, -1, 163,
	-1, 164, -1, 165, -1, 168, -1, 169, -1, 170,
	-1, 171, -1, 172, -1, 62, 108, 111, -1, 62,
	108, -1, 63, 108, 111, -1, 63, 108, -1, 64,
	108, 111, -1, 64, 108, -1, 64, -1, 12, -1,
	69, -1, 108, 87, 145, 175, -1, 108, 87, 145,
	108, -1, 146, -1, 147, 68, 146, -1, -1, 97,
	149, -1, 175, -1, 108, -1, 149, 93, 175, -1,
	149, 93, 108, -1, -1, 65, 66, 108, 87, 145,
	175, 151, 148, 156, -1, -1, 65, 66, 108, 87,
	145, 108, 152, 148, 156, -1, 65, 66, 108, 88,
	145, 175, 156, -1, 65, 66, 108, 88, 145, 108,
	156, -1, 65, 66, 108, 84, 156, -1, 65, 66,
	153, 154, 156, -1, 65, 66, 146, 68, 147, 156,
	-1, 175, -1, 108, -1, 153, 93, 175, -1, 153,
	93, 108, -1, -1, 89, 155, -1, 90, 107, -1,
	155, 93, 90, 107, -1, -1, 67, 107, -1, 70,
	67, 107, -1, 71, 108, 87, 145, 175, 156, -1,
	71, 108, 87, 145, 108, 156, -1, 71, 108, 88,
	145, 175, 156, -1, 71, 108, 88, 145, 108, 156,
	-1, 72, 108, 110, -1, 73, 110, -1, 73, 74,
	-1, 73, 74, 108, -1, 73, 74, 107, -1, 75,
	161, -1, 108, -1, 161, 93, 108, -1, 76, 77,
	108, -1, 76, 78, 108, -1, 79, 107, -1, 80,
	81, -1, 80, 82, 108, -1, 80, 83, 108, -1,
	80, 85, 108, -1, 80, 86, 108, 111, -1, 83,
	94, 131, -1, 82, 94, 131, -1, -1, 96, 167,
	91, 142, 92, -1, 71, 131, 95, 175, 166, -1,
	98, 108, 108, -1, 99, 108, 101, 109, -1, 99,
	108, 100, 101, 109, -1, 99, 108, 102, 109, -1,
	99, 108, 100, 102, 109, -1, 103, 110, 73, 110,
	-1, 104, 105, 112, 108, -1, 104, 106, 112, 108,
	-1, 11, 174, -1, -1, 174, 176, -1, 41, -1,
	42, -1, 43, -1, 44, -1, 45, -1, 46, -1,
	47, -1, 48, -1, 49, -1, 50, -1, 51, -1,
	52, -1, 53, -1, 54, -1, 55, -1, 56, -1,
	57, -1, 58, -1, 59, -1, 60, -1, 61, -1,
	108, -1, 109, -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
	0, 213, 213, 214, 218, 219, 220, 221, 222, 235,
	234, 244, 246, 250, 251, 252, 253, 254, 255, 256,
	269, 273, 280, 287, 293, 300, 307, 314, 327, 333,
	343, 349, 359, 369, 375, 386, 385, 402, 404, 413,
	414, 415, 416, 417, 421, 426, 432, 434, 453, 454,
	463, 480, 479, 487, 486, 494, 496, 500, 505, 510,
	514, 518, 522, 526, 530, 534, 538, 542, 546, 550,
	554, 560, 566, 571, 576, 581, 589, 595, 601, 615,
	636, 643, 654, 672, 687, 690, 698, 699, 700, 701,
	702, 703, 704, 705, 706, 707, 708, 709, 710, 711,
	712, 726, 733, 739, 746, 752, 760, 766, 793, 793,
	804, 819, 837, 838, 853, 855, 859, 867, 875, 882,
	894, 893, 905, 904, 915, 924, 933, 940, 954, 969,
	975, 982, 988, 1001, 1003, 1007, 1012, 1020, 1021, 1022,
	1033, 1041, 1049, 1057, 1075, 1090, 1097, 1101, 1107, 1120,
	1128, 1136, 1151, 1157, 1170, 1184, 1188, 1194, 1200, 1226,
	1260, 1266, 1283, 1283, 1288, 1307, 1332, 1341, 1350, 1359,
	1380, 1397, 1404, 1418, 1421, 1423, 1445, 1446, 1447, 1448,
	1449, 1450, 1451, 1452, 1453, 1454, 1455, 1456, 1457, 1458,
	1459, 1460, 1461, 1462, 1463, 1464, 1465, 1473, 1474
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
 * First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
	"$end", "error", "$undefined", "T_CLUSTER", "T_MONITOR", "T_NODE",
	"T_CITUS_COORDINATOR", "T_CITUS_WORKER", "T_SETUP", "T_TEARDOWN",
	"T_STEP", "T_SEQUENCE", "T_EQUALS", "T_IMAGE", "T_IMAGE_TARGET", "T_SSL",
	"T_AUTH", "T_AUTH_METHOD", "T_FORMATION", "T_NUM_SYNC", "T_COORDINATOR",
	"T_WORKER", "T_ASYNC", "T_NO_MONITOR", "T_LAUNCH", "T_DEFERRED",
	"T_IMMEDIATE", "T_INITIALLY", "T_VOLUME", "T_LISTEN",
	"T_CITUS_SECONDARY", "T_CANDIDATE_PRIORITY", "T_PORT", "T_PASSWORD",
	"T_MONITOR_PASSWORD", "T_CITUS_CLUSTER_NAME", "T_DEBIAN_CLUSTER",
	"T_REPLICATION_QUORUM", "T_REPLICATION_PASSWORD", "T_EXTENSION_VERSION",
	"T_BIND_SOURCE", "T_FS_INIT", "T_FS_SINGLE", "T_FS_PRIMARY",
	"T_FS_WAIT_PRIMARY", "T_FS_WAIT_STANDBY", "T_FS_DEMOTED",
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
	"T_MARK", "T_HEALTHY", "T_UNHEALTHY", "T_INTEGER", "T_IDENT", "T_STRING",
	"T_BLOCK", "T_SHELL_ARGS", "':'", "$accept", "spec", "spec_item",
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
	"network_cmd", "sleep_cmd", "compose_cmd", "postgres_ctl_cmd",
	"while_body", "@7", "stays_while_cmd", "set_monitor_cmd", "logs_cmd",
	"node_active_cmd", "mark_health_cmd", "sequence_block", "sequence_names",
	"fsm_state", "ident_or_string", 0
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
	365, 366, 58
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
	0, 113, 114, 114, 115, 115, 115, 115, 115, 117,
	116, 118, 118, 119, 119, 119, 119, 119, 119, 119,
	120, 120, 120, 120, 120, 120, 120, 120, 121, 121,
	122, 122, 123, 124, 124, 126, 125, 127, 127, 128,
	128, 128, 128, 128, 129, 129, 130, 130, 131, 131,
	132, 134, 133, 135, 133, 136, 136, 137, 137, 137,
	137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
	137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
	138, 139, 140, 141, 142, 142, 143, 143, 143, 143,
	143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
	143, 144, 144, 144, 144, 144, 144, 144, 145, 145,
	146, 146, 147, 147, 148, 148, 149, 149, 149, 149,
	151, 150, 152, 150, 150, 150, 150, 150, 150, 153,
	153, 153, 153, 154, 154, 155, 155, 156, 156, 156,
	157, 157, 157, 157, 158, 159, 159, 159, 159, 160,
	161, 161, 162, 162, 163, 164, 164, 164, 164, 164,
	165, 165, 167, 166, 168, 169, 170, 170, 170, 170,
	171, 172, 172, 173, 174, 174, 175, 175, 175, 175,
	175, 175, 175, 175, 175, 175, 175, 175, 175, 175,
	175, 175, 175, 175, 175, 175, 175, 176, 176
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
	0, 2, 1, 2, 1, 1, 1, 1, 1, 0,
	5, 0, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 3, 3, 3, 3, 4, 4, 6, 2, 2,
	2, 2, 2, 2, 2, 0, 6, 0, 2, 1,
	1, 1, 1, 1, 1, 2, 0, 2, 1, 1,
	0, 0, 4, 0, 7, 0, 2, 1, 1, 1,
	1, 1, 2, 2, 1, 1, 1, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
	2, 2, 3, 3, 0, 2, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 3, 2, 3, 2, 3, 2, 1, 1, 1,
	4, 4, 1, 3, 0, 2, 1, 1, 3, 3,
	0, 9, 0, 9, 7, 7, 5, 5, 6, 1,
	1, 3, 3, 0, 2, 2, 4, 0, 2, 3,
	6, 6, 6, 6, 3, 2, 2, 3, 3, 2,
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
	0, 0, 0, 0, 0, 174, 0, 2, 4, 5,
	6, 7, 8, 9, 84, 80, 81, 197, 198, 0,
	173, 1, 3, 11, 0, 82, 175, 0, 0, 0,
	107, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 83, 0, 0, 0, 0, 85, 86, 87, 88,
	89, 90, 91, 92, 93, 94, 95, 96, 97, 98,
	99, 100, 20, 0, 0, 0, 0, 35, 0, 19,
	10, 12, 13, 14, 17, 15, 16, 18, 102, 104,
	106, 0, 49, 48, 0, 0, 146, 145, 150, 149,
	0, 0, 154, 155, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	29, 28, 32, 33, 34, 37, 30, 31, 101, 103,
	105, 176, 177, 178, 179, 180, 181, 182, 183, 184,
	185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
	195, 196, 130, 0, 133, 129, 0, 0, 0, 144,
	148, 147, 0, 152, 153, 156, 157, 158, 0, 48,
	161, 160, 165, 0, 0, 0, 0, 0, 0, 22,
	23, 24, 21, 0, 0, 0, 137, 0, 0, 0,
	0, 0, 137, 108, 109, 0, 0, 0, 151, 159,
	0, 0, 166, 168, 170, 171, 172, 25, 26, 42,
	43, 41, 0, 46, 39, 40, 44, 38, 0, 0,
	126, 0, 0, 0, 112, 137, 0, 134, 132, 131,
	127, 137, 137, 137, 137, 162, 164, 167, 169, 0,
	45, 0, 138, 0, 122, 120, 137, 137, 0, 0,
	128, 135, 0, 141, 140, 143, 142, 0, 27, 0,
	36, 50, 47, 139, 114, 114, 125, 124, 0, 113,
	0, 84, 50, 51, 0, 137, 137, 111, 110, 136,
	0, 53, 55, 117, 115, 116, 123, 121, 163, 0,
	52, 0, 55, 0, 0, 0, 57, 58, 59, 60,
	0, 61, 64, 0, 65, 66, 0, 0, 0, 0,
	0, 0, 0, 0, 56, 119, 118, 0, 72, 73,
	74, 62, 63, 0, 67, 69, 77, 70, 71, 75,
	76, 68, 54, 78, 79
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
	-1, 6, 7, 8, 23, 27, 71, 72, 73, 74,
	75, 76, 77, 115, 175, 206, 207, 231, 84, 263,
	252, 272, 279, 280, 304, 9, 10, 11, 15, 24,
	46, 47, 185, 143, 215, 265, 274, 48, 255, 254,
	144, 182, 217, 210, 49, 50, 51, 52, 89, 53,
	54, 55, 56, 226, 247, 57, 58, 59, 60, 61,
	12, 20, 145, 19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
 * STATE-NUM.  */
#define YYPACT_NINF -171
static const yytype_int16 yypact[] =
{
	73, -47, -26, -26, -74, -171, 69, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -26,
	-74, -171, -171, -171, 410, -171, -171, 7, -40, -38,
	-21, 23, 3, -14, -55, -6, -35, -7, -25, 21,
	27, -171, -2, 19, 15, -31, -171, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -5, -17, 20, 34, 40, -171, -11, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, 46, 47,
	55, 137, -171, 17, 29, 62, 6, -171, -171, 33,
	91, 92, -171, -171, 93, 95, 96, 98, 4, 4,
	122, -52, 56, 90, 119, 124, 126, 125, 127, 12,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -58, 168, -72, -171, 2, 2, 520, -171,
	-171, -171, 129, -171, -171, -171, -171, -171, 128, -171,
	-171, -171, -171, 16, 131, 132, 133, 130, 134, -171,
	-171, -171, -171, 219, 183, -1, -8, 2, 2, 160,
	179, 167, -8, -171, -171, 205, 235, 174, -171, -171,
	162, 163, -171, -171, -171, -171, -171, 240, -171, -171,
	-171, -171, 190, -171, -171, -171, -171, -171, 191, 207,
	-171, 273, 303, 212, -171, 18, 193, 208, -171, -171,
	-171, -8, -8, -8, -8, -171, -171, -171, -171, 194,
	-171, 1, -171, 195, 236, 237, -8, -8, 2, 160,
	-171, -171, 216, -171, -171, -171, -171, 217, -171, 199,
	-171, -171, -171, -171, 213, 213, -171, -171, 341, -171,
	202, -171, -171, -171, 371, -8, -8, -171, -171, -171,
	456, -171, -171, -171, 218, -171, -171, -171, -171, 221,
	139, 409, -171, 227, 228, 229, -171, -171, -171, -171,
	94, -171, -171, 230, -171, -171, 232, 233, 256, 234,
	258, 259, 260, 261, -171, -171, -171, 115, -171, -171,
	-171, -171, -171, 14, -171, -171, -171, -171, -171, -171,
	-171, -171, -171, -171, -171
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
	-171, -171, 335, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -97, 108,
	-171, -171, -171, 89, -171, -171, -171, -171, 13, 111,
	-171, -171, -137, -166, -171, 118, -171, -171, -171, -171,
	-171, -171, -171, -170, -171, -171, -171, -171, -171, -171,
	-171, -171, -171, -171, -171, -171, -171, -171, -171, -171,
	-171, -171, -148, 354
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
 * positive, shift that token.  If negative, reduce the rule which
 * number is the opposite.  If zero, do what YYDEFACT says.
 * If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -112
static const yytype_int16 yytable[] =
{
	187, 160, 161, 199, 200, 82, 249, 82, 82, 105,
	186, 62, 220, 214, 183, 201, 16, 180, 202, 86,
	63, 181, 64, 65, 66, 67, 176, 106, 107, 177,
	178, 108, 25, 219, 17, 18, 173, 222, 224, 174,
	211, 212, 90, 91, 13, 240, 68, 69, 163, 164,
	165, 243, 244, 245, 246, 87, 93, 94, 95, 208,
	96, 97, 209, 235, 237, 14, 256, 257, 78, 21,
	79, 184, 1, 259, 103, 104, 1, 2, 3, 4,
	5, 2, 3, 4, 5, 208, 239, 80, 209, 81,
	203, 110, 111, 250, 85, 276, 277, 116, 117, 70,
	92, 258, 88, 109, 146, 147, 100, 204, 205, 159,
	268, 83, 159, 150, 151, 98, 275, 190, 191, 311,
	312, 99, 323, 324, 148, 102, 152, 101, 112, 166,
	283, 284, 285, 306, 251, 286, 287, 288, 289, 290,
	291, 292, 113, 293, 294, 295, 296, 297, 114, 298,
	299, 300, 301, 302, 283, 284, 285, 118, 119, 286,
	287, 288, 289, 290, 291, 292, 120, 293, 294, 295,
	296, 297, 149, 298, 299, 300, 301, 302, 121, 122,
	123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
	133, 134, 135, 136, 137, 138, 139, 140, 141, 153,
	154, 155, 167, 156, 157, 303, 158, 322, 121, 122,
	123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
	133, 134, 135, 136, 137, 138, 139, 140, 141, 303,
	162, 168, 169, 170, 171, 172, 179, 188, 195, 189,
	192, 193, 196, 194, 197, 142, 121, 122, 123, 124,
	125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 198, 213, 216,
	225, 227, 228, 229, 233, 218, 121, 122, 123, 124,
	125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 230, 232, 238,
	241, 242, 253, 248, -111, -110, 260, 262, 261, 269,
	264, 281, 282, 221, 121, 122, 123, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 134, 135, 136,
	137, 138, 139, 140, 141, 308, 309, 310, 313, 314,
	315, 22, 317, 223, 121, 122, 123, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 134, 135, 136,
	137, 138, 139, 140, 141, 316, 318, 319, 321, 320,
	271, 307, 270, 266, 26, 0, 0, 0, 0, 0,
	0, 234, 121, 122, 123, 124, 125, 126, 127, 128,
	129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 0, 0, 0, 0, 0, 0, 0,
	0, 236, 121, 122, 123, 124, 125, 126, 127, 128,
	129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 267,
	121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
	131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
	141, 0, 28, 29, 30, 31, 0, 0, 0, 273,
	0, 32, 33, 34, 0, 35, 36, 0, 0, 37,
	38, 0, 39, 40, 0, 0, 0, 0, 0, 0,
	0, 0, 41, 0, 0, 0, 0, 0, 42, 43,
	0, 0, 0, 44, 45, 0, 0, 305, 28, 29,
	30, 31, 0, 0, 0, 0, 0, 32, 33, 34,
	0, 35, 36, 0, 0, 37, 38, 0, 39, 40,
	0, 0, 0, 0, 0, 0, 0, 0, 278, 0,
	0, 0, 0, 0, 42, 43, 0, 0, 0, 44,
	45, 121, 122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
	140, 141
};

static const yytype_int16 yycheck[] =
{
	148, 98, 99, 4, 5, 4, 5, 4, 4, 14,
	147, 4, 182, 179, 12, 16, 3, 89, 19, 74,
	13, 93, 15, 16, 17, 18, 84, 32, 33, 87,
	88, 36, 19, 181, 108, 109, 24, 185, 186, 27,
	177, 178, 77, 78, 91, 215, 39, 40, 100, 101,
	102, 221, 222, 223, 224, 110, 81, 82, 83, 67,
	85, 86, 70, 211, 212, 91, 236, 237, 108, 0,
	108, 69, 3, 239, 105, 106, 3, 8, 9, 10,
	11, 8, 9, 10, 11, 67, 68, 108, 70, 66,
	91, 108, 109, 92, 108, 265, 266, 108, 109, 92,
	107, 238, 108, 108, 87, 88, 108, 108, 109, 108,
	258, 108, 108, 107, 108, 94, 264, 101, 102, 25,
	26, 94, 108, 109, 95, 110, 93, 108, 108, 73,
	15, 16, 17, 281, 231, 20, 21, 22, 23, 24,
	25, 26, 108, 28, 29, 30, 31, 32, 108, 34,
	35, 36, 37, 38, 15, 16, 17, 111, 111, 20,
	21, 22, 23, 24, 25, 26, 111, 28, 29, 30,
	31, 32, 110, 34, 35, 36, 37, 38, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 108,
	108, 108, 112, 108, 108, 90, 108, 92, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 90,
	108, 112, 108, 107, 109, 108, 68, 108, 108, 111,
	109, 109, 108, 110, 25, 108, 41, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 84, 108, 90,
	96, 109, 109, 33, 67, 108, 41, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 107, 107, 87,
	107, 93, 107, 109, 68, 68, 90, 108, 91, 107,
	97, 93, 91, 108, 41, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 108, 108, 108, 108, 107,
	107, 6, 108, 108, 41, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 109, 108, 108, 107, 109,
	262, 282, 261, 255, 20, -1, -1, -1, -1, -1,
	-1, 108, 41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, -1, -1, -1, -1, -1, -1, -1,
	-1, 108, 41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, 108,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, -1, 62, 63, 64, 65, -1, -1, -1, 108,
	-1, 71, 72, 73, -1, 75, 76, -1, -1, 79,
	80, -1, 82, 83, -1, -1, -1, -1, -1, -1,
	-1, -1, 92, -1, -1, -1, -1, -1, 98, 99,
	-1, -1, -1, 103, 104, -1, -1, 108, 62, 63,
	64, 65, -1, -1, -1, -1, -1, 71, 72, 73,
	-1, 75, 76, -1, -1, 79, 80, -1, 82, 83,
	-1, -1, -1, -1, -1, -1, -1, -1, 92, -1,
	-1, -1, -1, -1, 98, 99, -1, -1, -1, 103,
	104, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
	60, 61
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
 * symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
	0, 3, 8, 9, 10, 11, 114, 115, 116, 138,
	139, 140, 173, 91, 91, 141, 141, 108, 109, 176,
	174, 0, 115, 117, 142, 141, 176, 118, 62, 63,
	64, 65, 71, 72, 73, 75, 76, 79, 80, 82,
	83, 92, 98, 99, 103, 104, 143, 144, 150, 157,
	158, 159, 160, 162, 163, 164, 165, 168, 169, 170,
	171, 172, 4, 13, 15, 16, 17, 18, 39, 40,
	92, 119, 120, 121, 122, 123, 124, 125, 108, 108,
	108, 66, 4, 108, 131, 108, 74, 110, 108, 161,
	77, 78, 107, 81, 82, 83, 85, 86, 94, 94,
	108, 108, 110, 105, 106, 14, 32, 33, 36, 108,
	108, 109, 108, 108, 108, 126, 108, 109, 111, 111,
	111, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
	60, 61, 108, 146, 153, 175, 87, 88, 95, 110,
	107, 108, 93, 108, 108, 108, 108, 108, 108, 108,
	131, 131, 108, 100, 101, 102, 73, 112, 112, 108,
	107, 109, 108, 24, 27, 127, 84, 87, 88, 68,
	89, 93, 154, 12, 69, 145, 145, 175, 108, 111,
	101, 102, 109, 109, 110, 108, 108, 25, 84, 4,
	5, 16, 19, 91, 108, 109, 128, 129, 67, 70,
	156, 145, 145, 108, 146, 147, 90, 155, 108, 175,
	156, 108, 175, 108, 175, 96, 166, 109, 109, 33,
	107, 130, 107, 67, 108, 175, 108, 175, 87, 68,
	156, 107, 93, 156, 156, 156, 156, 167, 109, 5,
	92, 131, 133, 107, 152, 151, 156, 156, 145, 146,
	90, 91, 108, 132, 97, 148, 148, 108, 175, 107,
	142, 132, 134, 108, 149, 175, 156, 156, 92, 135,
	136, 93, 91, 15, 16, 17, 20, 21, 22, 23,
	24, 25, 26, 28, 29, 30, 31, 32, 34, 35,
	36, 37, 38, 90, 137, 108, 175, 136, 108, 108,
	108, 25, 26, 108, 107, 107, 109, 108, 108, 108,
	109, 107, 92, 108, 109
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
		case 9:
#line 235 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, "self-signed",
						sizeof(current_spec->cluster.ssl));
				strlcpy(current_spec->cluster.auth, "trust",
						sizeof(current_spec->cluster.auth));
			}
			break;

		case 19:
#line 256 "test_spec_parse.y"
			{
				current_spec->cluster.bindSource = true;
			}
			break;

		case 20:
#line 270 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
			}
			break;

		case 21:
#line 274 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) -
																		   (3)].str),
						sizeof(current_spec->cluster.monitorDebianCluster));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 22:
#line 281 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorImageTarget));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 23:
#line 288 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;

				/* monitor port not stored in TestCluster yet; ignore */
				(void) (yyvsp[(3) - (3)].ival);
			}
			break;

		case 24:
#line 294 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorPassword));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 25:
#line 301 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 26:
#line 308 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 27:
#line 315 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (6)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (6)].str));

				/* password for second monitor not yet stored */
				free((yyvsp[(6) - (6)].str));
			}
			break;

		case 28:
#line 328 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 29:
#line 334 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 30:
#line 344 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 31:
#line 350 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 32:
#line 360 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 33:
#line 370 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 34:
#line 376 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 35:
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
			}
			break;

		case 39:
#line 413 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 40:
#line 414 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 41:
#line 415 "test_spec_parse.y"
			{
				(yyval.str) = strdup("auth");
			}
			break;

		case 42:
#line 416 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 43:
#line 417 "test_spec_parse.y"
			{
				(yyval.str) = strdup("node");
			}
			break;

		case 44:
#line 422 "test_spec_parse.y"
			{
				strlcpy(current_formation->name, (yyvsp[(1) - (1)].str),
						sizeof(current_formation->name));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 45:
#line 427 "test_spec_parse.y"
			{
				current_formation->numSync = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 48:
#line 453 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 49:
#line 454 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 50:
#line 463 "test_spec_parse.y"
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

		case 51:
#line 480 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(1) - (2)].str),
						sizeof(current_node->name));
				free((yyvsp[(1) - (2)].str));
			}
			break;

		case 53:
#line 487 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(2) - (3)].str),
						sizeof(current_node->name));
				free((yyvsp[(2) - (3)].str));
			}
			break;

		case 57:
#line 501 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_COORDINATOR;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 58:
#line 506 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_WORKER;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 59:
#line 511 "test_spec_parse.y"
			{
				current_node->replicationQuorum = false;
			}
			break;

		case 60:
#line 515 "test_spec_parse.y"
			{
				current_node->noMonitor = true;
			}
			break;

		case 61:
#line 519 "test_spec_parse.y"
			{
				current_node->launchDeferred = true;
			}
			break;

		case 62:
#line 523 "test_spec_parse.y"
			{
				current_node->launchDeferred = true;
			}
			break;

		case 63:
#line 527 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 64:
#line 531 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 65:
#line 535 "test_spec_parse.y"
			{
				current_node->listen = true;
			}
			break;

		case 66:
#line 539 "test_spec_parse.y"
			{
				current_node->citusSecondary = true;
			}
			break;

		case 67:
#line 543 "test_spec_parse.y"
			{
				current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 68:
#line 547 "test_spec_parse.y"
			{
				current_node->group = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 69:
#line 551 "test_spec_parse.y"
			{
				current_node->pgPort = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 70:
#line 555 "test_spec_parse.y"
			{
				strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
						sizeof(current_node->citusClusterName));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 71:
#line 561 "test_spec_parse.y"
			{
				strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
						sizeof(current_node->debianCluster));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 72:
#line 567 "test_spec_parse.y"
			{
				strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_node->ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 73:
#line 572 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 74:
#line 577 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 75:
#line 582 "test_spec_parse.y"
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

		case 76:
#line 590 "test_spec_parse.y"
			{
				strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->replicationPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 77:
#line 596 "test_spec_parse.y"
			{
				strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->monitorPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 78:
#line 602 "test_spec_parse.y"
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

		case 79:
#line 616 "test_spec_parse.y"
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

		case 80:
#line 637 "test_spec_parse.y"
			{
				current_spec->setup = (yyvsp[(2) - (2)].step);
			}
			break;

		case 81:
#line 644 "test_spec_parse.y"
			{
				current_spec->teardown = (yyvsp[(2) - (2)].step);
			}
			break;

		case 82:
#line 655 "test_spec_parse.y"
			{
				TestStep *s = (yyvsp[(3) - (3)].step);
				strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
				free((yyvsp[(2) - (3)].str));
				register_step(current_spec, s);
			}
			break;

		case 83:
#line 673 "test_spec_parse.y"
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

		case 84:
#line 687 "test_spec_parse.y"
			{
				(yyval.step) = make_step("");
			}
			break;

		case 85:
#line 691 "test_spec_parse.y"
			{
				if ((yyvsp[(2) - (2)].cmd))
				{
					append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
				}
				(yyval.step) = (yyvsp[(1) - (2)].step);
			}
			break;

		case 86:
#line 698 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 87:
#line 699 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 88:
#line 700 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 89:
#line 701 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 90:
#line 702 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 91:
#line 703 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 92:
#line 704 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 93:
#line 705 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 94:
#line 706 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 95:
#line 707 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 96:
#line 708 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 97:
#line 709 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 98:
#line 710 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 99:
#line 711 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 100:
#line 712 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 101:
#line 727 "test_spec_parse.y"
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

		case 102:
#line 734 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 103:
#line 740 "test_spec_parse.y"
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

		case 104:
#line 747 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 105:
#line 753 "test_spec_parse.y"
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

		case 106:
#line 761 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
				strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 107:
#line 767 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			}
			break;

		case 110:
#line 805 "test_spec_parse.y"
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

		case 111:
#line 820 "test_spec_parse.y"
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

		case 116:
#line 860 "test_spec_parse.y"
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

		case 117:
#line 868 "test_spec_parse.y"
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

		case 118:
#line 876 "test_spec_parse.y"
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

		case 119:
#line 883 "test_spec_parse.y"
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

		case 120:
#line 894 "test_spec_parse.y"
			{
				current_pass_cmd = make_cmd(CMD_WAIT_STATE);
				strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
						sizeof(current_pass_cmd->service));
				strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
						sizeof(current_pass_cmd->state));
				free((yyvsp[(3) - (6)].str));
			}
			break;

		case 121:
#line 899 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 122:
#line 905 "test_spec_parse.y"
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

		case 123:
#line 910 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 124:
#line 916 "test_spec_parse.y"
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

		case 125:
#line 925 "test_spec_parse.y"
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

		case 126:
#line 934 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				free((yyvsp[(3) - (5)].str));
			}
			break;

		case 127:
#line 941 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 128:
#line 955 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 129:
#line 970 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
			}
			break;

		case 130:
#line 976 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 131:
#line 983 "test_spec_parse.y"
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

		case 132:
#line 989 "test_spec_parse.y"
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

		case 135:
#line 1008 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(2) - (2)].ival);
				}
			}
			break;

		case 136:
#line 1013 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(4) - (4)].ival);
				}
			}
			break;

		case 137:
#line 1020 "test_spec_parse.y"
			{
				(yyval.ival) = PGAF_TIMEOUT_DEFAULT;
			}
			break;

		case 138:
#line 1021 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 139:
#line 1022 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(3) - (3)].ival);
			}
			break;

		case 140:
#line 1034 "test_spec_parse.y"
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

		case 141:
#line 1042 "test_spec_parse.y"
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

		case 142:
#line 1050 "test_spec_parse.y"
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

		case 143:
#line 1058 "test_spec_parse.y"
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

		case 144:
#line 1076 "test_spec_parse.y"
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

		case 145:
#line 1091 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT);
				strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->expected));
				expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 146:
#line 1098 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			}
			break;

		case 147:
#line 1102 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->state));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 148:
#line 1108 "test_spec_parse.y"
			{
				/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d",
						 (yyvsp[(3) - (3)].ival));
			}
			break;

		case 149:
#line 1121 "test_spec_parse.y"
			{
				(yyval.cmd) = current_promote_cmd;
				current_promote_cmd = NULL;
			}
			break;

		case 150:
#line 1129 "test_spec_parse.y"
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

		case 151:
#line 1137 "test_spec_parse.y"
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

		case 152:
#line 1152 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 153:
#line 1158 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 154:
#line 1171 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_SLEEP);
				(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 155:
#line 1185 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
			}
			break;

		case 156:
#line 1189 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 157:
#line 1195 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 158:
#line 1201 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 159:
#line 1227 "test_spec_parse.y"
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

		case 160:
#line 1261 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 161:
#line 1267 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 162:
#line 1283 "test_spec_parse.y"
			{
				pgaf_next_brace_is_while = 1;
			}
			break;

		case 163:
#line 1284 "test_spec_parse.y"
			{
				(yyval.step) = (yyvsp[(4) - (5)].step);
			}
			break;

		case 164:
#line 1289 "test_spec_parse.y"
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

		case 165:
#line 1308 "test_spec_parse.y"
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

		case 166:
#line 1333 "test_spec_parse.y"
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

		case 167:
#line 1342 "test_spec_parse.y"
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

		case 168:
#line 1351 "test_spec_parse.y"
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

		case 169:
#line 1360 "test_spec_parse.y"
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

		case 170:
#line 1381 "test_spec_parse.y"
			{
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

		case 171:
#line 1398 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_MARK_HEALTH);
				strlcpy((yyval.cmd)->service, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->markHealthy = true;
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 172:
#line 1405 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_MARK_HEALTH);
				strlcpy((yyval.cmd)->service, (yyvsp[(4) - (4)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->markHealthy = false;
				free((yyvsp[(4) - (4)].str));
			}
			break;

		case 175:
#line 1424 "test_spec_parse.y"
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

		case 176:
#line 1445 "test_spec_parse.y"
			{
				(yyval.str) = "init";
			}
			break;

		case 177:
#line 1446 "test_spec_parse.y"
			{
				(yyval.str) = "single";
			}
			break;

		case 178:
#line 1447 "test_spec_parse.y"
			{
				(yyval.str) = "primary";
			}
			break;

		case 179:
#line 1448 "test_spec_parse.y"
			{
				(yyval.str) = "wait_primary";
			}
			break;

		case 180:
#line 1449 "test_spec_parse.y"
			{
				(yyval.str) = "wait_standby";
			}
			break;

		case 181:
#line 1450 "test_spec_parse.y"
			{
				(yyval.str) = "demoted";
			}
			break;

		case 182:
#line 1451 "test_spec_parse.y"
			{
				(yyval.str) = "demote_timeout";
			}
			break;

		case 183:
#line 1452 "test_spec_parse.y"
			{
				(yyval.str) = "draining";
			}
			break;

		case 184:
#line 1453 "test_spec_parse.y"
			{
				(yyval.str) = "secondary";
			}
			break;

		case 185:
#line 1454 "test_spec_parse.y"
			{
				(yyval.str) = "catchingup";
			}
			break;

		case 186:
#line 1455 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_promotion";
			}
			break;

		case 187:
#line 1456 "test_spec_parse.y"
			{
				(yyval.str) = "stop_replication";
			}
			break;

		case 188:
#line 1457 "test_spec_parse.y"
			{
				(yyval.str) = "maintenance";
			}
			break;

		case 189:
#line 1458 "test_spec_parse.y"
			{
				(yyval.str) = "join_primary";
			}
			break;

		case 190:
#line 1459 "test_spec_parse.y"
			{
				(yyval.str) = "apply_settings";
			}
			break;

		case 191:
#line 1460 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_maintenance";
			}
			break;

		case 192:
#line 1461 "test_spec_parse.y"
			{
				(yyval.str) = "wait_maintenance";
			}
			break;

		case 193:
#line 1462 "test_spec_parse.y"
			{
				(yyval.str) = "report_lsn";
			}
			break;

		case 194:
#line 1463 "test_spec_parse.y"
			{
				(yyval.str) = "fast_forward";
			}
			break;

		case 195:
#line 1464 "test_spec_parse.y"
			{
				(yyval.str) = "join_secondary";
			}
			break;

		case 196:
#line 1465 "test_spec_parse.y"
			{
				(yyval.str) = "dropped";
			}
			break;

		case 197:
#line 1473 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 198:
#line 1474 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;


/* Line 1267 of yacc.c.  */
#line 3430 "test_spec_parse.c"
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


#line 1477 "test_spec_parse.y"


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
