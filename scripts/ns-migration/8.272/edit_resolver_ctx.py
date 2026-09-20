p = 'compiler/src/compiler/types/resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:60], s.count(old))
    s = s.replace(old, new)

rep("""import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::types::arena::{ TypeArena };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
import compiler::types::arena::{ TypeArena };
""")

rep("""// ResolutionContext: context for type resolution

/// Ambient state needed while resolving type annotations.
type struct ResolutionContext {
    /// The module an instantiation resolved through this context is demanded
    /// for, so per-module codegen emits it there.
    source_file:      string;
    generic_bindings: Pair<SymbolStr, TypeRef>[];  // generic name -> bound TypeRef
""", """// ResolutionContext: context for type resolution

/// One type parameter bound to a type, keyed by the parameter's symbol.
///
/// The spelling rides along for the one consumer that mints an arena
/// parameter type from a binding (`cache_derived_trait_params`); it is
/// never what a binding is looked up by.  Two parameters may share a
/// spelling - a trait default's own `<U>` and the `implement<U>` its body is
/// copied under - and only the symbol says which one a written `U` names.
type struct GenericBinding {
    sym:  SymbolID;
    name: SymbolStr;
    ty:   TypeRef;
}

/// Ambient state needed while resolving type annotations.
type struct ResolutionContext {
    /// The module an instantiation resolved through this context is demanded
    /// for, so per-module codegen emits it there.
    source_file:      string;
    generic_bindings: GenericBinding[];
    /// An associated type of `This` bound to a type, by the member's
    /// spelling: `This::Item` in a default's body under an impl that binds
    /// `Item`.  A separate table because an associated type is not a symbol
    /// the name layer declares - its identity is the trait's and the
    /// member's name - so it cannot share the parameters' key.
    assoc_bindings:   Pair<SymbolStr, TypeRef>[];
""")

rep("""        return ResolutionContext {
            source_file:      source_file,
            generic_bindings: [],
            this_type:        TypeRef::invalid(),
            symbolic_no_demand: false,
        };
    }

    /// Add a generic binding (e.g., T -> i32).
    add_binding(mut &this, name: SymbolStr, bound_type: TypeRef) -> void {
        this.generic_bindings.push(Pair<SymbolStr, TypeRef>::new(name, bound_type));
    }

    /// Look up a generic binding by name.
    lookup_binding(&this, name: SymbolStr) -> TypeRef {
        for (mut i: i64 = 0; i < this.generic_bindings.length; i++) {
            if (this.generic_bindings[i].first.equals(name)) {
                return this.generic_bindings[i].second;
            }
        }
        return TypeRef::invalid();
    }
""", """        return ResolutionContext {
            source_file:      source_file,
            generic_bindings: [],
            assoc_bindings:   [],
            this_type:        TypeRef::invalid(),
            symbolic_no_demand: false,
        };
    }

    /// Bind the parameter `sym` was declared as to `bound_type`.  An invalid
    /// symbol names no parameter - a written arg that is a concrete type, a
    /// synthesized node nothing stamped - and binds nothing.
    add_binding(mut &this, sym: SymbolID, name: SymbolStr, bound_type: TypeRef) -> void {
        if (!sym.is_valid()) { return; }
        this.generic_bindings.push(GenericBinding { sym: sym, name: name, ty: bound_type });
    }

    /// The type the parameter `sym` is bound to, or invalid.
    lookup_binding(&this, sym: SymbolID) -> TypeRef {
        for (mut i: i64 = 0; i < this.generic_bindings.length; i++) {
            if (this.generic_bindings[i].sym.equals(sym)) {
                return this.generic_bindings[i].ty;
            }
        }
        return TypeRef::invalid();
    }

    /// Bind `This::member` to `bound_type`.
    add_assoc_binding(mut &this, member: SymbolStr, bound_type: TypeRef) -> void {
        this.assoc_bindings.push(Pair<SymbolStr, TypeRef>::new(member, bound_type));
    }

    /// The type `This::member` is bound to, or invalid.
    lookup_assoc_binding(&this, member: SymbolStr) -> TypeRef {
        for (mut i: i64 = 0; i < this.assoc_bindings.length; i++) {
            if (this.assoc_bindings[i].first.equals(member)) {
                return this.assoc_bindings[i].second;
            }
        }
        return TypeRef::invalid();
    }
""")

rep("""    /// drop-glue frees one buffer twice (double-free).  This produces an
    /// independent context with its own `generic_bindings` storage.  The
    /// element type `Pair<SymbolStr, TypeRef>` is POD (interned ids), so the
    /// per-element copies are trivial.
    clone(&this) -> ResolutionContext {
        mut out: ResolutionContext = ResolutionContext::new(this.source_file);
        out.this_type = this.this_type;
        out.symbolic_no_demand = this.symbolic_no_demand;
        for (mut i: i64 = 0; i < this.generic_bindings.length; i++) {
            out.add_binding(this.generic_bindings[i].first,
                            this.generic_bindings[i].second);
        }
        return out;
    }
""", """    /// drop-glue frees one buffer twice (double-free).  This produces an
    /// independent context with its own binding storage.  The element types
    /// are POD (interned ids), so the per-element copies are trivial.
    clone(&this) -> ResolutionContext {
        mut out: ResolutionContext = ResolutionContext::new(this.source_file);
        out.this_type = this.this_type;
        out.symbolic_no_demand = this.symbolic_no_demand;
        for (mut i: i64 = 0; i < this.generic_bindings.length; i++) {
            out.add_binding(this.generic_bindings[i].sym,
                            this.generic_bindings[i].name,
                            this.generic_bindings[i].ty);
        }
        for (mut i: i64 = 0; i < this.assoc_bindings.length; i++) {
            out.add_assoc_binding(this.assoc_bindings[i].first,
                                  this.assoc_bindings[i].second);
        }
        return out;
    }
""")

# resolve_projection: This::Member reads the assoc table
rep("""            TypeAnnotation::ThisType(_tt) => {
                const b: TypeRef = ctx.lookup_binding(proj.member);
                if (b.is_valid()) { return b; }
""", """            TypeAnnotation::ThisType(_tt) => {
                const b: TypeRef = ctx.lookup_assoc_binding(proj.member);
                if (b.is_valid()) { return b; }
""")

# 507: impl target args bound by the written arg's stamp
rep("""                    match (*ta) {
                        TypeAnnotation::Named(n) => { bctx.add_binding(n.name, base_args[ti]); }
                        _ => {}
                    }
""", """                    match (*ta) {
                        TypeAnnotation::Named(n) => { bctx.add_binding(n.param_sym(), n.name, base_args[ti]); }
                        _ => {}
                    }
""")

# 547: where-bound subject
rep("""            const bound: TraitBound* = &impl_node.where_bounds[bi];
            const subject: TypeRef = bctx.lookup_binding(bound.type_parameter);
""", """            const bound: TraitBound* = &impl_node.where_bounds[bi];
            const subject: TypeRef = bctx.lookup_binding(bound.subject_sym);
""")

# 587/588
rep("""            TypeAnnotation::Named(n) => {
                if (!bctx.lookup_binding(n.name).is_valid()) {
                    bctx.add_binding(n.name, concrete);
                }
            }
""", """            TypeAnnotation::Named(n) => {
                if (!bctx.lookup_binding(n.param_sym()).is_valid()) {
                    bctx.add_binding(n.param_sym(), n.name, concrete);
                }
            }
""")

open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
