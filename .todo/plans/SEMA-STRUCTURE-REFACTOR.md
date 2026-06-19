<!-- IGNORE UNTIL MONO AFTER SEMA IS COMPLETE -->
# Sema Structure Refactor: decompose `sema.cryo` into a clean `compiler/src/compiler/sema/` subsystem

> **Status:** Planned. **Gated on the mono-after-sema migration (Step C) being fully complete
> and committed.** Do not begin until then — the entangled binding-stash core is treated here as
> stable, which only holds once Step C lands.

## Context

The semantic-analysis pass lives in a single file, `compiler/src/compiler/passes/sema.cryo`
(~13,865 lines). It is dominated by one mega-class, `type class TypeCheckVisitor :
BaseASTVisitor` (~249 methods, 58 fields), plus a small `SemanticPasses` struct holding the
pass entry point and ~14 free helpers at the bottom. The class mixes a dozen unrelated
concerns (call resolution, method/generic binding, closures, pattern exhaustiveness, symbolic
generic-body checking, member access, diagnostics, scope tracking, literals), and several
methods are enormous (`trait_leaf_name` 609 lines, `resolve_method_call` 379, `resolve_method_overload`
368, `resolve_call` 312, several 200–350-line `visit` methods). The entry point name
`run_function_body_type_check` doesn't read as "this is Sema."

**Goal:** break the file into a top-level `compiler/src/compiler/sema/` folder (namespace
`Compiler::Sema`), following the exact pattern the codegen IR generator already uses — one main
`type class : BaseASTVisitor` plus `type struct` collaborators wired by back-pointer — *and*
break up the giant methods, for clean, maintainable code.

**Proven precedent to copy (read these first):**
- `compiler/src/compiler/codegen/visit/ir_generator.cryo` — main `type class IRGeneratorVisitor :
  BaseASTVisitor`; owns `cg`, a `state: VisitorState` (by value), and sub-emitters by value; `wire()`s back-pointers.
- `compiler/src/compiler/codegen/visit/call_emitter.cryo` — a collaborator as a `type struct`
  (not a vtable class) holding back-pointers (`cg`, `state*`, peer emitters), `static null()`, wire-style setup.
- `compiler/src/compiler/codegen/state/visitor_state.cryo` — shared mutable state factored into its own struct, shared by pointer.
- `compiler/src/compiler/codegen/_module.cryo` + `codegen/visit/_module.cryo` — folder = namespace; `public module <file>;` registration.

## Naming (decided)

| Current                                                     | New                                          |
| ----------------------------------------------------------- | -------------------------------------------- |
| `type class TypeCheckVisitor`                               | `type class SemaVisitor`                     |
| `SemanticPasses::run_function_body_type_check`              | `SemanticPasses::run_sema`                   |
| file `passes/sema.cryo`, namespace `Compiler::Passes::Sema` | folder `sema/`, namespace `Compiler::Sema`   |
| subsystems                                                  | `type struct` collaborators (stdlib pattern) |

## Module registration (top-level move)

- Add `public module Sema;` to `compiler/src/compiler/_module.cryo` (namespace `Compiler`).
- **Remove** `public module Sema;` from `compiler/src/compiler/passes/_module.cryo`.
- New `compiler/src/compiler/sema/_module.cryo`: `namespace Compiler::Sema;` + one `public module <file>;`
  per file, with `SemaVisitor` declared **last**.
- `compiler/src/compiler/passes/pass_registry.cryo`: change `import Compiler::Passes::Sema;` →
  `import Compiler::Sema;`, and the dispatch call at ~line 447 from
  `SemanticPasses::run_function_body_type_check(ctx)` → `SemanticPasses::run_sema(ctx)`.
  `PassID::FunctionBodyTypeCheck` and its metadata in `pass_id.cryo` are decoupled from the impl
  name and stay as-is (renaming the PassID enum is optional and out of scope).

## Target file layout — `compiler/src/compiler/sema/`

One namespace `Compiler::Sema`. Each file declares `namespace Compiler::Sema::<Name>;` and imports
peers. The main visitor keeps **all** `override visit(...)` methods (required on the `BaseASTVisitor`
subclass); each `visit` body becomes a one-line delegate (e.g. `this.calls.resolve(this, node)`),
exactly like `ir_generator.cryo`.

