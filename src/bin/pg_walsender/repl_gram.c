/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "repl_gram.y"

/*
 * src/bin/pg_walsender/repl_gram.y
 *   Grammar for the replication command language pg_walsender accepts on
 *   Query messages, adapted from PostgreSQL's own
 *   src/backend/replication/repl_gram.y -- same command alternatives, same
 *   generic_option_list shape for BASE_BACKUP/CREATE_REPLICATION_SLOT
 *   options, same START_REPLICATION/CREATE_REPLICATION_SLOT/
 *   DROP_REPLICATION_SLOT/READ_REPLICATION_SLOT/SHOW/TIMELINE_HISTORY/
 *   IDENTIFY_SYSTEM rules. What's different: the real grammar's semantic
 *   actions build a backend Node / DefElem / List parse tree via
 *   makeNode()/palloc(), none of which exist in this frontend project --
 *   here every action instead fills in the single WsCommand this file's
 *   caller (repl_command.c) already allocated, directly, field by field,
 *   with a plain fixed-size WsCommandOption array standing in for a
 *   DefElem List. ALTER_REPLICATION_SLOT, UPLOAD_MANIFEST and logical
 *   START_REPLICATION are dropped: pg_walsender never implements them
 *   (WsCommandType has no matching member), so there is nothing for their
 *   productions to build. FETCH_FILE is this project's own extension,
 *   with no Postgres equivalent (see cmd_fetch_file.h).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "log.h"

#include "repl_command.h"

/* the command being filled in by the current parse; set by repl_command_parse() */
static WsCommand *ws_parse_cmd = NULL;

/* set by yyerror() or the scanner on a lexical error; checked by repl_command_parse() */
static bool ws_parse_failed = false;

/* the raw command string currently being parsed, kept only for error messages */
char *ws_parse_query = NULL;

extern int yylex(void);

static void yyerror(const char *message);
static char *dotted_name(const char *a, const char *b);
static void add_option(const char *name, const char *value, bool hasValue);


