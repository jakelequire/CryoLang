#!/usr/bin/env python3
"""The name-resolution migration's definition of done, measured.

docs/name-resolution.md §0.0 states four conditions.  This script answers
the two that are measurable from the tree today:

  --outstanding   condition one: every lookup function in the residue's
                  population that still takes a spelling and is NOT
                  justified (classes S, B, C, F, W of
                  `residue_classify.py`), one line per function with its
                  call-site count.  Refuses to answer unless
                  `residue.py --check` reads OK, so the list is the tree's.
  --name-taking   condition two's population: every function or method
                  declared in compiler/src with a parameter that carries a
                  spelling - `SymbolStr`, `string` or `QualifiedName`, by
                  value, by reference or as an array - one line each
                  (file:line name).  No approved list exists yet, so every
                  line is unplaced.
  --selftest      drives the --name-taking matcher over declarations it
                  must list and ones it must not; the last line is the
                  number of cases it got right.

A count is the line count of either listing (`| wc -l`).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RESIDUE_PY = os.path.join(ROOT, "scripts", "ns-migration", "residue.py")
RESIDUE_MD = os.path.join(ROOT, "scripts", "ns-migration", "residue.md")
SRC = os.path.join(ROOT, "compiler", "src")

CONVERTIBLE = ("S", "B", "C", "F", "W")
ROW_RE = re.compile(r"^\| `([^`]+)` \| `([^`]+)` \| .*? \| ([A-Z]) \| ")


def outstanding():
    chk = subprocess.run([sys.executable, RESIDUE_PY, "--check"],
                         capture_output=True, text=True, cwd=ROOT)
    if "residue: OK" not in chk.stdout:
        sys.stderr.write("done.py: residue.py --check is not OK; the list would not be the tree's\n")
        sys.stderr.write(chk.stdout + chk.stderr)
        sys.exit(1)
    counts = {}
    with open(RESIDUE_MD, encoding="utf-8") as f:
        for line in f:
            m = ROW_RE.match(line)
            if not m or m.group(3) not in CONVERTIBLE:
                continue
            key = (m.group(3), m.group(2))
            counts[key] = counts.get(key, 0) + 1
    for (cls, door), n in sorted(counts.items(), key=lambda kv: (kv[0][0], -kv[1], kv[0][1])):
        print(f"{cls}\t{n}\t{door}")


# A declaration: a name, an optional generic list, a parameter list holding
# at least one `name: Type` annotation, then `->` or `{`.  A call statement
# ends in `;` and carries no annotation, so it does not match.
DECL_HEAD = re.compile(r"^\s*(?:public\s+|private\s+)?(?:static\s+|function\s+)?"
                       r"([a-z_][A-Za-z0-9_]*)\s*(?:<[^()]*?>)?\s*\(")
# A spelling reaches a function by value, by reference (`&SymbolStr[]`,
# `&string[]`) or as a `QualifiedName`, whose only content is `SymbolStr`
# segments.  A matcher that knows only the by-value shape does not see
# `Resolver::resolve_path(segments: &SymbolStr[], ..)`.
KEY_PARAM = re.compile(r"(?:^|[,(])\s*(?:mut\s+)?[a-z_][A-Za-z0-9_]*\s*:\s*(?:const\s+)?"
                       r"(?:&\s*(?:mut\s+)?)?(?:SymbolStr|string|QualifiedName)\b")
KEYWORDS = {"if", "for", "while", "match", "return", "switch", "sizeof", "alignof"}


def name_taking():
    for dirpath, _, files in os.walk(SRC):
        for fn in sorted(files):
            if not fn.endswith(".cryo"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
            for line_no, name in declarations_taking_a_name(lines):
                print(f"{rel}:{line_no}\t{name}")


def declarations_taking_a_name(lines):
    """(1-based line, name) of every declaration in `lines` whose parameter
    list carries a spelling."""
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("//"):
            i += 1
            continue
        m = DECL_HEAD.match(line)
        if not m or m.group(1) in KEYWORDS:
            i += 1
            continue
        # Join until the parameter list closes (signatures wrap).
        text = line[m.end():]
        depth = 1
        j = i
        while True:
            for ch in text:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        break
            if depth == 0 or j - i > 12 or j + 1 >= len(lines):
                break
            j += 1
            text += " " + lines[j]
        close = 0
        d = 1
        for k, ch in enumerate(text):
            if ch == "(":
                d += 1
            elif ch == ")":
                d -= 1
                if d == 0:
                    close = k
                    break
        params = "(" + text[:close]
        tail = text[close + 1:].lstrip()
        if (tail.startswith("->") or tail.startswith("{")) and KEY_PARAM.search(params):
            yield i + 1, m.group(1)
        i = j + 1


# (declaration text, listed?).  Each shape a spelling reaches a function
# through must be listed; an identity, a link symbol, text that is not a
# name, a call and a comment must not.
SELFTEST_CASES = [
    ("lookup(&this, name: SymbolStr) -> TypeRef {", True),
    ("static emit(msg: string) -> void {", True),
    ("register(mut &this, names: SymbolStr[]) -> void {", True),
    ("resolve_path(&this, segments: &SymbolStr[], ns: Namespace,\n"
     "             scope: ScopeID) -> Res {", True),
    ("specialize(&this, displays: &string[]) -> Result {", True),
    ("keep(&this, key: const SymbolStr) -> boolean {", True),
    ("for_free_function(qname: QualifiedName) -> MangledName {", True),
    ("join(&this, other: &QualifiedName) -> QualifiedName {", True),
    ("lookup(&this, def: DefId) -> TypeRef {", False),
    ("find(&this, path: ModulePath) -> DefId {", False),
    ("equals(&this, other: MangledName) -> boolean {", False),
    ("static strip_eol(line: &String) -> u64 {", False),
    ("        lookup(name);", False),
    ("// lookup(&this, name: SymbolStr) -> TypeRef {", False),
]


def selftest():
    right = 0
    for text, want in SELFTEST_CASES:
        got = bool(list(declarations_taking_a_name(text.split("\n"))))
        if got == want:
            right += 1
        else:
            print(f"WRONG ({'missed' if want else 'listed'}): {text.split(chr(10))[0]}")
    print(f"done.py selftest: {right} of {len(SELFTEST_CASES)} cases right")
    print(right)


def main():
    if "--outstanding" in sys.argv:
        outstanding()
    elif "--name-taking" in sys.argv:
        name_taking()
    elif "--selftest" in sys.argv:
        selftest()
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
