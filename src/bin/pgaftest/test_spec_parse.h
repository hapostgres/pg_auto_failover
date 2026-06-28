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
     T_EXEC = 267,
     T_WAIT = 268,
     T_UNTIL = 269,
     T_TIMEOUT = 270,
     T_ASSERT = 271,
     T_SQL = 272,
     T_EXPECT = 273,
     T_NETWORK = 274,
     T_DISCONNECT = 275,
     T_CONNECT = 276,
     T_SLEEP = 277,
     T_COMPOSE = 278,
     T_DOWN = 279,
     T_STATE = 280,
     T_ASSIGNED_STATE = 281,
     T_CANDIDATE_PRIORITY = 282,
     T_GROUP = 283,
     T_ASYNC = 284,
     T_EQUALS = 285,
     T_INTEGER = 286,
     T_IDENT = 287,
     T_STRING = 288,
     T_BLOCK = 289
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
#define T_EXEC 267
#define T_WAIT 268
#define T_UNTIL 269
#define T_TIMEOUT 270
#define T_ASSERT 271
#define T_SQL 272
#define T_EXPECT 273
#define T_NETWORK 274
#define T_DISCONNECT 275
#define T_CONNECT 276
#define T_SLEEP 277
#define T_COMPOSE 278
#define T_DOWN 279
#define T_STATE 280
#define T_ASSIGNED_STATE 281
#define T_CANDIDATE_PRIORITY 282
#define T_GROUP 283
#define T_ASYNC 284
#define T_EQUALS 285
#define T_INTEGER 286
#define T_IDENT 287
#define T_STRING 288
#define T_BLOCK 289




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 398 "test_spec_parse.y"
{
	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;
}
/* Line 1529 of yacc.c.  */
#line 124 "test_spec_parse.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

