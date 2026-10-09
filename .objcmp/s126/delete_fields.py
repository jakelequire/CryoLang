"""Delete write-only fields: each field's declaration line and every
`name: value,` initializer line of a struct literal of its owner type.

usage: python delete_fields.py <types.tsv> [--dry]
Rows with what == field are taken, except fields of the libclang mirror
structs (compiler/src/compiler/bindgen/clang.cryo), whose layout is C's.
A literal is recognized by the nearest less-indented line above the
initializer containing `Owner {`; an initializer whose value spans lines is
refused (printed, not touched).
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
dry = "--dry" in sys.argv
skip = set()
for a in sys.argv:
    if a.startswith("--skip="):
        skip |= set(a[len("--skip="):].split(","))
fields = []
with open(sys.argv[1], encoding="utf-8") as fh:
    head = fh.readline().rstrip("\n").split("\t")
    for l in fh:
        r = dict(zip(head, l.rstrip("\n").split("\t")))
        if r["what"] == "field" and "bindgen/clang.cryo" not in r["file"] \
                and r["owner"].split("::")[-1] + "." + r["name"] not in skip:
            fields.append(r)

files = {}
for base in ("compiler/src", "tools/CryoLSP/src"):
    for dp, dn, fnames in os.walk(os.path.join(ROOT, base)):
        for fn in fnames:
            if fn.endswith(".cryo"):
                p = os.path.join(dp, fn)
                with open(p, encoding="utf-8", newline="") as fh:
                    raw = fh.read()
                files[os.path.relpath(p, ROOT).replace("\\", "/")] = ["\r\n" in raw, raw.replace("\r\n", "\n").split("\n"), set()]


def indent(l):
    return len(l) - len(l.lstrip(" "))


for f in fields:
    owner = f["owner"].split("::")[-1]
    name = f["name"]
    decl = next(p for p in files if p.lower() == f["file"].lower())
    lines = files[decl][1]
    i = int(f["line"])
    if not re.match(r"^\s*" + re.escape(name) + r"\s*:", lines[i - 1]):
        print("DECL MISMATCH", decl, i, lines[i - 1])
        continue
    files[decl][2].add(i)
    # The field's own comment: the comment lines directly above it.
    k = i - 1
    while k >= 1 and lines[k - 1].strip().startswith("//"):
        files[decl][2].add(k)
        k -= 1
    init = re.compile(r"^\s*" + re.escape(name) + r"\s*:\s*(.*)$")
    for p, (crlf, ls, cut) in files.items():
        for j, l in enumerate(ls, 1):
            m = init.match(l)
            if not m or (p == decl and j == i):
                continue
            k = j - 1
            while k >= 1 and (ls[k - 1].strip() == "" or indent(ls[k - 1]) >= indent(l)):
                k -= 1
            if k < 1 or not re.search(r"\b" + re.escape(owner) + r"\s*\{", ls[k - 1]):
                continue
            val = m.group(1).split("//", 1)[0].rstrip()
            if not (val.endswith(",") or ls[j].strip().startswith("}")):
                print("MULTI-LINE, refused:", p, j, l.strip())
                continue
            cut.add(j)
            print("init %s:%d  %s" % (p, j, l.strip()))

n = 0
for p, (crlf, ls, cut) in files.items():
    if not cut:
        continue
    n += len(cut)
    if dry:
        continue
    for j in sorted(cut, reverse=True):
        del ls[j - 1]
    out = "\n".join(ls)
    if crlf:
        out = out.replace("\n", "\r\n")
    with open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="") as fh:
        fh.write(out)
print("%d field(s), %d line(s)%s" % (len(fields), n, " (dry)" if dry else " deleted"))
