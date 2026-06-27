# HANDOFF — Allocator-aware arena + per-target LLVM context (memory optimization)

You are picking up a compiler memory-optimization effort aimed at fixing an OOM in
`make selfhost-check` on constrained Linux boxes. **Read `ARENA_PLAN.md` first** — it is the full
spec + live progress tracker + every measurement. This file is the orientation + the decision you
need to drive next. Do NOT re-derive the findings below; they were measured this session.

## TL;DR — where things stand (2026-06-27)

- **The OOM**: `cryo build` compiles `[lib]` then `[[bin]]` in **one process**; each target's
  `CompilationContext` is heap-**leaked**, so the lib's heap never frees before the bin builds → peak
  **~2.48 GB**, OOM-kills small boxes. Goal: cap peak near one target (~1.1–1.3 GB).
- **Stages 0–4 are DONE, verified byte-identical, but net only ~174 MiB (~7%) off peak.** The
  measurements proved the arena architecture **structurally tops out well short of target.** The next
  move is a **strategic decision (Path A vs Path B), not another stage** — see §Decision below.
- **Peak RSS history (measured, WSL, compiler self-build):** baseline **2.48 GB** → Stage 2
  **2.30 GB** → Stage 4 **2.30 GB** (no further gain). Target **1.1–1.3 GB**.

## Git state — what's committed vs not

- **HEAD `d4a49a76`** "refactor arena allocation + introduce arena_alloc module" — **Stage 0 is
  COMMITTED + PINNED** (the maintainer committed it mid-session). This is the stdlib infra:
  `stdlib/alloc/arena.cryo` (now allocator-free: bump/owns/grow/release + the per-target globals
  `g_alloc_arena`/`g_alloc_arena_active` + route wrappers), `stdlib/alloc/arena_alloc.cryo` (NEW —
  `Arena`'s `Allocator`/`Drop` impls, re-exported via `public module`), `stdlib/alloc/allocator.cryo`
  (`GlobalAlloc` consult branches), `stdlib/alloc/_module.cryo`.
- **UNCOMMITTED (Stages 1+2+4)** on top of HEAD — 7 compiler files + `ARENA_PLAN.md` (untracked):
  - `compiler/src/compiler/codegen/ast_arena.cryo` — publishes the region (`set_alloc_arena`),
    `cryo_alloc_arena_set_active` toggle, `cryo_ast_arena_alloc` calls inherent `bump` directly.
  - `compiler/src/compiler/instance.cryo` — the per-target **activation bracket** (arena active + LLVM
    context) and the deactivation/dispose on success + all failure paths. **Read this file to
    understand the whole lifecycle.**
  - `compiler/src/compiler/codegen/llvm_types.cryo` — `g_codegen_context` + `LContext::current()` +
    lifecycle fns + all leaf constructors routed through `current()`.
  - `compiler/src/compiler/codegen/{abi,type_map,passes}.cryo`,
    `codegen/ops/declaration_emitter.cryo` — `LContext::global()` → `current()` + 2 module-flag i32s +
    debug-loc routed.
  - `ARENA_PLAN.md` — full tracker. **Keep its Progress table + measurement sections updated.**
- This `HANDOFF.md` is not part of the build.

All uncommitted work is GREEN (build + `make test` + `make selfhost-check` byte-identical Linux+Windows).
Left uncommitted for maintainer review. **Do not commit/pin without being asked.**

## What each stage did + what it bought (TRUST THESE — measured, do not re-investigate)

- **Stage 0 (committed):** stdlib `GlobalAlloc` can route to a per-target arena. KEY structural fact:
  the topo-sort **hard-fails on true import cycles**, and `arena→allocator` is unbreakable (Arena
  implements the `Allocator` trait), so `allocator` can only import an **allocator-free** `arena`.
  That's why the trait impls were split into `arena_alloc.cryo` (re-exported via `public module`,
  which adds NO dependency edge). The plan's original "`arena_hook` imported by both" sketch CANNOT
  break the cycle — don't revive it.
