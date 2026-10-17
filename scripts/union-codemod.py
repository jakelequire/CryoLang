#!/usr/bin/env python3
"""Rewrite reads of the union declaration class to the `UnionDecl` payload.

One-off: `UnionDeclNode` (a subclass of `DeclarationNode` with its own
`NodeKind::UnionDeclaration`) becomes the payload `UnionDecl` under
`DeclKind::Union`.  This rewrites the mechanical forms:

* `X as UnionDeclNode*` (and `X as declaration::UnionDeclNode*`), a cast of
  a node handle, becomes `DeclarationNode::union_of(X)` (qualified the same
  way), the bridge helper that answers the payload or null;
* `X.kind == NodeKind::UnionDeclaration` becomes
  `DeclarationNode::union_of(X) != null`, and `!=` the negation;
* every other spelling of the type - a variable's, a parameter's, a list's,
  an import - becomes `UnionDecl`.

`new UnionDeclNode(...)` and the `NodeKind::UnionDeclaration` arms of a
`match` are left for hand edits, and reported.  Deleted in the commit after
the one that ran it.

usage: python scripts/union-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

EXPR = r"((?:this\.)?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
CAST = re.compile(EXPR + r" as (declaration::|ast::declaration::)?UnionDeclNode\*")
EQ = re.compile(EXPR + r"\.kind (==|!=) (?:ast::)?NodeKind::UnionDeclaration\b")
NAME = re.compile(r"\bUnionDeclNode\b(?!\()")


def cast(m):
    return f"{m.group(2) or ''}DeclarationNode::union_of({m.group(1)})"


def eq(m):
    op = "!=" if m.group(2) == "==" else "=="
    return f"DeclarationNode::union_of({m.group(1)}) {op} null"


for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in sorted(fs):
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as h:
                src = h.read()
            out = CAST.sub(cast, src)
            out = EQ.sub(eq, out)
            out = re.sub(r"new UnionDeclNode\(", "new UNION_DECL_NODE_CTOR(", out)
            out = NAME.sub("UnionDecl", out)
            out = out.replace("new UNION_DECL_NODE_CTOR(", "new UnionDeclNode(")
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new UnionDeclNode\(|NodeKind::UnionDeclaration", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
