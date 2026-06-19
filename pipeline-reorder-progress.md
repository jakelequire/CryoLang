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

### Phase 2 — KEYSTONE: demand-free abstract resolution + param binding (2026-06-17) — DONE
THE REAL BLOCKER above is **RESOLVED**. The symbolic walk now resolves
generic-param-bearing annotations to ABSTRACT types *with* binding on, WITHOUT
leaking E0900 — the exact infrastructure the bridge and the flip were gated on.
This is strictly stronger than the `a7dd0530` checkpoint: that hit 0 would-emit
only by DEFERRING abstract calls (never resolving them); this RESOLVES them.

What landed (all paths dormant unless `CRYO_SYMBOLIC_CHECK` is set, so gate-OFF is
byte-identical):
- **`resolver.cryo`** — new `ResolutionContext.symbolic_no_demand: boolean`
  (default false in `new`, preserved in `clone`). In `resolve_generic`, when set,
  return `arena.create_instantiation(base, &args)` (arena-only, registry-free)
  instead of `generic_registry.instantiate_for_module(...)`. The arena path does
  NOT register a monomorphization demand, so `collect_unmonomorphized` never sees
  it → no E0900. (Confirmed by reading `create_instantiation` ~509: arena-cache
  only, no registry touch; `collect_unmonomorphized` scans only registry cache_keys.)
- **`sema.cryo`** — `symbolic_bind_params(ctx)` + `symbolic_param_ref(p, index)`:
  bind each in-scope generic param (owner params first, then the method/free-fn's
  own; `index` = position in its OWN list, matching `create_generic_param_types`)
  to its abstract `GenericParam` (or `BoundedParamType` when it has constraints)
  and flip `ctx.symbolic_no_demand = true`. No-op outside a symbolic walk. The
  param AST nodes are carried in two new globals `g_symbolic_owner_param_nodes` /
  `g_symbolic_method_param_nodes` (set+restored in `symbolic_check_owner_methods` /
  `symbolic_check_body`, alongside the existing id lists). Wired into the 4
  body-level resolution sites: `visit(DeclStmtNode)` lazy-resolve,
  `visit(DestructureDeclNode)`, `resolve_lambda`, `resolve_generic_scope_name`.
  → This fixes the cross-unit name-collision FP (bare `A` now resolves to the
  abstract param, not an unrelated same-named global type).
- **`sema.cryo`** — destructure defer: in `visit(DestructureDeclNode)`, before the
  field-presence check, `if (this.symbolic_defer_type(node.resolved_type))` →
  register bindings void + return. Closes the re-surfaced E0361 class. With binding
  on, `const { ... }: Box<T,A> = self;` resolves to an InstantiatedType whose
  template fields are unsynced (`run_struct_field_sync` skips `is_generic()`), so
  the field check would spuriously emit E0361 "non-struct"; deferring lets the
  concrete monomorphization re-check it.

**The FP cascade the bridge predicted, closed empirically:** gate-ON stdlib with
binding+demand-free initially showed exactly **7 would-emit, all E0361** on
abstract-owner destructures (`Box<T,A>`, `RawBuffer<T,A>`, `String<A>`,
`Sender<T>`, `JoinHandle<T>` — bodies `into_raw`/`leak`/`join`/`detach`/`close`).
The single destructure defer drove all 7 → 0. The lesser match-arm-binding class
(FmtError-vs-boolean) the bridge also saw did NOT reappear (binding fixed its root
cause — the abstract scrutinee now resolves correctly rather than to a wrong
concrete type).

**VALIDATED (Linux host, this session):**
- `make cryo` green.
- Gate-ON stdlib (`CRYO_SYMBOLIC_CHECK=1`): **1681 bodies walked / 0 would-emit /
  exit 0** — NO E0900 with binding on (the keystone's whole point).
- `make test`: unit ok, compile-fail **99/0**.
- `selfhost-check --no-windows`: **✓ FIXED POINT OK** (stage-3 == stage-4),
  Linux IR md5 `c5ba1405d09e94f617f08a42fc7498d4` (size 49,120,934). New md5 vs
  prior because the compiler source changed; it is its own clean fixed point.
- Temp `[symbolic-emit]` dump in `sink.cryo` added for FP triage, then REMOVED.
- UNCOMMITTED. Jake owns commit + (no repin needed — gate-OFF byte-identical
  through the pin).

NEXT (unchanged sequence, now unblocked): the BRIDGE — un-suppress the walk
(remove `begin/end_suppress` in `symbolic_check_body`, make it default-ON with an
env kill-switch, keep `g_in_symbolic_check`). Acceptance = make test 99 +
self-host byte-identical. Should be close: only 12/99 compile-fail tests use
generics and most assert concrete-instantiation errors the walk defers on.

### GLOBALS→FIELDS migration ATTEMPTED, REVERTED — landmine CONFIRMED LIVE (2026-06-17)
Jake asked to remove the `g_symbolic_*` / `g_try_counter` file-scope globals,
moving the per-walk state to `TypeCheckVisitor` fields (his hypothesis: the
"adding fields to the vtable class segfaults" landmine was stale). Did the full,
correct migration: 10 globals → fields, 2 free fns → methods, env-gate read once
in the constructor, owning-array save/restore via the `swap`-with-empty discipline
`resolve_lambda` uses, all 55 refs rewired.

**Result: the landmine is REAL and STILL LIVE.** The field-migrated source
*compiles its stdlib* but then **aborts the pinned `bin/cryo` (SIGABRT, core
dumped) during the compiler self-build** — a silent Cryo panic after parsing all
modules, deep in a whole-program analysis pass (heavy alloc → abort; stripped
backtrace shows recursion). The prior keystone+destructure source built fine
through the same pin, so the field additions to `TypeCheckVisitor` (a
`type class : BaseASTVisitor`, i.e. a vtable'd class hierarchy) are the sole
trigger. This matches the in-file note at `sema.cryo` ~1351: *"Direct field writes
to source_module don't survive vtable-offset miscompiles on class hierarchies, so
we route through the Monomorphizer as a side channel."* The bug is documented and
unfixed; my migration just exercised it harder (a hard abort, not the silent
miscompile the note describes).

**Reverted cleanly** — restored the globals + 2 free functions + the global-based
save/restore; `make cryo` green, `make test` 99/0, self-host re-validated (see
below). Working tree is back to the keystone state.

**The RIGHT fix (Jake's stated preference: right long-term call over a quick green,
grind through the struggle, no workaround hacks):** repair the underlying
vtable/class-hierarchy field-offset codegen miscompile, then repin. Once fields on
`TypeCheckVisitor` work, BOTH these symbolic globals AND the `source_module`
Monomorphizer side-channel hack (sema ~1351) can be deleted. This is a bootstrap
sequence: the codegen fix lives in the codegen/type-lowering pass (compilable by
the current pin) → validate → `make pin-cryo` → THEN migrate globals→fields. It is
a deep, separate task; flagged for Jake to greenlight before sinking days in.
Diagnosis pointers: the miscompile is offset/layout-sensitive on classes deriving a
vtable base (`: BaseASTVisitor`); start at declaration/field-layout emission and
the GEP offsets for derived-class fields (compare a field's computed offset vs the
vtable-prefixed layout). The `source_module` note is a concrete reproducer to
study (a single field whose direct writes are lost).

### MANGLER FIX — `swap<Array<ptr>>` ICE root-caused & fixed (2026-06-17)
Jake asked to remove the `g_symbolic_*`/`g_try_counter` globals (move per-walk state
to `TypeCheckVisitor` fields) and to FIX the underlying codegen bug rather than keep
a workaround. Investigation overturned the "vtable field-offset landmine" framing:

**The landmine was a MISDIAGNOSIS.** Fields on the vtable'd class work fine — bisection
showed scalar/`u32[]`/8-mixed-type fields + `this.field` access all build clean. The
globals→fields migration crashed for ONE reason: it used `swap(&this.field, …)` on the
`GenericParamNode*[]` (array-of-pointers) fields, i.e. `mem::swap<Array<ptr>>`. A
standalone repro with NO fields and NO vtable class — `mut a: Node*[]; mem::swap(&a,&b)`
— aborts the compiler; `swap<i32*>` and `swap<i32[]>` are fine, only `swap<Array<ptr>>`.

**Root cause (symbolized backtrace + instrumentation):** `MangledName::mangler_ice` ←
`encode_type_ref` ← `register_injected_decl` (spec registration), message
`encode_type_ref: invalid TypeRef (id=0) at Array.element`. The AST substituter
`rewrite_to_array` (AST/substituter.cryo) built the array element as bare
`Named("i32*")` with `pre_resolved=invalid` → resolved by name → `Array<invalid>` →
ICE. Its sibling `rewrite_to_pointer` was already fixed for exactly this; the array
path was missed.

**Fix:** `rewrite_to_array` now takes `inner_pre_resolved` and sets it on the element
`Named` (mirrors `rewrite_to_pointer`); caller passes
`arena.array_element_of(resolved_arg_typeref(i).id)` (new arena helper). Defensive
guard added to `TypeResolver`'s Array case (returns invalid on unresolvable element,
matching Pointer/Reference). +regression test `tests/tests/lang/swap_pointer_array.cryo`.

**VALIDATED:** repro compiles+runs (correct swap semantics); `make test` 99/0;
selfhost **FIXED POINT** IR md5 `88481be5e90bbbc00995149e7dfe7242` (new vs c5ba1405 —
source changed). Repinning (`make pin`) so the migration can build on the fixed pin.

**NEXT:** (C) re-do the globals→fields migration on the fixed pin — plain field swap
now works; (D) the `source_module` Monomorphizer side-channel (sema ~1351) was a
workaround for the SAME mangler-bug class and can likely be deleted too.

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

---

## 2026-06-17 — Phase C (globals→fields) DONE + Phase D (source_module side-channel) VERIFIED

Built directly on the mangler fix (`swap<Array<ptr>>` ICE) from the MANGLER FIX
section. Both validated; tree needs a **repin** (compiler source changed).

### Phase C — symbolic-check globals are now TypeCheckVisitor fields
Deleted the file-scope `mut g_*` block in `passes/sema.cryo` and the two free
functions; they are now ordinary fields + methods of `TypeCheckVisitor`:
- `g_try_counter`→`try_counter`; `g_symbolic_check_gate`→`symbolic_check_gate`
  (read ONCE in the constructor, so `sema_symbolic_check_enabled(&this)` is a pure
  `this.symbolic_check_gate == 2` reader); `g_in_symbolic_check`→`in_symbolic_check`;
  the four `g_symbolic_*param*` arrays + `g_symbolic_owner_is_generic` +
  `g_symbolic_bodies_walked`/`g_symbolic_total_would_emit` → fields.
- `symbolic_name_is_generic_param`/`sema_symbolic_check_enabled` are now methods.
- The owning-array fields (`symbolic_generic_param_ids`,
  `symbolic_method_param_nodes`, `symbolic_owner_param_ids`,
  `symbolic_owner_param_nodes`) use the **swap-with-empty save/restore idiom** in
  `symbolic_check_body`/`symbolic_check_owner_methods` (mirrors `resolve_lambda`'s
  `undo_*`). This is exactly the `swap<Array<ptr>>` shape the mangler fix enabled —
  it built clean, proving the fix is the real unblock.
- This confirms the OLD "adding fields to the vtable'd TypeCheckVisitor crashes the
  compiler" claim was a MISDIAGNOSIS; the crash was only the mangler ICE.

Validation: `make cryo` ✓; gate-ON stdlib walks **1681 generic bodies / 0
would-emit / exit 0** (identical to the globals baseline — the per-module
"cumulative" lines now report per-pass-invocation since the counters are fields,
but they sum to 1681); `make test` unit ok + **compile-fail 99/0**; selfhost
**✓ FIXED POINT** IR md5 `ba7ccfb4e9ab278994f1ec243e70f49f` (this md5 is AFTER
Phase C + D1 together).

### Phase D — verify-first on the `source_module` base-pointer field-write workaround
**Finding: the suspected base-pointer field-write miscompile is NOT reproducible.**
A faithful 3-level vtable'd repro (`ASTNode` root → `DeclarationNode` owns the
field → `StructDeclNode` leaf; write the field through an `ASTNode*`-cast-to-
`DeclarationNode*`, read back through the leaf and the mid) PASSES at both O0 and
O2 — field write survives, neighbor fields intact, vtable dispatch intact. So the
workaround's stated rationale ("direct field writes don't survive vtable-offset
miscompiles") is **obsolete** (it was the same misdiagnosis as the
TypeCheckVisitor one; the real bug was the mangler ICE).

- **D1 (done):** Simplified `tag_decl_source_module` (`passes/specialization.cryo`)
  from a 5-arm per-leaf concrete-cast match to a single kind-guarded base-pointer
  write (`node as DeclarationNode*`). Functionally identical, workaround removed.
- **D2 (NOT done — and should NOT be):** Deleting the `lookup_injected_origin`
  side-channel and reading the field in `dispatch_top_level` is **unsafe**, for a
  reason INDEPENDENT of the (obsolete) miscompile: the `source_module` field is
  semantically **overloaded**. `type_resolution.cryo` (~1177) stamps every
  NON-injected impl block with its own defining module (`ctx.source_file`), so the
  field cannot distinguish "injected spec from template X" from "ordinary local
  impl". The side table only ever holds injected specs, so it answers
  `dispatch_top_level`'s question (is-this-an-injected-spec + which template)
  cleanly. The side table is therefore load-bearing for disambiguation, not a
  codegen workaround. Kept it; corrected the misleading "vtable miscompile"
  comments at both sites (`dispatch_top_level` and the inject site) to state the
  real rationale.