- **Stage 1 (publish) + Stage 2 (activate):** the container arena (every `T[]`/`HashMap`/`Box`/
  `String` via `GlobalAlloc`) is bracketed ON around the heavy pipeline, reset between targets. **−174
  MiB.** Arena captures **~500 MiB/target** (measured used≈capacity, so NO realloc-stranding — `grow`
  frontier-in-place works). Correct, byte-identical.
- **Stage 4 (per-target LLVM context):** correct, byte-identical, **but ~0 RSS win.** A/B proved LLVM
  does NOT stack across targets — the Phase-7 `LLVMDisposeModule` (instance.cryo:1930-1934) already
  reclaims it per module. The "~385 MiB/target LLVM" was within-target codegen peak, never crossed the
  boundary. Keep as hygiene or revert — maintainer's call.

## THE WALL (why the arena tops out) — the central finding

Per-target peak ~1.24 GB = ~500 MiB arena (reclaimed between targets ✓) + **~740 MiB libc the arena
CANNOT reach**, for two structural reasons:

1. **Born-before-activation → realloc stays libc.** Any container first allocated before the Phase-2
   activation point (e.g. `InternTable.strings[]`, seeded by `push("")` in `InternTable::new` at
   `ctx::new`; `ModuleGraph`) has a libc first buffer; every later `reallocate` sees
   `owns(old)==false` → libc realloc **forever**, even after the arena activates.
2. **Direct `intrinsics::malloc` bypasses `GlobalAlloc` entirely** (125 sites in the compiler). The
   load-bearing one: `InternTable::intern` (`resolver/intern_table.cryo:55`) `intrinsics::malloc`s the
   bytes of **every interned string** of a ~200-module self-host build and never frees them. The arena
   hook physically cannot see these.

→ The GlobalAlloc-arena can only ever capture memory that BOTH routes through `GlobalAlloc` AND is born
during the active window. The rest stacks across lib+bin and that's the ~1 GB that keeps peak at 2.3 GB.

## DECISION YOU NEED TO DRIVE (this is the real task now)

Stages 0–4 are shippable but won't hit target. Two paths (full detail in `ARENA_PLAN.md` →
*Strategic fork*):

- **Path A — incremental capture (whack-a-mole, diminishing returns).** Move activation earlier
  (BLOCKER: the reset + arena live at `instance.cryo:783` *inside* `compile_project_with_ctx`, which is
  shared by the CLI path (called at :753) AND the LSP path (called at :569) — reset-reordering must not
  break LSP's per-request cache reset or arena-free its kept-alive ctx) + rewrite hot direct-malloc
  sites (InternTable first) to route through the arena. Each step modest; transients + `ctx::new`-born
  resist. Realistically a few hundred MiB more, never the full GB.
- **Path B — droppable per-target context (the architectural fix, RECOMMENDED).** Stop *leaking* the
  `CompilationContext`; **drop** it between targets so libc frees the whole per-target heap regardless
  of allocation source (GlobalAlloc OR `intrinsics::malloc`, born any time) → peak → ~one target
  (~1.24 GB) directly. The catch is exactly why it's leaked today: its owned graph (type arena,
  registries with shared/back-pointers; AST nodes are already arena-backed) isn't proven safe to Drop
  without double-frees/UAF. Separate, larger project. Under Path B the container-arena (Stages 0–3)
  becomes largely redundant — but the AST arena still earns its keep (AST nodes are intentionally not
  drop-traced). **Decide with the maintainer before starting; this is a direction change.**

Also pending a maintainer call: **keep Stage 4 (LLVM hygiene) or revert it** (zero RSS benefit, adds a
cross-cutting "every future leaf LLVM constructor must use `LContext::current()`" requirement).

## How to build / test / measure (Windows host + WSL for Linux)

- **Build:** PowerShell with `$env:CRYO_CC = "gcc"`, then `make cryo` (→ `compiler/build/cryo.exe`).
  Git Bash breaks on the cmd-syntax stdlib recipe — use PowerShell for `make`.
