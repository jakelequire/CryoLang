"""Compare two check logs' errors as sets of (code, file:line, message).
   usage: errset.py <log-a> <log-b>"""
import re
import sys


def errors(path):
    t = re.sub(r"\x1b\[[0-9;]*m", "", open(path, encoding="utf-8", errors="replace").read())
    lines = t.splitlines()
    out = []
    for i, l in enumerate(lines):
        m = re.match(r"^error\[(\w+)\]: (.*)$", l)
        if not m:
            continue
        loc = ""
        for j in range(i + 1, min(i + 4, len(lines))):
            n = re.search(r"--> (.*?:\d+):\d+", lines[j])
            if n:
                loc = n.group(1).replace("\\", "/")
                break
        out.append((m.group(1), loc, m.group(2)))
    return out


a, b = errors(sys.argv[1]), errors(sys.argv[2])
sa, sb = set(a), set(b)
print(f"a: {len(a)} errors; b: {len(b)}")
for e in sorted(sa - sb):
    print("only a:", e)
for e in sorted(sb - sa):
    print("only b:", e)
