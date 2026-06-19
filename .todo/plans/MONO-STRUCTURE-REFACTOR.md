<!-- IGNORE UNTIL MONO-AFTER-SEMA IS COMPLETE -->
# Monomorphizer Structure Refactor: delete the inference residue, then decompose the rest into `compiler/src/compiler/mono/`

> **Status:** Planned. **Gated on the mono-after-sema migration being fully complete** — and
> "complete" here specifically includes severing the **third** inference site (see Phase 0). This
> plan is written under the post-reorder assumption: semantic analysis is the authoritative
> resolver, runs **first**, pins generic type arguments onto `CallExprNode.resolved_type_args`, and
> types every literal at its real slot width. The new monomorphizer **consumes** sema's pinned
> bindings; it **never derives** them.

> **Path note:** "`./compiler/mono`" means `compiler/src/compiler/mono/` (namespace `Compiler::Mono`),
> a top-level peer of `codegen/`, `resolver/`, `types/`, `passes/` — matching the existing source
> layout. This doc uses the full path.

## Context

The monomorphizer lives in one mega-file, `compiler/src/compiler/types/monomorphizer.cryo`
(**7,040 lines**), defining `type struct Monomorphizer` (~25 fields), `MonomorphRequest`,
`SpecializationEntry`, and three free helpers. Unlike sema it is a **plain `type struct`** (no vtable
/ `BaseASTVisitor` inheritance), so the structural move is simpler — no recursion-seam or
vtable-method concerns.

**The dominant fact about this file: roughly half of it is type-inference machinery that the
mono-after-sema reorder makes dead.** The monomorphizer historically re-derived generic bindings by
walking call bodies and unifying argument types — the same job sema now does authoritatively and
earlier. So this refactor is **primarily a deletion**, and only secondarily a decomposition. The
explicit goal (per the maintainer, same complaint as the sema refactor): do **not** relocate the slop
— delete the dead inference engine outright, harden the remaining fallbacks into hard-errors, then
split what genuinely remains into a few well-bounded files.

**Proven precedent to copy (read first):** `compiler/src/compiler/codegen/visit/ir_generator.cryo`
(thin orchestrator) + `codegen/visit/call_emitter.cryo` (a `type struct` collaborator holding
back-pointers, `static null()`, `wire()`), and `codegen/_module.cryo` (folder = namespace). Also read
the sibling **`docs/SEMA-STRUCTURE-REFACTOR.md`** — this plan reuses its philosophy verbatim:
*port into the clean shape, don't relocate; verify by differential self-compilation, not a
byte-identical straitjacket.*

## What moves, what stays (decided)

A supporting-file audit (corrects two stale memory entries: `TemplateRegistry` was **merged into**
`types/generic_registry.cryo`; `ASTTypeSubstituter` lives in `AST/substituter.cryo`, not
`type_substituter.cryo`):

| File / type | Current home | Decision | Why |
| --- | --- | --- | --- |
| `Monomorphizer` (+ `MonomorphRequest`, `SpecializationEntry`) | `types/monomorphizer.cryo` | **MOVE → `mono/`** (the core, decomposed) | The subsystem itself. |
| `ASTSpecializer`, `SpecializationResult` | `AST/specializer.cryo` | **MOVE → `mono/`** (`Compiler::Mono::Specializer`) | Mono-exclusive collaborator ("Used by the Monomorphizer"); only the Monomorphizer calls it. |
| `GenericRegistry`, `TemplateEntry` | `types/generic_registry.cryo` | **STAYS** in `types/` | Shared store: instantiation cache populated by `TypeResolver`; trait-impl/coherence/inherent-owner tables read by **sema**. Moving it would invert the dependency. |
| `TypeSubstitution` | `types/substitution.cryo` | **STAYS** in `types/` | Pure `TypeArena` utility, no mono state. |
| `ASTCloner` | `AST/cloner.cryo` | **STAYS** in `AST/` | General reusable deep-clone visitor; specialization hooks are a thin affordance. |
| `ASTTypeSubstituter` | `AST/substituter.cryo` | **STAYS** in `AST/` | Reusable AST type-annotation rewriter, sibling to cloner. (But its visitor gaps get **fixed** — see Phase 2.) |
| `SpecializationPasses`, `SpecInjector`, register-* fns | `passes/specialization.cryo` | **STAYS** in `passes/` | Pass-pipeline citizen; owns the Monomorphizer lifecycle and is its main external consumer. |

So `mono/` is a **small** subsystem: the decomposed core + the moved `ASTSpecializer`. Everything else
stays and `mono/` depends *downward* on it — clean layering, no cycles.

