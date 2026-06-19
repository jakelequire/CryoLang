# Monomorphizer Structure Refactor — Progress Tracker

> Living log for the multi-session, **build-from-scratch + DARK** decomposition of
> `types/monomorphizer.cryo` (7,040 lines) into `compiler/src/compiler/mono/`
> (namespace `Compiler::Mono`). Append dated notes. Mirror of the sema tracker.
> Authoritative spec: `.todo/plans/MONO-STRUCTURE-REFACTOR.md`. Entry-ramp handoff in
> the originating session's first message. Sema precedent (complete, dark):
> `.todo/progress/sema-refactor-progress.md`.

## Branch / safety
- Working on `mono-sema-rewrite`. `main` is the safe `mono-before-sema` compiler (fallback).
- The maintainer owns every `git commit` and every `make pin`. I build/test only.

## The maintainer's modus operandi (READ FIRST — same as sema)
1. **Build FROM SCRATCH, not relocate.** Do NOT `git mv` monomorphizer.cryo into the folder.
   Write each `mono/` file clean, in its target shape, hardening as we go.
2. **Build DARK.** Do NOT add `public module Mono;` to `compiler/src/compiler/_module.cryo`.
   The whole `mono/` folder stays invisible until the entire refactor is finished. Leave the
   live `types/monomorphizer.cryo` + `AST/specializer.cryo` + `passes/specialization.cryo`
   untouched and wired in as today.
3. **Build everything first, debug holistically after** — no compiler feedback until flip-on.
   So: maximize compile-faithfulness by heavy reference to the working source + existing idioms.
4. **Aggressive about architecture.** Attack root causes; no shims. **Delete-don't-port** the
   dead inference engine — in a from-scratch build that means simply DON'T WRITE IT.
5. Small/validated/revertable once debugging post-flip; the build phase is one big dark push.

## ⚠ THE ONE CRITICAL CROSS-DEPENDENCY — DECISION LOCKED
`mono_type_contains_generic_param` (free `TypeArena*` predicate) is imported by the dark sema from
two files (`sema/symbolic_checker.cryo`, `sema/sema.cryo`) via `import Compiler::Types::Monomorphizer;`,
plus by `passes/specialization.cryo` and the live `passes/sema.cryo`.

**DECISION:** The pure free helpers (`mono_type_contains_generic_param`, `mono_str_ends_with`,
`mono_shared_prefix_len`, `mono_copy_type_refs`) STAY at namespace `Compiler::Types::Monomorphizer`.
- **During the dark build:** they still live in the live `types/monomorphizer.cryo` (untouched), so
  every importer keeps resolving. The new `mono/` files that need the predicate
  `import Compiler::Types::Monomorphizer;` and call it — single source of truth, no redefinition.
- **At flip-on:** gut `types/monomorphizer.cryo` down to a thin module that defines ONLY those four
  free `mono_*` functions (keep its `namespace Compiler::Types::Monomorphizer`). Sema's import is
  unchanged; the new `mono/` imports it for the predicate. No `mono/`→sema or sema→`mono/` edge.
- This is the handoff's "keep the namespace alive as a thin shim that re-exports it" option, chosen
  over a fresh `types/mono_predicates.cryo` because a new file would get module name `MonoPredicates`
  (→ namespace `...::MonoPredicates`), breaking sema's existing import path. Keeping the file name
  `monomorphizer.cryo` keeps module name `Monomorphizer` → namespace `Compiler::Types::Monomorphizer`.

**Second friction point:** `passes/specialization.cryo` pokes `mono.spec_entries` / `mono.injection_cursor`
directly. The new orchestrator EXPOSES accessors so the pass stops touching internal fields
(see "Orchestrator new accessors" below). `specialization.cryo` STAYS in `passes/`.

