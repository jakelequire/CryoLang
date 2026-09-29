#!/bin/bash
# usage: probe_run.sh <compiler.exe> <out-file>
# Builds the compiler, tests/, every project and example, and the unit suite
# (cryo test with a filter matching nothing), collecting UNB3 lines.
CC_EXE="$1"; OUT="$2"
R=C:/Programming/apps/CryoLang
: > "$OUT"
export CRYO_STDLIB=$R/stdlib CRYO_CC=gcc
build_one() {
  local d="$1"
  cd "$d" || return
  rm -rf build
  "$CC_EXE" build > .probe.log 2>&1
  echo "BUILD $? $d" >> "$OUT"
  grep -a "UNB3\|^error" .probe.log >> "$OUT"
  rm -f .probe.log
}
build_one "$R/compiler"
build_one "$R/tests"
for c in $(find "$R/tests/tests/projects" "$R/examples" -name cryoconfig); do
  build_one "$(dirname "$c")"
done
cd "$R/tests"
"$CC_EXE" test zzz_no_such_test_zzz > .probe.log 2>&1
echo "UNIT $?" >> "$OUT"
grep -a "UNB3" .probe.log >> "$OUT"
rm -f .probe.log
echo "PROBE_DONE" >> "$OUT"
