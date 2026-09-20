def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

# ---------------- call_specializer ----------------
edit('compiler/src/compiler/mono/call_specializer.cryo', [
# the instance-method path: hand the parameter NODES to specialize_method
("""        mut bindings: TypeRef[] = [];
        mut method_param_names: SymbolStr[] = [];
        for (mut i: i64 = 0; i < n_method_params; i++) {
            const gp: GenericParamNode* = orig_func.generic_params[i];
            if (gp == null) { return; }
            bindings.push(TypeRef::invalid());
            method_param_names.push(gp.name);
        }
""", """        mut bindings: TypeRef[] = [];
        mut method_param_names: SymbolStr[] = [];
        for (mut i: i64 = 0; i < n_method_params; i++) {
            const gp: GenericParamNode* = orig_func.generic_params[i];
            if (gp == null) { return; }
            bindings.push(TypeRef::invalid());
            method_param_names.push(gp.name);
        }
""", 1),
("""        const spec_method: MethodNode* = this.specialize_method(
            orig_method, &method_param_names, &bindings, recv_type, impl_node);
""", """        const spec_method: MethodNode* = this.specialize_method(
            orig_method, &orig_func.generic_params, &bindings, recv_type, impl_node);
""", 1),
# the static-method path
("""        const spec_method: MethodNode* = this.specialize_method(
            orig_method, &method_param_names, method_binds, recv_type, null);
""", """        const spec_method: MethodNode* = this.specialize_method(
            orig_method, &orig_method.func.generic_params, method_binds, recv_type, null);
""", 1),
# specialize_method itself
("""    /// Clone a generic method's func, substitute its own generic params with `args`,
    /// clear generic_params, and re-resolve the signature against the owner type.
    /// Returns the spec'd MethodNode, or null if the signature didn't fully resolve.
    specialize_method(mut &this, original: MethodNode*,
                      param_names: &SymbolStr[], args: &TypeRef[],
                      owner_type: TypeRef, owner_impl: ImplBlockNode*) -> MethodNode* {
        if (original == null || original.func == null) { return null; }
        if (param_names.length != args.length) { return null; }
""", """    /// Clone a generic method's func, substitute its own generic params
    /// (`params`, the method's declared parameter nodes) with `args`, clear
    /// generic_params, and re-resolve the signature against the owner type.
    /// Returns the spec'd MethodNode, or null if the signature didn't fully resolve.
    specialize_method(mut &this, original: MethodNode*,
                      params: &GenericParamNode*[], args: &TypeRef[],
                      owner_type: TypeRef, owner_impl: ImplBlockNode*) -> MethodNode* {
        if (original == null || original.func == null) { return null; }
        if (params.length != args.length) { return null; }
        mut param_names: SymbolStr[] = [];
        mut param_syms: SymbolID[] = [];
        for (mut i: i64 = 0; i < params.length; i++) {
            if (params[i] == null) { return null; }
            param_names.push(params[i].name);
            param_syms.push(params[i].sym_id);
        }
""", 1),
("""            if (owner_type.is_valid()) { res_ctx.set_this_type(owner_type); }
            if (owner_impl != null) {
                for (mut di: i64 = 0; di < owner_impl.derived_param_names.length; di++) {
                    res_ctx.add_binding(owner_impl.derived_param_names[di],
                                        owner_impl.derived_param_types[di]);
                }
            }
""", """            if (owner_type.is_valid()) { res_ctx.set_this_type(owner_type); }
            if (owner_impl != null) {
                for (mut di: i64 = 0; di < owner_impl.derived_param_names.length; di++) {
                    res_ctx.add_binding(owner_impl.derived_param_syms[di],
                                        owner_impl.derived_param_names[di],
                                        owner_impl.derived_param_types[di]);
                }
            }
""", 1),
("""        // The substituter takes ownership of its `param_names`; `param_names` is
        // borrowed here (by ref) so the caller stays its sole owner, so hand the
        // substituter an INDEPENDENT copy rather than aliasing the caller's buffer
        // (a by-value pass would shallow-copy it and both would free it -> the
        // double-free heap-corruption wall).
        mut pn_owned: SymbolStr[] = [];
        for (mut i: i64 = 0; i < param_names.length; i++) { pn_owned.push(param_names[i]); }
        const empty_sym: SymbolStr = SymbolStr::empty();
        // No spec id: the base/spec names are empty here, so the substituter's
        // base-name rewrite cannot fire and has nothing to attach.
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            empty_sym, empty_sym, pn_owned, arg_displays,
            TypeRef::invalid());
""", """        // The substituter takes ownership of its parameter lists, so it is
        // handed INDEPENDENT copies rather than the buffers this frame still
        // owns (a by-value pass would shallow-copy them and both would free
        // them -> the double-free heap-corruption wall).
        mut pn_owned: SymbolStr[] = [];
        mut ps_owned: SymbolID[] = [];
        for (mut i: i64 = 0; i < param_names.length; i++) {
            pn_owned.push(param_names[i]);
            ps_owned.push(param_syms[i]);
        }
        const empty_sym: SymbolStr = SymbolStr::empty();
        // No spec id: the base/spec names are empty here, so the substituter's
        // base-name rewrite cannot fire and has nothing to attach.
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            empty_sym, empty_sym, pn_owned, ps_owned, arg_displays,
            TypeRef::invalid());
""", 1),
# 2449: the bound's subject against the entry's symbols
("""            const bound: TraitBound* = &tmpl_func.trait_bounds[bi];
            // Position of this bound's type parameter = index into `bindings`.
            mut idx: i64 = -1;
            for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
                if (entry.param_names[pi].equals(bound.type_parameter)) {
                    idx = pi;
                    break;
                }
            }
""", """            const bound: TraitBound* = &tmpl_func.trait_bounds[bi];
            // Position of this bound's type parameter = index into `bindings`.
            mut idx: i64 = -1;
            for (mut pi: i64 = 0; pi < entry.param_syms.length; pi++) {
                if (entry.param_syms[pi].equals(bound.subject_sym)) {
                    idx = pi;
                    break;
                }
            }
""", 1),
])

