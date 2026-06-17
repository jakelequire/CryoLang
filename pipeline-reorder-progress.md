# Pipeline Reorder — Progress Log

**Mission:** Move Monomorphization to run *after* Semantic Analysis. Sema becomes
the single source of truth for resolved types; mono's private inference layer is
deleted. See the handoff for the full plan (Phases 0–7).

## Validation commands (this Windows host)
- Build: run from **PowerShell** so `make` uses `cmd` recipe shell —
  `$env:CRYO_CC='gcc'; make cryo` then `make test`.
  (Git Bash's `make` picks the Windows/cmd recipe branch but runs it under `sh`
  → `syntax error: unexpected end of file`. Use PowerShell for `make`.)
- Self-host: WSL — `wsl bash -lc "cd /mnt/c/Programming/apps/CryoLang &&
  export CRYO_CC=gcc && python3 scripts/selfhost-check.py --no-windows"`.
  (Windows host routes selfhost through WSL automatically; calling it in WSL
  directly skips the re-entry.)
- Run build/test/selfhost **serially** — shared .o cache corrupts on concurrent runs.

## Phase 0 — Baseline (COMPLETE, 2026-06-16)
- `make cryo`: green. `make test`: unit ok, compile-fail **99 passed**.
- `selfhost-check --no-windows`: **FIXED POINT OK**.
  - Baseline IR md5: `4ef7c5bfa5fbe8f2bee0cb9792750746`  (size 48,963,800 bytes)
- Guard corpus: `tests/` + `stdlib` + `examples/` already exercise collections,
  iterators/adapters, nested + cross-module generics, trait bounds. Added focused
  guards under `/tmp/reorder-guards/` (not committed) — see Phase 1 notes.

## Phase 1 — Extract TraitChecker, sever sema→mono dependency (COMPLETE, 2026-06-16)
**Validated green + byte-identical fixed point.**
- New self-host IR md5: `edd51eee51bfd94536469f555892c57c` (size 48,973,596).
  (Differs from the Phase-0 baseline md5 by design — the compiler source gained a
  file/struct; the new compiler is its own byte-identical fixed point, which is
  the invariant that matters, not equality with the old bytes.)
- `make cryo` green; `make test` unit ok + compile-fail **99**; `selfhost-check
  --no-windows` **FIXED POINT OK**.

What landed:
- New file `compiler/src/compiler/types/trait_checker.cryo` —
  `type struct TraitChecker { arena, generic_registry, intern_table, diagnostics }`
  holding the trait-resolution cluster (moved verbatim): `type_implements_trait`,
  `type_satisfies_opaque_bound`, `diagnose_method_bounds_failure`,
  `bounds_satisfied`, `bounds_satisfied_at_depth`, `method_bounds_satisfied`,
  `lookup_subst_for_param`, `bare_name_of`. No mutable analysis state.
- `monomorphizer.cryo`: added `trait_checker: TraitChecker` field (built in `new`);
  the 5 methods still called by mono-internal code (`type_implements_trait`,
  `bounds_satisfied`, `method_bounds_satisfied`, `lookup_subst_for_param`,
  `bare_name_of`) are now thin forwarders; the 3 sema-only ones
  (`type_satisfies_opaque_bound`, `diagnose_method_bounds_failure`,
  `bounds_satisfied_at_depth`) were deleted from mono (live only in TraitChecker).
- `sema.cryo`: added `trait_checker(&this) -> TraitChecker` helper (builds from
  `this.arena / this.ctx.generic_registry / this.intern / this.ctx.diagnostics`);
  all 5 `ctx.monomorphizer` sites now call through it. The mono-null guards became
  `generic_registry`-null guards (the only state the queries need). **`sema.cryo`
  has zero live `ctx.monomorphizer` references** (only comments remain).

Why this is sound regardless of pass order: the trait queries read template-level
trait-impl blocks (populated at TemplateRegistration, well before mono) + arena
types; they never touch mono's worklist/instantiation-cache. Pure relocation, no
behavior change — proven by byte-identical self-host.

