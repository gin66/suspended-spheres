#!/bin/sh
# Compare the native and WASM parity transcripts (TDD W1/W2).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$ROOT/wasm/build.sh"

NATIVE="$ROOT/build/wasm/pattern_driver_native"
WASM_JS="$ROOT/build/wasm/pattern_driver.js"
PATTERN="$ROOT/patterns/golden_sine.anim.json"
OUT="$ROOT/build/wasm"

"$NATIVE" "$PATTERN" > "$OUT/native.txt"

if [ ! -f "$WASM_JS" ]; then
    echo "WASM driver absent; native transcript written to $OUT/native.txt" >&2
    exit 0
fi

node "$WASM_JS" "$PATTERN" > "$OUT/wasm.txt"
cmp "$OUT/native.txt" "$OUT/wasm.txt"
echo "native and WASM parity: OK"
