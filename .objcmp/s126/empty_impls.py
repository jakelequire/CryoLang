"""Find (and with --delete, remove) `implement` blocks under compiler/src whose
body holds nothing but blank lines and comments - left behind when every
method in them was deleted - and a `private:` / `public:` label followed by
nothing but blank lines and comments up to the closing brace.

usage: python empty_impls.py [--delete]
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from spans import span  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
delete = "--delete" in sys.argv


def empty(lines):
    return all(l.strip() == "" or l.strip().startswith("//") for l in lines)


total = 0
for dp, dn, fnames in os.walk(os.path.join(ROOT, "compiler", "src")):
    for fn in fnames:
        if not fn.endswith(".cryo"):
            continue
        full = os.path.join(dp, fn)
        with open(full, encoding="utf-8", newline="") as fh:
            raw = fh.read()
        crlf = "\r\n" in raw
        lines = raw.replace("\r\n", "\n").split("\n")
        cuts = []
        for i, l in enumerate(lines, 1):
            if l.startswith("implement"):
                sp = span(lines, i)
                if sp and sp[1] > i and empty(lines[i:sp[1] - 1]):
                    cuts.append((sp[0], sp[1], "implement"))
            elif re.match(r"^\s*(private|public):\s*$", l):
                j = i
                while j < len(lines) and (lines[j].strip() == "" or lines[j].strip().startswith("//")):
                    j += 1
                if j < len(lines) and lines[j].strip() == "}":
                    cuts.append((i, j, "label"))
        rel = os.path.relpath(full, ROOT).replace("\\", "/")
        for (a, b, k) in cuts:
            print("%s:%d-%d %s" % (rel, a, b, k))
        total += len(cuts)
        if delete and cuts:
            for (a, b, k) in sorted(cuts, reverse=True):
                if a >= 2 and b < len(lines) and lines[a - 2].strip() == "" and lines[b].strip() == "":
                    b += 1
                del lines[a - 1:b]
            out = "\n".join(lines)
            if crlf:
                out = out.replace("\n", "\r\n")
            with open(full, "w", encoding="utf-8", newline="") as fh:
                fh.write(out)
print("%d empty block(s)%s" % (total, " deleted" if delete else ""))
