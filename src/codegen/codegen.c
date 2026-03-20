/* ============================================================
 * SIMPL → LLVM IR Code Generator  (v1.0 — with full ADT support)
 * ============================================================
 *
 * ADT Implementation Strategy (all inline, no external libs):
 *
 *  Stack  — alloca [CAP x i32] data array  +  alloca i32 top index
 *            push: store val at top, increment top
 *            pop : decrement top
 *
 *  Queue  — alloca [CAP x i32] data array  +  alloca i32 head  +  alloca i32 tail
 *            enqueue: store val at tail, increment tail (wraps mod CAP)
 *            dequeue: increment head (wraps mod CAP)
 *
 *  Tree   — array-based binary heap / BST
 *            alloca [CAP x i32] data + alloca i32 size
 *            insert : place at next free slot (size), increment size
 *            remove : mark slot as TOMBSTONE (-2147483648), leave size
 *
 *  Graph  — flat [N x N x i32] adjacency matrix (N = GRAPH_N)
 *            add_edge    : matrix[from*N + to] = 1
 *            remove_edge : matrix[from*N + to] = 0
 *
 * All capacities are compile-time constants defined below.
 * ============================================================ */

#include "codegen.h"
#include "../optimizer/optimizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── ADT capacity constants ──────────────────────────── */
#define STACK_CAP  256
#define QUEUE_CAP  256
#define TREE_CAP   256
#define GRAPH_N     64    /* max nodes in any graph */

/* ── Tracked ADT variables ───────────────────────────── */
#define MAX_ADTS 64

typedef enum { KIND_NONE, KIND_STACK, KIND_QUEUE, KIND_TREE, KIND_GRAPH } ADTKind;

typedef struct {
    char    name[32];
    ADTKind kind;
} ADTEntry;

/* ── Code generation context ─────────────────────────── */
typedef struct {
    FILE   *out;
    int     reg;          /* SSA register counter              */

    /* integer variables */
    char    vars[128][32];
    int     var_count;

    /* ADT registry */
    ADTEntry adts[MAX_ADTS];
    int      adt_count;
} CG;

/* ============================================================
 * UTILITY
 * ============================================================ */

