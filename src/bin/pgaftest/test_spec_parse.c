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
	T_CREATE = 280,
	T_DEFERRED = 281,
	T_IMMEDIATE = 282,
	T_FALSE = 283,
	T_INITIALLY = 284,
	T_VOLUME = 285,
	T_LISTEN = 286,
	T_CITUS_SECONDARY = 287,
	T_CANDIDATE_PRIORITY = 288,
	T_PORT = 289,
	T_PASSWORD = 290,
	T_MONITOR_PASSWORD = 291,
	T_CITUS_CLUSTER_NAME = 292,
	T_DEBIAN_CLUSTER = 293,
	T_REPLICATION_QUORUM = 294,
	T_REPLICATION_PASSWORD = 295,
	T_EXTENSION_VERSION = 296,
	T_BIND_SOURCE = 297,
	T_FS_INIT = 298,
	T_FS_SINGLE = 299,
	T_FS_PRIMARY = 300,
	T_FS_WAIT_PRIMARY = 301,
	T_FS_WAIT_STANDBY = 302,
	T_FS_DEMOTED = 303,
	T_FS_DEMOTE_TIMEOUT = 304,
	T_FS_DRAINING = 305,
	T_FS_SECONDARY = 306,
	T_FS_CATCHINGUP = 307,
	T_FS_PREP_PROMOTION = 308,
	T_FS_STOP_REPLICATION = 309,
	T_FS_MAINTENANCE = 310,
	T_FS_JOIN_PRIMARY = 311,
	T_FS_APPLY_SETTINGS = 312,
	T_FS_PREPARE_MAINTENANCE = 313,
	T_FS_WAIT_MAINTENANCE = 314,
	T_FS_REPORT_LSN = 315,
	T_FS_FAST_FORWARD = 316,
	T_FS_JOIN_SECONDARY = 317,
	T_FS_DROPPED = 318,
	T_EXEC = 319,
	T_EXEC_FAILS = 320,
	T_PG_AUTOCTL = 321,
	T_WAIT = 322,
	T_UNTIL = 323,
	T_TIMEOUT = 324,
	T_AND = 325,
	T_IS = 326,
	T_WITH = 327,
	T_ASSERT = 328,
	T_SQL = 329,
	T_EXPECT = 330,
	T_ERROR = 331,
	T_PROMOTE = 332,
	T_NETWORK = 333,
	T_DISCONNECT = 334,
	T_CONNECT = 335,
	T_SLEEP = 336,
	T_COMPOSE = 337,
	T_DOWN = 338,
	T_START = 339,
	T_STOP = 340,
	T_STOPPED = 341,
	T_KILL = 342,
	T_INJECT = 343,
	T_STATE = 344,
	T_ASSIGNED_STATE = 345,
	T_IN = 346,
	T_GROUP = 347,
	T_LBRACE = 348,
	T_RBRACE = 349,
	T_COMMA = 350,
	T_POSTGRES = 351,
	T_STAYS = 352,
	T_WHILE = 353,
	T_THROUGH = 354,
	T_SET = 355,
	T_LOGS = 356,
	T_NOT = 357,
	T_CONTAINS = 358,
	T_MATCHES = 359,
	T_INTEGER = 360,
	T_IDENT = 361,
	T_STRING = 362,
	T_BLOCK = 363,
	T_SHELL_ARGS = 364
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
#define T_INITIALLY 284
#define T_VOLUME 285
#define T_LISTEN 286
#define T_CITUS_SECONDARY 287
#define T_CANDIDATE_PRIORITY 288
#define T_PORT 289
#define T_PASSWORD 290
#define T_MONITOR_PASSWORD 291
#define T_CITUS_CLUSTER_NAME 292
#define T_DEBIAN_CLUSTER 293
#define T_REPLICATION_QUORUM 294
#define T_REPLICATION_PASSWORD 295
#define T_EXTENSION_VERSION 296
#define T_BIND_SOURCE 297
#define T_FS_INIT 298
#define T_FS_SINGLE 299
#define T_FS_PRIMARY 300
#define T_FS_WAIT_PRIMARY 301
#define T_FS_WAIT_STANDBY 302
#define T_FS_DEMOTED 303
#define T_FS_DEMOTE_TIMEOUT 304
#define T_FS_DRAINING 305
#define T_FS_SECONDARY 306
#define T_FS_CATCHINGUP 307
#define T_FS_PREP_PROMOTION 308
#define T_FS_STOP_REPLICATION 309
#define T_FS_MAINTENANCE 310
#define T_FS_JOIN_PRIMARY 311
#define T_FS_APPLY_SETTINGS 312
#define T_FS_PREPARE_MAINTENANCE 313
#define T_FS_WAIT_MAINTENANCE 314
#define T_FS_REPORT_LSN 315
#define T_FS_FAST_FORWARD 316
#define T_FS_JOIN_SECONDARY 317
#define T_FS_DROPPED 318
#define T_EXEC 319
#define T_EXEC_FAILS 320
#define T_PG_AUTOCTL 321
#define T_WAIT 322
#define T_UNTIL 323
#define T_TIMEOUT 324
#define T_AND 325
#define T_IS 326
#define T_WITH 327
#define T_ASSERT 328
#define T_SQL 329
#define T_EXPECT 330
#define T_ERROR 331
#define T_PROMOTE 332
#define T_NETWORK 333
#define T_DISCONNECT 334
#define T_CONNECT 335
#define T_SLEEP 336
#define T_COMPOSE 337
#define T_DOWN 338
#define T_START 339
#define T_STOP 340
#define T_STOPPED 341
#define T_KILL 342
#define T_INJECT 343
#define T_STATE 344
#define T_ASSIGNED_STATE 345
#define T_IN 346
#define T_GROUP 347
#define T_LBRACE 348
#define T_RBRACE 349
#define T_COMMA 350
#define T_POSTGRES 351
#define T_STAYS 352
#define T_WHILE 353
#define T_THROUGH 354
#define T_SET 355
#define T_LOGS 356
#define T_NOT 357
#define T_CONTAINS 358
#define T_MATCHES 359
#define T_INTEGER 360
#define T_IDENT 361
#define T_STRING 362
#define T_BLOCK 363
#define T_SHELL_ARGS 364


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
#line 465 "test_spec_parse.c"
YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 478 "test_spec_parse.c"

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
#define YYLAST 560

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS 110

