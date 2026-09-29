import re, collections
R = 'C:/Programming/apps/CryoLang/.objcmp/s76/'
pat = re.compile(r'UPROBE (\S+) fn=(\d+) impl=(\d+) owner=(\d+) param=(\S*) type=(.*?) func=(\S*) leaf=(\S*) walk=(\S+) at=(.*):(\d+)$')
rows = 0
sites = {}          # (file, line, leaf) -> (tag set, type, param, func)
build = None
per_build = collections.defaultdict(set)
for l in open(R + 'uprobe.out', encoding='utf-8', errors='replace'):
    if l.startswith('BUILD'):
        build = l.split()[2]
        continue
    if not l.startswith('UPROBE'):
        continue
    m = pat.match(l.rstrip('\n'))
    if not m:
        print('UNPARSED', l[:160]); continue
    rows += 1
    tag, nf, ni, no, param, ty, func, leaf, walk, f, line = m.groups()
    f = f.replace('\\', '/')
    f = re.sub(r'^.*?/stdlib/', 'stdlib/', f)
    if not f.startswith('stdlib/'):
        f = build.replace('C:/Programming/apps/CryoLang/', '') + '/' + f
    k = (f, int(line), leaf)
    e = sites.setdefault(k, [set(), ty, param, func, set()])
    e[0].add(tag); e[4].add(walk)
    per_build[k].add(build)
print('rows', rows, 'distinct call sites on a type parameter', len(sites))
unb = {k: v for k, v in sites.items() if 'UNBOUNDED' in v[0]}
mixed = {k: v for k, v in unb.items() if len(v[0]) > 1}
print('UNBOUNDED sites', len(unb), '(of which also bounded in another walk:', len(mixed), ')')
area = collections.Counter('stdlib' if k[0].startswith('stdlib/') else k[0].split('/')[0] + '/' + (k[0].split('/')[3] if k[0].startswith('tests/tests/projects') else k[0].split('/')[1]) for k in unb)
print('by area:', dict(area))
bytype = collections.defaultdict(list)
for k, v in unb.items():
    bytype[(v[1], v[2])].append(k)
print()
for (ty, param), ks in sorted(bytype.items(), key=lambda kv: (-len(kv[1]), kv[0])):
    leaves = collections.Counter(k[2] for k in ks)
    files = sorted(set(k[0] for k in ks))
    print('%3d  type=%s param=%s  methods=%s  files=%s' % (len(ks), ty, param, dict(leaves), files))
print()
for k in sorted(unb):
    v = unb[k]
    print('%s:%d %s  type=%s param=%s func=%s walks=%s tags=%s' % (k[0], k[1], k[2], v[1], v[2], v[3], sorted(v[4]), sorted(v[0])))
