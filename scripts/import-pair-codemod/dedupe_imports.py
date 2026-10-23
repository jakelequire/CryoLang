"""One-off codemod: delete `import P;` where the same file has `import P::{ ... };`.

A brace import binds the module as a plain import does, so the plain line
beside it says nothing the brace line does not.  Every tracked `.cryo` file
outside `legacy/` is rewritten in place; line endings are kept byte for byte.

A plain line is kept when an attribute (`![...]`) stands directly above it
or above the brace line, since the two would then be gated differently,
and when an LSP fixture's checks.json scripts an edit that starts from the
line's text; each one kept is printed.

A test project's `test.json` pins diagnostic locations as `src/x.cryo:L:C`;
each location in a rewritten file moves up by the lines deleted above it.

    python scripts/import-pair-codemod/dedupe_imports.py [--dry-run]
"""

import os
import re
import subprocess
import sys

PLAIN = re.compile(rb"^[ \t]*import[ \t]+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)[ \t]*;[ \t]*\r?\n?$")
BRACE = re.compile(rb"^[ \t]*import[ \t]+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)::\{")
ATTRIBUTE = re.compile(rb"^[ \t]*!\[")


def gated(lines, at):
    """True when the nearest non-blank line above `at` is an attribute."""
    k = at - 1
    while k >= 0 and lines[k].strip() == b"":
        k -= 1
    return k >= 0 and ATTRIBUTE.match(lines[k]) is not None


def rewrite(path, dry_run):
    with open(path, "rb") as fh:
        lines = fh.read().splitlines(keepends=True)
    brace_at = {}
    for at, line in enumerate(lines):
        m = BRACE.match(line)
        if m:
            brace_at.setdefault(m.group(1), []).append(at)
    if not brace_at:
        return 0, 0
    drop = set()
    kept = 0
    for at, line in enumerate(lines):
        m = PLAIN.match(line)
        if not m or m.group(1) not in brace_at:
            continue
        if gated(lines, at) or any(gated(lines, b) for b in brace_at[m.group(1)]):
            print("kept (attribute): %s:%d" % (path, at + 1))
            kept += 1
            continue
        if anchored(path, line.strip()):
            print("kept (LSP edit anchor): %s:%d" % (path, at + 1))
            kept += 1
            continue
        drop.add(at)
    if drop and not dry_run:
        with open(path, "wb") as fh:
            fh.write(b"".join(l for at, l in enumerate(lines) if at not in drop))
        shift_expectations(path, sorted(at + 1 for at in drop))
    return len(drop), kept


FIXTURE = re.compile(r"^(tools/CryoLSP/tests/fixtures/[^/]+)/")


def anchored(path, text):
    """True when an LSP fixture's checks.json edits the line by its text: the
    line is then the anchor of a scripted edit, not a redundant import."""
    m = FIXTURE.match(path)
    if not m:
        return False
    checks = os.path.join(m.group(1), "checks.json")
    if not os.path.isfile(checks):
        return False
    with open(checks, "rb") as fh:
        return (b'"from": "' + text + b'"') in fh.read()


PROJECT = re.compile(r"^(tests/tests/projects/[^/]+)/(.+\.cryo)$")


def shift_expectations(path, deleted):
    """Move every `rel.cryo:L` in the project's test.json up by the number of
    deleted lines numbered below L."""
    m = PROJECT.match(path)
    if not m:
        return
    spec = os.path.join(m.group(1), "test.json")
    if not os.path.isfile(spec):
        return
    location = re.compile(re.escape(m.group(2)).encode() + rb":(\d+)")

    def moved(found):
        line = int(found.group(1))
        return m.group(2).encode() + b":" + str(line - sum(1 for d in deleted if d < line)).encode()

    with open(spec, "rb") as fh:
        text = fh.read()
    shifted = location.sub(moved, text)
    if shifted != text:
        with open(spec, "wb") as fh:
            fh.write(shifted)
        print("shifted locations: %s" % spec)


def main():
    dry_run = "--dry-run" in sys.argv[1:]
    files = subprocess.run(["git", "ls-files", "*.cryo"], check=True,
                           stdout=subprocess.PIPE).stdout.decode().split("\n")
    total = touched = kept = 0
    for path in files:
        if not path or path.startswith("legacy/"):
            continue
        n, k = rewrite(path, dry_run)
        total += n
        kept += k
        touched += 1 if n else 0
    print("dedupe_imports: %d plain line(s) deleted in %d file(s), %d kept%s"
          % (total, touched, kept, " (dry run)" if dry_run else ""))


if __name__ == "__main__":
    main()
