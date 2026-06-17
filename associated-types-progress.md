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

- [x] **Stage 0 — baseline & loop.** DONE. Suite green; inner loop = `make cryo`
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
- [x] **Stage 2 — type-system representation.** Additive foundation landed:
  `AssocProjectionType` class + `TypeKind::AssocProjection` (only `to_string` needed an
  arm; `is_primitive`/`is_compound` have wildcards); arena `create_assoc_projection`
  factory + dedup cache; `TraitType.assoc_type_names` (+ add/has/count); assoc decls
  registered into TraitType in TypeResolution. **Remaining for Stage 3:** resolve impl
  bindings (`type Item = T` + positional-sugar desugar) onto the impl as TypeRefs.
- [x] **Stage 3 — resolution & inference.** DONE (engine landed; residual gaps tracked in the audit at the bottom). Landed & validated:
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
  - **Remaining:** diagnostics (Stage 5 — NOW DONE); `where I::Item: Copy` bounds
    (see gap note below); then the repin + stdlib migration (Stage 6) + full
    validation. (old note below.)
  - **`where I::Item: Copy` — DOCUMENTED GAP (not wired).** Tested
    `function f<I>(...) where I: Iterator, I::Item: Copy`: it does **not parse** —
    the where-clause subject parser rejects a projection (`expected trait name,
    found '::'` at the `::` in `I::Item`). This is a parser-level gap: a where
    bound's subject must be a bare generic param, not a `Param::Member`
    projection. Not exercised by the current stdlib (no adapter needs a bound on
    a projected item), and wiring it touches parser + AST (projection-subject
    representation) + resolver/sema bound-checking + monomorphizer enforcement —
    a real chunk of work with bootstrap-repin risk for zero current callers.
    Left as a documented limitation; revisit if a stdlib/user adapter ever needs
    a trait bound on `I::Item`.
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
- [x] **Stage 5 — diagnostics.** DONE. Two new error codes
  (`compiler/src/compiler/diag/_module.cryo`): **E0309** ("associated type
  `Item` not bound in this impl of trait `Foo`") and **E0310** ("trait `Foo`
  has generic parameters - bind `Out` by name (`Out = ...`)"). Both emitted from
  a new `validate_impl_assoc_bindings` in `passes/type_resolution.cryo`, called
  once per source-level trait impl in the FuncSig phase right after
  `bind_trait_args_for_impl` (so the positional-sugar binding is already
  recorded on the node when we check). Logic:
  - Return early unless the impl's trait declares ≥1 associated type (so every
    legacy / non-assoc impl is untouched — only Iterator is affected today).
  - **E0310** fires when `ngp > 0 && nargs > ngp` (overflow positional args can
    only be a positional assoc bind); returns before E0309.
  - **E0309** fires for any declared assoc type with no
    `impl_node.lookup_assoc_binding(name)` after binding. Where-clause adapters
    (`implement<I,A> Iterator<A> for TakeIter<I> where I: Iterator<A>`) bind
    `Item := A` via positional sugar (Iterator has 0 generic params) so they are
    correctly NOT flagged.
  - **Tests:** `tests/tests/negative/E0309_assoc_type_not_bound.cryo` and
    `E0310_positional_assoc_with_generics.cryo` (compile-fail 96 → 98). The
    positive stdlib adapters produce no false positives (the full stdlib builds
    clean). Suite **1230 unit / 98 compile-fail GREEN**, Linux selfhost
    byte-identical fixed point (md5 `00d9c2f9…`). UNCOMMITTED; needs a repin so
    the installed `bin/cryo` emits the new codes (not a bootstrap necessity —
    the compiler source uses no new syntax, so the old pin builds it fine).
