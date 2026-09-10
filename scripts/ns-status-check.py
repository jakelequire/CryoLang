#!/usr/bin/env python3
"""Run every check §0 of docs/name-resolution.md carries, and fail on drift.

§0 "Current state" exists so an incoming agent reads 143 lines instead of
re-deriving truth from 16,000 append-only ones.  That only works while the 143
are true, and a status section is the easiest kind of document to leave behind:
nothing about it breaks when it goes stale, which is precisely how the thing it
replaces got that way.

Every row that can be checked against the tree carries its command and the
answer it expects, in one shape:

    `<command>` -> **<expected>**

This extracts those rows, runs each command from the repo root, and compares.
A drifted row is a build failure, so the section cannot quietly become a second
archive.

WHAT THIS CANNOT SEE, and it is most of what goes wrong
-------------------------------------------------------
It catches a NUMBER THAT MOVED.  It cannot catch:

  * a decision that never got a row - §8.39's decision 2 was decided and then
    neither taken nor withdrawn across ninety-five entries, and no count
    anywhere would have said so;
  * a row whose number is right and whose STATUS WORD is wrong - `LIVE` where
    the lane is starved, `TAKEN` where the thing was only ruled;
  * a gate's blind spot changing.  Nobody would have grepped `lsp-check` off
    the pin.  That was found by reading the Makefile.

Those need the commit-time half (scripts/ns-commit-guard.py, run from the
commit-msg hook), which makes a §8 entry marked LANDED / FIXED / RULED that does
not touch §0 the same defect as a ledger-only commit.  Between them: the mechanical half keeps the numbers
honest, the structural half keeps the rows from going missing.  Neither is a
substitute for reading it.

THE RE-PIN RISK
---------------
Like any golden, §0 can be made green by editing the expected value.  Two
things push against that and neither is this script alone:

  * every expected value here is DERIVED FROM THE TREE by the command on its
    own row, so it is a duplicate of a tree fact rather than a free-standing
    number somebody chose;
  * the commit-msg hook refuses a commit that changes those values while
    changing nothing outside docs/, so a re-pin has to arrive alongside the
    change that moved it, in a diff a reader can check it against.

If §0's numbers are ever re-pinned in a commit of their own, it has become the
artifact it was built to replace.

Usage:
    python scripts/ns-status-check.py [--verbose] [--min-rows N] [--ledger F]

Exit codes: 0 every row holds; 1 drift, or fewer rows than the floor.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ns_ledger                                              # noqa: E402

# Rows that say out loud they have none.  Counted so the ratio is visible: a
# section of unverifiable claims should not read the same as a checked one.
NO_CHECK = re.compile(r"`no check`")


def section(path=None):
    """§0's text and the file it came from, or None with a reason.

    The file is DISCOVERED rather than named.  The ledger is being split - §0
    and the spec stay put, the §8 archive moves out - and a hardcoded path
    would have left this reading a section that was no longer there, or
    passing over one that had moved.  scripts/ns_ledger.py owns the finding.

    `path` overrides it so the gate can be pointed at a COPY and watched to
    fail; a gate nobody has seen refuse anything is a decoration.  The row
    commands still run from the repo root either way, because what they ask
    about is the tree, not the document.
    """
    if path is None:
        rel, _archives, problem = ns_ledger.find(ROOT)
        if problem:
            return None, None, problem
        path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        return None, None, "%s does not exist" % path
    sec = ns_ledger.section_of(io.open(path, encoding="utf-8").read())
    if not sec:
        return None, None, "no `## 0. Current state` heading in %s" % path
    return sec, path, None


def answer(out):
    """The command's answer: the last whitespace-separated token it printed.

    One rule for every row.  Most of these commands print a bare count, for
    which the answer IS the whole output; a couple print a matched line whose
    count is its last field, and a rule that handled only the first shape would
    have called a correct row drifted.  A second comparison mode for the second
    shape would be a fallback chain, and this file is in a repository that has
    paid for those.

    A row whose check prints more than its answer is noted in the run so it can
    be tightened; it is not a failure, because the claim it makes is still
    checked.
    """
    toks = out.split()
    return toks[-1] if toks else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ledger", default=None,
                    help="the document to read §0 from (default: whichever "
                         "file under docs/ carries the heading)")
    ap.add_argument("--verbose", action="store_true",
                    help="print every row, not only the drifted ones")
    ap.add_argument("--min-rows", type=int, default=25,
                    help="fail if §0 carries fewer checkable rows than this; "
                         "a row losing its check is drift too, and an empty "
                         "extraction otherwise reports as a clean sweep")
    args = ap.parse_args()

    if shutil.which("bash") is None:
        print("ns-status-check: FAIL -- no bash on PATH.")
        print("  The checks are POSIX one-liners (grep/wc/pipes) written to be")
        print("  copied whole; running them any other way would be running")
        print("  something other than what the row says.")
        return 1

    sec, path, why = section(args.ledger)
    if sec is None:
        print("ns-status-check: FAIL -- %s" % why)
        return 1

    checks = ns_ledger.rows(sec)
    declared_none = len(NO_CHECK.findall(sec))
    if len(checks) < args.min_rows:
        print("ns-status-check: FAIL -- §0 carries %d checkable row(s), "
              "expected at least %d." % (len(checks), args.min_rows))
        print("  A row that lost its check is drift: the claim stays and the")
        print("  thing that would have caught it going stale is gone.  If §0")
        print("  genuinely shrank, lower the floor in the same commit and say")
        print("  which row went and why.")
        return 1

    failed = []
    loose = []
    for cmd, expected in checks:
        r = subprocess.run(["bash", "-c", cmd], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = r.stdout.decode("utf-8", "replace")
        got = answer(out)
        if got != expected:
            failed.append((cmd, expected, got, out.strip()))
        elif len(out.split()) > 1:
            loose.append(cmd)
        if args.verbose:
            print("  %-5s %-64s %s" % ("ok" if got == expected else "DRIFT",
                                       cmd[:64], got))

    for cmd, expected, got, out in failed:
        print("ns-status-check: DRIFT")
        print("    check    %s" % cmd)
        print("    §0 says  %s" % expected)
        print("    tree says %s" % (got or "<no output>"))
        if out and out.split() != [got]:
            print("    output   %s" % out.splitlines()[0][:100])

    for cmd in loose:
        print("ns-status-check: note -- this row's check prints more than its "
              "answer, so the row is read off the last field:")
        print("    %s" % cmd)

    if failed:
        print()
        print("ns-status-check: %d of %d §0 row(s) no longer match the tree."
              % (len(failed), len(checks)))
        print("  §0 is what an incoming agent reads INSTEAD of re-deriving the")
        print("  truth, so a stale row is worse than an absent one.  Fix the")
        print("  row in the commit that moved the number, with the reason in")
        print("  the message -- a §0 re-pinned on its own is the document it")
        print("  was built to replace.")
        return 1

    print("ns-status-check: OK -- %d §0 row(s) in %s match the tree "
          "(%d row(s) declare `no check`)"
          % (len(checks), os.path.relpath(path, ROOT).replace(os.sep, "/"),
             declared_none))
    return 0


if __name__ == "__main__":
    sys.exit(main())
