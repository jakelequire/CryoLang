"""Rename every compiler parameter named by a declaration-opening keyword
(`module`, `namespace`), within its own function only.  The new name follows
the parameter's type.  Prints each function touched and each line changed."""
import re, subprocess, sys

KW = ['module', 'namespace']
NEW = {  # (keyword, type prefix) -> new name
    ('module', 'SymbolStr'): 'module_name', ('module', 'string'): 'module_name',
    ('module', 'DefId'): 'module_id', ('module', 'LModule'): 'llmodule',
    ('namespace', 'SymbolStr'): 'ns_name',
}
files = subprocess.run(['git', 'grep', '-lE', r'[(,]\s*(module|namespace)\s*:|^\s+(module|namespace)\s*:\s*\w+\s*[,)]', '--', 'compiler/src'],
                       capture_output=True, text=True).stdout.split()
param_re = re.compile(r'(?:[(,]|^)\s*(module|namespace)\s*:\s*(\w+)')
total = 0
for p in files:
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    lines = s.split(nl)
    i = 0
    changed = False
    while i < len(lines):
        ln = lines[i]
        # a function signature: a line opening a parameter list, ending in `{` within a few lines
        m = re.match(r'\s*(static\s+)?[a-z_][a-z_0-9]*\s*\(', ln)
        if not m:
            i += 1; continue
        # gather the signature up to the line holding `->` ... `{`
        j = i
        sig = ln
        while '{' not in lines[j] and j < i + 8 and j + 1 < len(lines):
            j += 1; sig += ' ' + lines[j]
        if '{' not in lines[j] or '->' not in sig:
            i += 1; continue
        params = [(k, t) for k, t in param_re.findall(sig.split('->')[0])]
        params = [(k, t) for k, t in params if k in KW]
        if not params:
            i = j + 1; continue
        # body: brace-match from line j
        depth = 0; end = j
        for e in range(j, len(lines)):
            depth += lines[e].count('{') - lines[e].count('}')
            if depth == 0:
                end = e; break
        for k, t in params:
            new = NEW.get((k, t))
            if new is None:
                sys.exit('%s:%d no name for %s: %s' % (p, i + 1, k, t))
            pat = re.compile(r'(?<![.\w])' + k + r'\b(?!\s*::)(?!\s*:(?!:))')
            decl = re.compile(r'(?<![.\w])' + k + r'(\s*:\s*' + t + r')')
            for e in range(i, end + 1):
                old = lines[e]
                if e <= j:
                    new_line = decl.sub(new + r'\1', old)
                else:
                    code, sep, com = old.partition('//')
                    new_line = pat.sub(new, code) + sep + com
                if new_line != old:
                    print('%s:%d: %s' % (p, e + 1, new_line.strip()))
                    lines[e] = new_line; changed = True; total += 1
        i = end + 1
    if changed:
        open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print('lines changed:', total)
