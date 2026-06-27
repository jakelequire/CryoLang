# Allocator-aware `T[]` — per-target arena for the whole frontend heap

> Live tracker for the arena-everything memory refactor. Update the **Progress** table as stages land.

## Progress

| Stage | Description | Status | Notes |
|-------|-------------|--------|-------|
| AST arena | `new ASTNode` → arena, reset between targets | ✅ DONE + PINNED | Reclaims ~130 MiB; byte-identical; committed by maintainer |
| 0 | stdlib infra (globals + `GlobalAlloc` consult + `Arena::owns`/`reallocate`), behavior-neutral | ✅ DONE (UNPINNED) | 2026-06-27. Build + `make test` (114 cf + 4 proj) + `selfhost-check` **FIXED POINT byte-identical Linux+Windows**. See **Stage 0 structure note** below. Re-pin optional (behavior-neutral, pinned boot compiles new source fine) — defer to Stage 2 measurement. |
| 1 | wire region (publish `g_ast_arena`), inert (active=false) | ✅ DONE (UNPINNED) | 2026-06-27. `get_ast_arena` calls `set_alloc_arena`. Byte-identical. |
| 2 | activate around the pipeline — **the RSS win**; measure | ⚠️ DONE but UNDER-TARGET (UNPINNED) | 2026-06-27. All gates green, selfhost byte-identical Linux+Windows with arena ACTIVE. **Peak RSS 2.48 → 2.30 GB (−174 MiB, ~7%) — far short of 1.1–1.3 GB.** Root cause measured: arena captures only **~500 MiB/target** (used≈capacity, NOT stranding-inflated); ~600 MiB/target is OUTSIDE the arena → **Stage 4 (LLVM) is required**, plus earlier activation. See **Stage 2 measurement** below. |
| 3 | harden reset ordering + multi-target loop | ✅ DONE (folded into Stage 2) | Activation bracketed in `compile_project_with_ctx`; deactivation on success + all 12 failure paths (`project_failure_release`) + defensive reset-site reset. Multi-target lib→bin loop verified (selfhost builds lib+bin). |
| 4 | (complementary) per-target LLVM context | ✅ DONE but ~0 RSS WIN (UNPINNED) | 2026-06-27. Correct + byte-identical (Linux+Windows). **Peak 2.30 → 2.30 GB (no change).** A/B proves LLVM does NOT stack across targets — the Phase-7 per-module `LLVMDisposeModule` already reclaims it; only small type-tables remained. Premise (LLVM ~385 MiB/target stacking) was WRONG. See **Stage 4 measurement** below. Keep as hygiene or revert — maintainer's call. |
| 5 | (optional) route the 3 inline array emitters | ⬜ TODO | won't matter — see strategic fork |

**Baseline peak RSS (measured, WSL):** ~2.48 GB. **Stage 2:** ~2.30 GB. **Stage 4:** ~2.30 GB. **Target:** ~1.1–1.3 GB.

## Stage 4 measurement (2026-06-27) — LLVM was a red herring; the wall is direct libc

- **Peak RSS 2,304,688 KB (~2.30 GB) — unchanged from Stage 2** (2,303,216). The per-target LLVM
  context is correct (selfhost byte-identical, no context-mismatch crash) but reclaims nothing
  measurable: disposing lib's context before bin builds frees ~0 because the Phase-7 loop already
  `LLVMDisposeModule`s each module right after its codegen (instance.cryo:1930-1934), so the global
  context only ever held the (small) accumulated type tables, not the ~385 MiB. That 385 was the
  *within-target codegen peak* (live module IR), which never stacked across targets to begin with.
- **Composition (from Stage 2 + Stage 4 A/B):** per-target peak ~1.24 GB = ~500 MiB arena (reclaimed
  between targets ✓) + ~740 MiB **libc the arena cannot reach**. The cross-target stacking that makes
  the 2-target peak ~2.30 GB instead of ~1.24 GB is lib's **non-arena libc** still resident during
  bin's codegen.
