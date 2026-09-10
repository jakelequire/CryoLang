#!/usr/bin/env python3
"""Find §0 and the §8 archive, wherever they live.

The name-resolution ledger is being split: §0 and the spec stay in one file,
the §8 archive moves out, and it may become one file or several.  Both checks
built on it - `ns-status-check` and the commit-msg guard - used to name
`docs/name-resolution.md` directly, so the split would have left the guard's
central rule looking installed and doing nothing.  That is the exact failure
this suite paid to learn: a guard that stops guarding is silent.

So neither one names a file any more.  They ask this.

HOW EACH IS FOUND
-----------------
§0 is found by its heading, `## 0. Current state`, across `docs/**/*.md`.  It
must appear in exactly one file.  Two would be the defect §0 exists to remove -
its own maintenance rule says the section is REPLACED, never appended to - so
two is a refusal rather than a choice between them.

The archive is found by DECLARATION.  The §0 document names its own archive:

    <!-- ns-archive: docs/name-resolution-archive.md -->

repeatable, and the value is a glob relative to the repo root, so
`docs/name-resolution/*.md` covers a split into several files without anyone
touching this code again.

Where nothing is declared, the archive is the §0 document itself - which is
true today, and true only while that document still carries entry headings.
A §0 document that declares no archive AND holds no entries is REFUSED, with
the directive to add: that state is precisely the one where the archive has
moved somewhere these checks cannot see, and the alternative is for them to
quietly pass over an empty set.

Content alone would not have done it.  `docs/cryo.md` has seven `### 8.x`
headings of its own and `docs/cryo-mangling-spec.md` two; discovering the
archive by that shape would have swept both in.  The status words the guard
matches on happen to exclude them today, which is luck rather than a rule, and
luck is the thing being removed here.
"""
import glob
import io
import os
import re

DOCS = "docs"
SECTION_START = "\n## 0. Current state"
SECTION_HEADING_RE = re.compile(r"^## 0\. Current state\s*$", re.M)

# `<!-- ns-archive: docs/name-resolution/*.md -->`
ARCHIVE_DECL_RE = re.compile(r"<!--\s*ns-archive:\s*(\S+?)\s*-->")

# An archive ENTRY that records something as having happened.  DEFERRED,
# PREPARED and a bare handoff are deliberately not here: they record something
# that did not land, and there is nothing for §0 to say about it yet.
ENTRY_RE = re.compile(r"^\+###\s+8\.\S+\s+.*\b(LANDED|FIXED|RULED)\b", re.M)
# Any entry heading at all - used only to ask whether a file still holds the
# archive, never to decide what a commit owes §0.
ENTRY_ANY_RE = re.compile(r"^###\s+8\.\S+\s", re.M)

# A §0 row's check: a backticked command, an arrow, a bold expected value.
ROW_RE = re.compile(r"`([^`]+)`\s*→\s*\*\*([^*]+)\*\*")


def _read(path):
    try:
        return io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def section_of(text):
    """§0's text: its heading through the start of the next `## ` heading."""
    i = text.find(SECTION_START)
    if i < 0:
        return ""
    j = text.find("\n## ", i + len(SECTION_START))
    return text[i:j if j >= 0 else len(text)]


def rows(section_text):
    """(command, expected) per checkable row, with markdown escaping undone.

    A table cell escapes a pipe as `\\|` so it does not end the column, and the
    command is meant to run with a real pipe.
    """
    return [(c.replace("\\|", "|").strip(), e.strip())
            for c, e in ROW_RE.findall(section_text)]


def find(root):
    """(section0_path, [archive_paths], problem).

    Paths are relative to `root`, with forward slashes, so they compare
    directly against `git diff --cached --name-only`.  `problem` is a string
    when the layout cannot be read; both other values are then meaningless.
    """
    candidates = []
    for path in sorted(glob.glob(os.path.join(root, DOCS, "**", "*.md"),
                                 recursive=True)):
        if SECTION_HEADING_RE.search(_read(path)):
            candidates.append(path)

    if not candidates:
        return None, [], (
            "no `## 0. Current state` heading anywhere under %s/. Either the "
            "section was removed or it was renamed; these checks cannot run "
            "over a section they cannot find, and passing over an empty set is "
            "the failure they exist to prevent." % DOCS)
    if len(candidates) > 1:
        return None, [], (
            "`## 0. Current state` appears in %d files: %s. A second "
            "current-state section is the defect §0 exists to remove, so this "
            "refuses rather than picking one." % (
                len(candidates),
                ", ".join(os.path.relpath(c, root).replace(os.sep, "/")
                          for c in candidates)))

    s0 = candidates[0]
    text = _read(s0)
    rel = lambda p: os.path.relpath(p, root).replace(os.sep, "/")

    declared = ARCHIVE_DECL_RE.findall(text)
    if declared:
        archives = []
        for pattern in declared:
            hits = sorted(glob.glob(os.path.join(root, pattern),
                                    recursive=True))
            if not hits:
                return None, [], (
                    "§0 declares an archive at `%s` and nothing matches it. A "
                    "declaration pointing at nothing is worse than none: the "
                    "rules would run over an empty archive and pass." % pattern)
            archives.extend(hits)
        return rel(s0), sorted(set(rel(a) for a in archives)), None

    if ENTRY_ANY_RE.search(text):
        # Undeclared, and the entries are still here - which is the layout
        # before the split, and correct while it lasts.
        return rel(s0), [rel(s0)], None

    return None, [], (
        "%s carries §0 but no §8 entries, and declares no archive. The archive "
        "has moved somewhere these checks cannot see. Add a line to it:\n"
        "      <!-- ns-archive: docs/<wherever-it-went>.md -->\n"
        "    The value is a glob from the repo root, and the directive repeats, "
        "so one file or several is not this code's business." % rel(s0))