**Two friction points to design around (not blockers):**
- **`mono_type_contains_generic_param`** is a `static`/free `TypeArena*` predicate that **sema depends
  on (×3)** plus the generic-validation pass. It must **not** force a `mono/` import onto sema. Keep
  it in a shared type-layer location (it only needs `TypeArena`), or re-export it so sema's import
  stays `types`-rooted.
- **`passes/specialization.cryo` reaches into `mono.spec_entries` / `mono.injection_cursor`
  directly.** Add accessor/iterator methods on the orchestrator so the pass stops depending on
  internal field layout.

## Target invariants — what the authoritative-sema assumption lets the new mono ASSERT

These are the north star. Each one converts a former fallback into a hard precondition, so the new
mono **hard-errors (ICE) instead of silently recovering**:

1. **Every reachable expression carries a concrete `resolved_type`.** → delete all of
   `resolve_arg_type_for_inference` and its ~10 AST-re-typing recovery arms; mono reads
   `expr.resolved_type`, never recomputes it.
2. **Every in-source generic call sema resolved carries a length-matched, all-concrete
   `resolved_type_args` stash.** → the *absence* of a stash on a generic call is an **ICE**, not a
   "leave it unspecialized" skip.
3. **Literal widths are sema-final.** → **no** literal-default logic anywhere in mono. The
   `Atomic<u64>::new ⇒ T=i32` guess is not gated-with-a-diagnostic — it is **deleted**, because sema
   owns literal width and the stash already carries `u64`.
4. **Mono's job narrows to: materialize concrete types + discover nested instantiations +
   cross-module naming/placement.** Never *derive* a binding.
5. **`run_generic_validation` (the arena sweep that E0900s any unresolved `InstantiatedType`) is the
   backstop** that makes hard-erroring safe — strengthen it, don't weaken it.

## Fallback / fragility taxonomy & disposition

