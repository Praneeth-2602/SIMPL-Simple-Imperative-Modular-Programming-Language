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

#ifndef YY_YY_SRC_PARSER_SIMPL_TAB_H_INCLUDED
# define YY_YY_SRC_PARSER_SIMPL_TAB_H_INCLUDED
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
    LET = 258,                     /* LET  */
    SET = 259,                     /* SET  */
    BE = 260,                      /* BE  */
    TO = 261,                      /* TO  */
    IF = 262,                      /* IF  */
    THEN = 263,                    /* THEN  */
    ELSE = 264,                    /* ELSE  */
    END = 265,                     /* END  */
    WHILE = 266,                   /* WHILE  */
    DO = 267,                      /* DO  */
    PRINT = 268,                   /* PRINT  */
    STACK = 269,                   /* STACK  */
    QUEUE = 270,                   /* QUEUE  */
    TREE = 271,                    /* TREE  */
    GRAPH = 272,                   /* GRAPH  */
    PUSH = 273,                    /* PUSH  */
    POP = 274,                     /* POP  */
    ENQUEUE = 275,                 /* ENQUEUE  */
    DEQUEUE = 276,                 /* DEQUEUE  */
    INSERT = 277,                  /* INSERT  */
    REMOVE = 278,                  /* REMOVE  */
    ADD_EDGE = 279,                /* ADD_EDGE  */
    REMOVE_EDGE = 280,             /* REMOVE_EDGE  */
    PLUS = 281,                    /* PLUS  */
    MINUS = 282,                   /* MINUS  */
    MUL = 283,                     /* MUL  */
    DIV = 284,                     /* DIV  */
    LT = 285,                      /* LT  */
    GT = 286,                      /* GT  */
    EQ = 287,                      /* EQ  */
    NEQ = 288,                     /* NEQ  */
    IDENTIFIER = 289,              /* IDENTIFIER  */
    NUMBER = 290                   /* NUMBER  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 13 "src/parser/simpl.y"

    int num;
    char *str;
    struct ASTNode *node;

#line 105 "src/parser/simpl.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_SRC_PARSER_SIMPL_TAB_H_INCLUDED  */
