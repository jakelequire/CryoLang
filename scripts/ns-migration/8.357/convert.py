"""Turn the arena-flip probe into the flip: every comparison by identity.

Run from the repo root over a tree carrying scripts/ns-migration/8.348's
probe.  Mechanical parts only; the arena, substitution and trait checker
definitions are edited by hand.
"""
import re, glob

B_MATCH = re.compile(
    r'TypeArena::b_match\("[^"]*",\s*([^,()]+(?:\[[^\]]*\])?),\s*[^,]+,\s*'
    r'([A-Za-z_][A-Za-z0-9_]*)\.param_sym,\s*\2\.param_name\)')
LOOKUP = re.compile(
    r'lookup_subst_for_param\("[^"]*",\s*([^,]+?),\s*[^,]+?,\s*(subst)\)', re.S)
B_SITE = re.compile(r'^[ \t]*TypeSubstitution::b_site\("[^"]*"\);\r?\n', re.M)

total = {'b_match': 0, 'lookup': 0, 'b_site': 0}
for path in glob.glob('compiler/src/**/*.cryo', recursive=True):
    src = open(path, encoding='utf-8', newline='').read()
    out, n1 = B_MATCH.subn(lambda m: '%s.declared_by(%s)' % (m.group(2), m.group(1).strip()), src)
    out, n2 = LOOKUP.subn(lambda m: 'lookup_subst_for_param(%s, %s)' % (m.group(1).strip(), m.group(2)), out)
    out, n3 = B_SITE.subn('', out)
    if n1 or n2 or n3:
        open(path, 'w', encoding='utf-8', newline='').write(out)
        print('%-60s b_match %d  lookup %d  b_site %d' % (path, n1, n2, n3))
        total['b_match'] += n1; total['lookup'] += n2; total['b_site'] += n3
print(total)
