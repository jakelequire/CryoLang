#!/usr/bin/env python3
"""Remove or weaken the residue classifier's literal rule one way at a time,
and show the self-test refusing each: the rule's inversion, reproducible.

Each mutation is applied ALONE to scripts/ns-migration/residue_classify.py,
then `residue_selftest.py` and `residue.py --check` (the real tree) run, and
the file is restored before the next.  A mutation the real-tree check reads
OK over and the self-test refuses is one only the self-test's cases catch.

Usage:
    python scripts/ns-migration/8.369/rule_mutations.py

Exit codes: 0 every mutation refused by the self-test; 1 otherwise.
"""
import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NS = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(NS))
TARGET = os.path.join(NS, "residue_classify.py")

# (name, text in the classifier, replacement)
MUTATIONS = [
    ("a prefix of the key is enough (the rule fires on `literal:\"a\", param:x`)",
     'CONSTANT_KEY.fullmatch(arg or "")', 'CONSTANT_KEY.match(arg or "")'),
    ("the rule never fires",
     'return CONSTANT_KEY.fullmatch(arg or "") is not None', 'return False'),
    ("an escaped quote ends the literal",
     r'(?:[^"\\]|\\.)*', r'[^"]*'),
    ("a site override may reclass a literal site",
     "return sorted(k for k in SITE_OVERRIDES if is_constant_key(k[2]) and any(",
     "return [] and sorted(k for k in SITE_OVERRIDES if is_constant_key(k[2]) and any("),
    ("the method table outranks the rule",
     "if is_constant_key(arg):", "if is_constant_key(arg) and key not in CLASS_OF_METHOD:"),
]


def run(script, *args):
    p = subprocess.run([sys.executable, os.path.join(NS, script)] + list(args),
                       capture_output=True, text=True, cwd=ROOT)
    lines = (p.stdout + p.stderr).strip().splitlines()
    return p.returncode, lines


def main():
    orig = io.open(TARGET, encoding="utf-8", newline="").read()
    missed = 0
    try:
        for name, old, new in MUTATIONS:
            if orig.count(old) != 1:
                print("MUTATION TEXT NOT FOUND ONCE (%d): %s" % (orig.count(old), name))
                missed += 1
                continue
            io.open(TARGET, "w", encoding="utf-8", newline="").write(orig.replace(old, new))
            st_code, st = run("residue_selftest.py")
            ck_code, ck = run("residue.py", "--check")
            io.open(TARGET, "w", encoding="utf-8", newline="").write(orig)
            print("== %s" % name)
            print("   self-test exit %d" % st_code)
            for l in st:
                if l.startswith("rule case") or l.startswith("facts mutation"):
                    print("     " + l[:200])
            print("   real-tree check exit %d: %s" % (ck_code, ck[-1][:160] if ck else "-"))
            if st_code == 0:
                missed += 1
    finally:
        io.open(TARGET, "w", encoding="utf-8", newline="").write(orig)
    print("rule_mutations: %d of %d refused by the self-test"
          % (len(MUTATIONS) - missed, len(MUTATIONS)))
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
