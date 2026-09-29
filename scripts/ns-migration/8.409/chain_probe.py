"""Can the facts chain a call's argument to the callee's parameter, and a
record's `param:<name>` provenance to its enclosing function's parameter
index?  Measures join coverage over .facts/compiler.facts."""
import collections, sys

F = sys.argv[1] if len(sys.argv) > 1 else ".facts/compiler.facts"
recs = [l.rstrip("\n").split("\t") for l in open(F, encoding="utf-8")]
params = {}          # (fn symbol, index) -> name
param_by_name = {}   # (fn symbol, name) -> index
fns = set()
for r in recs:
    if r[0] == "param":
        name = r[13].split(":", 1)[1] if ":" in r[13] else r[13]
        params[(r[8], int(r[4]))] = name
        param_by_name[(r[8], name)] = int(r[4])
    elif r[0] == "fn":
        fns.add(r[8])

src = lambda r: r[1].startswith("src/")
c = collections.Counter()
miss_callee = collections.Counter()
for r in recs:
    if r[0] not in ("arg", "sarg") or not src(r):
        continue
    decl = r[10]
    if not decl.startswith("src/"):
        c["callee outside compiler (stdlib/extern/unknown)"] += 1
        continue
    key = (r[8], int(r[4]))
    if key in params:
        c["arg->param joined"] += 1
    elif r[8] in fns:
        c["callee fn known, no param at index (variadic/receiver?)"] += 1
        miss_callee[r[12].split("(")[0]] += 1
    else:
        c["callee symbol has no fn record"] += 1
        miss_callee[r[12].split("(")[0]] += 1
print("== argument -> callee parameter ==")
for k, v in c.most_common():
    print("%7d  %s" % (v, k))
print("  top unjoined callees:", miss_callee.most_common(8))

c2 = collections.Counter()
miss2 = collections.Counter()
for r in recs:
    if r[0] not in ("arg", "sarg", "cmp", "match") or not src(r):
        continue
    for prov in (r[6],):
        if not prov.startswith("param:"):
            continue
        name = prov[len("param:"):]
        if (r[11], name) in param_by_name:
            c2["param provenance joined to enclosing fn's index"] += 1
        elif name == "this":
            c2["param:this (receiver, no record)"] += 1
        elif r[11] in fns:
            c2["enclosing fn known, param name not found"] += 1
            miss2[(r[13].split("(")[0], name)] += 1
        else:
            c2["enclosing symbol has no fn record"] += 1
            miss2[(r[13].split("(")[0], name)] += 1
print("== record provenance param:<name> -> enclosing parameter index ==")
for k, v in c2.most_common():
    print("%7d  %s" % (v, k))
print("  top unjoined:", miss2.most_common(8))
