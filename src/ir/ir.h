#ifndef IR_H
#define IR_H

#include "../parser/ast.h"

typedef enum {
    IR_ASSIGN,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_PRINT
} IROp;

typedef struct IRInstruction {
    IROp op;

    char result[32];
    char arg1[32];
    char arg2[32];

    struct IRInstruction *next;
} IRInstruction;

/* IR API */
IRInstruction* generate_ir(ASTNode *root);
void print_ir(IRInstruction *head);

#endif