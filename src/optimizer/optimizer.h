#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "../ir/ir.h"

/* ============================================================
 * SIMPL Optimizer
 * ============================================================
 *
 * Pass 1: Constant Folding
 *   Evaluates constant arithmetic/comparison expressions
 *   at compile time and replaces them with literal values.
 *   Example:  t0 = 5 + 3   →   t0 = 8
 *
 * Pass 2: Dead Code Elimination (DCE)
 *   Removes IR instructions whose result is never used.
 *   Operates within each basic block using a backward
 *   liveness scan.
 *   Example:  t1 = a + b   (t1 never read)  →  <deleted>
 *
 * Pass 3: Unreachable Block Elimination
 *   Uses the CFG to identify blocks with no predecessors
 *   (other than the entry block) and removes all instructions
 *   inside them.
 *
 * All passes are non-destructive to the original IR list
 * structure — they mark instructions as dead (is_dead = 1)
 * rather than freeing memory, so the pipeline can still
 * inspect or print the original IR for debugging.
 * ============================================================ */

/* ── Basic Block ─────────────────────────────────────────── */

#define MAX_INSTRS_PER_BLOCK  512
#define MAX_BLOCKS            256
#define MAX_SUCCESSORS          4
#define MAX_PREDECESSORS        8

typedef struct BasicBlock {
    int id;

    /* Pointers into the flat IR linked list */
    IRInstruction *first;
    IRInstruction *last;

    /* CFG edges */
    struct BasicBlock *successors[MAX_SUCCESSORS];
    int succ_count;

    struct BasicBlock *predecessors[MAX_PREDECESSORS];
    int pred_count;

    /* Set by unreachable-block elimination */
    int is_reachable;
} BasicBlock;

/* ── CFG ─────────────────────────────────────────────────── */

typedef struct {
    BasicBlock *blocks[MAX_BLOCKS];
    int count;
} CFG;

/* ── Optimization report ─────────────────────────────────── */

typedef struct {
    int constants_folded;      /* instructions simplified by constant folding */
    int dead_instrs_removed;   /* instructions removed by DCE                 */
    int unreachable_blocks;    /* blocks eliminated as unreachable             */
} OptReport;

/* ── Public API ──────────────────────────────────────────── */

/* Build basic blocks and CFG from a flat IR list. */
CFG *build_cfg(IRInstruction *ir_head);

/* Pass 1 — Constant Folding.
 * Walks every instruction in ir_head and folds constant
 * arithmetic/comparison operands in-place.
 * Returns the number of instructions folded. */
int  constant_folding(IRInstruction *ir_head);

/* Pass 2 — Dead Code Elimination.
 * Marks dead instructions (is_dead = 1) inside each block.
 * Returns the number of instructions marked dead. */
int  dead_code_elimination(CFG *cfg);

/* Pass 3 — Unreachable Block Elimination.
 * Marks unreachable blocks and their instructions dead.
 * Returns the number of blocks eliminated. */
int  unreachable_block_elimination(CFG *cfg);

/* Run all three passes in order and return a combined report. */
OptReport run_optimizer(IRInstruction *ir_head);

/* Print the optimized IR (skipping dead instructions). */
void print_optimized_ir(IRInstruction *ir_head);

/* Print CFG structure (for debugging). */
void print_cfg(CFG *cfg);

/* Free CFG memory. */
void free_cfg(CFG *cfg);

/* Query whether an instruction was marked dead by any optimizer pass.
 * Used by the code generator to skip dead instructions. */
int is_dead(IRInstruction *inst);

#endif /* OPTIMIZER_H */
