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

  python retype.py --prologue <list.tsv> <repo-root>
      For a body that works on the bytes: rename `p: Text` to `p_text` and
      unwrap it once where the body opens (`attribute.py` picks the list).
  python retype.py --collapse <list.tsv> <repo-root>
      After `--refusals`: inside those bodies `Text::new(p)` becomes `p_text`.
  python retype.py --round-trips <dir>
      `Text::new(x.as_string())` becomes `x`.
  python retype.py --drop-unused <build-log> <root>
      Delete each entry unwrap a W0001 names as an unused variable.

`relocate.py` re-points a list's line numbers at the working tree when
earlier edits have moved the declarations.
"""
import os, re, sys

IMPORT = "import utils::text::{ Text };"
TEXT_T = "utils::text::Text"


def code(line):
    """The line with string and char literals emptied and a `//` comment cut:
    a brace inside `"{"` or `'{'` must not move a body's depth, or the body
    runs on to the end of the file."""
    s = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    s = re.sub(r"'(?:\\.|[^'\\])'", "''", s)
    return s.split("//")[0]


def is_decl(line, fn):
    """Whether `line` is the head of a declaration of `fn` - not a call to
    it, which a forward search from a stale line number can reach first."""
    return re.match(r"\s*(?:public\s+|private\s+)?(?:static\s+|function\s+)?%s\s*(<[^()]*>)?\s*\("
                    % re.escape(fn), line) is not None and not line.rstrip().endswith(";")


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


def prologue(listfile, root):
    """For each listed (already retyped) parameter `p: Text`, rename it
    `p_text` and unwrap it once where the body opens -
    `const p: string = p_text.as_string();` - so a body that works on the
    bytes (lengths, indexing, `+`) is left as it was.  The signature is what
    the callers see, and it says text."""
    todo = {}
    for l in open(listfile, encoding="utf-8"):
        c = l.rstrip("\n").split("\t")
        if len(c) < 3:
            continue
        f, ln = c[0].rsplit(":", 1)
        todo.setdefault(f, []).append((int(ln), c[1], c[2]))
    n = 0
    for f, items in sorted(todo.items()):
        p = os.path.join(root, f)
        lines, nl = read(p)
        # bottom-up so inserted lines do not shift the ones still to do
        for ln, fn, pn in sorted(items, reverse=True):
            head = ln - 1
            while not is_decl(lines[head], fn):
                head += 1
            pat = re.compile(r"\b%s(\s*:\s*Text\b)" % re.escape(pn))
            k = head
            while not pat.search(lines[k]):
                k += 1
            lines[k] = pat.sub(pn + r"_text\1", lines[k], count=1)
            b = k
            while "{" not in lines[b]:
                b += 1
            unwrap = "const %s: string = %s_text.as_string();" % (pn, pn)
            # an earlier `--refusals` may have unwrapped `p` at a use; the
            # body's `p` is the string now, so those become plain `p`
            depth, started, e = 0, False, b
            while e < len(lines):
                for ch in code(lines[e]):
                    if ch == "{":
                        depth += 1; started = True
                    elif ch == "}":
                        depth -= 1
                if started and depth <= 0:
                    break
                e += 1
            back = re.compile(r"(?<![\w.])%s\.as_string\(\)" % re.escape(pn))
            for q in range(b, e + 1):
                lines[q] = back.sub(pn, lines[q])
            brace = lines[b].index("{")
            if lines[b][brace + 1:].strip() == "":
                indent = re.match(r"\s*", lines[head]).group(0) + "    "
                lines.insert(b + 1, indent + unwrap)
            else:
                # a body on the head's own line: unwrap inside it
                lines[b] = lines[b][:brace + 1] + " " + unwrap + lines[b][brace + 1:]
            n += 1
        write(p, lines, nl)
    print("unwrapped %d parameter(s) at entry" % n)


def collapse(listfile, root):
    """After `--refusals`: inside the body of each function `--prologue`
    unwrapped, `Text::new(p)` re-wraps the parameter's own bytes; pass
    `p_text`, the text the caller handed in, instead."""
    todo = {}
    for l in open(listfile, encoding="utf-8"):
        c = l.rstrip("\n").split("\t")
        if len(c) < 3:
            continue
        todo.setdefault(c[0].rsplit(":", 1)[0], []).append((int(c[0].rsplit(":", 1)[1]), c[1], c[2]))
    n = 0
    for f, items in sorted(todo.items()):
        p = os.path.join(root, f)
        lines, nl = read(p)
        for ln, fn, pn in items:
            head = ln - 1
            while not is_decl(lines[head], fn):
                head += 1
            depth, started, i = 0, False, head
            while i < len(lines):
                for ch in code(lines[i]):
                    if ch == "{":
                        depth += 1; started = True
                    elif ch == "}":
                        depth -= 1
                if started and depth <= 0:
                    break
                i += 1
            pat = re.compile(r"Text::new\(%s\)" % re.escape(pn))
            for k in range(head, i + 1):
                lines[k], c = pat.subn(pn + "_text", lines[k])
                n += c
        write(p, lines, nl)
    print("collapsed %d re-wrap(s)" % n)


def round_trips(root):
    """`Text::new(x.as_string())` is `x`: only `Text` has `as_string`, so the
    argument was already text.  `--refusals` writes the shape when a `Text`
    that had been unwrapped for a `string` parameter meets that parameter
    retyped.  The check refuses any rewrite that was not."""
    pat = re.compile(r"Text::new\(([A-Za-z_][\w.]*)\.as_string\(\)\)")
    n = 0
    for base, _, files in os.walk(root):
        if os.sep + "build" in base or "/build" in base:
            continue
        for fn in files:
            if not fn.endswith(".cryo"):
                continue
            p = os.path.join(base, fn)
            src, nl = read(p)
            out = [pat.sub(r"\1", l) for l in src]
            k = sum(1 for a, b in zip(src, out) if a != b)
            if k:
                write(p, out, nl)
                n += k
    print("removed %d round trip(s)" % n)


def drop_unused(log, root):
    """After `--collapse`: an entry unwrap whose every use was collapsed back
    to `p_text` is an unused local, W0001 in a build log.  Delete the unwrap
    each such warning names; leave every other W0001 alone."""
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    hits = {}
    for i, l in enumerate(lines):
        m = re.match(r"warning\[W0001\]: unused variable `(\w+)`", l)
        if not m:
            continue
        f, ln, _ = lines[i + 1].strip()[3:].strip().rsplit(":", 2)
        hits.setdefault(f, []).append((int(ln), m.group(1)))
    n = 0
    for f, items in hits.items():
        p = os.path.join(root, f)
        src, nl = read(p)
        for ln, v in sorted(items, reverse=True):
            want = "const %s: string = %s_text.as_string();" % (v, v)
            if src[ln - 1].strip() == want:
                del src[ln - 1]
                n += 1
            elif want in src[ln - 1]:
                src[ln - 1] = src[ln - 1].replace(" " + want, "")
                n += 1
        write(p, src, nl)
    print("dropped %d unused unwrap(s)" % n)


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
            elif (re.search(r"expected `(string|i8\*|u8\*)`", note)
                  and ("found `%s`" % TEXT_T) in note):
                # a `string` parameter, or a C function's `i8*` / `u8*`,
                # which a `string` argument already satisfied
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
    elif a[:1] == ["--round-trips"]:
        round_trips(a[1])
    elif a[:1] == ["--drop-unused"]:
        drop_unused(a[1], a[2])
    elif a[:1] == ["--collapse"]:
        collapse(a[1], a[2])
    elif a[:1] == ["--prologue"]:
        prologue(a[1], a[2])
    elif a[:1] == ["--refusals"]:
        refusals(a[1], a[2], "--dry" in a)
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)
