import re, sys, collections
p = sys.argv[1]
sites = collections.defaultdict(set)
rows = 0
build = None
for l in open(p, encoding='utf-8', errors='replace'):
    if l.startswith('BUILD') or l.startswith('UNIT'):
        build = l.split()[-1] if l.startswith('BUILD') else 'UNIT'
        continue
    if not l.startswith('UNB3'):
        continue
    rows += 1
    m = re.match(r'UNB3 (.*):(\d+) param=(\S*) m=(\S*)', l.rstrip())
    f = m.group(1).replace('\\', '/')
    f = re.sub(r'^.*?/(stdlib|tests|examples|compiler)/', r'\1/', f)
    sites[(f, int(m.group(2)), m.group(3), m.group(4))].add(build.replace('C:/Programming/apps/CryoLang/', ''))
print('rows', rows, 'distinct sites', len(sites))
for k in sorted(sites):
    b = sorted(sites[k])
    print('%s:%d param=%s m=%s  builds=%d %s' % (k[0], k[1], k[2], k[3], len(b), b[0]))
