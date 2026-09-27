"""Old lane gate (source patterns) vs new (facts): every counted site, by bucket.

    python scripts/ns-migration/8.368/diff_lane.py [facts] [old-rev]

The old gate is `scripts/lane-gate.py` at `old-rev` (default ac4966d8, the
last commit counting from source), read from git.  Writes
lane_old_sites.tsv / lane_new_sites.tsv to the current directory and prints
the per-bucket totals and the site differences (file:line granularity; the
old gate knows lines, not columns)."""
import importlib.util, os, re, subprocess, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = subprocess.run(["git", "-C", HERE, "rev-parse", "--show-toplevel"],
                      stdout=subprocess.PIPE, text=True, check=True).stdout.strip()
SRC = os.path.join(ROOT, "compiler", "src")
OLD_REV = sys.argv[2] if len(sys.argv) > 2 else "ac4966d8"

# The old gate, with every tally increment also recording its site.
old_text = subprocess.run(["git", "-C", ROOT, "show", OLD_REV + ":scripts/lane-gate.py"],
                          stdout=subprocess.PIPE, text=True, check=True, encoding="utf-8").stdout
old_text = old_text.replace("tally[row] += 1", "tally[row] += 1; SITES.append((row, rel, lineno))")
old_text = old_text.replace('tally["LOOKUP_LOCAL"] += 1', 'tally["LOOKUP_LOCAL"] += 1; SITES.append(("LOOKUP_LOCAL", rel, lineno))')
old_text = old_text.replace('tally["REENTRY"] += len(REENTRY_RE.findall(line))',
                            'n_ = len(REENTRY_RE.findall(line)); tally["REENTRY"] += n_; SITES.extend([("REENTRY", rel, lineno)] * n_)')
old_text = old_text.replace('tally["DEFID_PATH"] += len(DEFID_PATH_RE.findall(line))',
                            'n_ = len(DEFID_PATH_RE.findall(line)); tally["DEFID_PATH"] += n_; SITES.extend([("DEFID_PATH", rel, lineno)] * n_)')
assert old_text.count("SITES.") == 6, old_text.count("SITES.")
old_text = "SITES = []\n" + old_text
old = type(sys)("lane_old")
old.__file__ = os.path.join(ROOT, "scripts", "lane-gate.py")
exec(compile(old_text, "lane_old", "exec"), old.__dict__)
old_found, old_unplaced, old_sets, _ = old.scan(SRC)
old_sites = collections.Counter((k, r, l) for k, r, l in old.SITES)

spec = importlib.util.spec_from_file_location("lane_new", os.path.join(ROOT, "scripts", "lane-gate.py"))
new = importlib.util.module_from_spec(spec)
spec.loader.exec_module(new)
facts = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".facts", "compiler.facts")
tree = new.Tree(SRC)
new_sites = collections.Counter()
orig = new.count_facts


def capture(tree, facts):
    found, unplaced, sets = orig(tree, facts)
    return found, unplaced, sets


# Re-run count_facts's placement with sites exposed: patch the aggregation.
src = open(os.path.join(ROOT, "scripts", "lane-gate.py"), encoding="utf-8").read()
m = re.search(r"def count_facts\(tree, facts\):.*?\n    return found, unplaced, sets\n", src, re.S)
body = m.group(0).replace("    return found, unplaced, sets\n", "    return found, unplaced, sets, counted\n")
exec(compile(body, "count_facts_sites", "exec"), new.__dict__)
found, unplaced, sets, counted = new.count_facts(tree, facts)
for kind, (rel, line, col) in sorted(set(counted)):
    new_sites[(kind, rel, line)] += 1

with open(os.path.join(os.getcwd(), "lane_old_sites.tsv"), "w", newline="\n") as fh:
    for (k, r, l), n in sorted(old_sites.items()):
        fh.write("%s\t%s\t%d\t%d\n" % (k, r, l, n))
with open(os.path.join(os.getcwd(), "lane_new_sites.tsv"), "w", newline="\n") as fh:
    for (k, r, l), n in sorted(new_sites.items()):
        fh.write("%s\t%s\t%d\t%d\n" % (k, r, l, n))

print("old unplaced:", len(old_unplaced), " new unplaced:", len(unplaced))
for u in unplaced[:10]:
    print("   new unplaced", u)
print("%-16s %6s %6s" % ("bucket", "old", "new"))
for k in new.KINDS:
    o = sum(n for (kk, _r, _l), n in old_sites.items() if kk == k)
    n_ = sum(n for (kk, _r, _l), n in new_sites.items() if kk == k)
    print("%-16s %6d %6d%s" % (k, o, n_, "" if o == n_ else "   <-- %+d" % (n_ - o)))
print()
keys = sorted(set(old_sites) | set(new_sites))
diffs = [(k, old_sites.get(k, 0), new_sites.get(k, 0)) for k in keys if old_sites.get(k, 0) != new_sites.get(k, 0)]
print("site differences:", len(diffs))
for (k, r, l), o, n_ in diffs:
    print("%-14s %s:%d  old %d new %d" % (k, r, l, o, n_))
