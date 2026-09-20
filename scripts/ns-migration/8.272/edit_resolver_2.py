p = 'compiler/src/compiler/types/resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

# --- the where-bound binder: param list by symbol ---
rep("""    /// `param_names[i]` MUST align with `ictx.bindings[i]` (the caller builds the
    /// InferCtx from the same ordered param list). Index-keyed analogue of the
    /// impl-side `derive_where_assoc_bindings`; an existing binding is kept.
""", """    /// `param_syms[i]` MUST align with `ictx.bindings[i]` (the caller builds the
    /// InferCtx from the same ordered param list). Index-keyed analogue of the
    /// impl-side `derive_where_assoc_bindings`; an existing binding is kept.
""")
rep("""    project_where_bound_params_into(&this, trait_bounds: &TraitBound[],
                                    param_names: &SymbolStr[], ictx: InferCtx*,
                                    source_file: string, symbolic: boolean) -> void {
        if (this.generic_registry == null) { return; }
        for (mut bi: i64 = 0; bi < trait_bounds.length; bi++) {
            const bound: TraitBound* = &trait_bounds[bi];
            if (bound.has_projection_subject()) { continue; }
            const s: i64 = this.param_name_slot(param_names, bound.type_parameter);
""", """    project_where_bound_params_into(&this, trait_bounds: &TraitBound[],
                                    param_syms: &SymbolID[], ictx: InferCtx*,
                                    source_file: string, symbolic: boolean) -> void {
        if (this.generic_registry == null) { return; }
        for (mut bi: i64 = 0; bi < trait_bounds.length; bi++) {
            const bound: TraitBound* = &trait_bounds[bi];
            if (bound.has_projection_subject()) { continue; }
            const s: i64 = this.param_slot(param_syms, bound.subject_sym);
""")
rep("""                    const j: i64 = this.where_arg_param_slot(arg_ann, param_names);
""", """                    const j: i64 = this.where_arg_param_slot(arg_ann, param_syms);
""")
rep("""                        if (!this.where_arg_has_unbound_param(arg_ann, param_names, ictx)) { continue; }
""", """                        if (!this.where_arg_has_unbound_param(arg_ann, param_syms, ictx)) { continue; }
""")
rep("""                        this.bind_where_arg_params_from(arg_ann, param_names, derived, ictx);
""", """                        this.bind_where_arg_params_from(arg_ann, param_syms, derived, ictx);
""")
rep("""    /// Index of `name` in an ordered generic-param-name list, or -1. The list,
    /// the caller's param-id list, and the InferCtx's `bindings` share one index
    /// space (all built from the same ordered generic params).
    param_name_slot(&this, param_names: &SymbolStr[], name: SymbolStr) -> i64 {
        for (mut i: i64 = 0; i < param_names.length; i++) {
            if (param_names[i].equals(name)) { return i; }
        }
        return -1;
    }

    /// The param-list index a where-bound trait arg names, or -1 when it is not a
    /// bare `Named` parameter reference (a wrapped/concrete arg derives nothing).
    where_arg_param_slot(&this, ta: TypeAnnotation*, param_names: &SymbolStr[]) -> i64 {
        if (ta == null) { return -1; }
        match (*ta) {
            TypeAnnotation::Named(n) => { return this.param_name_slot(param_names, n.name); }
            _                       => { return -1; }
        }
    }
""", """    /// Index of the parameter `sym` in an ordered generic-param list, or -1.
    /// The list, the caller's arena param-id list, and the InferCtx's
    /// `bindings` share one index space (all built from the same ordered
    /// generic params).  An invalid symbol - a spelling that is no parameter
    /// of this list's owner - is in no slot.
    param_slot(&this, param_syms: &SymbolID[], sym: SymbolID) -> i64 {
        if (!sym.is_valid()) { return -1; }
        for (mut i: i64 = 0; i < param_syms.length; i++) {
            if (param_syms[i].equals(sym)) { return i; }
        }
        return -1;
    }

    /// The param-list index a where-bound trait arg names, or -1 when it is not a
    /// bare `Named` parameter reference (a wrapped/concrete arg derives nothing).
    where_arg_param_slot(&this, ta: TypeAnnotation*, param_syms: &SymbolID[]) -> i64 {
        if (ta == null) { return -1; }
        match (*ta) {
            TypeAnnotation::Named(n) => { return this.param_slot(param_syms, n.param_sym()); }
            _                       => { return -1; }
        }
    }
""")
rep("""    where_arg_has_unbound_param(&this, ta: TypeAnnotation*, param_names: &SymbolStr[],
                                ictx: InferCtx*) -> boolean {
        if (ta == null) { return false; }
        match (*ta) {
            TypeAnnotation::Named(n) => {
                const s: i64 = this.param_name_slot(param_names, n.name);
                if (s < 0 || s >= ictx.bindings.length) { return false; }
                return !ictx.bindings[s].is_valid();
            }
            TypeAnnotation::Generic(g) => {
                for (mut i: i64 = 0; i < g.args.length; i++) {
                    if (this.where_arg_has_unbound_param(g.args[i], param_names, ictx)) { return true; }
""", """    where_arg_has_unbound_param(&this, ta: TypeAnnotation*, param_syms: &SymbolID[],
                                ictx: InferCtx*) -> boolean {
        if (ta == null) { return false; }
        match (*ta) {
            TypeAnnotation::Named(n) => {
                const s: i64 = this.param_slot(param_syms, n.param_sym());
                if (s < 0 || s >= ictx.bindings.length) { return false; }
                return !ictx.bindings[s].is_valid();
            }
            TypeAnnotation::Generic(g) => {
                for (mut i: i64 = 0; i < g.args.length; i++) {
                    if (this.where_arg_has_unbound_param(g.args[i], param_syms, ictx)) { return true; }
""")
rep("""    bind_where_arg_params_from(&this, ta: TypeAnnotation*, param_names: &SymbolStr[],
                               actual: TypeRef, ictx: InferCtx*) -> void {
        if (ta == null || !actual.is_valid()) { return; }
        match (*ta) {
            TypeAnnotation::Named(n) => {
                const s: i64 = this.param_name_slot(param_names, n.name);
                if (s >= 0) { ictx.bind_slot(s, actual); }
            }
""", """    bind_where_arg_params_from(&this, ta: TypeAnnotation*, param_syms: &SymbolID[],
                               actual: TypeRef, ictx: InferCtx*) -> void {
        if (ta == null || !actual.is_valid()) { return; }
        match (*ta) {
            TypeAnnotation::Named(n) => {
                const s: i64 = this.param_slot(param_syms, n.param_sym());
                if (s >= 0) { ictx.bind_slot(s, actual); }
            }
""")
rep("""                for (mut i: i64 = 0; i < g.args.length; i++) {
                    this.bind_where_arg_params_from(g.args[i], param_names,
                                                    this.arena.inst_type_arg_at(actual, i), ictx);
""", """                for (mut i: i64 = 0; i < g.args.length; i++) {
                    this.bind_where_arg_params_from(g.args[i], param_syms,
                                                    this.arena.inst_type_arg_at(actual, i), ictx);
""")