#line 123 "repl_gram.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "repl_gram.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_SCONST = 3,                     /* SCONST  */
  YYSYMBOL_IDENT = 4,                      /* IDENT  */
  YYSYMBOL_UCONST = 5,                     /* UCONST  */
  YYSYMBOL_RECPTR = 6,                     /* RECPTR  */
  YYSYMBOL_K_BASE_BACKUP = 7,              /* K_BASE_BACKUP  */
  YYSYMBOL_K_IDENTIFY_SYSTEM = 8,          /* K_IDENTIFY_SYSTEM  */
  YYSYMBOL_K_READ_REPLICATION_SLOT = 9,    /* K_READ_REPLICATION_SLOT  */
  YYSYMBOL_K_SHOW = 10,                    /* K_SHOW  */
  YYSYMBOL_K_START_REPLICATION = 11,       /* K_START_REPLICATION  */
  YYSYMBOL_K_CREATE_REPLICATION_SLOT = 12, /* K_CREATE_REPLICATION_SLOT  */
  YYSYMBOL_K_DROP_REPLICATION_SLOT = 13,   /* K_DROP_REPLICATION_SLOT  */
  YYSYMBOL_K_TIMELINE_HISTORY = 14,        /* K_TIMELINE_HISTORY  */
  YYSYMBOL_K_WAIT = 15,                    /* K_WAIT  */
  YYSYMBOL_K_TIMELINE = 16,                /* K_TIMELINE  */
  YYSYMBOL_K_PHYSICAL = 17,                /* K_PHYSICAL  */
  YYSYMBOL_K_LOGICAL = 18,                 /* K_LOGICAL  */
  YYSYMBOL_K_SLOT = 19,                    /* K_SLOT  */
  YYSYMBOL_K_RESERVE_WAL = 20,             /* K_RESERVE_WAL  */
  YYSYMBOL_K_TEMPORARY = 21,               /* K_TEMPORARY  */
  YYSYMBOL_K_TWO_PHASE = 22,               /* K_TWO_PHASE  */
  YYSYMBOL_K_EXPORT_SNAPSHOT = 23,         /* K_EXPORT_SNAPSHOT  */
  YYSYMBOL_K_NOEXPORT_SNAPSHOT = 24,       /* K_NOEXPORT_SNAPSHOT  */
  YYSYMBOL_K_USE_SNAPSHOT = 25,            /* K_USE_SNAPSHOT  */
  YYSYMBOL_K_FETCH_FILE = 26,              /* K_FETCH_FILE  */
  YYSYMBOL_27_ = 27,                       /* ';'  */
  YYSYMBOL_28_ = 28,                       /* '.'  */
  YYSYMBOL_29_ = 29,                       /* '('  */
  YYSYMBOL_30_ = 30,                       /* ')'  */
  YYSYMBOL_31_ = 31,                       /* ','  */
  YYSYMBOL_YYACCEPT = 32,                  /* $accept  */
  YYSYMBOL_firstcmd = 33,                  /* firstcmd  */
  YYSYMBOL_opt_semicolon = 34,             /* opt_semicolon  */
  YYSYMBOL_command = 35,                   /* command  */
  YYSYMBOL_identify_system = 36,           /* identify_system  */
  YYSYMBOL_fetch_file = 37,                /* fetch_file  */
  YYSYMBOL_read_replication_slot = 38,     /* read_replication_slot  */
  YYSYMBOL_show = 39,                      /* show  */
  YYSYMBOL_var_name = 40,                  /* var_name  */
  YYSYMBOL_base_backup = 41,               /* base_backup  */
  YYSYMBOL_create_replication_slot = 42,   /* create_replication_slot  */
  YYSYMBOL_create_slot_options = 43,       /* create_slot_options  */
  YYSYMBOL_create_slot_legacy_opt_list = 44, /* create_slot_legacy_opt_list  */
  YYSYMBOL_create_slot_legacy_opt = 45,    /* create_slot_legacy_opt  */
  YYSYMBOL_drop_replication_slot = 46,     /* drop_replication_slot  */
  YYSYMBOL_start_replication = 47,         /* start_replication  */
  YYSYMBOL_timeline_history = 48,          /* timeline_history  */
  YYSYMBOL_opt_physical = 49,              /* opt_physical  */
  YYSYMBOL_opt_temporary = 50,             /* opt_temporary  */
  YYSYMBOL_opt_slot = 51,                  /* opt_slot  */
  YYSYMBOL_opt_timeline = 52,              /* opt_timeline  */
  YYSYMBOL_generic_option_list = 53,       /* generic_option_list  */
  YYSYMBOL_generic_option = 54,            /* generic_option  */
  YYSYMBOL_ident_or_keyword = 55           /* ident_or_keyword  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  31
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   65

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  32
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  24
/* YYNRULES -- Number of rules.  */
#define YYNRULES  69
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  89

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   281


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      29,    30,     2,     2,    31,     2,    28,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    27,
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
      25,    26
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    94,    94,   100,   101,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   120,   134,   145,   156,   163,   164,
     172,   176,   191,   198,   208,   209,   213,   214,   223,   224,
     225,   226,   227,   232,   238,   254,   273,   287,   288,   292,
     293,   297,   300,   304,   314,   318,   319,   323,   325,   327,
     329,   339,   340,   341,   342,   343,   344,   345,   346,   347,
     348,   349,   350,   351,   352,   353,   354,   355,   356,   357
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "SCONST", "IDENT",
  "UCONST", "RECPTR", "K_BASE_BACKUP", "K_IDENTIFY_SYSTEM",
  "K_READ_REPLICATION_SLOT", "K_SHOW", "K_START_REPLICATION",
  "K_CREATE_REPLICATION_SLOT", "K_DROP_REPLICATION_SLOT",
  "K_TIMELINE_HISTORY", "K_WAIT", "K_TIMELINE", "K_PHYSICAL", "K_LOGICAL",
  "K_SLOT", "K_RESERVE_WAL", "K_TEMPORARY", "K_TWO_PHASE",
  "K_EXPORT_SNAPSHOT", "K_NOEXPORT_SNAPSHOT", "K_USE_SNAPSHOT",
  "K_FETCH_FILE", "';'", "'.'", "'('", "')'", "','", "$accept", "firstcmd",
  "opt_semicolon", "command", "identify_system", "fetch_file",
  "read_replication_slot", "show", "var_name", "base_backup",
  "create_replication_slot", "create_slot_options",
  "create_slot_legacy_opt_list", "create_slot_legacy_opt",
  "drop_replication_slot", "start_replication", "timeline_history",
  "opt_physical", "opt_temporary", "opt_slot", "opt_timeline",
  "generic_option_list", "generic_option", "ident_or_keyword", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-30)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      15,   -24,   -30,    27,    40,    26,    42,    43,    44,    45,
      50,    24,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,
     -30,    -4,   -30,   -30,    25,    48,    37,    34,    41,   -30,
     -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,
     -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,
     -30,   -30,   -30,   -29,   -30,    33,    53,   -30,   -30,    52,
     -30,    22,   -30,   -30,    -4,   -30,   -30,   -30,   -30,    46,
      30,    56,   -30,    58,   -30,    -4,   -30,    10,    30,   -30,
      12,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,    21,    14,     0,     0,    42,     0,     0,     0,     0,
       0,     4,     5,    13,    10,    12,     6,     8,     9,     7,
      11,     0,    16,    18,    17,     0,    38,    40,    33,    36,
      15,     1,     3,     2,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,     0,    46,    47,     0,    41,    37,     0,
      39,     0,    34,    20,     0,    49,    48,    50,    19,    44,
      27,     0,    45,     0,    35,     0,    22,    25,    27,    43,
       0,    31,    32,    28,    29,    30,    26,    23,    24
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,
     -30,   -17,   -30,   -30,   -30,   -30,   -30,   -30,   -30,   -30,
     -30,   -11,     1,   -30
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,    10,    33,    11,    12,    13,    14,    15,    24,    16,
      17,    76,    77,    86,    18,    19,    20,    59,    61,    26,
      74,    53,    54,    55
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      34,    63,    64,    35,    36,    21,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,     1,     2,     3,     4,     5,     6,     7,     8,
      81,    22,    82,    83,    84,    85,    65,    66,    67,    70,
      71,     9,    88,    64,    23,    25,    27,    28,    30,    29,
      31,    32,    57,    56,    58,    60,    62,    68,    69,    75,
      78,    87,    73,    79,    80,    72
};

