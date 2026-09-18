"""List every LOOKUP / LOOKUP_OTHER site (DeclarationIndex readers) with its
line text, using lane-gate.py's own placement so the population is the gate's.
Usage: python sites.py [ROWS...]   (default: LOOKUP LOOKUP_OTHER)
"""
import importlib.util
import os
import re
import sys

ROOT = r"C:\Programming\apps\CryoLang"
spec = importlib.util.spec_from_file_location("lg", os.path.join(ROOT, "scripts", "lane-gate.py"))
lg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lg)

want = set(sys.argv[1:]) or {"LOOKUP", "LOOKUP_OTHER"}
tree = lg.Tree(lg.DEFAULT_SRC)
sets = {name: lg.store_methods(tree, name, st.defn) for name, st in lg.STORES.items()}
names = sorted(set().union(*sets.values()), key=len, reverse=True)
alt = "|".join(names)
dotted_re = re.compile(r"%s\.(%s)\s*\(" % (lg.RECEIVER, alt))
static_re = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)::(%s)\s*\(" % alt)


def row_for(store_name, name, rel):
    st = lg.STORES[store_name]
    kind = sets[store_name].get(name)
    if kind is None or st.owns(rel):
        return None
    if store_name == lg.INDEX_TYPE and name in lg.LOOKUPS:
        return "LOOKUP"
    if store_name == lg.ARENA_TYPE and name == lg.ARENA_NAME_LOOKUP:
        return "LOOKUP_ARENA"
    return st.write if kind == "write" else st.read


n = 0
for rel in tree.rels:
    for lineno, raw in enumerate(tree.files[rel], 1):
        line = lg.strip_comment(raw)
        if not line.strip():
            continue
        for m in dotted_re.finditer(line):
            recv, name = m.group(1), m.group(2)
            ty = tree.receiver_type(rel, lineno, recv)
            if ty in lg.STORES:
                row = row_for(ty, name, rel)
                if row in want:
                    n += 1
                    print("%s\t%s:%d\t%s\t%s" % (row, rel, lineno, name, line.strip()))
        for m in static_re.finditer(line):
            owner, name = m.group(1), m.group(2)
            if owner in lg.STORES and sets[owner].get(name) == "static":
                row = row_for(owner, name, rel)
                if row in want:
                    n += 1
                    print("%s\t%s:%d\t%s\t%s" % (row, rel, lineno, name, line.strip()))
print("TOTAL", n, file=sys.stderr)