## Phase 2 — Teach sema to type-check generic bodies symbolically (IN PROGRESS)
The heart. Building an **additive measurement harness** first: a symbolic walk of
generic bodies, gated behind env `CRYO_SYMBOLIC_CHECK`, under diagnostic
suppression, that logs how many errors it WOULD emit (`[symbolic-check]
would-emit N ...`). Default-off ⇒ production path unchanged. Goal: drive
would-emit to zero on valid generic code, THEN flip ordering (Phase 3+).

Harness pieces landed:
- `diag/sink.cryo`: `begin_suppress`/`end_suppress`/`suppressed_error_total`/
  `reset_suppressed`; emit() counts-and-discards while `suppress_depth>0`.
- `sema.cryo`: file-scope globals `g_symbolic_check_gate` (0=unread/1=off/2=on,
  zero-init-safe) + `g_in_symbolic_check`; `sema_symbolic_check_enabled()`;
  `symbolic_check_body(func,this,owner)` walks under suppression; wired into the
  `visit(FunctionDeclNode)` and `visit_methods` generic-skip sites.

**THREE compiler-behavior bugs hit and root-caused while building this** (each a
real landmine, not harness-specific):
1. **Class-field-offset miscompile**: adding `boolean` fields to `TypeCheckVisitor`
   (a `type class` with a vtable) corrupted an aliased field — writing `true`
   segfaulted even on non-generic input. Fix: keep the state in file-scope globals
   (like `g_try_counter`), never as fields on that class. (Matches the documented
   `source_module` vtable-offset note in sema.cryo.)
2. **Non-zero global initializers are NOT honored** (globals zero-init). A
   `mut g: i32 = -1` stayed 0. The codebase has zero other non-zero int-global
   inits. Fix: use 0 as the "unread" sentinel.
3. **`fmt::eprintln(String)` vs `format()→string` type confusion**: `format()`
   returns the primitive `string`; `fmt::eprintln` takes the `String` object; the
   mismatch compiles but reinterprets a `char*` as a `String` struct → segfault.
   Fix: use `fmt::eprintf(fmt: string, args...)` (printf-style, takes `string`).

Status: harness WORKS. Single-file `g2` (pulls in prelude generics) gate-on:
exit 0, walks all generic bodies, measures would-emit (map=2, and_then=1,
map_err=2, or_else=1, transmute=1, next=1, from_iter=1; many clean: min/max/swap/
sort_range/clamp=0). **gate-OFF: suite green (unit ok, compile-fail 99); selfhost
fixed-point check running.**

Known limitation (defer to Phase 4): gate-ON on the **multi-module** stdlib build
leaks `E0900 unresolved generic instantiation` — the symbolic walk creates concrete
InstantiatedTypes in the shared arena that a later module's `GenericValidation`
flags (per-module 6b interleaving in instance.cryo). Single-file builds don't hit
this. So Phase-2 measurement uses single-file vehicles; the arena side-effect is a
Phase-4 (orchestrator) concern. gate-OFF is unaffected.

### Phase 2 deferrals — drove stdlib would-emit 657 → 0
Categorized the suppressed false positives and taught each concrete-only check to
DEFER when its operand is abstract/unresolved in symbolic mode. Two predicate
helpers on the visitor (both no-ops unless `g_in_symbolic_check`):
- `symbolic_defer_type(ty)` — true if invalid or contains a generic param.
- `symbolic_type_unresolved(ty)` — true if invalid, `void` (sema's unresolved
  fallback), or contains a generic param. Used at receiver/operand positions.
Plus `symbolic_name_is_generic_param(sym)` (tracks the body's `generic_params`
intern ids in `g_symbolic_generic_param_ids`).

