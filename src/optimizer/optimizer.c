/* ============================================================
 * SIMPL Optimizer
 * ============================================================
 * Implements three optimization passes over the TAC IR:
 *   1. Constant Folding
 *   2. Dead Code Elimination (DCE)
 *   3. Unreachable Block Elimination
 * ============================================================ */

#include "optimizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * SECTION 1 — INTERNAL IR EXTENSION
 *
 * The IRInstruction struct in ir.h has no "is_dead" field, so
 * we maintain a parallel dead-flag table keyed by pointer
 * identity.  This keeps optimizer.c self-contained and avoids
 * touching ir.h / ir.c.
 * ============================================================ */

#define MAX_IR_INSTRS 4096

typedef struct {
    IRInstruction *instr;
    int            is_dead;
} DeadEntry;

static DeadEntry dead_table[MAX_IR_INSTRS];
static int       dead_table_size = 0;

static void dead_table_reset(void) {
    dead_table_size = 0;
}

/* Register an instruction (first time we see it). */
static void dead_table_register(IRInstruction *inst) {
    for (int i = 0; i < dead_table_size; i++) {
        if (dead_table[i].instr == inst) return;
    }
    if (dead_table_size < MAX_IR_INSTRS) {
        dead_table[dead_table_size].instr   = inst;
        dead_table[dead_table_size].is_dead = 0;
        dead_table_size++;
    }
}

static void dead_table_mark(IRInstruction *inst) {
    for (int i = 0; i < dead_table_size; i++) {
        if (dead_table[i].instr == inst) {
            dead_table[i].is_dead = 1;
            return;
        }
    }
}

int is_dead(IRInstruction *inst) {
    for (int i = 0; i < dead_table_size; i++) {
        if (dead_table[i].instr == inst) {
            return dead_table[i].is_dead;
        }
    }
    return 0;
}

/* ============================================================
 * SECTION 2 — HELPER UTILITIES
 * ============================================================ */

/* Is the string a decimal integer literal? */
static int is_constant(const char *s) {
    if (!s || s[0] == '\0') return 0;
    int i = 0;
    if (s[i] == '-') i++;          /* allow negative constants */
    if (s[i] == '\0') return 0;
    for (; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
    }
    return 1;
}

static const char *instr_defines(IRInstruction *inst) {
    switch (inst->op) {
        case IR_ASSIGN:
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
        case IR_CMP:
        case IR_CALL:
            return inst->result;
        default:
            return NULL;
    }
}

/* Is this a "side-effect" instruction that must never be removed
 * even if its result is unused? */
static int has_side_effect(IRInstruction *inst) {
    switch (inst->op) {
        case IR_PRINT:
        case IR_GOTO:
        case IR_IF_FALSE_GOTO:
        case IR_LABEL:
        case IR_ADT_DECL:
        case IR_STACK_PUSH:
        case IR_STACK_POP:
        case IR_QUEUE_ENQUEUE:
        case IR_QUEUE_DEQUEUE:
        case IR_TREE_INSERT:
        case IR_TREE_REMOVE:
        case IR_GRAPH_ADD_EDGE:
        case IR_GRAPH_REMOVE_EDGE:
        case IR_PRINT_ADT:
        case IR_FUNC_BEGIN:
        case IR_PARAM_DECL:
        case IR_FUNC_END:
        case IR_RETURN:
        case IR_CALL:
        case IR_CALL_ARG:
            return 1;
        default:
            return 0;
    }
}


/* Find the label string that a GOTO / IF_FALSE_GOTO targets. */
typedef struct {
    char names[128][32];
    char values[128][32];
    int count;
} ConstantSet;

static void constants_clear(ConstantSet *set) {
    set->count = 0;
}

static int constants_find(ConstantSet *set, const char *name) {
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->names[i], name) == 0) return i;
    }
    return -1;
}

static const char *constants_get(ConstantSet *set, const char *name) {
    int idx = constants_find(set, name);
    return idx >= 0 ? set->values[idx] : NULL;
}

static void constants_set(ConstantSet *set, const char *name, const char *value) {
    int idx;

    if (!name || name[0] == '\0' || !value || !is_constant(value)) return;

    idx = constants_find(set, name);
    if (idx < 0) {
        if (set->count >= 128) return;
        idx = set->count++;
    }

    strncpy(set->names[idx], name, sizeof(set->names[idx]) - 1);
    set->names[idx][sizeof(set->names[idx]) - 1] = '\0';
    strncpy(set->values[idx], value, sizeof(set->values[idx]) - 1);
    set->values[idx][sizeof(set->values[idx]) - 1] = '\0';
}

static void constants_kill(ConstantSet *set, const char *name) {
    int idx = constants_find(set, name);
    if (idx < 0) return;

    if (idx != set->count - 1) {
        strcpy(set->names[idx], set->names[set->count - 1]);
        strcpy(set->values[idx], set->values[set->count - 1]);
    }
    set->count--;
}

static int replace_with_known_constant(char *operand, size_t size, ConstantSet *set) {
    const char *value;

    if (!operand || operand[0] == '\0' || is_constant(operand)) return 0;

    value = constants_get(set, operand);
    if (!value) return 0;

    strncpy(operand, value, size - 1);
    operand[size - 1] = '\0';
    return 1;
}

static int evaluate_cmp(int a, int b, char cmp_op, int *result) {
    switch (cmp_op) {
        case '>': *result = (a > b) ? 1 : 0; return 1;
        case '<': *result = (a < b) ? 1 : 0; return 1;
        case '=': *result = (a == b) ? 1 : 0; return 1;
        case '!': *result = (a != b) ? 1 : 0; return 1;
        default: return 0;
    }
}

