"""The lane gate's store populations (rules 1 and 1b) two ways, side by side.

OLD: the gate as it stood before the switch - its `Tree` reads every `type`
block in compiler/src with regular expressions: a field whose type is
`HashMap`/`HashSet` makes a map owner; a field that is an array of names or
of records carrying a key-typed field makes an array owner, which is a
candidate when the type also declares a method taking a key.
NEW: the gate after the switch, reading the compiler's declaration records.

Both are imported from a gate script (pass the pre-switch one with --old,
the post-switch one with --new), each run over the same tree and facts.
Prints every map owner, array candidate and name-taking method set on one
side only, so each difference is explained rather than netted.

usage: python stores_side_by_side.py --old <lane-gate.py> --new <lane-gate.py>
"""
import importlib.util, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SRC = os.path.join(ROOT, "compiler", "src")


def load(path, name):
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def populations(mod, facts):
    tree = mod.Tree(SRC) if "facts" not in mod.Tree.__init__.__code__.co_varnames else mod.Tree(SRC, facts)
    maps = {n: sorted(set((r, f) for r, f, _m, _k in e)) for n, e in tree.map_owners.items()}
    arrays = mod.array_candidates(tree)
    placement = {"declared_in": {n: sorted(r) for n, r in tree.declared_in.items()},
                 "fields": {n: sorted(f) for n, f in tree.fields.items() if n in tree.declared_in},
                 "bases": dict(tree.bases)}
    return maps, {n: (r, sorted(f), sorted(m)) for n, (r, f, m) in arrays.items()}, placement


def main():
    old = load(sys.argv[sys.argv.index("--old") + 1], "gate_old")
    new = load(sys.argv[sys.argv.index("--new") + 1], "gate_new")
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import facts as facts_mod
    facts = facts_mod.facts_path("compiler")
    om, oa, op = populations(old, facts)
    nm, na, np_ = populations(new, facts)
    # Rule 1c's placement inputs: the files each type is declared in, its
    # fields' names, each class's base.
    for key in ("declared_in", "fields", "bases"):
        o, n = op[key], np_[key]
        diff = sorted(t for t in set(o) | set(n) if o.get(t) != n.get(t))
        print("%s: old %d  new %d  differ %d" % (key, len(o), len(n), len(diff)))
        for t in diff:
            print("  %-28s old %s\n  %-28s new %s" % (t, o.get(t), "", n.get(t)))
    print("map owners: old %d  new %d" % (len(om), len(nm)))
    for n in sorted(set(om) | set(nm)):
        if om.get(n) != nm.get(n):
            print("  %-28s old %s\n  %-28s new %s" % (n, om.get(n), "", nm.get(n)))
    print("array candidates: old %d  new %d" % (len(oa), len(na)))
    for n in sorted(set(oa) | set(na)):
        if oa.get(n) != na.get(n):
            print("  %-28s old %s\n  %-28s new %s" % (n, oa.get(n), "", na.get(n)))


if __name__ == "__main__":
    main()
