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

NO FILE IS NAMED HERE
---------------------
The ledger is being split: §0 and the spec stay put, the §8 archive moves out
and may become one file or several.  A hardcoded path would have left rule 2
reading a document with no entries in it - installed, silent, doing nothing,
which is the failure this suite has already paid to learn once.  §0 is found
by its heading and the archive by the declaration §0 carries; see
scripts/ns_ledger.py.  The self-test drives the split layout, so the move is
covered before it happens.

WHAT IT ENFORCES
----------------
1. No ledger file ever lands alone - the §0 document or any archive file.
   Already the rule in CLAUDE.md, unenforced until now: a ledger entry that
   lands apart from its change turns a behaviour change into two
   half-records - the commit says what moved without saying why, and the
   entry claims a measurement with no diff to check it against.

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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ns_ledger                                              # noqa: E402

# No file is named here.  The ledger is being split - §0 and the spec stay
# where they are, the §8 archive moves out and may become several files - and a
# hardcoded path would have left the LANDED rule reading a file with no entries
# in it: installed, silent, and doing nothing.  scripts/ns_ledger.py finds §0
# by its heading and the archive by the declaration §0 carries.
WAIVER = re.compile(r"^\s*no-section-0:\s*(\S.*)$", re.M | re.I)


def git(*args):
    r = subprocess.run(["git"] + list(args), cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode, r.stdout.decode("utf-8", "replace")


section_of = ns_ledger.section_of


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

    section0, archives, problem = ns_ledger.find(ROOT)
    if problem:
        # The layout cannot be read, so rules 2-4 cannot run.  Ordinary commits
        # still go through - blocking every commit over a documents problem is
        # how a hook gets uninstalled - but they are TOLD, and any commit that
        # touches the documents is refused until the layout is legible again.
        # Discovery reads the working tree, so the commit that fixes the layout
        # fixes discovery with it and there is nothing to deadlock on.
        print("ns-commit-guard: CANNOT SEE THE LEDGER -- %s" % problem)
        if any(f.startswith("docs/") and f.endswith(".md") for f in files):
            return refuse(
                "the ledger layout cannot be read, and this commit edits it.",
                "Rules 2-4 are blind until §0 and its archive can be found, and",
                "a commit that touches the documents while they are unreadable",
                "is the one that could exploit that.  The message above says",
                "what is missing.")
        print("  (rules 2-4 did NOT run for this commit)")
        return 0

    ledger_files = set([section0]) | set(archives)
    staged_ledger = [f for f in files if f in ledger_files]
    non_docs = [f for f in files if not f.startswith("docs/")]

    # --- 1. no ledger file ever lands alone ------------------------------
    # The whole set, not one path: after the split an archive-only commit is
    # the same defect the ledger-only commit was, and the rule should not have
    # to be rewritten to say so.
    if staged_ledger and len(files) == len(staged_ledger):
        return refuse(
            "it contains nothing but ledger file(s): %s." % ", ".join(staged_ledger),
            "A ledger entry is the record of a change, not a deliverable.  On",
            "its own the commit says what moved without saying why, and the",
            "entry claims a measurement with no diff to check it against.",
            "",
            "If the entry documents a change already committed, amend that",
            "commit or fold the entry into the next one touching the same area.",
            "\"It did not fit anywhere\" means the commit boundary was drawn in",
            "the wrong place.")

    have_head = git("rev-parse", "--verify", "HEAD")[0] == 0
    old_ledger = blob("HEAD", section0) if have_head else ""
    new_ledger = blob("", section0) if section0 in files else old_ledger
    old_sec, new_sec = section_of(old_ledger), section_of(new_ledger)
    section_moved = old_sec != new_sec

    # --- 2. a LANDED / FIXED / RULED entry must move §0 -------------------
    # Over every archive file, which is why the set is discovered rather than
    # named: §0 and the entries need not share a file, and after the split they
    # will not.
    landed = []
    for path in sorted(archives):
        code, diff = git("diff", "--cached", "-U0", "--", path)
        if code == 0:
            landed.extend(ns_ledger.ENTRY_RE.findall(diff))
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
        old_rows = dict(ns_ledger.rows(old_sec))
        new_rows = dict(ns_ledger.rows(new_sec))
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
