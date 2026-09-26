#!/usr/bin/env python3
"""Give every cross-module base-class annotation an explicit import.

A path naming a module binds no names, so a bare base-class annotation
(`type class PointerType : Type`) resolves only if `Type` was brought into the
file's scope by name.  Until the glob was removed these resolved through the
type layer's leaf/cursor tier instead, which ignores both the importing file's
imports and the base's own visibility -- so several of these bases are declared
non-public and were being subclassed from other modules regardless.

This does two things per cross-module base:

  * adds `import <base module>::{ <Base> };` to the subclassing file, and
  * marks the base declaration `public`, because a non-public name cannot be
    offered by an import and the cross-module use is already there.

Same-module bases are left alone: the declaration is in scope by declaration.

Idempotent; re-running changes nothing.  Line endings are preserved per file -
this tree is not uniformly LF, and a rewriter that assumes it is corrupts the
files it touches least visibly.
"""

import os
import re
import sys

ROOTS = ["compiler/src", "stdlib", "tools/CryoLSP/src"]

BASE_DECL = re.compile(
    r"^(?P<vis>public\s+)?type\s+class\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?::\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*)?\{",
    re.M,
)
NAMESPACE = re.compile(r"^namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.M)
IMPORT_LINE = re.compile(r"^import\s+[^;]+;", re.M)


def cryo_files():
    for root in ROOTS:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d != "build"]
            for fn in filenames:
                if fn.endswith(".cryo"):
                    yield os.path.join(dirpath, fn).replace("\\", "/")


def read(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def eol_of(text):
    return "\r\n" if "\r\n" in text else "\n"


def main():
    apply = "--apply" in sys.argv

    # Pass 1: where is each class declared, and is it public?
    declared_in = {}   # class name -> namespace
    decl_site = {}     # class name -> (path, is_public)
    files = {}
    for path in cryo_files():
        text = read(path)
        files[path] = text
        m = NAMESPACE.search(text)
        if not m:
            continue
        ns = m.group(1)
        for d in BASE_DECL.finditer(text):
            name = d.group("name")
            declared_in[name] = ns
            decl_site[name] = (path, bool(d.group("vis")))

    # Pass 2: the cross-module subclassing edges.
    need_import = {}   # path -> set((base module, base name))
    need_public = set()
    for path, text in files.items():
        m = NAMESPACE.search(text)
        if not m:
            continue
        ns = m.group(1)
        for d in BASE_DECL.finditer(text):
            base = d.group("base")
            if not base or base not in declared_in:
                continue
            base_ns = declared_in[base]
            if base_ns == ns:
                continue                      # in scope by declaration
            need_import.setdefault(path, set()).add((base_ns, base))
            if not decl_site[base][1]:
                need_public.add(base)

    edits = 0

    # Mark the bases public.
    for base in sorted(need_public):
        path, _ = decl_site[base]
        text = files[path]
        pat = re.compile(r"^type\s+class\s+" + re.escape(base) + r"\b", re.M)
        new = pat.sub("public type class " + base, text, count=1)
        if new != text:
            print("public: %s  (%s)" % (base, path))
            files[path] = new
            edits += 1
            if apply:
                write(path, new)

    # Add the explicit imports.
    for path in sorted(need_import):
        text = files[path]
        eol = eol_of(text)
        wanted = []
        for base_ns, base in sorted(need_import[path]):
            line = "import %s::{ %s };" % (base_ns, base)
            # Already present, in this exact form or in a braced list?
            braced = re.compile(
                r"^import\s+" + re.escape(base_ns) + r"::\{[^}]*\b"
                + re.escape(base) + r"\b[^}]*\};", re.M)
            if braced.search(text):
                continue
            wanted.append(line)
        if not wanted:
            continue
        last = None
        for m in IMPORT_LINE.finditer(text):
            last = m
        if last is None:
            print("SKIP (no import block): %s" % path)
            continue
        insert_at = last.end()
        addition = "".join(eol + w for w in wanted)
        new = text[:insert_at] + addition + text[insert_at:]
        print("import: %s  +%d" % (path, len(wanted)))
        for w in wanted:
            print("        %s" % w)
        files[path] = new
        edits += 1
        if apply:
            write(path, new)

    print("\n%d file edit(s) %s" % (edits, "applied" if apply else "(dry run)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
