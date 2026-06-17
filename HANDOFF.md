# HANDOFF — Pipeline Reorder (Monomorphization AFTER Semantic Analysis)

You are continuing a deliberate, multi-week architectural change to the CryoLang
self-hosted compiler: **make sema (type-checking) run BEFORE monomorphization
(specialization)**, so sema becomes the single source of truth for resolved
types and mono's private inference layer can eventually be deleted.

This file is the entry point. The live, detailed tracker is
**`pipeline-reorder-progress.md`** at the repo root — read it after this. Read
this whole file before touching code; several traps will cost you hours.

**Working tree is UNCOMMITTED.** Jake owns commits + pinning. Current modified
files: `compiler/src/compiler/passes/sema.cryo`,
`compiler/src/compiler/diag/sink.cryo`, `pipeline-reorder-progress.md`. The tree
is GREEN and a byte-identical self-host fixed point right now.

---

## 0. The one-paragraph mental model

Today the pipeline is `… TypeResolution → StructFieldTypeSync → Monomorphization
→ GenericExpressionResolution → GenericValidation → FunctionBodyTypeCheck(=sema)
→ MoveCheck → DropInsertion → …`. Sema runs AFTER mono and only checks the
*concrete monomorphized output* (it skips generic templates via `is_generic()`
guards). Because sema hasn't run when mono runs, mono carries its own duplicate,
lower-fidelity type-inference engine to decide what to specialize — that
duplication is what we are removing. End state: sema type-checks generic bodies
**symbolically** (with `T`/`This`/projections abstract), mono consumes sema's
resolved types, and mono's inference layer is deleted.

The de-risking vehicle is the **symbolic generic-body walk**: a walk of generic
templates with type params abstract, currently gated behind env
`CRYO_SYMBOLIC_CHECK` and run under diagnostic *suppression* (it measures what it
WOULD diagnose). Once it is clean (0 would-emit on all valid generic code) AND
side-effect-free, the order can be flipped.

---

## 1. STATUS

### DONE & VALIDATED (uncommitted, byte-identical self-host)
- **Phase 0/1** (committed by Jake earlier): baseline + `TraitChecker` extraction
  severing sema's runtime dependency on mono.
