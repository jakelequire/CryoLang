import os, re, sys
a, b = sys.argv[1], sys.argv[2]
strip = re.compile(r"NRPROBE")
norm = re.compile(r"C:/Programming/apps/CryoLang/\.objcmp/s136/[a-z0-9]+/")
diffs = 0
n = 0
for f in sorted(os.listdir(a)):
    pa, pb = os.path.join(a, f), os.path.join(b, f)
    if not os.path.exists(pb):
        print("MISSING", f); diffs += 1; continue
    def rd(p):
        out = []
        for l in open(p, encoding="utf-8", errors="replace"):
            if strip.search(l):
                continue
            out.append(norm.sub("", l))
        return out
    n += 1
    la, lb = rd(pa), rd(pb)
    if la != lb:
        diffs += 1
        print("DIFF", f)
print("compared", n, "differing", diffs)
