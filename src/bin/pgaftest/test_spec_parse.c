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
	T_INTEGER = 358,
	T_IDENT = 359,
	T_STRING = 360,
	T_BLOCK = 361,
	T_SHELL_ARGS = 362
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
#define T_INTEGER 358
#define T_IDENT 359
#define T_STRING 360
#define T_BLOCK 361
#define T_SHELL_ARGS 362


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
	fprintf(/* IGNORE-BANNED */ stderr, "pgaftest: parse error at line %d: %s\n",
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
			memcpy(tmp + pos, row, l); /* IGNORE-BANNED */
			pos += l;
		}
		tmp[pos] = '\0';
		first = false;
	}

	if (!first)
	{
		strlcpy(buf, tmp, buflen);
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
#line 461 "test_spec_parse.c"
YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 474 "test_spec_parse.c"

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
#define YYLAST 523

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS 108

/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS 62

/* YYNRULES -- Number of rules.  */
#define YYNRULES 193

/* YYNRULES -- Number of states.  */
#define YYNSTATES 312

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK 2
#define YYMAXUTOK 362

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
	105, 106, 107
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
	265, 268, 272, 275, 279, 282, 284, 286, 288, 293,
	298, 300, 304, 305, 308, 310, 312, 316, 320, 321,
	331, 332, 342, 350, 358, 364, 370, 377, 379, 381,
	385, 389, 390, 393, 396, 401, 402, 405, 409, 416,
	423, 430, 437, 441, 444, 447, 451, 455, 458, 460,
	464, 468, 472, 475, 478, 482, 486, 490, 495, 499,
	503, 504, 510, 516, 520, 525, 531, 536, 542, 545,
	546, 549, 551, 553, 555, 557, 559, 561, 563, 565,
	567, 569, 571, 573, 575, 577, 579, 581, 583, 585,
	587, 589, 591, 593
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
	109, 0, -1, 110, -1, 109, 110, -1, 111, -1,
	133, -1, 134, -1, 135, -1, 166, -1, -1, 3,
	91, 112, 113, 92, -1, -1, 113, 114, -1, 115,
	-1, 116, -1, 118, -1, 119, -1, 117, -1, 120,
	-1, 40, -1, 4, -1, 4, 36, 104, -1, 4,
	14, 104, -1, 4, 32, 103, -1, 4, 33, 105,
	-1, 4, 104, 24, 25, -1, 4, 104, 27, 84,
	-1, 4, 104, 24, 25, 33, 105, -1, 13, 105,
	-1, 13, 104, -1, 39, 104, -1, 39, 105, -1,
	15, 104, -1, 16, 104, -1, 17, 104, -1, -1,
	18, 121, 122, 91, 125, 92, -1, -1, 122, 124,
	-1, 104, -1, 105, -1, 16, -1, 4, -1, 5,
	-1, 123, -1, 19, 103, -1, -1, 125, 128, -1,
	104, -1, 4, -1, -1, -1, 126, 127, 129, 131,
	-1, -1, 5, 104, 127, 130, 91, 131, 92, -1,
	-1, 131, 132, -1, 20, -1, 21, -1, 22, -1,
	23, -1, 25, -1, 24, 25, -1, 24, 26, -1,
	26, -1, 29, -1, 30, -1, 31, 103, -1, 90,
	103, -1, 32, 103, -1, 35, 104, -1, 36, 104,
	-1, 15, 104, -1, 16, 104, -1, 17, 104, -1,
	37, 104, -1, 38, 105, -1, 34, 105, -1, 28,
	104, 104, -1, 28, 104, 105, -1, 8, 136, -1,
	9, 136, -1, 10, 169, 136, -1, 91, 137, 92,
	-1, -1, 137, 138, -1, 139, -1, 145, -1, 152,
	-1, 153, -1, 154, -1, 155, -1, 157, -1, 158,
	-1, 159, -1, 160, -1, 163, -1, 164, -1, 165,
	-1, 62, 104, 107, -1, 62, 104, -1, 63, 104,
	107, -1, 63, 104, -1, 64, 104, 107, -1, 64,
	104, -1, 64, -1, 12, -1, 69, -1, 104, 87,
	140, 168, -1, 104, 87, 140, 104, -1, 141, -1,
	142, 68, 141, -1, -1, 97, 144, -1, 168, -1,
	104, -1, 144, 93, 168, -1, 144, 93, 104, -1,
	-1, 65, 66, 104, 87, 140, 168, 146, 143, 151,
	-1, -1, 65, 66, 104, 87, 140, 104, 147, 143,
	151, -1, 65, 66, 104, 88, 140, 168, 151, -1,
	65, 66, 104, 88, 140, 104, 151, -1, 65, 66,
	104, 84, 151, -1, 65, 66, 148, 149, 151, -1,
	65, 66, 141, 68, 142, 151, -1, 168, -1, 104,
	-1, 148, 93, 168, -1, 148, 93, 104, -1, -1,
	89, 150, -1, 90, 103, -1, 150, 93, 90, 103,
	-1, -1, 67, 103, -1, 70, 67, 103, -1, 71,
	104, 87, 140, 168, 151, -1, 71, 104, 87, 140,
	104, 151, -1, 71, 104, 88, 140, 168, 151, -1,
	71, 104, 88, 140, 104, 151, -1, 72, 104, 106,
	-1, 73, 106, -1, 73, 74, -1, 73, 74, 104,
	-1, 73, 74, 103, -1, 75, 156, -1, 104, -1,
	156, 93, 104, -1, 76, 77, 104, -1, 76, 78,
	104, -1, 79, 103, -1, 80, 81, -1, 80, 82,
	104, -1, 80, 83, 104, -1, 80, 85, 104, -1,
	80, 86, 104, 107, -1, 83, 94, 126, -1, 82,
	94, 126, -1, -1, 96, 162, 91, 137, 92, -1,
	71, 126, 95, 168, 161, -1, 98, 104, 104, -1,
	99, 104, 101, 105, -1, 99, 104, 100, 101, 105,
	-1, 99, 104, 102, 105, -1, 99, 104, 100, 102,
	105, -1, 11, 167, -1, -1, 167, 169, -1, 41,
	-1, 42, -1, 43, -1, 44, -1, 45, -1, 46,
	-1, 47, -1, 48, -1, 49, -1, 50, -1, 51,
	-1, 52, -1, 53, -1, 54, -1, 55, -1, 56,
	-1, 57, -1, 58, -1, 59, -1, 60, -1, 61,
	-1, 104, -1, 105, -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
	0, 211, 211, 212, 216, 217, 218, 219, 220, 233,
	232, 242, 244, 248, 249, 250, 251, 252, 253, 254,
	267, 271, 278, 285, 291, 298, 305, 312, 325, 331,
	341, 347, 357, 367, 373, 384, 383, 400, 402, 411,
	412, 413, 414, 415, 419, 424, 430, 432, 451, 452,
	461, 478, 477, 485, 484, 492, 494, 498, 503, 508,
	512, 516, 520, 524, 528, 532, 536, 540, 544, 548,
	552, 558, 564, 569, 574, 579, 587, 593, 599, 613,
	634, 641, 652, 670, 685, 688, 696, 697, 698, 699,
	700, 701, 702, 703, 704, 705, 706, 707, 708, 722,
	729, 735, 742, 748, 756, 762, 789, 789, 800, 815,
	833, 834, 849, 851, 855, 863, 871, 878, 890, 889,
	901, 900, 911, 920, 929, 936, 950, 965, 971, 978,
	984, 997, 999, 1003, 1008, 1016, 1017, 1018, 1029, 1037,
	1045, 1053, 1071, 1086, 1093, 1097, 1103, 1116, 1124, 1132,
	1147, 1153, 1166, 1180, 1184, 1190, 1196, 1222, 1256, 1262,
	1279, 1279, 1284, 1303, 1328, 1337, 1346, 1355, 1371, 1374,
	1376, 1398, 1399, 1400, 1401, 1402, 1403, 1404, 1405, 1406,
	1407, 1408, 1409, 1410, 1411, 1412, 1413, 1414, 1415, 1416,
	1417, 1418, 1426, 1427
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
	355, 356, 357, 358, 359, 360, 361, 362
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
	0, 108, 109, 109, 110, 110, 110, 110, 110, 112,
	111, 113, 113, 114, 114, 114, 114, 114, 114, 114,
	115, 115, 115, 115, 115, 115, 115, 115, 116, 116,
	117, 117, 118, 119, 119, 121, 120, 122, 122, 123,
	123, 123, 123, 123, 124, 124, 125, 125, 126, 126,
	127, 129, 128, 130, 128, 131, 131, 132, 132, 132,
	132, 132, 132, 132, 132, 132, 132, 132, 132, 132,
	132, 132, 132, 132, 132, 132, 132, 132, 132, 132,
	133, 134, 135, 136, 137, 137, 138, 138, 138, 138,
	138, 138, 138, 138, 138, 138, 138, 138, 138, 139,
	139, 139, 139, 139, 139, 139, 140, 140, 141, 141,
	142, 142, 143, 143, 144, 144, 144, 144, 146, 145,
	147, 145, 145, 145, 145, 145, 145, 148, 148, 148,
	148, 149, 149, 150, 150, 151, 151, 151, 152, 152,
	152, 152, 153, 154, 154, 154, 154, 155, 156, 156,
	157, 157, 158, 159, 159, 159, 159, 159, 160, 160,
	162, 161, 163, 164, 165, 165, 165, 165, 166, 167,
	167, 168, 168, 168, 168, 168, 168, 168, 168, 168,
	168, 168, 168, 168, 168, 168, 168, 168, 168, 168,
	168, 168, 169, 169
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
	1, 1, 1, 1, 1, 1, 1, 1, 1, 3,
	2, 3, 2, 3, 2, 1, 1, 1, 4, 4,
	1, 3, 0, 2, 1, 1, 3, 3, 0, 9,
	0, 9, 7, 7, 5, 5, 6, 1, 1, 3,
	3, 0, 2, 2, 4, 0, 2, 3, 6, 6,
	6, 6, 3, 2, 2, 3, 3, 2, 1, 3,
	3, 3, 2, 2, 3, 3, 3, 4, 3, 3,
	0, 5, 5, 3, 4, 5, 4, 5, 2, 0,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
 * STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
 * means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
	0, 0, 0, 0, 0, 169, 0, 2, 4, 5,
	6, 7, 8, 9, 84, 80, 81, 192, 193, 0,
	168, 1, 3, 11, 0, 82, 170, 0, 0, 0,
	105, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 83, 0, 0, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95, 96, 97, 98, 20, 0,
	0, 0, 0, 35, 0, 19, 10, 12, 13, 14,
	17, 15, 16, 18, 100, 102, 104, 0, 49, 48,
	0, 0, 144, 143, 148, 147, 0, 0, 152, 153,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 29, 28, 32, 33, 34, 37, 30,
	31, 99, 101, 103, 171, 172, 173, 174, 175, 176,
	177, 178, 179, 180, 181, 182, 183, 184, 185, 186,
	187, 188, 189, 190, 191, 128, 0, 131, 127, 0,
	0, 0, 142, 146, 145, 0, 150, 151, 154, 155,
	156, 0, 48, 159, 158, 163, 0, 0, 0, 22,
	23, 24, 21, 0, 0, 0, 135, 0, 0, 0,
	0, 0, 135, 106, 107, 0, 0, 0, 149, 157,
	0, 0, 164, 166, 25, 26, 42, 43, 41, 0,
	46, 39, 40, 44, 38, 0, 0, 124, 0, 0,
	0, 110, 135, 0, 132, 130, 129, 125, 135, 135,
	135, 135, 160, 162, 165, 167, 0, 45, 0, 136,
	0, 120, 118, 135, 135, 0, 0, 126, 133, 0,
	139, 138, 141, 140, 0, 27, 0, 36, 50, 47,
	137, 112, 112, 123, 122, 0, 111, 0, 84, 50,
	51, 0, 135, 135, 109, 108, 134, 0, 53, 55,
	115, 113, 114, 121, 119, 161, 0, 52, 0, 55,
	0, 0, 0, 57, 58, 59, 60, 0, 61, 64,
	0, 65, 66, 0, 0, 0, 0, 0, 0, 0,
	0, 56, 117, 116, 0, 72, 73, 74, 62, 63,
	0, 67, 69, 77, 70, 71, 75, 76, 68, 54,
	78, 79
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
	-1, 6, 7, 8, 23, 27, 67, 68, 69, 70,
	71, 72, 73, 108, 165, 193, 194, 218, 80, 250,
	239, 259, 266, 267, 291, 9, 10, 11, 15, 24,
	44, 45, 175, 136, 202, 252, 261, 46, 242, 241,
	137, 172, 204, 197, 47, 48, 49, 50, 85, 51,
	52, 53, 54, 213, 234, 55, 56, 57, 12, 20,
	138, 19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
 * STATE-NUM.  */
#define YYPACT_NINF -160
static const yytype_int16 yypact[] =
{
	45, -76, -65, -65, -45, -160, 109, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -160, -65,
	-45, -160, -160, -160, 145, -160, -160, 6, -72, -64,
	-57, -24, 3, -25, -62, -19, 24, -14, 47, 19,
	32, -160, -9, 27, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -5, 11,
	33, 53, 111, -160, 30, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, 106, 112, 126, 122, -160, 55,
	29, 128, 102, -160, -160, 58, 127, 131, -160, -160,
	132, 134, 135, 136, 4, 4, 137, 21, 138, 129,
	140, 142, 73, -160, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -51, 244, -75, -160, 17,
	17, 440, -160, -160, -160, 213, -160, -160, -160, -160,
	-160, 211, -160, -160, -160, -160, 110, 214, 215, -160,
	-160, -160, -160, 296, 241, 1, 44, 17, 17, 224,
	239, 143, 44, -160, -160, 207, 228, 240, -160, -160,
	230, 232, -160, -160, 305, -160, -160, -160, -160, 236,
	-160, -160, -160, -160, -160, 237, 274, -160, 249, 313,
	255, -160, 20, 242, 253, -160, -160, -160, 44, 44,
	44, 44, -160, -160, -160, -160, 243, -160, -1, -160,
	248, 276, 279, 44, 44, 17, 224, -160, -160, 262,
	-160, -160, -160, -160, 327, -160, 315, -160, -160, -160,
	-160, 323, 323, -160, -160, 334, -160, 318, -160, -160,
	-160, 355, 44, 44, -160, -160, -160, 251, -160, -160,
	-160, 329, -160, -160, -160, -160, 332, 124, 419, -160,
	320, 321, 322, -160, -160, -160, -160, 197, -160, -160,
	324, -160, -160, 326, 328, 325, 330, 331, 333, 335,
	336, -160, -160, -160, 46, -160, -160, -160, -160, -160,
	125, -160, -160, -160, -160, -160, -160, -160, -160, -160,
	-160, -160
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
	-160, -160, 421, -160, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -93, 183,
	-160, -160, -160, 164, -160, -160, -160, -160, 22, 188,
	-160, -160, -129, -153, -160, 199, -160, -160, -160, -160,
	-160, -160, -160, -159, -160, -160, -160, -160, -160, -160,
	-160, -160, -160, -160, -160, -160, -160, -160, -160, -160,
	-141, 422
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
 * positive, shift that token.  If negative, reduce the rule which
 * number is the opposite.  If zero, do what YYDEFACT says.
 * If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -110
static const yytype_int16 yytable[] =
{
	177, 153, 154, 78, 236, 186, 187, 78, 78, 98,
	58, 176, 82, 207, 170, 13, 201, 188, 171, 59,
	189, 60, 61, 62, 63, 16, 14, 99, 100, 173,
	206, 101, 74, 166, 209, 211, 167, 168, 198, 199,
	75, 25, 77, 227, 83, 64, 65, 76, 1, 230,
	231, 232, 233, 2, 3, 4, 5, 222, 224, 17,
	18, 270, 271, 272, 243, 244, 273, 274, 275, 276,
	277, 278, 279, 246, 280, 281, 282, 283, 284, 81,
	285, 286, 287, 288, 289, 84, 174, 195, 226, 88,
	196, 237, 190, 263, 264, 96, 245, 163, 66, 102,
	164, 86, 87, 152, 255, 191, 192, 79, 152, 21,
	262, 195, 1, 94, 196, 103, 104, 2, 3, 4,
	5, 156, 157, 158, 141, 238, 95, 293, 89, 90,
	91, 97, 92, 93, 109, 110, 290, 105, 309, 270,
	271, 272, 139, 140, 273, 274, 275, 276, 277, 278,
	279, 145, 280, 281, 282, 283, 284, 106, 285, 286,
	287, 288, 289, 114, 115, 116, 117, 118, 119, 120,
	121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
	131, 132, 133, 134, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 143, 144, 28, 29, 30,
	31, 180, 181, 111, 290, 107, 32, 33, 34, 112,
	35, 36, 298, 299, 37, 38, 135, 39, 40, 310,
	311, 146, 160, 113, 142, 147, 148, 41, 149, 150,
	151, 155, 159, 42, 43, 161, 162, 205, 114, 115,
	116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
	126, 127, 128, 129, 130, 131, 132, 133, 134, 114,
	115, 116, 117, 118, 119, 120, 121, 122, 123, 124,
	125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123,
	124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
	134, 208, 169, 28, 29, 30, 31, 178, 179, 182,
	183, 184, 32, 33, 34, 185, 35, 36, 200, 203,
	37, 38, 210, 39, 40, 214, 212, 215, 216, 217,
	219, 220, 225, 265, -109, 228, 229, -108, 235, 42,
	43, 240, 247, 221, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 114, 115, 116, 117, 118,
	119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
	129, 130, 131, 132, 133, 134, 114, 115, 116, 117,
	118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
	128, 129, 130, 131, 132, 133, 134, 223, 248, 249,
	251, 256, 268, 269, 295, 296, 297, 22, 300, 301,
	303, 302, 258, 294, 304, 305, 257, 306, 254, 308,
	307, 253, 26, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 260,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123,
	124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
	134, 114, 115, 116, 117, 118, 119, 120, 121, 122,
	123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
	133, 134, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 292
};

static const yytype_int16 yycheck[] =
{
	141, 94, 95, 4, 5, 4, 5, 4, 4, 14,
	4, 140, 74, 172, 89, 91, 169, 16, 93, 13,
	19, 15, 16, 17, 18, 3, 91, 32, 33, 12,
	171, 36, 104, 84, 175, 176, 87, 88, 167, 168,
	104, 19, 66, 202, 106, 39, 40, 104, 3, 208,
	209, 210, 211, 8, 9, 10, 11, 198, 199, 104,
	105, 15, 16, 17, 223, 224, 20, 21, 22, 23,
	24, 25, 26, 226, 28, 29, 30, 31, 32, 104,
	34, 35, 36, 37, 38, 104, 69, 67, 68, 103,
	70, 92, 91, 252, 253, 104, 225, 24, 92, 104,
	27, 77, 78, 104, 245, 104, 105, 104, 104, 0,
	251, 67, 3, 94, 70, 104, 105, 8, 9, 10,
	11, 100, 101, 102, 95, 218, 94, 268, 81, 82,
	83, 104, 85, 86, 104, 105, 90, 104, 92, 15,
	16, 17, 87, 88, 20, 21, 22, 23, 24, 25,
	26, 93, 28, 29, 30, 31, 32, 104, 34, 35,
	36, 37, 38, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
	58, 59, 60, 61, 41, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 103, 104, 62, 63, 64,
	65, 101, 102, 107, 90, 104, 71, 72, 73, 107,
	75, 76, 25, 26, 79, 80, 104, 82, 83, 104,
	105, 104, 103, 107, 106, 104, 104, 92, 104, 104,
	104, 104, 104, 98, 99, 105, 104, 104, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 55, 56, 57, 58, 59, 60, 61, 41,
	42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 104, 68, 62, 63, 64, 65, 104, 107, 105,
	105, 25, 71, 72, 73, 84, 75, 76, 104, 90,
	79, 80, 104, 82, 83, 105, 96, 105, 33, 103,
	103, 67, 87, 92, 68, 103, 93, 68, 105, 98,
	99, 103, 90, 104, 41, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 41, 42, 43, 44, 45,
	46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
	56, 57, 58, 59, 60, 61, 41, 42, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 104, 91, 104,
	97, 103, 93, 91, 104, 104, 104, 6, 104, 103,
	105, 103, 249, 269, 104, 104, 248, 104, 104, 103,
	105, 242, 20, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, 104,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
	60, 61, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, 104
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
 * symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
	0, 3, 8, 9, 10, 11, 109, 110, 111, 133,
	134, 135, 166, 91, 91, 136, 136, 104, 105, 169,
	167, 0, 110, 112, 137, 136, 169, 113, 62, 63,
	64, 65, 71, 72, 73, 75, 76, 79, 80, 82,
	83, 92, 98, 99, 138, 139, 145, 152, 153, 154,
	155, 157, 158, 159, 160, 163, 164, 165, 4, 13,
	15, 16, 17, 18, 39, 40, 92, 114, 115, 116,
	117, 118, 119, 120, 104, 104, 104, 66, 4, 104,
	126, 104, 74, 106, 104, 156, 77, 78, 103, 81,
	82, 83, 85, 86, 94, 94, 104, 104, 14, 32,
	33, 36, 104, 104, 105, 104, 104, 104, 121, 104,
	105, 107, 107, 107, 41, 42, 43, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 104, 141, 148, 168, 87,
	88, 95, 106, 103, 104, 93, 104, 104, 104, 104,
	104, 104, 104, 126, 126, 104, 100, 101, 102, 104,
	103, 105, 104, 24, 27, 122, 84, 87, 88, 68,
	89, 93, 149, 12, 69, 140, 140, 168, 104, 107,
	101, 102, 105, 105, 25, 84, 4, 5, 16, 19,
	91, 104, 105, 123, 124, 67, 70, 151, 140, 140,
	104, 141, 142, 90, 150, 104, 168, 151, 104, 168,
	104, 168, 96, 161, 105, 105, 33, 103, 125, 103,
	67, 104, 168, 104, 168, 87, 68, 151, 103, 93,
	151, 151, 151, 151, 162, 105, 5, 92, 126, 128,
	103, 147, 146, 151, 151, 140, 141, 90, 91, 104,
	127, 97, 143, 143, 104, 168, 103, 137, 127, 129,
	104, 144, 168, 151, 151, 92, 130, 131, 93, 91,
	15, 16, 17, 20, 21, 22, 23, 24, 25, 26,
	28, 29, 30, 31, 32, 34, 35, 36, 37, 38,
	90, 132, 104, 168, 131, 104, 104, 104, 25, 26,
	104, 103, 103, 105, 104, 104, 104, 105, 103, 92,
	104, 105
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
		fprintf(/* IGNORE-BANNED */ File, "%d.%d-%d.%d", \
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
		fprintf(stderr, "   $%d = ", yyi + 1); /* IGNORE-BANNED */
		yy_symbol_print(stderr, yyrhs[yyprhs[yyrule] + yyi],
						&(yyvsp[(yyi + 1) - (yynrhs)])
						);
		fprintf(stderr, "\n"); /* IGNORE-BANNED */
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
				{
					if (yyres)
					{
						yyres[yyn] = '\0';
					}
					return yyn;
				}
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
			break;
		}

		case 19:
#line 254 "test_spec_parse.y"
		{
			current_spec->cluster.bindSource = true;
			break;
		}

		case 20:
#line 268 "test_spec_parse.y"
		{
			current_spec->cluster.withMonitor = true;
			break;
		}

		case 21:
#line 272 "test_spec_parse.y"
		{
			current_spec->cluster.withMonitor = true;
			strlcpy(current_spec->cluster.monitorDebianCluster, (yyvsp[(3) -
																	   (3)].str),
					sizeof(current_spec->cluster.monitorDebianCluster));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 22:
#line 279 "test_spec_parse.y"
		{
			current_spec->cluster.withMonitor = true;
			strlcpy(current_spec->cluster.monitorImageTarget, (yyvsp[(3) - (3)].str),
					sizeof(current_spec->cluster.monitorImageTarget));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 23:
#line 286 "test_spec_parse.y"
		{
			current_spec->cluster.withMonitor = true;

			/* monitor port not stored in TestCluster yet; ignore */
			(void) (yyvsp[(3) - (3)].ival);
			break;
		}

		case 24:
#line 292 "test_spec_parse.y"
		{
			current_spec->cluster.withMonitor = true;
			strlcpy(current_spec->cluster.monitorPassword, (yyvsp[(3) - (3)].str),
					sizeof(current_spec->cluster.monitorPassword));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 25:
#line 299 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
					sizeof(current_spec->cluster.secondMonitorName));
			current_spec->cluster.secondMonitorStopped = true;
			free((yyvsp[(2) - (4)].str));
			break;
		}

		case 26:
#line 306 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (4)].str),
					sizeof(current_spec->cluster.secondMonitorName));
			current_spec->cluster.secondMonitorStopped = true;
			free((yyvsp[(2) - (4)].str));
			break;
		}

		case 27:
#line 313 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.secondMonitorName, (yyvsp[(2) - (6)].str),
					sizeof(current_spec->cluster.secondMonitorName));
			current_spec->cluster.secondMonitorStopped = true;
			free((yyvsp[(2) - (6)].str));

			/* password for second monitor not yet stored */
			free((yyvsp[(6) - (6)].str));
			break;
		}

		case 28:
