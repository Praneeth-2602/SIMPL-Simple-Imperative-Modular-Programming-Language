/* ============================================================
 * SIMPL RL Agent — Phase 1: MDP Foundation & Q-Table Skeleton
 * ============================================================
 *
 * Implements:
 *   - RLAgent lifecycle (create / free)
 *   - Q-table persistence (load / save — binary, portable)
 *   - State encoder: SemanticReport → 6-dim bucketed RLState
 *   - ε-greedy policy with linear decay and pass-done masking
 *   - Bellman Q-update (Q-learning)
 *   - rl_run_episode(): full pass-selection episode wired to
 *     existing optimizer passes + green reward function
 * ============================================================ */

#include "rl_agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ============================================================
 * SECTION 1 — LIFECYCLE
 * ============================================================ */

RLAgent *rl_agent_create(void) {
    RLAgent *a = (RLAgent *)calloc(1, sizeof(RLAgent));
    if (!a) {
        fprintf(stderr, "[RL] rl_agent_create: out of memory\n");
        return NULL;
    }
    /* Q-table initialised to 0.0 by calloc.
     * Zero is a neutral start: no pass is preferred over any other,
     * so the agent explores uniformly at the start (ε=1.0 anyway). */
    a->total_episodes = 0;

    /* Seed the PRNG once per process for ε-greedy exploration. */
    srand((unsigned int)time(NULL));

    return a;
}

void rl_agent_free(RLAgent *agent) {
    if (agent) free(agent);
}

/* ============================================================
 * SECTION 2 — PERSISTENCE
 * ============================================================
 *
 * Binary layout (little-endian host order):
 *   [4 bytes]  magic  = 0x524C5154  ("RLTQ")
 *   [4 bytes]  version = 1
 *   [4 bytes]  total_episodes
 *   [4 bytes]  num_states  (sanity check)
 *   [4 bytes]  num_actions (sanity check)
 *   [num_states * num_actions * 4 bytes]  Q-table (float32)
 */

#define RL_MAGIC   0x524C5154u
#define RL_VERSION 1u

int rl_agent_load(RLAgent *agent, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Not an error — first run, start from scratch. */
        printf("[RL] No Q-table found at '%s' — starting fresh.\n", path);
        return 0;
    }

    unsigned int magic, version, episodes, ns, na;

    if (fread(&magic,    sizeof(unsigned int), 1, f) != 1 ||
        fread(&version,  sizeof(unsigned int), 1, f) != 1 ||
        fread(&episodes, sizeof(unsigned int), 1, f) != 1 ||
        fread(&ns,       sizeof(unsigned int), 1, f) != 1 ||
        fread(&na,       sizeof(unsigned int), 1, f) != 1) {
        fprintf(stderr, "[RL] load: short read on header in '%s'\n", path);
        fclose(f);
        return -1;
    }

    if (magic != RL_MAGIC) {
        fprintf(stderr, "[RL] load: bad magic in '%s' (got 0x%08X)\n",
                path, magic);
        fclose(f);
        return -1;
    }
    if (version != RL_VERSION) {
        fprintf(stderr, "[RL] load: unsupported version %u in '%s'\n",
                version, path);
        fclose(f);
        return -1;
    }
    if ((int)ns != RL_NUM_STATES || (int)na != RL_NUM_ACTIONS) {
        fprintf(stderr, "[RL] load: dimension mismatch in '%s' "
                "(expected %d×%d, got %u×%u)\n",
                path, RL_NUM_STATES, RL_NUM_ACTIONS, ns, na);
        fclose(f);
        return -1;
    }

    size_t table_floats = (size_t)RL_NUM_STATES * RL_NUM_ACTIONS;
    if (fread(agent->q, sizeof(float), table_floats, f) != table_floats) {
        fprintf(stderr, "[RL] load: short read on Q-table in '%s'\n", path);
        fclose(f);
        return -1;
    }

    agent->total_episodes = (int)episodes;
    fclose(f);

    printf("[RL] Loaded Q-table from '%s' (episodes=%d)\n",
           path, agent->total_episodes);
    return 0;
}

