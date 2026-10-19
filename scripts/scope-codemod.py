"""One-off codemod for the path (`scope::member`) payload conversion.

Rewrites, in every .cryo file under the roots given:
  * `X as ScopeResolutionNode*`        -> `ExpressionNode::scope_resolution_of(X)`
    (X a dotted/indexed name or a parenthesized expression with no nested
    parentheses; a parenthesized `(X as ScopeResolutionNode*)` loses its
    parentheses)
  * `X.kind == NodeKind::ScopeResolution` -> `ExpressionNode::scope_resolution_of(X) != null`
    and `!=` -> `== null`
  * every remaining `ScopeResolutionNode` -> `ScopeResolutionExpr`
Match arms on `NodeKind::ScopeResolution`, `k == NodeKind::ScopeResolution`
on a kind variable, and constructions are converted by hand; a construction
left behind reads `new ScopeResolutionExpr(` and is refused by the build.

usage: python scripts/scope-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

OPERAND = r"([A-Za-z_][\w.]*(?:\[[^\[\]]*\])*(?:\.[A-Za-z_]\w*)*|\([^()]*\))"
# Not after a name or a closing bracket: there the parenthesis is a call's.
CAST_PAREN = re.compile(r"(?<![\w)\]])\(\s*" + OPERAND + r"\s+as\s+ScopeResolutionNode\*\s*\)")
CAST = re.compile(OPERAND + r"\s+as\s+ScopeResolutionNode\*")
KIND = re.compile(OPERAND + r"\.kind\s*(==|!=)\s*NodeKind::ScopeResolution\b")
NAME = re.compile(r"\bScopeResolutionNode\b")


def kind_sub(m):
    test = "!= null" if m.group(2) == "==" else "== null"
    return "ExpressionNode::scope_resolution_of(%s) %s" % (m.group(1), test)


changed = 0
for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as fh:
                src = fh.read()
            out = CAST_PAREN.sub(lambda m: "ExpressionNode::scope_resolution_of(%s)" % m.group(1), src)
            out = CAST.sub(lambda m: "ExpressionNode::scope_resolution_of(%s)" % m.group(1), out)
            out = KIND.sub(kind_sub, out)
            out = NAME.sub("ScopeResolutionExpr", out)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as fh:
                    fh.write(out)
                changed += 1
                print(p)
print("files changed:", changed)
