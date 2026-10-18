#!/usr/bin/env python3
"""Rewrite reads of the struct declaration class to the `StructDecl` payload.

One-off: `StructDeclNode` (a subclass of `DeclarationNode` with its own
`NodeKind::StructDeclaration`) becomes the payload `StructDecl` under
`DeclKind::Struct`.  This rewrites the mechanical forms:

* `X as StructDeclNode*` (and `X as declaration::StructDeclNode*`), a cast
  of a node handle, becomes `DeclarationNode::struct_of(X)` (qualified the
  same way), the bridge helper that answers the payload or null;
* `X.kind == NodeKind::StructDeclaration` becomes
  `DeclarationNode::struct_of(X) != null`, and `!=` the negation;
* every other spelling of the type - a variable's, a parameter's, a list's,
  an import, a comment's - becomes `StructDecl`.

`new StructDeclNode(...)`, the setters the class had, and the
`NodeKind::StructDeclaration` arms of a `match` are left for hand edits, and
the constructions and arms are reported.  Deleted in the commit after the one
that ran it.

usage: python scripts/struct-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

EXPR = r"((?:this\.)?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
CAST = re.compile(EXPR + r" as (declaration::|ast::declaration::)?StructDeclNode\*")
EQ = re.compile(EXPR + r"\.kind (==|!=) (?:ast::)?NodeKind::StructDeclaration\b")
NAME = re.compile(r"\bStructDeclNode\b(?!\()")


def cast(m):
    return f"{m.group(2) or ''}DeclarationNode::struct_of({m.group(1)})"


def eq(m):
    op = "!=" if m.group(2) == "==" else "=="
    return f"DeclarationNode::struct_of({m.group(1)}) {op} null"


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
            out = re.sub(r"new StructDeclNode\(", "new STRUCT_DECL_NODE_CTOR(", out)
            out = NAME.sub("StructDecl", out)
            out = out.replace("new STRUCT_DECL_NODE_CTOR(", "new StructDeclNode(")
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new StructDeclNode\(|NodeKind::StructDeclaration", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
