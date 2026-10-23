"""One-off codemod: rewrite every `public module X;` line to the new forms.

A run of consecutive `public module` lines becomes one grouped
`module { ... }` block; a lone one becomes `module X;`.  A path listed in
INTERNAL (by file, relative to the repo root, and the path as written)
moves to an `internal module` line or group placed after the public ones.
`legacy/` is frozen and is not touched.

    python scripts/module-forms-codemod.py            # rewrite in place
    python scripts/module-forms-codemod.py --check    # list what would change
    python scripts/module-forms-codemod.py --comments # then the comments naming it

Deleted in the commit after the one that ran it.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INTERNAL = {
    "stdlib/random/_module.cryo": {"random::bits"},
}

LINE = re.compile(r"^(?P<indent>[ \t]*)public module (?P<path>[A-Za-z_][\w:]*)\s*;(?P<rest>.*?)(?P<eol>\r?\n?)$")


def files():
    out = subprocess.run(["git", "grep", "-l", "public module", "--", "*.cryo", ":!legacy"],
                         cwd=ROOT, stdout=subprocess.PIPE, check=True).stdout.decode()
    return [ln for ln in out.splitlines() if ln]


def emit(keyword, indent, entries, eol):
    """`entries` is a list of (path, trailing text)."""
    if len(entries) == 1:
        path, rest = entries[0]
        return ["%s%s %s;%s%s" % (indent, keyword, path, rest, eol)]
    lines = ["%s%s {%s" % (indent, keyword, eol)]
    for path, rest in entries:
        lines.append("%s    %s;%s%s" % (indent, path, rest, eol))
    lines.append("%s}%s" % (indent, eol))
    return lines


def rewrite(rel, text):
    hidden = INTERNAL.get(rel, set())
    lines = text.splitlines(keepends=True)
    out, i, moved = [], 0, 0
    while i < len(lines):
        m = LINE.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        indent, eol = m.group("indent"), m.group("eol") or "\n"
        public, internal = [], []
        while i < len(lines):
            m = LINE.match(lines[i])
            if not m or m.group("indent") != indent:
                break
            entry = (m.group("path"), m.group("rest"))
            (internal if m.group("path") in hidden else public).append(entry)
            moved += 1
            i += 1
        if public:
            out.extend(emit("module", indent, public, eol))
        if internal:
            out.extend(emit("internal module", indent, internal, eol))
    return "".join(out), moved


COMMENT = re.compile(r"^\s*//")


def comments(argv):
    """Second pass, once the declarations are rewritten: a comment naming the
    old form in backticks names the new one."""
    out = subprocess.run(["git", "grep", "-l", "`public module", "--", "*.cryo", ":!legacy"],
                         cwd=ROOT, stdout=subprocess.PIPE).stdout.decode()
    edited = 0
    for rel in [ln for ln in out.splitlines() if ln]:
        path = os.path.join(ROOT, rel)
        with open(path, "r", encoding="utf-8", newline="") as f:
            lines = f.read().splitlines(keepends=True)
        new = [ln.replace("`public module", "`module") if COMMENT.match(ln) else ln for ln in lines]
        if new != lines:
            edited += 1
            if "--check" not in argv:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write("".join(new))
    print("module-forms-codemod: comments rewritten in %d files" % edited)
    return 0


def main(argv):
    if "--comments" in argv:
        return comments(argv)
    check = "--check" in argv
    total, changed = 0, 0
    for rel in files():
        path = os.path.join(ROOT, rel)
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        new, moved = rewrite(rel, text)
        if moved == 0:
            continue
        total += moved
        changed += 1
        if check:
            print("%s: %d" % (rel, moved))
        else:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new)
    print("module-forms-codemod: %d declarations in %d files" % (total, changed))
    for rel, paths in INTERNAL.items():
        for p in paths:
            print("  internal: %s in %s" % (p, rel))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
