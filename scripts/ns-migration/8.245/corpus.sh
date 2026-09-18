#!/bin/bash
# Collect the unit-1 id-move shadow lines (SCOPEQUAL-MISS, STAMP-ARENA, STAMP-MISS, FNRET-DIFF, COHKEY-DIFF, PATLEAF-DIFF) over six halves.
cd /c/Programming/apps/CryoLang || exit 2
CRYO=/c/Programming/apps/CryoLang/compiler/build/cryo.exe
OUT=/c/Programming/apps/CryoLang/.objcmp/w1-lines.txt
: > "$OUT"
for d in examples/*/; do
  [ -f "$d/cryoconfig" ] || continue
  ( cd "$d" && rm -rf build && CRYO_STDLIB=/c/Programming/apps/CryoLang/stdlib "$CRYO" build . 2>&1 | grep '^SHADOW' | sed "s|^|examples\t|" >> "$OUT" )
done
echo "examples done"
( cd tests && rm -rf build && "$CRYO" test async 2>&1 | grep -E '^SHADOW' | sed 's|^|tests\t|' >> "$OUT" )
echo "tests done"
make lsp-check 2>&1 | grep -E '^SHADOW' | sed 's|^|lsp\t|' >> "$OUT"
echo "lsp done"
make cross-check 2>&1 | grep -E '^SHADOW' | sed 's|^|cross\t|' >> "$OUT"
echo "cross done"
echo "W1_DONE"
