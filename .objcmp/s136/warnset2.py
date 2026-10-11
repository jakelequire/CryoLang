"""warnset.py over the compiler's own build only: the part of each log after
its last stdlib/runtime stage, i.e. from the last line mentioning the
compiler's build directory onwards is not reliable, so instead keep the
warnings whose location is a compiler source (`src/...`), dropping the
standard library's (`./...`).  usage: warnset2.py <log-a> <log-b>"""
import re, sys


def warnings(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    text = re.sub(r"\x1b\[[0-9;]*m", "", text)
    out = []
    lines = text.splitlines()
    for i, l in enumerate(lines):
        m = re.match(r"^warning\[(\w+)\]: (.*)$", l)
        if not m:
            continue
        loc = ""
        for j in range(i + 1, min(i + 4, len(lines))):
            n = re.search(r"--> (.*?):\d+:\d+", lines[j])
            if n:
                loc = n.group(1).replace("\\", "/")
                break
        if loc.startswith("./"):
            continue
        out.append((m.group(1), loc, m.group(2)))
    return out


a, b = warnings(sys.argv[1]), warnings(sys.argv[2])
print(f"a: {len(a)} compiler warnings ({len(set(a))} distinct); b: {len(b)} ({len(set(b))} distinct)")
for w in sorted(set(a) - set(b)):
    print("only a:", w)
for w in sorted(set(b) - set(a)):
    print("only b:", w)
print("SAME SET" if set(a) == set(b) else "DIFFERENT")
