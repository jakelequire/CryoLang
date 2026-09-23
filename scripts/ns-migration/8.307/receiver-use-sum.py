# Sum receiver-use-instrument.py's per-compilation totals, and tally its C/D lines.
# usage: python scripts/ns-migration/8.307/receiver-use-sum.py <file with SHADOW lines (any prefix)>
import re, sys, collections
tot = collections.defaultdict(lambda: [0, 0, 0, 0])
lines = collections.Counter()
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.search(r"SHADOW rcv-sum site=(\d) A=(\d+) B=(\d+) C=(\d+) D=(\d+)", ln)
    if m:
        t = tot[int(m.group(1))]
        for i in range(4):
            t[i] += int(m.group(i + 2))
        continue
    m = re.search(r"SHADOW rcv site=(\d) cls=(\d) recv=(\d)", ln)
    if m:
        lines[(m.group(1), m.group(2), m.group(3))] += 1
for s in sorted(tot):
    a, b, c, d = tot[s]
    print(f"site {s}: A={a} B={b} C={c} D={d}")
for k, v in sorted(lines.items()):
    print(f"line site={k[0]} cls={k[1]} recv={k[2]}: {v}")