int constant_propagation(IRInstruction *ir_head) {
    ConstantSet set;
    IRInstruction *inst = ir_head;
    int propagated = 0;

    constants_clear(&set);

    while (inst) {
        const char *def = instr_defines(inst);

        if (inst->op == IR_LABEL) {
            constants_clear(&set);
        }

        switch (inst->op) {
            case IR_ASSIGN:
                propagated += replace_with_known_constant(inst->arg1, sizeof(inst->arg1), &set);
                if (is_constant(inst->arg1)) {
                    constants_set(&set, inst->result, inst->arg1);
                } else {
                    constants_kill(&set, inst->result);
                }
                break;

            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV:
            case IR_CMP:
                propagated += replace_with_known_constant(inst->arg1, sizeof(inst->arg1), &set);
                propagated += replace_with_known_constant(inst->arg2, sizeof(inst->arg2), &set);
                constants_kill(&set, inst->result);
                break;

            case IR_PRINT:
                propagated += replace_with_known_constant(inst->result, sizeof(inst->result), &set);
                break;

            case IR_RETURN:
            case IR_CALL_ARG:
                propagated += replace_with_known_constant(inst->result, sizeof(inst->result), &set);
                break;

            case IR_IF_FALSE_GOTO:
                propagated += replace_with_known_constant(inst->arg1, sizeof(inst->arg1), &set);
                constants_clear(&set);
                break;

            case IR_GOTO:
                constants_clear(&set);
                break;

            default:
                if (def) {
                    constants_kill(&set, def);
                }
                break;
        }

        inst = inst->next;
    }

    return propagated;
}

int simplify_constant_branches(IRInstruction *ir_head) {
    int simplified = 0;
    IRInstruction *inst = ir_head;

    while (inst) {
        if (inst->op == IR_IF_FALSE_GOTO && is_constant(inst->arg1)) {
            if (atoi(inst->arg1) == 0) {
                inst->op = IR_GOTO;
                inst->arg1[0] = '\0';
                inst->arg2[0] = '\0';
            } else {
                inst->op = IR_NOP;
                inst->result[0] = '\0';
                inst->arg1[0] = '\0';
                inst->arg2[0] = '\0';
            }
            simplified++;
        }
        inst = inst->next;
    }

    return simplified;
}

/* ============================================================
 * SECTION 3 — CFG CONSTRUCTION
 * ============================================================ */

/* Find an existing block that starts with a given label, or NULL. */
static BasicBlock *find_block_by_label(CFG *cfg, const char *label) {
    for (int i = 0; i < cfg->count; i++) {
        IRInstruction *first = cfg->blocks[i]->first;
        if (first && first->op == IR_LABEL &&
            strcmp(first->result, label) == 0) {
            return cfg->blocks[i];
        }
    }
    return NULL;
}

/* Allocate and register a new basic block. */
static BasicBlock *new_block(CFG *cfg) {
    if (cfg->count >= MAX_BLOCKS) {
        fprintf(stderr, "[CFG] Too many basic blocks (max %d)\n", MAX_BLOCKS);
        return NULL;
    }
    BasicBlock *b = calloc(1, sizeof(BasicBlock));
    b->id          = cfg->count;
    b->is_reachable = 0;
    cfg->blocks[cfg->count++] = b;
    return b;
}

/* Add a CFG edge from src → dst. */
static void add_edge(BasicBlock *src, BasicBlock *dst) {
    if (!src || !dst) return;
    /* Avoid duplicates */
    for (int i = 0; i < src->succ_count; i++) {
        if (src->successors[i] == dst) return;
    }
    if (src->succ_count < MAX_SUCCESSORS)
        src->successors[src->succ_count++] = dst;
    if (dst->pred_count < MAX_PREDECESSORS)
        dst->predecessors[dst->pred_count++] = src;
}

CFG *build_cfg(IRInstruction *ir_head) {
    if (!ir_head) return NULL;

    CFG *cfg = calloc(1, sizeof(CFG));
    dead_table_reset();

    /* ── PASS A: identify leaders and slice into blocks ── */

    /* A "leader" is the first instruction of a block:
     *   rule 1 — the very first instruction
     *   rule 2 — any LABEL instruction
     *   rule 3 — any instruction immediately following a GOTO
     *            or IF_FALSE_GOTO
     */

    BasicBlock *cur = new_block(cfg);
    cur->first = ir_head;

    IRInstruction *inst = ir_head;
    IRInstruction *prev = NULL;

    while (inst) {
        dead_table_register(inst);

        /* Rule 2: LABEL starts a new block (unless it IS the first). */
        if (inst->op == IR_LABEL && inst != ir_head) {
            cur->last = prev;
            cur = new_block(cfg);
            cur->first = inst;
        }

        /* Rule 3: instruction after a jump starts a new block. */
        if (prev &&
            (prev->op == IR_GOTO || prev->op == IR_IF_FALSE_GOTO) &&
            inst->op != IR_LABEL) {   /* LABEL already handled above */
            cur->last = prev;
            cur = new_block(cfg);
            cur->first = inst;
        }

        prev = inst;
        inst = inst->next;
    }
    /* Close the last block */
    if (cur) cur->last = prev;

    /* ── PASS B: wire CFG edges ── */

    for (int i = 0; i < cfg->count; i++) {
        BasicBlock *b    = cfg->blocks[i];
        IRInstruction *last = b->last;
        if (!last) continue;

        if (last->op == IR_GOTO) {
            /* Unconditional jump — one successor (the target label). */
            BasicBlock *target = find_block_by_label(cfg, last->result);
            add_edge(b, target);

        } else if (last->op == IR_IF_FALSE_GOTO) {
            /* Conditional branch — two successors:
             *   taken  → target label
             *   fallthrough → next block (i+1)  */
            BasicBlock *target = find_block_by_label(cfg, last->result);
            add_edge(b, target);
            if (i + 1 < cfg->count)
                add_edge(b, cfg->blocks[i + 1]);

        } else {
            /* Fall-through to next block */
            if (i + 1 < cfg->count)
                add_edge(b, cfg->blocks[i + 1]);
        }
    }

    return cfg;
}

