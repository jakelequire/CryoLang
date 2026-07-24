# HANDOFF — Cryo `async`/`await` (Track 3), Phase 3: **the executor**, stage (c) — multi-thread + `catch_unwind` isolation

You are picking up Cryo's stackless, poll-driven `async`/`await` implementation on a (probably) **fresh machine**.
**Phases 0–2 (design, stdlib core types, the entire compiler lowering) and Phase-3 stages (a) + (b) are done
and validated.** An `async fn` lowers to a `Future` state machine for all common control flow, and there is now
a working **single-thread executor** (`Executor`, `spawn`/`JoinHandle`, `block_on`, `join`/`detach`/`abort`).
**Your job is Phase 3 stage (c): make it multi-threaded** — a `pthread` worker pool, a per-task atomic state
machine so no task is polled by two workers at once, `join`/`block_on` that block on a condvar (workers drive)
instead of pumping, and a **`catch_unwind` poll boundary** so a panicking task yields `Err(Panicked)` and its
siblings survive.

---

## 0. START HERE

1. **Read `ASYNC_IMPL.md` (repo root) top-to-bottom.** It is the single source of truth: the locked model and
   surface (§2, §4, §7), the phased plan (§5), the state-machine lowering design (§10), the build/gate procedure
   + landmines (§6), and the **Progress Log (§9, append-only)** whose **newest three entries** are your immediate
   context — the Phase-3 surface lock (all seven decisions + the design-review refinements) and stages (a) + (b).
   This HANDOFF is a boot doc that points AT that file; it does not duplicate the detail.
2. **Keep `ASYNC_IMPL.md` current** every session — update its Status Dashboard (§1) and append to the Progress
   Log (§9). It is both the plan and the cross-session memory. This HANDOFF is init-only (written to boot you;
   not a running log).
3. File-based memory lives at `~/.claude/projects/.../memory/` (index `MEMORY.md`). The async entries are indexed
   there; the freshest pre-Phase-3 entry is `async_track3_inc4c_2026_07_23.md`. **The authoritative Phase-3
   record is `ASYNC_IMPL.md` §9** — trust it over any memory summary.

## 1. Environment — VERIFY IT YOURSELF, DON'T ASSUME

**The previous session ran on Jake's Windows box** (`win32`, PowerShell primary, `CRYO_CC=gcc`). **You may be on
a different machine — check your own system context first and adapt.** A past handoff once wrongly claimed
"Linux codespace" and misled the agent; don't repeat that.

- On Windows: run `make` from **PowerShell** (not the Bash tool = Git Bash), `CRYO_CC=gcc` required. `bin/cryo`
  is the Linux ELF, `bin/cryo.exe` the Windows PE; `python scripts/verify-pin.py` checks both.
