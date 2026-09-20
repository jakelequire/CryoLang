def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

imp = """import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
"""

# ---------------- symbolic_checker ----------------
edit('compiler/src/compiler/sema/symbolic_checker.cryo', [
("""            if (p != null) { rc.add_binding(p.name, this.symbolic_param_ref(p, i as u64)); }
""", """            if (p != null) { rc.add_binding(p.sym_id, p.name, this.symbolic_param_ref(p, i as u64)); }
""", 1),
("""            if (p != null) { rc.add_binding(p.name, this.symbolic_param_ref(p, j as u64)); }
""", """            if (p != null) { rc.add_binding(p.sym_id, p.name, this.symbolic_param_ref(p, j as u64)); }
""", 1),
])

# ---------------- method_binding ----------------
edit('compiler/src/compiler/sema/method_binding.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
""" + imp, 1),
("""                        imp_ctx.add_binding(gp.name, ar);
""", """                        imp_ctx.add_binding(gp.sym_id, gp.name, ar);
""", 1),
("""                res_ctx.add_binding(gp.name, this.types.view_type_arg_at(owner_ref, i));
""", """                res_ctx.add_binding(gp.sym_id, gp.name, this.types.view_type_arg_at(owner_ref, i));
""", 1),
("""            res_ctx.add_binding(gp.name, arg_ref);
""", """            res_ctx.add_binding(gp.sym_id, gp.name, arg_ref);
""", 1),
("""                owner_ctx.add_binding(gp.name, this.types.view_type_arg_at(recv_type, i));
""", """                owner_ctx.add_binding(gp.sym_id, gp.name, this.types.view_type_arg_at(recv_type, i));
""", 1),
("""                infer_ctx.add_binding(gp.name, gp_ref);
""", """                infer_ctx.add_binding(gp.sym_id, gp.name, gp_ref);
""", 1),
("""            mut method_param_names_d: SymbolStr[] = [];
            for (mut i: i64 = 0; i < n_method_params; i++) {
                const gp: GenericParamNode* = orig_func.generic_params[i];
                if (gp == null) { return []; }
                method_param_names_d.push(gp.name);
            }
            this.ctx.type_resolver.project_where_bound_params_into(
                &orig_func.trait_bounds, &method_param_names_d, &ictx, this.ctx.source_file,
                this.state.in_symbolic_check);
""", """            mut method_param_syms_d: SymbolID[] = [];
            for (mut i: i64 = 0; i < n_method_params; i++) {
                const gp: GenericParamNode* = orig_func.generic_params[i];
                if (gp == null) { return []; }
                method_param_syms_d.push(gp.sym_id);
            }
            this.ctx.type_resolver.project_where_bound_params_into(
                &orig_func.trait_bounds, &method_param_syms_d, &ictx, this.ctx.source_file,
                this.state.in_symbolic_check);
""", 1),
("""            ret_ctx.add_binding(gp.name, method_bindings[i]);
""", """            ret_ctx.add_binding(gp.sym_id, gp.name, method_bindings[i]);
""", 1),
("""                            rc.add_binding(n.name, this.arena.inst_type_arg_at(subject, i));
""", """                            rc.add_binding(n.param_sym(), n.name, this.arena.inst_type_arg_at(subject, i));
""", 1),
("""                        && res_ctx.lookup_binding(n.name).is_invalid()) {
                    res_ctx.add_binding(n.name, concrete);
""", """                        && res_ctx.lookup_binding(n.param_sym()).is_invalid()) {
                    res_ctx.add_binding(n.param_sym(), n.name, concrete);
""", 1),
("""                        rc.add_binding(n.name, this.arena.inst_type_arg_at(subject_ref, i));
""", """                        rc.add_binding(n.param_sym(), n.name, this.arena.inst_type_arg_at(subject_ref, i));
""", 1),
])

# ---------------- call_resolver ----------------
edit('compiler/src/compiler/sema/call_resolver.cryo', [
("""            pctx.add_binding(gp.name, bindings[i]);
""", """            pctx.add_binding(gp.sym_id, gp.name, bindings[i]);
""", 1),
("""    /// `TypeResolver::project_where_bound_params_into`, which the static + instance
    /// generic-method paths share; the template's `param_names` are index-aligned
    /// with `ictx.bindings`, exactly as that helper requires.
    project_where_bound_params(mut &this, tmpl: FunctionDeclNode*, entry: TemplateEntry*,
                               ictx: InferCtx*) -> void {
        if (this.ctx.type_resolver == null) { return; }
        this.ctx.type_resolver.project_where_bound_params_into(
            &tmpl.trait_bounds, &entry.param_names, ictx, this.ctx.source_file,
            this.state.in_symbolic_check);
    }
""", """    /// `TypeResolver::project_where_bound_params_into`, which the static + instance
    /// generic-method paths share; the template's `param_syms` are index-aligned
    /// with `ictx.bindings`, exactly as that helper requires.
    project_where_bound_params(mut &this, tmpl: FunctionDeclNode*, entry: TemplateEntry*,
                               ictx: InferCtx*) -> void {
        if (this.ctx.type_resolver == null) { return; }
        this.ctx.type_resolver.project_where_bound_params_into(
            &tmpl.trait_bounds, &entry.param_syms, ictx, this.ctx.source_file,
            this.state.in_symbolic_check);
    }
""", 1),
("""            for (mut pi: i64 = 0; pi < tmpl.param_names.length; pi++) {
                if (tmpl.param_names[pi].equals(bound.type_parameter)) { concrete = inst.type_args[pi]; break; }
            }
""", """            for (mut pi: i64 = 0; pi < tmpl.param_syms.length; pi++) {
                if (tmpl.param_syms[pi].equals(bound.subject_sym)) { concrete = inst.type_args[pi]; break; }
            }
""", 2),
("""    /// E0306 where-clause bound check for a generic call whose bound args are in
    /// `bindings` (declaration order, index-aligned with `entry.param_names`).
""", """    /// E0306 where-clause bound check for a generic call whose bound args are in
    /// `bindings` (declaration order, index-aligned with `entry.param_syms`).
""", 1),
("""            mut idx: i64 = -1;
            for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
                if (entry.param_names[pi].equals(bound.type_parameter)) { idx = pi; break; }
            }
