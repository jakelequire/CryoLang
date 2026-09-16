#!/usr/bin/env python3
"""Pin the resolution-lane SURFACE against a committed golden.

docs/name-resolution.md §7.2 mechanism 5 requires one `resolve_path(segments,
ns, scope)` and three locks to keep it one.  Two of those locks cannot hold on
their own:

  * privatizing the per-kind lookups stops a direct call from `sema`, but not
    someone adding a NEW public wrapper beside them;
  * deleting a string-keyed entity lookup stops that one, but not a helper
    being reintroduced under another name.

Only a ratchet catches growth.  This is that ratchet: a committed golden, drift
fails, and `--update` re-pins deliberately.

WHAT IS PINNED
--------------
Each count is broken down per file so a failure names what moved.

The five per-kind lookups (`lookup_type`, `lookup_func_return`,
`lookup_func_type`, `lookup_global`, `lookup_method_return`) are counted
outside the file that DEFINES them, and SPLIT BY THE RECEIVER that answers
them, because the five names are not the index's alone:

  * LOOKUP -- answered by the `DeclarationIndex` under one of the five.  This
    is the lane surface,
    and the only one of the three that should fall.
  * LOOKUP_OTHER -- answered by the `DeclarationIndex` under any other
    `lookup_*` name.  The index answers under sixteen of them, not five, and
    the five carried 65 of the 122 external index lookups in this tree: over
    half the surface the gate is named for was not on it.  A caller can leave
    a pinned row by switching to an unpinned name, and the pinned row then
    falls, which reads as exactly the progress this gate was built to
    distinguish from regrowth.  The name rule is mechanical (`lookup_` prefix)
    so that what is counted is not a matter of opinion.
  * LOOKUP_ROUTED -- answered by a `TypeUtils` wrapper.  Already at the
    destination, so it RISES as LOOKUP falls and is not a target; it is pinned
    because a new same-named wrapper is exactly the regrowth this gate exists
    to catch.
  * LOOKUP_LOCAL -- a type's OWN same-named method over its own symbol map or
    scope stack.  Not a lane site, and no migration can remove one, so it is a
    FLOOR: driving LOOKUP to zero is reachable, driving the total to zero never
    was.
  * LOOKUP_ARENA -- `lookup_by_name` on the TypeArena outside the file that
    defines it.  The arena's name caches are a second name-keyed store of the
    same declared types the index holds, and a lookup there is the same lane
    under a receiver none of the rows above can see: type resolution asked it
    at 17 sites, seven of them a qualified-miss→bare retry, and the gate read
    OK over every one.  Pinned so a name lookup cannot leave the index for
    the arena and read as progress.
  * REENTRY -- calls to `get_resolver()` outside the driver, and a cursor move
    on a resolver reached through a FIELD (`.name_resolver.set_module(...)`,
    `find_module_scope`, `restore_scope`), which is the same re-entry with no
    `get_resolver()` to count: the monomorphizer swaps the resolver's module
    that way, and a gate reading `get_resolver()` alone reported 3 over a tree
    holding 6.  §7.2's corollary: name resolution is a pass, not a service,
    and a stage that can call back into the resolver will.  A resolver called
    from `sema` no longer has the writer's imports in hand, so it answers from
    a string -- which is the mechanical origin of B1.

Every row is asserted exactly, in both directions.  For LOOKUP and REENTRY an
increase is the regrowth this exists to catch, and a decrease is progress that
still fails, because a silently-tolerated decrease leaves the old higher number
as the bound and lets a later regression climb back to it unnoticed.  A row
that is a destination or a floor has no preferred direction and is asserted for
the same reason: an unexplained move is what the gate is for, and it is the
reason `--update` exists rather than a tolerance.

WHY THIS GATE IS SOURCE-DERIVED AND HAS NO PER-HOST SECTIONS
------------------------------------------------------------
A gate that measures a BUILD has numbers that move with which stdlib modules
the host compiles, and needs a `[host:...]` section per host.  This gate
counts CALL SITES IN THE SOURCE.  The same tree gives the same answer on every
host, so a per-host split would encode a dimension that does not exist and
would let one host's re-pin hide another's regression.  It also means this gate
needs no compiler, no stdlib, and no successful link -- it runs on a fresh
clone in under a second, which is what makes it usable as a pre-commit check.

THREE THINGS A NAIVE GREP GETS WRONG, ALL OBSERVED HERE
--------------------------------------------------------
  * THE RECEIVER.  Matching `.lookup_type(` matches the NAME, so a call
    already routed through `TypeUtils` counted exactly like a raw index call,
    and a `lookup_type(&this, name)` over a local scope stack counted as one
    too though it never touches the index.  A single total therefore could not
    say what it was a total OF, and a migration measured against it partly
    rewarded renaming.  The split above is what fixes that; an unplaceable
    receiver is a hard failure, because a call this gate cannot classify is one
    it cannot pin.
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
# The same five names, captured WITH the receiver that answers them.
RECEIVER_RE = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.(?:%s)\s*\(" % "|".join(LOOKUPS))


def lookup_bucket(receiver):
    """Which surface a call belongs to, decided by its RECEIVER.

    The five names are not the index's alone.  `TypeUtils` carries same-named
    wrappers, so a call already routed through the funnel matched exactly like
    a raw index call; and `move_check`, `drop_insertion` and `ir_generator`
    each define their own `lookup_type(&this, name)` over a local symbol map
    or a scope stack, which has nothing to do with the `DeclarationIndex`.

    A count matched on the NAME cannot separate the surface from things that
    merely resemble it, and the consequence was not academic: one total read
    as a migration target when a fifth of it was already at its destination
    and a tenth of it could never move.  Splitting by receiver is what makes
    each row mean what its heading says.

    Returns None for a receiver it cannot place, which the caller treats as a
    hard failure rather than dropping - an uncounted call is the one outcome a
    ratchet must never produce.
    """
    if receiver == "di" or receiver.endswith("decl_index"):
        return "LOOKUP"
    if receiver == "types" or receiver.endswith(".types"):
        return "LOOKUP_ROUTED"
    if receiver == "this":
        return "LOOKUP_LOCAL"
    return None
REENTRY_RE = re.compile(
    r"\bget_resolver\s*\(\s*\)"
    r"|\.name_resolver\.(?:set_module|find_module_scope|restore_scope)\s*\(")
# The one door that turns a name into a resolution answer, and the one that
# turns an answer back into a name.  `DefId`'s field is private, so the literal
# cannot be written outside the type and every crossing goes through these two.
DEFID_MINT_RE = re.compile(r"DefId::of_definition\s*\(")
# The leading dot is what keeps the MODULE `compiler::resolver::qualified_name`
# out: a module is reached with `::` and an import names it bare, so neither
# can match, while a receiver can only be a value.
DEFID_UNWRAP_RE = re.compile(r"\.qualified_name\s*\(")

# A ResolutionContext being told which module its annotations were WRITTEN
# in.  Every writer is a place where a stage re-resolves syntax away from the
# pass that walked it, and the module it hands over is what decides which
# same-leaf declaration a bare name binds to: a home taken from the ambient
# cursor binds a name to whichever module the compiler is standing in.  The
# definition line has no receiver, so `.set_home_module(` matches calls only.
HOME_WRITE_RE = re.compile(r"\.set_home_module\s*\(")

# ANY lookup on the index, so the five cannot be routed around by a sixth.
ANY_LOOKUP_RE = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.(lookup_[A-Za-z_0-9]*)\s*\(")
# The arena's name-keyed lookup, with its receiver.  `lookup(id)` is not a
# name lookup and is not matched.
ARENA_LOOKUP_RE = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.lookup_by_name\s*\(")

# Every counted population, in the order they are rendered and compared.
KINDS = ("LOOKUP", "LOOKUP_OTHER", "LOOKUP_ROUTED", "LOOKUP_LOCAL",
         "LOOKUP_ARENA", "REENTRY", "HOME_WRITE", "DEFID_MINT", "DEFID_UNWRAP")

# The file that DEFINES the five lookups.  Its own calls are not the surface.
#
# Matched on the path relative to compiler/src, not on the basename.  A
# basename exclusion is a rule about a NAME: a second decl_index.cryo anywhere
# in the tree would have its calls silently dropped, and a gate whose blind
# spot can be created by naming a file is not one that can be trusted about a
# count.  The exclusion is meant to be about one specific definition site, so
# it names one.
LOOKUP_OWNERS = {"compiler/decl_index.cryo"}
# The file that DEFINES the arena's name lookup; its own calls are its caches.
ARENA_OWNERS = {"compiler/types/arena.cryo"}
# The driver legitimately owns the resolver and may ask for it.
REENTRY_OWNERS = {"compiler/instance.cryo"}


def strip_comment(line):
    """Drop a `//` comment tail, so a call named only in prose is not counted.

    Deliberately naive about `//` inside a string literal: no such line exists
    in this tree, and a gate that silently counted one would be worse than one
    that fails loudly when it appears.
    """
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def scan():
    """Return ({kind: {relpath: count}}, unplaced) over the compiler sources."""
    found = {k: {} for k in KINDS}
    unplaced = []
    for dirpath, _dirs, files in os.walk(SRC):
        for fname in sorted(files):
            if not fname.endswith(".cryo"):
                continue
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, SRC).replace(os.sep, "/")
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.readlines()
            tally = {k: 0 for k in KINDS}
            for lineno, raw in enumerate(text, 1):
                line = strip_comment(raw)
                if not line.strip():
                    continue
                if rel not in LOOKUP_OWNERS:
                    seen = len(LOOKUP_RE.findall(line))
                    accounted = 0
                    for m in RECEIVER_RE.finditer(line):
                        accounted += 1
                        bucket = lookup_bucket(m.group(1))
                        if bucket is None:
                            unplaced.append((rel, lineno, m.group(1)))
                            continue
                        tally[bucket] += 1
                    # A match the receiver pattern did not reach at all - a call
                    # on something other than a dotted name. Reported once, not
                    # once per match, so the count is of CALLS and not of checks.
                    for _ in range(seen - accounted):
                        unplaced.append((rel, lineno, "<no simple receiver>"))
                    # Every OTHER lookup_* on the index.  Mechanical rule, no
                    # judgement about which names are "real" lookups: the index
                    # answers under more than five names, and a migration
                    # measured only against the five rewards moving to a sixth.
                    for m in ANY_LOOKUP_RE.finditer(line):
                        if m.group(2) in LOOKUPS:
                            continue
                        if lookup_bucket(m.group(1)) == "LOOKUP":
                            tally["LOOKUP_OTHER"] += 1
                if rel not in ARENA_OWNERS:
                    # A name lookup on the arena is counted by its receiver
                    # too: one that is not an arena is a lookup this gate
                    # has no row for, and is refused rather than dropped.
                    for m in ARENA_LOOKUP_RE.finditer(line):
                        recv = m.group(1)
                        if recv == "arena" or recv.endswith(".arena") \
                                or recv.endswith("_arena"):
                            tally["LOOKUP_ARENA"] += 1
                        else:
                            unplaced.append((rel, lineno, recv))
                if rel not in REENTRY_OWNERS:
                    tally["REENTRY"] += len(REENTRY_RE.findall(line))
                tally["HOME_WRITE"] += len(HOME_WRITE_RE.findall(line))
                tally["DEFID_MINT"] += len(DEFID_MINT_RE.findall(line))
                tally["DEFID_UNWRAP"] += len(DEFID_UNWRAP_RE.findall(line))
            for kind in KINDS:
                if tally[kind]:
                    found[kind][rel] = tally[kind]
    return found, unplaced


HEADER = [
    "# Resolution-lane surface baseline -- docs/name-resolution.md §7.2 mechanism 5.",
    "#",
    "# ASSERTED: both totals and every per-file row.",
    "#",
    "# The five per-kind lookups outside decl_index.cryo, which defines them,",
    "# split by the RECEIVER that answers them - the names are not the index's",
    "# alone, and a name-matched total cannot say what it is a total of.",
    "#",
    "# LOOKUP         answered by the DeclarationIndex, under one of the five",
    "#                names mechanism 5 gives. The lane surface; falls.",
    "# LOOKUP_OTHER   answered by the DeclarationIndex under ANY OTHER lookup_*",
    "#                name. The index answers under sixteen, not five, and a",
    "#                surface pinned at five is one a caller can leave by",
    "#                switching names - which reads as progress on the row that",
    "#                is watched. Same receiver rule, mechanical name rule.",
    "# LOOKUP_ROUTED  answered by a TypeUtils wrapper. Already at the destination,",
    "#                so it RISES as LOOKUP falls. Pinned because a new same-named",
    "#                wrapper is the regrowth this gate exists to catch.",
    "# LOOKUP_LOCAL   a type's OWN same-named method over its own symbol map or",
    "#                scope stack. Not a lane site; no migration removes one. A",
    "#                FLOOR, so driving LOOKUP to zero is reachable and driving",
    "#                the total to zero never was.",
    "# LOOKUP_ARENA   lookup_by_name on the TypeArena outside arena.cryo. A",
    "#                second name-keyed store of the declared types the index",
    "#                holds, so the same lane under a receiver the rows above",
    "#                cannot see. Falls with LOOKUP; a move between the two is",
    "#                not progress.",
    "# REENTRY  get_resolver() outside the driver, and a cursor move on a resolver",
    "#          reached through a field (.name_resolver.set_module / find_module_scope",
    "#          / restore_scope), the same re-entry with nothing else to count. Name",
    "#          resolution is a PASS, not a service: a resolver called from sema no",
    "#          longer holds the writer's imports, so it answers from a string. That",
    "#          is where B1 comes from.",
    "# HOME_WRITE    ResolutionContext::set_home_module() calls -- every place a",
    "#               stage re-resolves syntax and says which module WROTE it.",
    "#               The module handed over decides which same-leaf declaration",
    "#               a bare name binds to, so a writer that hands over the",
    "#               ambient cursor binds by where the compiler stands. A new",
    "#               writer is a new such decision and must be placed.",
    "# DEFID_MINT    DefId::of_definition() -- where a name BECOMES a resolution",
    "#               answer. Legitimate only where the referent is known for a",
    "#               reason other than the spelling in front of it: a resolver",
    "#               that just walked to the declaration, or a read of the",
    "#               declaration index.",
    "# DEFID_UNWRAP  DefId::qualified_name() -- where an answer becomes a name",
    "#               again. Legitimate where the output IS text (a diagnostic, a",
    "#               mangled symbol); a re-keyed lookup here has re-derived what",
    "#               it was handed, and should take the DefId instead.",
    "#",
    "# Those two exist because Cryo's visibility is scoped to the declaring TYPE",
    "# rather than to a module: a constructor private enough to exclude codegen",
    "# excludes the resolver too, so DefId cannot be made mintable by the resolver",
    "# ALONE. The private field still buys one named door in each direction, and",
    "# these rows are what make the traffic through them countable.",
    "#",
    "# Every row is asserted exactly, both directions. For LOOKUP and REENTRY an",
    "# increase is regrowth and a decrease is progress that must still be re-pinned",
    "# with `make lane-check ARGS=--update`, because a tolerated decrease leaves",
    "# the old number as the ceiling. A destination or a floor row has no preferred",
    "# direction and is pinned so that an unexplained move fails.",
    "#",
    "# Source-derived, so there are no per-host sections: the same tree gives the",
    "# same answer everywhere, and splitting by host would let one host's re-pin",
    "# hide another's regression.",
]


def render(counts):
    lines = list(HEADER)
    for kind in KINDS:
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
    counts = {k: {} for k in KINDS}
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

    counts, unplaced = scan()
    if unplaced:
        sys.stderr.write(
            "lane-gate: %d lookup call(s) could not be placed by receiver.\n"
            "A call this gate cannot classify is a call it cannot pin, and a\n"
            "silently dropped one reads as progress. Extend lookup_bucket().\n"
            % len(unplaced))
        for rel, lineno, recv in unplaced:
            sys.stderr.write("  %s:%d  receiver %s\n" % (rel, lineno, recv))
        return 1
    live_totals = {k: sum(v.values()) for k, v in counts.items()}

    if args.update:
        with open(GOLDEN, "w", encoding="utf-8", newline=chr(10)) as fh:
            fh.write(render(counts))
        print("lane-gate: golden updated -- "
              + ", ".join("%s = %d" % (k, live_totals[k]) for k in KINDS))
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
    for kind in KINDS:
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

    print("lane-gate: OK -- "
          + ", ".join("%s = %d (%d files)" % (k, live_totals[k], len(counts[k]))
                      for k in KINDS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
