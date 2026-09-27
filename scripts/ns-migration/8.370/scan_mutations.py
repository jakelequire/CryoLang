#!/usr/bin/env python3
"""Remove each rule of the lane gate's facts-read scan placement (rule 1c)
alone, and show a self-test refusing it.

    python scripts/ns-migration/8.370/scan_mutations.py

Each mutation is applied ALONE to scripts/lane-gate.py, then
`lane-gate-selftest.py` and `ns-migration/residue_selftest.py` run, and the
file is restored before the next.  A mutation neither self-test refuses is
a rule no case covers.  Exit 0 when every mutation is refused.
"""
import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NS = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(NS))
TARGET = os.path.join(ROOT, "scripts", "lane-gate.py")

# (rule, text in the gate, replacement)
MUTATIONS = [
    ("a record outside every loop is no scan",
     "    if depth == 0:\n        return None\n    segs = prov.split", "    segs = prov.split"),
    ("only the innermost loop's element is the array searched",
     "        if cur != depth:\n            return None\n        if prev is not None",
     "        if prev is not None"),
    ("a value through a call is the call's",
     "            src = src[m.end():]\n        if src.startswith(\"call:\"):\n            return None\n        m = FACTS_ELEMENT",
     "            src = src[m.end():]\n        m = FACTS_ELEMENT"),
    ("the member read before the element is named without its local",
     "        if m is None:\n            prev = src\n", "        if m is None:\n            prev = seg\n"),
    ("a join is a read of each element",
     "                        names.append((rel, lineno) + label(hit) + (ops[1 - side],))\n",
     "                        names.append((rel, lineno) + label(hit) + (ops[1 - side],))\n                        break\n"),
    ("two members of one element are one read",
     "                    found = [None, found[1]]\n", "                    pass\n"),
    ("an array is named by the class declaring it",
     "            owner = tree.declaring_type(owner, field)\n", "            pass\n"),
    ("a key type's `equals` is a comparison",
     "(kind == \"arg\" and FACTS_KEY_EQUALS.match(f[12]))", "False"),
    ("an unplaced array is refused",
     "        if entry is None:\n            problems.append(\n",
     "        if entry is None:\n            continue\n            problems.append(\n"),
    ("the element must be the entry's",
     "        if elems != [entry.elem]:", "        if False:"),
    ("the owner must be declared where the entry says, with the array",
     "        if entry.defn not in declared or field not in tree.fields.get(owner, {}):", "        if False:"),
    ("an entry nothing scans is stale",
     "        if label not in seen:\n            problems.append(\"  `%s` is listed in SCANNED_ARRAYS but nothing",
     "        if False:\n            problems.append(\"  `%s` is listed in SCANNED_ARRAYS but nothing"),
    ("an identity entry scanned by a name is refused",
     "            if label in seen:\n                rel, lineno, _elem = seen[label][0]",
     "            if False:\n                rel, lineno, _elem = seen[label][0]"),
    ("an identity entry nothing scans by identity is stale",
     "            if label not in identity:", "            if False:"),
]


def run(path):
    p = subprocess.run([sys.executable, path], capture_output=True, text=True, cwd=ROOT)
    return p.returncode, (p.stdout + p.stderr).strip().splitlines()


def main():
    orig = io.open(TARGET, encoding="utf-8", newline="").read()
    missed = 0
    try:
        for name, old, new in MUTATIONS:
            if orig.count(old) != 1:
                print("MUTATION TEXT NOT FOUND ONCE (%d): %s" % (orig.count(old), name))
                missed += 1
                continue
            io.open(TARGET, "w", encoding="utf-8", newline="").write(orig.replace(old, new))
            lane_code, lane = run(os.path.join(ROOT, "scripts", "lane-gate-selftest.py"))
            res_code, res = run(os.path.join(NS, "residue_selftest.py"))
            io.open(TARGET, "w", encoding="utf-8", newline="").write(orig)
            print("== %s" % name)
            for tag, code, lines in (("lane", lane_code, lane), ("residue", res_code, res)):
                cases = [l for l in lines if l.startswith("case ") or l.startswith("facts mutation")
                         or l.startswith("baseline") or l.startswith("list mutation")]
                print("   %s self-test exit %d%s" % (tag, code, (": " + cases[0][:150]) if cases else ""))
            if lane_code == 0 and res_code == 0:
                missed += 1
    finally:
        io.open(TARGET, "w", encoding="utf-8", newline="").write(orig)
    print("scan_mutations: %d of %d refused by a self-test" % (len(MUTATIONS) - missed, len(MUTATIONS)))
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
