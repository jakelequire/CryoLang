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

ROOTS = ["compiler/src", "stdlib", "tools/CryoLSP/src", "tests"]

ERR = re.compile(
    r"cannot find type `(?P<name>[A-Za-z_][A-Za-z0-9_]*)` in this scope"
    r"[\s\S]{0,200}?-->\s*(?P<path>[^\s:]+):(?P<line>\d+):")
DECL = re.compile(
    r"^(?:public\s+)?type\s+(?:class|struct|enum|trait|alias)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)", re.M)
NAMESPACE = re.compile(r"^namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;", re.M)
IMPORT_LINE = re.compile(r"^import\s+[^;]+;", re.M)
# `cryo test` streams a project's diagnostics and THEN its result line, so the
# project a diagnostic belongs to is the one named by the NEXT marker after it.
PROJ_MARK = re.compile(r"^  (?P<name>[A-Za-z0-9_]+) \.\.\. \[", re.M)


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


def project_of(path):
    """The test project a path belongs to, or None for compiler/stdlib."""
    marker = "tests/tests/projects/"
    if path.startswith(marker):
        return path[len(marker):].split("/")[0]
    return None


def build_index():
    """leaf name -> set of (declaring namespace, declaring project)."""
    index = {}
    for path in cryo_files():
        text = read(path)
        m = NAMESPACE.search(text)
        if not m:
            continue
        ns = m.group(1)
        proj = project_of(path)
        for d in DECL.finditer(text):
            index.setdefault(d.group("name"), set()).add((ns, proj))
    return index


def visible_owners(decl, use_path):
    """The declaring namespaces the erroring file's compilation can see.

    Each test project is its own compilation, so a leaf declared by three
    different projects is not ambiguous - only one of them is in scope. An
    index over the whole tree cannot see that, and refusing on it would report
    an ambiguity the compiler never has.
    """
    here = project_of(use_path)
    return set(ns for ns, proj in decl if proj is None or proj == here)


_SUFFIX_INDEX = None


def suffix_index():
    """Every .cryo path in the tree, for suffix matching."""
    global _SUFFIX_INDEX
    if _SUFFIX_INDEX is None:
        _SUFFIX_INDEX = list(cryo_files())
    return _SUFFIX_INDEX


def resolve_repo_path(p, name, line):
    """The ONE repo file a diagnostic path names, verified by content.

    `cryo build` runs in a project dir and `cryo test` runs each project in
    ITS OWN dir, so a diagnostic path is relative to a directory this script
    cannot know.  The direct candidates cover the common cases; anything else
    is matched by PATH SUFFIX against the tree, which is unambiguous for a
    path of two or more segments and is refused when it is not.

    Returning None for a file that exists is what stalled this loop once: the
    tool printed `0 file edit(s)` while 18 projects were failing, because "no
    edit is available" and "I could not find the file" read identically unless
    the population is counted separately.  `main` counts it.
    """
    p = p.replace(chr(92), "/")
    while p.startswith("./"):
        p = p[2:]
    for cand in (p, "compiler/" + p, "tests/" + p):
        if os.path.isfile(cand):
            return [cand]
    hits = [f for f in suffix_index() if f.endswith("/" + p) or f == p]
    if len(hits) <= 1:
        return hits
    # Several files carry this basename (test projects reuse them), and the
    # log's own ordering cannot say which: `cryo test` streams a project's
    # diagnostics around its result line, so reading the nearest marker
    # attributes them to a project that passed. Ask the FILES instead - a
    # candidate owns the diagnostic only if it really uses that name on that
    # line. Every candidate that does needs the same import, so all of them
    # are returned rather than one being picked.
    word = re.compile(r"" + re.escape(name) + r"")
    owning = []
    for f in hits:
        try:
            lines = read(f).splitlines()
        except OSError:
            continue
        if 0 < line <= len(lines) and word.search(lines[line - 1]):
            owning.append(f)
    # More than one file still matches, so the diagnostic does not say which.
    # REFUSE. Editing all of them wrote four bogus imports into
    # `compiler/src/main.cryo` once, because ~20 projects carry a
    # `src/main.cryo` and a substring test let an unrelated line match. A
    # migration that guesses is worse than one that stops and says where.
    if len(owning) == 1:
        return owning
    return []


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
    marks = [(mm.start(), mm.group("name")) for mm in PROJ_MARK.finditer(log)]

    def project_at(pos):
        for at, nm in marks:
            if at > pos:
                return nm
        return None

    seen = 0
    for m in ERR.finditer(log):
        seen += 1
        name = m.group("name")
        paths = resolve_repo_path(m.group("path"), name, int(m.group("line")))
        if not paths:
            unknown.append((name, m.group("path"), "no file uses that name there"))
            continue
        decl = index.get(name)
        if not decl:
            unknown.append((name, paths[0], "no declaring module found"))
            continue
        for path in paths:
            text = read(path)
            ns_m = NAMESPACE.search(text)
            here = ns_m.group(1) if ns_m else None
            owners = set(o for o in visible_owners(decl, path) if o != here)
            if not owners:
                continue                   # declared in this very module
            if len(owners) > 1:
                ambiguous.append((name, path, sorted(owners)))
                continue
            wanted.setdefault(path, set()).add(
                "import %s::{ %s };" % (owners.pop(), name))

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

    # The control on a zero: "nothing to fix" and "I could not read the log"
    # both print `0 file edit(s)` unless the population is stated beside it.
    print("")
    print("parsed %d unresolved-type error(s); %d unlocatable, %d ambiguous"
          % (seen, len(unknown), len(ambiguous)))
    print("\n%d file edit(s) %s" % (edits, "applied" if apply else "(dry run)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
