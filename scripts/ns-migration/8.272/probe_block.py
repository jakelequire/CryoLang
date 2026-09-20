p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """        if (subst != null && entry != null) {
            mut gen_ctx: ResolutionContext = res_ctx.clone();
"""
new = """        if (subst != null && entry != null) {
            fmt::eprintf("SHADOW D30ID-BLOCK %s\n", this.intern_table.resolve(func.name));
            mut gen_ctx: ResolutionContext = res_ctx.clone();
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