# --- gp.name writers ---
rep("""                    method_ctx.add_binding(gp.name, gp_ref);
""", """                    method_ctx.add_binding(gp.sym_id, gp.name, gp_ref);
""", 2)
rep("""                def_ctx.add_binding(entry.param_names[j], filled[j]);
""", """                def_ctx.add_binding(entry.param_syms[j], entry.param_names[j], filled[j]);
""")
rep("""                        alias_ctx.add_binding(entry.param_names[i], args[i]);
""", """                        alias_ctx.add_binding(entry.param_syms[i], entry.param_names[i], args[i]);
""")

# --- resolve_named: the stamp is asked first, and it is asked by symbol ---
rep("""    /// Resolve a named type (user-defined or generic binding).
    ///
    /// In order:
    ///   1. Generic bindings (T -> i32 from enclosing generic context)
    ///   2. Primitive name check (a primitive spelling that reached Named
    ///      position carrying no TypeRef of its own)
    ///   2c. The name layer's stamp on the node - what the written spelling
    ///      names, decided where its imports were in hand
""", """    /// Resolve a named type (user-defined or generic binding).
    ///
    /// In order:
    ///   1. Generic bindings (T -> i32 from enclosing generic context), asked
    ///      by the parameter the stamp names, never by the spelling
    ///   2. Primitive name check (a primitive spelling that reached Named
    ///      position carrying no TypeRef of its own)
    ///   2c. The name layer's stamp on the node - what the written spelling
    ///      names, decided where its imports were in hand
""")
rep("""        LOG_DEBUG(LogComponent::TypeChecker, "resolve_named() name='%s'", name_str);
        // 1. Check generic bindings first (O(1) SymbolStr comparison)
        mut binding: TypeRef = ctx.lookup_binding(name);
        if (binding.is_valid()) {
            return binding;
        }

        // 1b. Associated-type projection on a generic parameter: `I::Item`,
        //     where `I` is a generic parameter in scope. Such a projection is
        //     parsed as a FLAT qualified Named (only `This::Item` becomes a
        //     Projection node), so detect it here: if the segment before the
        //     first `::` binds to a type in scope, project the remainder off it.
        //     A real module-qualified type (`core::iter::Foo`) has a prefix
        //     (`core`) that is NOT a generic-param binding, so it is unaffected.
        if (name_len > 2) {
            mut sep: i64 = -1;
            mut k: i64 = 0;
            while (k + 1 < name_len) {
                if (name_str[k] == ':' && name_str[k + 1] == ':') { sep = k; break; }
                k = k + 1;
            }
            if (sep > 0) {
                const prefix_str: string = name_str.substring(0, sep as u32);
                const prefix_sym: SymbolStr = this.intern_table.intern(prefix_str);
                const prefix_ty: TypeRef = ctx.lookup_binding(prefix_sym);
                if (prefix_ty.is_valid()) {
""", """        LOG_DEBUG(LogComponent::TypeChecker, "resolve_named() name='%s'", name_str);
        // 1. A type parameter, bound in the enclosing generic context.  The
        //    stamp says WHICH parameter the spelling names; the binding
        //    table is keyed by that and holds nothing under a spelling, so a
        //    trait default's own `<U>` copied under an `implement<U>` reads
        //    its own binding and not the impl's.
        mut binding: TypeRef = ctx.lookup_binding(slot.generic_param_sym());
        if (binding.is_valid()) {
            return binding;
        }

        // 1b. Associated-type projection on a generic parameter: `I::Item`,
        //     where `I` is a generic parameter in scope. Such a projection is
        //     parsed as a FLAT qualified Named (only `This::Item` becomes a
        //     Projection node), and the stamp says the type layer owns the
        //     rest of a path rooted at that parameter: if the parameter is
        //     bound, project the remainder off its binding.  A real
        //     module-qualified type (`core::iter::Foo`) is rooted at no
        //     parameter, so it is unaffected.
        if (name_len > 2) {
            mut sep: i64 = -1;
            mut k: i64 = 0;
            while (k + 1 < name_len) {
                if (name_str[k] == ':' && name_str[k + 1] == ':') { sep = k; break; }
                k = k + 1;
            }
            if (sep > 0) {
                const prefix_ty: TypeRef = ctx.lookup_binding(slot.relative_param_sym());
                if (prefix_ty.is_valid()) {
""")

open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
