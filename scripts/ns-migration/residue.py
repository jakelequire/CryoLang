#!/usr/bin/env python3
"""Enumerate every name-keyed READ in compiler/src: a call that hands a
SPELLING (`SymbolStr` / `string`) into a method of a declaration-holding
type and gets a declaration, a member or a fact about one back.

This is D32's population (docs/name-resolution.md §0.1): the sites the
residue `scripts/ns-migration/residue.md` must account for, one by one.
The population is derived, not listed: the same parser `scripts/lane-gate.py`
runs on every commit builds it (its stores, the map and array owners it
places, its receiver placement by declared type), so a reader added under
any spelling on any store is in the population the moment it is declared.

A site is in the population when
  * the method is declared by a STORE (lane-gate's rule 1 tables: the
    types that hold declarations under a name) or by an ARRAY OWNER the
    gate places as "a member table inside its owner" (rule 1b's exclusions
    of that one kind - an impl's or a trait's own members by leaf, D32's
    justified class); an array owner excluded for holding paths, flags,
    texts or a pass's own rib holds no declaration and is outside;
  * the method takes a key type in PARAMETER position (the caller hands a
    name in; a method that only RETURNS a name is a display, not a read);
  * the method is a read (`&this` or static; a `mut &this` registrar is the
    write side, which D32 does not count);
  * the call is outside the declaring type's own file (a store's own calls
    are its machinery, as lane-gate has it).

Every site prints as one tab-separated row:
    file<TAB>line<TAB>receiver-type<TAB>method<TAB>argument text
`--count` prints the size alone; `--check [residue.md]` reads the residue's
site table (`residue_classify.py` writes it) and refuses when the tree's
population and the table differ in either direction - matched by (file,
method, key text), so a line shift is not drift and a new or re-keyed site
is - when a row's class is not the one `residue_classify.py` derives for
that site (a class is a judgement held as code; the list is its rendering,
and a list that says otherwise is the list that is wrong), or when a class
letter is one the classifier has no meaning for; on OK it prints the count
per class FROM THE CLASSIFIER, so the count pinned in §0 is checked against
the tree, against the list and against the code that decides it.  A list
whose one J row had been flipped to N by hand read `OK ... J 51, N 118`
through a check that summed the letters as written.

Two controls the gate has and this enumerator did without, both refused
now: a call to a population name that neither receiver pattern reaches
(`(this.ctx.decl_index).lookup_type(`, a receiver with arguments) is
refused as unplaceable rather than not counted; and every dotted call to a
population name whose receiver is placed on a type that is NOT a holder
(`DropInserter::lookup_type`, `ScopeManager::lookup_local`, ...) is
tallied per (type, method) into the list's second table, so a holder
misplaced as something else moves a pinned number instead of vanishing -
the gate's LOOKUP_LOCAL row.  A static call's owner is spelled and cannot
be misplaced, so statics on non-holders are not tallied.
"""
import argparse
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")
DEFAULT_RESIDUE = os.path.join(HERE, "residue.md")
# lane-gate places an array owner whose array is a MEMBER TABLE inside its
# owner - an impl's `This::Member` bindings, a trait's own member names -
# with this phrase in its reason.  Those are D32's justified class and are
# in the population; an array owner excluded for any other reason (a file
# path, a flag, a diagnostic text, a pass's own rib) holds no declaration
# and is not.
MEMBER_TABLE = "a member table inside its owner"


