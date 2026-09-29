"""Join each MPROBE (typing) row to the latest SPROBE (selection) row for the
same call node, and tabulate arm x pin x shape x walk x why-the-selection-declined."""
import collections
import re
import sys

pat = re.compile(r'MPROBE shape=(\S*) arm=(\S+) pin=(\d) cpin=(\d) gen=(-?\d) ret=(\S+) walk=(\S+) leaf=(\S*) call=(\d+) at=(.*)$')
spat = re.compile(r'SPROBE call=(\d+) why=(\S+)')
last = {}
c = collections.Counter()
ex = {}
n = 0
for path in sys.argv[1:]:
    for l in open(path, encoding='utf-8', errors='replace'):
        if l.startswith('SPROBE'):
            m = spat.match(l)
            if m:
                last[m.group(1)] = m.group(2)
            continue
        if not l.startswith('MPROBE'):
            continue
        m = pat.match(l.rstrip('\n'))
        if not m:
            continue
        n += 1
        shape, arm, pin, cpin, gen, ret, walk, leaf, call, at = m.groups()
        shape = re.sub(r'^[PR]+', '', shape)
        why = last.get(call, 'none')
        why = re.sub(r':\d+$', '', why)
        k = (arm, 'pin' if pin == '1' else 'nopin', shape, walk.split('/')[0], why)
        c[k] += 1
        ex.setdefault(k, (leaf, at))
print('rows', n)
for k, v in sorted(c.items(), key=lambda kv: (kv[0][0], kv[0][1], -kv[1])):
    if v >= 5:
        print('%-5s %-5s %-18s %-3s %-16s %7d  eg %s %s' % (k + (v,) + ex[k]))
