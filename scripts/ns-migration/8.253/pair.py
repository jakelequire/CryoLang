#!/usr/bin/env python3
"""The pair for the store rule (docs/name-resolution.md section 8.253).

A gate is fixed when a mutation the OLD gate accepted is refused by the NEW
one.  This script builds the audit's mutation - a new map-keyed type carried
by the CompilationContext, with a SymbolStr reader and a caller - from the
self-test's own fixture, then runs both gates over it:

  * the old gate (scripts/lane-gate.py at the commit given, default 7239982f,
    whose rule 1 was a hand-written STORES dict of seven) pins a golden over
    the fixture and reads OK over the mutated tree;
  * the new gate (the working tree's) refuses the same tree, naming the type.

Usage:
    python scripts/ns-migration/8.253/pair.py [OLD_COMMIT]

Exit 0 when both halves behaved, 1 otherwise.  Nothing is written outside a
temporary directory.
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SELFTEST = os.path.join(ROOT, "scripts", "lane-gate-selftest.py")
NEW_GATE = os.path.join(ROOT, "scripts", "lane-gate.py")
MUTATION = "the audit's mutation"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(gate, src, golden, *extra):
    p = subprocess.run([sys.executable, gate, "--src", src, "--golden", golden] + list(extra),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main():
    old_commit = sys.argv[1] if len(sys.argv) > 1 else "7239982f"
    st = load(SELFTEST, "lane_gate_selftest")
    edits = [m for m in st.MUTATIONS if m[0].startswith(MUTATION)]
    if len(edits) != 1:
        sys.stderr.write("pair: the self-test no longer carries `%s`\n" % MUTATION)
        return 1
    _name, edits, _code, want = edits[0]

    work = tempfile.mkdtemp(prefix="lane-gate-pair-")
    try:
        old_gate = os.path.join(work, "old-lane-gate.py")
        src = subprocess.run(["git", "-C", ROOT, "show", "%s:scripts/lane-gate.py" % old_commit],
                             stdout=subprocess.PIPE, text=True, check=True).stdout
        with open(old_gate, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(src)

        base = os.path.join(work, "base")
        st.write_tree(base, st.FILES)
        mutated = os.path.join(work, "mutated")
        shutil.copytree(base, mutated)
        st.write_tree(mutated, edits)

        old_golden = os.path.join(work, "old-golden.txt")
        code, out = run(old_gate, base, old_golden, "--update")
        if code != 0:
            sys.stderr.write("pair: the old gate could not pin the fixture:\n%s" % out)
            return 1
        old_code, old_out = run(old_gate, mutated, old_golden)
        new_code, new_out = run(NEW_GATE, mutated, old_golden)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("OLD gate (%s) over the mutated tree: exit %d\n  %s" % (old_commit, old_code, old_out.strip()))
    print("NEW gate over the same tree: exit %d\n  %s" % (new_code, new_out.strip().replace("\n", "\n  ")))
    ok = old_code == 0 and "lane-gate: OK" in old_out and new_code == 1 and want in new_out
    print("pair: %s" % ("both halves behaved" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
