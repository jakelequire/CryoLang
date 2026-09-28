"""Plant each sealed-type breach in the real tree, alone, and run both checks.

    python scripts/ns-migration/8.399/sealed_mutations.py --cryo compiler/build/cryo.exe

For each mutation of `DefId` in `compiler/src/compiler/resolver/res.cryo`:
apply it, regenerate the compiler's facts (`scripts/facts.py compiler`),
run `sealed_side_by_side.py` (the old source-reading check and the new
record-reading one), restore the file.  Then the facts are regenerated from
the restored tree and the side by side runs once more as the control.  The
output is each run's verdict lines for `DefId` and its summary.
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
RES = os.path.join(ROOT, "compiler", "src", "compiler", "resolver", "res.cryo")

INVALID = """    static invalid() -> DefId {
        return DefId { index: 4294967295, table: 0 };
    }
"""
FORGE = """
    static forge(n: u32) -> DefId {
        return DefId { index: n, table: 0 };
    }
"""
AFTER_BLOCK = """/// What kind of declaration a table entry is.  Recorded at registration from"""
IMPL = """implement struct DefId {
    static forge(n: u32) -> DefId {
        return DefId { index: n, table: 0 };
    }
}

"""
MUTATIONS = [
    ("M1 a public static minting DefId inside its own block (both must refuse)",
     INVALID, INVALID + FORGE),
    ("M2 the same static in an `implement struct DefId` block (the old check reads only the type's block)",
     AFTER_BLOCK, IMPL + AFTER_BLOCK),
    ("M3 DefId's private label dropped, every field public (both must refuse)",
     "type struct DefId {\nprivate:\n", "type struct DefId {\n"),
]


def run(cmd):
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def facts_and_compare(cryo, label):
    code, out = run([sys.executable, "scripts/facts.py", "--cryo", cryo, "compiler"])
    if code != 0:
        print(label, "\n    facts FAILED:\n" + out)
        return
    code, out = run([sys.executable, os.path.join(HERE, "sealed_side_by_side.py")])
    lines = out.splitlines()
    print(label)
    for i, l in enumerate(lines):
        if l.endswith("::res::DefId"):
            print("   ", l)
            print("   ", lines[i + 1])
            print("   ", lines[i + 2])
    print("   ", lines[-1] if lines else "(no output)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    cryo = os.path.abspath(ap.parse_args().cryo)
    keep = open(RES, "rb").read()
    text = keep.decode("utf-8")
    try:
        for label, old, new in MUTATIONS:
            assert text.count(old) == 1, (label, text.count(old))
            open(RES, "wb").write(text.replace(old, new, 1).encode("utf-8"))
            try:
                facts_and_compare(cryo, label)
            finally:
                open(RES, "wb").write(keep)
    finally:
        open(RES, "wb").write(keep)
    facts_and_compare(cryo, "control: the tree unmutated")


main()
