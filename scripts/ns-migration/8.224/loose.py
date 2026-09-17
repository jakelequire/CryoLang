"""For each strict-OUT site, does the writing FILE import the trait's module
in any form (`import <mod>;`, `import <mod>::{...}`, `import <mod>::*`)?
Splits the strict count into loose-in / loose-OUT."""
import io, re, collections, os
out = [l.rstrip("\n").split("\t") for l in io.open(".objcmp/m1-out-sites.txt", encoding="utf-8") if l.strip()]
loose_in, loose_out = [], []
cache = {}
for kind, pos, trait, site in out:
    path = pos.rsplit(":", 2)[0]
    mod = trait.rsplit("::", 1)[0]
    if path not in cache:
        try: cache[path] = io.open(path, encoding="utf-8", errors="replace").read()
        except OSError: cache[path] = None
    src = cache[path]
    if src is None:
        loose_out.append((kind, pos, trait, "FILE-NOT-FOUND")); continue
    pat = re.compile(r"^\s*import\s+" + re.escape(mod) + r"\s*(;|::)", re.M)
    (loose_in if pat.search(src) else loose_out).append((kind, pos, trait, site))
print("strict OUT sites:", len(out))
print("  module imported in some form (loose IN):", len(loose_in))
print("  module not imported at all (loose OUT):", len(loose_out))
print("loose OUT by trait:")
for k, v in collections.Counter(r[2] for r in loose_out).most_common(): print(f"{v:6d}  {k}")
print("loose OUT by file:")
for k, v in collections.Counter(r[1].rsplit(':', 2)[0] for r in loose_out).most_common(40): print(f"{v:6d}  {k}")
print("files with loose OUT:", len(set(r[1].rsplit(':', 2)[0] for r in loose_out)))
print("files with strict OUT:", len(set(r[1].rsplit(':', 2)[0] for r in out)))
