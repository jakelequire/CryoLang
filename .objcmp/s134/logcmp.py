"""Compare two corpus runs' logs, entry by entry, after normalising each
run's own directory name and colour codes.  Prints the differing logs."""
import os
import re
import sys

a, b = sys.argv[1], sys.argv[2]


def norm(path, tag):
    t = open(path, encoding="utf-8", errors="replace").read()
    t = re.sub(r"\x1b\[[0-9;]*m", "", t)
    t = t.replace("/" + tag + "/", "/RUN/").replace("\\" + tag + "\\", "\\RUN\\")
    t = re.sub(r"\d+(\.\d+)? ?(ms|s)\b", "T", t)
    return t


la = sorted(os.listdir(os.path.join(a, "logs")))
lb = sorted(os.listdir(os.path.join(b, "logs")))
print("logs", len(la), len(lb), "same names" if la == lb else "NAMES DIFFER")
diff = 0
for f in la:
    if f not in lb:
        continue
    x = norm(os.path.join(a, "logs", f), os.path.basename(a))
    y = norm(os.path.join(b, "logs", f), os.path.basename(b))
    if x != y:
        diff += 1
        print("DIFF", f)
print("differing logs:", diff)
