"""The move checker's state: the old pattern checks and the declaration records,
over the tree and over the tree with a spelling planted in the move state.

    python scripts/ns-migration/8.399/move_state_pair.py --cryo compiler/build/cryo.exe

The mutation adds `last_moved: SymbolStr` to `MoveChecker` (move state kept by
a binding's SPELLING) and initializes it.  For the tree and for the mutation,
the compiler's facts are regenerated (`scripts/facts.py compiler`) and both
kinds of check run:

  OLD  `grep -c 'name\\.id' move_check.cryo` (expect 0) and
       `grep -c 'moved_keys:  u64\\[\\]' move_check.cryo` (expect 1)
  NEW  the `field` records of `MoveChecker` whose key column is not `-`
       (expect 0), and `moved_keys`' declared type (expect `u64[]`)

The file is restored after the mutation and the facts regenerated from the
restored tree.
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
MC = os.path.join(ROOT, "compiler", "src", "compiler", "passes", "move_check.cryo")
FACTS = os.path.join(ROOT, ".facts", "compiler.facts")
OWNER = "compiler::passes::move_check::MoveChecker"
EDITS = [("    fatal: i64;\n", "    fatal: i64;\n    last_moved: SymbolStr;\n"),
         ("            fatal: 0,\n", "            fatal: 0,\n            last_moved: SymbolStr::empty(),\n")]


def facts(cryo):
    p = subprocess.run([sys.executable, "scripts/facts.py", "--cryo", cryo, "compiler"], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode == 0, p.stdout.strip()


def measure(label):
    src = open(MC, encoding="utf-8").read()
    old_id = len([l for l in src.splitlines() if re.search(r"name\.id", l)])
    old_mk = len([l for l in src.splitlines() if "moved_keys:  u64[]" in l])
    keyed, mk_type = [], "?"
    with open(FACTS, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if f[0] == "field" and f[12] == OWNER:
                if f[9] != "-":
                    keyed.append("%s: %s" % (f[13], f[5]))
                if f[13] == "moved_keys":
                    mk_type = f[5]
    old_ok = old_id == 0 and old_mk == 1
    new_ok = not keyed and mk_type == "u64[]"
    print(label)
    print("    OLD  name\\.id lines %d, `moved_keys:  u64[]` lines %d  -> %s" % (old_id, old_mk, "OK" if old_ok else "DRIFT"))
    print("    NEW  key-carrying fields %s, moved_keys is %s  -> %s" % (keyed or "none", mk_type, "OK" if new_ok else "DRIFT"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    cryo = os.path.abspath(ap.parse_args().cryo)
    keep = open(MC, "rb").read()
    text = keep.decode("utf-8")
    try:
        mutated = text
        for old, new in EDITS:
            assert mutated.count(old) == 1, old
            mutated = mutated.replace(old, new, 1)
        open(MC, "wb").write(mutated.encode("utf-8"))
        ok, out = facts(cryo)
        if not ok:
            print("mutation: facts FAILED\n" + out)
        else:
            measure("mutation: `last_moved: SymbolStr` in MoveChecker")
    finally:
        open(MC, "wb").write(keep)
    ok, out = facts(cryo)
    if not ok:
        print("control: facts FAILED\n" + out)
        return 1
    measure("control: the tree")
    return 0


sys.exit(main())
