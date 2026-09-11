#!/usr/bin/env python3
"""Remove named `Site` variants from the resolution counter, mechanically.

`resolve_counter.cryo` names each site in four places -- the `Site` enum, the
`bucket()` match, the `label()` match, and one `print_row()` call in the report
-- and `bucket()`/`label()` are exhaustive, so a variant removed from one and
left in another does not compile.  Doing the four by hand is how a batch ends
up half-applied; this does all four or refuses.

A site's `bump()` call sites live in the passes and are NOT edited here.  They
carry surrounding control flow, and `--audit` exists because deleting one is
not always a null edit:

    if (match_count == 0) {                 if (match_count == 1) {
        SfScanNone.bump();                      SfScanOne.bump();
    } else if (match_count == 1) {  ==>     } else {
        SfScanOne.bump();                       SfScanPlural.bump();
    } else {                                }
        SfScanPlural.bump();
    }

Deleting the first arm did not drop a counter, it MERGED the zero case into
the `else`, and `SfScanPlural` -- pinned at 0 -- began counting "found
nothing" as "found several".  A `bump()` that is the SOLE statement of a
branch is a branch, not a statement: removing it needs the branch kept as an
empty body, or the condition rewritten so it still excludes the other cases.

THIS CLASSIFIER HAS BEEN WRONG THREE TIMES.  Read that as a warning about
trusting it, not as a claim that it is finished:

  1. It reported the very site above as PLAIN.  `} else if (c) {` closes one
     branch and opens the next on ONE line, so a per-line brace sum never
     returns to zero and the whole chain read as a single block.  Fixed by
     scanning a character stream with string literals and comments masked.
  2. A multi-line condition lost the `if` / `else` that names the branch kind,
     because the header was taken as the line the `{` sits on.  The header now
     runs back to the previous statement boundary.
  3. A one-line arm `_ => { X.bump(); }` was scanned from column 0, walking
     straight past the arm into the enclosing `match`.  The scan now starts at
     the bump's own column.

All three UNDER-reported: they turned a dangerous shape into a safe-looking
one, which is the only direction that matters.  Before believing an audit,
run it over a tree where a known BRANCH site exists and confirm it says
BRANCH -- `git show <rev>:<file>` into a scratch copy is enough.  A classifier
that has not been shown refusing is not evidence.

Usage:
    python3 scripts/retire-counter-sites.py SITE [SITE ...]
    python3 scripts/retire-counter-sites.py --audit [SITE ...]
    python3 scripts/retire-counter-sites.py --check SITE [SITE ...]

    (no flag)  remove the four counter-side lines for each SITE
    --audit    classify each SITE's bump sites by the shape they sit in, so a
               dangerous one is seen BEFORE it is edited.  With no SITE, audits
               every site in the tree.  Exits 1 if any branch-merge shape is
               present, so it can be run as a pre-edit gate.
    --check    prove no reference to SITE survives anywhere under compiler/src
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COUNTER = os.path.join(ROOT, "compiler", "src", "compiler", "resolve_counter.cryo")
SRC = os.path.join(ROOT, "compiler", "src")

# Shapes, most dangerous first.  The verdict is advisory: it says what a reader
# has to look at, never that an edit is safe.
BRANCH_MERGE = "BRANCH -- sole body of an if/else CHAIN; deleting it MERGES cases"
ARM_ONLY = "ARM    -- sole body of a match arm; the arm must stay"
LONE_IF = "IF     -- sole body of a lone `if`, no else; drop the whole `if`"
PLAIN = "PLAIN  -- one statement among others; delete the line"


def cryo_files():
    for root, _, files in os.walk(SRC):
        for f in sorted(files):
            if f.endswith(".cryo"):
                yield os.path.join(root, f)


def mask(text):
    """`text` with string literals and comments blanked, newlines preserved.

    Brace counting has to run over this rather than over the source: a `//`
    inside a string ("https://...") would eat the rest of a line, and a brace
    inside one would unbalance the file.  Blanking rather than deleting keeps
    every offset equal to the original's, so a position found here indexes
    straight back into the real text.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            out[i] = " "
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out)