/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS 62

/* YYNRULES -- Number of rules.  */
#define YYNRULES 196

/* YYNRULES -- Number of states.  */
#define YYNSTATES 319

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK 2
#define YYMAXUTOK 364

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
	105, 106, 107, 108, 109
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
	113, 115, 117, 119, 121, 123, 126, 129, 130, 133,
	135, 137, 138, 139, 144, 145, 153, 154, 157, 159,
	161, 163, 165, 167, 170, 173, 178, 181, 183, 185,
	187, 190, 193, 196, 199, 202, 205, 208, 211, 214,
	217, 220, 224, 228, 231, 234, 238, 242, 243, 246,
	248, 250, 252, 254, 256, 258, 260, 262, 264, 266,
	268, 270, 272, 276, 279, 283, 286, 290, 293, 295,
	297, 299, 304, 309, 311, 315, 316, 319, 321, 323,
	327, 331, 332, 342, 343, 353, 361, 369, 375, 381,
	388, 390, 392, 396, 400, 401, 404, 407, 412, 413,
	416, 420, 427, 434, 441, 448, 452, 455, 458, 462,
	466, 469, 471, 475, 479, 483, 486, 489, 493, 497,
	501, 506, 510, 514, 515, 521, 527, 531, 536, 542,
	547, 553, 556, 557, 560, 562, 564, 566, 568, 570,
	572, 574, 576, 578, 580, 582, 584, 586, 588, 590,
	592, 594, 596, 598, 600, 602, 604
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
	111, 0, -1, 112, -1, 111, 112, -1, 113, -1,
	135, -1, 136, -1, 137, -1, 168, -1, -1, 3,
	93, 114, 115, 94, -1, -1, 115, 116, -1, 117,
	-1, 118, -1, 120, -1, 121, -1, 119, -1, 122,
	-1, 42, -1, 4, -1, 4, 38, 106, -1, 4,
	14, 106, -1, 4, 34, 105, -1, 4, 35, 107,
	-1, 4, 106, 24, 26, -1, 4, 106, 29, 86,
	-1, 4, 106, 24, 26, 35, 107, -1, 13, 107,
	-1, 13, 106, -1, 41, 106, -1, 41, 107, -1,
	15, 106, -1, 16, 106, -1, 17, 106, -1, -1,
	18, 123, 124, 93, 127, 94, -1, -1, 124, 126,
	-1, 106, -1, 107, -1, 16, -1, 4, -1, 5,
	-1, 125, -1, 19, 105, -1, 51, 28, -1, -1,
	127, 130, -1, 106, -1, 4, -1, -1, -1, 128,
	129, 131, 133, -1, -1, 5, 106, 129, 132, 93,
	133, 94, -1, -1, 133, 134, -1, 20, -1, 21,
	-1, 22, -1, 23, -1, 26, -1, 24, 26, -1,
	25, 26, -1, 25, 70, 24, 26, -1, 24, 27,
	-1, 27, -1, 31, -1, 32, -1, 33, 105, -1,
	92, 105, -1, 34, 105, -1, 37, 106, -1, 38,
	106, -1, 15, 106, -1, 16, 106, -1, 17, 106,
	-1, 39, 106, -1, 40, 107, -1, 36, 107, -1,
	30, 106, 106, -1, 30, 106, 107, -1, 8, 138,
	-1, 9, 138, -1, 10, 171, 138, -1, 93, 139,
	94, -1, -1, 139, 140, -1, 141, -1, 147, -1,
	154, -1, 155, -1, 156, -1, 157, -1, 159, -1,
	160, -1, 161, -1, 162, -1, 165, -1, 166, -1,
	167, -1, 64, 106, 109, -1, 64, 106, -1, 65,
	106, 109, -1, 65, 106, -1, 66, 106, 109, -1,
	66, 106, -1, 66, -1, 12, -1, 71, -1, 106,
	89, 142, 170, -1, 106, 89, 142, 106, -1, 143,
	-1, 144, 70, 143, -1, -1, 99, 146, -1, 170,
	-1, 106, -1, 146, 95, 170, -1, 146, 95, 106,
	-1, -1, 67, 68, 106, 89, 142, 170, 148, 145,
	153, -1, -1, 67, 68, 106, 89, 142, 106, 149,
	145, 153, -1, 67, 68, 106, 90, 142, 170, 153,
	-1, 67, 68, 106, 90, 142, 106, 153, -1, 67,
	68, 106, 86, 153, -1, 67, 68, 150, 151, 153,
	-1, 67, 68, 143, 70, 144, 153, -1, 170, -1,
	106, -1, 150, 95, 170, -1, 150, 95, 106, -1,
	-1, 91, 152, -1, 92, 105, -1, 152, 95, 92,
	105, -1, -1, 69, 105, -1, 72, 69, 105, -1,
	73, 106, 89, 142, 170, 153, -1, 73, 106, 89,
	142, 106, 153, -1, 73, 106, 90, 142, 170, 153,
	-1, 73, 106, 90, 142, 106, 153, -1, 74, 106,
	108, -1, 75, 108, -1, 75, 76, -1, 75, 76,
	106, -1, 75, 76, 105, -1, 77, 158, -1, 106,
	-1, 158, 95, 106, -1, 78, 79, 106, -1, 78,
	80, 106, -1, 81, 105, -1, 82, 83, -1, 82,
	84, 106, -1, 82, 85, 106, -1, 82, 87, 106,
	-1, 82, 88, 106, 109, -1, 85, 96, 128, -1,
	84, 96, 128, -1, -1, 98, 164, 93, 139, 94,
	-1, 73, 128, 97, 170, 163, -1, 100, 106, 106,
	-1, 101, 106, 103, 107, -1, 101, 106, 102, 103,
	107, -1, 101, 106, 104, 107, -1, 101, 106, 102,
	104, 107, -1, 11, 169, -1, -1, 169, 171, -1,
	43, -1, 44, -1, 45, -1, 46, -1, 47, -1,
	48, -1, 49, -1, 50, -1, 51, -1, 52, -1,
	53, -1, 54, -1, 55, -1, 56, -1, 57, -1,
	58, -1, 59, -1, 60, -1, 61, -1, 62, -1,
	63, -1, 106, -1, 107, -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
	0, 211, 211, 212, 216, 217, 218, 219, 220, 233,
	232, 242, 244, 248, 249, 250, 251, 252, 253, 254,
	267, 271, 278, 285, 291, 298, 305, 312, 325, 331,
	341, 347, 357, 367, 373, 384, 383, 400, 402, 411,
	412, 413, 414, 415, 419, 424, 428, 434, 436, 455,
	456, 465, 482, 481, 489, 488, 496, 498, 502, 507,
	512, 516, 520, 526, 531, 535, 540, 544, 548, 552,
	556, 560, 564, 568, 574, 580, 585, 590, 595, 603,
	609, 615, 629, 650, 657, 668, 686, 701, 704, 712,
	713, 714, 715, 716, 717, 718, 719, 720, 721, 722,
	723, 724, 738, 745, 751, 758, 764, 772, 778, 805,
	805, 816, 831, 849, 850, 865, 867, 871, 879, 887,
	894, 906, 905, 917, 916, 927, 936, 945, 952, 966,
	981, 987, 994, 1000, 1013, 1015, 1019, 1024, 1032, 1033,
	1034, 1045, 1053, 1061, 1069, 1087, 1102, 1109, 1113, 1119,
	1132, 1140, 1148, 1163, 1169, 1182, 1196, 1200, 1206, 1212,
	1238, 1272, 1278, 1295, 1295, 1300, 1319, 1344, 1353, 1362,
	1371, 1387, 1390, 1392, 1414, 1415, 1416, 1417, 1418, 1419,
	1420, 1421, 1422, 1423, 1424, 1425, 1426, 1427, 1428, 1429,
	1430, 1431, 1432, 1433, 1434, 1442, 1443
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
	"T_WORKER", "T_ASYNC", "T_NO_MONITOR", "T_LAUNCH", "T_CREATE",
	"T_DEFERRED", "T_IMMEDIATE", "T_FALSE", "T_INITIALLY", "T_VOLUME",
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
	"T_SET", "T_LOGS", "T_NOT", "T_CONTAINS", "T_MATCHES", "T_INTEGER",
	"T_IDENT", "T_STRING", "T_BLOCK", "T_SHELL_ARGS", "$accept", "spec",
	"spec_item", "cluster_block", "@1", "cluster_item_list", "cluster_item",
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
	355, 356, 357, 358, 359, 360, 361, 362, 363, 364
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
	0, 110, 111, 111, 112, 112, 112, 112, 112, 114,
	113, 115, 115, 116, 116, 116, 116, 116, 116, 116,
	117, 117, 117, 117, 117, 117, 117, 117, 118, 118,
	119, 119, 120, 121, 121, 123, 122, 124, 124, 125,
	125, 125, 125, 125, 126, 126, 126, 127, 127, 128,
	128, 129, 131, 130, 132, 130, 133, 133, 134, 134,
	134, 134, 134, 134, 134, 134, 134, 134, 134, 134,
	134, 134, 134, 134, 134, 134, 134, 134, 134, 134,
	134, 134, 134, 135, 136, 137, 138, 139, 139, 140,
	140, 140, 140, 140, 140, 140, 140, 140, 140, 140,
	140, 140, 141, 141, 141, 141, 141, 141, 141, 142,
	142, 143, 143, 144, 144, 145, 145, 146, 146, 146,
	146, 148, 147, 149, 147, 147, 147, 147, 147, 147,
	150, 150, 150, 150, 151, 151, 152, 152, 153, 153,
	153, 154, 154, 154, 154, 155, 156, 156, 156, 156,
	157, 158, 158, 159, 159, 160, 161, 161, 161, 161,
	161, 162, 162, 164, 163, 165, 166, 167, 167, 167,
	167, 168, 169, 169, 170, 170, 170, 170, 170, 170,
	170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
	170, 170, 170, 170, 170, 171, 171
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
	0, 2, 1, 2, 1, 1, 1, 1, 1, 0,
	5, 0, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 3, 3, 3, 3, 4, 4, 6, 2, 2,
	2, 2, 2, 2, 2, 0, 6, 0, 2, 1,
	1, 1, 1, 1, 1, 2, 2, 0, 2, 1,
	1, 0, 0, 4, 0, 7, 0, 2, 1, 1,
	1, 1, 1, 2, 2, 4, 2, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 3, 3, 2, 2, 3, 3, 0, 2, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 3, 2, 3, 2, 3, 2, 1, 1,
	1, 4, 4, 1, 3, 0, 2, 1, 1, 3,
	3, 0, 9, 0, 9, 7, 7, 5, 5, 6,
	1, 1, 3, 3, 0, 2, 2, 4, 0, 2,
	3, 6, 6, 6, 6, 3, 2, 2, 3, 3,
	2, 1, 3, 3, 3, 2, 2, 3, 3, 3,
	4, 3, 3, 0, 5, 5, 3, 4, 5, 4,
	5, 2, 0, 2, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
 * STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
 * means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
	0, 0, 0, 0, 0, 172, 0, 2, 4, 5,
	6, 7, 8, 9, 87, 83, 84, 195, 196, 0,
	171, 1, 3, 11, 0, 85, 173, 0, 0, 0,
	108, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 86, 0, 0, 88, 89, 90, 91, 92, 93,
	94, 95, 96, 97, 98, 99, 100, 101, 20, 0,
	0, 0, 0, 35, 0, 19, 10, 12, 13, 14,
	17, 15, 16, 18, 103, 105, 107, 0, 50, 49,
	0, 0, 147, 146, 151, 150, 0, 0, 155, 156,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 29, 28, 32, 33, 34, 37, 30,
	31, 102, 104, 106, 174, 175, 176, 177, 178, 179,
	180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
	190, 191, 192, 193, 194, 131, 0, 134, 130, 0,
	0, 0, 145, 149, 148, 0, 153, 154, 157, 158,
	159, 0, 49, 162, 161, 166, 0, 0, 0, 22,
	23, 24, 21, 0, 0, 0, 138, 0, 0, 0,
	0, 0, 138, 109, 110, 0, 0, 0, 152, 160,
	0, 0, 167, 169, 25, 26, 42, 43, 41, 0,
	0, 47, 39, 40, 44, 38, 0, 0, 127, 0,
	0, 0, 113, 138, 0, 135, 133, 132, 128, 138,
	138, 138, 138, 163, 165, 168, 170, 0, 45, 46,
	0, 139, 0, 123, 121, 138, 138, 0, 0, 129,
	136, 0, 142, 141, 144, 143, 0, 27, 0, 36,
	51, 48, 140, 115, 115, 126, 125, 0, 114, 0,
	87, 51, 52, 0, 138, 138, 112, 111, 137, 0,
	54, 56, 118, 116, 117, 124, 122, 164, 0, 53,
	0, 56, 0, 0, 0, 58, 59, 60, 61, 0,
	0, 62, 67, 0, 68, 69, 0, 0, 0, 0,
	0, 0, 0, 0, 57, 120, 119, 0, 75, 76,
	77, 63, 66, 64, 0, 0, 70, 72, 80, 73,
	74, 78, 79, 71, 55, 0, 81, 82, 65
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
	-1, 6, 7, 8, 23, 27, 67, 68, 69, 70,
	71, 72, 73, 108, 165, 194, 195, 220, 80, 252,
	241, 261, 268, 269, 294, 9, 10, 11, 15, 24,
	44, 45, 175, 136, 203, 254, 263, 46, 244, 243,
	137, 172, 205, 198, 47, 48, 49, 50, 85, 51,
	52, 53, 54, 214, 236, 55, 56, 57, 12, 20,
	138, 19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
 * STATE-NUM.  */
