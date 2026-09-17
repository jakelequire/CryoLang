"""Tabulate the TSC shadow (.objcmp/<tag>-lines.txt) by DISTINCT source site,
with the per-half relative path normalized to a repo-relative one."""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import sys, io, os, collections
tag = sys.argv[1] if len(sys.argv) > 1 else "m1"
R = _REPO + "/"
rows = []
for line in io.open(f".objcmp/{tag}-lines.txt", encoding="utf-8", errors="replace"):
    f = line.rstrip("\n").split("\t")
    if len(f) < 9 or f[2] != "TSC":
        continue
    label, kind, verdict, bound, pos, trait, site = f[0], f[3], f[4], f[5], f[6], f[7], f[8]
    path, ln, col = pos.rsplit(":", 2)
    path = path.replace("\\", "/")
    if path.startswith(R): path = path[len(R):]
    while path.startswith("../"): path = path[3:]
    if path.startswith("./"): path = path[2:]
    while path.startswith("../"): path = path[3:]
    lab = label.rstrip("/")
    if lab == "tools/CryoLSP" and path.startswith("src/"): path = "tools/CryoLSP/" + path
    elif lab.startswith("tests/tests/projects/") or lab.startswith("examples/"):
        base = lab.split("(")[0]
        if path.startswith("src/") or path.startswith("tests/"): path = base + "/" + path
    elif lab == "tests/unit" and path.startswith("tests/"): path = "tests/" + path
    rows.append((kind, verdict, bound, path, int(ln), int(col), trait, site))
sites = {}
for r in rows:
    sites[(r[3], r[4], r[5])] = r  # last write wins; verdict is a property of the site
print("distinct sites:", len(sites))
c = collections.Counter((r[0], r[1], r[2]) for r in sites.values())
for k, v in sorted(c.items(), key=lambda kv: -kv[1]): print(f"{v:6d}  {k[0]:7s} {k[1]:8s} {k[2]}")
out = [r for r in sites.values() if r[1] == "OUT" and r[2] == "-"]
print("\nOUT unbound sites:", len(out))
area = collections.Counter("/".join(r[3].split("/")[:2]) if not r[3].startswith("tests/tests/projects") else "tests/tests/projects" for r in out)
for k, v in area.most_common(): print(f"{v:6d}  {k}")
print("\nOUT unbound by trait:")
for k, v in collections.Counter(r[6] for r in out).most_common(): print(f"{v:6d}  {k}")
print("\nOUT unbound by FILE (top 25):")
for k, v in collections.Counter(r[3] for r in out).most_common(25): print(f"{v:6d}  {k}")
outb = [r for r in sites.values() if r[1] == "OUT" and r[2] == "bnd"]
print("\nOUT bound-directed sites:", len(outb), "by trait:")
for k, v in collections.Counter(r[6] for r in outb).most_common(8): print(f"{v:6d}  {k}")
other = [r for r in sites.values() if r[1] == "other"]
print("\n'other' (a different symbol of that leaf in scope):", len(other))
for r in other[:10]: print("  ", r)
io.open(f".objcmp/{tag}-out-sites.txt", "w", encoding="utf-8").write(
    "\n".join(f"{r[0]}\t{r[3]}:{r[4]}:{r[5]}\t{r[6]}\t{r[7]}" for r in sorted(out, key=lambda r: (r[3], r[4]))) + "\n")
