"""Compare two corpus runs' logs and statuses, entry by entry, after
normalising each run's own directory name, colour codes and timings, and
dropping probe lines (any line holding PROBE).  Prints the differing logs.
   usage: logcmp.py <run-a> <run-b> [show]"""
import os
import re
import sys

a, b = sys.argv[1], sys.argv[2]
show = len(sys.argv) > 3


def norm(path, tag):
    t = open(path, encoding="utf-8", errors="replace").read()
    t = re.sub(r"\x1b\[[0-9;]*m", "", t)
    t = t.replace("/" + tag + "/", "/RUN/").replace("\\" + tag + "\\", "\\RUN\\")
    t = re.sub(r"\d+(\.\d+)? ?(ms|s)\b", "T", t)
    return "\n".join(l for l in t.split("\n") if "PROBE" not in l)


def status(run):
    t = open(os.path.join(run, "status.txt")).read().replace(os.path.basename(run), "RUN")
    return t.split("\n")


sa, sb = status(a), status(b)
print("status lines", len(sa), len(sb), "IDENTICAL" if sa == sb else "DIFFER")


def keyed(lines):
    out = {}
    for l in lines:
        parts = l.split(" ")
        if len(parts) >= 3:
            out[parts[0] + " " + " ".join(parts[2:])] = parts[1]
        elif l:
            out[l] = ""
    return out


ka, kb = keyed(sa), keyed(sb)
for k in sorted(set(ka) | set(kb)):
    if ka.get(k) != kb.get(k):
        print("  ", k, ka.get(k), "->", kb.get(k))
la = sorted(os.listdir(os.path.join(a, "logs")))
lb = sorted(os.listdir(os.path.join(b, "logs")))
print("logs", len(la), len(lb), "same names" if la == lb else "NAMES DIFFER")
diff = 0
for f in la:
    if f not in lb:
        continue
    x = norm(os.path.join(a, "logs", f), os.path.basename(a))
    y = norm(os.path.join(b, "logs", f), os.path.basename(b))
    if x != y and sorted(x.split("\n")) == sorted(y.split("\n")):
        print("REORDERED", f)
        continue
    if x != y:
        diff += 1
        print("DIFF", f)
        if show:
            xs, ys = x.split("\n"), y.split("\n")
            for i in range(max(len(xs), len(ys))):
                p = xs[i] if i < len(xs) else "<none>"
                q = ys[i] if i < len(ys) else "<none>"
                if p != q:
                    print("   a:", p[:200])
                    print("   b:", q[:200])
                    break
print("differing logs:", diff)
