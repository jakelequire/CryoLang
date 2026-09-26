#!/usr/bin/env python3
"""Run the approved list's checks: every function that still takes a name,
with the written reason it is correct and a command that fails when the
reason stops being true.

The list is `scripts/ns-migration/approved-names.tsv`, one entry per line:

    <signature> TAB <reason> TAB <command>

A command is run from the repository root; it passes by exiting 0.  The
commands are this script's own assertions, so the list needs no shell:

  --signature FILE TEXT     FILE declares TEXT verbatim - the entry's
                            parameter types are what its reason relies on
  --owner-is-identity       every variant of `FamilyOwner` carries a
                            `DefId` or a `TypeRef`, never a spelling
  --no-composed-leaf NAME   no call of NAME in compiler/src builds an
                            argument by formatting or by joining `::` in
                            the call's own text

Several may be given in one command; all must hold.

Usage:
    python scripts/ns-migration/approved.py --check
    python scripts/ns-migration/approved.py --selftest
"""
import argparse
import io
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIST = os.path.join(ROOT, "scripts", "ns-migration", "approved-names.tsv")
RES = "compiler/src/compiler/resolver/res.cryo"
SRC = "compiler/src"


def read(root, rel):
    with io.open(os.path.join(root, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def check_signature(root, rel, text):
    try:
        body = read(root, rel)
    except OSError:
        return "%s cannot be read" % rel
    if text not in body:
        return "%s no longer declares `%s`" % (rel, text)
    return None


def check_owner_is_identity(root):
    try:
        body = read(root, RES)
    except OSError:
        return "%s cannot be read" % RES
    m = re.search(r"type enum FamilyOwner \{(.*?)\n\}", body, re.S)
    if not m:
        return "%s declares no `FamilyOwner`" % RES
    variants = re.findall(r"^\s*(\w+)\(([^)]*)\);", m.group(1), re.M)
    if not variants:
        return "`FamilyOwner` has no variant carrying a payload"
    bad = [(v, p) for v, p in variants if p.strip() not in ("DefId", "TypeRef")]
    if bad:
        return "`FamilyOwner::%s(%s)` carries something other than an identity" % bad[0]
    return None


def calls(root, name):
    """(rel, line, statement text) for every call of `.name(` or `::name(`."""
    out = []
    pat = re.compile(r"[.:]" + re.escape(name) + r"\(")
    for dirpath, _, files in os.walk(os.path.join(root, SRC)):
        for f in files:
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dirpath, f)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            lines = read(root, rel).splitlines()
            for i, line in enumerate(lines):
                code = line.split("//", 1)[0]
                for m in pat.finditer(code):
                    # the call's own text, to the end of its statement
                    text, j = code[m.end():], i
                    while ";" not in text and "{" not in text and j + 1 < len(lines) and j - i < 6:
                        j += 1
                        text += " " + lines[j].split("//", 1)[0]
                    out.append((rel, i + 1, text.split(";", 1)[0]))
    return out


# A name built at the call site: formatted, joined with `::`, or a whole
# definition path read back as text.  A helper that returns one is not
# followed - only what the call's own text and its arguments' bindings show.
COMPOSED = re.compile(r'fmt::format\(|"::"|\bpath_of\(|\blookup_type_name\(|\bmodule_qualified_path\(')
IDENT = re.compile(r"\b([a-z_][a-z0-9_]*)\b")


def binding_texts(lines, before, ident):
    """The value of EVERY binding of `ident` above line index `before` -
    `const|mut ident: T = ...;` and each `ident = ...;` - in the same
    function (the search stops at the function's header).  Every one, not
    the nearest: a branch that binds it differently is still a value the
    call can receive."""
    decl = re.compile(r"(?:\b(?:const|mut)\s+" + re.escape(ident) + r"\s*:[^=]*|^\s*"
                      + re.escape(ident) + r"\s*)=(?!=)(.*)")
    out = []
    for j in range(before - 1, max(-1, before - 200), -1):
        code = lines[j].split("//", 1)[0]
        # A body's statements are indented past a method's four columns:
        # a line at four or fewer is its header, or the previous one's end.
        if code.strip() and len(code) - len(code.lstrip()) <= 4:
            break
        m = decl.search(code)
        if m:
            text, k = m.group(1), j
            while ";" not in text and k + 1 < len(lines) and k - j < 6:
                k += 1
                text += " " + lines[k].split("//", 1)[0]
            out.append(text.split(";", 1)[0])
    return out


def check_no_composed_leaf(root, name):
    found = calls(root, name)
    if not found:
        return "no call of `%s` in %s; the check measures nothing" % (name, SRC)
    for rel, line, text in found:
        if COMPOSED.search(text):
            return "%s:%d composes an argument of `%s`: %s" % (rel, line, name, text.strip()[:100])
        # An argument that is a local: the composition may sit in its
        # binding, or in the binding of a local that one reads (three hops).
        lines = read(root, rel).splitlines()
        frontier, seen = [(t, 0) for t in set(IDENT.findall(text))], set()
        while frontier:
            ident, hops = frontier.pop()
            if ident in seen or hops >= 3:
                continue
            seen.add(ident)
            for init in binding_texts(lines, line - 1, ident):
                if COMPOSED.search(init):
                    return "%s:%d passes `%s` to `%s`, composed where it is bound: %s" % (
                        rel, line, ident, name, init.strip()[:100])
                frontier += [(t, hops + 1) for t in set(IDENT.findall(init))]
    return None


def run_assertions(argv, root):
    """Run one command's assertions; the problems found."""
    problems = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--signature" and i + 2 < len(argv):
            problems.append(check_signature(root, argv[i + 1], argv[i + 2]))
            i += 3
        elif a == "--owner-is-identity":
            problems.append(check_owner_is_identity(root))
            i += 1
        elif a == "--no-composed-leaf" and i + 1 < len(argv):
            problems.append(check_no_composed_leaf(root, argv[i + 1]))
            i += 2
        else:
            return ["unknown assertion `%s`" % a]
    return [p for p in problems if p]


def entries(path):
    out = []
    with io.open(path, encoding="utf-8") as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 3 or not all(f.strip() for f in fields):
                out.append((n, line, None, None))
                continue
            out.append((n, fields[0], fields[1], fields[2]))
    return out


def check(path, root):
    rows = entries(path)
    if not rows:
        print("approved: FAIL -- %s lists nothing" % os.path.relpath(path, root))
        return 1
    bad = 0
    for n, sig, reason, command in rows:
        if reason is None:
            print("  FAIL line %d: not three tab-separated fields (signature, reason, command)" % n)
            bad += 1
            continue
        argv = shlex.split(command)
        if argv[:2] != ["python", "scripts/ns-migration/approved.py"]:
            print("  FAIL line %d: the command is not this checker's: %s" % (n, command))
            bad += 1
            continue
        problems = run_assertions(argv[2:], root)
        print("  %-4s %s" % ("ok" if not problems else "FAIL", sig))
        for p in problems:
            print("         %s" % p)
        bad += bool(problems)
    print("approved: %s -- %d entr(ies), %d failing"
          % ("OK" if not bad else "FAIL", len(rows), bad))
    return 1 if bad else 0


def selftest():
    """Every assertion through a throwaway tree, refusing and allowing."""
    tmp = tempfile.mkdtemp(prefix="approved-selftest-")
    try:
        def tree(owner_variants, call_line):
            shutil.rmtree(tmp, ignore_errors=True)
            os.makedirs(os.path.join(tmp, "compiler/src/compiler/resolver"))
            with io.open(os.path.join(tmp, RES), "w", encoding="utf-8") as fh:
                fh.write("type enum FamilyOwner {\n%s\n}\n" % owner_variants)
            with io.open(os.path.join(tmp, "compiler/src/a.cryo"), "w", encoding="utf-8") as fh:
                fh.write("    lookup(&this, owner: FamilyOwner, leaf: SymbolStr) -> X {\n    }\n")
                fh.write("    g() -> void {\n        const fam: SymbolStr = intern(fmt::format(\"%s::%s\", a, b));\n    }\n")
                fh.write("    f(owner: FamilyOwner,\n      fam: SymbolStr) -> void {\n        %s\n    }\n" % call_line)

        good_owner = "    Def(DefId);\n    Type(TypeRef);"
        good_call = "const e: X = di.lookup(owner, leaf);"
        cases = [
            ("all three hold", good_owner, good_call,
             ["--signature", "compiler/src/a.cryo", "lookup(&this, owner: FamilyOwner, leaf: SymbolStr)",
              "--owner-is-identity", "--no-composed-leaf", "lookup"], 0),
            ("the signature changed", good_owner, good_call,
             ["--signature", "compiler/src/a.cryo", "lookup(&this, owner: SymbolStr, leaf: SymbolStr)"], 1),
            ("an owner variant carries a spelling", "    Def(DefId);\n    Named(SymbolStr);", good_call,
             ["--owner-is-identity"], 1),
            ("a call formats its leaf", good_owner,
             'const e: X = di.lookup(owner, intern(fmt::format("%s::%s", a, b)));',
             ["--no-composed-leaf", "lookup"], 1),
            ("a call joins `::` over two lines", good_owner,
             'const e: X = di.lookup(owner,\n        intern(a + "::" + b));',
             ["--no-composed-leaf", "lookup"], 1),
            ("no call exists to check", good_owner, "const e: X = other(owner, leaf);",
             ["--no-composed-leaf", "lookup"], 1),
            ("a local composed before the call", good_owner,
             'const fam: SymbolStr = intern(fmt::format("%s::%s", a, b));\n'
             '        const e: X = di.lookup(owner, fam);',
             ["--no-composed-leaf", "lookup"], 1),
            ("a local reassigned a composed name", good_owner,
             'mut fam: SymbolStr = leaf;\n'
             '        fam = intern(a + "::" + b);\n'
             '        const e: X = di.lookup(owner, fam);',
             ["--no-composed-leaf", "lookup"], 1),
            ("composed two bindings back", good_owner,
             'const text: string = fmt::format("%s::%s", a, b);\n'
             '        const fam: SymbolStr = intern(text);\n'
             '        const e: X = di.lookup(owner, fam);',
             ["--no-composed-leaf", "lookup"], 1),
            ("a definition's whole path as the leaf", good_owner,
             'const e: X = di.lookup(owner, defs.path_of(d));',
             ["--no-composed-leaf", "lookup"], 1),
            ("a composed binding in the other branch", good_owner,
             'mut fam: SymbolStr = intern(fmt::format("%s::%s", a, b));\n'
             '        if (c) { fam = leaf_of(d); }\n'
             '        const e: X = di.lookup(owner, fam);',
             ["--no-composed-leaf", "lookup"], 1),
            ("a local bound plainly before the call", good_owner,
             'const fam: SymbolStr = scope.member_name;\n'
             '        const e: X = di.lookup(owner, fam);',
             ["--no-composed-leaf", "lookup"], 0),
        ]
        ok = 0
        for label, owner, call, argv, want in cases:
            tree(owner, call)
            got = len(run_assertions(argv, tmp))
            hit = (got > 0) == (want > 0)
            ok += hit
            print("  %-4s %-40s %d problem(s)" % ("ok" if hit else "FAIL", label, got))
        print("approved selftest: %d of %d cases as expected" % (ok, len(cases)))
        return 0 if ok == len(cases) else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--list", default=LIST)
    ap.add_argument("--root", default=ROOT,
                    help="the tree the assertions read (default: this checkout)")
    args, rest = ap.parse_known_args()
    if args.selftest:
        return selftest()
    if args.check:
        return check(args.list, args.root)
    if rest:
        problems = run_assertions(rest, args.root)
        for p in problems:
            print("approved: %s" % p)
        return 1 if problems else 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