| File                    | Defines                                                      | Moved from `TypeCheckVisitor`                                                                                                                                                                                                                                                                                                                                               | Stability      |
| ----------------------- | ------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- |
| `state.cryo`            | `SemaState` (leaf struct)                                    | mutable state fields only (see below)                                                                                                                                                                                                                                                                                                                                       | n/a            |
| `helpers.cryo`          | free fns + `const SEMA_*`                                    | the ~14 bottom free fns + constants                                                                                                                                                                                                                                                                                                                                         | stable         |
| `scope_manager.cryo`    | `ScopeManager{state*}`                                       | save/restore_scope, register_local*, local_is_mutable, lookup_local, is_local_or_param, is_payload_binding                                                                                                                                                                                                                                                                  | **first**      |
| `type_utils.cryo`       | `TypeUtils`                                                  | contains_generic_param, *_unresolved_projection, type_display_name, unwrap_to_enum/base_type, peel_*, generic_base_of, proj_pointer/reference, is_*_type, types_equivalent, lookup_type_sym/by_sym, DI lookup delegates, canonicalize_method_return                                                                                                                         | early          |
| `literal_resolver.cryo` | `LiteralResolver`                                            | resolve_literal/integer/float, int_literal_fits_target, is_signed_int_type, expected_type_is_int_type                                                                                                                                                                                                                                                                       | stable         |
| `diagnostics.cryo`      | `Diagnostics`                                                | emit_type_mismatch*, attach_*_suggestion, collect_*_names, suggest_similar_method, find_shadowed_*, import-insertion helpers, emit_method_arity_error, emit_switch_subject_error, emit_payload_escape, note_arity (`emit_free_*` go with CallResolver)                                                                                                                      | stable         |
| `sema_dispatch.cryo`    | `SemaDispatch{ctx,state*}`                                   | recursion-seam forwarder; dispatch_stmt, dispatch_top_level                                                                                                                                                                                                                                                                                                                 | stable         |
| `pattern_resolver.cryo` | `PatternResolver`                                            | bind_*_pattern, resolve_variant_payload_types, check_match_exhaustive, match_is_exhaustive, range/duplicate-arm checks, patterns_cover, variant_covered, arm_tail_expr                                                                                                                                                                                                      | stable         |
| `lambda_synth.cryo`     | `LambdaSynth`                                                | resolve_lambda, record_lambda_capture, mint_closure_*, synthesize_closure_struct, is_closure_struct_type, maybe_rewrite_closure_call, try_specialize_for_closure_args, find_top_level_function, reject_closure_struct_args_in_non_free_call                                                                                                                                 | medium         |
| `member_resolver.cryo`  | `MemberResolver`                                             | resolve_member_access, resolve_array_access, enforce_*_visibility, check_*_visibility, is_subclass_of, try_field_function_call, resolve_field_via_template                                                                                                                                                                                                                  | medium         |
| `symbolic_checker.cryo` | `SymbolicChecker`                                            | symbolic_check_body, symbolic_check_owner_methods, symbolic_bind_params, symbolic_param_ref, symbolic_owner_instance, symbolic_*_base/receiver/field/return, symbolic_name_is_generic_param, symbolic_defer_type, symbolic_type_unresolved                                                                                                                                  | **last group** |
| `method_binding.cryo`   | `MethodBinding`                                              | find_generic_method_*, find_method_in_trait_impls, lookup_method_through_*, solve_method_bindings, stash_method_call_bindings, stash_abstract_receiver_method, resolve_generic_method_return, resolve_method_return_via_template / _with_explicit_args, subst_this_in_type, subst_method_return_from_receiver, proj_*, reduce_assoc_projections, trait-impl method matching | **last group** |
| `call_resolver.cryo`    | `CallResolver`                                               | resolve_call, resolve_direct_call, resolve_method_call, resolve_method_overload, resolve_scope_call, resolve_scope_resolution, check_generic_free_call, stash_scope_resolution_call_bindings, all arity/arg-type checks, implicit-conversion synthesis, overload pinning, free-call inference, static/module resolution, emit_free_*                                        | **last group** |
| `sema_visitor.cryo`     | `type class SemaVisitor : BaseASTVisitor` + `SemanticPasses` | all `override visit(...)` (one-line delegates), the `resolve_expr` dispatch switch, enter_function, visit_methods, control-flow/return-escape analysis, switch checks, impl/trait-bound checks, assignment + binary/unary + cast + if/ternary/match/try + struct/array/tuple-literal + new-expr resolvers, resolve_identifier                                               | residual glue  |

