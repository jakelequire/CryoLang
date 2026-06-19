# Sema (and later Mono) Structure Refactor — Progress Tracker

> Living log for the multi-session, build-from-scratch decomposition of `sema.cryo`
> (and afterward `monomorphizer.cryo`) into clean subsystems. Append dated notes.
> Authoritative specs: `.todo/plans/SEMA-STRUCTURE-REFACTOR.md`,
> `.todo/plans/MONO-STRUCTURE-REFACTOR.md`. Entry-ramp handoff is in the first
> message of the originating session.

## Branch / safety
- Working on `mono-sema-rewrite`. `main` is the safe `mono-before-sema` compiler (fallback).
- The maintainer owns every `git commit` and every `make pin`. I build/test only.

## The maintainer's explicit deviations from the plans (READ FIRST)
1. **Build FROM SCRATCH, not relocate.** Do NOT `git mv` sema.cryo into the folder. Write
   each `sema/` file clean, in its target shape, hardening as we go.
2. **Build DARK.** Do NOT add `public module Sema;` to `compiler/src/compiler/_module.cryo`.
   The whole `sema/` folder stays invisible to the compiler (never compiled) until the
   entire refactor is finished. Leave the live `passes/sema.cryo` (`TypeCheckVisitor`)
   untouched and wired in as today.
3. **Build everything first, debug holistically after** — not incrementally. No test
   feedback until flip-on. So: maximize compile-faithfulness by heavy reference to the
   working source + existing idioms.
4. **Mono refactor next**, same approach: clean `compiler/src/compiler/mono/`, also dark,
   sequenced after the sema build.
5. Aggressive approach is fine (main is the safety net). Attack root causes; no shims.

## Architecture decisions locked in
- **Namespace** `Compiler::Sema`; folder `compiler/src/compiler/sema/`. Main class
  `SemaVisitor : BaseASTVisitor`; collaborators are plain `type struct`s with back-pointers,
  `static null()`, `wire()` — exactly the codegen `IRGeneratorVisitor` + sub-emitter pattern.
- **Recursion seam = Option B (side-channel), NOT Option A.** Rationale: Option A adds a
  virtual `resolve_expr_dyn` to the live shared `BaseASTVisitor`, which would alter every
  subclass's vtable in the LIVE compiler — violates "build dark / don't disturb main".
  Option B is fully contained in `sema/`. Mechanism: `SemaState.last_expr_type: TypeRef`
  is the expression return channel. The authoritative dispatch is `SemaVisitor.resolve_expr(e)
  -> TypeRef` (a `match` on node kind). Each `override visit(SomeExpr*)` is a thin delegate:
  `this.state.last_expr_type = this.resolve_expr(node);`. Collaborators recurse via
  `this.dispatch.resolve_expr(visitor, child)` which does `child.accept(visitor)` then returns
  `state.last_expr_type`. Collaborators import only the abstract `ASTVisitor`, never `SemaVisitor`
  (keeps the module graph acyclic).
