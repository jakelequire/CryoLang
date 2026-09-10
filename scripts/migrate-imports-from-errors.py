#!/usr/bin/env python3
"""Add the explicit import each `cannot find type X in this scope` error names.

The glob migration's demand was measured from `Resolver::lookup`, which sees
one resolution path.  A type ANNOTATION resolves through the type layer
instead, so that measurement could not see annotation demand and the migration
was incomplete by construction -- the build is the remaining instrument.

This is driven by the compiler's own diagnostics rather than by re-deriving
resolution here: re-implementing the lookup is how the first measurement went
wrong.  Because a build ABORTS at name resolution, one run reports only the
first failing module, so this is a fixpoint loop:

    make cryo                      # once, to get a compiler with the new rule
    <build the tree, keep the log>
    python scripts/migrate-imports-from-errors.py build.log --apply
    <repeat until the build is clean>

A leaf declared by more than one module is REFUSED, not guessed: the whole
point of the ruling is that such a name has no single owner, and picking one
here would reintroduce the defect the migration exists to remove.  Those are
printed for a human to qualify at the use site.

Line endings are preserved per file - this tree is not uniformly LF.
"""

import os
import re
import sys

ROOTS = ["compiler/src", "stdlib", "tools/CryoLSP/src"]

ERR = re.compile(
    r"cannot find type `(?P<name>[A-Za-z_][A-Za-z0-9_]*)` in this scope"
    r"[\s\S]{0,200}?-->\s*(?P<path>[^\s:]+):(?P<line>\d+):")
DECL = re.compile(
    r"^(?:public\s+)?type\s+(?:class|struct|enum|trait|alias)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)", re.M)
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


def build_index():
    """leaf name -> set of declaring namespaces."""
    index = {}
    for path in cryo_files():
        text = read(path)
        m = NAMESPACE.search(text)
        if not m:
            continue
        ns = m.group(1)
        for d in DECL.finditer(text):
            index.setdefault(d.group("name"), set()).add(ns)
    return index


def resolve_repo_path(p):
    """A diagnostic path is relative to the project dir it was built in."""
    p = p.replace("\\", "/")
    for cand in (p, "compiler/" + p, os.path.relpath(p).replace("\\", "/")):
        if os.path.isfile(cand):
            return cand
    return None


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: %s <build log> [--apply]\n" % sys.argv[0])
        return 2
    log_path = sys.argv[1]
    apply = "--apply" in sys.argv

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        log = f.read()

    index = build_index()

    wanted = {}     # repo path -> set of import lines
    ambiguous = []
    unknown = []
    for m in ERR.finditer(log):
        name = m.group("name")
        path = resolve_repo_path(m.group("path"))
        if path is None:
            unknown.append((name, m.group("path"), "path not found"))
            continue
        owners = index.get(name)
        if not owners:
            unknown.append((name, path, "no declaring module found"))
            continue
        text = read(path)
        ns_m = NAMESPACE.search(text)
        here = ns_m.group(1) if ns_m else None
        owners = set(o for o in owners if o != here)
        if not owners:
            continue                       # declared in this very module
        if len(owners) > 1:
            ambiguous.append((name, path, sorted(owners)))
            continue
        owner = owners.pop()
        wanted.setdefault(path, set()).add(
            "import %s::{ %s };" % (owner, name))

    edits = 0
    for path in sorted(wanted):
        text = read(path)
        eol = "\r\n" if "\r\n" in text else "\n"
        add = []
        for line in sorted(wanted[path]):
            name = line.split("{")[1].split("}")[0].strip()
            owner = line.split("import ")[1].split("::{")[0]
            braced = re.compile(
                r"^import\s+" + re.escape(owner) + r"::\{[^}]*\b"
                + re.escape(name) + r"\b[^}]*\};", re.M)
            if not braced.search(text):
                add.append(line)
        if not add:
            continue
        last = None
        for im in IMPORT_LINE.finditer(text):
            last = im
        if last is None:
            unknown.append((path, path, "no import block to extend"))
            continue
        new = text[:last.end()] + "".join(eol + a for a in add) + text[last.end():]
        print("%s  +%d" % (path, len(add)))
        for a in add:
            print("      %s" % a)
        edits += 1
        if apply:
            write(path, new)

    if ambiguous:
        print("\nREFUSED - leaf declared by more than one module; qualify at")
        print("the use site instead of importing one of them:")
        for name, path, owners in ambiguous:
            print("  %s in %s" % (name, path))
            for o in owners:
                print("      %s" % o)
    if unknown:
        print("\nUNRESOLVED:")
        for a, b, why in unknown:
            print("  %s (%s): %s" % (a, b, why))

    print("\n%d file edit(s) %s" % (edits, "applied" if apply else "(dry run)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
