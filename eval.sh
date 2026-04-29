#!/usr/bin/env bash
# ============================================================
# SIMPL RL Evaluation Harness
# ============================================================
# Runs all three optimizer strategies across the benchmark
# programs and generates a comparison report.
#
# Strategies compared:
#   1. Fixed-order   — all passes in a fixed sequence
#   2. Adaptive      — existing rule-based heuristic
#   3. RL agent      — learned Q-policy (exploitation mode)
#
# Usage:
#   ./eval.sh [qtable_path] [results_dir]
#
# Defaults:
#   qtable_path = rl_qtable.bin
#   results_dir = results/
#
# Optional environment variable:
#   EVAL_DIR            Directory of .simpl programs to evaluate
#
# Output files:
#   results/eval_raw.csv      — raw per-program metrics
#   results/eval_report.txt   — human-readable comparison table
# ============================================================

set -euo pipefail

QTABLE="${1:-rl_qtable.bin}"
RESULTS_DIR="${2:-results}"
EVAL_DIR="${EVAL_DIR:-eval_files}"
SIMPL=""

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

add_float() {
    awk -v a="$1" -v b="$2" 'BEGIN{printf "%.4f", a + b}'
}

parse_eval_csv() {
    local csv_line="$1"
    IFS=',' read -r csv_tag file_name base fixed_elim fixed_ms adapt_elim adapt_ms rl_elim rl_ms rl_reward <<< "$csv_line"
    if [ "${csv_tag:-}" != "EVAL_CSV" ] || [ -z "${file_name:-}" ] || [ -z "${rl_reward:-}" ]; then
        return 1
    fi
    return 0
}

if ! resolve_simpl; then
    echo "ERROR: Could not find ./simpl or ./simpl.exe." >&2; exit 1
fi
if [ ! -d "$EVAL_DIR" ]; then
    echo "ERROR: EVAL_DIR '$EVAL_DIR' not found. Run from the SIMPL project root or set EVAL_DIR." >&2; exit 1
fi

mkdir -p "$RESULTS_DIR"
RAW="$RESULTS_DIR/eval_raw.csv"
REPORT="$RESULTS_DIR/eval_report.txt"

