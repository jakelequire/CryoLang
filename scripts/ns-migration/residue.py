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
is - or when a row is unclassified; on OK it prints the count per class, so
the count pinned in §0 is checked against the tree AND against the list.
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
    alt = "|".join(re.escape(n) for n in names)
    dotted_re = re.compile(r"%s\.(%s)\s*\(" % (gate.RECEIVER, alt))
    static_re = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)::(%s)\s*\(" % alt)
    rows = []
    unplaced = []
    for rel in tree.rels:
        for lineno, raw in enumerate(tree.files[rel], 1):
            line = gate.strip_comment(raw)
            if not line.strip():
                continue
            for m in dotted_re.finditer(line):
                recv, name = m.group(1), m.group(2)
                ty = tree.receiver_type(rel, lineno, recv)
                if ty is None:
                    # A call this enumerator cannot place is one it cannot
                    # count, and a silently dropped one reads as a shorter
                    # list; lane-gate refuses the same way.
                    unplaced.append((rel, lineno, recv, name))
                    continue
                if ty not in sets or name not in sets[ty]:
                    continue
                if rel == holders[ty] or (ty in gate.STORES and gate.STORES[ty].owns(rel)):
                    continue
                rows.append((rel, lineno, ty, name, argument_text(line, m.end() - 1)))
            for m in static_re.finditer(line):
                owner, name = m.group(1), m.group(2)
                if owner not in sets or name not in sets[owner]:
                    continue
                if rel == holders[owner] or (owner in gate.STORES and gate.STORES[owner].owns(rel)):
                    continue
                rows.append((rel, lineno, owner, name, argument_text(line, m.end() - 1)))
    if unplaced:
        raise SystemExit("residue: %d call(s) to a read method on a receiver whose type "
                         "cannot be read, e.g. %s:%d `%s.%s(`; place it or the count is short"
                         % (len(unplaced), unplaced[0][0], unplaced[0][1], unplaced[0][2], unplaced[0][3]))
    return rows, sets


# A row of the residue's site table: `file:line` | `Holder::method` | `key` | class | reason.
# The list is matched to the tree by (file, method, key text), not by line,
# so an edit that only moves a file's lines does not read as drift; a site
# added, removed or re-keyed does.
SITE_RE = re.compile(r"^\|\s*`([^`]+):(\d+)`\s*\|\s*`([^`]+)`\s*\|\s*(?:`(.*?)`)?\s*\|\s*([A-Z?])\s*\|")


def residue_sites(path):
    """{(file, method, key): count} and {class: count} from the table."""
    sites = {}
    classes = {}
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            m = SITE_RE.match(line)
            if m:
                key = (m.group(1), m.group(3), (m.group(4) or "").replace("\\|", "|"))
                sites[key] = sites.get(key, 0) + 1
                classes[m.group(5)] = classes.get(m.group(5), 0) + 1
    return sites, classes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(ROOT, "compiler", "src"))
    ap.add_argument("--count", action="store_true", help="print the population size alone")
    ap.add_argument("--methods", action="store_true", help="print the read methods per holder")
    ap.add_argument("--check", nargs="?", const=DEFAULT_RESIDUE, metavar="RESIDUE_MD",
                    help="refuse unless the tree's population equals the residue's site table")
    args = ap.parse_args()
    gate = load_gate()
    rows, sets = population(gate, args.src)
    if args.methods:
        for holder in sorted(sets):
            if sets[holder]:
                print("%s: %s" % (holder, ", ".join(sorted(sets[holder]))))
        return 0
    if args.check:
        listed, classes = residue_sites(args.check)
        tree = {}
        for r, _l, t, n, a in rows:
            k = (r, t + "::" + n, a)
            tree[k] = tree.get(k, 0) + 1
        drift = False
        for k in sorted(set(tree) | set(listed)):
            if tree.get(k, 0) != listed.get(k, 0):
                drift = True
                print("residue: %s %s (%s): tree %d, list %d"
                      % (k[0], k[1], k[2], tree.get(k, 0), listed.get(k, 0)))
        if drift:
            print("residue: DRIFT - tree %d, list %d; re-run residue_classify.py and review the rows it changes"
                  % (sum(tree.values()), sum(listed.values())))
            return 1
        if "?" in classes:
            print("residue: %d sites UNCLASSIFIED" % classes["?"])
            return 1
        # The convertible remainder is every class that is neither justified
        # (J) nor outside D32 (N); the migration is complete at 0.
        convertible = sum(n for c, n in classes.items() if c not in ("J", "N"))
        print("residue: OK -- %d sites, list and tree agree; %s; convertible %d"
              % (sum(tree.values()),
                 ", ".join("%s %d" % (c, classes[c]) for c in sorted(classes)), convertible))
        return 0
    if args.count:
        print(len(rows))
        return 0
    for rel, lineno, ty, name, arg in rows:
        print("%s\t%d\t%s\t%s\t%s" % (rel, lineno, ty, name, arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
