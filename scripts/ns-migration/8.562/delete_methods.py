"""Delete named methods - with their doc comments and attributes - from a file.

Usage: python scripts/ns-migration/8.562/delete_methods.py FILE NAME[@LINE] ... [--apply]

A method is a line at four-space indentation beginning `NAME(` (optionally
after `static `), and runs to the first later line that is exactly `    }`.
The `///` doc lines and `![...]` attribute lines directly above it go with
it, as does one blank line after it.  `NAME@LINE` picks the declaration on
that line (1-based) when the name is declared more than once; a bare name
declared more than once is refused.  Without --apply it prints what it
would delete and writes nothing.
"""
import re
import sys


def main():
    args = [a for a in sys.argv[1:] if a != "--apply"]
    apply = "--apply" in sys.argv
    path, names = args[0], args[1:]
    with open(path, encoding="utf-8", newline="") as fh:
        lines = fh.read().split("\n")
    spans = []
    for spec in names:
        name, _, at = spec.partition("@")
        decl = re.compile(r"    (?:static )?" + re.escape(name) + r"\(")
        hits = [i for i, l in enumerate(lines) if decl.match(l)]
        if at:
            hits = [i for i in hits if i + 1 == int(at)]
        if len(hits) != 1:
            sys.exit("%s: %d declarations match" % (spec, len(hits)))
        start = end = hits[0]
        while lines[end].rstrip("\r") != "    }":
            end += 1
        while start > 0 and (lines[start - 1].lstrip().startswith("///")
                             or lines[start - 1].lstrip().startswith("![")):
            start -= 1
        if end + 1 < len(lines) and lines[end + 1].strip() == "":
            end += 1
        spans.append((start, end, spec))
    spans.sort()
    for a, b in zip(spans, spans[1:]):
        if a[1] >= b[0]:
            sys.exit("overlapping: %s %s" % (a[2], b[2]))
    for s, e, spec in spans:
        print("%s: lines %d-%d (%d)" % (spec, s + 1, e + 1, e - s + 1))
    if apply:
        for s, e, _ in reversed(spans):
            del lines[s:e + 1]
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write("\n".join(lines))
        print("written")


if __name__ == "__main__":
    main()
