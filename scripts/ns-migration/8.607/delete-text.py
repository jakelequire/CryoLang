"""Delete the compiler's `Text` type: it wraps a `string` and seals nothing.

Two steps, run in order:

  rewrite            over compiler/src and tools/CryoLSP/src:
                     - `import utils::text::{ Text };` and `import utils::text;`
                       lines are removed;
                     - `Text::new(X)` becomes `X` (or `(X)` when a `.`
                       follows and X is not a single operand);
                     - the identifier `Text` in code (not after `::` or `.`,
                       not in a comment or a literal) becomes `string`;
                     - compiler/src/utils/text.cryo is deleted.
  strip [--base=DIR] LOG [LOG..]
                     removes `.as_string()` at each place a compiler log
                     reports `as_string` refused on a `string` receiver.
                     `MangledName` and `Keyword` keep their own
                     `as_string`, so the receivers are taken from the
                     compiler's own type check, not from the text.  Run the
                     compiler, strip, repeat until the log has none.

Line endings are kept as each file has them.  Prints what it changed.

Usage: python scripts/ns-migration/8.607/delete-text.py rewrite
       python scripts/ns-migration/8.607/delete-text.py strip build.log
"""
import os
import re
import sys

ROOTS = [os.path.join("compiler", "src"), os.path.join("tools", "CryoLSP", "src")]
TEXT_FILE = os.path.join("compiler", "src", "utils", "text.cryo")
IMPORT = re.compile(r'^[ \t]*import utils::text(::\{ Text \})?;[ \t]*\r?\n?$')
IDENT = re.compile(r'[A-Za-z0-9_]')


def cryo_files():
    for root in ROOTS:
        for dirpath, _dirs, files in os.walk(root):
            if os.sep + "build" in dirpath:
                continue
            for f in sorted(files):
                if f.endswith(".cryo"):
                    yield os.path.join(dirpath, f)


def skip_literal(s, i):
    """`s[i]` opens a string or char literal; the index past its end."""
    q = s[i]
    j = i + 1
    while j < len(s):
        if s[j] == "\\":
            j += 2
            continue
        if s[j] == q:
            return j + 1
        if s[j] == "\n" and q == "'":
            return j
        j += 1
    return j


def code_spans(s):
    """Yield (start, end, kind) over `s`: kind is 'code', 'comment' or 'lit'."""
    i = 0
    start = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] == "/":
            if start < i:
                yield (start, i, "code")
            j = s.find("\n", i)
            j = n if j < 0 else j
            yield (i, j, "comment")
            i = start = j
            continue
        if c == "/" and i + 1 < n and s[i + 1] == "*":
            if start < i:
                yield (start, i, "code")
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            yield (i, j, "comment")
            i = start = j
            continue
        if c == '"' or (c == "'" and not (i > 0 and IDENT.match(s[i - 1]))):
            if start < i:
                yield (start, i, "code")
            j = skip_literal(s, i)
            yield (i, j, "lit")
            i = start = j
            continue
        i += 1
    if start < n:
        yield (start, n, "code")


def kind_map(s):
    kinds = bytearray(len(s))
    for a, b, k in code_spans(s):
        if k != "code":
            for x in range(a, b):
                kinds[x] = 1
    return kinds


def matching_paren(s, kinds, open_i):
    depth = 0
    i = open_i
    while i < len(s):
        if not kinds[i]:
            if s[i] == "(":
                depth += 1
            elif s[i] == ")":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    raise ValueError("unbalanced Text::new( at %d" % open_i)


SIMPLE = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*'
                    r'|"(\\.|[^"\\])*"|f"(\\.|[^"\\])*")\s*$')


def needs_parens(inner, s, close_i, kinds):
    j = close_i + 1
    while j < len(s) and s[j] in " \t":
        j += 1
    if j >= len(s) or s[j] != "." or kinds[j]:
        return False
    if SIMPLE.match(inner):
        return False
    # A single call or member chain needs none either.
    depth = 0
    for k, ch in enumerate(inner):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0 and ch in "+-*/%<>=!&|?: ," and not inner.startswith(("f\"", "\"")):
            return True
    return False