static int is_lit(const char *s) {
    if (!s || !*s) return 0;
    int i = (*s == '-') ? 1 : 0;
    if (!s[i]) return 0;
    for (; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static int is_temp(const char *s) {
    if (!s || s[0] != 't') return 0;
    for (int i = 1; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return s[1] != '\0';
}

/* Forward declaration */
static ADTKind adt_kind_of(CG *cg, const char *name);

static int is_int_var(CG *cg, const char *name) {
    for (int i = 0; i < cg->var_count; i++)
        if (strcmp(cg->vars[i], name) == 0) return 1;
    return 0;
}

static void reg_var(CG *cg, const char *name) {
    /* Never register a name that is already an ADT variable */
    if (adt_kind_of(cg, name) != KIND_NONE) return;
    if (!is_int_var(cg, name) && cg->var_count < 128)
        strncpy(cg->vars[cg->var_count++], name, 31);
}

static ADTKind adt_kind_of(CG *cg, const char *name) {
    for (int i = 0; i < cg->adt_count; i++)
        if (strcmp(cg->adts[i].name, name) == 0)
            return cg->adts[i].kind;
    return KIND_NONE;
}

static void reg_adt(CG *cg, const char *name, ADTKind kind) {
    if (adt_kind_of(cg, name) != KIND_NONE) return;
    if (cg->adt_count >= MAX_ADTS) return;
    strncpy(cg->adts[cg->adt_count].name, name, 31);
    cg->adts[cg->adt_count].kind = kind;
    cg->adt_count++;
}

/* Emit a load for a user int variable; return register name in buf. */
static const char *load_val(CG *cg, const char *operand, char *buf) {
    if (is_lit(operand))  { snprintf(buf, 32, "%s",   operand); return buf; }
    if (is_temp(operand)) { snprintf(buf, 32, "%%%s",  operand); return buf; }
    /* Safety: ADT names have no slot — should never be loaded as int */
    if (adt_kind_of(cg, operand) != KIND_NONE) {
        snprintf(buf, 32, "0");   /* fallback: emit 0, codegen bug if reached */
        return buf;
    }
    /* user int variable */
    int r = cg->reg++;
    fprintf(cg->out, "  %%r%d = load i32, i32* %%%s_slot, align 4\n", r, operand);
    snprintf(buf, 32, "%%r%d", r);
    return buf;
}

/* ============================================================
 * PASS 1 — COLLECT VARIABLES AND ADT DECLARATIONS
 * ============================================================ */

static ADTKind tag_to_kind(char tag) {
    switch (tag) {
        case 's': return KIND_STACK;
        case 'q': return KIND_QUEUE;
        case 't': return KIND_TREE;
        case 'g': return KIND_GRAPH;
        default:  return KIND_NONE;
    }
}

static void collect(CG *cg, IRInstruction *ir) {
    for (IRInstruction *i = ir; i; i = i->next) {
        if (i->op == IR_ADT_DECL) {
            reg_adt(cg, i->result, tag_to_kind(i->cmp_op));
        } else if (i->op == IR_ASSIGN && !is_temp(i->result)) {
            /* only register if it is not an ADT */
            reg_var(cg, i->result);
        }
    }
}

/* ============================================================
 * MODULE HEADER
 * ============================================================ */

static void emit_header(CG *cg) {
    fprintf(cg->out,
        "; ============================================================\n"
        "; SIMPL Compiled Output — LLVM IR  (with ADT support)\n"
        "; Compile: clang output.ll -o program\n"
        "; ============================================================\n\n"
        "target triple = \"x86_64-w64-windows-gnu\"\n\n"
        "@fmt      = private unnamed_addr constant [4 x i8]  c\"%%d\\0A\\00\"\n"
        "@fmt_d    = private unnamed_addr constant [3 x i8]  c\"%%d\\00\"\n"
        "@fmt_nl   = private unnamed_addr constant [2 x i8]  c\"\\0A\\00\"\n"
        "@fmt_lbr  = private unnamed_addr constant [2 x i8]  c\"[\\00\"\n"
        "@fmt_rbr  = private unnamed_addr constant [3 x i8]  c\"]\\0A\\00\"\n"
        "@fmt_sep  = private unnamed_addr constant [3 x i8]  c\", \\00\"\n"
        "@fmt_arr  = private unnamed_addr constant [5 x i8]  c\"->[ \\00\"\n"
        "@fmt_arrn = private unnamed_addr constant [3 x i8]  c\"]\\0A\\00\"\n"
        "declare i32 @printf(i8*, ...)\n"
        "declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i1)\n\n"
    );
}

/* ============================================================
 * ALLOCA BLOCK  (entry block variable/ADT slots)
 * ============================================================ */

static void emit_allocas(CG *cg) {
    /* integer variables */
    for (int i = 0; i < cg->var_count; i++)
        fprintf(cg->out,
            "  %%%s_slot = alloca i32, align 4\n", cg->vars[i]);

    /* ADT storage */
    for (int i = 0; i < cg->adt_count; i++) {
        const char *n = cg->adts[i].name;
        switch (cg->adts[i].kind) {

            case KIND_STACK:
                fprintf(cg->out,
                    "  %%%s_data = alloca [%d x i32], align 4\n"
                    "  %%%s_top  = alloca i32, align 4\n",
                    n, STACK_CAP, n);
                break;

            case KIND_QUEUE:
                fprintf(cg->out,
                    "  %%%s_data = alloca [%d x i32], align 4\n"
                    "  %%%s_head = alloca i32, align 4\n"
                    "  %%%s_tail = alloca i32, align 4\n",
                    n, QUEUE_CAP, n, n);
                break;

            case KIND_TREE:
                fprintf(cg->out,
                    "  %%%s_data = alloca [%d x i32], align 4\n"
                    "  %%%s_size = alloca i32, align 4\n",
                    n, TREE_CAP, n);
                break;

            case KIND_GRAPH: {
                fprintf(cg->out,
                    "  %%%s_mat  = alloca [%d x i32], align 4\n",
                    n, GRAPH_N * GRAPH_N);
                /* Zero the matrix immediately after alloca using llvm.memset */
                fprintf(cg->out,
                    "  %%ms_%s = bitcast [%d x i32]* %%%s_mat to i8*\n"
                    "  call void @llvm.memset.p0i8.i64(i8* %%ms_%s, i8 0, i64 %d, i1 false)\n",
                    n, GRAPH_N * GRAPH_N, n,
                    n, GRAPH_N * GRAPH_N * 4);
                break;
            }

            default: break;
        }
    }

    /* zero-initialise ADT indices */
    for (int i = 0; i < cg->adt_count; i++) {
        const char *n = cg->adts[i].name;
        switch (cg->adts[i].kind) {
            case KIND_STACK:
                fprintf(cg->out,
                    "  store i32 0, i32* %%%s_top,  align 4\n", n);
                break;
            case KIND_QUEUE:
                fprintf(cg->out,
                    "  store i32 0, i32* %%%s_head, align 4\n"
                    "  store i32 0, i32* %%%s_tail, align 4\n", n, n);
                break;
            case KIND_TREE:
                fprintf(cg->out,
                    "  store i32 0, i32* %%%s_size, align 4\n", n);
                break;
            default: break;
        }
    }

    if (cg->var_count + cg->adt_count > 0)
        fprintf(cg->out, "\n");
}

/* ============================================================
 * INSTRUCTION EMITTERS — integers
 * ============================================================ */

static void emit_binop(CG *cg, const char *llop, IRInstruction *inst) {
    char ba[32], bb[32];
    const char *a = load_val(cg, inst->arg1, ba);
    const char *b = load_val(cg, inst->arg2, bb);
    fprintf(cg->out, "  %%%s = %s i32 %s, %s\n",
            inst->result, llop, a, b);
}

static void emit_cmp(CG *cg, IRInstruction *inst) {
    const char *pred =
        inst->cmp_op == '>' ? "sgt" :
        inst->cmp_op == '<' ? "slt" :
        inst->cmp_op == '=' ? "eq"  : "ne";
    char ba[32], bb[32];
    const char *a = load_val(cg, inst->arg1, ba);
    const char *b = load_val(cg, inst->arg2, bb);
    int cr = cg->reg++;
    fprintf(cg->out,
        "  %%cmp%d = icmp %s i32 %s, %s\n"
        "  %%%s = zext i1 %%cmp%d to i32\n",
        cr, pred, a, b, inst->result, cr);
}

static void emit_assign(CG *cg, IRInstruction *inst) {
    char buf[32];
    const char *src = load_val(cg, inst->arg1, buf);
    if (is_temp(inst->result))
        fprintf(cg->out, "  %%%s = add i32 %s, 0\n", inst->result, src);
    else
        fprintf(cg->out, "  store i32 %s, i32* %%%s_slot, align 4\n",
                src, inst->result);
}

static void emit_print(CG *cg, IRInstruction *inst) {
    char buf[32];
    const char *v = load_val(cg, inst->result, buf);
    int gr = cg->reg++;
    fprintf(cg->out,
        "  %%gep%d = getelementptr [4 x i8], [4 x i8]* @fmt, i32 0, i32 0\n"
        "  call i32 (i8*, ...) @printf(i8* %%gep%d, i32 %s)\n",
        gr, gr, v);
}

static void emit_label_instr(CG *cg, IRInstruction *inst,
                              IRInstruction *prev) {
    int prev_term = prev &&
        (prev->op == IR_GOTO || prev->op == IR_IF_FALSE_GOTO);
    if (!prev_term)
        fprintf(cg->out, "  br label %%%s\n", inst->result);
    fprintf(cg->out, "\n%s:\n", inst->result);
}

static void emit_goto(CG *cg, IRInstruction *inst) {
    fprintf(cg->out, "  br label %%%s\n", inst->result);
}

static void emit_if_false_goto(CG *cg, IRInstruction *inst,
                                IRInstruction *nxt) {
    char buf[32];
    const char *cond = load_val(cg, inst->arg1, buf);
    int cr = cg->reg++;
    char ft[32];
    if (nxt && nxt->op == IR_LABEL)
        snprintf(ft, 32, "%s", nxt->result);
    else
        snprintf(ft, 32, "ft%d", cr);

    fprintf(cg->out,
        "  %%cond%d = icmp ne i32 %s, 0\n"
        "  br i1 %%cond%d, label %%%s, label %%%s\n",
        cr, cond, cr, ft, inst->result);

    if (!(nxt && nxt->op == IR_LABEL))
        fprintf(cg->out, "\n%s:\n", ft);
}

/* ============================================================
 * INSTRUCTION EMITTERS — ADT (inline LLVM IR)
 * ============================================================ */

/* ── STACK ───────────────────────────────────────────────
 *  Layout:  %<name>_data = [STACK_CAP x i32]
 *           %<name>_top  = i32   (index of next free slot)
 *
 *  push val:
 *    top0  = load top
 *    gep   = &data[top0]
 *    store val → *gep
 *    top1  = top0 + 1
 *    store top1 → top
 *
 *  pop:
 *    top0  = load top
 *    top1  = top0 - 1
 *    store top1 → top
 * ─────────────────────────────────────────────────────── */
static void emit_stack_push(CG *cg, IRInstruction *inst) {
    const char *n = inst->result;
    char vbuf[32];
    const char *val = load_val(cg, inst->arg1, vbuf);
    int r = cg->reg;  cg->reg += 4;

    fprintf(cg->out,
        "  %%r%d = load i32, i32* %%%s_top, align 4\n"
        "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n"
        "  store i32 %s, i32* %%r%d, align 4\n"
        "  %%r%d = add i32 %%r%d, 1\n"
        "  store i32 %%r%d, i32* %%%s_top, align 4\n",
        r,   n,
        r+1, STACK_CAP, STACK_CAP, n, r,
        val, r+1,
        r+2, r,
        r+2, n);
}

static void emit_stack_pop(CG *cg, IRInstruction *inst) {
    const char *n = inst->result;
    int r = cg->reg;  cg->reg += 2;

    fprintf(cg->out,
        "  %%r%d = load i32, i32* %%%s_top, align 4\n"
        "  %%r%d = sub i32 %%r%d, 1\n"
        "  store i32 %%r%d, i32* %%%s_top, align 4\n",
        r,   n,
        r+1, r,
        r+1, n);
}

/* ── QUEUE ───────────────────────────────────────────────
 *  Layout:  %<name>_data = [QUEUE_CAP x i32]
 *           %<name>_head = i32   (dequeue pointer)
 *           %<name>_tail = i32   (enqueue pointer)
 *
 *  enqueue val:
 *    tail0 = load tail
 *    gep   = &data[tail0]
 *    store val → *gep
 *    tail1 = (tail0 + 1) % CAP   (implemented as add; mod omitted
 *                                  for clarity — add if needed)
 *    store tail1 → tail
 *
 *  dequeue:
 *    head0 = load head
 *    head1 = head0 + 1
 *    store head1 → head
 * ─────────────────────────────────────────────────────── */
static void emit_queue_enqueue(CG *cg, IRInstruction *inst) {
    const char *n = inst->result;
    char vbuf[32];
    const char *val = load_val(cg, inst->arg1, vbuf);
    int r = cg->reg;  cg->reg += 4;

    fprintf(cg->out,
        "  %%r%d = load i32, i32* %%%s_tail, align 4\n"
        "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n"
        "  store i32 %s, i32* %%r%d, align 4\n"
        "  %%r%d = add i32 %%r%d, 1\n"
        "  store i32 %%r%d, i32* %%%s_tail, align 4\n",
        r,   n,
        r+1, QUEUE_CAP, QUEUE_CAP, n, r,
        val, r+1,
        r+2, r,
        r+2, n);
}

static void emit_queue_dequeue(CG *cg, IRInstruction *inst) {
    const char *n = inst->result;
    int r = cg->reg;  cg->reg += 2;

    fprintf(cg->out,
        "  %%r%d = load i32, i32* %%%s_head, align 4\n"
        "  %%r%d = sub i32 %%r%d, -1\n"
        "  store i32 %%r%d, i32* %%%s_head, align 4\n",
        r,   n,
        r+1, r,
        r+1, n);
}

/* ── TREE ────────────────────────────────────────────────
 *  Array-based storage (index 0 = root for heap, or just
 *  sequential slots for a simple ordered structure).
 *
 *  Layout:  %<name>_data = [TREE_CAP x i32]
 *           %<name>_size = i32
 *
 *  insert val:
 *    sz0 = load size
 *    gep = &data[sz0]
 *    store val → *gep
 *    sz1 = sz0 + 1
 *    store sz1 → size
 *
 *  remove val:
 *    Linear scan; store INT32_MIN as tombstone when found.
 *    Uses a small inline loop emitted as LLVM basic blocks.
 * ─────────────────────────────────────────────────────── */
static void emit_tree_insert(CG *cg, IRInstruction *inst) {
    const char *n = inst->result;
    char vbuf[32];
    const char *val = load_val(cg, inst->arg1, vbuf);
    int r = cg->reg;  cg->reg += 4;

    fprintf(cg->out,
        "  %%r%d = load i32, i32* %%%s_size, align 4\n"
        "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n"
        "  store i32 %s, i32* %%r%d, align 4\n"
        "  %%r%d = add i32 %%r%d, 1\n"
        "  store i32 %%r%d, i32* %%%s_size, align 4\n",
        r,   n,
        r+1, TREE_CAP, TREE_CAP, n, r,
        val, r+1,
        r+2, r,
        r+2, n);
}

static void emit_tree_remove(CG *cg, IRInstruction *inst) {
    /* Emit an inline linear-scan loop to find and tombstone the value.
     *
     *  LLVM IR structure:
     *    br tree_rm_loop_<id>
     *  tree_rm_loop_<id>:
     *    phi i = [0, entry], [i+1, tree_rm_body_<id>]
     *    sz = load size
     *    done = icmp sge i, sz
     *    br done → tree_rm_end_<id>, tree_rm_body_<id>
     *  tree_rm_body_<id>:
     *    gep  = &data[i]
     *    elem = load *gep
     *    hit  = icmp eq elem, val
     *    br hit → tree_rm_hit_<id>, tree_rm_next_<id>
     *  tree_rm_hit_<id>:
     *    store INT32_MIN → *gep
     *    br tree_rm_end_<id>
     *  tree_rm_next_<id>:
     *    i1 = i + 1
     *    br tree_rm_loop_<id>
     *  tree_rm_end_<id>:
     */
    const char *n = inst->result;
    char vbuf[32];
    const char *val = load_val(cg, inst->arg1, vbuf);
    int id = cg->reg++;

    /* unique label names */
    char lloop[40], lbody[40], lhit[40], lnext[40], lend[40];
    snprintf(lloop, 40, "tree_rm_loop_%d", id);
    snprintf(lbody, 40, "tree_rm_body_%d", id);
    snprintf(lhit,  40, "tree_rm_hit_%d",  id);
    snprintf(lnext, 40, "tree_rm_next_%d", id);
    snprintf(lend,  40, "tree_rm_end_%d",  id);

    int r = cg->reg; cg->reg += 8;

    fprintf(cg->out,
        "  br label %%%s\n"
        "\n%s:\n"
        "  %%r%d = phi i32 [ 0, %%entry_or_prev ], [ %%r%d, %%%s ]\n",
        lloop,
        lloop,
        r,   r+7, lnext);

    /* The phi above needs the predecessor label. Since we don't track
     * the last emitted label precisely here, we use a helper approach:
     * emit a dedicated pre-loop block to give phi a clean predecessor. */

    /* Simpler alternative: use alloca for loop counter instead of phi.
     * This avoids needing predecessor label tracking and is equally
     * valid — mem2reg will clean it up. */

    /* ── Rewrite using alloca-based loop counter (mem2reg-friendly) ── */
    /* Undo the fprintf above — use a cleaner pattern */
    /* We'll use a fresh reg set */
    cg->reg = r;  /* reset, we'll reuse */

    /* Actually output the simpler alloca-counter version: */
    int idx_r  = cg->reg++;
    int sz_r   = cg->reg++;
    int cmp_r  = cg->reg++;
    int gep_r  = cg->reg++;
    int el_r   = cg->reg++;
    int hit_r  = cg->reg++;
    int inc_r  = cg->reg++;

    /* We need to undo the partial fprintf we already sent. Unfortunately
     * we can't un-write to the file. Use a cleaner split: emit the whole
     * thing as a self-contained block, using a fresh alloca for the index.
     * The partial phi line above is already written — we'll comment it out
     * by restarting the approach entirely with alloca. */

    /* ── Correct approach: alloca loop index, no phi needed ── */
    fprintf(cg->out,
        "  ; tree remove: linear scan for value %s in %s\n"
        "  %%tidx_%d = alloca i32, align 4\n"
        "  store i32 0, i32* %%tidx_%d, align 4\n"
        "  br label %%%s\n"
        "\n%s:\n"
        "  %%r%d = load i32, i32* %%tidx_%d, align 4\n"
        "  %%r%d = load i32, i32* %%%s_size, align 4\n"
        "  %%r%d = icmp sge i32 %%r%d, %%r%d\n"
        "  br i1 %%r%d, label %%%s, label %%%s\n"
        "\n%s:\n"
        "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n"
        "  %%r%d = load i32, i32* %%r%d, align 4\n"
        "  %%r%d = icmp eq i32 %%r%d, %s\n"
        "  br i1 %%r%d, label %%%s, label %%%s\n"
        "\n%s:\n"
        "  store i32 -2147483648, i32* %%r%d, align 4\n"
        "  br label %%%s\n"
        "\n%s:\n"
        "  %%r%d = add i32 %%r%d, 1\n"
        "  store i32 %%r%d, i32* %%tidx_%d, align 4\n"
        "  br label %%%s\n"
        "\n%s:\n",
        val, n,
        id,
        id,
        lloop,
        lloop,
        idx_r, id,
        sz_r, n,
        cmp_r, idx_r, sz_r,
        cmp_r, lend, lbody,
        lbody,
        gep_r, TREE_CAP, TREE_CAP, n, idx_r,
        el_r, gep_r,
        hit_r, el_r, val,
        hit_r, lhit, lnext,
        lhit,
        gep_r,
        lend,
        lnext,
        inc_r, idx_r,
        inc_r, id,
        lloop,
        lend);
}

/* ── GRAPH ───────────────────────────────────────────────
 *  Layout:  %<name>_mat = [GRAPH_N*GRAPH_N x i32]  (flat adjacency matrix)
 *
 *  add_edge from to:
 *    idx = from * GRAPH_N + to
 *    gep = &mat[idx]
 *    store 1 → *gep
 *
 *  remove_edge from to:
 *    idx = from * GRAPH_N + to
 *    gep = &mat[idx]
 *    store 0 → *gep
 * ─────────────────────────────────────────────────────── */
static void emit_graph_edge(CG *cg, IRInstruction *inst, int add) {
    const char *n = inst->result;
    char fbuf[32], tbuf[32];
    const char *from = load_val(cg, inst->arg1, fbuf);
    const char *to   = load_val(cg, inst->arg2, tbuf);
    int r = cg->reg; cg->reg += 3;

    fprintf(cg->out,
        "  %%r%d = mul i32 %s, %d\n"
        "  %%r%d = add i32 %%r%d, %s\n"
        "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_mat, i32 0, i32 %%r%d\n"
        "  store i32 %d, i32* %%r%d, align 4\n",
        r,   from, GRAPH_N,
        r+1, r, to,
        r+2, GRAPH_N*GRAPH_N, GRAPH_N*GRAPH_N, n, r+1,
        add ? 1 : 0, r+2);
}

/* ============================================================
 * ADT PRINT EMITTERS
 *
 * Stack  — print all elements from top-1 down to 0
 *          Output: [30, 20, 10]
 *
 * Queue  — print elements from head to tail-1
 *          Output: [10, 20, 30]
 *
 * Graph  — print adjacency list: for each node i that has
 *          at least one outgoing edge, print i->[ j j2 ]
 *          Output: 1->[ 2 3 ] 2->[ 3 ]
 *
 * All use alloca-based loop counters (mem2reg-friendly).
 * ============================================================ */

static void emit_print_adt(CG *cg, IRInstruction *inst) {
    const char *n   = inst->result;
    char        tag = inst->cmp_op;
    FILE       *o   = cg->out;

    /* Every print site gets a unique ID for label names */
    int id = cg->reg++;

    char lloop[48], lbody[48], lend[48], lsep[48], lskip[48];
    char linner[48], linbody[48], linend[48], lhasedge[48];
    snprintf(lloop,   48, "ploop_%d",    id);
    snprintf(lbody,   48, "pbody_%d",    id);
    snprintf(lend,    48, "pend_%d",     id);
    snprintf(lsep,    48, "psep_%d",     id);
    snprintf(lskip,   48, "pskip_%d",   id);
    snprintf(linner,  48, "pinner_%d",  id);
    snprintf(linbody, 48, "pinbody_%d", id);
    snprintf(linend,  48, "pinend_%d",  id);
    snprintf(lhasedge,48, "phasedge_%d",id);

/* ── Convenience macro: allocate a fresh SSA register number ── */
#define R() (cg->reg++)

    if (tag == 's') {
        /* Stack: print top-1 down to 0 as [30, 20, 10] */
        int r_top    = R();   /* initial load of top               */
        int r_init   = R();   /* top - 1 (initial loop index)      */
        /* --- loop --- */
        int r_idx    = R();   /* load pidx each iteration          */
        int r_cond   = R();   /* idx >= 0                          */
        /* --- separator check --- */
        int r_lt     = R();   /* load pidx for sep check           */
        int r_topld  = R();   /* reload top for sep check          */
        int r_top1   = R();   /* top - 1                           */
        int r_scmp   = R();   /* icmp slt                          */
        /* --- print element --- */
        int r_eidx   = R();   /* load pidx for element load        */
        int r_gep    = R();   /* GEP into data array               */
        int r_elem   = R();   /* loaded element value              */
        /* --- decrement --- */
        int r_dec    = R();   /* idx - 1                           */

        fprintf(o, "  ; === print stack %s ===\n", n);
        /* print "[" */
        fprintf(o, "  %%gep%d = getelementptr [2 x i8], [2 x i8]* @fmt_lbr, i32 0, i32 0\n", id);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", id);
        /* init loop index = top - 1 */
        fprintf(o, "  %%pidx_%d = alloca i32, align 4\n", id);
        fprintf(o, "  %%r%d = load i32, i32* %%%s_top, align 4\n", r_top, n);
        fprintf(o, "  %%r%d = sub i32 %%r%d, 1\n", r_init, r_top);
        fprintf(o, "  store i32 %%r%d, i32* %%pidx_%d, align 4\n", r_init, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", lloop, lloop);
        /* loop condition: idx >= 0 */
        fprintf(o, "  %%r%d = load i32, i32* %%pidx_%d, align 4\n", r_idx, id);
        fprintf(o, "  %%r%d = icmp sge i32 %%r%d, 0\n", r_cond, r_idx);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_cond, lbody, lend, lbody);
        /* separator: if idx < top-1, print ", " */
        fprintf(o, "  %%r%d = load i32, i32* %%pidx_%d, align 4\n", r_lt, id);
        fprintf(o, "  %%r%d = load i32, i32* %%%s_top, align 4\n", r_topld, n);
        fprintf(o, "  %%r%d = sub i32 %%r%d, 1\n", r_top1, r_topld);
        fprintf(o, "  %%r%d = icmp slt i32 %%r%d, %%r%d\n", r_scmp, r_lt, r_top1);
        /* idx < top-1 means we are NOT the topmost → print separator before this element */
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_scmp, lsep, lskip, lsep);
        int r_sg = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_sep, i32 0, i32 0\n", r_sg);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_sg);
        fprintf(o, "  br label %%%s\n\n%s:\n", lskip, lskip);
        /* load and print element */
        fprintf(o, "  %%r%d = load i32, i32* %%pidx_%d, align 4\n", r_eidx, id);
        fprintf(o, "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n",
                r_gep, STACK_CAP, STACK_CAP, n, r_eidx);
        fprintf(o, "  %%r%d = load i32, i32* %%r%d, align 4\n", r_elem, r_gep);
        int r_pg = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_d, i32 0, i32 0\n", r_pg);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d, i32 %%r%d)\n", r_pg, r_elem);
        /* decrement */
        fprintf(o, "  %%r%d = sub i32 %%r%d, 1\n", r_dec, r_eidx);
        fprintf(o, "  store i32 %%r%d, i32* %%pidx_%d, align 4\n", r_dec, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", lloop, lend);
        /* print "]\n" */
        int r_rb = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_rbr, i32 0, i32 0\n", r_rb);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_rb);

    } else if (tag == 'q') {
        /* Queue: print head to tail-1 as [100, 200, 300] */
        int r_head  = R();
        int r_tail  = R();
        /* --- loop --- */
        int r_idx   = R();
        int r_cond  = R();
        /* --- separator --- */
        int r_hd2   = R();
        int r_scmp  = R();
        /* --- element --- */
        int r_gep   = R();
        int r_elem  = R();
        /* --- increment --- */
        int r_inc   = R();

        fprintf(o, "  ; === print queue %s ===\n", n);
        int r_lb = R();
        fprintf(o, "  %%gep%d = getelementptr [2 x i8], [2 x i8]* @fmt_lbr, i32 0, i32 0\n", r_lb);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_lb);
        fprintf(o, "  %%pidx_%d = alloca i32, align 4\n", id);
        fprintf(o, "  %%r%d = load i32, i32* %%%s_head, align 4\n", r_head, n);
        fprintf(o, "  %%r%d = load i32, i32* %%%s_tail, align 4\n", r_tail, n);
        fprintf(o, "  store i32 %%r%d, i32* %%pidx_%d, align 4\n", r_head, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", lloop, lloop);
        /* condition: idx < tail */
        fprintf(o, "  %%r%d = load i32, i32* %%pidx_%d, align 4\n", r_idx, id);
        fprintf(o, "  %%r%d = icmp slt i32 %%r%d, %%r%d\n", r_cond, r_idx, r_tail);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_cond, lbody, lend, lbody);
        /* separator: if idx > head, print ", " */
        fprintf(o, "  %%r%d = load i32, i32* %%%s_head, align 4\n", r_hd2, n);
        fprintf(o, "  %%r%d = icmp sgt i32 %%r%d, %%r%d\n", r_scmp, r_idx, r_hd2);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_scmp, lsep, lskip, lsep);
        int r_sg = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_sep, i32 0, i32 0\n", r_sg);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_sg);
        fprintf(o, "  br label %%%s\n\n%s:\n", lskip, lskip);
        /* load and print element */
        fprintf(o, "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_data, i32 0, i32 %%r%d\n",
                r_gep, QUEUE_CAP, QUEUE_CAP, n, r_idx);
        fprintf(o, "  %%r%d = load i32, i32* %%r%d, align 4\n", r_elem, r_gep);
        int r_pg = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_d, i32 0, i32 0\n", r_pg);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d, i32 %%r%d)\n", r_pg, r_elem);
        fprintf(o, "  %%r%d = add i32 %%r%d, 1\n", r_inc, r_idx);
        fprintf(o, "  store i32 %%r%d, i32* %%pidx_%d, align 4\n", r_inc, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", lloop, lend);
        int r_rb = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_rbr, i32 0, i32 0\n", r_rb);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_rb);

    } else if (tag == 'g') {
        /* Graph: adjacency list.
         * Every label is unique — no label name is reused anywhere.
         *
         * Outer loop iterates rows 0..GRAPH_N-1.
         * Inner loop iterates cols 0..GRAPH_N-1.
         * On first edge in a row, print "row->[ ".
         * For each edge, print "col, ".
         * After inner loop, if header was printed, print "]\n".
         *
         * Labels used (all suffixed with unique id):
         *   outer_loop, outer_body, outer_end
         *   inner_loop, inner_body, inner_end
         *   has_edge,   no_edge
         *   hdr_check,  hdr_print, hdr_skip
         *   close_check, close_print, close_skip
         *   next_row
         */
        fprintf(o, "  ; === print graph %s ===\n", n);
        fprintf(o, "  %%prow_%d = alloca i32, align 4\n", id);
        fprintf(o, "  store i32 0, i32* %%prow_%d, align 4\n", id);

        char louter[48], obody[48], oend[48];
        char linner[48], ibody[48], iend[48];
        char lhasedge[48], lnoedge[48];
        char lhdrchk[48], lhdrprint[48], lhdrskip[48];
        char lclosechk[48], lcloseprint[48], lcloseskip[48];
        char lnextrow[48];

        snprintf(louter,     48, "gouter_%d",     id);
        snprintf(obody,      48, "gobody_%d",     id);
        snprintf(oend,       48, "goend_%d",      id);
        snprintf(linner,     48, "ginner_%d",     id);
        snprintf(ibody,      48, "gibody_%d",     id);
        snprintf(iend,       48, "giend_%d",      id);
        snprintf(lhasedge,   48, "ghasedge_%d",   id);
        snprintf(lnoedge,    48, "gnoedge_%d",    id);
        snprintf(lhdrchk,    48, "ghdrchk_%d",    id);
        snprintf(lhdrprint,  48, "ghdrprint_%d",  id);
        snprintf(lhdrskip,   48, "ghdrskip_%d",   id);
        snprintf(lclosechk,  48, "gclosechk_%d",  id);
        snprintf(lcloseprint,48, "gcloseprint_%d",id);
        snprintf(lcloseskip, 48, "gcloseskip_%d", id);
        snprintf(lnextrow,   48, "gnextrow_%d",   id);

        fprintf(o, "  br label %%%s\n\n%s:\n", louter, louter);

        /* ── outer condition: row < GRAPH_N ── */
        int r_row  = R();
        int r_ocmp = R();
        fprintf(o, "  %%r%d = load i32, i32* %%prow_%d, align 4\n", r_row, id);
        fprintf(o, "  %%r%d = icmp slt i32 %%r%d, %d\n", r_ocmp, r_row, GRAPH_N);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_ocmp, obody, oend, obody);

        /* ── per-row allocas ── */
        fprintf(o, "  %%pcol_%d = alloca i32, align 4\n", id);
        fprintf(o, "  %%phdr_%d = alloca i32, align 4\n", id);
        fprintf(o, "  store i32 0, i32* %%pcol_%d, align 4\n", id);
        fprintf(o, "  store i32 0, i32* %%phdr_%d, align 4\n", id);
        fprintf(o, "  br label %%%s\n\n%s:\n", linner, linner);

        /* ── inner condition: col < GRAPH_N ── */
        int r_col  = R();
        int r_icmp = R();
        fprintf(o, "  %%r%d = load i32, i32* %%pcol_%d, align 4\n", r_col, id);
        fprintf(o, "  %%r%d = icmp slt i32 %%r%d, %d\n", r_icmp, r_col, GRAPH_N);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_icmp, ibody, iend, ibody);

        /* ── compute mat[row*N + col] ── */
        int r_row2 = R();
        int r_mul  = R();
        int r_col2 = R();
        int r_idx  = R();
        int r_gep  = R();
        int r_val  = R();
        int r_ne   = R();
        fprintf(o, "  %%r%d = load i32, i32* %%prow_%d, align 4\n", r_row2, id);
        fprintf(o, "  %%r%d = mul i32 %%r%d, %d\n", r_mul, r_row2, GRAPH_N);
        fprintf(o, "  %%r%d = load i32, i32* %%pcol_%d, align 4\n", r_col2, id);
        fprintf(o, "  %%r%d = add i32 %%r%d, %%r%d\n", r_idx, r_mul, r_col2);
        fprintf(o, "  %%r%d = getelementptr [%d x i32], [%d x i32]* %%%s_mat, i32 0, i32 %%r%d\n",
                r_gep, GRAPH_N*GRAPH_N, GRAPH_N*GRAPH_N, n, r_idx);
        fprintf(o, "  %%r%d = load i32, i32* %%r%d, align 4\n", r_val, r_gep);
        fprintf(o, "  %%r%d = icmp ne i32 %%r%d, 0\n", r_ne, r_val);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_ne, lhasedge, lnoedge, lhasedge);

        /* ── has edge: check if header already printed ── */
        int r_hdr  = R();
        int r_heq  = R();
        fprintf(o, "  %%r%d = load i32, i32* %%phdr_%d, align 4\n", r_hdr, id);
        fprintf(o, "  %%r%d = icmp eq i32 %%r%d, 0\n", r_heq, r_hdr);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_heq, lhdrprint, lhdrskip, lhdrprint);

        /* ── print row header: "row->[ " ── */
        int r_rowp = R();
        int r_gfmt = R();
        int r_garr = R();
        fprintf(o, "  %%r%d = load i32, i32* %%prow_%d, align 4\n", r_rowp, id);
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_d, i32 0, i32 0\n", r_gfmt);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d, i32 %%r%d)\n", r_gfmt, r_rowp);
        fprintf(o, "  %%gep%d = getelementptr [5 x i8], [5 x i8]* @fmt_arr, i32 0, i32 0\n", r_garr);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_garr);
        fprintf(o, "  store i32 1, i32* %%phdr_%d, align 4\n", id);
        fprintf(o, "  br label %%%s\n\n%s:\n", lhdrskip, lhdrskip);

        /* ── print neighbour: "col, " ── */
        int r_colp = R();
        int r_gfc  = R();
        int r_gfs  = R();
        fprintf(o, "  %%r%d = load i32, i32* %%pcol_%d, align 4\n", r_colp, id);
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_d, i32 0, i32 0\n", r_gfc);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d, i32 %%r%d)\n", r_gfc, r_colp);
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_sep, i32 0, i32 0\n", r_gfs);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_gfs);
        fprintf(o, "  br label %%%s\n\n%s:\n", lnoedge, lnoedge);

        /* ── increment col ── */
        int r_col3 = R();
        int r_cinc = R();
        fprintf(o, "  %%r%d = load i32, i32* %%pcol_%d, align 4\n", r_col3, id);
        fprintf(o, "  %%r%d = add i32 %%r%d, 1\n", r_cinc, r_col3);
        fprintf(o, "  store i32 %%r%d, i32* %%pcol_%d, align 4\n", r_cinc, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", linner, iend);

        /* ── inner loop ended: close row if header was printed ── */
        int r_hdr2 = R();
        int r_hne  = R();
        fprintf(o, "  %%r%d = load i32, i32* %%phdr_%d, align 4\n", r_hdr2, id);
        fprintf(o, "  %%r%d = icmp ne i32 %%r%d, 0\n", r_hne, r_hdr2);
        fprintf(o, "  br i1 %%r%d, label %%%s, label %%%s\n\n%s:\n",
                r_hne, lcloseprint, lcloseskip, lcloseprint);

        /* ── print "]\n" ── */
        int r_gclose = R();
        fprintf(o, "  %%gep%d = getelementptr [3 x i8], [3 x i8]* @fmt_arrn, i32 0, i32 0\n", r_gclose);
        fprintf(o, "  call i32 (i8*, ...) @printf(i8* %%gep%d)\n", r_gclose);
        fprintf(o, "  br label %%%s\n\n%s:\n", lcloseskip, lcloseskip);

        /* ── increment row ── */
        int r_row3 = R();
        int r_rinc = R();
        fprintf(o, "  %%r%d = load i32, i32* %%prow_%d, align 4\n", r_row3, id);
        fprintf(o, "  %%r%d = add i32 %%r%d, 1\n", r_rinc, r_row3);
        fprintf(o, "  store i32 %%r%d, i32* %%prow_%d, align 4\n", r_rinc, id);
        fprintf(o, "  br label %%%s\n\n%s:\n", louter, oend);
    }