/* ============================================================
 * SECTION 4 — PASS 1: CONSTANT FOLDING
 *
 * Walks every instruction in the flat IR list.  When both
 * operands of an arithmetic or comparison instruction are
 * integer literals, the result is computed at compile time
 * and the instruction is replaced with a simple assignment:
 *
 *   t0 = 5 + 3    →    t0 = 8
 *   t1 = 10 > 3   →    t1 = 1   (true)
 *
 * The instruction's opcode is changed to IR_ASSIGN and arg1
 * is set to the folded literal string.  arg2 is cleared.
 * ============================================================ */

int constant_folding(IRInstruction *ir_head) {
    int folded = 0;
    int changed;

    do {
        IRInstruction *inst = ir_head;
        changed = 0;

        while (inst) {
            if (inst->op == IR_ADD || inst->op == IR_SUB ||
                inst->op == IR_MUL || inst->op == IR_DIV ||
                inst->op == IR_CMP) {

                if (is_constant(inst->arg1) && is_constant(inst->arg2)) {
                    int a = atoi(inst->arg1);
                    int b = atoi(inst->arg2);
                    int result = 0;
                    int valid  = 1;

                    switch (inst->op) {
                        case IR_ADD: result = a + b; break;
                        case IR_SUB: result = a - b; break;
                        case IR_MUL: result = a * b; break;
                        case IR_DIV:
                            if (b == 0) {
                                fprintf(stderr,
                                    "[CF] Warning: division by zero in constant "
                                    "expression, skipping fold.\n");
                                valid = 0;
                            } else {
                                result = a / b;
                            }
                            break;
                        case IR_CMP:
                            valid = evaluate_cmp(a, b, inst->cmp_op, &result);
                            break;
                        default:
                            valid = 0;
                            break;
                    }

                    if (valid) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", result);

                        inst->op = IR_ASSIGN;
                        strncpy(inst->arg1, buf, sizeof(inst->arg1) - 1);
                        inst->arg1[sizeof(inst->arg1) - 1] = '\0';
                        inst->arg2[0] = '\0';
                        inst->cmp_op = 0;
                        changed++;
                        inst = inst->next;
                        continue;
                    }
                }

                if (inst->op == IR_ADD) {
                    if (is_constant(inst->arg2) && atoi(inst->arg2) == 0) {
                        inst->op = IR_ASSIGN;
                        inst->arg2[0] = '\0';
                        changed++;
                    } else if (is_constant(inst->arg1) && atoi(inst->arg1) == 0) {
                        inst->op = IR_ASSIGN;
                        strncpy(inst->arg1, inst->arg2, sizeof(inst->arg1) - 1);
                        inst->arg1[sizeof(inst->arg1) - 1] = '\0';
                        inst->arg2[0] = '\0';
                        changed++;
                    }
                } else if (inst->op == IR_SUB) {
                    if (is_constant(inst->arg2) && atoi(inst->arg2) == 0) {
                        inst->op = IR_ASSIGN;
                        inst->arg2[0] = '\0';
                        changed++;
                    }
                } else if (inst->op == IR_MUL) {
                    if ((is_constant(inst->arg1) && atoi(inst->arg1) == 0) ||
                        (is_constant(inst->arg2) && atoi(inst->arg2) == 0)) {
                        inst->op = IR_ASSIGN;
                        strncpy(inst->arg1, "0", sizeof(inst->arg1) - 1);
                        inst->arg1[sizeof(inst->arg1) - 1] = '\0';
                        inst->arg2[0] = '\0';
                        inst->cmp_op = 0;
                        changed++;
                    } else if (is_constant(inst->arg2) && atoi(inst->arg2) == 1) {
                        inst->op = IR_ASSIGN;
                        inst->arg2[0] = '\0';
                        changed++;
                    } else if (is_constant(inst->arg1) && atoi(inst->arg1) == 1) {
                        inst->op = IR_ASSIGN;
                        strncpy(inst->arg1, inst->arg2, sizeof(inst->arg1) - 1);
                        inst->arg1[sizeof(inst->arg1) - 1] = '\0';
                        inst->arg2[0] = '\0';
                        changed++;
                    }
                } else if (inst->op == IR_DIV) {
                    if (is_constant(inst->arg2) && atoi(inst->arg2) == 1) {
                        inst->op = IR_ASSIGN;
                        inst->arg2[0] = '\0';
                        changed++;
                    }
                }
            }

            inst = inst->next;
        }

        folded += changed;
    } while (changed > 0);

    return folded;
}

/* ============================================================
 * SECTION 5 — PASS 2: DEAD CODE ELIMINATION
 *
 * Algorithm (per basic block, backward scan):
 *
 *   live = {}
 *   for each instruction I from last to first:
 *       if I has side effects → keep I, add uses(I) to live
 *       else if result(I) ∈ live:
 *           keep I, remove result(I) from live, add uses(I)
 *       else:
 *           mark I as dead
 *
 * "Temporaries" (names starting with 't' followed by digits)
 * are eligible for elimination.  User-declared variables are
 * conservatively kept because they may be observed by PRINT
 * or future statements not visible in this block.
 * ============================================================ */

/* A simple fixed-size set of variable names for liveness. */
#define LIVE_SET_SIZE 128

typedef struct {
    char names[LIVE_SET_SIZE][32];
    int  count;
} LiveSet;

static void live_clear(LiveSet *s) { s->count = 0; }

