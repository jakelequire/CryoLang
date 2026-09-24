#!/usr/bin/env python3
"""Delete `BoundedParamType` and every branch that handles it (section 8.323).

Nothing constructs one: the only `new BoundedParamType` is inside
`TypeArena::create_bounded_param`, which has no caller, and every other `Type`
constructor passes a literal kind.  Each edit below is an exact replacement
asserted to match ONCE, so a second run refuses rather than editing twice.

    python scripts/ns-migration/8.323/delete_bounded_param.py          # apply
    python scripts/ns-migration/8.323/delete_bounded_param.py --check  # 0 = applied
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
C = "compiler/src/compiler/"

EDITS = [
    # -- the type itself ----------------------------------------------------
    (C + "types/generic.cryo",
     """// BoundedParamType: Generic parameter with trait bounds (e.g., T: Clone + Debug)

type class BoundedParamType : Type {
public:
    param_name:  SymbolStr;
    param_index: u64;
    bounds:      TypeRef[];   // Each points to a TraitType

    BoundedParamType(id: u64, param_name: SymbolStr, param_index: u64, bounds: TypeRef[])
        : Type(id, TypeKind::BoundedParam) {
        this.param_name  = param_name;
        this.param_index = param_index;
        this.bounds      = bounds;
    }


    override display_name(&this) -> string {
        return fmt::format("T%lu", this.param_index);
    }

    override size_bytes(&this) -> u64 { return 0; }
    override alignment(&this) -> u64  { return 0; }

    /// Bounded parameters are NOT resolved until instantiation.
    override is_resolved(&this) -> boolean { return false; }
}
""", ""),
    (C + "types/_module.cryo",
     "    GenericParam; BoundedParam; InstantiatedType;",
     "    GenericParam; InstantiatedType;"),
    (C + "types/_module.cryo",
     '            TypeKind::BoundedParam     => { "BoundedParam" }\n', ""),
    (C + "types/_module.cryo",
     "            TypeKind::BoundedParam     => { true }\n", ""),

    # -- the arena ----------------------------------------------------------
    (C + "types/arena.cryo",
     "    AssocProjectionType, BoundedParamType, ErrorType,",
     "    AssocProjectionType, ErrorType,"),
    (C + "types/arena.cryo",
     "    //   * GenericParam / BoundedParam - keyed by NAME ONLY, dropping `index`\n"
     "    //     and `bounds`.",
     "    //   * GenericParam - keyed by NAME ONLY, dropping `index`."),
    (C + "types/arena.cryo",
     "    bounded_param_cache:   HashMap<u64, TypeRef>;\n", ""),
    (C + "types/arena.cryo",
     "            bounded_param_cache: HashMap::<u64, TypeRef>::new(),\n", ""),
    (C + "types/arena.cryo",
     """    /// Get or create a bounded generic parameter type, deduplicated by NAME
    /// (see `create_generic_param` for why leaves are name-canonical).  The
    /// bounds and `param_index` from the FIRST creation win; the checker already
    /// compares BoundedParams by name alone, so this is consistent.
    create_bounded_param(mut &this, name: SymbolStr, index: u64, bounds: TypeRef[]) -> TypeRef {
        const key: u64 = name.id as u64;

        const cached: Option<TypeRef> = this.bounded_param_cache.get(&key);
        if (cached.is_some()) { return cached.unwrap(); }

        mut ref: TypeRef = this.alloc_type(new BoundedParamType(this.next_id, name, index, bounds));
        this.bounded_param_cache.insert(key, ref);
        return ref;
    }

