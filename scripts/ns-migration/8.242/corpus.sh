#!/bin/bash
# Build every example and the unit suite under the shadow compiler; collect
# the SHADOW lines.
cd /c/Programming/apps/CryoLang || exit 2
CRYO=/c/Programming/apps/CryoLang/compiler/build/cryo.exe
OUT=/c/Programming/apps/CryoLang/.objcmp/s1-lines.txt
: > "$OUT"
n=0
for d in examples/*/; do
  [ -f "$d/cryoconfig" ] || continue
  n=$((n+1))
  ( cd "$d" && rm -rf build && CRYO_STDLIB=/c/Programming/apps/CryoLang/stdlib "$CRYO" build . 2>&1 | grep '^SHADOW' | sed "s|^|$d\t|" >> "$OUT" )
done
echo "examples built: $n"
( cd tests && rm -rf build && "$CRYO" test async 2>&1 | grep -E '^SHADOW|OVERALL' | sed 's|^|tests\t|' >> "$OUT" )
echo "SHADOW_DONE"
