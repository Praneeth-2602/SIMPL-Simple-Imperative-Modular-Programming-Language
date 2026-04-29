#ifndef RL_AGENT_H
#define RL_AGENT_H

/* ============================================================
 * SIMPL RL Agent — Phase 1: MDP Foundation & Q-Table Skeleton
 * ============================================================
 *
 * Implements a tabular Q-learning agent that learns energy-aware
 * pass-selection policies for the SIMPL compiler.
 *
 * MDP Formulation
 * ---------------
 *   State:   6-dimensional bucketed vector derived from SemanticReport
 *            (loop_bucket, arith_bucket, adt_bucket, const_bucket,
 *             size_bucket, passes_done_bitmask)
 *            Total states: 3×3×3×2×3×32 = 5,184
 *
 *   Actions: 6 discrete choices
 *            0 = Constant Folding+Propagation
 *            1 = Copy Propagation
 *            2 = CSE
 *            3 = DCE
 *            4 = Unreachable Block Elimination
 *            5 = STOP
 *
 *   Reward (Phase 2):  instructions_eliminated / compile_time_ms
 *            Penalises energy-expensive passes that yield little benefit.
 *
 * Q-Table
 * -------
 *   Dimensions: RL_NUM_STATES × RL_NUM_ACTIONS
 *   Persisted:  binary file rl_qtable.bin in the working directory.
 *               Load on startup, save after every episode.
 *
 * Training Parameters
 * -------------------
 *   Alpha (α):  0.1   — learning rate
 *   Gamma (γ):  0.9   — discount factor
 *   Epsilon (ε): 1.0 → 0.1  — decays linearly over training episodes
 *   Max steps per episode: RL_MAX_STEPS (6 — one per pass + STOP)
 *
 * Usage
 * -----
 *   RLAgent *agent = rl_agent_create();
 *   rl_agent_load(agent, "rl_qtable.bin");   // no-op if file absent
 *
 *   RLState s = rl_encode_state(&sem, ir_head);
 *   int     a = rl_agent_select_action(agent, s, episode);
 *   // ... apply pass, measure reward ...
 *   rl_agent_update(agent, s, a, reward, s_next);
 *
 *   rl_agent_save(agent, "rl_qtable.bin");
 *   rl_agent_free(agent);
 * ============================================================ */

#include "../semantic/semantic.h"
#include "../ir/ir.h"
#include "../optimizer/optimizer.h"

/* ── Dimensions ─────────────────────────────────────────── */

/* Bucket counts per state dimension */
#define RL_LOOP_BUCKETS    3    /* 0=none, 1=shallow, 2=deep/nested      */
#define RL_ARITH_BUCKETS   3    /* 0=none, 1=few(<=4), 2=many(>4)        */
#define RL_ADT_BUCKETS     3    /* 0=none, 1=light, 2=ADT-dominated      */
#define RL_CONST_BUCKETS   2    /* 0=no constant exprs, 1=has some       */
#define RL_SIZE_BUCKETS    3    /* 0=tiny(<10), 1=medium, 2=large(>50)   */
#define RL_PASSES_STATES   32   /* 2^5 — bitmask of passes already done  */

#define RL_NUM_STATES  (RL_LOOP_BUCKETS  * \
                        RL_ARITH_BUCKETS * \
                        RL_ADT_BUCKETS   * \
                        RL_CONST_BUCKETS * \
                        RL_SIZE_BUCKETS  * \
                        RL_PASSES_STATES)   /* = 5,184 */

#define RL_NUM_ACTIONS  6   /* 5 passes + STOP */

/* ── Action indices ─────────────────────────────────────── */

#define RL_ACTION_CONST_FOLD   0
#define RL_ACTION_COPY_PROP    1
#define RL_ACTION_CSE          2
#define RL_ACTION_DCE          3
#define RL_ACTION_UNREACHABLE  4
#define RL_ACTION_STOP         5

/* ── Training hyperparameters ───────────────────────────── */