### Status
UNCOMMITTED. Needs a **repin** (`make pin` refreshes linux + win on this host).
Validated: make cryo ✓, gate-ON stdlib 1681/0/exit0, make test 99/0, selfhost
FIXED POINT `ba7ccfb4`. Repro files: `/tmp/basewrite.cryo`, `/tmp/basewrite3.cryo`.

---

## 2026-06-17 — BRIDGE attempt #2 (post Phase-C) → EXPERIMENT data, REVERTED

Attempted the BRIDGE again now that Phase C landed: made the symbolic generic-body
walk **default-ON** (kill-switch `CRYO_NO_SYMBOLIC_CHECK`) and **removed
`begin/end_suppress`** in `symbolic_check_body` so it emits REAL diagnostics; the
per-body would-emit eprintf was replaced with real-error telemetry
(`sink.errors_since(snap)`) and the cumulative line gated behind `debug_mode`.
Verified first that `TypeCheckVisitor` emits 0 warnings (63 error sites, 0 warning
sites) → un-suppressing can only surface errors, never spurious warnings.

**Pre-flight (clean):**
- stdlib-alone via the default-ON compiler: **0 errors, exit 0**.
- `CRYO_SYMBOLIC_CHECK=1 make cryo` (compiler + stdlib, fresh): **9515 generic
  bodies walked / 0 would-emit / exit 0**. So the compiler's OWN generics are clean.

**`make test` REGRESSED (5 errors) — the EXPERIMENT result.** The failure is NOT in
the 12 generic compile-fail tests; it is in **stdlib `fmt/display.cryo`**, and ONLY
when stdlib is compiled in the same unit as the test corpus (stdlib-alone and
compiler+stdlib are both clean). Two FP classes, both **cross-unit spec pollution**:
1. **4× E0214 in `Result<T,E>`'s `Display::fmt` / `Debug::fmt`** (display.cryo
   590/601/621/632). The arm `Result::Err(e) => fmt_err(e)` binds `e` to a CONCRETE
   `Result` spec's Err-payload pulled from ANOTHER unit — `std::test::error::TestError`
   (Display) and `boolean` (Debug) — instead of `FmtError` from the concrete
   scrutinee `f.write_str()` (`Result<(), FmtError>`). The symbolic walk's match-arm
   enum-pattern payload binding resolves the `Result::Err` variant payload against a
   cross-unit concrete spec rather than the abstract owner / the scrutinee's resolved
   type. (`mono_type_contains_generic_param` DOES recurse through Reference/Pointer,
   so the `resolve_method_call` defer guard at sema ~8034 is not the gap; the gap is
   the enum-pattern payload resolution path.)
2. **1× E0900 `Pair` (inst_id=15334)** — an abstract `Pair<,>` instantiation leaks as
   un-monomorphized in the test-corpus unit despite `symbolic_no_demand`; a path that
   doesn't route through the demand-free resolver. Cross-unit only.

**This confirms prior bridge findings #1/#3 persist** for constructs the keystone
didn't cover: the keystone's `symbolic_bind_params` fixed bare-param-annotation
collisions (`mut alloc: A`), but NOT (a) match-arm enum-pattern payload binding, nor
(b) this `Pair` E0900 path. "0 would-emit on stdlib/compiler" remains necessary but
NOT sufficient — cross-unit (test-corpus) builds still pollute symbolic resolution.

**REVERTED** to the committed checkpoint (`git checkout HEAD -- sema.cryo`); make cryo
+ make test 99/0 confirm a clean revert. The env-gated default-OFF measurement harness
(committed @ `54277d4e`) is intact.

**GAP TO CLOSE before the bridge is shippable (the multi-day core, now scoped):**
make the symbolic walk's resolution collision-proof across units for:
- **match-arm enum-pattern payload binding** — bind the variant payload from the
  scrutinee's resolved type (or the abstract owner), never via a global/cross-unit
  concrete-spec lookup; defer when the subject is symbolic-unresolved.
- **the `Pair` E0900 path** — route (or defer) whatever instantiates `Pair` in this
  body through `symbolic_no_demand` so it stays arena-only.
Recommended next: build a minimal 2-"unit" repro (an abstract-owner generic method
matching `Result::Err(e)` + a colliding concrete `Result<_, X>` spec in the same
build) to pin the exact resolution site, then fix + re-run the bridge. Flagged for
Jake to greenlight the deep dive.

---

## 2026-06-17 — BRIDGE SHIPPED ✅ (un-suppressed, default-ON) + 3 FP classes root-caused & fixed

The BRIDGE is DONE: the symbolic generic-body walk now runs **default-ON** and
**un-suppressed**, emitting REAL diagnostics for type errors in never-instantiated
generic templates. Kill-switch: `CRYO_NO_SYMBOLIC_CHECK`. This turns the Phase-2
measurement harness into a production feature and is the stepping-stone to the flip.

**Validated (Linux):** `make test` unit ok + **compile-fail 99/0**; `selfhost-check
--no-windows` **✓ FIXED POINT** IR md5 `ab153ba479198730fdc29e2f811476f1`; TEETH
confirmed (a never-instantiated `Thing<T>` with `string + 5` inside → E0200 ON,
silent under the kill-switch). gate-OFF path is byte-identical (all new logic is
behind the arena's walk-scoped flag, set only during `symbolic_check_body`).

The BRIDGE attempt #2 had surfaced 3 cross-unit FP classes (only in the multi-unit
`cryo test` build; stdlib-alone + compiler were clean). Each root-caused and fixed
with a principled, sound (never-false-positive) defer/tag — NOT a suppression hack:

1. **E0214 `FmtError` vs `TestError`/`boolean`** (`Result<T,E>::fmt`, display.cryo).
   Root: the inner `Result::Err(e)` matches `f.write_str()` (`Result<(),FmtError>`)
   but its subject DEFERS (receiver `&Formatter<W>`, W abstract → `resolve_method_call`
   returns invalid). With an invalid subject, `resolve_variant_payload_types` fell to
   **Fallback B** — a global name lookup of the pattern's enum name — which, with the
   pattern node carrying a stale/concrete `Result<i32,X>` name from another
   instantiation context, bound `e` to a wrong cross-unit payload. **Fix:** in a
   symbolic walk, skip Fallback B when the subject type is `symbolic_type_unresolved`
   (defer; the concrete mono re-checks). sema ~1184. When the subject IS resolved,
   Fallback A already wins, so only the can't-know case changes.

2. **E0900 `Pair` unresolved-instantiation** (GenericValidation, specialization.cryo
   ~916). Root (NOT the registry/demand path — that was a red herring): the walk
   resolves a CONCRETE `Pair<X,Y>` that lives only inside a never-instantiated
   template; it lands in the **arena** with `resolved_type` invalid, nothing
   monomorphizes it, and a later module's GenericValidation (shared arena) flags it.
   gate-OFF the walk never runs so the node never exists → false positive. **Fix:**
   a walk-scoped flag on the **TypeArena** (`symbolic_no_demand_active`, the single
   instantiation chokepoint) tags every InstantiatedType minted during the walk
   `is_symbolic` (new field on InstantiatedType); GenericValidation skips `is_symbolic`
   nodes exactly like it already skips generic-arg ones. The GenericRegistry also
   reads the arena flag to keep walk instantiations demand-free. Real code that later
   needs the same (base,args) reuses the arena node and sets resolved_type, after
   which the tag is moot.

3. **E0229 `Cannot apply '+' to Int and Void`** (io/traits.cryo `total + n`).
   Root: with the #1 fix, a deferred-subject match binding resolves to `void`; the
   binary-op check emitted E0229 (`Int + Void`) BEFORE its existing symbolic defer.
   **Fix:** move the `symbolic_type_unresolved(lhs|rhs)` defer ABOVE the
   `check_binary_op`/E0229 emit (sema ~5128). No-op outside the walk.

**Design note (soundness):** every fix makes the walk DEFER (or tag-and-skip) what it
cannot resolve abstractly — a sound over-approximation that never false-positives,
and the concrete monomorphization still re-checks the deferred parts. This is exactly
the bridge's intended semantics. The eventual FLIP (mono-after-sema) will need the
walk to RESOLVE more abstractly (e.g. trait-method return types on abstract receivers)
rather than defer, but that is Phase 3+ and is not regressed here.

Files: `passes/sema.cryo` (bridge + #1 + #3 + arena-flag wiring), `types/arena.cryo`
(flag + `is_symbolic` tagging + setter), `types/generic.cryo` (`InstantiatedType.is_symbolic`),
`types/generic_registry.cryo` (demand-free via arena flag), `passes/specialization.cryo`
(GenericValidation skip). UNCOMMITTED; needs a **repin** (compiler source changed).

NEXT (per RECOMMENDED NEXT SEQUENCE): the EXPERIMENT — apply the actual pass reorder
(FunctionBodyTypeCheck before mono), run `make test`, record which compile-fail tests
regress, then REVERT. That quantifies the remaining gap for the real flip.

---

## 2026-06-17 — THE EXPERIMENT (§2): reorder measured, REVERTED — result is a CONFOUNDED zero

Ran the §2 gap-measurement experiment on `build_standard_pipeline` (single-module,
smallest surface) and reverted it. **Headline: 0 regressions — but the measurement is
confounded and must NOT be read as "the flip is free."**

**What was changed (two edits, both reverted):**
1. `passes/pass_registry.cryo build_standard_pipeline`: moved `FunctionBodyTypeCheck`
   from after `GenericValidation` to **immediately before `Monomorphization`** (right
   after `DirectiveProcessing`). MoveCheck/DropInsertion stayed after mono.
2. `passes/pass_id.cryo`: changed `FunctionBodyTypeCheck`'s requirement from
   `GenericsValidated` → `StructFieldsSynced`. **Required** — the registry runs passes
   in array order and `validate()` does a linear "is each required provision satisfied
   by an earlier pass" scan (no topo re-sort), so without this the new order is
   rejected. `StructFieldsSynced` is provided by `StructFieldTypeSync` (already
   upstream). This is exactly flip step 4 for this one builder.

   NB: flip step 2 ("route is_generic bodies through the symbolic check") was **already
   done by the BRIDGE** — `visit(FunctionDeclNode*)` (sema ~1415) already calls
   `symbolic_check_body` for generic nodes and `symbolic_check_owner_methods` for
   methods. So the experiment is purely the reorder + the requirement swap.

**Result:** `make cryo` built clean (pinned cryo compiled the reordered source fine);
`make test` → **unit 1233/1233 PASS, compile-fail 99/0**. No validation abort, no
unit regression, no compile-fail regression. The gap list is EMPTY.

**Why the zero is CONFOUNDED (the important finding):** the experiment moved
`FunctionBodyTypeCheck` earlier but **left Monomorphization, GenericExpressionResolution,
and GenericValidation fully intact and still running after it.** Those passes still
emit the instantiation-site diagnostics. Concretely, `E0306_trait_bound.cryo`
(`cf_need<i32>(5)`, `i32` fails `where T: CfShow`) is the canonical §3 gap — a
trait-bound check on a concrete instantiation — and it STILL fired, because it is
emitted by **`monomorphizer.cryo:4483 emit_call_bound_failure`** (mono's own
`type_implements_trait` call-site check), NOT by `FunctionBodyTypeCheck`. Moving the
body check earlier cannot lose an error that a still-present downstream pass owns.

So: the reorder is mechanically sound and loses nothing **while mono's diagnostic
engine remains as a backstop.** The real defer-vs-resolve gap (§3) is MASKED by that
backstop and will only surface as regressions when **flip step 6** deletes mono's
private inference/checking. The §2 experiment as literally specified cannot see the
gap — its premise ("reordering exposes the regressions") is wrong given the current
pipeline shape.

**Corrected next measurement (the experiment that actually quantifies the gap):**
reorder AS ABOVE **and** neuter mono's type-error emission (a proxy for step 6 —
e.g. suppress `emit_call_bound_failure` + mono's other diagnostic sites, or gate them
off), THEN run `make test` and count regressions. Those regressions = the concrete
defer→resolve worklist for flip step 3 (trait-bound satisfaction at instantiation
sites, abstract trait-method return resolution, bound-directed dispatch). Until mono's
diagnostics are removed, the body-check reorder is a no-op from the error-coverage
point of view.

Tree restored to checkpoint (HEAD `5f4a2223`); baseline `build/cryo` rebuilt. Only
this doc + untracked HANDOFF.md dirty. No repin needed (no committed source changed).

---

## 2026-06-17 (cont.) — FLIP IMPLEMENTATION: single-module reorder + sema owns call-site checks (VALIDATED, selfhost FIXED POINT)

Continuing the mono-after-sema hard switch. This session corrected a flawed
measurement, then landed the first real implementation increment.

### Correction to the prior "§2 experiment = 0 regressions"
That experiment reordered `build_standard_pipeline` — but `cryo check` (the
compile-fail path) goes through `compile_for_check` → **`build_frontend_pipeline`**,
which I had NOT reordered. The reorder was inert; 99/0 proved nothing. Redone
correctly (all three single-module builders reordered), the real gap surfaced:
**5 compile-fail regressions = exactly {E0306×2, E0307, E0214×2}** — the three
monomorphizer inference-engine diagnostics. E0645 (static-match) and E0900 (ICE)
stay in mono (specialization mechanics, not inference). **Zero §3 abstract-
resolution gap in the corpus** — all five are concrete call-site checks.

