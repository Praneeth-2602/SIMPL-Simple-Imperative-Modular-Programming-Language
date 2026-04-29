#!/usr/bin/env bash
# ============================================================
# SIMPL RL Training Harness
# ============================================================
# Runs N training episodes across training programs and records
# per-episode metrics to a CSV file.
#
# Usage:
#   ./train.sh [episodes] [qtable_path] [results_dir]
#
# Defaults:
#   episodes    = 100
#   qtable_path = rl_qtable.bin
#   results_dir = results/
#
# Optional environment variables (for scale and portability):
#   TRAIN_DIR           Directory of .simpl programs used for training
#   TRAIN_SAMPLE_SIZE   Programs per episode (0 = use all shard files)
#   TRAIN_SHARD_INDEX   Shard number, 0-based (default 0)
#   TRAIN_SHARD_COUNT   Number of shards (default 1)
#
# Output files:
#   results/train_log.csv       — one row per (episode, program)
#   results/train_summary.csv   — per-checkpoint averages
# ============================================================

set -euo pipefail

EPISODES="${1:-100}"
QTABLE="${2:-rl_qtable.bin}"
RESULTS_DIR="${3:-results}"
SIMPL=""
TRAIN_DIR="${TRAIN_DIR:-tests/train_files}"
TRAIN_SAMPLE_SIZE="${TRAIN_SAMPLE_SIZE:-0}"
TRAIN_SHARD_INDEX="${TRAIN_SHARD_INDEX:-0}"
TRAIN_SHARD_COUNT="${TRAIN_SHARD_COUNT:-1}"

# Checkpoint episodes for summary
CHECKPOINTS=(0 50 100 200 350 500 1000)
CKPT_REWARD=(0 0 0 0 0 0 0)
CKPT_ELIM=(0 0 0 0 0 0 0)
CKPT_COUNT=(0 0 0 0 0 0 0)

add_float() {
    awk -v a="$1" -v b="$2" 'BEGIN{printf "%.4f", a + b}'
}

resolve_simpl() {
    local candidate
    for candidate in "./simpl" "./simpl.exe"; do
        if [ -f "$candidate" ]; then
            SIMPL="$candidate"
            return 0
        fi
    done
    return 1
}

parse_eval_csv() {
    local csv_line="$1"
    IFS=',' read -r csv_tag file_name base fixed_elim fixed_ms adapt_elim adapt_ms rl_elim rl_ms rl_reward <<< "$csv_line"
    if [ "${csv_tag:-}" != "EVAL_CSV" ] || [ -z "${file_name:-}" ] || [ -z "${rl_reward:-}" ]; then
        return 1
    fi
    return 0
}

parse_train_csv() {
    local csv_line="$1"
    IFS=',' read -r csv_tag episode_n file_name actions reward epsilon elim <<< "$csv_line"
    if [ "${csv_tag:-}" != "TRAIN_CSV" ] || [ -z "${episode_n:-}" ] || [ -z "${elim:-}" ]; then
        return 1
    fi
    return 0
}

is_checkpoint_episode() {
    local ep="$1"
    local i
    for i in "${!CHECKPOINTS[@]}"; do
        if [ "$ep" -eq "${CHECKPOINTS[$i]}" ]; then
            echo "$i"
            return
        fi
    done
    echo "-1"
}

collect_all_files() {
    local files=()
    while IFS= read -r f; do
        files+=("$f")
    done < <(find "$TRAIN_DIR" -maxdepth 1 -type f -name "*.simpl" | sort)
    printf '%s\n' "${files[@]}"
}

select_shard_files() {
    local all=("$@")
    local out=()
    local i
    for i in "${!all[@]}"; do
        if [ $((i % TRAIN_SHARD_COUNT)) -eq "$TRAIN_SHARD_INDEX" ]; then
            out+=("${all[$i]}")
        fi
    done
    printf '%s\n' "${out[@]}"
}

select_episode_files() {
    local ep="$1"
    shift
    local shard=("$@")
    local n="${#shard[@]}"
    local out=()

    if [ "$TRAIN_SAMPLE_SIZE" -le 0 ] || [ "$TRAIN_SAMPLE_SIZE" -ge "$n" ]; then
        printf '%s\n' "${shard[@]}"
        return
    fi

    # Deterministic rotating window over shard files. This avoids full-corpus
    # passes per episode while ensuring every file is revisited over time.
    local start=$(( ((ep - 1) * TRAIN_SAMPLE_SIZE) % n ))
    local k idx
    for k in $(seq 0 $((TRAIN_SAMPLE_SIZE - 1))); do
        idx=$(( (start + k) % n ))
        out+=("${shard[$idx]}")
    done
    printf '%s\n' "${out[@]}"
}

# ── Sanity checks ─────────────────────────────────────────
if ! resolve_simpl; then
    echo "ERROR: Could not find ./simpl or ./simpl.exe. Run: make (or the gcc build command)" >&2
    exit 1
fi
if [ ! -d "$TRAIN_DIR" ]; then
    echo "ERROR: TRAIN_DIR '$TRAIN_DIR' not found. Run from the SIMPL project root or set TRAIN_DIR." >&2
    exit 1
fi
if [ "$TRAIN_SHARD_COUNT" -lt 1 ]; then
    echo "ERROR: TRAIN_SHARD_COUNT must be >= 1" >&2
    exit 1