- **Stage (c) needs `--panic=unwind`, which is Linux/hosted only** (links libunwind + libgcc_s → needs libc;
  Windows unwind is a deferred abort stub). So the **task-isolation validation MUST run in WSL** (`bin/cryo` is
  the Linux ELF that runs there). The multi-thread executor itself works under the default `--panic=abort`
  (a task panic just aborts the process there — that's the accepted degradation, §7-6); only per-task isolation
  needs unwind.

**Verify the baseline at session start:**
```
git branch --show-current            # ll-impl
git log --oneline -3                 # HEAD was 10414486 when this was written (Phase 2 / Inc 4c)
git status --short                   # see the note below
python scripts/verify-pin.py         # expect: verify-pin: OK
```

**⚠ Stages (a)+(b) were UNCOMMITTED when Jake switched machines.** They live in these files — confirm they made
the trip before doing anything:
- `stdlib/future/executor.cryo` (**NEW** — the whole executor; must contain `Executor`, `spawn`, `JoinHandle`,
  `TaskShared`, `block_on`)
- `stdlib/future/waker.cryo` (**MODIFIED** — the expanded `Waker { wake_fn, clone_fn, drop_fn, data }`)
- `stdlib/future/_module.cryo` (**MODIFIED** — registers `future::executor`)
- `ASYNC_IMPL.md` (**MODIFIED** — the Progress Log through stage (b))

Depending on how Jake moved the repo, they may now be **committed** (check `git log`) or still **uncommitted**
(`git status`: `?? stdlib/future/executor.cryo`, `M` on the other three). Either is fine. **If
`stdlib/future/executor.cryo` is missing entirely, the work did not transfer — STOP and tell Jake** (do not
rebuild it from scratch silently). Sanity check it compiles: `CRYO_CC=gcc make stdlib` (expect 148 modules,
"Project compilation succeeded"), `verify-pin: OK`.

## 2. The non-negotiables (Jake's standing rules — mirror exactly)

- **Only Jake commits.** NEVER `git commit`, never co-author, no trailers. You MAY `make pin` at a clean green
  boundary and MUST leave the tree ready. Repin with **plain `make pin`** (never `CRYO_CC=gcc make pin` — a
  landmine), verify with `python scripts/verify-pin.py`. **Phase 3 is PURE STDLIB (no compiler source touched),
  so the compiler's selfhost IR is definitionally unchanged → NO REPIN** (stages a+b confirmed this). The
  relevant green check is `make stdlib` + `verify-pin: OK`; a full `selfhost-check` is unnecessary unless you
  touch compiler source. If you somehow do touch the compiler, gate with `selfhost-check` and diff `win-s2` vs
  `win-s3` `.ll` before considering a repin.
- **Real solutions, not workarounds.** Fix root causes in the shared layer; a correct change that breaks the
  build → FIX the build. (Stage (b) example: an unreschedulable `Pending` task is *reclaimed*, not leaked.)
- **When something genuinely needs Jake's opinion, ASK** (use the question tool) — for surface / semantics /
  one-way-door decisions with two defensible answers. Stage (c) has one such (see §4): does `Executor::new()`
  spin up worker threads (always-multi-thread), or stay single-thread with an opt-in threaded mode? Flag it.
- **Comments describe logic** (invariant + failure mode prevented), never project narrative — no
  dated/audit/phase/batch labels in code. `ASYNC_IMPL.md` is the exception (it IS the narrative).
- **Never run two heavy builds at once** (`make stdlib`/`cryo`/`test`/`selfhost`/`pin`, or a WSL build) →
  environmental **exit -15 (SIGTERM)** mid-compile. Serial only. **Never blind `git stash pop`.**
- Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)` over
  type-suffixed names; **emit bare integer literals** (`1`, not `1u32`) in committed/generated code where
  inference allows; pass owning aggregates BY POINTER; no `let` (use `const`/`mut`); enums can't have inline
  methods (separate `implement enum X {}`; structs can); **callbacks must be non-capturing** → thread state
  through an explicit `void*` ctx (the `Waker`/`CatchCtx`/thread-`Payload` idiom).

## 3. What exists now (the substrate you build on) — see `ASYNC_IMPL.md` §9 for full detail

- **`stdlib/future/`** — `Poll<T>`, the `Future` trait (`type Output`; `poll(mut &this, cx: Context*) ->
  Poll<This::Output>`), `Context { waker }`, and the **`Waker` — already expanded to the reserved Rust vtable
  shape** `{ wake_fn: (u8*)->void; clone_fn: (u8*)->u8*; drop_fn: (u8*)->void; data: u8* }` (POD/Copy;
  `wake`/`wake_by_ref`/`clone`/`noop`/`new`/**`new_nonowning`**). **DO NOT touch the Waker shape in stage (c)** —
  its `clone_fn`/`drop_fn` are deliberate noops (correct for a non-owning waker); they gain real bodies + a
  `Drop` impl only in **Phase 4** when the reactor stores wakers that outlive a poll. Free `future::block_on(fut)
  -> R` (the trivial single-future driver) stays as-is.
- **`stdlib/future/executor.cryo` (stages a+b — what you EVOLVE):**
  - Type-erased `Task` (box + monomorphized `poll_fn`/`drop_fn`/`dispose_fn` vtable) on a **singly-linked FIFO**
    ready-queue (`ExecInner { head, tail }`). A **re-enqueueing `Waker`** (`task_wake(data=Task*)` →
    `exec_enqueue`), with a `queued: boolean` guard against double-linking.
  - `TaskShared<O> { ctl: TaskCtl; result: O }` where the **non-generic `TaskCtl` (3×`Atomic<u8>`:
    `outcome`/`hs`/`cancel`) is at offset 0**, so the erased executor reads/writes state via
    `task.shared as TaskCtl*` without knowing `O`. A **2-actor handshake** on `ctl.hs` (mirror `thread::spawn`:
    `HS_LIVE`/`HS_TASK_FIN`/`HS_HANDLE_GONE`) decides who frees the block.
  - `Executor { inner }`: `spawn<F,O>(fut) -> JoinHandle<O>`, `spawn_detached<F,O>`, `block_on<F,O>(root) -> O`
    (spawns root, **pumps the queue on the calling thread**, unwraps), `run()`. `JoinHandle<O>`: `join(mut this)
    -> Result<O, JoinError{Cancelled; Panicked(PanicInfo)}>` (**single-thread: pumps then reads**), `detach`,
    `abort(&this)`, `is_finished`, **drop=detach**. `Executor::drop` reclaims still-queued tasks.
  - **Known gotcha:** a genuinely **zero-sized** hand-written future can't be boxed (`allocate(0)` → Err → panic;
    Windows `abort()` exits **code 3**). Real lowered async futures always carry a `state: u32` field so they
    never hit it. ZST-robustness is a deferred TODO (Rust uses `NonNull::dangling()`).
- **Substrate to mirror for stage (c) (grep the symbols — line numbers drift):**
  - `stdlib/thread/_module.cryo` — `spawn<C,T>` over `pthread_create`: `os_create_thread<C,T>` +
    `thread_trampoline<C,T>` (the addressable-generic-fn shape), the heap `Shared` + `Atomic<u8>` 2-actor
    handshake. **Model the worker pool + the completion signaling on this.**
  - `stdlib/sync/` — `Mutex<T,A>`, `CondVar` (`wait`/`notify_one`/`notify_all`), `Atomic<T>`
    (`load`/`store`/`swap`/`compare_exchange`/`fetch_add`), `mpsc` (a mutex+condvar FIFO — a ready reference
    for a guarded queue). No lock poisoning, no cross-thread panic-catch.
  - `stdlib/core/panic_unwind.cryo` (prelude) — `catch_unwind<T>(f: () -> T) -> Result<T, PanicInfo>` +
    `CatchCtx` + **`run_catch_body<T>`** (the non-capturing-fn-ptr + `void*` trampoline). **Model the poll-boundary
    `run_poll(ctx)` trampoline on `run_catch_body`.** `catch_unwind` is a **compile error under `--panic=abort`**
    (see §4 risk).
  - `runtime/panic/unwind/src/lib.cryo` — the in-flight-panic globals are `![thread_local]` (Track 2), which is
    exactly what makes poll-boundary `catch_unwind` on a worker thread sound (concurrent worker panics don't race).

## 4. YOUR TASK — Phase 3 stage (c): multi-thread worker pool + `catch_unwind` isolation

Evolve `stdlib/future/executor.cryo`. Build → validate under `--panic=unwind` in WSL → leave green → **no repin**
(pure stdlib). The pieces (design detail + rationale in `ASYNC_IMPL.md` §9 latest entry "NEXT: stage (c)"):

1. **Guarded ready-queue.** Put the `ExecInner` queue behind a mutex + condvar (embed pthread buffers like
   `mpsc::ChannelInner`, or compose `sync::Mutex`/`CondVar`). Workers block on the condvar when the queue is
   empty; `enqueue` signals it. Add a `shutdown` flag.

2. **`pthread` worker pool.** N workers (mirror `thread::os_create_thread` + a trampoline). Each worker loops:
   lock → wait while (empty && !shutdown) → dequeue → unlock → `drive_task`. Exit on shutdown once drained.
   `Executor::new()` sizes the pool (`thread::available_parallelism()`); add `Executor::with_threads(n)`.
   `Executor::drop`/`shutdown()` sets shutdown, broadcasts, joins workers.

3. **Per-task atomic state machine — the correctness crux.** With multiple workers, the stage-(a/b) `queued:
   boolean` is NOT enough: a task can self-wake (or be woken) mid-poll and be picked up by a second worker →
   **two workers polling one task = data race on its fields.** Replace `queued` with an `Atomic<u8>` state:
   **IDLE / SCHEDULED (in queue) / RUNNING (being polled) / NOTIFIED (woken while running)**. `wake()`: CAS
   `IDLE→SCHEDULED` (and enqueue) or `RUNNING→NOTIFIED` (defer — do NOT enqueue). Worker: dequeue a SCHEDULED
   task → CAS `SCHEDULED→RUNNING` → poll → on `Pending`, CAS `RUNNING→IDLE`; if that fails (state is NOTIFIED)
   → set `SCHEDULED` + re-enqueue. This is the standard tokio/`async-task` machine and guarantees a task is
   enqueued at most once and polled by at most one worker. **NB (design-review correction, durable):** this is a
   per-task *state machine*, **NOT a refcount** — `thread::spawn` detaches with a 2-state swap and no refcount,
   and Phase-3 wakers don't escape a poll. The waker-held **refcount** (and the `Waker` `clone_fn`/`drop_fn`
   bodies) are **Phase 4** (when the reactor stores wakers). Don't build refcounting here.

4. **`join`/`block_on` block on a condvar instead of pumping.** With workers driving, `join` waits on a
   completion condvar (signal it in `finish_task`, after the `outcome.store` + `hs.swap`) until `outcome !=
   PENDING`, then reads the result exactly as stage (b) does (the `hs` handshake already decides task-vs-handle
   ownership; join implies handle-present so the task left the block for it). `block_on` = `spawn(root).join()`.
   A simple global "something finished" condvar on the executor + a `while outcome==PENDING { wait }` re-check is
   the least-machinery correct option.

5. **`catch_unwind` at the poll boundary → task isolation.** Wrap the future's `poll` in `catch_unwind` via a
   `run_poll(ctx: void*)` trampoline modeled on `run_catch_body` (non-capturing fn-ptr + `void*`). A caught
   panic → `finish_task(outcome = PANICKED)`, stashing a `PanicInfo` so `join` returns `Err(Panicked)`.
   **⚠ THE KEY RISK: `catch_unwind` is a COMPILE ERROR under `--panic=abort`,** so the executor cannot
   unconditionally reference it — the whole module would fail to compile for any consumer building with the
   default abort strategy. **You must gate the catch path on the panic strategy** so that under abort the poll
   boundary is a plain direct call (task panic aborts, per §7-6) and under unwind it goes through `catch_unwind`.
   **First thing to investigate:** does Cryo's ConfigGating support a panic-strategy predicate analogous to
   `![target(unix)]` (e.g. `![panic(unwind)]`/`![panic(abort)]`)? Grep the compiler's ConfigGating +
   `panic_unwind.cryo`'s own gating. If such a predicate exists, gate two `poll_boundary` free functions on it.
   If it does NOT, that gating mechanism is a prerequisite — surface it to Jake before hacking around it (do not
   `#ifdef`-smuggle or leave the executor unwind-only). This is the single most likely place to get stuck.

