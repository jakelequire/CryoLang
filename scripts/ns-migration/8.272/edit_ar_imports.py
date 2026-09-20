p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = "import compiler::types::generic::{ GenericParamType };\n"
assert s.count(old) == 1
s = s.replace(old, "")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
