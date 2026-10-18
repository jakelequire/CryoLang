#!/usr/bin/env python3
"""Move each `NodeKind::FunctionDeclaration` arm into its match's
`Declaration` arm.

One-off, for the function conversion: a function is built with
`NodeKind::Declaration` now, so an arm for its old kind is refused.  For
each `file:line` given (the line an arm `NodeKind::FunctionDeclaration =>
{` opens on), the arm is cut out and its body becomes the first arm,
`DeclKind::Function(_) => { ... }`, of the `match (... .shape)` inside the
same match's `NodeKind::Declaration` arm, re-indented to it.  The body is
unchanged: it still reaches the function through the node it matched on,
which is the declaration.  A match with no such `Declaration` arm is
reported and left for a hand edit.

usage: python scripts/fn-armmove.py FILE:LINE...   (paths relative to cwd)
"""
import re
import sys


def block_end(src, open_brace):
    depth, i = 0, open_brace
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def line_start(src, i):
    return src.rfind("\n", 0, i) + 1


sites = {}
for arg in sys.argv[1:]:
    path, line = arg.rsplit(":", 1)
    sites.setdefault(path, []).append(int(line))

for path, lines in sites.items():
    with open(path, encoding="utf-8", newline="") as h:
        src = h.read()
    nl = "\r\n" if "\r\n" in src else "\n"
    # bottom-up so earlier offsets stay valid
    for line in sorted(lines, reverse=True):
        starts = [0]
        for m in re.finditer("\n", src):
            starts.append(m.end())
        at = starts[line - 1]
        m = re.compile(r"[ \t]*NodeKind::FunctionDeclaration\s*=>\s*\{").match(src, at)
        if not m:
            print(f"{path}:{line}: no arm here")
            continue
        arm_ind = re.match(r"[ \t]*", src[at:]).group(0)
        ob = m.end() - 1
        cb = block_end(src, ob)
        body = src[ob:cb + 1]
        # the enclosing match: the nearest `match (` above whose block holds the arm
        k = at
        enclosing = None
        while True:
            k = src.rfind("match (", 0, k)
            if k < 0:
                break
            o = src.find("{", k)
            e = block_end(src, o)
            if o < at and e > cb:
                enclosing = (o, e)
                break
        if enclosing is None:
            print(f"{path}:{line}: no enclosing match")
            continue
        o, e = enclosing
        d = re.compile(r"(?m)^" + re.escape(arm_ind) + r"NodeKind::Declaration\s*=>\s*\{").search(src, o, e)
        if not d:
            print(f"{path}:{line}: no Declaration arm, left for hand edit")
            continue
        inner = re.compile(r"match\s*\(\(?[^\n]*\.shape\)\s*\{").search(src, d.end(), e)
        if not inner:
            print(f"{path}:{line}: Declaration arm has no shape match, left for hand edit")
            continue
        first = re.compile(r"\n([ \t]*)\S").search(src, inner.end())
        inner_ind = first.group(1)
        shift = len(inner_ind) - len(arm_ind)
        moved_lines = body.split(nl)
        moved = [moved_lines[0]] + [(" " * shift + l) if l.strip() else l for l in moved_lines[1:]]
        new_arm = inner_ind + "DeclKind::Function(_) => " + nl.join(moved) + nl
        ins = line_start(src, first.end() - 1)
        # cut the old arm (with its line), then insert (the insertion point
        # is below the cut only if the Declaration arm follows it)
        cut_from = at
        cut_to = cb + 1
        if src[cut_to:cut_to + len(nl)] == nl:
            cut_to += len(nl)
        if ins > cut_from:
            src = src[:ins] + new_arm + src[ins:]
            src = src[:cut_from] + src[cut_to:]
        else:
            src = src[:cut_from] + src[cut_to:]
            src = src[:ins] + new_arm + src[ins:]
        print(f"{path}:{line}: moved")
    with open(path, "w", encoding="utf-8", newline="") as h:
        h.write(src)
