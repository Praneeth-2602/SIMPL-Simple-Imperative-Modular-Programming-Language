#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

resolve_simpl() {
    local candidate
    for candidate in "./simpl" "./simpl.exe"; do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

SIMPL_BIN="$(resolve_simpl || true)"
if [ -z "${SIMPL_BIN:-}" ]; then
    echo "ERROR: Could not find ./simpl or ./simpl.exe. Build the compiler first." >&2
    exit 1
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
archive_root="${ARCHIVE_ROOT:-archive/training_runs}"
archive_dir="$archive_root/$timestamp"
run_root="${RUN_ROOT:-results/fresh_training_$timestamp}"
qtable_path="${QTABLE_PATH:-rl_qtable.bin}"

warmup_sweeps="${WARMUP_SWEEPS:-10}"
chunk_sweeps="${CHUNK_SWEEPS:-25}"
max_chunks="${MAX_CHUNKS:-8}"
patience="${PATIENCE:-2}"

warmup_dir="$(mktemp -d "${TMPDIR:-/tmp}/simpl-warmup.XXXXXX")"
cleanup() {
    rm -rf "$warmup_dir"
}
trap cleanup EXIT

mkdir -p "$archive_dir/preexisting_results" "$run_root/chunks" "$run_root/best"

archive_if_exists() {
    local path="$1"
    local dest_dir="$2"
    if [ -e "$path" ]; then
        mkdir -p "$dest_dir"
        mv "$path" "$dest_dir/"
    fi
}

echo ">>> Archiving existing training artifacts to $archive_dir"
archive_if_exists "$qtable_path" "$archive_dir"

if [ -d results ]; then
    shopt -s nullglob
    for f in results/*.csv results/*.txt; do
        archive_if_exists "$f" "$archive_dir/preexisting_results"
    done
    shopt -u nullglob
fi

warmup_files=(
    "tests/train_files/train_deep_none_none_no_medium_v01.simpl"
    "tests/train_files/train_deep_none_none_no_medium_v02.simpl"
    "tests/train_files/train_deep_none_none_no_medium_v03.simpl"
    "tests/train_files/train_shallow_none_none_no_tiny_v01.simpl"
    "tests/train_files/train_shallow_none_none_no_tiny_v02.simpl"
    "tests/train_files/train_shallow_none_none_no_tiny_v03.simpl"
)

echo ">>> Preparing targeted warm-up corpus"
for src in "${warmup_files[@]}"; do
    if [ ! -f "$src" ]; then
        echo "ERROR: Missing warm-up file $src" >&2
        exit 1
    fi
    cp "$src" "$warmup_dir/"
done

best_total_rl_elim=-1
best_total_rl_ms=999999999
best_label=""
no_improve_rounds=0
total_full_sweeps=0

summary_csv="$run_root/progress_summary.csv"
echo "phase,chunk_label,total_full_sweeps,total_rl_elim,total_rl_ms,total_fixed_elim,total_adapt_elim,improved" > "$summary_csv"

collect_eval_metrics() {
    local csv_path="$1"
    awk -F',' '
        NR > 1 {
            fixed += $3
            adapt += $5
            rl += $7
            rl_ms += $8
        }
        END {
            printf "%.0f,%.4f,%.0f,%.0f\n", rl, rl_ms, fixed, adapt
        }
    ' "$csv_path"
}

is_improved() {
    local candidate_rl="$1"
    local candidate_ms="$2"
    awk -v cand_rl="$candidate_rl" \
        -v cand_ms="$candidate_ms" \
        -v best_rl="$best_total_rl_elim" \
        -v best_ms="$best_total_rl_ms" '
        BEGIN {
            if (cand_rl > best_rl) {
                print 1
            } else if (cand_rl == best_rl && cand_ms + 0.0001 < best_ms) {
                print 1
            } else {
                print 0
            }
        }
    '
}

run_eval_checkpoint() {
    local phase="$1"
    local label="$2"
    local eval_dir="$run_root/chunks/$label/eval"

    mkdir -p "$eval_dir"
    echo ">>> Running eval checkpoint: $label"
    bash ./eval.sh "$qtable_path" "$eval_dir"

    local metrics
    metrics="$(collect_eval_metrics "$eval_dir/eval_raw.csv")"
    local total_rl_elim total_rl_ms total_fixed_elim total_adapt_elim
    IFS=',' read -r total_rl_elim total_rl_ms total_fixed_elim total_adapt_elim <<< "$metrics"

    local improved
    improved="$(is_improved "$total_rl_elim" "$total_rl_ms")"

    if [ "$improved" -eq 1 ]; then
        best_total_rl_elim="$total_rl_elim"
        best_total_rl_ms="$total_rl_ms"
        best_label="$label"
        no_improve_rounds=0
        cp "$qtable_path" "$run_root/best/$(basename "$qtable_path")"
        cp "$eval_dir/eval_raw.csv" "$run_root/best/eval_raw.csv"
        cp "$eval_dir/eval_report.txt" "$run_root/best/eval_report.txt"
        echo ">>> New best checkpoint: $label (rl_elim=$total_rl_elim, rl_ms=$total_rl_ms)"
    else
        no_improve_rounds=$((no_improve_rounds + 1))
        echo ">>> No improvement at $label (streak=$no_improve_rounds/$patience)"
    fi

    echo "$phase,$label,$total_full_sweeps,$total_rl_elim,$total_rl_ms,$total_fixed_elim,$total_adapt_elim,$improved" >> "$summary_csv"
}

echo ">>> Fresh training run"
echo ">>> Compiler       : $SIMPL_BIN"
echo ">>> Q-table path   : $qtable_path"
echo ">>> Run directory  : $run_root"
echo ">>> Warm-up sweeps : $warmup_sweeps"
echo ">>> Chunk sweeps   : $chunk_sweeps"
echo ">>> Max chunks     : $max_chunks"
echo ">>> Patience       : $patience"

warmup_label="$(printf 'warmup_%03d' "$warmup_sweeps")"
mkdir -p "$run_root/chunks/$warmup_label/train"
echo ">>> Warm-up training on the two previously missed states"
TRAIN_DIR="$warmup_dir" bash ./train.sh "$warmup_sweeps" "$qtable_path" "$run_root/chunks/$warmup_label/train"
run_eval_checkpoint "warmup" "$warmup_label"

for chunk_idx in $(seq 1 "$max_chunks"); do
    label="$(printf 'chunk_%03d' "$((chunk_idx * chunk_sweeps))")"
    mkdir -p "$run_root/chunks/$label/train"
    echo ">>> Full-corpus training chunk $chunk_idx / $max_chunks"
    bash ./train.sh "$chunk_sweeps" "$qtable_path" "$run_root/chunks/$label/train"
    total_full_sweeps=$((total_full_sweeps + chunk_sweeps))
    run_eval_checkpoint "full" "$label"

    if [ "$no_improve_rounds" -ge "$patience" ]; then
        echo ">>> Plateau detected after $label"
        break
    fi
done

if [ -f "$run_root/best/$(basename "$qtable_path")" ]; then
    cp "$run_root/best/$(basename "$qtable_path")" "$qtable_path"
fi

echo ""
echo "=== Fresh RL Training Run Complete ==="
echo "Archived old artifacts : $archive_dir"
echo "Run directory          : $run_root"
echo "Best checkpoint        : ${best_label:-none}"
echo "Best RL total elim     : $best_total_rl_elim"
echo "Best RL total ms       : $best_total_rl_ms"
echo "Progress summary       : $summary_csv"
if [ -f "$run_root/best/eval_report.txt" ]; then
    echo "Best eval report       : $run_root/best/eval_report.txt"
fi
