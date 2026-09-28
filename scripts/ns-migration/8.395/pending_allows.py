#!/usr/bin/env python3
"""Mark every declaration the spelling lint refuses as pending a reason.

usage: pending_allows.py [--root DIR] CHECK_LOG [[--root DIR] CHECK_LOG]...

Each CHECK_LOG is the output of `cryo check` run by a compiler carrying the
`lookup_by_spelling` lint (uncapped: the shipped cap of 500 errors hides the
rest).  Every E0157 names the function and points at the parameter; this
walks up from the parameter to the declaration's head - the nearest line at
or above it where the function's name opens a parameter list - and inserts,
above the head at its indentation,

    ![allow(lookup_by_spelling, reason = "pending: ...")]

once per declaration.  The reason says no reason has been written: the lint
accepts it, and the count of these lines is what the reasons still owed
number.  A declaration already carrying an allow for the lint is left alone,
so a second run over the same log changes nothing.

Refuses (exit 1, nothing written) if any reported parameter's head cannot be
found, or if a log holds no E0157 at all (a zero from the wrong compiler).
Paths in a log are relative to the directory the compiler ran in: the
--root before it, repo-relative (default: compiler/).  The logs this was run
over: `cryo check src/main.cryo` in compiler/, and `cryo build` in
tools/CryoLSP, which also reaches the editor's own declarations.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PENDING = ('![allow(lookup_by_spelling, reason = "pending: this took a spelling '
           'before the lint existed, and why is not written yet")]')
HEAD_WORDS = r"(?:(?:public|private|protected|static|override|virtual|function|async|mut)\s+)*"
MSG = re.compile(r"^error\[E0157\]: `([^`]+)` takes a spelling: parameter `[^`]+` carries a `[^`]+`")


def reported(log):
    lines = open(log, encoding="utf-8", errors="replace").read().split("\n")
    out = []
    for i, line in enumerate(lines):
        m = MSG.match(line)
        if not m:
            continue
        loc = lines[i + 1].split("--> ", 1)[1].strip()
        path, ln, _col = loc.rsplit(":", 2)
        out.append((path.replace("\\", "/"), int(ln), m.group(1)))
    return out


def main(argv):
    sites = []
    while argv:
        root = os.path.join(REPO, "compiler")
        if argv[:1] == ["--root"]:
            root, argv = os.path.join(REPO, argv[1]), argv[2:]
        log, argv = argv[0], argv[1:]
        got = reported(log)
        if not got:
            sys.exit(f"pending_allows: {log} carries no E0157; wrong compiler or wrong log")
        sites.extend((root, rel, ln, fn) for rel, ln, fn in got)

    heads = {}   # abs path -> set of 0-based head lines
    missing = []
    cache = {}
    for root, rel, ln, fn in sites:
        # The compiler spells some paths in another case than the file system
        # does; one key per file, or two logs' insertions overwrite each other.
        path = os.path.normcase(os.path.normpath(os.path.join(root, rel)))
        if path not in cache:
            cache[path] = open(path, encoding="utf-8", newline="").read().splitlines(keepends=True)
        src = cache[path]
        head_re = re.compile(r"^\s*" + HEAD_WORDS + re.escape(fn) + r"\s*(?:<[^()]*>)?\s*\(")
        found = None
        for k in range(ln - 1, max(-1, ln - 41), -1):
            if head_re.match(src[k]):
                found = k
                break
        if found is None:
            missing.append(f"{rel}:{ln} {fn}")
            continue
        heads.setdefault(path, set()).add(found)
    if missing:
        sys.exit("pending_allows: no declaration head found for\n  " + "\n  ".join(missing))

    inserted = 0
    for path, ks in sorted(heads.items()):
        src = cache[path]
        for k in sorted(ks, reverse=True):
            above = k - 1
            while above >= 0 and src[above].strip().startswith("!["):
                if "lookup_by_spelling" in src[above]:
                    break
                above -= 1
            if above >= 0 and "lookup_by_spelling" in src[above]:
                continue
            line = src[k]
            indent = line[: len(line) - len(line.lstrip())]
            eol = "\r\n" if line.endswith("\r\n") else "\n"
            src.insert(k, indent + PENDING + eol)
            inserted += 1
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("".join(src))
    print(f"pending_allows: {len(sites)} parameters, {sum(len(v) for v in heads.values())} "
          f"declarations in {len(heads)} files; {inserted} allows inserted")


if __name__ == "__main__":
    main(sys.argv[1:])
