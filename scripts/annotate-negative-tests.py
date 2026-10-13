#!/usr/bin/env python3
"""Give every compile-fail file a `//~` annotation per diagnostic it provokes.

WHY
---
`run_negative_suite` has two strengths of check, and which one a file gets is
decided by the file itself.  Without `//~` annotations it asserts one thing:
that `error[<code>]` appears somewhere in the combined output.  That passes on
the code raised at a different line, about a different symbol, or as the third
entry in a cascade - none of which is what the file was written to pin.  With
annotations it asserts each diagnostic's severity, code, line and message
substring, AND that no diagnostic anchored in the file is unannotated, so an
unexpected cascade fails.

The tree had 178 compile-fail files and 32 of them carried no annotation at
all.  This script writes them, from what the compiler actually reports, and is
committed with what it produced so the population and the rule are both
reproducible rather than living in a diff.

HOW
---
For each file: run `<cryo> check <file> --stdlib=<abs>`, parse the diagnostics
anchored in that file by the same rule `Executor::parse_diagnostics` uses (a
`error[`/`warning[` header, the message after `]: `, and the first `-->` line
inside the block that names the file), then insert

    <indent>//~^ ERROR[<code>] <message>

directly BELOW each diagnostic's source line.  Below rather than appended to
the end of that line, because a line inside an unterminated literal cannot take
a comment tail; and `^` rather than a bare `//~` because the annotation is one
line further down.  Insertions are applied from the bottom up so a line number
measured before the edit still names the same source line during it, and the
carets stay relative so the whole set survives later insertions above.

VERIFY, THEN KEEP
-----------------
A file whose annotations do not verify on a second run is REVERTED, not
committed with a note.  Some diagnostics are anchored past the end of the file
(an unterminated string is reported at EOF) and inserting a line moves them; an
annotation that does not hold is worse than none, because the next reader takes
it for a checked claim.  The run prints which files were left alone and why.

Usage:
    python scripts/annotate-negative-tests.py --cryo <compiler> [--only NAME]...
    python scripts/annotate-negative-tests.py --cryo <compiler> --dry-run
"""
import argparse, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEG = os.path.join(ROOT, "tests", "tests", "negative")
STDLIB = os.path.join(ROOT, "stdlib")

HEADER_RE = re.compile(r"^(error|warning)\[([^\]]+)\]:\s*(.*)$")
ARROW_RE = re.compile(r"-->\s*(\S+?):(\d+):(\d+)")


def diagnose(cryo, path):
    """Every diagnostic anchored in `path`, as (line, severity, code, message).

    Scoped to the file the same way the runner scopes it: a block whose `-->`
    names another file (the stdlib, say) is not this file's to annotate.
    """
    r = subprocess.run([cryo, "check", path, "--stdlib=%s" % STDLIB],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       cwd=ROOT)
    out = r.stdout.decode("utf-8", "replace").splitlines()
    base = os.path.basename(path)
    found = []
    cur = None
    for ln in out:
        m = HEADER_RE.match(ln.strip())
        if m:
            cur = (m.group(1), m.group(2), m.group(3).strip())
            continue
        if cur is None:
            continue
        a = ARROW_RE.search(ln)
        if a:
            if os.path.basename(a.group(1)) == base:
                found.append((int(a.group(2)), cur[0], cur[1], cur[2]))
            cur = None
    return found


def annotate(text, diags):
    """Insert one `//~^` line below each diagnostic's source line."""
    lines = text.split("\n")
    by_line = {}
    for line, sev, code, msg in diags:
        by_line.setdefault(line, []).append((sev, code, msg))
    for line in sorted(by_line, reverse=True):
        if line < 1 or line > len(lines):
            return None                     # anchored off the end of the file
        indent = re.match(r"[ \t]*", lines[line - 1]).group(0)
        block = []
        for i, (sev, code, msg) in enumerate(by_line[line], 1):
            word = "ERROR" if sev == "error" else "WARN"
            block.append("%s//~%s %s[%s] %s" % (indent, "^" * i, word, code, msg))
        lines[line:line] = block
    return "\n".join(lines)


def verifies(cryo, path, want):
    """Both directions, as `check_annotations` checks them."""
    got = diagnose(cryo, path)
    text = open(path, encoding="utf-8", newline="").read()
    anns = []
    for i, raw in enumerate(text.split("\n"), 1):
        m = re.search(r"//~(\^*)\s*(ERROR|WARN|WARNING)\[([^\]]+)\]\s*(.*)$", raw)
        if m:
            anns.append((i - len(m.group(1)),
                         "error" if m.group(2) == "ERROR" else "warning",
                         m.group(3), m.group(4).strip()))
    for line, sev, code, msg in anns:
        if not any(g[0] == line and g[1] == sev and g[2] == code
                   and (not msg or msg in g[3]) for g in got):
            return False, "annotation %s[%s] at %d matches no diagnostic" % (sev, code, line)
    for line, sev, code, _msg in got:
        if not any(a[0] == line and a[1] == sev and a[2] == code for a in anns):
            return False, "unannotated %s[%s] at %d" % (sev, code, line)
    return True, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    cryo = os.path.abspath(args.cryo)

    names = sorted(n for n in os.listdir(NEG) if n.endswith(".cryo"))
    if args.only:
        names = [n for n in names if n in args.only]
    wrote, skipped = [], []
    for name in names:
        path = os.path.join(NEG, name)
        original = open(path, encoding="utf-8", newline="").read()
        if "//~" in original:
            continue
        diags = diagnose(cryo, path)
        if not diags:
            skipped.append((name, "no diagnostic is anchored in the file"))
            continue
        new = annotate(original, diags)
        if new is None:
            skipped.append((name, "a diagnostic is anchored past the last line"))
            continue
        if args.dry_run:
            wrote.append((name, len(diags)))
            continue
        open(path, "w", encoding="utf-8", newline="").write(new)
        ok, why = verifies(cryo, path, diags)
        if not ok:
            open(path, "w", encoding="utf-8", newline="").write(original)
            skipped.append((name, why))
            continue
        wrote.append((name, len(diags)))

    for name, n in wrote:
        print("  annotated  %-52s %d diagnostic(s)" % (name, n))
    for name, why in skipped:
        print("  LEFT ALONE %-52s %s" % (name, why))
    print("annotate-negative-tests: %d annotated, %d left alone"
          % (len(wrote), len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
