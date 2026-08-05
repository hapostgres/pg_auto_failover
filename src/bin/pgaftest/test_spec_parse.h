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




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 146 "test_spec_parse.y"
{
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}
/* Line 1529 of yacc.c.  */
#line 298 "test_spec_parse.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

