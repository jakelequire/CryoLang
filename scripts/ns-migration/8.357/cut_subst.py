p = 'compiler/src/compiler/types/substitution.cryo'
src = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
def cut(s, n=1):
    global src
    assert src.count(s) >= n, s
    src = src.replace(s, '', n)
cut('import std::fmt;' + nl)
src = src.replace('import compiler::types::generic::{ AssocProjectionType, GenericParamType, InstantiatedType };',
                  'import compiler::types::generic::{ AssocProjectionType, InstantiatedType };', 1)
cut('mut g_b_site: string = "?";' + nl + nl)
cut('    site:         string;' + nl)
cut('        const s: string = g_b_site;' + nl + '        g_b_site = "?";' + nl, 2)
cut('            site:         s,' + nl, 2)
a = src.index('    static b_site(s: string) -> void { g_b_site = s; }')
b = src.index('    get(&this, param_id: u64) -> TypeRef {')
src = src[:a] + src[b:]
assert src.count('this.get_probe(ty.id, arena)') == 1
src = src.replace('this.get_probe(ty.id, arena)', 'this.get(ty.id)', 1)
open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
