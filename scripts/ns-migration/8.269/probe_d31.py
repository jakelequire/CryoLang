#!/usr/bin/env python3
"""D31's population probe: every place the parser opens generic arguments in
EXPRESSION position by lookahead (`is_generic_call_ahead`), printed as one
`SHADOW-D31` line per site so `scripts/objcmp/corpus2.sh` collects them over
the six halves.

    python scripts/ns-migration/8.269/probe_d31.py apply    # splice the probe in
    python scripts/ns-migration/8.269/probe_d31.py revert   # take it out again
    python scripts/ns-migration/8.269/probe_d31.py tally .objcmp/<tag>-lines.txt [more lines files]

Four sites, one per expression shape the lookahead decides:

    ident   `Name<T>(..)` / `Name<T> {..}` / `Name<T>::m` / a bare `Name<T>`,
            with `guess=1` when the name is not declared generic in the
            writing module (the token scan alone spoke - an import, or a
            comparison the scan misread)
    new     `new Type<T>(..)`
    member  `obj.method<T>(..)`
    scope   `Scope::member<T>(..)`

The type-position site in `parse_base_type` is not probed: a `<` after a
type name is never a comparison, and D31 leaves it alone.

`tally` reads a corpus2 lines file (`<half>\\tSHADOW-D31\\t<shape>\\t<file>:<line>:<col>\\t<name>`),
de-duplicates by location (every half re-parses the stdlib), and prints the
count per population and per shape.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
EXPR = os.path.join(ROOT, "compiler", "src", "compiler", "parser", "expr_parser.cryo")

PROBE_FN = '''    /// D31 population probe: one line per generic-argument list the lookahead
    /// opened in expression position.  Read by scripts/objcmp/corpus2.sh.
    probe_d31(shape: string, tok: Token, guess: boolean) -> void {
        fmt::eprintf("SHADOW-D31\\t%s\\t%s:%d:%d\\t%s\\tguess=%d\\n", shape, this.source_file,
            tok.line, tok.column, tok.lexeme, if (guess) { 1 } else { 0 });
    }

'''

# (anchor line that must exist exactly once, probe line to insert AFTER it)
SITES = [
    # 1. identifier: after the guessed-flag decision, before parse_generic_args
    ("            const gen_args: TypeAnnotation*[] = this.parse_generic_args();\n",
     "            this.probe_d31(\"ident\", tok, !this.is_generic_decl_name(name));\n", "before"),
    # 2. new Type<T>
    ("            const scope_gen_args: TypeAnnotation*[] = this.parse_generic_args();\n            node.set_generic_args(scope_gen_args);\n",
     "            this.probe_d31(\"new\", type_tok, false);\n", "before"),
    # 3. obj.method<T>
    ("            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();\n            node.set_generic_args(member_gen_args);\n",
     "            this.probe_d31(\"member\", member_tok, false);\n", "before"),
    # 4. Scope::member<T>
    ("            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();\n            sr.set_generic_args(member_gen_args);\n",
     "            this.probe_d31(\"scope\", member_tok, false);\n", "before"),
]
FN_ANCHOR = "    is_generic_call_ahead() -> boolean {\n"


def read():
    with open(EXPR, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(s):
    with open(EXPR, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def apply():
    s = read()
    assert "probe_d31" not in s, "already applied"
    for anchor, probe, where in SITES:
        assert s.count(anchor) == 1, "anchor not unique: %r (%d)" % (anchor[:60], s.count(anchor))
        s = s.replace(anchor, probe + anchor if where == "before" else anchor + probe)
    assert s.count(FN_ANCHOR) == 1
    s = s.replace(FN_ANCHOR, PROBE_FN + FN_ANCHOR)
    write(s)
    print("probe_d31: applied (4 sites + 1 method)")


def revert():
    s = read()
    assert "probe_d31" in s, "not applied"
    for anchor, probe, where in SITES:
        assert s.count(probe) == 1, "probe line not unique: %r" % probe
        s = s.replace(probe, "")
    assert s.count(PROBE_FN) == 1
    s = s.replace(PROBE_FN, "")
    assert "probe_d31" not in s
    write(s)
    print("probe_d31: reverted")


def normalize(half, loc):
    """One spelling per file, whichever half parsed it: every half runs from
    its own directory, so the same stdlib or compiler file arrives as an
    absolute path, `../../compiler/src/..`, or `src/..`."""
    p = loc.replace("\\", "/")
    # The unit suite's own `tests/stdlib/*.cryo` is not the stdlib.
    i = p.find("stdlib/")
    if i >= 0 and (i == 0 or p[i - 1] == "/") and not p[:i].endswith("tests/"):
        return "stdlib", p[i:]
    i = p.find("compiler/src/")
    if i >= 0 and (i == 0 or p[i - 1] == "/"):
        return "compiler/src", p[i:]
    i = p.find("tools/CryoLSP/")
    if i >= 0:
        return "tools/CryoLSP", p[i:]
    if half == "compiler-build" and p.startswith("src/"):
        return "compiler/src", "compiler/" + p
    if half.startswith("tools/CryoLSP") and p.startswith("src/"):
        return "tools/CryoLSP", "tools/CryoLSP/" + p
    if half.startswith("tests/tests/projects/"):
        return "tests/projects", half.split("(")[0] + p
    if half.startswith("tests/unit"):
        return "tests/unit", "tests/" + p
    if half.startswith("examples/"):
        return "examples", half + p
    if half.startswith("tests/tests/negative/"):
        return "tests/negative", "tests/" + p
    return "other:" + half, p


_SRC = {}


def follower(loc):
    """What follows the argument list at `loc` (`file:line:col`, the NAME
    token): `(` a call, `{` a struct literal, `::` a scope step, else bare.
    Read from the source, so the ident shape can be split without the probe
    looking past the list it has not parsed yet."""
    file, line, col = loc.rsplit(":", 2)
    path = os.path.join(ROOT, file)
    if path not in _SRC:
        try:
            # `newline=""`: the tree holds files with a lone `\r` inside a
            # line, which universal newlines would count as a line and the
            # lexer does not.
            with open(path, "r", encoding="utf-8", errors="replace", newline="") as f:
                _SRC[path] = f.read().split("\n")
        except OSError:
            _SRC[path] = None
    lines = _SRC[path]
    if lines is None:
        return "?"
    text = "\n".join(lines[int(line) - 1:int(line) + 3])
    i = text.find("<", int(col) - 1)
    if i < 0:
        return "?"
    depth = 0
    while i < len(text):
        c = text[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
            if depth == 0:
                j = i + 1
                while j < len(text) and text[j] in " \t\n":
                    j += 1
                if text.startswith("::", j):
                    return "scope-step"
                if j < len(text) and text[j] == "(":
                    return "call"
                if j < len(text) and text[j] == "{":
                    return "struct-lit"
                return "bare"
        i += 1
    return "?"


def tally(paths):
    seen = {}
    for lines_path in paths:
        with open(lines_path, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                parts = ln.rstrip("\n").split("\t")
                if len(parts) < 5 or parts[1] != "SHADOW-D31":
                    continue
                half, _, shape, loc, name = parts[0], parts[1], parts[2], parts[3], parts[4]
                guess = parts[5] if len(parts) > 5 else ""
                pop, loc = normalize(half, loc)
                key = (pop, loc)
                if key in seen:
                    continue
                seen[key] = (shape, name, guess, half)
    by_pop = {}
    by_shape = {}
    by_pop_shape = {}
    guessed = []
    for (pop, loc), (shape, name, guess, half) in seen.items():
        by_pop[pop] = by_pop.get(pop, 0) + 1
        by_shape[shape] = by_shape.get(shape, 0) + 1
        by_pop_shape[(pop, shape)] = by_pop_shape.get((pop, shape), 0) + 1
        if guess == "guess=1":
            guessed.append((pop, loc, name))
    print("D31 population: %d distinct sites" % len(seen))
    for pop in sorted(by_pop):
        print("  %-16s %6d   " % (pop, by_pop[pop]) +
              "  ".join("%s=%d" % (sh, by_pop_shape.get((pop, sh), 0)) for sh in sorted(by_shape)))
    print("  by shape: " + "  ".join("%s=%d" % (sh, by_shape[sh]) for sh in sorted(by_shape)))
    # The ident shape split by what follows the list, read from the source.
    sub = {}
    for (pop, loc), (shape, name, guess, half) in seen.items():
        if shape != "ident":
            continue
        k = follower(loc)
        sub[k] = sub.get(k, 0) + 1
    print("  ident by follower: " + "  ".join("%s=%d" % (k, sub[k]) for k in sorted(sub)))
    # Per file, for the module-by-module view.
    by_file = {}
    for (pop, loc), _ in seen.items():
        f = loc.rsplit(":", 2)[0]
        by_file[f] = by_file.get(f, 0) + 1
    print("  files: %d" % len(by_file))
    for f, n in sorted(by_file.items(), key=lambda kv: (-kv[1], kv[0])):
        print("    %5d  %s" % (n, f))
    print("  ident sites the scan alone decided (not declared generic in the writing module): %d" % len(guessed))
    for pop, loc, name in sorted(guessed):
        print("    %-16s %s  %s" % (pop, loc, name))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "apply":
        apply()
    elif cmd == "revert":
        revert()
    elif cmd == "tally":
        tally(sys.argv[2:])
    else:
        print(__doc__)
        sys.exit(2)
