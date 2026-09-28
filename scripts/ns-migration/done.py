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
  --name-taking   condition two's population: every function, method or
                  constructor declared in compiler/src with a parameter that
                  carries a spelling - `SymbolStr`, `string` or
                  `QualifiedName`, by value, by reference or as an array -
                  one line each (file:line name).  Read from the compiler's
                  own declaration records (`.facts/compiler.facts`, refused
                  when stale): a `fn` record and its `param` records, whose
                  key column is the compiler's answer to "what key does this
                  parameter's resolved type carry".  No approved list exists
                  yet, so every line is unplaced.
  --selftest      drives the --name-taking reader over records it must
                  list and ones it must not; the last line is the number of
                  cases it got right.

A count is the line count of either listing (`| wc -l`).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RESIDUE_PY = os.path.join(ROOT, "scripts", "ns-migration", "residue.py")
RESIDUE_MD = os.path.join(ROOT, "scripts", "ns-migration", "residue.md")

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


# The key column of a `param` record names what the parameter's resolved
# type carries under any number of references and arrays (`CallFacts::key_of`
# in the compiler); these three are spellings.  `ModulePath` is a module's
# identity and is outside condition two.  The compiler answers the type, so
# an alias, a wrapped signature, a return arrow on its own line, a
# constructor named by its class and a capitalised name are all the same
# record.
NAME_KEYS = ("string", "SymbolStr", "QualifiedName")
# Records are spelled relative to the compiler project; this is the part of
# the population condition two is about.
SRC_PREFIX = "src/"


def facts_lines():
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import facts as facts_mod
    with open(facts_mod.facts_path("compiler"), encoding="utf-8") as fh:
        return fh.read().splitlines()


def name_taking():
    for rel, line_no, name in declarations_taking_a_name(facts_lines()):
        print(f"{rel}:{line_no}\t{name}")


def declarations_taking_a_name(records):
    """(compiler/src path, line, name) of every `fn` record under
    compiler/src with a `param` record carrying a spelling, in file and line
    order.  A parameter names its function by the owner, the function's name
    and its linker symbol, which is what separates two overloads."""
    fns = {}
    taking = set()
    for line in records:
        c = line.split("\t")
        if len(c) < 15 or c[0] not in ("fn", "param") or not c[1].startswith(SRC_PREFIX):
            continue
        if c[0] == "fn":
            fns[(c[12], c[13], c[8])] = ("compiler/" + c[1], int(c[2]), c[13])
        elif c[9] in NAME_KEYS:
            taking.add((c[12], c[13].split(":", 1)[0], c[8]))
    missing = sorted(k for k in taking if k not in fns)
    if missing:
        raise SystemExit("done.py: %d parameter record(s) name no function record, first %s"
                         % (len(missing), "/".join(missing[0])))
    return sorted(fns[k] for k in taking)


def record(kind, path, line, key, owner, name, symbol="C$sym", type_text="-"):
    return "\t".join([kind, path, str(line), "5", "0", type_text, "public", "method",
                      symbol, key, "-", "-", owner, name, "0"])


def fn_with(key, path="src/a.cryo", name="f", owner="a::T"):
    return [record("fn", path, 3, "-", owner, name),
            record("param", path, 3, key, owner, name + ":p")]


# (records, listed?).  Each key a spelling reaches a function through must
# be listed, whoever declares it; a key that is no spelling, a function with
# no keyed parameter and a declaration outside compiler/src must not.  A
# parameter record whose function has no record is refused, not dropped.
SELFTEST_CASES = [
    (fn_with("SymbolStr"), True),
    (fn_with("string"), True),
    (fn_with("QualifiedName"), True),
    (fn_with("string", name="Parser", owner="compiler::parser::parser::Parser"), True),
    (fn_with("ModulePath"), False),
    (fn_with("-"), False),
    ([record("fn", "src/a.cryo", 3, "-", "a::T", "f")], False),
    (fn_with("SymbolStr", path="<stdlib>/a.cryo"), False),
    ([record("fn", "src/a.cryo", 3, "-", "a::T", "f", symbol="C$one"),
      record("param", "src/a.cryo", 3, "string", "a::T", "f:p", symbol="C$two")], "refused"),
]


def selftest():
    right = 0
    for records, want in SELFTEST_CASES:
        try:
            got = bool(declarations_taking_a_name(records))
        except SystemExit:
            got = "refused"
        if got == want:
            right += 1
        else:
            print(f"WRONG (want {want}, got {got}): {records[-1]}")
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
