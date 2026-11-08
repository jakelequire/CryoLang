#!/usr/bin/env bash
#
# valgrind-check.sh - runtime memory-safety gate for the example programs.
#
# The static move-checker rejects use-after-move/double-free at compile time,
# but nothing else exercises the *runtime* drop/free paths the generated code
# emits. This script builds the deterministic, input-free examples and runs
# each under valgrind, failing on any invalid free/read/write or definite
# leak. It is the "valgrind gate" referenced by the memory-safety tests; a
# miscompile that double-freed or leaked an owned value fails CI here instead
# of reaching users.
#
# Only definite leaks are treated as errors (--errors-for-leak-kinds=definite):
# "still reachable" allocations live to process exit and are not bugs. Invalid
# frees/reads/writes always count (--error-exitcode=1).
#
# Env:
#   CRYO         compiler to use (default: bin/cryo). The Makefile passes the
#                freshly built stage-2 compiler.
#   CRYO_STDLIB  stdlib root (default: this repo's stdlib).
#   ROOT         the checkout (default: this script's parent directory). The
#                Makefile passes it, since it hands bash the script's text
#                with carriage returns stripped (`bash -c`): a working copy
#                checked out with CRLF endings runs the same.
#
# The program run is the one this host builds, `build/<name>`: a checkout
# shared with Windows also holds a `build/<name>.exe` from a Windows build,
# which valgrind cannot start. A program valgrind cannot start fails as
# that, not as a memory error.
#
# Exit: 0 if every listed example builds, runs, and is valgrind-clean; 1
# otherwise.

set -uo pipefail

ROOT="${ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CRYO="${CRYO:-$ROOT/bin/cryo}"
export CRYO_STDLIB="${CRYO_STDLIB:-$ROOT/stdlib}"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "valgrind-check: valgrind not found on PATH" >&2
    exit 1
fi

# Deterministic, input-free examples only (same set as check-examples-output.sh):
# no argv/stdin, clock, RNG, or threads, so a run is reproducible under valgrind.
EXAMPLES=(02-fizzbuzz 07-shapes 10-expr-interpreter 13-closures)

fail=0
for n in "${EXAMPLES[@]}"; do
    dir="$ROOT/examples/$n"

    if ! ( cd "$dir" && "$CRYO" build >/dev/null 2>&1 ); then
        echo "FAIL $n: build failed"
        fail=1
        continue
    fi

    bin="$dir/build/$n"
    if [ ! -x "$bin" ]; then
        echo "FAIL $n: built binary not found: $bin"
        fail=1
        continue
    fi

    valgrind --error-exitcode=1 --leak-check=full \
             --errors-for-leak-kinds=definite -q "$bin" >/dev/null 2>"$dir/build/valgrind.log"
    status=$?
    if [ "$status" -eq 0 ]; then
        echo "ok   $n"
    elif [ "$status" -eq 1 ]; then
        echo "FAIL $n: valgrind reported errors"
        grep -E 'ERROR SUMMARY|definitely lost|Invalid (free|read|write)|blocks are definitely' \
            "$dir/build/valgrind.log" | head -20
        fail=1
    else
        echo "FAIL $n: valgrind could not run $bin (exit $status)"
        head -5 "$dir/build/valgrind.log"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "valgrind-check: FAILED"
    exit 1
fi
echo "valgrind-check: OK (${#EXAMPLES[@]} examples)"