#define YYPACT_NINF -157
static const yytype_int16 yypact[] =
{
	75, -77, -50, -50, -80, -157, 46, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -157, -50,
	-80, -157, -157, -157, 396, -157, -157, 6, -55, -39,
	-25, 21, 3, -14, -58, -9, 8, -42, -43, 7,
	15, -157, -7, 9, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -2, -11,
	22, 27, 28, -157, -5, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, 34, 35, 41, 139, -157, 24,
	62, 52, 11, -157, -157, 23, 63, 64, -157, -157,
	70, 98, 99, 100, 4, 4, 102, -33, 125, 127,
	96, 128, 36, -157, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -61, 165, -29, -157, 2,
	2, 497, -157, -157, -157, 130, -157, -157, -157, -157,
	-157, 129, -157, -157, -157, -157, 18, 132, 133, -157,
	-157, -157, -157, 211, 155, 1, -8, 2, 2, 136,
	151, 167, -8, -157, -157, 203, 231, 146, -157, -157,
	160, 161, -157, -157, 234, -157, -157, -157, -157, 166,
	242, -157, -157, -157, -157, -157, 190, 227, -157, 267,
	295, 183, -157, 10, 192, 204, -157, -157, -157, -8,
	-8, -8, -8, -157, -157, -157, -157, 191, -157, -157,
	-1, -157, 195, 232, 233, -8, -8, 2, 136, -157,
	-157, 209, -157, -157, -157, -157, 212, -157, 198, -157,
	-157, -157, -157, 207, 207, -157, -157, 331, -157, 202,
	-157, -157, -157, 359, -8, -8, -157, -157, -157, 438,
	-157, -157, -157, 213, -157, -157, -157, -157, 238, 141,
	395, -157, 226, 228, 229, -157, -157, -157, -157, 97,
	-17, -157, -157, 230, -157, -157, 254, 255, 256, 258,
	259, 260, 261, 257, -157, -157, -157, 115, -157, -157,
	-157, -157, -157, -157, 309, 19, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, 335, -157, -157, -157
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
	-157, -157, 361, -157, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -93, 118,
	-157, -157, -157, 101, -157, -157, -157, -157, 12, 120,
	-157, -157, -129, -156, -157, 152, -157, -157, -157, -157,
	-157, -157, -157, -135, -157, -157, -157, -157, -157, -157,
	-157, -157, -157, -157, -157, -157, -157, -157, -157, -157,
	-141, 351
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
 * positive, shift that token.  If negative, reduce the rule which
 * number is the opposite.  If zero, do what YYDEFACT says.
 * If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -113
static const yytype_int16 yytable[] =
{
	177, 153, 154, 78, 238, 186, 187, 78, 78, 303,
	58, 176, 98, 202, 173, 16, 13, 188, 82, 59,
	189, 60, 61, 62, 63, 166, 17, 18, 167, 168,
	207, 25, 99, 100, 210, 212, 101, 208, 199, 200,
	89, 90, 91, 14, 92, 93, 21, 64, 65, 1,
	83, 74, 190, 304, 2, 3, 4, 5, 224, 226,
	163, 196, 170, 88, 197, 164, 171, 75, 229, 156,
	157, 158, 248, 174, 232, 233, 234, 235, 1, 196,
	228, 76, 197, 2, 3, 4, 5, 86, 87, 77,
	245, 246, 81, 239, 191, 103, 104, 84, 247, 96,
	66, 109, 110, 94, 102, 152, 257, 192, 193, 79,
	152, 95, 264, 139, 140, 97, 143, 144, 145, 265,
	266, 180, 181, 301, 302, 316, 317, 240, 105, 296,
	272, 273, 274, 106, 107, 275, 276, 277, 278, 279,
	280, 281, 282, 111, 112, 283, 284, 285, 286, 287,
	113, 288, 289, 290, 291, 292, 272, 273, 274, 141,
	142, 275, 276, 277, 278, 279, 280, 281, 282, 146,
	147, 283, 284, 285, 286, 287, 148, 288, 289, 290,
	291, 292, 114, 115, 116, 117, 118, 119, 120, 121,
	122, 123, 124, 125, 126, 127, 128, 129, 130, 131,
	132, 133, 134, 161, 149, 150, 151, 293, 155, 314,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123,
	124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
	134, 159, 160, 293, 162, 169, 178, 184, 179, 182,
	183, 185, 201, 204, 213, 135, 114, 115, 116, 117,
	118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
	128, 129, 130, 131, 132, 133, 134, 215, 216, 217,
	219, 218, 227, 206, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 221, 222, 230, 237, 231,
	242, 249, -112, -111, 251, 250, 253, 258, 270, 209,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123,
	124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
	134, 271, 298, 315, 299, 300, 305, 211, 114, 115,
	116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
	126, 127, 128, 129, 130, 131, 132, 133, 134, 306,
	307, 318, 313, 308, 309, 310, 311, 22, 312, 260,
	259, 26, 297, 223, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 0, 255, 0, 0, 0,
	0, 225, 114, 115, 116, 117, 118, 119, 120, 121,
	122, 123, 124, 125, 126, 127, 128, 129, 130, 131,
	132, 133, 134, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 256, 114, 115,
	116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
	126, 127, 128, 129, 130, 131, 132, 133, 134, 0,
	28, 29, 30, 31, 0, 262, 0, 0, 0, 32,
	33, 34, 0, 35, 36, 0, 0, 37, 38, 0,
	39, 40, 0, 0, 0, 0, 0, 0, 0, 0,
	41, 0, 0, 0, 0, 0, 42, 43, 0, 0,
	0, 295, 28, 29, 30, 31, 0, 0, 0, 0,
	0, 32, 33, 34, 0, 35, 36, 0, 0, 37,
	38, 0, 39, 40, 0, 0, 0, 0, 0, 0,
	0, 0, 267, 0, 0, 0, 0, 0, 42, 43,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123,
	124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
	134
};

static const yytype_int16 yycheck[] =
{
	141, 94, 95, 4, 5, 4, 5, 4, 4, 26,
	4, 140, 14, 169, 12, 3, 93, 16, 76, 13,
	19, 15, 16, 17, 18, 86, 106, 107, 89, 90,
	171, 19, 34, 35, 175, 176, 38, 172, 167, 168,
	83, 84, 85, 93, 87, 88, 0, 41, 42, 3,
	108, 106, 51, 70, 8, 9, 10, 11, 199, 200,
	24, 69, 91, 105, 72, 29, 95, 106, 203, 102,
	103, 104, 228, 71, 209, 210, 211, 212, 3, 69,
	70, 106, 72, 8, 9, 10, 11, 79, 80, 68,
	225, 226, 106, 94, 93, 106, 107, 106, 227, 106,
	94, 106, 107, 96, 106, 106, 247, 106, 107, 106,
	106, 96, 253, 89, 90, 106, 105, 106, 95, 254,
	255, 103, 104, 26, 27, 106, 107, 220, 106, 270,
	15, 16, 17, 106, 106, 20, 21, 22, 23, 24,
	25, 26, 27, 109, 109, 30, 31, 32, 33, 34,
	109, 36, 37, 38, 39, 40, 15, 16, 17, 97,
	108, 20, 21, 22, 23, 24, 25, 26, 27, 106,
	106, 30, 31, 32, 33, 34, 106, 36, 37, 38,
	39, 40, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, 107, 106, 106, 106, 92, 106, 94,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
	63, 106, 105, 92, 106, 70, 106, 26, 109, 107,
	107, 86, 106, 92, 98, 106, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 62, 63, 107, 107, 35,
	28, 105, 89, 106, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 63, 105, 69, 105, 107, 95,
	105, 92, 70, 70, 106, 93, 99, 105, 95, 106,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
	63, 93, 106, 24, 106, 106, 106, 106, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62, 63, 105,
	105, 26, 105, 107, 106, 106, 106, 6, 107, 251,
	250, 20, 271, 106, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 63, -1, 244, -1, -1, -1,
	-1, 106, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, 106, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62, 63, -1,
	64, 65, 66, 67, -1, 106, -1, -1, -1, 73,
	74, 75, -1, 77, 78, -1, -1, 81, 82, -1,
	84, 85, -1, -1, -1, -1, -1, -1, -1, -1,
	94, -1, -1, -1, -1, -1, 100, 101, -1, -1,
	-1, 106, 64, 65, 66, 67, -1, -1, -1, -1,
	-1, 73, 74, 75, -1, 77, 78, -1, -1, 81,
	82, -1, 84, 85, -1, -1, -1, -1, -1, -1,
	-1, -1, 94, -1, -1, -1, -1, -1, 100, 101,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
	63
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
 * symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
	0, 3, 8, 9, 10, 11, 111, 112, 113, 135,
	136, 137, 168, 93, 93, 138, 138, 106, 107, 171,
	169, 0, 112, 114, 139, 138, 171, 115, 64, 65,
	66, 67, 73, 74, 75, 77, 78, 81, 82, 84,
	85, 94, 100, 101, 140, 141, 147, 154, 155, 156,
	157, 159, 160, 161, 162, 165, 166, 167, 4, 13,
	15, 16, 17, 18, 41, 42, 94, 116, 117, 118,
	119, 120, 121, 122, 106, 106, 106, 68, 4, 106,
	128, 106, 76, 108, 106, 158, 79, 80, 105, 83,
	84, 85, 87, 88, 96, 96, 106, 106, 14, 34,
	35, 38, 106, 106, 107, 106, 106, 106, 123, 106,
	107, 109, 109, 109, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 63, 106, 143, 150, 170, 89,
	90, 97, 108, 105, 106, 95, 106, 106, 106, 106,
	106, 106, 106, 128, 128, 106, 102, 103, 104, 106,
	105, 107, 106, 24, 29, 124, 86, 89, 90, 70,
	91, 95, 151, 12, 71, 142, 142, 170, 106, 109,
	103, 104, 107, 107, 26, 86, 4, 5, 16, 19,
	51, 93, 106, 107, 125, 126, 69, 72, 153, 142,
	142, 106, 143, 144, 92, 152, 106, 170, 153, 106,
	170, 106, 170, 98, 163, 107, 107, 35, 105, 28,
	127, 105, 69, 106, 170, 106, 170, 89, 70, 153,
	105, 95, 153, 153, 153, 153, 164, 107, 5, 94,
	128, 130, 105, 149, 148, 153, 153, 142, 143, 92,
	93, 106, 129, 99, 145, 145, 106, 170, 105, 139,
	129, 131, 106, 146, 170, 153, 153, 94, 132, 133,
	95, 93, 15, 16, 17, 20, 21, 22, 23, 24,
	25, 26, 27, 30, 31, 32, 33, 34, 36, 37,
	38, 39, 40, 92, 134, 106, 170, 133, 106, 106,
	106, 26, 27, 26, 70, 106, 105, 105, 107, 106,
	106, 106, 107, 105, 94, 24, 106, 107, 26
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
#line 233 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, "self-signed",
						sizeof(current_spec->cluster.ssl));
				strlcpy(current_spec->cluster.auth, "trust",
						sizeof(current_spec->cluster.auth));
			}
			break;

		case 19:
