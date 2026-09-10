#!/usr/bin/env python3
"""Run the test suite and assert it ran the whole corpus, not just some of it.

`make test` passes on an exit code, and every suite the runner drives reports
a zero the same way it reports success: silently.  A pattern that matches
nothing suppresses the compile-fail and project sections entirely - no header,
no summary, no line saying they were skipped - and the run ends

    OVERALL  PASS  (unit: ok)

with exit 0.  Broken discovery produces exactly that output.  So does a
`tests/negative` that failed to open, a project directory whose marker went
missing, and a filter someone left in an ARGS.  CLAUDE.md has said for a while
that the COUNT is the evidence and not the word PASS; nothing was checking the
count.

This does.  It runs the suite, streams the output through unchanged so the CI
log reads the same, and then asserts that the three suites accounted for every
entry the roster golden pins:

    unit          passed + failed + ignored + filtered == pinned unit entries
    compile-fail  passed + failed                      == pinned negative files
    projects      passed + failed + skipped + ignored  == pinned projects
    projects      every pinned project was REACHED, by name

Each is an identity, not a floor, and each is HOST-INDEPENDENT even though the
outcomes are not: a project gated to another OS is skipped rather than absent,
and skipped is one of the terms.  A suite whose summary block never printed is
a refusal - that is the zero this exists to see, and it cannot be told from
success any other way.

The golden is the one `roster-check` pins, which is what ties the two
together: roster-check asserts what SHOULD run, this asserts that it DID.
Neither is much use alone - a roster nothing executes is a list, and a count
with nothing to compare it against is a number.

Usage:
    python scripts/test-census.py --cryo <compiler> [-- <cryo test flags>...]

Exit codes: the suite's own, unless the census itself fails (then 1).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(ROOT, "tests")
GOLDEN = os.path.join(TESTS_DIR, "test-roster.txt")

# `    passed        2113` - the rows every suite summary block prints.
ROW = re.compile(r"^\s*(passed|failed|ignored|filtered)\s+(\d+)\s*$")
BLOCK = re.compile(r"^\s*(unit test|compile-fail|projects) summary\s*$")
# plain format: one line per project the runner reached, whatever the verdict.
PROJ_LINE = re.compile(r"^test \(project\) (\S+) \.\.\.")
# The verdict tokens are matched WHEREVER they land rather than as the tail of
# the line above, because they do not reliably land there.  The `cxx`
# requirement probe shells out without redirecting stderr, so on Windows the
# shell writes `The system cannot find the path specified.` into the middle of
# ffi_cpp_link's line and pushes its own verdict onto the next one.  Anchoring
# the verdict to the line counted 43 of 44 projects and blamed the runner for a
# leak in a probe.
PROJ_SKIP = re.compile(r"skipped \(requires (.+?)\)\s*$")
PROJ_IGNORE = re.compile(r"(?:^|\.\.\. )ignored\s*$")


def pinned():
    """(unit count, negative count, project NAMES) from the roster golden."""
    unit = neg = 0
    projects = set()
    with open(GOLDEN, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("negative "):
                neg += 1
            elif line.startswith("project "):
                projects.add(line.split(None, 1)[1].split(":", 1)[0])
            elif ": test" in line:
                unit += 1
    return unit, neg, projects


def parse(lines):
    """Suite summary rows, the projects reached, and the ones that did not run.

    Reads the SUMMARY blocks rather than counting per-test lines: the per-test
    line differs by --format and the summary does not, and a census that works
    in one output format only is one nobody runs in the format they use.
    """
    blocks = {}
    reached = []
    skipped = []
    ignored = 0
    last_project = None
    current = None
    for raw in lines:
        line = raw.rstrip("\n").rstrip("\r")
        m = BLOCK.match(line)
        if m:
            current = m.group(1)
            blocks.setdefault(current, {})
            continue
        m = PROJ_LINE.match(line.strip())
        if m:
            last_project = m.group(1)
            reached.append(last_project)
        m = PROJ_SKIP.search(line.strip())
        if m:
            skipped.append((last_project or "<unknown>", m.group(1)))
            continue
        if PROJ_IGNORE.search(line.strip()):
            ignored += 1
            continue
        if current:
            m = ROW.match(line)
            if m:
                blocks[current][m.group(1)] = int(m.group(2))
            elif line.strip().startswith("result"):
                current = None
    return blocks, reached, skipped, ignored


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    # parse_known_args, not a REMAINDER positional: REMAINDER only starts
    # collecting after something argparse already accepted, so a forwarded
    # `--opt-level=0` arriving straight after `--cryo X` was rejected as an
    # unrecognized argument rather than passed on.  The Makefile appends
    # $(ARGS) with no separator, which is precisely that shape.
    args, extra = ap.parse_known_args()
    extra = [a for a in extra if a != "--"]

    # `--format=plain` is forced, not defaulted: the project cryoconfig asks
    # for `pretty`, whose per-project skip line reads `[SKIPPED: requires x]`,
    # and the census has to reconcile skips against the roster.  One format,
    # one parse.  A caller-supplied --format would silently change what this
    # can see, so it is refused rather than overridden.
    if any(a.startswith("--format") for a in extra):
        sys.stderr.write("test-census: --format is fixed at plain; the census "
                         "parses the runner output and cannot certify a format "
                         "it was not written against\n")
        return 1
    if any(not a.startswith("-") for a in extra):
        sys.stderr.write("test-census: a test-name PATTERN filters all three "
                         "suites, which is the exact shape of the hole this "
                         "checks for; run the census unfiltered\n")
        return 1

    cmd = [os.path.abspath(args.cryo), "test", "--format=plain"] + extra
    proc = subprocess.Popen(cmd, cwd=TESTS_DIR, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    captured = []
    for raw in proc.stdout:
        line = raw.decode("utf-8", "replace")
        captured.append(line)
        sys.stdout.write(line)
    sys.stdout.flush()
    code = proc.wait()

    want_unit, want_neg, want_projects = pinned()
    blocks, reached, skipped, ignored = parse(captured)

    problems = []

    def census(name, key, want, terms):
        got = blocks.get(key)
        if got is None:
            problems.append(
                "%s: no summary block was printed at all.  A suite that ran "
                "nothing prints exactly this, and so does one that could not "
                "be reached; %d entries are pinned." % (name, want))
            return
        total = sum(got.get(t, 0) for t in terms)
        if total != want:
            problems.append(
                "%s: accounted for %d of %d pinned (%s)"
                % (name, total, want,
                   ", ".join("%s %d" % (t, got.get(t, 0)) for t in terms)))

    census("unit", "unit test", want_unit,
           ("passed", "failed", "ignored", "filtered"))
    census("compile-fail", "compile-fail", want_neg, ("passed", "failed"))

    proj = blocks.get("projects")
    if proj is None:
        problems.append(
            "projects: no summary block was printed at all.  A projects "
            "directory the runner could not open prints exactly this; %d "
            "projects are pinned." % len(want_projects))
    else:
        # Skipped and ignored are not in the runner's `ran` count, so they are
        # not in its summary either - they have to be counted off the
        # per-project lines or they read as missing.  A project gated to
        # another OS is skipped HERE and run elsewhere, which is why this is an
        # identity over four terms rather than a comparison against a number.
        total = (proj.get("passed", 0) + proj.get("failed", 0)
                 + len(skipped) + ignored)
        if total != len(want_projects):
            problems.append(
                "projects: accounted for %d of %d pinned (passed %d, failed "
                "%d, skipped %d, ignored %d)"
                % (total, len(want_projects), proj.get("passed", 0),
                   proj.get("failed", 0), len(skipped), ignored))
        # By NAME as well as by count.  A count can be met by the wrong set -
        # one project added and another silently dropped nets to zero - and the
        # name check is what says which one went missing rather than that one
        # did.
        missing = sorted(want_projects - set(reached))
        for name in missing:
            problems.append("projects: %s is pinned but the runner never "
                            "reached it" % name)
        for name in sorted(set(reached) - want_projects):
            problems.append("projects: %s ran but is not pinned; re-pin the "
                            "roster" % name)

    print()
    print("test-census: roster pins %d unit, %d compile-fail, %d project(s)"
          % (want_unit, want_neg, len(want_projects)))
    for name, reason in skipped:
        print("  skipped  %-40s requires %s" % (name, reason))
    if ignored:
        print("  ignored  %d project(s)" % ignored)

    if problems:
        for p in problems:
            sys.stderr.write("test-census: %s\n" % p)
        sys.stderr.write(
            "test-census: the suite did not account for the pinned corpus.  A\n"
            "  suite that ran nothing reports the same green as one that ran\n"
            "  everything, so this fails even when the run exited 0.  Re-pin\n"
            "  the roster (scripts/roster-check.py <cryo> --merge) only if the\n"
            "  corpus genuinely changed.\n")
        return 1

    print("test-census: OK -- every pinned entry accounted for; suite exited %d"
          % code)
    return code


if __name__ == "__main__":
    sys.exit(main())
