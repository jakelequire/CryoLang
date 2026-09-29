"""Compare two compiler logs' warnings as sets of (code, file, message).
Line numbers are left out: an edited file shifts them."""
import re, sys

def warnings(path):
    out = []
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    for i, l in enumerate(lines):
        m = re.match(r"^warning\[(\w+)\]: (.*)$", l)
        if not m:
            continue
        loc = ""
        for j in range(i + 1, min(i + 4, len(lines))):
            n = re.match(r"^\s*--> (.*?):\d+:\d+", lines[j])
            if n:
                loc = n.group(1)
                break
        loc = re.sub(r"^.*?/stdlib/", "stdlib/", loc.replace(chr(92), "/"))
        out.append((m.group(1), loc, m.group(2)))
    return out

a, b = warnings(sys.argv[1]), warnings(sys.argv[2])
print("before", len(a), "after", len(b))
sa, sb = sorted(a), sorted(b)
from collections import Counter
ca, cb = Counter(a), Counter(b)
for k in sorted((ca - cb).keys()):
    print("GONE", (ca - cb)[k], k)
for k in sorted((cb - ca).keys()):
    print("NEW ", (cb - ca)[k], k)
