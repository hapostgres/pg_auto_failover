/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

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




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 145 "test_spec_parse.y"
{
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}
/* Line 1529 of yacc.c.  */
#line 284 "test_spec_parse.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