static const yytype_int8 yycheck[] =
{
       4,    30,    31,     7,     8,    29,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,     7,     8,     9,    10,    11,    12,    13,    14,
      20,     4,    22,    23,    24,    25,     3,     4,     5,    17,
      18,    26,    30,    31,     4,    19,     4,     4,     3,     5,
       0,    27,     4,    28,    17,    21,    15,     4,     6,    29,
       4,    78,    16,     5,    75,    64
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     7,     8,     9,    10,    11,    12,    13,    14,    26,
      33,    35,    36,    37,    38,    39,    41,    42,    46,    47,
      48,    29,     4,     4,    40,    19,    51,     4,     4,     5,
       3,     0,    27,    34,     4,     7,     8,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    53,    54,    55,    28,     4,    17,    49,
      21,    50,    15,    30,    31,     3,     4,     5,     4,     6,
      17,    18,    54,    16,    52,    29,    43,    44,     4,     5,
      53,    20,    22,    23,    24,    25,    45,    43,    30
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    32,    33,    34,    34,    35,    35,    35,    35,    35,
      35,    35,    35,    35,    36,    37,    38,    39,    40,    40,
      41,    41,    42,    42,    43,    43,    44,    44,    45,    45,
      45,    45,    45,    46,    46,    47,    48,    49,    49,    50,
      50,    51,    51,    52,    52,    53,    53,    54,    54,    54,
      54,    55,    55,    55,    55,    55,    55,    55,    55,    55,
      55,    55,    55,    55,    55,    55,    55,    55,    55,    55
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     0,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     2,     1,     3,
       4,     1,     5,     6,     3,     1,     2,     0,     1,     1,
       1,     1,     1,     2,     3,     5,     2,     1,     0,     1,
       0,     2,     0,     2,     0,     3,     1,     1,     2,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
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






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
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
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
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
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* firstcmd: command opt_semicolon  */
#line 95 "repl_gram.y"
                                {
					(void) yynerrs; /* suppress compiler warning */
				}
#line 1227 "repl_gram.c"
    break;

  case 14: /* identify_system: K_IDENTIFY_SYSTEM  */
#line 121 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_IDENTIFY_SYSTEM;
				}
