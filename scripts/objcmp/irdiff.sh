#!/usr/bin/env bash
# IR of the unit-suite build, HEAD's compiler vs the tree's, for the modules whose objects moved.
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; mkdir -p "$S"; cd "$R" || exit 1
build_ir() { # $1 = tag
  rm -rf compiler/build; CRYO_CC=gcc timeout 900 make cryo > "$S/irdiff-build-$1.log" 2>&1 || { echo "build $1 FAILED"; return 1; }
  find tests -type d -name build -prune -exec rm -rf {} + 2>/dev/null
  (cd tests && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc timeout 1200 $R/compiler/build/cryo.exe test lambdas_never_matches_anything --list --emit-llvm > "$S/irdiff-test-$1.log" 2>&1; echo "test build $1 exit=$?")
  rm -rf "$S/ir-$1"; mkdir -p "$S/ir-$1"
  find tests/build -name '*.ll' | while read f; do cp "$f" "$S/ir-$1/$(echo "$f" | sed 's#.*/deps/##; s#/#__#g')"; done
  echo "$1: $(ls "$S/ir-$1" | wc -l) .ll files"
}
git stash push -q -- compiler/src || { echo "STASH FAILED"; exit 1; }
build_ir head
git stash pop -q || { echo "POP FAILED"; exit 1; }; echo POPPED
build_ir tree
echo IRDIFF_DONE
