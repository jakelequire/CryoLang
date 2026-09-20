import re, sys, os
# For every `implement<...> [trait X] for [struct] Name<args>` list the impl params and the target args,
# and compare against the struct's declared param spelling.
root = sys.argv[1]
structs = {}
impls = []
decl_re = re.compile(r'type\s+(?:struct|class|enum|union)\s+([A-Za-z_][A-Za-z0-9_]*)\s*<([^>]*)>')
impl_re = re.compile(r'implement\s*<([^>]*)>\s*(?:trait\s+[A-Za-z0-9_:<>, ]+?\s+for\s+)?(?:struct\s+|class\s+|enum\s+|union\s+)?([A-Za-z_][A-Za-z0-9_:]*)\s*<([^>]*)>')
for dp, dn, fn in os.walk(root):
    if '.bin' in dp or 'build' in dp.split(os.sep) or '.objcmp' in dp or 'legacy' in dp: continue
    for f in fn:
        if not f.endswith('.cryo'): continue
        p = os.path.join(dp, f)
        try: s = open(p, encoding='utf-8', errors='replace').read()
        except: continue
        for m in decl_re.finditer(s):
            structs.setdefault(m.group(1), []).append(([a.strip().split(' ')[0].split('=')[0].strip() for a in m.group(2).split(',')], p))
        for m in impl_re.finditer(s):
            params = [a.strip() for a in m.group(1).split(',')]
            args = [a.strip() for a in m.group(3).split(',')]
            impls.append((p, s[:m.start()].count('\n')+1, params, m.group(2), args))
n = 0; diff = 0
for p, ln, params, target, args in impls:
    leaf = target.split('::')[-1]
    n += 1
    ds = structs.get(leaf)
    if not ds: continue
    dparams = ds[0][0]
    if [a for a in args if a in params] != dparams[:len(args)] and any(a in params for a in args):
        diff += 1
        print(f"{p}:{ln}: implement<{','.join(params)}> for {leaf}<{','.join(args)}> vs decl <{','.join(dparams)}>")
print(f"impls={n} spelled_differently={diff}")
