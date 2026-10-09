"""Delete the function spans listed in a candidate TSV (from dead.py).

usage: python delete.py <cand.tsv> [class ...]
Only rows whose `class` column is one of the given classes (default: clean)
are deleted.  Re-locates each declaration by its `decl` line, recomputing the
span from the CURRENT file so a previous deletion in the same file does not
shift it (rows are applied bottom-up per file).  A blank line left doubled by
the deletion is collapsed.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from spans import span  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
classes = set(sys.argv[2:]) or {"clean"}
rows = []
with open(sys.argv[1], encoding="utf-8") as fh:
    head = fh.readline().rstrip("\n").split("\t")
    for l in fh:
        r = dict(zip(head, l.rstrip("\n").split("\t")))
        if r["class"] in classes:
            rows.append(r)

by_file = {}
for r in rows:
    by_file.setdefault(r["path"], []).append(r)

removed_lines = 0
for path, rs in sorted(by_file.items()):
    full = os.path.join(ROOT, path)
    with open(full, encoding="utf-8", newline="") as fh:
        raw = fh.read()
    crlf = "\r\n" in raw
    lines = raw.replace("\r\n", "\n").split("\n")
    for r in sorted(rs, key=lambda r: -int(r["decl"])):
        sp = span(lines, int(r["decl"]))
        if sp is None or (sp[0], sp[1]) != (int(r["first"]), int(r["last"])):
            print("SPAN MOVED, skipped:", path, r["decl"], r["name"], sp)
            continue
        first, last = sp
        # Drop one surrounding blank line when the deletion leaves two.
        if first >= 2 and last < len(lines) and lines[first - 2].strip() == "" \
                and lines[last].strip() == "":
            last += 1
        removed_lines += last - first + 1
        del lines[first - 1:last]
    out = "\n".join(lines)
    if crlf:
        out = out.replace("\n", "\r\n")
    with open(full, "w", encoding="utf-8", newline="") as fh:
        fh.write(out)
print("deleted %d functions, %d lines, %d files" % (len(rows), removed_lines, len(by_file)))