""", """            mut idx: i64 = -1;
            for (mut pi: i64 = 0; pi < entry.param_syms.length; pi++) {
                if (entry.param_syms[pi].equals(bound.subject_sym)) { idx = pi; break; }
            }
""", 1),
])

# ---------------- type_resolution ----------------
edit('compiler/src/compiler/passes/type_resolution.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
""" + imp, 1),
("""        for (mut i: i64 = 0; i < generic_params.length; i++) {
            const gp: GenericParamNode* = generic_params[i];
            res_ctx.add_binding(gp.name, refs[i]);
        }
""", """        for (mut i: i64 = 0; i < generic_params.length; i++) {
            const gp: GenericParamNode* = generic_params[i];
            res_ctx.add_binding(gp.sym_id, gp.name, refs[i]);
        }
""", 1),
("""                const arg_ty: TypeRef = this.resolver.resolve(gen.args[i], res_ctx);
                if (!arg_ty.is_valid()) { continue; }
                res_ctx.add_binding(gp.name, arg_ty);
""", """                const arg_ty: TypeRef = this.resolver.resolve(gen.args[i], res_ctx);
                if (!arg_ty.is_valid()) { continue; }
                res_ctx.add_binding(gp.sym_id, gp.name, arg_ty);
""", 1),
("""        // Associated-type positional sugar (plan rule #6): a trait with 0
        // generic params and exactly 1 associated type binds that associated
        // type from a single positional arg - `implement Iterator<T>` =>
        // `Item -> T`. Binds `Item` in the impl context so `This::Item` (and
        // cloned trait default methods) resolve, and records the binding on
        // the impl node so a concrete `X::Item` projection can look it up.
        if (trait_decl.assoc_types.length == 1 && nargs == 1) {
            const adecl: AssocTypeDeclNode* = trait_decl.assoc_types[0];
            if (adecl != null) {
                const arg_ty: TypeRef = this.resolver.resolve(gen.args[0], res_ctx);
                if (arg_ty.is_valid()) {
                    res_ctx.add_binding(adecl.name, arg_ty);
                }
""", """        // Associated-type positional sugar: a trait with 0 generic params and
        // exactly 1 associated type binds that associated type from a single
        // positional arg - `implement Iterator<T>` => `Item -> T`. Binds
        // `This::Item` in the impl context so a cloned trait default's body
        // resolves it, and records the binding on the impl node so a
        // concrete `X::Item` projection can look it up.
        if (trait_decl.assoc_types.length == 1 && nargs == 1) {
            const adecl: AssocTypeDeclNode* = trait_decl.assoc_types[0];
            if (adecl != null) {
                const arg_ty: TypeRef = this.resolver.resolve(gen.args[0], res_ctx);
                if (arg_ty.is_valid()) {
                    res_ctx.add_assoc_binding(adecl.name, arg_ty);
                }
""", 1),
("""    /// for a concrete instantiation.  `param_names[i]` maps to `arg_anns[i]`;
    /// complex args (e.g. `Pair<u64, A>`) are deep-cloned into the slot.
    static rewrite_trait_params_annotation(ann: TypeAnnotation*,
                                            param_names: &SymbolStr[],
                                            arg_anns: &TypeAnnotation*[]) -> void {
        if (ann == null) { return; }
        match (*ann) {
            TypeAnnotation::Named(n) => {
                // Pre-resolved leaves already map to a concrete TypeRef and
                // can't be a trait param awaiting substitution.
                if (n.pre_resolved.is_valid()) { return; }
                for (mut i: i64 = 0; i < param_names.length; i++) {
                    if (n.name.equals(param_names[i])) {
""", """    /// for a concrete instantiation.  `param_syms[i]` maps to `arg_anns[i]`;
    /// complex args (e.g. `Pair<u64, A>`) are deep-cloned into the slot.
    static rewrite_trait_params_annotation(ann: TypeAnnotation*,
                                            param_syms: &SymbolID[],
                                            arg_anns: &TypeAnnotation*[]) -> void {
        if (ann == null) { return; }
        match (*ann) {
            TypeAnnotation::Named(n) => {
                // Pre-resolved leaves already map to a concrete TypeRef and
                // can't be a trait param awaiting substitution.
                if (n.pre_resolved.is_valid()) { return; }
                const sym: SymbolID = n.param_sym();
                if (!sym.is_valid()) { return; }
                for (mut i: i64 = 0; i < param_syms.length; i++) {
                    if (sym.equals(param_syms[i])) {
""", 1),
("""            TypeAnnotation::Reference(r) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    r.inner, param_names, arg_anns);
            }
            TypeAnnotation::Pointer(p) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    p.inner, param_names, arg_anns);
            }
            TypeAnnotation::Array(a) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    a.element, param_names, arg_anns);
            }
            TypeAnnotation::Generic(g) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    g.base, param_names, arg_anns);
                for (mut i: i64 = 0; i < g.args.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        g.args[i], param_names, arg_anns);
                }
            }
            TypeAnnotation::Tuple(tu) => {
                for (mut i: i64 = 0; i < tu.elements.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        tu.elements[i], param_names, arg_anns);
                }
            }
            TypeAnnotation::Function(f) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    f.return_type, param_names, arg_anns);
                for (mut i: i64 = 0; i < f.parameters.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        f.parameters[i], param_names, arg_anns);
                }
            }
""", """            TypeAnnotation::Reference(r) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    r.inner, param_syms, arg_anns);
            }
            TypeAnnotation::Pointer(p) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    p.inner, param_syms, arg_anns);
            }
            TypeAnnotation::Array(a) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    a.element, param_syms, arg_anns);
            }
            TypeAnnotation::Generic(g) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    g.base, param_syms, arg_anns);
                for (mut i: i64 = 0; i < g.args.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        g.args[i], param_syms, arg_anns);
                }
            }
            TypeAnnotation::Tuple(tu) => {
                for (mut i: i64 = 0; i < tu.elements.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        tu.elements[i], param_syms, arg_anns);
                }
            }
            TypeAnnotation::Function(f) => {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    f.return_type, param_syms, arg_anns);
                for (mut i: i64 = 0; i < f.parameters.length; i++) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        f.parameters[i], param_syms, arg_anns);
                }
            }
""", 1),
("""    static rewrite_default_method_signature(cloned: FunctionDeclNode*,
                                            target: SymbolStr,
                                            target_args: &TypeAnnotation*[],
                                            param_names: &SymbolStr[],
                                            arg_anns: &TypeAnnotation*[],
                                            home_span: SourceSpan,
                                            home_res: ResSlot) -> void {
        if (cloned == null) { return; }
        const ret_ann: TypeAnnotation* = cloned.return_type_annotation;
        if (ret_ann != null) {
            TypeResolutionPasses::rewrite_this_type_annotation(ret_ann, target, target_args, home_span, home_res);
            if (param_names.length > 0) {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    ret_ann, param_names, arg_anns);
            }
        }
        for (mut p: i64 = 0; p < cloned.parameters.length; p++) {
            const param: VarDeclNode* = cloned.parameters[p];
            if (param != null && param.type_annotation != null) {
                TypeResolutionPasses::rewrite_this_type_annotation(
                    param.type_annotation, target, target_args, home_span, home_res);
                if (param_names.length > 0) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        param.type_annotation, param_names, arg_anns);
                }
            }
        }
    }
""", """    static rewrite_default_method_signature(cloned: FunctionDeclNode*,
                                            target: SymbolStr,
                                            target_args: &TypeAnnotation*[],
                                            param_syms: &SymbolID[],
                                            arg_anns: &TypeAnnotation*[],
                                            home_span: SourceSpan,
                                            home_res: ResSlot) -> void {
        if (cloned == null) { return; }
        const ret_ann: TypeAnnotation* = cloned.return_type_annotation;
        if (ret_ann != null) {
            TypeResolutionPasses::rewrite_this_type_annotation(ret_ann, target, target_args, home_span, home_res);
            if (param_syms.length > 0) {
                TypeResolutionPasses::rewrite_trait_params_annotation(
                    ret_ann, param_syms, arg_anns);
            }
        }
        for (mut p: i64 = 0; p < cloned.parameters.length; p++) {
            const param: VarDeclNode* = cloned.parameters[p];
            if (param != null && param.type_annotation != null) {
                TypeResolutionPasses::rewrite_this_type_annotation(
                    param.type_annotation, target, target_args, home_span, home_res);
                if (param_syms.length > 0) {
                    TypeResolutionPasses::rewrite_trait_params_annotation(
                        param.type_annotation, param_syms, arg_anns);
                }
            }
        }
    }
""", 1),
("""        mut trait_param_names: SymbolStr[] = [];
        if (arg_count == trait_decl.generic_params.length) {
            for (mut gi: i64 = 0; gi < trait_decl.generic_params.length; gi++) {
                const gp: GenericParamNode* = trait_decl.generic_params[gi];
                if (gp == null) {
                    trait_param_names = [];
                    break;
                }
                trait_param_names.push(gp.name);
            }
        }
""", """        mut trait_param_syms: SymbolID[] = [];
        if (arg_count == trait_decl.generic_params.length) {
            for (mut gi: i64 = 0; gi < trait_decl.generic_params.length; gi++) {
                const gp: GenericParamNode* = trait_decl.generic_params[gi];
                if (gp == null) {
                    trait_param_syms = [];
                    break;
                }
                trait_param_syms.push(gp.sym_id);
            }
        }
""", 1),
("""                trait_param_names, args_ref, this_home_span, impl_node.res);
""", """                trait_param_syms, args_ref, this_home_span, impl_node.res);
""", 1),
("""                        const gp: GenericParamNode* = node.generic_params[gpi];
                        if (gp == null) { continue; }
                        res_ctx.add_binding(gp.name, refs[gpi]);
""", """                        const gp: GenericParamNode* = node.generic_params[gpi];
                        if (gp == null) { continue; }
                        res_ctx.add_binding(gp.sym_id, gp.name, refs[gpi]);
""", 1),
])
