"""Remove each rule of `done.py --name-taking`'s reader in turn and show its
self-test fail on the case written for it.

Each mutation is applied to a throwaway copy of done.py (beside the real one,
so its imports resolve) and the copy's --selftest is run; the real file is
never touched.  An unmutated control runs first and must read all cases
right.

usage: python done_mutations.py
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DONE = os.path.join(os.path.dirname(HERE), "done.py")
COPY = os.path.join(os.path.dirname(HERE), "_done_mutant.py")

MUTATIONS = [
    ("control", None, None),
    ("drop SymbolStr from the spelling keys",
     'NAME_KEYS = ("string", "SymbolStr", "QualifiedName")',
     'NAME_KEYS = ("string", "QualifiedName")'),
    ("drop string from the spelling keys",
     'NAME_KEYS = ("string", "SymbolStr", "QualifiedName")',
     'NAME_KEYS = ("SymbolStr", "QualifiedName")'),
    ("drop QualifiedName from the spelling keys",
     'NAME_KEYS = ("string", "SymbolStr", "QualifiedName")',
     'NAME_KEYS = ("string", "SymbolStr")'),
    ("count ModulePath as a spelling",
     'NAME_KEYS = ("string", "SymbolStr", "QualifiedName")',
     'NAME_KEYS = ("string", "SymbolStr", "QualifiedName", "ModulePath")'),
    ("read every project's records, not compiler/src's",
     'not c[1].startswith(SRC_PREFIX)', 'False'),
    ("drop a parameter whose function has no record instead of refusing",
     "    return sorted(fns[k] for k in taking)\n",
     "    return sorted(fns[k] for k in taking if k in fns)\n"),
    ("list every function, keyed or not",
     "        elif c[9] in NAME_KEYS:\n", "        elif True:\n"),
]


def main():
    src = open(DONE, encoding="utf-8").read()
    bad = 0
    try:
        for label, old, new in MUTATIONS:
            text = src
            if old is not None:
                if src.count(old) != 1:
                    print("MUTATION DOES NOT APPLY: %s" % label)
                    bad += 1
                    continue
                text = src.replace(old, new)
                if label.startswith("drop a parameter"):
                    text = text.replace("    if missing:\n", "    if False:\n")
            with open(COPY, "w", encoding="utf-8") as fh:
                fh.write(text)
            p = subprocess.run([sys.executable, COPY, "--selftest"], capture_output=True, text=True)
            out = p.stdout.strip().splitlines()
            summary = out[-2] if len(out) >= 2 else p.stdout + p.stderr
            wrong = [l for l in out if l.startswith("WRONG")]
            ok = (old is None) == (not wrong)
            if not ok:
                bad += 1
            print("%-4s %-66s %s" % ("ok" if ok else "BAD", label, summary))
            for w in wrong:
                print("       " + w[:140])
    finally:
        if os.path.exists(COPY):
            os.remove(COPY)
    print("done_mutations: %s" % ("OK" if bad == 0 else "%d BAD" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