- **`ResolveOutcome` = struct-with-kind-discriminator**, NOT a payload enum. Mirrors the
  deliberate choice documented in `resolver/symbol_key.cryo` ("struct with a kind discriminator
  rather than an enum with associated data, so fields are directly accessible"). Kind =
  `{ Resolved, Deferred, Error }`; carries `type: TypeRef`.
- **`accept(visitor: ASTVisitor*)`** is the existing virtual on `ASTNode` (node.cryo:27) — the
  seam relies on it; no base-class change needed.

## Gating split (from the handoff)
- **DO NOW (no dependency on mono-after-sema being finished):** all structure (folders,
  collaborators, state, interfaces) + R3 (typed `ResolveOutcome` instead of void/invalid
  sentinels) + R4 (one `resolve(SymbolKey, ctx)` lookup over the widen ladder).
- **GATED (do NOT delete yet — port faithfully + tag `// GATED: mono-after-sema`):** R1 pre-mono
  fallback deletion (`resolve_method_return_with_explicit_args`, `resolve_generic_method_return`,
  `try_resolve_static_method` cascade, enum-pattern Fallback A/B, …) and R2 codegen
  parallel-resolver deletion + the Phase-5 pass-exit "every call pinned" assertion. These are
  load-bearing while sema can still run pre-mono in some paths. Becomes a finite deletion
  worklist once mono-after-sema lands.
- **Frozen external contract:** `CallExprNode.resolved_type_args` (the stash mono consumes).
  Do not change its shape/meaning. Everything else inside sema is fair game.

## Target file layout (≈14 files) — see plan §"Target file layout" for per-file contents
state.cryo, outcome.cryo, helpers.cryo, scope_manager.cryo, type_utils.cryo,
literal_resolver.cryo, diagnostics.cryo, sema_dispatch.cryo, pattern_resolver.cryo,
lambda_synth.cryo, member_resolver.cryo, symbolic_checker.cryo, method_binding.cryo,
call_resolver.cryo, sema_visitor.cryo (main, declared LAST in _module.cryo). _module.cryo.

## SemaState field set (mutable per-pass; env stays on SemaVisitor)
Moved into SemaState: locals, local_muts, local_kw_spans, undo_{keys,prev,muts,kw_spans},
return_type, return_type_ann, this_type, this_is_mut, current_owner_type, body_count,
loop_depth, switch_depth, expected_type, payload_binding_ids, param_ids, in_lambda_depth,
lambda_outer_locals, current_lambda, closure_counter, closure_spec_map, in_implicit_conversion,
post_mono_verify, try_counter, symbolic_check_gate, in_symbolic_check,
symbolic_generic_param_ids, symbolic_owner_param_ids, symbolic_owner_is_generic,
symbolic_owner_param_nodes, symbolic_method_param_nodes, symbolic_impl_node,
symbolic_current_func, symbolic_bodies_walked, symbolic_total_would_emit,
**plus** last_expr_type (the Option-B seam channel).
Stays on SemaVisitor: arena, checker (by value), intern, ctx, this_sym.

## STATUS LOG
### 2026-06-19 (session 1)
- Read both plans, codegen precedent (ir_generator/visitor_state/_module), SymbolKey, the AST
  visitor base, sema.cryo head (imports + 58 fields + constructor + first ~700 lines of helpers).
- Locked the architecture decisions above.
- Confirmed collaborator-calling convention from codegen `call_emitter.cryo`:
  `emit(mut &this, visitor: ASTVisitor*, node: X*)` — collaborators take the abstract
  `ASTVisitor*` and recurse via `dispatch.<recurse>(visitor, child)` → `child.accept(visitor)`.

**Files written (all DARK — `sema/` is NOT registered in parent `_module.cryo`):**
- `sema/_module.cryo` — dark module list (grows as files are added; SemaVisitor declared last).
- `sema/outcome.cryo` — `ResolveOutcome` (struct+kind: Resolved/Deferred/Error) + `get_type`.
- `sema/state.cryo` — `SemaState` (all 36 mutable fields + `last_expr_type` seam channel; `new()`).
- `sema/sema_dispatch.cryo` — `SemaDispatch{ctx,state*}`: resolve_expr / dispatch_stmt /
  dispatch_top_level (the Option-B seam).
- `sema/scope_manager.cryo` — `ScopeManager{state*}`: lookup_local, is_local_or_param, is_param,
  is_payload_binding, local_is_mutable, save_scope, register_local{,_mut,_with_mut},
  record_local_kw_span, restore_scope. (Dropped a dead `hash_int` line from lookup_local.)

**Method index of sema.cryo captured** (single-line-signature methods, with line numbers) — saved
mentally / re-grep with: `^    (override |static )?[a-z_]\w*\(.*\) -> `. NOTE: the big entangled-core
methods have MULTI-LINE signatures so they don't appear in that grep — they live in the gaps
(e.g. resolve_method_call, resolve_method_return_with_explicit_args, resolve_generic_method_return,
try_resolve_static_method, solve_method_bindings, stash_method_call_bindings, find_template_method
~8876, reduce_assoc_projections ~10991, the import-insertion/diag block ~9883-10800). Target those
by name when building method_binding/call_resolver.

**Decision on R4 (lookup-cascade consolidation):** the single `resolve(SymbolKey, ctx)` needs a
global view of all ~10 cascade call sites (in type_utils + call_resolver + member_resolver +
identifier resolution). So: port the cascades FAITHFULLY into their collaborators now, tag each
`// R4: fold into resolve(SymbolKey)`, and do the consolidation once all consumers exist and the
subsystem stands. Principled (needs whole-subsystem view), not slop-relocation. Same tracking
discipline as the GATED R1/R2 tags.

**Files written this session (8 total, all DARK):** _module.cryo, outcome.cryo, state.cryo,
sema_dispatch.cryo, scope_manager.cryo, helpers.cryo, literal_resolver.cryo, type_utils.cryo.
- `helpers.cryo` — also absorbed the stateless predicates `is_numeric_type_name`,
  `lexeme_looks_like_number`, `ns_shared_prefix_len` (were `&this` methods that ignored `this`),
  and exposes `sema_int_unsigned_bound` / `sema_default_int_type` so literal_resolver never
  references the private `SEMA_*` consts cross-module.
- `literal_resolver.cryo` — `LiteralResolver{arena,ctx,state}`: resolve_literal/integer/float,
  expected_type_is_int_type, int_literal_fits_target, is_signed_int_type. Owns the single
  literal-default rule (via helpers). `expected_type` reads now go through `state.expected_type`.
- `type_utils.cryo` — `TypeUtils{arena,intern,ctx,checker}`: ~28 methods — DI lookup delegates
  (lookup_func_return/method_return{,_raw}/type_by_sym/type_sym/global_var, canonicalize_method_return,
  resolve_cross_module_name, scope_fn_arity, find_template_module), structural probes
  (contains_generic_param/_unresolved_projection, unwrap_to_enum/base_type, peel_to_instantiation,
  generic_base_of, proj_pointer/reference), relational predicates (enum_int_compatible,
  is_reference_type, is_ref_or_ptr_type, peel_indirection, indirection_compatible,
  arg_type_is_concrete, same_param_sig, types_equivalent, is_ptr_or_ref_to), type_display_name.
  Lookup cascades tagged `// R4:`.

**MEMBERSHIP NOTES decided while building (so the next session doesn't re-litigate):**
- Visibility enforcement (enforce_field_visibility, check_type_name_visibility,
  check_annotation_visibility, current_module_name, visibility_leaf_name, is_subclass_of@12291)
  → member_resolver, NOT type_utils. (type_utils stays pure type queries.)
- Return-escape helpers (ret_carries_pointer_to, callee_names_ret_variant, check_return_payload_escape,
  check_return_stack_address, arm_tail_expr, emit_payload_escape) → sema_visitor / pattern_resolver
  per plan, NOT type_utils. (types_equivalent + is_ptr_or_ref_to ARE in type_utils — pure.)
- Arg-checking (flag_autoref_from_params, check_scope_call_arg_types, check_static_scope_method_args,
  check_args_against_params, args_fit_params_loosely, has_implicit_converter) → call_resolver.

- `diagnostics.cryo` — `Diagnostics{arena,intern,ctx,state,types:TypeUtils*}`: collectors
  (collect_local_names + bare callable/type + struct_or_class_member_names), the mismatched-types
  builder (emit_type_mismatch{,_with_value} + attach_type_coercion_suggestion via Helpers'
  is_numeric_type_name/lexeme_looks_like_number), attach_did_you_mean, emit_method_arity_error,
  note_arity, emit_payload_escape. type_display_name is reached via the `types` peer pointer.
  DEFERRED (tagged TODO in-file): find_import_insertion_span, suggest_similar_method,
  find_shadowed_*, emit_switch_subject_error (all in the ~9883+ core tangle) — port when reading
  that region. emit_free_* stays with CallResolver.

### SESSION 2 (2026-06-19 cont.) — core trio + medium collaborators built
Built (all DARK): symbolic_checker, method_binding, member_resolver, pattern_resolver, lambda_synth.
14 of ~16 files now exist. Remaining: **call_resolver.cryo** (the last big core file) + **sema_visitor.cryo**
(the main visitor / entry point). Then flip-on + debug.

Key build decisions made this session (so the next session doesn't re-litigate):
- **Walk-drivers stay on SemaVisitor, not the symbolic collaborator.** `symbolic_check_body`,
  `symbolic_check_owner_methods`, `visit_methods`, `enter_function` drive `this.visit(body)` +
  per-function frame setup → they live on the MAIN visitor (can't import SemaVisitor from a
  collaborator). SymbolicChecker owns only the PURE helpers (param-binding, owner-receiver detection,
  abstract field/method resolution, defer predicates). The drivers call `this.symbolic.<helper>`.
- **method_binding** (28 methods) holds a private `trait_checker()` that constructs a fresh
  `TraitChecker` on demand (4-pointer view) — matches the monolith. GATED-tagged the two R1
  `fallback`-returning methods (resolve_method_return_with_explicit_args, resolve_generic_method_return).
  proj_pointer_pointee/proj_reference_referent are NOT redefined — it calls `this.types.*` (they live
  in type_utils). `subst_this_in_type` + reduce_assoc_projections + all proj_* + resolve_trait_impl_method_return
  + finalize_assoc_return are here.
- **member_resolver** takes `visitor: ASTVisitor*` on resolve_member_access/resolve_array_access (dispatch
  recursion). Holds peers types/symbolic/binding/diag/dispatch + env arena/intern/ctx/checker/state.
  Owns resolve_field_via_template + try_field_function_call + all visibility (enforce_field/method,
  check_type_name/annotation, is_subclass_of, current_module_name, visibility_leaf_name).
- **pattern_resolver** — binding + exhaustiveness; uses scopes.register_local + sema_copy_type_refs (added
  to Helpers) + sema_decode_range_bound. arm_tail_expr lives here.
- **lambda_synth** — resolve_lambda takes `visitor`; per-function frame save/restore goes through
  `state.*` with `swap()` for owning arrays (faithful to monolith's alias-and-restore). Owns
  record_lambda_capture, synthesize_closure_struct, is_closure_struct_type, maybe_rewrite_closure_call,
  try_specialize_for_closure_args, find_top_level_function, mint_closure_{name,spec_name},
  reject_closure_struct_args_in_non_free_call.
- Helpers grew: is_numeric_type_name, lexeme_looks_like_number, ns_shared_prefix_len, sema_copy_type_refs.

**call_resolver.cryo — METHOD MAP (read these by line when building it):** resolve_call 5970,
resolve_direct_call 8174, resolve_method_call 9070, resolve_method_overload 8356, resolve_scope_call 11213,
resolve_scope_resolution 13219, resolve_module_qualified_function 865 (already read 865-1050) +
subst_return_from_call_args 1058 (read), resolve_generic_scope_name 11825, scope_is_generic_template 11292,
try_resolve_static_method 11472 (GATED R1), infer_static_owner_return_from_args 11671 (GATED, literal-default
guess — the Atomic<u64> trap), pin_scope_callee_qsym 11462, pin_method_callee_from_qname 8309,
check_generic_free_call 6458, emit_free_cannot_infer 6814, check_call_arity 6871, has_implicit_converter 7089,
int_literal helpers (int_literal_fits_target/is_signed in literal_resolver already), flag_autoref_from_params
7732, check_args_against_params 7218, args_fit_params_loosely 7050, check_method_call_arg_types 7452,
check_scope_call_arg_types 7768, check_static_scope_method_args 7823, lookup_callee_function_type 5932,
lookup_scope_variant_payload_types 5806, find_fn_template_for_call 6282, overload_return_type 8144,
overload_param_matches_arg 8153, method_origin_trait 7432, canon_trait_name 7446, method_type_is_variadic 7420,
subst_free_call_return 6638, infer_free_call_bindings 6664, sema_arg_is_polymorphic_literal 6331,
free_infer_type_concrete 6363, literal_adopts_binding 6401, free_infer_arg_reliable 6419, promote_to_expected_instantiation
11982, generic_base_of (type_utils). Plus the diagnostics-group methods STILL pending in diagnostics.cryo
(suggest_similar_method 9785, find_shadowed_type_candidates 9816, find_import_insertion_span 9883,
attach_shadow_import_suggestions 9921 — port when reading the 9785-9955 region).

### call_resolver BUILD PLAN (spine + inference cluster already READ this session)
READ so far (5806-6871): lookup_scope_variant_payload_types 5806, copy_type_refs 5884 (→ now
Helpers::sema_copy_type_refs), substitute_payload_types 5893, lookup_callee_function_type 5932,
**resolve_call 5970** (the central dispatcher — read in full), find_fn_template_for_call 6282,
sema_arg_is_polymorphic_literal 6331, free_infer_type_concrete 6363, literal_adopts_binding 6401,
free_infer_arg_reliable 6419, **check_generic_free_call 6458** (the keystone — read), subst_free_call_return
6638, infer_free_call_bindings 6664, stash_scope_resolution_call_bindings 6745, emit_free_call_bound_failure
6794, emit_free_cannot_infer 6814, emit_free_infer_conflict 6827, check_call_arity 6871 (start).
STILL TO READ before writing call_resolver: check_call_arity tail 6874-7050, args_fit_params_loosely 7050,
has_implicit_converter 7089-7325, check_args_against_params 7218, method_type_is_variadic 7420,
method_origin_trait 7432, canon_trait_name 7446, check_method_call_arg_types 7452,
check_scope_call_arg_types 7768 (read earlier), check_static_scope_method_args 7823 (read earlier),
overload_return_type 8144, overload_param_matches_arg 8153, resolve_direct_call 8174,
pin_method_callee_from_qname 8309, resolve_method_overload 8356, resolve_method_call 9070,
resolve_scope_call 11213, scope_is_generic_template 11292, pin_scope_callee_qsym 11462,
try_resolve_static_method 11472 (GATED R1), infer_static_owner_return_from_args 11671 (GATED — the
Atomic<u64> literal-default trap; HARDEN to one gated diagnostic site), resolve_generic_scope_name 11825,
promote_to_expected_instantiation 11982, resolve_scope_resolution 13219, resolve_module_qualified_function
865 (read), subst_return_from_call_args 1058 (read). ALSO referenced by resolve_call but not yet located:
**lookup_method_param_types**, **try_pin_overload_mangled_callee** (grep them).
call_resolver env+peers: arena/intern/ctx/checker + state, types, symbolic, binding, diag, lambda
(maybe_rewrite_closure_call/try_specialize/reject_closure...), dispatch (resolve_expr on args), literals
(int_literal_fits_target/is_signed_int_type — for the arg-type checks), pattern? (no). Needs its own
private `trait_checker()` helper (like method_binding). resolve_call takes `visitor: ASTVisitor*`.
GATED-tag try_resolve_static_method + the two-resolver "decline to pin" seams (scope_is_generic_template,
try_pin_overload_mangled_callee no-op). Collapse the 3 inference copies is the R1/#"one inference routine"
target but is GATED — port faithfully now.

### sema_visitor.cryo BUILD PLAN (the main visitor, built LAST)
Owns: SemaVisitor class (env arena/checker/intern/ctx/this_sym + state by value + ALL collaborators by
value), constructor (::null() all collaborators), `static build(ctx)`, `wire()` (after leak),
`run_sema` entry (leak+wire+visit, from 13828 run_function_body_type_check — RENAME to run_sema),
SemanticPasses struct. Plus: resolve_expr 4306 (the authoritative match-on-kind switch → delegates to
collaborators), every `override visit(...)` as a thin delegate that stashes `state.last_expr_type`,
the per-function drivers enter_function 1485 + visit_methods 2126 + symbolic_check_body 2197 +
symbolic_check_owner_methods 2281, control-flow/returns (check_function_returns 1636,
default_return_expr_for 1685, return_type_requires_return 1716, block_always_returns 1727,
stmt_always_returns 1739, block_contains_break 1831, stmt_contains_break 1839, expr_is_divergent 1886),
return-escape (check_return_stack_address 3495, check_return_payload_escape 3657, ret_carries_pointer_to
3601, callee_names_ret_variant 3616, is_local_or_param/is_param via scopes), switch checks
(visit SwitchStmt 4180, switch_type_class 4201, the case/duplicate checks 4226/4271), decl visitors
(FunctionDecl 1607, Struct/Class/Trait 1958/1972/1986, ImplBlock 2008 + check_assoc_decl_bounds 2069 +
trait_checker 2052 + trait_leaf_name 2753 [609-LINE monster — SPLIT], DeclStmt 2762, Destructure 3111 +
register_destructure_locals_void 3362, impl_trait_annotation_of 2575, check_opaque_return_item 2716),
the expr resolvers that stay here (resolve_binary 5332, resolve_unary 5612, resolve_identifier 5253
[calls lambda.record_lambda_capture + scopes + types + diag], check_assignment_lvalue 5507, resolve_cast
12579 + is_aggregate_scalar_cast 12544, resolve_if_expr 12612, resolve_ternary 12621, resolve_match_expr
12630, resolve_static_match_expr 12716, enum_try_shape 12770, build_try_desugar 12807, resolve_try_expr
12884, resolve_struct_literal 12965, resolve_scope_resolution 13219, resolve_new_expr 13249,
expected_is_dynamic_array 13282, resolve_array_literal 13295, resolve_tuple_literal 13429,
promote_to_expected_instantiation 11982 [maybe call_resolver]), prime_typeof_annotation 428,
the two reorder idempotency guards. resolve_expr dispatches: Literal→literals, Identifier→resolve_identifier,
Member→member.resolve_member_access(visitor,..), ArrayAccess→member.resolve_array_access(visitor,..),
Call→call.resolve_call(visitor,..), Lambda→lambda.resolve_lambda(visitor,..), etc.

### ✅✅ SESSION 3 (2026-06-19 cont.) — ALL 16 FILES BUILT (sema subsystem complete, DARK)
The entire decomposed Sema subsystem now exists in `compiler/src/compiler/sema/`:
_module, outcome, state, dispatch (renamed from sema_dispatch; type SemaDispatch, namespace
Compiler::Sema::Dispatch), helpers, scope_manager, type_utils, literal_resolver, diagnostics,
symbolic_checker, method_binding, member_resolver, pattern_resolver, lambda_synth, call_resolver,
**sema.cryo** (main visitor SemaVisitor + SemanticPasses::run_sema; namespace Compiler::Sema::Sema).
Renames done per maintainer: sema_dispatch.cryo→dispatch.cryo, main file→sema.cryo.
Still DARK: parent compiler/src/compiler/_module.cryo does NOT have `public module Sema;`.

**sema.cryo wiring (the composition root):** owns arena/checker(by value)/intern/ctx/this_sym +
state(by value) + 11 collaborators(by value). build(ctx)→ctor null()s all. wire() (after Box::leak)
sets every back-pointer. run_sema = leak→set post_mono_verify→wire→visit(root). resolve_expr is the
authoritative match dispatch; 25 thin expr visit() overrides feed the Option-B seam; stmt/decl visits
carry real logic. Added Helpers::sema_i32_max_mag() accessor (resolve_binary needs the private const).

**KNOWN RISKS to verify in the holistic-debug phase (first `make cryo` after flip-on):**
1. `Box<SemaVisitor>` — assumed prelude-provided (codegen/passes.cryo uses Box::new().leak()); confirm import.
2. Plain (non-`public`) free functions cross-module: extract_leaf_segment/extract_parent_path (Types::Arena),
   annotation_span (AST::NodeLocator) — used via import; the monolith did the same, so should be fine.
3. `namespace Compiler::Sema::Sema` (double Sema) — module system requires file `sema.cryo`→module `Sema`.
   If the compiler rejects the double name, rename the file (e.g. visitor.cryo→module Visitor) OR ask maintainer.
4. `AssocProjectionType` import home — assumed Types::Generic/Compound (imported in the files that use it).
5. `DiagnosticSink*` type (used in sema.cryo symbolic_check_body) — from Compiler::Diag; confirm exported.
6. Collaborator `&this` vs `mut &this`: methods that mutate ONLY through pointer fields (state/arena) were
   given `&this` where the monolith had it; if the compiler complains about mutation through a const-this
   pointer field, flip to `mut &this`.
7. The 25 expr visit() overrides: each calls resolve_expr which re-enters via the seam — verify no
   infinite recursion / stale last_expr_type (the channel is reset in dispatch.resolve_expr before accept).
8. Field shapes / method names assumed from reads (e.g. node.has_else(), et.is_simple_enum(),
   ft.is_variadic, MethodInfo struct-literal fields) — any drift surfaces as a compile error at flip-on.

**FLIP-ON STEPS (when maintainer is ready to test):**
1. Add `public module Sema;` to `compiler/src/compiler/_module.cryo`.
2. Remove `public module Sema;` from `compiler/src/compiler/passes/_module.cryo`.
3. `passes/pass_registry.cryo`: import `Compiler::Passes::Sema;` → `Compiler::Sema::Sema;` (line ~38);
   dispatch `SemanticPasses::run_function_body_type_check(ctx)` → `SemanticPasses::run_sema(ctx)` (line ~447).
4. (Optional) delete `passes/sema.cryo` once the new subsystem passes — keep it until then for diff/reference.
5. `make cryo` (PowerShell, CRYO_CC=gcc) → iterate on compile errors (the risks above) → `make test` →
   differential self-compile (old pin vs new) → review diffs. Per the plan: differential, not byte-identical.

### SESSION 3 (2026-06-19 cont.) — call_resolver + diagnostics complete (15/16 files)
Built `call_resolver.cryo` (the largest file, ~50 methods, fully ported with visitor-threading +
peer-mapping + GATED tags). Added the 4 deferred import-shadow methods to `diagnostics.cryo`
(suggest_similar_method, find_shadowed_type_candidates, find_import_insertion_span,
attach_shadow_import_suggestions — `extract_leaf_segment`/`extract_parent_path` are plain functions in
`Compiler::Types::Arena`, visible via import). ONLY `sema_visitor.cryo` (the main visitor) remains.

**call_resolver peer-wiring confirmed:** env arena/intern/ctx/checker + peers state, types, literals,
diag, binding, symbolic, member, lambda, scopes, dispatch. Has its own private `trait_checker()` +
`current_module_name()`. `resolve_call`/`lookup_method_param_types`/`resolve_method_overload`/
`resolve_method_call`/`check_method_call_arg_types`/`check_call_arity`/`check_args_against_params`/
`try_apply_implicit_conversion`/`build_implicit_conversion`/`check_scope_call_arg_types`(not built —
see note)/`check_static_scope_method_args`(not built) take `visitor: ASTVisitor*`.
✅ RESOLVED: `check_scope_call_arg_types` + `check_static_scope_method_args` are now appended to
call_resolver.cryo (with visitor threaded). call_resolver.cryo is COMPLETE.

### sema_visitor.cryo — COMPLETE BUILD SPEC (the final file)
**Class:** `type class SemaVisitor : BaseASTVisitor`. Env BY VALUE: `arena: TypeArena*`,
`checker: TypeChecker` (by value!), `intern: InternTable*`, `ctx: CompilationContext*`,
`this_sym: SymbolStr`. `state: SemaState` (by value). ALL collaborators BY VALUE (ScopeManager,
TypeUtils, LiteralResolver, Diagnostics, SemaDispatch, SymbolicChecker, MethodBinding, PatternResolver,
LambdaSynth, MemberResolver, CallResolver). Constructor `::null()`s every collaborator + state=SemaState::new()
+ checker=TypeChecker::new(arena) + this_sym=intern.intern("this"). `static build(ctx)`. `wire()` (after leak)
sets every collaborator's back-pointers (env=&this.arena etc.? NO — arena is already a pointer; pass
this.arena, this.intern, this.ctx, &this.checker, &this.state, &this.<peer>). `run_sema(ctx)` =
SemanticPasses entry (sema.cryo 13828): Box<SemaVisitor>::new(build(ctx)).leak() → v.state.post_mono_verify =
ctx.provisions.has(Provision::MonomorphizationComplete) → v.wire() → v.visit(root). (RENAME run_function_body_type_check→run_sema.)

**resolve_expr (4306) — the authoritative dispatch, STAYS on SemaVisitor.** Maps (confirmed):
Literal→this.literals.resolve_literal; Identifier→this.resolve_identifier (stays here); Binary→this.resolve_binary;
Unary→this.resolve_unary; Call→this.calls.resolve_call(this, ..); Member→this.member.resolve_member_access(this, ..);
ArrayAccess→this.member.resolve_array_access(this, ..); Cast→this.resolve_cast; If→this.resolve_if_expr;
Ternary→this.resolve_ternary; Match→this.resolve_match_expr; StaticMatch→this.resolve_static_match_expr;
StructLiteral→this.resolve_struct_literal; ScopeResolution→this.resolve_scope_resolution (stays here; uses
this.calls.promote_to_expected_instantiation + validate_enum_scope_member); New→this.resolve_new_expr;
ArrayLiteral→this.resolve_array_literal; TupleLiteral→this.resolve_tuple_literal; Sizeof/Alignof→arena.get_u64();
Typeof→emit E0209; Delete→resolve operand+void; Try→this.resolve_try_expr; Lambda→this.lambda.resolve_lambda(this, ..).
Internal recursion in SemaVisitor methods uses `this.resolve_expr(child)` directly. Head guard:
`if (expr.has_resolved_type() && !this.state.post_mono_verify) return expr.resolved_type;` tail: set_resolved_type.

**SEAM REQUIREMENT (Option B):** the monolith has NO expression visit() overrides (exprs go through
resolve_expr directly). But collaborators recurse via dispatch.resolve_expr(visitor,child)→child.accept(visitor)
→ needs a visit override per concrete EXPR node that writes the channel. So ADD ~25 thin expr visit overrides:
`override visit(mut &this, node: LiteralNode*) -> void { this.state.last_expr_type = this.resolve_expr(node as ExpressionNode*); }`
for: Literal, Identifier, Binary, Unary, Ternary, IfExpr, MatchExpr, StaticMatchExpr, Call, New, Sizeof, Alignof,
Cast, StructLiteral, ArrayLiteral, TupleLiteral, Lambda, ArrayAccess, MemberAccess, ScopeResolution, Typeof,
Delete, Await, Yield, Try. (These are the seam; the monolith didn't need them.)

**Statement/decl visit overrides (REAL logic, port from monolith):** ProgramNode 1545 (loops
dispatch_top_level), dispatch_top_level 1553 (injected-spec namespace swap; uses this.types.find_template_module),
FunctionDecl 1607, StructDecl 1958, ClassDecl 1972, TraitDecl 1986, ImplBlock 2008, BlockStmt 2559, ExprStmt 2568,
DeclStmt 2762, DestructureDecl 3111, ReturnStmt 3426, IfStmt 3716, WhileStmt 3724, DoWhileStmt 3731, ForStmt 3738,
MatchStmt 3797, StaticMatchStmt 3826, LoopStmt 4154, BreakStmt 4160, ContinueStmt 4168, UnsafeBlockStmt 4176,
SwitchStmt 4180. Internal recursion into stmts uses `this.dispatch_stmt(stmt)` = stmt.accept(this) (keep a
dispatch_stmt method on SemaVisitor, or use this.dispatch.dispatch_stmt(this, stmt)).

**Methods that STAY on SemaVisitor (read+port):** enter_function 1485 (READ — resets state.* + registers params
via this.scopes.register_local_with_mut), visit_methods 2126 (READ), symbolic_check_body 2197 (READ),
symbolic_check_owner_methods 2281 (READ), check_assoc_decl_bounds 2069 (READ; uses this.binding.trait_checker?
no — make a local trait_checker or call this.binding's; uses trait_leaf_name), trait_checker() 2052 (tiny),
prime_typeof_annotation 428 (READ), resolve_identifier 5253 (READ; uses scopes/lambda.record_lambda_capture/
types/diag/state), expected_type_is_int_type→this.literals. Control-flow/returns: check_function_returns 1636,
default_return_expr_for 1685, return_type_requires_return 1716, block_always_returns 1727, stmt_always_returns 1739,
block_contains_break 1831, stmt_contains_break 1839, expr_is_divergent 1886, match_is_exhaustive→this.patterns.
Return-escape: check_return_stack_address 3495 (READ), check_return_payload_escape 3657, is_local_or_param→this.scopes,
is_param→this.scopes, ret_carries_pointer_to 3601 (uses this.types.is_ptr_or_ref_to), is_ptr_or_ref_to→this.types,
callee_names_ret_variant 3616, arm_tail_expr→this.patterns, emit_payload_escape→this.diag,
check_return_payload_escape 3657. Switch: switch_type_class 4201, + the case/duplicate checks 4226/4271.
Expr resolvers (STAY here, READ+port): resolve_binary 5332, resolve_unary 5612, check_assignment_lvalue 5507,
resolve_identifier 5253, resolve_cast 12579, is_aggregate_scalar_cast 12544 (READ), resolve_if_expr 12612,
resolve_ternary 12621, resolve_match_expr 12630, resolve_static_match_expr 12716, enum_try_shape 12770,
build_try_desugar 12807, resolve_try_expr 12884, resolve_struct_literal 12965, resolve_scope_resolution 13219 (READ),
validate_enum_scope_member (grep), resolve_new_expr 13249, expected_is_dynamic_array 13282, resolve_array_literal 13295,
resolve_tuple_literal 13429. Decl-helper: impl_trait_annotation_of 2575, check_opaque_return_item 2716,
trait_leaf_name 2753 (⚠609 LINES — SPLIT into sub-methods), register_destructure_locals_void 3362,
bind_arm_patterns→this.patterns, bind_subpattern→this.patterns, check_match_exhaustive→this.patterns,
check_duplicate_default_arm→this.patterns, check_range_patterns→this.patterns.
**Peer-mapping in SemaVisitor methods:** this.lookup_local→this.scopes.lookup_local; this.register_local*→this.scopes;
this.unwrap_to_enum/peel_to_instantiation/lookup_type_by_sym/contains_generic_param/etc→this.types;
this.resolve_literal/int_literal_fits_target→this.literals; this.emit_*→this.diag; this.resolve_call→this.calls;
this.resolve_member_access→this.member; this.resolve_lambda/record_lambda_capture→this.lambda;
this.symbolic_*→this.symbolic; this.solve_method_bindings etc→this.binding; this.resolve_expr/dispatch_stmt stay.
READ-LIST for the final write: 1607-1958, 1958-2126, 2126-2340(symbolic drivers READ), 2559-2770, 2753-3110
(trait_leaf_name), 3111-3475, 3716-3826, 4154-4306, 5332-5810, 12544-13000, 13249-13530, + grep validate_enum_scope_member.

### END OF SESSION 1 — clean checkpoint (9 files, all DARK / not wired into parent)
Foundational + leaf layer COMPLETE: _module, outcome, state, sema_dispatch, scope_manager,
helpers, literal_resolver, type_utils, diagnostics. This is everything the medium/core
collaborators depend on for state, the recursion seam, pure helpers, literal typing, scope, type
queries, and diagnostics. No compiler run yet (build-dark; debug holistically at the end).

**WHERE TO RESUME (session 2), in order:**
1. `member_resolver.cryo` — resolve_member_access (12308-12469), resolve_array_access (12470-12543),
   is_subclass_of@12291, enforce_field_visibility (12068), check_type_name_visibility (12164),
   check_annotation_visibility (12183), current_module_name (12139), visibility_leaf_name (12146),
   try_field_function_call + resolve_field_via_template (multi-line sigs — find by name). Recurses
   into resolve_expr (object) → takes `visitor: ASTVisitor*` + uses dispatch. Likely calls method
   resolution → may need a method_binding peer ptr (build that interface first if so).
2. `pattern_resolver.cryo` — bind_arm_patterns (1340), bind_subpattern (1467),
   resolve_variant_payload_types, check_match_exhaustive/match_is_exhaustive, range/dup-arm checks
   (check_duplicate_default_arm 3846), patterns_cover/variant_covered, arm_tail_expr (3638),
   arm_payload_irrefutable (3938). Uses sema_decode_range_bound (Helpers).
3. `lambda_synth.cryo` — resolve_lambda (4383 ~250ln), record_lambda_capture, mint_closure_name
   (4629), synthesize_closure_struct (4658 ~250ln), is_closure_struct_type (4908),
   maybe_rewrite_closure_call (4921), try_specialize_for_closure_args, find_top_level_function
   (5111), reject_closure_struct_args_in_non_free_call.
4. CORE TRIO (largest, GATED R1/R2 fallbacks — tag `// GATED: mono-after-sema`, do NOT delete):
   `symbolic_checker.cryo` (symbolic_* @2338-2549, symbolic_check_body/owner_methods — find),
   `method_binding.cryo` (find_generic_method_*, find_template_method 8876, solve_method_bindings,
   stash_method_call_bindings, resolve_method_return_*, resolve_generic_method_return,
   subst_this_in_type 9449, reduce_assoc_projections 10991, try_trait_impls_for 9741,
   lookup_trait_defining_method 9774), `call_resolver.cryo` (resolve_call 5970, resolve_direct_call
   8174, resolve_method_call [find], resolve_method_overload 8356, resolve_scope_*,
   resolve_module_qualified_function 865, check_generic_free_call 6458, check_call_arity 6871,
   has_implicit_converter 7089, flag_autoref_from_params 7732, check_*_arg_types, the Pass-A/B/C
   free-call inference 6331-6814, emit_free_cannot_infer 6814).
5. `sema_visitor.cryo` (main) — fields/ctor/build/wire/run_sema (leak+wire, from 13828) +
   resolve_expr match (4306) + all ~50 `override visit(...)` as thin delegates + control-flow/
   return analysis (check_function_returns 1636, block/stmt_always_returns, expr_is_divergent) +
   return-escape (check_return_stack_address 3495, check_return_payload_escape 3657,
   ret_carries_pointer_to 3601, callee_names_ret_variant 3616) + switch checks (switch_type_class
   4201) + match/try/if/binary/unary/cast/struct-lit/array-lit/tuple-lit/new resolvers + decl
   visitors (FunctionDecl 1607, Struct/Class/Trait/Impl, DeclStmt 2762, Destructure 3111) +
   enter_function (1485) + the two reorder idempotency guards.

**Still to gather before the core trio:** TypeChecker public API (types/checker.cryo), TypeArena/Type
full API, AST expr-node field shapes. VERIFY `AssocProjectionType` import home (assumed in the
already-imported Types::Generic/Compound — confirm at flip-time).

**STRUCTURAL FINDING (verified by reading resolve_member_access @12308):** member_resolver is
NOT an independent leaf. `resolve_member_access` calls `symbolic_is_generic_owner_receiver` /
`symbolic_resolve_owner_field` / `symbolic_type_unresolved` (→ symbolic_checker peer),
`subst_method_return_from_receiver` / `resolve_field_via_template` / `lookup_method_with_inheritance`
(→ method_binding peer), plus `checker.check_field_access`/`check_index_access` and recursion via
dispatch. So member/pattern/lambda all sit ABOVE the core trio and hold peer pointers into it.
⇒ BUILD ORDER for session 2: pin the core-trio method signatures FIRST (symbolic_checker +
method_binding public surface), THEN write member/pattern/lambda + call_resolver against them, so no
collaborator is written calling an unverified signature. The 9 leaf-layer files are the clean,
genuinely-independent boundary — everything past here is the interdependent cluster, best built as
one coherent unit with all cross-signatures pinned. (Methods already located while reading 12164+:
check_type_name_visibility 12164, check_annotation_visibility 12183, enforce_method_visibility 12224,
is_subclass_of 12291, resolve_member_access 12308, resolve_array_access 12470,
is_aggregate_scalar_cast 12544, resolve_cast 12579. enforce_field_visibility 12068 already read.)

**At flip-on (after ALL files exist):** add `public module Sema;` to compiler/src/compiler/_module.cryo,
uncomment `public module SemaVisitor;` (last) in sema/_module.cryo, remove `public module Sema;`
from passes/_module.cryo, repoint passes/pass_registry.cryo (import line ~38 + dispatch ~447:
run_function_body_type_check -> run_sema). THEN first `make cryo` + debug holistically.