Defer points added (all gated on symbolic mode, so gate-off is a pure no-op):
| category | site | fix |
|---|---|---|
| E0200 return-type mismatch (combinators: map/and_then/…) | `visit(ReturnStmtNode)` | skip when expected/actual abstract |
| E0233 `T::method` static call | `resolve_scope_call` | defer when scope name is a generic param |
| E0204 field/method on `void` | `resolve_member_access` (entry + walk_ty fail path) | defer unresolved receiver |
| E0154 ambiguous trait method | `check_method_call_arg_types` | defer in symbolic mode (mono sets `resolved_trait`, never on the template) |
| E0230 bad unary/`*` operand | `resolve_unary` | defer unresolved operand |
| E0218 immutable assign (`entry.field=`) | `check_assignment_lvalue` member/array | defer unresolved receiver type |
| E0200 via `<` mis-resolving | `resolve_binary` | a binary expr with a deferred operand itself defers (don't fall back to the other operand's type) |
| (also) method-call no-method | `resolve_method_call` | defer unresolved receiver |

**Result: full stdlib (1212 generic bodies) → 0 would-emit; single-file g2 → 0.**
The symbolic checker now type-checks every stdlib generic body with `T`/`U`
abstract and reports nothing spurious. Temp debug logs (`enter`, `[symbolic-emit]`)
removed; only the gated `would-emit N` summary remains (silent when clean).
**VALIDATED CHECKPOINT (committable):** gate-OFF suite green (unit ok,
compile-fail 99) + selfhost **FIXED POINT OK**, md5
`0c6b7d117d765afabf09068266d1a3ec`.

### Phase 2 broadening — generic STRUCT/CLASS/IMPL owner methods (1212 → 1662 bodies)
Wired the three generic-owner decl visitors (`visit(StructDeclNode/ClassDeclNode/
ImplBlockNode)`) to walk their methods symbolically via a new
`symbolic_check_owner_methods` (registers the owner's params in
`g_symbolic_owner_param_ids`, merged with each method's params; sets
`g_symbolic_owner_is_generic`). All gated — gate-off still `return`s early
(unchanged). Coverage went 1212 → **1662** generic bodies.

