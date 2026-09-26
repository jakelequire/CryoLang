#!/usr/bin/env bash
# run.sh <compiler> <shape>... : build+run each shape dir under shapes/
set -u
R="$(git rev-parse --show-toplevel)"; C="$1"; shift
S="$R/scripts/ns-migration/8.356/shapes"; O="$R/.objcmp/8.356/logs"; mkdir -p "$O"
for v in "$@"; do
  d="$S/$v"
  printf '[project]\nproject_name = "shape%s"\noutput_dir = "build"\ntarget_type = "executable"\nsource_dir = "src"\nentry_point = "src/main.cryo"\n\n[compiler]\n\n[dependencies]\n' "$v" > "$d/cryoconfig"
  (cd "$d" && rm -rf build && CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc "$C" build . > "$O/$v.log" 2>&1); rc=$?
  run=""
  if [ $rc -eq 0 ]; then (cd "$d" && ./build/shape$v.exe > /dev/null 2>&1); run="/run=$?"; fi
  rm -rf "$d/build"
  echo "$v=$rc$run  $(grep -a -m1 '^error' "$O/$v.log")"
done
