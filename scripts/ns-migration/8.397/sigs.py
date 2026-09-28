"""Print the signature of every name-taking row whose spelling arrives only as `string`.

Usage: python sigs.py <done.py --name-taking listing> <out tsv>

Each output row: the spelling types the parameters name (`string`,
`SymbolStr`, `QualifiedName`, joined with +), file:line, name, signature.
"""
import re, sys, collections, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")).replace("\\", "/") + "/"
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
cache = {}
out = open(sys.argv[2], "w", encoding="utf-8", newline="\n")
c = collections.Counter()
for loc, name in rows:
    f, ln = loc.rsplit(":", 1); ln = int(ln)
    if f not in cache:
        cache[f] = open(ROOT + f, encoding="utf-8", errors="replace").read().splitlines()
    L = cache[f]; sig = ""
    for i in range(ln - 1, min(ln + 12, len(L))):
        sig += L[i].strip() + " "
        if re.search(r"\)\s*(->[^{;]*)?(\{|;|$)", L[i]) and "(" in sig: break
    m = re.search(r"\((.*)\)", sig)
    params = m.group(1) if m else sig
    kinds = []
    for t in ("SymbolStr", "QualifiedName"):
        if re.search(r"\b%s\b" % t, params): kinds.append(t)
    if re.search(r"\bstring\b", params): kinds.append("string")
    k = "+".join(kinds) or "?"
    c[k] += 1
    out.write("%s\t%s\t%s\t%s\n" % (k, loc, name, re.sub(r"\s+", " ", sig)[:300]))
for k, v in c.most_common(): print("%5d %s" % (v, k))