int rl_agent_save(const RLAgent *agent, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[RL] save: cannot open '%s' for writing\n", path);
        return -1;
    }

    unsigned int magic    = RL_MAGIC;
    unsigned int version  = RL_VERSION;
    unsigned int episodes = (unsigned int)agent->total_episodes;
    unsigned int ns       = (unsigned int)RL_NUM_STATES;
    unsigned int na       = (unsigned int)RL_NUM_ACTIONS;

    fwrite(&magic,    sizeof(unsigned int), 1, f);
    fwrite(&version,  sizeof(unsigned int), 1, f);
    fwrite(&episodes, sizeof(unsigned int), 1, f);
    fwrite(&ns,       sizeof(unsigned int), 1, f);
    fwrite(&na,       sizeof(unsigned int), 1, f);

    size_t table_floats = (size_t)RL_NUM_STATES * RL_NUM_ACTIONS;
    if (fwrite(agent->q, sizeof(float), table_floats, f) != table_floats) {
        fprintf(stderr, "[RL] save: write error on Q-table\n");
        fclose(f);
        return -1;
    }

    fclose(f);
    printf("[RL] Q-table saved to '%s'\n", path);
    return 0;
}

/* ============================================================
 * SECTION 3 — STATE ENCODING
 * ============================================================
 *
 * Converts a SemanticReport into a discrete RLState by bucketing
 * each continuous feature.  The bucketing boundaries match the
 * thresholds used by run_optimizer_adaptive() so the agent starts
 * with the same coarse distinctions the rule engine uses.
 *
 * State index (row-major, dimension order matches header):
 *
 *   idx = loop_bucket
 *       × (RL_ARITH_BUCKETS × RL_ADT_BUCKETS × RL_CONST_BUCKETS
 *          × RL_SIZE_BUCKETS × RL_PASSES_STATES)
 *       + arith_bucket × (...)
 *       + ...
 *       + passes_done
 */

/* Count IR instructions (needed for size_bucket). */
static int count_instrs(IRInstruction *ir) {
    int n = 0;
    for (IRInstruction *p = ir; p; p = p->next) n++;
    return n;
}

RLState rl_encode_state(const SemanticReport *sem,
                        IRInstruction        *ir_head) {
    RLState s;
    memset(&s, 0, sizeof(s));

    /* loop_bucket: 0=none, 1=shallow(depth==1), 2=deep/nested(depth>1) */
    if (sem->max_loop_depth == 0)      s.loop_bucket = 0;
    else if (sem->max_loop_depth == 1) s.loop_bucket = 1;
    else                               s.loop_bucket = 2;

    /* arith_bucket: 0=none, 1=few(<=4), 2=many(>4) */
    if (sem->arithmetic_ops == 0)      s.arith_bucket = 0;
    else if (sem->arithmetic_ops <= 4) s.arith_bucket = 1;
    else                               s.arith_bucket = 2;

    /* adt_bucket: 0=none, 1=light (adt<=arith), 2=dominated (adt>arith) */
    if (sem->adt_ops == 0)
        s.adt_bucket = 0;
    else if (sem->adt_ops > sem->arithmetic_ops)
        s.adt_bucket = 2;
    else
        s.adt_bucket = 1;

    /* const_bucket: 0=no constant exprs, 1=has some */
    s.const_bucket = (sem->constant_exprs > 0) ? 1 : 0;

    /* size_bucket: 0=tiny(<10), 1=medium(10-50), 2=large(>50) */
    int n = count_instrs(ir_head);
    if (n < 10)      s.size_bucket = 0;
    else if (n <= 50) s.size_bucket = 1;
    else              s.size_bucket = 2;

    /* passes_done: starts at 0 for a fresh episode.
     * Updated by the caller between steps via rl_encode_state or
     * directly by setting s.passes_done |= OPT_PASS_* bitmask. */
    s.passes_done = 0;

    return s;
}

int rl_state_index(RLState s) {
    /* Row-major encoding matching the dimension declaration order. */
    int idx = s.loop_bucket;
    idx = idx * RL_ARITH_BUCKETS   + s.arith_bucket;
    idx = idx * RL_ADT_BUCKETS     + s.adt_bucket;
    idx = idx * RL_CONST_BUCKETS   + s.const_bucket;
    idx = idx * RL_SIZE_BUCKETS    + s.size_bucket;
    idx = idx * RL_PASSES_STATES   + s.passes_done;
    return idx;
}

/* ============================================================
 * SECTION 4 — POLICY (ε-GREEDY WITH PASS MASKING)
 * ============================================================ */

float rl_current_epsilon(const RLAgent *agent) {
    if (agent->total_episodes >= RL_EPSILON_DECAY) {
        return RL_EPSILON_MIN;
    }
    /* Linear decay from RL_EPSILON_START to RL_EPSILON_MIN */
    float frac = (float)agent->total_episodes / (float)RL_EPSILON_DECAY;
    return RL_EPSILON_START - frac * (RL_EPSILON_START - RL_EPSILON_MIN);
}

/*
 * Return 1 if the given action is available in state s.
 *
 * An action is unavailable if:
 *   - The corresponding pass has already been applied this episode
 *     (bit set in passes_done).
 *   - STOP is always available.
 */