# ---------------- specializer ----------------
edit('compiler/src/compiler/mono/specializer.cryo', [
("""        // Independent copy of `param_names` (SymbolStr is Copy): the
        // substituter stores and owns this array, so handing it the template
        // entry's own buffer would double-free `entry.param_names`.
        mut pn_copy: SymbolStr[] = [];
        for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
            pn_copy.push(entry.param_names[pi]);
        }
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            entry.name, spec_sym,
            pn_copy, arg_syms, spec_typeref
        );
""", """        // Independent copies of the parameter lists (both element types are
        // Copy): the substituter stores and owns its arrays, so handing it
        // the template entry's own buffers would double-free them.
        mut pn_copy: SymbolStr[] = [];
        mut ps_copy: SymbolID[] = [];
        for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
            pn_copy.push(entry.param_names[pi]);
            ps_copy.push(entry.param_syms[pi]);
        }
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            entry.name, spec_sym,
            pn_copy, ps_copy, arg_syms, spec_typeref
        );
""", 1),
("""                        for (mut i: i64 = 0; i < gen.args.length; i++) {
                            mut spec_disp: string = "";
                            if (i < type_arg_displays.length) {
                                spec_disp = type_arg_displays[i];
                            }
                            if (!this.annotation_matches_param_or_spec(
                                gen.args[i], entry.param_names[i], spec_disp)) {
                                return true;
                            }
                        }
""", """                        for (mut i: i64 = 0; i < gen.args.length; i++) {
                            mut spec_disp: string = "";
                            if (i < type_arg_displays.length) {
                                spec_disp = type_arg_displays[i];
                            }
                            if (!this.annotation_matches_param_or_spec(
                                gen.args[i], entry.param_syms[i], spec_disp)) {
                                return true;
                            }
                        }
""", 1),
("""    /// True if `ann` is `Named(N)` where N matches either the bare outer
    /// param name (template identity) OR the spec's type-arg display
    /// (whole string or its leaf segment after `::`). The spec-display
    /// match handles the case where DefaultExpansion has already rewritten
    /// bare `T` annotations into `Named(<concrete-default>)` before the
    /// specializer runs.
    annotation_matches_param_or_spec(&this, ann: TypeAnnotation*,
                                      param_name: SymbolStr,
                                      spec_disp: string) -> boolean {
        if (ann == null) { return false; }
        match (*ann) {
            TypeAnnotation::Named(named) => {
                if (named.name.equals(param_name)) { return true; }
""", """    /// True if `ann` is `Named(N)` where N is the outer parameter `param_sym`
    /// (template identity, read off the stamp) OR spells the spec's type-arg
    /// display (whole string or its leaf segment after `::`). The
    /// spec-display match handles the case where DefaultExpansion has
    /// already rewritten bare `T` annotations into `Named(<concrete-default>)`
    /// before the specializer runs.
    annotation_matches_param_or_spec(&this, ann: TypeAnnotation*,
                                      param_sym: SymbolID,
                                      spec_disp: string) -> boolean {
        if (ann == null) { return false; }
        match (*ann) {
            TypeAnnotation::Named(named) => {
                if (param_sym.is_valid() && named.param_sym().equals(param_sym)) { return true; }
""", 1),
])

