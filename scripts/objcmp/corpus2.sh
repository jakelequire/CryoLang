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
# Every half echoes its exit code: a grep in a pipeline hides the compiler's, so a
# project or example that STOPS compiling under the shadow would otherwise read
# as a half with no disagreements.  A non-zero exit is a finding; `$S/$T-fail.log`
# keeps the failing half's full output.
: > "$S/$T-fail.log"
# `want` is the exit the half is EXPECTED to produce: `zero` for a project that
# builds, `nonzero` for a `compile_fail` project or a `collect` project whose
# test.json says `"fails": true`.  A `compile_fail` project that COMPILES is as
# much a finding as a `run` project that stops compiling.
run_half() { # $1 = label, $2 = want (zero|nonzero), $3.. = command, run in the CURRENT directory
  local label="$1" want="$2"; shift 2
  local out; out="$("$@" 2>&1)"; local rc=$?
  printf '%s' "$out" | grep -a '^SHADOW' | sed "s#^#$label\t#" >> "$S/$T-lines.txt"
  local bad=0
  if [ "$want" = zero ] && [ $rc -ne 0 ]; then bad=1; fi
  if [ "$want" = nonzero ] && [ $rc -eq 0 ]; then bad=1; fi
  if [ $bad -ne 0 ]; then echo "FAIL exit=$rc want=$want $label"; { echo "=== $label exit=$rc want=$want"; printf '%s\n' "$out"; } >> "$S/$T-fail.log"; fi
}
want_of() { # $1 = test.json path, $2 = build|test: what that half of the project should exit
  local j; j="$(tr -d ' \n\r' < "$1")"
  case "$2:$j" in
    *'"outcome":"compile_fail"'*) echo nonzero ;;
    test:*'"fails":true'*)        echo nonzero ;;   # a canary: it BUILDS, and its tests fail by design
    *)                            echo zero ;;
  esac
}
for d in tests/tests/projects/*/; do [ -f "$d/test.json" ] || continue
  # A `requires` project (a toolchain this host may lack) is skipped by the runner too.
  if grep -q '"requires"' "$d/test.json"; then echo "SKIP requires: $d"; continue; fi
  (cd "$d" && rm -rf build && run_half "$d" "$(want_of test.json build)" env CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build .; rm -rf build)
  if [ -d "$d/tests" ]; then (cd "$d" && rm -rf build && run_half "$d(test)" "$(want_of test.json test)" env CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe test; rm -rf build); fi
done
for d in examples/*/; do (cd "$d" && rm -rf build && run_half "$d" zero env CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build .); done
# The compile-fail suite is EXPECTED to exit non-zero; only its SHADOW lines are read.
for f in tests/tests/negative/*.cryo; do (cd tests && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe check "${f#tests/}" --stdlib=$R/stdlib 2>&1 | grep -a '^SHADOW' | sed "s#^#$f\t#" >> "$S/$T-lines.txt"); done
echo "failing halves: $(grep -c '^=== ' "$S/$T-fail.log")"
echo "SHADOW lines (lsp+suite+projects+project tests+examples+negatives): $(wc -l < "$S/$T-lines.txt")"
cut -f2,3 "$S/$T-lines.txt" | sort | uniq -c | sort -rn | head; echo CORPUS_DONE