#line 254 "test_spec_parse.y"
			{
				current_spec->cluster.bindSource = true;
			}
			break;

		case 20:
#line 268 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
			}
			break;

		case 21:
#line 272 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) -
																		   (3)].str),
						sizeof(current_spec->cluster.monitorDebianCluster));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 22:
#line 279 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorImageTarget));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 23:
#line 286 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;

				/* monitor port not stored in TestCluster yet; ignore */
				(void) (yyvsp[(3) - (3)].ival);
			}
			break;

		case 24:
#line 292 "test_spec_parse.y"
			{
				current_spec->cluster.withMonitor = true;
				strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
						sizeof(current_spec->cluster.monitorPassword));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 25:
#line 299 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 26:
#line 306 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
						sizeof(current_spec->cluster.secondMonitorName));
				current_spec->cluster.secondMonitorStopped = true;
				free((yyvsp[(2) - (4)].str));
			}
			break;

		case 27:
#line 313 "test_spec_parse.y"
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
#line 326 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 29:
#line 332 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.image));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 30:
#line 342 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 31:
#line 348 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.extensionVersion));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 32:
#line 358 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 33:
#line 368 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 34:
#line 374 "test_spec_parse.y"
			{
				strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
						sizeof(current_spec->cluster.auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 35:
#line 384 "test_spec_parse.y"
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
#line 411 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 40:
#line 412 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 41:
#line 413 "test_spec_parse.y"
			{
				(yyval.str) = strdup("auth");
			}
			break;

		case 42:
#line 414 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 43:
#line 415 "test_spec_parse.y"
			{
				(yyval.str) = strdup("node");
			}
			break;

		case 44:
#line 420 "test_spec_parse.y"
			{
				strlcpy(current_formation->name, (yyvsp[(1) - (1)].str),
						sizeof(current_formation->name));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 45:
#line 425 "test_spec_parse.y"
			{
				current_formation->numSync = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 46:
#line 429 "test_spec_parse.y"
			{
				current_formation->disableSecondary = true;
			}
			break;

		case 49:
#line 455 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 50:
#line 456 "test_spec_parse.y"
			{
				(yyval.str) = strdup("monitor");
			}
			break;

		case 51:
#line 465 "test_spec_parse.y"
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

		case 52:
#line 482 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(1) - (2)].str),
						sizeof(current_node->name));
				free((yyvsp[(1) - (2)].str));
			}
			break;

		case 54:
#line 489 "test_spec_parse.y"
			{
				strlcpy(current_node->name, (yyvsp[(2) - (3)].str),
						sizeof(current_node->name));
				free((yyvsp[(2) - (3)].str));
			}
			break;

		case 58:
#line 503 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_COORDINATOR;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 59:
#line 508 "test_spec_parse.y"
			{
				current_node->kind = NODE_KIND_CITUS_WORKER;
				current_spec->cluster.withCitus = true;
			}
			break;

		case 60:
#line 513 "test_spec_parse.y"
			{
				current_node->replicationQuorum = false;
			}
			break;

		case 61:
#line 517 "test_spec_parse.y"
			{
				current_node->noMonitor = true;
			}
			break;

		case 62:
#line 521 "test_spec_parse.y"
			{
				/* bare "deferred" = create and launch deferred (both gates) */
				current_node->createDeferred = true;
				current_node->launchDeferred = true;
			}
			break;

		case 63:
#line 527 "test_spec_parse.y"
			{
				/* "launch deferred" alone = run-deferred only, create immediate */
				current_node->launchDeferred = true;
			}
			break;

		case 64:
#line 532 "test_spec_parse.y"
			{
				current_node->createDeferred = true;
			}
			break;

		case 65:
#line 536 "test_spec_parse.y"
			{
				current_node->createDeferred = true;
				current_node->launchDeferred = true;
			}
			break;

		case 66:
#line 541 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 67:
#line 545 "test_spec_parse.y"
			{
				current_node->launchDeferred = false;
			}
			break;

		case 68:
#line 549 "test_spec_parse.y"
			{
				current_node->listen = true;
			}
			break;

		case 69:
#line 553 "test_spec_parse.y"
			{
				current_node->citusSecondary = true;
			}
			break;

		case 70:
#line 557 "test_spec_parse.y"
			{
				current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 71:
#line 561 "test_spec_parse.y"
			{
				current_node->group = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 72:
#line 565 "test_spec_parse.y"
			{
				current_node->pgPort = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 73:
#line 569 "test_spec_parse.y"
			{
				strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
						sizeof(current_node->citusClusterName));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 74:
#line 575 "test_spec_parse.y"
			{
				strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
						sizeof(current_node->debianCluster));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 75:
#line 581 "test_spec_parse.y"
			{
				strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str),
						sizeof(current_node->ssl));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 76:
#line 586 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 77:
#line 591 "test_spec_parse.y"
			{
				strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
						sizeof(current_node->auth));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 78:
#line 596 "test_spec_parse.y"
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

		case 79:
#line 604 "test_spec_parse.y"
			{
				strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->replicationPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 80:
#line 610 "test_spec_parse.y"
			{
				strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
						sizeof(current_node->monitorPassword));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 81:
#line 616 "test_spec_parse.y"
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

		case 82:
#line 630 "test_spec_parse.y"
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

		case 83:
#line 651 "test_spec_parse.y"
			{
				current_spec->setup = (yyvsp[(2) - (2)].step);
			}
			break;

		case 84:
#line 658 "test_spec_parse.y"
			{
				current_spec->teardown = (yyvsp[(2) - (2)].step);
			}
			break;

		case 85:
#line 669 "test_spec_parse.y"
			{
				TestStep *s = (yyvsp[(3) - (3)].step);
				strncpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name) - 1);
				free((yyvsp[(2) - (3)].str));
				register_step(current_spec, s);
			}
			break;

		case 86:
#line 687 "test_spec_parse.y"
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

		case 87:
#line 701 "test_spec_parse.y"
			{
				(yyval.step) = make_step("");
			}
			break;

		case 88:
#line 705 "test_spec_parse.y"
			{
				if ((yyvsp[(2) - (2)].cmd))
				{
					append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
				}
				(yyval.step) = (yyvsp[(1) - (2)].step);
			}
			break;

		case 89:
#line 712 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 90:
#line 713 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 91:
#line 714 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 92:
#line 715 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 93:
#line 716 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 94:
#line 717 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 95:
#line 718 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 96:
#line 719 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 97:
#line 720 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 98:
#line 721 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 99:
#line 722 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 100:
#line 723 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 101:
#line 724 "test_spec_parse.y"
			{
				(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			}
			break;

		case 102:
#line 739 "test_spec_parse.y"
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

		case 103:
#line 746 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 104:
#line 752 "test_spec_parse.y"
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

		case 105:
#line 759 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
				strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 106:
#line 765 "test_spec_parse.y"
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

		case 107:
#line 773 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
				strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->args));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 108:
#line 779 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			}
			break;

		case 111:
#line 817 "test_spec_parse.y"
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

		case 112:
#line 832 "test_spec_parse.y"
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

		case 117:
#line 872 "test_spec_parse.y"
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

		case 118:
#line 880 "test_spec_parse.y"
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

		case 119:
#line 888 "test_spec_parse.y"
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

		case 120:
#line 895 "test_spec_parse.y"
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

		case 121:
#line 906 "test_spec_parse.y"
			{
				current_pass_cmd = make_cmd(CMD_WAIT_STATE);
				strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
						sizeof(current_pass_cmd->service));
				strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
						sizeof(current_pass_cmd->state));
				free((yyvsp[(3) - (6)].str));
			}
			break;

		case 122:
