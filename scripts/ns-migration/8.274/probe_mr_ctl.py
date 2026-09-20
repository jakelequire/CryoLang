import sys
p = 'compiler/src/compiler/passes/type_resolution.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """                        ctx.decl_index.register_method_bare_probe(node.name, tfunc.name,
                            tfunc.resolved_return_type);
"""
new = """                        ctx.decl_index.register_method_bare_probe(node.name, tfunc.name,
                            tfunc.resolved_return_type);
                        ctx.decl_index.lookup_method_return(node.name, tfunc.name);   // PROBE CONTROL
"""
if sys.argv[1] == 'on':
    assert s.count(old) == 1
    s = s.replace(old, new)
else:
    assert s.count(new) == 1
    s = s.replace(new, old)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok", sys.argv[1])
