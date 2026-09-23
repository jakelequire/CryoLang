#!/usr/bin/env python3
"""Every site in compiler/src and tools that compares a name with the RECEIVER'S
SPELLING: against a symbol interned from "this" / "&this", or against the
string itself.

A parameter carries whether it is the method's receiver (`VarDeclNode.receiver`,
set where the receiver is minted), so no site asks a PARAMETER by that spelling
(section 8.305).  What this prints are the sites that ask an IDENTIFIER USE
whether it is the keyword `this` - a use has no declaration to carry a mark.
The count is pinned in section 0, so a new spelling check of either kind moves
it.

Rule 1c cannot see most of these: it recognises a scan of an array element's
interned field, and these compare a resolved string, one parameter handed in,
or an identifier.  Of the 57 parameter sites section 8.305 converted, 10 were
of that kind and in no residue row.

Method: (1) collect every name assigned `intern("this")` / `intern("&this")`,
per file (fields across files); (2) report every line comparing against one of
them (`.equals(X)`, `== X.id`, `X.id ==`, `!= X.id`), or comparing a string with
the literal.

Usage: python scripts/ns-migration/receiver-spelling-sites.py
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = [os.path.join(ROOT, "compiler", "src"), os.path.join(ROOT, "tools")]
ASSIGN = re.compile(r'\b((?:this\.)?[A-Za-z_][A-Za-z_0-9]*)\s*(?::\s*SymbolStr\s*)?=\s*[A-Za-z_.()]*\bintern\(\s*"(&this|this)"\s*\)')
FIELD_INIT = re.compile(r'\b([A-Za-z_][A-Za-z_0-9]*)\s*:\s*[A-Za-z_.()]*\bintern\(\s*"(&this|this)"\s*\)')
STRCMP = re.compile(r'(==\s*"(&this|this)"|"(&this|this)"\s*==|\.eq\(\s*"(&this|this)"\s*\))')
INLINE = re.compile(r'(\.equals\(|==|!=)[^;]*\bintern\(\s*"(&this|this)"\s*\)')


def main():
    files = []
    for d in DIRS:
        for dp, _, fs in os.walk(d):
            for f in fs:
                if f.endswith(".cryo"):
                    files.append(os.path.join(dp, f))
    texts = {p: open(p, encoding="utf-8", errors="replace").read().split("\n") for p in files}
    fieldnames = set()
    per_file = {}
    for p, lines in texts.items():
        names = set()
        for line in lines:
            code = line.split("//")[0]
            for m in ASSIGN.finditer(code):
                n = m.group(1)
                names.add(n.split(".")[-1])
                if n.startswith("this."):
                    fieldnames.add(n.split(".")[-1])
            for m in FIELD_INIT.finditer(code):
                fieldnames.add(m.group(1))
                names.add(m.group(1))
        per_file[p] = names
    out = []
    for p, lines in texts.items():
        names = per_file[p] | fieldnames
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        for i, line in enumerate(lines):
            code = line.split("//")[0]
            if not code.strip():
                continue
            hit = None
            for n in sorted(names):
                e = re.escape(n)
                if (re.search(r'\.equals\(\s*(?:this\.)?%s\s*\)' % e, code)
                        or re.search(r'==\s*(?:this\.)?%s\.id\b' % e, code)
                        or re.search(r'\b(?:this\.)?%s\.id\s*==' % e, code)
                        or re.search(r'!=\s*(?:this\.)?%s\.id\b' % e, code)):
                    hit = n
                    break
            if hit is None and (STRCMP.search(code) or INLINE.search(code)):
                hit = "<string>"
            if hit is not None:
                out.append((rel, i + 1, hit, code.strip()))
    for r in sorted(out):
        print("%s:%d\t%s\t%s" % r)
    print("receiver-spelling-sites: %d" % len(out), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