- **Why the arena can't reach it — two structural walls:**
  1. **Born-before-activation (realloc stays libc).** Any container first allocated before the Phase-2
     activation — `InternTable.strings[]` (seeded by `push("")` in `InternTable::new` at `ctx::new`),
     `ModuleGraph`, etc. — has a libc first buffer; every later `reallocate` sees `owns(old)==false`
     → libc realloc forever. Earlier activation would capture *some* of this, but only the
     GlobalAlloc-routed part, and it requires reset-reordering that collides with the LSP path
     (`compile_project_with_ctx` is shared by CLI line 753 and LSP line 569; the reset+arena live at
     783, inside it).
  2. **Direct `intrinsics::malloc` (bypasses GlobalAlloc entirely).** 125 sites in the compiler. The
     load-bearing persistent one is `InternTable::intern` (intern_table.cryo:55) — it `intrinsics::
     malloc(len+1)`s the bytes of **every interned string** of a ~200-module self-host build and never
     frees them (the leaked context owns them). The arena's `GlobalAlloc` hook *cannot* see these; only
     rewriting the call sites to route through the arena would capture them.

## Strategic fork (2026-06-27) — decision needed before more arena work

Stages 0–4 are correct, safe, byte-identical, shippable — net **~174 MiB (~7%)** off peak. Getting to
the ~1.1–1.3 GB target is blocked on the two walls above, and the arena architecture (GlobalAlloc
routing + reset) can only ever capture the subset of per-target memory that *both* routes through
GlobalAlloc *and* is born during the active window. The rest is structurally out of reach.

- **Path A — incremental capture (whack-a-mole).** Earlier activation (reset-reordering, LSP-aware) +
  rewrite the hot direct-malloc sites (InternTable first, then audit the other 124) to route through
  the arena, measuring each. Each step is modest; transient `intrinsics::malloc` and anything born at
  `ctx::new` resist cleanly. Realistically claws back maybe a few hundred MiB more, not the full GB.
- **Path B — droppable per-target context (the architectural fix).** Stop *leaking* the per-target
  `CompilationContext`; **drop** it (run Drop on all its containers → libc frees everything) between
  targets. Reclaims the whole per-target heap **regardless of allocation source** (GlobalAlloc OR
  `intrinsics::malloc`, born any time), so peak → ~one target (~1.24 GB) directly. The catch is the
  reason it's leaked today: the context's owned graphs (AST nodes — already arena-backed —, type
  arena, registries with shared/back pointers) aren't proven safe to Drop without double-frees/UAF.
  Under Path B the GlobalAlloc container-arena (Stages 0–3) becomes largely redundant (the AST arena
  still earns its keep, since AST nodes are intentionally not drop-traced). This is a separate,
  larger project but is the only path that actually hits the target.

**Recommendation:** Path B is the real fix; Path A is diminishing returns on an architecture that
tops out well short of target. Keep Stages 0–3 (correct, +174 MiB, harmless) regardless. Decide
whether Stage 4 stays (LLVM hygiene) or is reverted (zero benefit, cross-cutting `current()` surface).

## Stage 2 measurement (2026-06-27) — the arena works but is not the whole story

Measured in WSL with `CRYO_ARENA_STATS` (temporary env-gated probe, since removed) +
`/usr/bin/time -v ./build/cryo build --no-incremental`:

- **Peak RSS: 2,303,216 KB (~2.30 GB)** vs baseline 2,477,584 KB (~2.48 GB) → **−174 MiB (~7%)**.
- **Arena traffic per target: used ≈ 496–505 MiB, capacity ≈ 496–512 MiB** (lib `compiler` then bin
  `cryo`). used≈capacity means **no realloc-stranding** (the `grow` frontier-in-place + bump arena
  are behaving); the arena is NOT the inflation source the plan feared.
- **Therefore the bottleneck is COVERAGE, not stranding:** the arena captures ~500 MiB/target (AST
  nodes + the `[]`-init container growers), but ~600 MiB/target lives OUTSIDE it and stacks across
  the lib+bin targets:
  1. **LLVM global context (~385 MiB/target, the dominant axis).** `LLVMGetGlobalContext()` is never
     disposed; named structs/types accumulate across both targets. The `T[]` arena cannot touch this
     — it is exactly **Stage 4**. This is now the critical path, not a "complementary" nicety.
  2. **libc-born containers that never migrate.** Anything first allocated *before* the activation
     point (`ctx::new` at instance.cryo:750 → `InternTable`, `ModuleGraph` seeds; plus all of module
     discovery, which runs before Phase 2) is born in libc. Because `GlobalAlloc::reallocate`
     consults `owns(old_ptr)` and a libc pointer is never arena-resident, those buffers **realloc in
     libc forever** — they never move into the arena even after activation. The `InternTable` (grows
     to hold every interned identifier of a ~200-module self-host build) is the big one here.

