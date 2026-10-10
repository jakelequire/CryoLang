"""An index of the compiler's functions: for each, its qualified name as the
spelling-flow list writes it (`compiler::mod::Type.method`, or
`compiler::mod::function`), its file, its line span, whether it carries an
allow of `lookup_by_spelling` (and the reason) and whether it is a door.

Heuristic parser: a type block opens at column 0 with `type struct X`,
`type class X`, `type enum X`, `type union X`, `implement struct X`,
`implement trait T for struct X`, `implement class X`...; a function is a
line inside a block at 4 spaces of indent of the form `[static ]name(` or
`[override ]...name(`, or `function name(` at column 0.  A function's span
runs to the line before the next function or block start."""
import json
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "compiler/src"
OUT = sys.argv[2] if len(sys.argv) > 2 else ".objcmp/s134/xcheck/index.json"

block_re = re.compile(r"^(?:type\s+(?:struct|class|enum|union|trait)|implement\s+(?:trait\s+\S+(?:<[^>]*>)?\s+for\s+)?(?:struct|class|enum|union|trait))\s+([A-Za-z_][A-Za-z_0-9]*)")
fn_in_block = re.compile(r"^    (?:static\s+|override\s+|virtual\s+)*([A-Za-z_][A-Za-z_0-9]*)\s*(?:<[^()]*>)?\s*\(")
fn_top = re.compile(r"^(?:public\s+|private\s+)?function\s+([A-Za-z_][A-Za-z_0-9]*)")
ns_re = re.compile(r"^namespace\s+([A-Za-z_:0-9]+)\s*;")
allow_re = re.compile(r'!\[allow\(lookup_by_spelling,\s*reason\s*=\s*"([^"]*)"')
KEYWORDS = {"if", "for", "while", "match", "return", "switch"}

funcs = []
for dp, dn, fn in os.walk(ROOT):
    for f in fn:
        if not f.endswith(".cryo"):
            continue
        path = os.path.join(dp, f).replace("\\", "/")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        ns = ""
        cur_type = None
        pending_doc = []
        pending_allow = None
        open_funcs = []
        for i, l in enumerate(lines, 1):
            m = ns_re.match(l)
            if m:
                ns = m.group(1)
                continue
            m = block_re.match(l)
            if m:
                cur_type = m.group(1)
                if open_funcs:
                    open_funcs[-1]["end"] = i - 1
                    open_funcs = []
                pending_doc = []
                pending_allow = None
                continue
            if l.startswith("}"):
                if open_funcs:
                    open_funcs[-1]["end"] = i - 1
                    open_funcs = []
                if not l.startswith("} "):
                    cur_type = cur_type  # a function body's close at col 0 is a top function's
                continue
            s = l.strip()
            if s.startswith("///"):
                pending_doc.append(s)
                continue
            m = allow_re.search(l)
            if m and s.startswith("!["):
                pending_allow = m.group(1)
                continue
            if s.startswith("!["):
                continue
            name = None
            m = fn_top.match(l)
            if m:
                name = m.group(1)
                owner = None
                cur_type = None
            else:
                m = fn_in_block.match(l)
                if m and cur_type and m.group(1) not in KEYWORDS:
                    name = m.group(1)
                    owner = cur_type
            if name:
                if open_funcs:
                    open_funcs[-1]["end"] = i - 1
                door = any(d.startswith("/// Door `") for d in pending_doc)
                q = ns + "::" + (owner + "." + name if owner else name)
                rec = {"q": q, "file": path, "start": i, "end": len(lines), "allow": pending_allow,
                       "door": door, "name": name, "owner": owner}
                funcs.append(rec)
                open_funcs = [rec]
            if s and not s.startswith("///"):
                pending_doc = []
                if not s.startswith("!["):
                    pending_allow = None
json.dump(funcs, open(OUT, "w"), indent=0)
print("indexed", len(funcs), "functions;", sum(1 for f in funcs if f["allow"]), "allowed;",
      sum(1 for f in funcs if f["door"]), "doors")
