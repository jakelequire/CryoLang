#!/usr/bin/env python3
"""D31's rewrite: every generic-argument list the OLD parser opened in
expression position by lookahead gets the turbofish, `Name<T>` -> `Name::<T>`,
at the location the probe printed.  The population is the input, not a
re-derivation: 567 of the 955 identifier-headed sites were decided by the
token scan alone, so nothing but the parser's own report says where they are.

    python scripts/ns-migration/8.278/turbofish.py apply  scripts/ns-migration/8.277/sites.tsv [more site lists]
    python scripts/ns-migration/8.278/turbofish.py check  scripts/ns-migration/8.277/sites.tsv [more site lists]

`sites.tsv` is `probe_d31.py sites` over the probe's lines files:
`pop\\tfile\\tline\\tcol\\tshape\\tname\\tguess`, the (1-based) line and column of
the NAME token whose `<` the lookahead opened.  Every site is rewritten the same
way: the `<` that follows the name (on the same line, across spaces) becomes
`::<` and the spaces before it go.  A site already spelled `name::<` is left
alone, so a second run over the same input edits nothing - that is the audit -
and a site that reads neither way refuses the whole run before any file is
written.

`check` verifies every site reads `name::<` and writes nothing.

Files are read and written as bytes: the tree holds CRLF files and a lone
`\\r` inside a line, both of which a text-mode read would count differently
from the lexer.  Only `\\n` ends a line, as the lexer has it.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def load_sites(paths):
    by_file = {}
    for path in paths:
        with open(path, "r", encoding="utf-8") as f:
            for ln in f:
                ln = ln.rstrip("\n")
                if not ln or ln.startswith("#"):
                    continue
                pop, file, line, col, shape, name, guess = ln.split("\t")
                file = os.path.normpath(file).replace("\\", "/")
                by_file.setdefault(file, []).append((int(line), int(col), pop, shape, name))
    return by_file


def site_state(lines, line, col, name, shift):
    """'old' if the site reads `name<`, 'new' if `name::<`, else why not.
    `shift` is two bytes per site already rewritten to its left on the same
    line, which is where a column from the probe's run lands afterwards."""
    if line < 1 or line > len(lines):
        return "line out of range"
    text = lines[line - 1]
    i = col - 1 + shift
    if not text.startswith(name, i):
        return "name mismatch: " + repr(text[i:i + len(name) + 8])
    j = i + len(name)
    k = j
    while k < len(text) and text[k:k + 1] == b" ":
        k += 1
    if text.startswith(b"::<", k):
        return "new"
    if text.startswith(b"<", k):
        return "old"
    return "follower: " + repr(text[j:j + 8])


def rewrite_line(text, col, name):
    i = col - 1
    j = i + len(name)
    k = j
    while text[k:k + 1] == b" ":
        k += 1
    return text[:j] + b"::" + text[k:], k - j


def run(mode, sites_paths):
    by_file = load_sites(sites_paths)
    files = {}
    refused = []
    pending = {}
    already = 0
    # First every site is read and classified; nothing is written while a
    # single site reads neither spelling.
    for file in sorted(by_file):
        path = os.path.join(ROOT, file)
        with open(path, "rb") as f:
            files[file] = f.read().split(b"\n")
        shift_line = 0
        shift = 0
        for line, col, pop, shape, name in sorted(by_file[file]):
            if line != shift_line:
                shift_line = line
                shift = 0
            state = site_state(files[file], line, col, name.encode("utf-8"), shift)
            if state == "new":
                already += 1
                shift += 2
            elif state == "old":
                if mode == "check":
                    refused.append((file, line, col, name, "still the old spelling"))
                else:
                    pending.setdefault(file, []).append((line, col + shift, pop, name))
            else:
                refused.append((file, line, col, name, state))
    if refused:
        print("REFUSED %d site(s); nothing written" % len(refused))
        for file, line, col, name, why in refused:
            print("  %s:%d:%d  %s  %s" % (file, line, col, name, why))
        sys.exit(1)
    edits = 0
    spaces = 0
    per_pop = {}
    for file in sorted(pending):
        lines = files[file]
        # Highest column first on a line, so an insertion never shifts a
        # site still to be rewritten on the same line.
        for line, col, pop, name in sorted(pending[file], reverse=True):
            lines[line - 1], dropped = rewrite_line(lines[line - 1], col, name.encode("utf-8"))
            spaces += 1 if dropped else 0
            edits += 1
            per_pop[pop] = per_pop.get(pop, 0) + 1
        with open(os.path.join(ROOT, file), "wb") as f:
            f.write(b"\n".join(lines))
    total = sum(len(v) for v in by_file.values())
    print("%s: %d sites in %d files; %d rewritten (%d had spaces before `<`), %d already `::<`" % (
        mode, total, len(by_file), edits, spaces, already))
    for pop in sorted(per_pop):
        print("  %-16s %6d" % (pop, per_pop[pop]))


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("apply", "check"):
        print(__doc__)
        sys.exit(2)
    run(sys.argv[1], sys.argv[2:])
