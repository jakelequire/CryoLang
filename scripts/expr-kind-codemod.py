"""Codemod for converting one expression class to a payload behind the
expression bridge.  Deleted in the commit after the last expression kind
converts.

usage: python scripts/expr-kind-codemod.py OLD_CLASS NEW_STRUCT KIND PROJECTION [--factory NAME] ROOT...
  e.g. MemberAccessNode MemberAccessExpr MemberAccess member_access_of compiler/src tools/CryoLSP/src

Rewrites, in every .cryo file under the roots:
  * `X as OLD_CLASS*`         -> `ExpressionNode::PROJECTION(X)`
    (X a dotted/indexed name or a parenthesized expression with no nested
    parentheses; a parenthesized `(X as OLD_CLASS*)` loses its parentheses
    unless they are a call's)
  * `X.kind == NodeKind::KIND` -> `ExpressionNode::PROJECTION(X) != null`,
    and `!=` -> `== null`
  * every remaining `OLD_CLASS` -> `NEW_STRUCT`
Match arms on `NodeKind::KIND`, a test of a kind held in a variable, and
constructions are converted by hand; a construction left behind reads
`new NEW_STRUCT(` and is refused by the build.
"""
import os
import re
import sys

old, new, kind, proj = sys.argv[1:5]
roots = sys.argv[5:]
# `--factory NAME`: also rewrite constructions whose arguments the factory
# takes in the constructor's order.
factory = None
if roots and roots[0] == "--factory":
    factory = roots[1]
    roots = roots[2:]
OPERAND = r"([A-Za-z_][\w.]*(?:\[[^\[\]]*\])*(?:\.[A-Za-z_]\w*)*|\([^()]*\))"
# Not after a name or a closing bracket: there the parenthesis is a call's.
CAST_PAREN = re.compile(r"(?<![\w)\]])\(\s*" + OPERAND + r"\s+as\s+" + old + r"\*\s*\)")
CAST = re.compile(OPERAND + r"\s+as\s+" + old + r"\*")
KIND = re.compile(OPERAND + r"\.kind\s*(==|!=)\s*NodeKind::" + kind + r"\b")
NAME = re.compile(r"\b" + old + r"\b")


def projected(m):
    return "ExpressionNode::%s(%s)" % (proj, m.group(1))


def kind_sub(m):
    test = "!= null" if m.group(2) == "==" else "== null"
    return "ExpressionNode::%s(%s) %s" % (proj, m.group(1), test)


changed = 0
for root in roots:
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as fh:
                src = fh.read()
            out = CAST_PAREN.sub(projected, src)
            out = CAST.sub(projected, out)
            out = KIND.sub(kind_sub, out)
            out = NAME.sub(new, out)
            if factory:
                # A construction builds the header: `T* = new T(` declares the
                # header, and any other `new T(` is the factory call.
                out = re.sub(new + r"\*(\s*)=(\s*)new " + new + r"\(",
                             lambda m: "ExpressionNode*" + m.group(1) + "=" + m.group(2)
                             + "ExpressionNode::" + factory + "(", out)
                out = out.replace("new " + new + "(", "ExpressionNode::" + factory + "(")
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as fh:
                    fh.write(out)
                changed += 1
                print(p)
print("files changed:", changed)