**Two levers to close the gap (next session):**
- **Stage 4 (LLVM per-target context)** — reclaims the ~385 MiB/target LLVM stacking. Biggest single
  win; required to approach the target. Files listed in the Stage 4 row below.
- **Earlier activation** — move `set_alloc_arena_active(true)` from Phase 2 (instance.cryo ~1186) up
  to just after the survivors are built (after `ctx.stdlib_root=` ~879) AND ideally before `ctx::new`
  (compile_project_with_config:750) so the `InternTable`/`ModuleGraph` are arena-backed. BLOCKER:
  earlier activation crosses 5+ early-return sites (instance.cryo 899/952/1073/1081 + the
  `do_incremental` up-to-date short-circuit at ~1180, which is a *success* return that leaks the
  active flag into `compile_project_multi`'s `base.clone()` → arena → freed at next reset → UAF). Each
  needs a deactivation, OR route survivor + pre-pass allocations through an explicit
  `cryo_strdup_libc`. Do this only with the same survivor-audit rigor; measure the delta.

**Net:** Stage 2 is correct, safe, byte-identical, and shippable, but on its own only removes ~7% of
peak. The OOM fix needs Stage 4 (and likely earlier activation) on top.

Status legend: ⬜ TODO · 🔄 in progress · ✅ done · ⛔ blocked

---

## Context

The AST-node arena (already shipped + pinned) reclaims only ~130 MiB. RSS-by-phase measurement
showed each compilation target needs ~1.1 GB spread across frontend/mono/sema/codegen, and because
the compiler builds `[lib]` then `[[bin]]` in **one process** with each target's `CompilationContext`
heap-leaked, the lib's memory is never released before the bin builds — peak stacks to **2.27 GB** and
OOM-kills constrained Linux boxes. Lifetime analysis **proved** a per-target bulk free is safe:
nothing in a target's context is read by the next (handoff is `libcompiler.a` on disk; only
`output_path`/`stdlib_root` strings + the `ProjectConfig` clone survive, none owned by the context).

**Key finding that makes this tractable:** a builtin dynamic `T[]` *is* `Array<T, GlobalAlloc>` for
method dispatch + drop (call_emitter.cryo:794-806 routes `T[]` methods to the monomorphized
`Array_<T>` registry; `GlobalAlloc` is a ZST so layouts are identical 24-byte `{ptr,len,cap}`). The
heavy containers (`TypeArena.types`, `Monomorphizer.spec_entries`, `Resolver.symbols/scopes`,
`DeclIndex` arrays, `GenericRegistry`) are all `[]`-initialized (e.g. `arena.cryo:122 types: []`), so
their first buffer comes from `push` → `Array::resize_storage` (`ptr==null` branch) →
`GlobalAlloc::allocate`. **So ~1 GB/target already funnels through one chokepoint: `GlobalAlloc`.**

**Goal:** route the compiler's `GlobalAlloc` (hence all `T[]` + default `HashMap`/`Box`/`String`)
through a per-target arena, reclaimed in bulk between targets — capping peak near one target
(~1.1–1.3 GB), byte-identically, with the self-host fixed point + tests green, and **zero impact on
compiled user programs**.

## Stage 0 structure note (as-built — deviates from the original "hook" sketch)

The original sketch put the globals in a leaf `arena_hook.cryo` "imported by both."
That **cannot break the cycle**: `GlobalAlloc` must call into the arena's bump code, and
the bump code needs the `Arena` type, which lives in a module that implements the
`Allocator` trait → `arena → allocator` is a permanent edge. Any path by which `allocator`
reaches the bump code (directly or via a hook) re-closes the cycle, and the module
topo-sort (`module_graph.cryo compute_order`) **hard-fails on true import cycles** (verified).

