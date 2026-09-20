import re, sys, os
# Population of the control's shape: a trait DEFAULT method with its own generic
# parameter <P>, and an `implement<...P...> trait T for ...` whose own list spells P.
root = sys.argv[1]
trait_re = re.compile(r'type\s+trait\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\{')
impl_re = re.compile(r'implement\s*<([^>]*)>\s*trait\s+([A-Za-z0-9_:]+)')
defaults = {}   # trait leaf -> set of method param spellings (defaults only)
impls = []
def scan_trait(s, start, name):
    depth = 0; i = start
    body_start = s.index('{', start)
    i = body_start; depth = 0
    while i < len(s):
        if s[i] == '{': depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0: break
        i += 1
    body = s[body_start:i]
    # a method with generic params and a body: name<P...>(...) ... {
    for m in re.finditer(r'\b([a-z_][A-Za-z0-9_]*)\s*<([^>]*)>\s*\([^)]*\)[^;{]*\{', body):
        for p in m.group(2).split(','):
            p = p.strip().split(':')[0].strip()
            if p: defaults.setdefault(name, set()).add(p)
for dp, dn, fn in os.walk(root):
    if '.bin' in dp or 'build' in dp.split(os.sep) or '.objcmp' in dp or 'legacy' in dp or 'dist' in dp.split(os.sep): continue
    for f in fn:
        if not f.endswith('.cryo'): continue
        p = os.path.join(dp, f)
        try: s = open(p, encoding='utf-8', errors='replace').read()
        except: continue
        for m in trait_re.finditer(s):
            scan_trait(s, m.start(), m.group(1))
        for m in impl_re.finditer(s):
            impls.append((p, s[:m.start()].count('\n')+1, [a.strip().split(':')[0].strip() for a in m.group(1).split(',')], m.group(2).split('::')[-1]))
hits = 0
for p, ln, params, trait in impls:
    d = defaults.get(trait)
    if not d: continue
    clash = [q for q in params if q in d]
    if clash:
        hits += 1
        print(f"{p}:{ln}: implement<{','.join(params)}> trait {trait}: default method param(s) {clash} spelled by the impl")
print(f"traits_with_generic_defaults={len(defaults)} impls={len(impls)} clashes={hits}")