- **Tests:** `make test` (full suite: 114 compile-fail + 4 projects + unit).
- **Selfhost byte-identity gate (THE correctness gate):** `make selfhost-check` — runs the 6-stage
  Linux byte-identity chain via WSL (~5 min) then the Windows native gate (~2 min). Must report
  **FIXED POINT OK** on both. Any divergence = an allocation-address dependency leaked into output →
  bisect. Everything stayed byte-identical this session; it's your safety net for Path A/B too.
- **Peak RSS (this command works):**
  ```
  wsl.exe -e bash -lc "cd /mnt/c/Programming/apps/CryoLang && make cryo >/tmp/mk.log 2>&1 && \
    cd compiler && /usr/bin/time -v ./build/cryo build --no-incremental --build-dir=build/measure \
    2>&1 | grep -iE 'Maximum resident|Elapsed'"
  ```
  No two-round bootstrap lag for the container arena (it's stdlib-linked, not codegen-lowered): the
  `build/cryo` from `make cryo` already arena-allocates. Baseline 2,477,584 KB; current ~2,304,000 KB.
  To break down arena vs libc, re-add a temporary `CRYO_ARENA_STATS` probe: a `libc::getenv`-gated
  `fmt::eprintln` of `g_alloc_arena.used()/capacity()` (via temp getters on `AstArena`) at the
  deactivation point in `instance.cryo`. Measured this session: used≈capacity≈500 MiB/target.

## Gotchas / traps (hard-won)

- **Do NOT use POSIX-only libc in compiler source** (e.g. `getrusage`) — breaks the *Windows* selfhost
  link (mingw lacks it) while Linux passes. Any measurement probe must be portable (`libc::getenv` is
  fine) and **removed before any re-pin**.
- **Cryo cast precedence:** `(libc::getenv("X") as i8* != null)` is a parse error — bind to a local
  first: `const p: i8* = libc::getenv("X") as i8*; if (p != null) {...}`.
- **Bash tool (Git Bash) and PowerShell tool SHARE a working dir** — a `cd` in one moves the other.
  Use absolute paths.
- **Stale ELF helper-archive trap:** `make test` may fail with `undefined reference to abi_sum_two_i32`
  if `tests/helpers/libabihelpers.a` was built by WSL (ELF) but you're linking PE. Fix:
  `rm tests/helpers/abi_helpers.o tests/helpers/libabihelpers.a` then re-run `make test`.
- **`make selfhost-check` rebuilds via the pinned boot** — your uncommitted source is what it compiles,
  so it validates uncommitted changes correctly without a re-pin. Re-pin (`make pin`) only when the
  maintainer asks and the gates are green.
- **LLVM context is context-scoped:** if you add ANY new leaf LLVM type/module/builder/metadata
  constructor, it MUST go through `LContext::current()` (not `LLVM<T>Type()`/`LLVMGetGlobalContext()`),
  or LLVM asserts on a context mismatch under Stage 4. Compound constructors (`LLVMPointerType`/
  `ArrayType`/`FunctionType`/`ConstInt`/`StructSetBody`) inherit context from their args — leave those.

## Key files

- **`ARENA_PLAN.md`** (root) — full spec, staged rollout, ALL measurements, the strategic fork. Single
  source of truth. Mirror at `C:\Users\jacob\.claude\plans\cozy-hugging-horizon.md`.
- `compiler/src/compiler/instance.cryo` — the per-target driver + the activation/dispose bracket.
  Search `cryo_alloc_arena_set_active` and `LLVMTypes::` to find all lifecycle sites: create/activate
  at Phase 2 (~line 1186), deactivate+dispose on success before the `has_errors` gate, deactivate+
  dispose in `project_failure_release`, defensive reset at the :783 target-start reset.
- `stdlib/alloc/{arena,arena_alloc,allocator}.cryo` — Stage 0 (committed).
- `compiler/src/compiler/codegen/{llvm_types,abi,type_map,passes}.cryo`,
  `codegen/ops/declaration_emitter.cryo` — Stage 4.
- `compiler/src/compiler/resolver/intern_table.cryo` — the InternTable; biggest single direct-malloc
  leak (Path A target #1) and a prime example of why born-before-activation matters.