#line 1235 "repl_gram.c"
    break;

  case 15: /* fetch_file: K_FETCH_FILE SCONST  */
#line 135 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_FETCH_FILE;
					strlcpy(ws_parse_cmd->filename, (yyvsp[0].str), sizeof(ws_parse_cmd->filename));
				}
#line 1244 "repl_gram.c"
    break;

  case 16: /* read_replication_slot: K_READ_REPLICATION_SLOT IDENT  */
#line 146 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_READ_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, (yyvsp[0].str), sizeof(ws_parse_cmd->slotName));
				}
#line 1253 "repl_gram.c"
    break;

  case 17: /* show: K_SHOW var_name  */
#line 157 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_SHOW;
					strlcpy(ws_parse_cmd->showName, (yyvsp[0].str), sizeof(ws_parse_cmd->showName));
				}
#line 1262 "repl_gram.c"
    break;

  case 18: /* var_name: IDENT  */
#line 163 "repl_gram.y"
                        { (yyval.str) = (yyvsp[0].str); }
#line 1268 "repl_gram.c"
    break;

  case 19: /* var_name: var_name '.' IDENT  */
#line 165 "repl_gram.y"
                                { (yyval.str) = dotted_name((yyvsp[-2].str), (yyvsp[0].str)); }
#line 1274 "repl_gram.c"
    break;

  case 20: /* base_backup: K_BASE_BACKUP '(' generic_option_list ')'  */
#line 173 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_BASE_BACKUP;
				}
#line 1282 "repl_gram.c"
    break;

  case 21: /* base_backup: K_BASE_BACKUP  */
#line 177 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_BASE_BACKUP;
				}
#line 1290 "repl_gram.c"
    break;

  case 22: /* create_replication_slot: K_CREATE_REPLICATION_SLOT IDENT opt_temporary K_PHYSICAL create_slot_options  */
#line 192 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, (yyvsp[-3].str), sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->temporary = (yyvsp[-2].boolval);
					ws_parse_cmd->isLogical = false;
				}
#line 1301 "repl_gram.c"
    break;

  case 23: /* create_replication_slot: K_CREATE_REPLICATION_SLOT IDENT opt_temporary K_LOGICAL IDENT create_slot_options  */
#line 199 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_CREATE_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, (yyvsp[-4].str), sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->temporary = (yyvsp[-3].boolval);
					ws_parse_cmd->isLogical = true;
				}
#line 1312 "repl_gram.c"
    break;

  case 28: /* create_slot_legacy_opt: K_EXPORT_SNAPSHOT  */
#line 223 "repl_gram.y"
                                                        { add_option("snapshot", "export", true); }
#line 1318 "repl_gram.c"
    break;

  case 29: /* create_slot_legacy_opt: K_NOEXPORT_SNAPSHOT  */
#line 224 "repl_gram.y"
                                                { add_option("snapshot", "nothing", true); }
#line 1324 "repl_gram.c"
    break;

  case 30: /* create_slot_legacy_opt: K_USE_SNAPSHOT  */
#line 225 "repl_gram.y"
                                                        { add_option("snapshot", "use", true); }
#line 1330 "repl_gram.c"
    break;

  case 31: /* create_slot_legacy_opt: K_RESERVE_WAL  */
#line 226 "repl_gram.y"
                                                        { add_option("reserve_wal", "true", true); }
#line 1336 "repl_gram.c"
    break;

  case 32: /* create_slot_legacy_opt: K_TWO_PHASE  */
#line 227 "repl_gram.y"
                                                        { add_option("two_phase", "true", true); }
#line 1342 "repl_gram.c"
    break;

  case 33: /* drop_replication_slot: K_DROP_REPLICATION_SLOT IDENT  */