static void live_copy(LiveSet *dst, const LiveSet *src) {
    dst->count = src->count;
    for (int i = 0; i < src->count; i++) {
        strncpy(dst->names[i], src->names[i], sizeof(dst->names[i]) - 1);
        dst->names[i][sizeof(dst->names[i]) - 1] = '\0';
    }
}

static int live_contains(LiveSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) return 1;
    }
    return 0;
}

static void live_add(LiveSet *s, const char *name) {
    if (!name || name[0] == '\0') return;
    if (live_contains(s, name))   return;
    if (s->count < LIVE_SET_SIZE) {
        strncpy(s->names[s->count], name, 31);
        s->names[s->count][31] = '\0';
        s->count++;
    }
}

static void live_remove(LiveSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) {
            /* Replace with last entry when removing a non-last slot. */
            if (i != s->count - 1) {
                strncpy(s->names[i], s->names[s->count - 1], 31);
                s->names[i][31] = '\0';
            }
            s->names[s->count - 1][0] = '\0';
            s->count--;
            return;
        }
    }
}

static int live_union_into(LiveSet *dst, LiveSet *src) {
    int changed = 0;
    for (int i = 0; i < src->count; i++) {
        if (!live_contains(dst, src->names[i])) {
            live_add(dst, src->names[i]);
            changed = 1;
        }
    }
    return changed;
}

static int live_equal(const LiveSet *a, const LiveSet *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++) {
        if (!live_contains((LiveSet *) b, a->names[i])) return 0;
    }
    return 1;
}

/* Add all operands that 'inst' reads into the live set. */
static void live_add_uses(LiveSet *live, IRInstruction *inst) {
    switch (inst->op) {
        case IR_ASSIGN:
            if (!is_constant(inst->arg1))
                live_add(live, inst->arg1);
            break;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
        case IR_CMP:
            if (!is_constant(inst->arg1)) live_add(live, inst->arg1);
            if (!is_constant(inst->arg2)) live_add(live, inst->arg2);
            break;
        case IR_PRINT:
            if (!is_constant(inst->result)) live_add(live, inst->result);
            break;
        case IR_RETURN:
        case IR_CALL_ARG:
            if (!is_constant(inst->result)) live_add(live, inst->result);
            break;
        case IR_IF_FALSE_GOTO:
            if (!is_constant(inst->arg1)) live_add(live, inst->arg1);
            break;
        default:
            break;
    }
}

static void compute_block_use_def(BasicBlock *b, LiveSet *use, LiveSet *def) {
    IRInstruction *cur = b->first;

    live_clear(use);
    live_clear(def);

    while (cur) {
        if (!is_dead(cur) && cur->op != IR_NOP) {
            if (cur->op == IR_ASSIGN) {
                if (!is_constant(cur->arg1) && !live_contains(def, cur->arg1)) {
                    live_add(use, cur->arg1);
                }
            } else if (cur->op == IR_ADD || cur->op == IR_SUB ||
                       cur->op == IR_MUL || cur->op == IR_DIV ||
                       cur->op == IR_CMP) {
                if (!is_constant(cur->arg1) && !live_contains(def, cur->arg1)) {
                    live_add(use, cur->arg1);
                }
                if (!is_constant(cur->arg2) && !live_contains(def, cur->arg2)) {
                    live_add(use, cur->arg2);
                }
            } else if (cur->op == IR_PRINT) {
                if (!is_constant(cur->result) && !live_contains(def, cur->result)) {
                    live_add(use, cur->result);
                }
            } else if (cur->op == IR_RETURN || cur->op == IR_CALL_ARG) {
                if (!is_constant(cur->result) && !live_contains(def, cur->result)) {
                    live_add(use, cur->result);
                }
            } else if (cur->op == IR_IF_FALSE_GOTO) {
                if (!is_constant(cur->arg1) && !live_contains(def, cur->arg1)) {
                    live_add(use, cur->arg1);
                }
            }

            const char *defined = instr_defines(cur);
            if (defined && defined[0] != '\0' && !live_contains(def, defined)) {
                live_add(def, defined);
            }
        }

        if (cur == b->last) break;
        cur = cur->next;
    }
}

static int dce_block(BasicBlock *b, LiveSet *live_out) {
    if (!b->first || !b->last) return 0;

    int removed = 0;
    LiveSet live;
    live_copy(&live, live_out);

    /* Collect the block's instructions into a temp array for
     * convenient backward iteration. */
    IRInstruction *instrs[MAX_INSTRS_PER_BLOCK];
    int n = 0;

    IRInstruction *cur = b->first;
    while (cur && n < MAX_INSTRS_PER_BLOCK) {
        instrs[n++] = cur;
        if (cur == b->last) break;
        cur = cur->next;
    }

    /* Backward scan */
    for (int i = n - 1; i >= 0; i--) {
        IRInstruction *inst = instrs[i];

        if (is_dead(inst) || inst->op == IR_NOP) {
            continue;
        }

        if (has_side_effect(inst)) {
            /* Always keep; mark its inputs live. */
            live_add_uses(&live, inst);
            continue;
        }

        /* Does this instruction define a name? */
        const char *def = instr_defines(inst);

        if (!def) {
            /* No definition — keep conservatively. */
            live_add_uses(&live, inst);
            continue;
        }

        if (live_contains(&live, def)) {
            /* This definition is needed — keep it. */
            live_remove(&live, def);
            live_add_uses(&live, inst);
        } else {
            /* Value is never read on any outgoing path. */
            dead_table_mark(inst);
            removed++;
        }
    }

    return removed;
}

