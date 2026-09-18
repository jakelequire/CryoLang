"""Unit 2: delete func_returns, register_function, lookup_func_return (index and
TypeUtils); every reader takes the return off the one signature map.  Every
anchor must match exactly once."""
import io
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

edit(R + r"\decl_index.cryo", [
("""type struct DeclarationIndex {
    // Function name -> return type
    func_returns:   HashMap<u32, TypeRef>;

    // Function name -> FunctionType TypeRef (full signature in the arena)
""",
"""type struct DeclarationIndex {
    // Function name -> FunctionType TypeRef (full signature in the arena)
"""),
("""            func_returns:        HashMap<u32, TypeRef>::new(),
""", ""),
("""    /// Register a function's return type.
    register_function(mut &this, name: SymbolStr, return_type: TypeRef) -> void {
        this.func_returns.insert(name.id, return_type);
    }

""", ""),
("""            this.register_function(combined_sym, func.resolved_return_type);

""", ""),
("""    // Lookups (called by Sema and future passes)

    /// Look up a function's return type by name.
    lookup_func_return(&this, name: SymbolStr) -> TypeRef {
        return match (this.func_returns.get(&name.id)) {
            Option::Some(val) => { val }
            Option::None      => { TypeRef::invalid() }
        };
    }

    /// Look up a function's FunctionType TypeRef (full signature).
""",
"""    // Lookups (called by Sema and future passes)

    /// Look up a function's FunctionType TypeRef (full signature).  A
    /// function's return type is this signature's; there is no second map
    /// keyed by the same name that could answer differently.
"""),
("""                                symbol: SymbolStr, span: SourceSpan) -> OverloadId {
        this.register_function(name, return_type);
        this.note_callable_name(name);
""",
"""                                symbol: SymbolStr, span: SourceSpan) -> OverloadId {
        this.note_callable_name(name);
"""),
])

edit(R + r"\passes\type_resolution.cryo", [
("""                    if (!is_source_decl && !q_name.equals(func.name)) {
                        ctx.decl_index.register_function(func.name, func.resolved_return_type);
                        ctx.decl_index.register_signature(
""",
"""                    if (!is_source_decl && !q_name.equals(func.name)) {
                        ctx.decl_index.register_signature(
"""),
])

edit(R + r"\sema\type_utils.cryo", [
("""    // -- DeclarationIndex lookup delegates --

    lookup_func_return(&this, name: SymbolStr) -> TypeRef {
        return this.ctx.decl_index.lookup_func_return(name);
    }

""",
"""    // -- DeclarationIndex lookup delegates --

"""),
])

edit(R + r"\sema\call_resolver.cryo", [
# 964: the rescue arm - a family pin with no signature is a family with no
# registration, and the signature map is the one map.
("""                // so the `![implicit]` conversions below run for it as for a
                // declaration; a family registered with a return alone
                // answers that.  A declaration pin answers through its
                // entry's own signature.
                mut pinned_ft: TypeRef = TypeRef::invalid();
                match (&call.resolved_callee) {
                    CalleePin::Family(fam) => {
                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);
                        if (!pinned_ft.is_valid()) {
                            const spec_ret: TypeRef = this.types.lookup_func_return(fam);
                            if (spec_ret.is_valid()) { return spec_ret; }
                        }
                    }
""",
"""                // so the `![implicit]` conversions below run for it as for a
                // declaration.  A declaration pin answers through its
                // entry's own signature.
                mut pinned_ft: TypeRef = TypeRef::invalid();
                match (&call.resolved_callee) {
                    CalleePin::Family(fam) => {
                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);
                    }
"""),
# 997
("""                    call.set_resolved_callee(CalleePin::Family(spec_sym));
                    const spec_ret: TypeRef = this.types.lookup_func_return(spec_sym);
                    if (spec_ret.is_valid()) { return spec_ret; }
                }
            }
""",
"""                    call.set_resolved_callee(CalleePin::Family(spec_sym));
                    const spec_ret: TypeRef = this.family_return_type(spec_sym);
                    if (spec_ret.is_valid()) { return spec_ret; }
                }
            }
"""),
# 4969
("""        const q_sym: SymbolStr = this.ctx.intern(scope_str + "::" + member_str);
        const ret: TypeRef = this.types.lookup_func_return(q_sym);
        if (ret.is_valid()) {
            scope.set_resolved_type(ret);
""",
"""        const q_sym: SymbolStr = this.ctx.intern(scope_str + "::" + member_str);
        const ret: TypeRef = this.family_return_type(q_sym);
        if (ret.is_valid()) {
            scope.set_resolved_type(ret);
"""),
# 5016
("""        if (!this.types.lookup_func_return(q).is_valid()) { return SymbolStr::empty(); }
        return q;
""",
"""        if (!this.family_return_type(q).is_valid()) { return SymbolStr::empty(); }
        return q;
"""),
# 5057
("""        const q_sym: SymbolStr = this.resolve_module_qualified_symbol(scope_name, member_name, scope);
        if (!q_sym.is_valid()) { return TypeRef::invalid(); }
        const ret: TypeRef = this.types.lookup_func_return(q_sym);
""",
"""        const q_sym: SymbolStr = this.resolve_module_qualified_symbol(scope_name, member_name, scope);
        if (!q_sym.is_valid()) { return TypeRef::invalid(); }
        const ret: TypeRef = this.family_return_type(q_sym);
"""),
# 5091
("""            if (spec_sym.is_valid()) {
                const spec_ret: TypeRef = this.types.lookup_func_return(spec_sym);
                if (spec_ret.is_valid()) { call.set_resolved_callee(CalleePin::Family(spec_sym)); return spec_ret; }
""",
"""            if (spec_sym.is_valid()) {
                const spec_ret: TypeRef = this.family_return_type(spec_sym);
                if (spec_ret.is_valid()) { call.set_resolved_callee(CalleePin::Family(spec_sym)); return spec_ret; }
"""),
# the one reader, beside overload_return_type
("""    overload_return_type(&this, fn_ref: TypeRef) -> TypeRef {
        const ft: Type* = this.arena.lookup(fn_ref.id);
        if (ft == null || ft.kind != TypeKind::Function) { return TypeRef::invalid(); }
        return (ft as FunctionType*).return_type;
    }
""",
"""    overload_return_type(&this, fn_ref: TypeRef) -> TypeRef {
        const ft: Type* = this.arena.lookup(fn_ref.id);
        if (ft == null || ft.kind != TypeKind::Function) { return TypeRef::invalid(); }
        return (ft as FunctionType*).return_type;
    }

    /// The return type of the signature registered under the family key
    /// `family`, or invalid when none is: the one map a family answers
    /// from, so a family with a signature and no return, or a return and
    /// no signature, cannot exist.
    family_return_type(&this, family: SymbolStr) -> TypeRef {
        return this.overload_return_type(this.types.lookup_func_type_exact(family));
    }
"""),
])
print("ok")
