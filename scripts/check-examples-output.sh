#!/usr/bin/env bash
#
# check-examples-output.sh - golden-output regression guard for the example
# programs.
#
# `make examples` only *builds* every example (it would catch a compile
# regression but not a runtime one). This script additionally *runs* the
# deterministic, no-input examples and diffs their stdout against committed
# golden files at `examples/<name>/expected.out`, so a miscompile or stdlib
# change that alters a shipped "getting started" program's output fails CI
# instead of reaching users.
#
# Only examples with stable, input-free output are listed; anything that
# reads argv/stdin, sleeps, threads, or uses the clock/RNG is intentionally
# excluded.
#
# Output is compared line-ending-normalized (CRLF -> LF) so one golden serves
# both Linux (LF) and Windows (CRLF from text-mode stdio).
#
# Env:
#   CRYO         compiler to use (default: bin/cryo for this repo). The
#                Makefile target passes the freshly built stage-2 compiler.
#   CRYO_CC      C compiler for the final link (passed through to `cryo`).
#
# Exit: 0 if every listed example builds, runs, and matches its golden; 1
# otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CRYO="${CRYO:-$ROOT/bin/cryo}"
export CRYO_STDLIB="${CRYO_STDLIB:-$ROOT/stdlib}"

# Deterministic, input-free examples only.
EXAMPLES=(02-fizzbuzz 07-shapes 10-expr-interpreter 13-closures)

fail=0
for n in "${EXAMPLES[@]}"; do
    dir="$ROOT/examples/$n"
    golden="$dir/expected.out"

    if [ ! -f "$golden" ]; then
        echo "FAIL $n: missing golden file ($golden)"
        fail=1
        continue
    fi

    if ! ( cd "$dir" && "$CRYO" build >/dev/null 2>&1 ); then
        echo "FAIL $n: build failed"
        fail=1
        continue
    fi

    bin="$dir/build/$n.exe"
    [ -x "$bin" ] || bin="$dir/build/$n"
    if [ ! -x "$bin" ]; then
        echo "FAIL $n: built binary not found under $dir/build/"
        fail=1
        continue
    fi

    got="$("$bin" 2>/dev/null | sed 's/\r$//')"
    want="$(sed 's/\r$//' "$golden")"
    if [ "$got" = "$want" ]; then
        echo "ok   $n"
    else
        echo "FAIL $n: stdout does not match $golden"
        diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | head -40
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "examples-golden: FAILED"
    exit 1
fi
echo "examples-golden: OK (${#EXAMPLES[@]} examples)"
