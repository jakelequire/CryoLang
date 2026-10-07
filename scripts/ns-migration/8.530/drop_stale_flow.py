"""Drop from scripts/spelling-flow.outstanding.tsv every line a check-fast
log names as `stale`.  The log prints the tsv's fields separated by two
spaces; each named line must match exactly one tsv line, or nothing is
written.
usage: drop_stale_flow.py <check-fast log>"""
import sys

TSV = "scripts/spelling-flow.outstanding.tsv"
stale = []
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    s = line.strip()
    if s.startswith("stale  "):
        stale.append(s[len("stale  "):].split("  "))
with open(TSV, encoding="utf-8", newline="") as f:
    rows = f.read().splitlines(keepends=True)
keep, hits = [], {}
for r in rows:
    fields = r.rstrip("\r\n").split("\t")
    m = [i for i, s in enumerate(stale) if s == fields]
    if m:
        hits[m[0]] = hits.get(m[0], 0) + 1
        continue
    keep.append(r)
bad = [i for i in range(len(stale)) if hits.get(i, 0) != 1]
if bad:
    sys.exit("unmatched or duplicated stale entries: %s" % [stale[i] for i in bad])
with open(TSV, "w", encoding="utf-8", newline="") as f:
    f.write("".join(keep))
print("dropped %d of %d lines" % (len(rows) - len(keep), len(rows)))
