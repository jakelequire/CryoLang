#!/bin/bash
# Build the unit-test project and every example with one compiler, keeping
# each project's objects under <out>/<name>.
#   usage: objbuild.sh <cryo.exe> <out-dir>
CC_EXE="$1"; OUT="$2"; R=C:/Programming/apps/CryoLang
mkdir -p "$OUT"; : > "$OUT/status.txt"
export CRYO_STDLIB=$R/stdlib CRYO_CC=gcc
for d in "$R/tests" $(ls -d "$R"/examples/*/); do
  d="${d%/}"; n=$(basename "$d")
  cd "$d" || continue
  rm -rf "$OUT/$n"
  "$CC_EXE" build --build-dir="$OUT/$n" > "$OUT/$n.log" 2>&1
  echo "BUILD rc=$? $n" >> "$OUT/status.txt"
done
echo "OBJ_DONE" >> "$OUT/status.txt"