static int action_available(RLState s, int action) {
    if (action == RL_ACTION_STOP) return 1;

    /* Map action index → OPT_PASS_* bitmask bit */
    static const int action_to_pass_bit[RL_NUM_ACTIONS] = {
        OPT_PASS_CONST_FOLD,    /* RL_ACTION_CONST_FOLD   */
        OPT_PASS_COPY_PROP,     /* RL_ACTION_COPY_PROP    */
        OPT_PASS_CSE,           /* RL_ACTION_CSE          */
        OPT_PASS_DCE,           /* RL_ACTION_DCE          */
        OPT_PASS_UNREACHABLE,   /* RL_ACTION_UNREACHABLE  */
        0,                      /* RL_ACTION_STOP — unused entry */
    };

    return !(s.passes_done & action_to_pass_bit[action]);
}

int rl_agent_select_action(const RLAgent *agent, RLState state) {
    float eps   = rl_current_epsilon(agent);
    float rng   = (float)rand() / ((float)RAND_MAX + 1.0f);

    /* ── Explore: random available action ── */
    if (rng < eps) {
        /* Collect available actions */
        int avail[RL_NUM_ACTIONS];
        int navail = 0;
        for (int a = 0; a < RL_NUM_ACTIONS; a++) {
            if (action_available(state, a)) avail[navail++] = a;
        }
        /* navail is always >=1 because STOP is always available */
        return avail[rand() % navail];
    }

    /* ── Exploit: greedy best available action ── */
    int   si      = rl_state_index(state);
    int   best    = -1;
    float best_q  = -1e30f;

    for (int a = 0; a < RL_NUM_ACTIONS; a++) {
        if (!action_available(state, a)) continue;
        if (agent->q[si][a] > best_q) {
            best_q = agent->q[si][a];
            best   = a;
        }
    }

    /* Fallback: if somehow no action was selected (shouldn't happen), STOP */
    return (best >= 0) ? best : RL_ACTION_STOP;
}

/* ============================================================
 * SECTION 5 — Q-UPDATE (BELLMAN)
 * ============================================================ */

void rl_agent_update(RLAgent *agent,
                     RLState  s,
                     int      action,
                     float    reward,
                     RLState  s_next) {
    int si      = rl_state_index(s);
    int si_next = rl_state_index(s_next);

    /* max Q[s'][a'] over available actions in the next state */
    float max_next = -1e30f;
    for (int a = 0; a < RL_NUM_ACTIONS; a++) {
        if (!action_available(s_next, a)) continue;
        if (agent->q[si_next][a] > max_next)
            max_next = agent->q[si_next][a];
    }
    if (max_next < -1e29f) max_next = 0.0f; /* terminal / all done */

    /* Bellman update */
    float current = agent->q[si][action];
    float target  = reward + RL_GAMMA * max_next;
    agent->q[si][action] = current + RL_ALPHA * (target - current);
}

/* ============================================================
 * SECTION 6 — EPISODE DRIVER
 * ============================================================
 *
 * Green reward function:
 *   R = instructions_eliminated / compile_time_ms
 *
 * compile_time_ms is measured with clock() — a CPU-time proxy for
 * energy.  STOP yields reward 0 (no benefit, no cost).
 *
 * Each step:
 *   1. Select action via ε-greedy policy.
 *   2. Apply the chosen pass through existing optimizer infrastructure.
 *   3. Compute reward.
 *   4. Advance state (update passes_done bitmask).
 *   5. Q-update.
 *   6. Repeat until STOP or all passes exhausted.
 */

/* Map action index to OPT_PASS_* bitmask bit (0 for STOP). */
static int action_to_pass_bit(int action) {
    switch (action) {
        case RL_ACTION_CONST_FOLD:  return OPT_PASS_CONST_FOLD;
        case RL_ACTION_COPY_PROP:   return OPT_PASS_COPY_PROP;
        case RL_ACTION_CSE:         return OPT_PASS_CSE;
        case RL_ACTION_DCE:         return OPT_PASS_DCE;
        case RL_ACTION_UNREACHABLE: return OPT_PASS_UNREACHABLE;
        default:                    return 0;
    }
}

static const char *action_name(int action) {
    switch (action) {
        case RL_ACTION_CONST_FOLD:  return "ConstFold+Prop";
        case RL_ACTION_COPY_PROP:   return "CopyProp";
        case RL_ACTION_CSE:         return "CSE";
        case RL_ACTION_DCE:         return "DCE";
        case RL_ACTION_UNREACHABLE: return "UnreachableElim";
        case RL_ACTION_STOP:        return "STOP";
        default:                    return "?";
    }
}

