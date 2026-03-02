#include "ast.h"
#include <stdlib.h>
#include <string.h>

/* Global AST root */
ASTNode *ast_root = NULL;

ASTNode *make_node(ASTNodeType type,
                   ASTNode *left,
                   ASTNode *right,
                   ASTNode *third)
{
    ASTNode *n = malloc(sizeof(ASTNode));
    n->type = type;
    n->left = left;
    n->right = right;
    n->third = third;
    n->name = NULL;
    n->value = 0;
    n->op = 0;
    n->inferred_type = TYPE_UNKNOWN;
    return n;
}

ASTNode *make_number(int value)
{
    ASTNode *n = make_node(AST_NUMBER, NULL, NULL, NULL);
    n->value = value;
    return n;
}

ASTNode *make_identifier(char *name)
{
    ASTNode *n = make_node(AST_IDENTIFIER, NULL, NULL, NULL);
    n->name = strdup(name);
    return n;
}

ASTNode *make_binop(char op, ASTNode *l, ASTNode *r)
{
    ASTNode *n = make_node(AST_BINOP, l, r, NULL);
    n->op = op;
    return n;
}
