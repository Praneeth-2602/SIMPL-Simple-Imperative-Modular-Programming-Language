# SIMPL RL Compiler — Implementation Plan & Status

---

## Phase 1 — MDP Foundation & Q-Table Skeleton ✅ Complete

### What was built

| File | Description |
|---|---|
| `src/rl_agent/rl_agent.h` | MDP types, Q-table struct, full public API |
| `src/rl_agent/rl_agent.c` | State encoder, ε-greedy policy, Bellman update, episode driver |
| `src/optimizer/optimizer.h` | `constant_propagation` and `simplify_constant_branches` promoted to public |

### MDP formulation

**State space** — 6 bucketed dimensions from `SemanticReport`:

| Dimension | Buckets | Encoding |
|---|---|---|
| `loop_bucket` | 3 | 0=none, 1=shallow, 2=deep/nested |
| `arith_bucket` | 3 | 0=none, 1=few (≤4 ops), 2=many (>4 ops) |
| `adt_bucket` | 3 | 0=none, 1=light, 2=ADT-dominated (adt_ops > arith_ops) |
| `const_bucket` | 2 | 0=no literal binary ops at source level, 1=has some |
| `size_bucket` | 3 | 0=tiny (<10 IR instrs), 1=medium, 2=large (>50) |
| `passes_done` | 32 | 5-bit bitmask of passes already applied this episode |

Total states: 3 × 3 × 3 × 2 × 3 × 32 = **5,184**

**Action space**: 6 discrete actions — ConstFold+Prop (0), CopyProp (1), CSE (2), DCE (3), UnreachableElim (4), STOP (5).

**Reward**: `instructions_eliminated / compile_time_ms` — green reward encoding energy efficiency.

**Q-learning**: α=0.1, γ=0.9, ε decays linearly 1.0→0.1 over 8,000 per-program episodes (= 500 outer training passes × 16 programs).

---

## Phase 2 — Training Harness & Evaluation Pipeline ✅ Complete

### What was built

| File | Description |
|---|---|
| `src/main.c` | `--train`, `--eval`, `--quiet`, `--qtable`, `--dump-qtable` CLI modes |
| `train.sh` | Multi-episode training loop with CSV logging and checkpoint summaries |
| `eval.sh` | Three-way comparison (fixed / adaptive / RL) across all 15 programs |

### CLI modes

```bash
# Normal compilation (loads Q-table, runs RL episode, saves Q-table)
./simpl program.simpl [output.ll] [--qtable rl_qtable.bin]

# Training (one episode per program invocation, updates Q-table)
./simpl program.simpl --train --qtable rl_qtable.bin --quiet

# Evaluation (read-only: runs all 3 strategies, prints EVAL_CSV row)
./simpl program.simpl --eval --qtable rl_qtable.bin --quiet

# Inspect learned policy
./simpl --dump-qtable --qtable rl_qtable.bin
```

---

## Phase 3 — Bug Fixes & Calibration ✅ Complete

Five bugs were identified from the 17,600-episode training run and corrected.
**Delete `rl_qtable.bin` and retrain from scratch** after applying these files —
the old table is poisoned by Bug A and must not be continued from.

### Fix A — ConstFold convergence loop removed (critical)

**Root cause**: The `RL_ACTION_CONST_FOLD` case ran a `do { } while (changed > 0)`
loop, accumulating all folding reward in a single RL step. On a constant-heavy
program this produced rewards of 17,000–29,000 per step — 10–50× higher than any
other pass. The Bellman update then pushed Q(s, ConstFold) to dominate every
state, regardless of program type. After ConstFold ran to convergence, all other
passes found nothing left to do, so their Q-values stayed near zero. The agent
locked into ConstFold-first for every state after ~30 outer episodes and never
escaped.

**Symptom**: Learning curve completely flat across all 1,000+ outer episodes. Rewards
identical at episode 1 and episode 1000.

**Fix**: Each RL action now calls its sub-passes exactly once:

```c
/* Before (broken): convergence loop ate all available reward in one step */
int changed;
do {
    changed  = constant_propagation(ir_head);
    changed += constant_folding(ir_head);
    eliminated += changed;
} while (changed > 0);

/* After (fixed): one atomic pass — other actions can compete for reward */
eliminated  = constant_propagation(ir_head);
eliminated += constant_folding(ir_head);
eliminated += simplify_constant_branches(ir_head);
```

The same principle applies to CopyProp and CSE, which previously re-folded after
themselves. All secondary loops removed. Each action is now a single measurable
unit of work with a reward comparable to other actions.

### Fix B — Q-table summary now shows actual visited states

**Root cause**: `rl_print_qtable_summary()` fixed `const_bucket=1` and
`size_bucket=1` when iterating. Since 15 of 16 test programs have `const_bucket=0`,
the summary printed only the state matching `02_constant_folding.simpl` with a
non-zero Q-value, and showed 26 phantom zero rows for states never visited.

**Symptom**: `--dump-qtable` output showed `Q=0.0000` for every row except one
even after 17,600 episodes, making it appear the agent learned nothing.

**Fix**: Summary now iterates all five bucketed dimensions and skips any row where
`max_Q < 0.001` (unvisited). Output now shows only the states that were actually
trained, with their correct Q-values:

```
loop     arith      adt         const     size         best_action    Q-value
none     none       dominated   no-const  medium       ConstFold      1614.84
deep     few(<=4)   none        no-const  medium       DCE            960.20
...
Total visited initial states: 9 / 162
```

