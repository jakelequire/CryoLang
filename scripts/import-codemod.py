"""One-off: the name layer's import functions read the import's span from
the declaration header once ImportDeclNode becomes the ImportDecl payload.

Within the five functions that take an import (process_import and the four
helpers it calls), `node: ImportDeclNode*` becomes
`decl: DeclarationNode*, node: ImportDecl*`, every `node.span` becomes
`decl.span`, and every call passing `node` to one of them passes
`decl, node`.  (`d` is taken: those functions name their diagnostic `d`.)
Refuses to run unless each function is found exactly once.

Deletion point: the commit after the one that ran it.
"""
import pathlib
import re
import sys

PATH = pathlib.Path("compiler/src/compiler/resolver/name_resolution.cryo")
FUNCS = ["refuse_unoffered_import", "refuse_unknown_module_import",
         "refuse_item_or_module_import", "sub_module_index", "process_import"]

raw = PATH.read_bytes()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n")
lines = text.split("\n")

starts = {}
for f in FUNCS:
    hits = [i for i, l in enumerate(lines) if re.match(r"    %s\((mut )?&this, node: ImportDeclNode\*" % f, l)]
    if len(hits) != 1:
        sys.exit("%s: %d definitions" % (f, len(hits)))
    starts[f] = hits[0]

def body_end(i):
    # A method ends at the first line after it that is exactly "    }".
    for j in range(i + 1, len(lines)):
        if lines[j] == "    }":
            return j
    sys.exit("no end for line %d" % i)

calls = re.compile(r"this\.(%s)\(node\b" % "|".join(FUNCS))
count = 0
for f, i in starts.items():
    lines[i] = lines[i].replace("node: ImportDeclNode*", "decl: DeclarationNode*, node: ImportDecl*")
    for j in range(i + 1, body_end(i) + 1):
        new = lines[j].replace("node.span", "decl.span")
        new = calls.sub(lambda m: "this.%s(decl, node" % m.group(1), new)
        if new != lines[j]:
            count += 1
            lines[j] = new

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
PATH.write_bytes(out.encode("utf-8"))
print("rewrote %d lines in %d functions" % (count, len(starts)))
