import io
def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

edit('compiler/src/compiler/passes/specialization.cryo', [
("""        mut param_names: SymbolStr[] = [];
        mut param_type_ids: u64[] = [];
        for (mut i: i64 = 0; i < generic_params.length; i++) {
            const gp: GenericParamNode* = generic_params[i];
            param_names.push(gp.name);
            param_type_ids.push(refs[i].id);
        }
""", """        mut param_names: SymbolStr[] = [];
        mut param_syms: SymbolID[] = [];
        mut param_type_ids: u64[] = [];
        for (mut i: i64 = 0; i < generic_params.length; i++) {
            const gp: GenericParamNode* = generic_params[i];
            param_names.push(gp.name);
            param_syms.push(gp.sym_id);
            param_type_ids.push(refs[i].id);
        }
""", 1),
("""        const entry: TemplateEntry = TemplateEntry::new(
            name, qualified_sym, module_sym,
            param_names, param_type_ids,
            ast_node, node_kind,
            base_type
        );
""", """        const entry: TemplateEntry = TemplateEntry::new(
            name, qualified_sym, module_sym,
            param_names, param_syms, param_type_ids,
            ast_node, node_kind,
            base_type
        );
""", 1),
("""            mut param_names: SymbolStr[] = [];
            mut param_type_ids: u64[] = [];
            for (mut j: i64 = 0; j < m.func.generic_params.length; j++) {
                const gp: GenericParamNode* = m.func.generic_params[j];
                param_names.push(gp.name);
                param_type_ids.push(refs[j].id);
            }
""", """            mut param_names: SymbolStr[] = [];
            mut param_syms: SymbolID[] = [];
            mut param_type_ids: u64[] = [];
            for (mut j: i64 = 0; j < m.func.generic_params.length; j++) {
                const gp: GenericParamNode* = m.func.generic_params[j];
                param_names.push(gp.name);
                param_syms.push(gp.sym_id);
                param_type_ids.push(refs[j].id);
            }
""", 1),
("""            const entry: TemplateEntry = TemplateEntry::new(
                m.func.name, qualified_sym, module_sym,
                param_names, param_type_ids,
                m.func as ASTNode*, NodeKind::FunctionDeclaration,
                base_type
            );
""", """            const entry: TemplateEntry = TemplateEntry::new(
                m.func.name, qualified_sym, module_sym,
                param_names, param_syms, param_type_ids,
                m.func as ASTNode*, NodeKind::FunctionDeclaration,
                base_type
            );
""", 1),
])

edit('compiler/src/compiler/sema/async_lower.cryo', [
("""            mut param_names: SymbolStr[] = [];
            mut param_type_ids: u64[] = [];
            for (mut gi: i64 = 0; gi < fut_params.length; gi++) {
                param_names.push(fut_params[gi].name);
                param_type_ids.push(param_refs[gi].id);
            }
""", """            mut param_names: SymbolStr[] = [];
            mut param_syms: SymbolID[] = [];
            mut param_type_ids: u64[] = [];
            for (mut gi: i64 = 0; gi < fut_params.length; gi++) {
                param_names.push(fut_params[gi].name);
                param_syms.push(fut_params[gi].sym_id);
                param_type_ids.push(param_refs[gi].id);
            }
""", 1),
("""            this.ctx.generic_registry.register_template(TemplateEntry::new(
                bare, q_name, tmpl_module, param_names, param_type_ids,
                struct_decl as ASTNode*, NodeKind::StructDeclaration, struct_ref));
""", """            this.ctx.generic_registry.register_template(TemplateEntry::new(
                bare, q_name, tmpl_module, param_names, param_syms, param_type_ids,
                struct_decl as ASTNode*, NodeKind::StructDeclaration, struct_ref));
""", 1),
])

edit('compiler/src/compiler/AST/declaration.cryo', [
("""    derived_param_names: SymbolStr[];
    derived_param_types: TypeRef[];
""", """    derived_param_names: SymbolStr[];
    /// The symbol each derived parameter was declared as, index-aligned
    /// with the names; what a body's stamped `A` is matched by.  The name
    /// beside it mints the arena's parameter type and nothing else.
    derived_param_syms:  SymbolID[];
    derived_param_types: TypeRef[];
""", 1),
("""        this.derived_param_names   = [];
""", """        this.derived_param_names   = [];
        this.derived_param_syms    = [];
""", 1),
("""    /// Record a concrete trait-element binding recovered at impl-spec time.
    /// See `derived_param_names` / `derived_param_types`.
    add_derived_param(mut &this, name: SymbolStr, ty: TypeRef) -> void {
        this.derived_param_names.push(name);
        this.derived_param_types.push(ty);
    }

    /// Concrete element type previously cached for `name`, or invalid.
    lookup_derived_param(&this, name: SymbolStr) -> TypeRef {
        for (mut i: i64 = 0; i < this.derived_param_names.length; i++) {
            if (this.derived_param_names[i].equals(name)) {
                return this.derived_param_types[i];
            }
        }
        return TypeRef::invalid();
    }
""", """    /// Record a concrete trait-element binding recovered at impl-spec time.
    /// See `derived_param_names` / `derived_param_types`.
    add_derived_param(mut &this, sym: SymbolID, name: SymbolStr, ty: TypeRef) -> void {
        this.derived_param_syms.push(sym);
        this.derived_param_names.push(name);
        this.derived_param_types.push(ty);
    }

    /// Concrete element type previously cached for the parameter `sym`, or
    /// invalid.
    lookup_derived_param(&this, sym: SymbolID) -> TypeRef {
        for (mut i: i64 = 0; i < this.derived_param_syms.length; i++) {
            if (this.derived_param_syms[i].equals(sym)) {
                return this.derived_param_types[i];
            }
        }
        return TypeRef::invalid();
    }
""", 1),
])

edit('compiler/src/compiler/AST/cloner.cryo', [
("""        for (mut i: i64 = 0; i < node.derived_param_names.length; i++) {
            c.add_derived_param(node.derived_param_names[i], node.derived_param_types[i]);
""", """        for (mut i: i64 = 0; i < node.derived_param_names.length; i++) {
            c.add_derived_param(node.derived_param_syms[i], node.derived_param_names[i],
                                node.derived_param_types[i]);
""", 1),
])