#line 233 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_DROP_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, (yyvsp[0].str), sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->dropWait = false;
				}
#line 1352 "repl_gram.c"
    break;

  case 34: /* drop_replication_slot: K_DROP_REPLICATION_SLOT IDENT K_WAIT  */
#line 239 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_DROP_REPLICATION_SLOT;
					strlcpy(ws_parse_cmd->slotName, (yyvsp[-1].str), sizeof(ws_parse_cmd->slotName));
					ws_parse_cmd->dropWait = true;
				}
#line 1362 "repl_gram.c"
    break;

  case 35: /* start_replication: K_START_REPLICATION opt_slot opt_physical RECPTR opt_timeline  */
#line 255 "repl_gram.y"
                                {
					ws_parse_cmd->type = WS_CMD_START_REPLICATION;

					if ((yyvsp[-3].str) != NULL)
					{
						strlcpy(ws_parse_cmd->slotName, (yyvsp[-3].str), sizeof(ws_parse_cmd->slotName));
					}

					ws_parse_cmd->startLsn = (yyvsp[-1].recptr);
					ws_parse_cmd->timeline = (int) (yyvsp[0].uintval);
					ws_parse_cmd->haveTimeline = ((yyvsp[0].uintval) != 0);
				}
#line 1379 "repl_gram.c"
    break;

  case 36: /* timeline_history: K_TIMELINE_HISTORY UCONST  */
#line 274 "repl_gram.y"
                                {
					if ((yyvsp[0].uintval) == 0)
					{
						yyerror("invalid timeline 0");
						YYERROR;
					}

					ws_parse_cmd->type = WS_CMD_TIMELINE_HISTORY;
					ws_parse_cmd->timeline = (int) (yyvsp[0].uintval);
				}
#line 1394 "repl_gram.c"
    break;

  case 39: /* opt_temporary: K_TEMPORARY  */
#line 292 "repl_gram.y"
                                                        { (yyval.boolval) = true; }
#line 1400 "repl_gram.c"
    break;

  case 40: /* opt_temporary: %empty  */
#line 293 "repl_gram.y"
                                                { (yyval.boolval) = false; }
#line 1406 "repl_gram.c"
    break;

  case 41: /* opt_slot: K_SLOT IDENT  */
#line 298 "repl_gram.y"
                                { (yyval.str) = (yyvsp[0].str); }
#line 1412 "repl_gram.c"
    break;

  case 42: /* opt_slot: %empty  */
#line 300 "repl_gram.y"
                                { (yyval.str) = NULL; }
#line 1418 "repl_gram.c"
    break;

  case 43: /* opt_timeline: K_TIMELINE UCONST  */
#line 305 "repl_gram.y"
                                {
					if ((yyvsp[0].uintval) == 0)
					{
						yyerror("invalid timeline 0");
						YYERROR;
					}

					(yyval.uintval) = (yyvsp[0].uintval);
				}
#line 1432 "repl_gram.c"
    break;

  case 44: /* opt_timeline: %empty  */
#line 314 "repl_gram.y"
                                                { (yyval.uintval) = 0; }
#line 1438 "repl_gram.c"
    break;

  case 47: /* generic_option: ident_or_keyword  */
#line 324 "repl_gram.y"
                                { add_option((yyvsp[0].str), NULL, false); }
#line 1444 "repl_gram.c"
    break;

  case 48: /* generic_option: ident_or_keyword IDENT  */
#line 326 "repl_gram.y"
                                { add_option((yyvsp[-1].str), (yyvsp[0].str), true); }
#line 1450 "repl_gram.c"
    break;

  case 49: /* generic_option: ident_or_keyword SCONST  */
#line 328 "repl_gram.y"
                                { add_option((yyvsp[-1].str), (yyvsp[0].str), true); }
#line 1456 "repl_gram.c"
    break;

  case 50: /* generic_option: ident_or_keyword UCONST  */
#line 330 "repl_gram.y"
                                {
					char numbuf[32];

					snprintf(numbuf, sizeof(numbuf), "%u", (yyvsp[0].uintval));
					add_option((yyvsp[-1].str), numbuf, true);
				}
