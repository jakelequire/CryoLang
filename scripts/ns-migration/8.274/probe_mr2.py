p = 'compiler/src/compiler/decl_index.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = 'fmt::eprintf("SHADOW MR-BARE-HIT %s::%s\\n", this.intern_table.resolve(type_sym), this.intern_table.resolve(method_sym));'
new = 'fmt::eprintf("SHADOW MR-BARE-HIT %u::%u\\n", type_sym.id, method_sym.id);'
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