int dead_code_elimination(CFG *cfg) {
    if (!cfg) return 0;

    LiveSet *use = calloc(cfg->count, sizeof(LiveSet));
    LiveSet *def = calloc(cfg->count, sizeof(LiveSet));
    LiveSet *live_in = calloc(cfg->count, sizeof(LiveSet));
    LiveSet *live_out = calloc(cfg->count, sizeof(LiveSet));
    int changed;

    if (!use || !def || !live_in || !live_out) {
        fprintf(stderr, "[DCE] Failed to allocate liveness sets.\n");
        free(use);
        free(def);
        free(live_in);
        free(live_out);
        return 0;
    }

    for (int i = 0; i < cfg->count; i++) {
        compute_block_use_def(cfg->blocks[i], &use[i], &def[i]);
        live_clear(&live_in[i]);
        live_clear(&live_out[i]);
    }

    do {
        changed = 0;
        for (int i = cfg->count - 1; i >= 0; i--) {
            LiveSet next_out;
            LiveSet next_in;

            live_clear(&next_out);
            for (int j = 0; j < cfg->blocks[i]->succ_count; j++) {
                BasicBlock *succ = cfg->blocks[i]->successors[j];
                if (succ) {
                    live_union_into(&next_out, &live_in[succ->id]);
                }
            }

            live_copy(&next_in, &use[i]);
            for (int j = 0; j < next_out.count; j++) {
                if (!live_contains(&def[i], next_out.names[j])) {
                    live_add(&next_in, next_out.names[j]);
                }
            }

            if (!live_equal(&next_out, &live_out[i])) {
                live_copy(&live_out[i], &next_out);
                changed = 1;
            }
            if (!live_equal(&next_in, &live_in[i])) {
                live_copy(&live_in[i], &next_in);
                changed = 1;
            }
        }
    } while (changed);

    int total = 0;
    for (int i = 0; i < cfg->count; i++) {
        total += dce_block(cfg->blocks[i], &live_out[i]);
    }

    free(use);
    free(def);
    free(live_in);
    free(live_out);

    return total;
}

/* ============================================================
 * SECTION 6 — PASS 3: UNREACHABLE BLOCK ELIMINATION
 *
 * Uses a simple BFS/DFS reachability traversal starting from
 * the entry block (index 0).  Any block not reached is marked
 * unreachable and all its instructions are flagged dead.
 * ============================================================ */

int unreachable_block_elimination(CFG *cfg) {
    if (!cfg || cfg->count == 0) return 0;

    /* BFS worklist */
    BasicBlock *queue[MAX_BLOCKS];
    int head_q = 0, tail_q = 0;

    /* Seed with entry block */
    cfg->blocks[0]->is_reachable = 1;
    queue[tail_q++] = cfg->blocks[0];

    while (head_q < tail_q) {
        BasicBlock *b = queue[head_q++];
        for (int i = 0; i < b->succ_count; i++) {
            BasicBlock *s = b->successors[i];
            if (s && !s->is_reachable) {
                s->is_reachable = 1;
                queue[tail_q++] = s;
            }
        }
    }

    /* Mark instructions in unreachable blocks as dead */
    int unreachable = 0;
    for (int i = 0; i < cfg->count; i++) {
        BasicBlock *b = cfg->blocks[i];
        if (!b->is_reachable) {
            unreachable++;
            IRInstruction *inst = b->first;
            while (inst) {
                dead_table_mark(inst);
                if (inst == b->last) break;
                inst = inst->next;
            }
        }
    }

    return unreachable;
}

/* ============================================================
 * SECTION 7 —  OUTPUT & DEBUGGING HELPERS
 * ============================================================ */

void print_optimized_ir(IRInstruction *ir_head) {
    printf("\n=== Optimized IR ===\n");
    IRInstruction *inst = ir_head;
    int printed = 0;
    while (inst) {
        if (!is_dead(inst)) {
            print_ir_instruction(inst);
            printed++;
        }
        inst = inst->next;
    }
    if (printed == 0) printf("  <empty>\n");
    printf("====================\n");
}

void print_cfg(CFG *cfg) {
    if (!cfg) return;
    printf("\n=== CFG ===\n");
    for (int i = 0; i < cfg->count; i++) {
        BasicBlock *b = cfg->blocks[i];
        printf("Block %d [%s]:\n",
               b->id,
               b->is_reachable ? "reachable" : "UNREACHABLE");

        /* Print instructions */
        IRInstruction *inst = b->first;
        while (inst) {
            printf("  ");
            if (is_dead(inst)) printf("[DEAD] ");
            print_ir_instruction(inst);
            if (inst == b->last) break;
            inst = inst->next;
        }

        /* Print edges */
        if (b->succ_count > 0) {
            printf("  Successors: ");
            for (int j = 0; j < b->succ_count; j++) {
                printf("B%d ", b->successors[j]->id);
            }
            printf("\n");
        } else {
            printf("  Successors: (none — exit block)\n");
        }
        printf("\n");
    }
    printf("===========\n");
}

void free_cfg(CFG *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->count; i++) {
        free(cfg->blocks[i]);
    }
    free(cfg);
}

/* ============================================================
 * SECTION 9 — PASS 4: COPY PROPAGATION
 *
 * Tracks assignments of the form  dst = src  where src is a
 * variable or temp (not a literal — that's constant propagation).
 * Replaces later uses of dst with src directly, eliminating the
 * copy chain.
 *
 * Algorithm (single forward pass, restarted until stable):
 *   copies = {}
 *   for each instruction I:
 *     if I is a LABEL or branch:
 *         clear copies  (can't track across blocks safely)
 *     replace any operand in I that maps through copies
 *     if I is  dst = src  (IR_ASSIGN, src is non-literal):
 *         copies[dst] = src
 *     else if I defines dst:
 *         kill copies where dst appears as src or as key
 *
 * Example:
 *   t0 = x          copies: {t0→x}
 *   t1 = t0 + 1  →  t1 = x + 1   (t0 replaced by x)
 *   t2 = t1         copies: {t0→x, t2→t1}
 *   print t2     →  print t1      (t2 replaced by t1)
 * ============================================================ */