Net ~14 files; largest residuals `call_resolver.cryo` (~3000) and `sema_visitor.cryo` (~2500) — acceptable; method-breakup (below) shrinks both.

## `SemaState` contents

Mirror codegen: **mutable** per-pass state → `SemaState` (shared by pointer); **immutable env**
(`arena`, `intern`, `ctx`) and the **mutated-by-value `checker`** stay owned on `SemaVisitor` and
are handed to collaborators via `wire()` as pointers (`checker: TypeChecker*` = `&this.checker`).

- **Into `SemaState`:** locals/undo (`locals`, `local_muts`, `local_kw_spans`, `undo_*`); per-function
  frame (`return_type`, `return_type_ann`, `this_type`, `this_is_mut`, `current_owner_type`,
  `payload_binding_ids`, `param_ids`, `body_count`); `loop_depth`, `switch_depth`, `expected_type`;
  lambda/closure (`in_lambda_depth`, `lambda_outer_locals`, `current_lambda`, `closure_counter`,
  `closure_spec_map`); `in_implicit_conversion`; `post_mono_verify`; `try_counter`; the 11
  `symbolic_*` fields.
- **Stay on `SemaVisitor`:** `arena`, `intern`, `ctx`, `this_sym` (interned-once constant),
  `checker` (by value, mutated).

`SemaState::new(intern)` reproduces the current constructor body minus the env fields.

## Wiring & the recursion seam

- Each collaborator is a `type struct` with env pointers + `state: SemaState*` + peer pointers, a
  `static null()` zero-init, and a `wire(...)`. Collaborators are plain `type struct`s (not vtable
  `type class`es) for two real reasons, mirroring codegen's emitters: (a) they are never visited
  polymorphically — only `SemaVisitor` is `accept()`ed — so they need no vtable; (b) a plain struct
  imports only the abstract `ASTVisitor`, never the concrete `SemaVisitor`, which is what keeps the
  module graph acyclic. (This is NOT to avoid any field-add miscompile — the self-hosted compiler has
  no such bug: `SemaVisitor`/`TypeCheckVisitor` is itself a vtable `type class` carrying ~58 fields,
  and `IRGeneratorVisitor` ~11; the only historical "vtable" bug was in the retired **legacy C++**
  compiler.)
- `SemaVisitor` owns `checker` and `state` by value and all collaborators by value; the constructor
  `::null()`s them; `wire()` (called **after** the visitor is leaked to a stable address) sets every
  back-pointer.