What actually works (as-built, all gates green):
- **`stdlib/alloc/arena.cryo`** is now **allocator-free** (imports only `intrinsics` + `layout`).
  It holds: `Arena`/`Chunk` structs; inherent `new`/`with_chunk_size`/`reset`/`capacity`/`used`;
  the raw bump surface `bump(size,align)->void*`, `owns(ptr)->boolean`,
  `grow(ptr,old,new,align)->void*` (frontier grow-in-place else alloc+copy), `release()`;
  and the per-target globals `g_alloc_arena`/`g_alloc_arena_active` + accessors + the
  route wrappers `alloc_arena_routing()/take()/holds()/grow()`.
- **`stdlib/alloc/arena_alloc.cryo`** (NEW) holds `implement trait Allocator/Drop for Arena`,
  thin wrappers over the inherent raw ops. It imports `allocator` (+`arena`); nobody imports
  it back. `arena.cryo` re-exports it via `public module alloc::arena_alloc;` — a `public module`
  decl adds **no dependency edge** (`ModuleLoader::discover_module`), so `import alloc::arena`
  alone still gives you `Arena`-as-`Allocator` (`Array<T,Arena>`) with no cycle.
- **`stdlib/alloc/allocator.cryo`** `import alloc::arena;` (one-way edge, no back-edge) and the
  three `GlobalAlloc` bodies consult `alloc_arena_routing()` (allocate) / `alloc_arena_holds()`
  (deallocate + reallocate, **regardless of active flag**) / `alloc_arena_grow()`.
- **`compiler/.../codegen/ast_arena.cryo`** now calls the inherent `a.bump(sz,al)` directly
  (not the trait) — drops its `allocator`/`layout`/`ptr`/`result` imports; the hot AST path no
  longer depends on cross-module trait-impl resolution.
- `_module.cryo` lists `public module alloc::arena_alloc;` after `alloc::arena`.

Net: globals null/false in every non-compiler program → all consult branches dead → zero user
impact, selfhost byte-identical. Stage 1 just calls `set_alloc_arena(get_ast_arena())`; Stage 2
brackets `set_alloc_arena_active(true/false)` around the heavy pipeline in `instance.cryo`.

## Stage 1+2 as-built (the activation bracket)

- **Stage 1 (publish):** `ast_arena.cryo get_ast_arena()` calls `set_alloc_arena(g_ast_arena)` on
  first use — the container arena and the AST arena are THE SAME region (one reset reclaims both).
- **Toggle surface:** `ast_arena.cryo` exposes `public function cryo_alloc_arena_set_active(bool)`
  (wraps `arena::set_alloc_arena_active`) so `instance.cryo` toggles via the same
  `Compiler::Codegen::AstArena::*` namespace it already uses for `cryo_ast_arena_reset` — no new import.
- **Stage 2 bracket in `compile_project_with_ctx` (instance.cryo):**
  - ACTIVATE at the top of Phase 2 (just before the per-module ast/scope buffers, ~line 1186) — AFTER
    the up-to-date short-circuit and every pre-pass error return (they stay inactive) and AFTER the
    survivors (`ProjectConfig` clone ~844, `ctx.stdlib_root=` ~879) are built in libc.
  - DEACTIVATE on success just before the `has_errors()` result gate (after linking), so `exe_path` +
    manifest side-files are libc and outlive the next target's reset.
  - DEACTIVATE on failure in `project_failure_release` (the single chokepoint for all 12 pass-failure
    returns).
  - DEFENSIVE: a `set_alloc_arena_active(false)` paired with the reset at the top of the function, so
    a target always STARTS inactive even if a prior target somehow leaked the flag.
- Why this is leak-safe: failed multi-target builds abort (no next target); on success every target
  deactivates before returning; the only cross-reset survivors (`output_path`/`stdlib_root`/the
  `ProjectConfig` clone) are all built pre-activation. Verified by selfhost byte-identity (lib+bin) on
  both platforms.

## Mechanism (decided)

A **scoped-global arena consulted by `GlobalAlloc`**, with a pointer-range membership check for
free/realloc routing. Reuse `g_ast_arena` (codegen/ast_arena.cryo) as THE single per-target region so
AST nodes + container buffers share one arena and one existing reset. Two stdlib globals (null in
every user program → the arena is physically incapable of activating there):

- `g_alloc_arena: Arena*` — the per-target region (set by the compiler driver; null elsewhere).
- `g_alloc_arena_active: boolean` — gates whether *new* allocations route to the arena.

