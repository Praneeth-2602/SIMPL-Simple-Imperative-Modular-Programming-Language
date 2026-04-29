#!/usr/bin/env bash

set -euo pipefail

LOG_FILE="output_simpl.txt"
: > "$LOG_FILE"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "==== SIMPL Clean Run ===="

rm -f rl_qtable.bin

MAKE_CMD=""
if command -v make >/dev/null 2>&1; then
    MAKE_CMD="make"
elif command -v gmake >/dev/null 2>&1; then
    MAKE_CMD="gmake"
elif command -v mingw32-make >/dev/null 2>&1; then
    MAKE_CMD="mingw32-make"
fi

if [ -z "$MAKE_CMD" ]; then
    echo "ERROR: make not found (expected make/gmake/mingw32-make)." >&2
    exit 1
fi

echo "[1/6] make clean"
"$MAKE_CMD" clean

echo "[2/6] make"
"$MAKE_CMD"

SIMPL_BIN=""
if [ -x ./simpl ]; then
    SIMPL_BIN="./simpl"
elif [ -x ./simpl.exe ]; then
    SIMPL_BIN="./simpl.exe"
else
    echo "ERROR: build completed but neither ./simpl nor ./simpl.exe exists." >&2
    exit 1
fi

chmod +x train.sh eval.sh

echo "[3/6] bash ./train.sh 1000"
bash ./train.sh 8000

echo "[4/6] bash ./eval.sh"
EVAL_DIR="${EVAL_DIR:-eval_files}" bash ./eval.sh

echo "[5/6] dump qtable"
"$SIMPL_BIN" --dump-qtable

echo "[6/6] full output is in $LOG_FILE"
echo "==== DONE ===="