#ifndef AST_H
#define AST_H

#include "../semantic/symbol_table.h"

typedef enum {
    AST_PROGRAM,
    AST_STATEMENT_LIST,

    AST_DECL,
    AST_ASSIGN,
    AST_PRINT,

    AST_IF,
    AST_WHILE,

    AST_ADT_DECL,
    AST_ADT_OP,

    AST_BINOP,
    AST_IDENTIFIER,
    AST_NUMBER
} ASTNodeType;

/* ADT type codes (stored in type_node->value for AST_ADT_DECL) */
#define ADT_STACK 0
#define ADT_QUEUE 1
#define ADT_TREE  2
#define ADT_GRAPH 3

/* ADT operation codes (stored in op_node->value for AST_ADT_OP) */
#define OP_PUSH        'P'
#define OP_POP         'p'
#define OP_ENQUEUE     'E'
#define OP_DEQUEUE     'D'
#define OP_INSERT      'I'
#define OP_REMOVE      'R'
#define OP_ADD_EDGE    'A'
#define OP_REMOVE_EDGE 'X'

typedef struct ASTNode {
    ASTNodeType type;

    char *name;          /* for identifiers */
    int value;           /* for numbers, ADT type codes, op codes */
    char op;             /* '+', '-', '*', '/', '<', '>', '=', '!' */

    Type inferred_type;  /* set during semantic analysis */

    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *third;   /* useful for IF-ELSE, ADT op code */

} ASTNode;

/* Constructors */
ASTNode *make_node(ASTNodeType type,
                   ASTNode *left,
                   ASTNode *right,
                   ASTNode *third);

ASTNode *make_number(int value);
ASTNode *make_identifier(char *name);
ASTNode *make_binop(char op, ASTNode *l, ASTNode *r);

/* Global AST root (set by parser) */
extern ASTNode *ast_root;

#endif
