#!/usr/bin/env python3
"""Move each `NodeKind::MethodDeclaration` arm into its match's
`Declaration` arm.

One-off, for the method conversion (the method form of the function
conversion's `fn-armmove.py`): a method is built with
`NodeKind::Declaration` now, so an arm for its old kind is refused.  For
each `file:line` given (the line an arm `NodeKind::MethodDeclaration =>`
opens on - alone, or first in a `|` group of kinds sharing one body), the
arm's body becomes the first arm, `DeclKind::Method(_) => { ... }`, of the
`match (... .shape)` inside the same match's `NodeKind::Declaration` arm,
re-indented to it, and the kind is cut from the old arm (the whole arm
when it was alone).  The body is unchanged: it still reaches the method
through the node it matched on, which is the declaration.  A match with
no such `Declaration` arm is reported and left for a hand edit.

With `--noop`, each `file:line` is instead the line a `match (... .shape)`
opens on that the type check calls non-exhaustive, in a match whose old
`_` arm a method fell to and did nothing: `DeclKind::Method(_)` is added to
the arm naming `DeclKind::Legacy`, when that arm's body is one of the no-op
bodies (`{}`, `{ }`, `{ continue; }`, `{ DefId::invalid() }`).  Which
matches qualify was read off each one's outer default by hand.

usage: python scripts/method-armmove.py [--noop] FILE:LINE...   (paths relative to cwd)
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


noop = "--noop" in sys.argv[1:]
sites = {}
for arg in sys.argv[1:]:
    if arg == "--noop":
        continue
    path, line = arg.rsplit(":", 1)
    sites.setdefault(path, []).append(int(line))

NOOP_BODY = re.compile(r"\{\s*(?:continue;|DefId::invalid\(\))?\s*\}")

if noop:
    for path, lines in sites.items():
        with open(path, encoding="utf-8", newline="") as h:
            src = h.read()
        for line in sorted(lines, reverse=True):
            starts = [0]
            for m in re.finditer("\n", src):
                starts.append(m.end())
            at = starts[line - 1]
            o = src.find("{", src.find("match", at))
            e = block_end(src, o)
            k = src.find("DeclKind::Legacy", o, e)
            arrow = src.find("=>", k, e) if k >= 0 else -1
            body = re.compile(r"\s*(\{[^{}]*\})").match(src, arrow + 2) if arrow >= 0 else None
            if not body or not NOOP_BODY.fullmatch(body.group(1)) or "DeclKind::Method" in src[o:e]:
                print(f"{path}:{line}: Legacy arm is not a no-op here, left for hand edit")
                continue
            src = src[:k] + "DeclKind::Method(_) | " + src[k:]
            print(f"{path}:{line}: no-op Method arm")
        with open(path, "w", encoding="utf-8", newline="") as h:
            h.write(src)
    sys.exit(0)

for path, lines in sites.items():
    with open(path, encoding="utf-8", newline="") as h:
        src = h.read()
    nl = "\r\n" if "\r\n" in src else "\n"
    for line in sorted(lines, reverse=True):
        starts = [0]
        for m in re.finditer("\n", src):
            starts.append(m.end())
        at = starts[line - 1]
        m = re.compile(r"[ \t]*NodeKind::MethodDeclaration\s*=>\s*\{").match(src, at)
        if not m:
            print(f"{path}:{line}: not a lone arm here, left for hand edit")
            continue
        arm_ind = re.match(r"[ \t]*", src[at:]).group(0)
        ob = m.end() - 1
        cb = block_end(src, ob)
        body = src[ob:cb + 1]
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
        new_arm = inner_ind + "DeclKind::Method(_) => " + nl.join(moved) + nl
        ins = line_start(src, first.end() - 1)
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
