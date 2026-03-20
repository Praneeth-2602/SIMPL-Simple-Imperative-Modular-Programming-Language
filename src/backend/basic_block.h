#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H

#include "../ir/ir.h"

typedef struct BasicBlock {
    int id;

    IRInstruction *start;   /* first instruction in block */
    IRInstruction *end;     /* last instruction in block */

    struct BasicBlock *next;  /* next block in list */

    struct BasicBlock *succ1; /* primary successor */
    struct BasicBlock *succ2; /* secondary successor */

    struct BasicBlock *pred1; /* primary predecessor */
    struct BasicBlock *pred2; /* secondary predecessor */
} BasicBlock;

typedef struct LabelMap {
    char label[32];
    BasicBlock *block;
    struct LabelMap *next;
} LabelMap;

BasicBlock* build_basic_blocks(IRInstruction *ir);
LabelMap* build_label_map(BasicBlock *head);
void build_cfg(BasicBlock *head, LabelMap *labels);
void print_basic_blocks(BasicBlock *head);
void print_cfg(BasicBlock *head);

#endif
