"""Write `refused.tsv` from a probe log: one row per function a compiler
that ignores allows (doors still exempt) refused, from its
`S124 refuse <file>:<line> <name> ...` lines.  Only `compiler/src` functions.

Usage: python refused_from_probe.py PROBE_LOG
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
rows = set()
for l in open(sys.argv[1], encoding="utf-8", errors="replace"):
    p = l.rstrip("\n").split(" ")
    if len(p) < 4 or p[0] != "S124" or p[1] != "refuse":
        continue
    f, line = p[2].rsplit(":", 1)
    f = f.replace("\\", "/")
    if not f.lower().startswith("src/compiler/"):
        continue
    rows.add((f, int(line), p[3]))
with open(os.path.join(HERE, "refused.tsv"), "w", encoding="utf-8", newline="\n") as h:
    for f, line, name in sorted(rows):
        h.write("%s\t%d\t%s\n" % (f, line, name))
print("%d functions" % len(rows))
