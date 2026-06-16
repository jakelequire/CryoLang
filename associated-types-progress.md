# Associated Types — implementation progress & architecture

> Implements `associated-types-plan.md`. Decision (2026-06-15): **full first-class
> associated-type engine** (not a reinterpretation shim). Iterator-only rollout.
> Baseline before work: full suite GREEN (1165 tests, exit 0).

## Chosen representation (the "full model")

### AST layer
- **`AssocTypeDeclNode`** (new) — a `type Item;` / `type Item: Bound;` member of a
  trait body. Fields: `name: SymbolStr`, `bounds: TraitRef[]`, `span`.
  Added to `TraitDeclNode.assoc_types: AssocTypeDeclNode*[]`.
- **`TypeAnnotation::Projection(ProjectionAnnotation*)`** (new variant) — `Base::Member`
  in type position. Fields: `base: TypeAnnotation*`, `member: SymbolStr`,
  `resolved: TypeRef`, `span`. Covers `This::Item` and `I::Item`.
- **Impl assoc bindings** on `ImplBlockNode`:
  `assoc_binding_names: SymbolStr[]`, `assoc_binding_annotations: TypeAnnotation*[]`.
  Filled from the explicit `type Item = T;` body form **or** from positional-sugar
  desugaring of the impl header (`implement Iterator<A> for X` ⇒ `Item := A`).

### Type-system layer
- **`TraitType.associated_types`** — list of `{name, bound}` members.
- **New `TypeKind::AssocProjection`** — arena type holding `(base: TypeRef,
  trait: TypeRef, member: SymbolStr)`. Stays symbolic while `base` is an unresolved
  generic param; resolves to a concrete type once `base`'s impl is known.
- **Impl binding table** — per-impl `{member → TypeRef}`, the single source of truth
  the projection resolver consults. Replaces the ad-hoc `derived_param_*` recovery as
  the canonical mechanism (derived_param stays as the cache it already is, now fed by
  the binding table).

### Projection parsing/resolution split (disambiguation)
The parser does NOT track generic-param scope, so `I::Item` (projection) is
indistinguishable from `core::iter::Foo` (qualified type) at parse time. Decision:
- **`This::Member`** → parser emits a `Projection` node directly (keyword base, unambiguous).
- **Identifier paths** (`I::Item`, `core::iter::Foo`) → parser flattens to `NamedAnnotation`
  as today. The **resolver** disambiguates: try normal qualified-type resolution first;
  if that fails, split at the last `::` and, if the prefix is a generic param / concrete
  type whose trait declares the suffix as an associated type, build an `AssocProjection`.
- The **ASTTypeSubstituter** rewrites a `Named("P::Member")` (or a `Projection` with a
  param base) into a `Projection{base: <concrete P>, member}` when it substitutes `P`,
  so a projection in a struct-field type (`MapIter.f: (I::Item)->O`) resolves after
  `I` is specialized. Both paths funnel into one `resolve_projection(base, member)` routine.

### Desugaring (positional sugar — plan decisions #4–6)
`Iterator<T>` at ANY site ≡ `Iterator<Item = T>`. Applied at impl headers, `where`
bounds, and opaque returns. Rule: positional args fill declared generic params in
order; a lone positional arg binds the sole associated type **only when the trait has
0 generic params** (Iterator's case). Else associated types must be named `<Name = T>`.

## Staging (each stage compiles; validated before the next)

- [~] **Stage 0 — baseline & loop.** DONE. Suite green; inner loop = `make cryo`
  (pin rebuilds stage-2) → test stage-2 against standalone programs. CRYO_CC=gcc.
- [x] **Stage 1 — parser + AST.** DONE. Added `AssocTypeDeclNode` + `TraitDeclNode.assoc_types`;
  `ImplBlockNode` assoc bindings; `TypeAnnotation::Projection` + `ProjectionAnnotation`.
  Parser: `type Item;`/`type Item: B;` in trait bodies (`parse_assoc_type_decl`); `type
  Item = T;` in impl bodies; `This::Member` → Projection in `parse_base_type`. Only **4**
  exhaustive matches needed arms (dumper, resolver, substituter, node_locator); the rest
  had wildcards. **Gate met:** compiler builds clean; `scratch/assoc_parse_test.cryo`
  (assoc decl + `This::Item` + positional header + `type Item = T;` body form) compiles
  AND runs → `exit=7`. NB: didn't yet *force* projection resolution (impls gave concrete
  return types); that's Stage 3.
- [~] **Stage 2 — type-system representation.** Additive foundation landed:
  `AssocProjectionType` class + `TypeKind::AssocProjection` (only `to_string` needed an
  arm; `is_primitive`/`is_compound` have wildcards); arena `create_assoc_projection`
  factory + dedup cache; `TraitType.assoc_type_names` (+ add/has/count); assoc decls
  registered into TraitType in TypeResolution. **Remaining for Stage 3:** resolve impl
  bindings (`type Item = T` + positional-sugar desugar) onto the impl as TypeRefs.