#define COPY_TABLE_SIZE 128

typedef struct {
    char dst[32];
    char src[32];
} CopyEntry;

typedef struct {
    CopyEntry entries[COPY_TABLE_SIZE];
    int       count;
} CopyTable;

static void copy_table_clear(CopyTable *t) { t->count = 0; }

static int copy_table_find(CopyTable *t, const char *dst) {
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->entries[i].dst, dst) == 0) return i;
    return -1;
}

static const char *copy_table_get(CopyTable *t, const char *dst) {
    int i = copy_table_find(t, dst);
    return i >= 0 ? t->entries[i].src : NULL;
}

static void copy_table_set(CopyTable *t, const char *dst, const char *src) {
    if (!dst || !*dst || !src || !*src) return;
    if (is_constant(src)) return;  /* literals → constant propagation's job */
    int i = copy_table_find(t, dst);
    if (i < 0) {
        if (t->count >= COPY_TABLE_SIZE) return;
        i = t->count++;
    }
    strncpy(t->entries[i].dst, dst, 31);
    strncpy(t->entries[i].src, src, 31);
    t->entries[i].dst[31] = t->entries[i].src[31] = '\0';
}

/* Kill all copies where `name` appears as dst OR src
 * (if src is overwritten, the copy is no longer valid). */
static void copy_table_kill(CopyTable *t, const char *name) {
    for (int i = t->count - 1; i >= 0; i--) {
        if (strcmp(t->entries[i].dst, name) == 0 ||
            strcmp(t->entries[i].src, name) == 0) {
            /* Remove by swapping with last */
            t->entries[i] = t->entries[--t->count];
        }
    }
}

/* Replace operand in-place if a copy is known. Returns 1 if changed. */
static int cp_replace(CopyTable *t, char *operand) {
    if (!operand || !*operand || is_constant(operand)) return 0;
    const char *repl = copy_table_get(t, operand);
    if (!repl) return 0;
    strncpy(operand, repl, 31);
    operand[31] = '\0';
    return 1;
}

int copy_propagation(IRInstruction *ir_head) {
    int total = 0;
    int changed;

    do {
        CopyTable t;
        copy_table_clear(&t);
        changed = 0;

        for (IRInstruction *inst = ir_head; inst; inst = inst->next) {
            if (is_dead(inst) || inst->op == IR_NOP) continue;

            /* Block boundaries invalidate copy info */
            if (inst->op == IR_LABEL || inst->op == IR_GOTO ||
                inst->op == IR_IF_FALSE_GOTO) {
                copy_table_clear(&t);
                /* For conditional branch, still try to replace its cond */
                if (inst->op == IR_IF_FALSE_GOTO)
                    changed += cp_replace(&t, inst->arg1);
                continue;
            }

            /* Replace operands first, then update the table */
            switch (inst->op) {
                case IR_ASSIGN:
                    changed += cp_replace(&t, inst->arg1);
                    /* Kill anything defined by this dst */
                    copy_table_kill(&t, inst->result);
                    /* Record copy only if src is a non-literal name */
                    if (!is_constant(inst->arg1))
                        copy_table_set(&t, inst->result, inst->arg1);
                    break;

                case IR_ADD: case IR_SUB:
                case IR_MUL: case IR_DIV: case IR_CMP:
                    changed += cp_replace(&t, inst->arg1);
                    changed += cp_replace(&t, inst->arg2);
                    copy_table_kill(&t, inst->result);
                    break;

                case IR_PRINT:
                    changed += cp_replace(&t, inst->result);
                    break;

                case IR_RETURN:
                case IR_CALL_ARG:
                    changed += cp_replace(&t, inst->result);
                    break;

                case IR_STACK_PUSH: case IR_QUEUE_ENQUEUE:
                case IR_TREE_INSERT: case IR_TREE_REMOVE:
                    changed += cp_replace(&t, inst->arg1);
                    break;

                case IR_GRAPH_ADD_EDGE: case IR_GRAPH_REMOVE_EDGE:
                    changed += cp_replace(&t, inst->arg1);
                    changed += cp_replace(&t, inst->arg2);
                    break;

                default:
                    break;
            }
        }
        total += changed;
    } while (changed > 0);

    return total;
}

/* ============================================================
 * SECTION 10 — PASS 5: COMMON SUBEXPRESSION ELIMINATION (CSE)
 *
 * Within each basic block, tracks every arithmetic/comparison
 * expression computed so far as a (op, arg1, arg2) tuple.
 * If the same expression appears again AND the result variable
 * from the first computation has not been overwritten since,
 * replace the second instruction with a copy:
 *
 *   t0 = x + y        ← first occurrence, record (ADD, x, y) → t0
 *   ...               ← neither x, y, nor t0 are redefined
 *   t3 = x + y   →   t3 = t0   ← redundant, replaced with copy
 *
 * Commutative operators (+, *) are matched regardless of arg order.
 *
 * After replacing, the copy is immediately eligible for Copy
 * Propagation on the next overall iteration.
 * ============================================================ */

#define CSE_TABLE_SIZE 128

typedef struct {
    IROp   op;
    char   arg1[32];
    char   arg2[32];
    char   result[32];   /* the temp that holds this expr's value */
} CSEEntry;

typedef struct {
    CSEEntry entries[CSE_TABLE_SIZE];
    int      count;
} CSETable;

static void cse_table_clear(CSETable *t) { t->count = 0; }

