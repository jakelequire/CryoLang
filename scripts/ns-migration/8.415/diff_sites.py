import collections, re, sys
pat = re.compile(r'MPROBE shape=(\S*) arm=(\S+) pin=(\d) cpin=(\d) gen=(-?\d) ret=(\S+) walk=(\S+) leaf=(\S*) call=\d+ at=(.*)$')
def load(p):
    c = collections.Counter()
    for l in open(p, encoding='utf-8', errors='replace'):
        m = pat.match(l.rstrip('\n'))
        if m:
            shape, arm, pin, cpin, gen, ret, walk, leaf, at = m.groups()
            c[(leaf, at.replace('C:/Programming/apps/CryoLang/', ''), arm, pin, gen, ret, walk)] += 1
    return c
a, b = load(sys.argv[1]), load(sys.argv[2])
# per site totals
sa = collections.Counter(); sb = collections.Counter()
for k, v in a.items(): sa[k[:2]] += v
for k, v in b.items(): sb[k[:2]] += v
print("sites whose typing count changed:")
for k in sorted(set(sa) | set(sb)):
    if sa[k] != sb[k]: print(" ", sa[k], "->", sb[k], *k)
print("ret mismatch rows (pinned, return by name != pinned):")
for k, v in b.items():
    if k[5] == 'NE': print(" ", v, *k)
