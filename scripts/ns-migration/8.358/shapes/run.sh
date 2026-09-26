#!/usr/bin/env bash
# run.sh <compiler> <shape>...: build and run each shape here, print its exit
# and first error.  v1/v4: a public type and a private function under one
# leaf (both orders) - E0353; v2: both public - runs 7; v3: private type,
# public function - runs 9.
C="$1"; shift
D="$(cd "$(dirname "$0")" && pwd)"; R="$(git -C "$D" rev-parse --show-toplevel)"
for p in "$@"; do
  printf '[project]\nproject_name = "%s"\noutput_dir = "build"\ntarget_type = "executable"\nsource_dir = "src"\nentry_point = "src/main.cryo"\n\n[compiler]\n\n[dependencies]\n' "$p" > "$D/$p/cryoconfig"
  (cd "$D/$p" && rm -rf build && CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc "$C" build . > "$D/$p.log" 2>&1); rc=$?
  r=""; if [ $rc = 0 ]; then (cd "$D/$p" && ./build/$p.exe > /dev/null 2>&1); r="/run=$?"; fi
  echo "$p=$rc$r  $(grep -a -m1 '^error' "$D/$p.log")"
  rm -rf "$D/$p/build" "$D/$p/cryoconfig" "$D/$p.log"
done
