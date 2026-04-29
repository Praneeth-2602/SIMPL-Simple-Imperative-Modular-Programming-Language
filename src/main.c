// ============================================================
// SIMPL Compiler - Main Pipeline Driver (v2.0)
// ============================================================
// Full pipeline:
//   parse -> semantic -> IR -> optimizer -> LLVM IR codegen
//
// Usage (normal compilation):
//   ./simpl program.simpl              # compiles via RL agent
//   ./simpl program.simpl output.ll    # custom output path
//
// RL training & evaluation flags:
//   --qtable PATH          Path to Q-table file (default: rl_qtable.bin)
//   --train                Run one RL episode, update Q-table, exit
//   --eval                 Run all three optimizers, print CSV metrics row
//   --quiet                Suppress verbose parser/semantic/IR output
//   --dump-qtable          Print Q-table summary and exit
//
// Typical training loop (from train.sh):
//   for i in $(seq 1 100); do
//     for f in tests/files/*.simpl; do
//       ./simpl "$f" --train --qtable rl_qtable.bin --quiet
//     done
//   done
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "parser/ast.h"
#include "semantic/semantic.h"
#include "ir/ir.h"
#include "optimizer/optimizer.h"
#include "rl_agent/rl_agent.h"
#include "codegen/codegen.h"

extern int     yyparse(void);
extern FILE   *yyin;
extern ASTNode *ast_root;

/* ── CLI flags ─────────────────────────────────────────────── */
typedef struct {
    const char *in_path;
    const char *out_path;
    const char *qtable_path;
    int mode_train;
    int mode_eval;
    int mode_dump_qtable;
    int quiet;
} CLIArgs;

static CLIArgs parse_args(int argc, char **argv) {
    CLIArgs a;
    memset(&a, 0, sizeof(a));
    a.out_path    = "output.ll";
    a.qtable_path = "rl_qtable.bin";
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--train")       == 0) a.mode_train       = 1;
        else if (strcmp(argv[i], "--eval")        == 0) a.mode_eval        = 1;
        else if (strcmp(argv[i], "--dump-qtable") == 0) a.mode_dump_qtable = 1;
        else if (strcmp(argv[i], "--quiet")       == 0) a.quiet            = 1;
        else if (strcmp(argv[i], "--qtable") == 0 && i + 1 < argc)
            a.qtable_path = argv[++i];
        else if (argv[i][0] != '-') {
            if (!a.in_path)        a.in_path  = argv[i];
            else                   a.out_path = argv[i];
        }
    }
    return a;
}

/* ── Shared frontend: parse + semantic + IR ─────────────────── */
typedef struct {
    SemanticReport sem;
    IRInstruction *ir;
    int ok;
} PipelineResult;

static PipelineResult run_frontend(const char *in_path, int quiet) {
    PipelineResult r;
    memset(&r, 0, sizeof(r));
    if (in_path) {
        yyin = fopen(in_path, "r");
        if (!yyin) { perror(in_path); return r; }
    }
    if (yyparse() != 0) {
        fprintf(stderr, "Parsing failed.\n");
        if (in_path && yyin) fclose(yyin);
        return r;
    }
    if (!quiet) printf("Parsing successful.\n");
    SemanticOptions opts = { .verbose = !quiet, .show_optimizations = !quiet };
    r.sem = semantic_check_with_options(ast_root, opts);
    if (r.sem.error_count > 0) {
        fprintf(stderr, "Compilation aborted: %d semantic error(s).\n", r.sem.error_count);
        if (in_path && yyin) fclose(yyin);
        return r;
    }
    if (!quiet) printf("\n=== Three-Address Code (unoptimized) ===\n");
    r.ir = generate_ir(ast_root);
    if (!quiet) { print_ir(r.ir); printf("=========================================\n"); }
    if (in_path && yyin) fclose(yyin);
    r.ok = 1;
    return r;
}

/* ── IR helpers ─────────────────────────────────────────────── */
static int count_ir_list(IRInstruction *ir) {
    int n = 0; for (IRInstruction *p = ir; p; p = p->next) n++; return n;
}
static IRInstruction *clone_ir(IRInstruction *src) {
    IRInstruction *head = NULL, *tail = NULL;
    for (IRInstruction *p = src; p; p = p->next) {
        IRInstruction *n = malloc(sizeof(IRInstruction));
        if (!n) return head;
        *n = *p; n->next = NULL;
        if (!head) head = n; else tail->next = n;
        tail = n;
    }
    return head;
}
static void free_ir(IRInstruction *ir) {
    while (ir) { IRInstruction *nx = ir->next; free(ir); ir = nx; }
}

/* ── MODE: --dump-qtable ────────────────────────────────────── */
static int mode_dump_qtable(const CLIArgs *a) {
    RLAgent *agent = rl_agent_create();
    rl_agent_load(agent, a->qtable_path);
    printf("[RL] episodes trained: %d\n", agent->total_episodes);
    rl_print_qtable_summary(agent);
    rl_agent_free(agent);
    return 0;
}

/* ── MODE: --train ──────────────────────────────────────────── */
static int mode_train(const CLIArgs *a) {
    PipelineResult p = run_frontend(a->in_path, a->quiet);
    if (!p.ok) return 1;
    RLAgent *agent = rl_agent_create();
    rl_agent_load(agent, a->qtable_path);
    RLEpisodeResult ep = rl_run_episode(agent, p.ir, &p.sem);
    int instrs_elim = ep.opt.constants_folded + ep.opt.copies_propagated
                    + ep.opt.cse_eliminated   + ep.opt.dead_instrs_removed
                    + ep.opt.unreachable_blocks;
    /* TRAIN_CSV: episode, file, actions, total_reward, epsilon, instrs_elim */
    printf("TRAIN_CSV,%d,%s,%d,%.4f,%.3f,%d\n",
           agent->total_episodes,
           a->in_path ? a->in_path : "<stdin>",
           ep.actions_taken, ep.total_reward, ep.epsilon_used, instrs_elim);
    rl_agent_save(agent, a->qtable_path);
    rl_agent_free(agent);
    return 0;
}

