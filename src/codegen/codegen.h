#ifndef CODEGEN_H
#define CODEGEN_H

#include "../ir/ir.h"
#include <stdio.h>

/* ============================================================
 * SIMPL → LLVM IR Code Generator
 * ============================================================
 *
 * Translates the optimized SIMPL TAC IR into LLVM IR text
 * format (.ll files).  The output can be fed directly to:
 *
 *   llc output.ll -o output.s          # compile to assembly
 *   clang output.ll -o program         # compile to native binary
 *
 * Design decisions:
 *
 * 1. Memory model (alloca + load/store)
 *    LLVM IR is in SSA form — every register is assigned once.
 *    SIMPL allows re-assignment of variables (set x to ...).
 *    To bridge this gap without implementing full SSA
 *    construction (phi nodes), we use LLVM's mem2reg idiom:
 *      - Each SIMPL variable gets an alloca slot on the stack.
 *      - Reads  → load  i32, i32* %var_slot
 *      - Writes → store i32 <value>, i32* %var_slot
 *    The LLVM optimizer's mem2reg pass will promote these to
 *    SSA registers automatically when you run `opt -mem2reg`.
 *
 * 2. Temporaries
 *    SIMPL compiler-generated temps (t0, t1, ...) map directly
 *    to LLVM SSA registers (%t0, %t1, ...) since they are
 *    written exactly once by construction.
 *
 * 3. Print
 *    Lowered to a call to printf via an @fmt string constant.
 *
 * 4. Labels / control flow
 *    SIMPL labels (L0, L1, ...) map to LLVM basic block labels.
 *    Every LLVM basic block must end with a terminator
 *    (br or ret), so the generator inserts implicit
 *    fallthrough branches where needed.
 *
 * 5. Dead instructions
 *    Instructions marked dead by the optimizer are skipped
 *    via the is_dead() predicate from optimizer.h.
 * ============================================================ */

/* Code generation context — tracks alloca'd variables,
 * the current label, and the output file. */
typedef struct {
    FILE *out;               /* output .ll file                  */
    char  vars[128][32];     /* names of alloca'd SIMPL variables */
    int   var_count;
    char  current_label[32]; /* label of the block being emitted  */
    int   reg_counter;       /* SSA register counter for anon regs */
} CodegenCtx;

/* ── Public API ─────────────────────────────────────────── */

/* Emit a complete LLVM IR module to `out_path`.
 * `ir_head`  — start of the (possibly optimized) IR list.
 * `out_path` — path to write the .ll file (e.g. "output.ll").
 * Returns 0 on success, -1 on error. */
int codegen_emit_llvm(IRInstruction *ir_head, const char *out_path);

#endif /* CODEGEN_H */