`GlobalAlloc` (stdlib/alloc/allocator.cryo:111-167) becomes:
```
allocate(layout):  if (g_alloc_arena != null && g_alloc_arena_active) arena.allocate(layout)
                   else <today's aligned_alloc>
deallocate(p,l):   if (g_alloc_arena != null && g_alloc_arena.owns(p)) return   // bulk-reclaimed
                   else <today's aligned_free>
reallocate(p,o,n): if (g_alloc_arena != null && g_alloc_arena.owns(p)) arena.reallocate(p,o,n)
                   else <today's realloc>
```
**Critical correctness rule:** free/realloc consult `owns()` *regardless of the active flag*, so an
arena buffer is never handed to libc `free` (no corruption); the active flag governs only *allocate*
routing. `Arena` gains `owns(ptr) -> boolean` (range-scan chunk list, mirrors `capacity()`
arena.cryo:85-94) and an `Allocator::reallocate` override (frontier grow-in-place via `try_bump`;
else alloc-new + memcpy + no-op-free).

Why not alternatives: a per-array allocator field (4th field) is a 24→32-byte ABI change to *every*
program's arrays + the bootstrap pin — rejected. Header-tag-every-allocation changes returned
pointers for all programs — rejected. "Assume arena-owned when active" can libc-free an arena pointer
after deactivation (corruption) — rejected in favor of `owns()`.

**Survivors** must be allocated while `g_alloc_arena_active == false`: the `ProjectConfig` clone
(instance.cryo:684,694) is already created before the per-target pipeline; bracket activation to
exclude result construction (`output_path`/`stdlib_root`, instance.cryo:~165-197/410-411/459) and
linking. Defense in depth: assert no survivor is `owns(...)` before reset; `strdup`-to-libc any
straggler found by audit.

**Zero user-program impact:** no array-emitter/IR change; the only delta is a never-taken
`g_alloc_arena != null` null-check atop the three `GlobalAlloc` methods (global is null in user
programs; branch statically predictable, arena code dead).

**Determinism:** routing changes only *where* bytes come from (pointer values); the compiler is
pointer-value-independent (same invariant the AST arena relies on). Gated every stage by selfhost
byte-identity.

## Staged rollout (each stage: `make cryo` + `make test` + `make selfhost-check` byte-identical)

- **Stage 0 — stdlib infra, behavior-neutral.** Add `g_alloc_arena`/`g_alloc_arena_active` +
  setters/getter; add `Arena::owns` + `Arena::reallocate` override; wrap the three `GlobalAlloc`
  bodies with the consult branches. Globals null/false → identical behavior + one-time stdlib IR
  delta only. Resolve global placement to avoid an `allocator`↔`arena` import cycle (small shared
  `stdlib/alloc/arena_hook.cryo` imported by both).
- **Stage 1 — wire region, inert.** `ast_arena.cryo get_ast_arena` publishes the region via
  `set_alloc_arena`; keep active flag false (empty arena owns nothing → no-op). Still byte-identical.
- **Stage 2 — activate around the pipeline (the RSS win).** instance.cryo: set active `true` after
  context creation / before heavy passes; `false` after codegen+object-emission and before result
  construction + linking. Existing boundary reset (instance.cryo:438-439/500-501) now reclaims the
  container heap too. Survivor audit. **Measure peak RSS**; selfhost MUST stay byte-identical.
- **Stage 3 — harden reset ordering + multi-target loop.** Confirm reset runs after the prior target
  fully returns (incl. linking off the leaked context) and before the next target's first allocation;
  confirm `compile_project_multi` (instance.cryo:670-701) resets between lib and each bin.
- **Stage 4 — (complementary) per-target LLVM context.** `T[]` arena doesn't touch LLVM-side memory,
  which hangs off the implicit global context and stacks across targets (~385 MiB/target). Add a
  settable `g_codegen_context` + `LContext::current()` accessor (fallback `LLVMContextCreate()`),
  build modules via `LLVMModuleCreateWithNameInContext`, route the type primitives + named-struct
  creation through `*InContext` variants, `LLVMContextDispose` + null at target end (cache already
  reset per target). Files: codegen/llvm_types.cryo (759-801, 832-833, 259-276, 161),
  codegen/type_map.cryo (266-286), codegen/abi.cryo, codegen/context.cryo, declaration_emitter.cryo:1549.
  Schedule after Stage 2 is measured (the arena is the larger axis); needed to fully cap peak.