def enclosing_block(masked, pos):
    """(open, close) offsets of the innermost `{}` containing `pos`.

    Scanned by CHARACTER, not by line.  `} else if (c) {` closes one branch and
    opens the next on a single line, so a per-line depth sum never returns to
    zero there and reports the whole if/else chain as one block -- which is
    exactly the shape this is here to recognise, and the reason the first
    version of this function called the SfScanNone site PLAIN.
    """
    depth = 0
    open_p = None
    for j in range(pos, -1, -1):
        c = masked[j]
        if c == "}":
            depth += 1
        elif c == "{":
            if depth == 0:
                open_p = j
                break
            depth -= 1
    if open_p is None:
        return None, None
    depth = 0
    for j in range(open_p, len(masked)):
        c = masked[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return open_p, j
    return open_p, None


def head_of(masked, text, open_p):
    """The branch header: everything from the end of the previous statement.

    NOT just the line the `{` sits on.  A condition may be spelled across
    several lines, and taking only the last of them loses the `if` and the
    `else` that say what kind of branch this is -- which made an `else if`
    with a wrapped condition read as an ordinary statement.
    """
    j = open_p - 1
    depth = 0
    while j >= 0:
        c = masked[j]
        if c in ")]":
            depth += 1
        elif c in "([":
            depth -= 1
        elif depth <= 0 and c in ";{}":
            break
        j -= 1
    return text[j + 1:open_p]


def classify(lines, i, col=None):
    """Shape of the branch holding the bump on line `i` (0-based).

    `col` is the bump's column.  It is load-bearing: a one-line match arm
    `_ => { X.bump(); }` opens and closes its block after the start of the
    line, so a scan begun at column 0 walks straight past it into the
    enclosing `match` and reports the arm as an ordinary statement.
    """
    text = "\n".join(lines)
    masked = mask(text)
    starts = []
    off = 0
    for line in lines:
        starts.append(off)
        off += len(line) + 1
    if col is None:
        found = re.search(r"\bSite::\w+\s*\.bump\(\)", lines[i])
        col = found.start() if found else 0
    pos = starts[i] + col
    open_p, close_p = enclosing_block(masked, pos)
    if open_p is None:
        return PLAIN, None
    open_i = masked.count("\n", 0, open_p)
    body = [s.strip() for s in
            re.split(r"\n", text[open_p + 1:close_p if close_p else len(text)])]
    body = [s for s in body
            if s and not s.startswith("//") and s not in ("{", "}")]
    if len(body) != 1:
        return PLAIN, open_i
    head = head_of(masked, text, open_p)
    if "=>" in head:
        return ARM_ONLY, open_i
    if re.search(r"\bif\s*\(", head) or "else" in head:
        # An `else` in the header, or an `else` after the close, means this
        # branch is one case of a chain: its condition is stated only by what
        # the other cases already excluded, so removing it hands its
        # population to a sibling.
        after = text[close_p + 1:close_p + 40] if close_p else ""
        chained = ("else" in head) or re.match(r"\s*else\b", after) is not None
        return (BRANCH_MERGE if chained else LONE_IF), open_i
    return PLAIN, open_i


def audit(sites):
    pat = re.compile(r"\bSite::(\w+)\s*\.bump\(\)")
    want = set(sites) if sites else None
    rows = []
    for p in cryo_files():
        if p == COUNTER:
            continue
        lines = open(p, encoding="utf-8").read().splitlines()
        for i, line in enumerate(lines):
            m = pat.search(line)
            if not m or (want and m.group(1) not in want):
                continue
            verdict, open_i = classify(lines, i)
            rows.append((verdict, os.path.relpath(p, ROOT).replace(os.sep, "/"),
                         i + 1, m.group(1),
                         lines[open_i].strip() if open_i is not None else ""))
    for verdict in (BRANCH_MERGE, ARM_ONLY, LONE_IF, PLAIN):
        hits = [r for r in rows if r[0] == verdict]
        if not hits:
            continue
        print("== %s  (%d)" % (verdict, len(hits)))
        for _, path, n, site, head in hits:
            print("   %-28s %s:%d" % (site, path, n))
            if verdict != PLAIN:
                print("       enclosing: %s" % head)
        print("")
    # A site reached only through a `Site`-typed parameter (a `door`, an
    # `enter`) never appears as `Site::X.bump()`, so it cannot be classified
    # from the call.  Named, so its absence is not read as safety.
    if want:
        seen = set(r[3] for r in rows)
        for s in sorted(want - seen):
            print("== NOT A DIRECT BUMP: %s" % s)
            print("   passed as a `Site` value, or already gone -- locate it "
                  "with --check before editing")
    return 1 if any(r[0] == BRANCH_MERGE for r in rows) else 0


def remaining(site):
    hits = []
    pat = re.compile(r"\bSite::%s\b" % re.escape(site))
    for p in cryo_files():
        for n, line in enumerate(open(p, encoding="utf-8"), 1):
            if pat.search(line):
                hits.append((os.path.relpath(p, ROOT).replace(os.sep, "/"),
                             n, line.rstrip()))
    return hits


def remove(sites):
    lines = open(COUNTER, encoding="utf-8", newline="").read().splitlines(True)
    for s in sites:
        pats = [re.compile(r"^\s*%s;\s*$" % re.escape(s)),
                re.compile(r"^\s*Site::%s\s+=>\s*\{.*\}\s*$" % re.escape(s)),
                re.compile(r"^\s*Site::%s\.print_row\(\);\s*$" % re.escape(s))]
        kept, hits = [], 0
        for line in lines:
            if any(p.match(line) for p in pats):
                hits += 1
                continue
            kept.append(line)
        # One variant, two match arms, one report row.  Anything else means the
        # file does not have the shape this assumes, and a partial removal
        # would not compile -- so refuse rather than write it.
        if hits != 4:
            sys.stderr.write("refusing: %s matched %d lines, expected 4\n"
                             % (s, hits))
            return 1
        lines = kept
    open(COUNTER, "w", encoding="utf-8", newline="").write("".join(lines))
    print("removed %d site(s) from %s"
          % (len(sites), os.path.relpath(COUNTER, ROOT)))
    return 0


def main():
    args = sys.argv[1:]
    sites = [a for a in args if not a.startswith("--")]
    if "--audit" in args:
        return audit(sites)
    if not sites:
        sys.stderr.write(__doc__)
        return 2
    if "--check" in args:
        bad = 0
        for s in sites:
            for path, n, line in remaining(s):
                print("%s:%d: %s" % (path, n, line))
                bad += 1
        print("%d reference(s) left for %d site(s)" % (bad, len(sites)))
        return 1 if bad else 0
    return remove(sites)


if __name__ == "__main__":
    sys.exit(main())
