"""The name-taking population two ways, side by side, before the switch.

OLD: `done.py --name-taking` as it stood before the switch - a regular
expression over `compiler/src` declaration heads.  Run from a checkout of
the commit before (pass its done.py with --old).
NEW: the compiler's own declaration records (`.facts/compiler.facts`): every
`fn` record under `compiler/src` with a `param` record whose key column is
`string`, `SymbolStr` or `QualifiedName`.

Prints each side's count, then every function on one side only, grouped by a
reason the script can state from the records (the class of difference), so a
difference is explained rather than netted.

usage: python name_taking_side_by_side.py --old <old done.py> [--out <tsv>]
"""
import os, subprocess, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
FACTS = os.path.join(ROOT, ".facts", "compiler.facts")
NAME_KEYS = ("string", "SymbolStr", "QualifiedName")


def old_list(done_py):
    p = subprocess.run([sys.executable, done_py, "--name-taking"], cwd=ROOT,
                       capture_output=True, text=True)
    out = {}
    for line in p.stdout.splitlines():
        loc, name = line.split("\t")
        rel, ln = loc.rsplit(":", 1)
        out[(rel.lower(), int(ln))] = (rel, name)
    return out


def new_list():
    fns = {}
    taking = set()
    with open(FACTS, encoding="utf-8") as fh:
        for line in fh:
            c = line.rstrip("\n").split("\t")
            if c[0] not in ("fn", "param") or not c[1].startswith("src/"):
                continue
            rel = "compiler/" + c[1]
            if c[0] == "fn":
                fns[(c[12], c[13], c[8])] = (rel, int(c[2]), c[13], c[7])
            elif c[9] in NAME_KEYS:
                fn_name = c[13].split(":", 1)[0]
                taking.add((c[12], fn_name, c[8]))
    out = {}
    for k in taking:
        rel, ln, name, role = fns[k]
        out[(rel.lower(), ln)] = (rel, name, role, k[0])
    return out


def main():
    done_py = sys.argv[sys.argv.index("--old") + 1]
    old = old_list(done_py)
    new = new_list()
    print("old %d  new %d  both %d" % (len(old), len(new), len(set(old) & set(new))))
    only_old = sorted(set(old) - set(new))
    only_new = sorted(set(new) - set(old))
    groups = collections.defaultdict(list)
    for k in only_new:
        rel, name, role, owner = new[k]
        groups["new-only " + role].append("%s:%d\t%s\t%s" % (rel, k[1], owner, name))
    for k in only_old:
        rel, name = old[k]
        groups["old-only"].append("%s:%d\t%s" % (rel, k[1], name))
    lines = []
    for g in sorted(groups):
        print("%-22s %d" % (g, len(groups[g])))
        for l in groups[g]:
            lines.append(g + "\t" + l)
    if "--out" in sys.argv:
        with open(sys.argv[sys.argv.index("--out") + 1], "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
