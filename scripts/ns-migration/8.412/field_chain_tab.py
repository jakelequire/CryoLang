import re, sys, collections
R = 'C:/Programming/apps/CryoLang/.objcmp/s73/'
lines = []
skip = False
for l in open(R + 'fprobe.out', encoding='utf-8', errors='replace'):
    if l.startswith('BUILD'):
        skip = l.rstrip().endswith('/compiler')
        continue
    if not skip and l.startswith('FPROBE'):
        lines.append(l.rstrip('\n'))
for l in open(R + 'cbuild.log', encoding='utf-8', errors='replace'):
    if l.startswith('FPROBE'):
        lines.append(l.rstrip('\n'))
pat = re.compile(r'FPROBE site=(\S+) win=(\S+) wi=(\S+) wt=(\S+) S=(\S+) C=(\S+) T=(\S+) shape=(\S*) walk=(\S+) leaf=(\S*) at=(.*)$')
rows = []
for l in lines:
    m = pat.match(l)
    if not m:
        print('UNPARSED', l[:200]); continue
    rows.append(m.groups())
print('rows', len(rows))
tab = collections.Counter((r[0], r[1]) for r in rows)
for k, v in sorted(tab.items()):
    print('%-6s %-5s %8d' % (k[0], k[1], v))

def idx_ty(x):
    # "d:i/key" or "i/key" or "nofl/key" or miss/na
    if x in ('miss', 'na'):
        return None
    a, b = x.rsplit('/', 1)
    if ':' in a:
        a = a.split(':')[1]
    return (a, b)

# Pattern of who COULD answer, per site/win, with agreement.
pc = collections.Counter()
ex = {}
for r in rows:
    site, win, wi, wt, S, C, T, shape, walk, leaf, at = r
    s, c, t = idx_ty(S), idx_ty(C), idx_ty(T)
    avail = ''.join(n for n, v in (('S', s), ('C', c), ('T', t)) if v is not None)
    vals = [v for v in (s, c, t) if v is not None and v[0] != 'nofl']
    agree = 'agree' if len(set(vals)) <= 1 else 'DISAGREE'
    idxs = set(v[0] for v in vals)
    if agree == 'DISAGREE' and len(idxs) == 1:
        agree = 'DIFFTYPE'
    key = (site, win, avail or '-', agree, re.sub(r'\d+', 'n', shape), walk)
    pc[key] += 1
    ex.setdefault(key, r)
for k, v in sorted(pc.items(), key=lambda kv: -kv[1]):
    print('%8d %s   eg %s %s %s' % (v, ' '.join(k), ex[k][9], ex[k][10], ' '.join(ex[k][4:7])))
