p = 'compiler/src/compiler/sema/sema.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""        match (ident.res.require("sema/resolve_identifier")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
            fmt::format("cannot find value `%s` in this scope", name_str));
""", """        match (ident.res.require("sema/resolve_identifier")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        // Bound to a function whose signature was refused where it was
        // written: it registered no type to take a value by, and it exists.
        if (this.ctx.decl_index.signature_refused_of_def(def_id)) { return TypeRef::invalid(); }
        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
            fmt::format("cannot find value `%s` in this scope", name_str));
""")

rep("""        const static_ft: TypeRef = this.scope_static_method_type(found, scope);
        if (static_ft.is_valid()) {
            return this.calls.pin_scope_value_static_method(scope, found, static_ft);
        }
""", """        const static_ft: TypeRef = this.scope_static_method_type(found, scope);
        if (static_ft.is_valid()) {
            // The owner's method table holds every declared method, a
            // refused signature's included, as `() -> ?`; the index holds
            // the refusal, and a value taken from it reports nothing more.
            if (this.calls.scope_callee_signature_refused(scope, false)) { return TypeRef::invalid(); }
            return this.calls.pin_scope_value_static_method(scope, found, static_ft);
        }
""")

rep("""        if (this.state.in_symbolic_check) { return found; }
        if (this.symbolic.symbolic_name_is_generic_param(scope.scope_name)) { return found; }
        const path_str: string = fmt::format("%s::%s", scope_str, member_str);
        mut miss: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
""", """        if (this.state.in_symbolic_check) { return found; }
        if (this.symbolic.symbolic_name_is_generic_param(scope.scope_name)) { return found; }
        // A module's function whose signature was refused where it was
        // written registered none, and the value reports nothing more.
        mut names_module: boolean = false;
        match (scope.require_scope_res("scope-value owner kind")) {
            Res::Def(_) => { names_module = true; }
            _           => { }
        }
        if (this.calls.scope_callee_signature_refused(scope, names_module)) { return found; }
        const path_str: string = fmt::format("%s::%s", scope_str, member_str);
        mut miss: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
