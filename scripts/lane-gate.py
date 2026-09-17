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

THE RULE IS DERIVED FROM THE DEFINITION, NOT FROM A LIST OF NAMES.  A
name-keyed reader of the `DeclarationIndex` is any method the index declares
whose signature mentions `SymbolStr` - as a parameter (the caller hands a
name in and gets an answer) or as the return type (the caller hands an
answer in and gets a name back, which is the same boundary crossed the other
way).  The set is read from `decl_index.cryo` every run, and the same rule
read from `type_utils.cryo` gives the `TypeUtils` funnel's set.  A gate that
pins readers by a NAME PATTERN (`lookup_*`) is blind by construction to a
reader called anything else, and the tree held twenty-four such calls
(`is_candidate_public`, `namespace_of`, `ns_imports`, `find_global_*`,
`intrinsic_owner_of`, `resolve_method_owner`, ...) under a gate that read OK;
a reader added tomorrow as `owner_of(name)` is inside this rule the moment
it is declared.  The signature is the one thing a name-keyed reader cannot
be written without.

The calls are SPLIT BY THE RECEIVER that answers them, because the names are
not the index's alone:

  * LOOKUP -- answered by the `DeclarationIndex` under one of the five
    per-kind names mechanism 5 gives (`lookup_type`, `lookup_func_return`,
    `lookup_func_type`, `lookup_global`, `lookup_method_return`).  This is
    the lane surface, and the only one of the rows that should fall.
  * LOOKUP_OTHER -- answered by the `DeclarationIndex` under any OTHER
    name-crossing method that READS (`&this`, or a static): the registry's
    entry accessors, the visibility and reachability questions, the global
    and extern tables, the intrinsic owner.  A caller can leave the pinned
    LOOKUP row by switching to one of these, and the pinned row then falls,
    which reads as exactly the progress this gate was built to distinguish
    from regrowth.
  * REGISTER -- a name-keyed WRITE to the `DeclarationIndex` (`mut &this`):
    every registrar that is handed a key the CALLER derived from a
    declaration it holds.  The migration's other half - the key derived at
    registration and nowhere else - falls here, and a registrar that grows a
    second name-keyed door is caught the same way a reader is.
  * LOOKUP_ROUTED -- answered by a `TypeUtils` wrapper, any name-crossing
    method that type declares.  Already at the destination, so it RISES as
    LOOKUP falls and is not a target; it is pinned because a new wrapper is
    exactly the regrowth this gate exists to catch, and a wrapper under a
    sixth name was one the five-name rule could not see.
  * LOOKUP_LOCAL -- a name from either set called on `this` OUTSIDE the file
    that defines the set: a type's OWN same-named method over its own symbol
    map or scope stack (`move_check`, `drop_insertion`).  Not a lane site,
    and no migration can remove one, so it is a FLOOR: driving LOOKUP to
    zero is reachable, driving the total to zero never was.
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
  * THE OWNER'S OWN CALLS.  `decl_index.cryo` defines the index's methods and
    calls them internally; `type_utils.cryo` does the same for the funnel's.
    Those are not the surface this gate is about -- the surface is what OTHER
    stages reach for -- so each defining file is excluded from its own set
    (and only its own: the funnel's calls INTO the index are counted).
    Excluding it by name is safe here precisely because it is the definition
    site, not a special case about some caller.

A row that reaches zero is DELETED from the golden rather than pinned at 0, so
the golden reads as the live surface rather than a graveyard; a file reappearing
is then an added row, which fails the same way an increase does.

Usage:
    python3 scripts/lane-gate.py [--update] [--names]

`--names` prints the two derived sets and exits, so what the rule swept up
can be read rather than inferred.

Exit codes: 0 match (or golden updated); 1 drift.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "compiler", "src")
GOLDEN = os.path.join(ROOT, "tests", "lane-baseline.txt")

# The five per-kind lookups §7.2 mechanism 5 names: the LOOKUP row.  They are
# the only names this gate holds as a list, and the list decides which ROW a
# call lands in, never whether it is counted.
LOOKUPS = (
    "lookup_type",
    "lookup_func_return",
    "lookup_func_type",
    "lookup_global",
    "lookup_method_return",
)

# The two definition sites.  Each is the one file whose `type struct` carries
# the methods, and each is excluded from counting calls to ITS OWN set.
#
# Matched on the path relative to compiler/src, not on the basename.  A
# basename exclusion is a rule about a NAME: a second decl_index.cryo anywhere
# in the tree would have its calls silently dropped, and a gate whose blind
# spot can be created by naming a file is not one that can be trusted about a
# count.  The exclusion is meant to be about one specific definition site, so
# it names one.
INDEX_FILE = "compiler/decl_index.cryo"
INDEX_TYPE = "DeclarationIndex"
ROUTED_FILE = "compiler/sema/type_utils.cryo"
ROUTED_TYPE = "TypeUtils"
# The type whose signatures make a method name-keyed.
NAME_TYPE = "SymbolStr"

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