- **Phase 2 — symbolic walk built & FP-free (1673 bodies / 0 would-emit / exit 0):**
  - Free fns + methods on non-generic owners + generic struct/class/impl owner
    methods all walked symbolically with `T`/`This` abstract, under suppression.
  - **10 residual FPs root-caused & fixed** (see tracker "Phase 2 — 10 residual
    FPs"): (a) explicit method generic-arg poison in
    `resolve_method_return_with_explicit_args` — fixed by resolving explicit args
    in a FRESH context in symbolic mode and deferring if abstract; (b) a
    match-arm-binding assignment FP — fixed by guarding the DeclStmt assignment
    check with `symbolic_defer_type`. Bonus: this killed the multi-module E0900
    leak (deferring abstract calls stops the walk creating stray instantiations).
  - **`this.field` resolves to abstract field type** (`symbolic_resolve_owner_field`)
    — reads the owner template AST `FieldDeclNode.resolved_type` because generic
    templates have EMPTY arena `FieldInfo[]` (`run_struct_field_sync` skips
    `is_generic()`). Has teeth (catches bogus fields).
  - **`this.method()` resolves to abstract return** (`symbolic_resolve_owner_method_return`)
    — reads the owner template AST `MethodNode.func.resolved_return_type`. Has teeth.
  - Last good self-host IR md5: `a7dd0530b91e03fc79cbaa1c238cdacb`.

### ATTEMPTED & REVERTED (this is the key learning — read before re-trying)
- **The "bridge"** (un-suppress the walk so it emits REAL diagnostics in
  production, keeping current pass order) was built and **reverted**. It cascaded
  through FP classes that pinpoint the true blocker:
  1. **Cross-unit name-collision FPs.** Un-suppressed, a stdlib generic body
     compiled alongside the test corpus mis-resolves a bare param annotation:
     `mut alloc: A` in `rwlock.cryo` resolved `A` to an UNRELATED test type via
     global name lookup. ⇒ "0 would-emit on stdlib alone" is necessary but NOT
     sufficient. Fix = bind enclosing params to abstract `GenericParam`s in the
     walk's resolution contexts (`symbolic_bind_params`).
  2. **But binding re-introduces E0900 (the Phase-4 wall).** Resolving an abstract
     `Pair<T,V>` routes through `resolve_generic → generic_registry.instantiate_for_module`,
     which caches an `InstantiatedType`; `collect_unmonomorphized` (scans the
     registry cache) then flags it as un-monomorphized. So binding trades the
     name-collision FP for arena pollution.
  3. Lesser classes: destructure-of-abstract-owner (E0361); match-arm binding off
     an abstract scrutinee resolving to a wrong concrete type (FmtError-vs-boolean).

  Reverted cleanly — post-revert self-host reproduced the EXACT md5 above.

### NOT STARTED
- Phase 3 (flip single-module pipeline order), Phase 4 (multi-module
  orchestrator), Phase 5 (delete mono's inference engine).

---

## 2. ⚠️ Validation loop — exact mechanics (NON-NEGOTIABLE)

Native **Windows** host; build bootstrapped from the committed pin `bin/cryo(.exe)`.
Self-host runs through **WSL** (has `cryo` + `gcc`).

- **Build/test from PowerShell** (so `make` uses the cmd recipe shell). Git Bash's
  `make` runs the cmd recipe under `sh` → `syntax error`. If you used the Bash
  tool and `cd`'d, PowerShell CWD can drift — prefix with
  `Set-Location 'C:\Programming\apps\CryoLang';`.
  ```
  $env:CRYO_CC='gcc'; make cryo        # stage-2 -> compiler/build/cryo.exe
  $env:CRYO_CC='gcc'; make test        # unit + compile-fail (expect 99 pass)
  ```
- **Self-host byte-identity gate (WSL), run in the BACKGROUND (~4 min):**
  ```
  wsl bash -lc "cd /mnt/c/Programming/apps/CryoLang && export CRYO_CC=gcc && \
    unset CRYO_SYMBOLIC_CHECK && python3 scripts/selfhost-check.py --no-windows"
  ```
  Pass = `✓ FIXED POINT OK`. NOTE: a successful self-host WIPES `compiler/build/`
  and rebuilds the *Linux* `build/cryo`, deleting your Windows `cryo.exe` — re-run
  `make cryo` afterward to get `.exe` back.
- **RUN build / test / self-host STRICTLY SERIALLY, and do NOT edit source or run
  any build while a self-host is running.** They share the `stdlib/.bin` +
  `compiler/build` caches; a concurrent `rm -rf .bin` / build corrupts the run.
  (Learned the hard way — cost a full self-host run.)
- **Gate-ON measurement** (find/measure FPs): set `CRYO_SYMBOLIC_CHECK=1` and run
  the freshly-built `compiler/build/cryo.exe`. Clean corpus = stdlib rebuild:
  ```
  cd stdlib && rm -rf .bin && \
    CRYO_CC=gcc CRYO_SYMBOLIC_CHECK=1 ../compiler/build/cryo.exe build 2>e.txt
  grep cumulative e.txt | tail -1     # bodies walked + total would-emit
  ```
  A gate-ON stdlib build currently EXITS 0 (no E0900). It leaves `stdlib/.bin`
  in a special state — run `make cryo` (gate-off, via pin) afterward to restore a
  clean `.bin` before `make test`. Single-file builds
  (`cryo build foo.cryo --stdlib=$(pwd)/stdlib -o /tmp/foo`) are the cleanest
  measurement vehicle.
- To see WHICH diagnostics the suppressed walk would emit, temporarily re-add a
  dump inside `DiagnosticSink::emit` (`diag/sink.cryo`) in the
  `if (this.suppress_depth > 0)` branch:
  ```
  intrinsics::fprintf(g_stderr, "[symbolic-emit] E%u %s:%u %s\n",
      diag.code.to_u32(), diag.span.file, diag.span.start_line, diag.message);
  intrinsics::fflush(g_stderr);
  ```
  Remove it before committing.

---

## 3. ⚠️⚠️ Compiler-authoring landmines (compile fine, fail at runtime)

Memory file: `cryo-authoring-gotchas.md`.
1. **Never add fields to a `type class` with a vtable** (e.g. `TypeCheckVisitor`).
   It corrupts an aliased field via a field-offset/vtable miscompile → segfault.
   Keep mutable pass state in file-scope `mut g_*` globals (see the `g_symbolic_*`
   globals near the top of `sema.cryo`).
2. **Non-zero global initializers are NOT honored** — globals are zero-init. Use 0
   as the "unread" sentinel (see `g_symbolic_check_gate`: 0=unread/1=off/2=on).
3. **`fmt::eprintln(text: String)` vs `format()->string` is a silent type
   confusion** → segfault. For printf-style logging from compiler code use
   `fmt::eprintf(fmt: string, args...)`.

---

## 4. How the symbolic checker works (so you can extend it)

All in `compiler/src/compiler/passes/sema.cryo` unless noted. Search the symbols
below — line numbers drift.

**File-scope globals (top of file):** `g_symbolic_check_gate` (env cache),
`g_in_symbolic_check` (true during a walk), `g_symbolic_generic_param_ids`
(intern ids of in-scope params), `g_symbolic_owner_param_ids`,
`g_symbolic_owner_is_generic`, `g_symbolic_bodies_walked` /
`g_symbolic_total_would_emit` (measurement).

**Suppression API** (`diag/sink.cryo`): `begin_suppress`/`end_suppress`/
`suppressed_error_total`/`reset_suppressed`. While `suppress_depth>0`, `emit()`
counts errors and discards them.

**Entry points:** `symbolic_check_body(func, this_type, owner_type)` (core walk
under suppression); `symbolic_check_owner_methods(...)` (walks all methods of a
generic owner). Wired into the `is_generic()` skip sites in `visit(FunctionDeclNode)`,
`visit_methods`, `visit(StructDeclNode/ClassDeclNode/ImplBlockNode)`. Pass
summary printed in `run_function_body_type_check`.

**Defer predicates (the heart of "defer what needs concreteness"):**
`symbolic_defer_type(ty)`, `symbolic_type_unresolved(ty)` (invalid/`void`/contains
generic param), `symbolic_name_is_generic_param(sym)`,
`symbolic_is_generic_owner_receiver(ty)` (ty's base == current generic owner).

**Abstract resolution helpers (this session):**
`symbolic_resolve_owner_field(recv, field)` and
`symbolic_resolve_owner_method_return(recv, method)` — read the owner template AST
(via `generic_registry.get_template_by_type_id(base.id)`) for the abstract field
type / method return. Wired into `resolve_member_access` and
`resolve_method_call`. Unfound members defer (no FP).

**Defer points already added** (each gated on symbolic mode → gate-off no-op):
`visit(ReturnStmtNode)`, `resolve_scope_call`, `resolve_member_access`,
`resolve_method_call`, `check_method_call_arg_types`, `resolve_unary`,
`check_assignment_lvalue`, `resolve_binary`, `visit(StaticMatchStmtNode)`,
the `?`-operator resolver, `resolve_array_access`, `visit(DeclStmtNode)`
assignment check.

---

## 5. THE BLOCKER (do this first) — demand-free abstract resolution

Both the bridge and the flip are blocked on the same thing: the walk must resolve
generic-param-bearing annotations to ABSTRACT types **without creating
registry-cached `InstantiatedType`s** (which leak as un-monomorphized → E0900).

**CONFIRMED FIX PATH** (verified by reading the code, not yet implemented):
- `arena.create_instantiation` (`types/arena.cryo` ~line 509) builds the
  `InstantiatedType` + dedups via `instantiated_cache` and **does NOT touch the
  `generic_registry`**.
- `generic_registry.collect_unmonomorphized` (`types/generic_registry.cryo`) scans
  only the registry `cache_keys` — so arena-only instantiations are invisible to
  `GenericValidation`. No E0900.
- Plan: add a `symbolic_no_demand: boolean` field to `ResolutionContext`
  (`types/resolver.cryo`, default false in `new`/`clone`). In `resolve_generic`,
  when the flag is set, return `arena.create_instantiation(base, args)` instead of
  `generic_registry.instantiate_for_module(...)`. The walk sets the flag on its
  resolution contexts.
- Then re-add `symbolic_bind_params(ctx)` (bind each in-scope param name to its
  abstract `GenericParam` TypeRef via `arena.create_generic_param(name, index)`,
  index = position in its own list to match `create_generic_param_types`) so bare
  `A` resolves to the abstract param, not a global same-named type. Apply it at
  the body-level annotation-resolution sites: `visit(DeclStmtNode)` lazy-resolve,
  `visit(DestructureDeclNode)`, `resolve_lambda`, `resolve_generic_scope_name`.

**VALIDATION for this step:** with binding + demand-free both on, a gate-ON
**stdlib** build must stay exit 0 (no E0900) and ideally 0 would-emit. Binding
*without* demand-free WILL produce E0900 — that's the proof the demand-free path
is doing its job. EXPECT the binding to re-surface the lesser FP classes from §1
(destructure E0361, match-arm-binding wrong type) as would-emit; close each with a
symbolic defer (the destructure defer: in `visit(DestructureDeclNode)`, if
`symbolic_type_unresolved(node.resolved_type)`, register the bindings void and
return before the field check).

---

## 6. Recommended next increments (in order; each ends green + byte-identical)

1. **Demand-free abstract resolution + param binding** (§5). Land it under
   suppression first (still measurement) — validate gate-ON stdlib exit 0 + make
   test 99 + self-host byte-identical. This is the keystone.
2. **Close the re-surfaced FP cascade** (destructure, match-arm binding, any
   others) to get back to 0 would-emit WITH binding on.
3. **The bridge:** un-suppress the walk (make `sema_symbolic_check_enabled`
   default-ON with an env kill-switch; remove `begin/end_suppress` in
   `symbolic_check_body`; keep `g_in_symbolic_check`). Acceptance = make test 99 +
   self-host byte-identical. Ships generic-template checking as a real feature.
   NOTE: only 12 of 99 compile-fail tests use generics, and most assert
   *concrete-instantiation* errors the walk defers on — so the bridge should be
   close once the cascade is closed.
4. **Phase 3 — flip single-module order.** Reorder `build_standard_pipeline` /
   `build_frontend_pipeline` / `build_raw_pipeline` (`passes/pass_registry.cryo`)
   so `FunctionBodyTypeCheck` precedes `Monomorphization`; keep `MoveCheck` /
   `DropInsertion` AFTER mono (they need concrete code). Rewire the provision DAG
   in `get_pass_metadata` (`passes/pass_id.cryo`): `FunctionBodyTypeCheck` requires
   `StructFieldsSynced` (not `GenericsValidated`); `Monomorphization` may require
   `BodiesTypeChecked`. CORE OBSTACLE: today sema checks the post-mono CONCRETE
   output; after the flip only templates + non-generic bodies are checked, so the
   pre-mono check must catch the post-mono error set. Run it as an EXPERIMENT
   first — apply the reorder, `make test`, record which compile-fail tests
   regress (that quantifies the gap), then close the gaps (instantiation-site
   trait-bound checking is the main lever).
5. **Phase 4 — multi-module orchestrator** (`instance.cryo`): mono runs per-module
   in Phase 6a-ii (~1724-1728), sema in Phase 6b (~1924-1928). Hoist
   `FunctionBodyTypeCheck` ahead of the per-module mono interleave.
6. **Phase 5 — delete mono's inference engine**: `try_infer_function_call`/
   `try_infer_method_call`, `resolve_arg_type_for_inference`,
   `collect_locals_in_block/stmt`, `lookup_local_type`, scratch stacks
   (`types/monomorphizer.cryo`, roughly lines 251-306, 858-1171, 2552-3700). Mono
   reads sema's AST-annotated resolved types instead.

---

## 7. File map

- `compiler/src/compiler/passes/sema.cryo` — `FunctionBodyTypeCheck`; the symbolic
  harness + all defer guards + the `symbolic_resolve_owner_field/method_return`
  helpers. The `is_generic()` skip sites are where coverage lives.
- `compiler/src/compiler/diag/sink.cryo` — `DiagnosticSink` + suppression API.
- `compiler/src/compiler/types/resolver.cryo` — `TypeResolver`, `ResolutionContext`
  (add the `symbolic_no_demand` flag here), `resolve_generic` (~1135),
  `resolve_named` (~919).
- `compiler/src/compiler/types/arena.cryo` — `create_instantiation` (~509,
  registry-free), `create_generic_param`.
- `compiler/src/compiler/types/generic_registry.cryo` — `instantiate_for_module`
  (~667, the demand site), `collect_unmonomorphized` (~718).
- `compiler/src/compiler/passes/type_resolution.cryo` — `run_struct_field_sync`
  (~2710, the `is_generic()` skip that empties template fields/methods),
  `bind_generic_params` / `make_generic_context` / `create_generic_param_types`.
- `compiler/src/compiler/passes/pass_registry.cryo` — 3 pipeline builders (ORDER).
- `compiler/src/compiler/passes/pass_id.cryo` — `get_pass_metadata` (provision DAG).
- `compiler/src/compiler/instance.cryo` — multi-module orchestrator (Phase 4 wall).
- `compiler/src/compiler/types/monomorphizer.cryo` — the inference layer to delete
  (Phase 5); the consumer that must learn to read sema's results.
- `pipeline-reorder-progress.md` (repo root) — the running tracker; update it
  every phase.

---

## 8. Working agreement & first steps

- **Jake owns commits and pinning.** Do not `git commit` or `make pin` unless
  asked. No repin is needed for the current state (gate-off is byte-identical
  through the pin). When a repin becomes necessary, surface it with the reason.
- **Honest signal over green-by-skip.** A silently no-opped check is a regression
  even if the suite passes. Defer (and report) what needs concreteness; never
  delete/skip a test to go green.
- **Keep `pipeline-reorder-progress.md` updated** as you go.

**First steps for the new agent:**
1. `git status`; read `pipeline-reorder-progress.md` (esp. the "BRIDGE EXPERIMENT"
   and "FLIP-READINESS ASSESSMENT" sections).
2. From PowerShell: `$env:CRYO_CC='gcc'; make cryo; make test` — confirm green
   (compile-fail 99).
3. WSL: `python3 scripts/selfhost-check.py --no-windows` — confirm FIXED POINT
   (expect md5 `a7dd0530b91e03fc79cbaa1c238cdacb`), then `make cryo` to restore
   `cryo.exe`.
4. Start increment #1 of §6 (demand-free abstract resolution, §5).

You have a clean, validated checkpoint to build on. When it fights back: reduce to
the smallest repro, root-cause, and keep the self-host a fixed point.
