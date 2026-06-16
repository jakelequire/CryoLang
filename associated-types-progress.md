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
  - **CHAINED ADAPTERS NOW WORK — `scratch/assoc_chain_test.cryo` → exit 4**
    (minimized: `scratch/cyc_a.cryo`; controls `cyc_b`/`cyc_c`). `First<First<IntSeq>>`
    with `type Item = I::Item` (a projection whose base is itself an adapter) compiles
    and delegates correctly through both layers. Root cause was NOT in the projection
    logic at all: `resolver.cryo` `resolve_concrete_member` did a by-value owning-array
    assignment `base_args = (bt as InstantiatedType*).type_args;`, which aliases the
    base `InstantiatedType`'s buffer; the local drops at function exit and FREES
    `First<IntSeq>`'s live `type_args` storage. The freed block is reused by the next
    instantiation, so `First<IntSeq>.type_args[0]` transiently reads as
    `First<First<IntSeq>>` → cyclic `InstantiatedType` graph (597↔598) → unbounded
    `TypeSubstitution.apply`↔`apply_instantiation` recursion → SIGSEGV. (cyc_c, no
    `type Item =` binding, never calls `resolve_concrete_member` → no crash; cyc_b, no
    trait → no crash.) **Fix:** element-wise `.push` copy so `base_args` owns its
    buffer — the documented Cryo by-value/`*ptr` owning-array-alias landmine in a fresh
    spot. 10-line change, sole edit. Validated: all 6 prior assoc repros + the chain
    repro correct, suite 1230 + 94 GREEN, Linux selfhost BYTE-IDENTICAL fixed point.
    Committed + repinned (`make pin` now pins both linux and windows).
  - **Core engine status:** associated types now work for the Iterator-rollout shapes —
    `type Item;` decls, positional sugar, `This::Item` in default methods (sig+body),
    `I::Item` in method signatures AND struct fields, concrete reduction at mono, AND
    chained adapters (`First<First<IntSeq>>`). Seven repros pass
    (min1/min2/default/parse/adapter/field/chain).
  - **Remaining:** diagnostics (Stage 5); `where I::Item: Copy` bounds; then the
    repin + stdlib migration (Stage 6) + full validation. (old note below.)
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
- [x] **Stage 4 — opaque returns carry Item.** DONE. The opaque `implement Trait<T>`
  type now CARRIES + ENFORCES its associated item: `Trait<T>` is positional sugar for
  `Trait<Item = T>`, so the declared `<T>` is checked against the concrete type's actual
  `Item` at BOTH sites — the binding (`mut it: implement Seq<i64> = make_seq()`) and the
  return (`function f() -> implement Seq<i64> { return X; }`). Before Stage 4 the `<T>`
  was decorative (any element type was silently accepted). Implemented in `sema.cryo`:
  shared core `opaque_assoc_item_mismatch` (resolves declared `<T>` vs
  `resolve_concrete_member(concrete, Item)`, canon-id compare; emits ONLY on a confirmed
  mismatch — silent when undeterminable, and skips legacy generic-param traits) +
  `check_opaque_assoc_item` (binding, wired into `verify_impl_trait_bounds`) +
  `check_opaque_return_item` (return, wired into the function visit). The concrete return
  type already flows for monomorphization (a producer `-> implement Seq<i32>` returning a
  concrete impl builds + iterates correctly).
  - **Bonus crash fix (assoc types under `cryo check`):** `cryo check` SIGABRT'd on ANY
    associated-type trait+impl (mangler ICE — `encode_type_ref` aborts on an invalid
    `TypeRef`). Root cause: in `check` mode monomorphization (which resolves impl-method
    signatures in a full build) does not run, so a trait-impl method whose trait declares
    a `This::Item`-projecting signature reaches decl-index registration with an unresolved
    return type, and the mangler aborts the whole compiler. Fix in `decl_index.cryo`
    `register_methods_with_module_aliased`: skip FunctionType-registration + mangling when
    `resolved_return_type` is invalid (the mangled symbol only feeds codegen, which `check`
    skips; a full build always has a valid type here → no-op for build, byte-identical).
    This made the LSP/editor path stop crashing on assoc code AND unblocked the harness
    (compile-fail tests run via `cryo check`). NB: `cryo check` on a *bare single file*
    still emits unrelated false positives (`Option::Some not found`, method-not-found) —
    that's a pre-existing general check-mode-on-loose-file limitation (reproduces on plain
    non-assoc Option code too), NOT assoc-specific and out of Stage-4 scope.
  - **Tests:** two negative regression tests
    `tests/tests/negative/E0200_opaque_assoc_item_{binding,return}.cryo`
    (compile-fail count 94 → 96). Full suite GREEN (1230 unit / 96 compile-fail), Linux
    selfhost byte-identical fixed point. UNCOMMITTED.
