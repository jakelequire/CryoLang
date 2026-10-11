#!/bin/bash
# Run a compiler over the full corpus: compiler, stdlib, tests, every
# project/example/tool (and `cryo test` in each project that has a tests/
# directory, which `cryo build` never compiles), the unit leg, and each
# negative file alone.  Logs every report.
#   usage: corpus108.sh <cryo.exe> <out-dir> [ENV=VAL]
CC_EXE="$1"; OUT="$2"; EXTRA="$3"; R=C:/Programming/apps/CryoLang
mkdir -p "$OUT/logs"; : > "$OUT/status.txt"
export CRYO_STDLIB=$R/stdlib CRYO_CC=gcc
[ -n "$EXTRA" ] && export "$EXTRA"
name() { echo "$1" | sed -e "s|$R/||" -e 's|[/:]|_|g'; }
build_one() {
  local d="$1" n; n=$(name "$d")
  cd "$d" || { echo "NODIR $d" >> "$OUT/status.txt"; return; }
  rm -rf "$OUT/bd"
  "$CC_EXE" build --build-dir="$OUT/bd" > "$OUT/logs/$n.log" 2>&1
  echo "BUILD rc=$? $d" >> "$OUT/status.txt"
  if [ -d "$d/tests" ] && [ -f "$d/test.json" ]; then
    rm -rf "$OUT/bd"
    "$CC_EXE" test --build-dir="$OUT/bd" > "$OUT/logs/$n.test.log" 2>&1
    echo "PTEST rc=$? $d" >> "$OUT/status.txt"
  fi
}
build_one "$R/compiler"
build_one "$R/stdlib"
build_one "$R/tests"
for c in $(find "$R/tests/tests/projects" "$R/examples" "$R/tools" -name cryoconfig | sort); do
  build_one "$(dirname "$c")"
done
cd "$R/tests"
rm -rf "$OUT/bd"
"$CC_EXE" test zzz_no_such_test_zzz --build-dir="$OUT/bd" > "$OUT/logs/unit.log" 2>&1
echo "UNIT rc=$?" >> "$OUT/status.txt"
[ "$4" = "noneg" ] && { echo "PROBE_DONE" >> "$OUT/status.txt"; exit 0; }
for f in "$R"/tests/tests/negative/*.cryo; do
  cd "$R/tests"
  b=$(basename "$f" .cryo)
  "$CC_EXE" check "$f" > "$OUT/logs/neg_$b.log" 2>&1
  echo "NEG rc=$? $f" >> "$OUT/status.txt"
done
rm -rf "$OUT/bd"
echo "PROBE_DONE" >> "$OUT/status.txt"
