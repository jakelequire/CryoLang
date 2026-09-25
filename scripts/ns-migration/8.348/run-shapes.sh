#!/usr/bin/env bash
# run-shapes.sh <compiler> <shape>...: build and run each shape under
# shapes/ with <compiler>, and a same-spelling control "<shape>0" - the
# impl's parameter respelled as the type's (U->T; I->J in shape j) - which
# must build either way.  Prints `<shape>=<build exit>/run=<exit>` and the
# first error.  Work and logs go to ${OBJCMP_OUT:-.objcmp}/shapes-8.348.
set -u
R="$(git rev-parse --show-toplevel)"; C="$1"; shift
H="$(cd "$(dirname "$0")" && pwd)/shapes"
O="${OBJCMP_OUT:-$R/.objcmp}/shapes-8.348"; mkdir -p "$O"
for s in "$@"; do
  for v in "$s" "${s}0"; do
    d="$O/w-$v"; rm -rf "$d"; mkdir -p "$d/src"
    if [ "$v" = "$s" ]; then
      cp "$H/$s/src/main.cryo" "$d/src/main.cryo"
    else
      sed -e 's/\bU\b/T/g' -e 's/<I, A>/<J, A>/; s/Skip<I>/Skip<J>/; s/where I:/where J:/' "$H/$s/src/main.cryo" > "$d/src/main.cryo"
    fi
    printf '[project]\nproject_name = "shape%s"\noutput_dir = "build"\ntarget_type = "executable"\nsource_dir = "src"\nentry_point = "src/main.cryo"\n\n[compiler]\n\n[dependencies]\n' "$v" > "$d/cryoconfig"
    (cd "$d" && CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc "$C" build . > "$O/$v.log" 2>&1); rc=$?
    run=""
    if [ $rc -eq 0 ]; then (cd "$d" && ./build/shape$v.exe > /dev/null 2>&1); run="/run=$?"; fi
    echo "$v=$rc$run  $(grep -a -m1 '^error' "$O/$v.log")"
  done
done
echo SHAPES_DONE