static int cse_exprs_match(const CSEEntry *e, IROp op,
                            const char *a1, const char *a2) {
    if (e->op != op) return 0;
    /* Direct match */
    if (strcmp(e->arg1, a1) == 0 && strcmp(e->arg2, a2) == 0) return 1;
    /* Commutative: + and * */
    if (op == IR_ADD || op == IR_MUL)
        if (strcmp(e->arg1, a2) == 0 && strcmp(e->arg2, a1) == 0) return 1;
    return 0;
}

static const char *cse_table_lookup(CSETable *t, IROp op,
                                     const char *a1, const char *a2) {
    for (int i = 0; i < t->count; i++)
        if (cse_exprs_match(&t->entries[i], op, a1, a2))
            return t->entries[i].result;
    return NULL;
}

static void cse_table_add(CSETable *t, IROp op,
                           const char *a1, const char *a2,
                           const char *result) {
    if (t->count >= CSE_TABLE_SIZE) return;
    CSEEntry *e = &t->entries[t->count++];
    e->op = op;
    strncpy(e->arg1,   a1,     31); e->arg1[31]   = '\0';
    strncpy(e->arg2,   a2,     31); e->arg2[31]   = '\0';
    strncpy(e->result, result, 31); e->result[31] = '\0';
}

/* Kill all CSE entries that use `name` as an operand or result.
 * Called when a variable is overwritten. */
static void cse_table_kill(CSETable *t, const char *name) {
    for (int i = t->count - 1; i >= 0; i--) {
        if (strcmp(t->entries[i].result, name) == 0 ||
            strcmp(t->entries[i].arg1,   name) == 0 ||
            strcmp(t->entries[i].arg2,   name) == 0) {
            t->entries[i] = t->entries[--t->count];
        }
    }
}

static int is_cse_eligible(IROp op) {
    return op == IR_ADD || op == IR_SUB ||
           op == IR_MUL || op == IR_DIV || op == IR_CMP;
}

static int cse_block(BasicBlock *b) {
    CSETable t;
    cse_table_clear(&t);
    int eliminated = 0;

    IRInstruction *inst = b->first;
    while (inst) {
        if (is_dead(inst) || inst->op == IR_NOP) {
            if (inst == b->last) break;
            inst = inst->next;
            continue;
        }

        if (is_cse_eligible(inst->op)) {
            const char *prior = cse_table_lookup(&t, inst->op,
                                                  inst->arg1, inst->arg2);
            if (prior) {
                /* Replace with copy from prior result */
                inst->op = IR_ASSIGN;
                strncpy(inst->arg1, prior, sizeof(inst->arg1) - 1);
                inst->arg1[sizeof(inst->arg1) - 1] = '\0';
                inst->arg2[0] = '\0';
                inst->cmp_op  = 0;
                /* The result variable is now a copy — kill stale CSE entries
                 * that depended on the old computation's result name. */
                cse_table_kill(&t, inst->result);
                eliminated++;
            } else {
                /* First occurrence — record in table */
                cse_table_add(&t, inst->op, inst->arg1, inst->arg2,
                               inst->result);
            }
        } else {
            /* Any instruction that defines a variable kills CSE entries
             * that used that variable as an operand. */
            const char *def = instr_defines(inst);
            if (def && *def) cse_table_kill(&t, def);
        }

        if (inst == b->last) break;
        inst = inst->next;
    }
    return eliminated;
}

int cse(CFG *cfg) {
    if (!cfg) return 0;
    int total = 0;
    for (int i = 0; i < cfg->count; i++) {
        BasicBlock *b = cfg->blocks[i];
        if (b->is_reachable)
            total += cse_block(b);
    }
    return total;
}

/* ============================================================
 * SECTION 11 — ADAPTIVE OPTIMIZATION ENGINE
 *
 * Instead of running every pass unconditionally, the engine
 * analyses the SemanticReport and selects the most beneficial
 * passes for the specific program being compiled.
 *
 * Decision table:
 * ┌─────────────────────────┬──────────────────────────────────┐
 * │ Program characteristic  │ Action                           │
 * ├─────────────────────────┼──────────────────────────────────┤
 * │ constant_exprs > 0      │ Run constant folding/propagation │
 * │ max_loop_depth > 0      │ Run DCE + CSE (loops amplify     │
 * │                         │ benefit of both)                 │
 * │ arithmetic_ops > 4      │ Run CSE (enough ops to find      │
 * │                         │ common subexpressions)           │
 * │ assignment_count > 2    │ Run copy propagation             │
 * │ adt_ops > arithmetic_   │ Skip CSE (ADT-dominated program, │
 * │   ops                   │ little arithmetic to share)      │
 * │ total instrs < 10       │ Skip inter-block DCE overhead    │
 * └─────────────────────────┴──────────────────────────────────┘
 *
 * The engine always runs at least one constant-folding pass and
 * DCE regardless, since they have negligible overhead.
 * ============================================================ */

static int count_ir_instrs(IRInstruction *ir) {
    int n = 0;
    for (IRInstruction *i = ir; i; i = i->next) n++;
    return n;
}