#line 911 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 123:
#line 917 "test_spec_parse.y"
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

		case 124:
#line 922 "test_spec_parse.y"
			{
				current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
				(yyval.cmd) = current_pass_cmd;
				current_pass_cmd = NULL;
			}
			break;

		case 125:
#line 928 "test_spec_parse.y"
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

		case 126:
#line 937 "test_spec_parse.y"
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

		case 127:
#line 946 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str),
						sizeof((yyval.cmd)->service));
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				free((yyvsp[(3) - (5)].str));
			}
			break;

		case 128:
#line 953 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 129:
#line 967 "test_spec_parse.y"
			{
				(yyval.cmd) = current_wait_cmd;
				(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
				current_wait_cmd = NULL;
			}
			break;

		case 130:
#line 982 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
			}
			break;

		case 131:
#line 988 "test_spec_parse.y"
			{
				current_wait_cmd = make_cmd(CMD_WAIT_STATES);
				strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
						(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
				free((yyvsp[(1) - (1)].str));
			}
			break;

		case 132:
#line 995 "test_spec_parse.y"
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

		case 133:
#line 1001 "test_spec_parse.y"
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

		case 136:
#line 1020 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(2) - (2)].ival);
				}
			}
			break;

		case 137:
#line 1025 "test_spec_parse.y"
			{
				if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
				{
					current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
						(yyvsp[(4) - (4)].ival);
				}
			}
			break;

		case 138:
