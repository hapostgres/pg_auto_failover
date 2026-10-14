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

#ifndef YY_YY_REPL_GRAM_H_INCLUDED
# define YY_YY_REPL_GRAM_H_INCLUDED
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
    SCONST = 258,                  /* SCONST  */
    IDENT = 259,                   /* IDENT  */
    CRC32C_HEX = 260,              /* CRC32C_HEX  */
    UCONST = 261,                  /* UCONST  */
    RECPTR = 262,                  /* RECPTR  */
    K_BASE_BACKUP = 263,           /* K_BASE_BACKUP  */
    K_IDENTIFY_SYSTEM = 264,       /* K_IDENTIFY_SYSTEM  */
    K_READ_REPLICATION_SLOT = 265, /* K_READ_REPLICATION_SLOT  */
    K_SHOW = 266,                  /* K_SHOW  */
    K_START_REPLICATION = 267,     /* K_START_REPLICATION  */
    K_CREATE_REPLICATION_SLOT = 268, /* K_CREATE_REPLICATION_SLOT  */
    K_DROP_REPLICATION_SLOT = 269, /* K_DROP_REPLICATION_SLOT  */
    K_TIMELINE_HISTORY = 270,      /* K_TIMELINE_HISTORY  */
    K_WAIT = 271,                  /* K_WAIT  */
    K_TIMELINE = 272,              /* K_TIMELINE  */
    K_PHYSICAL = 273,              /* K_PHYSICAL  */
    K_LOGICAL = 274,               /* K_LOGICAL  */
    K_SLOT = 275,                  /* K_SLOT  */
    K_RESERVE_WAL = 276,           /* K_RESERVE_WAL  */
    K_TEMPORARY = 277,             /* K_TEMPORARY  */
    K_TWO_PHASE = 278,             /* K_TWO_PHASE  */
    K_EXPORT_SNAPSHOT = 279,       /* K_EXPORT_SNAPSHOT  */
    K_NOEXPORT_SNAPSHOT = 280,     /* K_NOEXPORT_SNAPSHOT  */
    K_USE_SNAPSHOT = 281,          /* K_USE_SNAPSHOT  */
    K_FETCH_FILE = 282,            /* K_FETCH_FILE  */
    K_CHECK_FILE = 283,            /* K_CHECK_FILE  */
    K_ARCHIVE_FILE = 284           /* K_ARCHIVE_FILE  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 54 "repl_gram.y"

	char	   *str;
	bool		boolval;
	uint32_t	uintval;
	uint64_t	recptr;

#line 100 "repl_gram.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_REPL_GRAM_H_INCLUDED  */