#line 1467 "repl_gram.c"
    break;

  case 51: /* ident_or_keyword: IDENT  */
#line 339 "repl_gram.y"
                                                                        { (yyval.str) = (yyvsp[0].str); }
#line 1473 "repl_gram.c"
    break;

  case 52: /* ident_or_keyword: K_BASE_BACKUP  */
#line 340 "repl_gram.y"
                                                                { (yyval.str) = "base_backup"; }
#line 1479 "repl_gram.c"
    break;

  case 53: /* ident_or_keyword: K_IDENTIFY_SYSTEM  */
#line 341 "repl_gram.y"
                                                                { (yyval.str) = "identify_system"; }
#line 1485 "repl_gram.c"
    break;

  case 54: /* ident_or_keyword: K_SHOW  */
#line 342 "repl_gram.y"
                                                                        { (yyval.str) = "show"; }
#line 1491 "repl_gram.c"
    break;

  case 55: /* ident_or_keyword: K_START_REPLICATION  */
#line 343 "repl_gram.y"
                                                        { (yyval.str) = "start_replication"; }
#line 1497 "repl_gram.c"
    break;

  case 56: /* ident_or_keyword: K_CREATE_REPLICATION_SLOT  */
#line 344 "repl_gram.y"
                                                        { (yyval.str) = "create_replication_slot"; }
#line 1503 "repl_gram.c"
    break;

  case 57: /* ident_or_keyword: K_DROP_REPLICATION_SLOT  */
#line 345 "repl_gram.y"
                                                        { (yyval.str) = "drop_replication_slot"; }
#line 1509 "repl_gram.c"
    break;

  case 58: /* ident_or_keyword: K_TIMELINE_HISTORY  */
#line 346 "repl_gram.y"
                                                        { (yyval.str) = "timeline_history"; }
#line 1515 "repl_gram.c"
    break;

  case 59: /* ident_or_keyword: K_WAIT  */
#line 347 "repl_gram.y"
                                                                        { (yyval.str) = "wait"; }
#line 1521 "repl_gram.c"
    break;

  case 60: /* ident_or_keyword: K_TIMELINE  */
#line 348 "repl_gram.y"
                                                                { (yyval.str) = "timeline"; }
#line 1527 "repl_gram.c"
    break;

  case 61: /* ident_or_keyword: K_PHYSICAL  */
#line 349 "repl_gram.y"
                                                                { (yyval.str) = "physical"; }
#line 1533 "repl_gram.c"
    break;

  case 62: /* ident_or_keyword: K_LOGICAL  */
#line 350 "repl_gram.y"
                                                                        { (yyval.str) = "logical"; }
#line 1539 "repl_gram.c"
    break;

  case 63: /* ident_or_keyword: K_SLOT  */
#line 351 "repl_gram.y"
                                                                        { (yyval.str) = "slot"; }
#line 1545 "repl_gram.c"
    break;

  case 64: /* ident_or_keyword: K_RESERVE_WAL  */
#line 352 "repl_gram.y"
                                                                { (yyval.str) = "reserve_wal"; }
#line 1551 "repl_gram.c"
    break;

  case 65: /* ident_or_keyword: K_TEMPORARY  */
#line 353 "repl_gram.y"
                                                                { (yyval.str) = "temporary"; }
#line 1557 "repl_gram.c"
    break;

  case 66: /* ident_or_keyword: K_TWO_PHASE  */
#line 354 "repl_gram.y"
                                                                { (yyval.str) = "two_phase"; }
#line 1563 "repl_gram.c"
    break;

  case 67: /* ident_or_keyword: K_EXPORT_SNAPSHOT  */
#line 355 "repl_gram.y"
                                                                { (yyval.str) = "export_snapshot"; }
#line 1569 "repl_gram.c"
    break;

  case 68: /* ident_or_keyword: K_NOEXPORT_SNAPSHOT  */
#line 356 "repl_gram.y"
                                                        { (yyval.str) = "noexport_snapshot"; }
