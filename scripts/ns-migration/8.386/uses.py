"""Every use of a retyped parameter inside its own function body.

A `Text` handed to a generic sink (`String::push<T>`, an array's `push`, a
C variadic like `printf`) is not refused where it is written: an array
method's argument is not checked before code generation, and a variadic
takes anything.  So after a retype, each use of the parameter is listed for
reading, with the uses the compiler does check (an argument it would have
refused, an `.as_string()`) filtered out.

  python uses.py <list.tsv> <repo-root>
"""
import re, sys

def body(lines, ln):
    """Lines of the function whose head is on 1-based line `ln`."""
    i = ln - 1
    depth = 0
    started = False
    out = []
    while i < len(lines):
        l = lines[i]
        out.append((i + 1, l))
        # braces inside a string or char literal, or a comment, do not count:
        # a body holding `"{"` otherwise ends early and its uses go unlisted
        code = re.sub(r'"(?:\\.|[^"\\])*"', '""', l)
        code = re.sub(r"'(?:\\.|[^'\\])'", "''", code)
        code = code.split("//")[0]
        for ch in code:
            if ch == "{":
                depth += 1
                started = True
            elif ch == "}":
                depth -= 1
        if started and depth <= 0:
            break
        i += 1
    return out

rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
root = sys.argv[2]
cache = {}
for r in rows:
    f, ln = r[0].rsplit(":", 1)
    pn = r[2]
    if f not in cache:
        cache[f] = open(root + "/" + f, encoding="utf-8", errors="replace").read().split("\n")
    b = body(cache[f], int(ln))
    head = " ".join(l for _, l in b[:14]).split("{")[0]
    if re.search(r"\b%s_text\s*:\s*Text\b" % re.escape(pn), head):
        continue  # unwrapped at entry: the body's `pn` is the string
    pat =re.compile(r"(?<![\w.])%s\b(?!\s*:)(?!\.as_string\(\))" % re.escape(pn))
    # the body starts after the first `{` - on the head's own line for a
    # one-line body, whose uses a scan from the next line would never see
    opened = False
    for n, l in b:
        if not opened:
            if "{" not in l:
                continue
            opened = True
            l = l[l.index("{") + 1:]
        if pat.search(l):
            print("%s:%d\t%s\t%s" % (f, n, pn, l.strip()))
