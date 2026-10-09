#!/usr/bin/env python3
"""`cryo build --ast` prints every syntax-tree node whole, in colour.

Builds `tests/fixtures/ast-dump` with `--ast` and reads the dump of its own
`src/main.cryo`.  Every line of the tree must carry a node after its tree
prefix (`|-`, `` `- ``, `| `) once the colour sequences are stripped, the dump
must name what the fixture declares (`answer_of`, `seed`, `doubled`, `21`),
and its lines must be coloured with ESC sequences.

The dumper prints through `printf`, which stops at a NUL byte: a colour
written with an escape the lexer does not read as ESC (an octal `\\033` is
`\\0` followed by "33") ends every line at its first colour, leaving the bare
tree prefix and nothing of the node.

usage: python scripts/ast-dump-check.py --cryo <compiler>
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "ast-dump")
SCRATCH = os.path.join(ROOT, ".verify", "ast-dump")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")
HEADER = "=== AST: src/main.cryo ==="
NAMES = ("answer_of", "seed", "doubled", "21")
COLOUR = re.compile(r"\x1b\[[0-9;]*m")
PREFIX = re.compile(r"^[|` -]*")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    args = ap.parse_args()
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH)
    p = subprocess.run([args.cryo, "build", "--ast", "--build-dir=" + SCRATCH.replace("\\", "/"),
                        "--stdlib=" + STDLIB],
                       cwd=FIXTURE, env=dict(os.environ, CRYO_STDLIB=STDLIB),
                       capture_output=True)
    out = p.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    if p.returncode != 0:
        sys.stdout.write(out + p.stderr.decode("utf-8", errors="replace"))
        print("ast-dump-check: FAIL -- the fixture did not build (exit %d)" % p.returncode)
        return 1
    lines = out.split("\n")
    if HEADER not in lines:
        print("ast-dump-check: FAIL -- no `%s` in the output" % HEADER)
        return 1
    section = []
    for l in lines[lines.index(HEADER) + 1:]:
        if l.startswith("=== AST:") or l.startswith("Building ") or l.startswith("Compiled ->"):
            break
        if l.strip():
            section.append(l)
    problems = []
    bare = [l for l in section if not PREFIX.sub("", COLOUR.sub("", l)).strip()]
    if bare:
        problems.append("%d of %d tree line(s) carry no node, e.g. %r" % (len(bare), len(section), bare[0]))
    text = "\n".join(COLOUR.sub("", l) for l in section)
    missing = [n for n in NAMES if not re.search(r"\b" + re.escape(n) + r"\b", text)]
    if missing:
        problems.append("the dump never names %s" % ", ".join(missing))
    if not any(COLOUR.search(l) for l in section):
        problems.append("no line is coloured")
    if "\x00" in out:
        problems.append("the output holds a NUL byte")
    if problems:
        print("ast-dump-check: FAIL -- " + "; ".join(problems))
        return 1
    print("ast-dump-check: OK -- %d tree line(s), each with its node, coloured, naming %s"
          % (len(section), ", ".join(NAMES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
