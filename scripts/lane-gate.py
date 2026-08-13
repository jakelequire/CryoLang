#!/usr/bin/env python3
"""Pin the resolution-lane SURFACE against a committed golden.

docs/name-resolution.md §7.2 mechanism 5 requires one `resolve_path(segments,
ns, scope)` and three locks to keep it one.  Two of those locks cannot hold on
their own:

  * privatizing the per-kind lookups stops a direct call from `sema`, but not
    someone adding a NEW public wrapper beside them;
  * deleting a string-keyed entity lookup stops that one, but not a helper
    being reintroduced under another name.

Only a ratchet catches growth.  This is that ratchet, in the shape mechanism 3
already proved with `b1-gate.py`: a committed golden, drift fails, and `--update`
re-pins deliberately.

WHAT IS PINNED
--------------
Two counts, each broken down per file so a failure names what moved:

  * LOOKUP -- direct calls to the five per-kind lookups (`lookup_type`,
    `lookup_func_return`, `lookup_func_type`, `lookup_global`,
    `lookup_method_return`) outside the file that DEFINES them.
  * REENTRY -- calls to `get_resolver()` outside the driver.  §7.2's corollary:
    name resolution is a pass, not a service, and a stage that can call back
    into the resolver will.  A resolver called from `sema` no longer has the
    writer's imports in hand, so it answers from a string -- which is the
    mechanical origin of B1.

Both ratchet DOWNWARD.  An increase is the regrowth this exists to catch; a
decrease is progress and still fails, because a silently-tolerated decrease
leaves the old higher number as the bound and lets a later regression climb
back to it unnoticed.  That is the same reasoning as the B1 gate, and it is the
reason `--update` exists rather than a tolerance.

WHY THIS GATE IS SOURCE-DERIVED AND HAS NO PER-HOST SECTIONS
------------------------------------------------------------
`b1-gate.py` measures a BUILD, so its numbers move with which stdlib modules
the host compiles, and it needs a `[host:...]` section per host.  This gate
counts CALL SITES IN THE SOURCE.  The same tree gives the same answer on every
host, so a per-host split would encode a dimension that does not exist and
would let one host's re-pin hide another's regression.  It also means this gate
needs no compiler, no stdlib, and no successful link -- it runs on a fresh
clone in under a second, which is what makes it usable as a pre-commit check.

TWO THINGS A NAIVE GREP GETS WRONG, BOTH OBSERVED HERE
-------------------------------------------------------
  * COMMENTED-OUT CALLS.  `instance.cryo` carries a commented `//
    ctx.get_resolver();`.  Counting it pins 9 re-entries where 8 exist, so
    deleting a real call and leaving the comment would read as progress while
    uncommenting it would read as clean.  Lines whose first non-space
    characters are `//` are skipped, and a trailing `//` comment is cut before
    matching.
  * THE OWNER'S OWN CALLS.  `decl_index.cryo` defines the five lookups and
    calls them internally.  Those are not the surface this gate is about -- the
    surface is what OTHER stages reach for -- so the defining file is excluded.
    Excluding it by name is safe here precisely because it is the definition
    site, not a special case about some caller.

A row that reaches zero is DELETED from the golden rather than pinned at 0, so
the golden reads as the live surface rather than a graveyard; a file reappearing
is then an added row, which fails the same way an increase does.

Usage:
    python3 scripts/lane-gate.py [--update]

Exit codes: 0 match (or golden updated); 1 drift.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "compiler", "src")
GOLDEN = os.path.join(ROOT, "tests", "lane-baseline.txt")

# The five per-kind lookups §7.2 mechanism 5 names.  Matched as `.name(` so a
# definition (`lookup_type(&this, ...)`) is not counted as a call.
LOOKUPS = (
    "lookup_type",
    "lookup_func_return",
    "lookup_func_type",
    "lookup_global",
    "lookup_method_return",
)
LOOKUP_RE = re.compile(r"\.(?:%s)\s*\(" % "|".join(LOOKUPS))
REENTRY_RE = re.compile(r"\bget_resolver\s*\(\s*\)")

# The file that DEFINES the five lookups.  Its own calls are not the surface.
LOOKUP_OWNERS = {"decl_index.cryo"}
# The driver legitimately owns the resolver and may ask for it.
REENTRY_OWNERS = {"instance.cryo"}


def strip_comment(line):
    """Drop a `//` comment tail, so a call named only in prose is not counted.

    Deliberately naive about `//` inside a string literal: no such line exists
    in this tree, and a gate that silently counted one would be worse than one
    that fails loudly when it appears.
    """
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def scan():
    """Return {kind: {relpath: count}} over the compiler sources."""
    found = {"LOOKUP": {}, "REENTRY": {}}
    for dirpath, _dirs, files in os.walk(SRC):
        for fname in sorted(files):
            if not fname.endswith(".cryo"):
                continue
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, SRC).replace(os.sep, "/")
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.readlines()
            lookups = 0
            reentries = 0
            for raw in text:
                line = strip_comment(raw)
                if not line.strip():
                    continue
                if fname not in LOOKUP_OWNERS:
                    lookups += len(LOOKUP_RE.findall(line))
                if fname not in REENTRY_OWNERS:
                    reentries += len(REENTRY_RE.findall(line))
            if lookups:
                found["LOOKUP"][rel] = lookups
            if reentries:
                found["REENTRY"][rel] = reentries
    return found


HEADER = [
    "# Resolution-lane surface baseline -- docs/name-resolution.md §7.2 mechanism 5.",
    "#",
    "# ASSERTED: both totals and every per-file row.",
    "#",
    "# LOOKUP   direct calls to the five per-kind lookups outside decl_index.cryo,",
    "#          which defines them.",
    "# REENTRY  get_resolver() outside the driver. Name resolution is a PASS, not a",
    "#          service: a resolver called from sema no longer holds the writer's",
    "#          imports, so it answers from a string. That is where B1 comes from.",
    "#",
    "# Both ratchet DOWNWARD. An increase is regrowth; a decrease is progress and",
    "# must still be re-pinned with `make lane-check ARGS=--update`, because a",
    "# tolerated decrease leaves the old number as the ceiling.",
    "#",
    "# Source-derived, so there are no per-host sections: the same tree gives the",
    "# same answer everywhere, and splitting by host would let one host's re-pin",
    "# hide another's regression.",
]


def render(counts):
    lines = list(HEADER)
    for kind in ("LOOKUP", "REENTRY"):
        rows = counts[kind]
        lines.append("")
        lines.append("[%s]" % kind)
        lines.append("TOTAL %d" % sum(rows.values()))
        lines.append("")
        for rel in sorted(rows):
            lines.append("%-52s %d" % (rel, rows[rel]))
    return "\n".join(lines) + "\n"


def parse_golden(path):
    if not os.path.exists(path):
        return None
    counts = {"LOOKUP": {}, "REENTRY": {}}
    totals = {}
    kind = None
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                kind = line[1:-1]
                continue
            if kind is None:
                continue
            parts = line.rsplit(None, 1)
            if len(parts) != 2:
                continue
            name, value = parts[0].strip(), parts[1]
            try:
                value = int(value)
            except ValueError:
                continue
            if name == "TOTAL":
                totals[kind] = value
            else:
                counts[kind][name] = value
    return counts, totals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="rewrite the golden from the current measurement")
    args = ap.parse_args()

    counts = scan()
    live_totals = {k: sum(v.values()) for k, v in counts.items()}

    if args.update:
        with open(GOLDEN, "w", encoding="utf-8") as fh:
            fh.write(render(counts))
        print("lane-gate: golden updated -- LOOKUP = %d, REENTRY = %d"
              % (live_totals["LOOKUP"], live_totals["REENTRY"]))
        return 0

    parsed = parse_golden(GOLDEN)
    if parsed is None:
        sys.stderr.write(
            "lane-gate: no golden at %s.\n"
            "An unpinned surface is unmeasured, and this gate must never report\n"
            "OK for a number nobody committed. Create it with:\n"
            "    make lane-check ARGS=--update\n"
            "and commit it. NOTE: .gitignore ignores *.txt repo-wide, so the\n"
            "golden needs an explicit `!tests/lane-baseline.txt` negation or CI\n"
            "fails on a fresh clone with exactly this message.\n" % GOLDEN)
        return 1
    gold_counts, gold_totals = parsed

    problems = []
    for kind in ("LOOKUP", "REENTRY"):
        if kind not in gold_totals:
            problems.append("  %s: golden has no TOTAL line" % kind)
            continue
        if gold_totals[kind] != live_totals[kind]:
            direction = ("INCREASE -- a lane regrew"
                         if live_totals[kind] > gold_totals[kind]
                         else "decrease -- progress; re-pin deliberately")
            problems.append("  %s TOTAL %d -> %d  (%s)"
                            % (kind, gold_totals[kind], live_totals[kind], direction))
        for rel in sorted(set(gold_counts[kind]) | set(counts[kind])):
            was = gold_counts[kind].get(rel, 0)
            now = counts[kind].get(rel, 0)
            if was != now:
                problems.append("    %-8s %-46s %d -> %d" % (kind, rel, was, now))

    if problems:
        sys.stderr.write("lane-gate: DRIFT against %s\n" % os.path.relpath(GOLDEN, ROOT))
        sys.stderr.write("\n".join(problems) + "\n")
        sys.stderr.write(
            "\nAn increase means a per-kind lookup or a resolver re-entry was added:\n"
            "route it through the primitive instead. A decrease is progress -- re-pin\n"
            "with `make lane-check ARGS=--update` and commit the golden with the\n"
            "change that moved it.\n")
        return 1

    print("lane-gate: OK -- LOOKUP = %d (%d files), REENTRY = %d (%d files)"
          % (live_totals["LOOKUP"], len(counts["LOOKUP"]),
             live_totals["REENTRY"], len(counts["REENTRY"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
