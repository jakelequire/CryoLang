"""Retype a list of `string` parameters as `Text`, and rewrite what the
compiler then refuses.

Two modes.  The compiler decides every call site; this script only edits
where a refusal points.

  python retype.py --params <list.tsv> <repo-root>
      Each line of the list is `file:line <TAB> function <TAB> parameter`
      (further columns ignored): the parameter of the declaration whose
      head is on that line has its `string` type replaced by `Text`.  Each
      file edited gets `import utils::text::{ Text };` after its last
      top-level import.

  python retype.py --refusals <check-log> <root> [--dry]
      Reads `cryo check` output.  At each E0214 whose note reads
      "expected `utils::text::Text`, found `string`", the argument that
      starts at the reported column is wrapped `Text::new(...)`; at each
      "expected `string`, found `utils::text::Text`" it gets `.as_string()`
      appended (parenthesised when it is not a plain path).  Every other
      refusal is listed, not edited: a return, an assignment, an operator,
      a field initialiser each need reading.  Files edited gain the import.
"""
import os, re, sys

IMPORT = "import utils::text::{ Text };"
TEXT_T = "utils::text::Text"


def read(p):
    raw = open(p, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in raw else "\n"
    return raw.split(nl), nl


def write(p, lines, nl):
    open(p, "w", encoding="utf-8", newline="").write(nl.join(lines))


def add_import(lines):
    if any(l.strip() == IMPORT for l in lines):
        return False
    last = -1
    i = 0
    while i < len(lines):
        l = lines[i]
        s = l.strip()
        if s.startswith("import ") and not l.startswith((" ", "\t")):
            # an import may wrap: it ends at the line carrying its `;`
            while not lines[i].rstrip().endswith(";") and i + 1 < len(lines):
                i += 1
            last = i
        elif s.startswith(("type ", "function ", "implement", "const ", "mut ", "public ",
                           "private ", "extern ", "static ", "intrinsic ")) and last >= 0:
            break
        i += 1
    if last < 0:
        for i, l in enumerate(lines):
            if l.strip().startswith("namespace "):
                last = i
                break
    lines.insert(last + 1, IMPORT)
    return True


def retype_params(listfile, root):
    todo = {}
    for l in open(listfile, encoding="utf-8"):
        c = l.rstrip("\n").split("\t")
        if len(c) < 3:
            continue
        f, ln = c[0].rsplit(":", 1)
        todo.setdefault(f, []).append((int(ln), c[2]))
    n, miss = 0, []
    for f, items in sorted(todo.items()):
        p = os.path.join(root, f)
        lines, nl = read(p)
        for ln, pn in items:
            pat = re.compile(r"(\b%s\s*:\s*(?:const\s+)?(?:&\s*(?:mut\s+)?)?)string\b" % re.escape(pn))
            for k in range(ln - 1, min(ln + 12, len(lines))):
                t, c = pat.subn(r"\1Text", lines[k], count=1)
                if c:
                    lines[k] = t
                    n += 1
                    break
                if re.search(r"\)\s*(->|\{)", lines[k]):
                    miss.append("%s:%d %s" % (f, ln, pn))
                    break
            else:
                miss.append("%s:%d %s" % (f, ln, pn))
        add_import(lines)
        write(p, lines, nl)
    print("retyped %d parameter(s) in %d file(s)" % (n, len(todo)))
    for m in miss:
        print("  NOT FOUND " + m)


def arg_end(lines, li, c):
    """(line, col) just past the argument starting at lines[li][c]."""
    depth = 0
    instr = None
    while li < len(lines):
        text = lines[li]
        while c < len(text):
            ch = text[c]
            if instr:
                if ch == "\\":
                    c += 2
                    continue
                if ch == instr:
                    instr = None
            elif ch in "\"'":
                instr = ch
            elif ch in "([{":
                depth += 1
            elif ch in ")]}":
                if depth == 0:
                    return li, c
                depth -= 1
            elif ch in ",;" and depth == 0:
                return li, c
            c += 1
        li += 1
        c = 0
    return None


SIMPLE = re.compile(r"^[A-Za-z_][\w.]*(\[[^\[\]]*\])?(\.[A-Za-z_]\w*)*(\(\))?$")


def refusals(log, root, dry):
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    sites, other = {}, []
    for i, l in enumerate(lines):
        if not l.startswith("error["):
            continue
        loc = None
        for j in range(i + 1, min(i + 6, len(lines))):
            s = lines[j].strip()
            if s.startswith("-->"):
                loc = s[3:].strip()
                break
        if loc is None:
            other.append(l)
            continue
        note = ""
        for k in range(i, min(i + 14, len(lines))):
            # the caret line: `   |   ^~~~ expected `A`, found `B``
            if re.match(r"^\s*\|\s*\^~*\s+expected `", lines[k]):
                note = lines[k]
                break
        kind = None
        if l.startswith("error[E0214]"):
            if ("expected `%s`" % TEXT_T) in note and "found `string`" in note:
                kind = "wrap"
            elif "expected `string`" in note and ("found `%s`" % TEXT_T) in note:
                kind = "unwrap"
        f, ln, col = loc.rsplit(":", 2)
        if kind is None:
            other.append("%s:%s:%s  %s" % (f, ln, col, l))
            continue
        sites.setdefault(f, set()).add((int(ln), int(col), kind))
    done = 0
    for f, locs in sorted(sites.items()):
        p = os.path.join(root, f)
        src, nl = read(p)
        for ln, col, kind in sorted(locs, reverse=True):
            li, c = ln - 1, col - 1
            end = arg_end(src, li, c)
            if end is None:
                other.append("%s:%d:%d  (no argument end)" % (f, ln, col))
                continue
            el, ec = end
            if el != li:
                # a multi-line argument: wrap across lines
                tail = src[el][:ec]
                t = tail.rstrip()
                pad = tail[len(t):]
                if kind == "wrap":
                    src[el] = t + ")" + pad + src[el][ec:]
                    src[li] = src[li][:c] + "Text::new(" + src[li][c:]
                else:
                    src[el] = t + ").as_string()" + pad + src[el][ec:]
                    src[li] = src[li][:c] + "(" + src[li][c:]
                done += 1
                continue
            seg = src[li][c:ec]
            arg = seg.rstrip()
            pad = seg[len(arg):]
            if kind == "wrap":
                new = "Text::new(" + arg + ")"
            elif SIMPLE.match(arg):
                new = arg + ".as_string()"
            else:
                new = "(" + arg + ").as_string()"
            src[li] = src[li][:c] + new + pad + src[li][ec:]
            done += 1
        if not dry:
            if any(k == "wrap" for _, _, k in locs):
                add_import(src)
            write(p, src, nl)
    print("rewrote %d argument(s) in %d file(s)" % (done, len(sites)))
    print("left %d:" % len(other))
    for o in other:
        print("  " + o)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--params"]:
        retype_params(a[1], a[2])
    elif a[:1] == ["--refusals"]:
        refusals(a[1], a[2], "--dry" in a)
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)
