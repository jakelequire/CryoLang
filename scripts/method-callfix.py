#!/usr/bin/env python3
"""Finish the mechanical half of the method conversion from the type
check's refusals.

One-off, run after `scripts/method-codemod.py`, the method form of the
function conversion's `fn-callfix.py`: the codemod made every field, list,
return value and parameter typed by the method class hold the header,
while a local holding a method became a header `m_decl` and a payload `m`.
This reads `cryo check` (or `cryo build`) output on stdin and rewrites at
each reported place:

* a call passing `m` where the callee now takes the header, refused as
  `no method named `f` found`: in that call's argument list a whole
  argument `m` becomes `m_decl`;
* a payload `m` returned, assigned or stored where the header is wanted,
  refused as `mismatched types` at `m`: it becomes `m_decl`;
* a payload member read through a header, refused as `no field or method
  `f` on type `DeclarationNode``: the receiver `e` becomes
  `DeclarationNode::method_of(e)`, when `f` is a member of the method
  payload;
* a payload projection `const m: MethodDecl* = DeclarationNode::method_of(m_decl);`
  the function never reads, reported as an unused variable: it is deleted.

`m_decl` is used only when it is declared (a local or a parameter) earlier
in the enclosing function.  Anything else is left for hand edits.

usage: cryo check src/main.cryo 2>&1 | python ../scripts/method-callfix.py
       (run from the project directory; the report's paths are relative to it)
"""
import re
import sys

PAY = "MethodDecl"
PROJ = "method_of"
NO_METHOD = re.compile(r"error\[E0358\]: no method named `(\w+)` found")
MISMATCH = re.compile(r"error\[E0200\]: mismatched types")
NO_FIELD = re.compile(r"error\[E0204\]: no field or method `(\w+)` on type `[\w:]*DeclarationNode`")
UNUSED = re.compile(r"warning\[W0001\]: unused variable `(\w+)`")
LOC = re.compile(r"^\s*--> (\S+):(\d+):(\d+)")
MEMBERS = set(("func visibility is_constructor is_destructor is_static is_default_destructor "
               "is_virtual is_override is_sink base_ctor_name base_ctor_args spec_of has_base_ctor "
               "set_visibility set_constructor set_destructor set_static set_default_destructor "
               "set_virtual set_override set_sink set_base_ctor_name add_base_ctor_arg").split())
HEAD = re.compile(r"(?m)^ {0,4}(?:static |public )*(?:function )?[a-z_][a-z0-9_]*(?:<[^>]*>)?\(")

text = sys.stdin.read().splitlines()
sites = []
for i, line in enumerate(text):
    loc = LOC.match(text[i + 1]) if i + 1 < len(text) else None
    if not loc:
        continue
    where = (loc.group(1), int(loc.group(2)), int(loc.group(3)))
    m = NO_METHOD.search(line)
    if m:
        sites.append(where + ("call", m.group(1)))
        continue
    if MISMATCH.search(line):
        sites.append(where + ("value", None))
        continue
    m = NO_FIELD.search(line)
    if m and m.group(1) in MEMBERS:
        sites.append(where + ("member", m.group(1)))
        continue
    m = UNUSED.search(line)
    if m:
        sites.append(where + ("unused", m.group(1)))

by_file = {}
for path, line, col, kind, name in sites:
    by_file.setdefault(path, []).append((line, col, kind, name))


def offset_of(src, line, col):
    lines = src.splitlines(keepends=True)
    return sum(len(l) for l in lines[:line - 1]) + col - 1


def declared_in_function(src, at, n):
    head = 0
    for h in HEAD.finditer(src[:at]):
        head = h.start()
    return re.search(r"\b" + n + r"_decl: DeclarationNode\*", src[head:at]) is not None


def receiver_start(src, dot):
    i = dot
    while i > 0:
        c = src[i - 1]
        if c.isalnum() or c in "_.:":
            i -= 1
        elif c in ")]":
            close, open_ = c, "(" if c == ")" else "["
            depth, i = 1, i - 1
            while i > 0 and depth > 0:
                i -= 1
                if src[i] == close:
                    depth += 1
                elif src[i] == open_:
                    depth -= 1
        else:
            break
    return i


for path, items in by_file.items():
    with open(path, encoding="utf-8", newline="") as h:
        src = h.read()
    fixed = 0
    for line, col, kind, name in sorted(items, key=lambda t: (t[0], t[1]), reverse=True):
        at = offset_of(src, line, col)
        if kind == "call":
            k = src.find(name + "(", at)
            if k < 0:
                continue
            j = k + len(name) + 1
            depth, args_start = 1, j
            while j < len(src) and depth > 0:
                if src[j] == "(":
                    depth += 1
                elif src[j] == ")":
                    depth -= 1
                j += 1
            args = src[args_start:j - 1]

            def swap(m):
                if declared_in_function(src, k, m.group(2)):
                    return m.group(1) + m.group(2) + "_decl"
                return m.group(0)

            new_args = re.sub(r"(^\s*|,\s*)([a-z_][a-z0-9_]*)(?=\s*(?:,|$))", swap, args)
            if new_args != args:
                src = src[:args_start] + new_args + src[j - 1:]
                fixed += 1
        elif kind == "value":
            m = re.match(r"([a-z_][a-z0-9_]*)\b(?!\s*[.(\[])", src[at:])
            if m and declared_in_function(src, at, m.group(1)):
                src = src[:at] + m.group(1) + "_decl" + src[at + len(m.group(1)):]
                fixed += 1
        elif kind == "member":
            if src[at:at + len(name)] != name or src[at - 1] != ".":
                continue
            s = receiver_start(src, at - 1)
            recv = src[s:at - 1]
            if not recv or recv.startswith("DeclarationNode::" + PROJ + "("):
                continue
            src = src[:s] + "DeclarationNode::" + PROJ + "(" + recv + ")" + src[at - 1:]
            fixed += 1
        elif kind == "unused":
            ls = src.rfind("\n", 0, at) + 1
            le = src.find("\n", at) + 1
            proj = r"\s*const " + name + r": " + PAY + r"\* = DeclarationNode::" + PROJ + r"\(" + name + r"_decl\);\r?\n"
            if re.fullmatch(proj, src[ls:le]):
                src = src[:ls] + src[le:]
                fixed += 1
    with open(path, "w", encoding="utf-8", newline="") as h:
        h.write(src)
    print(f"{path}: {fixed} of {len(items)}")
