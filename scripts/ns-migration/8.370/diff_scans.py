"""Rule 1c old (source patterns) vs new (facts): every scanned site, by array.

    python scripts/ns-migration/8.370/diff_scans.py [facts] [old-rev]

The old reader is `inline_scans` in `scripts/lane-gate.py` at `old-rev`
(default 29b46dad, the last commit reading scans from source), loaded from
git; the new one is `facts_scans` in the working tree over `facts` (default
.facts/compiler.facts).  Prints, for key scans and identity scans, the
per-array totals and every site on one side only (file:line:array; the old
reader knows lines, not columns)."""
import collections
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = subprocess.run(["git", "-C", HERE, "rev-parse", "--show-toplevel"],
                      stdout=subprocess.PIPE, text=True, check=True).stdout.strip()
SRC = os.path.join(ROOT, "compiler", "src")
OLD_REV = sys.argv[2] if len(sys.argv) > 2 else "29b46dad"
FACTS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".facts", "compiler.facts")

old_text = subprocess.run(["git", "-C", ROOT, "show", OLD_REV + ":scripts/lane-gate.py"],
                          stdout=subprocess.PIPE, text=True, check=True, encoding="utf-8").stdout
old = type(sys)("lane_old")
old.__file__ = os.path.join(ROOT, "scripts", "lane-gate.py")
exec(compile(old_text, "lane_old", "exec"), old.__dict__)
old_tree = old.Tree(SRC)

spec = importlib.util.spec_from_file_location("lane_new", os.path.join(ROOT, "scripts", "lane-gate.py"))
new = importlib.util.module_from_spec(spec)
spec.loader.exec_module(new)
new_names, new_ids = new.facts_scans(new.Tree(SRC), FACTS)


def old_sites(types):
    return collections.Counter((r[0], r[1], "%s.%s" % (r[2], r[3])) for r in old.inline_scans(old_tree, types))


def new_sites(rows):
    return collections.Counter((r[0], r[1], r[2]) for r in rows)


def report(what, o, n):
    print("== %s: old %d sites, new %d sites" % (what, sum(o.values()), sum(n.values())))
    lo = collections.Counter(k[2] for k in o.elements())
    ln = collections.Counter(k[2] for k in n.elements())
    for label in sorted(set(lo) | set(ln)):
        entry = new.SCANNED_ARRAYS.get(label)
        mark = "" if lo[label] == ln[label] else "   <-- differs"
        print("  %-44s old %3d new %3d  %s%s" % (label, lo[label], ln[label],
                                               entry.kind if entry else "-", mark))
    for k in sorted(set(o) - set(n)):
        print("  OLD ONLY %s:%d %s" % k)
    for k in sorted(set(n) - set(o)):
        print("  NEW ONLY %s:%d %s" % k)


report("key scans", old_sites(old.KEY_TYPES), new_sites(new_names))
report("identity scans", old_sites(old.IDENTITY_TYPES), new_sites(new_ids))
