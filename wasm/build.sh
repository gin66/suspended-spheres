#!/bin/sh
# Build the native and Emscripten pattern drivers (TDD W1/W2).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/wasm"
mkdir -p "$OUT"

CXX=${CXX:-c++}
SRC="$ROOT/wasm/pattern_driver.cpp $ROOT/lib/pattern_core/src/bake.cpp \
$ROOT/lib/pattern_core/src/hbp.cpp $ROOT/lib/pattern_core/src/json.cpp \
$ROOT/lib/pattern_core/src/limits.cpp $ROOT/lib/pattern_core/src/live.cpp \
$ROOT/lib/motion_core/src/motion_core.cpp"
INC="-I$ROOT/lib/pattern_core/include -I$ROOT/lib/motion_core/include"

# shellcheck disable=SC2086
$CXX -std=c++17 -O2 $INC $SRC -o "$OUT/pattern_driver_native"

if ! command -v em++ >/dev/null 2>&1; then
    echo "em++ not found; native driver built at $OUT/pattern_driver_native" >&2
    echo "install Emscripten (emsdk or brew install emscripten) to build the WASM side" >&2
    exit 0
fi

# shellcheck disable=SC2086
em++ -std=c++17 -O2 -fexceptions $INC $SRC \
    -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 \
    -o "$OUT/pattern_driver.js"
echo "built $OUT/pattern_driver_native and $OUT/pattern_driver.js"
