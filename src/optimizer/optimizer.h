#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "../ir/ir.h"
#include "../semantic/semantic.h"

/* ============================================================
 * SIMPL Optimizer — v2
 * ============================================================
 *
 * v1 passes (retained):
 *   Pass 1 — Constant Folding + Propagation + Branch Simplification
 *   Pass 2 — Dead Code Elimination (global liveness, inter-block)
 *   Pass 3 — Unreachable Block Elimination
 *
 * v2 passes (new):
 *   Pass 4 — Copy Propagation
 *     Tracks assignments of the form  x = y  (copy of a variable).
 *     Replaces downstream uses of x with y directly, collapsing
 *     unnecessary copy chains.
 *     Example:
 *       t0 = x        →   (eliminated)
 *       t1 = t0 + 1   →   t1 = x + 1
 *
 *   Pass 5 — Common Subexpression Elimination (CSE)
 *     Within each basic block, tracks every computation seen so far.
 *     If the same (op, arg1, arg2) tuple is computed again and its
 *     result is still available (not overwritten), the second
 *     instruction is replaced with a copy from the first result.
 *     Example:
 *       t0 = x + y    (first occurrence — kept)
 *       t1 = x + y    →   t1 = t0   (redundant — replaced)
 *
 *   Adaptive Optimization Engine:
 *     Uses the SemanticReport metrics to decide which passes to run
 *     and in what order, rather than blindly executing all of them.
 *     Decision logic:
 *       constant-heavy  → prioritize folding + propagation
 *       loop-heavy      → prioritize DCE + CSE (most benefit in loops)
 *       ADT-heavy       → skip CSE (ADT ops not eligible anyway)
 *       small program   → skip inter-block analysis overhead
 *
 * All passes are non-destructive: dead instructions are flagged
 * via is_dead() rather than freed, so the IR list stays intact
 * for debugging and the codegen can inspect both versions.
 * ============================================================ */

/* ── Basic Block ─────────────────────────────────────────── */

#define MAX_INSTRS_PER_BLOCK  512
#define MAX_BLOCKS            256
#define MAX_SUCCESSORS          4
#define MAX_PREDECESSORS        8

typedef struct BasicBlock {
    int id;

    IRInstruction *first;
    IRInstruction *last;

    struct BasicBlock *successors[MAX_SUCCESSORS];
    int succ_count;

    struct BasicBlock *predecessors[MAX_PREDECESSORS];
    int pred_count;

    int is_reachable;
} BasicBlock;

/* ── CFG ─────────────────────────────────────────────────── */

typedef struct {
    BasicBlock *blocks[MAX_BLOCKS];
    int count;
} CFG;

/* ── Optimization report ─────────────────────────────────── */

typedef struct {
    /* v1 */
    int constants_folded;
    int dead_instrs_removed;
    int unreachable_blocks;

    /* v2 */
    int copies_propagated;       /* Pass 4 — Copy Propagation   */
    int cse_eliminated;          /* Pass 5 — CSE                */

    /* Adaptive engine decisions */
    int passes_run;              /* bitmask — which passes fired */
} OptReport;

/* Bitmask values for passes_run */
#define OPT_PASS_CONST_FOLD   (1 << 0)
#define OPT_PASS_DCE          (1 << 1)
#define OPT_PASS_UNREACHABLE  (1 << 2)
#define OPT_PASS_COPY_PROP    (1 << 3)
#define OPT_PASS_CSE          (1 << 4)

/* ── Public API ──────────────────────────────────────────── */

CFG *build_cfg(IRInstruction *ir_head);

/* v1 passes */
int constant_folding(IRInstruction *ir_head);
int dead_code_elimination(CFG *cfg);
int unreachable_block_elimination(CFG *cfg);

/* v2 passes */
int copy_propagation(IRInstruction *ir_head);
int cse(CFG *cfg);

/* Adaptive engine — main entry point for v2.
 * Takes the SemanticReport so it can make data-driven decisions. */
OptReport run_optimizer(IRInstruction *ir_head);
OptReport run_optimizer_adaptive(IRInstruction *ir_head,
                                  const SemanticReport *sem);

void print_optimized_ir(IRInstruction *ir_head);
void print_opt_report(const OptReport *r);
void print_cfg(CFG *cfg);
void free_cfg(CFG *cfg);

int is_dead(IRInstruction *inst);

#endif /* OPTIMIZER_H */
