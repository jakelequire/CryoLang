#!/usr/bin/env python3
"""Enumerate every name-keyed READ in compiler/src: a call that hands a
SPELLING (`SymbolStr` / `string` / `ModulePath`) into a method of a
declaration-holding type and gets a declaration, a member or a fact about
one back, and every loop that searches such a type's member table by one.

This is D32's population (docs/name-resolution.md §0.1): the sites the
residue `scripts/ns-migration/residue.md` must account for, one by one.

The sites are READ FROM THE COMPILER, not from the source text: the facts
`cryo build --emit=facts` writes for the compiler (`make facts`,
`.facts/compiler.facts`, refused when missing or stale).  Each record is a
call or a comparison sema resolved, with the callee's identity and where
every value came from, so a site is found by what it IS - whatever its
receiver is spelled, however it is parenthesized, cast or reached through a
local - and not by what a pattern can read off one line.

Which types HOLD declarations is placement, decided over declarations and
kept from `scripts/lane-gate.py`: every store (a type owning a map) and
every array owner the gate places as "a member table inside its owner"
(an impl's or a trait's members by leaf, the user-defined types' member
records).  An array owner excluded for holding paths, flags, texts or a
pass's own rib holds no declaration and is outside.

A site is in the population when
  * METHOD: an `arg` record whose parameter type is a key type, whose callee
    is a method of a holder taking `&this` or a static (a `mut &this`
    registrar is the write side, which D32 does not count), called outside
    the holder's own file and outside the files its store owns (a store's
    own calls are its machinery).  One site per call; its key is the
    provenance of its key arguments, in order.
  * SCAN: a `cmp` record, or an `arg` record of a key type's `equals`/`eq`,
    one of whose operands is a key read off an element of an array the gate
    places as a TABLE, where the element is read by the INNERMOST loop around
    the comparison - read at the record's own loop depth, directly or
    through a local declared at it, or indexed by that loop's counter.  An
    element bound outside the innermost loop is the source of the key being
    searched for, not the array being searched.  The site's read is spelled
    `Owner::field[]` (`local::Elem[]` for a local or parameter array, named
    by its element type), outside the owner's own file; its key is the other
    operand's provenance.

A call sema left unpinned is written by the compiler as `noparams` with the
callee as spelled; one whose spelled method is a holder's read method is
REFUSED - a site the instrument cannot identify is a site it cannot count.

Every site prints as one tab-separated row:
    file<TAB>line<TAB>holder<TAB>method<TAB>key provenance
`--count` prints the size alone; `--check [residue.md]` reads the residue's
site table (`residue_classify.py` writes it) and refuses when the tree's
population and the table differ in either direction - matched by (file,
method, key), so a line shift is not drift and a new or re-keyed site is -
when a row's class is not the one `residue_classify.py` derives for that
site, or when a class letter is one the classifier has no meaning for; on OK
it prints the count per class FROM THE CLASSIFIER.
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
# owner with this phrase in its reason; those hold declarations.
MEMBER_TABLE = "a member table inside its owner"
# D32's key types, as the facts spell a parameter or a compared value.
KEY = re.compile(r"\b(SymbolStr|string|ModulePath)\b")
# A key type's own comparison, called on one key with another.
KEY_EQUALS = re.compile(
    r"^(compiler::resolver::symbol_str::SymbolStr|string|compiler::module_graph::ModulePath)"
    r"\.(equals|eq)\(")
LOCAL_AT = re.compile(r"^local:(?:mut )?[A-Za-z_0-9]+@(\d+)=")
ELEMENT = re.compile(r"^element(?:@(\d+))?:")


def load_gate():
    spec = importlib.util.spec_from_file_location("lane_gate", GATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_methods(gate, tree, type_name, defn):
    """{name} of `type_name`'s methods that take a key type as a PARAMETER
    and are reads (not `mut &this`), as the declarations state them."""
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


def placement(gate, src):
    """The holders and their declaring files, each holder's read methods as
    declared, and the tree the placement read."""
    tree = gate.Tree(src)
    gate.place_map_owners(tree)
    gate.place_array_owners(tree)
    holders = {name: st.defn for name, st in gate.STORES.items()}
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
    for label, entry in gate.SCANNED_ARRAYS.items():
        if entry.kind == gate.TABLE:
            owner, field = label.split(".", 1)
            sets.setdefault(owner, set()).add(field + "[]")
    return tree, holders, sets


def bare_type(t):
    """`compiler::ast::declaration::MethodNode*` -> `MethodNode`."""
    t = t.strip().lstrip("&").replace("mut ", "").strip().rstrip("*").strip()
    depth = 0
    out = []
    for ch in t:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return "".join(out).rsplit("::", 1)[-1]


def split_callee(text):
    """(owner leaf, method, is_static) of a rendered callee, or None for a
    free function or a callee the compiler could not name."""
    head = text.split("(", 1)[0]
    depth = 0
    flat = []
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            flat.append(ch)
    head = "".join(flat)
    last = head.rsplit("::", 1)[-1]
    if "." in last:
        owner, meth = head.rsplit(".", 1)
        return owner.rsplit("::", 1)[-1], meth, False
    if "::" in head:
        owner, meth = head.rsplit("::", 1)
        return owner.rsplit("::", 1)[-1], meth, True
    return None


def scanned_table(prov, depth):
    """The TABLE label (`Owner.field`, `local.Elem`) of the nearest array
    element the key in `prov` is read off, when the innermost loop reads it;
    None otherwise (a key from a call, a parameter, a local bound outside the
    innermost loop, no element at all)."""
    segs = prov.split("<-")
    cur = depth
    for i, seg in enumerate(segs):
        src = seg
        if src.startswith("local:"):
            m = LOCAL_AT.match(src)
            if m:
                cur = int(m.group(1))
            eq = src.find("=")
            src = src[eq + 1:] if eq >= 0 else ""
        if src.startswith("call:"):
            return None
        m = ELEMENT.match(src)
        if not m:
            continue
        if m.group(1) is not None:
            cur = int(m.group(1))
        if cur != depth:
            return None
        inner = src[m.end():]
        if inner.startswith("field:"):
            owner, field = inner[len("field:"):].rsplit(".", 1)
            return "%s.%s" % (bare_type(owner), field)
        if i > 0 and segs[i - 1].startswith("field:"):
            owner = segs[i - 1][len("field:"):].rsplit(".", 1)[0]
            return "local.%s" % bare_type(owner)
        return None
    return None


def element_of(prov):
    """The chain from the nearest array element on - which element a key is
    read off - or None when there is none."""
    segs = prov.split("<-")
    for i, seg in enumerate(segs):
        src = seg[seg.find("=") + 1:] if seg.startswith("local:") else seg
        if src.startswith("call:"):
            return None
        if ELEMENT.match(src):
            return "<-".join([src] + segs[i + 1:])
    return None


def population(gate, src, facts):
    """The population from the facts at `facts`: (rows, sets)."""
    tree, holders, sets = placement(gate, src)
    by_lower = {rel.lower(): rel for rel in tree.rels}

    def tree_rel(path):
        p = path[4:] if path.startswith("src/") else None
        return by_lower.get(p.lower()) if p is not None else None

    def owned(holder, rel):
        if rel == holders.get(holder):
            return True
        return holder in gate.STORES and gate.STORES[holder].owns(rel)

    read_names = {m for ms in sets.values() for m in ms if not m.endswith("[]")}
    calls = {}
    scans = set()
    unpinned = []
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15:
                raise SystemExit("residue: %s: a record with %d fields, not 15; the facts "
                                 "format has changed and this reader has not" % (facts, len(f)))
            rel = tree_rel(f[1])
            if rel is None:
                continue
            kind, lineno, depth = f[0], int(f[2]), int(f[14])
            if kind in ("noparams", "unaligned"):
                spelled = f[12]
                meth = spelled[1:] if spelled.startswith(".") else spelled.rsplit("::", 1)[-1]
                if f[9] == "none" and meth in read_names:
                    unpinned.append((rel, lineno, spelled))
                continue
            if kind == "arg" and KEY.search(f[5]):
                sc = split_callee(f[12])
                if sc is not None and sc[0] in holders:
                    holder, meth, static = sc
                    params = f[12].split("(", 1)[1] if "(" in f[12] else ""
                    if (static or params.startswith("&this")) and not owned(holder, rel):
                        calls.setdefault((rel, lineno, f[3], holder, meth), []).append(
                            (int(f[4]), f[6]))
            if kind == "cmp" or (kind == "arg" and KEY_EQUALS.match(f[12])):
                operands = (f[6], f[7])
                sides = (0, 1)
                # Two members of ONE element compared with each other
                # (`b.source_field == b.local_name`) search nothing by a key
                # from elsewhere: one site, keyed by the first operand.
                if element_of(operands[0]) is not None and element_of(operands[0]) == element_of(operands[1]):
                    sides = (1,)
                for side in sides:
                    label = scanned_table(operands[side], depth)
                    if label is None:
                        continue
                    entry = gate.SCANNED_ARRAYS.get(label)
                    if entry is None or entry.kind != gate.TABLE:
                        continue
                    owner, field = label.split(".", 1)
                    if entry.defn and rel == entry.defn:
                        continue
                    if owner in gate.STORES and gate.STORES[owner].owns(rel):
                        continue
                    scans.add((rel, lineno, owner, field + "[]", operands[1 - side]))
    if unpinned:
        raise SystemExit("residue: %d call(s) to a holder's read method that sema left unpinned, "
                         "e.g. %s:%d `%s`; a site the compiler cannot identify is one this "
                         "cannot count" % (len(unpinned), unpinned[0][0], unpinned[0][1], unpinned[0][2]))
    rows = []
    for (rel, lineno, _col, holder, meth), args in calls.items():
        rows.append((rel, lineno, holder, meth, ", ".join(p for _i, p in sorted(args))))
    rows.extend(sorted(scans))
    rows.sort()
    return rows, sets


# A row of the residue's site table: `file:line` | `Holder::method` | `key` | class | reason.
# The list is matched to the tree by (file, method, key), not by line, so an
# edit that only moves a file's lines does not read as drift; a site added,
# removed or re-keyed does.  The class letter is one character of any kind,
# so a letter the classifier does not know is read here and refused there
# instead of falling out of the table unseen.
SITE_RE = re.compile(r"^\|\s*`([^`]+):(\d+)`\s*\|\s*`([^`]+)`\s*\|\s*(?:`(.*?)`)?\s*\|\s*(\S+)\s*\|")


def residue_sites(path):
    """{(file, method, key): [class, ...]} from the site table."""
    sites = {}
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    for line in text.splitlines():
        m = SITE_RE.match(line)
        if m:
            key = (m.group(1), m.group(3), (m.group(4) or "").replace("\\|", "|"))
            sites.setdefault(key, []).append(m.group(5))
    return sites


def check(rows, path, out=print):
    """The list at `path` against the tree's `rows` and the classifier's
    verdict on each: every site present in both, no more than once each
    way; every row's class the one the classifier derives for it; every
    class letter one the classifier defines.  Prints each disagreement and
    returns 0 on OK, 1 on any refusal.  The counts it prints on OK are the
    classifier's."""
    import residue_classify as rc
    listed = residue_sites(path)
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
    # The class is the classifier's; the list only renders it.
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
    counts = {}
    for row in classified:
        counts[row[4]] = counts.get(row[4], 0) + 1
    conflicts = rc.override_conflicts(rows)
    if conflicts:
        out("residue: site overrides on a site whose key is a literal, which the literal rule "
            "classifies: %s" % "; ".join("%s %s (%s)" % k for k in conflicts))
        return 1
    # The convertible remainder is every class that is neither justified
    # (J) nor outside D32 (L, N); the migration is complete at 0.
    convertible = sum(n for c, n in counts.items() if c not in ("J", "L", "N"))
    out("residue: OK -- %d sites, list, tree and classifier agree; %s; convertible %d"
        % (len(classified), ", ".join("%s %d" % (c, counts[c]) for c in sorted(counts)), convertible))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(ROOT, "compiler", "src"))
    ap.add_argument("--facts", help="the compiler's facts (default: .facts/compiler.facts, refused when stale)")
    ap.add_argument("--count", action="store_true", help="print the population size alone")
    ap.add_argument("--methods", action="store_true", help="print the read methods per holder")
    ap.add_argument("--check", nargs="?", const=DEFAULT_RESIDUE, metavar="RESIDUE_MD",
                    help="refuse unless the tree's population, the residue's site table and the classifier agree")
    args = ap.parse_args()
    gate = load_gate()
    sys.path.insert(0, os.path.dirname(HERE))
    facts = args.facts
    if facts is None:
        import importlib
        facts_mod = importlib.import_module("facts")
        facts = facts_mod.facts_path("compiler")
    import parse_cache
    rows, sets = parse_cache.memo(
        "residue-population", [os.path.abspath(__file__), GATE, facts], args.src,
        lambda: population(gate, args.src, facts))
    if args.methods:
        for holder in sorted(sets):
            if sets[holder]:
                print("%s: %s" % (holder, ", ".join(sorted(sets[holder]))))
        return 0
    if args.check:
        sys.path.insert(0, HERE)
        return check(rows, args.check)
    if args.count:
        print(len(rows))
        return 0
    for rel, lineno, ty, name, arg in rows:
        print("%s\t%d\t%s\t%s\t%s" % (rel, lineno, ty, name, arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