#line 326 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.image));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 29:
#line 332 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.image, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.image));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 30:
#line 342 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.extensionVersion));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 31:
#line 348 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.extensionVersion, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.extensionVersion));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 32:
#line 358 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.ssl, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.ssl));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 33:
#line 368 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.auth));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 34:
#line 374 "test_spec_parse.y"
		{
			strlcpy(current_spec->cluster.auth, (yyvsp[(2) - (2)].str),
					sizeof(current_spec->cluster.auth));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 35:
#line 384 "test_spec_parse.y"
		{
			TestCluster *cl = &current_spec->cluster;
			if (cl->formationCount >= PGAF_MAX_FORMATIONS)
			{
				fprintf(/* IGNORE-BANNED */ stderr,
						"pgaftest: too many formations (max %d)\n",
						PGAF_MAX_FORMATIONS);
				exit(1);
			}
			current_formation = &cl->formations[cl->formationCount++];
			strlcpy(current_formation->name, "default",
					sizeof(current_formation->name));
			current_formation->numSync = -1;
			break;
		}

		case 39:
#line 411 "test_spec_parse.y"
		{
			(yyval.str) = (yyvsp[(1) - (1)].str);
			break;
		}

		case 40:
#line 412 "test_spec_parse.y"
		{
			(yyval.str) = (yyvsp[(1) - (1)].str);
			break;
		}

		case 41:
#line 413 "test_spec_parse.y"
		{
			(yyval.str) = strdup("auth");
			break;
		}

		case 42:
#line 414 "test_spec_parse.y"
		{
			(yyval.str) = strdup("monitor");
			break;
		}

		case 43:
#line 415 "test_spec_parse.y"
		{
			(yyval.str) = strdup("node");
			break;
		}

		case 44:
#line 420 "test_spec_parse.y"
		{
			strlcpy(current_formation->name, (yyvsp[(1) - (1)].str),
					sizeof(current_formation->name));
			free((yyvsp[(1) - (1)].str));
			break;
		}

		case 45:
#line 425 "test_spec_parse.y"
		{
			current_formation->numSync = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 48:
#line 451 "test_spec_parse.y"
		{
			(yyval.str) = (yyvsp[(1) - (1)].str);
			break;
		}

		case 49:
#line 452 "test_spec_parse.y"
		{
			(yyval.str) = strdup("monitor");
			break;
		}

		case 50:
#line 461 "test_spec_parse.y"
		{
			if (current_formation->nodeCount >= PGAF_MAX_NODES)
			{
				fprintf(/* IGNORE-BANNED */ stderr,
						"pgaftest: too many nodes in formation (max %d)\n",
						PGAF_MAX_NODES);
				exit(1);
			}
			current_node = &current_formation->nodes[current_formation->nodeCount++];
			current_node->kind = NODE_KIND_STANDALONE;
			current_node->candidatePriority = 50;
			current_node->replicationQuorum = true;
			break;
		}

		case 51:
#line 478 "test_spec_parse.y"
		{
			strlcpy(current_node->name, (yyvsp[(1) - (2)].str),
					sizeof(current_node->name));
			free((yyvsp[(1) - (2)].str));
			break;
		}

		case 53:
#line 485 "test_spec_parse.y"
		{
			strlcpy(current_node->name, (yyvsp[(2) - (3)].str),
					sizeof(current_node->name));
			free((yyvsp[(2) - (3)].str));
			break;
		}

		case 57:
#line 499 "test_spec_parse.y"
		{
			current_node->kind = NODE_KIND_CITUS_COORDINATOR;
			current_spec->cluster.withCitus = true;
			break;
		}

		case 58:
#line 504 "test_spec_parse.y"
		{
			current_node->kind = NODE_KIND_CITUS_WORKER;
			current_spec->cluster.withCitus = true;
			break;
		}

		case 59:
#line 509 "test_spec_parse.y"
		{
			current_node->replicationQuorum = false;
			break;
		}

		case 60:
#line 513 "test_spec_parse.y"
		{
			current_node->noMonitor = true;
			break;
		}

		case 61:
#line 517 "test_spec_parse.y"
		{
			current_node->launchDeferred = true;
			break;
		}

		case 62:
#line 521 "test_spec_parse.y"
		{
			current_node->launchDeferred = true;
			break;
		}

		case 63:
#line 525 "test_spec_parse.y"
		{
			current_node->launchDeferred = false;
			break;
		}

		case 64:
#line 529 "test_spec_parse.y"
		{
			current_node->launchDeferred = false;
			break;
		}

		case 65:
#line 533 "test_spec_parse.y"
		{
			current_node->listen = true;
			break;
		}

		case 66:
#line 537 "test_spec_parse.y"
		{
			current_node->citusSecondary = true;
			break;
		}

		case 67:
#line 541 "test_spec_parse.y"
		{
			current_node->candidatePriority = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 68:
#line 545 "test_spec_parse.y"
		{
			current_node->group = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 69:
#line 549 "test_spec_parse.y"
		{
			current_node->pgPort = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 70:
#line 553 "test_spec_parse.y"
		{
			strlcpy(current_node->citusClusterName, (yyvsp[(2) - (2)].str),
					sizeof(current_node->citusClusterName));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 71:
#line 559 "test_spec_parse.y"
		{
			strlcpy(current_node->debianCluster, (yyvsp[(2) - (2)].str),
					sizeof(current_node->debianCluster));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 72:
#line 565 "test_spec_parse.y"
		{
			strlcpy(current_node->ssl, (yyvsp[(2) - (2)].str),
					sizeof(current_node->ssl));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 73:
#line 570 "test_spec_parse.y"
		{
			strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
					sizeof(current_node->auth));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 74:
#line 575 "test_spec_parse.y"
		{
			strlcpy(current_node->auth, (yyvsp[(2) - (2)].str),
					sizeof(current_node->auth));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 75:
#line 580 "test_spec_parse.y"
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
			break;
		}

		case 76:
#line 588 "test_spec_parse.y"
		{
			strlcpy(current_node->replicationPassword, (yyvsp[(2) - (2)].str),
					sizeof(current_node->replicationPassword));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 77:
#line 594 "test_spec_parse.y"
		{
			strlcpy(current_node->monitorPassword, (yyvsp[(2) - (2)].str),
					sizeof(current_node->monitorPassword));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 78:
#line 600 "test_spec_parse.y"
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
			break;
		}

		case 79:
#line 614 "test_spec_parse.y"
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
			break;
		}

		case 80:
#line 635 "test_spec_parse.y"
		{
			current_spec->setup = (yyvsp[(2) - (2)].step);
			break;
		}

		case 81:
#line 642 "test_spec_parse.y"
		{
			current_spec->teardown = (yyvsp[(2) - (2)].step);
			break;
		}

		case 82:
#line 653 "test_spec_parse.y"
		{
			TestStep *s = (yyvsp[(3) - (3)].step);
			strlcpy(s->name, (yyvsp[(2) - (3)].str), sizeof(s->name));
			free((yyvsp[(2) - (3)].str));
			register_step(current_spec, s);
			break;
		}

		case 83:
#line 671 "test_spec_parse.y"
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
			break;
		}

		case 84:
#line 685 "test_spec_parse.y"
		{
			(yyval.step) = make_step("");
			break;
		}

		case 85:
#line 689 "test_spec_parse.y"
		{
			if ((yyvsp[(2) - (2)].cmd))
			{
				append_cmd((yyvsp[(1) - (2)].step), (yyvsp[(2) - (2)].cmd));
			}
			(yyval.step) = (yyvsp[(1) - (2)].step);
			break;
		}

		case 86:
#line 696 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 87:
#line 697 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 88:
#line 698 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 89:
#line 699 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 90:
#line 700 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 91:
#line 701 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 92:
#line 702 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 93:
#line 703 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 94:
#line 704 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 95:
#line 705 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 96:
#line 706 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 97:
#line 707 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 98:
#line 708 "test_spec_parse.y"
		{
			(yyval.cmd) = (yyvsp[(1) - (1)].cmd);
			break;
		}

		case 99:
#line 723 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXEC);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->args));
			free((yyvsp[(2) - (3)].str));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 100:
#line 730 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXEC);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 101:
#line 736 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->args));
			free((yyvsp[(2) - (3)].str));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 102:
#line 743 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXEC_FAILS);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (2)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 103:
#line 749 "test_spec_parse.y"
		{
			/* "pg_autoctl perform failover --formation auth"
			 * EXEC_ARGS returns T_IDENT for first word, T_SHELL_ARGS for rest */
			(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			sformat((yyval.cmd)->args, sizeof((yyval.cmd)->args), "%s %s",
					(yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
			free((yyvsp[(2) - (3)].str));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 104:
#line 757 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			strlcpy((yyval.cmd)->args, (yyvsp[(2) - (2)].str),
					sizeof((yyval.cmd)->args));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 105:
#line 763 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_PG_AUTOCTL);
			break;
		}

		case 108:
#line 801 "test_spec_parse.y"
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
			break;
		}

		case 109:
#line 816 "test_spec_parse.y"
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
			break;
		}

		case 114:
#line 856 "test_spec_parse.y"
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
			break;
		}

		case 115:
#line 864 "test_spec_parse.y"
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
			break;
		}

		case 116:
#line 872 "test_spec_parse.y"
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
			break;
		}

		case 117:
#line 879 "test_spec_parse.y"
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
			break;
		}

		case 118:
#line 890 "test_spec_parse.y"
		{
			current_pass_cmd = make_cmd(CMD_WAIT_STATE);
			strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
					sizeof(current_pass_cmd->service));
			strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
					sizeof(current_pass_cmd->state));
			free((yyvsp[(3) - (6)].str));
			break;
		}

		case 119:
#line 895 "test_spec_parse.y"
		{
			current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
			(yyval.cmd) = current_pass_cmd;
			current_pass_cmd = NULL;
			break;
		}

		case 120:
#line 901 "test_spec_parse.y"
		{
			current_pass_cmd = make_cmd(CMD_WAIT_STATE);
			strlcpy(current_pass_cmd->service, (yyvsp[(3) - (6)].str),
					sizeof(current_pass_cmd->service));
			strlcpy(current_pass_cmd->state, (yyvsp[(6) - (6)].str),
					sizeof(current_pass_cmd->state));
			free((yyvsp[(3) - (6)].str));
			free((yyvsp[(6) - (6)].str));
			break;
		}

		case 121:
#line 906 "test_spec_parse.y"
		{
			current_pass_cmd->timeoutSeconds = (yyvsp[(9) - (9)].ival);
			(yyval.cmd) = current_pass_cmd;
			current_pass_cmd = NULL;
			break;
		}

		case 122:
#line 912 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_WAIT_STATE);
			(yyval.cmd)->kind = CMD_ASSERT_ASSIGNED;
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (7)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->state, (yyvsp[(6) - (7)].str),
					sizeof((yyval.cmd)->state));
			(yyval.cmd)->timeoutSeconds = (yyvsp[(7) - (7)].ival);
			free((yyvsp[(3) - (7)].str));
			break;
		}

		case 123:
#line 921 "test_spec_parse.y"
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
			break;
		}

		case 124:
#line 930 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_WAIT_STOPPED);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (5)].str),
					sizeof((yyval.cmd)->service));
			(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
			free((yyvsp[(3) - (5)].str));
			break;
		}

		case 125:
#line 937 "test_spec_parse.y"
		{
			(yyval.cmd) = current_wait_cmd;
			(yyval.cmd)->timeoutSeconds = (yyvsp[(5) - (5)].ival);
			current_wait_cmd = NULL;
			break;
		}

		case 126:
#line 951 "test_spec_parse.y"
		{
			(yyval.cmd) = current_wait_cmd;
			(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
			current_wait_cmd = NULL;
			break;
		}

		case 127:
#line 966 "test_spec_parse.y"
		{
			current_wait_cmd = make_cmd(CMD_WAIT_STATES);
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
					(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
			break;
		}

		case 128:
#line 972 "test_spec_parse.y"
		{
			current_wait_cmd = make_cmd(CMD_WAIT_STATES);
			strlcpy(current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
					(yyvsp[(1) - (1)].str), sizeof(current_wait_cmd->waitStates[0]));
			free((yyvsp[(1) - (1)].str));
			break;
		}

		case 129:
#line 979 "test_spec_parse.y"
		{
			if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			{
				strlcpy(
					current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
					(yyvsp[(3) - (3)].str),
					sizeof(current_wait_cmd->waitStates[0]));
			}
			break;
		}

		case 130:
#line 985 "test_spec_parse.y"
		{
			if (current_wait_cmd->waitStateCount < PGAF_MAX_WAIT_STATES)
			{
				strlcpy(
					current_wait_cmd->waitStates[current_wait_cmd->waitStateCount++],
					(yyvsp[(3) - (3)].str),
					sizeof(current_wait_cmd->waitStates[0]));
			}
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 133:
#line 1004 "test_spec_parse.y"
		{
			if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			{
				current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
					(yyvsp[(2) - (2)].ival);
			}
			break;
		}

		case 134:
#line 1009 "test_spec_parse.y"
		{
			if (current_wait_cmd->waitGroupCount < PGAF_MAX_WAIT_GROUPS)
			{
				current_wait_cmd->waitGroups[current_wait_cmd->waitGroupCount++] =
					(yyvsp[(4) - (4)].ival);
			}
			break;
		}

		case 135:
#line 1016 "test_spec_parse.y"
		{
			(yyval.ival) = PGAF_TIMEOUT_DEFAULT;
			break;
		}

		case 136:
#line 1017 "test_spec_parse.y"
		{
			(yyval.ival) = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 137:
#line 1018 "test_spec_parse.y"
		{
			(yyval.ival) = (yyvsp[(3) - (3)].ival);
			break;
		}

		case 138:
#line 1030 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd((yyvsp[(6) - (6)].ival) > 0 ? CMD_WAIT_STATE :
								   CMD_ASSERT_STATE);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
					sizeof((yyval.cmd)->state));
			(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
			free((yyvsp[(2) - (6)].str));
			break;
		}

		case 139:
#line 1038 "test_spec_parse.y"
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
			break;
		}

		case 140:
#line 1046 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
					sizeof((yyval.cmd)->state));
			(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
			free((yyvsp[(2) - (6)].str));
			break;
		}

		case 141:
#line 1054 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_ASSERT_ASSIGNED);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (6)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->state, (yyvsp[(5) - (6)].str),
					sizeof((yyval.cmd)->state));
			(yyval.cmd)->timeoutSeconds = (yyvsp[(6) - (6)].ival);
			free((yyvsp[(2) - (6)].str));
			free((yyvsp[(5) - (6)].str));
			break;
		}

		case 142:
#line 1072 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_SQL);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (3)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->args, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->args));
			free((yyvsp[(2) - (3)].str));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 143:
#line 1087 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXPECT);
			strlcpy((yyval.cmd)->expected, (yyvsp[(2) - (2)].str),
					sizeof((yyval.cmd)->expected));
			expand_tuple_expect((yyval.cmd)->expected, sizeof((yyval.cmd)->expected));
			free((yyvsp[(2) - (2)].str));
			break;
		}

		case 144:
#line 1094 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			break;
		}

		case 145:
#line 1098 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			strlcpy((yyval.cmd)->state, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->state));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 146:
#line 1104 "test_spec_parse.y"
		{
			/* SQLSTATE codes like 25006 are all digits, lexed as T_INTEGER */
			(yyval.cmd) = make_cmd(CMD_EXPECT_ERROR);
			snprintf(/* IGNORE-BANNED */ (yyval.cmd)->state,
					 sizeof((yyval.cmd)->state), "%d",
					 (yyvsp[(3) - (3)].ival));
			break;
		}

		case 147:
#line 1117 "test_spec_parse.y"
		{
			(yyval.cmd) = current_promote_cmd;
			current_promote_cmd = NULL;
			break;
		}

		case 148:
#line 1125 "test_spec_parse.y"
		{
			current_promote_cmd = make_cmd(CMD_PROMOTE);
			current_promote_cmd->timeoutSeconds = PGAF_TIMEOUT_DEFAULT;
			strlcpy(
				current_promote_cmd->promoteNodes[current_promote_cmd->promoteCount++],
				(yyvsp[(1) - (1)].str),
				sizeof(current_promote_cmd->promoteNodes[0]));
			free((yyvsp[(1) - (1)].str));
			break;
		}

		case 149:
#line 1133 "test_spec_parse.y"
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
			break;
		}

		case 150:
#line 1148 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_NETWORK_OFF);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 151:
#line 1154 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_NETWORK_ON);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 152:
#line 1167 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_SLEEP);
			(yyval.cmd)->timeoutSeconds = (yyvsp[(2) - (2)].ival);
			break;
		}

		case 153:
#line 1181 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_COMPOSE_DOWN);
			break;
		}

		case 154:
#line 1185 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_COMPOSE_START);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 155:
#line 1191 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_COMPOSE_STOP);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 156:
#line 1197 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_COMPOSE_KILL);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 157:
#line 1223 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_COMPOSE_INJECT);
			strlcpy((yyval.cmd)->expected, (yyvsp[(3) - (4)].str),
					sizeof((yyval.cmd)->expected));                                             /* image */

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
				strlcpy((yyval.cmd)->service, svcdst, sizeof((yyval.cmd)->service));     /* dst svc  */
				strlcpy((yyval.cmd)->state, colon + 1, sizeof((yyval.cmd)->state));     /* dst path */
			}
			free((yyvsp[(3) - (4)].str));
			free((yyvsp[(4) - (4)].str));
			break;
		}

		case 158:
#line 1257 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_STOP_POSTGRES);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 159:
#line 1263 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_START_POSTGRES);
			strlcpy((yyval.cmd)->service, (yyvsp[(3) - (3)].str),
					sizeof((yyval.cmd)->service));
			free((yyvsp[(3) - (3)].str));
			break;
		}

		case 160:
#line 1279 "test_spec_parse.y"
		{
			pgaf_next_brace_is_while = 1;
			break;
		}

		case 161:
#line 1280 "test_spec_parse.y"
		{
			(yyval.step) = (yyvsp[(4) - (5)].step);
			break;
		}

		case 162:
#line 1285 "test_spec_parse.y"
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
			break;
		}

		case 163:
#line 1304 "test_spec_parse.y"
		{
			/* only "set monitor <svc>" is supported; $2 must be "monitor" */
			if (strcmp((yyvsp[(2) - (3)].str), "monitor") != 0)
			{
				fprintf(/* IGNORE-BANNED */ stderr,
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
			break;
		}

		case 164:
#line 1329 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str),
					sizeof((yyval.cmd)->args));
			(yyval.cmd)->logsNegate = false;
			(yyval.cmd)->allowError = false;     /* false = fixed string, true = PCRE */
			free((yyvsp[(2) - (4)].str));
			free((yyvsp[(4) - (4)].str));
			break;
		}

		case 165:
