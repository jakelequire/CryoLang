#!/usr/bin/env python3
"""Respell array types written postfix inside comments: `T[]` -> `[T]`,
`T[N]` -> `[T; N]`, `u64[2][3]` -> `[[u64; 2]; 3]`.

Only the comment part of a line is touched (from the first `//`, so a `//`
inside a string literal would be misread; the dry run is reviewed for that).
A base is a type only when it is a primitive keyword, a capitalised name that
is not all capitals (a one-letter parameter aside), or ends in `*` or `>`:
`order[]` and `modules[]` name fields, `operator[]` and `new[]` are
operators, and `ERROR[E0104]` is a test annotation, so they are left alone.  A fixed
length is a number or a capitalised constant; `t[0]` on a lowercase name is
indexing and is left alone.

    python scripts/array-comment-codemod.py            # dry run: print each rewrite
    python scripts/array-comment-codemod.py --apply    # rewrite the files

Files: every tracked `.cryo` under compiler/src, stdlib, runtime,
tools/CryoLSP/src, tests and examples.
"""

import re
import subprocess
import sys

ROOTS = ["compiler/src", "stdlib", "runtime", "tools/CryoLSP/src", "tests", "examples"]

# Files whose comments spell the refused postfix form on purpose: the parser
# that refuses it, and the test of the refusal.
SKIP_FILES = {
    "compiler/src/compiler/parser/expr_parser.cryo",
    "tests/tests/negative/E0104_postfix_array_type.cryo",
}

# C declarations quoted in a comment (a glibc `dirent`, a Windows struct):
# their arrays are C's.
SKIP_TEXT = ("char[256]", "Handles[1]")

PRIMITIVES = {
    "void", "boolean", "char", "string", "int", "i8", "i16", "i32", "i64", "i128",
    "uint", "u8", "u16", "u32", "u64", "u128", "float", "f32", "f64", "double",
    "usize", "isize", "never", "This", "va_list",
}

# A path, optional generic arguments (two levels of nesting), then pointer
# stars; then one or more postfix array suffixes.
BASE = r"(?P<base>[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*" \
       r"(?:<(?:[^<>]|<(?:[^<>]|<[^<>]*>)*>)*>)?\**)"
SUFFIX = r"\[(?:[0-9]+|[A-Z][A-Z0-9_]*(?: *[*+] *[0-9A-Z_]+)?)?\]"
POSTFIX = re.compile(r"(?<![A-Za-z0-9_\]])" + BASE + r"(?P<suffixes>(?:" + SUFFIX + r")+)")


def is_type_base(base):
    head = base.split("<", 1)[0].rstrip("*").split("::")[-1]
    if base.endswith("*") or base.endswith(">"):
        return head not in ("operator", "new")
    if head in PRIMITIVES:
        return True
    # A capitalised name is a type, unless it is all capitals and longer than
    # a type parameter: `GRID[0]` indexes a constant, `ERROR[E0104]` is a
    # test annotation.
    return head[:1].isupper() and (len(head) == 1 or head != head.upper())


def rewrite_type(base, suffixes):
    text = base
    for length in re.findall(r"\[([^\]]*)\]", suffixes):
        text = "[%s]" % text if length == "" else "[%s; %s]" % (text, length)
    return text


def rewrite_comment(comment):
    def sub(m):
        base, suffixes = m.group("base"), m.group("suffixes")
        if not is_type_base(base) or m.group(0) in SKIP_TEXT:
            return m.group(0)
        # `new u8[n]` allocates; it is not an array type and keeps its spelling.
        if comment[:m.start()].endswith("new "):
            return m.group(0)
        # `buf[0]` on a lowercase name is indexing even when it looks fixed.
        if base[:1].islower() and base not in PRIMITIVES and not base.endswith(("*", ">")):
            return m.group(0)
        return rewrite_type(base, suffixes)
    return POSTFIX.sub(sub, comment)


def main():
    apply = "--apply" in sys.argv[1:]
    files = subprocess.run(["git", "ls-files", "--"] + ROOTS, stdout=subprocess.PIPE,
                           check=True).stdout.decode().split()
    changed_files = 0
    changed_lines = 0
    for path in files:
        if not path.endswith(".cryo") or path in SKIP_FILES:
            continue
        with open(path, "rb") as fh:
            data = fh.read()
        lines = data.split(b"\n")
        touched = False
        for i, raw in enumerate(lines):
            text = raw.decode("utf-8")
            cut = text.find("//")
            if cut < 0:
                continue
            new = text[:cut] + rewrite_comment(text[cut:])
            if new != text:
                print("%s:%d:\n  - %s\n  + %s" % (path, i + 1, text.strip(), new.strip()))
                lines[i] = new.encode("utf-8")
                touched = True
                changed_lines += 1
        if touched:
            changed_files += 1
            if apply:
                with open(path, "wb") as fh:
                    fh.write(b"\n".join(lines))
    print("array-comment-codemod: %d line(s) in %d file(s)%s"
          % (changed_lines, changed_files, "" if apply else " (dry run)"))


if __name__ == "__main__":
    main()
