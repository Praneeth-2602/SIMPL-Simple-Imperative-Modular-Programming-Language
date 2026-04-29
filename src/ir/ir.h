#ifndef IR_H
#define IR_H

#include "../parser/ast.h"

typedef enum {
    IR_NOP,
    IR_ASSIGN,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_PRINT,
    IR_LABEL,
    IR_GOTO,
    IR_IF_FALSE_GOTO,
    IR_CMP,

    /* ── ADT opcodes ─────────────────────────────────────────
     * result = ADT variable name
     * arg1   = value being pushed/enqueued/inserted (or "")
     * arg2   = second argument (graph: destination node)
     *
     * For IR_ADT_DECL the ADT type is encoded in cmp_op:
     *   's' = stack   'q' = queue   't' = tree   'g' = graph
     * ──────────────────────────────────────────────────────── */
    IR_ADT_DECL,          /* declare ADT variable              */
    IR_STACK_PUSH,        /* push  <var> <value>               */
    IR_STACK_POP,         /* pop   <var>                       */
    IR_QUEUE_ENQUEUE,     /* enqueue <var> <value>             */
    IR_QUEUE_DEQUEUE,     /* dequeue <var>                     */
    IR_TREE_INSERT,       /* insert <var> <value>              */
    IR_TREE_REMOVE,       /* remove <var> <value>              */
    IR_GRAPH_ADD_EDGE,    /* add_edge    <var> <from> <to>     */
    IR_GRAPH_REMOVE_EDGE, /* remove_edge <var> <from> <to>     */

    /* ADT print — result = var name, cmp_op = type tag (s/q/t/g) */
    IR_PRINT_ADT,

    IR_FUNC_BEGIN,
    IR_PARAM_DECL,
    IR_FUNC_END,
    IR_RETURN,
    IR_CALL,
    IR_CALL_ARG
} IROp;

typedef struct IRInstruction {
    IROp op;

    char result[32];   /* destination / ADT variable name      */
    char arg1[32];     /* first source operand / graph 'from'  */
    char arg2[32];     /* second source / graph 'to'           */

    /* For IR_CMP: relational operator (>, <, =, !)
     * For IR_ADT_DECL: ADT type tag ('s','q','t','g')        */
    char cmp_op;

    struct IRInstruction *prev;
    struct IRInstruction *next;
} IRInstruction;

/* IR API */
IRInstruction* generate_ir(ASTNode *root);
void print_ir(IRInstruction *head);
void print_ir_instruction(IRInstruction *inst);

#endif
