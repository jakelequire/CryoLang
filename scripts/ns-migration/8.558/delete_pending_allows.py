"""Delete the pending `lookup_by_spelling` allows the spelling-lookup check
no longer needs.

A pending allow is one whose reason reads "pending: this took a spelling
before the lint existed".  Each sits on a function or method; the check now
refuses a function only where it looks a parameter's spelling up outside a
door, so an allow stays only on a function in `refused.tsv`: the functions a
compiler that ignores allows (doors still exempt) refused, one row per
function as `<file>\t<line>\t<name>`, `<line>` the declaration's own line.
An allow on anything that is not a function (a field) is kept.

Usage: python delete_pending_allows.py [--apply]
Prints what it would delete and keep; `--apply` rewrites the files.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SRC = os.path.join(ROOT, "compiler", "src")
PENDING = re.compile(r'^\s*!\[allow\(lookup_by_spelling, reason = "pending: this took a spelling before the lint existed[^"]*"\)\]\s*$')
DECL = re.compile(r'^\s*(?:(?:public|private|static|override|virtual|function|mut)\s+)*(~?[A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\(')


def norm(path):
    p = path.replace("\\", "/").lower()
    i = p.find("src/")
    return p[i:] if i >= 0 else p


def main():
    apply = "--apply" in sys.argv
    refused = set()
    for l in open(os.path.join(HERE, "refused.tsv"), encoding="utf-8"):
        f, line, name = l.rstrip("\n").split("\t")
        refused.add((norm(f), name, int(line)))
    deleted, kept, other = [], [], []
    for d, _, files in os.walk(SRC):
        for fn in files:
            if not fn.endswith(".cryo"):
                continue
            path = os.path.join(d, fn)
            rel = norm(os.path.relpath(path, os.path.dirname(SRC)))
            with open(path, encoding="utf-8", newline="") as h:
                lines = h.readlines()
            drop = set()
            for i, l in enumerate(lines):
                if not PENDING.match(l):
                    continue
                j = i + 1
                while j < len(lines) and re.match(r'^\s*(!\[|///|//)', lines[j]):
                    j += 1
                m = DECL.match(lines[j]) if j < len(lines) else None
                if not m:
                    other.append("%s:%d" % (rel, i + 1))
                    continue
                name, decl_line = m.group(1), j + 1
                if any((rel, name, decl_line + k) in refused for k in (-1, 0, 1)):
                    kept.append("%s:%d %s" % (rel, decl_line, name))
                else:
                    deleted.append("%s:%d %s" % (rel, decl_line, name))
                    drop.add(i)
            if drop and apply:
                with open(path, "w", encoding="utf-8", newline="") as h:
                    h.writelines(l for i, l in enumerate(lines) if i not in drop)
    for x in sorted(kept):
        print("keep   " + x)
    for x in sorted(other):
        print("other  " + x)
    for x in sorted(deleted):
        print("delete " + x)
    print("pending allows: %d on functions (%d kept, %d deleted), %d elsewhere (kept)"
          % (len(kept) + len(deleted), len(kept), len(deleted), len(other)))


main()
