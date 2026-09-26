#!/usr/bin/env bash
# corpus.sh <compiler> <tag>: with a compiler built from probe.patch, run the
# test suite and build every example, keeping full output in
# .objcmp/8.356/<tag>/.  Leak sites:
#   cat .objcmp/8.356/<tag>/*.out | grep -a '^FOREIGN' | awk '{print $2, $3}' | sort -u
set -u
R="$(git rev-parse --show-toplevel)"; C="$1"; T="$R/.objcmp/8.356/$2"; mkdir -p "$T"
export CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc
(cd "$R/tests" && "$C" test > "$T/test.out" 2>&1; echo "test exit=$?" >> "$T/test.out")
for d in "$R"/examples/*/; do
  n=$(basename "$d"); [ -f "$d/cryoconfig" ] || continue
  (cd "$d" && "$C" build . > "$T/ex-$n.out" 2>&1; echo "exit=$?" >> "$T/ex-$n.out")
done
echo CORPUS_DONE >> "$T/done"
