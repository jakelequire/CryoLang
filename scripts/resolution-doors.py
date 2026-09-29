#!/usr/bin/env python3
"""The doors of `docs/resolution-rules.md` against the doors in the code.

A door is the one function where a spelling may become a binding (rule three
of that document).  The document lists them in a table between `<!-- doors
-->` and `<!-- /doors -->`, one row per function; the code marks each with a
doc comment on its declaration whose first line is

    /// Door `<id>`.  <the reason it may take text>

This refuses the tree when the two disagree, in either direction:

    - a marker in `compiler/src` whose (door, function, file) no row lists -
      a door added in code and not on the ruled list;
    - a row whose function carries no such marker - a door on the list the
      code no longer has, or whose reason comment was deleted;
    - a marker with no reason, a line that starts `/// Door` in any other
      form (a marker this reader would otherwise skip), a marker placed on
      nothing a declaration follows, or one function marked twice.

It reads source text only - the markers and the table are both text - so it
needs no build and no facts.

Usage:
    python scripts/resolution-doors.py              check the tree
    python scripts/resolution-doors.py --list       the doors as the code marks them
    python scripts/resolution-doors.py --selftest   drive every refusal and the pass

Exit codes: 0 agreement; 1 otherwise, each disagreement named.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = "docs/resolution-rules.md"
SRC = "compiler/src"

ANY_MARK = re.compile(r"^\s*///\s*Door\b")
MARK = re.compile(r"^\s*///\s*Door `([a-z][a-z0-9-]*)`\.(.*)$")
DOC_LINE = re.compile(r"^\s*///")
ATTR_LINE = re.compile(r"^\s*!\[")
DECL = re.compile(r"^\s*(?:(?:public|private|protected|static|mut|function)\s+)*"
                  r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\(")
ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|")


def table(doc_text):
    """[(door, function leaf, file, the function as written)] from the doc."""
    rows, inside, problems = [], False, []
    for n, line in enumerate(doc_text.splitlines(), 1):
        if line.strip() == "<!-- doors -->":
            inside = True
            continue
        if line.strip() == "<!-- /doors -->":
            inside = False
            continue
        if not inside or not line.startswith("|") or line.startswith("|---") \
                or line.startswith("| door "):
            continue
        m = ROW.match(line)
        if not m:
            problems.append("%s:%d: a table row this reader cannot parse: %s" % (DOC, n, line))
            continue
        door, func, path = m.groups()
        rows.append((door, func.split("::")[-1], path, func))
    if not rows:
        problems.append("%s: no door rows between `<!-- doors -->` and `<!-- /doors -->`" % DOC)
    return rows, problems


def markers(files):
    """[(door, function leaf, file, line)] from {repo-relative path: text}."""
    found, problems = [], []
    for path in sorted(files):
        lines = files[path].splitlines()
        for i, line in enumerate(lines):
            if not ANY_MARK.match(line):
                continue
            where = "%s:%d" % (path, i + 1)
            m = MARK.match(line)
            if not m:
                problems.append("%s: a `/// Door` line not of the form ``/// Door `<id>`.  "
                                "<reason>``: %s" % (where, line.strip()))
                continue
            door, rest = m.group(1), m.group(2).strip()
            j = i + 1
            reason = rest
            while j < len(lines) and DOC_LINE.match(lines[j]):
                reason += lines[j].strip()[3:].strip()
                j += 1
            while j < len(lines) and ATTR_LINE.match(lines[j]):
                j += 1
            d = DECL.match(lines[j]) if j < len(lines) else None
            if not d:
                problems.append("%s: door `%s` marks no declaration" % (where, door))
                continue
            if not reason:
                problems.append("%s: door `%s` on `%s` gives no reason" % (where, door, d.group(1)))
            found.append((door, d.group(1), path, i + 1))
    return found, problems


def compare(doc_text, files):
    rows, problems = table(doc_text)
    marks, mp = markers(files)
    problems += mp
    listed = {(d, f, p) for d, f, p, _ in rows}
    marked = {}
    for d, f, p, n in marks:
        key = (d, f, p)
        if key in marked:
            problems.append("%s:%d: `%s` is marked as door `%s` twice" % (p, n, f, d))
        marked[key] = n
    for key in sorted(set(marked) - listed):
        d, f, p = key
        problems.append("%s:%d: `%s` is marked as door `%s` and %s lists no such door - "
                        "a door is added to the ruled list, not only to the code"
                        % (p, marked[key], f, d, DOC))
    for d, f, p, written in rows:
        if (d, f, p) not in marked:
            problems.append("%s lists door `%s` as `%s` in %s, and no marker there says so - "
                            "the door is gone, renamed, or lost its reason comment"
                            % (DOC, d, written, p))
    return rows, marks, problems


def read_tree():
    with open(os.path.join(ROOT, DOC), encoding="utf-8") as fh:
        doc = fh.read()
    files = {}
    for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, SRC)):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith(".") and d != "build")
        for fn in filenames:
            if fn.endswith(".cryo"):
                p = os.path.join(dirpath, fn)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    files[os.path.relpath(p, ROOT).replace("\\", "/")] = fh.read()
    return doc, files


def selftest():
    doc = ("x\n<!-- doors -->\n| door | function | file | kind |\n|---|---|---|---|\n"
           "| `intern` | `T::intern` | `a.cryo` | text |\n"
           "| `scope-value` | `R::lookup_value` | `b.cryo` | bare |\n<!-- /doors -->\n")
    good = {
        "a.cryo": "type struct T {}\n/// Door `intern`.  Text becomes a name here.\n"
                  "intern(mut &this, text: string) -> SymbolStr {\n",
        "b.cryo": "/// Door `scope-value`.\n/// The rib walk.\n"
                  "![allow(lookup_by_spelling, reason = \"x\")]\n"
                  "lookup_value(&this, name: SymbolStr) -> SymbolID {\n",
    }
    cases = [
        ("agreement", good, None),
        ("a door in code the list does not have",
         dict(good, **{"c.cryo": "/// Door `module-by-path`.  Modules.\n"
                                 "module_named(&this, ns: SymbolStr) -> ModulePath {\n"}),
         "lists no such door"),
        ("a listed door the code no longer marks",
         dict(good, **{"b.cryo": "/// The rib walk.\nlookup_value(&this, name: SymbolStr) -> SymbolID {\n"}),
         "no marker there says so"),
        ("a listed door moved to another function",
         dict(good, **{"b.cryo": "/// Door `scope-value`.  Rib.\nlookup(&this, name: SymbolStr) -> SymbolID {\n"}),
         "lists no such door"),
        ("a marker with no reason",
         dict(good, **{"a.cryo": "/// Door `intern`.\nintern(mut &this, text: string) -> SymbolStr {\n"}),
         "gives no reason"),
        ("a malformed marker",
         dict(good, **{"d.cryo": "/// Door intern: text.\nfoo(&this) -> void {\n"}),
         "not of the form"),
        ("a marker on nothing",
         dict(good, **{"d.cryo": "/// Door `intern`.  Text.\n\nfoo(&this) -> void {\n"}),
         "marks no declaration"),
        ("one function marked twice",
         dict(good, **{"a.cryo": good["a.cryo"] + "/// Door `intern`.  Again.\n"
                                                  "intern(mut &this, text: string) -> SymbolStr {\n"}),
         "twice"),
    ]
    failed = 0
    for name, files, expect in cases:
        _rows, _marks, problems = compare(doc, files)
        text = "\n".join(problems)
        if expect is None:
            ok = not problems
        else:
            ok = any(expect in p for p in problems)
        print("  %-4s %s%s" % ("ok" if ok else "FAIL", name,
                               "" if ok else " -- got: " + (text or "no refusal")))
        failed += 0 if ok else 1
    _none, empty = table("nothing here\n")
    if not any("no door rows" in p for p in empty):
        print("  FAIL an empty table is refused")
        failed += 1
    else:
        print("  ok   an empty table is refused")
    if failed:
        print("resolution-doors: selftest FAIL -- %d case(s)" % failed)
        return 1
    print("resolution-doors: selftest OK -- %d cases" % (len(cases) + 1))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    doc, files = read_tree()
    rows, marks, problems = compare(doc, files)
    if args.list:
        for d, f, p, n in sorted(marks):
            print("%-16s %-28s %s:%d" % (d, f, p, n))
    if problems:
        print("resolution-doors: FAIL -- %d disagreement%s between %s and the code:"
              % (len(problems), "" if len(problems) == 1 else "s", DOC))
        for p in problems:
            print("  " + p)
        return 1
    print("resolution-doors: OK -- %d door functions, %d doors, listed and marked alike"
          % (len(rows), len({r[0] for r in rows})))
    return 0


if __name__ == "__main__":
    sys.exit(main())
