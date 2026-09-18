#!/bin/bash
# The pair for §8.243's structural rules: 9e7b778b's ns-status-check (no
# shape rules) beside this tree's, over a COPY of the current ledger with one
# corruption re-applied at a time (corrupt.py).  Both run the row commands
# against the current tree, so a row that holds under one holds under both;
# the only difference is whether the damaged shape is seen.
#
# The old gate reads its repo root from its own location, so its two files
# are copied into scripts/ under other names for the run and removed after;
# the old script's `import ns_ledger` is pointed at the old module.
cd "$(dirname "$0")/../../.." || exit 2
OUT=.objcmp/ns-pair
mkdir -p "$OUT/docs"
git show 9e7b778b:scripts/ns_ledger.py > scripts/_ns_ledger_9e7b778b.py
git show 9e7b778b:scripts/ns-status-check.py \
  | sed 's/^import ns_ledger .*$/import _ns_ledger_9e7b778b as ns_ledger/' \
  > scripts/_ns-status-check-9e7b778b.py
grep -q '_ns_ledger_9e7b778b as ns_ledger' scripts/_ns-status-check-9e7b778b.py || { echo "old gate not repointed"; exit 2; }

pair() {
  local label="$1" ledger="$2"
  echo "---- $label"
  echo -n "OLD: "; ${PYTHON:-python} scripts/_ns-status-check-9e7b778b.py --ledger "$ledger" 2>&1 | tail -1 | cut -c1-90
  echo -n "NEW: "; ${PYTHON:-python} scripts/ns-status-check.py --ledger "$ledger" 2>&1 | grep -E 'MALFORMED|because|OK --|DRIFT' | head -4 | tr '\n' ' ' | cut -c1-230; echo
}
${PYTHON:-python} scripts/ns-migration/8.243/corrupt.py docs/name-resolution.md "$OUT/docs"
pair "control: the current ledger" "$PWD/docs/name-resolution.md"
pair "D5's cell as spliced at fd3756af" "$PWD/$OUT/docs/d5.md"
pair "the trait-registry row's fragment as at fd3756af" "$PWD/$OUT/docs/row109.md"
pair "an expected value in backticks (D6's, as it stood until 8.243)" "$PWD/$OUT/docs/d6.md"
pair "an unescaped pipe in a check (the is_alias_keyword row, as it stood)" "$PWD/$OUT/docs/pipe.md"
rm -f scripts/_ns_ledger_9e7b778b.py scripts/_ns-status-check-9e7b778b.py
echo "---- tree: $(git status --short scripts | wc -l) changed under scripts/"
