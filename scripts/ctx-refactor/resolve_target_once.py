"""Read the build's target triple, resolved once, instead of resolving it
at each use.

`BuildOptions.target_triple` holds the effective triple (the host's when no
target was asked for), so every
`LTargetMachine::resolve_effective_triple(Text::new(<recv>.options.target_triple))`
is that field.

    python scripts/ctx-refactor/resolve_target_once.py [--check]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAT = re.compile(r"LTargetMachine::resolve_effective_triple\(Text::new\(([\w.]+?\.options\.target_triple)\)\)")


def main():
    check = "--check" in sys.argv
    total = 0
    for dp, _, fns in os.walk(os.path.join(ROOT, "compiler", "src")):
        for fn in fns:
            if not fn.endswith(".cryo"):
                continue
            path = os.path.join(dp, fn)
            text = open(path, "rb").read().decode("utf-8")
            new, n = PAT.subn(r"\1", text)
            if n:
                total += n
                print("%s: %d" % (os.path.relpath(path, ROOT).replace(os.sep, "/"), n))
                if not check:
                    open(path, "wb").write(new.encode("utf-8"))
    print("rewritten: %d" % total)


if __name__ == "__main__":
    main()
