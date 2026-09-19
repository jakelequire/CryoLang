"""Rewrite section 0's checks that grep a GOLDEN into checks that read the
TREE (docs/name-resolution.md section 8.255).

Audit 13: 42 of section 0's checks read a recorded number rather than
reality - 14 against tests/lane-baseline.txt (the lane gate's golden) and 28
against tests/test-roster.txt (the roster golden, verified only on ubuntu
CI).  Three shapes, each with a tree-reading form:

    grep -A1 '^[[]KIND]' tests/lane-baseline.txt | grep -o '[0-9]*$'
        -> python3 scripts/lane-gate.py --row KIND         (the live count)
    grep -c '^\\[' tests/lane-baseline.txt
        -> python3 scripts/lane-gate.py --rows              (the gate's buckets)
    grep -c 'LOOKUP_ROUTED' tests/lane-baseline.txt  (D12: the row exists)
        -> python3 scripts/lane-gate.py --row LOOKUP_ROUTED (the row's count)
    grep -c '^project PREFIX' tests/test-roster.txt
        -> ls -d tests/tests/projects/PREFIX*/test.json | wc -l
    grep -c '^negative PREFIX' tests/test-roster.txt
        -> ls tests/tests/negative/PREFIX*.cryo | wc -l

A project is real when its directory carries the `test.json` marker the
runner discovers it by (a directory without one is skipped without a word),
and a negative when its file exists; a glob that matches nothing prints an
error the checker reads as a mismatch, so the tree form fails closed.

Every rewritten check is RUN in both forms before the file is touched, and
the script refuses if any pair disagrees - a rewrite that changed an answer
is not a rewrite.  `--dry` prints the pairs and writes nothing.
"""
import io, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DOC = os.path.join(ROOT, "docs", "name-resolution.md")
dry = "--dry" in sys.argv

ROW_RE = re.compile(r"grep -A1 '\^(?:\[\[\]|\\\[)([A-Z_]+)(?:\]|\\\])' tests/lane-baseline\.txt (\\\||\|) grep -o '\[0-9\]\*\$?'")
ROWS_RE = re.compile(r"grep -c '\^\\\[' tests/lane-baseline\.txt")
D12_RE = re.compile(r"grep -c 'LOOKUP_ROUTED' tests/lane-baseline\.txt")
PROJ_RE = re.compile(r"grep -c '\^project ([A-Za-z0-9_]*)' tests/test-roster\.txt")
NEG_RE = re.compile(r"grep -c '\^negative ([A-Za-z0-9_.*]*)' tests/test-roster\.txt")


def run(cmd):
    p = subprocess.run(["bash", "-c", cmd], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.stdout.strip().split()[-1] if p.stdout.strip() else ""


def rewrite_line(line, pairs):
    pipe = "\\|" if line.startswith("|") else "|"

    def row(m):
        new = "python3 scripts/lane-gate.py --row %s" % m.group(1)
        pairs.append((m.group(0).replace("\\|", "|"), new))
        return new

    def rows(m):
        new = "python3 scripts/lane-gate.py --rows"
        pairs.append((m.group(0), new))
        return new

    def d12(m):
        new = "python3 scripts/lane-gate.py --row LOOKUP_ROUTED"
        pairs.append((m.group(0), new, "D12"))
        return new

    def proj(m):
        new = "ls -d tests/tests/projects/%s*/test.json %s wc -l" % (m.group(1), pipe)
        pairs.append((m.group(0), new.replace("\\|", "|")))
        return new

    def neg(m):
        pat = m.group(1).replace(".*", "*")
        new = "ls tests/tests/negative/%s*.cryo %s wc -l" % (pat, pipe)
        pairs.append((m.group(0), new.replace("\\|", "|")))
        return new

    line = ROW_RE.sub(row, line)
    line = ROWS_RE.sub(rows, line)
    line = D12_RE.sub(d12, line)
    line = PROJ_RE.sub(proj, line)
    line = NEG_RE.sub(neg, line)
    return line


def main():
    src = io.open(DOC, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    lines = src.split(nl)
    end = next(i for i, l in enumerate(lines) if l.startswith("## 1. The root cause"))
    pairs = []
    out = list(lines)
    for i in range(end):
        out[i] = rewrite_line(lines[i], pairs)
    # D12's check changes meaning (the row exists -> the row's count), so its
    # expected value is rewritten with it; every other pair must agree.
    problems = []
    d12_new = None
    for p in pairs:
        old, new = p[0], p[1]
        a, b = run(old), run(new)
        tag = p[2] if len(p) > 2 else ""
        print("  %-70s %s -> %-52s %s%s" % (old, a, new, b, "  (" + tag + ")" if tag else ""))
        if tag == "D12":
            d12_new = b
        elif a != b:
            problems.append("  %s -> %s: %s != %s" % (old, new, a, b))
    if problems:
        raise SystemExit("tree_checks: %d pair(s) disagree, nothing written:\n%s" % (len(problems), "\n".join(problems)))
    if d12_new is not None:
        for i in range(end):
            out[i] = out[i].replace("`python3 scripts/lane-gate.py --row LOOKUP_ROUTED` → **2**",
                                    "`python3 scripts/lane-gate.py --row LOOKUP_ROUTED` → **%s**" % d12_new)
    print("tree_checks: %d check(s) rewritten, every pair agrees" % len(pairs))
    if dry:
        return
    io.open(DOC, "w", encoding="utf-8", newline="").write(nl.join(out))


if __name__ == "__main__":
    main()