# ---------------- trait_specializer ----------------
edit('compiler/src/compiler/mono/trait_specializer.cryo', [
("""            const arg_ty: TypeRef = this.type_resolver.resolve(trait_args[i], res_ctx);
            if (!arg_ty.is_valid()) { continue; }
            res_ctx.add_binding(gp.name, arg_ty);
""", """            const arg_ty: TypeRef = this.type_resolver.resolve(trait_args[i], res_ctx);
            if (!arg_ty.is_valid()) { continue; }
            res_ctx.add_binding(gp.sym_id, gp.name, arg_ty);
""", 1),
("""                        TypeAnnotation::Named(n) => {
                            ctx.add_binding(n.name, this.arena.inst_type_arg_at(subject, i));
""", """                        TypeAnnotation::Named(n) => {
                            ctx.add_binding(n.param_sym(), n.name, this.arena.inst_type_arg_at(subject, i));
""", 1),
("""        for (mut i: i64 = 0; i < res_ctx.generic_bindings.length; i++) {
            const name: SymbolStr = res_ctx.generic_bindings[i].first;
            const ty: TypeRef = res_ctx.generic_bindings[i].second;
            if (!ty.is_valid()) { continue; }
            if (this.arena.contains_generic_param(ty)) { continue; }
            if (impl_node.lookup_derived_param(name).is_valid()) { continue; }
            impl_node.add_derived_param(name, ty);
        }
""", """        for (mut i: i64 = 0; i < res_ctx.generic_bindings.length; i++) {
            const sym: SymbolID = res_ctx.generic_bindings[i].sym;
            const ty: TypeRef = res_ctx.generic_bindings[i].ty;
            if (!ty.is_valid()) { continue; }
            if (this.arena.contains_generic_param(ty)) { continue; }
            if (impl_node.lookup_derived_param(sym).is_valid()) { continue; }
            impl_node.add_derived_param(sym, res_ctx.generic_bindings[i].name, ty);
        }
""", 1),
("""        // Copy the node's derived names: the substituter constructor moves
        // `param_names`, and the node keeps owning its `derived_param_names`.
        mut pnames: SymbolStr[] = [];
        for (mut i: i64 = 0; i < decl.derived_param_names.length; i++) {
            pnames.push(decl.derived_param_names[i]);
        }
        // No spec id: the base/spec names are empty here, so the substituter's
        // base-name rewrite cannot fire and has nothing to attach.
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            empty_sym, empty_sym,
            pnames, arg_displays, TypeRef::invalid());
""", """        // Copy the node's derived lists: the substituter constructor moves
        // its parameter arrays, and the node keeps owning its own.
        mut pnames: SymbolStr[] = [];
        mut psyms: SymbolID[] = [];
        for (mut i: i64 = 0; i < decl.derived_param_names.length; i++) {
            pnames.push(decl.derived_param_names[i]);
            psyms.push(decl.derived_param_syms[i]);
        }
        // No spec id: the base/spec names are empty here, so the substituter's
        // base-name rewrite cannot fire and has nothing to attach.
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            empty_sym, empty_sym,
            pnames, psyms, arg_displays, TypeRef::invalid());
""", 1),
("""            TypeAnnotation::Named(n) => {
                if (this.lookup_subst_for_param(n.name, subst).is_invalid()
                        && res_ctx.lookup_binding(n.name).is_invalid()) {
                    res_ctx.add_binding(n.name, concrete);
                }
            }
""", """            TypeAnnotation::Named(n) => {
                if (this.lookup_subst_for_param(n.name, subst).is_invalid()
                        && res_ctx.lookup_binding(n.param_sym()).is_invalid()) {
                    res_ctx.add_binding(n.param_sym(), n.name, concrete);
                }
            }
""", 1),
])

# ---------------- ast_resolver ----------------
edit('compiler/src/compiler/mono/ast_resolver.cryo', [
("""                    const gp_ref: TypeRef = this.arena.create_generic_param(
                        gp.name, j as u64);
                    method_ctx.add_binding(gp.name, gp_ref);
""", """                    const gp_ref: TypeRef = this.arena.create_generic_param(
                        gp.name, j as u64);
                    method_ctx.add_binding(gp.sym_id, gp.name, gp_ref);
""", 1),
])