#line 1338 "test_spec_parse.y"
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
			break;
		}

		case 166:
#line 1347 "test_spec_parse.y"
		{
			(yyval.cmd) = make_cmd(CMD_LOGS_CHECK);
			strlcpy((yyval.cmd)->service, (yyvsp[(2) - (4)].str),
					sizeof((yyval.cmd)->service));
			strlcpy((yyval.cmd)->args, (yyvsp[(4) - (4)].str),
					sizeof((yyval.cmd)->args));
			(yyval.cmd)->logsNegate = false;
			(yyval.cmd)->allowError = true;     /* true = PCRE (-P) */
			free((yyvsp[(2) - (4)].str));
			free((yyvsp[(4) - (4)].str));
			break;
		}

		case 167:
#line 1356 "test_spec_parse.y"
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
			break;
		}

		case 170:
#line 1377 "test_spec_parse.y"
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
				fprintf(/* IGNORE-BANNED */ stderr,
						"pgaftest: too many steps in sequence (max %d)\n",
						PGAF_MAX_SEQ);
				exit(1);
			}
			break;
		}

		case 171:
#line 1398 "test_spec_parse.y"
		{
			(yyval.str) = "init";
			break;
		}

		case 172:
#line 1399 "test_spec_parse.y"
		{
			(yyval.str) = "single";
			break;
		}

		case 173:
