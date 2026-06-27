# HANDOFF — compiler peak-RSS reduction: the ROOT CAUSE is found

You are continuing the memory-optimization effort on the Cryo self-hosted compiler. `cryo build`
compiles `[lib]` then `[[bin]]` in ONE process; each target's heap stacks instead of being reclaimed,
so peak RSS (~2.4 GB) OOM-kills constrained boxes. **Read `ARENA_PLAN.md` first** for the full
diagnosis/measurement log. This file is your orientation + the exact next move.

The previous session (this one) **found the real root cause** that every prior session missed. Read
the next section carefully — it changes the whole strategy.

---

## THE ROOT CAUSE (proven by measurement this session)

**The arena's `release()` does not return memory to the OS.** Arena chunks are `DEFAULT_CHUNK_SIZE =
64 KiB` blocks from `intrinsics::malloc` (`stdlib/alloc/arena.cryo` `alloc_chunk`), freed with
`intrinsics::free` (`Arena::release`). 64 KiB is **below glibc's 128 KiB mmap threshold**, so every
arena chunk lives on glibc's **main heap**. When `release()` frees them, glibc **retains the pages**
(does NOT munmap) — RSS never drops. This is the same libc-retention that defeated the `malloc_trim`
dead-end, and it's why `cryo_ast_arena_release()`'s comment ("frees large mmap-backed chunks to the
OS") is a lie for the common case: the millions of small AST-node / intern / container allocations
pack into 64 KiB malloc'd chunks that glibc holds forever.

**Consequence:** all prior sessions' arena work (Stages 0–4) was correct in *capturing* memory into the
arena, but the arena could never give it back between targets → only ~7% RSS win ever materialized.

### Proof (RSS timeline, this Linux codespace, `cryo build --no-incremental` of the compiler itself = lib `compiler` + bin `cryo`)

| Arena chunk backing | Inter-target drop (lib→bin boundary) | Peak RSS |
|---|---|---|
| 64 KiB malloc (original) | **none** — monotonic climb | 2188 MB |
| 64 MiB malloc (forces glibc mmap; validation) | **~159 MB** drop (1361→1202 at the boundary) | 2188 MB |

The 64 MiB run **proves the mechanism**: bumping chunks above glibc's 32 MiB dynamic-threshold ceiling
forces mmap, and now `release()` munmaps → a real boundary drop appears (was exactly zero before).

**BUT peak is still 2188 MB**, because the boundary drop was only ~159 MB. The other **~1040 MB of
lib's heap is NOT in the arena at all** — it's direct-`intrinsics::malloc` main-heap that is never
freed, and bin stacks straight on top of it. **That residual main-heap is the real remaining target.**
The intern reroute (below) did NOT move peak, which means either intern is not the bulk, or most
interning happens *before* the arena is activated (Phase 2, `instance.cryo:~1232`) and routes to libc.

### The decomposition probe is RUNNING / needs re-running (answers "what is the ~1040 MB")

The previous session added a temporary `malloc_stats()` probe at the per-target teardown
(`instance.cryo`, right after `cryo_ast_arena_release()`) and was mid-measurement when the session
ended. **First thing to do: re-run it and read the numbers** (the codespace background job + `/tmp`
scratch did NOT survive the move to your Windows PC). The probe prints, after each target's teardown:
- `Total ... in use bytes` minus `max mmap bytes` = **direct-malloc main-heap still held** = the
  stacking culprit. After the **lib** target this is the number to attack.
- `max mmap bytes` = arena/mmap (the part `release()` now reclaims with big chunks).

Run it (Windows/WSL or any glibc box): build, then `./build/cryo build --no-incremental
--build-dir=build/probe 2>probe.txt`, then read the two `POST-TEARDOWN malloc_stats` blocks in
`probe.txt`. The FIRST block (lib) tells you how big the direct-malloc residual is and confirms where
to aim.

---

## Uncommitted changes (on top of HEAD `6a97c6f1`, clean before this session)

`git status` shows 5 modified files. Classify before you build on them:

**KEEP (correct, principled):**
- `compiler/src/compiler/resolver/intern_table.cryo` — interned-string byte copies rerouted
  `intrinsics::malloc` → `allocator::global_heap_alloc(len+1, 1)` (+ `import std::alloc::allocator`);
  `release_strings` now uses `global_heap_free` (no-ops arena-owned, frees libc-born). This is
  **complementary**: once the arena release actually returns to the OS, lib's interned bytes get
  reclaimed too. It alone produced no peak drop (see above) but is the right routing and harmless.

**TEMPORARY — must be reverted/replaced:**
- `stdlib/alloc/arena.cryo` — `DEFAULT_CHUNK_SIZE` 65536 → **67108864 (64 MiB)**. This is a *validation
  hack* (forces glibc to mmap). **Replace with explicit mmap/munmap chunk backing at 1 MiB** (spec
  below). 64 MiB wastes up to 64 MiB tail per arena and relies on a glibc heuristic — not shippable.
- `stdlib/ffi/libc.cryo` — added `malloc_stats()` to the `![target(linux)]` extern block. **REMOVE
  before any re-pin** (breaks the Windows/mingw link). Probe only.
- `compiler/src/compiler/instance.cryo` — added the `malloc_stats()` probe call at the teardown
  boundary. **REMOVE before re-pin.** Probe only.

**DOC:**
- `ARENA_PLAN.md` — updated with the full Path B feasibility spike (see below) + this root-cause.

---

## YOUR NEXT MOVES (in order)

### 1. Implement the PROPER mmap-backed arena chunks (the core fix)
Replace the 64 MiB validation hack. Make `alloc_chunk` get the chunk **data** buffer from the OS
directly, and `release()` return it directly, so reclamation is guaranteed regardless of glibc's
heuristics. Both backends are **already available** (verified this session):
- **POSIX** (`import ffi::libc`): `mmap(null, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,
  -1, 0)`; check for `MAP_FAILED` (`(void*)-1`, i.e. `as u64 == 0xFFFFFFFFFFFFFFFF`); free via
  `munmap(ptr, size)`. Constants `PROT_*`/`MAP_*` are in `stdlib/ffi/libc.cryo` (~line 1419-1443).
- **Windows** (`import ffi::syscall`): `VirtualAlloc(null, size, MEM_COMMIT|MEM_RESERVE,
  PAGE_READWRITE)`; free via `VirtualFree(ptr, 0, MEM_RELEASE)`. Bindings + flags in
  `stdlib/ffi/syscall.cryo` (~1242-1272, 1577).
- Gate with `![target(unix)]` / `![target(windows)]` helper fns (`os_chunk_alloc(size)->void*` /
  `os_chunk_free(ptr,size)`), mirroring `intrinsics.cryo`'s `aligned_alloc` split. `Chunk` already
  stores `capacity`, so `munmap`/`VirtualFree` have the length.
- Keep the tiny `Chunk` **header** on `intrinsics::malloc` (sizeof(Chunk) ≈ 40 B, negligible) — only
  the big `data` buffer needs OS backing. Or mmap both; your call.
- Set `DEFAULT_CHUNK_SIZE` to **1 MiB (1048576)**: low VMA/syscall count, low tail waste, demand-paged
  (untouched pages cost no RSS, so small arenas stay cheap). Arena is general stdlib infra (used by
  `Array<T,Arena>` in user programs too) — 1 MiB is a fine default there.
- `arena.cryo` is currently FFI-free (imports only `intrinsics`+`layout`). Adding `ffi::libc`/
  `ffi::syscall` creates **no cycle** (they don't import alloc). Confirm the build still topo-sorts.
- After this lands, re-measure. Expect the boundary drop to grow from ~159 MB toward the full arena
  size (~500 MB+), but **peak will still be gated by the direct-malloc residual** until step 2.

### 2. Attack the direct-malloc residual (what the probe quantifies)
The ~1040 MB that stacks is direct `intrinsics::malloc` NOT in the arena. Options, decide via the probe
+ A/B:
- **Earlier arena activation.** Much interning/allocation happens in discovery/lex/parse *before*
  activation at Phase 2 (`instance.cryo:~1232`) → routes to libc. Moving activation earlier would
  capture it. CAVEAT: ARENA_PLAN records earlier activation was "+80 MB worse" in a prior test (bump
  arena hoards transients until reset) — but that was BEFORE the mmap-release fix; re-evaluate, and
  note the 5+ early-return sites that each need a matching deactivate (instance.cryo
  899/952/1073/1081 + the `do_incremental` up-to-date short-circuit ~1180 — a *success* return that
  must not leak the active flag).
- **Reroute the remaining hot direct-malloc sites** the census found (all per-string byte copies):
  `lex/lexer.cryo:951` and `parser/parser_base.cryo:511` (never-freed literal byte copies →
  `allocator::global_heap_alloc(.., 1)`); `passes/type_resolution.cryo:1425` (→ `new`/arena) and
  `:1419` (a pure staging buffer memcpy'd into `ann` then leaked → make it a **stack local**, no alloc
  at all). These are clean (feed AST nodes). Edits are trivial; both files need `import
  std::alloc::allocator`. They were staged but NOT applied (kept attribution clean).
- **Verify the intern reroute is actually routing.** If the probe shows intern bytes still on the
  main heap, interning is happening pre-activation → step 2's "earlier activation" is the lever.

### 3. Path B — bulk-drop the per-target context (Jake asked for this too, smaller win)
The spike PROVED a bulk drop is **SAFE** (every cross-component ref is a raw non-owning `T*`/`void*`;
the arena `owns()` no-op frees mixed structs correctly; zero real `Drop` impls — all generated glue).
But it reclaims only **libc-born struct headers + the `ProjectConfig` clone + a few buffers** — the ~1
GB container bulk is arena-backed and reclaimed by the arena release. So it's a *minor* incremental
win on top of steps 1–2, not a substitute. Named raw-byte leaks drop-glue can't reach (free
explicitly, like `release_strings`): `module_graph.module_ast_buf`/`module_scope_buf` (raw `void*`),
`diag_renderer.cached_contents`/`cached_files`. Mechanism: at the teardown boundary
(`compile_project_with_config`, instance.cryo:~772-778), after `dup_survivor` and BEFORE
`cryo_ast_arena_release()`, `Box::from_raw` + drop the libc-born components. Preserve the existing
order: `dup_survivor` (survivors → libc) → `release_strings` → component drop → arena release. This
path is CLI-only; the LSP entry (`compile_project_with_ctx`) keeps its ctx — don't touch it.

### 4. Clean up + gates
- Remove BOTH temporary probes (`malloc_stats` in `libc.cryo` + `instance.cryo`).
- **Determinism gate (every step):** same compiler builds the compiler twice → `diff -rq` the `*.ll`
  trees → **0 differing**. The compiler is pointer-value-independent; routing changes only addresses.
- **Ship gate (on your Windows+WSL box):** `make selfhost-check` (byte-identical fixed point, Linux +
  Windows) + `make test` (114 cf + 4 proj). The Linux codespace OOMs on these; that's why they're
  deferred to your box.
- Re-pin only when Jake asks and gates are green: `make pin-cryo` / `make pin-all`. The Stage-0 stdlib
  touch means follow the stash-stdlib / `make pin` / restore dance if needed.

---

## Measurements so far (Linux codespace; reproduces baseline + OOMs near 2.4 GB)

| Build | Peak RSS | Inter-target drop |
|---|---|---|
| baseline pinned `bin/cryo` (pre-arena-active) | 2422 MB | — |
| committed HEAD `6a97c6f1` (arena active, 64 KiB malloc chunks) | ~2161 MB | none |
| + intern reroute (64 KiB chunks) | 2188 MB | none |
| + 64 MiB chunks (validation) | 2188 MB | **~159 MB** (mechanism proven) |
| single target (floor / realistic target) | ~1385 MB | — |

The gap from 2188 → ~1385 is the direct-malloc residual (step 2). Step 1 (mmap) widens the boundary
drop; step 2 is what actually pulls the peak down.

---

## DEAD ENDS (measured — do NOT repeat)
- **Drop the leaked context via `Box::from_raw` (alone):** −9 MB. Components are arena-backed.
- **`malloc_trim(0)` at the boundary:** ~0. Freed main-heap is held by glibc, not free in its lists.
- **Intern reroute alone:** no peak change (arena release wasn't returning to OS — root cause above).
- **A `cryo_alloc_raw`-style parallel allocator:** rejected as slop. Route through `GlobalAlloc`.

## GOVERNING PRINCIPLE (do not violate)
ONE arena-routing implementation: `GlobalAlloc::allocate/deallocate` (`stdlib/alloc/allocator.cryo`).
Every heap alloc reaches the arena *through it*. In compiler source use `new`/`Box`; for raw byte
buffers use `global_heap_alloc(size,align)`/`global_heap_free(ptr,align)` (thin `void*` bridges that
delegate to `GlobalAlloc`). NEVER re-check `alloc_arena_routing()` in a second place; NEVER add a
parallel allocator. The arena `owns()` check makes freeing arena-backed pointers a safe no-op — this
is what makes both `delete` routing and Path B's bulk drop correct.

## Gotchas
- **Build is OOM-hostile** (SIGTERM/Error 143 near 2.4 GB): wrap `make cryo` / measurement runs in a
  `for i in $(seq 1 8); do … && break; done` retry loop (resumes from cache). Your Windows+WSL box may
  not OOM — then this is moot.
- **Staging:** `make cryo` builds `compiler/build/cryo` (STAGE2) via pinned `bin/cryo`. Source changes
  (intern_table, arena) take effect in that binary directly; emitter changes need a second self-build.
  When unsure, build stage2 and measure it.
- **Measurement on Windows:** the `/tmp/peak.py` + `/tmp/poll.py` pollers were Linux `/proc` +
  `getrusage` based and are GONE. On Windows/WSL use `/usr/bin/time -v … | grep Maximum` (WSL) or
  re-create the `/proc` poller under WSL. The `poll.py` 3 s-bucket timeline is what revealed the
  monotonic-climb / boundary-drop behavior — recreate it, it's worth it.
- **Cryo cast precedence:** `libc::getenv("X") as i8* != null` is a parse error — bind to a local first.
- Prefer the `Edit` tool over `sed` for source edits (Bash classifier had intermittent outages).
- `bin/cryo` is the pin; **don't re-pin** until Jake asks and full gates are green.

## Key files
- `ARENA_PLAN.md` — full diagnosis, the Path B spike, measurements, dead ends. Keep updated.
- `stdlib/alloc/arena.cryo` — `Arena` (bump/reset/release/owns/grow) + per-target globals + chunk
  alloc/release. **The mmap fix goes here.**
- `stdlib/alloc/allocator.cryo` — `GlobalAlloc` chokepoint + `global_heap_alloc`/`global_heap_free`.
- `compiler/src/compiler/codegen/ast_arena.cryo` — `cryo_ast_arena_reset/release` + `g_ast_arena`.
- `compiler/src/compiler/instance.cryo` — per-target activation bracket (~1232 activate / ~2065,2164
  deactivate) + teardown boundary (~772-778, where Path B drop + probe removal go).
- `compiler/src/compiler/resolver/intern_table.cryo` — interned-string byte copies (rerouted, KEEP).
- Probe-only (REMOVE): `malloc_stats` in `stdlib/ffi/libc.cryo` + its call in `instance.cryo`.

## TL;DR for the impatient
1. Re-run the `malloc_stats` probe; read the lib-target `POST-TEARDOWN` block → confirm direct-malloc
   residual size.
2. Replace the 64 MiB chunk hack with real `mmap`/`munmap` (+`VirtualAlloc`/`VirtualFree`) at 1 MiB.
3. Pull the direct-malloc residual into the arena (earlier activation and/or reroute lexer/parser/
   type_resolution string copies) — this is what drops the peak.
4. Add Path B context drop (smaller win, proven safe).
5. Remove probes; determinism gate each step; `make selfhost-check` + `make test` on your box; then Jake re-pins.