#line 1032 "test_spec_parse.y"
			{
				(yyval.ival) = PGAF_TIMEOUT_DEFAULT;
			}
			break;

		case 139:
#line 1033 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 140:
#line 1034 "test_spec_parse.y"
			{
				(yyval.ival) = (yyvsp[(3) - (3)].ival);
			}
			break;

		case 141:
#line 1046 "test_spec_parse.y"
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

		case 142:
#line 1054 "test_spec_parse.y"
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

		case 143:
#line 1062 "test_spec_parse.y"
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

		case 144:
#line 1070 "test_spec_parse.y"
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

		case 145:
#line 1088 "test_spec_parse.y"
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

		case 146:
#line 1103 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT);
				strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str),
						sizeof((yyval.cmd)->expected));
				expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
				free((yyvsp[(2) - (2)].str));
			}
			break;

		case 147:
#line 1110 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			}
			break;

		case 148:
#line 1114 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->state));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 149:
#line 1120 "test_spec_parse.y"
			{
				/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
				(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
				snprintf((yyval.cmd)->state, sizeof((yyval.cmd)->state), "%d",
						 (yyvsp[(3) - (3)].ival));
			}
			break;

		case 150:
#line 1133 "test_spec_parse.y"
			{
				(yyval.cmd) = current_promote_cmd;
				current_promote_cmd = NULL;
			}
			break;

		case 151:
#line 1141 "test_spec_parse.y"
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

		case 152:
#line 1149 "test_spec_parse.y"
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

		case 153:
#line 1164 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 154:
#line 1170 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 155:
#line 1183 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_SLEEP);
				(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
			}
			break;

		case 156:
#line 1197 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
			}
			break;

		case 157:
#line 1201 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 158:
#line 1207 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 159:
#line 1213 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 160:
#line 1239 "test_spec_parse.y"
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

		case 161:
#line 1273 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 162:
#line 1279 "test_spec_parse.y"
			{
				(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
				strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
						sizeof((yyval.cmd)->service));
				free((yyvsp[(3) - (3)].str));
			}
			break;

		case 163:
#line 1295 "test_spec_parse.y"
			{
				pgaf_next_brace_is_while = 1;
			}
			break;

		case 164:
#line 1296 "test_spec_parse.y"
			{
				(yyval.step) = (yyvsp[(4) - (5)].step);
			}
			break;

		case 165:
#line 1301 "test_spec_parse.y"
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

		case 166:
#line 1320 "test_spec_parse.y"
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

		case 167:
#line 1345 "test_spec_parse.y"
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

		case 168:
#line 1354 "test_spec_parse.y"
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

		case 169:
#line 1363 "test_spec_parse.y"
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

		case 170:
#line 1372 "test_spec_parse.y"
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

		case 173:
#line 1393 "test_spec_parse.y"
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

		case 174:
#line 1414 "test_spec_parse.y"
			{
				(yyval.str) = "init";
			}
			break;

		case 175:
#line 1415 "test_spec_parse.y"
			{
				(yyval.str) = "single";
			}
			break;

		case 176:
