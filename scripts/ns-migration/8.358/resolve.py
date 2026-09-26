import sys, re, os
# resolve.py FILE SPEC: SPEC is a comma list of o|t|c per conflict in order
# (o=ours, t=theirs, c=custom text from resolve-<file-stem>-<n>.fragment beside this script)
path, spec = sys.argv[1], sys.argv[2].split(',')
src = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
pat = re.compile(r'<<<<<<< ours\r?\n(.*?)=======\r?\n(.*?)>>>>>>> theirs\r?\n', re.S)
i = [0]
def rep(m):
    k = spec[i[0]]; i[0] += 1
    if k == 'o': return m.group(1)
    if k == 't': return m.group(2)
    stem = os.path.basename(path).split('.')[0]
    here = os.path.dirname(os.path.abspath(__file__))
    txt = open(os.path.join(here, 'resolve-%s-%d.fragment' % (stem, i[0])), encoding='utf-8').read()
    return txt.replace('\r\n', '\n').replace('\n', nl)
out = pat.sub(rep, src)
assert i[0] == len(spec), (i[0], len(spec))
open(path, 'w', encoding='utf-8', newline='').write(out)
print('resolved', i[0])
