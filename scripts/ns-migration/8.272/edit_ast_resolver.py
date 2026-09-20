p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""                this.resolve_methods(decl.methods, &res_ctx, subst, this_type);
""", """                this.resolve_methods(decl.methods, &res_ctx, entry, subst, this_type);
""", 4)
rep("""                this.resolve_func_and_body(decl, &res_ctx, subst, TypeRef::invalid());
""", """                this.resolve_func_and_body(decl, &res_ctx, entry, subst, TypeRef::invalid());
""")
rep("""    resolve_methods(mut &this, methods: &MethodNode*[], res_ctx: ResolutionContext*,
                    subst: TypeSubstitution*, this_type: TypeRef) -> void {
""", """    resolve_methods(mut &this, methods: &MethodNode*[], res_ctx: ResolutionContext*,
                    entry: TemplateEntry*, subst: TypeSubstitution*, this_type: TypeRef) -> void {
""")
rep("""                    method_ctx.add_binding(gp.sym_id, gp.name, gp_ref);
                }
                this.resolve_func_and_body(method.func, &method_ctx, subst, m_this);
            } else {
                this.resolve_func_and_body(method.func, res_ctx, subst, m_this);
            }
""", """                    method_ctx.add_binding(gp.sym_id, gp.name, gp_ref);
                }
                this.resolve_func_and_body(method.func, &method_ctx, entry, subst, m_this);
            } else {
                this.resolve_func_and_body(method.func, res_ctx, entry, subst, m_this);
            }
""")
rep("""    resolve_func_and_body(mut &this, func: FunctionDeclNode*, res_ctx: ResolutionContext*,
                          subst: TypeSubstitution*, this_type: TypeRef) -> void {
""", """    resolve_func_and_body(mut &this, func: FunctionDeclNode*, res_ctx: ResolutionContext*,
                          entry: TemplateEntry*, subst: TypeSubstitution*,
                          this_type: TypeRef) -> void {
""")
rep("""        // Fallback resolution with substitution-derived bindings.  The
        // ASTTypeSubstituter doesn't always reach every spot (virtual-dispatch gaps
        // and some annotation kinds - Function, nested Generic - aren't walked), so
        // a post-substitution annotation can still read `T` and the empty res_ctx
        // can't resolve it.  Build a `gen_ctx` mapping every original generic-param
        // SymbolStr to its concrete replacement and re-resolve still-invalid slots.
        if (subst != null) {
            mut gen_ctx: ResolutionContext = res_ctx.clone();
            if (this_type.is_valid()) { gen_ctx.set_this_type(this_type); }
            for (mut si: i64 = 0; si < subst.param_ids.length; si++) {
                const param_t: Type* = this.arena.lookup(subst.param_ids[si]);
                if (param_t != null && param_t.kind == TypeKind::GenericParam) {
                    const gp: GenericParamType* = param_t as GenericParamType*;
                    gen_ctx.add_binding(gp.param_name, subst.replacements[si]);
                }
            }
            // Return type fallback.
            if (!func.has_resolved_return_type() && func.return_type_annotation != null) {
                const resolved: TypeRef = this.type_resolver.resolve(
                    func.return_type_annotation, &gen_ctx);
                if (resolved.is_valid()) {
                    func.resolved_return_type = resolved;
                }
            }
""", """        // Fallback resolution with substitution-derived bindings.  The
        // ASTTypeSubstituter doesn't always reach every spot (virtual-dispatch gaps
        // and some annotation kinds - Function, nested Generic - aren't walked), so
        // a post-substitution annotation can still read `T` and the empty res_ctx
        // can't resolve it.  Build a `gen_ctx` binding every parameter of the
        // template - by the symbol its declaration bound, which is what a
        // written `T` in the clone carries - to its concrete replacement, and
        // re-resolve still-invalid slots.  The substitution's arena ids are
        // the template's `param_type_ids`, index-aligned with its symbols.
        if (subst != null && entry != null) {
            mut gen_ctx: ResolutionContext = res_ctx.clone();
            if (this_type.is_valid()) { gen_ctx.set_this_type(this_type); }
            for (mut si: i64 = 0; si < subst.param_ids.length; si++) {
                for (mut pj: i64 = 0; pj < entry.param_type_ids.length; pj++) {
                    if (entry.param_type_ids[pj] != subst.param_ids[si]) { continue; }
                    gen_ctx.add_binding(entry.param_syms[pj], entry.param_names[pj],
                                        subst.replacements[si]);
                    break;
                }
            }
            // Return type fallback.
            if (!func.has_resolved_return_type() && func.return_type_annotation != null) {
                const resolved: TypeRef = this.type_resolver.resolve(
                    func.return_type_annotation, &gen_ctx);
                if (resolved.is_valid()) {
                    fmt::eprintf("D30ID-FALLBACK ret %s\\n", this.intern_table.resolve(func.name));
                    func.resolved_return_type = resolved;
                }
            }
""")
rep("""                const ptype: TypeRef = this.type_resolver.resolve(
                    param.type_annotation, &gen_ctx);
                if (ptype.is_valid()) {
                    param.resolved_type = ptype;
                }
""", """                const ptype: TypeRef = this.type_resolver.resolve(
                    param.type_annotation, &gen_ctx);
                if (ptype.is_valid()) {
                    fmt::eprintf("D30ID-FALLBACK param %s\\n", this.intern_table.resolve(func.name));
                    param.resolved_type = ptype;
                }
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