def unwrap_text_new(s):
    count = 0
    while True:
        kinds = kind_map(s)
        found = -1
        for m in re.finditer(r'\bText::new\(', s):
            if not kinds[m.start()] and (m.start() == 0 or s[m.start() - 1] not in ":."):
                found = m.start()
                break
        if found < 0:
            return s, count
        open_i = found + len("Text::new")
        close_i = matching_paren(s, kinds, open_i)
        inner = s[open_i + 1:close_i]
        if needs_parens(inner, s, close_i, kinds):
            rep = "(" + inner + ")"
        else:
            rep = inner
        s = s[:found] + rep + s[close_i + 1:]
        count += 1


def retype(s):
    kinds = kind_map(s)
    out = []
    count = 0
    last = 0
    for m in re.finditer(r'\bText\b', s):
        a = m.start()
        if kinds[a]:
            continue
        before = s[a - 2:a] if a >= 2 else ""
        if before == "::" or (a > 0 and s[a - 1] == "."):
            continue
        if s[m.end():m.end() + 2] == "::":
            continue
        # An enum variant declared `Text;` (the editor's completion kinds)
        # is a name of its own, not the type.
        line_start = s.rfind("\n", 0, a) + 1
        if s[line_start:a].strip() == "" and re.match(r'\s*[;,(]', s[m.end():]):
            continue
        out.append(s[last:a])
        out.append("string")
        last = m.end()
        count += 1
    out.append(s[last:])
    return "".join(out), count


def rewrite():
    totals = [0, 0, 0]
    for path in cryo_files():
        if os.path.normpath(path) == os.path.normpath(TEXT_FILE):
            continue
        with open(path, "r", encoding="utf-8", newline="") as fh:
            src = fh.read()
        lines = src.splitlines(keepends=True)
        kept = [l for l in lines if not IMPORT.match(l)]
        imports = len(lines) - len(kept)
        s = "".join(kept)
        s, wraps = unwrap_text_new(s)
        s, types = retype(s)
        if imports or wraps or types:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(s)
            print("%s: %d import(s), %d Text::new, %d type(s)" % (path, imports, wraps, types))
            totals[0] += imports
            totals[1] += wraps
            totals[2] += types
    if os.path.exists(TEXT_FILE):
        os.remove(TEXT_FILE)
        print("deleted %s" % TEXT_FILE)
    print("total: %d import(s), %d Text::new, %d type(s)" % tuple(totals))


HEAD = re.compile(r'^error\[E\d+\]: .*`as_string`')
LOC = re.compile(r'^\s*--> (.*?):(\d+):(\d+)')


def strip(args):
    # A log written by a compiler run inside a project names its files
    # relative to that project: `--base=DIR` says which.
    base = ""
    logs = []
    for a in args:
        if a.startswith("--base="):
            base = a[len("--base="):]
        else:
            logs.append(a)
    sites = set()
    for log in logs:
        with open(log, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
        for i, l in enumerate(lines):
            if not HEAD.match(l):
                continue
            for j in range(i + 1, min(i + 4, len(lines))):
                m = LOC.match(lines[j])
                if m:
                    path = m.group(1)
                    if base and not os.path.isabs(path):
                        path = os.path.join(base, path)
                    sites.add((os.path.normpath(path), int(m.group(2)), int(m.group(3))))
                    break
    by_file = {}
    for f, line, col in sites:
        by_file.setdefault(f, set()).add((line, col))
    removed = 0
    for f, wanted in sorted(by_file.items()):
        with open(f, "r", encoding="utf-8", newline="") as fh:
            lines = fh.readlines()
        n = 0
        # The reported column is the method name's; the `.` before it opens
        # the call to remove.  Right to left, so a removal on a line leaves
        # the columns of the others before it in place.
        for ln, col in sorted(wanted, key=lambda s: (s[0], -s[1])):
            l = lines[ln - 1]
            dot = col - 2
            if l[dot:dot + len(".as_string()")] != ".as_string()":
                print("%s:%d:%d: no `.as_string()` at the reported column" % (f, ln, col))
                continue
            lines[ln - 1] = l[:dot] + l[dot + len(".as_string()"):]
            n += 1
        if n:
            with open(f, "w", encoding="utf-8", newline="") as fh:
                fh.writelines(lines)
            print("%s: %d `.as_string()` removed" % (f, n))
            removed += n
    print("total: %d `.as_string()` removed" % removed)


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "rewrite":
        rewrite()
    elif len(sys.argv) >= 3 and sys.argv[1] == "strip":
        strip(sys.argv[2:])
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
