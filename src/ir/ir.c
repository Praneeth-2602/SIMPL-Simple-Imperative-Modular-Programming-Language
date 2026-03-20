#include "ir.h"
#include "../semantic/symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IRInstruction *ir_head = NULL;
static IRInstruction *ir_tail = NULL;

static int temp_count  = 0;
static int label_count = 0;

static char* new_temp() {
    char buf[32];
    sprintf(buf, "t%d", temp_count++);
    return strdup(buf);
}

static char* new_label() {
    char buf[32];
    sprintf(buf, "L%d", label_count++);
    return strdup(buf);
}

static void emit(IROp op, const char *res, const char *a1, const char *a2) {
    IRInstruction *inst = malloc(sizeof(IRInstruction));
    inst->op = op;
    snprintf(inst->result, sizeof(inst->result), "%s", res ? res : "");
    snprintf(inst->arg1,   sizeof(inst->arg1),   "%s", a1  ? a1  : "");
    snprintf(inst->arg2,   sizeof(inst->arg2),   "%s", a2  ? a2  : "");
    inst->cmp_op = 0;
    inst->prev   = ir_tail;
    inst->next   = NULL;
    if (!ir_head) { ir_head = ir_tail = inst; }
    else          { ir_tail->next = inst; ir_tail = inst; }
}

static void emit_cmp(const char *res, const char *a1, const char *a2, char cmp_op) {
    emit(IR_CMP, res, a1, a2);
    if (ir_tail) ir_tail->cmp_op = cmp_op;
}

static char* generate_expr(ASTNode *node);
static void  generate_node(ASTNode *node);

IRInstruction* generate_ir(ASTNode *root) {
    ir_head = ir_tail = NULL;
    temp_count = label_count = 0;
    generate_node(root);
    return ir_head;
}

/* ── ADT type tag helper ───────────────────────────────── */
static char adt_type_tag(int adt_code) {
    switch (adt_code) {
        case ADT_STACK: return 's';
        case ADT_QUEUE: return 'q';
        case ADT_TREE:  return 't';
        case ADT_GRAPH: return 'g';
        default:        return '?';
    }
}

