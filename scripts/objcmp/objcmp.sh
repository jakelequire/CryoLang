#!/usr/bin/env bash
# Compare objects: HEAD's compiler (stash working compiler/src) vs the working tree's.
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; D="$(dirname "$0")"; mkdir -p "$S"; cd "$R" || exit 1
git stash push -q -- compiler/src || { echo "STASH FAILED"; exit 1; }
rm -rf compiler/build; CRYO_CC=gcc timeout 900 make cryo > /dev/null 2>&1; echo "head build exit=$?"
"$D/ex-hash.sh" "$S/ex-A.txt" | tail -1; "$D/tests-hash.sh" "$S/t-A.txt" | tail -1
git stash pop -q || { echo "POP FAILED"; exit 1; }
echo "POPPED"
rm -rf compiler/build; CRYO_CC=gcc timeout 900 make cryo > /dev/null 2>&1; echo "tree build exit=$?"
"$D/ex-hash.sh" "$S/ex-B.txt" | tail -1; "$D/tests-hash.sh" "$S/t-B.txt" | tail -1
LC_ALL=C sort "$S/ex-A.txt" > "$S/ex-A.s"; LC_ALL=C sort "$S/ex-B.txt" > "$S/ex-B.s"
echo "examples objects changed: $(LC_ALL=C comm -3 "$S/ex-A.s" "$S/ex-B.s" | wc -l)"
echo "tests objects changed: $(LC_ALL=C comm -3 "$S/t-A.txt" "$S/t-B.txt" | wc -l)"
grep OVERALL "$S/t-B.txt.log"; echo OBJCMP_DONE
