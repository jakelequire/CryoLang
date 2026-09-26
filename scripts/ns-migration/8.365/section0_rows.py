#!/usr/bin/env python3
"""Move the section-0 rows the facts emitter moved (docs/name-resolution.md).

Each replacement names the old text exactly and the number of times it must
occur, and refuses otherwise, so a row that already moved or a pattern that
matches somewhere else is caught instead of rewritten.
"""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LEDGER = os.path.join(ROOT, "docs", "name-resolution.md")

EDITS = [
    ("wc -l` → **634** (+1 in §8.364:",
     "wc -l` → **637** (+3 in §8.365: the facts recorder `sema/call_facts.cryo` and the "
     "inversion harness's two project templates under `scripts/ns-migration/8.365/`; "
     "634 before, +1 in §8.364:", 1),
    ("`python3 scripts/ns-migration/done.py --name-taking | wc -l` → **1110** (",
     "`python3 scripts/ns-migration/done.py --name-taking | wc -l` → **1114** (+4 in §8.365: "
     "the facts writer's `write` (a file path), `sort_key` (a record), `normalized_path` (a "
     "source path) and `local_provenance` (a local's spelling, printed) - text written, never "
     "looked up; 1,110 before, ", 1),
    ("`python3 scripts/ns-migration/residue.py --count` → **316** (",
     "`python3 scripts/ns-migration/residue.py --count` → **317** (+1 in §8.365: the facts "
     "recorder resolves a family pin to its one entry, `lookup_family_entries` by the owner and "
     "the leaf, J member; 316 before, ", 1),
    ("`python3 scripts/ns-migration/residue.py --check \\| grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **189** (",
     "`python3 scripts/ns-migration/residue.py --check \\| grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **190** "
     "(+1 in §8.365, the facts recorder's family read; 189 before, ", 1),
    ("`grep -rho 'CalleePin::Decl(' compiler/src --include=*.cryo \\| wc -l` → **17** (",
     "`grep -rho 'CalleePin::Decl(' compiler/src --include=*.cryo \\| wc -l` → **18** (+1 in "
     "§8.365, the facts recorder reading a pin's entry; 17 before, ", 1),
    ("`grep -rho 'CalleePin::Family(' compiler/src --include=*.cryo \\| wc -l` → **15** (",
     "`grep -rho 'CalleePin::Family(' compiler/src --include=*.cryo \\| wc -l` → **16** (+1 in "
     "§8.365, the facts recorder reading a family pin; 15 before, ", 1),
    ("`python3 scripts/lane-gate.py --row DEFID_PATH` → **51** (",
     "`python3 scripts/lane-gate.py --row DEFID_PATH` → **52** (+1 in §8.365: the facts recorder "
     "prints a definition's path - a global argument's provenance, a key type's declaration - "
     "as report text, through one helper; 51 before, ", 1),
    ("`python3 scripts/lane-gate.py --row ARENA_READ` → **60** (",
     "`python3 scripts/lane-gate.py --row ARENA_READ` → **61** (+1 in §8.365: the facts recorder "
     "prints a type's display name as report text, through one helper; 60 before, ", 1),
    ("| **RULED** | `no check` — not built | §8.363 |",
     "| **The emitter TAKEN (§8.365)**: `cryo build --emit=facts` writes `<output_dir>/<name>.facts`, "
     "one record per argument a call passes to a key parameter and per comparison of keys, from "
     "the body check before monomorphization; `make facts` runs it over the compiler, the stdlib "
     "and the editor into `.facts/`. **The gate's counting half reading the facts: NOT BUILT** "
     "| `grep -c '^public module CallFacts;' compiler/src/compiler/sema/_module.cryo` → **1**; "
     "`grep -c '^facts: ' Makefile` → **2** (one per host branch) | §8.363, §8.365 |", 1),
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