This surfaced **656** new false positives, driven to **10** by more deferrals:
- 629× E0204 `this.field` on the abstract owner template (`Box<T>`'s `T`-typed
  fields can't be resolved by `check_field_access` here). Deferred via
  `symbolic_is_generic_owner_receiver` (receiver base == current owner) in
  `resolve_member_access`/`resolve_method_call`. The proper fix — resolve
  `this.field` to its abstract field type — is the deep Phase-2 follow-up
  (the "resolve generic bodies for real" core).
- E0645 `static match (T)` unpruned multi-arm (expected in templates) → defer.
- E0234/E0235 `?` operator on abstract operand / abstract enclosing return type → defer.
- E0200 index into `void` → defer in `resolve_array_access`.

**Residual: 10 would-emit / 1662 bodies (0.6%)** — all from a deeper resolver
behavior: an abstract local annotation (`const inner_ptr: RcInner<T,A>*`)
mis-resolves to an *unrelated* concrete spec (consistently
`ChannelInner<WorkerResult>`), so `inner_ptr.count` fails on the wrong type.
Not a simple defer (the receiver is a valid-but-wrong concrete type); needs
root-causing in the type resolver's handling of abstract generic annotations.
(rc.cryo:93-95, arc.cryo:100-102, rwlock.cryo:194/200/201, http2 connection:410.)

So: **zero FPs proven for the 1212-body subset (free fns + methods on non-generic
owners); broadened 1662-body coverage has 10 documented residuals.**

**VALIDATED CHECKPOINT (with broadening):** gate-OFF suite green (unit ok,
compile-fail 99) + `selfhost-check --no-windows` exit 0 = **FIXED POINT HOLDS**
(stage-3 IR == stage-4 IR). All symbolic-check code is gated; gate-off is a pure
no-op, so the production compiler is unaffected. Safe to commit.

### Phase 2 — 10 residual FPs ROOT-CAUSED & FIXED (2026-06-16)
Drove the broadened-coverage residuals **10 → 0** with two principled,
symbolic-mode-gated fixes (gate-off remains a pure no-op). Bonus: the
multi-module **E0900 leak is also gone** — a full gate-ON stdlib build now
exits 0 (was exit 1) because the deferral stops the walk from creating stray
concrete instantiations in the shared arena.

**FP class A (9 of 10: rc.cryo:93-95, arc.cryo:100-102, rwlock.cryo:194/200/201).**
Root cause: a generic body's local-inference chain
`const typed = raw.cast<RcInner<T,A>>(); const inner_ptr = typed.as_ptr();`
runs through `resolve_method_return_with_explicit_args`. The explicit method
generic-arg `RcInner<T,A>` references the ENCLOSING body's still-abstract `T,A`,
but that fn resolves it in a context where the OWNER's param (NonNull's own `T`)
is bound — a name collision — and on the inevitable miss returns the concrete
`fallback` (a stale mono'd `NonNull<ChannelInner<…>>`). That wrong concrete type
propagated down the local chain to `inner_ptr`, so `inner_ptr.count` was checked
against `ChannelInner`. Fix (`sema.cryo` `resolve_method_return_with_explicit_args`):
in symbolic mode, resolve each explicit generic-arg in a FRESH context (no
bindings, so an enclosing param → invalid while a genuine concrete arg like
`cast<i32>()` still resolves); if any arg is invalid or contains a generic param,
return invalid → the whole call defers. No owner-param collision, no concrete
poison.

**FP class B (1 of 10: net/http2/connection.cryo:410, E0200).** Root cause:
`mut on_stream: RequestOnStream = ros;` where `ros` is bound from
`match (this.read_request()) { Ok(maybe) => match (maybe) { Some(ros) => … }}`.
`this.read_request()` defers (generic-owner receiver) → scrutinee invalid →
`bind_enum_pattern`'s fallback resolves the arm binding to the enum TEMPLATE's
bare `GenericParam` (a valid-but-abstract type), so `ros : GenericParam`. The
DeclStmt assignment check then ran `can_assign(GenericParam, RequestOnStream)` →
spurious E0200, because (unlike the ReturnStmt check) it lacked a symbolic-defer
guard. Fix (`sema.cryo` `visit(DeclStmtNode)`): guard the assignment-compat
check with `!symbolic_defer_type(init_type) && !symbolic_defer_type(resolved_type)`,
matching the existing ReturnStmt defer.

**VALIDATED:** `make test` unit ok + compile-fail **99**; `selfhost-check
--no-windows` **FIXED POINT OK** (md5 `aa7769d595ff68a6da6216a1140f4739`,
49,060,992 bytes); full gate-ON stdlib walk = **1673 bodies, 0 would-emit, exit
0**. Temp `[symbolic-emit]`/`[symbolic-decl]` debug removed.

### Phase 2 — `this.field` resolved to abstract field types (2026-06-16) — DONE
The deep core: the symbolic walk now genuinely type-checks `this.<field>` in
generic-owner methods instead of deferring, so it resolves real field types AND
catches bogus field accesses the current pipeline misses.

Root cause of the old defer: generic struct/class *templates* have empty arena
`FieldInfo[]` because `run_struct_field_sync` (type_resolution.cryo ~2739/2813)
skips `is_generic()` decls — so `check_field_access(Box<T>, "value")` finds no
field. BUT the owner template's AST `FieldDeclNode.resolved_type` IS populated
with the abstract type (fields are resolved via `make_generic_context`, each
param bound to its `GenericParam`, at ~2425). (Generic *enums* already populate
their base template variants — an asymmetry; the struct/class arena sync could be
unified with that later, but reading off the AST is the low-risk path and needs
no codegen-facing change.)

Fix (`sema.cryo`):
- New helper `symbolic_resolve_owner_field(recv, field)` — when `recv` is the
  abstract generic owner, looks up the owner template AST via
  `generic_registry.get_template_by_type_id(base.id)` and returns the matching
  `FieldDeclNode.resolved_type` (abstract `T` or concrete `u64`); invalid when not
  a field of it.
- `resolve_member_access`: the `symbolic_is_generic_owner_receiver` site no longer
  blanket-defers; it resolves the field via the helper. A non-field name falls
  through to the normal method-lookup / E0204 path, so `this.nonexistent` is a
  true error. Concrete fields resolve concretely (checked); `T`-typed fields
  resolve to a GenericParam that downstream checks defer on.
- `resolve_method_call`'s owner-receiver defer is LEFT as-is: `this.method()` on a
  generic owner still defers (no FP, just deferred coverage). Resolving generic
  method *return types* abstractly is the next fidelity increment.

**Teeth verified** with a never-instantiated generic struct (so the normal
pipeline never monomorphizes its methods): gate-OFF compiles clean; gate-ON flags
*exactly* the method with `this.nonexistent_field` (would-emit 1) while
`this.count : u64` and `this.value : T` methods stay clean. This is the Phase-2
value proposition — checking generic bodies the pipeline skips.

**VALIDATED:** `make test` unit ok + compile-fail **99**; full gate-ON stdlib
walk = **1673 bodies, 0 would-emit, exit 0**; `selfhost-check --no-windows`
**FIXED POINT OK** (md5 `2962b20dd1aa55b6cc4b439f7109137d`, 49,079,590 bytes).
(Clean re-run after a serial-discipline slip — a concurrent gate-ON stdlib build
`rm -rf .bin` corrupted an earlier run; lesson re-learned: NOTHING touches the
build while selfhost runs.)

KNOWN LIMITATION (documented, not a FP): `symbolic_resolve_owner_field` reads only
the owner's OWN fields, not inherited fields of a generic *class* base chain. No
stdlib generic class triggers this (0 would-emit), but a future
`this.<inherited_field>` on a generic subclass would fall through to E0204. Extend
by walking `ClassDeclNode` base classes if it ever surfaces.

### Phase 2 — `this.method()` abstract return resolution (2026-06-16) — DONE
Mirror of the `this.field` fix: new `symbolic_resolve_owner_method_return` reads
the owner template AST `MethodNode.func.resolved_return_type` (arena method list
is empty for generic templates, same `run_struct_field_sync` skip). Integrated at
the `resolve_method_call` owner-receiver site — an UNAMBIGUOUS non-static method
of that name resolves to its abstract return; 0 matches / overload set / trait-impl
/ inherited methods return invalid and DEFER exactly as the old blanket defer did
(no new FP). **VALIDATED:** `make test` 99/99; gate-ON stdlib 1673 bodies, 0
would-emit, exit 0; teeth confirmed (never-instantiated `Box<T>` — gate-on flags a
`this.count_val()`→`u64` assigned to `string`, abstract-returning `this.get_value()`
stays clean); selfhost **FIXED POINT OK** md5 `a7dd0530b91e03fc79cbaa1c238cdacb`.
(Build was briefly blocked by an Anthropic classifier outage, then completed.)

### BRIDGE EXPERIMENT (2026-06-16) — attempted, REVERTED; pinpoints the real wall
Attempted the "bridge": un-suppress the generic-body walk so it emits REAL
diagnostics in production (gate default-ON, kill-switch via env), keeping the
current pass order. Goal: turn the measurement harness into a real check of
never-instantiated generics, as a stepping stone to the flip. **Reverted** — it
surfaced a cascade of issues that together identify the true blocker. Findings
(each a concrete work item):

1. **Cross-unit name-collision FPs (18 errors).** Un-suppressed, stdlib generic
   bodies compiled alongside the test corpus mis-resolve bare param annotations:
   `mut alloc: A` in `rwlock.cryo` resolved `A` to an UNRELATED test type
   (`…GenericMethodNestedDispatch::A`) via global name lookup, then
   `alloc.deallocate(...)` failed. The stdlib-only measurement never showed this
   (no colliding `A` present) — so "0 would-emit on stdlib" is necessary but NOT
   sufficient; the walk has latent FPs under cross-unit name collision.
2. **Param-binding fixes #1 but RE-INTRODUCES E0900 (18→5 errors).** Binding the
   enclosing params to abstract `GenericParam`s in the walk's resolution contexts
   (new `symbolic_bind_params`, mirroring TypeResolution's `bind_generic_params`)
   makes `A` resolve to the abstract param — collision gone. BUT resolving an
   abstract generic annotation (`Pair<T,V>`) routes through
   `resolve_generic → instantiate_for_module`, which CREATES an `InstantiatedType`
   in the GenericRegistry CACHE; `collect_unmonomorphized` (iterates the cache, not
   demands) then flags it → **E0900 unresolved generic instantiation** — exactly
   the Phase-4 arena-pollution wall. This DIRECTLY regresses task #1's hard-won
   side-effect-free property. So binding trades the name-collision FP for arena
   pollution: neither un-suppress-alone nor un-suppress+binding is clean.
3. Lesser classes also seen: destructure of the abstract owner
   (`const { tid, shared }: JoinHandle<T> = this;` → E0361, fixable with a defer);
   a match-arm binding off an abstract source resolving to a wrong concrete type
   (FmtError-vs-boolean).

**THE REAL BLOCKER (shared by the bridge AND the flip):** the symbolic walk needs
to resolve generic-param-bearing annotations to ABSTRACT types **without creating
registry-cached `InstantiatedType`s** (which leak as un-monomorphized → E0900).
Task #1 sidestepped this by DEFERRING abstract calls (never resolving them, so
nothing is created) — sound for measurement, but the bridge/flip need real
resolution. The fix is a resolver "symbolic/abstract" mode: for an annotation
whose args contain a generic param, build the `InstantiatedType` in the ARENA only
(via `arena.create_instantiation`) and SKIP `generic_registry` caching + demand
registration, so `collect_unmonomorphized` never sees it. This is the key piece of
infrastructure to build next; it unblocks both the bridge and Phase 3.

Reverted cleanly to the validated tasks #1–3 state (gate-OFF byte-identical;
gate-ON 1673 bodies / 0 would-emit / exit 0). All bridge edits removed; selfhost
after revert reproduced the EXACT task-#3 fixed point md5
`a7dd0530b91e03fc79cbaa1c238cdacb` — confirming a perfectly clean revert.

