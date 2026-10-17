#!/usr/bin/env python3
"""Drop `DeclKind::IntrinsicConst(_)` from every grouped `match` arm.

One-off: the `intrinsic const` declaration form is deleted, and every arm
that listed it beside other kinds as a no-op loses the alternative.  Arms
that bind the payload (`DeclKind::IntrinsicConst(n)`) are left for hand
edits, and the script reports them.  Deleted in the commit after the one
that ran it.

usage: python scripts/intrinsic-const-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

ALT_AFTER = re.compile(r"DeclKind::IntrinsicConst\(_\)[ \t]*\|[ \t]*")
ALT_BEFORE = re.compile(r"[ \t]*\|[ \t]*DeclKind::IntrinsicConst\(_\)")

for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as h:
                src = h.read()
            out = ALT_AFTER.sub("", src)
            out = ALT_BEFORE.sub("", out)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"DeclKind::IntrinsicConst\(", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
