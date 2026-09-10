#!/usr/bin/env python3
"""Refuse a commit that records a change in §8 without moving §0 with it.

WHY THIS IS NOT A CI JOB
------------------------
CI fires on `main` and on a manual dispatch only, deliberately - the commit
rate on a migration branch makes per-commit Actions minutes a real cost.  So on
the branch where this work actually happens, a CI check is not enforcement; it
is a message delivered at merge time about a hundred commits at once.  This
runs on the machine, at the moment the record is being written, which is the
only moment the person writing it still has the reason in their head.

It is a `commit-msg` hook rather than `pre-commit` because the escape hatch
belongs in the message: a bypass that leaves no trace is a bypass nobody can
audit, and `--no-verify` leaves none.

WHAT IT ENFORCES
----------------
1. `docs/name-resolution.md` never lands alone.  Already the rule in
   CLAUDE.md, unenforced until now: a ledger entry that lands apart from its
   change turns a behaviour change into two half-records - the commit says what
   moved without saying why, and the entry claims a measurement with no diff to
   check it against.

2. A §8 entry whose heading says LANDED, FIXED or RULED must move §0 in the
   same commit.  The mechanical row checks catch a number that drifted; they
   cannot catch a decision that never got a row.  §8.39's second decision was
   taken and then neither built nor withdrawn across ninety-five entries, and
   no count anywhere would have said so.  This is the half that would have.

3. §0's expected values may not change unless something outside `docs/` changes
   too.  §0 is a golden like any other and can be made green by re-pinning it;
   the protection is that a number moves in the same commit as the change that
   moved it, in a diff a reader can check it against.  A §0 re-pinned on its
   own has become the archive it was built to replace.

When §0 does change, `ns-status-check` runs, so a row cannot be committed
already disagreeing with the tree.

THE ESCAPE HATCH
----------------
A commit-message line

    no-section-0: <reason>

waives rule 2 for that commit.  It is deliberately a line in the permanent
record rather than a flag: `git log --grep='no-section-0'` is the audit, and a
reason that will not survive being read is a reason not to waive.

Usage (as a hook, installed by `make install-hooks`):
    python scripts/ns-commit-guard.py --message-file <path>

Exit codes: 0 allowed; 1 refused.
"""
import argparse
import os
import re
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER_REL = "docs/name-resolution.md"
SECTION_START = "\n## 0. Current state"

# `### 8.134 ... - MEASURED AND FIXED 2026-09-10`.  The status words are the
# ones the archive actually uses for a change that HAPPENED; DEFERRED,
# PREPARED and a bare handoff are not among them on purpose - they record
# something that did not land, and there is nothing for §0 to say yet.
ENTRY = re.compile(r"^\+###\s+8\.\S+\s+.*\b(LANDED|FIXED|RULED)\b", re.M)
WAIVER = re.compile(r"^\s*no-section-0:\s*(\S.*)$", re.M | re.I)
ROW = re.compile(r"`([^`]+)`\s*→\s*\*\*([^*]+)\*\*")


