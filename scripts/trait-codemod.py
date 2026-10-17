#!/usr/bin/env python3
"""Rewrite reads of the trait declaration class to the `TraitDecl` payload.

One-off: `TraitDeclNode` (a subclass of `DeclarationNode` with its own
`NodeKind::TraitDeclaration`) becomes the payload `TraitDecl` under
`DeclKind::Trait`.  This rewrites the mechanical forms:

* `X as TraitDeclNode*` (and `X as declaration::TraitDeclNode*`), a cast of
  a node handle, becomes `DeclarationNode::trait_of(X)` (qualified the same
  way), the bridge helper that answers the payload or null;
* `X.kind == NodeKind::TraitDeclaration` becomes
  `DeclarationNode::trait_of(X) != null`, and `!=` the negation;
* every other spelling of the type - a variable's, a parameter's, a list's,
  an import - becomes `TraitDecl`.

`new TraitDeclNode(...)` and the `NodeKind::TraitDeclaration` arms of a
`match` are left for hand edits, and reported.  Deleted in the commit after
the one that ran it.

usage: python scripts/trait-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

EXPR = r"((?:this\.)?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
CAST = re.compile(EXPR + r" as (declaration::|ast::declaration::)?TraitDeclNode\*")
EQ = re.compile(EXPR + r"\.kind (==|!=) (?:ast::)?NodeKind::TraitDeclaration\b")
NAME = re.compile(r"\bTraitDeclNode\b(?!\()")


def cast(m):
    return f"{m.group(2) or ''}DeclarationNode::trait_of({m.group(1)})"


def eq(m):
    op = "!=" if m.group(2) == "==" else "=="
    return f"DeclarationNode::trait_of({m.group(1)}) {op} null"


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
            out = re.sub(r"new TraitDeclNode\(", "new TRAIT_DECL_NODE_CTOR(", out)
            out = NAME.sub("TraitDecl", out)
            out = out.replace("new TRAIT_DECL_NODE_CTOR(", "new TraitDeclNode(")
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new TraitDeclNode\(|NodeKind::TraitDeclaration", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