shopt -s nullglob
EVAL_FILES=("$EVAL_DIR"/*.simpl)
shopt -u nullglob

if [ "${#EVAL_FILES[@]}" -eq 0 ]; then
    echo "ERROR: No .simpl files found in EVAL_DIR '$EVAL_DIR'." >&2; exit 1
fi

# ── CSV header ─────────────────────────────────────────────
echo "file,base_instrs,fixed_elim,fixed_ms,adapt_elim,adapt_ms,rl_elim,rl_ms,rl_reward" \
    > "$RAW"

# Accumulators for totals
total_base=0
total_fixed_elim=0;  total_fixed_ms="0"
total_adapt_elim=0;  total_adapt_ms="0"
total_rl_elim=0;     total_rl_ms="0"; total_rl_reward="0"
n_programs=0
skipped_programs=0

echo ">>> Evaluating all programs..."
echo ">>> Compiler       : $SIMPL"
echo ">>> Eval corpus    : ${#EVAL_FILES[@]} programs from $EVAL_DIR"
for f in "${EVAL_FILES[@]}"; do
    OUT=$("$SIMPL" "$f" --eval --qtable "$QTABLE" --quiet 2>/dev/null \
          | grep "^EVAL_CSV" || true)
    if [ -z "$OUT" ]; then
        echo "  SKIP: $f (parse/semantic error)"
        skipped_programs=$((skipped_programs + 1))
        continue
    fi

    if ! parse_eval_csv "$OUT"; then
        echo "  SKIP: $f (malformed EVAL_CSV row: $OUT)"
        skipped_programs=$((skipped_programs + 1))
        continue
    fi

    echo "${file_name},${base},${fixed_elim},${fixed_ms},${adapt_elim},${adapt_ms},${rl_elim},${rl_ms},${rl_reward}" \
        >> "$RAW"

    # Accumulate
    total_base=$((total_base + base))
    total_fixed_elim=$((total_fixed_elim + fixed_elim))
    total_fixed_ms=$(add_float "$total_fixed_ms" "$fixed_ms")
    total_adapt_elim=$((total_adapt_elim + adapt_elim))
    total_adapt_ms=$(add_float "$total_adapt_ms" "$adapt_ms")
    total_rl_elim=$((total_rl_elim + rl_elim))
    total_rl_ms=$(add_float "$total_rl_ms" "$rl_ms")
    total_rl_reward=$(add_float "$total_rl_reward" "$rl_reward")
    n_programs=$((n_programs + 1))

    printf "  %-40s base=%3d  fixed=%2d  adapt=%2d  rl=%2d\n" \
        "$(basename "$file_name")" "$base" "$fixed_elim" "$adapt_elim" "$rl_elim"
done

# ── Build human-readable report ────────────────────────────
{
echo "============================================================"
echo " SIMPL RL Compiler — Evaluation Report"
echo "============================================================"
echo " Q-table: $QTABLE"

if [ -f "$QTABLE" ]; then
    episodes=$("$SIMPL" --dump-qtable --qtable "$QTABLE" 2>/dev/null \
               | grep "episodes trained" | awk '{print $NF}' || echo "?")
    echo " RL episodes trained: $episodes"
else
    echo " RL episodes trained: 0 (Q-table not found at $QTABLE)"
fi

echo " Programs evaluated : $n_programs"
echo " Programs skipped   : $skipped_programs"
echo ""
echo "Per-program results:"
echo "------------------------------------------------------------"
printf "%-32s %5s  %8s  %8s  %8s\n" "Program" "Base" "Fixed" "Adaptive" "RL"
printf "%-32s %5s  %8s  %8s  %8s\n" \
    "--------------------------------" "-----" \
    "--------" "--------" "--------"

# Re-read raw CSV to build the table body
tail -n +2 "$RAW" | while IFS=',' read -r fname base fe fm ae am re rm rw; do
    short=$(basename "$fname" .simpl)
    printf "%-32s %5d  %8d  %8d  %8d\n" \
        "$short" "$base" \
        "$fe" \
        "$ae" \
        "$re"
done

echo "------------------------------------------------------------"
if [ "$n_programs" -gt 0 ]; then
    avg_base=$(awk  "BEGIN{printf \"%.1f\", $total_base/$n_programs}")
    avg_fe=$(awk    "BEGIN{printf \"%.1f\", $total_fixed_elim/$n_programs}")
    avg_ae=$(awk    "BEGIN{printf \"%.1f\", $total_adapt_elim/$n_programs}")
    avg_re=$(awk    "BEGIN{printf \"%.1f\", $total_rl_elim/$n_programs}")
    avg_fm=$(awk    "BEGIN{printf \"%.4f\", $total_fixed_ms/$n_programs}")
    avg_am=$(awk    "BEGIN{printf \"%.4f\", $total_adapt_ms/$n_programs}")
    avg_rm=$(awk    "BEGIN{printf \"%.4f\", $total_rl_ms/$n_programs}")
    avg_rw=$(awk    "BEGIN{printf \"%.4f\", $total_rl_reward/$n_programs}")

    printf "%-32s %5s  %8s  %8s  %8s\n" "AVERAGE" \
        "$avg_base" "$avg_fe" "$avg_ae" "$avg_re"
    echo ""
    echo "Average optimizer CPU time per program (ms):"
    printf "  Fixed    : %.4f ms\n" "$avg_fm"
    printf "  Adaptive : %.4f ms\n" "$avg_am"
    printf "  RL       : %.4f ms\n" "$avg_rm"
    echo ""
    echo "Green reward (avg RL reward per program):"
    printf "  RL avg reward : %.4f\n" "$avg_rw"
    echo ""

    # Winner summary
    echo "Strategy comparison summary:"
    rl_vs_fixed=$(awk  "BEGIN{d=$total_rl_elim-$total_fixed_elim; printf \"%+d\", d}")
    rl_vs_adapt=$(awk  "BEGIN{d=$total_rl_elim-$total_adapt_elim; printf \"%+d\", d}")
    rl_e_vs_fix=$(awk  "BEGIN{d=$total_fixed_ms-$total_rl_ms; printf \"%+.4f\", d}")
    rl_e_vs_ada=$(awk  "BEGIN{d=$total_adapt_ms-$total_rl_ms; printf \"%+.4f\", d}")
    printf "  RL vs Fixed     : %s instrs eliminated total\n" "$rl_vs_fixed"
    printf "  RL vs Adaptive  : %s instrs eliminated total\n" "$rl_vs_adapt"
    printf "  RL CPU-time delta vs fixed    : %s ms total\n"   "$rl_e_vs_fix"
    printf "  RL CPU-time delta vs adaptive : %s ms total\n"   "$rl_e_vs_ada"
    echo ""
    echo "Notes:"
    echo "  - Base is the unoptimized IR instruction count."
    echo "  - Fixed/Adaptive/RL columns are raw optimizer elimination counts reported by the compiler."
    echo "  - Those counts are accumulated across passes, so they can exceed the base IR count and should not be read as percentages."
    echo "  - Runtime comes from the compiler's clock()-based CPU-time measurement, not wall-clock elapsed time."
else
    echo "No valid programs were evaluated."
fi

echo ""
echo "============================================================"
echo " Full raw data: $RAW"
echo "============================================================"
} | tee "$REPORT"

echo ""
echo "Report written to: $REPORT"
