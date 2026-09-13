#!/usr/bin/env python3
"""Rewrite bare intrinsic calls to their module-qualified form.

An intrinsic is declared in `std::core::intrinsics` and reached behind that
path (`intrinsics::atomic_fence(..)`); a bare leaf names the user's function
or nothing.  This rewrites `name(` to `intrinsics::name(` for the names given,
in the files given, when the leaf is not already qualified, not a member access,
not a declaration, and the file imports `std::core::intrinsics`.

    python scripts/qualify-intrinsic-calls.py <file>... --names atomic_fence,ptr_diff
    python scripts/qualify-intrinsic-calls.py stdlib/sync/atomic.cryo --names @stdlib/core/intrinsics.cryo

`--names @<intrinsics file>` takes every `intrinsic function` declared there.
Prints one line per file: `<file> <n> rewritten`.  Exits 1 when a listed file
does not import the module, since the qualified call would then not resolve.
"""
import re
import sys


def names_from(arg: str) -> list[str]:
    if arg.startswith("@"):
        src = open(arg[1:], encoding="utf-8").read()
        return re.findall(r"^intrinsic function (\w+)\(", src, re.M)
    return [n for n in arg.split(",") if n]


def main(argv: list[str]) -> int:
    if "--names" not in argv:
        print(__doc__)
        return 2
    i = argv.index("--names")
    names = names_from(argv[i + 1])
    files = argv[:i] + argv[i + 2:]
    alt = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    # Not preceded by `::`, `.`, an identifier character, or `function `.
    pat = re.compile(r"(?<![\w:.])(?<!function )(" + alt + r")\(")
    rc = 0
    for f in files:
        raw = open(f, encoding="utf-8", newline="").read()
        if not re.search(r"^import std::core::intrinsics;", raw, re.M):
            print(f"{f}: does not import std::core::intrinsics", file=sys.stderr)
            rc = 1
            continue
        n = 0

        def sub(m: re.Match) -> str:
            nonlocal n
            n += 1
            return "intrinsics::" + m.group(1) + "("

        out = []
        for line in raw.split("\n"):
            code = line.split("//", 1)[0]
            rest = line[len(code):]
            out.append(pat.sub(sub, code) + rest)
        new = "\n".join(out)
        if new != raw:
            open(f, "w", encoding="utf-8", newline="").write(new)
        print(f"{f} {n} rewritten")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
