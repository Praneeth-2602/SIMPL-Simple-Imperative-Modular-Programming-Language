#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IRInstruction *ir_head = NULL;
static IRInstruction *ir_tail = NULL;

static int temp_count = 0;
static int label_count = 0;

/* Generate new temporary variable */
static char* new_temp() {
    char buffer[32];
    sprintf(buffer, "t%d", temp_count++);
    return strdup(buffer);
}

/* Generate new label */
static char* new_label() {
    char buffer[32];
    sprintf(buffer, "L%d", label_count++);
    return strdup(buffer);
}

/* Append instruction */
static void emit(IROp op, const char *res, const char *a1, const char *a2) {
    IRInstruction *inst = malloc(sizeof(IRInstruction));
    inst->op = op;

    strcpy(inst->result, res ? res : "");
    strcpy(inst->arg1, a1 ? a1 : "");
    strcpy(inst->arg2, a2 ? a2 : "");
    inst->cmp_op = 0;

    inst->next = NULL;

    if (!ir_head) {
        ir_head = ir_tail = inst;
    } else {
        ir_tail->next = inst;
        ir_tail = inst;
    }
}

/* Emit comparison with operator */
static void emit_cmp(const char *res, const char *a1, const char *a2, char cmp_op) {
    emit(IR_CMP, res, a1, a2);
    if (ir_tail) {
        ir_tail->cmp_op = cmp_op;
    }
}

/* Forward declarations */
static char* generate_expr(ASTNode *node);
static void generate_node(ASTNode *node);

/* Generate IR from AST */
IRInstruction* generate_ir(ASTNode *root) {
    ir_head = ir_tail = NULL;
    temp_count = 0;
    label_count = 0;

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

    else if (root->type == AST_WHILE) {
        char *start_label = new_label();
        char *exit_label = new_label();

        emit(IR_LABEL, start_label, NULL, NULL);

        char *cond_temp = generate_expr(root->left);
        emit(IR_IF_FALSE_GOTO, exit_label, cond_temp, NULL);

        generate_node(root->right);

        emit(IR_GOTO, start_label, NULL, NULL);
        emit(IR_LABEL, exit_label, NULL, NULL);
    }

    else if (root->type == AST_IF) {
        char *else_label = new_label();
        char *end_label = new_label();

        char *cond_temp = generate_expr(root->left);
        emit(IR_IF_FALSE_GOTO, else_label, cond_temp, NULL);

        generate_node(root->right);

        emit(IR_GOTO, end_label, NULL, NULL);
        emit(IR_LABEL, else_label, NULL, NULL);

        if (root->third) {
            generate_node(root->third);
        }

        emit(IR_LABEL, end_label, NULL, NULL);
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
            case '>': emit_cmp(temp, left, right, '>'); break;
            case '<': emit_cmp(temp, left, right, '<'); break;
            case '=': emit_cmp(temp, left, right, '='); break;
            case '!': emit_cmp(temp, left, right, '!'); break;
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

            case IR_LABEL:
                printf("%s:\n", curr->result);
                break;

            case IR_GOTO:
                printf("goto %s\n", curr->result);
                break;

            case IR_IF_FALSE_GOTO:
                printf("if_false %s goto %s\n", curr->arg1, curr->result);
                break;

            case IR_CMP:
                printf("%s = %s %c %s\n", curr->result, curr->arg1, curr->cmp_op, curr->arg2);
                break;
        }

        curr = curr->next;
    }
}