/* ── Main AST→IR lowering ─────────────────────────────── */
static void generate_node(ASTNode *root) {
    if (!root) return;

    switch (root->type) {

        case AST_PROGRAM:
            generate_node(root->left);
            break;

        case AST_STATEMENT_LIST:
            generate_node(root->left);
            generate_node(root->right);
            break;

        case AST_DECL: {
            /* Integer declaration with initialiser */
            char *rhs = generate_expr(root->right);
            emit(IR_ASSIGN, root->left->name, rhs, NULL);
            break;
        }

        case AST_ASSIGN: {
            char *rhs = generate_expr(root->right);
            emit(IR_ASSIGN, root->left->name, rhs, NULL);
            break;
        }

        case AST_PRINT: {
            /* Check if printing an ADT variable — look up symbol table
             * directly since child inferred_type may not be set yet */
            if (root->left && root->left->type == AST_IDENTIFIER) {
                Type t = symtab_lookup(root->left->name);
                if (t == TYPE_STACK || t == TYPE_QUEUE ||
                    t == TYPE_TREE  || t == TYPE_GRAPH) {
                    char tag = (t == TYPE_STACK) ? 's' :
                               (t == TYPE_QUEUE) ? 'q' :
                               (t == TYPE_TREE)  ? 't' : 'g';
                    IRInstruction *inst;
                    emit(IR_PRINT_ADT, root->left->name, NULL, NULL);
                    inst = ir_tail;
                    inst->cmp_op = tag;
                    break;
                }
            }
            char *val = generate_expr(root->left);
            emit(IR_PRINT, val, NULL, NULL);
            break;
        }

        case AST_WHILE: {
            char *start = new_label();
            char *exit  = new_label();
            emit(IR_LABEL, start, NULL, NULL);
            char *cond = generate_expr(root->left);
            emit(IR_IF_FALSE_GOTO, exit, cond, NULL);
            generate_node(root->right);
            emit(IR_GOTO,  start, NULL, NULL);
            emit(IR_LABEL, exit,  NULL, NULL);
            break;
        }

        case AST_IF: {
            char *else_lbl = new_label();
            char *end_lbl  = new_label();
            char *cond = generate_expr(root->left);
            emit(IR_IF_FALSE_GOTO, else_lbl, cond, NULL);
            generate_node(root->right);
            emit(IR_GOTO,  end_lbl,  NULL, NULL);
            emit(IR_LABEL, else_lbl, NULL, NULL);
            if (root->third) generate_node(root->third);
            emit(IR_LABEL, end_lbl, NULL, NULL);
            break;
        }

        /* ── ADT Declaration ───────────────────────────────
         * node->left  = identifier
         * node->right = number node (ADT code: 0=stack …)  */
        case AST_ADT_DECL: {
            IRInstruction *inst;
            emit(IR_ADT_DECL, root->left->name, NULL, NULL);
            inst = ir_tail;
            inst->cmp_op = adt_type_tag(root->right->value);
            break;
        }

        /* ── ADT Operation ─────────────────────────────────
         * node->left  = identifier (ADT var name)
         * node->third = number node (op code char)
         * node->right = expression node (value arg, or BINOP
         *               holding two args for graph ops)      */
        case AST_ADT_OP: {
            const char *var  = root->left->name;
            int         op   = root->third->value;

            switch (op) {

                case OP_PUSH: {
                    char *val = generate_expr(root->right);
                    emit(IR_STACK_PUSH, var, val, NULL);
                    break;
                }
                case OP_POP:
                    emit(IR_STACK_POP, var, NULL, NULL);
                    break;

                case OP_ENQUEUE: {
                    char *val = generate_expr(root->right);
                    emit(IR_QUEUE_ENQUEUE, var, val, NULL);
                    break;
                }
                case OP_DEQUEUE:
                    emit(IR_QUEUE_DEQUEUE, var, NULL, NULL);
                    break;

                case OP_INSERT: {
                    char *val = generate_expr(root->right);
                    emit(IR_TREE_INSERT, var, val, NULL);
                    break;
                }
                case OP_REMOVE: {
                    char *val = generate_expr(root->right);
                    emit(IR_TREE_REMOVE, var, val, NULL);
                    break;
                }

                /* Graph ops carry two args packed in a dummy BINOP */
                case OP_ADD_EDGE: {
                    char *from = generate_expr(root->right->left);
                    char *to   = generate_expr(root->right->right);
                    emit(IR_GRAPH_ADD_EDGE, var, from, to);
                    break;
                }
                case OP_REMOVE_EDGE: {
                    char *from = generate_expr(root->right->left);
                    char *to   = generate_expr(root->right->right);
                    emit(IR_GRAPH_REMOVE_EDGE, var, from, to);
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ── Expression lowering ─────────────────────────────── */
static char* generate_expr(ASTNode *node) {
    if (!node) return strdup("0");

    if (node->type == AST_NUMBER) {
        char buf[32];
        sprintf(buf, "%d", node->value);
        return strdup(buf);
    }
    if (node->type == AST_IDENTIFIER) {
        return strdup(node->name);
    }
    if (node->type == AST_BINOP) {
        char *left  = generate_expr(node->left);
        char *right = generate_expr(node->right);
        char *temp  = new_temp();
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
    return strdup("0");
}

/* ── Pretty printer ──────────────────────────────────── */
void print_ir_instruction(IRInstruction *curr) {
    if (!curr) return;
    switch (curr->op) {
        case IR_NOP:   break;
        case IR_ASSIGN:
            printf("%s = %s\n", curr->result, curr->arg1); break;
        case IR_ADD:
            printf("%s = %s + %s\n", curr->result, curr->arg1, curr->arg2); break;
        case IR_SUB:
            printf("%s = %s - %s\n", curr->result, curr->arg1, curr->arg2); break;
        case IR_MUL:
            printf("%s = %s * %s\n", curr->result, curr->arg1, curr->arg2); break;
        case IR_DIV:
            printf("%s = %s / %s\n", curr->result, curr->arg1, curr->arg2); break;
        case IR_PRINT:
            printf("print %s\n", curr->result); break;
        case IR_LABEL:
            printf("%s:\n", curr->result); break;
        case IR_GOTO:
            printf("goto %s\n", curr->result); break;
        case IR_IF_FALSE_GOTO:
            printf("if_false %s goto %s\n", curr->arg1, curr->result); break;
        case IR_CMP:
            printf("%s = %s %c %s\n",
                   curr->result, curr->arg1, curr->cmp_op, curr->arg2); break;
        case IR_ADT_DECL:
            printf("adt_decl %s [%c]\n", curr->result, curr->cmp_op); break;
        case IR_STACK_PUSH:
            printf("stack_push %s %s\n", curr->result, curr->arg1); break;
        case IR_STACK_POP:
            printf("stack_pop %s\n", curr->result); break;
        case IR_QUEUE_ENQUEUE:
            printf("queue_enqueue %s %s\n", curr->result, curr->arg1); break;
        case IR_QUEUE_DEQUEUE:
            printf("queue_dequeue %s\n", curr->result); break;
        case IR_TREE_INSERT:
            printf("tree_insert %s %s\n", curr->result, curr->arg1); break;
        case IR_TREE_REMOVE:
            printf("tree_remove %s %s\n", curr->result, curr->arg1); break;
        case IR_GRAPH_ADD_EDGE:
            printf("graph_add_edge %s %s %s\n",
                   curr->result, curr->arg1, curr->arg2); break;
        case IR_GRAPH_REMOVE_EDGE:
            printf("graph_remove_edge %s %s %s\n",
                   curr->result, curr->arg1, curr->arg2); break;
        case IR_PRINT_ADT:
            printf("print_adt %s [%c]\n", curr->result, curr->cmp_op); break;
    }
}

void print_ir(IRInstruction *head) {
    for (IRInstruction *c = head; c; c = c->next)
        if (c->op != IR_NOP)
            print_ir_instruction(c);
}
