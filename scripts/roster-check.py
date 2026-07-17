#!/usr/bin/env python3
"""Assert the discovered unit-test roster matches the committed golden.

CI never asserted a test COUNT: a compiler change that silently broke
`![test]` discovery (dropping files, modules, or the whole tests/ tree)
stayed green, because "0 of 0 tests failed" is a pass.  This gate pins the
full sorted roster from `cryo test --list` against tests/test-roster.txt.

Usage:
    python scripts/roster-check.py <path-to-cryo> [--update]

Runs `<cryo> test --list` in tests/ (exactly what `make test-list` does),
normalizes the output (strip CR, drop non-test lines, sort), and diffs it
against the golden.  Sorting makes the comparison order-insensitive, so
filesystem enumeration order differences across OSes cannot break it.

    --update   rewrite tests/test-roster.txt from the current roster.
               Do this DELIBERATELY when adding or removing tests, and
               commit the golden alongside the test change.

Exit codes: 0 roster matches (or golden updated); 1 mismatch or failure.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(ROOT, "tests")
GOLDEN = os.path.join(TESTS_DIR, "test-roster.txt")


def roster(cryo):
    r = subprocess.run(
        [cryo, "test", "--list"],
        cwd=TESTS_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    text = r.stdout.decode("utf-8", "replace")
    if r.returncode != 0:
        sys.stderr.write(text)
        sys.stderr.write("roster-check: `cryo test --list` exited %d\n" % r.returncode)
        sys.exit(1)
    lines = [ln.strip() for ln in text.splitlines()]
    entries = sorted(set(ln for ln in lines if ": test" in ln))
    if not entries:
        sys.stderr.write(text)
        sys.stderr.write("roster-check: --list produced no test entries; discovery is broken\n")
        sys.exit(1)
    return entries


def main(argv):
    update = "--update" in argv
    argv = [a for a in argv if a != "--update"]
    if len(argv) != 1:
        sys.stderr.write(__doc__)
        return 2
    entries = roster(argv[0])

    if update:
        with open(GOLDEN, "w", newline="\n") as f:
            f.write("\n".join(entries) + "\n")
        print("roster-check: wrote %d entries to %s" % (len(entries), GOLDEN))
        return 0

    if not os.path.exists(GOLDEN):
        sys.stderr.write("roster-check: golden %s missing; run once with --update\n" % GOLDEN)
        return 1
    with open(GOLDEN, "r") as f:
        want = [ln.strip() for ln in f if ln.strip()]

    got_set, want_set = set(entries), set(want)
    missing = sorted(want_set - got_set)
    extra = sorted(got_set - want_set)
    if not missing and not extra:
        print("roster-check: OK (%d tests)" % len(entries))
        return 0
    for ln in missing:
        sys.stderr.write("roster-check: MISSING  %s\n" % ln)
    for ln in extra:
        sys.stderr.write("roster-check: NEW      %s\n" % ln)
    sys.stderr.write(
        "roster-check: roster drifted (%d missing, %d new vs %d pinned).\n"
        "  Intentional? re-pin with: python scripts/roster-check.py <cryo> --update\n"
        % (len(missing), len(extra), len(want))
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
