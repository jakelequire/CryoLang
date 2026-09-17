"""Pair the `tie`/`ptie` lines by site: for each call whose receiver has two
traits providing the method, how many of the two are in scope (strict)?"""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import io, collections
R = _REPO + "/"
ties = collections.defaultdict(dict)   # (kind, path, line, col) -> {trait: (verdict, bound)}
for line in io.open(".objcmp/m2-lines.txt", encoding="utf-8", errors="replace"):
    f = line.rstrip("\n").split("\t")
    if len(f) < 9 or f[2] != "TSC" or f[3] not in ("tie", "ptie"): continue
    label, kind, verdict, bound, pos, trait, site = f[0], f[3], f[4], f[5], f[6], f[7], f[8]
    path, ln, col = pos.rsplit(":", 2)
    path = path.replace("\\", "/")
    if path.startswith(R): path = path[len(R):]
    if path.startswith("./"): path = path[2:]
    while path.startswith("../"): path = path[3:]
    lab = label.rstrip("/")
    if lab == "tools/CryoLSP" and path.startswith("src/"): path = "tools/CryoLSP/" + path
    elif (lab.startswith("tests/tests/projects/") or lab.startswith("examples/")) and (path.startswith("src/") or path.startswith("tests/")):
        path = lab.split("(")[0] + "/" + path
    elif lab == "tests/unit" and path.startswith("tests/"): path = "tests/" + path
    ties[(kind, path, int(ln), int(col))][trait] = (verdict, bound)
print("tie SITES:", len(ties))
IN = ("same", "prelude", "import")
summary = collections.Counter()
rows = []
for k, cands in sorted(ties.items()):
    n_in = sum(1 for v, b in cands.values() if v in IN)
    bound = any(b == "bnd" for v, b in cands.values())
    summary[(k[0], "bound" if bound else "unbound", f"{n_in}/{len(cands)} in scope")] += 1
    rows.append((k, cands, n_in, bound))
for kk, v in sorted(summary.items()): print(f"{v:6d}  {kk}")
print("\nby trait pair:")
pairs = collections.Counter(tuple(sorted(c.keys())) for _, c, _, _ in rows)
for k, v in pairs.most_common(): print(f"{v:6d}  {k}")
print("\nunbound sites (the ones that are or would be E0154):")
for k, cands, n_in, bound in rows:
    if not bound: print("  ", k, {t: v for t, (v, b) in cands.items()})
print("\nbound sites where the scope rule ALONE would settle it (exactly 1 in scope):")
print("  ", sum(1 for _, c, n, b in rows if b and n == 1), "of", sum(1 for _, c, n, b in rows if b))