RLEpisodeResult rl_run_episode(RLAgent              *agent,
                               IRInstruction        *ir_head,
                               const SemanticReport *sem) {
    RLEpisodeResult result;
    memset(&result, 0, sizeof(result));

    if (!ir_head || !sem) {
        fprintf(stderr, "[RL] rl_run_episode: null ir_head or sem\n");
        return result;
    }

    /* Encode initial state (passes_done = 0) */
    RLState state = rl_encode_state(sem, ir_head);

    float eps = rl_current_epsilon(agent);
    result.epsilon_used = eps;

    printf("\n=== SIMPL RL Optimizer (episode %d, ε=%.3f) ===\n",
           agent->total_episodes + 1, eps);
    printf("State: loop=%d arith=%d adt=%d const=%d size=%d\n",
           state.loop_bucket, state.arith_bucket, state.adt_bucket,
           state.const_bucket, state.size_bucket);

    /* Build CFG once — reused by CSE, DCE, Unreachable passes */
    CFG *cfg = build_cfg(ir_head);
    if (!cfg) {
        fprintf(stderr, "[RL] rl_run_episode: failed to build CFG\n");
        return result;
    }

    /* ── Episode loop ── */
    for (int step = 0; step < RL_MAX_STEPS; step++) {

        int action = rl_agent_select_action(agent, state);
        result.pass_sequence[step] = action;

        if (action == RL_ACTION_STOP) {
            printf("  Step %d: STOP\n", step);
            break;
        }

        /* ── Apply pass, measure wall-clock time ── */
        clock_t t0 = clock();

        int eliminated = 0;

        switch (action) {

            case RL_ACTION_CONST_FOLD:
                /* ONE iteration of each sub-pass — not a convergence loop.
                 * Keeping this atomic lets the agent learn that CSE or CopyProp
                 * may be more valuable BEFORE folding on some program types.
                 * The loop from run_optimizer_adaptive() is intentionally NOT
                 * replicated here: each RL step must be a single, measurable
                 * unit of work so rewards are comparable across actions. */
                eliminated  = constant_propagation(ir_head);
                eliminated += constant_folding(ir_head);
                eliminated += simplify_constant_branches(ir_head);
                result.opt.constants_folded += eliminated;
                result.opt.passes_run |= OPT_PASS_CONST_FOLD;
                break;

            case RL_ACTION_COPY_PROP:
                eliminated = copy_propagation(ir_head);
                result.opt.copies_propagated += eliminated;
                result.opt.passes_run |= OPT_PASS_COPY_PROP;
                break;

            case RL_ACTION_CSE:
                eliminated = cse(cfg);
                result.opt.cse_eliminated += eliminated;
                result.opt.passes_run |= OPT_PASS_CSE;
                break;

            case RL_ACTION_DCE:
                eliminated = dead_code_elimination(cfg);
                result.opt.dead_instrs_removed += eliminated;
                result.opt.passes_run |= OPT_PASS_DCE;
                break;

            case RL_ACTION_UNREACHABLE:
                eliminated = unreachable_block_elimination(cfg);
                result.opt.unreachable_blocks += eliminated;
                result.opt.passes_run |= OPT_PASS_UNREACHABLE;
                break;

            default:
                break;
        }

        clock_t t1 = clock();
        double  ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        if (ms < 0.001) ms = 0.001;  /* floor to avoid division by zero */

        /* Green reward: benefit / cost */
        float reward = (float)eliminated / (float)ms;

        printf("  Step %d: %-16s → eliminated=%d  time=%.3fms  reward=%.4f\n",
               step, action_name(action), eliminated, ms, reward);

        result.actions_taken++;
        result.total_reward += reward;

        /* Advance state: mark this pass as done */
        RLState s_next  = state;
        s_next.passes_done |= action_to_pass_bit(action);

        /* Q-update — skipped in eval_only mode */
        if (!agent->eval_only)
            rl_agent_update(agent, state, action, reward, s_next);

        state = s_next;

        /* If all non-STOP passes are done, terminate */
        if ((state.passes_done & (OPT_PASS_CONST_FOLD |
                                   OPT_PASS_COPY_PROP  |
                                   OPT_PASS_CSE        |
                                   OPT_PASS_DCE        |
                                   OPT_PASS_UNREACHABLE)) ==
                (OPT_PASS_CONST_FOLD | OPT_PASS_COPY_PROP |
                 OPT_PASS_CSE        | OPT_PASS_DCE       |
                 OPT_PASS_UNREACHABLE)) {
            printf("  All passes exhausted — terminating episode.\n");
            break;
        }
    }

    free_cfg(cfg);

    if (!agent->eval_only)
        agent->total_episodes++;

    printf("Episode summary: steps=%d  total_reward=%.4f\n",
           result.actions_taken, result.total_reward);
    printf("================================================\n");

    return result;
}