| Cluster (representative sites in `monomorphizer.cryo` unless noted) | Disposition |
| --- | --- |
| **A — Inference residue.** `try_infer_static_method_on_generic_template` (4710–5000, the live 3rd site + its PASS A/B/C); `unify_for_inference` + the `Compiler::Types::Inference` import (line 32, `InferCtx`); `resolve_arg_type_for_inference` (3394–3790); the literal-default guess (4883–4910, 3747–3764); combinator/receiver type recovery (3542–3596, 5531–5563); on-demand local re-resolution (2658–2674); the inference scratch fields (251–312); the `discover_inferred_calls_*` walk insofar as it *infers* | **DELETE.** The keystone is the 3rd site — it alone keeps the whole supporting cast reachable. Convert its callers to consume `call.resolved_type_args` (as the two sibling sites already were), then the rest is dead. Severs the last `Inference` import; `types/inference.cryo` can likely be retired. |
| **B1/B2 — Stash-seed then silent skip.** `if (!from_stash) { return; }` (4259–4283, 5743–5769) | **HARDEN.** Already collapsed to the target shape, but the silent `return` on a missing/partial stash is the new fragility the reorder *introduces* if left unguarded — it converts "sema didn't pin this" into "leave it unspecialized." Make it an **ICE** (invariant #2). The defensive `subst.apply` over an already-concrete stash (5740) becomes an assert. |
| **B3 — Spec-signature re-resolution, 3 stacked passes.** (2450–2502, 6030–6063, 6065–6088) — "the substituter doesn't always reach every spot (virtual-dispatch gaps)" | **FIX THE ROOT, don't relocate.** These mask `ASTTypeSubstituter`'s incomplete coverage of `Function`/nested-`Generic` annotation kinds. This is the **one genuine, reorder-independent fallback** — close the substituter's visitor gaps (Phase 2) so the three "one more shot" passes collapse to one. |
| **C — Legitimate guards / precedence.** template-lookup arity ladder (4132–4208); DI cross-module mangle-placeholder guards (6175–6191, 6245–6248); `propagate_cached_resolution` / `ensure_receiver_inst_resolved` cache coherence (7009–7027, 4589–4633); `origin_module` creating-module owner (1248–1256); `SpecInjector` null-routing + owner fallback (`specialization.cryo:254–266, 817–820`); `run_generic_validation` backstop (`specialization.cryo:893–962`) | **KEEP** (make explicit). Real disambiguation / cross-module naming / cache-coherence — independent of pass order. Note `ensure_receiver_inst_resolved` *forces monomorphization* of a receiver mid-walk (a real ordering need) even though the type-recovery feeding it (cluster A) dies. |

**Stale comments to scrub** (they assert the *old* pass order and actively mislead):
`monomorphizer.cryo:2660` "Mono runs BEFORE sema," `3711` "Sema runs after mono," `3748` "Sema hasn't
run yet at mono time," `4470`, `4574–4588`.

## Phase order (aggressive, delete-first)

The cardinal rule: **delete the dead inference engine while it is still in one file, before splitting.**
Splitting first would mean lovingly re-homing ~half a file of dead code into new collaborators only to
delete it — exactly the "slop spread across files" outcome to avoid.

- **Phase 0 — finish severing inference (the keystone deletion).** Convert
  `try_infer_static_method_on_generic_template` to consume `call.resolved_type_args` (its two sibling
  sites already were); then cascade-delete `unify_for_inference`, `resolve_arg_type_for_inference`,
  the literal-default machinery (`arg_is_polymorphic_literal` / `bound_type_for_formal` /
  `literal_adopts_binding`), the inference scratch fields (251–312), and the `import
  Compiler::Types::Inference`. Retire `types/inference.cryo` if its last caller is gone. This phase is
  on the mono-after-sema TODO already; if it's done by the time this refactor starts, skip it.
  - **MUST-VERIFY before deleting (caveat):** the 3rd site (and `try_infer_method_call`/
    `specialize_method`) also does **inline, queue-less method specialization + DI registration**
    (`register_spec_method_in_di*`, `mangled_symbol_for_spec_method`). Confirm sema now emits a
    request/stash for **every** generic-method call these handle inline. If a gap exists, **re-home**
    the DI-registration tail into the core method-spec path (Phase 4's `type_populator`) — **do not
    drop it**, or spec methods silently lose their concrete symbol (link error / miscompile).
  - Verify: differential + the `Atomic<u64>::new(0)` trap test. On correct inputs this should be a
    *no-op* (sema already pins these), so the self-host fixed point is a useful bonus gate here.
- **Phase 1 — harden the stash-consumption seam.** Turn B1/B2's silent `if (!from_stash) return` into
  an **ICE**; add a pass-entry assertion of invariant #2 (every in-source generic call carries a
  length-matched, all-concrete stash). Scrub the stale pass-order comments. Strengthen
  `run_generic_validation` to be the authoritative backstop.
- **Phase 2 — fix `ASTTypeSubstituter` coverage (the real fallback removal).** Close the
  `Function`/nested-`Generic` visitor gaps so B3's three stacked spec-signature re-resolution passes
  collapse to one. Reorder-independent; the genuine architecture fix, not a deletion.
- **Phase 3 — scaffold `mono/` (mechanical, byte-identical).** Create `compiler/src/compiler/mono/` +
  `_module.cryo`; add `public module Mono;` to `compiler/src/compiler/_module.cryo`; move
  `types/monomorphizer.cryo` → `mono/monomorphizer.cryo` and `AST/specializer.cryo` →
  `mono/specializer.cryo` verbatim; rename namespaces (`Compiler::Types::Monomorphizer` →
  `Compiler::Mono`; `Compiler::AST::Specializer` → `Compiler::Mono::Specializer`); repoint importers
  (`compilation_context.cryo`, `passes/specialization.cryo`, `instance.cryo`). Keep
  `mono_type_contains_generic_param` reachable by sema **without** a `mono/`→sema dependency. This is
  pure plumbing — the IR fixed point legitimately holds, so gate on byte-identical here.
- **Phase 4 — split the lean core into collaborators (port into clean shape).** Decompose the now
  ~3k-line `mono/monomorphizer.cryo` into the layout below, each collaborator a `type struct` with
  back-pointers + `static null()` + `wire()` (codegen pattern). Add the `spec_entries`/
  `injection_cursor` accessors so `passes/specialization.cryo` stops poking fields. Each move:
  tests green + differential diff reviewed → commit.
- **Phase 5 — residual tidy.** Any remaining oversized methods in the spine; consolidate the
  `mono_*` free helpers into a small `mono/helpers.cryo` if warranted.

## Target file layout — `compiler/src/compiler/mono/` (namespace `Compiler::Mono`)

The orchestrator owns the worklist, cache, cycle-detection, and the single-request spine, and owns the
collaborators by value, wiring back-pointers after it's at a stable address.

| File | Defines | Concern | approx size |
| --- | --- | --- | --- |
| `mono/monomorphizer.cryo` | `Monomorphizer` + `MonomorphRequest` + `SpecializationEntry`; `process_all`, `specialize_request`/`_with_entry`, `create_concrete_type`, keying/cache/cycle, batch + result API; `mono_*` free helpers | Thin orchestrator: worklist engine, request dedup/cycle/cache, single-request spine, batch/result API | ~1,000 |
| `mono/specializer.cryo` | `ASTSpecializer`, `SpecializationResult` (moved from `AST/`) | Clone→substitute orchestration (drives `ASTCloner` + `ASTTypeSubstituter`); eager-spec impl-method self-shape filter | ~300 |
| `mono/ast_resolver.cryo` | `MonoAstResolver` (back-ptr) | Post-clone AST re-resolution of spec'd decls/methods/bodies + local-table build; static-match arm pruning | ~1,000 (post-Phase-2 shrink) |
| `mono/type_populator.cryo` | `MonoTypePopulator` (back-ptr) | Read spec'd AST → fill concrete arena struct/class/enum/function types; nested-instantiation discovery; **(re-homed DI spec-method registration if Phase 0 caveat requires)** | ~400 |
| `mono/trait_specializer.cryo` | `MonoTraitSpecializer` (back-ptr) | Trait-impl conditional instantiation: where-bound derivation/binding, assoc-projection reduction, bounds-violation filtering; forwards trait-satisfaction to the shared `TraitChecker` | ~570 |
| `mono/dispatch_annotator.cryo` | `MonoDispatchAnnotator` (back-ptr) | Bound-directed dispatch annotation (Display-vs-Debug `fmt` disambiguation) — the one pre-mono AST pass that survives the reorder | ~290 |

~6 files, none huge. (Optional 7th: split static-match arm pruning out of `ast_resolver.cryo` into
`mono/static_match.cryo` if the resolver feels large in practice — it's a self-contained compile-time
arm-selection walk.) **No file is created for the inference residue — it is deleted in Phase 0**, not
re-homed.

Keep on the orchestrator (shared, by value): `arena`, `generic_registry`, `intern_table`,
`diagnostics`, `type_resolver`, `decl_index`, the `specializer`, and the `trait_checker` (which stays
exposed for sema). The deleted inference scratch fields (251–312) are gone.

## Verification (differential, not byte-identical — same regime as the sema plan)

Removing a fallback path changes behavior on the inputs that used to hit it, so a byte-identical gate
would forbid the whole point. Instead, at **every committed step**:

1. **`make test` green at `-O0` and `-O2`** (necessary, not sufficient — mono has coverage gaps; the
   `Atomic<u64>` miscompile was latent because no test hit it).
2. **Differential self-compilation — primary net.** Build the new stage-2 and compile the whole
   `stdlib/` + `compiler/src/` + `tests/` + `examples/` with **both** the pre-refactor pin and the new
   compiler; diff emitted IR **and** diagnostics. A non-empty diff is expected — read each one and
   confirm it's an intended change (an inference path that's now sema-authoritative produces the same
   concrete spec) and not collateral.