### FLIP-READINESS ASSESSMENT (2026-06-16) — what Phase 3 actually requires
Mapped the full reorder surface so the flip can be executed deliberately:

**Pass order today (`pass_id.cryo` order() + `pass_registry.cryo` builders):**
`… TypeResolution(12) → StructFieldTypeSync(13) → DirectiveProcessing(14) →
Monomorphization(15) → GenericExpressionResolution(16) → GenericValidation(17) →
FunctionBodyTypeCheck(18) → MoveCheck(19) → DropInsertion(20) → TypeLowering …`
Provision chain forces it: `FunctionBodyTypeCheck` REQUIRES `GenericsValidated`,
which requires `GenericExpressionsResolved` ← `MonomorphizationComplete`.

**Three single-module builders** (`build_standard_pipeline`,
`build_frontend_pipeline`, `build_raw_pipeline`) list the order explicitly. The
**multi-module orchestrator** (`instance.cryo`) is the harder "Phase-4 wall":
mono runs PER-MODULE in Phase 6a-ii (`~1724-1728`: Monomorphization +
GenericExpressionResolution interleaved across modules), then Phase 6b
(`~1924-1928`: GenericValidation, FunctionBodyTypeCheck, MoveCheck, DropInsertion,
TypeLowering). Flipping multi-module means hoisting FunctionBodyTypeCheck ahead of
the per-module mono interleave — non-trivial.

