#!/usr/bin/env python3
"""Pin the B1 "fuzzy fallback" bucket against a committed golden.

docs/name-resolution.md §7.2 mechanism 3 calls `B1 == 0` a permanent CI gate:
the nine-step resolution cascade grew for years because nobody could see it
growing, and a gate converts "do not add fallbacks" from a principle into a
build failure.  Until this script existed the gate was a sentence in a document
-- `CRYO_RESOLVE_COUNTER` appeared in no Makefile, script, or workflow -- so
every B1 figure in §8 was a snapshot taken by hand and the cascade could regrow
between snapshots exactly as it did before.

WHY THIS IS A RATCHET AND NOT A LITERAL `B1 == 0` ASSERTION
-----------------------------------------------------------
B1 is 153,464 today, not zero.  Zero is the END STATE of the Phase 2-4 work,
not a property of the current tree, so `assert b1 == 0` would be red from the
moment it was wired.  §7.2a already recorded the sibling failure -- "a gate that
cannot fail is indistinguishable from a gate that passes" -- and a gate that can
ONLY fail is worse still, because it gets disabled.

So this pins the CURRENT value and fails on DRIFT IN EITHER DIRECTION:

  * an increase is a regression -- a fallback regrew, which is the thing §7.2
    exists to catch;
  * a decrease is progress, and must be re-pinned DELIBERATELY so the new,
    lower ceiling is what holds afterwards.  A silently-tolerated decrease
    leaves the old higher number as the bound, and a later regression back up
    to it goes unnoticed -- which would reintroduce the exact blind spot.

When the keystone and the §5 deletions drive the total to 0, the golden reads 0
and this becomes the permanent `B1 == 0` gate §7.2 specifies, with no change to
this script.

WHAT IS ASSERTED
----------------
The B1 total AND every per-site row the report flags `B1` or `B3*`.  The
breakdown is asserted, not merely reported, because a change that moves 500
answers from one fallback to another without moving the total is still a
resolution behavior change, and the cascade's history is precisely that of steps
being added and shifted without anyone seeing it.  The failure output is a
per-site diff naming which step moved, so re-pinning is a decision rather than a
guess.

`B3*` is the one exception to "B3 is context only", and it is not a general
licence to pin B3 rows.  It marks a site that left B1 because it answers a
module-INDEPENDENT identity question rather than binding a name to a
declaration -- so it has no reduction target and belongs in B3 -- while still
being the kind of leaf lookup a future caller could misuse for binding.  Such a
row keeps its ratchet because the alternative is to trade a reachable B1 target
for a lane nobody can see growing, which is the failure this gate exists to
prevent.  A B3 row with no such regrowth story stays unasserted.

THE ROWS DO NOT SUM TO THE TOTAL, AND MUST NOT BE ASSERTED TO
-------------------------------------------------------------
The `B1` flag in the report is a FAMILY label -- "this site belongs to the
fuzzy-fallback group" -- not the summation set.  Three ways they differ, all
deliberate in resolve_counter.cryo:

  * calls vs hits.  `lookup_by_leaf calls` and `M1..M5 calls` are flagged B1,
    but only the HITS are summed: B1 counts ANSWERS PRODUCED, never attempts,
    because a fallback that is called and answers nothing is not load-bearing
    and counting attempts would make "drive it to zero" unreachable for the
    wrong reason.
  * nesting.  Cascade step 5 is flagged `B1*` and excluded, because it is
    already inside `lookup_by_leaf hits`; adding both double-counts it.
  * so row_sum > total, normally by a lot.

What DOES hold is `total <= row_sum`: every summand is flagged B1.  That is
checked, and it is the real completeness test -- if the total ever exceeds the
rows, a summand is not being parsed and the per-site assertion has gone blind.

The B2 and B3 TOTALS are recorded for context but NOT asserted.  B2 is
documented as a floor (§8.3, sema's dispatch is uninstrumented) and B3's target
is "once per path", not a fixed number; asserting either would make the gate
noisy, and a noisy gate gets turned off.  Individual B3 rows are likewise
unasserted, apart from the `B3*` exception above.

THE GOLDEN IS PER-HOST, WITH NO INHERITANCE
-------------------------------------------
One row genuinely differs by host: `lookup_by_leaf calls` is 5,041 on Windows
and 4,989 on Linux for the same commit, because a Windows build compiles
Windows-only stdlib modules that a Linux build never loads.  The same asymmetry
moves `2c` by 20 and `declaration took a slot an import held` from 9 to 2.  B1's
TOTAL is identical on both.

A single cross-host golden therefore reads as DRIFT on whichever host did not
pin it -- which is what it did: the gate was red on Linux at a commit whose
handoff certified it green on Windows, and the offered fix was to re-pin
DOWNWARD, i.e. to hand the redness to the other host and flip-flop with whoever
ran it last.  A permanently red ratchet is what §7 says gets switched off, so
the gate is taught the dimension instead.

Each host gets its OWN `[host:<key>]` section and nothing is inherited between
them.  Inheritance was rejected deliberately: with a base plus overrides, a
re-pin on one host silently changes the other host's bound for every row it did
not override, which is the same invisible-movement failure this gate exists to
prevent.  The cost is that a genuine improvement must be re-pinned once per
host; that is the intended cost, because each host is a separate measurement and
`--update` can only honestly rewrite the one it just ran.

A host with no section is a hard failure, not a pass: an unpinned host is
unmeasured, and this gate must never report OK for a number nobody committed.

MEASUREMENT CONSTRAINTS -- all three are load-bearing
-----------------------------------------------------
  * `--no-incremental`, or the build prints "up to date" and the counter
    reports nothing at all (a zero that looks like a pass).
  * `CRYO_CODEGEN_THREADS=1`.  Counters are per-process; multiprocess codegen
    (>=16 modules) silently drops child tallies.
  * A FIXED EXTERNAL TARGET, never the compiler's own build.  The counter's
    source is part of the compiler being measured, so self-build counts drift
    legitimately between runs and could never be pinned (§8's landmine 9).
  * `CRYO_STDLIB` must point at the repo's stdlib: a target outside the repo
    root cannot find it, and the two runs of a comparison would not be
    measuring the same program.

Usage:
    python3 scripts/b1-gate.py <path-to-cryo> [--update] [--verify-determinism]

    --update               rewrite the golden from the current measurement.
                           Do this deliberately, and commit the golden with
                           the change that moved it.
    --verify-determinism   run the measurement twice and report whether the
                           two runs agree.  Proves the gate is stable before
                           it is trusted; does not touch the golden.

Exit codes: 0 match (or golden updated); 1 drift or measurement failure.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STDLIB = os.path.join(ROOT, "stdlib")
GOLDEN = os.path.join(ROOT, "tests", "b1-baseline.txt")

# The measured target.  A fixed, in-repo project that is NOT the compiler
# (see MEASUREMENT CONSTRAINTS).  It must exercise enough cross-module
# resolution for B1 to be nonzero -- a target with no B1 events would give a
# gate that cannot observe regrowth, which is the §7.2a failure mode.
TARGET = os.path.join(ROOT, "examples", "09-json-config")
BUILD_SUBDIR = os.path.join("build", "b1-gate")

_B1_TOTAL_RE = re.compile(r"^\s*B1 fuzzy fallback\s*->\s*ZERO\s+(\d+)\s*$")
_B2_TOTAL_RE = re.compile(r"^\s*B2 type-dependent\s*->\s*stays \(FLOOR\)\s+(\d+)\s*$")
_B3_TOTAL_RE = re.compile(r"^\s*B3 authoritative\s*->\s*once per path\s+(\d+)\s*$")
_B4_TOTAL_RE = re.compile(r"^\s*B4 instantiation id\s*->\s*stays \(FLOOR\)\s+(\d+)\s*$")


def split_row(line):
    """Parse one `print_row` line -> (bucket, label, count), or None.

    The emitted format is `"  %-3s %-46s %12llu"` (resolve_counter.cryo):
    two leading spaces, a 3-char bucket field, a space, the label padded to
    46, then the tally.  Parsed by column and `rsplit` rather than by regex
    because BOTH of the obvious regexes are wrong here:

      * anchoring on `^B1` never matches -- every row starts with two spaces;
      * splitting the label from the count on a run of 2+ spaces splits inside
        the label instead, because labels contain internal double spaces
        ("2c  home-module preference", "  of those, leaf declared by >1
        module").

    Taking the count as the last whitespace-separated token is exact for any
    label, and the bucket is a fixed column.
    """
    stripped = line.rstrip()
    if len(stripped) < 6 or not stripped.startswith("  "):
        return None
    parts = stripped.rsplit(None, 1)
    if len(parts) != 2 or not parts[1].isdigit():
        return None
    head, count = parts[0], int(parts[1])
    if len(head) < 5:
        return None
    return head[2:5].strip(), head[5:].strip(), count


def resolve_cryo(cryo):
    """Pin the compiler path to the INVOKING directory.

    The measurement runs with `cwd=TARGET`, and on POSIX that chdir happens in
    the child before exec -- so a relative `compiler/build/cryo` would resolve
    against the example directory and fail with a bare ENOENT naming the path
    the user typed.  Same reasoning as roster-check.py's copy of this.
    """
    if os.path.isabs(cryo) or os.sep not in cryo.replace("/", os.sep):
        return cryo
    return os.path.abspath(cryo)


def measure(cryo):
    """Build TARGET with the counter on; return (b1, b2, b3, b4, rows).

    `rows` is the ordered list of (label, count) for the sites that make up
    B1.  Returns via sys.exit(1) on any measurement failure -- a gate that
    cannot measure must fail loudly rather than pass on a missing number.
    """
    cryo = resolve_cryo(cryo)
    if not os.path.isdir(TARGET):
        sys.stderr.write("b1-gate: measured target is missing: %s\n" % TARGET)
        sys.exit(1)

    # A stale build dir would let the compiler skip work even under
    # --no-incremental (artifacts present == nothing to do for some steps),
    # so the measurement always starts from nothing.
    build_dir = os.path.join(TARGET, BUILD_SUBDIR)
    shutil.rmtree(build_dir, ignore_errors=True)

    env = dict(os.environ)
    env["CRYO_RESOLVE_COUNTER"] = "1"
    env["CRYO_CODEGEN_THREADS"] = "1"
    env["CRYO_STDLIB"] = STDLIB

    r = subprocess.run(
        [cryo, "build", "--no-incremental",
         "--build-dir=%s" % BUILD_SUBDIR],
        cwd=TARGET,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out = r.stdout.decode("utf-8", "replace")
    err = r.stderr.decode("utf-8", "replace")
    if r.returncode != 0:
        sys.stderr.write(out)
        sys.stderr.write(err)
        sys.stderr.write("b1-gate: `cryo build` exited %d\n" % r.returncode)
        # The counter report is emitted only on the SUCCESS path of a full
        # build -- after link (instance.cryo).  `cryo check` produces no report
        # at all, so this gate cannot fall back to a non-linking path, and a
        # link failure for reasons unrelated to name resolution presents as
        # "the gate is broken".  The one that actually happens:
        # `selfhost-check` wipes the FLAT, SHARED runtime/.bin and leaves
        # WINDOWS objects in it, so the next Linux link dies on __ImageBase.
        if "__ImageBase" in err or "R_AMD64_IMAGEBASE" in err:
            sys.stderr.write(
                "\n         This is the shared-runtime/.bin landmine, NOT a B1 change:\n"
                "         runtime/.bin currently holds Windows objects (selfhost-check\n"
                "         leaves them there). Restore the host tiers and re-run:\n"
                "             make -C %s stdlib runtime-tiers\n" % ROOT)
        sys.exit(1)

    return parse_report(err)


def parse_report(err):
    """Pure parse of the counter report on stderr -> (b1, b2, b3, b4, rows).

    Split out from `measure` so it is testable without running a build.
    """
    # Checked before anything is parsed, because an overflow makes every row
    # below it unreadable rather than merely suspect: a site whose bumps were
    # discarded still prints a plausible 0, so its drop reads as progress and
    # invites exactly the re-pin that would bake the blindness into the golden.
    # This gate exists to notice a fallback regrowing; certifying a number the
    # compiler stopped recording is the one way it can fail silently.
    if "TALLY OVERFLOW" in err:
        # The report itself is not dumped: unlike the parse failures below it is
        # perfectly well-formed, just untrustworthy, and the compiler has already
        # printed the banner that says so.
        sys.stderr.write(
            "b1-gate: the counter's tally array overflowed.\n"
            "         Sites past its capacity reported 0 WITHOUT being measured,\n"
            "         so neither the total nor the breakdown can be trusted.\n"
            "         Raise TALLY_CAP in resolve_counter.cryo. Do NOT re-pin:\n"
            "         a row that fell to 0 here did not improve, it went blind.\n")
        sys.exit(1)

    b1 = b2 = b3 = b4 = None
    rows = []
    for line in err.splitlines():
        # The three bucket-total lines MUST be matched before split_row.
        # `  B1 fuzzy fallback   -> ZERO   153464` satisfies split_row's shape
        # too -- it would otherwise be recorded as a B1 site labelled
        # "fuzzy fallback   -> ZERO" and double-count the whole bucket.
        m = _B1_TOTAL_RE.match(line)
        if m:
            b1 = int(m.group(1))
            continue
        m = _B2_TOTAL_RE.match(line)
        if m:
            b2 = int(m.group(1))
            continue
        m = _B3_TOTAL_RE.match(line)
        if m:
            b3 = int(m.group(1))
            continue
        m = _B4_TOTAL_RE.match(line)
        if m:
            b4 = int(m.group(1))
            continue
        row = split_row(line)
        # `B1` and `B1*` (the nested step-5 row) are both fuzzy-fallback
        # family sites worth pinning; see the module docstring for why this
        # set is not the same as the set of summands.  `B3*` is the narrow
        # exception to "B3 is context only": a site that left B1 because it
        # keys identity rather than binding names, but could regrow into a
        # binding path if a future caller misused it, and would do so
        # invisibly in an unasserted bucket.
        if row and (row[0].startswith("B1") or row[0].startswith("B3*")
                    or row[0].startswith("B4")):
            rows.append((row[1], row[2]))

    if b1 is None or b4 is None:
        sys.stderr.write(err)
        sys.stderr.write(
            "b1-gate: no B1/B4 total in the counter report.\n"
            "         The build may not have run the counter at all -- check that\n"
            "         CRYO_RESOLVE_COUNTER is still honored and that the build was\n"
            "         not skipped as up-to-date.\n")
        sys.exit(1)
    if not rows:
        sys.stderr.write(
            "b1-gate: the B1 total is present but no per-site B1 rows were parsed.\n"
            "         The report's row format changed; update _B1_ROW_RE rather\n"
            "         than pinning a total whose breakdown is invisible.\n")
        sys.exit(1)

    # Labels are the row identity, and the drift report compares them as dict
    # keys -- so two sites sharing a label would silently collapse into one and
    # the gate would assert less than it appears to.  Labels come from an
    # exhaustive `match` in resolve_counter.cryo, so a duplicate is a typo
    # there, not a condition to tolerate.
    seen = {}
    for label, count in rows:
        if label in seen:
            sys.stderr.write(
                "b1-gate: two B1 sites share the label %r.\n"
                "         Row identity is the label, so these would silently\n"
                "         merge. Give them distinct labels in Site::label().\n"
                % label)
            sys.exit(1)
        seen[label] = count

    # Completeness check.  The rows do NOT sum to the total (calls-vs-hits and
    # the nested step-5 row -- see the module docstring), so equality is the
    # WRONG assertion here and asserting it would make this gate unrunnable.
    # What must hold is that every summand appears among the flagged rows,
    # i.e. the total never exceeds their sum.  If it does, a B1 contributor is
    # not being parsed and the per-site assertion has silently gone blind.
    row_sum = sum(c for _, c in rows)
    if b1 + b4 > row_sum:
        sys.stderr.write(
            "b1-gate: B1+B4 total is %d but the flagged rows sum to only %d.\n"
            "         A summand is not being parsed, so the per-site assertion\n"
            "         covers less than it claims to. Fix the parser before\n"
            "         trusting this gate.\n" % (b1 + b4, row_sum))
        sys.exit(1)

    return b1, b2, b3, b4, rows


def host_key():
    """The golden section this host asserts against.

    Deliberately coarse -- the dimension that moves the numbers is "which
    stdlib modules does a build of this OS compile", not the libc or the CPU.
    """
    if os.name == "nt" or sys.platform.startswith("win"):
        return "windows"
    if sys.platform.startswith("linux"):
        return "linux"
    return sys.platform


HEADER = [
    "# B1 fuzzy-fallback baseline -- docs/name-resolution.md §7.2 mechanism 3.",
    "#",
    "# ASSERTED: the B1 and B4 totals, and every per-site B1 / B3* / B4 row,",
    "# PER HOST.",
    "# CONTEXT ONLY (not asserted): the B2/B3 totals in the comments.",
    "#",
    "# Target: examples/09-json-config, --no-incremental, CRYO_CODEGEN_THREADS=1.",
    "# Re-pin DELIBERATELY with `make b1-check ARGS=--update` and commit the",
    "# golden alongside the change that moved it. A decrease is progress and",
    "# still requires a re-pin -- see the module docstring for why.",
    "#",
    "# Each [host:<key>] section is a SEPARATE measurement and inherits nothing",
    "# from any other. `--update` rewrites only the section for the host it ran",
    "# on and leaves the rest byte-for-byte, because it cannot honestly speak",
    "# for a host it did not measure. One row really is host-dependent:",
    "# `lookup_by_leaf calls`, because a Windows build compiles Windows-only",
    "# stdlib modules. The B1 and B4 TOTALS are the same on both.",
    "#",
    "# B1 is fuzzy fallback and its target is ZERO. B4 is instantiation",
    "# identity -- a mangled name minted after the name layer has finished,",
    "# which a `Res` cannot name -- so it is a FLOOR, pinned to catch growth,",
    "# not a debt to pay down. Do not sum them.",
]


def render_section(host, b1, b2, b3, b4, rows):
    lines = ["[host:%s]" % host,
             "# context: B2=%s B3=%s" % (b2, b3),
             "B1_TOTAL %d" % b1,
             "B4_TOTAL %d" % b4,
             ""]
    for label, count in rows:
        lines.append("%-46s %d" % (label, count))
    return lines


def render(host, b1, b2, b3, b4, rows, keep=None):
    """Render the golden: this host's section, plus every other host verbatim.

    `keep` is the ordered {host: [raw lines]} of the sections that were already
    committed, so a re-pin here cannot perturb a host it did not run on.
    """
    lines = list(HEADER)
    emitted = set()
    for other, raw in (keep or {}).items():
        if other == host:
            continue
        lines.append("")
        lines.extend(raw)
        emitted.add(other)
    lines.append("")
    lines.extend(render_section(host, b1, b2, b3, b4, rows))
    return "\n".join(lines) + "\n"


def parse_golden(text):
    """-> ({host: (b1_total, b4_total, rows)}, {host: [raw lines]}).

    The raw blocks are kept so `--update` can round-trip the hosts it did not
    measure without reformatting them.
    """
    parsed = {}
    raw = {}
    host = None
    for line in text.splitlines():
        line = line.rstrip()
        stripped = line.strip()
        if stripped.startswith("[host:") and stripped.endswith("]"):
            host = stripped[len("[host:"):-1].strip()
            parsed[host] = [None, None, []]
            raw[host] = [line]
            continue
        if host is None:
            continue
        raw[host].append(line)
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("B1_TOTAL"):
            parsed[host][0] = int(stripped.split()[1])
            continue
        if stripped.startswith("B4_TOTAL"):
            parsed[host][1] = int(stripped.split()[1])
            continue
        # `<label padded to 46> <count>`.  Same reasoning as split_row: the
        # count is the last whitespace-separated token, and the label keeps
        # its internal double spaces.
        parts = stripped.rsplit(None, 1)
        if len(parts) == 2 and parts[1].isdigit():
            parsed[host][2].append((parts[0].strip(), int(parts[1])))
    # Trailing blank lines inside a block would be re-emitted on every update.
    for h in raw:
        while raw[h] and not raw[h][-1].strip():
            raw[h].pop()
    return ({h: (t1, t4, r) for h, (t1, t4, r) in parsed.items()}, raw)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("cryo")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--verify-determinism", action="store_true")
    args = ap.parse_args()

    if args.verify_determinism:
        a = measure(args.cryo)
        b = measure(args.cryo)
        if a == b:
            print("b1-gate: determinism OK -- two runs agree (B1=%d, %d rows)"
                  % (a[0], len(a[4])))
            return 0
        sys.stderr.write(
            "b1-gate: NOT DETERMINISTIC -- two runs of the same target disagree.\n"
            "         run 1: B1=%d  run 2: B1=%d\n"
            "         Do not wire this as a gate until the source of the drift is\n"
            "         understood; a flaky gate is worse than no gate.\n"
            % (a[0], b[0]))
        for (la, ca), (lb, cb) in zip(a[4], b[4]):
            if (la, ca) != (lb, cb):
                sys.stderr.write("           %-40s %d -> %d\n" % (la, ca, cb))
        return 1

    host = host_key()
    existing, raw = ({}, {})
    if os.path.exists(GOLDEN):
        with open(GOLDEN, "r", encoding="utf-8") as f:
            existing, raw = parse_golden(f.read())

    b1, b2, b3, b4, rows = measure(args.cryo)

    if args.update:
        # `newline="\n"` suppresses the translation that would write CRLF on
        # Windows, matching `roster-check.py`.  The golden is committed and
        # diffed, so a re-pin from a Windows host would otherwise rewrite all 34
        # lines and bury the one or two rows that actually moved.
        with open(GOLDEN, "w", encoding="utf-8", newline="\n") as f:
            f.write(render(host, b1, b2, b3, b4, rows, keep=raw))
        others = sorted(h for h in raw if h != host)
        print("b1-gate: golden updated for host %r -- B1 = %d, B4 = %d%s"
              % (host, b1, b4,
                 ("; left untouched: " + ", ".join(others)) if others else ""))
        return 0

    if not existing:
        sys.stderr.write(
            "b1-gate: no golden at %s\n"
            "         Create it deliberately with `make b1-check ARGS=--update`.\n"
            % GOLDEN)
        return 1

    if host not in existing:
        sys.stderr.write(
            "b1-gate: the golden has no [host:%s] section.\n"
            "         Pinned hosts: %s\n"
            "         This host is UNMEASURED, and a gate must not report OK for a\n"
            "         number nobody committed. Measure and pin it deliberately:\n"
            "             make b1-check ARGS=--update\n"
            "         Sections inherit nothing, so this cannot disturb the others.\n"
            % (host, ", ".join(sorted(existing)) or "(none)"))
        return 1

    want_b1, want_b4, want_rows = existing[host]

    if want_b1 is None or want_b4 is None:
        sys.stderr.write("b1-gate: golden section [host:%s] is missing a B1_TOTAL"
                         " or B4_TOTAL line; it is corrupt, or predates the\n"
                         "         B4 split. Re-pin it deliberately.\n" % host)
        return 1

    if want_b1 == b1 and want_b4 == b4 and want_rows == rows:
        print("b1-gate: OK -- B1 = %d, B4 = %d, %d sites, matches golden"
              " [host:%s]" % (b1, b4, len(rows), host))
        return 0

    sys.stderr.write("b1-gate: DRIFT  (host %s)\n\n" % host)
    sys.stderr.write("  B1 (-> zero)   golden %d   measured %d   (%+d)\n"
                     % (want_b1, b1, b1 - want_b1))
    sys.stderr.write("  B4 (floor)     golden %d   measured %d   (%+d)\n\n"
                     % (want_b4, b4, b4 - want_b4))
    want_map = dict(want_rows)
    got_map = dict(rows)
    for label in sorted(set(want_map) | set(got_map)):
        w = want_map.get(label)
        g = got_map.get(label)
        if w == g:
            continue
        if w is None:
            sys.stderr.write("  NEW SITE   %-40s %d\n" % (label, g))
        elif g is None:
            sys.stderr.write("  GONE       %-40s was %d\n" % (label, w))
        else:
            sys.stderr.write("  %-10s %-40s %d -> %d (%+d)\n"
                             % ("CHANGED", label, w, g, g - w))
    if b1 > want_b1 or b4 > want_b4:
        sys.stderr.write(
            "\n  A bucket went UP: a fallback regrew. This is the regression\n"
            "  §7.2 mechanism 3 exists to catch. Fix the cause, do not re-pin.\n")
    else:
        sys.stderr.write(
            "\n  A bucket went DOWN: that is progress, and the new value must be\n"
            "  pinned so it becomes the bound. Re-pin with:\n"
            "      make b1-check ARGS=--update\n"
            "  and commit tests/b1-baseline.txt with the change that moved it.\n"
            "  Name the mechanism first: a row can also fall because it stopped\n"
            "  being recorded, and only this host's section is re-pinned.\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
