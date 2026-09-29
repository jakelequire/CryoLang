import re, collections
R = 'C:/Programming/apps/CryoLang/.objcmp/s73/'
pat = re.compile(r'FPROBE site=(\S+) win=(\S+) wi=(\S+) wt=(\S+) S=(\S+) C=(\S+) T=(\S+) shape=(\S*) walk=(\S+) leaf=(\S*) at=(.*)$')
def rows():
    skip=False
    for l in open(R+'fprobe.out',encoding='utf-8',errors='replace'):
        if l.startswith('BUILD'): skip=l.rstrip().endswith('/compiler'); continue
        if not skip and l.startswith('FPROBE'): yield pat.match(l).groups()
    for l in open(R+'cbuild.log',encoding='utf-8',errors='replace'):
        if l.startswith('FPROBE'): yield pat.match(l).groups()
c=collections.Counter(); ex={}
for r in rows():
    site,win,wi,wt,S,C,T,shape,walk,leaf,at=r
    k=(site, 'S:'+('na' if S=='na' else 'miss' if S=='miss' else 'hit'), 'C:'+('hit' if C!='miss' else 'miss'), 'T:'+('hit' if T!='miss' else 'miss'), win, shape)
    c[k]+=1; ex.setdefault(k,(leaf,at))
for k,v in sorted(c.items()):
    if k[1]!='S:na' or k[2]=='C:miss':
        print(v, ' '.join(k), ex[k])
