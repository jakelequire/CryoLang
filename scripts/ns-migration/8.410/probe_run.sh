#!/bin/bash
# usage: probe_run.sh <compiler.exe> <out-file>
# Builds the compiler, every tests/projects project and every example with the
# given compiler from clean, collecting CONSUME_PROBE lines into <out-file>.
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
  grep -a "CONSUME_PROBE" .probe.log >> "$OUT"
  rm -f .probe.log
}
if [ -z "$SKIP_COMPILER" ]; then build_one "$R/compiler"; fi
build_one "$R/tests"
for c in $(find "$R/tests/tests/projects" "$R/examples" -name cryoconfig); do
  build_one "$(dirname "$c")"
done
echo "PROBE_DONE" >> "$OUT"
