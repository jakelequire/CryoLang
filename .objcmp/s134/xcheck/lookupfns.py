"""Write xcheck/lookupfns.txt: every lookup function the spelling-flow list
names, once each, for mapcheck.py."""
names = set()
for l in open("scripts/spelling-flow.outstanding.tsv", encoding="utf-8"):
    if not l.strip() or l.startswith("#"):
        continue
    c = l.rstrip("\n").split("\t")
    if len(c) > 4:
        names.add(c[4])
open(".objcmp/s134/xcheck/lookupfns.txt", "w", encoding="utf-8").write("\n".join(sorted(names)) + "\n")
print(len(names), "lookup functions")
