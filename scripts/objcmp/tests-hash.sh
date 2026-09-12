#!/usr/bin/env bash
# Wipe every build dir under tests/, run the suite, hash every object it produced. $1 = out
set -u
R="$(git rev-parse --show-toplevel)"; cd "$R" || exit 1
find tests -type d -name build -prune -exec rm -rf {} + 2>/dev/null
CRYO_CC=gcc timeout 3000 make test > "$1.log" 2>&1; echo "make test exit=$?"
grep 'OVERALL' "$1.log"
find tests -name '*.o' -path '*/build/*' | LC_ALL=C sort | xargs sha256sum | LC_ALL=C sort > "$1"
echo "# objects: $(wc -l < "$1")"
