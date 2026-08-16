/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_TEST_SPEC_PARSE_H_INCLUDED
# define YY_YY_TEST_SPEC_PARSE_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    T_CLUSTER = 258,               /* T_CLUSTER  */
    T_MONITOR = 259,               /* T_MONITOR  */
    T_NODE = 260,                  /* T_NODE  */
    T_CITUS_COORDINATOR = 261,     /* T_CITUS_COORDINATOR  */
    T_CITUS_WORKER = 262,          /* T_CITUS_WORKER  */
    T_SETUP = 263,                 /* T_SETUP  */
    T_TEARDOWN = 264,              /* T_TEARDOWN  */
    T_STEP = 265,                  /* T_STEP  */
    T_SEQUENCE = 266,              /* T_SEQUENCE  */
    T_EQUALS = 267,                /* T_EQUALS  */
    T_IMAGE = 268,                 /* T_IMAGE  */
    T_IMAGE_TARGET = 269,          /* T_IMAGE_TARGET  */
    T_SSL = 270,                   /* T_SSL  */
    T_AUTH = 271,                  /* T_AUTH  */
    T_AUTH_METHOD = 272,           /* T_AUTH_METHOD  */
    T_FORMATION = 273,             /* T_FORMATION  */
    T_NUM_SYNC = 274,              /* T_NUM_SYNC  */
    T_COORDINATOR = 275,           /* T_COORDINATOR  */
    T_WORKER = 276,                /* T_WORKER  */
    T_ARCHIVER = 277,              /* T_ARCHIVER  */
    T_ASYNC = 278,                 /* T_ASYNC  */
    T_NO_MONITOR = 279,            /* T_NO_MONITOR  */
    T_SUSPENDED = 280,             /* T_SUSPENDED  */
    T_LAUNCH = 281,                /* T_LAUNCH  */
    T_CREATE = 282,                /* T_CREATE  */
    T_DEFERRED = 283,              /* T_DEFERRED  */
    T_IMMEDIATE = 284,             /* T_IMMEDIATE  */
    T_FALSE = 285,                 /* T_FALSE  */
    T_TRUE = 286,                  /* T_TRUE  */
    T_INITIALLY = 287,             /* T_INITIALLY  */
    T_VOLUME = 288,                /* T_VOLUME  */
    T_LISTEN = 289,                /* T_LISTEN  */
    T_CITUS_SECONDARY = 290,       /* T_CITUS_SECONDARY  */
    T_CANDIDATE_PRIORITY = 291,    /* T_CANDIDATE_PRIORITY  */
    T_PORT = 292,                  /* T_PORT  */
    T_PASSWORD = 293,              /* T_PASSWORD  */
    T_MONITOR_PASSWORD = 294,      /* T_MONITOR_PASSWORD  */
    T_CITUS_CLUSTER_NAME = 295,    /* T_CITUS_CLUSTER_NAME  */
    T_DEBIAN_CLUSTER = 296,        /* T_DEBIAN_CLUSTER  */
    T_REPLICATION_QUORUM = 297,    /* T_REPLICATION_QUORUM  */
    T_REPLICATION_PASSWORD = 298,  /* T_REPLICATION_PASSWORD  */
    T_EXTENSION_VERSION = 299,     /* T_EXTENSION_VERSION  */
    T_BIND_SOURCE = 300,           /* T_BIND_SOURCE  */
    T_LEGACY_STARTUP = 301,        /* T_LEGACY_STARTUP  */
    T_REGION = 302,                /* T_REGION  */
    T_NODEINI = 303,               /* T_NODEINI  */
    T_FS_INIT = 304,               /* T_FS_INIT  */
    T_FS_SINGLE = 305,             /* T_FS_SINGLE  */
    T_FS_PRIMARY = 306,            /* T_FS_PRIMARY  */
    T_FS_WAIT_PRIMARY = 307,       /* T_FS_WAIT_PRIMARY  */
    T_FS_WAIT_STANDBY = 308,       /* T_FS_WAIT_STANDBY  */
    T_FS_DEMOTED = 309,            /* T_FS_DEMOTED  */
    T_FS_DEMOTE_TIMEOUT = 310,     /* T_FS_DEMOTE_TIMEOUT  */
    T_FS_DRAINING = 311,           /* T_FS_DRAINING  */
    T_FS_SECONDARY = 312,          /* T_FS_SECONDARY  */
    T_FS_CATCHINGUP = 313,         /* T_FS_CATCHINGUP  */
    T_FS_PREP_PROMOTION = 314,     /* T_FS_PREP_PROMOTION  */
    T_FS_STOP_REPLICATION = 315,   /* T_FS_STOP_REPLICATION  */
    T_FS_MAINTENANCE = 316,        /* T_FS_MAINTENANCE  */
    T_FS_JOIN_PRIMARY = 317,       /* T_FS_JOIN_PRIMARY  */
    T_FS_APPLY_SETTINGS = 318,     /* T_FS_APPLY_SETTINGS  */
    T_FS_PREPARE_MAINTENANCE = 319, /* T_FS_PREPARE_MAINTENANCE  */
    T_FS_WAIT_MAINTENANCE = 320,   /* T_FS_WAIT_MAINTENANCE  */
    T_FS_REPORT_LSN = 321,         /* T_FS_REPORT_LSN  */
    T_FS_FAST_FORWARD = 322,       /* T_FS_FAST_FORWARD  */
    T_FS_JOIN_SECONDARY = 323,     /* T_FS_JOIN_SECONDARY  */
    T_FS_DROPPED = 324,            /* T_FS_DROPPED  */
    T_EXEC = 325,                  /* T_EXEC  */
    T_EXEC_FAILS = 326,            /* T_EXEC_FAILS  */
    T_RUN = 327,                   /* T_RUN  */
    T_PG_AUTOCTL = 328,            /* T_PG_AUTOCTL  */
    T_WAIT = 329,                  /* T_WAIT  */
    T_UNTIL = 330,                 /* T_UNTIL  */
    T_TIMEOUT = 331,               /* T_TIMEOUT  */
    T_AND = 332,                   /* T_AND  */
    T_IS = 333,                    /* T_IS  */
    T_WITH = 334,                  /* T_WITH  */
    T_REPLAYS = 335,               /* T_REPLAYS  */
    T_ASSERT = 336,                /* T_ASSERT  */
    T_SQL = 337,                   /* T_SQL  */
    T_EXPECT = 338,                /* T_EXPECT  */
    T_ERROR = 339,                 /* T_ERROR  */
    T_PROMOTE = 340,               /* T_PROMOTE  */
    T_PERFORM = 341,               /* T_PERFORM  */
    T_FAILOVER = 342,              /* T_FAILOVER  */
    T_NETWORK = 343,               /* T_NETWORK  */
    T_DISCONNECT = 344,            /* T_DISCONNECT  */
    T_CONNECT = 345,               /* T_CONNECT  */
    T_SLEEP = 346,                 /* T_SLEEP  */
    T_COMPOSE = 347,               /* T_COMPOSE  */
    T_DOWN = 348,                  /* T_DOWN  */
    T_START = 349,                 /* T_START  */
    T_STOP = 350,                  /* T_STOP  */
    T_STOPPED = 351,               /* T_STOPPED  */
    T_KILL = 352,                  /* T_KILL  */
    T_INJECT = 353,                /* T_INJECT  */
    T_STATE = 354,                 /* T_STATE  */
    T_ASSIGNED_STATE = 355,        /* T_ASSIGNED_STATE  */
    T_IN = 356,                    /* T_IN  */
    T_GROUP = 357,                 /* T_GROUP  */
    T_LBRACE = 358,                /* T_LBRACE  */
    T_RBRACE = 359,                /* T_RBRACE  */
    T_COMMA = 360,                 /* T_COMMA  */
    T_POSTGRES = 361,              /* T_POSTGRES  */
    T_STAYS = 362,                 /* T_STAYS  */
    T_WHILE = 363,                 /* T_WHILE  */
    T_THROUGH = 364,               /* T_THROUGH  */
    T_SET = 365,                   /* T_SET  */
    T_GET = 366,                   /* T_GET  */
    T_FSM = 367,                   /* T_FSM  */
    T_LOGS = 368,                  /* T_LOGS  */
    T_NOT = 369,                   /* T_NOT  */
    T_CONTAINS = 370,              /* T_CONTAINS  */
    T_MATCHES = 371,               /* T_MATCHES  */
    T_WAL = 372,                   /* T_WAL  */
    T_SEGMENT = 373,               /* T_SEGMENT  */
    T_ARCHIVED = 374,              /* T_ARCHIVED  */
    T_BASEBACKUP = 375,            /* T_BASEBACKUP  */
    T_SLASH = 376,                 /* T_SLASH  */
    T_INTEGER = 377,               /* T_INTEGER  */
    T_IDENT = 378,                 /* T_IDENT  */
    T_STRING = 379,                /* T_STRING  */
    T_BLOCK = 380,                 /* T_BLOCK  */
    T_SHELL_ARGS = 381             /* T_SHELL_ARGS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 148 "test_spec_parse.y"

	int         ival;
	char       *str;
	TestStep   *step;
	TestCmd    *cmd;

#line 197 "test_spec_parse.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_TEST_SPEC_PARSE_H_INCLUDED  */
