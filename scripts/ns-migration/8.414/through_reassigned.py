#!/usr/bin/env python3
"""How much the flow check missed while it read a local's initializer only.

Over `.facts/compiler.facts` (fresh, from a compiler that writes `assign`
records), prints:

  - the rule-three paths the check lists when it follows a local through its
    initializer alone (its behaviour before `assign` records), and how many of
    them pass a local or parameter that something is later assigned to;
  - the totals with the `reassign` rule off, on, and on with every
    ambiguous join dropped (a function declaring two locals of one name, the
    only case the join by function and name can cross).

Usage: python scripts/ns-migration/8.414/through_reassigned.py
"""
import collections
import importlib.util
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
spec = importlib.util.spec_from_file_location("sf", os.path.join(ROOT, "scripts", "spelling-flow.py"))
sf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sf)
PATH = os.path.join(ROOT, ".facts", "compiler.facts")


def facts(rules):
    return sf.Facts(PATH, sf.door_functions(), rules, "src/")


def total(f):
    return sum(collections.Counter(sf.entry(v) for v in f.violations()).values())


F = facts(sf.RULES)


def paths(rec, prov, seen, passed):
    """The initializer-only trace, marking whether a reassigned local was passed."""
    if rec[11] in F.doors:
        return [(rec, "door", F.doors[rec[11]], passed)]
    while True:
        m = sf.LOCAL_NAME.match(prov)
        if m and F.assigns.get((rec[11], m.group(1), m.group(2))):
            passed = True
        s = sf.step(prov, F.rules)
        if s[0] == "next":
            prov = s[1]
            continue
        if s[0] == "origin":
            return [(rec, s[1], s[2], passed)]
        name = s[1]
        if name == "this":
            return [(rec, "receiver", "this", passed)]
        idx = F.param_index.get((rec[11], name))
        if idx is None:
            return [(rec, "chain-lost", "param %s has no record" % name, passed)]
        key = (rec[11], idx)
        if key in seen:
            return []
        calls = F.callers.get(key, [])
        if not calls:
            return [(rec, "chain-lost", "no recorded caller passes %s" % name, passed)]
        out = []
        for c in calls:
            out += paths(c, c[6], seen | {key}, passed)
        return out


listed = passing = 0
for kind, r, prov in F.lookups():
    for o, okind, otext, passed in paths(r, prov, frozenset(), False):
        if okind != "door":
            listed += 1
            passing += 1 if passed else 0
print("rule-three paths, initializer-only trace: %d; passing a reassigned local or parameter: %d"
      % (listed, passing))

exact = facts(sf.RULES)
for k in [k for k, rs in exact.assigns.items() if len({r[8] for r in rs}) > 1]:
    del exact.assigns[k]
print("rule three + four: reassign off %d, on %d, on without ambiguous joins %d"
      % (total(facts([x for x in sf.RULES if x != "reassign"])), total(F), total(exact)))