- **Stage 5 — (optional, measured) route the 3 inline array emitters** (`array_lit_emitter.cryo`
  185-194/328-338 non-empty literal; `new_delete_emitter.cryo` 443-456 `new T[n]`) to a stdlib
  `cryo_alloc_raw(size,align)` so non-empty-literal/`new T[n]` arrays are arena-backed too. Changes
  user IR (`call malloc`→`cryo_alloc_raw`), so keep last behind explicit acceptance. Only if Stage-2
  measurement shows these material (the `[]`-init containers are already covered without it).

## Files
- **stdlib:** `stdlib/alloc/allocator.cryo` (GlobalAlloc allocate/deallocate/reallocate),
  `stdlib/alloc/arena.cryo` (`owns`, `reallocate` override), new `stdlib/alloc/arena_hook.cryo`
  (globals + accessors).
- **compiler:** `compiler/src/compiler/codegen/ast_arena.cryo` (publish region in `get_ast_arena`),
  `compiler/src/compiler/instance.cryo` (toggle active flag around the heavy pipeline; survivor audit;
  confirm resets + multi-target loop). Stage 4 LLVM files as listed above.
- **reference (no change):** `compiler/src/compiler/codegen/visit/call_emitter.cryo` (dispatch),
  `compiler/src/compiler/types/arena.cryo` / `mono/monomorphizer.cryo` (the `[]`-init containers).

## Bootstrap / re-pin
**Single-phase re-pin** — no new language features (globals, null compares, pointer-range arithmetic,
a trait-method override are all already exercised by `g_ast_arena`/`Arena`). No bootstrap loop:
`g_alloc_arena` is null-initialized; the `Arena` backing comes from `intrinsics::malloc` via
`alloc_chunk`, never from `GlobalAlloc`; the `Box<Arena>::new(...).leak()` init runs with the active
flag still false. Sequence: pinned builds new compiler → new compiler rebuilds stdlib+compiler →
`selfhost-check` to fixed point. Stage 0 is behavior-neutral so its first re-pin should already be a
fixed point; re-confirm at Stage 2 under the active arena. (Stage 0 touches stdlib `GlobalAlloc`, so
follow the stash-stdlib / `make pin` / restore dance if the pin can't parse intermediate state.)

## Verification
- `make cryo` (PowerShell, `CRYO_CC=gcc`) — builds each stage.
- `make test` — full suite green each stage (arena invisible to semantics).
- `make selfhost-check` — **FIXED POINT, byte-identical** (Linux + Windows). Any divergence = an
  allocation-address dependency leaked into output → bisect the offending pass.
- **Peak RSS** before/after in WSL: `/usr/bin/time -v <compiler>/build/.../cryo build --no-incremental`,
  read "Maximum resident set size". Baseline ~2.48 GB (measured); expect post-Stage-2 toward one
  target (~1.1–1.3 GB) plus arena realloc slack; Stage 4 removes the LLVM stacking.

## Risks & mitigations
- **Survivor lands in the arena → use-after-reset (central risk).** Bracket activation to exclude
  result/survivor construction + linking; `ProjectConfig` clone is already pre-window; assert
  survivors aren't `owns(...)` before reset; `strdup`-to-libc stragglers.
- **Realloc-stranding inflates peak (the thing to MEASURE).** Geometric doubling strands each old
  buffer in the bump arena until reset (~up to 2×/target), which could erode the win. Mitigate with
  `Arena::reallocate` frontier grow-in-place (kills stranding for monotonic single-growers like
  `TypeArena.types`), `reserve()`/`with_capacity` on the hottest containers, and chunk-size tuning.
  This is why Stage 2 measures before declaring victory.
- **Arena pointer libc-freed while inactive → corruption.** Prevented: free/realloc consult `owns()`
  regardless of the active flag.
- **Determinism regression** (pointer-keyed hashing/ordering): gated by selfhost byte-identity each
  stage.
- **Import cycle placing stdlib globals:** resolve in Stage 0 via a small shared `arena_hook.cryo`.
- **User-program impact creep:** keep the 3 emitters untouched in core stages (sole user delta is the
  never-taken null-check); defer emitter routing to Stage 5 behind explicit acceptance.