**THE CORE OBSTACLE (not just reordering):** today `FunctionBodyTypeCheck` checks
the CONCRETE, post-mono output (it SKIPS `is_generic()` templates via guards; only
the gated symbolic walk touches templates, under suppression). If you move it
before mono, concrete instantiations are NEVER type-checked — only templates +
non-generic bodies are. For that to be SOUND, template-level checking + trait-bound
checking at instantiation sites must catch everything the post-mono check catches.
Many of the **99 compile-fail tests very likely assert errors detected post-mono**
(type errors that only manifest for specific concrete args), so a naive reorder
will regress them. The flip is therefore gated on:
  1. Un-suppress the symbolic generic-body walk and make it the REAL generic-body
     check (remove the `CRYO_SYMBOLIC_CHECK` gate + `begin/end_suppress` in
     `symbolic_check_body`; keep `g_in_symbolic_check` so the defers stay). For
     VALID code this is byte-identical (0 would-emit) — a safe, shippable bridge
     that turns the measurement harness into a production check of
     never-instantiated generics.
  2. Make `FunctionBodyTypeCheck`'s normal visitors STOP skipping `is_generic()`
     bodies and route them through the (now-real) symbolic check.
  3. Prove the pre-mono check catches the post-mono error set: run the reorder as
     an EXPERIMENT, see which of the 99 compile-fail tests regress, and close each
     gap (trait-bound checking at instantiation sites is the main lever). This is
     the multi-day core.
  4. Rewire the provision DAG: `FunctionBodyTypeCheck` requires `StructFieldsSynced`
     (not `GenericsValidated`); `Monomorphization` may require `BodiesTypeChecked`;
     `MoveCheck`/`DropInsertion` STAY after mono (they need concrete code).
  5. Reorder all 3 single-module builders + the multi-module orchestrator.
  6. Phase 5: delete mono's private inference engine — it's large:
     `try_infer_function_call`/`try_infer_method_call`,
     `resolve_arg_type_for_inference`, `collect_locals_in_block/stmt`,
     `lookup_local_type`, the per-walker scratch stacks (`monomorphizer.cryo`
     ~251-306, ~858-1171, ~2552-3700). Mono must instead read sema's
     AST-annotated resolved types.

