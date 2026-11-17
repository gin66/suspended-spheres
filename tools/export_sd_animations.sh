#!/bin/sh
# Bake every patterns/*.anim.json into sd/animations/*.hbp for the panel's
# /sdcard/animations folder.  Run from the repository root.
set -eu
out="${1:-sd/animations}"
cmake --build build >/dev/null
mkdir -p "$out"
for src in patterns/*.anim.json; do
  name="$(basename "$src" .anim.json)"
  ./build/pattern_check "$src" --hbp "$out/$name.hbp" >/dev/null
  printf '%s\n' "  $out/$name.hbp"
done