- [~] **Stage 3 — resolution & inference.** IN PROGRESS. Landed & validated:
  - `bind_trait_args_for_impl` extended with the positional-sugar desugar
    (0 generic params + 1 assoc type ⇒ `Item -> T` bound in the impl context +
    recorded on the impl node). Legacy generic-param path preserved exactly.
  - `resolve_projection` / `project_from_base` in the resolver: `This::Item` →
    impl-context `Item` binding (reuses the old bare-`Item` mechanism, minimal
    disturbance); generic-param base → symbolic `AssocProjection`.
  - Monomorphizer concreteness gate (`mono_type_contains_generic_param`) now
    treats an unresolved `AssocProjection` as non-concrete, so `Option<This::Item>`
    in an abstract signature is correctly DEFERRED (not monomorphized → was crashing).
  - **Evidence:** `scratch/min1.cryo` (default method `get_or` with `This::Item`
    param+return) → exit 5; `scratch/min2.cryo` (`Option<This::Item>` in a method
    signature, pattern-unwrapped at a concrete impl) → exit 5. Full suite still
    GREEN (1231 + 94), so existing generic-param default bodies (iter `find`) intact.
  - **Blocker #1 RESOLVED.** Root cause: sema types `this.next_one()` (receiver =
    abstract `This` placeholder) as bare `Option`, dropping the `AssocProjection`
    arg, so the abstract default body couldn't be checked (`Some(v)` → `v: T`,
    mismatched vs `This::Item`). Fully preserving the projection through abstract
    method-call typing is deep; the existing code already DEGRADES GRACEFULLY for the
    analogous abstract-`This` return case (sema.cryo:6562). Consistent fix: in the
    abstract pass, skip the return-type mismatch when the declared/returned type
    is, or contains, an unresolved `AssocProjection` (uncheckable until
    specialization, which re-checks concretely). Added
    `Sema::type_has_unresolved_projection` + guard at the E0200 return site.
    **Evidence:** `scratch/assoc_default_test.cryo` (`first_or` with `Option<This::Item>`
    pattern-unwrap + `This::Item` return, default method) → exit 5 — the exact shape
    of Iterator's `find`/`fold`/`any`/`all` defaults.
  - **Partial NEXT landed:** `TypeSubstitution.apply_assoc_projection` substitutes the
    base (`I::Item` under `I→Range<i32>` → `Range<i32>::Item`), keeping it symbolic.
    Additive/safe (no current code has projections); suite stays green.
  - **THE remaining deep piece — concrete reduction `Range<i32>::Item` → `i32`:**
    The exact reusable primitive already exists:
    **`Monomorphizer::concrete_trait_args_for(subject, "Iterator")`** (monomorphizer.cryo:1895)
    returns the concrete trait args of `subject`'s Iterator impl (`[i32]` for
    `Range<i32>`) — and for positional-sugar impls that arg list *is* the `Item`
    binding (`Item` == positional arg 0). So a concrete-projection resolver is:
    `resolve_concrete_projection(base, member, trait) = concrete_trait_args_for(base, trait)[idx_of(member)]`
    (idx 0 for the lone-assoc Iterator case). **Integration challenge (the risk):**
    `TypeSubstitution.apply` has only the arena, not the registry/resolver, so it can't
    reduce inline. Options: (a) thread a nullable registry+resolver into TypeSubstitution
    and reduce in `apply_assoc_projection` when the base is concrete; (b) a post-substitution
    sweep in the monomorphizer that walks specialized struct-field/method types and reduces
    concrete-base `AssocProjection`s before TypeLowering/codegen. Also: set
    `AssocProjection.owning_trait` at creation (from the bound for `I::Item`, the enclosing
    trait for `This::Item`) so the resolver knows which trait — or, for the Iterator-only
    rollout, search the base's impls for one whose trait declares `member`.
  - Then: concrete-base `project_from_base` arm (resolver) reusing the same resolver;
    opaque returns carry Item; diagnostics; repin + stdlib migration; full validation.
  - **GENERIC-PARAM PROJECTION NOW WORKS — `scratch/assoc_adapter_test.cryo` → exit 7.**
    A generic adapter `First<I> where I: Seq` with `type Item = I::Item` +
    `next_one -> Option<I::Item>`, instantiated `First<IntSeq>`, reduces `I::Item` to
    `i32` and runs correctly. Root-caused + fixed (no workarounds):
    1. **Parser/resolver gap:** `I::Item` (generic-param base) is a FLAT `Named("I::Item")`,
       not a Projection node (only `This::` is). Added detection in `resolve_named`:
       if the segment before the first `::` binds to a type in scope, project the rest.
    2. **Mangler ICE (exit 3):** `encode_type` (mangled_name.cryo) deliberately ICEs on
       unhandled `TypeKind`; it had no `AssocProjection` case. Added one mirroring
       `GenericParam` (template placeholder) — projections appear only in template
       signatures, resolved before a real spec is mangled.
    3. **Concrete reduction:** the spec re-resolves `Option<I::Item>` with `I→IntSeq`
       bound. `project_member` now resolves a CONCRETE base straight to its member type
       (`resolve_concrete_member` + `proj_bare_name` in the resolver: find the trait
       declaring the member that the base implements, read its impl binding under the
       base's args) — so `Option<i32>` forms directly and no `Option<AssocProjection>`
       is ever registered for monomorphization. A generic base stays symbolic.
    4. **Monomorphizer safety net:** `reduce_projections` + `resolve_concrete_projection`
       (reusing `concrete_trait_args_for`) wired into `specialize_method` and the
       spec-signature path, to concretize any projection that arrives symbolic.
  - **STRUCT-FIELD PROJECTION NOW WORKS — `scratch/assoc_field_test.cryo` → exit 6.**
    `MapSeq<I,O> { f: (I::Item)->O }` (the exact stdlib `MapIter` shape) reduces the
    field's `I::Item` to `i32` for `MapSeq<IntSeq,i32>`. Root cause: the AST
    substituter rewrites `Named("I")`→`IntSeq` but left the flat `Named("I::Item")`
    untouched, so the spec field lost its binding. Fix: `substitute_named_annotation`
    now detects a `P::Member` flat name whose prefix is a substituted param and
    rewrites it to a `Projection` over the param's concrete replacement — routing
    field/sig projections through the existing `resolve_projection`→`project_member`
    →concrete-resolution path. Guarded to params only (module-qualified names like
    `std::core::Option` have non-param prefixes, unaffected).
  - **Core engine status:** associated types now work for the Iterator-rollout shapes —
    `type Item;` decls, positional sugar, `This::Item` in default methods (sig+body),
    `I::Item` in method signatures AND struct fields, concrete reduction at mono. All
    six repros pass (min1/min2/default/parse/adapter/field).
  - **Remaining:** opaque returns carry Item (decision #4); diagnostics (Stage 5);
    chained adapters / `where I::Item: Copy` bounds; then the repin + stdlib migration
    (Stage 6) + full validation. (old note below.)
  - (old note) Repro `scratch/assoc_adapter_test.cryo` (a generic
    adapter `First<I> where I: Seq` with `type Item = I::Item` + `next_one -> Option<I::Item>`):
    currently CRASHES (exit 3, no diagnostic) during **TypeResolution Phase 3 (function
    bodies)** — i.e. resolving/checking a method body that calls `this.inner.next_one()`
    on a `where`-bounded generic param `I` and projects `I::Item`. This is the generic-param
    projection path (sema method-call on a `BoundedParam` receiver returning `Option<I::Item>`
    + the `type Item = I::Item` binding). It crashes BEFORE monomorphization, so the first
    fix is making this sema/type-resolution path AssocProjection-safe (guard the hard crash →
    symbolic), THEN the concrete reduction at mono. The compiler itself is unaffected:
    builds clean, full suite GREEN (1231 + 94), blocker #1 repros (min1/min2/assoc_default)
    all pass.
- [ ] **Stage 4 — opaque returns carry Item.** `implement Iterator<T>` propagates the
  bound `Item` for monomorphization + for-in + (optionally) the §2.11 cross-check.
- [ ] **Stage 5 — diagnostics.** "assoc type `Item` not bound in this impl";
  "trait `Foo` has generic params — bind `Out` by name".
- [ ] **Stage 6 — repin, then migrate stdlib.** `make pin` (WSL) so the pin parses the
  new syntax. Then: trait decl → `type Item;`; defaults → `This::Item`; drop adapter
  input-item params (`MapIter<I,A,O>`→`<I,O>`, `FilterIter<I,A>`→`<I>`,
  `EnumerateIter<I,A>`→`<I>`, `ZipIter<I,J,A,O>`→`<I,J>`); 16 impl headers stay
  positional (sugar), arity updated.
- [ ] **Stage 7 — validate.** Full suite O2 + O0; stage-2 self-host rebuild; add the
  re-adapt-opaque-local test + negative tests (unbound / wrongly-positional assoc);
  final repin.

## Key files (from the structural map)
- Parser: `parser/parser.cryo` (`parse_trait_declaration` :1344, `parse_implementation_block`
  :1688, `parse_trait_bounds` :2144); `parser/expr_parser.cryo` (`parse_base_type` :2260).
- AST: `AST/declaration.cryo` (`TraitDeclNode` :624, `ImplBlockNode` :772, derived_param
  :806); `AST/_module.cryo` (`TypeAnnotation` :190, annotations, `TraitRef`/`TraitBound`).
- Types: `types/user_defined.cryo` (`TraitType` :465), `types/generic.cryo`,
  `types/resolver.cryo`, `types/substitution.cryo`, `types/monomorphizer.cryo`.
- Passes: `passes/type_resolution.cryo`, `passes/sema.cryo`, `passes/specialization.cryo`.

## Risk notes
- The element-recovery machinery (`derived_param_*`, spec-key canonicalization,
  DirectPair receiver reconstruction) is pinned by ~20 cases in
  `tests/tests/stdlib/iter.cryo`. Dropping adapter params puts a projection type
  (`I::Item`) into **struct field types** (`MapIter.f: (I::Item)->O`) — the highest-risk
  spot; it must resolve during `MapIter` monomorphization.
- Bootstrap: compiler source must NOT use the new syntax until after the repin
  (Stage 6), so the old pin can always rebuild the compiler.
