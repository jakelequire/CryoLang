#!/usr/bin/env bash
# 46 projects one by one + 14 examples under compiler/build/cryo.exe; SHADOW lines to $S/$1-projex-lines.txt
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; mkdir -p "$S"; T="$1"; cd "$R" || exit 1
: > "$S/$T-projex-lines.txt"; : > "$S/$T-projex-fails.txt"
for d in tests/tests/projects/*/; do [ -f "$d/test.json" ] || continue
  (cd "$d" && rm -rf build && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build . > "$S/projex-one.log" 2>&1; echo "$? $d" >> "$S/$T-projex-fails.txt"; grep -a '^SHADOW' "$S/projex-one.log" | sed "s#^#$d\t#" >> "$S/$T-projex-lines.txt"; grep -a 'error\[' "$S/projex-one.log" | sed "s#^#$d\t#" >> "$S/$T-projex-fails.txt"; rm -rf build); done
for d in examples/*/; do (cd "$d" && rm -rf build && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build . > "$S/projex-one.log" 2>&1; echo "$? $d" >> "$S/$T-projex-fails.txt"; grep -a '^SHADOW' "$S/projex-one.log" | sed "s#^#$d\t#" >> "$S/$T-projex-lines.txt"; grep -a 'error\[' "$S/projex-one.log" | sed "s#^#$d\t#" >> "$S/$T-projex-fails.txt"); done
echo "SHADOW lines (46 projects + 14 examples): $(wc -l < "$S/$T-projex-lines.txt")"
echo "non-zero exits: $(grep -c -v '^0 ' "$S/$T-projex-fails.txt")"
echo PROJEX_DONE
