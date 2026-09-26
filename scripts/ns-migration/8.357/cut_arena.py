p = 'compiler/src/compiler/types/arena.cryo'
src = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
for line in ['import std::env;', 'import std::collections::str;',
             'import std::collections::string::{ String };',
             'mut g_b_loaded: boolean = false;', 'mut g_b_strict: string = "";']:
    assert (line + nl) in src, line
    src = src.replace(line + nl, '', 1)
# the probe added a second `import compiler::resolver::res;`
dup = 'import compiler::resolver::symbol_id::{ SymbolID };' + nl + 'import compiler::resolver::res;' + nl
assert dup in src
src = src.replace(dup, 'import compiler::resolver::symbol_id::{ SymbolID };' + nl, 1)
a = src.index('    /// PROBE: the spelling answer')
b = src.index('    /// The `This` placeholder')
src = src[:a] + src[b:]
src = src.replace('/// declaration: it is recorded as a defect and given no type.',
                  '/// declaration and is given no type.', 1)
# the blank line the two globals left behind
src = src.replace('}' + nl + nl + nl + 'type struct TypeArena', '}' + nl + nl + 'type struct TypeArena', 1)
open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
