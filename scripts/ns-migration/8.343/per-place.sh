#!/usr/bin/env bash
# Option B's count, one place at a time.
#
# Needs a compiler built from `option-b-probe.patch` applied to 0aa16644
# (`git apply scripts/ns-migration/8.343/option-b-probe.patch && make cryo`,
# then copy compiler/build/cryo.exe somewhere and pass its path as $1).
# That compiler keys a generic parameter's type by its declaration, and every
# lookup the flip makes answer by identity answers by SPELLING instead,
# printing `SHADOW BSPELL <kind> <place>` where the two differ.  The env var
# B_STRICT makes the named places answer by identity: `a,b` = those places,
# `-a,b` = every place but those, `all` = every place.
#
# For each B_STRICT value given after $1, builds 01-hello and the shapes here,
# and prints each build's exit (and the shape's own exit when it built) and the
# distinct error locations.
set -u
R="$(git rev-parse --show-toplevel)"; C="$1"; shift
H="$(cd "$(dirname "$0")" && pwd)/shapes"
O="${OBJCMP_OUT:-$R/.objcmp}/per-place"; mkdir -p "$O"
for tag in "$@"; do
  safe=$(echo "$tag" | tr ':/,' '___')
  line="[$tag]"
  for d in "$R/examples/01-hello" "$H/a" "$H/b" "$H/c" "$H/f"; do n=$(basename "$d")
    (cd "$d" && rm -rf build && B_STRICT="$tag" CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc "$C" build . > "$O/$safe-$n.log" 2>&1); rc=$?
    run=""
    if [ $rc -eq 0 ] && [ "$n" != "01-hello" ]; then (cd "$d" && ./build/shape$n.exe > /dev/null 2>&1); run="/run=$?"; fi
    rm -rf "$d/build"
    line="$line $n=$rc$run"
  done
  echo "$line"
  grep -a -h -A1 '^error' "$O/$safe"-*.log | grep -a -- '-->' | sed 's/:[0-9]*$//' | sort | uniq -c | sort -rn | head -6 | sed 's/^/      /'
done
echo PER_PLACE_DONE
