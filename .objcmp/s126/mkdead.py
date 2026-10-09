"""Render DEAD.md from the sweep's candidate lists and keep.txt.

usage: python mkdead.py <out.md> <cand.tsv> [<cand.tsv> ...]
Each later list is a round run over the tree the earlier ones left.  A row
whose `Type::name` (or free name) keep.txt names was not deleted.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# A run of comment lines is the reason for the entries that follow it.
keep = {}
why = ""
after_entry = True
with open(os.path.join(HERE, "keep.txt"), encoding="utf-8") as fh:
    for l in fh:
        s = l.strip()
        if s.startswith("#"):
            text = s.lstrip("# ")
            why = text if after_entry else why + " " + text
            after_entry = False
        elif s:
            keep[s] = why
            after_entry = True

rows = []
for i, path in enumerate(sys.argv[2:]):
    with open(path, encoding="utf-8") as fh:
        head = fh.readline().rstrip("\n").split("\t")
        for l in fh:
            r = dict(zip(head, l.rstrip("\n").split("\t")))
            r["round"] = i + 1
            rows.append(r)


def key(r):
    return r["owner"].split("::")[-1] + "::" + r["name"]


deleted = [r for r in rows if key(r) not in keep and r["name"] not in keep]
held = [r for r in rows if not (key(r) not in keep and r["name"] not in keep)]
lines = sum(int(r["last"]) - int(r["first"]) + 1 for r in deleted)

out = []
out.append("# Compiler dead-code sweep")
out.append("")
out.append("Every function under `compiler/src` that nothing live reaches, found from")
out.append("the compiler's own `--emit=facts` call records (`dead.py`), deleted by")
out.append("`delete.py`; `iter.sh` runs one round (regenerate, delete, type-check).")
out.append("Live = reachable from `main`, from every function outside `compiler/src`")
out.append("(the stdlib and the LSP, whose calls into the compiler are in `lsp.facts`),")
out.append("from destructors, from a method in an `implement trait` block (trait")
out.append("dispatch), and from a class method an ancestor class also declares")
out.append("(virtual dispatch).  Constructors are never candidates: a `new` leaves no")
out.append("call record.  The roots the records cannot see are in `keep.txt`, each")
out.append("found by a type-check, `make cross-check` or a read that refused its")
out.append("deletion.")
out.append("")
out.append("Round 2 is the same sweep over the tree round 1 left: a method that")
out.append("overrode a base method round 1 deleted loses its virtual-dispatch root.")
out.append("")
out.append("Deleted: **%d functions**, %d lines of declaration and body (the diff also" % (len(deleted), lines))
out.append("drops a blank line beside some).  Held (not deleted): %d." % len(held))
out.append("")
out.append("## Held")
out.append("")
out.append("| function | file | why |")
out.append("|---|---|---|")
for r in held:
    k = key(r) if key(r) in keep else r["name"]
    out.append("| `%s` | `%s:%s` | %s |" % (key(r), r["path"], r["decl"], keep[k]))
out.append("")
out.append("## Roots the call records cannot see (`keep.txt`)")
out.append("")
out.append("| function | why |")
out.append("|---|---|")
for k, w in keep.items():
    out.append("| `%s` | %s |" % (k, w))
out.append("")
out.append("## Deleted")
out.append("")
out.append("| round | function | role | file | lines |")
out.append("|---|---|---|---|---|")
for r in sorted(deleted, key=lambda r: (r["path"], int(r["decl"]))):
    out.append("| %d | `%s` | %s | `%s:%s` | %d |" % (r["round"], key(r), r["role"], r["path"], r["decl"],
                                                   int(r["last"]) - int(r["first"]) + 1))
with open(sys.argv[1], "w", encoding="utf-8") as fh:
    fh.write("\n".join(out) + "\n")
print("deleted %d (%d lines), held %d" % (len(deleted), lines, len(held)))
