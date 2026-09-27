"""python scripts/ns-migration/8.368/gate_mutations.py

Remove each counting rule from scripts/lane-gate.py, one at a time, and run
the self-test: every mutation must turn it red.  The gate is restored after
each run, whatever happens."""
import io, os, subprocess, sys, shutil

ROOT = subprocess.run(["git", "-C", os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--show-toplevel"],
                      stdout=subprocess.PIPE, text=True, check=True).stdout.strip()
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")
SELFTEST = os.path.join(ROOT, "scripts", "lane-gate-selftest.py")

MUTATIONS = [
    ("key test removed: every store call is name-keyed",
     "    return any(t.rsplit(\"::\", 1)[-1] in KEY_TYPES for t in TYPE_PATH_RE.findall(sig))",
     "    return True"),
    ("key test by substring, not by a type's last segment",
     "    return any(t.rsplit(\"::\", 1)[-1] in KEY_TYPES for t in TYPE_PATH_RE.findall(sig))",
     "    return any(k in sig for k in KEY_TYPES)"),
    ("key test reads parameters only, not the return",
     "    sig = text[text.index(\"(\"):] if \"(\" in text else \"\"",
     "    sig = text[text.index(\"(\"):text.index(\")\")] if \"(\" in text else \"\""),
    ("member test removed: a free function is read as a static",
     "    if \"-\" not in path.split(\"$F\", 1)[0]:\n        return None",
     "    pass"),
    ("receiver kind ignored: a write lands in the read row",
     "                counted.append((st.write if recv == \"write\" else st.read, site))",
     "                counted.append((st.read, site))"),
    ("static read as an instance write",
     "        return owner, meth, \"static\"",
     "        return owner, meth, \"write\""),
    ("LOOKUP names not singled out",
     "            if store == INDEX_TYPE and meth in LOOKUPS:",
     "            if False:"),
    ("a store's own file not excluded",
     "            if st.owns(rel):\n                continue",
     "            pass"),
    ("the resolver's owners not excluded from get_resolver",
     "                if not STORES[\"Resolver\"].owns(rel):\n                    counted.append((\"REENTRY\", site))",
     "                counted.append((\"REENTRY\", site))"),
    ("the path_of door dropped",
     "            if door == DEFID_PATH_DOOR:\n                counted.append((\"DEFID_PATH\", site))\n                continue",
     ""),
    ("the home-write door dropped",
     "            if door == HOME_WRITE_DOOR:\n                counted.append((\"HOME_WRITE\", site))\n                continue",
     ""),
    ("LOOKUP_LOCAL counts every other type's call",
     "        if meth in names:\n            counted.append((\"LOOKUP_LOCAL\", site))",
     "        counted.append((\"LOOKUP_LOCAL\", site))"),
    ("LOOKUP_LOCAL dropped",
     "        if meth in names:\n            counted.append((\"LOOKUP_LOCAL\", site))",
     "        pass"),
    ("unpinned calls dropped silently",
     "    unplaced = [(site[0], site[1], spelled) for site, meth, spelled in unpinned if meth in watched]",
     "    unplaced = []"),
    ("every unpinned call refused",
     "    unplaced = [(site[0], site[1], spelled) for site, meth, spelled in unpinned if meth in watched]",
     "    unplaced = [(site[0], site[1], spelled) for site, meth, spelled in unpinned]"),
    ("reach control removed",
     "    if missing:\n        raise SystemExit(\"lane-gate: no call in %s reaches",
     "    if False:\n        raise SystemExit(\"lane-gate: no call in %s reaches"),
    ("one call recorded twice counted twice",
     "    for kind, (rel, _line, _col) in sorted(set(counted)):",
     "    for kind, (rel, _line, _col) in sorted(counted):"),
    ("a call outside the tree counted under its own path",
     "            rel = by_lower.get(p.lower()) if p is not None else None",
     "            rel = by_lower.get(p.lower()) if p is not None else f[1]"),
    ("generic owner not flattened",
     "        if ch == \"<\":\n            depth += 1\n        elif ch == \">\":\n            depth -= 1\n        elif depth == 0:\n            flat.append(ch)\n    head = \"\".join(flat)\n    last",
     "        flat.append(ch)\n    head = \"\".join(flat)\n    last"),
    ("field count unchecked",
     "            if len(f) != 15:",
     "            if len(f) < 4:"),
    ("missing facts not refused",
     "    if not os.path.isfile(facts):\n        raise SystemExit",
     "    if False:\n        raise SystemExit"),
]


def main():
    original = io.open(GATE, encoding="utf-8", newline="").read()
    results = []
    try:
        for name, old, new in MUTATIONS:
            if original.count(old) != 1:
                results.append((name, "NOT APPLIED (%d matches)" % original.count(old)))
                continue
            io.open(GATE, "w", encoding="utf-8", newline="").write(original.replace(old, new, 1))
            p = subprocess.run([sys.executable, SELFTEST], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
            first = [l for l in p.stdout.splitlines() if l.startswith("case ") or l.startswith("baseline")
                     or l.startswith("--names") or l.startswith("missing")]
            verdict = "REFUSED (exit %d): %s" % (p.returncode, first[0][:110] if first else p.stdout.strip()[:110]) \
                if p.returncode != 0 else "PASSED - the self-test did not notice"
            results.append((name, verdict))
    finally:
        io.open(GATE, "w", encoding="utf-8", newline="").write(original)
    p = subprocess.run([sys.executable, SELFTEST], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for name, v in results:
        print("%-62s %s" % (name, v))
    print("restored gate, self-test:", p.stdout.strip()[:100])
    return 0 if all(v.startswith("REFUSED") for _n, v in results) else 1


if __name__ == "__main__":
    sys.exit(main())
