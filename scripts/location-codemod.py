#!/usr/bin/env python3
"""Rewrite the `FILE, LINE` argument pair to `intrinsics::location()`.

One-off: the source-location pseudo-constants `FILE` and `LINE` are deleted
in favour of the intrinsic function `location()`.  Every call that passes the
pair - `panic(msg, FILE, LINE)`, `panic(msg, FILE, LINE as u32)` - passes
`intrinsics::location().file, intrinsics::location().line` instead, which is
the same place: each `location()` answers the call it is written in, and a
`![caller_location]` body's answers its caller's, as `FILE` and `LINE` did.
A file that does not import `std::core::intrinsics` gets the import after its
last top-level `import` line (or after its `namespace` line when it has
none).  Comment lines are left alone.  Any other use of `FILE` or `LINE` is
reported for a hand edit.  Deleted in the commit after the one that ran it.

usage: python scripts/location-codemod.py stdlib compiler/src tests/tests
"""
import os
import re
import sys

PAIR = re.compile(r"\bFILE, LINE(?: as u32)?\)")
NEW = "intrinsics::location().file, intrinsics::location().line)"
IMPORT = "import std::core::intrinsics;"
LONE = re.compile(r"\b(FILE|LINE)\b")


def code_part(line):
    """The line with a trailing `//` comment and string literals blanked."""
    out = re.sub(r'"(?:[^"\\]|\\.)*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', line)
    i = out.find("//")
    return out if i < 0 else out[:i]


def rewrite(path):
    with open(path, encoding="utf-8", newline="") as h:
        src = h.read()
    nl = "\r\n" if "\r\n" in src else "\n"
    lines = src.split(nl)
    changed = False
    for i, line in enumerate(lines):
        if line.lstrip().startswith("//"):
            continue
        code = code_part(line)
        if PAIR.search(code):
            lines[i] = PAIR.sub(NEW, line)
            changed = True
            code = code_part(lines[i])
        for m in LONE.finditer(code):
            if code[m.end():].startswith("*") or code[m.end():].startswith(" *"):
                continue
            print(f"left for hand edit: {path}:{i + 1}: {line.strip()}")
    if not changed:
        return
    if not any(l.strip() == IMPORT for l in lines):
        at = -1
        for i, l in enumerate(lines):
            if l.startswith("import "):
                at = i
            elif l.startswith("namespace ") and at < 0:
                at = i
            elif l.startswith(("function ", "type ", "implement")):
                break
        # an import may run over several lines: step past its closing brace
        if lines[at].startswith("import ") and "{" in lines[at] and "}" not in lines[at]:
            while "}" not in lines[at]:
                at += 1
        lines.insert(at + 1, IMPORT)
    with open(path, "w", encoding="utf-8", newline="") as h:
        h.write(nl.join(lines))
    print("rewrote", path)


for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in sorted(fs):
            if f.endswith(".cryo"):
                rewrite(os.path.join(dp, f))
