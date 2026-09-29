#!/bin/bash
# usage: probe_err.sh <compiler.exe> <out-file>
# Like probe_run.sh, but with a compiler that REPORTS (the tree, or a
# mutants.py binary): collects the `-->` line of every "no method named"
# error.  The compiler's own sources are reached through `cryo check`, which
# writes nothing, so compiler/build is left alone.  Diff two runs' outputs.
CC_EXE="$1"; OUT="$2"
R=C:/Programming/apps/CryoLang
: > "$OUT"
export CRYO_STDLIB=$R/stdlib CRYO_CC=gcc
grab() { grep -a -A1 "no method named" "$1" | grep -a -- "-->" | sed 's#\\#/#g' >> "$OUT"; }
cd $R/compiler && "$CC_EXE" check src/main.cryo > $R/.objcmp/s77/pe.log 2>&1; echo "CHECK $?" >> "$OUT"; grab $R/.objcmp/s77/pe.log
build_one() {
  cd "$1" || return
  rm -rf build
  "$CC_EXE" build > $R/.objcmp/s77/pe.log 2>&1
  echo "BUILD $? $1" >> "$OUT"
  grab $R/.objcmp/s77/pe.log
}
build_one "$R/tests"
for c in $(find "$R/tests/tests/projects" "$R/examples" -name cryoconfig); do
  build_one "$(dirname "$c")"
done
cd "$R/tests"; "$CC_EXE" test zzz_no_such_test_zzz > $R/.objcmp/s77/pe.log 2>&1; echo "UNIT $?" >> "$OUT"; grab $R/.objcmp/s77/pe.log
echo "PROBE_DONE" >> "$OUT"
