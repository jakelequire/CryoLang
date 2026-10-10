"""For logs logcmp.py reports differing: compare them as sorted line
multisets, to tell a reordering (the test runner's concurrent output) from
a change in content."""
import os
import re
import sys

a, b = sys.argv[1], sys.argv[2]
names = sys.argv[3:]


def lines(path, tag):
    t = open(path, encoding="utf-8", errors="replace").read()
    t = re.sub(r"\x1b\[[0-9;]*m", "", t)
    t = t.replace("/" + tag + "/", "/RUN/").replace("\\" + tag + "\\", "\\RUN\\")
    t = re.sub(r"\d+(\.\d+)? ?(ms|s)\b", "T", t)
    return sorted(t.splitlines())


for f in names:
    x = lines(os.path.join(a, "logs", f), os.path.basename(a))
    y = lines(os.path.join(b, "logs", f), os.path.basename(b))
    print("REORDERED ONLY" if x == y else "CONTENT DIFFERS", f)
