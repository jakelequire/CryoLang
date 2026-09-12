#!/usr/bin/env bash
# Hash the CURRENT tree's compiler over examples + tests (no build of the compiler). $1 = tag
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; D="$(dirname "$0")"; mkdir -p "$S"; T="$1"; cd "$R" || exit 1
"$D/ex-hash.sh" "$S/ex-$T.txt" | tail -1; "$D/tests-hash.sh" "$S/t-$T.txt" | tail -1
LC_ALL=C sort "$S/ex-$T.txt" > "$S/ex-$T.s"
grep OVERALL "$S/t-$T.txt.log"
echo HASH_DONE
