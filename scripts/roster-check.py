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

               THE ROSTER IS PLATFORM-SENSITIVE.  Some tests are gated to
               one OS (`ProcessCommand::output_large_stderr_no_deadlock_win`
               is Windows-only), so --update run on Linux silently DELETES
               the other platform's entries -- and the gate then passes,
               because the golden it just wrote is what this host found.
               Use --update only when deliberately REMOVING tests.

    --merge    add newly discovered tests to the golden without dropping
               entries this host cannot see.  This is the right mode when
               ADDING tests.  Prints the golden-only entries so a genuine
               deletion is still visible rather than silently preserved.

Exit codes: 0 roster matches (or golden updated); 1 mismatch or failure.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(ROOT, "tests")
GOLDEN = os.path.join(TESTS_DIR, "test-roster.txt")


def resolve_cryo(cryo):
    """Pin the compiler path to the INVOKING directory.

    `roster()` runs the compiler with `cwd=tests/`, and on POSIX that chdir
    happens in the child before exec -- so a relative path like
    `compiler/build/cryo` would be looked up as `tests/compiler/build/cryo`
    and fail with a bare ENOENT that names the path the user typed, which
    reads as "the compiler is missing" rather than "it was resolved from
    somewhere else". The Makefile passes an absolute path and never sees
    this; a hand-typed relative one does.

    A bare name with no separator is left alone so a PATH lookup still works.
    """
    if os.path.isabs(cryo) or os.sep not in cryo.replace("/", os.sep):
        return cryo
    return os.path.abspath(cryo)


def roster(cryo):
    r = subprocess.run(
        [resolve_cryo(cryo), "test", "--list"],
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


def golden_entries():
    """The committed golden, filtered exactly as `roster()` filters --list."""
    if not os.path.exists(GOLDEN):
        return []
    with open(GOLDEN, "r") as f:
        return sorted(set(ln.strip() for ln in f if ": test" in ln))


def main(argv):
    update = "--update" in argv
    merge = "--merge" in argv
    argv = [a for a in argv if a not in ("--update", "--merge")]
    if len(argv) != 1 or (update and merge):
        sys.stderr.write(__doc__)
        return 2
    entries = roster(argv[0])

    if merge:
        want = golden_entries()
        only_golden = sorted(set(want) - set(entries))
        added = sorted(set(entries) - set(want))
        merged = sorted(set(entries) | set(want))
        with open(GOLDEN, "w", newline="\n") as f:
            f.write("\n".join(merged) + "\n")
        print("roster-check: merged -> %d entries (%d added)" % (len(merged), len(added)))
        for ln in added:
            print("  + %s" % ln)
        # Kept, not dropped: this host cannot run them, which is not the same
        # as their having been deleted.  Inspect them -- on Linux this must be
        # exactly the Windows-only entries.
        print("roster-check: %d golden-only entry(ies) KEPT (other platform):" % len(only_golden))
        for ln in only_golden:
            print("  = %s" % ln)
        return 0

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
        "  Added tests?   python scripts/roster-check.py <cryo> --merge\n"
        "  Removed tests? python scripts/roster-check.py <cryo> --update\n"
        "  (--update rewrites the golden from THIS host and drops the other\n"
        "   platform's OS-gated entries; --merge keeps them.)\n"
        % (len(missing), len(extra), len(want))
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
