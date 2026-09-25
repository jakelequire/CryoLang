#!/usr/bin/env bash
# per-place.sh <compiler> <outdir> <B_STRICT tag>...: the per-place table.
#
# <compiler> is built from `combined.patch` (the rebased option-B probe plus
# this entry's change) applied to cf230067; `probe-cf23.patch` alone gives
# the control.  Under the probe every lookup the arena flip turns into an
# identity question answers by SPELLING unless B_STRICT names it (a tag, a
# comma list, or `all`).  For each tag, builds examples/01-hello, 8.343's
# shapes a c f and this entry's i j l, runs each shape that built, and
# prints the exits plus the distinct error locations.
set -u
R="$(git rev-parse --show-toplevel)"; C="$1"; O="$2"; shift 2; mkdir -p "$O"
S1="$R/scripts/ns-migration/8.343/shapes"; S2="$(cd "$(dirname "$0")" && pwd)/shapes"
for tag in "$@"; do
  safe=$(echo "$tag" | tr ':/,' '___'); [ -z "$safe" ] && safe=NONE
  [ ${#safe} -gt 60 ] && safe=$(echo "$safe" | md5sum | cut -c1-12)
  line="[$tag]"
  for d in "$R/examples/01-hello" "$S1/a" "$S1/c" "$S1/f" "$S2/i" "$S2/j" "$S2/l"; do n=$(basename "$d")
    w="$O/w-$n"; rm -rf "$w"; cp -r "$d" "$w"; rm -rf "$w/build"
    (cd "$w" && B_STRICT="$tag" CRYO_STDLIB="$R/stdlib" CRYO_CC=gcc "$C" build . > "$O/$safe-$n.log" 2>&1); rc=$?
    run=""
    if [ $rc -eq 0 ] && [ "$n" != "01-hello" ]; then exe=$(ls "$w"/build/*.exe | head -1); (cd "$w" && "$exe" > /dev/null 2>&1); run="/run=$?"; fi
    line="$line $n=$rc$run"
  done
  echo "$line"
  grep -a -h -A1 '^error' "$O/$safe"-*.log | grep -a -- '-->' | sed 's/:[0-9]*$//' | sort | uniq -c | sort -rn | head -4 | sed 's/^/      /'
done
echo PER_PLACE_DONE