- [x] **Stage 6 — repin, then migrate stdlib.** DONE (committed + pinned; the “UNCOMMITTED” notes below are historical).
  - **Phase A — compiler prep (DONE, COMMITTED+PINNED as `6effa486`).** A spike mirroring the
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
  - **Phase B — stdlib migration (DONE, validated, UNCOMMITTED; needs a bootstrap repin).**
    `stdlib/core/iter.cryo`: trait `Iterator<Item>` → `Iterator { type Item; }`, every default's
    `Item` → `This::Item`; `MapIter<I,A,O>`→`<I,O>` (field `f: (I::Item)->O`),
    `FilterIter<I,A>`→`<I>` (`pred: (I::Item)->boolean`, impl `Iterator<I::Item>`),
    `EnumerateIter<I,A>`→`<I>` (impl `Iterator<Pair<u64,I::Item>>`). `TakeIter`/`ChainIter`/
    `ZipIter` were ALREADY where-clause-only → struct+impl UNCHANGED. Collection iterators
    (`HashMapIter`, `RefIter`, `CopiedIter`, `SliceIter`, `Range`, …) use positional sugar and
    needed NO source change. Call sites fixed: `tests/tests/stdlib/iter.cryo`
    (`EnumerateIter<R>` ×2, `MapIter<...,i32>`).
    - **Gap 3 found + fixed during Phase B:** `CopiedIter`/`ClonedIter` bind their item via a
      POINTER-wrapped where-arg (`where I: Iterator<T*>` — bridge `RefIter`'s `T*` to `T`). My
      Phase-A `derive_where_assoc_bindings` only bound simple `Named` args, so `CopiedIter<RefIter<u8>>::Item`
      stayed symbolic and leaked an unreduced projection into `Option<u8>` (E0200 in option.cryo
      during stdlib build). Fix: factored `bind_where_arg_into_ctx` (resolver.cryo) mirroring the
      monomorphizer's `bind_where_arg_param` — peels `Pointer`/`Reference` wrappers to bind the
      inner param. (UNCOMMITTED, NOT in pin yet.)
    - **Stage-4 cross-check relaxed (false positive fixed):** `implement Iterator<String>` bound to
      an iterator whose Item is `String<GlobalAlloc>` (same nominal type, default-allocator repr)
      wrongly errored under the strict `canon_type_id` compare. Now flags ONLY a category-level
      mismatch (`check_compatibility == Incompatible` BOTH directions) — distinct user structs /
      struct-vs-primitive. Trade-off: a widening-adjacent slip (`i32` vs `i64`) is implicit-
      convertible so it is no longer flagged. Negative tests rewritten to distinct user structs
      (`Apple` vs `Orange`) which also resolve under the compile-fail harness's `cryo check`.
    - **VALIDATION (via `build/cryo` built against the migrated stdlib, NOT the stale pin):** full
      suite **1230 unit / 96 compile-fail GREEN**; the compiler self-builds against the migrated
      stdlib; manual 2-cycle rebuild is **BYTE-IDENTICAL FIXED POINT** (md5 `6b0cb226…`).
    - **⚠ BOOTSTRAP REPIN REQUIRED (the catch).** The committed pin (`6effa486`) predates Gap 3 +
      the migrated stdlib, so `make pin`/`make cryo`/`selfhost-check.py` (which build the stdlib
      with the pin) will FAIL on the migrated stdlib. Recovery (2-phase): promote the known-good
      working compiler into the pin slot first, e.g. `cp compiler/build/cryo bin/cryo` (Linux)
      then `make pin` + `selfhost-check.py` succeed; the Windows pin (`bin/cryo.exe`) needs its
      usual wine cross-build. Until then the working tree's normal `make` targets are bootstrap-
      blocked — validate with `cd stdlib && ../compiler/build/cryo build` then
      `cd compiler && build/cryo build` + `make test`.
- [x] **Stage 7 — validate.** DONE (one finding + the final repin is the user's).
  - **Full suite at BOTH opt levels:** `make test ARGS="--opt-level=0"` and
    `--opt-level=2` → **1230 unit / 98 compile-fail GREEN** at each.
  - **Self-host rebuild:** `selfhost-check.py --no-windows` → BYTE-IDENTICAL
    fixed point (md5 `00d9c2f9444aa3b72d71c25c43fac816`). The committed/pinned
    `bin/cryo` builds the new compiler source fine (no new syntax in the
    compiler), so there is NO bootstrap gate this round — unlike the Stage 6
    Phase B catch.
  - **re-adapt-opaque-local — NOT lifted (verified, limitation stands).** Tested
    `mut it: implement Iterator<i32> = Range<i32>::new(0,10); it.take(3)` → still
    `E0636` (`no method 'take' on Range<i32>` / `no method 'next' on
    TakeIter<This>`). The opaque local's *visible* type is the abstract trait, so
    the combinator has no concrete receiver to specialize against — exactly the
    `docs/cryo.md` §2.11 limitation, which this migration did **not** change. So
    NO positive re-adapt test was added, and the §2.11 + `core/iter.cryo`
    header + CHANGELOG limitation notes were KEPT (only the stale "`<i32>` not
    yet cross-checked" §2.11 paragraph was corrected — Stage 4 made it real).
  - **Negative tests for unbound / wrongly-positional:** the E0309 / E0310 cases
    added in Stage 5 (overlap, as noted).
  - **Final repin: the user's action.** A repin captures E0309/E0310 into
    `bin/cryo`; it is not required for a green tree (additive, no new syntax).
- **Docs sweep — DONE.** `docs/cryo.md`: new §11.5 "Associated Types" (decls,
  projections, positional/body binding, E0309/E0310), updated the §11.4 Iterator
  table row, and corrected the §2.11 cross-check paragraph. `CHANGELOG.md`: new
  `[Unreleased]` section (Added: associated types + diagnostics; Changed:
  `core::iter` Iterator migration, source-compatible via sugar) — the frozen
  1.0.0 section is left intact (it correctly records the historical
  generic-param form). `stdlib/core/iter.cryo` header limitation note KEPT
  (re-adapt-opaque-local is genuinely still restricted).

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

## Feature-completeness audit (2026-06-16, post-repin)

Jake asked "is this completely complete?" — so the whole plan (not just the
progress file) was re-audited against `associated-types-plan.md`. **The Iterator
rollout — the actual v1.0 deliverable — is complete and solid:** trait `type
Item;` decls, `This::Item`/`I::Item` projections, positional sugar, the explicit
`type Item = …;` body form (works for generic-param traits too), concrete
reduction at mono, chained adapters, opaque-return Item cross-check (E0200),
declaration-site bound enforcement (E0306), E0309/E0310, full suite 1230/99 at
**O0 + O2**, selfhost byte-identical. Three plan items were flagged in the
audit; **item 2 is now implemented**, items 1 & 3 remain latent (no
current/stdlib caller) — documented here as conscious deferrals, not oversights:

1. **Inline named binding `<Name = T>` — NOT parsed (deferred sugar).**
   Plan decision #6 ("Named bindings are always allowed", example
   `implement trait Foo<i32, Out = bool>`). The parser rejects `=` inside the
   angle brackets (`expected '>', found '='`). **Capability is NOT lost** — an
   associated type on a generic-param trait is bound via the **body form**
   (`implement trait Conv<i32> for X { type Out = i64; … }`), which works and is
   what **E0310 now recommends** (the message was reworded from the unparseable
   `Out = …` angle form to `type Out = …;`). Only the inline-sugar convenience
   is missing; zero stdlib callers (Iterator has 0 generic params). Wiring it =
   parser (`parse_generic_arguments` to accept `ident = type`) + desugar into the
   impl assoc-binding table. Deferred.

2. **Declaration-site assoc bounds (`type Item: Copy`) — NOW ENFORCED (Gap 2
   DONE, 2026-06-16).** Plan decision #1 + compiler-work #2. Previously
   `AssocTypeDeclNode.bounds` was parsed but read by no pass, so an impl binding
   a non-`Copy` `Item` to a `type Item: Copy` trait was silently accepted. Fix:
   `Sema::check_assoc_decl_bounds` (sema.cryo), called from `visit(ImplBlockNode*)`
   for each CONCRETE trait impl. For every associated type the trait declares
   WITH a bound, it resolves the impl's concrete binding via
   `type_resolver.resolve_concrete_member(impl_this, Item)` and checks it against
   each `adecl.bounds` entry with the authoritative
   `ctx.monomorphizer.type_implements_trait` (markers Copy/Send/Sync handled
   structurally via OwnershipQuery; nominal traits via the trait-impl registry,
   which is fully populated by the time sema runs). Emits **E0306** on a
   confirmed violation; conservative (skips an undeterminable or still-generic
   item → no false positives, and an unbounded `type Item;` never reaches the
   bound loop, so Iterator is untouched). Fires under both `build` and `check`
   (positional sugar AND `type Item = …;` body form). **Test:**
   `tests/tests/negative/E0306_assoc_decl_bound.cryo` (compile-fail 98 → 99).
   **Deferred slice:** generic-adapter impls whose `Item` is symbolic
   (`I::Item`) are checked per concrete instantiation only if reached; today
   they're doubly-unreachable (you'd need the non-parsing `where I::Item: Copy`).
   Suite 1230/99 GREEN at O0+O2, selfhost byte-identical. UNCOMMITTED — needs a
   repin.

3. **Opaque-local re-adaptation — PARTIALLY LIFTED (static-constructor form,
   2026-06-17).** The canonical §2.11 case now works:
   `mut it: implement Iterator<i32> = Range<i32>::new(0,10); it.take(3)`
   compiles and runs (positive test `opaque_iter_local_takes` in
   `tests/tests/stdlib/iter.cryo`). **The pipeline reorder was NOT done — it is
   a multi-week, cross-module rewrite** (spike findings below); the narrower
   fallback the handoff proposed was implemented instead, and it covers the
   acceptance criterion. Suite 1230+/99 GREEN at O0+O2, selfhost byte-identical
   (md5 `4ef7c5bf…`). UNCOMMITTED — needs a repin (additive, no new syntax; the
   old pin self-builds the new source, so no bootstrap gate).

   **The fix (mono-side, ~45 lines, `monomorphizer.cryo`):** new helper
   `opaque_impl_local_init_type(vd)` recovers the CONCRETE type of an opaque
   `implement Trait` local from a direct static-constructor initializer
   (`Range<i32>::new(...)`) via the existing `resolve_static_call_return_type`,
   and `collect_locals_in_stmt` stamps that into the local-type table BEFORE the
   combinator-specialization walk runs. So `it.take(3)`'s receiver resolves to
   `Range<i32>` (was the abstract trait → E0636). Reuses the SAME primitive the
   working inline form (`Range<i32>::new(..).take(3)`) and the `is_auto`
   for-in lift already trust; deliberately does NOT route through
   `resolve_arg_type_for_inference` (its static-call-receiver guard is why last
   session's attempt collided — see the old note). `ensure_receiver_inst_resolved`
   (mono 5742) then monomorphizes the recovered receiver, identical to the inline
   path.

   **Still restricted (documented, NOT a regression):** an opaque local whose
   initializer is a PRODUCER that itself returns an opaque iterator
   (`let it = arr.iter(); it.take(3)`) — the concrete cursor is hidden behind
   that opaque return, so there's no concrete type to recover. That's the harder
   opaque-return-composition problem, not opaque-local typing. Workaround
   unchanged: chain off the source expression. Docs §2.11 + `core/iter.cryo`
   header + CHANGELOG updated to draw this exact line.

   **Why the full reorder was NOT attempted (spike, 2026-06-17):** mapped all
   three dependency directions. (a) Sema's 5 `ctx.monomorphizer` sites
   (`check_assoc_decl_bounds` E0306, `verify_impl_trait_bounds` E0200, the two
   method-bound diagnostics E0358, `resolve_method_call`) use ONLY
   arena/registry/intern_table — no live mono state — so they COULD be re-homed
   via an extracted `type_implements_trait` helper. (b) BUT the multi-module
   orchestrator (`instance.cryo` Phase 6a/6b) deliberately monomorphizes ALL
   modules before ANY validation/sema, because `GenericValidation` + sema walk a
   shared cross-module TypeArena; and the pass-graph hard-chains
   `Mono → GenExprRes → GenValidation → FunctionBodyTypeCheck` via provisions
   (`pass_id.cryo`). (c) Sema currently SKIPS generic bodies entirely and relies
   on mono's concrete specs being registered in `decl_index` — flipping the
   order means teaching sema to type-check generic bodies symbolically. That is
   the multi-week rewrite the handoff warned about, in the most regression-prone
   machinery in the compiler. The fallback achieves the payoff for the documented
   case at ~45 lines and zero architectural risk.

   --- historical (pre-fix) note kept for context ---
   `mut it: implement Iterator<i32> = …; it.take(3)` → E0636. **Precise blocker (root-caused this session):** the receiver typing is
   actually fine — sema already back-fills the opaque local's `resolved_type` to
   the concrete initializer type (sema.cryo ~2279-2309). The failure is pass
   ORDER: **Monomorphization runs BEFORE FunctionBodyTypeCheck/sema** (pipeline
   309 vs 312), so at mono time the opaque local has no concrete type yet, and
   mono's combinator-specialization walk leaves `TakeIter<This>` unspecialized.
   The natural fix — type the opaque local from its initializer in
   `Monomorphizer::collect_locals_in_stmt` — was tried and **reverted**: mono's
   `resolve_arg_type_for_inference` *deliberately* returns invalid for a
   static-call receiver like `Range<i32>::new(...)` (monomorphizer.cryo
   ~3895-3905, to avoid mis-specializing generic methods on call-expr receivers),
   which is exactly the canonical §2.11 initializer. So typing the initializer at
   mono time collides head-on with that guard, in the most regression-prone part
   of the compiler (the combinator inference pinned by ~30 `iter.cryo` tests).
   A real fix needs either an early opaque-local concrete-inference pre-pass or a
   targeted static-constructor-return resolver — substantial, and tangential to
   associated types (the limit is opaque-type/inference machinery, not Item
   ambiguity). **Verdict:** kept as a documented limitation (§2.11 +
   `core/iter.cryo` header); the clean workaround (chain on the expression, or
   bind a concrete adapter-typed local) stands. Recommended next pickup if the
   limit is ever worth lifting: the pass-order + static-call-receiver-typing work
   above, on its own branch, with the full iter suite as the guard.

**Bookkeeping reconciled:** Stage 0/2/3/6 checkboxes flipped `[~]`→`[x]`; the
Stage-6 "UNCOMMITTED / needs bootstrap repin" notes are historical (now
committed `14ed77ba`/`6effa486`/… + pinned by Jake). The `where I::Item: Copy`
where-bound-subject gap (also a parser limitation, see Stage-3 note) stands.

**Status (2026-06-17, post-Gap-3-partial):** the Iterator rollout, Gap 2
(decl-site bounds), and the static-constructor slice of Gap 3 (opaque-local
re-adaptation) are all done. The full Mono-after-sema pipeline reorder was
SPIKED and consciously NOT done (multi-week cross-module rewrite — see item 3
above); the narrower fallback lifts the documented §2.11 case. Remaining
deferred, all zero-caller today: inline `<Name = T>` sugar (item 1), the
`where I::Item: Copy` where-subject parse, and the producer-initialized
opaque-local form (the opaque-return-composition residual of item 3). Suite
1230 unit / 99 compile-fail GREEN at O0+O2, selfhost byte-identical
(md5 `4ef7c5bf…`). UNCOMMITTED — the Gap-3 fix (monomorphizer.cryo + the
`opaque_iter_local_takes` test + docs/CHANGELOG/iter.cryo header) needs a repin
(additive, no new syntax → no bootstrap gate; old pin self-builds new source).
