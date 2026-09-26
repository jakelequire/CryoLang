#!/usr/bin/env python3
"""Move the section-0 rows the residue's switch to the compiler's facts
moved (docs/name-resolution.md).  Each replacement names the old text
exactly and the number of times it must occur, and refuses otherwise."""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LEDGER = os.path.join(ROOT, "docs", "name-resolution.md")

EDITS = [
    ("`python3 scripts/ns-migration/residue.py --count` → **317** (",
     "`python3 scripts/ns-migration/residue.py --count` → **319** (+2 in §8.366: the population "
     "is read from the compiler's facts - three loops the text reader could not follow, a local "
     "bound to a member of the element (`call_resolver.cryo`, two) and an element bound through `&` "
     "(`instance.cryo`), and one it over-counted, a loop searching a list of ids for a key taken "
     "from a table (`ast_validation.cryo`); 317 before, ", 1),
    ("`python3 scripts/ns-migration/residue.py --check \\| grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **190** (",
     "`python3 scripts/ns-migration/residue.py --check \\| grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **192** "
     "(+2 in §8.366, the same four sites, all J; 190 before, ", 1),
    ("`grep -c '^check-fast: lane-check lane-selftest' Makefile` → **1**",
     "`grep -c '^check-fast: facts-fresh lane-check lane-selftest' Makefile` → **1** (§8.366: "
     "the facts are refreshed first when stale)", 1),
    ("The gate's counting half reading the facts: NOT BUILT**",
     "The residue's counting half reads the facts (§8.366)**, its text-pattern counting deleted; "
     "the lane gate's buckets are still counted from source text", 1),
    ("every site derived from the lane gate's own parser",
     "every site derived from the lane gate's own parser (READ FROM THE COMPILER'S FACTS since "
     "§8.366: the calls and comparisons from `cryo build --emit=facts`, the holders still placed "
     "by the lane gate over declarations)", 1),
]


def main():
    with io.open(LEDGER, encoding="utf-8", newline="") as fh:
        s = fh.read()
    for old, new, count in EDITS:
        n = s.count(old)
        if n != count:
            print("section0_rows: expected %d of %r, found %d" % (count, old[:80], n))
            return 1
        s = s.replace(old, new)
    with io.open(LEDGER, "w", encoding="utf-8", newline="") as fh:
        fh.write(s)
    print("section0_rows: %d edits" % len(EDITS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