- **Recursion seam (the crux — avoid import cycles).** `resolve_expr` references nearly every
  collaborator, and collaborators must recurse back into `resolve_expr`. Keep the authoritative
  `match (n.kind)` switch on `SemaVisitor` (the one place allowed to import all collaborators), and
  route collaborator recursion through a `SemaDispatch` collaborator — same *shape* as codegen's
  `ExprDispatch`, but adapted to sema's return-by-value expression model.

  **Mechanism difference vs. codegen (don't copy it verbatim).** Codegen's `ExprDispatch` does
  **not** add a new virtual: its `visit(...)` overrides return `void` and publish their result on a
  side-channel (`state.last_value`), so `dispatch.codegen_rvalue(visitor, e)` just calls
  `e.accept(visitor)` (the *existing* double-dispatch vtable) and reads `state.last_value` back.
  Sema is different — `resolve_expr(e) -> TypeRef` returns the type directly and is the central
  dispatch (sema's expression `visit` overrides are not the recursion driver). Two options:
  - **(A) Add one virtual hook** `resolve_expr_dyn(e) -> TypeRef` on `BaseASTVisitor` (default
    invalid), `override`n on `SemaVisitor` to call the real `resolve_expr`; `SemaDispatch.resolve_expr(
    visitor: ASTVisitor*, e)` returns `visitor.resolve_expr_dyn(e)`. Least churn, but it grows the
    vtable of **every** `BaseASTVisitor` subclass (IRGenerator, type-resolution visitors, etc.), so
    it's a shared-base change, not a sema-local one — land it in Phase 1 and verify the fixed point.
  - **(B) Side-channel like codegen** — publish the resolved type on `SemaState` (e.g.
    `state.last_expr_type`) and have `SemaDispatch.resolve_expr` call `e.accept(visitor)` then read it
    back. No base-class change, but it touches sema's expression `visit` overrides to write the
    channel.

  Both avoid rewriting hundreds of internal `TypeRef` return paths (the in-`SemaVisitor` switch keeps
  calling `this.resolve_expr(child)` directly). Prefer (A) for minimal disruption unless the
  shared-vtable change proves risky under the fixed-point check, in which case fall back to (B).
  Collaborators import only `Compiler::AST::Visitor` (`ASTVisitor`), never `SemaVisitor`, and recurse
  via `this.dispatch.resolve_expr(visitor, child)`. Statements already recurse cycle-safely via
  `stmt.accept(visitor)`.
- **Entry point** (`run_sema`), copying `codegen/passes.cryo`:
  ```
  const _box = Box<SemaVisitor>::new(SemaVisitor::build(ctx));
  const v: SemaVisitor* = _box.leak();
  v.state.post_mono_verify = ctx.provisions.has(Provision::MonomorphizationComplete);
  v.wire();          // MUST run after leak (stable address)
  v.visit(root);
  ```
  Debug/flag reads become `v.state.body_count`, `v.state.symbolic_*`, etc.

## Execution order

Extract stable leaf subsystems first; touch the entangled core (CallResolver + MethodBinding +
SymbolicChecker — all share the binding-stash and symbolic walk) last.

**The one external contract that stays frozen:** `CallExprNode.resolved_type_args` (the stash the
monomorphizer consumes). It is sema's *output* interface to mono; freezing it is precisely what lets
us redesign sema's *internals* aggressively without touching mono. Everything *inside* sema —
sentinels, fallback paths, lookup cascades, the dual inference copies — is in scope to be rewritten,
not relocated.

**Discipline: port into the target shape, don't relocate the slop.** This refactor's whole point is
to remove fragility, not rehouse it. A byte-identical-IR gate would *forbid* that (it can only prove
"behaves exactly as before"), so we explicitly reject verbatim relocation as the unit of work. Each
subsystem moves **into its clean interface, hardening in the same commit** (per the disposition table
and target invariants below):
1. **Port + harden together.** When a concern moves to its new file, it arrives in target form —
   resolution paths yield a typed `ResolveOutcome` instead of `void`/`invalid()` sentinels; method/
   call resolution **pins authoritatively or hard-errors** instead of "return null so codegen falls
   back"; name lookups route through the single `resolve(SymbolKey, ctx)` resolver instead of an
   inline `bare→qualified→cross-module→arena-leaf` cascade. The pre-mono-only `return fallback` guard
   exits are **deleted, not ported** (dead weight once sema is authoritative).
2. **Aggressive ≠ one mega-commit.** Keep each concern's port+harden as one isolated, reviewable,
   revertable commit — tests green + the differential diff reviewed (see Verification) → commit. This
   is what keeps "aggressive" from becoming "unbisectable."

**Hard prerequisite:** mono-after-sema must be fully landed first. It is the enabler — the
authoritative post-mono pass (concrete specs present, sema pinning) is what makes the R1/R2 fallbacks
*deletable* rather than load-bearing. Do not start the aggressive port before it.

Phase order (hardening is woven through, not deferred):
- **Phase 0 — scaffold (genuinely mechanical → keep the byte-identical check here).** Create `sema/`
  + `_module.cryo`; update root + passes `_module.cryo`; move `sema.cryo` → `sema/sema_visitor.cryo`
  verbatim (still one class); rename class → `SemaVisitor`, entry → `run_sema`; repoint
  `pass_registry.cryo`. This is pure plumbing — the IR fixed point legitimately holds, so use it. Commit.
- **Phase 1 — lay the clean foundations.** `SemaState` + env-to-pointers + leak/wire entry point +
  recursion seam (Option A/B), **and introduce the target interfaces up front:** the
  `ResolveOutcome { Resolved(TypeRef) | Deferred | Error }` type and the single
  `resolve(SymbolKey, ctx)` lookup entry (wire `SymbolKey` from `symbol_key.cryo` — already built,
  currently unused in sema). Their bodies may initially delegate to the old logic; the point is that
  the clean *interfaces* exist before concerns move, so each port targets them. Tests + reviewed diff.
- **Phases 2–4 — port each concern INTO its clean interface, deleting its fallbacks in the same
  commit** (leaf → medium → entangled core). Order: `helpers` → `scope_manager` → `type_utils`
  (folds the lookup cascades into the `SymbolKey` resolver) → `literal_resolver` (owns the *single*
  gated literal-default rule) → `diagnostics` → `sema_dispatch`; then `pattern_resolver` (kills the
  `void`-binding sentinel) → `lambda_synth` → `member_resolver`; then the core `symbolic_checker` +
  `method_binding` (largest *delete-don't-port* surface — the `fallback: TypeRef` refine helpers) +
  `call_resolver` (always-pin, no codegen deferral). Keep the 11 `symbolic_*` fields and the
  binding-stash writes intact; narrow the *keep* cluster's guards to symbolic-only.
- **Phase 5 — kill the second resolver + split residual giants.** With sema now authoritative, add
  the pass-exit assertion that every resolvable call/member node is pinned, then **delete codegen's
  arity-only parallel resolver** (this deletion *should* be byte-identical — it was dead code on
  correct inputs — so verify it with the fixed point as a bonus). Split the residual giant methods
  (`trait_leaf_name` 609; the 200–350-line `visit` methods) one at a time.

The detailed root-cause taxonomy, per-cluster disposition, and target invariants that Phases 1–5
execute against are the **design contract** documented in the next section.

## Critical files

- `compiler/src/compiler/passes/sema.cryo` (source being decomposed)
- `compiler/src/compiler/codegen/visit/ir_generator.cryo`, `codegen/visit/call_emitter.cryo`,
  `codegen/state/visitor_state.cryo` (pattern to copy)
- `compiler/src/compiler/_module.cryo`, `compiler/src/compiler/passes/_module.cryo` (registration)
- `compiler/src/compiler/passes/pass_registry.cryo` (dispatch + import)
- new: `compiler/src/compiler/sema/_module.cryo` + the ~14 files above

## Verification (differential, not byte-identical)

The byte-identical IR fixed point is the correct oracle for *pure relocation* — and pure relocation
is exactly what we are **not** doing past Phase 0 (and the Phase-5 codegen-resolver deletion).
Demanding zero IR diff would forbid every fallback fix and guarantee the slop survives the move. So
the gate changes shape: from "prove nothing changed" to **"prove every change was intended."**

At **every committed step**:
1. **`make test` green at `-O0` and `-O2`** (sema diagnostic tests included). Necessary but *not
   sufficient* — sema has real coverage gaps; the `Atomic<u64>` miscompile was latent precisely
   because no unit test exercised it. Hence (2).
2. **Differential self-compilation — the primary net.** Build the new stage-2 compiler and compile a
   large *real* corpus with **both** the pre-refactor pin and the new compiler, then diff emitted IR
   **and** diagnostics: the whole `stdlib/` + the compiler's own `compiler/src/` + `tests/` +
   `examples/`. This exercises orders of magnitude more code than the unit suite. A non-empty diff is
   **expected** — the discipline is to read every diff and confirm each changed function is one the
   commit *intended* to change (a fallback fix), never collateral. "Intended diff, explained" passes;
   "surprise diff" fails.
3. **Self-host fixed point as a change *detector*, not a gate.** stage-3 vs stage-4 still runs; a diff
   is now a prompt to explain (it localizes *which* compiler function changed behavior), not an
   automatic failure. For the Phase-0 scaffold and the Phase-5 codegen-resolver deletion it *should*
   stay byte-identical — there it reverts to a hard gate.
4. **Regression tests for the known traps land FIRST**, before their fallbacks are touched, and stay
   green across the whole refactor: `Atomic<u64>::new(0)` (literal-default guess), same-leaf
   cross-module overloads (R2/R4 mis-resolution), an `Int + void-bound-local` case (R3 sentinel leak).
5. **Fallback census ratchet.** Track the counts per commit (`fallback` mentions, `return fallback`
   exits, `void`-sentinel registrations, "codegen fall back" deferrals) — they must decrease
   monotonically toward the irreducible set (symbolic-walk deferrals + the one gated literal-default).
   A count that rises = slop regrew; investigate before committing.
6. **Authoritative-pinning assertion licenses the codegen-resolver deletion** (Phase 5): a post-mono
   pass-exit check that every resolvable call/member node carries a pinned callee proves codegen's
   fallback is unreachable; only then delete it.

Windows specifics (from project memory): build via `make` from **PowerShell** (not Git Bash); build
the new stage-2 + run the self-host fixed point **via WSL**; run the **test suite serially**.
Per-step procedure: `make` (clean build) → `make test` O0 → `make test` O2 → build new stage-2 →
differential-compile corpus with old-pin vs new, review diffs → WSL stage-3/stage-4 (gate for
Phase 0 / Phase 5, explained-diff otherwise) → commit.

---

# Design contract — removing the fragility, not just relocating it

> **This is the spec Phases 1–5 execute against, not a deferred phase.** The aggressive port means
> each concern arrives in its target form (below) the moment it moves; there is no later "cleanup
> pass." Hardening is woven through the port and verified by differential self-compilation + a
> fallback census (see Verification), *not* a byte-identical gate — removing a fallback path is, by
> definition, an intended behavior change on the inputs that used to hit it.

Today's `sema.cryo` carries **82 "fallback" mentions, ~80 `try_`/widen sites, and a dozen
`return fallback` / `return TypeRef::invalid()` deferral exits.** A census of all of them (three
deep-dive audits) shows they are **not** 82 independent design choices — they are symptoms of
**four root causes**, three of which the surrounding work already dissolves. The skill in this phase
is telling a *fragile* fallback (silent recovery from a state that shouldn't occur, or a guess) apart
from *legitimate language precedence* (an ordered resolution that encodes a real scoping rule) — and
treating each correctly. Indiscriminately deleting both would break the language; leaving both is the
status quo.

## Why sema is fallback-heavy — four root causes

- **R1 — Pass order (sema ran before monomorphization).** The dominant cause. Pre-mono, a generic
  instantiation (`Array<i32>`, `Option<u64>`, `Slice<u8>`) has no concrete spec and its template
  annotations aren't `pre_resolved`, so dozens of sites **speculatively re-derive** a concrete type
  off the *base template* + manual `type_args` substitution, and `return fallback` (the caller's prior
  abstract type) on any miss. The docstrings say so verbatim: *"Sema runs before the monomorphizer, so
  the template annotations don't yet have `pre_resolved` populated"* (`sema.cryo:10131`). Once sema is
  the **authoritative post-mono pass**, the concrete spec exists and these branches are unreachable on
  the real run — they survive only as symbolic-generic-body crutches.
- **R2 — Two authoritative resolvers (sema under-resolves, codegen re-resolves).** Sema deliberately
  leaves `resolved_callee`/`resolved_method` unset in several cases *"so codegen can fall back
  cleanly"* (`sema.cryo:8355`). But codegen's fallback is **arity-only** and is documented to
  miscompile — *"can't distinguish `push(Str)` from `push(u8)` and picks the first declared (a hard
  miscompile / DirectPair-expand crash)"* (`sema.cryo:8586`). Two resolvers that must agree, where the
  backstop is known-broken, is the textbook fragile architecture.
- **R3 — Sentinel overloading (no `Unresolved`/`Deferred` type).** Sema has no first-class
  "not-yet-resolved" type, so it overloads two real values: **`void` as the unresolved-local
  sentinel** (`sema.cryo:3414` literally calls it *"sema's unresolved fallback"*) and
  **`TypeRef::invalid()` as the deferral return** (12+ sites). Because `void` is a genuine inhabited
  type, a void-sentinel local collides with real `void` and trips spurious checks (the documented
  *"Int and Void"* E0229 leak at `sema.cryo:5468`). This directly violates the type system's own stated
  invariant — **"No fallbacks by design — errors propagate as `ErrorType`"** — which sema bypasses.
- **R4 — Bare-name, last-write-wins registries.** The recurring justification for the inline lookup
  cascades is that *"`lookup_func_return(bare)` is a single-slot, last-write-wins map: when two
  modules both define `classify`, the bare lookup returns whichever was registered last"*
  (`sema.cryo:8197`, the #9 cross-module bug). Sema compensates with ~10 hand-rolled
  `bare → qualified → cross-module → arena-leaf` widen chains — **even though a consolidated
  `SymbolKey` with a `widen_to_method/qualified/simple` ladder already exists** (`symbol_key.cryo:244`)
  and is wired into exactly one collaborator (`new_delete_emitter`) and **zero** sema sites.

*(Not a defect — keep:)* a fifth cluster, **symbolic generic-body checking**, legitimately defers
because `T` is never concrete *in the template body itself* (`BoundedParam` method-through-bounds
lookup `sema.cryo:5737`; variant-name-only base read `sema.cryo:3970`). No reordering removes these;
they are inherent to checking generic bodies abstractly and must stay.

## Disposition taxonomy — what to do with each cluster

| Cluster (representative sites) | Root | Disposition |
| --- | --- | --- |
| Pre-mono re-derivation: `resolve_method_return_with_explicit_args` (9995, ~10 `return fallback`), `resolve_generic_method_return` (10756), `try_resolve_static_method` layered cascade (11472), method-return / field-via-template (9157, 9478), enum-pattern Fallback A/B (1373) | R1 | **DELETE — don't port.** On the authoritative post-mono pass the concrete spec is present; keep only the genuinely symbolic-walk-only branch (guarded by `in_symbolic_check`), delete the rest. |
| Two-resolver deferrals: `try_pin_overload_mangled_callee` no-op (8021), `resolve_method_overload` "null so codegen falls back" (8355/8586), `scope_is_generic_template` "decline to pin" (11280), `pin_method_callee_from_qname` (8315), "prefer mono's pin, recompute as fallback" (966, 11411) | R2 | **MAKE SEMA AUTHORITATIVE → delete codegen's parallel resolver.** Sema always pins `resolved_callee`/`resolved_method` post-mono; codegen's arity-only path becomes dead code and is removed. `scope_is_generic_template` is the linchpin "don't-pin-so-the-later-pass-can" seam the reorder dissolves. |
| The one **guess**: literal-default `→ i32/f64` last resort — PASS C (6541, 6709) + `infer_static_owner_return_from_args` Pass B (11698) | R1 (+ inherent tail) | **HARDEN TO A SINGLE GATED SITE.** This is the `Atomic<u64>::new ⇒ T=i32` miscompile. The reorder + keystone return-substitution removes the bad guess whenever a turbofish or concrete `expected_type` exists; the *only* irreducible case is a context-free all-literal call. Collapse the two sites into one function, gate it on "no turbofish AND no concrete expected type", and **emit a diagnostic requiring a turbofish** for externally-parameterized owners rather than silently choosing `i32`. |
| Lookup widen cascades (legitimate precedence, scattered): `lookup_type_by_sym` (376), `lookup_method_return_for_type` (360), `lookup_callee_function_type` (5937), `find_method_return_for_generic_receiver` (9544), same-leaf disambiguation (879, 8196), identifier local→capture→global (5272) | R4 | **CONSOLIDATE into one `resolve(SymbolKey, ctx)`** that builds the most-specific key, walks the existing `widen_to_*` ladder, and consults registry tiers (DI-qualified → DI-direct → Resolver scope chain → arena leaf) in **one** explicit precedence with a single same-leaf disambiguator. Semantics preserved; ~10 inline cascades become one resolver. Also fix the bare-name maps to stop colliding cross-module. |
| Sentinel overloading: `void`-as-unresolved local (3092, 3359, 1442, 3414), `invalid()`-as-deferral (5477…12923) | R3 | **REPLACE WITH TYPED OUTCOMES.** Introduce a dedicated `Unresolved`/`Deferred` (or a `ResolveOutcome { Resolved(t) \| Deferred \| Error }` return) so concrete checks short-circuit on identity instead of accidentally matching `void`, the defer/error distinction is checked once at the resolution boundary, and real errors propagate as `ErrorType` — restoring the type-system invariant. |
| Symbolic-walk deferrals + variant-name base read: `BoundedParam` through-bounds (5737), `patterns_cover` base enum (3970), Fallback A/B symbolic branch (1381) | (inherent) | **KEEP** — correct by design for abstract body checking; only narrow their guards to symbolic-only once R1 sites are deleted so they can't fire on the concrete pass. |
| Reorder-*introduced* idempotency guards: lambda re-lower guard (4388), synthetic-`this` guard (5258) | (dual-pass cost) | **MITIGATE.** These exist only to make one-shot side-effecting lowering safe under the second walk. Prefer moving closure/lambda *lowering* out of `resolve_expr` into a separate non-re-entrant lowering step rather than guarding re-entry; if that's out of scope, keep the guards but document them as dual-pass artifacts. |

## Target invariants for the new `sema/` (the north star)

1. **Single authoritative resolution.** Post-mono `SemaVisitor` always pins `resolved_callee` /
   `resolved_method` / `resolved_type`. Codegen consumes; it never re-resolves. Enforce with a
   pass-exit debug assertion ("every resolvable call node has a pinned callee"), then delete codegen's
   arity-only fallback chain.
2. **No sentinels.** `void` and `invalid()` stop meaning "unresolved"/"deferred". A typed
   `ResolveOutcome` (or dedicated arena `Unresolved` type) carries that state; genuine errors are
   `ErrorType`. The `symbolic_type_unresolved` / `symbolic_defer_type` predicates collapse to one
   boundary check.
3. **One lookup path.** Every name lookup goes through `resolve(SymbolKey, ctx)` over the existing
   `widen_to_*` ladder + an explicit registry-tier precedence. No inline `if valid return; else widen`
   cascades. Bare-name registries are keyed to not collide cross-module (fixes the #9 family at the
   source instead of compensating downstream).
4. **One inference routine.** The three Pass-A/B/C copies (`check_generic_free_call` 6458,
   `infer_free_call_bindings` 6664, `infer_static_owner_return_from_args` 11671) collapse into one;
   the literal-default rule lives in exactly one gated, diagnostic-emitting place.
5. **Delete-don't-port.** Pre-mono-only guard exits are dead weight in an authoritative-sema world.
   The port (Phases 2–4) removes them in the same commit that moves their concern; it does not
   relocate them into the new collaborators with the abstract logic intact.

## Mapping work to the collaborators (from the file-layout table)

- **`call_resolver.cryo`** — R2 deferrals (B-family), R4 call/identifier cascades (A4/A7/C1/C2), the
  free-call Pass-A/B/C collapse + literal-default gating.
- **`method_binding.cryo`** — the R1 `fallback: TypeRef` refine helpers (`resolve_method_return_*`,
  `resolve_generic_method_return`, `solve_method_bindings`, `try_resolve_static_method`) — the largest
  *delete-don't-port* surface.
- **`type_utils.cryo`** — the `SymbolKey`-driven lookup consolidation (`lookup_type_by_sym`,
  `lookup_method_return_for_type`, `resolve_type_name`).
- **`member_resolver.cryo`** — method-return / field-via-template base-template crutches (R1).
- **`pattern_resolver.cryo`** — enum-pattern Fallback A/B + the `void`-binding sentinel (R1 + R3);
  keep the variant-name base read.
- **`literal_resolver.cryo`** — owns the single canonical literal-default rule.
- **`scope_manager.cryo` / `state.cryo`** — the `Unresolved` typed-outcome for local registration (R3).
- **`symbolic_checker.cryo`** — owns the *keep* cluster; its guards get narrowed to symbolic-only.
- **`sema_visitor.cryo`** — the `post_mono_verify` re-resolve gate and the two reorder-introduced
  idempotency guards (the lambda-lowering-extraction mitigation lives here).

*(Verification for all of the above is the single differential-self-compilation + fallback-census
strategy in the **Verification** section above — there is no separate verification regime for the
hardening, because the hardening is not a separate phase.)*