**Validation (WSL, `--panic=unwind`):** a probe with N tasks where one panics — assert the panicking task's
`join` returns `Err(Panicked)` and the siblings still complete with their values (observe via an atomic counter;
native exit doesn't flush stdio → assert via exit code, read Drop/at-exit prints on stderr). Also re-run the
stage-(a)/(b) probes (`scratchpad/async_exec_a`, `async_exec_b`) under the multi-thread executor as regressions,
and confirm the default `--panic=abort` build still compiles + runs (isolation absent, but the executor works).

**Surface question to flag to Jake (one-way-door-ish):** does `Executor::new()` spin up worker threads (so
`Executor` is *always* a pool, and `join`/`block_on` always condvar-wait), or does the single-thread pump model
stay as `Executor::new()` with a separate `Executor::with_threads(n)` for the pool? The former is simpler
(one behavior) but means even a trivial `block_on` starts threads; the latter keeps two code paths. Bring a short
recommendation and let Jake pick before hardening around it.

## 5. Build / gate / validate

- **Stdlib type-check (the fast loop):** `CRYO_CC=gcc make stdlib` (compiles all stdlib with the pinned
  compiler; ~fast). Green = 148 modules "Project compilation succeeded".
- **Probe:** a scratchpad project (`cryoconfig` = `[project]` name/output_dir=build/target_type=executable/
  source_dir=src/entry_point=src/main.cryo + empty `[compiler]`/`[dependencies]`), `import std::future;` +
  `import std::future::executor;` + `import std::sync::atomic;`. Build with the pinned compiler:
  `CRYO_CC=gcc CRYO_STDLIB=<repo>/stdlib <repo>/bin/cryo(.exe) build` from the project dir (it recompiles the
  stdlib sources it imports, so it picks up your `executor.cryo` edits). Assert via **exit code**.
  - **Single-thread probes** run native. **Multi-thread + isolation probes need `--panic=unwind` → build & run
    in WSL** (`bin/cryo` = Linux ELF). Pass the unwind panic mode via the project's compiler flags / cryoconfig.
  - **Zero-sized-future trap:** give probe futures at least one field (a fieldless struct → `allocate(0)` → Err).
- **No repin** for a pure-stdlib change; `make stdlib` green + `verify-pin: OK` is the boundary. Leave the tree
  green and this doc + `ASYNC_IMPL.md` current. **Do NOT commit.**

## 6. Landmines & durable gotchas (also `ASYNC_IMPL.md` §6 + the Progress Log)

- **NEVER two heavy builds at once** → environmental exit -15 (SIGTERM). Serial only.
- **`--panic=unwind` is Linux/WSL only** — Windows unwind is an abort stub, so isolation MUST be validated in
  WSL. Don't try to prove it on native Windows.
- **`catch_unwind` won't compile under `--panic=abort`** — see §4 risk; the poll boundary must be strategy-gated.
- **Callbacks are non-capturing** — the worker trampoline, `run_poll`, and the waker are bare fn-ptrs + `void*`.
- **Concurrent poll is UB** — the per-task IDLE/SCHEDULED/RUNNING/NOTIFIED CAS machine (§4.3) is not optional
  once there are ≥2 workers.
- **Windows `abort()` exits code 3** — a panic under abort looks like a `return 3` from a probe; don't be fooled.
- **Native exit doesn't flush stdio** — assert via exit code; read at-exit/Drop prints on fd 2 (stderr).
- **`cdebug(fmt, …)`** (needs `import Utils::Logger;`) is a `--debug`-gated stderr printf for tracing new runtime
  code — REMOVE before leaving green.
- **PowerShell cwd drifts** on Windows — if `make` says "No rule to make target", `Set-Location <repo>` first.
- **Anchors drift; symbols are durable.** Re-grep.

## 7. Key files (grep the symbol — line numbers drift)

- **Evolve:** `stdlib/future/executor.cryo` (the whole executor — stages a+b).
- **Model on / mirror:** `stdlib/thread/_module.cryo` (worker pool / trampoline / handshake), `stdlib/sync/`
  (`mutex`/`condvar`/`atomic`/`mpsc`), `stdlib/core/panic_unwind.cryo` (`catch_unwind`/`run_catch_body`/
  `PanicInfo` → the `run_poll` trampoline), `runtime/panic/unwind/src/lib.cryo` (the `![thread_local]` panic
  globals that make worker isolation sound).
- **Do NOT touch** (already correct): `stdlib/future/waker.cryo` (shape reserved for Phase 4), the sample futures
  in `stdlib/future/ready.cryo`, and any compiler source (Phase 3 is pure stdlib).
- **Reference:** `ASYNC_IMPL.md` (source of truth — §5 Phase 3, §7 decisions, §9 Progress Log's newest three
  entries).

**First action:** confirm your OS/shell, verify the baseline + that stages (a)+(b) transferred (§1), read
`ASYNC_IMPL.md` (esp. §9's newest three entries and §7), then investigate the panic-strategy gating for
`catch_unwind` (§4.5) and bring Jake the `Executor::new()`-spins-workers surface question (§4) before building
the pool out. Do NOT commit; leave the tree green for Jake.
