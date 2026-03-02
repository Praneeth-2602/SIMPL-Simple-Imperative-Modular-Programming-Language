#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IRInstruction *ir_head = NULL;
static IRInstruction *ir_tail = NULL;

static int temp_count = 0;

/* Generate new temporary variable */
static char* new_temp() {
    char buffer[32];
    sprintf(buffer, "t%d", temp_count++);
    return strdup(buffer);
}

/* Append instruction */
static void emit(IROp op, const char *res, const char *a1, const char *a2) {
    IRInstruction *inst = malloc(sizeof(IRInstruction));
    inst->op = op;

    strcpy(inst->result, res ? res : "");
    strcpy(inst->arg1, a1 ? a1 : "");
    strcpy(inst->arg2, a2 ? a2 : "");

    inst->next = NULL;

    if (!ir_head) {
        ir_head = ir_tail = inst;
    } else {
        ir_tail->next = inst;
        ir_tail = inst;
    }
}

/* Forward declarations */
static char* generate_expr(ASTNode *node);
static void generate_node(ASTNode *node);

/* Generate IR from AST */
IRInstruction* generate_ir(ASTNode *root) {
    ir_head = ir_tail = NULL;
    temp_count = 0;

    generate_node(root);
    return ir_head;
}

/* Recursive walk that does not reset global IR state */
static void generate_node(ASTNode *root) {
    if (!root) return;

    if (root->type == AST_PROGRAM) {
        generate_node(root->left);
    }

    else if (root->type == AST_STATEMENT_LIST) {
        generate_node(root->left);
        generate_node(root->right);
    }

    else if (root->type == AST_DECL) {
        /* Declaration has no runtime effect in this IR. */
        return;
    }

    else if (root->type == AST_ASSIGN) {
        char *rhs = generate_expr(root->right);
        emit(IR_ASSIGN, root->left->name, rhs, NULL);
    }

    else if (root->type == AST_PRINT) {
        char *val = generate_expr(root->left);
        emit(IR_PRINT, val, NULL, NULL);
    }

    else if (root->type == AST_ADT_DECL || root->type == AST_ADT_OP) {
        /* ADT nodes are currently ignored by the IR. */
        return;
    }
}

/* Generate expression and return variable holding result */
static char* generate_expr(ASTNode *node) {
    if (!node) return NULL;
    if (node->type == AST_NUMBER) {
        char buffer[32];
        sprintf(buffer, "%d", node->value);
        return strdup(buffer);
    }

    if (node->type == AST_IDENTIFIER) {
        return strdup(node->name);
    }

    if (node->type == AST_BINOP) {
        char *left = generate_expr(node->left);
        char *right = generate_expr(node->right);

        char *temp = new_temp();

        switch (node->op) {
            case '+': emit(IR_ADD, temp, left, right); break;
            case '-': emit(IR_SUB, temp, left, right); break;
            case '*': emit(IR_MUL, temp, left, right); break;
            case '/': emit(IR_DIV, temp, left, right); break;
        }

        return temp;
    }

    return NULL;
}

/* Pretty print IR */
void print_ir(IRInstruction *head) {
    IRInstruction *curr = head;

    while (curr) {

        switch (curr->op) {
            case IR_ASSIGN:
                printf("%s = %s\n", curr->result, curr->arg1);
                break;

            case IR_ADD:
                printf("%s = %s + %s\n", curr->result, curr->arg1, curr->arg2);
                break;

            case IR_SUB:
                printf("%s = %s - %s\n", curr->result, curr->arg1, curr->arg2);
                break;

            case IR_MUL:
                printf("%s = %s * %s\n", curr->result, curr->arg1, curr->arg2);
                break;

            case IR_DIV:
                printf("%s = %s / %s\n", curr->result, curr->arg1, curr->arg2);
                break;

            case IR_PRINT:
                printf("print %s\n", curr->result);
                break;
        }

        curr = curr->next;
    }
}