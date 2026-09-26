"""Classify the family keys the probe logged: registered and asked.

Shapes of a key string:
  root        no '::' at all (a clone id, a single-file function, ...)
  qualified   'prefix::leaf'
and whether the leaf carries '$' (a compiler-minted leaf no source can write).
For asked keys, also whether the key was ever registered (found vs absent).
"""
import collections
import sys

path = sys.argv[1]
reg = collections.Counter()
ask = collections.Counter()
ask_by_door = collections.defaultdict(collections.Counter)
with open(path, encoding="utf-8", errors="replace") as fh:
    for line in fh:
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "FAMREG":
            reg[parts[1]] += 1
        elif parts[0] == "FAMGET":
            ask[parts[2]] += 1
            ask_by_door[parts[1]][parts[2]] += 1


def shape(k):
    if "::" not in k:
        return "root" + ("$" if "$" in k else "")
    leaf = k.rsplit("::", 1)[1]
    return "qualified" + ("$" if "$" in leaf else "") + ("+prefix$" if "$" in k.rsplit("::", 1)[0] else "")


def summarize(title, counter):
    by = collections.Counter()
    ex = {}
    for k, n in counter.items():
        s = shape(k)
        by[s] += 1
        ex.setdefault(s, k)
    print(title, "distinct", len(counter), "events", sum(counter.values()))
    for s, n in by.most_common():
        print("   %-24s %6d distinct   e.g. %s" % (s, n, ex[s]))


summarize("REGISTERED", reg)
summarize("ASKED", ask)
for door, c in ask_by_door.items():
    found = sum(1 for k in c if k in reg)
    print("  door %-10s distinct %6d  registered %6d  never-registered %6d"
          % (door, len(c), found, len(c) - found))
    miss = [k for k in c if k not in reg]
    by = collections.Counter(shape(k) for k in miss)
    print("       never-registered shapes:", dict(by), " e.g.", miss[:4])
