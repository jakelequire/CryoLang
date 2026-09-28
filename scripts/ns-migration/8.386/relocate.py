"""Re-point a list's `file:line` at the same declaration in the working tree.

  python relocate.py <list.tsv> <rev> > <list-now.tsv>

The list's line numbers were taken at <rev>; edits since then (inserted
imports and entry unwraps) move declarations down.  A row's declaration is
found in <rev>'s file as the n-th declaration of its function's name, and
re-pointed at the n-th declaration of that name in the working tree - so a
function declared twice under two target gates keeps its own row.
"""
import re, subprocess, sys

def decls(lines, fn):
    pat = re.compile(r"\s*(?:public\s+|private\s+)?(?:static\s+|function\s+)?%s\s*(<[^()]*>)?\s*\("
                     % re.escape(fn))
    return [i + 1 for i, l in enumerate(lines) if pat.match(l) and not l.rstrip().endswith(";")]

listfile, rev = sys.argv[1:3]
old_cache, new_cache = {}, {}
for l in open(listfile, encoding="utf-8"):
    c = l.rstrip("\n").split("\t")
    if len(c) < 3:
        continue
    f, ln = c[0].rsplit(":", 1)
    if f not in old_cache:
        old_cache[f] = subprocess.run(["git", "show", "%s:%s" % (rev, f)], capture_output=True,
                                      text=True, encoding="utf-8").stdout.split("\n")
        new_cache[f] = open(f, encoding="utf-8").read().split("\n")
    old = decls(old_cache[f], c[1])
    new = decls(new_cache[f], c[1])
    n = old.index(int(ln))
    c[0] = "%s:%d" % (f, new[n])
    print("\t".join(c))
