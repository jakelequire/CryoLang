#!/usr/bin/env python3
"""Assert the discovered test roster matches the committed golden.

CI never asserted a test COUNT: a compiler change that silently broke
`![test]` discovery (dropping files, modules, or the whole tests/ tree)
stayed green, because "0 of 0 tests failed" is a pass.  This gate pins the
full sorted roster against tests/test-roster.txt.

THREE suites, one golden
------------------------
`cryo test` runs three suites and `cryo test --list` enumerates ONE of them:
the `--list` path returns before the compile-fail and project suites are
reached.  A roster built from `--list` alone therefore pinned 2,113 unit tests
and none of the 44 projects or 178 negative files - the majority of the corpus,
carrying every module-system, visibility and resolution gate, was unpinned, and
a deleted project or negative file was a silent no-op.

The other two suites are enumerated HERE instead, from the filesystem, by the
same rules their runners use:

  * projects  - `tests/tests/projects/*/`, each pinned with the FINGERPRINT of
    its `test.json`: the fixture, the polarity, and how many assertions it
    makes.  So a project deleted, or an assertion quietly dropped from one,
    shows up as roster drift rather than as nothing at all.
  * negative  - `tests/tests/negative/*.cryo` (flat, non-recursive - the runner
    does not descend), pinned with the error code its `![config(negative,...)]`
    declares and its count of `//~` annotations.  Losing an annotation weakens
    a file from "this diagnostic, on this line, and no others" to "this code
    appears somewhere", which is a change worth seeing.

Both are pure filesystem reads: no host runs a different population, so unlike
the unit roster these sections need no OS waiver.

Two things are HARD FAILURES rather than golden entries, because pinning them
would grandfather the very silence they cause:

  * a directory under projects/ with no `test.json` - the runner skips it
    without a word, and a skipped project prints exactly what a passing one
    prints (nothing);
  * a negative file with no `![config(negative, <code>)]` directive.

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
               It no longer drops the other platform's entries: a golden
               entry whose test is still `![target]`-gated elsewhere in
               the source is KEPT, and one whose test is actually gone
               is not.

    --merge    add newly discovered tests to the golden without dropping
               entries this host cannot see.  This is the right mode when
               ADDING tests.  Prints the golden-only entries so a genuine
               deletion is still visible rather than silently preserved.

Because the golden is a union, the CHECK waives golden entries whose test is
gated to a different OS: it reads the `![target(<os>)]` directive attached to
each `![test]` function under tests/tests/ and treats those as absent by design
rather than deleted.  Without that, a union golden fails everywhere -- it lists
Windows-only tests Linux cannot compile, and CI runs roster-check on Linux.
The waiver is narrow: a test gated to THIS host, or carrying no gate at all,
still fails when it disappears, which is the discovery breakage this gate exists
to catch.

Exit codes: 0 roster matches (or golden updated); 1 mismatch or failure.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(ROOT, "tests")
TESTS_SRC = os.path.join(TESTS_DIR, "tests")
PROJECTS_DIR = os.path.join(TESTS_SRC, "projects")
NEGATIVE_DIR = os.path.join(TESTS_SRC, "negative")
GOLDEN = os.path.join(TESTS_DIR, "test-roster.txt")

HOST_TARGET = (
    "windows" if sys.platform.startswith("win")
    else "macos" if sys.platform == "darwin"
    else "linux"
)

_DIRECTIVE_RE = re.compile(r"^\s*!\[\s*(\w+)\s*(?:\(([^)]*)\))?\s*\]\s*$")
_FUNCTION_RE = re.compile(r"^\s*(?:public\s+|private\s+)?function\s+(\w+)\s*\(")


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


def gated_tests():
    """Map each `![test]` function's leaf name -> the OS it is gated to.

    The golden is a UNION across platforms -- `--merge` deliberately keeps
    entries this host cannot see -- but `cryo test --list` only ever reports
    what the CURRENT host compiles.  Without this map every OS-gated test reads
    as a deletion, so a union golden could never pass on any host.

    Only a `![target(<os>)]` sharing the contiguous directive run directly above
    a `![test]` function counts; a comment or blank line ends the run.
    """
    gated = {}
    for dirpath, _dirnames, filenames in os.walk(TESTS_SRC):
        for name in filenames:
            if not name.endswith(".cryo"):
                continue
            try:
                with open(os.path.join(dirpath, name), "r",
                          encoding="utf-8", errors="replace") as f:
                    lines = f.read().splitlines()
            except OSError:
                continue
            for i, line in enumerate(lines):
                fn = _FUNCTION_RE.match(line)
                if not fn:
                    continue
                is_test, target, j = False, None, i - 1
                while j >= 0:
                    d = _DIRECTIVE_RE.match(lines[j])
                    if not d:
                        break
                    if d.group(1) == "test":
                        is_test = True
                    elif d.group(1) == "target" and d.group(2):
                        target = d.group(2).strip().strip('"').strip("'")
                    j -= 1
                if not (is_test and target):
                    continue
                leaf = fn.group(1)
                # Two same-named tests gated to different systems would make
                # "absent here" ambiguous; refuse to excuse either.
                gated[leaf] = target if gated.get(leaf, target) == target else None
    return gated


def leaf_name(entry):
    """`CryoTests::Tests::Stdlib::Foo::bar: test` -> `bar`."""
    return entry.rsplit(": test", 1)[0].rsplit("::", 1)[-1].strip()


def golden_entries():
    """Every committed golden entry, whitespace-normalized."""
    if not os.path.exists(GOLDEN):
        return []
    with open(GOLDEN, "r") as f:
        return sorted(set(ln.strip() for ln in f if ln.strip()))


# --- the two suites `cryo test --list` does not reach ----------------------

def _read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def project_entries():
    """One pinned line per test project, plus the hard failures found.

    The line carries a FINGERPRINT of test.json rather than just the name:
    the fixture (`collect`/`build`/`run`), whether the run must fail, the
    declared requirements, and how many assertions the `expect` block makes.
    A name-only line would pin existence and nothing else, and the way these
    projects rot is not deletion - it is an `output_contains` entry going
    missing, which leaves the project running, passing, and checking less.

    A project directory with no `test.json` is returned as an ERROR, not as a
    line: the runner skips it in silence, and a skipped project is
    indistinguishable from a passing one in the output.  Pinning that state
    would preserve it.
    """
    entries, errors = [], []
    if not os.path.isdir(PROJECTS_DIR):
        return entries, ["projects directory %s does not exist" % PROJECTS_DIR]
    for name in sorted(os.listdir(PROJECTS_DIR)):
        d = os.path.join(PROJECTS_DIR, name)
        if not os.path.isdir(d):
            continue
        marker = os.path.join(d, "test.json")
        if not os.path.isfile(marker):
            errors.append(
                "project %s has no test.json; `cryo test` skips the whole "
                "directory without a word, reporting the same green and the "
                "same count as before it existed" % name)
            continue
        try:
            spec = json.loads(_read(marker))
        except ValueError as e:
            errors.append("project %s has a malformed test.json (%s)" % (name, e))
            continue
        outcome = spec.get("outcome", "collect")
        expect = spec.get("expect", {}) or {}
        # `compile_fail` is the build fixture plus the failing polarity, and
        # `diagnostic` is `fails` plus an error[<code>] marker; count what the
        # runner will actually check, not what the file happens to spell.
        fails = bool(expect.get("fails", False)) or outcome == "compile_fail"             or "diagnostic" in expect
        asserts = 0
        if "exit_code" in expect:
            asserts += 1
        if "stdout_contains" in expect:
            asserts += 1
        if "diagnostic" in expect:
            asserts += 1
        asserts += len(expect.get("output_contains", []) or [])
        asserts += len(expect.get("output_excludes", []) or [])
        reqs = ",".join(spec.get("requires", []) or []) or "-"
        entries.append(
            "project %s: outcome=%s fails=%d asserts=%d requires=%s ignore=%d"
            % (name, outcome, int(fails), asserts, reqs,
               int(bool(spec.get("ignore", False)))))
    return entries, errors


def _negative_code(content):
    """The code `Executor::neg_code_from` would read out of this file.

    Same rule, deliberately: first `config(negative`, skip the comma and any
    spaces, then the token up to `)`, `,`, ` ` or `]`.  A different rule here
    would pin a population the runner does not have.
    """
    i = content.find("config(negative")
    if i < 0:
        return ""
    j = i + len("config(negative")
    while j < len(content) and content[j] in ", ":
        j += 1
    out = []
    while j < len(content) and content[j] not in "),  ]" and len(out) < 23:
        out.append(content[j])
        j += 1
    return "".join(out)


def negative_entries():
    """One pinned line per compile-fail file, plus the hard failures found.

    FLAT, not recursive: `run_negative_suite` calls readdir on
    tests/tests/negative once and filters on the `.cryo` extension, so a file
    in a subdirectory there is never run.  Enumerating recursively would pin a
    larger population than the one that executes, which is the failure this
    gate exists to prevent rather than commit.

    `annotations` is the count of `//~` lines.  With none, the suite asserts
    only that `error[<code>]` appears somewhere in the output - the code can
    come from a different line, a different symbol, or a cascade.  With them it
    also asserts each message and its line, and that nothing else was reported.
    Pinning the count makes a file being weakened from the second to the first
    visible.
    """
    entries, errors = [], []
    if not os.path.isdir(NEGATIVE_DIR):
        return entries, ["negative directory %s does not exist" % NEGATIVE_DIR]
    for name in sorted(os.listdir(NEGATIVE_DIR)):
        path = os.path.join(NEGATIVE_DIR, name)
        if not os.path.isfile(path) or not name.endswith(".cryo"):
            continue
        content = _read(path)
        code = _negative_code(content)
        if not code:
            errors.append(
                "negative %s carries no ![config(negative, <code>)] directive; "
                "the suite counts it as a failure but nothing pins that it "
                "still exists" % name)
            continue
        anns = sum(1 for ln in content.splitlines() if "//~" in ln)
        entries.append("negative %s: %s annotations=%d" % (name, code, anns))
    return entries, errors


def is_unit(entry):
    return entry.endswith(": test") or ": test" in entry


def main(argv):
    update = "--update" in argv
    merge = "--merge" in argv
    argv = [a for a in argv if a not in ("--update", "--merge")]
    if len(argv) != 1 or (update and merge):
        sys.stderr.write(__doc__)
        return 2

    # The two suites `--list` cannot reach are enumerated first and their hard
    # failures reported BEFORE the compiler runs: a missing test.json costs
    # nothing to detect and there is no reason to spend a `cryo test --list` to
    # find out about it.  These are refusals, not drift -- --update does not
    # silence them, because writing them into the golden is what preserves the
    # silence they cause.
    proj, proj_errs = project_entries()
    neg, neg_errs = negative_entries()
    for msg in proj_errs + neg_errs:
        sys.stderr.write("roster-check: REFUSED  %s\n" % msg)
    if proj_errs or neg_errs:
        sys.stderr.write(
            "roster-check: %d unrunnable test(s) found; fix them rather than "
            "pinning them.\n" % (len(proj_errs) + len(neg_errs)))
        return 1

    entries = sorted(set(roster(argv[0])) | set(proj) | set(neg))

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
        # KEEP the golden entries whose test is gated to another OS.  They are
        # absent here by design, not deleted, and rewriting the golden from one
        # host used to drop them - after which the gate passed, because the
        # golden it had just written was exactly what this host found.  The
        # docstring warned about it; nothing enforced the warning.
        #
        # Not a second mode bolted on: the waiver is read from the SOURCE, so a
        # test genuinely deleted stops being gated-to-another-OS the moment its
        # `![target(...)]` goes with it, and this drops it.  What survives is
        # only a test that still exists and cannot run here.
        gated = gated_tests()
        keep = [ln for ln in golden_entries()
                if is_unit(ln)
                and gated.get(leaf_name(ln)) not in (None, HOST_TARGET)
                and ln not in set(entries)]
        merged = sorted(set(entries) | set(keep))
        with open(GOLDEN, "w", newline="\n") as f:
            f.write("\n".join(merged) + "\n")
        print("roster-check: wrote %d entries to %s" % (len(merged), GOLDEN))
        for ln in keep:
            print("  kept  %s (gated to %s; absent here by design)"
                  % (ln, gated.get(leaf_name(ln))))
        return 0

    if not os.path.exists(GOLDEN):
        sys.stderr.write("roster-check: golden %s missing; run once with --update\n" % GOLDEN)
        return 1
    with open(GOLDEN, "r") as f:
        want = [ln.strip() for ln in f if ln.strip()]

    got_set, want_set = set(entries), set(want)
    missing = sorted(want_set - got_set)
    extra = sorted(got_set - want_set)

    # An entry gated to a different OS is absent BY DESIGN on this host, not
    # deleted.  A test gated to THIS host that went missing is still a failure,
    # and so is one with no gate at all -- only the other platform's are waived.
    gated = gated_tests()

    def other_platform(ln):
        # Unit entries only.  The project and negative sections are read off
        # the filesystem, so every host enumerates the same population and an
        # absence there is a deletion, never a gate.
        if not is_unit(ln):
            return False
        g = gated.get(leaf_name(ln))
        return g is not None and g != HOST_TARGET

    waived = [ln for ln in missing if other_platform(ln)]
    missing = [ln for ln in missing if not other_platform(ln)]
    for ln in waived:
        print("roster-check: gated   %s (not built on %s)" % (ln, HOST_TARGET))

    if not missing and not extra:
        print("roster-check: OK (%d entries: %d unit, %d project, %d negative; "
              "%d gated to another platform)"
              % (len(entries), len(entries) - len(proj) - len(neg),
                 len(proj), len(neg), len(waived)))
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
