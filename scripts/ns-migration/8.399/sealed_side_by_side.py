"""Run the lane gate's sealed-type check two ways over one tree and compare.

    python scripts/ns-migration/8.399/sealed_side_by_side.py [--facts F] [--src compiler/src]

OLD: `lane-gate.py`'s check as it reads the tree's source (the type's block,
its `private` fields, its public statics returning it).
NEW: the same three questions asked of the compiler's declaration records
(`type`, `field`, `fn` in `.facts/compiler.facts`).

For every SEALED_TYPES entry it prints what each side found - the private
fields and the public statics that take an argument and return the type -
and each side's verdict, then `AGREE` or `DIFFER` per entry.  Exit 0 only
when both sides agree on every entry.
"""
import argparse, importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

# The definition path of each SEALED_TYPES entry, which the records key by.
PATHS = {
    "DefId": "compiler::resolver::res::DefId",
    "DefTable": "compiler::resolver::res::DefTable",
    "OverloadId": "compiler::decl_index::OverloadId",
    "SymbolID": "compiler::resolver::symbol_id::SymbolID",
    "ModulePath": "compiler::module_graph::ModulePath",
    "TypeRef": "compiler::types::type_ref::TypeRef",
}


def load_gate():
    spec = importlib.util.spec_from_file_location("lane_gate", os.path.join(ROOT, "scripts", "lane-gate.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def sealed_records(facts, paths):
    """{path: (declared at, [private fields], [public minting statics])} from
    the compiler's declaration records; a path with no `type` record is absent."""
    found = {}
    private, mints = {}, {}
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15 or f[12] not in paths:
                continue
            if f[0] == "type":
                found[f[12]] = "%s:%s" % (f[1], f[2])
            elif f[0] == "field" and f[6] == "private":
                private.setdefault(f[12], []).append(f[13])
            elif (f[0] == "fn" and f[7] == "static" and f[6] == "public"
                  and int(f[4]) > 0 and f[5] == f[12]):
                mints.setdefault(f[12], []).append("%s (%s:%s)" % (f[13], f[1], f[2]))
    return {p: (at, sorted(private.get(p, [])), sorted(mints.get(p, []))) for p, at in found.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--facts", default=os.path.join(ROOT, ".facts", "compiler.facts"))
    ap.add_argument("--src", default=os.path.join(ROOT, "compiler", "src"))
    a = ap.parse_args()
    gate = load_gate()
    tree = gate.Tree(a.src)
    new = sealed_records(a.facts, set(PATHS.values()))
    differ = 0
    for name, (defn, _reason) in sorted(gate.SEALED_TYPES.items()):
        lines = tree.files.get(defn)
        head = None
        for i, raw in enumerate(lines or []):
            m = gate.SEALED_HEAD_RE.match(gate.strip_comment(raw))
            if m is not None and m.group(1) == name:
                head = i
                break
        if head is None:
            old = ("absent", [], [])
        else:
            priv = [p.split(":")[0].replace("private", "").strip() for p in gate.private_fields(lines, head)]
            mints = [] if name in gate.SEALED_STORES else gate.public_mints(lines, head, name)
            old = ("%s:%d" % (defn, head + 1), sorted(priv), sorted(mints))
        path = PATHS[name]
        nw = new.get(path, ("absent", [], []))
        if path in ("compiler::resolver::res::DefTable",):
            nw = (nw[0], nw[1], [])
        old_ok = old[0] != "absent" and old[1] and not old[2]
        new_ok = nw[0] != "absent" and nw[1] and not nw[2]
        verdict = "AGREE" if bool(old_ok) == bool(new_ok) else "DIFFER"
        differ += verdict == "DIFFER"
        print("%s %s" % (verdict, path))
        print("    old: %s  private %s  mints %s  -> %s" % (old[0], old[1], old[2], "OK" if old_ok else "REFUSED"))
        print("    new: %s  private %s  mints %s  -> %s" % (nw[0], nw[1], nw[2], "OK" if new_ok else "REFUSED"))
    print("sealed side by side: %d entr(ies), %d differ" % (len(gate.SEALED_TYPES), differ))
    return 1 if differ else 0


sys.exit(main())
