#!/usr/bin/env bash
# Phase A exit gate: every .cryo file in experimental/stdlib-next/core/
# and experimental/stdlib-next/alloc/ must reach the parser's success
# state. Sema/codegen errors (E0044, E0046, E0091, etc.) are expected
# until Phase B/C land — they're filtered out so this script reports
# only PARSE failures.
#
# Usage: scripts/parse-stdlib-next.sh
#
# Exit 0 = every file parses. Exit 1 = at least one file failed to
# parse (any E00xx code in the parse range).

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CRYO="${ROOT}/compiler/build/bin/cryo"
TARGETS=(
    "${ROOT}/experimental/stdlib-next/core"
    "${ROOT}/experimental/stdlib-next/alloc"
)

if [ ! -x "${CRYO}" ]; then
    echo "ERROR: ${CRYO} not found. Run 'make cryo' or 'make cryo-fast' first." >&2
    exit 2
fi

# Parser-stage error codes:
#   E0026  expected token
#   E0027  unexpected token
#   E0030  expected type
#   E0100  expected token (parser variant)
#   E0101  unexpected token
#   E0104  expected type in generic args
# Anything outside this set is downstream of the parser and ignored here.
PARSE_CODES='E0026|E0027|E0030|E0100|E0101|E0104'

fail=0
for dir in "${TARGETS[@]}"; do
    if [ ! -d "${dir}" ]; then continue; fi
    while IFS= read -r -d '' f; do
        out=$("${CRYO}" raw "$f" 2>&1)
        # Does any line contain a parser error code?
        if echo "$out" | grep -qE "error\[(${PARSE_CODES})\]"; then
            rel=${f#${ROOT}/}
            echo "FAIL  $rel"
            echo "$out" | grep -E "error\[(${PARSE_CODES})\]" | sed 's/^/  /'
            fail=$((fail + 1))
        fi
    done < <(find "${dir}" -maxdepth 2 -name '*.cryo' -print0)
done

if [ $fail -eq 0 ]; then
    echo "OK  every stdlib-next/{core,alloc} file parses cleanly"
    exit 0
else
    echo
    echo "${fail} file(s) failed to parse"
    exit 1
fi