3. **Self-host fixed point as a gate for the mechanical phases** (Phase 3 move) and a *change detector*
   elsewhere. Phase 0's inference deletion *should* be byte-identical on correct inputs — use the
   fixed point there as a bonus correctness check.
4. **Trap regression tests land FIRST and stay green:** `Atomic<u64>::new(0)` (literal-default
   guess — must resolve `u64`, not `i32`); a **generic-method call whose spec method is registered in
   DI** (guards the Phase-0 caveat — the inline-spec DI-registration re-home); a same-leaf
   cross-module generic call (template-lookup precedence, cluster C1).
5. **Fallback census ratchet.** Track `monomorphizer.cryo` line count + `fallback`/`try_infer`/`infer`/
   `unify`/`return ... invalid` counts per commit — they must fall monotonically toward the
   irreducible set (cluster C + the collapsed single spec-signature pass). A rise = slop regrew.
6. **The `run_generic_validation` backstop** (strengthened in Phase 1) must stay green — it is what
   licenses hard-erroring instead of falling back: any unspecialized `InstantiatedType` that slips
   through is an E0900, caught at the arena sweep rather than miscompiled downstream.

Windows specifics (from project memory): build via `make` from **PowerShell** (not Git Bash); build
the new stage-2 + run the self-host fixed point **via WSL**; run the **test suite serially**.

## Critical files

- `compiler/src/compiler/types/monomorphizer.cryo` (the 7,040-line source; ~half deleted in Phase 0)
- `compiler/src/compiler/types/inference.cryo` (`InferCtx` — retire after Phase 0)
- `compiler/src/compiler/AST/specializer.cryo` (moves into `mono/`)
- `compiler/src/compiler/AST/substituter.cryo` (visitor gaps fixed in Phase 2 — **stays** in `AST/`)
- `compiler/src/compiler/passes/specialization.cryo` (orchestrating consumer — **stays** in `passes/`; gains accessors)
- `compiler/src/compiler/types/generic_registry.cryo`, `types/substitution.cryo` (shared — **stay**)
- `compiler/src/compiler/compilation_context.cryo`, `instance.cryo`, `_module.cryo` (import/namespace repoints)
- `compiler/src/compiler/codegen/visit/` (the orchestrator + `type struct` collaborator + `wire()` pattern to copy)