### Fix C — Checkpoint metric switched to `avg_reward`

**Root cause**: `train.sh` accumulated `instrs_elim` (field 7 of `TRAIN_CSV`) for
checkpoint summaries. Since the same programs are compiled each episode, their
per-episode instruction elimination counts are structurally nearly constant — the
metric cannot detect learning. Checkpoints at episodes 10, 50, and 100 showed
identical values.

**Fix**: Checkpoint accumulation now uses `total_reward` (field 5), which changes
as the policy learns which pass sequences are energy-efficient. Checkpoint episodes
extended from `0 10 25 50 100` to `0 50 100 200 350 500 1000` to span the full
ε-decay window.

### Fix D — Eval mode no longer touches the Q-table

**Root cause**: `rl_run_episode()` in eval mode still called `rl_agent_update()` and
incremented `total_episodes`, silently modifying Q-values and the episode counter
even though the table was not saved to disk.

**Fix**: `RLAgent` struct gains an `eval_only` flag. When set, `rl_run_episode()`
skips both the Bellman update and the episode counter increment. `run_rl_eval_mode()`
in `main.c` sets this flag before calling and restores it after, making `--eval`
strictly read-only.

### Fix E — Epsilon decay extended to 8,000 episodes

**Root cause**: `RL_EPSILON_DECAY=100` (then corrected to 500) caused ε to hit its
0.1 floor after only 100/16 ≈ 6 outer passes (then 500/16 ≈ 31). The agent
committed to a policy before exploring enough of the multi-step pass-sequence space.

**Fix**: `RL_EPSILON_DECAY=8000`. With 16 programs per outer pass, this gives ε
500 full outer sweeps to decay from 1.0 to 0.1 — matching the planned 500-episode
exploration window from the original proposal.

---

## Phase 4 — Final Evaluation ⬅ Run on your machine

With the five fixes applied and a fresh Q-table, run:

```bash
# 1. Delete the poisoned Q-table
rm -f rl_qtable.bin

# 2. Rebuild (no bison/flex needed — pre-generated files are in the zip)
gcc -Wall -Wextra -g \
  -I src/parser -I src/semantic -I src/ir \
  -I src/optimizer -I src/rl_agent -I src/codegen \
  src/parser/simpl.tab.c src/lexer/lex.yy.c src/parser/ast.c \
  src/semantic/symbol_table.c src/semantic/semantic.c src/ir/ir.c \
  src/optimizer/optimizer.c src/rl_agent/rl_agent.c \
  src/codegen/codegen.c src/main.c -o simpl.exe -lm

# 3. Train for 1000 outer episodes (takes ~10-20 min)
./train.sh 1000

# 4. Evaluate
./eval.sh

# 5. Inspect learned policy
./simpl.exe --dump-qtable
```

### What to look for in results

**Learning curve** (`results/train_summary.csv`) — `avg_reward` should increase from
the baseline (ep=0) to a plateau around ep=500. The curve will be noisy due to
ε-exploration but the trend must be upward. If it is flat, the convergence loop bug
has not been fixed.

**Q-table policy** (`--dump-qtable`) — after 1000 outer episodes, the table should
show semantically-meaningful first-action preferences:

| State | Expected best action | Reason |
|---|---|---|
| ADT-dominated (adt=2) | ConstFold or STOP | ADT ops not algebraically reducible; CSE cannot help |
| Deep loop (loop=2) | DCE | Loops generate dead temporaries; DCE eliminates them |
| Arith-heavy (arith=2) + constants | ConstFold | Constants in expressions; fold first to expose CSE |
| Tiny program (size=0) | STOP or cheap pass | Overhead of global analysis exceeds benefit |

**Three-way comparison** (`results/eval_report.txt`) — the RL agent is expected to:
- Match or beat adaptive on total instructions eliminated (Topic #8)
- Achieve a higher green reward (instrs/ms) than fixed-order on ADT-heavy programs,
  because it skips CSE and inter-block DCE which find nothing on those programs (Topic #40)
- Show lower total compile time than fixed-order on programs where it issues STOP early

---

## File inventory

```
SIMPL/
├── src/
│   ├── rl_agent/
│   │   ├── rl_agent.h       Phase 1 + Phase 3 fixes (all 5 fixes)
│   │   └── rl_agent.c       Phase 1 + Phase 3 fixes (all 5 fixes)
│   ├── main.c               Phase 2 + Phase 3 Fix D (eval_only)
│   └── optimizer/
│       └── optimizer.h      Phase 1 (constant_propagation exposed)
├── train.sh                 Phase 2 + Phase 3 Fix C (avg_reward metric)
├── eval.sh                  Phase 2 (no changes needed)
└── rl_qtable.bin            DELETE THIS — retrain from scratch
```

## Key parameter values

| Parameter | Value | Rationale |
|---|---|---|
| α (learning rate) | 0.1 | Standard Q-learning; stable convergence |
| γ (discount factor) | 0.9 | Values future pass rewards; not too myopic |
| ε start | 1.0 | Full exploration at episode 1 |
| ε floor | 0.1 | 10% exploration retained after convergence |
| ε decay | 8,000 per-program episodes | = 500 outer passes × 16 programs |
| Max steps/episode | 6 | One per pass + STOP |
| State space | 5,184 | 3×3×3×2×3×32; tabular Q-table fits in ~120KB |
| Action space | 6 | Five passes + STOP |
