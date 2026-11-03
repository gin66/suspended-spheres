#!/bin/sh
# Build the browser WASM module (single file, ES module factory).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/webapp/src/wasm"
mkdir -p "$OUT"

if ! command -v em++ >/dev/null 2>&1; then
    echo "em++ not found; install Emscripten to rebuild the viewer core" >&2
    exit 1
fi

INC="-I$ROOT/lib/pattern_core/include -I$ROOT/lib/motion_core/include"
SRC="$ROOT/wasm/viewer_api.cpp \
$ROOT/lib/pattern_core/src/bake.cpp \
$ROOT/lib/pattern_core/src/json.cpp \
$ROOT/lib/pattern_core/src/limits.cpp \
$ROOT/lib/motion_core/src/motion_core.cpp"

# shellcheck disable=SC2086
em++ -std=c++17 -O2 -fexceptions $INC $SRC \
    --no-entry \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 \
    -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web \
    -sEXPORTED_FUNCTIONS='["_pc_load_json","_pc_free","_pc_last_error","_pc_grid_cols","_pc_grid_rows","_pc_dt_ms","_pc_n","_pc_duration_ms","_pc_tier","_pc_layer_count","_pc_layer_op","_pc_layer_axis","_pc_eval_um","_pc_report_json","_pc_bus_load_percent","_mc_um_to_code","_mc_code_to_um","_mc_um_to_steps","_mc_steps_to_um"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -o "$OUT/pattern_core.js"

echo "built $OUT/pattern_core.js"
