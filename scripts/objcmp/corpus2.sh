#!/usr/bin/env bash
# Whole corpus under the shadowed compiler, stderr captured everywhere. $1 = tag
# SIX halves: the LSP built directly (lsp-gate.py swallows the compiler's stderr on success), the unit suite,
# every project built one by one (`cryo test` swallows a passing project's stderr), a `collect` project's own
# tests/ files (compiled only by `cryo test`), the examples, and the compile-fail suite (the runner redirects
# each child's output to a temp file).
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; mkdir -p "$S"; T="$1"; cd "$R" || exit 1
: > "$S/$T-lines.txt"
(cd tools/CryoLSP && rm -rf build/gate-direct && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build --build-dir=build/gate-direct > "$S/$T-lsp.log" 2>&1; echo "lsp direct exit=$?")
grep -a 'Building cryolsp' "$S/$T-lsp.log" | head -1
grep -a '^SHADOW' "$S/$T-lsp.log" | sed 's#^#tools/CryoLSP\t#' >> "$S/$T-lines.txt"
find tests -type d -name build -prune -exec rm -rf {} + 2>/dev/null
CRYO_CC=gcc timeout 3000 make test > "$S/$T-test.log" 2>&1; echo "make test exit=$?"; grep OVERALL "$S/$T-test.log"
grep -a '^SHADOW' "$S/$T-test.log" | sed 's#^#tests/unit\t#' >> "$S/$T-lines.txt"
for d in tests/tests/projects/*/; do [ -f "$d/test.json" ] || continue
  (cd "$d" && rm -rf build && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build . 2>&1 | grep -a '^SHADOW' | sed "s#^#$d\t#" >> "$S/$T-lines.txt"; rm -rf build)
  if [ -d "$d/tests" ]; then (cd "$d" && rm -rf build && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe test 2>&1 | grep -a '^SHADOW' | sed "s#^#$d(test)\t#" >> "$S/$T-lines.txt"; rm -rf build); fi
done
for d in examples/*/; do (cd "$d" && rm -rf build && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build . 2>&1 | grep -a '^SHADOW' | sed "s#^#$d\t#" >> "$S/$T-lines.txt"); done
for f in tests/tests/negative/*.cryo; do (cd tests && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe check "${f#tests/}" --stdlib=$R/stdlib 2>&1 | grep -a '^SHADOW' | sed "s#^#$f\t#" >> "$S/$T-lines.txt"); done
echo "SHADOW lines (lsp+suite+projects+project tests+examples+negatives): $(wc -l < "$S/$T-lines.txt")"
cut -f2,3 "$S/$T-lines.txt" | sort | uniq -c | sort -rn | head; echo CORPUS_DONE