### What landed (committed-quality, in tree; NOT git-committed)
1. **Pipeline reorder (single-module).** `build_standard/frontend/raw_pipeline`
   in `passes/pass_registry.cryo`: `FunctionBodyTypeCheck` moved to immediately
   before `Monomorphization`. `passes/pass_id.cryo`: FunctionBodyTypeCheck
   requirement `GenericsValidated` → `StructFieldsSynced` (so `validate()`'s
   linear provision scan accepts the new order). MoveCheck/DropInsertion/
   GenericValidation stay after mono.
2. **Sema owns the three call-site checks** (`passes/sema.cryo`). New
   `check_generic_free_call(call, ident)`, invoked from `resolve_call`'s
   Identifier branch, ports the monomorphizer's `try_infer_function_call`
   three-pass inference onto the shared `InferCtx` unifier (new
   `import Compiler::Types::Inference`) reusing sema's already-resolved arg
   types. Emits E0306 (where-bound), E0307 (un-inferable `![implicit]`), E0214
   (conflicting `T`). Helpers: `find_fn_template_for_call` (bare-name+arity,
   defers on ambiguity), `sema_arg_is_polymorphic_literal` (node-kind only — in
   sema literals already carry a resolved_type, unlike pre-sema mono),
   `free_infer_type_concrete`, `free_infer_arg_reliable`, plus three emit_*.

### The key soundness finding (drives the rest of the flip)
Moving FunctionBodyTypeCheck ahead of mono exposed that **sema's `resolved_type`
is not yet authoritative pre-mono** for *call-expression arguments*:
  * a nested **inferred generic call** stays abstract (`max_of(3,9)` → `T`, since
    mono binds its `T`); and
  * a **bare-name call mis-resolves cross-namespace** to a same-leaf sibling
    (`classify(...)` → `PatternMatching::Sign` via `decl_index.lookup_func_return`
    on the bare name) in the mega unit-test build.
Trusting those wrong types made the call-site check false-positive (14 unit-build
errors: `Sign`-vs-`i32`, `T`-vs-`i32`). Fix = the bridge's defer discipline:
`free_infer_arg_reliable` treats a `CallExpression` arg as non-authoritative and
**defers** it (mono still backstops the specialization); literals / variables /
turbofish stay reliable and drive the checks. E0307 tightened to the airtight
arg-less case. Result: **make test 99/0 + unit ok; selfhost ✓ FIXED POINT**, new
IR md5 `79c53d890923e539a9525e79396548d1`.

### Why this is an increment, not the finished flip
The compiler's OWN build still uses the multi-module orchestrator, which runs
sema in Phase 6b (AFTER mono) — so the orchestrator is NOT yet flipped. And the
two `resolved_type` deficiencies above are *deferred*, not *fixed*; mono's
inference still backstops them. The remaining flip work, in order:
  * **(A)** Sema must INFER + PIN `resolved_callee` + set concrete `resolved_type`
    for generic free calls, so nested calls resolve concretely pre-mono
    (closes the `max_of`→`T` deficiency). `check_generic_free_call` already
    computes the bindings; extend it to pin + set the return type.
  * **(B)** Sema must resolve a bare-name direct call's `resolved_type` from the
    arg-type-selected overload (its `resolved_callee`), not the last-write-wins
    bare `decl_index.lookup_func_return` (closes `classify`→`Sign`).
  * **Orchestrator reorder**: extract `FunctionBodyTypeCheck` into a new
    per-module phase between Phase 6a-i (DirectiveProcessing) and 6a-ii (mono);
    Phase 6b keeps GenericValidation/MoveCheck/DropInsertion/TypeLowering.
  * **Delete mono's inference engine** (the ~27 fns A2 mapped) only AFTER A+B
    make sema authoritative — it is the backstop until then.

Tree: single-module reorder + sema checks in place, selfhost fixed point. Not
git-committed; not yet repinned (pinned BRIDGE compiler still builds the new
source fine).

---

## 2026-06-17 (cont. 2) — ORCHESTRATOR REORDER attempted; the (A) blocker mapped precisely; CONSOLIDATED to a green selfhost fixed point

Pushed past the single-module reorder into the real flip: reordering the
multi-module orchestrator (`instance.cryo`) so the compiler's OWN build runs
`FunctionBodyTypeCheck` before `Monomorphization`.  The reorder itself is
mechanically correct and got remarkably far, but it surfaced a concrete,
multi-faceted blocker — sema is not yet authoritative for *generic call return
types* pre-mono.  Reverted the orchestrator change to keep a green, validated
base; kept the sound pre-mono groundwork.

