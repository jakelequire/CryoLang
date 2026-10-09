"""Types and fields under compiler/src that nothing references, by text.

Types: a compiler type whose leaf name appears nowhere in compiler/src or
tools/CryoLSP/src outside its own declaration (the `type ... X {` block, any
`implement ... X` block, and the spans of other unreferenced types - a
fixpoint).  Fields: a field whose name is never read as `.name` anywhere in
compiler/src or tools/CryoLSP/src (a struct literal's `name:` initializer is
not a read).

usage: python deadtypes.py <out.tsv>
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
from spans import span  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

types = []
fields = []
with open(os.path.join(ROOT, ".facts", "compiler.facts"), encoding="utf-8", errors="replace") as fh:
    for line in fh:
        c = line.rstrip("\n").split("\t")
        if len(c) < 15 or not c[1].startswith("src/"):
            continue
        if c[0] == "type":
            types.append(dict(file="compiler/" + c[1], line=int(c[2]), kind=c[7], path=c[12],
                              name=c[12].split("::")[-1]))
        elif c[0] == "field":
            fields.append(dict(file="compiler/" + c[1], line=int(c[2]), owner=c[12], name=c[13]))

corpus = {}
for base in ("compiler/src", "tools/CryoLSP/src"):
    for dp, dn, fnames in os.walk(os.path.join(ROOT, base)):
        for fn in fnames:
            if fn.endswith(".cryo"):
                p = os.path.join(dp, fn)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    corpus[os.path.relpath(p, ROOT).replace("\\", "/")] = fh.read().replace("\r\n", "\n").split("\n")


def real(path):
    return next((p for p in corpus if p.lower() == path.lower()), None)


def code(l):
    return l.split("//", 1)[0]


# Own spans of each type: its declaration and its implement blocks.
own = defaultdict(list)
for t in types:
    p = real(t["file"])
    sp = span(corpus[p], t["line"]) if p else None
    if sp:
        own[t["path"]].append((p, sp[0], sp[1]))
    for q, lines in corpus.items():
        for i, l in enumerate(lines, 1):
            if l.startswith("implement") and re.search(r"\b(struct|class|enum|union)\s+" + re.escape(t["name"]) + r"\b", l):
                s2 = span(lines, i)
                if s2:
                    own[t["path"]].append((q, s2[0], s2[1]))

refs = defaultdict(list)
names = set(t["name"] for t in types)
pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names), key=len, reverse=True)) + r")\b")
for q, lines in corpus.items():
    for i, l in enumerate(lines, 1):
        for m in pat.finditer(code(l)):
            refs[m.group(1)].append((q, i))

dead = set(t["path"] for t in types)
while True:
    drop = set()
    for t in types:
        if t["path"] not in dead:
            continue
        spans_ok = [s for d in dead for s in own[d]]
        for (q, i) in refs[t["name"]]:
            if not any(q == p and a <= i <= b for (p, a, b) in spans_ok):
                drop.add(t["path"])
                break
    if not drop:
        break
    dead -= drop

# Fields never read as `.name`.
reads = defaultdict(int)
fnames = set(f["name"] for f in fields)
fpat = re.compile(r"\.\s*(" + "|".join(sorted(map(re.escape, fnames), key=len, reverse=True)) + r")\b")
for q, lines in corpus.items():
    for l in lines:
        for m in fpat.finditer(code(l)):
            reads[m.group(1)] += 1

with open(sys.argv[1], "w", encoding="utf-8") as out:
    out.write("what\tfile\tline\towner\tname\n")
    for t in types:
        if t["path"] in dead:
            out.write("type\t%s\t%d\t%s\t%s\n" % (t["file"], t["line"], t["kind"], t["path"]))
    for f in fields:
        if reads[f["name"]] == 0 and f["owner"].split("::")[-1] not in [x["path"].split("::")[-1] for x in types if x["path"] in dead]:
            out.write("field\t%s\t%d\t%s\t%s\n" % (f["file"], f["line"], f["owner"], f["name"]))
print("types %d, unreferenced %d; fields %d, never read as .name %d"
      % (len(types), len(dead), len(fields),
         sum(1 for f in fields if reads[f["name"]] == 0)))