#define RL_ALPHA          0.1f   /* learning rate                        */
#define RL_GAMMA          0.9f   /* discount factor                      */
#define RL_EPSILON_START  1.0f   /* initial exploration rate             */
#define RL_EPSILON_MIN    0.1f   /* minimum exploration rate             */
#define RL_EPSILON_DECAY  8000   /* episodes over which e decays to min  */
                                 /* = 500 outer passes x 16 programs,    */
                                 /* giving full exploration for 500 outer */
                                 /* sweeps before policy commits          */
#define RL_MAX_STEPS      6      /* max actions per episode (= #passes+1) */

/* ── State representation ───────────────────────────────── */

/*
 * RLState encodes the 6 bucketed dimensions plus the passes_done
 * bitmask as a flat integer index into the Q-table.
 *
 * The struct fields are kept for readability and debugging;
 * the flat index is computed by rl_state_index().
 */
typedef struct {
    int loop_bucket;     /* 0..RL_LOOP_BUCKETS-1  */
    int arith_bucket;    /* 0..RL_ARITH_BUCKETS-1 */
    int adt_bucket;      /* 0..RL_ADT_BUCKETS-1   */
    int const_bucket;    /* 0..RL_CONST_BUCKETS-1 */
    int size_bucket;     /* 0..RL_SIZE_BUCKETS-1  */
    int passes_done;     /* 5-bit bitmask (OPT_PASS_* flags)            */
} RLState;

/* ── Agent ──────────────────────────────────────────────── */

typedef struct {
    float  q[RL_NUM_STATES][RL_NUM_ACTIONS];  /* Q-table                */
    int    total_episodes;   /* episodes seen so far (drives e decay)   */
    int    eval_only;        /* if 1: select actions but skip Q-updates */
} RLAgent;

/* ── Episode result ─────────────────────────────────────── */

/*
 * Returned by rl_run_episode() — mirrors OptReport but adds
 * RL-specific fields for tracking learning behaviour.
 */
typedef struct {
    OptReport opt;           /* standard optimization counts             */

    /* RL metrics */
    int    actions_taken;    /* number of passes applied (excl. STOP)   */
    float  total_reward;     /* sum of per-step rewards                  */
    float  epsilon_used;     /* ε value at start of this episode         */
    int    pass_sequence[RL_MAX_STEPS]; /* ordered actions chosen        */
} RLEpisodeResult;

/* ============================================================
 * Public API
 * ============================================================ */

/* Lifecycle */
RLAgent *rl_agent_create(void);
void     rl_agent_free(RLAgent *agent);

/* Persistence */
int  rl_agent_load(RLAgent *agent, const char *path);  /* 0=ok, -1=err  */
int  rl_agent_save(const RLAgent *agent, const char *path);

/* State encoding */
RLState rl_encode_state(const SemanticReport *sem,
                        IRInstruction        *ir_head);
int     rl_state_index(RLState s);   /* flat index into Q-table          */

/* Policy */
float rl_current_epsilon(const RLAgent *agent);

/*
 * rl_agent_select_action — ε-greedy policy.
 *
 * Returns an action index 0..RL_NUM_ACTIONS-1.
 * Actions already in passes_done are masked out:
 *   - if the greedy best action was already done, the agent picks
 *     the next-best available action (or STOP if all passes done).
 *   - STOP is never masked — it can always be chosen.
 */
int rl_agent_select_action(const RLAgent *agent, RLState state);

/*
 * rl_agent_update — Bellman Q-update.
 *
 *   Q[s][a] ← Q[s][a] + α × ( R + γ × max_a' Q[s'][a'] − Q[s][a] )
 */
void rl_agent_update(RLAgent *agent,
                     RLState  s,
                     int      action,
                     float    reward,
                     RLState  s_next);

/* Reporting */
void rl_print_state(RLState s);
void rl_print_qtable_summary(const RLAgent *agent);

/*
 * rl_run_episode — full compilation episode using the RL agent.
 *
 * Replaces run_optimizer_adaptive(): selects passes via Q-policy,
 * applies them through the existing optimizer infrastructure,
 * computes green reward per step, and updates the Q-table.
 *
 * Call rl_agent_save() after this returns to persist learning.
 */
RLEpisodeResult rl_run_episode(RLAgent              *agent,
                               IRInstruction        *ir_head,
                               const SemanticReport *sem);

#endif /* RL_AGENT_H */