fi
if [ "$TRAIN_SHARD_INDEX" -lt 0 ] || [ "$TRAIN_SHARD_INDEX" -ge "$TRAIN_SHARD_COUNT" ]; then
    echo "ERROR: TRAIN_SHARD_INDEX must be in [0, TRAIN_SHARD_COUNT-1]" >&2
    exit 1
fi

mkdir -p "$RESULTS_DIR"

LOG="$RESULTS_DIR/train_log.csv"
SUMMARY="$RESULTS_DIR/train_summary.csv"

echo "episode,file,actions_taken,total_reward,epsilon,instrs_eliminated" > "$LOG"

# Build ordered corpus, then select this process shard.
ALL_FILES=()
while IFS= read -r f; do
    [ -n "$f" ] && ALL_FILES+=("$f")
done < <(collect_all_files)

if [ "${#ALL_FILES[@]}" -eq 0 ]; then
    echo "ERROR: No .simpl files found in TRAIN_DIR '$TRAIN_DIR'" >&2
    exit 1
fi

SHARD_FILES=()
while IFS= read -r f; do
    [ -n "$f" ] && SHARD_FILES+=("$f")
done < <(select_shard_files "${ALL_FILES[@]}")

if [ "${#SHARD_FILES[@]}" -eq 0 ]; then
    echo "ERROR: Shard selection is empty. Adjust TRAIN_SHARD_INDEX/COUNT." >&2
    exit 1
fi

echo ">>> Training dir  : $TRAIN_DIR"
echo ">>> Compiler      : $SIMPL"
echo ">>> Corpus summary: total=${#ALL_FILES[@]} shard=${#SHARD_FILES[@]} sample_per_episode=${TRAIN_SAMPLE_SIZE}"
echo ">>> Shard config : index=${TRAIN_SHARD_INDEX} count=${TRAIN_SHARD_COUNT}"

# ── Collect baseline (episode 0) before any training ───────
echo ">>> Collecting baseline metrics (episode 0)..."
for f in "${SHARD_FILES[@]}"; do
    OUT=$("$SIMPL" "$f" --eval --qtable "$QTABLE" --quiet 2>/dev/null | grep "^EVAL_CSV" || true)
    if [ -z "$OUT" ]; then
        continue
    fi
    if parse_eval_csv "$OUT"; then
        echo "0,${f},0,${rl_reward},1.000,${rl_elim}" >> "$LOG"
        CKPT_ELIM[0]=$((CKPT_ELIM[0] + rl_elim))
        CKPT_REWARD[0]="$(add_float "${CKPT_REWARD[0]}" "$rl_reward")"
        CKPT_COUNT[0]=$((CKPT_COUNT[0] + 1))
    fi
done

# ── Main training loop ─────────────────────────────────────
echo ">>> Training for $EPISODES episodes..."
for ep in $(seq 1 "$EPISODES"); do
    ep_start_ts=$(date +%s)

    EP_FILES=()
    while IFS= read -r f; do
        [ -n "$f" ] && EP_FILES+=("$f")
    done < <(select_episode_files "$ep" "${SHARD_FILES[@]}")

    ck_idx="$(is_checkpoint_episode "$ep")"

    for f in "${EP_FILES[@]}"; do
        OUT=$("$SIMPL" "$f" --train --qtable "$QTABLE" --quiet 2>/dev/null \
              | grep "^TRAIN_CSV" || true)
        if [ -z "$OUT" ]; then
            continue
        fi
        if parse_train_csv "$OUT"; then
            echo "${episode_n},${f},${actions},${reward},${epsilon},${elim}" >> "$LOG"

            if [ "$ck_idx" -ge 0 ]; then
                CKPT_REWARD[$ck_idx]="$(add_float "${CKPT_REWARD[$ck_idx]}" "$reward")"
                CKPT_ELIM[$ck_idx]=$((CKPT_ELIM[$ck_idx] + elim))
                CKPT_COUNT[$ck_idx]=$((CKPT_COUNT[$ck_idx] + 1))
            fi
        fi
    done

    ep_end_ts=$(date +%s)
    ep_elapsed_sec=$((ep_end_ts - ep_start_ts))
    echo "    Episode $ep / $EPISODES complete | files=${#EP_FILES[@]} | time=${ep_elapsed_sec}s"
done

# ── Write summary CSV ──────────────────────────────────────
echo "checkpoint_episode,avg_reward,avg_instrs_eliminated,programs_measured" \
    > "$SUMMARY"

for i in "${!CHECKPOINTS[@]}"; do
    ck="${CHECKPOINTS[$i]}"
    cnt="${CKPT_COUNT[$i]}"
    if [ "$cnt" -gt 0 ]; then
        avg_rew=$(awk  "BEGIN{printf \"%.2f\", ${CKPT_REWARD[$i]} / $cnt}")
        avg_elim=$(awk "BEGIN{printf \"%.2f\", ${CKPT_ELIM[$i]} / $cnt}")
    else
        avg_rew="N/A"
        avg_elim="N/A"
    fi
    echo "${ck},${avg_rew},${avg_elim},${cnt}" >> "$SUMMARY"
done

echo ""
echo "=== Training complete ==="
echo "Q-table saved to       : $QTABLE"
echo "Full training log      : $LOG"
echo "Checkpoint summary     : $SUMMARY"
echo ""
echo "Learning curve (avg instrs eliminated per episode checkpoint):"
echo "----------------------------------------------------------"
cat "$SUMMARY"
echo "----------------------------------------------------------"
echo ""
echo "Next: run ./eval.sh to compare RL vs fixed vs adaptive"