#line 1416 "test_spec_parse.y"
			{
				(yyval.str) = "primary";
			}
			break;

		case 177:
#line 1417 "test_spec_parse.y"
			{
				(yyval.str) = "wait_primary";
			}
			break;

		case 178:
#line 1418 "test_spec_parse.y"
			{
				(yyval.str) = "wait_standby";
			}
			break;

		case 179:
#line 1419 "test_spec_parse.y"
			{
				(yyval.str) = "demoted";
			}
			break;

		case 180:
#line 1420 "test_spec_parse.y"
			{
				(yyval.str) = "demote_timeout";
			}
			break;

		case 181:
#line 1421 "test_spec_parse.y"
			{
				(yyval.str) = "draining";
			}
			break;

		case 182:
#line 1422 "test_spec_parse.y"
			{
				(yyval.str) = "secondary";
			}
			break;

		case 183:
#line 1423 "test_spec_parse.y"
			{
				(yyval.str) = "catchingup";
			}
			break;

		case 184:
#line 1424 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_promotion";
			}
			break;

		case 185:
#line 1425 "test_spec_parse.y"
			{
				(yyval.str) = "stop_replication";
			}
			break;

		case 186:
#line 1426 "test_spec_parse.y"
			{
				(yyval.str) = "maintenance";
			}
			break;

		case 187:
#line 1427 "test_spec_parse.y"
			{
				(yyval.str) = "join_primary";
			}
			break;

		case 188:
#line 1428 "test_spec_parse.y"
			{
				(yyval.str) = "apply_settings";
			}
			break;

		case 189:
#line 1429 "test_spec_parse.y"
			{
				(yyval.str) = "prepare_maintenance";
			}
			break;

		case 190:
#line 1430 "test_spec_parse.y"
			{
				(yyval.str) = "wait_maintenance";
			}
			break;

		case 191:
#line 1431 "test_spec_parse.y"
			{
				(yyval.str) = "report_lsn";
			}
			break;

		case 192:
#line 1432 "test_spec_parse.y"
			{
				(yyval.str) = "fast_forward";
			}
			break;

		case 193:
#line 1433 "test_spec_parse.y"
			{
				(yyval.str) = "join_secondary";
			}
			break;

		case 194:
#line 1434 "test_spec_parse.y"
			{
				(yyval.str) = "dropped";
			}
			break;

		case 195:
#line 1442 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;

		case 196:
#line 1443 "test_spec_parse.y"
			{
				(yyval.str) = (yyvsp[(1) - (1)].str);
			}
			break;


/* Line 1267 of yacc.c.  */
#line 3398 "test_spec_parse.c"
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


#line 1446 "test_spec_parse.y"


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
