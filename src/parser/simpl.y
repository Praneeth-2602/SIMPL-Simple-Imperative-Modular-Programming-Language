%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "../semantic/semantic.h"

int yylex(void);
void yyerror(const char *s);
%}

%define parse.error verbose

%union {
    int num;
    char *str;
    struct ASTNode *node;
}

/* ---------- Core Keywords ---------- */
%token LET SET BE TO
%token IF THEN ELSE END
%token WHILE DO
%token PRINT

/* ---------- Built-in Data Types ---------- */
%token STACK QUEUE TREE GRAPH

/* ---------- ADT Operations ---------- */
%token PUSH POP
%token ENQUEUE DEQUEUE
%token INSERT REMOVE
%token ADD_EDGE REMOVE_EDGE

/* ---------- Operators ---------- */
%token PLUS MINUS MUL DIV
%token LT GT EQ NEQ

/* ---------- Identifiers & Literals ---------- */
%token <str> IDENTIFIER
%token <num> NUMBER

/* ---------- AST Node Types ---------- */
%type <node> program statement_list statement
%type <node> declaration assignment print_stmt
%type <node> if_stmt while_stmt
%type <node> expression term factor condition
%type <node> adt_declaration adt_operation

%start program

%%

/* ---------- Program ---------- */

program
    : statement_list
      {
          ast_root = make_node(AST_PROGRAM, $1, NULL, NULL);
      }
    ;

statement_list
    : statement
      { $$ = make_node(AST_STATEMENT_LIST, $1, NULL, NULL); }
    | statement_list statement
      { $$ = make_node(AST_STATEMENT_LIST, $1, $2, NULL); }
    ;

/* ---------- Statements ---------- */

statement
    : declaration     { $$ = $1; }
    | assignment      { $$ = $1; }
    | print_stmt      { $$ = $1; }
    | if_stmt         { $$ = $1; }
    | while_stmt      { $$ = $1; }
    | adt_declaration { $$ = $1; }
    | adt_operation   { $$ = $1; }
    ;

/* ---------- Variable Declarations ---------- */

declaration
    : LET IDENTIFIER BE expression
      {
        ASTNode *id = make_identifier($2);
        $$ = make_node(AST_DECL, id, $4, NULL);
      }
    ;

/* ---------- ADT Declarations ---------- */

adt_declaration
    : LET IDENTIFIER BE STACK
      {
        ASTNode *id = make_identifier($2);
        ASTNode *type_node = make_number(0);  /* 0 = TYPE_STACK */
        $$ = make_node(AST_ADT_DECL, id, type_node, NULL);
      }
    | LET IDENTIFIER BE QUEUE
      {
        ASTNode *id = make_identifier($2);
        ASTNode *type_node = make_number(1);  /* 1 = TYPE_QUEUE */
        $$ = make_node(AST_ADT_DECL, id, type_node, NULL);
      }
    | LET IDENTIFIER BE TREE
      {
        ASTNode *id = make_identifier($2);
        ASTNode *type_node = make_number(2);  /* 2 = TYPE_TREE */
        $$ = make_node(AST_ADT_DECL, id, type_node, NULL);
      }
    | LET IDENTIFIER BE GRAPH
      {
        ASTNode *id = make_identifier($2);
        ASTNode *type_node = make_number(3);  /* 3 = TYPE_GRAPH */
        $$ = make_node(AST_ADT_DECL, id, type_node, NULL);
      }
    ;

/* ---------- Assignments ---------- */

assignment
    : SET IDENTIFIER TO expression
      {
        ASTNode *id = make_identifier($2);
        $$ = make_node(AST_ASSIGN, id, $4, NULL);
      }
    ;

/* ---------- Print ---------- */

print_stmt
    : PRINT expression
      { $$ = make_node(AST_PRINT, $2, NULL, NULL); }
    ;

/* ---------- Control Flow ---------- */

if_stmt
    : IF condition THEN statement_list END
      { $$ = make_node(AST_IF, $2, $4, NULL); }
    | IF condition THEN statement_list ELSE statement_list END
      { $$ = make_node(AST_IF, $2, $4, $6); }
    ;

while_stmt
    : WHILE condition DO statement_list END
      { $$ = make_node(AST_WHILE, $2, $4, NULL); }
    ;

/* ---------- Conditions ---------- */

condition
    : expression LT expression  { $$ = make_binop('<', $1, $3); }
    | expression GT expression  { $$ = make_binop('>', $1, $3); }
    | expression EQ expression  { $$ = make_binop('=', $1, $3); }
    | expression NEQ expression { $$ = make_binop('!', $1, $3); }
    ;

/* ---------- Expressions ---------- */

expression
    : expression PLUS term  { $$ = make_binop('+', $1, $3); }
    | expression MINUS term { $$ = make_binop('-', $1, $3); }
    | term                  { $$ = $1; }
    ;

term
    : term MUL factor { $$ = make_binop('*', $1, $3); }
    | term DIV factor { $$ = make_binop('/', $1, $3); }
    | factor          { $$ = $1; }
    ;

factor
    : NUMBER     { $$ = make_number($1); }
    | IDENTIFIER { $$ = make_identifier($1); }
    ;

/* ---------- ADT Operations ---------- */

adt_operation
    : PUSH IDENTIFIER expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('P');  /* P = push */
        $$ = make_node(AST_ADT_OP, id, $3, op_node);
      }
    | POP IDENTIFIER
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('p');  /* p = pop */
        $$ = make_node(AST_ADT_OP, id, NULL, op_node);
      }
    | ENQUEUE IDENTIFIER expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('E');  /* E = enqueue */
        $$ = make_node(AST_ADT_OP, id, $3, op_node);
      }
    | DEQUEUE IDENTIFIER
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('D');  /* D = dequeue */
        $$ = make_node(AST_ADT_OP, id, NULL, op_node);
      }
    | INSERT IDENTIFIER expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('I');  /* I = insert */
        $$ = make_node(AST_ADT_OP, id, $3, op_node);
      }
    | REMOVE IDENTIFIER expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('R');  /* R = remove */
        $$ = make_node(AST_ADT_OP, id, $3, op_node);
      }
    | ADD_EDGE IDENTIFIER expression expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('A');  /* A = add_edge */
        ASTNode *args = make_node(AST_BINOP, $3, $4, NULL);
        $$ = make_node(AST_ADT_OP, id, args, op_node);
      }
    | REMOVE_EDGE IDENTIFIER expression expression
      {
        ASTNode *id = make_identifier($2);
        ASTNode *op_node = make_number('X');  /* X = remove_edge */
        ASTNode *args = make_node(AST_BINOP, $3, $4, NULL);
        $$ = make_node(AST_ADT_OP, id, args, op_node);
      }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Syntax error: %s\n", s);
}
