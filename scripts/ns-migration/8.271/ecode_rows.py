#!/usr/bin/env python3
"""Which section-0 grep checks count COMMENTS as well as code.

A row's check is `grep -c 'PAT' FILE` or `grep -rho 'PAT' PATHS | wc -l`, and
either counts a line (or a hit) whether it is an emit, a call, a declaration
or a `//` comment that happens to spell the pattern.  Audit 14's pair: change
one emit's code and add a comment naming the old one - the count is unchanged
and ns-status-check reads OK.  This lists every such check whose count today
includes at least one comment-line hit, with the split.

    python scripts/ns-migration/8.271/ecode_rows.py            # the audit
    python scripts/ns-migration/8.271/ecode_rows.py --rewrite  # bare E-codes -> the constant

`--rewrite` fixes the one class with a mechanical answer: a check grepping a
BARE error code (`'E0240'`) is rewritten to grep the emitter's constant
(`'ErrorCode::E0240_'`) with the constant's count as its expectation, which a
comment or a message text spelling `E0240` cannot move.  The other classes
are reported, not rewritten: their patterns are names, and a name in a
comment is what the comment is about.
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
LEDGER = os.path.join(ROOT, "docs", "name-resolution.md")

# `grep -c 'PAT' FILE` -> **N**        `grep -rho 'PAT' PATHS [--include=*.cryo] \| wc -l` -> **N**
CHECK_C = re.compile(r"`grep -c '([^']+)' (\S+)` → \*\*(\d+)\*\*")
CHECK_R = re.compile(r"`grep -rho '([^']+)' ([^`|]+?)(?: --include=\*\.cryo)? \\\| wc -l` → \*\*(\d+)\*\*")
BARE_CODE = re.compile(r"^E0\d{3}$")


def section0(text):
    start = text.index("\n## 0. Current state")
    end = text.index("\n## 1.", start)
    return start, end


def hits(pattern, paths, recursive):
    """Every (line text, count of matches on it) grep reports for the check's
    own pattern, so the split is made of the same hits the check counts."""
    args = ["grep", "-rn" if recursive else "-n", "-o", pattern] + paths
    if recursive:
        args.append("--include=*.cryo")
    r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
    out = r.stdout.decode("utf-8", "replace")
    per_line = {}
    for ln in out.splitlines():
        # file:line:match  (recursive)  or  line:match  (single file)
        key = ln.rsplit(":", 1)[0]
        per_line[key] = per_line.get(key, 0) + 1
    return per_line


def line_text(key, recursive, single_file):
    if recursive:
        path, lineno = key.rsplit(":", 1)
    else:
        path, lineno = single_file, key
    try:
        with io.open(os.path.join(ROOT, path), encoding="utf-8", errors="replace", newline="") as f:
            lines = f.read().split("\n")
        return lines[int(lineno) - 1]
    except (OSError, IndexError, ValueError):
        return ""


def main():
    text = io.open(LEDGER, encoding="utf-8", newline="").read()
    s, e = section0(text)
    sec = text[s:e]
    checks = []
    for m in CHECK_C.finditer(sec):
        checks.append(("c", m.group(1), [m.group(2)], int(m.group(3)), m.group(0)))
    for m in CHECK_R.finditer(sec):
        checks.append(("r", m.group(1), m.group(2).split(), int(m.group(3)), m.group(0)))
    src_checks = [c for c in checks if all(p.startswith("compiler/src") or p.startswith("tools/") for p in c[2])]
    print("section 0 grep checks parsed: %d (%d over compiler/src or tools/)" % (len(checks), len(src_checks)))
    mixed = []
    drift = []
    bare = []
    for kind, pat, paths, expected, old in src_checks:
        recursive = kind == "r"
        per_line = hits(pat, paths, recursive)
        if kind == "c":
            total = len(per_line)          # -c counts LINES
        else:
            total = sum(per_line.values())  # -o | wc -l counts HITS
        comment = 0
        for key, n in per_line.items():
            t = line_text(key, recursive, paths[0])
            if t.lstrip().startswith("//"):
                comment += 1 if kind == "c" else n
        if total != expected:
            drift.append((pat, expected, total))
        if comment:
            mixed.append((pat, paths, expected, total, comment))
        if BARE_CODE.match(pat):
            bare.append((pat, paths, expected, total, comment, old))
    print("checks whose count includes a comment-line hit: %d of %d" % (len(mixed), len(src_checks)))
    for pat, paths, expected, total, comment in sorted(mixed):
        print("  %-52s expected %4d  code %4d  comment %3d   %s" % ("'" + pat + "'", expected, total - comment, comment, " ".join(paths)))
    if drift:
        print("checks whose count does not match the row (re-run by this script, not by ns-status-check): %d" % len(drift))
        for pat, expected, total in drift:
            print("  %-52s row %d  tree %d" % ("'" + pat + "'", expected, total))
    print("checks grepping a BARE error code: %d" % len(bare))
    for pat, paths, expected, total, comment, old in bare:
        print("  %-8s expected %3d  comment %3d" % (pat, expected, comment))
    if "--rewrite" not in sys.argv:
        return
    out = text
    n = 0
    for kind, pat, paths, expected, old in src_checks:
        recursive = kind == "r"
        per_line = hits(pat, paths, recursive)
        comment = 0
        for key, cnt in per_line.items():
            t = line_text(key, recursive, paths[0])
            if t.lstrip().startswith("//"):
                comment += 1 if kind == "c" else cnt
        if not comment or expected == 0:
            continue
        if kind == "c" and len(per_line) == comment or kind == "r" and sum(per_line.values()) == comment:
            # A check that is ABOUT a comment (the row asserts a sentence
            # in the source); excluding comments would assert its absence.
            print("  kept    %-40s %d: every hit is a comment, the row asserts one" % ("'" + pat + "'", expected))
            continue
        if BARE_CODE.match(pat):
            # The emitter's constant: a comment or a message text spelling
            # the code cannot move it.
            const_pat = "ErrorCode::" + pat + "_"
            code_hits = sum(hits(const_pat, paths, True).values())
            new = "`grep -rho '%s' %s --include=*.cryo \\| wc -l` → **%d**" % (
                const_pat, " ".join(paths), code_hits)
        elif kind == "c":
            code_hits = len(per_line) - comment
            new = "`grep -v '^\\s*//' %s \\| grep -c '%s'` → **%d**" % (paths[0], pat, code_hits)
        else:
            code_hits = sum(per_line.values()) - comment
            new = "`grep -rh '%s' %s --include=*.cryo \\| grep -v '^\\s*//' \\| grep -o '%s' \\| wc -l` → **%d**" % (
                pat, " ".join(paths), pat, code_hits)
        assert out.count(old) == 1, old
        out = out.replace(old, new)
        n += 1
        print("  rewrote %-40s %d -> %d (comment hits %d)" % ("'" + pat + "'", expected, code_hits, comment))
    io.open(LEDGER, "w", encoding="utf-8", newline="").write(out)
    print("rewritten: %d check(s)" % n)


if __name__ == "__main__":
    main()
