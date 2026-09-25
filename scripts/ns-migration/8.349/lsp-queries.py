"""Write the completion queries for the lspf fixture, positions found by marker.

Each query: (label, line substring, cursor-after substring, optional replace).
The cursor sits just after `cursor_after` within the first line containing
`line_has` (after the optional replace is applied to the text).
"""
import json
import os
import sys

F = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lspf", "src")

def build(fname, specs):
    with open(os.path.join(F, fname), encoding="utf-8") as fh:
        text = fh.read()
    out = []
    for label, line_has, after, rep in specs:
        t = text
        if rep:
            t = t.replace(rep[0], rep[1], 1)
        lines = t.split("\n")
        li = next(i for i, l in enumerate(lines) if line_has in l)
        ci = lines[li].index(after) + len(after)
        q = {"label": label, "method": "completion", "line": li, "character": ci}
        if rep:
            q["replace"] = list(rep)
            text = t
        out.append(q)
    return out

main = build("main.cryo", [
    ("dot wb.",               "wb.get()",            "wb.",            None),
    ("dot f.",                "f.width()",           "f.",             None),
    ("scope B::Item::",       "B::Item::make(4)",    "B::Item::",      None),
    ("scope C::",             "C::Formatter::mk()",  "C::",            None),
    ("scope C::Formatter::",  "C::Formatter::mk()",  "C::Formatter::", None),
    ("scope bare Item::",     "Item::make(4)",       " Item::",        ("B::Item::make(4)", "Item::make(4)")),
])
d = build("d.cryo", [
    ("d dot f.",              "f.width()",           "f.",             None),
    ("d scope Formatter::",   "Formatter::mk()",     "Formatter::",    None),
    ("d half-typed Formatter::", "= Formatter::",    "= Formatter::",  ("Formatter::mk();", "Formatter::")),
])
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "q-main.json"), "w", encoding="utf-8") as fh:
    json.dump(main, fh, indent=1)
with open(os.path.join(here, "q-d.json"), "w", encoding="utf-8") as fh:
    json.dump(d, fh, indent=1)
print("QUERIES_DONE", len(main), len(d))
