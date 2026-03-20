#include "basic_block.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int block_count = 0;

static BasicBlock* new_block(IRInstruction *start) {
    BasicBlock *block = malloc(sizeof(BasicBlock));
    block->id = block_count++;
    block->start = start;
    block->end = NULL;
    block->next = NULL;
    block->succ1 = block->succ2 = NULL;
    block->pred1 = block->pred2 = NULL;
    return block;
}

BasicBlock* build_basic_blocks(IRInstruction *ir) {
    if (!ir) return NULL;

    block_count = 0;

    BasicBlock *head = NULL;
    BasicBlock *current_block = NULL;

    IRInstruction *curr = ir;

    while (curr) {
        int is_leader = 0;

        /* First instruction is always a leader */
        if (curr == ir) {
            is_leader = 1;
        }

        /* Labels mark block starts */
        if (curr->op == IR_LABEL) {
            is_leader = 1;
        }

        /* Instruction immediately following a jump starts a block */
        if (curr->prev && (curr->prev->op == IR_GOTO || curr->prev->op == IR_IF_FALSE_GOTO)) {
            is_leader = 1;
        }

        if (is_leader) {
            BasicBlock *newblk = new_block(curr);

            if (!head) {
                head = newblk;
            } else {
                current_block->next = newblk;
            }

            current_block = newblk;
        }

        if (current_block) {
            current_block->end = curr;
        }

        curr = curr->next;
    }

    return head;
}

static void add_label(LabelMap **labels, const char *label, BasicBlock *block) {
    LabelMap *node = malloc(sizeof(LabelMap));
    strncpy(node->label, label, sizeof(node->label) - 1);
    node->label[sizeof(node->label) - 1] = '\0';
    node->block = block;
    node->next = *labels;
    *labels = node;
}

LabelMap* build_label_map(BasicBlock *head) {
    LabelMap *labels = NULL;
    BasicBlock *curr = head;

    while (curr) {
        if (curr->start && curr->start->op == IR_LABEL) {
            add_label(&labels, curr->start->result, curr);
        }
        curr = curr->next;
    }

    return labels;
}

static BasicBlock* lookup_label(LabelMap *labels, const char *label) {
    while (labels) {
        if (strcmp(labels->label, label) == 0) {
            return labels->block;
        }
        labels = labels->next;
    }
    return NULL;
}

static void add_pred(BasicBlock *target, BasicBlock *pred) {
    if (!target || !pred) return;
    if (!target->pred1) {
        target->pred1 = pred;
    } else if (!target->pred2 && target->pred1 != pred) {
        target->pred2 = pred;
    }
}

void build_cfg(BasicBlock *head, LabelMap *labels) {
    BasicBlock *curr = head;

    while (curr) {
        IRInstruction *last = curr->end;

        if (!last) {
            curr = curr->next;
            continue;
        }

        if (last->op == IR_GOTO) {
            BasicBlock *target = lookup_label(labels, last->result);
            curr->succ1 = target;
            add_pred(target, curr);
        }
        else if (last->op == IR_IF_FALSE_GOTO) {
            BasicBlock *target = lookup_label(labels, last->result);
            curr->succ1 = target;
            add_pred(target, curr);

            if (curr->next) {
                curr->succ2 = curr->next;
                add_pred(curr->next, curr);
            }
        }
        else {
            if (curr->next) {
                curr->succ1 = curr->next;
                add_pred(curr->next, curr);
            }
        }

        curr = curr->next;
    }
}

void print_basic_blocks(BasicBlock *head) {
    while (head) {
        printf("\nBasic Block %d:\n", head->id);

        IRInstruction *curr = head->start;
        while (curr) {
            print_ir_instruction(curr);
            if (curr == head->end) {
                break;
            }
            curr = curr->next;
        }

        head = head->next;
    }
}

void print_cfg(BasicBlock *head) {
    while (head) {
        printf("Block %d ->", head->id);

        if (head->succ1) {
            printf(" B%d", head->succ1->id);
        }

        if (head->succ2) {
            printf(" B%d", head->succ2->id);
        }

        printf("\n");
        head = head->next;
    }
}
