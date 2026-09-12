#!/usr/bin/env bash
# Build every example clean and hash its objects. $1 = output file.
set -u
R="$(git rev-parse --show-toplevel)"; cd "$R" || exit 1
out="$1"; : > "$out"; fails=0
for d in examples/*/; do
  [ -f "$d/cryoconfig" ] || continue
  rm -rf "$d/build"
  if ! (cd "$d" && CRYO_CODEGEN_THREADS=1 CRYO_STDLIB=$R/stdlib CRYO_CC=gcc ../../compiler/build/cryo.exe build --no-incremental > /dev/null 2>&1); then echo "FAIL $d"; fails=$((fails+1)); fi
  find "$d/build" -name '*.o' | sort | xargs sha256sum >> "$out"
done
echo "# objects: $(wc -l < "$out")  fails: $fails"
