"""Rewrite the call sites a handle-taking signature refuses.

Input: the log of a `cryo check` run over a tree whose arena reads take a
`TypeRef` where they took the handle's number.  Each refusal is an E0214
("expected `...TypeRef`, found `u64`") at the argument's first character.
Where that argument reads `<expr>.id`, the `.id` is dropped, so the call
passes the handle it was reading the number off.  Anything else is left
alone and listed: a stored number is not a handle, and turning it into
one is a design question per site, not a rewrite.

The compiler decides which arguments are refused; this script only edits
where it points.  A `.id` stripped off something that is not a `TypeRef`
(a `Type*`'s own id field, say) is refused by the next check.

Usage:
  python strip_id_args.py <check-log> <compiler-root> [--dry]
  python strip_id_args.py --rename <old> <new> <dir>...
"""
import os, re, sys

def rename(old, new, dirs):
    pat = re.compile(r"\b%s\(" % re.escape(old))
    n = 0
    for d in dirs:
        for base, _, files in os.walk(d):
            if os.sep + "build" in base or "/build" in base:
                continue
            for f in files:
                if not f.endswith(".cryo"):
                    continue
                p = os.path.join(base, f)
                s = open(p, encoding="utf-8", newline="").read()
                t, k = pat.subn(new + "(", s)
                if k:
                    open(p, "w", encoding="utf-8", newline="").write(t)
                    n += k
    print("renamed %d call(s) of %s -> %s" % (n, old, new))

def arg_span(lines, li, c):
    """(line, col) of the end of the argument starting at lines[li][c]."""
    depth = 0
    while li < len(lines):
        text = lines[li]
        while c < len(text):
            ch = text[c]
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                if depth == 0:
                    return li, c
                depth -= 1
            elif ch == "," and depth == 0:
                return li, c
            c += 1
        li += 1
        c = 0
    return None

def strip(log, root, dry):
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    sites = {}
    for i, l in enumerate(lines):
        if not l.startswith("error[E0214]"):
            continue
        for j in range(i + 1, min(i + 6, len(lines))):
            s = lines[j].strip()
            if s.startswith("-->"):
                f, ln, col = s[3:].strip().rsplit(":", 2)
                note = ""
                for k in range(j + 1, min(j + 12, len(lines))):
                    if "expected `" in lines[k]:
                        note = lines[k]
                        break
                if "TypeRef`" in note and "found `u64`" in note:
                    sites.setdefault(f, set()).add((int(ln), int(col)))
                break
    done, left = 0, []
    for f, locs in sorted(sites.items()):
        p = os.path.join(root, f)
        raw = open(p, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in raw else "\n"
        src = raw.split(nl)
        for ln, col in sorted(locs, reverse=True):
            # The column of `lookup((t as PointerType*).pointee.id)` is the
            # `t` inside the parenthesised cast: the argument starts one back.
            line = src[ln - 1]
            if col >= 3 and line[col - 2] == "(" and line[col - 3] == "(":
                col -= 1
            end = arg_span(src, ln - 1, col - 1)
            if end is None:
                left.append("%s:%d:%d  (no argument end)" % (f, ln, col)); continue
            el, ec = end
            # trim trailing whitespace before the terminator
            head = src[el][:ec]
            stripped = head.rstrip()
            if stripped.endswith(".id") and not re.search(r"\w\.id\w", stripped[-5:]):
                if not dry:
                    src[el] = stripped[:-3] + head[len(stripped):] + src[el][ec:]
                done += 1
            else:
                if el == ln - 1:
                    arg = src[el][col - 1:ec]
                else:
                    arg = src[ln - 1][col - 1:] + " ..."
                left.append("%s:%d:%d  %s" % (f, ln, col, arg.strip()))
        if not dry:
            open(p, "w", encoding="utf-8", newline="").write(nl.join(src))
    print("stripped %d `.id` argument(s)" % done)
    print("left %d:" % len(left))
    for l in left:
        print("  " + l)

def field(log, root, old, new):
    """Rename a field at each E0204 ("no field or method `old`") the check
    reports: only where the compiler says the renamed field is read."""
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    sites = {}
    for i, l in enumerate(lines):
        if not l.startswith("error[E0204]: no field or method `%s`" % old):
            continue
        for j in range(i + 1, min(i + 6, len(lines))):
            s = lines[j].strip()
            if s.startswith("-->"):
                f, ln, col = s[3:].strip().rsplit(":", 2)
                sites.setdefault(f, set()).add((int(ln), int(col)))
                break
    n = 0
    for f, locs in sorted(sites.items()):
        p = os.path.join(root, f)
        raw = open(p, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in raw else "\n"
        src = raw.split(nl)
        for ln, col in sorted(locs, reverse=True):
            t = src[ln - 1]
            c = col - 1
            if t[c:c + len(old)] != old:
                print("  no `%s` at %s:%d:%d" % (old, f, ln, col)); continue
            src[ln - 1] = t[:c] + new + t[c + len(old):]
            n += 1
        open(p, "w", encoding="utf-8", newline="").write(nl.join(src))
    print("renamed field %s -> %s at %d site(s)" % (old, new, n))

if __name__ == "__main__":
    if sys.argv[1] == "--rename":
        rename(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif sys.argv[1] == "--field":
        field(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        strip(sys.argv[1], sys.argv[2], "--dry" in sys.argv)
