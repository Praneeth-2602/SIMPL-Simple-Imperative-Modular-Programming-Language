/* ============================================================
 * SIMPL Compiler — Main Pipeline Driver (v1.0)
 * ============================================================
 * Full pipeline:
 *   parse → semantic → IR → optimizer → LLVM IR codegen
 *
 * Usage:
 *   ./simpl program.simpl              # compiles to output.ll
 *   ./simpl program.simpl output.ll    # custom output path
 *   clang output.ll -o program         # link to native binary
 *   ./program                          # run it
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser/ast.h"
#include "semantic/semantic.h"
#include "ir/ir.h"
#include "optimizer/optimizer.h"
#include "codegen/codegen.h"

extern int    yyparse(void);
extern FILE  *yyin;
extern ASTNode *ast_root;

int main(int argc, char **argv) {

    /* ── Resolve input / output paths ── */
    const char *in_path  = NULL;
    const char *out_path = "output.ll";

    if (argc >= 2) in_path  = argv[1];
    if (argc >= 3) out_path = argv[2];

    if (in_path) {
        yyin = fopen(in_path, "r");
        if (!yyin) { perror(in_path); return 1; }
    }

    /* ── 1. Parse ── */
    if (yyparse() != 0) {
        fprintf(stderr, "Parsing failed.\n");
        return 1;
    }
    printf("Parsing successful.\n");

    /* ── 2. Semantic Analysis ── */
    SemanticOptions opts = { .verbose = 1, .show_optimizations = 1 };
    SemanticReport sem   = semantic_check_with_options(ast_root, opts);

    if (sem.error_count > 0) {
        fprintf(stderr, "\nCompilation aborted: %d semantic error(s).\n",
                sem.error_count);
        return 1;
    }

    /* ── 3. IR Generation ── */
    printf("\n=== Three-Address Code (unoptimized) ===\n");
    IRInstruction *ir = generate_ir(ast_root);
    print_ir(ir);
    printf("=========================================\n");

    /* ── 4. Optimization ── */
    OptReport opt = run_optimizer(ir);
    print_optimized_ir(ir);

    /* ── 5. LLVM IR Code Generation ── */
    printf("\n=== Code Generation → LLVM IR ===\n");
    if (codegen_emit_llvm(ir, out_path) != 0) {
        fprintf(stderr, "Code generation failed.\n");
        return 1;
    }

    /* ── 6. Final summary ── */
    printf("\n=== Compilation Summary ===\n");
    printf("Input             : %s\n", in_path ? in_path : "<stdin>");
    printf("Output            : %s\n", out_path);
    printf("Semantic errors   : %d\n", sem.error_count);
    printf("Semantic warnings : %d\n", sem.warning_count);
    printf("Constants folded  : %d\n", opt.constants_folded);
    printf("Dead instrs elim. : %d\n", opt.dead_instrs_removed);
    printf("Unreachable blocks: %d\n", opt.unreachable_blocks);
    printf("===========================\n");
    printf("\nTo build a native binary:\n");
    printf("  clang %s -o program && ./program\n", out_path);

    if (in_path) fclose(yyin);
    return 0;
}