/* ── MODE: --eval ───────────────────────────────────────────── */
static void run_fixed_mode(IRInstruction *ir, const SemanticReport *sem,
                           int *elim, double *ms) {
    (void)sem;
    clock_t t0 = clock();
    OptReport r = run_optimizer(ir);
    clock_t t1 = clock();
    *elim = r.constants_folded + r.copies_propagated + r.cse_eliminated
          + r.dead_instrs_removed + r.unreachable_blocks;
    *ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    if (*ms < 0.001) *ms = 0.001;
}
static void run_adaptive_mode(IRInstruction *ir, const SemanticReport *sem,
                              int *elim, double *ms) {
    clock_t t0 = clock();
    OptReport r = run_optimizer_adaptive(ir, sem);
    clock_t t1 = clock();
    *elim = r.constants_folded + r.copies_propagated + r.cse_eliminated
          + r.dead_instrs_removed + r.unreachable_blocks;
    *ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    if (*ms < 0.001) *ms = 0.001;
}
static void run_rl_eval_mode(IRInstruction *ir, const SemanticReport *sem,
                             RLAgent *agent,
                             int *elim, double *ms, float *reward) {
    /* Force exploitation and disable Q-updates — eval is strictly read-only */
    int saved_ep   = agent->total_episodes;
    int saved_eval = agent->eval_only;
    if (agent->total_episodes < RL_EPSILON_DECAY)
        agent->total_episodes = RL_EPSILON_DECAY;
    agent->eval_only = 1;

    clock_t t0 = clock();
    RLEpisodeResult ep = rl_run_episode(agent, ir, sem);
    clock_t t1 = clock();

    agent->total_episodes = saved_ep;
    agent->eval_only      = saved_eval;

    *elim   = ep.opt.constants_folded + ep.opt.copies_propagated
            + ep.opt.cse_eliminated   + ep.opt.dead_instrs_removed
            + ep.opt.unreachable_blocks;
    *ms     = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    if (*ms < 0.001) *ms = 0.001;
    *reward = ep.total_reward;
}

static int mode_eval(const CLIArgs *a) {
    PipelineResult p = run_frontend(a->in_path, 1);
    if (!p.ok) return 1;
    int base = count_ir_list(p.ir);

    IRInstruction *ir1 = clone_ir(p.ir);
    int fe; double fm; run_fixed_mode(ir1, &p.sem, &fe, &fm); free_ir(ir1);

    IRInstruction *ir2 = clone_ir(p.ir);
    int ae; double am; run_adaptive_mode(ir2, &p.sem, &ae, &am); free_ir(ir2);

    IRInstruction *ir3 = clone_ir(p.ir);
    RLAgent *agent = rl_agent_create();
    rl_agent_load(agent, a->qtable_path);
    int re; double rm; float rw;
    run_rl_eval_mode(ir3, &p.sem, agent, &re, &rm, &rw);
    rl_agent_free(agent);  /* no save — eval is read-only */
    free_ir(ir3);

    /* EVAL_CSV: file, base_instrs,
     *           fixed_elim, fixed_ms,
     *           adapt_elim, adapt_ms,
     *           rl_elim, rl_ms, rl_reward */
    printf("EVAL_CSV,%s,%d,%d,%.4f,%d,%.4f,%d,%.4f,%.4f\n",
           a->in_path ? a->in_path : "<stdin>", base,
           fe, fm, ae, am, re, rm, rw);
    return 0;
}

/* ── MODE: normal compile ───────────────────────────────────── */
static int mode_compile(const CLIArgs *a) {
    PipelineResult p = run_frontend(a->in_path, a->quiet);
    if (!p.ok) return 1;
    RLAgent *agent = rl_agent_create();
    rl_agent_load(agent, a->qtable_path);
    RLEpisodeResult ep = rl_run_episode(agent, p.ir, &p.sem);
    OptReport opt = ep.opt;
    rl_agent_save(agent, a->qtable_path);
    rl_agent_free(agent);
    if (!a->quiet) print_optimized_ir(p.ir);
    printf("\n=== Code Generation → LLVM IR ===\n");
    if (codegen_emit_llvm(p.ir, a->out_path) != 0) {
        fprintf(stderr, "Code generation failed.\n"); return 1;
    }
    printf("\n=== Compilation Summary ===\n");
    printf("Input             : %s\n", a->in_path ? a->in_path : "<stdin>");
    printf("Output            : %s\n", a->out_path);
    printf("Semantic errors   : %d\n", p.sem.error_count);
    printf("Semantic warnings : %d\n", p.sem.warning_count);
    printf("Episode reward    : %.4f\n", ep.total_reward);
    print_opt_report(&opt);
    printf("===========================\n");
    printf("\nTo build a native binary:\n");
    printf("  clang %s -o program && ./program\n", a->out_path);
    return 0;
}

/* ─────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    CLIArgs a = parse_args(argc, argv);
    if (!a.in_path && !a.mode_dump_qtable) {
        fprintf(stderr,
            "Usage: simpl <program.simpl> [output.ll]\n"
            "       [--train] [--eval] [--qtable PATH] [--quiet] [--dump-qtable]\n");
        return 1;
    }
    if (a.mode_dump_qtable) return mode_dump_qtable(&a);
    if (a.mode_train)       return mode_train(&a);
    if (a.mode_eval)        return mode_eval(&a);
    return mode_compile(&a);
}