## Architecture decisions locked in
- **Namespace** `Compiler::Mono`; folder `compiler/src/compiler/mono/`.
- **Main file `mono/monomorphizer.cryo`** (→ namespace `Compiler::Mono::Monomorphizer`, type
  `Monomorphizer`). NOT `mono.cryo` (the double-name `Compiler::Mono::Mono` is untested — plan
  recommends `monomorphizer.cryo`; staying safe). Type name ≠ last namespace segment is FINE
  (codegen's `IRGeneratorVisitor` lives in namespace `IRGenerator`).
- `Monomorphizer` is a **plain `type struct`** (no vtable / BaseASTVisitor). So SIMPLER than sema:
  **no recursion seam, no expression-visit overrides, no double-dispatch.** Collaborators are plain
  `type struct`s with back-pointers + `static null()` + `wire()` — codegen `call_emitter.cryo` pattern.
- Orchestrator owns collaborators BY VALUE + owns shared fields BY VALUE; `wire()`s back-pointers
  AFTER it's at a stable heap address (the `Box::leak()` dance). It's constructed in
  `passes/specialization.cryo` — mirror the leak-then-wire there at flip-on.
- The deleted inference scratch fields (old lines 251–312) are GONE — do not recreate them.

## Target file layout (`compiler/src/compiler/mono/`)
| File | Defines | ~size | STATUS |
| --- | --- | --- | --- |
| `monomorphizer.cryo` | `Monomorphizer` + `MonomorphRequest` + `SpecializationEntry`; worklist/cache/cycle/keying, batch+result API, accessors, `mono_*` helpers (imported from types) | ~1,000 | **DONE (session 1)** |
| `specializer.cryo` | `ASTSpecializer`, `SpecializationResult` (moved from `AST/`) | ~300 | **DONE (session 1)** |
| `ast_resolver.cryo` | `MonoAstResolver` — post-clone re-resolution + local-table build + static-match arm pruning | ~1,000 | TODO |
| `type_populator.cryo` | `MonoTypePopulator` — spec'd AST → concrete arena types; nested-instantiation discovery; **re-homed DI spec-method registration** | ~400 | TODO |
| `trait_specializer.cryo` | `MonoTraitSpecializer` — trait-impl conditional instantiation, where-bound derivation, assoc-projection reduction, bounds filtering | ~570 | TODO |
| `dispatch_annotator.cryo` | `MonoDispatchAnnotator` — bound-directed dispatch annotation (Display-vs-Debug fmt). The one pre-mono AST pass that survives | ~290 | **DONE (session 1)** |
| `_module.cryo` | dark module list (main declared last) | — | **DONE (session 1)** |

## METHOD → DESTINATION MAP (the planning artifact — built from full method grep of the 7040-line file)
Line numbers are in the CURRENT live `types/monomorphizer.cryo`. "DELETE" = do NOT write it in the
from-scratch build (dead post-reorder inference). VERIFY-tagged ones need a usage check at build time.

### orchestrator → `mono/monomorphizer.cryo`
MonomorphRequest (51) + clone (63); SpecializationEntry (80); new→null()/wire() (315);
set_current_module (358), worklist_empty (369), pending_count (374), spec_count (384),
spec_owner_source_at (391); process_all (400), drain_pending_worklist (539);
specialize_request (983), specialize_with_entry (1011), create_concrete_type (1298),
qualify_spec_name (1330); lookup_owner_type_ref (968);
reset_for_next_module (6820), get_new_specialized_asts (6832), get_all_specialized_asts (6850),
specialization_count (6865), record_consuming_module (6871), uninjected_spec_count (6882),
get_consuming_modules (6891); enqueue_from_type_ref (6911), type_contains_generic_param (6961,
delegates to the types free fn), enqueue_from_inst (6965); make_key (6987), make_key_for (6993),
is_cached (7004), propagate_cached_resolution (7017), is_in_progress (7030), remove_in_progress (7036).
**NEW accessors** (so passes/specialization.cryo stops poking fields): spec_entry_at(i)/spec_entries_len
(have spec_count), injection_cursor getter+advance, spec_entry mutation needed by SpecInjector — audit
specialization.cryo's exact field pokes when building.

### `mono/dispatch_annotator.cryo` (MonoDispatchAnnotator — SURVIVES the reorder)
annotate_bound_dispatch_in_module (589), annotate_dispatch_in_methods (622), annotate_dispatch_in_func
(641), annotate_dispatch_member (650), single_bound_trait_for (683), annotate_dispatch_stmt (701),
annotate_dispatch_expr (792).

### `mono/ast_resolver.cryo` (MonoAstResolver)
resolve_specialized_ast (1361); prune_static_match_arms (1464), prune_static_match_in_methods (1498),
prune_static_match_in_block (1509), prune_static_match_in_stmt (1517), prune_static_match_in_expr (1609),
select_static_match_arm (1692); resolve_methods (1773), method_bounds_satisfied (1833),
resolve_func_and_body (2406), resolve_body_locals (6331).
(Optional 7th file mono/static_match.cryo if the prune_* cluster makes this file too big.)

### `mono/type_populator.cryo` (MonoTypePopulator)
populate_concrete_type (6463), populate_struct_fields (6488), populate_class_fields (6514),
populate_struct_methods (6543), populate_class_methods (6578), populate_enum_variants (6624),
build_function_type_from_ast (6679); request_nested_instantiations (6712).
**RE-HOMED DI (Phase-0 caveat — do NOT drop):** specialize_method (5924, the queue-less method-spec
CORE — keep the spec+DI, drop its inference-driven call sites), register_spec_method_in_di (6179),
register_spec_method_in_di_inherent (6230), mangled_symbol_for_spec_method (6286),
type_has_unresolved (6129, guard used by spec — VERIFY keep).

### `mono/trait_specializer.cryo` (MonoTraitSpecializer)
bind_trait_args_for_spec_impl (1853), concrete_trait_args_for (1903), resolve_concrete_projection (1983),
reduce_projections (2023), derive_impl_where_generics (2114), cache_derived_trait_params (2149),
substitute_derived_params_in_method_bodies (2180), bind_where_arg_param (2251),
peel_pointer_pointee (2287), peel_reference_referent (2296), bounds_satisfied (2312),
filter_bounds_violating_methods (2325), filter_bounds_violating_impls (2349), filter_methods (2367),
type_implements_trait (2386), lookup_subst_for_param (2392), bare_name_of (2399).

### DELETE — cluster A inference residue (do NOT write)
discover_inferred_calls_in_module (872), discover_in_func_body (942),
discover_inferred_calls_in_block (2746), discover_inferred_calls_in_stmt (2757),
discover_inferred_calls_in_expr (3176), collect_locals_in_block (2581), collect_locals_in_stmt (2629),
opaque_impl_local_init_type (2609), lookup_local_type (2738), push_arm_bindings_to_scope (3007),
push_subpattern_bindings_mono (3117), pop_scope_bindings (3167), try_resolve_function_value_ref (3319),
seed_fixed_array_iter_specs (3371), resolve_arg_type_for_inference (3394),
resolve_method_call_arg_type (3809), resolve_static_call_return_type (3905),
fn_template_arity_compatible (3981), find_scoped_function_template (3990),
find_function_template_for_call (4057), try_infer_function_call (4101),
check_function_bounds_at_call (4328), emit_call_bound_failure (4375), infer_type_is_concrete (4407),
literal_adopts_binding (4453), arg_is_polymorphic_literal (4472), bound_type_for_formal (4525),
emit_infer_conflict (4544), force_instantiate_static_converter (4648),
try_infer_static_method_on_generic_template (4710, the KEYSTONE), return_type_matches_owner_template
(5008), find_spec_inline_method (5069), find_spec_impl_method (5138), find_inherent_method (5187),
find_trait_impl_method_for_target (5241), find_self_returning_default (5291),
try_instantiate_self_returning_default (5338), resolve_scope_type (5423),
resolve_concrete_call_receiver_type (5456), try_infer_method_call (5500), unify_for_inference (6312).
Plus the inference scratch FIELDS (251–312) and `import Compiler::Types::Inference`.

### ⚠ VERIFY at build time (keep vs delete — cluster boundaries to confirm by reading callers)
- `ensure_receiver_inst_resolved` (4589) — cluster C says it FORCES monomorphization of a receiver
  mid-walk (a real ordering need) even though the type-recovery feeding it dies. KEEP if a live
  (non-inference) caller remains post-deletion; else delete. Likely belongs on orchestrator/type_populator.
- `find_spec_inline_method`/`find_spec_impl_method`/`find_inherent_method`/`find_trait_impl_method_for_target`/
  `find_self_returning_default` — method-lookup helpers. DELETE if only inference calls them; KEEP+re-home
  any the DI spec path (`specialize_method`) needs.
- `type_has_unresolved` (6129) — guard; confirm it's used by the surviving spec/DI path.
- `request_nested_instantiations` (6712) — KEEP (nested discovery), but confirm it doesn't recurse
  through deleted inference helpers.

## Phase-0 caveat (MUST-VERIFY before relying on deletion)
`try_infer_static_method_on_generic_template` + `try_infer_method_call` + `specialize_method` ALSO do
inline, queue-less method specialization + DI registration (`register_spec_method_in_di*`,
`mangled_symbol_for_spec_method`). Confirm the new sema emits a request/stash for EVERY generic-method
call these handled inline. If a gap exists, the DI-registration tail MUST be re-homed into
`type_populator` (the core method-spec path) so spec methods don't silently lose their concrete symbol
(link error / miscompile). In the from-scratch build: make sure type_populator registers spec methods
in DI for every stashed generic-method call.

## ⚠ SESSION 2 — DECISIVE FINDING + maintainer decision (READ THIS)
**Maintainer decision (AskUserQuestion):** BUILD THE STASH-CONSUMER NOW (not port-GATED, not omit). The
new mono consumes sema's `CallExprNode.resolved_type_args` stash to specialize generic free/method calls.

**State of the working tree (grep-verified):** the mono-after-sema reorder is INCOMPLETE on the mono side.
`passes/specialization.cryo` (run_monomorphization) still calls `annotate_bound_dispatch_in_module` (550),
`process_all` (553), `force_instantiate_static_converter` (571), `discover_inferred_calls_in_module` (578),
`drain_pending_worklist` (584). So mono still runs its own inference walk.

**BUT the inference walk is ALREADY MOSTLY a stash-consumer** (this is the key discovery):
- `try_infer_function_call` (4101): FULLY stash-based already. Reads `call.resolved_type_args` (4259-4283),
  applies active `subst`, `if(!from_stash) return`, bounds-check, pins `spec_sym`, enqueues. The PASS A/B/C
  unification engine is ALREADY DELETED from it. Template-lookup helpers it calls
  (`fn_template_arity_compatible` 3981, `find_scoped_function_template` 3990,
  `find_function_template_for_call` 4057) are LOOKUP not inference → **KEEP** (they were wrongly in DELETE).
  Only field→local change: it uses `this.inference_bindings`/`inference_param_ids` as scratch; convert to LOCALS.
  Still calls `try_infer_static_method_on_generic_template` (4222, the 3rd site) + `check_function_bounds_at_call` (4328, KEEP).
- `try_infer_method_call` (5500): PARTIALLY stash-based. Method-level bindings ALREADY from stash
  (M1, 5722-5769, `if(!m_from_stash) return`). The REMAINING inference is RECEIVER recovery
  (5530-5563: `resolve_arg_type_for_inference`/`resolve_static_call_return_type`/`resolve_concrete_call_receiver_type`).
  **Reorder transformation:** replace receiver recovery with `recv_type = (ma.object as ExpressionNode*).resolved_type`
  (invariant #1 — sema runs first and types the receiver). KEEP: the method-lookup ladder
  (`find_spec_impl_method` 5138, `find_inherent_method` 5187, `find_spec_inline_method` 5069,
  `find_trait_impl_method_for_target` 5241 — all LOOKUP), `ensure_receiver_inst_resolved` (4589, cluster C),
  the unwrap loop, kind checks, defer-on-generic-binding (5782), `specialize_method` (5924, the spec CORE —
  deinferenced, args come from stash), DI registration (`register_spec_method_in_di{,_inherent}`,
  `mangled_symbol_for_spec_method`), `type_has_unresolved` (6129). The body-rewalk (5884-5908) loses its
  local-table build (just rewalk; args from stash).
  **VERIFY at debug:** `try_instantiate_self_returning_default` (5338) + `find_self_returning_default` (5291) —
  zero-own-generic combinators (take/filter) skip the method-stash (n_method_params==0). Under the reorder sema
  pre-types adapter returns as InstantiatedTypes that the normal worklist picks up, so these MAY be droppable —
  but KEEP for now (lookup+instantiate, not binding-inference) and verify at flip-on whether sema's pre-typing
  fully replaces them.
- 3rd site `try_infer_static_method_on_generic_template` (4710, NOT yet read): convert to read stash like the others.

**DELETE (pure receiver/arg/binding inference, reorder-obsoleted):** `resolve_arg_type_for_inference` (3394),
`resolve_method_call_arg_type` (3809), `resolve_static_call_return_type` (3905),
`resolve_concrete_call_receiver_type` (5456), `unify_for_inference` (6312), `infer_type_is_concrete` (4407),
`literal_adopts_binding`/`arg_is_polymorphic_literal`/`bound_type_for_formal`/`emit_infer_conflict`,
`collect_locals_in_block/stmt` (2581/2629), `opaque_impl_local_init_type` (2609), `lookup_local_type` (2738),
`push_arm_bindings_to_scope`/`push_subpattern_bindings_mono`/`pop_scope_bindings`,
`try_resolve_function_value_ref` (3319), `seed_fixed_array_iter_specs` (3371), `return_type_matches_owner_template` (5008),
+ inference scratch fields. The discover WALK (`discover_inferred_calls_in_*`) KEEPS its structure (it FINDS
calls) but DROPS local-table building (no longer needed — receiver from resolved_type, args from stash).

## REFINED FILE PLAN (8 files — added MonoState + MonoCallSpecializer)
The stash-consumer is a large distinct concern → its own collaborator `call_specializer.cryo`.
| File | Defines | STATUS |
| --- | --- | --- |
| `state.cryo` | `MonoState` — worklist(pending/cursor) + nested_visited + error_count + env(arena,gr,intern,tr); enqueue_from_type_ref/type_contains_generic_param/enqueue_from_inst/request_nested_instantiations/worklist_empty/pending_count/reset | TODO (session 2) |
| `trait_specializer.cryo` | `MonoTraitSpecializer` (+ method_bounds_satisfied moved here from ast_resolver) | TODO (session 2) |
| `type_populator.cryo` | `MonoTypePopulator` — populate_* + build_function_type_from_ast ONLY (DI moved to call_specializer) | TODO (session 2) |
| `ast_resolver.cryo` | `MonoAstResolver` — resolve_specialized_ast/resolve_methods/resolve_func_and_body(inference body-walk DELETED)/resolve_body_locals + static-match prune/select | TODO (session 2) |
| `call_specializer.cryo` | `MonoCallSpecializer` — the STASH-CONSUMER: discover walk (no local tables) + free-call stash + method-call stash (recv via resolved_type) + 3rd static site + find_* ladder + specialize_method(deinferenced) + register_spec_method_in_di{,_inherent} + mangled_symbol_for_spec_method + type_has_unresolved + ensure_receiver_inst_resolved + check_function_bounds_at_call | TODO (session 2, the big one) |

**MonoState rationale:** ast_resolver's `resolve_func_and_body` calls `request_nested_instantiations` (return-type
discovery) + `resolve_methods` resets `nested_visited`; call_specializer enqueues + bumps error_count. Both need
the worklist/discovery/error state. Putting it in a shared `MonoState*` (sema's VisitorState precedent) avoids a
collaborator→orchestrator module cycle. The orchestrator owns MonoState by value; collaborators hold `state: MonoState*`.
The CACHE (spec_keys/spec_entries/spec_index), in_progress, injection_cursor, current_module, consumer arrays stay
on the orchestrator (only it touches them). NOTE: orchestrator's process_all/drain/specialize_with_entry +
request_nested_instantiations must be repointed to `this.state.*` (enqueue/pending/nested_visited/error_count).
call_specializer also needs `current_module` (free-call instantiate_for_module) + `spec_entries` read (5835) +
the orchestrator's `specializer`? No — it builds its own subst. It needs: state, env, trait_spec peer (reduce_projections),
+ read access to spec_entries (for the inherent-owner template-vs-spec routing 5833-5842) → give it a back-channel
via MonoState or an accessor. Decide when building it.

## SESSION 3 (2026-06-19 cont.) — METHOD PATH BUILT (sema producer + C1 + C2); stub GONE
Reviewed the dark sema+mono rebuilds, verified the stash contract per call-shape, then (with maintainer
sign-off) built the method-call path. See [[mono-method-path-stash-gap-2026-06-19]].

**Maintainer decisions this session (AskUserQuestion):**
1. **Owner-generic static stash gap → EXTEND SEMA** (not keep inference in mono). Sema now stashes the
   owner's concrete type args for `Slice<T>::from_raw` / `String<GA>::from_cstr`.
2. **Adapter-receiver force-resolve → WALK/DRAIN FIXPOINT (no seam).** `ensure_receiver_inst_resolved`
   is DROPPED (it needed the orchestrator's spec cache + specialize_request, unreachable from
   call_specializer without an import cycle). Adapter chains (`r.map(f).fold()`) resolve across
   run_monomorphization walk/drain iterations instead.

**PART 1 — sema producer (DARK sema, `sema/call_resolver.cryo`):** `resolve_static_method_return_via_template`
now calls the new `stash_static_owner_bindings` (additive; the existing return-typing is untouched).
Refactored `infer_static_owner_return_from_args` → extracted `infer_static_owner_bindings_from_args`
(returns the owner bindings or `[]`); added `compute_static_owner_bindings` (priority: inferred-from-args
→ expected-type → explicit scope turbofish; each source returns its OWN array, no owning-array aliasing)
and `emit_static_owner_literal_default` (the SINGLE gated literal-default diagnostic — `Diagnostic::warning`
E0307; flip severity to error later if desired). Memory-safety: avoided the `binds = ebinds` shallow-copy
double-free trap via early-return-per-source.

**PART 2 — mono method path (`mono/call_specializer.cryo`):** stub REPLACED. Built:
- `specialize_method_call` (from `try_infer_method_call`, DEINFERENCED): receiver = `ma.object.resolved_type`
  (no inference preamble); method bindings from `call.resolved_type_args` only; defer-on-generic guard
  (Shape-4); find_* ladder; inherent-vs-impl spec routing (reads `state.spec_entries`); DI register + pin
  (inherent-only); body-rewalk via `walk_block(body, ctx, null)` (substituter already concretized nested
  stashes — `substituter.cryo:620`). NO `ensure_receiver_inst_resolved`.
- `try_instantiate_self_returning_default` (combinator defaults; receiver passed in; no ensure-receiver re-find).
- `specialize_static_method_on_generic_owner` (C2; replaces `try_infer_static_method_on_generic_template`
  as a pure STASH CONSUMER) — hooked into `specialize_free_call`'s former TODO (the scope-resolves-to-a-type
  branch). Reads `call.resolved_type_args` (sema's owner stash), instantiates+enqueues the owner, pins the
  combined `<spec-owner>::<method>` callee. `force_instantiate_static_converter` is NOT ported (sema
  synthesizes converter calls with `scope_generic_args` pre-mono → normal stash path covers them).
- find_* ladder (`find_spec_inline_method`/`find_spec_impl_method`/`find_inherent_method`/
  `find_trait_impl_method_for_target`/`find_self_returning_default`), `specialize_method`,
  `type_has_unresolved`, `register_spec_method_in_di{,_inherent}`, `mangled_symbol_for_spec_method`,
  `trait_leaf_of_ann` + `qualify_spec_name` (re-homed copies). Remaps: `this.spec_entries`→
  `this.state.spec_entries`, `enqueue`→`this.state.enqueue_from_type_ref`, `type_contains_generic_param`
  →`this.state.*`, `reduce_projections`/`bare_name_of`→`this.trait_spec.*`. Mangle free-fns from `DeclIndex`.

**⚠ NEW FLIP-ON REQUIREMENT (walk/drain fixpoint):** `passes/specialization.cryo run_monomorphization` must
LOOP `specialize_calls_in_module(root)` + `drain_pending_worklist()` until no new spec entries appear
(track `mono.spec_count()` delta), instead of the single walk+drain. This is what resolves adapter-chain
receivers now that `ensure_receiver_inst_resolved` is gone. Re-walk is idempotent (guarded by
`resolved_callee`/non-generic `resolved_method`). Validate convergence at flip-on (differential self-compile).

**NOT compiled yet (all still DARK).** Risk items for the first `make cryo` after flip-on: `TypeAnnotation`
enum-variant match import, `GenericParamNode` import, `&this` vs `mut &this` on the find_* methods,
`MemberAccessNode.resolved_trait` field name, `TypeSubstitution::from_params` param-passing (assumed
by-ref like the monolith). Differential self-compile + the trap tests (Atomic<u64>, generic-method DI,
same-leaf cross-module) gate correctness.

## SESSION 2 FINAL STATUS (8 of 8 files exist, all DARK; one path stubbed)
ALL files written: `_module`, `state`, `specializer`, `dispatch_annotator`, `monomorphizer` (orchestrator),
`trait_specializer`, `type_populator`, `ast_resolver`, `call_specializer`.
- `call_specializer.cryo` COMPLETE for: the module walk (`specialize_calls_in_module` + walk_func_body/
  walk_methods/walk_block/walk_stmt/walk_expr — simplified, NO local tables), the **free-call stash consumer**
  (`specialize_free_call` — template-lookup ladder + stash read + bounds check + pin + enqueue),
  `specialize_fn_value_ref` (turbofish value-ref), `fn_template_arity_compatible`,
  `find_scoped_function_template`, `find_function_template_for_call`, `check_function_bounds_at_call`
  (bindings passed in, not scratch), `emit_call_bound_failure`.
- `specialize_method_call` is an INTENTIONAL NO-OP STUB with the full port spec inline. **DO NOT FLIP ON
  until the method path is built** — generic METHOD calls (hash<H>/fmt<W>/relay<A>/map<B>) won't specialize.

### ⏭ REMAINING WORK = the call_specializer METHOD PATH (next session, task #6)
Source to READ (monolith `types/monomorphizer.cryo`, all still present):
- `ensure_receiver_inst_resolved` 4589-4647 (KEEP — forces mono of an adapter receiver mid-walk)
- `try_infer_static_method_on_generic_template` 4710-5007 (3rd static site; convert to stash; re-homes inline
  method-spec DI registration — Phase-0 caveat keystone)
- `return_type_matches_owner_template` 5008-5026, `type_annotation_base_name` 5027-5042 (helpers for static site)
- find_* ladder 5069-5290: `find_spec_inline_method` 5069, `find_spec_impl_method` 5138, `find_inherent_method` 5187,
  `find_trait_impl_method_for_target` 5241 (all LOOKUP, KEEP)
- `find_self_returning_default` 5291-5337, `try_instantiate_self_returning_default` 5338-5422 (combinator defaults;
  VERIFY-KEEP — may be droppable post-reorder; keep+tag)
- `resolve_scope_type` 5423-5455 (used by static site)
Already READ (port from memory of this session): `try_infer_method_call` 5500-5909 (the walker — apply RECEIVER
transform), `specialize_method` 5924-6122 (deinferenced), `register_spec_method_in_di` 6179, 
`register_spec_method_in_di_inherent` 6230, `mangled_symbol_for_spec_method` 6286, `type_has_unresolved` 6129.
DELETE (do NOT port): `resolve_concrete_call_receiver_type` 5456, `resolve_static_call_return_type` 3905,
`resolve_arg_type_for_inference` 3394, `resolve_method_call_arg_type` 3809, `unify_for_inference` 6312,
`infer_type_is_concrete` 4407, `literal_adopts_binding` 4453, `arg_is_polymorphic_literal` 4472,
`bound_type_for_formal` 4525, `emit_infer_conflict` 4544, `seed_fixed_array_iter_specs` 3371 (its only caller died).
The method path also needs the static-site hook in `specialize_free_call` (the TODO at the non-type-scope branch).
These re-homed methods need: state (enqueue), trait_spec (reduce_projections in specialize_method),
decl_index (DI), arena/intern, and state.spec_entries (inherent-vs-template routing 5835). All already wired.

### SESSION 2 BUILD NOTES (7 of 8 files — superseded by FINAL STATUS above)
DONE: `_module.cryo` (manifest, 8 modules), `specializer.cryo`, `dispatch_annotator.cryo`, `state.cryo`
(MonoState + MonomorphRequest + SpecializationEntry — worklist/discovery/spec_entries/error), `monomorphizer.cryo`
(orchestrator reworked onto MonoState; spec_entries→state.spec_entries; references call_specializer peer +
specialize_calls_in_module delegate), `trait_specializer.cryo` (MonoTraitSpecializer, +method_bounds_satisfied),
`type_populator.cryo` (MonoTypePopulator — populate_* + build_function_type; arena+intern only),
`ast_resolver.cryo` (MonoAstResolver — resolve_specialized_ast/resolve_methods/resolve_func_and_body[inference
body-walk DELETED]/resolve_body_locals/static-match prune+select; state+trait_spec peers).

**Wire signatures FINAL (orchestrator wire() calls these):**
- state.wire(arena, gr, intern, tr)
- dispatch_annotator.wire(arena, intern)
- trait_spec.wire(arena, gr, intern, diag, tr, trait_checker*)
- type_populator.wire(arena, intern)
- ast_resolver.wire(arena, intern, diag, tr, state*, trait_spec*)
- call_specializer.wire(arena, gr, intern, diag, tr, decl_index, state*, trait_spec*, ast_resolver*) ← TBD when built

**REMAINING: `call_specializer.cryo` (MonoCallSpecializer) — the stash-consumer. BUILD SPEC:**
Fields: arena, generic_registry, intern_table, diagnostics, type_resolver, decl_index, state: MonoState*,
trait_spec: MonoTraitSpecializer*, ast_resolver: MonoAstResolver*, current_module: SymbolStr (+ set_current_module).
Needs ast_resolver peer because the spec'd-method-body re-walk re-resolves via resolve_body_locals? No — it
re-walks for MORE stashed calls (recursion into specialize_calls). Actually the body-rewalk (5884-5908) just
recurses the discover walk on the spec'd body → call_specializer's OWN walk methods. So it may NOT need ast_resolver.
VERIFY: does specialize_method need reduce_projections? YES (5896-5104) → trait_spec peer. Receiver spec routing
(5835) reads state.spec_entries. instantiate_for_module needs current_module.

PUBLIC ENTRY: `specialize_calls_in_module(mut &this, root: ProgramNode*) -> void` (replaces
discover_inferred_calls_in_module). Walks every non-generic top-level fn + inline/impl method body; for each
generic call with a stash, specializes. DROPS the local-table building (collect_locals_*) — args come from the
stash, receiver from resolved_type.

METHODS TO PORT (with reorder transforms):
- discover walk: discover_in_func_body(942)/in_block(2746)/in_stmt(2757)/in_expr(3176) — KEEP structure, DROP
  local_names/local_types params + collect_locals calls. The walk just finds CallExprs and dispatches to the
  free/method consumers. (Read 2746-3318 to port the stmt/expr recursion shape.)
- FREE consumer = try_infer_function_call (4101, ALREADY stash-based, READ): port nearly verbatim; convert
  this.inference_bindings/inference_param_ids → LOCAL `mut bindings: TypeRef[]`. Keep template-lookup ladder
  (fn_template_arity_compatible 3981, find_scoped_function_template 3990, find_function_template_for_call 4057 —
  READ THESE), check_function_bounds_at_call (4328, READ) + emit_call_bound_failure (4375), the stash read
  (4259-4283 → use local `bindings`), pin spec_sym + state.enqueue_from_type_ref. `if(!from_stash) return` —
  port faithfully (plan says HARDEN to ICE eventually; tag, don't ICE yet — too risky in dark build).
- METHOD consumer = try_infer_method_call (5500, READ): RECEIVER TRANSFORM — replace lines 5530-5563
  (resolve_arg_type_for_inference + resolve_static_call_return_type + resolve_concrete_call_receiver_type) with
  `recv_type = (ma.object as ExpressionNode*).resolved_type` (sema-pinned). KEEP: ensure_receiver_inst_resolved
  (4589, READ IT), unwrap loop, kind checks, the find_* ladder (find_spec_impl_method 5138, find_inherent_method
  5187, find_spec_inline_method 5069, find_trait_impl_method_for_target 5241 — READ 5069-5290), method-level stash
  seed (5722-5769 → local bindings), defer-on-generic (5782), specialize_method (5924, READ — port deinferenced:
  args come from caller), the inherent-vs-impl routing (5804-5852, reads state.spec_entries), DI registration
  (register_spec_method_in_di{,_inherent} 6179/6230 READ, mangled_symbol_for_spec_method 6286 READ), body-rewalk
  (5884-5908 → recurse walk, no local tables). VERIFY-KEEP: try_instantiate_self_returning_default (5338) +
  find_self_returning_default (5291) — READ 5291-5422; keep (lookup+instantiate, not binding-inference); they may
  be droppable post-reorder (sema pre-types adapter returns) but keep+tag.
- 3rd STATIC site = try_infer_static_method_on_generic_template (4710, READ 4710-5007): convert to read stash like
  the others; it does inline method-spec DI registration too (the Phase-0 caveat keystone). Port the spec+DI;
  drive bindings from stash.
- Helpers: type_has_unresolved (6129, READ — used by specialize_method), resolve_scope_type (5423)/
  return_type_matches_owner_template (5008)/type_annotation_base_name (5027)/trait_leaf_of_ann (already in
  dispatch_annotator — either dup or peer) — READ 5008-5068, keep what specialize_method/static-site need.
- force_instantiate_static_converter (4648, READ 4648-4709): the pass calls it. Under reorder (sema before mono),
  converters synthesized by sema exist already → likely DROP. Flag for flip-on (pass drops the call).
DELETE (pure inference, NOT ported): resolve_arg_type_for_inference (3394), resolve_method_call_arg_type (3809),
resolve_static_call_return_type (3905), resolve_concrete_call_receiver_type (5456), unify_for_inference (6312),
infer_type_is_concrete/literal_adopts_binding/arg_is_polymorphic_literal/bound_type_for_formal/emit_infer_conflict,
collect_locals_*, opaque_impl_local_init_type, lookup_local_type, push/pop scope bindings,
try_resolve_function_value_ref, seed_fixed_array_iter_specs.

**FLIP-ON pass edits (passes/specialization.cryo run_monomorphization):** drop the force_instantiate_static_converter
loop (569-574); replace the single `discover_inferred_calls_in_module(root)` + `drain_pending_worklist()` (578-584)
with a WALK/DRAIN FIXPOINT LOOP: `specialize_calls_in_module(root)` then `drain_pending_worklist()`, repeated
while `mono.spec_count()` grows (adapter-chain convergence — replaces the dropped ensure_receiver_inst_resolved).
Plus the general repoints (parent _module.cryo, importer namespaces, leak-then-wire, gut types/monomorphizer.cryo
to the mono_* shim, delete AST/specializer.cryo).

## Target invariants the new mono ASSERTS (north star)
1. Every reachable expression carries a concrete `resolved_type` → mono reads `expr.resolved_type`,
   never recomputes. (no resolve_arg_type_for_inference)
2. Every in-source generic call sema resolved carries a length-matched, all-concrete
   `resolved_type_args` stash → ABSENCE of a stash on a generic call is an **ICE**, not a skip.
3. Literal widths are sema-final → NO literal-default logic in mono (Atomic<u64>::new ⇒ i32 guess
   DELETED, not gated).
4. Mono's job narrows to: materialize concrete types + discover nested instantiations + cross-module
   naming/placement. Never derive a binding.
5. `run_generic_validation` (passes/specialization.cryo) is the backstop that licenses hard-erroring —
   strengthen, don't weaken.

**Frozen external contract:** `CallExprNode.resolved_type_args` (the stash sema produces, mono consumes).
Do NOT change its shape/meaning. New sema + new mono should flip together.

## Cryo authoring gotchas (carried from sema build / project memory)
- File `foo_bar.cryo` → `public module FooBar;` → `namespace Compiler::Mono::FooBar;`.
- `public function` for cross-module free fns (plain `function` also visible but `public` is safe).
- NO `public const` — expose constants via a `public function` accessor.
- Pass owning aggregates BY POINTER, never by value (shallow-copy + drop = UAF).
- `swap(&this.field, &local)` when reassigning owning-array fields (no drop-on-reassign).
- `&this` vs `mut &this`: method mutating only through a pointer field (*state/*arena) can be `&this`;
  flip to `mut &this` if the compiler complains at flip-on. Match the monolith's choice when porting.
- Non-zero global inits ignored (use 0-sentinels).

## Verification regime (differential, not byte-identical — same as plan/sema)
1. `make test` green at -O0 and -O2 (necessary, not sufficient).
2. Differential self-compilation (PRIMARY net): old pin vs new compiler over stdlib+compiler+tests+
   examples; diff IR + diagnostics; read every diff.
3. Self-host fixed point — gate for the mechanical move; change-detector elsewhere.
4. Trap regression tests land FIRST + stay green: Atomic<u64>::new(0) (literal-default → must be u64);
   generic-method call whose spec method is DI-registered (Phase-0 caveat); same-leaf cross-module
   generic call (template-lookup precedence).
5. Fallback census ratchet: line count + fallback/try_infer/infer/unify/return-invalid counts fall
   monotonically.
6. run_generic_validation backstop stays green.

Windows build loop (from memory): `make` from PowerShell (NOT Git Bash) with `CRYO_CC=gcc`; build
stage-2 + self-host fixed point via WSL; tests serial; kill stray `cryo-tests-test` before re-running.

## FLIP-ON STEPS (when maintainer is ready — DARK until then)
1. Add `public module Mono;` to `compiler/src/compiler/_module.cryo`.
2. Gut `types/monomorphizer.cryo` to the thin `mono_*` free-fn shim (keep namespace
   `Compiler::Types::Monomorphizer`); delete `AST/specializer.cryo`.
3. Repoint importers: `compilation_context.cryo`, `passes/specialization.cryo`, `instance.cryo` —
   `Compiler::Types::Monomorphizer` (the TYPE `Monomorphizer`) → `Compiler::Mono::Monomorphizer`,
   while keeping the predicate import `types`-rooted.
4. `passes/specialization.cryo` switches to orchestrator accessors instead of `spec_entries`/
   `injection_cursor` field pokes; mirror the leak-then-wire when constructing the Monomorphizer.
5. (Optional) retire `types/inference.cryo` if its last caller is gone.
6. Coordinate with sema flip-on (`.todo/progress/sema-refactor-progress.md`) — flip both together so
   the authoritative-stash assumption holds.
7. `make cryo` (PowerShell, CRYO_CC=gcc) → iterate compile errors → `make test` → differential
   self-compile (old pin vs new), review diffs → WSL self-host fixed point.

## STATUS LOG
### 2026-06-19 (session 1)
- Read `MONO-STRUCTURE-REFACTOR.md` end-to-end + the sema tracker (full worked precedent) + codegen
  pattern (`call_emitter.cryo`, `codegen/_module.cryo`).
- Mapped EVERY method of the 7040-line monomorphizer to its destination (see MAP above), tagging the
  cluster-A deletions and the VERIFY-at-build-time boundary cases.
- LOCKED the `mono_type_contains_generic_param` home decision (thin `types/` shim at flip-on; lives in
  the untouched live file during the dark build).
- Read in full: monomorphizer head (imports, MonomorphRequest, SpecializationEntry, the 4 free `mono_*`
  helpers, Monomorphizer fields + new()), the orchestrator tail (6820–7040: reset/result-access/
  consuming-module/enqueue/keying/cache helpers), and `AST/specializer.cryo` (the whole file).
**Files written (DARK — `mono/` NOT registered in parent `_module.cryo`):**
- `mono/_module.cryo` — dark module list (main `monomorphizer.cryo` declared LAST; collaborators above).
- `mono/specializer.cryo` — `ASTSpecializer` + `SpecializationResult`, namespace
  `Compiler::Mono::Specializer`, verbatim port of `AST/specializer.cryo` (only namespace changed).
### 2026-06-19 (session 1 cont.) — LEAN ORCHESTRATOR BUILT (`mono/monomorphizer.cryo`, DARK)
Wrote the full orchestrator from scratch under the post-reorder assumption. ~750 lines (vs the old
spine's sprawl). Contains: MonomorphRequest + clone; SpecializationEntry; Monomorphizer struct with the
**REDUCED field set** (env + specializer + trait_checker + 4 collaborator peers + worklist/cache/cycle/
injection/consumer tracking) — ALL inference scratch fields DELETED; `new()` (collaborators null()'d) +
`wire()` (leak-then-wire); accessors (set_current_module/worklist_empty/pending_count/spec_count/
spec_owner_source_at); process_all; drain_pending_worklist; annotate_bound_dispatch_in_module (delegates
to dispatch_annotator); specialize_request; specialize_with_entry (collaborator-routed, current_spec
stash block REMOVED); create_concrete_type; qualify_spec_name; request_nested_instantiations (kept on
orchestrator — it enqueues); reset_for_next_module + result-access + consuming-module tracking; the NEW
accessors for passes/specialization.cryo (spec_entry_at/get_injection_cursor/advance_injection_cursor);
enqueue_from_type_ref/type_contains_generic_param/enqueue_from_inst; make_key/make_key_for/is_cached/
propagate_cached_resolution/is_in_progress/remove_in_progress. Dropped dead `hash_str` locals (h/ip_h/
rip_h/cache_h — were unused).

**CONFIRMED DELETIONS (grep-verified, session 1):**
- `lookup_owner_type_ref` (968) — only callers are inside the deleted `discover_inferred_calls_in_module`
  (888/904/920). DELETED. (Tracker MAP above had it under orchestrator — it is NOT; removed.)
- The `current_spec_inst_id`/`current_spec_struct` stash in specialize_with_entry (1176-1198) — only
  reader is `resolve_arg_type_for_inference` (3660, cluster A). REMOVED from specialize_with_entry.
- Scratch fields `last_static_inst_ref`, `scope_names`, `scope_types`, `current_spec_*`, `inference_*`,
  `infer_conflict_*`, `current_expected_type` — all read only by cluster A. NOT in the new struct.

**PEER CONTRACT — pinned (the collaborators MUST match these signatures exactly; the orchestrator already
calls them):**
- `MonoDispatchAnnotator`: `static null()`; `wire(arena: TypeArena*, intern: InternTable*)`;
  `annotate_bound_dispatch_in_module(mut &this, root: ProgramNode*) -> void`. (+ its private helpers
  annotate_dispatch_*/single_bound_trait_for/trait_leaf_of_ann — port 589-852 + trait_leaf_of_ann@5043.)
- `MonoTraitSpecializer`: `static null()`; `wire(arena: TypeArena*, gr: GenericRegistry*,
  intern: InternTable*, diag: DiagnosticSink*, tr: TypeResolver*, tc: TraitChecker*)`;
  `filter_bounds_violating_methods(mut &this, ast: ASTNode*, subst: TypeSubstitution*) -> void` (2325);
  `filter_bounds_violating_impls(mut &this, impl_asts: &ASTNode*[], subst: TypeSubstitution*) -> ASTNode*[]` (2349).
  (+ all of 1853-2405: bind_trait_args_for_spec_impl/concrete_trait_args_for/resolve_concrete_projection/
  reduce_projections/derive_impl_where_generics/cache_derived_trait_params/
  substitute_derived_params_in_method_bodies/bind_where_arg_param/peel_*/bounds_satisfied/filter_methods/
  type_implements_trait/lookup_subst_for_param/bare_name_of.)
- `MonoTypePopulator`: `static null()`; `wire(arena, gr, intern, diag, tr, di: DeclarationIndex*,
  trait_spec: MonoTraitSpecializer*)`;
  `populate_concrete_type(mut &this, concrete_type: TypeRef, specialized_ast: ASTNode*,
   node_kind: NodeKind, subst: TypeSubstitution*, entry: TemplateEntry*) -> void` (6463);
  `build_function_type_from_ast(mut &this, ast_node: ASTNode*) -> TypeRef` (6679).
  (+ populate_struct/class_fields/methods, populate_enum_variants; RE-HOMED DI: specialize_method core,
  register_spec_method_in_di{,_inherent}, mangled_symbol_for_spec_method, type_has_unresolved.)
- `MonoAstResolver`: `static null()`; `wire(arena, gr, intern, diag, tr, di: DeclarationIndex*,
  trait_spec: MonoTraitSpecializer*)`;
  `prune_static_match_arms(mut &this, ast_node: ASTNode*, node_kind: NodeKind, entry: TemplateEntry*,
   subst: TypeSubstitution*) -> void` (1464);
  `resolve_specialized_ast(mut &this, ast_node: ASTNode*, node_kind: NodeKind, entry: TemplateEntry*,
   subst: TypeSubstitution*, this_type: TypeRef) -> void` (1361).
  (+ prune_static_match_in_*/select_static_match_arm 1498-1772; resolve_methods 1773;
  method_bounds_satisfied 1833; resolve_func_and_body 2406; resolve_body_locals 6331.)

### SESSION 1 CHECKPOINT — 4 of 6 files built (all DARK)
Built: `_module.cryo`, `specializer.cryo`, `monomorphizer.cryo` (lean orchestrator), `dispatch_annotator.cryo`
(MonoDispatchAnnotator — leaf, fully ported incl. trait_leaf_of_ann@5043; holds arena+intern back-ptrs even
though the walk uses neither, kept for wire() symmetry / future helpers). REMAINING: `trait_specializer.cryo`,
`type_populator.cryo`, `ast_resolver.cryo` (the 3 interdependent core files, ~2k lines, need careful reads of
~3k source lines). Nothing wired into parent `_module.cryo` — live engine untouched.

**WHERE TO RESUME (session 2): build the 3 remaining collaborators (task #4)** against the pinned contract above.
Suggested order (deps first): trait_specializer (env-only, no peers), then type_populator + ast_resolver
(both hold a trait_spec peer; possibly each other for DI). BUILD-TIME VERIFICATIONS to do while reading the bodies:
1. **DI re-home (Phase-0 caveat):** trace every `register_spec_method_in_di*` / `mangled_symbol_for_spec_method`
   call. In the monolith they fired from `specialize_method` (inference path). Confirm the spec methods
   produced via `populate_*_methods` / `resolve_methods` get DI-registered for EVERY stashed
   generic-method call. If `resolve_methods` (ast_resolver) is where methods get re-resolved, the DI
   registration tail likely belongs THERE or in type_populator's populate_*_methods — wire whichever
   needs it. Do NOT drop DI registration (link error / miscompile otherwise).
2. Confirm `populate_concrete_type` does NOT itself call `request_nested_instantiations` (orchestrator
   already calls it separately after populate at specialize_with_entry). If it DOES, either keep a
   back-call seam or move that recursion out — request_nested lives on the orchestrator.
3. Confirm `resolve_specialized_ast`/`resolve_methods` call into trait_spec (reduce_projections /
   bind_trait_args_for_spec_impl / derive_impl_where_generics) — that's why ast_resolver holds a
   trait_spec peer. If they ALSO need type_populator (e.g. for DI), add that peer + wire it.
4. VERIFY keep-vs-delete: ensure_receiver_inst_resolved (4589), find_spec_*/find_inherent_method/
   find_trait_impl_method_for_target/find_self_returning_default (5069-5337), type_has_unresolved (6129) —
   keep ONLY those the surviving DI/populate path calls; the rest die with cluster A.
5. Audit `passes/specialization.cryo` for the exact `mono.spec_entries`/`mono.injection_cursor` pokes to
   confirm the NEW accessors (spec_entry_at/get_injection_cursor/advance_injection_cursor) cover them;
   add more accessors if the pass also reads e.g. spec_keys or mutates entries in ways not yet covered.
</content>
</invoke>