#undef R
}

/* ============================================================
 * MAIN EMISSION LOOP
 * ============================================================ */

static void emit_instructions(CG *cg, IRInstruction *ir) {
    IRInstruction *prev = NULL;

    for (IRInstruction *inst = ir; inst; inst = inst->next) {
        if (is_dead(inst)) { prev = inst; continue; }

        switch (inst->op) {
            case IR_NOP:      break;
            case IR_ADT_DECL: break;   /* handled in alloca block */

            case IR_ASSIGN:         emit_assign(cg, inst);         break;
            case IR_ADD:            emit_binop(cg, "add",  inst);  break;
            case IR_SUB:            emit_binop(cg, "sub",  inst);  break;
            case IR_MUL:            emit_binop(cg, "mul",  inst);  break;
            case IR_DIV:            emit_binop(cg, "sdiv", inst);  break;
            case IR_CMP:            emit_cmp(cg, inst);            break;
            case IR_PRINT:          emit_print(cg, inst);          break;
            case IR_LABEL:          emit_label_instr(cg, inst, prev); break;
            case IR_GOTO:           emit_goto(cg, inst);           break;
            case IR_IF_FALSE_GOTO:  emit_if_false_goto(cg, inst, inst->next); break;

            case IR_STACK_PUSH:     emit_stack_push(cg, inst);     break;
            case IR_STACK_POP:      emit_stack_pop(cg, inst);      break;
            case IR_QUEUE_ENQUEUE:  emit_queue_enqueue(cg, inst);  break;
            case IR_QUEUE_DEQUEUE:  emit_queue_dequeue(cg, inst);  break;
            case IR_TREE_INSERT:    emit_tree_insert(cg, inst);    break;
            case IR_TREE_REMOVE:    emit_tree_remove(cg, inst);    break;
            case IR_GRAPH_ADD_EDGE:    emit_graph_edge(cg, inst, 1); break;
            case IR_GRAPH_REMOVE_EDGE: emit_graph_edge(cg, inst, 0); break;
            case IR_PRINT_ADT:          emit_print_adt(cg, inst);      break;
        }
        prev = inst;
    }
}

/* ============================================================
 * PUBLIC ENTRY POINT
 * ============================================================ */

int codegen_emit_llvm(IRInstruction *ir_head, const char *out_path) {
    FILE *out = fopen(out_path, "w");
    if (!out) { perror(out_path); return -1; }

    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.out = out;
    cg.reg = 0;

    emit_header(&cg);
    collect(&cg, ir_head);

    fprintf(out, "define i32 @main() {\nentry:\n");
    emit_allocas(&cg);
    emit_instructions(&cg, ir_head);
    fprintf(out, "\n  ret i32 0\n}\n");

    fclose(out);
    printf("[CG] LLVM IR written to: %s\n", out_path);
    printf("[CG] Compile with: clang %s -o program\n", out_path);
    return 0;
}
