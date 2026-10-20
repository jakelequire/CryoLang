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
With `--factory NAME`, constructions become factory calls, and a local the
factory builds is the header: up to the next method, or the next
declaration of the same name, its payload's fields and instance methods
(read from `type struct NEW_STRUCT` in the AST's expression.cryo) are
reached through the projection, `ExpressionNode::PROJECTION(local).field`,
and its casts to `ExpressionNode*` are dropped.  Run again after the
struct is written, the rewrites before it find nothing left to change.
Match arms on `NodeKind::KIND` and a test of a kind held in a variable are
converted by hand.

usage: python scripts/expr-kind-codemod.py --thread-header NEW_STRUCT PARAM FILE...
  e.g. --thread-header CallExpr call compiler/src/compiler/sema/call_resolver.cryo

A payload has no span or type of its own.  In each file, a method with a
parameter `PARAM: NEW_STRUCT*` whose body reads a header member through it
(`PARAM.span`, `PARAM.resolved_type`, ...), or passes `PARAM` to such a
method of the same file, gains `PARAM_e: ExpressionNode*` before it; its
reads go through `PARAM_e`, and every `this.method(...)` of the file that
passes `PARAM` to such a method passes `PARAM_e, PARAM`.  Callers in other
files, and callers that hold the payload under another name, are fixed by
hand: the build refuses each.
"""
import os
import re
import sys

HEADER_MEMBERS = ["span", "resolved_type", "set_resolved_type", "has_resolved_type",
                  "generated", "has_error", "autoref", "arg_binding", "shape"]


def split_args(text):
    """Top-level comma-separated arguments of a call's parenthesized text."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        cur += ch
    out.append(cur)
    return out


def matching_paren(src, i):
    """Index of the `)` matching the `(` at `i`."""
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "(":
            depth += 1
        elif src[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


def thread_header(struct, param, paths):
    head = re.compile(r"^    (?:static\s+)?(\w+)(?:<[^>]*>)?\(", re.M)
    hdr = param + "_e"
    reads = re.compile(r"(?<![\w.])" + param + r"\.(" + "|".join(HEADER_MEMBERS) + r")\b|(?<![\w.])"
                       + param + r" as (?:ExpressionNode|ASTNode)\*")
    for p in paths:
        src = open(p, encoding="utf-8", newline="").read()
        heads = list(head.finditer(src))
        methods = []
        for k, m in enumerate(heads):
            end = heads[k + 1].start() if k + 1 < len(heads) else len(src)
            close = matching_paren(src, m.end() - 1)
            params = src[m.end():close]
            methods.append((m.group(1), m.start(), close, end, params))
        has_param = {name for (name, _, _, _, params) in methods
                     if re.search(r"\b" + param + r"\s*:\s*" + struct + r"\*", params)}
        needs = set()
        for (name, s, close, end, _) in methods:
            if name in has_param and reads.search(src, close, end):
                needs.add(name)
        calls = re.compile(r"this\.(\w+)\(")
        changed = True
        while changed:
            changed = False
            for (name, s, close, end, _) in methods:
                if name not in has_param or name in needs:
                    continue
                for c in calls.finditer(src, close, end):
                    if c.group(1) not in needs:
                        continue
                    args = split_args(src[c.end():matching_paren(src, c.end() - 1)])
                    if any(a.strip() == param for a in args):
                        needs.add(name)
                        changed = True
                        break
        # Rewrite from the end so offsets stay valid.
        edits = []
        for (name, s, close, end, params) in methods:
            body_start = close
            if name in needs:
                sig = src[s:close]
                sig = re.sub(r"\b" + param + r"(\s*):(\s*)" + struct + r"\*",
                             hdr + r": ExpressionNode*, " + param + r": " + struct + "*", sig, count=1)
                edits.append((s, close, sig))
            body = src[body_start:end]
            if name in needs:
                body = re.sub(r"(?<![\w.])" + param + r"\.(" + "|".join(HEADER_MEMBERS) + r")\b",
                              lambda r: hdr + "." + r.group(1), body)
                body = re.sub(r"(?<![\w.])" + param + r" as ExpressionNode\*", hdr, body)
                body = re.sub(r"(?<![\w.])" + param + r" as ASTNode\*", hdr + " as ASTNode*", body)
            out, pos = "", 0
            for c in calls.finditer(body):
                if c.group(1) not in needs:
                    continue
                close_c = matching_paren(body, c.end() - 1)
                args = split_args(body[c.end():close_c])
                if not any(a.strip() == param for a in args):
                    continue
                new_args = []
                for a in args:
                    if a.strip() == param:
                        lead = a[:len(a) - len(a.lstrip())]
                        new_args.append(lead + hdr + ", " + param)
                    else:
                        new_args.append(a)
                out += body[pos:c.end()] + ",".join(new_args)
                pos = close_c
            out += body[pos:]
            edits.append((body_start, end, out))
        for (a, b, text) in sorted(edits, key=lambda e: e[0], reverse=True):
            src = src[:a] + text + src[b:]
        open(p, "w", encoding="utf-8", newline="").write(src)
        print(p, "methods threaded:", len(needs))


if len(sys.argv) > 1 and sys.argv[1] == "--thread-header":
    thread_header(sys.argv[2], sys.argv[3], sys.argv[4:])
    sys.exit(0)

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


def payload_members():
    """The fields and instance methods of `type struct NEW_STRUCT`, or none
    while the struct is not written yet."""
    for root in roots:
        for dp, _, fs in os.walk(root):
            if "expression.cryo" in fs and dp.replace("\\", "/").endswith("AST"):
                src = open(os.path.join(dp, "expression.cryo"), encoding="utf-8").read()
                m = re.search(r"^type struct " + new + r" \{\r?\n(.*?)^\}", src, re.M | re.S)
                if not m:
                    return []
                body = m.group(1)
                fields = re.findall(r"^    (\w+)\s*:", body, re.M)
                methods = re.findall(r"^    (?!static\b)(\w+)(?:<[^>]*>)?\(", body, re.M)
                return fields + methods
    return []


METHOD_HEAD = re.compile(r"^    (?:static\s+)?\w+(?:<[^>]*>)?\(", re.M)


def header_locals(src, members):
    """Reach a factory-built local's payload members through the projection."""
    if not factory or not members:
        return src
    decl = re.compile(r"(?:const|mut)\s+(\w+)\s*:\s*ExpressionNode\*\s*=\s*ExpressionNode::"
                      + factory + r"\(")
    member_alt = "|".join(sorted(set(members), key=len, reverse=True))
    for m in reversed(list(decl.finditer(src))):
        name = m.group(1)
        start = m.end()
        stop = len(src)
        head = METHOD_HEAD.search(src, start)
        if head:
            stop = head.start()
        again = re.compile(r"(?:const|mut)\s+" + re.escape(name) + r"\s*:").search(src, start, stop)
        if again:
            stop = again.start()
        region = src[start:stop]
        region = re.sub(r"(?<![\w.])" + re.escape(name) + r"\.(" + member_alt + r")\b",
                        lambda r: "ExpressionNode::%s(%s).%s" % (proj, name, r.group(1)), region)
        region = re.sub(r"(?<![\w.])" + re.escape(name) + r" as ExpressionNode\*", name, region)
        src = src[:start] + region + src[stop:]
    return src


members = payload_members()
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
                out = header_locals(out, members)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as fh:
                    fh.write(out)
                changed += 1
                print(p)
print("files changed:", changed)