# The arena's name-keyed lookup, with its receiver.  `lookup(id)` is not a
# name lookup and is not matched.
ARENA_LOOKUP_RE = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.lookup_by_name\s*\(")

# Every counted population, in the order they are rendered and compared.
KINDS = ("LOOKUP", "LOOKUP_OTHER", "REGISTER", "LOOKUP_ROUTED", "LOOKUP_LOCAL",
         "LOOKUP_ARENA", "REENTRY", "HOME_WRITE", "DEFID_MINT", "DEFID_UNWRAP")

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


STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
METHOD_HEAD_RE = re.compile(r"^    (static\s+)?([a-z_][a-z_0-9]*)\s*(?:<[^>]*>)?\s*\(")


def name_crossing_methods(rel, type_name):
    """{name: "read" | "write" | "static"} for every method declared inline in
    `type struct <type_name> { ... }` in `rel` whose head mentions NAME_TYPE.

    The head is everything from the method's name to the `{` that opens its
    body, joined across lines.  A method is a WRITE when its receiver is
    `mut &this`, STATIC when it has none, and a READ otherwise.

    Refuses rather than returning an empty set: a parser that finds no
    struct, or a struct with no name-crossing method, has not measured the
    tree, and a gate fed an empty set would report OK over every call.
    """
    path = os.path.join(SRC, rel.replace("/", os.sep))
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    head_re = re.compile(r"^type struct %s\b" % re.escape(type_name))
    i = 0
    while i < len(lines) and not head_re.match(lines[i]):
        i += 1
    if i == len(lines):
        raise SystemExit("lane-gate: no `type struct %s` in %s" % (type_name, rel))
    found = {}
    depth = 0
    entered = False
    while i < len(lines):
        code = STRING_RE.sub('""', strip_comment(lines[i]))
        m = METHOD_HEAD_RE.match(code)
        if m and depth == 1:
            head = code
            j = i
            while "{" not in head or head.count("(") > head.count(")"):
                j += 1
                if j == len(lines):
                    raise SystemExit("lane-gate: unterminated method head at %s:%d"
                                     % (rel, i + 1))
                head += " " + STRING_RE.sub('""', strip_comment(lines[j])).strip()
            head = head[:head.index("{")]
            if NAME_TYPE in head:
                if m.group(1):
                    found[m.group(2)] = "static"
                elif "mut &this" in head:
                    found[m.group(2)] = "write"
                else:
                    found[m.group(2)] = "read"
        for ch in code:
            if ch == "{":
                depth += 1
                entered = True
            elif ch == "}":
                depth -= 1
        if entered and depth == 0:
            break
        i += 1
    if not found:
        raise SystemExit("lane-gate: `type struct %s` in %s declares no method "
                         "whose signature mentions %s; the parser has not "
                         "measured the tree" % (type_name, rel, NAME_TYPE))
    return found


def receiver_kind(receiver):
    """Which type a dotted receiver names, decided by its spelling.

    Returns "index", "routed", "this", or None for a receiver it cannot
    place, which the caller treats as a hard failure rather than dropping -
    an uncounted call is the one outcome a ratchet must never produce.
    """
    if receiver == "di" or receiver.endswith("decl_index"):
        return "index"
    if receiver == "types" or receiver.endswith(".types"):
        return "routed"
    if receiver == "this":
        return "this"
    return None


def scan():
    """Return ({kind: {relpath: count}}, unplaced) over the compiler sources."""
    index_set = name_crossing_methods(INDEX_FILE, INDEX_TYPE)
    routed_set = name_crossing_methods(ROUTED_FILE, ROUTED_TYPE)
    # Control on the parser: the LOOKUP row is the five names, and they are
    # declared on the index.  A parser that cannot see them cannot see the
    # row it is asked to pin.
    missing = [n for n in LOOKUPS if n not in index_set]
    if missing:
        raise SystemExit("lane-gate: %s does not declare %s as name-crossing; "
                         "the parser has not measured the tree"
                         % (INDEX_FILE, ", ".join(missing)))
    names = sorted(set(index_set) | set(routed_set), key=len, reverse=True)
    alt = "|".join(names)
    # Any call to a set name: a dotted receiver, a `Type::` static, or neither
    # (which is a call this gate cannot place and refuses).  A definition line
    # has no `.` or `::` before the name, so it is not a call.
    seen_re = re.compile(r"(?:\.|::)\s*(?:%s)\s*\(" % alt)
    dotted_re = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.(%s)\s*\(" % alt)
    static_re = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)::(%s)\s*\(" % alt)

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
                seen = len(seen_re.findall(line))
                accounted = 0
                for m in dotted_re.finditer(line):
                    accounted += 1
                    recv, name = m.group(1), m.group(2)
                    kind = receiver_kind(recv)
                    if kind == "index" and name in index_set:
                        if rel == INDEX_FILE:
                            continue
                        if name in LOOKUPS:
                            tally["LOOKUP"] += 1
                        elif index_set[name] == "write":
                            tally["REGISTER"] += 1
                        else:
                            tally["LOOKUP_OTHER"] += 1
                    elif kind == "routed" and name in routed_set:
                        if rel == ROUTED_FILE:
                            continue
                        tally["LOOKUP_ROUTED"] += 1
                    elif kind == "this":
                        # The owner's own call to its own method is not the
                        # surface; anywhere else, `this` is some other type
                        # with a same-named method of its own.
                        if (rel == INDEX_FILE and name in index_set) \
                                or (rel == ROUTED_FILE and name in routed_set):
                            continue
                        tally["LOOKUP_LOCAL"] += 1
                    else:
                        unplaced.append((rel, lineno, recv))
                for m in static_re.finditer(line):
                    accounted += 1
                    owner, name = m.group(1), m.group(2)
                    if owner == INDEX_TYPE and index_set.get(name) == "static":
                        if rel != INDEX_FILE:
                            tally["LOOKUP_OTHER"] += 1
                    elif owner == ROUTED_TYPE and routed_set.get(name) == "static":
                        if rel != ROUTED_FILE:
                            tally["LOOKUP_ROUTED"] += 1
                    else:
                        unplaced.append((rel, lineno, owner + "::"))
                # A match the receiver patterns did not reach at all - a call
                # on something other than a dotted name. Reported once, not
                # once per match, so the count is of CALLS and not of checks.
                for _ in range(seen - accounted):
                    unplaced.append((rel, lineno, "<no simple receiver>"))
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
    return found, unplaced, index_set, routed_set


HEADER = [
    "# Resolution-lane surface baseline -- docs/name-resolution.md §7.2 mechanism 5.",
    "#",
    "# ASSERTED: both totals and every per-file row.",
    "#",
    "# A name-keyed method is one whose signature mentions SymbolStr, read from",
    "# the type's definition on every run - not a list of names, so a reader",
    "# added under any spelling is inside the rule as soon as it is declared.",
    "# Calls are split by the RECEIVER that answers them, because the names are",
    "# not the index's alone and a name-matched total cannot say what it is a",
    "# total of.",
    "#",
    "# LOOKUP         answered by the DeclarationIndex, under one of the five",
    "#                names mechanism 5 gives. The lane surface; falls.",
    "# LOOKUP_OTHER   answered by the DeclarationIndex under ANY OTHER name-",
    "#                crossing READ (entry accessors, visibility, reachability,",
    "#                the global and extern tables, a static key parser). A",
    "#                surface pinned at five names is one a caller can leave by",
    "#                switching names - which reads as progress on the row that",
    "#                is watched.",
    "# REGISTER       a name-keyed WRITE to the DeclarationIndex: a registrar",
    "#                handed a key the CALLER derived from a declaration it",
    "#                holds. Falls as registration takes the DefId instead.",
    "# LOOKUP_ROUTED  answered by a TypeUtils wrapper, any name-crossing method",
    "#                that type declares. Already at the destination, so it",
    "#                RISES as LOOKUP falls. Pinned because a new wrapper is the",
    "#                regrowth this gate exists to catch, and one under a sixth",
    "#                name was invisible to a five-name rule.",
    "# LOOKUP_LOCAL   a set name called on `this` outside the defining file: a",
    "#                type's OWN same-named method over its own symbol map or",
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
    ap.add_argument("--names", action="store_true",
                    help="print the two definition-derived name sets and exit")
    args = ap.parse_args()

    counts, unplaced, index_set, routed_set = scan()
    if args.names:
        for label, names in ((INDEX_TYPE, index_set), (ROUTED_TYPE, routed_set)):
            print("%s (%d):" % (label, len(names)))
            for name in sorted(names):
                print("  %-6s %s" % (names[name], name))
        return 0
    if unplaced:
        sys.stderr.write(
            "lane-gate: %d name-keyed call(s) could not be placed by receiver.\n"
            "A call this gate cannot classify is a call it cannot pin, and a\n"
            "silently dropped one reads as progress. Extend receiver_kind().\n"
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