### Orchestrator reorder (reverted, but documented for redo)
In `compile_project_with_ctx`: inserted a new per-module phase
`[FunctionBodyTypeCheck]` between Phase 6a-i (DirectiveProcessing) and 6a-ii
(Monomorphization), with `validate_with_initial` provisions {ASTValidated,
NamesResolved, TypesResolved, StructFieldsSynced, DirectivesProcessed}; removed
FunctionBodyTypeCheck from Phase 6b and marked `BodiesTypeChecked` as an incoming
provision there (for MoveCheck).  No `pm_cached` skip in the new phase (the
incremental-cache decision isn't computed until after mono).  This compiles and
the structure is right; redo it once (A) below lands.

### The blocker: sema must resolve GENERIC CALL RETURN TYPES pre-mono (work item A)
With mono after sema, mono SKIPS calls sema already resolved, so any wrong/abstract
`resolved_type` sema leaves on a generic call reaches codegen.  The reorder build
failed ONLY on this class (the rest of the huge compiler+stdlib build was clean):

1. **Turbofish scope call return** — `mem::transmute<f64,i64>(x)` resolved to the
   abstract template return `To` instead of `i64`.  **FIXED** (kept): new
   `subst_return_from_call_args` + a pre-mono fallback in
   `resolve_module_qualified_function` (sema ~895) that substitutes the turbofish
   args (`scope.generic_args`/`call.generic_args`) into the template return via
   `TypeSubstitution::from_params` when the spec doesn't exist yet.
2. **`?` on an unresolved `Result<...>`** — `enum_try_shape` → `unwrap_to_enum`
   returns null for an InstantiatedType with no `resolved_type`.  **FIXED** (kept):
   localized `generic_base` fallback in `enum_try_shape` (sema ~10567) — variant
   NAMES only, so payload-reading callers of unwrap_to_enum are untouched.
3. **Match exhaustiveness / all-paths-return on unresolved `Option<...>`** —
   `patterns_cover` → same null.  **FIXED** (kept): same localized `generic_base`
   fallback in `patterns_cover` (sema ~3726).
4. **STILL OPEN — the core of (A): INFERRED generic call/method returns stay
   abstract.**  `choose(&r, &view)` (free) → `Option<T>` not `Option<i32>`;
   `s.get(index)` (method on `Slice<u8>`) → `Option<T>` not `Option<u8>`;
   `op_from_u8(..)` similarly.  The match subject is then abstract, so `Some(b)`
   binds `b: T` (E0200 `found T`), exhaustiveness/divergence misfire (E0405/E0403).
   FIX = sema must INFER the type args from the arguments and SUBSTITUTE them into
   the return type, for BOTH free calls and method calls, setting the call's
   `resolved_type` to the concrete instantiation (`Option<i32>`/`Option<u8>`).
   `check_generic_free_call` already computes the bindings (via `InferCtx`); extend
   it to return the substituted concrete return and have `resolve_call` use it; do
   the analogous thing on the method-call path (`resolve_method_call`).  WATCH the
   mono coupling: do NOT pin `resolved_callee` for the inferred case (the
   pin-stranding warning at sema ~859) — leave it unpinned so mono still
   specializes; only set `resolved_type`.  Pattern binding already substitutes
   concrete `type_args` into payloads via `peel_to_instantiation`, so a concrete
   `Option<i32>` subject is enough for `Some(b): i32`.

Item 4 is the heart of "make sema the source of truth" (Phase 5 / step A) and is a
real multi-faceted piece (free + method + turbofish inferred returns).  It must
land BEFORE the orchestrator reorder can keep selfhost green, and BEFORE mono's
inference engine can be deleted (step 6).

### Consolidated checkpoint (GREEN, VALIDATED — current tree, uncommitted)
- Single-module pipelines reordered (`pass_registry.cryo`) + `pass_id.cryo`
  requirement swap.
- Sema owns E0306/E0307/E0214 call-site checks (`check_generic_free_call` + helpers).
- Pre-mono resolution groundwork (items 1-3 above) — INERT in the current
  (orchestrator-unchanged) order because sema still runs after mono there, so the
  fallbacks never trigger; they are correct and ready for the orchestrator redo.
- `make test`: unit ok + compile-fail **99/0**.
- `selfhost-check --no-windows`: **✓ FIXED POINT**, IR md5
  `d767168da7706567c64ab5fb6b1c7bb6`.
- NOT git-committed; NOT repinned (the pinned BRIDGE compiler still builds the new
  source fine, so no repin is required to continue).

### RECOMMENDED NEXT SEQUENCE (for the next session)
1. Land work item **A.4** (inferred free + method call return substitution) — the
   keystone.  Validate against the orchestrator reorder by re-applying it
   temporarily and building stdlib via stage-2 (`frame.cryo` is the canary:
   `byte_at` / `read_frame`).  Iterate until stdlib+compiler build clean under the
   reorder.
2. Re-apply the orchestrator reorder (documented above) + validate selfhost fixed
   point + make test.
3. THEN delete mono's inference engine (step 6, the ~27 fns A2 mapped) and mono's
   E0306/E0307/E0214 emission; sema is now authoritative.  Validate.
4. Repin (linux) once the flip is complete and green.

---

## 2026-06-18 — THE FLIP SELF-HOSTS ✅ (orchestrator reordered, sema-before-mono, byte-identical fixed point)

Drove the mono-after-sema flip all the way through the compiler's OWN multi-module
build.  The orchestrator now runs `FunctionBodyTypeCheck` BEFORE `Monomorphization`,
sema resolves generic call/member types pre-mono, and the compiler **self-hosts
byte-identically** under the new order.

### Result
- `selfhost-check --no-windows`: **✓ FIXED POINT OK** (6/6 stages, stage-3 == stage-4
  byte-identical IR, md5 `83bc582db52cc446e2a654aacd9d7bb2`).
- `cryo build stdlib/lib.cryo`: **compiles clean** under the new-order stage-2.
- `make test`: **6 residual errors** — ALL in `tests/stdlib/iter.cryo`, all
  associated-type-projection reductions in iterator combinators (`filter`/`zip`/
  `enumerate` `This::Item`).  See "Remaining" below.  Everything else green.
- NOT git-committed, NOT repinned (pinned BRIDGE compiler still builds the new source).

### Architecture landed (in `instance.cryo`)
Sema runs in TWO orchestrator phases now, with distinct jobs:
- **Phase 6a-i.5 (NEW, pre-mono)**: `FunctionBodyTypeCheck` over all modules, between
  6a-i (DirectiveProcessing) and 6a-ii (Monomorphization).  Drives generic CALL-SITE
  resolution (the A.4 keystone) so mono specializes correctly.  It CANNOT fully resolve
  bodies that reference concrete generic instantiations whose specs don't exist yet
  (`Array<i64>.ptr`, `Slice<u8>::get`, ...), so it leaves some nodes abstract.
- **Phase 6b (post-mono)**: `FunctionBodyTypeCheck` REMAINS here (the handoff said remove
  it; that's wrong — spec bodies created by mono only exist post-mono and MUST be
  type-checked, e.g. `offset<u8>`'s body).  It is the AUTHORITATIVE pass for codegen.
  6b is detected via `Provision::MonomorphizationComplete` and sets
  `TypeCheckVisitor.post_mono_verify`.

**Key correctness mechanism — `post_mono_verify`:** in the post-mono pass `resolve_expr`
does NOT short-circuit on a cached `resolved_type` (it re-resolves), because the pre-mono
pass may have left a parent expression resolved while a child arg stayed abstract (e.g.
`Option::Some(this.values.ptr[slot])` — the call resolved to `Option<i64>` in 6a but its
arg stayed unresolved because `Array<i64>` wasn't specialized).  Without re-resolution 6b
skipped the resolved parent and never fixed the child → codegen got a null payload →
SIGSEGV.

### The surface area of the flip — pre-mono member resolution (all in `sema.cryo`)
With mono AFTER sema, every place sema leaned on mono having run first had to learn to
resolve against the generic TEMPLATE + substitute type_args.  Each is principled (no
band-aids):
1. **`resolve_direct_call` expected-type strip** — only swap in `expected_type` for an
   unresolved-instantiation return when it's genuinely ABSTRACT (`contains_generic_param`),
   not merely spec-less; a concrete `Option<Color>` pre-mono was being collapsed to
   `Color`, breaking match exhaustiveness/binding.
2. **`check_generic_free_call` returns the concrete return** (A.4 keystone, free calls):
   infer type args, `TypeSubstitution::from_params(...).apply(template_ret)` →
   `choose(...) -> Option<i32>`.  `resolved_callee` stays unpinned (mono specializes +
   backfills via `propagate_instantiated_resolution`).
3. **`resolve_method_return_via_template`** (A.4, methods) — receiver instantiation has no
   reverse type-symbol pre-mono; resolve the method off the BASE template (inline struct
   methods) OR the DI under the base symbol (impl-block + enum methods like
   `Option::unwrap`) OR the implemented traits (combinator defaults like `take`), then
   `subst_method_return_from_receiver` (own type_args) + `subst_this_in_type` (the `This`
   Self placeholder, `take -> TakeIter<This>` -> `TakeIter<Range<i32>>`).
4. **`resolve_static_method_return_via_template`** — `String::with_capacity`,
   `Slice::from_raw`: resolve off the template, prefer `expected_type` when it instantiates
   the same base (`Array::new()` with `mut x: Array<u8>`), else substitute the owner's
   default/explicit instantiation.
5. **`resolve_field_via_template` + field subst** — `Pair<u32,i64>.second` pre-mono:
   `check_field_access` fails on the unresolved instantiation; read the field off the
   template AST and substitute (`Y -> i64`).
6. **Pin-stranding fixes** (a call pinned to an ABSTRACT template symbol makes mono SKIP
   it — `try_infer_*` early-returns on a valid `resolved_callee`/`resolved_method` — and
   codegen then can't resolve the never-built spec):
   - `resolve_module_qualified_function`: don't pin the constructed spec name in the
     pre-mono turbofish path (`mem::transmute<f32,u32>`).
   - `scope_is_generic_template`: also resolve the bare scope to its QUALIFIED template
     (`Slice` -> `std::core::slice::Slice`) so `Slice::from_raw` isn't pinned.
   - `try_pin_overload_mangled_callee`: skip generic function templates, found via
     `find_fn_template_for_call` (bare-name+arity) so a call whose template lives under a
     DIFFERENT module path (`swap` -> `std::core::mem::swap`) is recognized — this was the
     `undefined reference to swap<T>` link failure in `symbolic_check_body`.
7. **Void back-fill guard** — don't back-fill an opaque/adapter local's `resolved_type`
   from a `void` initializer (an adapter chain resolves to void pre-mono); leaving it
   unresolved lets the post-mono pass back-fill the concrete type.

### Remaining (the ONLY thing between here and full green)
6 errors in `tests/stdlib/iter.cryo`: associated-type-projection reduction in combinators.
`next(mut &this) -> Option<This::Item>` on `FilterIter<Range<i32>>` / `ZipIter` /
`EnumerateIter`.  `subst_this_in_type` substitutes the projection BASE
(`This -> FilterIter<Range<i32>>`) but the result is still an `AssocProjection`
(`FilterIter<Range<i32>>::Item`) — it needs RECURSIVE reduction to `i32` (walk the impl's
`Item` binding through the combinator chain).  Mono does this today
(`proj.set_resolved_type`); sema must do it pre-mono.  This is a DISTINCT mechanism from
the type-arg substitution above (`substitution.cryo:apply_assoc_projection` only rebuilds
the projection, doesn't reduce it).  Next session's work item.

### Mono's inference engine is NOT yet deleted (step 6)
It still backstops every generic specialization (sema leaves `resolved_callee`/
`resolved_method` unpinned for the inferred case precisely so mono still discovers +
specializes).  Sema is now the source of truth for resolved TYPES; mono still owns
DISCOVERY + specialization.  Deleting mono's `try_infer_*` requires sema to create the
instantiation demand — defer until after the assoc-projection reduction lands and the
suite is fully green.

### Files
`instance.cryo` (orchestrator: +Phase 6a-i.5, 6b keeps FunctionBodyTypeCheck +
BodiesTypeChecked provision).  `sema.cryo` (all of the above; `post_mono_verify` field).
The single-module pipeline reorder + `pass_id.cryo` requirement swap are from the prior
session (committed at HEAD `f9a1ffa1`).  UNCOMMITTED; Jake commits.

---

## 2026-06-17 — Cross-platform baseline re-audit (start of finish-the-flip session)

Rebuilt stage-2 and ran `make test` on BOTH platforms from a clean checkout of HEAD
`6e074d37` (pin still pre-flip `54277d4e`).

**The prior docs undercounted the Linux failing set.** The claim "Linux green except 6
iter.cryo errors" is WRONG. Reality, verified this session:

- **Windows** (`$env:CRYO_CC='gcc'; make cryo` clean → `make test`): aborts in
  `tests/lang/lambdas.cryo` with 3× `E0200` on `Option::map`:
  - :234 `some.map<i32>(...)` — method turbofish
  - :244 `some.map(tentimes)` — named-fn arg
  - :252 `some.map((n:i32)->i32{...})` — lambda arg
  `expected Option<i32>, found Option<U>`.
- **Linux/WSL** (`make cryo` clean → `make test`): **IDENTICAL** — same 3× E0200 in
  `lambdas.cryo`, same lines, same message.

**Discrepancy resolved: both platforms fail identically.** The suite aborts at
`lambdas.cryo` on BOTH, so neither ever reaches `iter.cryo`. The "6 iter.cryo errors"
are real (Step B) but masked behind the lambdas abort (Step A). Platform parity holds —
I can develop on Windows and confirm on Linux rather than hunting a platform gap.

**Plan unchanged, ordering confirmed:** Step A (method-call/turbofish inferred return)
unblocks the suite to reach `iter.cryo`, then Step B (assoc-projection reduction), then
Step C (delete mono inference), then Step D (Jake repins). lambdas.cryo is the Step A
canary; iter.cryo the Step B canary.


## 2026-06-17 (cont.) — Steps A+B landed; flip is FAR more broken than documented

### CRITICAL: the pinned (pre-flip 54277d4e) compiler passes ALL 1234 unit tests, 0 failed.
Ran `bin/cryo.exe test` (pinned) on the current tree: 1234 passed / 0 failed, then into
compile-fail tests. So EVERY failure below is a **flip-introduced regression**, masked
behind the `lambdas.cryo` abort the suite hit first. The handoff's "Linux green except 6
iter.cryo" was very wrong — the flip broke a broad swath; they just never ran past lambdas.

### Fixed this session (each root-caused, both the symptom and the cause):
1. **lambdas.cryo (Step A)** — inferred/turbofish generic METHOD-call returns. New
   `resolve_generic_method_return` in sema.cryo: infers method-level type params from args
   (shared `InferCtx`) or reads method turbofish, binds `This`, handles inline/impl-block/
   trait-impl/trait-DEFAULT methods AND non-generic owners (`VaArgs::next<T>`), plus
   `![implicit]` return-type-driven inference. Wired into all three method-return paths in
   `resolve_method_call`. Leaves callee/method UNPINNED.
2. **send_sync_auto_derive.cryo** — `is_send`/`is_sync`/(`is_copy` shares the shape)
   returned false for an UNRESOLVED instantiation (`Atomic<u32>` pre-mono). Added
   `OwnershipQuery::inst_template_thread_safe`: walks the template's fields/variant payloads
   with type-args substituted when no concrete spec exists yet. (ownership.cryo)
3. **iter.cryo (Step B)** — recursive assoc-type-projection reduction. Ported mono's
   `concrete_trait_args_for` / `derive_impl_where_generics` / `bind_where_arg_param` /
   `resolve_concrete_projection` / `reduce_projections` into sema as the `proj_*` /
   `reduce_assoc_projections` cluster, plus `resolve_trait_impl_method_return` (binds struct
   params + where-derived params + This, resolves the return annotation, reduces
   projections) and `finalize_assoc_return`. Handles FilterIter/EnumerateIter (`I::Item`),
   ZipIter (`Pair<A,O>` where-derived), MapIter/TakeIter chains.
4. **varargs.cryo** — `![implicit]` `va.next()` on the NON-generic `VaArgs` receiver:
   generalized `resolve_generic_method_return` to handle non-generic owners
   (`lookup_inherent_owner`) and added return-type-driven inference from `expected_type`.
5. **array_literals.cryo** — `expect_eq(i64_val, 3)` false E0214. Root cause: mono's
   `arg_is_polymorphic_literal` short-circuited on a valid `resolved_type` — correct only
   when mono ran first (literals untyped); post-flip sema types every literal, so EVERY
   literal read as non-polymorphic and an `i64`/`u64` param vs a bare `i32` literal
   spuriously conflicted. Removed the short-circuit (monomorphizer.cryo); node kind is the
   signal, matching sema's version. This is a Step-C-class staleness fix.

### Current wall: enum_discriminant_base.cryo — wrong overload picked
`classify(Color::Blue)` (file-local `classify(Color)->i32`) resolves to `Sign` — the
`classify(Option<i32>)->Sign` overload from pattern_matching.cryo (the whole tests/ dir
compiles together). So sema's flip-era call-site overload resolution is picking a same-named
free fn by bare name rather than by argument type. Next: inspect resolve_direct_call's
overload selection. This is a regression (pin resolves it correctly).

### NOT yet done
Step C (delete mono inference engine, sema creates demand) and Step D (repin). Self-host
NOT re-validated since these edits; Linux NOT re-checked this session. Tree UNCOMMITTED.

### Continued fixes (overload + abstract-defer), then hit a runtime wall
6. **enum_discriminant_base.cryo** — wrong overload picked. `resolve_direct_call` set the
   call's return type via BARE-name `lookup_func_return`, ignoring the arg-matched overload
   `try_pin_overload_mangled_callee` had just selected — so `classify(Color)->i32` resolved
   to a same-named `classify(Option<i32>)->Sign` from another test module. Fix: made
   `try_pin_overload_mangled_callee` RETURN the matched overload's concrete return type and
   `resolve_call` prefer it (sema.cryo). Unique-(qualified-)name and multi-overload-by-arg
   both return the authoritative type; generic templates still defer to resolve_direct_call.
7. **for_in.cryo** — `Range::new(0,5)` in a for-in: mono static-method inference PASS B
   unified `Range<T>` against an ABSTRACT expected type (the still-generic scrutinee type),
   pinning `T:=T`, after which the i32 literal conflicted. Gated PASS B on a CONCRETE
   expected type (monomorphizer.cryo). Exposed once #5 routed literals into PASS C.
8. **generics.cryo** — calls inside generic bodies (`expect_eq(t_val, 0)` with abstract
   `T`): mono's PASS C literal fallback emitted E0214/checked compatibility against an
   ABSTRACT bound instead of deferring. Added the `infer_type_is_concrete(bound)` guard to
   BOTH mono PASS C sites (free + static), mirroring sema's `free_infer_type_concrete`
   guard. Exposed by #5.

### CURRENT WALL (2026-06-17 end): runtime HEAP CORRUPTION in the full test binary
After the above, ALL test files COMPILE (huge progress from "aborts at lambdas"). But
`cryo test` now crashes with `0xC0000374` (STATUS_HEAP_CORRUPTION) at runtime — and the
runner buffers per-test output, so the crash loses all of it (even a single-test pattern
crashes, because the harness builds the whole tests/ project into one binary).
- A standalone iterator program (`Range::new(0,5).map(f)` for-in summing to 20) compiles
  AND runs correctly with the new compiler — so the core iterator/method-inference codegen
  is sound; the corruption is elsewhere in the full set.
- The PINNED (pre-flip) compiler runs the identical test sources 1234/0 clean. So this is a
  flip-era MISCOMPILE in some file my compile-fixes newly enabled (a bad global-init / drop
  / layout from a wrong resolved_type somewhere), NOT in the basic paths I validated.
- NEXT: file-level bisection of tests/ (move out halves, re-run) to find the miscompiling
  file, then trace its codegen. Determine whether a resolved_type one of my fixes sets is
  subtly wrong for that shape, or it's independent flip codegen debt.

### Both-platform status: PERFECT PARITY maintained
At every checkpoint Windows and Linux failed identically (verified Linux at the
enum_discriminant point). My changes are platform-agnostic compiler logic.

### Validation gaps to close next session
- self-host fixed point NOT re-checked since these edits.
- O2 not separately run (blocked behind the heap corruption at O0).
- Step C (delete mono inference) still pending — though #5/#7/#8 already adapted several
  mono-staleness points toward it.
- Tree UNCOMMITTED. Jake commits + repins.

## 2026-06-17 (cont. 3) — "heap corruption" ROOT-CAUSED (by-value TraitRef UAF) + cascade narrowed to 3 sema-authority gaps

### The "STATUS_HEAP_CORRUPTION at startup" diagnosis was WRONG — corrected here
Prior session reported the test binary heap-corrupts at startup, even `--list`. Re-measured:
- The unit test BINARY runs fine standalone: from `tests/` cwd, `--jobs=1` AND default
  parallel both give **1234 passed / 0 failed**. (`--list` works too.) The earlier "even
  --list crashes" was running `cryo test` — i.e. the COMPILER — not the binary, and from the
  wrong cwd (the 2 TLS failures were just a missing `fixtures/tls/` relative to `tests/build`).
- The crash is in the **compiler process**, during the in-process `compile_project` of the
  test project (`cryo test` rebuilds it), specifically in **Monomorphization**.

### Root cause (valgrind, Linux): use-after-free of a shared `Array<SymbolStr>` buffer
Linux glibc TOLERATES the corruption (exit 0); Windows heap aborts (0xC0000374) — so parity
"held" only in the sense both miscompile; only Windows surfaces it. Ran the Linux build under
`valgrind --track-origins=yes`: **Invalid read** in `Monomorphizer::check_function_bounds_at_call`,
of a 16-byte block **free'd by `Array<SymbolStr>::drop` inside `Monomorphizer::emit_call_bound_failure`**.
- `emit_call_bound_failure(... tref: TraitRef ...)` took the `TraitRef` **BY VALUE**. Cryo
  shallow-copies the struct (incl. `path: SymbolStr[]`), sharing the backing buffer; the
  param drops at return and frees the AST's `bound.trait_refs[ri].path` buffer → next read of
  that path (here, or the post-mono sema pass) is a UAF. Sema's twin `emit_free_call_bound_failure`
  already takes `tref: TraitRef*`; mono diverged. **FIX: take `tref: TraitRef*`** (monomorphizer.cryo).
- Under gdb the UAF surfaced as a zeroed trait-name SymbolStr → false `E0306 "T: " is not
  satisfied` (empty trait name); without gdb, a clobbered pointer → hard crash. Same bug.

### Second fix exposed by the first: mono bound-check ran on ABSTRACT bindings
After the UAF fix the 4 remaining errors were `E0306: T: Eq is not satisfied` (concrete = the
still-abstract `T`). `check_function_bounds_at_call` lacked the abstract-defer guard sema has.
**FIX: `if (this.type_contains_generic_param(concrete)) { continue; }`** before the bound check
(mirrors sema's `check_generic_free_call`). monomorphizer.cryo.

### Result: crash GONE. Suite now COMPILES past mono; 149 → 145 real compile errors remain.
All flip regressions (pin compiles identical sources 1234/0). Two more fixes landed:
- **InstantiatedType structural equality** in `checker.cryo` `check_compatibility` (mirrors the
  existing Reference/Tuple/GenericParam fallbacks): the arena does NOT dedupe instantiations,
  so pre-/post-mono mint distinct TypeIDs for one logical `Foo<Bar<i32>>`. Compare base+args
  structurally. (Helped marginally; the bulk of E0200 is a different shape — see below.)
- **Sema-demand enqueue** in mono discovery (monomorphizer.cryo `discover_inferred_calls_in_stmt`
  VarDecl case): enqueue a local binding's sema-resolved concrete instantiation directly, so
  adapter CHAINS (`a.iter_ref().copied()`, the for-in iterator local) get specialized even when
  mono's call-by-call walk can't reconstruct the chain result. Fixed all 3 **E0900** (unresolved
  instantiation). Minimal "sema creates the demand" — the spirit of Step C.

### Remaining 145 = THREE sema-authority gaps (all in the for-in / lambda / static-ctor area):
1. **E0200 (104)** — `for (i in Range::new(0,5))`: sema types the initializer as abstract
   `Range<T>` (no type-arg inference for a generic STATIC-CONSTRUCTOR call), while mono lifts the
   binding to `Range<i32>` → `can_assign(Range<T>, Range<i32>)` fails in the post-mono pass.
   Need sema to infer `T` from the value args of `Range::new(0,5)` (the static-ctor analogue of
   `check_generic_free_call` / `resolve_generic_method_return`).
2. **E0201 (36)** — `cannot find value 'this'` inside a lambda body that captures a local
   (`(x) -> i32 { x + bias }`). Closure-capture resolution gap (a captured local is being looked
   up through a `this` that the pass doesn't have).
3. **E0358 (5)** — `for (x in arr)` over an `Array<i32>`: "no method named `next`". The for-in
   desugar isn't inserting the `.iter()` (container → iterator) for a direct-container scrutinee.

### Validation state
- Windows: `cryo test` no longer crashes; 145 compile errors (above). Repros for the for-iter
  E0900 and a standalone iterator program compile AND run correctly.
- Linux: not re-run since these 4 fixes; parity expected. self-host NOT re-checked. O2 not run.
- Tree UNCOMMITTED. Files touched: monomorphizer.cryo (UAF by-ptr, abstract-defer, demand-enqueue),
  checker.cryo (InstantiatedType structural eq). Jake commits + repins.

## 2026-06-17 (cont. 4) — Heap-corruption KILLED + cascade driven 149 → 2 (nine root-caused fixes)

Picked up from "cont. 3". Drove the flip from "crashes, nothing runs" to a **2-error**
compile of the full suite. Every fix root-caused, no masking. The two remaining are ONE
precisely-identified nested-generic corner (below).

### Fixes that landed this session (all validated against the full suite, both root + cause):
1. **UAF (the "heap corruption")** — `monomorphizer.cryo` `emit_call_bound_failure` took
   `tref: TraitRef` BY VALUE; the shallow copy shared the AST's `Array<SymbolStr> path`
   buffer and the param-drop freed it → next read UAF (crash on Windows, silent on Linux).
   FIX: take `tref: TraitRef*` (matches sema's twin). Found via valgrind on Linux.
2. **mono bound-check on abstract bindings** — `check_function_bounds_at_call` lacked the
   abstract-defer guard. FIX: `if (this.type_contains_generic_param(concrete)) continue;`.
3. **InstantiatedType structural equality** — `checker.cryo` `check_compatibility` had no
   structural fallback for two non-deduped instantiations of one logical type (it has them
   for Reference/Tuple/GenericParam). FIX: compare generic_base + each type_arg recursively.
4. **for-iter adapter-chain demand** — mono never enqueued the for-in iterator local's
   sema-resolved instantiation, so `a.iter_ref().copied()` (`CopiedIter<RefIter<i32>>`)
   stayed unresolved (E0900). FIX: in mono VarDecl discovery, `enqueue_from_type_ref(
   vd.resolved_type)` (guarded no-op unless a registered concrete template).
5. **generic STATIC-CONSTRUCTOR inference** — `Range::new(0,5)` (no turbofish, no expected
   type, the for-in local) typed as abstract `Range<T>` while mono lifted the binding to
   `Range<i32>` → E0200. FIX: `infer_static_owner_return_from_args` in sema unifies the
   ctor's params against the arg types to recover the owner's type args; wired into
   `try_resolve_static_method` as a refinement when steps 1-3 return an abstract result.
   (Cleared 97 of the E0200s.)
6. **for-in over a CONTAINER** — `for (x in arr)` typed the iterator binding as the
   container `Array<i32>` (pre-mono couldn't peel the unresolved inst to find `iter()`),
   inconsistent with post-mono → E0200/E0358. FIX: `lookup_type_sym` falls back to an
   instantiation's `generic_base` name when unresolved, so the `iter()`/`next()` probe
   resolves identically in both passes.
7. **PASS B unified against an ABSTRACT expected type** — a nested call `identity(123)` fed
   to `expect_eq<T>` saw the outer abstract `T` as its expected type and bound identity's
   own param to it. FIX: gate `check_generic_free_call` PASS B on
   `!contains_generic_param(expected_type)`. (Cleared the `max_of` E0636s.)
8. **lambda double-lowering (`cannot find value this`, 36×E0201)** — `resolve_lambda` re-ran
   in the post-mono pass (`post_mono_verify` re-resolves cached nodes) and re-invoked
   `synthesize_closure_struct`, re-walking synthesized `this.cap_i` with no `this_type`.
   FIXES: (a) idempotency guard - once a capturing lambda carries its closure-struct type,
   return it; (b) `resolve_identifier` honors a `this` node that already carries a resolved
   type (compiler-synthesized closure-field access is authoritative).

### THE REMAINING 2 (generics.cryo:158,168) — ROOT-CAUSED, fix deferred (cascade risk)
`expect_eq(identity(123), 123)` / `expect_eq(identity(v), v)`: there are **two** `identity<T>`
(1-arg) templates - `tests/lang/generics.cryo:148` and `tests/lang/match_generic_free_fn.cryo:24`.
`find_fn_template_for_call` requires a UNIQUE bare-name+arity match (`count == 1`), so the
collision makes it return null → `check_generic_free_call` bails → `identity(123)` surfaces
its template's abstract return `T` → poisons the outer `expect_eq` inference → no spec pinned
→ E0636 at codegen. (`max_of` has one definition, so #7 already fixed those.)
- Confirmed via debug print: `check_generic_free_call` is never entered for `identity`.
- ATTEMPTED FIX (reverted): namespace-disambiguate the collision to the calling module's
  candidate. Done in the shared helper it broke `resolve_direct_call`'s deferral (8 errors);
  isolated to a `check_generic_free_call`-only `_local` variant it STILL cascaded into 8
  `va.next()` E0636 in varargs.cryo. Fixing identity's resolution shifts the specialization
  graph and unmasks a VaArgs method-spec gap. This is the fragile mono-inference interaction
  Step C is meant to delete; it wants the holistic treatment, not another point-patch.
- Recommended next step: tackle as part of Step C (sema creates demands), OR rename one of
  the two test-local `identity`s if a quick green is needed for an interim checkpoint - but
  the collision handling is a real compiler gap worth fixing properly.

### Files touched (all uncommitted): monomorphizer.cryo (#1,#2,#4), checker.cryo (#3),
### ownership.cryo (prior session #2), sema.cryo (#5,#6,#7,#8), passes unchanged.

### Validation: full suite compiles to 2 errors (above). Self-host fixed-point + Linux O0/O2
### + `make test` green were NOT yet reached (blocked on the 2). Self-host check running at
### handoff write time. Jake commits + repins once green.

### Self-host VALIDATED 2026-06-17 (end of "cont. 4")
Linux `selfhost-check.py --no-windows`: **byte-identical FIXED POINT** (stage-3 == stage-4),
IR md5 `3b7396b71606a4ce108ab0b0c3b7e1df`, 49,883,221 bytes. So the nine fixes did NOT
break self-host. (Prior baseline `83bc582db52cc446e2a654aacd9d7bb2` was pre-these-fixes;
md5 differs because compiler source changed, but the fixed-point property holds.) Windows
stage-2 rebuilt after (WSL run wipes compiler/build). Tree compiles the full suite to the
2 documented `identity`-collision errors; everything else green. NOT yet run: `make test`
to completion (blocked on the 2), O0/O2 split, Windows self-host. Jake commits + repins.

================================================================================
## 2026-06-18 (cont. 5) — VaArgs + identity + static-method-spec cascade: 8 errors -> 1

### Starting state (committed `12a9d306`, repinned)
Contrary to the cont.4 handoff ("2 identity errors"), the COMMITTED tree builds the
suite to **8 errors, all `va.next<T>()` E0636** in `tests/stdlib/varargs.cryo`. The
`identity` collision was resolved in `12a9d306` and that resolution UNMASKED the VaArgs
gap (the cont.4 doc predicted exactly this coupling). The committed PIN (`bin/cryo`) is
**NOT** the "1234/0 green oracle" the handoff claims — it fails `va.next<i64>()` too (that
claim refers to a stale pre-flip pin). Verified with a standalone repro.

### THREE root-caused fixes (suite 8 errors -> 1). Self-host VALIDATED.
**#1 — VaArgs generic-instance-method-on-concrete-receiver (monomorphizer.cryo
`try_infer_method_call` ~5640).** The early-return `if (call.resolved_method != null)
return;` assumed "sema resolved the method => fully handled". FALSE for generic methods:
sema resolves the call's TYPE and pins `resolved_method` to the **template** (`next<T>`,
`generic_params.length==1`) + pins `resolved_callee` to the abstract template mangle, but
does NOT mint the per-instantiation spec. Mono then skipped specialization -> codegen
E0636 (no `next<i64>` symbol). FIX: skip only when the resolved method is already concrete
(`rm.func.generic_params.length == 0` — a non-generic method or an already-minted spec
that mono pinned back). A still-generic template falls through to the existing turbofish/
inference path; downstream guards (unresolved receiver, still-generic bindings) keep
abstract contexts deferred. Confirmed via debug: `resolved_method=1 rm_generic_params=1
generic_args=1`.

**#2 — `identity<T>` collision (sema.cryo `find_fn_template_for_call` ~6114).** Two
1-arg `identity<T>` templates (generics.cryo + match_generic_free_fn.cryo, distinct
namespaces). Bare-name+arity lookup returned null on the tie (count!=1) -> identity's
abstract `T` poisoned the enclosing `expect_eq` inference -> E0636. FIX: module-disambig
TIE-BREAK — preserve the `count==1` fast path exactly; on a tie, prefer the candidate whose
`module_name` == `current_module_name()`. Helps ALL callers (incl. `resolve_direct_call`,
which WANTS the non-null generic template to defer correctly). The cont.4 `_local`-variant
attempts cascaded into VaArgs — that cascade target is now fixed at the root (#1), so the
shared-helper fix is clean.

**#3 — generic STATIC-method abstract-callee pin (sema.cryo `scope_is_generic_template`
~10659).** `Layout::of<T>()`, `Request::parse<R>`, `Response::parse<R>` linked against
ABSTRACT template symbols (`...Layout::of` no type-args, `...parse<1R>`). Cause:
`register_static_method_templates` keys generic statics under the FULLY-QUALIFIED owner
path (`std::alloc::layout::Layout::of`), but the guard's combined-key check built only the
BARE `Layout::of` -> missed -> `pin_scope_callee_combined` pinned the abstract `scope::member`
symbol -> mono's `try_infer_function_call` early-returns on a valid `resolved_callee` ->
spec never built -> link error. FIX: mirror the existing qualified-scope lookup on the
combined key (resolve_qualified(scope)::member). All three statics now specialize+link.
(These three were PRE-EXISTING, masked by the earlier codegen abort; surfaced once codegen
passed.)

### Validation
- Standalone repros built+ran: `va.next<i64>()` (7+35=42), `Layout::of<Thing>()` (size 16).
- Full suite: **8 errors -> 1** (the one below).
- **Linux self-host: byte-identical FIXED POINT** (stage-3==stage-4),
  IR md5 `88120bf3ad1135b905c0ca3d27372e54`, 49,893,083 bytes. The 3 fixes do NOT break
  self-compilation.
- Diff: sema.cryo +45, monomorphizer.cryo +20. No debug code left. UNCOMMITTED.
- NOT done: O0/O2 split, Windows. Jake commits + repins.

### THE 1 REMAINING ERROR — PRE-EXISTING (pin fails identically), NOT from these fixes
`Box<String>::clone` (in box.o) references the BARE `std::collections::string::String::clone`
which is never emitted (the real spec is `String<GlobalAlloc>::clone`). Root cause: a
MALFORMED `Box<String /*bare, missing inner GlobalAlloc*/, GlobalAlloc>` spec is created
whose body's `self.value.clone()` has receiver `String*` (bare). Confirmed by debug: the
FAILING case (`Box::new(String::from_str(...))` inline init) discovers an extra clone
receiver `std::collections::string::String*` that the PASSING case (`Box::new(s)` typed var)
does not. The bare `String` (a generic with all-defaulted `A=GlobalAlloc`) is NOT canonical-
ized to `String<GlobalAlloc>` in mono's substitution path — the substituted body annotation
carries a `pre_resolved` Named = bare String (resolver.cryo:174 short-circuits resolve_named
on `pre_resolved`, so any resolve-time canonicalizer is bypassed).
- Repro: `/tmp/vrepro/co2.cryo` (fails on BOTH my build AND committed pin).
- ATTEMPTED (reverted): a `resolve_named` canonicalizer (`maybe_canonicalize_default_generic`)
  folding bare all-default generics -> default instantiation + a `resolve_generic` base-peel.
  It fixed the SEMA side (a string.cryo `String<A>` E0200 it first caused, then peeled) but
  did NOT fix co2 — the bare String also enters via mono's `pre_resolved` substitution, which
  bypasses resolve_named. Incomplete -> reverted to avoid broad unvalidated behavior change.
- The real fix is one of: (a) canonicalize bare all-default generics at SUBSTITUTION time
  (where `pre_resolved` is set) so `String` -> `String<GlobalAlloc>` consistently; or
  (b) Step C — mono trusts sema's already-canonical resolved_type instead of re-deriving the
  element type. (b) is the principled mono-after-sema direction.

### NEXT
1. Fix the `Box<String>::clone` bare-String default-expansion (option (a) above is the
   localized fix: canonicalize in the AST substituter / wherever `pre_resolved` bare
   all-default generics are formed). Then re-run suite -> expect green, chase any unmask.
2. O0/O2 split + Windows self-host.
3. Step C — delete mono's inference engine (the payoff).

### UPDATE (cont. 5, same day) — SUITE NOW GREEN. The "1 remaining" fixed at root.
Fixed the pre-existing `Box<String>::clone` bare-String bug — fix #4. Root cause confirmed
by instrumentation: sema typed `String::from_str(...)` (a generic-with-all-defaults static
return, `-> String`) as the BARE template base `String`, NOT `String<GlobalAlloc>` — because
the method's return annotation was resolved during BOOTSTRAP (resolve_named's arena fallback,
which no default-expansion path covers). A free fn `mk() -> String` inline worked, a typed var
laundered it, a cast laundered it — only the direct static-method-call return leaked the bare
base into `Box::new`'s T inference → `Box<String/*bare*/>` → bogus bare `String::clone` ref.

FIX #4 (resolver.cryo + sema.cryo): added `TypeResolver::canonicalize_default_generic_type` —
folds an already-resolved bare all-default-generic BASE (`String`) into its default
instantiation (`String<GlobalAlloc>`) via the arena's dedup (no new demand; the canonical form
is already demanded). Wired into sema's `lookup_method_return` (split into `_raw` + a
canonicalizing wrapper) so EVERY method/static return read agrees with the annotation/inference
paths. Safe: operates on a resolved TypeRef (an explicit `String<A>` is an InstantiatedType,
skipped — no double-apply), fires only on a registered template base whose every param is
defaulted (so `Array<T>`/`Box<T>` are untouched).
- An earlier resolve_named-wide canonicalizer was tried+reverted (broke `String<A>` -> needed a
  peel, and missed the bootstrap path anyway). The read-site fix is narrower and correct.

### FINAL VALIDATION (cont. 5) — STEP A/B COMPLETE
- **`make test` GREEN at O0 AND O2**: unit **1233 passed / 0 failed**, compile-fail **99 / 0**.
- **Linux self-host: byte-identical FIXED POINT** (stage-3==stage-4),
  IR md5 `35513c9a565c04519ebf999c29cd9243`, 49,913,597 bytes.
- Diff (UNCOMMITTED): sema.cryo +62, monomorphizer.cryo +20, resolver.cryo +43. No debug code.
- Four fixes total this session: #1 mono generic-method early-return, #2 sema find_fn module
  disambig, #3 sema qualified-combined static-spec, #4 default-generic method-return canonical.
- NOT done: Windows (suite + self-host); Jake commits + repins. THEN Step C (delete mono engine).

---

## 2026-06-18 (cont. 6) — STEP C0 done: instrumented sema↔mono binding divergence

**Baseline:** HEAD `a73b2ce3`, clean. Goal: measure how load-bearing mono's inference
engine actually is vs sema, before deleting it.

### What landed (UNCOMMITTED, behavior-neutral, green: unit ok / compile-fail 99/0)
A validated **C1 foundation** — a channel for sema to hand mono its concrete type-arg
bindings so mono need not re-infer:
- `CallExprNode.resolved_type_args: TypeRef[]` (expression.cryo) + setter + ctor init.
- sema `check_generic_free_call`: stashes its computed bindings (concrete OR abstract-but-
  fully-bound) on the call at the keystone success.
- cloner `visit(CallExprNode*)`: element-wise copies `resolved_type_args` into the clone
  (so specialized bodies carry the stash; element-wise to avoid owning-array double-free).
Nothing READS the field yet for resolution (only the now-stripped C0 probe), so the suite is
unchanged. All temporary `eprintf` instrumentation + the temp `std::fmt` import were stripped.

### The measurement (corpus = `build/cryo build` over the compiler itself, free calls only)
| | MATCH (sema==mono) | ABSENT (no sema stash) | MISMATCH (disagree) |
|---|---|---|---|
| free-identifier path only | 150 | 9126 | **0** |
| + cloner copies stash + mono applies subst | 366 | 8902 | **8** |

Sema `check_generic_free_call` ENTERs 2733× / STASHes 2577×.

### Findings (the dependency surface, honest)
1. **0 mismatches** on the pure free-identifier path — where sema's keystone speaks, it never
   disagrees with mono. Safe seam confirmed.
2. The DOMINANT load-bearing mono inference is **module-scoped scope-resolution generic free
   calls** — `mem::offset` (4422), `mem::swap` (1046), `Layout::array` (987), `Layout::of`
   (405), `Slice::from_raw`, `transmute`, `try_array`, etc. sema's keystone is gated to
   **Identifier** callees (caller at sema.cryo ~5988), so `mod::fn<T>(...)` never reaches it.
   This is NOT a from-scratch port: the keystone's 3-pass `InferCtx` inference is module-
   agnostic once it has the `TemplateEntry`; C1 = route module-scoped ScopeResolution callees
   through it (mirroring mono `try_infer_function_call`'s `callee_scope` branch) + stash.
3. sema ALREADY infers bindings for **static-constructor** calls
   (`infer_static_owner_return_from_args`, `Range::new` → owner T) — just doesn't stash them.
4. **8 genuine divergences** in the abstract-stash+subst model: `hash_u64_le`, `write_i64`,
   `write_u64` (sema=1/mono=1 but different id). MUST be root-caused before mono READS the
   stash — exactly the latent sema/mono drift the mission warns about. (Likely a stash param-id
   vs subst-domain mismatch; characterize first, don't guess.)

### Revised C1 plan (in dependency order)
- C1a: root-cause the 8 free-call divergences (hash_u64_le/write_*). Gate: 0 mismatches.
- C1b: extend sema's free-call keystone to module-scoped ScopeResolution callees + stash
  (covers offset/swap/array/of/from_raw/transmute/try_array — the bulk).
- C1c: stash from `infer_static_owner_return_from_args` (+ the static-method-on-generic-template
  analogue mono's `try_infer_static_method_on_generic_template` owns — verify sema covers it).
- C1d: make mono discovery READ `resolved_type_args` (apply active subst) + enqueue, falling
  back to inference only where stash is absent; drive ABSENT→0; THEN C2 deletes the engine.
Self-host NOT re-run this session (no behavior change); suite green at default opt.

---

## 2026-06-18 (cont. 7) — STEP C1a + C1b landed (sema stashes free + module-scoped bindings)

**GREEN + SELF-HOST FIXED POINT.** Suite O0+O2 (unit ok / compile-fail 99/0). Self-host
byte-identical, IR md5 `940235d6efbf07033ffd4259ccb1803f` (49,972,272 bytes; baseline was
`35513c9a`, changed as expected — observable behavior improved). UNCOMMITTED, NOT repinned
(no bootstrap landmine: pinned compiler still builds everything; defer repin until a change needs it).

### C1a — root-caused the 8 free-call divergences → 0 mismatches
The 8 (`hash_u64_le`, `write_i64/u64`) had `subst==NULL` at mono discovery: they were in
SUBSTITUTED clones whose other type fields were concretized by `ASTTypeSubstituter`, but the new
`resolved_type_args` stash was left abstract (the substituter didn't know the field) → mismatch.
Fix: `substituter.cryo visit(CallExprNode*)` now SUBSTITUTES `resolved_type_args` (vs clearing
like resolved_callee/method). Safe because the stash is ONLY ever written by sema, always
abstractly (template params) — never a stale sibling's concrete pin — so applying this spec's
subst concretizes it correctly; a no-op on already-concrete elements. Corpus MISMATCH → 0.

### C1b — sema now infers + stashes bindings for module-scoped scope-resolution free calls
`mem::offset(p,n)`, `mem::swap<T>(a,b)`, `mem::transmute<F,T>(v)` etc. reach
`resolve_module_qualified_function` (callee is ScopeResolution, scope=module), which previously
returned the ABSTRACT template return and stashed nothing.
- New `infer_free_call_bindings(call, entry, tmpl)` — pure PASS A/B/C InferCtx inference, NO
  diagnostics (mono still owns E0214/E0306/E0307). Mirrors `check_generic_free_call`'s inference;
  the two converge when mono's engine is deleted (acknowledged temporary duplication).
- No-turbofish arg-inferred branch: infer → stash → `subst_free_call_return` for the concrete return.
- Turbofish branch: `subst_return_from_call_args` now takes `call` and stashes the resolved
  explicit args (covers `swap<T>`/`transmute<F,T>`, incl. void return).

### Measured impact (corpus = `build/cryo build` over the compiler, free calls)
ABSENT 9126 → **3454**; MATCH 374 → **5822**; MISMATCH **0**. `mem::offset` (4422) + `mem::swap`
(1046) fully covered.

### Remaining ABSENT (next)
- Type-scoped static methods: `Layout::array` (987), `Layout::of` (405), `Layout::try_array`
  (239), `Slice::from_raw` → **C1c** (static-method path; sema already has
  `infer_static_owner_return_from_args` for ctors — extend + stash).
- Bare free calls still absent: `sort_range_by`/`sort_*` (~1616), `alloc_entry` (90),
  `free_entry` (81), residual `swap` (36) — sema STASHes these on the template but mono's clone
  shows ABSENT. PUZZLE to root-cause (clone-source vs sema-stash-instance mismatch?). **C1b-resid**.
- Then **C1d**: mono discovery READS `resolved_type_args` (apply subst) + enqueues; inference only
  as fallback; drive ABSENT→0. THEN C2 deletes the engine.

### Files touched (uncommitted, cumulative this session)
expression.cryo (field+setter+ctor), sema.cryo (stash in check_generic_free_call +
infer_free_call_bindings + module-path hook + subst_return_from_call_args stash),
cloner.cryo (copy stash), substituter.cryo (substitute stash). monomorphizer.cryo back to baseline.

---

## 2026-06-18 (cont. 7b) — C1b-residual ANALYSIS (open puzzle, not yet cracked)

After C1a+C1b, corpus ABSENT = 3454 (down from 9126). Remaining classes:
- Type-scoped static methods (`Layout::array` 987, `Layout::of` 405, `Layout::try_array` 239,
  `Slice::from_raw`) — **C1c**, a different sema path (`infer_static_owner_return_from_args` +
  static-method-on-generic-template), almost certainly the same root cause as below.
- Nested bare free calls inside STDLIB generic bodies: `sort_range_by`/`sort_partition_by`/
  `sort_*` (~1616), `alloc_entry` (90), `free_entry` (81), residual `swap` (36).

### What the probes established (kept here so the fix doesn't re-derive it)
- The stash channel WORKS: cloner copies a non-empty stash **5731×**, substituter concretizes
  **5731×** → those are the MATCHes. `entry.ast_node` (cloned at `specializer.cryo:118`) is the
  ORIGINAL template node, so a stash on it propagates to every clone. Mechanism is sound.
- The ABSENT clones simply had **no stash on their template call node at clone time**.
- sema `check_generic_free_call` for `sort_partition_by`: ENTER 196× (192 `in_symbolic_check=N`,
  4 `=Y`), STASH 192×, ZERO `NOT-all-bound`. So the 4 PRE-MONO symbolic enters did NOT stash
  (and didn't bail via the printed all-bound path, and didn't emit a diagnostic — suite green).
  The 192 stashes are `symbolic=N` = post-mono Phase-6b (re-resolving concrete clones) → TOO LATE
  for mono's 6a-ii discovery.
- Net: the PRE-MONO pass is not producing a useful stash for calls nested inside stdlib generic
  bodies. `mem::offset` is covered only because C1b's hook lives in
  `resolve_module_qualified_function`, fired wherever the call is resolved.

### Leading hypothesis (verify FIRST next session)
The compiler build imports the PREBUILT stdlib. Open question: does sema's FunctionBodyTypeCheck
(Phase 6a-i.5) actually run its **symbolic generic-body walk over imported stdlib template
bodies**, or only over the current project's? If stdlib template bodies aren't symbolic-walked
pre-mono, their INTERNAL nested generic calls never get a pre-mono stash — exactly the observed
sort_*/alloc_entry/Layout pattern. (But `offset` inside `Array::get` IS covered, which seems to
cut against a blanket "stdlib bodies not walked" — so the truth is subtler; could be that only the
scope-resolution/module hook fires for stdlib bodies while the bare-Identifier keystone's stash
doesn't reach the cloned node. RESOLVE the offset-vs-sort asymmetry before designing the fix.)

### Suggested next probe (decisive, ~1 build)
Tag every `return`/exit of `check_generic_free_call` with an id, gated on `in_symbolic_check &&
ident.name=="sort_partition_by"`, to see which path the 4 symbolic enters take. AND confirm
whether sema's symbolic walk runs over the `std::...::sort` module at all during a compiler build
(print module name in `symbolic_check_body`). That pins the asymmetry.

### State at this checkpoint
- Tree GREEN (unit ok / compile-fail 99/0 at O0; O2 + self-host validated at the identical
  pre-strip code: IR md5 `940235d6efbf07033ffd4259ccb1803f`). All probes stripped; diff is the
  real C1a+C1b plumbing only (expression/sema/cloner/substituter). monomorphizer.cryo = baseline.
- UNCOMMITTED, NOT repinned (no bootstrap landmine; pinned compiler still builds everything).
- C1d (mono READS the stash + enqueues; inference as fallback) is unblocked for the COVERED
  classes already, but should wait until the residual is cracked so C2 can delete inference with
  ABSENT≈0. Until then mono's inference still runs and agrees (MISMATCH=0), so reading-or-inferring
  is behaviorally identical — C1d can land safely even with residual ABSENT, deferring full
  deletion to after C1c + residual.

---

## 2026-06-18 (cont. 8) — C1b-resid + C1c landed: free-call ABSENT 9126 → 207

**GREEN + SELF-HOST FIXED POINT.** Suite O0+O2 (unit ok / compile-fail 99/0). Self-host
byte-identical, IR md5 `e2ef668eff16882a5aa64770f4f1d1a4`. UNCOMMITTED, not repinned.

### Two root-caused fixes (both purely additive to the stash channel; MISMATCH stayed 0 throughout)
1. **Symbolic turbofish (cracked the sort_* residual).** `sort_range_by<T>`/`sort_partition_by<T>`
   (turbofish where `T` is the ENCLOSING template param) were resolved in
   `check_generic_free_call`'s turbofish branch via a fresh `ResolutionContext` WITHOUT
   `symbolic_bind_params`, so during the pre-mono symbolic walk `<T>` resolved to invalid → the
   binding was dropped → never stashed. Fix: `this.symbolic_bind_params(&rc)` in that branch (the
   inferred branch and `subst_return_from_call_args` already did this). Cleared sort_* (≈1616).
   The earlier "node-identity puzzle" was a red herring: the template node simply never got a stash.
2. **C1c — type-scoped generic static methods.** `Layout::of<T>()`, `Layout::array<T>(n)`,
   `Layout::try_array<T>` (generic METHOD on a NON-generic owner; registry stores them as
   `Owner::method` FunctionDeclaration templates). New `stash_scope_resolution_call_bindings`
   (additive, called after `resolve_scope_call` in the ScopeResolution branch) mirrors mono's
   type-scope template lookup, resolves the turbofish with `symbolic_bind_params` (or infers from
   args), and stashes. Cleared Layout::* (≈1631).

### Measured (corpus = `build/cryo build` over the compiler, free calls)
ABSENT 3454 → **207**; MATCH 7438 → **9069**; MISMATCH **0**.
Remaining ABSENT: `alloc_entry` (90), `free_entry` (81), `swap` (36) — all called inside
HashMap/`mem` via the `?` operator or a non-turbofish shape; likely a single shared cause
(`?`-lowering replacing the call node, or `this`-receiver inference in symbolic mode). Small,
isolated; next.

### Cumulative session diff (UNCOMMITTED)
expression.cryo (resolved_type_args field), sema.cryo (stash in check_generic_free_call +
infer_free_call_bindings + module-path hook + turbofish stash + symbolic_bind_params fix +
stash_scope_resolution_call_bindings + ScopeResolution hook), cloner.cryo (copy stash),
substituter.cryo (substitute stash). monomorphizer.cryo = baseline (all probes stripped).

### Critical-path status
C1 ~90% done (free-call classes). Left: the 207 residual (alloc_entry/free_entry/swap), then
**C1d** (mono READS resolved_type_args + enqueues; inference as fallback - behaviorally neutral
since MISMATCH=0), then **C2** (delete mono inference once ABSENT≈0), C3 (diagnostics), C4 (dual pass).

## 2026-06-18 (cont. 8b) — 207-residual diagnosed (alloc_entry/free_entry/swap)

Probed `alloc_entry` (`AEDBG`): in PRE-MONO symbolic mode it enters `check_generic_free_call`
(12×) but is **NOT-all-bound → never stashed**; the 180 stashes are `symbolic=N` (post-mono
Phase-6b, too late for mono). Signature: `alloc_entry<K,V,A>(map: mut &HashMap<K,V,A>, h, key, value)
where A: Allocator`. K binds from `key`, V from `value`, but **A is ONLY recoverable from `map`
(arg0 = `this`)**, and in symbolic mode the `this`/`map` arg doesn't expose a `HashMap<K,V,A>` with
a bindable `A` (the `where A: Allocator` BoundedParam doesn't unify out of the receiver type
symbolically). So `A` stays unbound → NOT-all-bound. `free_entry` is the same shape. `swap` (36)
is the orthogonal case: non-turbofish `mem::swap(call_expr, call_expr)` where the args are
CallExpressions that `free_infer_arg_reliable` deliberately defers.

### Fix direction for the residual (next session, not done)
- alloc_entry/free_entry: make the symbolic receiver (`this`/`map`) carry the owner's FULL generic
  args (incl. the allocator/bounded param) so unify binds `A` — investigate `sym_this` construction
  in `symbolic_check_owner_methods` / how `this`'s resolved_type is set in symbolic mode. Likely the
  cleanest general fix and may help other allocator-generic calls.
- swap residual: the call-expr-arg defer is sound conservatism; for the stash, mono's substituted
  clone DOES have concrete arg types, so this is a case where the stash legitimately can't come from
  the pre-mono walk. Acceptable to leave as an inference-fallback case at C1d, OR stash from the
  post-substitution clone walk.

### SESSION SUMMARY (2026-06-18 cont. 6-8) — C0 + C1a + C1b + C1c
Free-call mono inference now sema-authoritative for **97.7%** of call sites:
ABSENT 9126 → **207**, MATCH → 9069, **MISMATCH 0 throughout**. Suite green O0+O2 (1233?/0 unit,
99/0 cf). Self-host byte-identical fixed point, md5 `e2ef668eff16882a5aa64770f4f1d1a4`.
Landed (uncommitted, monomorphizer.cryo untouched/baseline):
- `CallExprNode.resolved_type_args` channel + cloner copy + substituter substitute.
- sema stash in `check_generic_free_call` (+ symbolic_bind_params turbofish fix).
- `infer_free_call_bindings` + module-scoped hook (`mem::offset`/`swap`/`transmute`).
- `subst_return_from_call_args` stash (turbofish module calls).
- `stash_scope_resolution_call_bindings` + ScopeResolution hook (type-scoped statics `Layout::*`).
NOT repinned (no bootstrap landmine). NEXT: 207 residual → C1d (mono reads stash) → C2 (delete engine).

## 2026-06-18 (cont. 9) — Step 1 DONE: free-call ABSENT 207 → 0 (3 root fixes)

**GREEN + SELF-HOST FIXED POINT.** Suite O0+O2 (unit ok / compile-fail 99/0). Self-host
byte-identical, IR md5 `022313d02c959af4dbd49799640c90aa` (moved from `e2ef668e`: sema now
resolves `this`/method/field types via the owner self-instantiation and stashes every free call;
still a fixed point → behavior self-consistent). UNCOMMITTED, not repinned (no bootstrap landmine).
`monomorphizer.cryo` remains at BASELINE (all probes stripped — zero debug code in the tree).

Jake's directive this session: **aggressive** mono-after-sema — no fallback/hacky-green
proliferation; fix the cause in the shared layer, not per-call-site. Saved as memory
`mono_after_sema_aggressive_2026_06_18`. The handoff's "supply the instantiated type only at the
unify site" idea was explicitly rejected as a per-site hack; all three fixes below are root-level.

### Measured (corpus = `compiler/ build`, free calls via `try_infer_function_call`)
ABSENT 207 → **0**; MATCH 9069 → **9276**; MISMATCH **0** throughout. The handoff's residual
(`alloc_entry` 90, `free_entry` 81, `swap` 36) is fully closed.

### Three root-caused fixes
1. **Symbolic owner self-instantiation (alloc_entry/free_entry, 171 calls).** During the symbolic
   generic-body walk `this` was typed as the BARE owner template (`HashMap`, kind=Struct), which
   carries no type-args — so `alloc_entry<K,V,A>(this,…)` could not recover the allocator `A` from
   the receiver (`A` is only bindable from `map: HashMap<K,V,A>`). Fix: new
   `symbolic_owner_instance` builds the owner's abstract self-instantiation `Owner<P0,P1,…>` (each
   param via `symbolic_param_ref`, tagged `is_symbolic`), and `symbolic_check_body` types `this`
   with it for the non-static generic-owner receiver. The shared `InferCtx` then binds `A`
   structurally via the existing Inst-vs-Inst path — NO unify special-case.
   - Supporting (the abstract-Inst `this` must resolve fields/methods like the bare template did):
     `symbolic_inst_generic_base` + `symbolic_recv_base` follow the never-monomorphized
     self-instantiation to its `generic_base` (where the template's field/method tables live);
     `symbolic_is_generic_owner_receiver`, `symbolic_resolve_owner_method_return` and
     `symbolic_resolve_owner_field` use it. The owner fast-paths in `resolve_method_call` /
     `resolve_member_access` were reordered AHEAD of the `symbolic_type_unresolved` bail (the
     self-instantiation legitimately contains abstract params, so the broad guard would otherwise
     defer it and method/field resolution returned `void`).
2. **InferCtx unify Pointer-formal vs Reference-actual ordering (swap, 36 calls).** `swap<T>(a: T*,
   b: T*)` called `swap(&this.field, …)`: the formal is `Pointer(T)`, but sema types `&x` as a
   `Reference`. The generic actual-is-Reference peel ran BEFORE the Pointer-formal block, stripping
   the reference and leaving `T*` vs the bare referent with no binding rule (the correct
   Pointer-vs-Reference cross-peel existed at the bottom of the Pointer block but was unreachable).
   Fix: moved the Pointer-formal block ahead of the actual-Reference peel. General win for any `T*`
   param inferred from an `&value` arg. (`inference.cryo`.)

### Cumulative session diff (UNCOMMITTED) — `git diff --stat`
expression.cryo / cloner.cryo / substituter.cryo (the stash channel, from cont. 6-8),
sema.cryo (cont. 6-8 stash producers + this session's symbolic owner-instance + recv-base helpers
+ owner fast-path reorders), inference.cryo (Pointer/Reference unify reorder).
monomorphizer.cryo = BASELINE.

### Critical-path status
C1 free-call coverage **100% (ABSENT 0, MISMATCH 0)**. NEXT: **C1d** — make mono READ
`call.resolved_type_args` (apply active `subst`) and skip PASS A/B/C; behaviorally neutral
(MISMATCH=0) so self-host must stay byte-identical. Then **C2** delete mono's inference engine,
**C3** delete mono's call-site diagnostics (E0214/E0306/E0307; sema is the oracle), **C4** collapse
the dual pass. SCOPE NOTE still open: METHOD-call inference (`try_infer_method_call`) was NOT
measured — needs its own stash producer in sema before that engine can be deleted.

## 2026-06-18 (cont. 10) — C1d landed: mono READS the free-call stash

**GREEN + FIXED POINT.** Suite O0+O2 (unit ok / cf 99/0). Self-host fixed point md5
`9c04ffff8d83e798f3d589db3f816f49` (moved from `022313d0` — see neutrality proof below).
UNCOMMITTED on top of `2b1b772a`.

### What landed (monomorphizer.cryo `try_infer_function_call`)
After sizing `inference_bindings`, if `call.resolved_type_args` is present and length-matches
`entry.param_type_ids`, seed the bindings from it (apply active `subst`, exactly as the validated
probe did) and set `from_stash=true`; PASS A/B/C are guarded by `!from_stash` and skipped. A
partial/invalid stash resets and falls back to inference. So mono now reads sema's bindings instead
of re-deriving them for every covered free call (ABSENT=0 → effectively all of them).

### Neutrality PROOF (important — corrects the handoff's "must stay byte-identical")
The self-host md5 MOVED (`022313d0`→`9c04ffff`, +8526 bytes). This is NOT a behavior change: the
compiler compiles itself, so adding C1d source to monomorphizer.cryo necessarily changes the
compiler's OWN IR. Verified neutral by:
- **Identical symbol set**: `comm` of all `define` symbols pre-C1d vs C1d = **0 new, 0 lost specs**.
- **Diff fully confined to one function**: all 44 IR diff hunks fall between the `define` of
  `try_infer_function_call` (old line 345489) and the next function `try_infer_method_call`
  (346826). `diff` reports every difference in the 50MB file; there are none elsewhere. The changes
  are literally my new SSA temps (`%from_stash`, `%b`, `%i312`…) + downstream per-function temp
  renumbering.
- Suite green O0+O2; fixed point holds.
**Takeaway for C2/C3:** deleting compiler source will likewise move the md5 (compiler shrinks). The
correctness oracle is **fixed-point + identical symbol set (per `define` diff) + changes confined to
the edited functions**, NOT md5 stability.

### NEXT
M0: measure method-call inference (`try_infer_method_call`) to size the method-side stash work
(turbofish vs formal/actual vs combinator/derived). Then M1 (sema method stash) → C2 (delete the now
mostly-dead inference engine). Free-call inference body in `try_infer_function_call` is now dead for
ABSENT=0 but its shared helpers (`resolve_arg_type_for_inference`, `unify_for_inference`,
`inference_bindings`) are still used by the method engine, so deletion waits on the method side.

## 2026-06-18 (cont. 11) — M0: method-call inference measured

Probed `try_infer_method_call` at the specialize point, classifying each minted method spec.

### Full test suite (`cryo test`): 175 method specs
- **58 turbofish** (`cast`, some `spawn`) — sema-trivial (resolve the annotation).
- **102 plain formal/actual unify** (`fmt`, `map`, `fold`, `hash`, `next`, `zip`, `chain`…) — same
  unify machinery as free calls.
- **16 derived/combinator** (`map`/`fold` on adapters; `impl_node.derived_param_names` non-empty) —
  the hard case: the element type `A` comes from the impl's where-bound element binding computed at
  mono impl-specialization time. May be tractable in sema IF sema has the receiver's concrete
  element type (the "derived" complexity is a mono artifact of working from where-bounds, not the
  concrete receiver) — UNKNOWN until built.
- **2 implicit** return-type-driven.
Compiler-only corpus: 86 specs, all turbofish(72 `cast`)/unify(14 `hash`/`fmt`), 0 derived.

### Read
~91% of method-generic inference (turbofish + plain unify) is sema-tractable with the free-call
machinery. ~9% derived/combinator is the open question for full C2 deletion. NOTE: own-generic
combinators reach this path only after `try_instantiate_self_returning_default` returns false;
zero-own-generic combinators (take/filter) are handled earlier and never hit method inference.

### Decision
Pivot to deleting the FREE-CALL inference body first (C2/C3 free-call portion): C1d's `from_stash`
covers all free calls (ABSENT=0), and sema already owns the free-call diagnostics
(E0214/E0306/E0307 via `emit_free_*`). Validate the body is dead across the FULL corpus, then delete
+ run compile-fail (the diagnostic oracle) + self-host symbol-set diff.

## 2026-06-18 (cont. 12) — C2-free attempt: REVERTED (uncovered 2 deeper blockers)

Tried to make the free-call inference body deletable by closing the last test-corpus fallbacks.
Measured mono's free-call inference RELIANCE (the calls where sema's stash is ABSENT so mono's
PASS A/B/C still pins): **compiler 0, full test suite 7** — `expect_eq` (4), `from_iter` (2),
`litexpr_box` (1). All are the "call-expression argument" shape (`litexpr_box(take_i64(x))`,
`expect_eq(classify(...), 1)`), which `free_infer_arg_reliable` blanket-defers.

Attempted root fix: make a CONCRETE call-expr arg reliable (sema-authoritative). This **regressed**
and was REVERTED after uncovering two genuine, deeper issues that must be fixed FIRST:

1. **sema PASS-C literal conflict diverges from mono.** A polymorphic numeric literal meeting a
   concrete binding: sema runs `check_compatibility(i32_default, bnd)` and flags i32-vs-i64 (or
   i32-vs-enum) as a conflict; mono NEVER flags a literal here (its inference-actual is invalid →
   skips). Fixing "numeric literal adopts any numeric binding" got expect_eq's i64 cases, but an
   int literal must ALSO be allowed to adopt an ENUM (enums hold int discriminants) and NOT a string
   — the discriminator is literal-adoptability, not `is_numeric`. The only divergent case sema must
   still catch that mono doesn't is `second(5, "hello")` (T=string). Needs a literal-aware
   adoptability check (or align both passes to one rule).
2. **Cross-module same-leaf call mis-resolution (the deferral's original reason).** `expect_eq(
   classify(Outer::Wrap(Inner::A)), 1)` in nested_patterns: the LOCAL `classify` returns `i32`, but
   the inference path saw `PatternMatching::Sign` (a same-leaf `classify` in another module). The
   original `free_infer_arg_reliable` comment explicitly defers call-expr args because "a bare-name
   call can mis-resolve to a same-leaf sibling in another namespace" — this is that hazard, live.
   Relaxing the defer exposes it; mono tolerates it today only because it re-infers per-instantiation.

**Decision (per Jake's philosophy):** the relaxation was not a clean, validated step — it pulled in
an unrelated dispatch bug and a literal-semantics divergence. Reverted to C1d-only. Both blockers are
real and worth fixing on their own (then C2-free becomes safe), but not as a rushed change.

### State after revert
C1d only (monomorphizer.cryo, +32 lines). sema.cryo CLEAN. Suite green O0+O2 + full `cryo test`.
Self-host fixed point (expected md5 `9c04ffff...`, the validated C1d point). Zero probe residue.

## 2026-06-18 (cont. 13) — #8 DONE: sema/mono PASS-C literal adoptability aligned

**GREEN + FIXED POINT.** Suite O0+O2 (unit ok / cf 99/0) + full `cryo test` PASS. Self-host
byte-identical fixed point, IR md5 `02124ef0f2467176711a9c95c1ced804` (moved from `9c04ffff` — the
compiler's OWN source changed, expected; neutrality argued below). UNCOMMITTED on top of `2b1b772a`.

### The bug (handoff prereq #8)
Both sema `check_generic_free_call` PASS C and mono PASS C (free + method-owner) tested a polymorphic
numeric literal against an already-pinned binding with a RAW
`check_compatibility(i32_default, bnd) == Incompatible`. That wrongly flags **i32-vs-i64** and
**i32-vs-enum** as a `T` conflict, whereas a width-less literal legitimately adopts any numeric
binding (codegen coerces) and any enum binding (an int literal names a discriminant). This divergence
(mono never reached the check for the call-expr-arg shape because its inference-actual came back
invalid) is exactly what broke the cont. 12 `free_infer_arg_reliable` relaxation.

### The fix (shared rule, both passes)
Added `literal_adopts_binding(bnd) -> boolean` to BOTH sema (passes/sema.cryo, after
`free_infer_type_concrete`) and mono (types/monomorphizer.cryo, after `infer_type_is_concrete`):
true iff `bnd`'s resolved kind is `Int`, `Float`, or `Enum`. Replaced the `check_compatibility`
conflict test at all THREE PASS-C sites (sema free-call ~6501, mono free-call ~4415, mono
method-owner ~5076) with `infer_type_is_concrete(bnd) && !literal_adopts_binding(bnd)`. So a literal
meeting a string/bool/struct binding still hard-conflicts (`second(5, "hello")` → E0214), but numeric
width and enum cases no longer false-positive.

### Why neutral (no current-suite behavior change)
`pick(i32_var, i64_var)` (E0214_generic_numeric_conflict) is caught in PASS A (two non-literals),
untouched. `second(5,"hello")` keeps flagging (string not adoptable). The method E0214 negatives
(`cf_bump`/`cf_absorb`/`cf_of`) pass a `string` to a CONCRETE `i32` param — not generic inference, not
a polymorphic literal — untouched. Any actual behavior change would have flipped a positive test
(spurious E0214) or a negative test (suppressed E0214); all 99 cf + unit + full project stay green, so
the monomorphized symbol set is necessarily consistent. (This change emits/suppresses a DIAGNOSTIC; it
cannot add or drop a spec.)

### Unblocks
This is prereq #8 for #7 (C2-free). #9 (cross-module same-leaf call misresolution) still pending before
the `free_infer_arg_reliable` relaxation is safe.

### NEXT
Per handoff dependency order: #9, then #7 (C2-free). In parallel, M1 (#3) is independently startable
and neutral — sema's `resolve_generic_method_return` (sema.cryo ~10232) ALREADY computes
`method_bindings` (the method's own generic params in decl order) via the same turbofish/unify
machinery mono's `try_infer_method_call` uses, so the method-stash producer can piggyback on it.
GAP found: methods whose generics appear ONLY in arg positions (`hash<H> -> void`) have no return-type
reason for sema to infer them today → need a dedicated binding-compute+stash to reach ABSENT 0.

## 2026-06-18 (cont. 14) — M1 step 1+2 DONE: mono reads sema's method-call stash (combinator path)

**GREEN + FIXED POINT + SYMBOL-SET IDENTICAL.** Suite O0+O2 (cf 99/0) + full `cryo test` PASS.
Self-host byte-identical fixed point IR md5 `27d3ca4a27fd3d3cf4d1907278acd713`. **Define-symbol set vs
pre-M1 baseline: 0 new / 0 lost** (16697 == 16697) — proves the stash-read agrees with mono's
inference everywhere it fires (MISMATCH 0). UNCOMMITTED on top of `2b1b772a`.

### What landed (the method analogue of C1d)
- **Producer (sema, `resolve_generic_method_return` ~10462):** right before `return refined` (where
  every `method_bindings[i]` is already proven concrete), stash them on `call.resolved_type_args` in
  `orig_func.generic_params` declaration order. Mirrors the free-call producer in
  `check_generic_free_call`. Reuses the SAME field (a CallExpr is free XOR method, never both).
- **Reader (mono, `try_infer_method_call` ~5948):** after sizing `inference_bindings`/`method_ctx`,
  if `call.resolved_type_args.length == n_method_params`, seed `inference_bindings` from it (apply
  `subst`; concrete today so a no-op), set `m_from_stash=true`, and make the turbofish/unify block an
  `else if`/`else` under `if (m_from_stash) {}`. Partial/invalid stash resets → falls back to
  inference. The downstream concreteness-defer + `specialize_method` are unchanged (run for all paths).

### Coverage measured (TEMP probe `MSTASH`/`MABSENT` at the specialize point, since STRIPPED)
- **Compiler corpus:** MSTASH 0 / MABSENT 86 (72 `cast`, 8 `hash`, 6 `fmt`).
- **Full `cryo test` corpus:** MSTASH **56** / MABSENT **119**.
  - STASHED (sema-covered): `map` 22, `fold` 15, `zip` 8, `chain` 7, `next` 2, `wrap` 2 — exactly the
    combinator / return-carrying-method-generic family that flows through
    `resolve_generic_method_return`.
  - ABSENT (still mono-inferred): `fmt` 38, `cast` 38, `hash` 9, `run` 7, `sample` 6, `next` 6,
    `try_spawn` 4, `spawn` 4, `relay` 4, `write_to` 2, `wrap` 1.

### Why the 119 are absent (and the exact plan to close them) — this is the rest of M1
sema's `resolve_method_call` routes these AROUND `resolve_generic_method_return`, so my piggyback
producer never sees them:
1. **Turbofish** (`cast<U>`, `spawn`/`try_spawn`): `member.generic_args.length > 0` → resolved via
   `resolve_method_return_with_explicit_args`, not `resolve_generic_method_return`. The bindings are
   the turbofish args themselves — fully authoritative, trivially stashable.
2. **Arg-only generics** (`hash<H> -> void`, `fmt<W>`, `write_to`, `relay`, `run`, `sample`): the
   method generic appears ONLY in a parameter, never the return, so `contains_generic_param(refined)`
   is false and `resolve_generic_method_return` is never called. Needs formal/actual unify run for
   its side-effect (stash) even though the return is already concrete.
**Plan (next):** refactor the binding-computation out of `resolve_generic_method_return` into a
`compute_method_bindings(recv_type, member, call) -> TypeRef[]` (turbofish branch + unify branch +
implicit-return branch — code already exists there), then call it from a single
`stash_method_call_bindings` invoked for EVERY generic method call in `resolve_method_call`
(regardless of return shape), stashing whenever the full set is concrete. Method resolution is
receiver-anchored, so the cross-module same-leaf hazard (#9) that blocks the FREE-call relaxation does
NOT apply here. Re-measure to drive MABSENT→0; the genuinely-irreducible residual (if any) is the
derived/combinator element-binding case the handoff flagged — surface it to Jake rather than force it.

### NOTE on the symbol-set oracle
The compiler self-build has MSTASH 0 (no combinator generic-method calls), so the 0/0 symbol-set diff
chiefly proves the READER is inert when nothing stashes. Neutrality of the 56 STASHED test-corpus
calls is proven by the FULL GREEN suite (a wrong-method stash → miscompile → a red test); all green.

### State
Clean tree (zero probe residue). Changed vs HEAD: `monomorphizer.cryo` (C1d + #8 + M1 reader),
`sema.cryo` (#8 + M1 producer), `pipeline-reorder-progress.md`. UNCOMMITTED, NOT repinned (Jake owns).