def load_gate():
    spec = importlib.util.spec_from_file_location("lane_gate", GATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_methods(gate, tree, type_name, defn):
    """{name} of `type_name`'s methods that take a key type as a PARAMETER
    and are reads (not `mut &this`): lane-gate's rule 2 narrowed to the
    direction D32 counts, using rule 1b's parameter test."""
    taking = gate.name_taking_methods(tree, type_name)
    kinds = {}
    head_re = re.compile(r"^type\s+(?:struct|class|union|enum)\s+%s\b" % re.escape(type_name))
    for rel in tree.rels:
        lines = tree.files[rel]
        for i, line in enumerate(lines):
            code = gate.strip_comment(line)
            m = gate.INHERENT_IMPL_RE.match(code)
            if head_re.match(code) is None and (m is None or m.group(1) != type_name):
                continue
            kinds.update(gate.block_methods(lines, i))
    return {n for n in taking if kinds.get(n) in ("read", "static")}


def argument_text(line, start):
    """The text between the `(` at `start` and its matching `)`, one line."""
    depth = 0
    for i in range(start, len(line)):
        ch = line[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return line[start + 1:i].strip()
    return line[start + 1:].strip() + " ..."


def population(gate, src):
    tree = gate.Tree(src)
    gate.place_map_owners(tree)
    gate.place_array_owners(tree)
    # The types that hold something under a name: every store, and every
    # placed array owner (rule 1b's candidates, each already in
    # EXCLUDED_ARRAYS with its reason).  Their declaring files.
    holders = {}
    for name, st in gate.STORES.items():
        holders[name] = st.defn
    members = 0
    for name, (rels, _fields, _methods) in gate.array_candidates(tree).items():
        ex = gate.EXCLUDED_ARRAYS.get(name)
        if ex is not None and MEMBER_TABLE in ex.reason:
            holders[name] = rels[0]
            members += 1
    if members == 0:
        raise SystemExit("residue: no array owner is placed as `%s`; the gate's "
                         "placement has changed shape and this enumerator has not" % MEMBER_TABLE)
    sets = {}
    for name, defn in holders.items():
        try:
            sets[name] = read_methods(gate, tree, name, defn)
        except SystemExit:
            sets[name] = set()
    names = sorted(set().union(*sets.values()), key=len, reverse=True)
    if not names:
        raise SystemExit("residue: no name-taking read on any holder; the parser has not measured the tree")
    seen_re, dotted_re, cast_re, static_re = gate.call_patterns(names)
    rows = []
    unplaced = []
    unreached = []
    elsewhere = {}
    for rel in tree.rels:
        for lineno, raw in enumerate(tree.files[rel], 1):
            line = gate.strip_comment(raw)
            if not line.strip():
                continue
            seen = len(seen_re.findall(line))
            accounted = 0
            placed = [(m, m.group(1), m.group(2), tree.receiver_type(rel, lineno, m.group(1)))
                      for m in dotted_re.finditer(line)]
            placed += [(m, m.group(0), m.group(2), m.group(1)) for m in cast_re.finditer(line)]
            for m, recv, name, ty in placed:
                accounted += 1
                if ty is None:
                    # A call this enumerator cannot place is one it cannot
                    # count, and a silently dropped one reads as a shorter
                    # list; lane-gate refuses the same way.
                    unplaced.append((rel, lineno, recv, name))
                    continue
                if ty not in sets:
                    # A population name on a type that holds nothing: not a
                    # site, but tallied so a holder misplaced as one shows.
                    elsewhere[(ty, name)] = elsewhere.get((ty, name), 0) + 1
                    continue
                if name not in sets[ty]:
                    continue
                if rel == holders[ty] or (ty in gate.STORES and gate.STORES[ty].owns(rel)):
                    continue
                rows.append((rel, lineno, ty, name, argument_text(line, m.end() - 1)))
            for m in static_re.finditer(line):
                accounted += 1
                owner, name = m.group(1), m.group(2)
                if owner not in sets or name not in sets[owner]:
                    continue
                if rel == holders[owner] or (owner in gate.STORES and gate.STORES[owner].owns(rel)):
                    continue
                rows.append((rel, lineno, owner, name, argument_text(line, m.end() - 1)))
            # A call to a population name on something no pattern reaches: a
            # receiver with arguments, a parenthesized expression.  Refused,
            # as the gate refuses it - unreached is uncounted.
            for _ in range(seen - accounted):
                unreached.append((rel, lineno, line.strip()))
    if unplaced:
        raise SystemExit("residue: %d call(s) to a read method on a receiver whose type "
                         "cannot be read, e.g. %s:%d `%s.%s(`; place it or the count is short"
                         % (len(unplaced), unplaced[0][0], unplaced[0][1], unplaced[0][2], unplaced[0][3]))
    if unreached:
        raise SystemExit("residue: %d call(s) to a read method on no simple receiver, "
                         "e.g. %s:%d `%s`; a call no pattern reaches is a call this "
                         "enumerator cannot count"
                         % (len(unreached), unreached[0][0], unreached[0][1], unreached[0][2][:120]))
    return rows, sets, elsewhere


# A row of the residue's site table: `file:line` | `Holder::method` | `key` | class | reason.
# The list is matched to the tree by (file, method, key text), not by line,
# so an edit that only moves a file's lines does not read as drift; a site
# added, removed or re-keyed does.  The class letter is one character of any
# kind, so a letter the classifier does not know is read here and refused
# there instead of falling out of the table unseen.
SITE_RE = re.compile(r"^\|\s*`([^`]+):(\d+)`\s*\|\s*`([^`]+)`\s*\|\s*(?:`(.*?)`)?\s*\|\s*(\S+)\s*\|")
# A row of the list's second table: `Type::method` | calls - the dotted
# calls to a population name whose receiver is placed on a type that holds
# nothing (the gate's LOOKUP_LOCAL, pinned here per (type, method)).
ELSEWHERE_RE = re.compile(r"^\|\s*`([^`]+)::([^`]+)`\s*\|\s*(\d+)\s*\|")
ELSEWHERE_BEGIN = "<!-- residue-elsewhere:begin -->"
ELSEWHERE_END = "<!-- residue-elsewhere:end -->"


def residue_sites(path):
    """{(file, method, key): [class, ...]} from the site table and
    {(type, method): calls} from the elsewhere table, as the list has them."""
    sites = {}
    elsewhere = {}
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    b, e = text.find(ELSEWHERE_BEGIN), text.find(ELSEWHERE_END)
    other = text[b:e] if b >= 0 and e > b else ""
    for line in text.splitlines():
        m = SITE_RE.match(line)
        if m:
            key = (m.group(1), m.group(3), (m.group(4) or "").replace("\\|", "|"))
            sites.setdefault(key, []).append(m.group(5))
    for line in other.splitlines():
        m = ELSEWHERE_RE.match(line)
        if m:
            elsewhere[(m.group(1), m.group(2))] = int(m.group(3))
    return sites, elsewhere


def check(rows, elsewhere, path, out=print):
    """The list at `path` against the tree's `rows` and the classifier's
    verdict on each: every site present in both, no more than once each
    way; every row's class the one the classifier derives for it; every
    class letter one the classifier defines; the elsewhere table equal to
    the tree's tally.  Prints each disagreement and returns 0 on OK, 1 on
    any refusal.  The counts it prints on OK are the classifier's."""
    import residue_classify as rc
    listed, listed_elsewhere = residue_sites(path)
    classified, unknown = rc.classify(rows)
    if unknown:
        out("residue: read methods called in the tree with no class in residue_classify.py: %s"
            % ", ".join(sorted(unknown)))
        return 1
    tree = {}
    for rel, _l, key, arg, cls, _reason in classified:
        tree.setdefault((rel, key, arg), []).append(cls)
    drift = False
    for k in sorted(set(tree) | set(listed)):
        if len(tree.get(k, ())) != len(listed.get(k, ())):
            drift = True
            out("residue: %s %s (%s): tree %d, list %d"
                % (k[0], k[1], k[2], len(tree.get(k, ())), len(listed.get(k, ()))))
    if drift:
        out("residue: DRIFT - tree %d, list %d; re-run residue_classify.py and review the rows it changes"
            % (sum(len(v) for v in tree.values()), sum(len(v) for v in listed.values())))
        return 1
    # The class is the classifier's; the list only renders it.  Every letter
    # in the list is checked against the letter the code derives for that
    # site, so a row edited by hand - flipped, blanked, given a letter the
    # classes do not include - is refused rather than summed.
    mismatched = 0
    for k in sorted(tree):
        want = tree[k][0]
        for got in listed[k]:
            if got == want:
                continue
            mismatched += 1
            if got not in rc.CLASSES:
                out("residue: %s %s (%s): the list says `%s`, which is no class (%s); the classifier says %s"
                    % (k[0], k[1], k[2], got, rc.CLASSES, want))
            else:
                out("residue: %s %s (%s): the list says %s, the classifier says %s"
                    % (k[0], k[1], k[2], got, want))
    if mismatched:
        out("residue: %d row(s) whose class is not the classifier's; a class is a judgement held in "
            "residue_classify.py - change it there, with its reason, and regenerate the list" % mismatched)
        return 1
    moved = False
    for k in sorted(set(elsewhere) | set(listed_elsewhere)):
        if elsewhere.get(k, 0) != listed_elsewhere.get(k, 0):
            moved = True
            out("residue: elsewhere %s::%s: tree %d, list %d"
                % (k[0], k[1], elsewhere.get(k, 0), listed_elsewhere.get(k, 0)))
    if moved:
        out("residue: ELSEWHERE - a population name on a type that holds nothing moved (tree %d, list %d); "
            "a holder misplaced as another type lands here - place it, or regenerate the list and say why"
            % (sum(elsewhere.values()), sum(listed_elsewhere.values())))
        return 1
    counts = {}
    for row in classified:
        counts[row[4]] = counts.get(row[4], 0) + 1
    # The convertible remainder is every class that is neither justified
    # (J) nor outside D32 (N); the migration is complete at 0.
    convertible = sum(n for c, n in counts.items() if c not in ("J", "N"))
    out("residue: OK -- %d sites, list, tree and classifier agree; %s; convertible %d; elsewhere %d"
        % (len(classified), ", ".join("%s %d" % (c, counts[c]) for c in sorted(counts)),
           convertible, sum(elsewhere.values())))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(ROOT, "compiler", "src"))
    ap.add_argument("--count", action="store_true", help="print the population size alone")
    ap.add_argument("--methods", action="store_true", help="print the read methods per holder")
    ap.add_argument("--elsewhere", action="store_true",
                    help="print the population names called on types that hold nothing, per (type, method)")
    ap.add_argument("--check", nargs="?", const=DEFAULT_RESIDUE, metavar="RESIDUE_MD",
                    help="refuse unless the tree's population, the residue's site table and the classifier agree")
    args = ap.parse_args()
    gate = load_gate()
    rows, sets, elsewhere = population(gate, args.src)
    if args.methods:
        for holder in sorted(sets):
            if sets[holder]:
                print("%s: %s" % (holder, ", ".join(sorted(sets[holder]))))
        return 0
    if args.elsewhere:
        for (ty, name), n in sorted(elsewhere.items()):
            print("%s::%s\t%d" % (ty, name, n))
        return 0
    if args.check:
        sys.path.insert(0, HERE)
        return check(rows, elsewhere, args.check)
    if args.count:
        print(len(rows))
        return 0
    for rel, lineno, ty, name, arg in rows:
        print("%s\t%d\t%s\t%s\t%s" % (rel, lineno, ty, name, arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
