"""python scripts/ns-migration/8.399/sealed_gate_mutations.py

Remove each rule of the lane gate's sealed-type check from scripts/lane-gate.py,
one at a time, and run the self-test: every mutation must turn it red, on a
sealed-type case.  The gate is restored after each run, whatever happens."""
import os, subprocess, sys

ROOT = subprocess.run(["git", "-C", os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--show-toplevel"],
                      stdout=subprocess.PIPE, text=True, check=True).stdout.strip()
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")
SELFTEST = os.path.join(ROOT, "scripts", "lane-gate-selftest.py")

MUTATIONS = [
    ("the private-field rule removed",
     "        if not private:\n",
     "        if False:\n"),
    ("the minting-static rule removed",
     "            for mint in mints:\n",
     "            for mint in []:\n"),
    ("a static taking no argument read as a mint",
     "                  and int(f[4]) > 0 and f[5] == f[12]):\n",
     "                  and f[5] == f[12]):\n"),
    ("a static returning another type read as a mint",
     "                  and int(f[4]) > 0 and f[5] == f[12]):\n",
     "                  and int(f[4]) > 0):\n"),
    ("a method read as a static",
     "            elif (f[0] == \"fn\" and f[7] == \"static\" and f[6] == \"public\"\n",
     "            elif (f[0] == \"fn\" and f[6] == \"public\"\n"),
    ("visibility ignored for a static",
     "            elif (f[0] == \"fn\" and f[7] == \"static\" and f[6] == \"public\"\n",
     "            elif (f[0] == \"fn\" and f[7] == \"static\"\n"),
    ("a store no longer exempt",
     "        if path not in SEALED_STORES:\n",
     "        if True:\n"),
    ("only the type's own file read for its statics",
     "                mints.setdefault(f[12], []).append(",
     "                if f[1] == \"src/compiler/sealed.cryo\": mints.setdefault(f[12], []).append("),
    ("a stale entry skipped silently",
     "            problems.append(\"  `%s` is listed in SEALED_TYPES but the compiler declares no such type: \"\n"
     "                            \"a stale entry\" % path)\n",
     "            pass\n"),
]


def main():
    keep = open(GATE, "rb").read()
    text = keep.decode("utf-8")
    bad = 0
    try:
        for label, old, new in MUTATIONS:
            if text.count(old) != 1:
                print("NOT APPLIED %s: the gate text to replace appears %d times" % (label, text.count(old)))
                bad += 1
                continue
            open(GATE, "wb").write(text.replace(old, new, 1).encode("utf-8"))
            try:
                p = subprocess.run([sys.executable, SELFTEST], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
            finally:
                open(GATE, "wb").write(keep)
            red = p.returncode != 0
            cases = [l.split(":")[0] for l in p.stdout.splitlines() if l.startswith("case ")]
            print("%-5s %s%s" % ("RED" if red else "GREEN", label,
                                 (" -- " + ", ".join(cases)) if cases else ""))
            bad += not red
    finally:
        open(GATE, "wb").write(keep)
    p = subprocess.run([sys.executable, SELFTEST], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print("control, the gate restored:", p.stdout.strip().splitlines()[-1] if p.stdout.strip() else p.returncode)
    return 1 if bad or p.returncode != 0 else 0


sys.exit(main())
