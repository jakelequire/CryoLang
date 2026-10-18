#!/usr/bin/env python3
"""Finish the mechanical half of the function conversion from the type
check's refusals.

One-off, run after `scripts/fn-codemod.py`: that codemod made every field,
list, return value and parameter typed by the function class hold the
header, while a local holding a function became a header `f_decl` and a
payload `f`.  The type check then refuses three forms, which this reads
from `cryo check` output on stdin and rewrites at each reported place:

* a call passing `f` where the callee now takes the header, refused as
  `no method named `m` found` (the argument types pick no overload): in
  that call's argument list a whole argument `f` becomes `f_decl`;
* a payload `f` returned, assigned or stored where the header is wanted,
  refused as `mismatched types` at `f`: it becomes `f_decl`;
* a payload member read through a header, refused as `no field or method
  `m` on type `DeclarationNode``: the receiver `e` becomes
  `DeclarationNode::fn_of(e)`, when `m` is a member of the function payload;
* a payload projection `const f: FunctionDecl* = DeclarationNode::fn_of(f_decl);`
  the function never reads, reported as an unused variable: it is deleted.

`f_decl` is used only when it is declared (a local or a parameter) earlier
in the enclosing function.  Anything else the check refuses is left for
hand edits; a rewrite that picks wrongly is refused again by the next check.
The first run looked `f_decl` up over the whole file; the wrong picks that
produced (a template entry `tmpl` named `tmpl_decl` in six calls) were
refused by the check and put back by hand.

usage: cryo check src/main.cryo 2>&1 | python ../scripts/fn-callfix.py
       (run from compiler/, the paths in the report are relative to it)
"""
import re
import sys

NO_METHOD = re.compile(r"error\[E0358\]: no method named `(\w+)` found")
MISMATCH = re.compile(r"error\[E0200\]: mismatched types")
NO_FIELD = re.compile(r"error\[E0204\]: no field or method `(\w+)` on type `[\w:]*DeclarationNode`")
UNUSED = re.compile(r"warning\[W0001\]: unused variable `(\w+)`")
LOC = re.compile(r"^\s*--> (\S+):(\d+):(\d+)")
MEMBERS = set(("name parameters generic_params trait_bounds body is_public is_static is_async "
               "is_inline is_noinline is_caller_located written_in is_variadic is_implicit "
               "is_synthesized_body is_synth_default is_intrinsic is_self_returning_default "
               "is_lazy_self_growing link_name has_link_name is_weak is_naked link_section "
               "return_type_annotation resolved_return_type origin_trait async_fut_slot "
               "spec_type_args overload_entry is_specialization def copy_of instance has_body "
               "is_generic has_resolved_return_type add_parameter add_generic_param "
               "add_trait_bound set_name set_body set_public set_static set_async set_variadic "
               "set_synthesized_body set_synth_default set_intrinsic set_self_returning_default "
               "set_lazy_self_growing set_return_type_annotation set_resolved_return_type "
               "set_origin_trait set_async_fut_slot set_spec_type_args set_overload_entry").split())
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
    """Start of the postfix expression ending just before `src[dot] == '.'`."""
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
    # bottom-up, so an edit never moves a later site
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

            new_args = re.sub(r"(^|,\s*)([a-z_][a-z0-9_]*)(?=\s*(?:,|$))", swap, args)
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
            if not recv or recv.startswith("DeclarationNode::fn_of("):
                continue
            src = src[:s] + "DeclarationNode::fn_of(" + recv + ")" + src[at - 1:]
            fixed += 1
        elif kind == "unused":
            ls = src.rfind("\n", 0, at) + 1
            le = src.find("\n", at) + 1
            proj = r"\s*const " + name + r": FunctionDecl\* = DeclarationNode::fn_of\(" + name + r"_decl\);\r?\n"
            if re.fullmatch(proj, src[ls:le]):
                src = src[:ls] + src[le:]
                fixed += 1
    with open(path, "w", encoding="utf-8", newline="") as h:
        h.write(src)
    print(f"{path}: {fixed} of {len(items)}")
