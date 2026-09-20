#!/usr/bin/env bash
# The two halves `scripts/objcmp/corpus2.sh` does not reach, for a probe that
# prints one `SHADOW` line per site the PARSER opens: every stdlib file under
# `cryo check` (a project parses only the modules it imports, and the stdlib
# archive is built by the pin, which carries no probe), and the compiler built
# from `compiler/` by the probed compiler (its own 160-odd modules).
#
#   bash scripts/ns-migration/8.277/halves.sh <tag>
#
# Appends to `$OBJCMP_OUT/<tag>-extra-lines.txt` in corpus2's format
# (`<half>\tSHADOW...`), so `probe_d31.py tally` reads both files as one.
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; mkdir -p "$S"; T="$1"; cd "$R" || exit 1
: > "$S/$T-extra-lines.txt"
fails=0
while IFS= read -r f; do
  out="$(CRYO_STDLIB=$R/stdlib CRYO_CC=gcc "$R/compiler/build/cryo.exe" check "$f" --stdlib="$R/stdlib" 2>&1)"; rc=$?
  if [ $rc -ne 0 ]; then echo "check FAIL exit=$rc $f"; fails=$((fails+1)); fi
  printf '%s\n' "$out" | grep -a '^SHADOW' | sed "s#^#stdlib-check/$f\t#" >> "$S/$T-extra-lines.txt"
done < <(find stdlib -name '*.cryo' | LC_ALL=C sort)
echo "stdlib-check: $(find stdlib -name '*.cryo' | wc -l) files, $fails failed"
# The compiler's own source, parsed by the probed compiler.  A fresh build
# directory: an incremental build parses nothing and prints 0.
(cd compiler && rm -rf build/probe-half && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc "$R/compiler/build/cryo.exe" build --build-dir=build/probe-half > "$S/$T-compiler-half.log" 2>&1; echo "compiler-build exit=$?"; rm -rf build/probe-half)
grep -a '^SHADOW' "$S/$T-compiler-half.log" | sed 's#^#compiler-build\t#' >> "$S/$T-extra-lines.txt"
echo "extra SHADOW lines (stdlib-check + compiler-build): $(wc -l < "$S/$T-extra-lines.txt")"
echo HALVES_DONE
