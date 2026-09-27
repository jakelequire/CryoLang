"""Rewrite the reads of `TypeRef.id` a seal refuses.

Input: the log of a `cryo check` run over a tree whose `TypeRef.id` is
private (every read outside its module an E0353 "field `id` of
`...TypeRef` is private", the column at the `id` token).  Each refused
read is rewritten where the compiler points:

* two refused reads compared with `==` / `!=` on one line - the same
  question asked of two handles - become `a.equals(b)` / `!a.equals(b)`;
* every other refused read becomes `.key()`: the number, read one way, for
  a pass's own table, a hash, or a debug line.

Nothing else is touched; a construction (`TypeRef::new`) is left for a
hand edit and listed.

Usage: python seal_reads.py <check-log> <root> [--dry]
"""
import os, re, sys

IDENT = re.compile(r"[A-Za-z0-9_]")

def match_back(t, i, close, open_):
    depth = 0
    while i >= 0:
        if t[i] == close:
            depth += 1
        elif t[i] == open_:
            depth -= 1
            if depth == 0:
                return i
        i -= 1
    return -1

def operand_start(t, dot):
    """Start of the postfix expression whose last segment is `.id` at `dot`."""
    i = dot - 1
    while i >= 0:
        ch = t[i]
        if IDENT.match(ch) or ch == ".":
            i -= 1
        elif ch == ":" and i >= 1 and t[i - 1] == ":":
            i -= 2
        elif ch == ")":
            j = match_back(t, i, ")", "(")
            if j < 0:
                return None
            i = j - 1
        elif ch == "]":
            j = match_back(t, i, "]", "[")
            if j < 0:
                return None
            i = j - 1
        elif ch == "*" and i >= 1 and t[i - 1] == "(":
            # `(*first).second` - a deref inside the parens it opened
            i -= 1
        else:
            break
    return i + 1

def balanced(s):
    d = 0
    for ch in s:
        if ch in "([":
            d += 1
        elif ch in ")]":
            d -= 1
            if d < 0:
                return False
    return d == 0

def main(log, root, dry):
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    sites, news = {}, []
    for i, l in enumerate(lines):
        if not l.startswith("error[E0353]"):
            continue
        is_id = "field `id` of `compiler::types::type_ref::TypeRef`" in l
        is_new = "method `new` of `compiler::types::type_ref::TypeRef`" in l
        for j in range(i + 1, min(i + 6, len(lines))):
            s = lines[j].strip()
            if s.startswith("-->"):
                f, ln, col = s[3:].strip().rsplit(":", 2)
                if is_id:
                    sites.setdefault(f, {}).setdefault(int(ln), set()).add(int(col) - 1)
                elif is_new:
                    news.append("%s:%s:%s" % (f, ln, col))
                break
    n_eq = n_key = 0
    left = []
    for f, per_line in sorted(sites.items()):
        p = os.path.join(root, f)
        raw = open(p, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in raw else "\n"
        src = raw.split(nl)
        for ln, cols in per_line.items():
            t = src[ln - 1]
            for c in cols:
                if t[c - 1:c + 2] != ".id":
                    raise SystemExit("no `.id` at %s:%d:%d: %r" % (f, ln, c + 1, t))
            edits = []          # (start, end, text)
            used = set()
            for c in sorted(cols):
                if c in used:
                    continue
                after = t[c + 2:]
                m = re.match(r"\s*(==|!=)\s*", after)
                if not m:
                    continue
                rstart = c + 2 + m.end()
                partner = None
                for c2 in sorted(cols):
                    if c2 <= rstart or c2 in used:
                        continue
                    rhs = t[rstart:c2 - 1]
                    if (balanced(rhs) and not re.search(r"\s(&&|\|\||==|!=|<|>)\s", rhs)
                            and operand_start(t, c2 - 1) == rstart):
                        partner = c2
                    break
                if partner is None:
                    continue
                lstart = operand_start(t, c - 1)
                if lstart is None:
                    continue
                lhs = t[lstart:c - 1]
                rhs = t[rstart:partner - 1]
                text = "%s.equals(%s)" % (lhs, rhs)
                if m.group(1) == "!=":
                    text = "!" + text
                edits.append((lstart, partner + 2, text))
                used.add(c); used.add(partner)
                n_eq += 1
            for c in cols:
                if c in used:
                    continue
                edits.append((c - 1, c + 2, ".key()"))
                n_key += 1
            edits.sort(reverse=True)
            for k in range(len(edits) - 1):
                if edits[k + 1][1] > edits[k][0]:
                    raise SystemExit("overlapping edits at %s:%d" % (f, ln))
            for s0, e0, text in edits:
                t = t[:s0] + text + t[e0:]
            src[ln - 1] = t
        if not dry:
            open(p, "w", encoding="utf-8", newline="").write(nl.join(src))
    print("equals: %d comparison(s); key(): %d read(s)" % (n_eq, n_key))
    print("constructions left for a hand edit: %d" % len(news))
    for s in news:
        print("  " + s)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], "--dry" in sys.argv)
