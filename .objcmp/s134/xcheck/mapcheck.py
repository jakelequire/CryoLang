"""Map each spelling-flow lookup function to the index; list the unmapped."""
import json
idx = json.load(open(".objcmp/s134/xcheck/index.json"))
byq = {}
for f in idx:
    byq.setdefault(f["q"], []).append(f)
    if f["owner"]:
        byq.setdefault(f["q"].rsplit(".", 1)[0] + "::" + f["name"], []).append(f)
names = [l.strip() for l in open(".objcmp/s134/xcheck/lookupfns.txt") if l.strip()]
un = [n for n in names if n not in byq]
for n in un:
    print("UNMAPPED", n)
print(len(names) - len(un), "of", len(names), "mapped")