/* ============================================================
 * SECTION 7 — REPORTING / DEBUGGING
 * ============================================================ */

void rl_print_state(RLState s) {
    static const char *loop_names[]  = {"no loops", "shallow", "deep/nested"};
    static const char *arith_names[] = {"none", "few(<=4)", "many(>4)"};
    static const char *adt_names[]   = {"no ADT", "light ADT", "ADT-dominated"};
    static const char *const_names[] = {"no consts", "has consts"};
    static const char *size_names[]  = {"tiny(<10)", "medium", "large(>50)"};

    printf("RLState { loop=%s arith=%s adt=%s const=%s size=%s passes=0x%02x }\n",
           loop_names[s.loop_bucket],
           arith_names[s.arith_bucket],
           adt_names[s.adt_bucket],
           const_names[s.const_bucket],
           size_names[s.size_bucket],
           s.passes_done);
}

/*
 * Print the learned first-action preference for every state that has
 * been visited (max Q-value > 0).  Iterates all 5 bucketed dimensions
 * with passes_done=0 and skips rows where all Q-values are still zero
 * (unvisited states).
 *
 * Previously this function fixed const_bucket=1 and size_bucket=1,
 * which caused it to show only the single state matching
 * 02_constant_folding.simpl while hiding the 9 other visited states.
 * The bug is fixed by iterating all dimension combinations.
 *
 * Expected output after convergence:
 *   ADT-dominated states  → ConstFold or STOP (not CSE — ADT ops
 *                           are not algebraically reducible)
 *   Arith-heavy states    → CSE or DCE first
 *   Constant-heavy states → ConstFold first
 */
void rl_print_qtable_summary(const RLAgent *agent) {
    static const char *action_labels[RL_NUM_ACTIONS] = {
        "ConstFold", "CopyProp", "CSE", "DCE", "Unreachable", "STOP"
    };
    static const char *loop_lbl[]  = {"none",      "shallow",   "deep"     };
    static const char *arith_lbl[] = {"none",      "few(<=4)",  "many(>4)" };
    static const char *adt_lbl[]   = {"none",      "light",     "dominated"};
    static const char *const_lbl[] = {"no-const",  "has-const"             };
    static const char *size_lbl[]  = {"tiny(<10)", "medium",    "large(>50)"};

    printf("\n=== Q-Table: learned first-action (passes_done=0, visited states only) ===\n");
    printf("%-8s %-10s %-11s %-9s %-11s  %-13s  %s\n",
           "loop", "arith", "adt", "const", "size", "best_action", "Q-value");
    printf("%-8s %-10s %-11s %-9s %-11s  %-13s  %s\n",
           "------", "--------", "---------", "-------", "---------",
           "-------------", "-------");

    int printed = 0;

    for (int l  = 0; l  < RL_LOOP_BUCKETS;  l++ )
    for (int ar = 0; ar < RL_ARITH_BUCKETS; ar++)
    for (int ad = 0; ad < RL_ADT_BUCKETS;   ad++)
    for (int cn = 0; cn < RL_CONST_BUCKETS; cn++)
    for (int sz = 0; sz < RL_SIZE_BUCKETS;  sz++) {
        RLState s = {
            .loop_bucket  = l,
            .arith_bucket = ar,
            .adt_bucket   = ad,
            .const_bucket = cn,
            .size_bucket  = sz,
            .passes_done  = 0
        };
        int   si   = rl_state_index(s);
        int   best = 0;
        float bq   = agent->q[si][0];
        for (int a = 1; a < RL_NUM_ACTIONS; a++) {
            if (agent->q[si][a] > bq) { bq = agent->q[si][a]; best = a; }
        }
        /* Skip unvisited states — all Q-values remain 0 */
        if (bq < 0.001f) continue;

        printf("%-8s %-10s %-11s %-9s %-11s  %-13s  %.2f\n",
               loop_lbl[l], arith_lbl[ar], adt_lbl[ad],
               const_lbl[cn], size_lbl[sz],
               action_labels[best], bq);
        printed++;
    }

    if (printed == 0)
        printf("  (no states visited yet — run ./train.sh first)\n");

    printf("=== Total visited initial states: %d / %d ===\n",
           printed, RL_LOOP_BUCKETS * RL_ARITH_BUCKETS * RL_ADT_BUCKETS
                    * RL_CONST_BUCKETS * RL_SIZE_BUCKETS);
}
