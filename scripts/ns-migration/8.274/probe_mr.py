p = 'compiler/src/compiler/decl_index.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""    // Packed(type_id, method_id) -> return type
    method_returns: HashMap<u64, TypeRef>;
""", """    // Packed(type_id, method_id) -> return type
    method_returns: HashMap<u64, TypeRef>;
    // PROBE: keys written under a trait's BARE leaf by the trait arm.
    bare_probe: HashMap<u64, TypeRef>;
""")
rep("""            method_returns:      HashMap<u64, TypeRef>::new(),
""", """            method_returns:      HashMap<u64, TypeRef>::new(),
            bare_probe:          HashMap<u64, TypeRef>::new(),
""")
rep("""    register_method(mut &this, type_sym: SymbolStr, method_sym: SymbolStr,
                    return_type: TypeRef) -> void {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        this.method_returns.insert(key, return_type);
    }
""", """    register_method(mut &this, type_sym: SymbolStr, method_sym: SymbolStr,
                    return_type: TypeRef) -> void {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        this.method_returns.insert(key, return_type);
    }

    /// PROBE: the trait arm's bare-leaf write, recorded.
    register_method_bare_probe(mut &this, type_sym: SymbolStr, method_sym: SymbolStr,
                    return_type: TypeRef) -> void {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        this.method_returns.insert(key, return_type);
        this.bare_probe.insert(key, return_type);
    }
""")
rep("""    lookup_method_return(&this, type_sym: SymbolStr, method_sym: SymbolStr) -> TypeRef {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        return match (this.method_returns.get(&key)) {
""", """    lookup_method_return(&this, type_sym: SymbolStr, method_sym: SymbolStr) -> TypeRef {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        if (this.bare_probe.contains_key(&key)) {
            fmt::eprintf("SHADOW MR-BARE-HIT %s::%s\\n", this.intern_table.resolve(type_sym), this.intern_table.resolve(method_sym));
        }
        return match (this.method_returns.get(&key)) {
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")

p2 = 'compiler/src/compiler/passes/type_resolution.cryo'
s2 = open(p2, encoding='utf-8', newline='').read()
old = """                    if (!qualified_sym.equals(node.name)) {
                        ctx.decl_index.register_method(node.name, tfunc.name,
                            tfunc.resolved_return_type);
                    }
"""
new = """                    if (!qualified_sym.equals(node.name)) {
                        fmt::eprintf("SHADOW MR-BARE-WRITE %s::%s\\n", ctx.intern_table.resolve(node.name), ctx.intern_table.resolve(tfunc.name));
                        ctx.decl_index.register_method_bare_probe(node.name, tfunc.name,
                            tfunc.resolved_return_type);
                    }
"""
assert s2.count(old) == 1
s2 = s2.replace(old, new)
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print("ok2")
