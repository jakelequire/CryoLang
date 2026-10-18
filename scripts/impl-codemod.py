#!/usr/bin/env python3
"""Rewrite reads of the implement-block class to the `ImplBlock` payload.

One-off: `ImplBlockNode` (a subclass of `DeclarationNode` with its own
`NodeKind::ImplementationBlock`) becomes the payload `ImplBlock` under
`DeclKind::Impl`.  This rewrites the mechanical forms:

* `X as ImplBlockNode*` (and `X as declaration::ImplBlockNode*`), a cast of
  a node handle, becomes `DeclarationNode::impl_of(X)` (qualified the same
  way), the bridge helper that answers the payload or null;
* `X.kind == NodeKind::ImplementationBlock` becomes
  `DeclarationNode::impl_of(X) != null`, and `!=` the negation;
* every other spelling of the type - a variable's, a parameter's, a list's,
  an import, a comment's - becomes `ImplBlock`.

`new ImplBlockNode(...)`, the setters the class had, and the
`NodeKind::ImplementationBlock` arms of a `match` are left for hand edits,
and the constructions and arms are reported.  Deleted in the commit after
the one that ran it.

usage: python scripts/impl-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

EXPR = r"((?:this\.)?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
CAST = re.compile(EXPR + r" as (declaration::|ast::declaration::)?ImplBlockNode\*")
EQ = re.compile(EXPR + r"\.kind (==|!=) (?:ast::)?NodeKind::ImplementationBlock\b")
NAME = re.compile(r"\bImplBlockNode\b(?!\()")


def cast(m):
    return f"{m.group(2) or ''}DeclarationNode::impl_of({m.group(1)})"


def eq(m):
    op = "!=" if m.group(2) == "==" else "=="
    return f"DeclarationNode::impl_of({m.group(1)}) {op} null"


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
            out = re.sub(r"new ImplBlockNode\(", "new IMPL_BLOCK_NODE_CTOR(", out)
            out = NAME.sub("ImplBlock", out)
            out = out.replace("new IMPL_BLOCK_NODE_CTOR(", "new ImplBlockNode(")
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new ImplBlockNode\(|NodeKind::ImplementationBlock", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
