p = 'compiler/src/compiler/decl_index.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""    // Packed(type_id, method_id) -> return type
    method_returns: HashMap<u64, TypeRef>;
    // PROBE: keys written under a trait's BARE leaf by the trait arm.
    bare_probe: HashMap<u64, TypeRef>;
""", """    // Packed(type_id, method_id) -> return type
    method_returns: HashMap<u64, TypeRef>;
""")
rep("""            method_returns:      HashMap<u64, TypeRef>::new(),
            bare_probe:          HashMap<u64, TypeRef>::new(),
""", """            method_returns:      HashMap<u64, TypeRef>::new(),
""")
rep("""
    /// PROBE: the trait arm's bare-leaf write, recorded.
    register_method_bare_probe(mut &this, type_sym: SymbolStr, method_sym: SymbolStr,
                    return_type: TypeRef) -> void {
        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        this.method_returns.insert(key, return_type);
        this.bare_probe.insert(key, return_type);
    }
""", "")
rep("""        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        if (this.bare_probe.contains_key(&key)) {
            fmt::eprintf("SHADOW MR-BARE-HIT %u::%u\\n", type_sym.id, method_sym.id);
        }
        return match (this.method_returns.get(&key)) {
""", """        const key: u64 = DeclarationIndex::pack_method_key(type_sym, method_sym);
        return match (this.method_returns.get(&key)) {
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")

p2 = 'compiler/src/compiler/passes/type_resolution.cryo'
s2 = open(p2, encoding='utf-8', newline='').read()
old = """                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);
                // Register each trait method's return type on both the
                // qualified and bare trait name so bound-aware dispatch
                // (B.4) can find the method by leaf name when the receiver
                // is a `BoundedParamType` carrying this trait.
                for (mut mi: i64 = 0; mi < node.methods.length; mi++) {
                    const tfunc: FunctionDeclNode* = node.methods[mi];
                    if (tfunc == null) { continue; }
                    if (!tfunc.has_resolved_return_type()) { continue; }
                    ctx.decl_index.register_method(qualified_sym, tfunc.name,
                        tfunc.resolved_return_type);
                    if (!qualified_sym.equals(node.name)) {
                        fmt::eprintf("SHADOW MR-BARE-WRITE %s::%s\\n", ctx.intern_table.resolve(node.name), ctx.intern_table.resolve(tfunc.name));
                        ctx.decl_index.register_method_bare_probe(node.name, tfunc.name,
                            tfunc.resolved_return_type);
                    }
                }
"""
new = """                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);
                // Register each trait method's return type under the trait's
                // canonical name - the key a bound's stamped identity asks
                // by.  A trait declares a return and no signature: its
                // parameters are checked against the implementation that is
                // selected, never against the trait.  Nothing asks by the
                // bare leaf: a reader holds a registered name, and two
                // modules' same-leaf traits would be one key under the leaf.
                for (mut mi: i64 = 0; mi < node.methods.length; mi++) {
                    const tfunc: FunctionDeclNode* = node.methods[mi];
                    if (tfunc == null) { continue; }
                    if (!tfunc.has_resolved_return_type()) { continue; }
                    ctx.decl_index.register_method(qualified_sym, tfunc.name,
                        tfunc.resolved_return_type);
                }
"""
assert s2.count(old) == 1, s2.count(old)
s2 = s2.replace(old, new)
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print("ok2")