#line 1400 "test_spec_parse.y"
		{
			(yyval.str) = "primary";
			break;
		}

		case 174:
#line 1401 "test_spec_parse.y"
		{
			(yyval.str) = "wait_primary";
			break;
		}

		case 175:
#line 1402 "test_spec_parse.y"
		{
			(yyval.str) = "wait_standby";
			break;
		}

		case 176:
#line 1403 "test_spec_parse.y"
		{
			(yyval.str) = "demoted";
			break;
		}

		case 177:
#line 1404 "test_spec_parse.y"
		{
			(yyval.str) = "demote_timeout";
			break;
		}

		case 178:
#line 1405 "test_spec_parse.y"
		{
			(yyval.str) = "draining";
			break;
		}

		case 179:
#line 1406 "test_spec_parse.y"
		{
			(yyval.str) = "secondary";
			break;
		}

		case 180:
#line 1407 "test_spec_parse.y"
		{
			(yyval.str) = "catchingup";
			break;
		}

		case 181:
#line 1408 "test_spec_parse.y"
		{
			(yyval.str) = "prepare_promotion";
			break;
		}

		case 182:
#line 1409 "test_spec_parse.y"
		{
			(yyval.str) = "stop_replication";
			break;
		}

		case 183:
#line 1410 "test_spec_parse.y"
		{
			(yyval.str) = "maintenance";
			break;
		}

		case 184:
#line 1411 "test_spec_parse.y"
		{
			(yyval.str) = "join_primary";
			break;
		}

		case 185:
#line 1412 "test_spec_parse.y"
		{
			(yyval.str) = "apply_settings";
			break;
		}

		case 186:
#line 1413 "test_spec_parse.y"
		{
			(yyval.str) = "prepare_maintenance";
			break;
		}

		case 187:
#line 1414 "test_spec_parse.y"
		{
			(yyval.str) = "wait_maintenance";
			break;
		}

		case 188:
#line 1415 "test_spec_parse.y"
		{
			(yyval.str) = "report_lsn";
			break;
		}

		case 189:
#line 1416 "test_spec_parse.y"
		{
			(yyval.str) = "fast_forward";
			break;
		}

		case 190:
#line 1417 "test_spec_parse.y"
		{
			(yyval.str) = "join_secondary";
			break;
		}

		case 191:
#line 1418 "test_spec_parse.y"
		{
			(yyval.str) = "dropped";
			break;
		}

		case 192:
#line 1426 "test_spec_parse.y"
		{
			(yyval.str) = (yyvsp[(1) - (1)].str);
			break;
		}

		case 193:
#line 1427 "test_spec_parse.y"
		{
			(yyval.str) = (yyvsp[(1) - (1)].str);
			break;
		}


/* Line 1267 of yacc.c.  */
#line 3360 "test_spec_parse.c"
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


#line 1430 "test_spec_parse.y"


/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */
TestSpec *
parse_test_spec(const char *filename)
{
	FILE *f = fopen(filename, "r"); /* IGNORE-BANNED */
	if (!f)
	{
		fprintf(/* IGNORE-BANNED */ stderr,
				"pgaftest: cannot open spec file \"%s\": %s\n",
				filename, strerror(errno) /* IGNORE-BANNED */);
		return NULL;
	}

	TestSpec *spec = (TestSpec *) calloc(1, sizeof(TestSpec));
	if (!spec)
	{
		fprintf(stderr, "out of memory\n"); /* IGNORE-BANNED */
		exit(1);
	}

	strlcpy(spec->filename, filename, sizeof(spec->filename));

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
		fprintf(stderr, "out of memory\n"); /* IGNORE-BANNED */
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
		fprintf(stderr, "out of memory\n"); /* IGNORE-BANNED */
		exit(1);
	}
	if (name)
	{
		strlcpy(s->name, name, sizeof(s->name));
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