OptReport run_optimizer_adaptive(IRInstruction *ir_head,
                                  const SemanticReport *sem) {
    OptReport report;
    memset(&report, 0, sizeof(report));
    if (!ir_head) return report;

    /* ── Analyse program characteristics ── */
    int total_instrs   = count_ir_instrs(ir_head);
    int constant_heavy = sem->constant_exprs > 0;
    int loop_heavy     = sem->max_loop_depth > 0;
    int arith_heavy    = sem->arithmetic_ops > 4;
    int copy_worthy    = sem->assignment_count > 2;
    int adt_dominated  = sem->adt_ops > sem->arithmetic_ops;
    int small_program  = total_instrs < 10;

    printf("\n=== SIMPL Adaptive Optimizer (v2) ===\n");
    printf("Program profile:\n");
    printf("  Instructions : %d\n", total_instrs);
    printf("  Loop depth   : %d\n", sem->max_loop_depth);
    printf("  Arith ops    : %d\n", sem->arithmetic_ops);
    printf("  Const exprs  : %d\n", sem->constant_exprs);
    printf("  Assignments  : %d\n", sem->assignment_count);
    printf("  ADT ops      : %d\n", sem->adt_ops);
    printf("\nPass selection:\n");

    /* ── Pass 1: Constant Folding + Propagation (always run) ── */
    printf("  [PASS 1] Constant folding/propagation  → ALWAYS\n");
    {
        int changed;
        do {
            changed  = constant_propagation(ir_head);
            changed += constant_folding(ir_head);
            changed += simplify_constant_branches(ir_head);
            report.constants_folded += changed;
        } while (changed > 0);
    }
    report.passes_run |= OPT_PASS_CONST_FOLD;

    /* ── Pass 4: Copy Propagation ── */
    if (copy_worthy || constant_heavy) {
        printf("  [PASS 4] Copy propagation              → YES"
               " (assignments=%d)\n", sem->assignment_count);
        report.copies_propagated = copy_propagation(ir_head);
        report.passes_run |= OPT_PASS_COPY_PROP;
        /* Re-fold after copy prop may expose new constants */
        {
            int changed;
            do {
                changed  = constant_folding(ir_head);
                changed += constant_propagation(ir_head);
                report.constants_folded += changed;
            } while (changed > 0);
        }
    } else {
        printf("  [PASS 4] Copy propagation              → SKIPPED"
               " (few assignments)\n");
    }

    /* ── Build CFG (needed for block-level passes) ── */
    CFG *cfg = build_cfg(ir_head);
    if (!cfg) {
        fprintf(stderr, "[OPT] Failed to build CFG.\n");
        return report;
    }

    /* ── Pass 5: CSE ── */
    if (!adt_dominated && (arith_heavy || loop_heavy)) {
        printf("  [PASS 5] Common subexpr elimination    → YES"
               " (arith=%d, loops=%d)\n",
               sem->arithmetic_ops, sem->max_loop_depth);
        report.cse_eliminated = cse(cfg);
        report.passes_run |= OPT_PASS_CSE;
        /* CSE introduces copies — run copy prop again */
        if (report.cse_eliminated > 0) {
            report.copies_propagated += copy_propagation(ir_head);
            /* And fold again */
            int changed;
            do {
                changed = constant_folding(ir_head);
                report.constants_folded += changed;
            } while (changed > 0);
        }
    } else {
        printf("  [PASS 5] Common subexpr elimination    → SKIPPED"
               " (adt_dominated=%d, arith=%d)\n",
               adt_dominated, sem->arithmetic_ops);
    }

    /* ── Pass 2: DCE ── */
    if (!small_program || loop_heavy) {
        printf("  [PASS 2] Dead code elimination         → YES\n");
        report.dead_instrs_removed = dead_code_elimination(cfg);
        report.passes_run |= OPT_PASS_DCE;
    } else {
        printf("  [PASS 2] Dead code elimination         → SKIPPED"
               " (small program)\n");
    }

    /* ── Pass 3: Unreachable Block Elimination (always run if CFG built) ── */
    printf("  [PASS 3] Unreachable block elimination → ALWAYS\n");
    report.unreachable_blocks = unreachable_block_elimination(cfg);
    report.passes_run |= OPT_PASS_UNREACHABLE;

    printf("\nResults:\n");
    printf("  Constants simplified : %d\n", report.constants_folded);
    printf("  Copies propagated    : %d\n", report.copies_propagated);
    printf("  CSE eliminated       : %d\n", report.cse_eliminated);
    printf("  Dead instrs removed  : %d\n", report.dead_instrs_removed);
    printf("  Unreachable blocks   : %d\n", report.unreachable_blocks);
    printf("=====================================\n");

    free_cfg(cfg);
    return report;
}

/* ============================================================
 * UPDATED MASTER DRIVER
 * run_optimizer() now delegates to the adaptive engine.
 * For callers that don't have a SemanticReport, a default
 * all-passes profile is used.
 * ============================================================ */

OptReport run_optimizer(IRInstruction *ir_head) {
    /* Build a conservative profile that enables all passes */
    SemanticReport default_sem;
    memset(&default_sem, 0, sizeof(default_sem));
    default_sem.constant_exprs   = 1;   /* assume: run folding       */
    default_sem.max_loop_depth   = 1;   /* assume: run DCE + CSE     */
    default_sem.arithmetic_ops   = 8;   /* assume: run CSE           */
    default_sem.assignment_count = 4;   /* assume: run copy prop     */
    default_sem.adt_ops          = 0;   /* assume: not ADT dominated */
    return run_optimizer_adaptive(ir_head, &default_sem);
}

void print_opt_report(const OptReport *r) {
    printf("\n=== Optimization Report ===\n");
    printf("Passes run             : ");
    if (r->passes_run & OPT_PASS_CONST_FOLD)  printf("ConstFold ");
    if (r->passes_run & OPT_PASS_COPY_PROP)   printf("CopyProp ");
    if (r->passes_run & OPT_PASS_CSE)         printf("CSE ");
    if (r->passes_run & OPT_PASS_DCE)         printf("DCE ");
    if (r->passes_run & OPT_PASS_UNREACHABLE) printf("UnreachElim ");
    printf("\n");
    printf("Constants simplified   : %d\n", r->constants_folded);
    printf("Copies propagated      : %d\n", r->copies_propagated);
    printf("CSE eliminated         : %d\n", r->cse_eliminated);
    printf("Dead instrs removed    : %d\n", r->dead_instrs_removed);
    printf("Unreachable blocks     : %d\n", r->unreachable_blocks);
    printf("===========================\n");
}
