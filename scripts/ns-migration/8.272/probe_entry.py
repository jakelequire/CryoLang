p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
old1 = """            if (!func.has_resolved_return_type() && func.return_type_annotation != null) {
                const resolved: TypeRef = this.type_resolver.resolve(
                    func.return_type_annotation, &gen_ctx);
"""
new1 = """            if (!func.has_resolved_return_type() && func.return_type_annotation != null) {
                fmt::eprintf("SHADOW D30ID-ENTRY ret %s\n", this.intern_table.resolve(func.name));
                const resolved: TypeRef = this.type_resolver.resolve(
                    func.return_type_annotation, &gen_ctx);
"""
old2 = """                if (param.type_annotation == null) { continue; }
                const ptype: TypeRef = this.type_resolver.resolve(
                    param.type_annotation, &gen_ctx);
"""
new2 = """                if (param.type_annotation == null) { continue; }
                fmt::eprintf("SHADOW D30ID-ENTRY param %s\n", this.intern_table.resolve(func.name));
                const ptype: TypeRef = this.type_resolver.resolve(
                    param.type_annotation, &gen_ctx);
"""
assert s.count(old1) == 1 and s.count(old2) == 1
s = s.replace(old1, new1).replace(old2, new2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