RECOMMENDED NEXT SEQUENCE (each its own validated step):
- Build+validate `this.method()` (above).
- BRIDGE: un-suppress the generic-body walk → validate byte-identical selfhost +
  99 compile-fail. Ships generic-template checking as a real feature.
- EXPERIMENT: apply the reorder, run `make test`, record which compile-fail tests
  regress (this quantifies the remaining gap precisely), then REVERT.
- Close the gaps (instantiation-site bound checking), then land the flip for real.

- `is_self_returning_default` combinator templates still skipped (intentional).
- Multi-module gate-ON E0900 leak: ELIMINATED as a side effect of the FP-class-A
  fix; clean gate-ON stdlib builds now exit 0. Re-confirm before flipping order.
Goal: the 5 `ctx.monomorphizer` sites in `sema.cryo` (~1730, 1770, 1889, 7429,
7480, 7711) all call read-only trait queries. Extract the cluster into a
standalone `TraitChecker` (depends only on arena/generic_registry/intern_table/
diagnostics). Mono composes a `TraitChecker` and forwards; sema uses it directly.
Nothing reorders yet — suite stays green, selfhost stays a fixed point.

Cluster to move (all closed over the 4 read-only pointers; verified):
`type_implements_trait`, `type_satisfies_opaque_bound`,
`diagnose_method_bounds_failure`, `bounds_satisfied`, `bounds_satisfied_at_depth`,
`method_bounds_satisfied`, `lookup_subst_for_param`, `bare_name_of`.
Free-fn deps: `find_inst_wrapping` (types/inference.cryo), `OwnershipQuery::*`.

Status: writing `compiler/src/compiler/types/trait_checker.cryo`.