""", ""),
    (C + "types/arena.cryo",
     "    /// Recursively check whether a type contains any GenericParam or\n"
     "    /// BoundedParam at any depth.",
     "    /// Recursively check whether a type contains any GenericParam at any\n"
     "    /// depth."),
    (C + "types/arena.cryo",
     "        if (t.kind == TypeKind::GenericParam) { return true; }\n"
     "        if (t.kind == TypeKind::BoundedParam) { return true; }\n",
     "        if (t.kind == TypeKind::GenericParam) { return true; }\n"),
    (C + "types/arena.cryo",
     "            const bare_param: boolean = at.kind == TypeKind::GenericParam\n"
     "                                     || at.kind == TypeKind::BoundedParam;",
     "            const bare_param: boolean = at.kind == TypeKind::GenericParam;"),
    # the two display renderers carry the same arm
    (C + "types/arena.cryo",
     """            TypeKind::BoundedParam => {
                const bp: BoundedParamType* = ty as BoundedParamType*;
                return this.intern_table.resolve(bp.param_name);
            }
            TypeKind::AssocProjection => {
                // `Base::Member` (e.g. `This::Item`).  The class-level""",
     """            TypeKind::AssocProjection => {
                // `Base::Member` (e.g. `This::Item`).  The class-level"""),
    (C + "types/arena.cryo",
     """            TypeKind::BoundedParam => {
                const bp: BoundedParamType* = ty as BoundedParamType*;
                return this.intern_table.resolve(bp.param_name);
            }
            TypeKind::AssocProjection => {
                // `Base::Member` (e.g. `This::Item`) - see the long-form""",
     """            TypeKind::AssocProjection => {
                // `Base::Member` (e.g. `This::Item`) - see the long-form"""),
    (C + "types/arena.cryo",
     "            if (t.kind == TypeKind::GenericParam || t.kind == TypeKind::BoundedParam ||\n"
     "                t.kind == TypeKind::FunctionTemplate) {",
     "            if (t.kind == TypeKind::GenericParam || t.kind == TypeKind::FunctionTemplate) {"),
    (C + "types/arena.cryo",
     '        cdebug("  GenericParam: %lld | BoundedParam: %lld\\n",\n'
     "            this.generic_param_cache.length(), this.bounded_param_cache.length());",
     '        cdebug("  GenericParam: %lld\\n", this.generic_param_cache.length());'),
    (C + "types/arena.cryo",
     '    ///   InstType:     "2a"     (type args)\n'
     '    ///   BoundedParam: "1b"     (bounds)\n',
     '    ///   InstType:     "2a"     (type args)\n'),
    (C + "types/arena.cryo",
     """        if (ty.kind == TypeKind::BoundedParam) {
            const bp: BoundedParamType* = ty as BoundedParamType*;
            return fmt::format("%lldb", bp.bounds.length);
        }
""", ""),
    (C + "types/arena.cryo",
     "                    if (arg_ty.kind == TypeKind::GenericParam ||\n"
     "                        arg_ty.kind == TypeKind::BoundedParam) {",
     "                    if (arg_ty.kind == TypeKind::GenericParam) {"),
    (C + "types/arena.cryo",
     """        if (ty.kind == TypeKind::BoundedParam) {
            const bp: BoundedParamType* = ty as BoundedParamType*;
            return fmt::format("idx=%lu", bp.param_index);
        }
""", ""),

    # -- the rest of the type layer ------------------------------------------
    (C + "types/checker.cryo",
     """        // Accept GenericParam / BoundedParam against anything here so
        // generic numeric helpers (`a + b` for `T: Numeric`) don't trip.
        const lhs_generic: boolean = lhs_type.kind == TypeKind::GenericParam
                                  || lhs_type.kind == TypeKind::BoundedParam;
        const rhs_generic: boolean = rhs_type.kind == TypeKind::GenericParam
                                  || rhs_type.kind == TypeKind::BoundedParam;""",
     """        // Accept a GenericParam against anything here so generic numeric
        // helpers (`a + b` for `T: Numeric`) don't trip.
        const lhs_generic: boolean = lhs_type.kind == TypeKind::GenericParam;
        const rhs_generic: boolean = rhs_type.kind == TypeKind::GenericParam;"""),
    (C + "types/generic_registry.cryo",
     "        if (at != null && (at.kind == TypeKind::GenericParam || at.kind == TypeKind::BoundedParam)) {",
     "        if (at != null && at.kind == TypeKind::GenericParam) {"),
    (C + "types/inference.cryo",
     "        if (ft.kind == TypeKind::GenericParam || ft.kind == TypeKind::BoundedParam) {",
     "        if (ft.kind == TypeKind::GenericParam) {"),
    (C + "types/resolver.cryo",
     "        if (bt.kind == TypeKind::GenericParam || bt.kind == TypeKind::BoundedParam\n"
     "                || bt.kind == TypeKind::AssocProjection) {",
     "        if (bt.kind == TypeKind::GenericParam || bt.kind == TypeKind::AssocProjection) {"),
    (C + "types/substitution.cryo",
     "///! Maps GenericParam/BoundedParam TypeIDs to concrete TypeRefs and",
     "///! Maps GenericParam TypeIDs to concrete TypeRefs and"),
    (C + "types/substitution.cryo",
     "    param_ids:    u64[];       // GenericParamType/BoundedParamType TypeIDs",
     "    param_ids:    u64[];       // GenericParamType TypeIDs"),
    (C + "types/substitution.cryo",
     """            TypeKind::BoundedParam => {
                this.apply_bounded_param(ty, t, arena)
            }
""", ""),
    (C + "types/substitution.cryo",
     """    apply_bounded_param(&this, ty: TypeRef, t: Type*, arena: TypeArena*) -> TypeRef {
        const replacement: TypeRef = this.get(ty.id);
        if (replacement.is_valid()) {
            return replacement;
        }
        return ty;
    }

""", ""),
    (C + "types/trait_checker.cryo",
     "import compiler::types::generic::{ BoundedParamType, GenericParamType };",
     "import compiler::types::generic::{ GenericParamType };"),
    (C + "types/trait_checker.cryo",
     """            } else if (pt.kind == TypeKind::BoundedParam) {
                const bp: BoundedParamType* = pt as BoundedParamType*;
                if (bp.param_name.equals(name)) { return subst.replacements[i]; }
            }
""", "            }\n"),

    # -- mangling, codegen, mono ----------------------------------------------
    (C + "resolver/mangled_name.cryo",
     "    AssocProjectionType, BoundedParamType, GenericParamType, InstantiatedType",
     "    AssocProjectionType, GenericParamType, InstantiatedType"),
    (C + "resolver/mangled_name.cryo",
     """        if (ty.kind == TypeKind::BoundedParam) {
            const bp: BoundedParamType* = ty as BoundedParamType*;
            return "N$L" + MangleContext::encode_ident(this.table.resolve(bp.param_name)) + "$G";
        }
""", ""),
    (C + "codegen/ops/symbol_resolver.cryo",
     "    /// Returns true if the FunctionType reaches a GenericParam / BoundedParam\n",
     "    /// Returns true if the FunctionType reaches a GenericParam\n"),
    (C + "codegen/ops/symbol_resolver.cryo",
     "        if (k == TypeKind::GenericParam || k == TypeKind::BoundedParam) {",
     "        if (k == TypeKind::GenericParam) {"),
    (C + "codegen/type_map.cryo",
     "            TypeKind::BoundedParam     => { LType::null() }\n", ""),
    (C + "mono/monomorphizer.cryo",
     "                if (elem_t.kind == TypeKind::GenericParam ||\n"
     "                    elem_t.kind == TypeKind::BoundedParam) { continue; }",
     "                if (elem_t.kind == TypeKind::GenericParam) { continue; }"),
    (C + "mono/state.cryo",
     "    ///   - Type args containing unresolved GenericParamType or BoundedParamType",
     "    ///   - Type args containing an unresolved GenericParamType"),
    (C + "mono/state.cryo",
     "                    if (elem_t != null && elem_t.kind != TypeKind::GenericParam\n"
     "                                       && elem_t.kind != TypeKind::BoundedParam) {",
     "                    if (elem_t != null && elem_t.kind != TypeKind::GenericParam) {"),
    (C + "passes/type_resolution.cryo",
     "                    // BoundedParamType receiver call has to fall back to",
     "                    // bounded-parameter receiver call has to fall back to"),
    (C + "passes/type_resolution.cryo",
     "                    // Use the canonical helper so a constrained `T: Bound`\n"
     "                    // produces a BoundedParamType matching what\n"
     "                    // template-registration created for the same enum.",
     "                    // Use the canonical helper so the parameter types match\n"
     "                    // what template-registration created for the same enum."),

    # -- sema --------------------------------------------------------------
    (C + "sema/async_lower.cryo",
     "    AssocProjectionType, BoundedParamType, GenericParamType, InstantiatedType",
     "    AssocProjectionType, GenericParamType, InstantiatedType"),
    (C + "sema/async_lower.cryo",
     """        if (t.kind == TypeKind::GenericParam || t.kind == TypeKind::BoundedParam) {
            const spelled: SymbolStr = if (t.kind == TypeKind::GenericParam) {
                (t as GenericParamType*).param_name
            } else {
                (t as BoundedParamType*).param_name
            };""",
     """        if (t.kind == TypeKind::GenericParam) {
            const spelled: SymbolStr = (t as GenericParamType*).param_name;"""),
    (C + "sema/call_resolver.cryo",
     "import compiler::types::generic::{ BoundedParamType, GenericParamType, InstantiatedType };",
     "import compiler::types::generic::{ GenericParamType, InstantiatedType };"),
    (C + "sema/call_resolver.cryo",
     """        } else if (base_t.kind == TypeKind::BoundedParam) {
            const bp: BoundedParamType* = base_t as BoundedParamType*;
            for (mut bi: i64 = 0; bi < bp.bounds.length; bi++) {
                const bt: Type* = this.arena.lookup(bp.bounds[bi].id);
                if (bt == null || bt.kind != TypeKind::Trait) { continue; }
                const tt: TraitType* = bt as TraitType*;
                for (mut mi: i64 = 0; mi < tt.required_methods.length; mi++) {
                    const m: MethodInfo* = &tt.required_methods[mi];
                    if (m.name.equals(member.member) && m.param_types.length == arg_count) { found.push(m); }
                }
            }
        } else if (base_t.kind == TypeKind::GenericParam) {""",
     """        } else if (base_t.kind == TypeKind::GenericParam) {"""),
    (C + "sema/call_resolver.cryo",
     """            } else if (pt.kind == TypeKind::BoundedParam) {
                const bp: BoundedParamType* = pt as BoundedParamType*;
                mut substituted: TypeRef = payload[i];
                for (mut p: i64 = 0; p < entry.param_count; p++) {
                    if (entry.param_names[p].equals(bp.param_name)) { substituted = type_args[p]; break; }
                }
                result.push(substituted);
            } else {""",
     """            } else {"""),
    (C + "sema/call_resolver.cryo",
     """            mut pname: SymbolStr = SymbolStr::empty();
            mut named: boolean = false;
            if (pt.kind == TypeKind::GenericParam) {
                pname = (pt as GenericParamType*).param_name;
                named = true;
            } else if (pt.kind == TypeKind::BoundedParam) {
                pname = (pt as BoundedParamType*).param_name;
                named = true;
            }
            if (!named) { continue; }""",
     """            if (pt.kind != TypeKind::GenericParam) { continue; }
            const pname: SymbolStr = (pt as GenericParamType*).param_name;"""),
    (C + "sema/call_resolver.cryo",
     "        if (ty.kind == TypeKind::GenericParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::BoundedParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::FunctionTemplate) { return false; }\n"
     "        if (ty.kind == TypeKind::Function)         { return false; }",
     "        if (ty.kind == TypeKind::GenericParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::FunctionTemplate) { return false; }\n"
     "        if (ty.kind == TypeKind::Function)         { return false; }"),
    (C + "sema/call_resolver.cryo",
     """            // Four rescue lookups used to run here first - through a
            // BoundedParam's bounds, through a projection's bounds, through a
            // generic return, and through the owner template.  Every receiver
            // shape they could answer for is already answered ABOVE, by the
            // generic-owner and abstract-receiver arms, so reaching this point
            // means those arms declined; re-asking the same sources with less
            // information cannot turn that into an answer.  They only spent
            // four lookups before the failure below.
""",
     """            // Every abstract receiver shape - a bounded parameter, a
            // projection, a generic return, the owner template - is answered
            // ABOVE, by the generic-owner and abstract-receiver arms, so
            // reaching this point means those arms declined; re-asking the
            // same sources with less information cannot turn that into an
            // answer.
"""),
    (C + "sema/call_resolver.cryo",
     """        if (base_t != null && base_t.kind == TypeKind::BoundedParam) {
            const trait_ret: TypeRef = this.binding.lookup_method_through_bounds(
                base_t as BoundedParamType*, lookup_ref, member.member);
            if (trait_ret.is_valid()) {
                member.set_resolved_type(trait_ret);
                return trait_ret;
            }
        }

""", ""),
    (C + "sema/call_resolver.cryo",
     """            if (pt.kind == TypeKind::GenericParam) { pname = (pt as GenericParamType*).param_name; }
            else if (pt.kind == TypeKind::BoundedParam) { pname = (pt as BoundedParamType*).param_name; }
            else { continue; }""",
     """            if (pt.kind == TypeKind::GenericParam) { pname = (pt as GenericParamType*).param_name; }
            else { continue; }"""),
    (C + "sema/member_resolver.cryo",
     "import compiler::types::generic::{ BoundedParamType, InstantiatedType };",
     "import compiler::types::generic::{ InstantiatedType };"),
    (C + "sema/member_resolver.cryo",
     """        mut overloads: boolean = false;
        if (t.kind == TypeKind::BoundedParam) {
            overloads = this.bounded_param_has_index(t as BoundedParamType*, index_id);
        } else if (t.kind == TypeKind::Struct || t.kind == TypeKind::Class
                || t.kind == TypeKind::Enum || t.kind == TypeKind::InstantiatedType) {
            overloads = this.trait_checker().select_impl(index_id, base_ty, 0) != null;
        } else {
            return null;
        }
        if (!overloads) { return null; }""",
     """        if (t.kind != TypeKind::Struct && t.kind != TypeKind::Class
                && t.kind != TypeKind::Enum && t.kind != TypeKind::InstantiatedType) {
            return null;
        }
        if (this.trait_checker().select_impl(index_id, base_ty, 0) == null) { return null; }"""),
    (C + "sema/member_resolver.cryo",
     """    /// True when a generic `BoundedParam`'s bounds (or their super-traits, to
    /// bounded depth) include the trait whose identity is `index_id`, so
    /// `a[i]` on that param overloads.
    bounded_param_has_index(&this, bp: BoundedParamType*, index_id: SymbolStr) -> boolean {
        for (mut i: i64 = 0; i < bp.bounds.length; i++) {
            if (this.trait_bound_names(bp.bounds[i], index_id, 0)) { return true; }
        }
        return false;
    }

    /// True when `bound` resolves to the `TraitType` whose identity is
    /// `trait_id`, or (transitively, to bounded depth) to one that has it as
    /// a super-trait.  By identity, never by leaf: a user trait spelled
    /// `Index` is another trait.
    trait_bound_names(&this, bound: TypeRef, trait_id: SymbolStr, depth: i32) -> boolean {
        if (depth > 4) { return false; }
        const bt: Type* = this.arena.lookup(bound.id);
        if (bt == null || bt.kind != TypeKind::Trait) { return false; }
        const tt: TraitType* = bt as TraitType*;
        if (tt.qualified_name.equals(trait_id)) { return true; }
        for (mut i: i64 = 0; i < tt.super_traits.length; i++) {
            if (this.trait_bound_names(tt.super_traits[i], trait_id, depth + 1)) { return true; }
        }
        return false;
    }

""", ""),
    (C + "sema/method_binding.cryo",
     "    AssocProjectionType, BoundedParamType, GenericParamType, InstantiatedType",
     "    AssocProjectionType, GenericParamType, InstantiatedType"),
    (C + "sema/method_binding.cryo",
     """    /// Bound-aware method dispatch: when the receiver is a `BoundedParamType`
    /// (`T: Display`), the concrete impl isn't known but each bound trait's decl
    /// determines the signature; return the first bound trait's method return.
    lookup_method_through_bounds(mut &this, bp: BoundedParamType*, recv: TypeRef,
                                 method_name: SymbolStr) -> TypeRef {
        if (bp == null) { return TypeRef::invalid(); }
        for (mut i: i64 = 0; i < bp.bounds.length; i++) {
            const bound_t: Type* = this.arena.lookup(bp.bounds[i].id);
            if (bound_t == null || bound_t.kind != TypeKind::Trait) { continue; }
            const ret: TypeRef = this.types.lookup_method_return(bp.bounds[i], method_name);
            if (ret.is_valid()) { return this.subst_this_in_trait_return(ret, recv); }
        }
        return TypeRef::invalid();
    }

""", ""),
    (C + "sema/method_binding.cryo",
     "    /// Projection-bounded dispatch: the associated-type-projection sibling of\n"
     "    /// `lookup_method_through_bounds`. When the receiver is an `AssocProjection`",
     "    /// Projection-bounded dispatch: the associated-type-projection sibling of\n"
     "    /// `lookup_method_through_param_bounds`. When the receiver is an `AssocProjection`"),
    (C + "sema/method_binding.cryo",
     """        if (t.kind == TypeKind::BoundedParam) {
            const bp_ret: TypeRef = this.lookup_method_through_bounds(
                t as BoundedParamType*, walk, method_name);
            if (bp_ret.is_valid()) { return bp_ret; }
            return this.lookup_method_through_param_bounds(
                (t as BoundedParamType*).param_name, walk, method_name);
        }
""", ""),
    (C + "sema/method_binding.cryo",
     """    /// Resolve `method_name` through the where-clause bounds naming the generic
    /// parameter `param` (`where S: AsyncRead`).
    ///
    /// A parameter constrained by a WHERE clause rather than inline stays a plain
    /// `GenericParam`: its bounds live on the enclosing function/impl node, not
    /// on the type, so `lookup_method_through_bounds` — which reads a
    /// `BoundedParamType`'s own bound list — never sees them. The bare-parameter
    /// sibling of `lookup_method_through_projection_bounds`.""",
     """    /// Resolve `method_name` through the where-clause bounds naming the generic
    /// parameter `param` (`where S: AsyncRead`).
    ///
    /// A parameter's bounds live on the enclosing function/impl node, not on
    /// its type, so they are read there. The bare-parameter sibling of
    /// `lookup_method_through_projection_bounds`."""),
    (C + "sema/method_binding.cryo",
     """        mut pname: SymbolStr = SymbolStr::from_id(0);
        mut have_pname: boolean = false;
        if (t.kind == TypeKind::GenericParam) {
            pname = (t as GenericParamType*).param_name;
            have_pname = true;
        } else if (t.kind == TypeKind::BoundedParam) {
            pname = (t as BoundedParamType*).param_name;
            have_pname = true;
        }
        if (!have_pname || !pname.is_valid()) { return rc; }""",
     """        if (t.kind != TypeKind::GenericParam) { return rc; }
        const pname: SymbolStr = (t as GenericParamType*).param_name;
        if (!pname.is_valid()) { return rc; }"""),
    (C + "sema/sema.cryo",
     "import compiler::types::generic::{ BoundedParamType, InstantiatedType };",
     "import compiler::types::generic::{ InstantiatedType };"),
    (C + "sema/sema.cryo",
     """    /// True when `bound` resolves to the trait whose identity is `trait_id`,
    /// or (transitively, to bounded depth) to one that has it as a
    /// super-trait.  Covers `T: Ord` satisfying `Eq` (`Ord : Eq`).
    ///
    /// The comparison is by identity, never by leaf: a user trait spelled
    /// `Add` is a different trait, and a bound naming it does not make `+`
    /// overload - the concrete-operand path asks `wellknown` and selects by
    /// identity, and the two paths must agree on what a bound means.
    trait_ref_matching(&this, bound: TypeRef, trait_id: SymbolStr, depth: i32) -> boolean {
        if (depth > 4) { return false; }
        const bt: Type* = this.arena.lookup(bound.id);
        if (bt == null || bt.kind != TypeKind::Trait) { return false; }
        const tt: TraitType* = bt as TraitType*;
        if (tt.qualified_name.equals(trait_id)) { return true; }
        for (mut i: i64 = 0; i < tt.super_traits.length; i++) {
            if (this.trait_ref_matching(tt.super_traits[i], trait_id, depth + 1)) { return true; }
        }
        return false;
    }

    /// True when a generic `BoundedParam`'s declared bounds (or their
    /// super-traits) include the trait whose identity is `trait_id`, so an
    /// operator on that param overloads.
    /// The type a `for` scrutinee's""",
     """    /// The type a `for` scrutinee's"""),
    (C + "sema/sema.cryo",
     """        if (t == null) { return false; }
        if (t.kind == TypeKind::BoundedParam) {
            return this.bounded_param_trait(t as BoundedParamType*, iterator_id);
        }
        return this.trait_checker().type_satisfies_opaque_bound(subject, iterator_id);
    }

    bounded_param_trait(&this, bp: BoundedParamType*, trait_id: SymbolStr) -> boolean {
        if (!trait_id.is_valid()) { return false; }
        for (mut i: i64 = 0; i < bp.bounds.length; i++) {
            if (this.trait_ref_matching(bp.bounds[i], trait_id, 0)) { return true; }
        }
        return false;
    }
""",
     """        if (t == null) { return false; }
        return this.trait_checker().type_satisfies_opaque_bound(subject, iterator_id);
    }
"""),
    (C + "sema/sema.cryo",
     "    /// Phase 1: a concrete user type (Struct/Class/Enum/Instantiated) with a\n"
     "    /// registered trait impl.  Phase 2: a generic param bounded by the trait.\n",
     "    /// Only a concrete user type (Struct/Class/Enum/Instantiated) with a\n"
     "    /// registered trait impl overloads.\n"),
    (C + "sema/sema.cryo",
     """        const is_bounded: boolean = t.kind == TypeKind::BoundedParam;
        const is_user: boolean = t.kind == TypeKind::Struct || t.kind == TypeKind::Class
                              || t.kind == TypeKind::Enum || t.kind == TypeKind::InstantiatedType;
        if (!is_bounded && !is_user) { return null; }
        // The operator's trait is the language's (`std::core::ops::Add` for
        // `+`, as Rust's `+` is `core::ops::Add`), by the identity its
        // declaration claimed; the table spells the leaf and nothing else.
        // A parameter overloads when a bound names that trait, a concrete
        // type when it has an impl of it; a user trait of the same spelling
        // is neither, on either path.
        const trait_id: SymbolStr = this.ctx.generic_registry.wellknown(map.lang);
        if (!trait_id.is_valid()) { return null; }
        mut overloads: boolean = false;
        if (is_bounded) {
            overloads = this.bounded_param_trait(t as BoundedParamType*, trait_id);
        } else {
            overloads = this.trait_checker().select_impl(trait_id, base_ty, 0) != null;
        }
        if (!overloads) { return null; }""",
     """        const is_user: boolean = t.kind == TypeKind::Struct || t.kind == TypeKind::Class
                              || t.kind == TypeKind::Enum || t.kind == TypeKind::InstantiatedType;
        if (!is_user) { return null; }
        // The operator's trait is the language's (`std::core::ops::Add` for
        // `+`, as Rust's `+` is `core::ops::Add`), by the identity its
        // declaration claimed; the table spells the leaf and nothing else.
        // A type overloads when it has an impl of that trait; a user trait
        // of the same spelling is not it.
        const trait_id: SymbolStr = this.ctx.generic_registry.wellknown(map.lang);
        if (!trait_id.is_valid()) { return null; }
        if (this.trait_checker().select_impl(trait_id, base_ty, 0) == null) { return null; }"""),
    (C + "sema/sema.cryo",
     "        if (st.kind != TypeKind::GenericParam && st.kind != TypeKind::BoundedParam) { return save; }",
     "        if (st.kind != TypeKind::GenericParam) { return save; }"),
    (C + "sema/state.cryo",
     "    // order = abstract-param index; carries constraints for BoundedParamType.",
     "    // order = abstract-param index."),
    (C + "sema/symbolic_checker.cryo",
     "/// `GenericParam`/`BoundedParam` refs, recognizing the owner's abstract",
     "/// `GenericParam` refs, recognizing the owner's abstract"),
    (C + "sema/symbolic_checker.cryo",
     "import compiler::types::generic::{ AssocProjectionType, BoundedParamType, GenericParamType, InstantiatedType };",
     "import compiler::types::generic::{ AssocProjectionType, GenericParamType, InstantiatedType };"),
    (C + "sema/symbolic_checker.cryo",
     "    /// arena's `GenericParamType` / `BoundedParamType` carry a name and an\n"
     "    /// index and no symbol",
     "    /// arena's `GenericParamType` carries a name and an index and no\n"
     "    /// symbol"),
    (C + "sema/symbolic_checker.cryo",
     """        if (t.kind == TypeKind::BoundedParam) {
            return this.symbolic_name_is_generic_param((t as BoundedParamType*).param_name);
        }
""", ""),
    (C + "sema/type_utils.cryo",
     "    /// Any unsubstituted generic parameter (GenericParam / BoundedParam) anywhere",
     "    /// Any unsubstituted generic parameter (GenericParam) anywhere"),
    (C + "sema/type_utils.cryo",
     "        if (t.kind == TypeKind::GenericParam || t.kind == TypeKind::BoundedParam) {\n"
     "            return true;",
     "        if (t.kind == TypeKind::GenericParam) {\n"
     "            return true;"),
    (C + "sema/type_utils.cryo",
     "        if (ty.kind == TypeKind::GenericParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::BoundedParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::FunctionTemplate) { return false; }\n"
     "        if (ty.kind == TypeKind::Pointer) {",
     "        if (ty.kind == TypeKind::GenericParam)     { return false; }\n"
     "        if (ty.kind == TypeKind::FunctionTemplate) { return false; }\n"
     "        if (ty.kind == TypeKind::Pointer) {"),

    # -- the editor: a hover arm over the deleted kind, which stops compiling --
    ("tools/CryoLSP/src/handlers/hover.cryo",
     """        if (t.kind == types::TypeKind::BoundedParam) {
            return (t as generic::BoundedParamType*).param_name;
        }
""", ""),
]


def main() -> int:
    check = "--check" in sys.argv[1:]
    texts: dict = {}
    pending = 0
    for rel, old, new in EDITS:
        path = ROOT / rel
        if rel not in texts:
            texts[rel] = path.read_bytes().decode("utf-8")
        text = texts[rel]
        crlf = "\r\n" in text
        o = old.replace("\n", "\r\n") if crlf else old
        n = new.replace("\n", "\r\n") if crlf else new
        count = text.count(o)
        if count == 0:
            if check:
                continue
            print(f"NO MATCH: {rel}: {old[:70]!r}")
            return 1
        if count > 1:
            print(f"AMBIGUOUS ({count}): {rel}: {old[:70]!r}")
            return 1
        pending += 1
        texts[rel] = text.replace(o, n, 1)
    if check:
        print(f"{pending} edit(s) not applied")
        return 1 if pending else 0
    for rel, text in texts.items():
        (ROOT / rel).write_bytes(text.encode("utf-8"))
    print(f"applied {pending} edit(s) over {len(texts)} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