#line 1575 "repl_gram.c"
    break;

  case 69: /* ident_or_keyword: K_USE_SNAPSHOT  */
#line 357 "repl_gram.y"
                                                                { (yyval.str) = "use_snapshot"; }
#line 1581 "repl_gram.c"
    break;


#line 1585 "repl_gram.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
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

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
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
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
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
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 360 "repl_gram.y"


/*
 * yyerror is bison's required error hook: called on a syntax error (and by
 * this file's own timeline-validation actions via YYERROR above). It
 * cannot touch the socket -- repl_command_parse() below reports the
 * failure to its caller, which sends the ErrorResponse -- so it only logs
 * and records that this parse failed.
 */
static void
yyerror(const char *message)
{
	ws_parse_failed = true;
	log_error("Syntax error in replication command \"%s\": %s",
			  ws_parse_query != NULL ? ws_parse_query : "", message);
}


/* dotted_name builds "a.b" for a var_name like "wal_segment_size" that
 * SHOW never actually needs multi-part, but real Postgres's grammar
 * supports it, so this does too. */
static char *
dotted_name(const char *a, const char *b)
{
	size_t len = strlen(a) + strlen(b) + 2;
	char *out = malloc(len); /* IGNORE-BANNED */

	if (out != NULL)
	{
		snprintf(out, len, "%s.%s", a, b);
	}

	return out;
}


/*
 * add_option appends one BASE_BACKUP/CREATE_REPLICATION_SLOT option onto
 * the command being parsed -- this project's plain-array stand-in for
 * lappend()'ing a DefElem onto a List. Silently drops an option beyond
 * WS_MAX_COMMAND_OPTIONS rather than erroring: real clients never send
 * anywhere near that many.
 */
static void
add_option(const char *name, const char *value, bool hasValue)
{
	if (ws_parse_cmd->nOptions >= WS_MAX_COMMAND_OPTIONS)
	{
		log_warn("Ignoring replication command option \"%s\": too many "
				 "options in one command (max %d)",
				 name, WS_MAX_COMMAND_OPTIONS);
		return;
	}

	WsCommandOption *opt = &ws_parse_cmd->options[ws_parse_cmd->nOptions++];

	strlcpy(opt->name, name, sizeof(opt->name));
	opt->hasValue = hasValue;

	if (hasValue && value != NULL)
	{
		strlcpy(opt->value, value, sizeof(opt->value));
	}
	else
	{
		opt->value[0] = '\0';
	}
}


/*
 * repl_command_parse fills *cmd from the given Query-message string,
 * running the flex/bison parser generated from repl_scanner.l/repl_gram.y
 * above (see repl_command.h). Not reentrant (global yylex()/yyparse()
 * state, same as this project's own pgaftest test_spec_scan.l/
 * test_spec_parse.y), which is safe here because pg_walsender forks one
 * child per connection (accept_loop.c) and never parses two commands
 * concurrently in the same process.
 */
extern void ws_scanner_init(const char *str);
extern void ws_scanner_finish(void);

bool
repl_command_parse(const char *query, WsCommand *cmd)
{
	memset(cmd, 0, sizeof(WsCommand));
	cmd->type = WS_CMD_UNKNOWN;

	ws_parse_cmd = cmd;
	ws_parse_failed = false;
	ws_parse_query = (char *) query;

	ws_scanner_init(query);

	int parseResult = yyparse();

	ws_scanner_finish();

	if (parseResult != 0 || ws_parse_failed || cmd->type == WS_CMD_UNKNOWN)
	{
		cmd->type = WS_CMD_UNKNOWN;
		return false;
	}

	return true;
}


/*
 * ws_command_find_option looks up a previously parsed BASE_BACKUP/
 * CREATE_REPLICATION_SLOT option by name (case-insensitive, matching
 * ident_or_keyword's own case folding via the scanner's IDENT rule).
 */
const WsCommandOption *
ws_command_find_option(const WsCommand *cmd, const char *name)
{
	for (int i = 0; i < cmd->nOptions; i++)
	{
		if (strcasecmp(cmd->options[i].name, name) == 0)
		{
			return &cmd->options[i];
		}
	}

	return NULL;
}
