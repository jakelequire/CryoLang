"""Delete the compiler's inert lint exemptions and door markers.

The lint that `![allow(lookup_by_spelling, ...)]` exempted from, and the
check that read `/// Door `id`.` markers, are both gone from the compiler;
the directives and markers in `compiler/src` say nothing to anything.

- every line of `compiler/src/**/*.cryo` that is exactly one
  `![allow(lookup_by_spelling, reason = "...")]` directive is removed;
- a doc line opening `/// Door `id`.  ` loses that marker and keeps the
  prose after it.

The editor (`tools/CryoLSP`) and the tests are not touched.  Line endings
are kept as each file has them.  Prints what it changed; `--check` changes
nothing and exits 1 if anything is left to delete.

Usage: python scripts/ns-migration/8.605/delete-allows.py [--check]
"""
import os
import re
import sys

ROOT = os.path.join("compiler", "src")
ALLOW = re.compile(r'^[ \t]*!\[allow\(lookup_by_spelling, reason = ".*"\)\][ \t]*\r?\n?$')
DOOR = re.compile(r'^([ \t]*///[ \t]*)Door `[a-z][a-z0-9-]*`\.[ \t]+')


def main():
    check = "--check" in sys.argv[1:]
    allows = 0
    doors = 0
    for dirpath, _dirs, files in os.walk(ROOT):
        for f in sorted(files):
            if not f.endswith(".cryo"):
                continue
            path = os.path.join(dirpath, f)
            with open(path, "r", encoding="utf-8", newline="") as fh:
                lines = fh.readlines()
            out = []
            a = d = 0
            for line in lines:
                if ALLOW.match(line):
                    a += 1
                    continue
                new = DOOR.sub(r"\1", line, count=1)
                if new != line:
                    d += 1
                out.append(new)
            if a or d:
                print("%s: %d allow(s), %d door marker(s)" % (path, a, d))
                allows += a
                doors += d
                if not check:
                    with open(path, "w", encoding="utf-8", newline="") as fh:
                        fh.writelines(out)
    print("total: %d allow(s), %d door marker(s)%s"
          % (allows, doors, " left" if check else " deleted"))
    if check and (allows or doors):
        sys.exit(1)


if __name__ == "__main__":
    main()
