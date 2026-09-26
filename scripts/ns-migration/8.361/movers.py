"""Two-sided check of the movers: an object moved iff its symbols carry a
generated definition's marked leaf (`$__Closure_N`, `$<fn>$Future_N`,
`$main$async`).  Usage: movers.py <base.sha256> <tree.sha256>"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MARK = re.compile(r"\$__Closure_\d+|\$[A-Za-z_]\w*\$Future_\d+|\$main\$async")


def hashes(p):
    m = {}
    for line in open(p, encoding="utf-8"):
        line = line.rstrip("\n")
        if line:
            m[line[66:]] = line[:64]
    return m


a, b = hashes(sys.argv[1]), hashes(sys.argv[2])
moved = {p for p in a.keys() & b.keys() if a[p] != b[p]}
rows = collections.Counter()
examples = {}
for p in sorted(b):
    full = os.path.join(ROOT, p)
    if not os.path.isfile(full):
        rows["missing"] += 1
        continue
    out = subprocess.run(["nm", full], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode("latin-1")
    marked = bool(MARK.search(out))
    key = ("moved" if p in moved else "same") + "/" + ("marked" if marked else "unmarked")
    rows[key] += 1
    examples.setdefault(key, p)
for k, n in sorted(rows.items()):
    print("%-18s %5d   e.g. %s" % (k, n, examples.get(k, "")))