- [ ] **Stage 5 — diagnostics.** "assoc type `Item` not bound in this impl";
  "trait `Foo` has generic params — bind `Out` by name".
- [~] **Stage 6 — repin, then migrate stdlib.** IN PROGRESS.
  - **Phase A — compiler prep (DONE, validated, UNCOMMITTED).** A spike mirroring the
    real adapter shapes (`scratch/assoc_combinator_chain.cryo`, `scratch/proj_wherebound.cryo`)
    surfaced TWO compiler gaps the migration needs; both fixed in `resolver.cryo`
    `resolve_concrete_member`:
    1. **Project `::Item` off a where-clause-only adapter.** `TakeIter`/`ChainIter`/
       `ZipIter`/`FilterIter` carry their item only via a where-bound
       (`implement<I,A> Iterator<A> for TakeIter<I> where I: Iterator<A>` → Item = A,
       A = I::Item). The resolver only read direct target-arg bindings, so `TakeIter<R>::Item`
       didn't resolve. New helper `derive_where_assoc_bindings` mirrors the monomorphizer's
       `derive_impl_where_generics`: binds where-clause-derived generics into the projection
       context, recursing through `resolve_concrete_member` so an adapter chain bottoms out
       at the source.
    2. **Recover the InstantiatedType wrapper for a concrete spec receiver.** During
       combinator inference (`c.take(2).map(dbl)`) the receiver resolves to a concrete
       *spec struct* (mangled name, NO type_args), so `This::Item` stayed symbolic and the
       `map<B>` formal `(This::Item)->B` never unified → no spec → unsubstituted return.
       `resolve_concrete_member` now recovers the `InstantiatedType` via `find_inst_wrapping`
       (same recovery `concrete_trait_args_for` already uses) to get the bare template name +
       type_args. (Required a new `import Compiler::Types::Inference` in resolver.cryo.)
    Validated against the CURRENT (un-migrated) stdlib: full suite 1230 unit / 96 compile-fail
    GREEN, Linux selfhost byte-identical fixed point. The OLD 3-param adapter shape worked all
    along; only the NEW assoc-type shape exposed these — confirmed by spike A/B/C bisection.
  - **BOOTSTRAP GATE (Phase A must be pinned before Phase B).** `make cryo`/`make test`/
    selfhost build the stdlib with the PIN. The migrated stdlib needs the two Phase-A fixes
    to build, so the pin must include them first. Order: commit Phase A → `make pin` → then
    Phase B can build. (This is the plan's "repin, then migrate" — the pin must carry the
    fixes, not just parse the syntax.)
  - **Phase B — stdlib migration (NOT STARTED, needs Phase-A pin).** trait decl → `type Item;`,
    defaults → `This::Item`; drop adapter input-item params (`MapIter<I,A,O>`→`<I,O>`,
    `FilterIter<I,A>`→`<I>`, `EnumerateIter<I,A>`→`<I>`). NOTE from reading the code:
    `TakeIter<I>`, `ChainIter<I,J>`, `ZipIter<I,J>` are ALREADY where-clause-only (`A`/`O` are
    impl generics, not struct params) → their struct defs + impls are UNCHANGED; only `MapIter`,
    `FilterIter`, `EnumerateIter` drop a struct param. Explicit-arity call sites to fix:
    `tests/tests/stdlib/iter.cryo` (`EnumerateIter<R,i32>`→`<R>` ×2,
    `MapIter<TakeIter<R>,i32,i32>`→`<...,i32>`). 16 impl headers stay positional (sugar).
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
