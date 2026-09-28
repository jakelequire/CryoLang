"""Remove each rule the lane gate's declaration reader applies, in turn, and
show scripts/lane-gate-selftest.py fail on the case written for it.

Each mutation rewrites scripts/lane-gate.py in place (the self-test loads
the gate by that path), runs the self-test, and restores the file byte for
byte in a `finally`; an unmutated control runs first and must read OK.

usage: python gate_mutations.py
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")
SELFTEST = os.path.join(ROOT, "scripts", "lane-gate-selftest.py")

# (label, [(old, new)], a case name the self-test must report failing)
MUTATIONS = [
    ("control", [], None),
    ("a map field no longer makes a map owner",
     [('if short_type(ty) in ("HashMap", "HashSet"):', 'if False:')],
     "the audit's mutation"),
    ("an array of `string` is no array of names",
     [('if elem == "string" or (', 'if False or (')],
     "an array of `string` names"),
    ("a pair headed by a name is no array of names",
     [('return short_type(elem) == "Pair" and', 'return False and')],
     "audit 14's m8"),
    ("a trait impl's methods are read as the type's own",
     [('and not f[8].startswith("C$tr$"):\n                methods.setdefault((f[12], f[13], f[8]), [])',
       ':\n                methods.setdefault((f[12], f[13], f[8]), [])'),
      ('and not f[8].startswith("C$tr$"):\n                methods.setdefault((f[12], f[13].split',
       ':\n                methods.setdefault((f[12], f[13].split')],
     "a trait impl's method"),
    ("a method taking an array of keys counts as taking a key",
     [('key in KEY_TYPES and not ty.endswith("[]") for ty, key in params',
       'key in KEY_TYPES for ty, key in params')],
     "a method taking an ARRAY"),
    ("a record array counts whatever its element's fields carry",
     [('if any(k in KEY_TYPES for k in self.field_keys.get(elem, {}).values()):',
       'if True:')],
     "an array of records with no key-typed field"),
    ("a class's base is not read from its type record",
     [('if f[5] not in ("-", "?"):', 'if False:')],
     "an array read through a derived class"),
    ("the file a type is declared in is not read from its type record",
     [('self.declared_in.setdefault(name, set()).add(rel)', 'pass')],
     # Every scanned entry's owner then reads as declared nowhere, so the
     # baseline itself is refused.
     "is declared in no file"),
    ("a record naming a file outside the tree is dropped, not refused",
     [('if rel is None:\n                    raise', 'if rel is None:\n                    continue\n                    raise')],
     "declaration records naming a file the tree does not hold"),
]


def main():
    original = open(GATE, "rb").read()
    text = original.decode("utf-8")
    crlf = "\r\n" in text
    text = text.replace("\r\n", "\n")
    bad = 0
    try:
        for label, edits, case in MUTATIONS:
            mutated = text
            applies = True
            for old, new in edits:
                if mutated.count(old) != 1:
                    applies = False
                    break
                mutated = mutated.replace(old, new)
            if not applies:
                print("BAD  %-62s the mutation does not apply" % label)
                bad += 1
                continue
            out = mutated.replace("\n", "\r\n") if crlf else mutated
            open(GATE, "wb").write(out.encode("utf-8"))
            p = subprocess.run([sys.executable, SELFTEST], capture_output=True, text=True)
            report = p.stdout + p.stderr
            if case is None:
                ok = p.returncode == 0
                why = report.strip().splitlines()[-1] if report.strip() else ""
            else:
                ok = p.returncode != 0 and case in report
                why = "fails `%s`" % case if ok else "did not fail `%s`" % case
            if not ok:
                bad += 1
            print("%-4s %-62s %s" % ("ok" if ok else "BAD", label, why[:110]))
            if not ok:
                for line in report.splitlines():
                    if line.startswith("case ") or line.startswith("baseline") or "FAILURE" in line:
                        print("       " + line[:150])
    finally:
        open(GATE, "wb").write(original)
    print("gate_mutations: %s" % ("OK" if bad == 0 else "%d BAD" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