def git(*args):
    r = subprocess.run(["git"] + list(args), cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def section_of(text):
    i = text.find(SECTION_START)
    if i < 0:
        return ""
    j = text.find("\n## ", i + len(SECTION_START))
    return text[i:j if j >= 0 else len(text)]


def blob(rev, path):
    """A file's content at `rev`, or '' if it is not there.

    `rev` is empty for the INDEX, because git spells that `:path` - the
    separating colon is the whole revision.  Passing ":" here instead built
    "::path", which git rejects, so the staged side read as empty, every §0
    comparison saw a section that had appeared from nothing, and the LANDED
    rule never fired.  The self-test is what found that; the rule looked
    correct in the source.
    """
    code, out = git("show", "%s:%s" % (rev, path))
    return out if code == 0 else ""


def refuse(title, *lines):
    print()
    print("commit refused: %s" % title)
    for ln in lines:
        print("  %s" % ln)
    print()
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--message-file", required=True)
    args = ap.parse_args()

    code, staged = git("diff", "--cached", "--name-only")
    if code != 0:
        return 0                      # not a state this can judge; stay out
    files = [f.strip().replace("\\", "/") for f in staged.splitlines() if f.strip()]
    if not files:
        return 0

    try:
        with open(args.message_file, "r", encoding="utf-8", errors="replace") as fh:
            message = fh.read()
    except OSError:
        message = ""

    ledger_staged = LEDGER_REL in files
    non_docs = [f for f in files if not f.startswith("docs/")]

    # --- 1. the ledger never lands alone ---------------------------------
    if ledger_staged and len(files) == 1:
        return refuse(
            "%s is the only file in it." % LEDGER_REL,
            "A ledger entry is the record of a change, not a deliverable.  On",
            "its own the commit says what moved without saying why, and the",
            "entry claims a measurement with no diff to check it against.",
            "",
            "If the entry documents a change already committed, amend that",
            "commit or fold the entry into the next one touching the same area.",
            "\"It did not fit anywhere\" means the commit boundary was drawn in",
            "the wrong place.")

    have_head = git("rev-parse", "--verify", "HEAD")[0] == 0
    old_ledger = blob("HEAD", LEDGER_REL) if have_head else ""
    new_ledger = blob("", LEDGER_REL) if ledger_staged else old_ledger
    old_sec, new_sec = section_of(old_ledger), section_of(new_ledger)
    section_moved = old_sec != new_sec

    # --- 2. a LANDED / FIXED / RULED entry must move §0 -------------------
    code, diff = git("diff", "--cached", "-U0", "--", LEDGER_REL)
    landed = ENTRY.findall(diff) if code == 0 else []
    if landed and not section_moved:
        waiver = WAIVER.search(message)
        if not waiver:
            return refuse(
                "a §8 entry says %s and §0 did not move."
                % "/".join(sorted(set(landed))),
                "§0 is what an incoming agent reads INSTEAD of re-deriving the",
                "truth from the archive.  An entry that records something as",
                "landed, fixed or ruled has changed the status of a decision, a",
                "lane or a gate - and the row checks cannot catch a decision",
                "that never got a row.  That is how §8.39's second decision was",
                "taken and then neither built nor withdrawn for ninety-five",
                "entries.",
                "",
                "Update the row §0 already has, or add the row it is missing.",
                "",
                "If this entry genuinely changes nothing in §0, say so in the",
                "message and the commit goes through:",
                "",
                "    no-section-0: <why this one changes no row>",
                "",
                "That line is the audit trail; `git log --grep=no-section-0`",
                "reads it back.")
        print("ns-commit-guard: §0 waived -- %s" % waiver.group(1).strip())

    # --- 3. §0's numbers may not be re-pinned on their own ----------------
    if section_moved and not non_docs:
        old_rows = dict((c, e) for c, e in ROW.findall(old_sec))
        new_rows = dict((c, e) for c, e in ROW.findall(new_sec))
        repinned = sorted(c for c in set(old_rows) & set(new_rows)
                          if old_rows[c] != new_rows[c])
        if repinned:
            return refuse(
                "§0 expected values changed and nothing outside docs/ did.",
                "Re-pinned row(s):",
                *(["    %s: %s -> %s" % (c, old_rows[c], new_rows[c])
                   for c in repinned]
                  + ["",
                     "A §0 number is a duplicate of a tree fact, so it moves",
                     "when the tree moves.  Changed on its own it is a golden",
                     "edited to match, which is how the document this section",
                     "replaced became something nobody could trust.  Commit the",
                     "new number with the change that produced it and put the",
                     "reason in the message."]))

    # --- 4. a moved §0 must still agree with the tree ---------------------
    if section_moved:
        r = subprocess.run([sys.executable,
                            os.path.join(ROOT, "scripts", "ns-status-check.py")],
                           cwd=ROOT)
        if r.returncode != 0:
            return refuse(
                "§0 changed and its rows no longer match the tree.",
                "The drift is printed above.  A row committed already",
                "disagreeing with the tree is the failure mode this section",
                "exists to prevent, arriving in the section itself.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
