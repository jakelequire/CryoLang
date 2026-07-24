# ASYNC_IMPL.md — Cryo async/await: plan + tracker

Single source of truth for implementing **stackless `async`/`await`** in Cryo, end to end. This file is
both the **design/plan** and the **progress tracker** — update the Status Dashboard and Progress Log as
work lands. It is self-contained: everything a fresh agent needs is here (the prior scattered runtime
markdown was consolidated into this file).

Owner: Jake. Implementer: whoever picks this up. Work spans many sessions — leave the tree green and this
doc current at every stop.

---

## 0. Rules (Jake's — standing, mirror exactly)

1. **Only Jake commits.** Never `git commit`, never co-author. You MAY `make pin` at a clean green
   boundary and MUST leave the tree ready for Jake to commit.
2. **Repin BOTH OSes** with plain `make pin` (writes `bin/cryo` ELF + `bin/cryo.exe` PE + the two
   `.pin.txt` sidecars). **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `python scripts/verify-pin.py`. Repin ONLY when a change moves the **default-path (`--panic=abort`) IR**
   — the async lowering will, once it emits anything.
3. **Right decision over green. No workarounds, no half-baked solutions.** Fix root causes in the shared
   layer; if a correct change breaks the build, FIX the build. Async is large — resist special-casing.
4. **Comments describe the logic** (invariant + failure mode prevented), never project narrative — no
   dated/audit/phase/batch labels in code. This doc is the exception (it IS the narrative).
5. Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
   over type-suffixed names; avoid suffixed numeric literals (`5i64`); pass owning aggregates BY POINTER.
6. When something genuinely needs Jake's opinion, ASK.** Use the question
tool. For language-semantics / semver decisions with two defensible answers,
not for routine judgement calls.
---

## 1. Status Dashboard  (update every session)

| Phase | What | Status |
|---|---|---|
| 0 | Design lock-down (surface, trait shapes, lowering placement) — get Jake's sign-off | ✅ done — locked 2026-07-22 (§7 filled) |
| 1 | Core types in stdlib (`Poll`/`Future`/`Context`/`Waker`) + `block_on` + hand-written futures | ✅ done+validated 2026-07-22 (no repin) |
| 2 | Compiler: `async fn` parse + state-machine lowering + `await` desugar | ✅ DONE+validated (2026-07-23) — parse + no-await (1b) + single await (2) + N straight-line (3) + awaits across `if`/`else` (4a) + awaits across `while`/`for`/`loop`/`do-while` + `break`/`continue` + `mut` loop-carried promotion (4b) + scope-aware alpha-rename, all committed through `5e28a74f`. **Inc 4c DONE (2026-07-23): `await` inside a `match` arm — dispatch `match(subj)` → per-arm entry states → join; scalar pattern bindings captured to fields (aggregate→E0600, ref→E0455); pattern bindings alpha-renamed for by-name soundness; non-exhaustive match gets synth `_ => join`. Plus the bare-block-with-await warm-up. Both-OS fixed point, `win-s2`/`win-s3` = 0/235 `.ll` → NO REPIN, UNCOMMITTED.** All common control flow now lowers → Phase 2 complete. |
| 3 | Executor + `Waker` + `spawn`/`JoinHandle`; multi-thread; poll-boundary `catch_unwind` isolation | ✅ DONE+validated (2026-07-24) — surface LOCKED 2026-07-23. **(a)** single-thread executor + ready-queue + re-enqueueing Waker; **(b)** `spawn`/`JoinHandle` (Output via `TaskShared<O>`), `block_on`, `join`/`detach`/`abort`, drop=detach; **(c)** pthread worker pool + per-task atomic run-state (IDLE/SCHEDULED/RUNNING/NOTIFIED) + condvar `join`/`block_on` + `catch_unwind` poll-boundary isolation. Needed a NEW compiler `![config(panic_unwind)]` gating atom (see §9) → Phase-1 both-OS REPIN (selfhost fixed point, 235 `.ll`). Executor is self-contained (own pthread wrappers, no `thread::Scope` dep). Validated on Linux: regression 30/30, isolation 30/30. UNCOMMITTED. |
| 4 | Reactor (epoll/IOCP) + async I/O over `std::net` + timers + `async fn main` + combinators | ◐ in progress 2026-07-24. Forks LOCKED: waker lifetime = **separate `Arc`-wrapper**; reactor = **dedicated thread, per-`Executor`** (`![thread_local]` current-reactor handle); platform = **both OSes (epoll + IOCP)**. **Inc 4a DONE+validated (2026-07-24): Arc-refcounted `Waker` (Copy→non-Copy) + `Arc<Task>` lifetime; finish-vs-park now implicit via `Task::drop` on the last decref. Needed a COMPILER fix (owner-generic static with a defaulted owner param + nested return → E0636; `call_resolver.cryo` default-backfill) → selfhost fixed point BOTH OS, win-s2 vs win-s3 = 0/235 `.ll`, REPINNED both OS. UNCOMMITTED.** NEXT = Inc 4b (the reactor: epoll+IOCP interface — bring Jake the readiness-vs-completion unification fork). |

Legend: ☐ not started · ◐ in progress · ✅ done+validated · ⏸ blocked (note why in the Progress Log).

**Current HEAD baseline:** `8e5a7694` (Phase 3 stage (c) — multi-thread executor + `catch_unwind` isolation +
the `panic_unwind` config atom — committed + repinned by Jake). Branch `ll-impl`. Pin verifies OK, tree clean
(only `HANDOFF.md` touched). `make stdlib` = 148 modules green. Phases 0–3 done + committed. NOTE: the §9
stage-(c) entry's "UNCOMMITTED" is stale — that work is committed in `8e5a7694`.

**✅ FIXED (2026-07-23) — `block_on` (and every where-bound-only-param generic) now infers without a
turbofish.** Was: `block_on(fut)` — and any `f<F, R>(fut: F) -> R where F: Future<R>` — failed **codegen
`E0636`** when `R` appeared only in the return type + where-bound (never a parameter), because argument
inference bound `F` but nothing projected `R = F::Output`; the "all bound" gate then bailed without stashing,
so mono couldn't specialize. Fix = a **bound-directed projection pass ("PASS D")** in
`CallResolver::check_generic_free_call` + its qualified twin `infer_free_call_bindings`: after arg inference,
for each where-bound `P: Trait<…, R, …>` whose subject `P` is bound to a concrete type, project the trait's
positional-sugar associated type off `P` (`resolve_concrete_member(P, "Output")`) and fill the still-unbound
arg param `R`. Mirrors the impl-side `TypeResolver::derive_where_assoc_bindings`. Now `block_on(fut)` (incl.
on an **unnameable async future** — `block_on(add_two())`), `future::block_on(…)`, and `from_iter(it)` all
resolve from arguments alone; turbofish / expected-type / non-`Future`-rejection paths unchanged. Gates: both
OS fixed points, `win-s2` vs `win-s3` = 0 `.ll`, Linux normalized pinned-vs-new = 0 residual → **no repin**.
**The inline `loop { match f.poll(&cx) }` async-validation workaround is retired — use `block_on`.**

**Generic-METHOD analogue — deferred (NOT a simple mirror; deeper pre-existing blockers).** A method
`m<F, R>(fut: F) -> R where F: Future<R>` (static `Owner::drive(fut)` or instance `obj.run(fut)`) is a bigger
job than the free-fn fix: (1) `infer_generic_method_bindings` (the function the free-call fix's twin lives
next to) is **not even reached** for these calls — static generic-method calls resolve through a different
scope-resolution path; and (2) generic methods of this shape **fail even WITH a turbofish** (E0633/E0636
cascade), and even a trivial `Bo::id<i32>(42)` on a non-generic owner fails **E0200** (return-type
substitution) — so the generic-method-with-where-bound machinery has multiple independent gaps, of which
inference is only one. Fixing it means untangling the static/instance generic-method resolution+mono paths,
not adding one projection pass. Left as a separate effort. The free-function fix (which covers `block_on`,
`from_iter`, and any `f<F,R> where F: Trait<R>` free call) fully lands the async driver.

---

## 2. The locked model (do NOT re-litigate)

**Stackless, poll-driven state machine, Rust-style. LOCKED by Jake (2026-07-21).**

- The compiler lowers an `async fn` to a **poll-driven state machine** — NOT stackful (no per-coroutine
  stack, no stack-switch trampoline, no green threads). Threads stay OS threads (`pthread_create`).
- A panic inside an `async fn` unwinds the **native** frame of whoever called `poll()` — there is no
  coroutine stack to walk, so the unwinder is unchanged. Async surfaces panics through the existing
  `__cryo_panic` funnel via the poll boundary.
- Task-local state lives as **ordinary struct fields** in the generated state machine — no new TLS
  requirement beyond the `![thread_local]` panic state Track 2 already delivered.

Consequence: this is a **codegen + stdlib + runtime** effort. No new unwinder work, no new TLS primitive.

---

## 3. What already exists (the substrate)

**Async surface today — the entire starting point:**
- Lexer: `async` (`KwAsync`) and `await` (`KwAwait`) are keywords (`compiler/src/compiler/lex/_module.cryo`).
- Parser: `await <unary-expr>` parses to `AwaitExprNode { operand }`
  (`parser/expr_parser.cryo::parse_await_expression` ~1672; dispatched ~640).
  `AwaitExprNode` is defined in `AST/expression.cryo` ~848 and handled by cloner/dumper/substituter/
  node_locator. A sibling `yield <expr>` → `YieldExprNode` also parses (generators — OUT of scope here).
- **`async fn` does NOT parse.** `KwAsync` is tokenized but consumed NOWHERE; there is no `is_async` field
  on `FunctionDeclNode`. `async fn f()` is currently a parse error.
- Sema visits `AwaitExprNode` as a plain expression (`sema/sema.cryo` ~1130) — no async typing.
- Codegen: `await` hard-errors — `codegen/visit/ir_generator.cryo` ~1946 ("await expressions are not
  implemented"). **This is the codegen seam to replace.**
- **No `Future`, `Poll`, `Context`, `Waker`, executor, or reactor exist** in `stdlib/` or `runtime/`.

**The panic seam async plugs into (done — Tracks 1 & 2):**
- `runtime/panic/unwind/src/lib.cryo`: `__cryo_panic` (raises via libunwind), `__cryo_personality_v0`
  (LSDA parse + two-phase cleanup), and the now-`![thread_local]` in-flight-panic globals
  (`g_exn`, `g_panic_msg/_file/_line`) — so concurrent panics on worker threads don't race. This
  thread-local isolation is **exactly** what makes poll-boundary `catch_unwind` on a worker sound.
- `stdlib/core/panic_unwind.cryo` (prelude): `catch_unwind<T>(f: () -> T) -> Result<T, PanicInfo>`,
  `PanicInfo { msg, file, line }`, `run_catch_body<T>`. Task isolation reuses this verbatim.
- `--panic=unwind` is Linux/hosted only (links libunwind + libgcc_s → needs libc). Windows unwind is a
  deferred abort stub. Under the default `--panic=abort`, a panic aborts the process — so async task
  isolation degrades to "a task panic aborts the process" under abort (accepted; confirm in Phase 0).

**Substrate for executor/reactor:**
- `stdlib/thread/_module.cryo`: `spawn<C,T>(ctx, body) -> JoinHandle<T>` over `pthread_create`, with a
  heap control-block state machine + `join()`. Mirror this for the worker pool and `JoinHandle`.
- `stdlib/net/`: blocking sockets (to make async). `stdlib/sys/syscall.cryo`: raw syscalls — grep for
  `epoll_*` (Linux reactor) before assuming they exist.

---

## 4. Target design (Rust-analogous — LOCK the specifics in Phase 0)

- `async fn f(args) -> T` lowers to a fn returning an **anonymous generated state-machine struct** that
  implements a `Future` trait: `poll(mut &this, cx: &mut Context) -> Poll<T>`.
- Locals live across an `.await` become **fields** of that struct. Each `await` is a **suspend state**;
  `poll` is a resumable `switch (this.state)` that re-enters at the correct point.
- `await e` desugars to (roughly):
  `loop { match (e.poll(cx)) { Poll::Ready(v) => { break v; } Poll::Pending => { <save state>; return Poll::Pending; } } }`.
- Core types (stdlib): `Poll<T> { Ready(T); Pending }`; `Future` trait (`type Output`,
  `poll(mut &this, cx: &mut Context) -> Poll<Output>`); `Context { waker: Waker }`; `Waker` (wake callback
  + data pointer).
- Runtime: `block_on(fut)` (single-thread driver) first; then a multi-thread work-stealing executor;
  then a `Reactor` (epoll Linux / IOCP Windows) that parks Pending I/O and `wake()`s tasks.
- Task isolation: `catch_unwind` at the poll boundary (sound because of Track 2).

**The subtlest correctness question — self-reference / pinning. DECIDED (2026-07-22): forbid borrows held
live across an `await`.** Rust needs `Pin` because a future may hold a pointer into its own fields (a borrow
held across an await); moving such a future dangles the internal pointer. A *real* (Rust-sound) `Pin` is
**infeasible in current Cryo** — it rests on lifetimes + a borrow checker + an enforcing `unsafe`, all of
which Cryo deliberately lacks (`docs/cryo.md:2314`, `:2369`, `:1030-1041`); a `Pin<T>` we could write would
enforce nothing (safety theater). Instead we make self-referential futures *impossible to construct*: the
move-checker (`passes/move_check.cryo` — Cryo's one enforced ownership mechanism, a hard error) rejects any
reference held live across an `.await`. No self-refs ⇒ nothing to pin ⇒ futures are freely movable ⇒ no
`Pin` type, no address-stability requirement. Owned values across await are fine (that is the normal case
the state machine promotes to fields); only *reference-typed* values across await are rejected. This is a
strict subset of Rust — relaxable later (add lifetimes/Pin) without breaking code. Cost: `async fn f(x: &T)`
can't use `x` after an await, and you can't hold `&mut self.field` across await (owned-value rewrites exist).

---

## 5. Phased plan (each phase: build → validate with probes → leave green → update this doc)

### Phase 0 — Design lock-down (NO code) — get Jake's sign-off before Phase 2
Decide and record here (§7 has the open questions), then get Jake to APPROVE — the trait/surface is a
one-way door like the stackless decision was:
- Await syntax (keep prefix `await e`, or add postfix `e.await`).
- Exact `Future`/`Poll`/`Context`/`Waker` signatures; the **pinning/self-reference story** (§4).
- WHERE the async→state-machine lowering runs in the pipeline (recommend: an AST desugar that emits an
  ordinary struct + `poll` method so the generated code rides normal sema + mono + drop-insertion + codegen
  — confirm this composes with mono-after-sema).
- Executor surface (`block_on`/`spawn`; is there a default runtime; does `async fn main` imply one).
- Abort-mode degradation (task panic aborts under `--panic=abort`).
- **Deliverable:** §7 filled in + Jake's lock recorded in the Progress Log.

### Phase 1 — Core types (stdlib only; NO compiler change; testable immediately)
`Poll<T>`, `Future` trait, `Context`, `Waker` in a new `stdlib/async/` (name TBD). Hand-write a couple of
`Future`s (`Ready<T>`; a "pending N times then Ready" counter) and a trivial single-thread `block_on` that
loops `poll` with a no-op waker. **Validate:** a hand-written future runs through `block_on` end to end.
These types are generic/lazy → default selfhost IR untouched until instantiated → **no repin**.

### Phase 2 — `async fn` parse + state-machine lowering (the hard core; moves default IR → repin)
(a) Parse `async fn`: add `is_async` to `FunctionDeclNode` (+ clone it), consume `KwAsync` as a fn modifier.
(b) Lower each `async fn` into a generated `Future`-implementing struct + `poll`: split the body at `await`
    points into states; promote across-await locals to fields; emit the resumable `switch`.
(c) Lower `AwaitExprNode` into the poll-loop/suspend desugar.
Build up by control-flow complexity, validating each under `block_on`: no awaits (immediately-Ready) → one
await → straight-line awaits → **awaits across branches/loops** (the hard part — mirror how a real stackless
lowering handles control flow crossing a suspend). Repin at each green boundary.

### Phase 3 — Executor + Waker + task spawn
Real single-thread executor + task queue; a `Waker` that re-enqueues its task; `spawn(future) -> JoinHandle`.
Then multi-thread it (worker pool over `pthread`) with `catch_unwind` at the poll boundary. **Validate:**
task isolation — a panicking task yields an error, siblings keep running (needs `--panic=unwind`).

### Phase 4 — Reactor + async I/O
epoll (Linux) / IOCP (Windows) reactor that parks Pending I/O and wakes tasks; async adapters over
`std::net`; timers; `async fn main`; `join`/`select` combinators as needed. May itself be split; Windows
IOCP may be deferred. Keep `--panic=abort` (default) working throughout — async must not REQUIRE unwind.

---

## 6. Build / gate / repin + landmines

**Build/test/gate (run `make` from PowerShell, NOT the Bash tool = Git Bash; WSL via a script file):**
- `CRYO_CC=gcc make cryo` (~1.5 min). `make test` does NOT rebuild the compiler — run `make cryo` first.
- Selfhost canary (~5-7 min): `CRYO_CC=gcc make selfhost-check` → exit 0 + TWO `FIXED POINT OK`. Repin test
  = diff `compiler/build/self/win-s2/**/*.ll` (pinned) vs `win-s3/**/*.ll` (new): 0 = no repin, non-zero =
  repin. `make selfhost-check` clobbers `compiler/build/cryo.exe` to a Linux ELF → `make cryo` again after.
  Tee'd log is UTF-16 → PowerShell `Select-String`.
- Async probes are HOSTED (need threads/I/O): build with `CRYO_CC=gcc`, run on Linux/WSL (`bin/cryo` is a
  Linux ELF that works in WSL). Model on the exit-code-as-observed-value probe style. The runtime tier is a
  SEPARATE build (`cd runtime && <cryo> build`).
- Repin: `make pin` (plain, never `CRYO_CC=gcc`), then `python scripts/verify-pin.py`. **Do NOT commit.**

**Landmines (paid for in blood):**
- **NEVER two heavy builds at once** (`make cryo`/`test`/`selfhost`/`pin` or a WSL build) → environmental
  **exit -15 (SIGTERM)** mid-compile. Serial only.
- **NEVER blind `git stash pop`** — one applied an unrelated parked stash and conflicted, re-creating
  deleted files. Prefer `git checkout <commit> -- <file>` / copy-aside.
- **Incremental-cache staleness:** if a compiler-source edit seems ignored, clear
  `compiler/build/target/release/host*/local/incremental` and `rm compiler/build/cryo*`, then `make cryo`.
- **OS-clobber:** the runtime tier's Linux objects get overwritten to Windows PE by a Windows build (and
  vice-versa) — rebuild the tier for the OS you're testing.
- **`cdebug(fmt, …)`** (from `Utils::Logger`) is a `--debug`-gated stderr printf — the clean way to trace a
  new pass; REMOVE before repin. `--debug` also dumps the AST to stdout (huge) → redirect + `Select-String`.
- **Native exit doesn't flush stdio** — observe at-exit/Drop prints on fd 2 (stderr).
- **Anchors drift; symbols are durable.** Re-grep.

---

## 7. Locked design decisions (Phase 0 — Jake signed off 2026-07-22)

All grounded in three recon sweeps of the real codebase (see the Progress Log for the substrate facts).

1. **Await syntax → prefix `await e`.** Already lexes+parses into `AwaitExprNode` (`expr_parser.cryo:1672`);
   every AST walker (cloner/substituter/name-res/dead-code/node-locator) already has an await visitor. Zero
   parser work. Postfix `e.await` reconsidered post-v1 (no correctness gain, needs member-access-on-keyword).

2. **Core types (`stdlib/async/`) — LOCKED signatures:**
   ```cryo
   type enum Poll<T> { Ready(T); Pending; }
   type struct Waker { wake: (u8*) -> void; data: u8*; }   // manual vtable, non-owning, Copy
   type struct Context { waker: Waker; }
   type trait Future {
       type Output;                                         // associated type (Iterator-style)
       poll(mut &this, cx: Context*) -> Poll<This::Output>;
   }
   ```
   - Associated `Output` (not a generic `Future<Output>` param): a future has exactly one output type; matches
     `Iterator { type Item; }` precedent (`stdlib/core/iter.cryo:29`). Projection is `F::Output`.
   - `mut &this` is Cryo's mutable-self receiver (confirmed; `&mut this` does not exist).
   - `cx: Context*` (raw pointer) matches Cryo's pervasive pass-aggregates-by-pointer idiom and threads
     trivially down the poll tree. `Waker` mirrors the existing `CatchCtx`/thread-`Payload` shape (non-capturing
     fn-ptr + raw `void*`) — sidesteps `dyn` (post-1.0). **No blanket impls** exist, so every concrete future
     emits its own `implement trait Future for struct <generated> { type Output = …; poll(…){…} }`.

3. **Self-reference / pinning → forbid borrows held live across `await`** (enforced in `move_check.cryo`). See
   §4 (DECIDED). No `Pin` type; no self-referential futures ⇒ futures freely movable. Rationale: a real Pin is
   infeasible without lifetimes + enforcing `unsafe` (Cryo has neither); the move-checker is the one enforced
   mechanism, so "forbid the dangerous pattern" is the only *soundly-enforced* option.

4. **Lowering placement → AST desugar in the `FunctionBodyTypeCheck` window** (before mono; mono-after-sema
   confirmed). Hook at `sema/sema.cryo:261 visit(FunctionDeclNode*)`, gated on a new `FunctionDeclNode.is_async`
   flag; reuse the closure-lowering machinery (`sema/lambda_synth.cryo:239-423` is the working precedent:
   `new …Node` → `arena.create_struct`/`set_fields`/`add_method` → `decl_index.register_type_with_module` +
   `register_methods_with_module` → `ctx.artifacts.ast.root.add_statement(...)`). The generated struct + `poll`
   then rides normal mono → move-check → drop-insertion → type-lowering → codegen with no special-casing;
   `AwaitExprNode` is rewritten to the poll-loop/suspend desugar so the `ir_generator.cryo:1944` hard-error
   becomes dead. **Phase-2 risk to resolve then (not a Phase-0 lock):** a *generic* async fn has a symbolic body
   until mono — decide whether its lowering runs pre- or post-mono (mirror how closures inside generic fns are
   handled). The lowering must also compute the live-across-await local set (⇒ struct fields), which is exactly
   where the "reference live across await" check (decision 3) is emitted.

5. **Executor surface → `block_on` first, `spawn` later; no implicit global runtime for v1.**
   - Phase 1/3: `block_on<F>(fut: F) -> F::Output where F: Future` (single-thread driver; holds `fut` in a fixed
     slot). Phase 3: `spawn<F>(fut) -> JoinHandle<F::Output>` — heterogeneous futures stored in the task queue
     via **manual type erasure** (box + monomorphized poll thunk, same vtable trick as `Waker`; mirror
     `thread::spawn`'s heap `Shared` control block + `Atomic<u8>` state machine).
   - `async fn main` → a Phase-4 nicety desugaring to `fn main() { block_on(__async_main()); }`. Not v1-critical.

6. **Abort-mode degradation → accepted.** Under the default `--panic=abort`, a task panic aborts the process
   (`catch_unwind` is a *compile error* under abort). Per-task isolation (panic → error, siblings survive)
   requires `--panic=unwind` (Linux/hosted; Track 2's thread-local panic state makes it sound at the poll
   boundary). Async **works** under abort — it just lacks isolation. Async must never *require* unwind.

7. **Module home → everything under `stdlib/async/`** (core types + `executor` + `reactor`), same layer as
   `thread`/`net`. The `runtime/` tier stays separate. `Poll`/`Future` start in `stdlib/async/` (explicit
   import); promotion to a prelude deferred.

**Substrate follow-ups surfaced in recon (not Phase-0 blockers):** Linux reactor is fully wired (`epoll_*`,
`eventfd`, `timerfd` bound at libc + raw-syscall layers); Windows reactor (IOCP/kqueue **do not exist**) needs
new bindings → defer per plan; sockets need a `set_nonblocking` added (trivial — the `fcntl`+`O_NONBLOCK`
pattern already lives in `process/command.cryo:796`, Windows `ioctlsocket`+`FIONBIO` in `syscall.cryo:2156`).

---

## 8. Key files & anchors (grep the symbol — line numbers drift)

- Async surface: `lex/_module.cryo` (`KwAsync`/`KwAwait`); `parser/expr_parser.cryo`
  (`parse_await_expression` ~1672, dispatch ~640); `AST/expression.cryo` (`AwaitExprNode` ~848);
  `AST/declaration.cryo` (`FunctionDeclNode` — add `is_async`); `AST/cloner.cryo` (clone the new field);
  `sema/sema.cryo` (`AwaitExprNode` visit ~1130); `codegen/visit/ir_generator.cryo` (~1946 the seam).
- Panic seam (done): `runtime/panic/unwind/src/lib.cryo`; `stdlib/core/panic_unwind.cryo` (prelude).
- Substrate: `stdlib/thread/_module.cryo` (spawn/JoinHandle/pthread); `stdlib/net/`;
  `stdlib/sys/syscall.cryo` (epoll?).
- Pipeline: Frontend → ModuleResolution → DeclCollection → TypeResolution → SemanticAnalysis →
  Specialization(mono) → CodegenPrep → IRGen → Optimization (`compiler/src/compiler/instance.cryo`).
  Generics are monomorphized (each concrete `Future` is a mono instance).

---

## 10. Phase 2 — state-machine lowering design (detailed implementation plan)

Grounded in the `lambda_synth.cryo` synthesis precedent + a full trait-impl-registration recon. This is the
implementation guide; the hard part (Inc 4) needs Jake's sign-off before coding.

**Placement.** Lower during `FunctionBodyTypeCheck`, in `sema/sema.cryo:261 visit(FunctionDeclNode*)`, AFTER
`this.visit(node.body)` types the body against `Output` and `check_function_returns` runs. Gate on
`node.is_async && !node.is_generic()` (generic async fns deferred — their bodies are symbolic until mono; see
the risk note). New helper `sema/async_lower.cryo` (`AsyncLower`), wired as a sema field exactly like
`LambdaSynth` (`sema.cryo:148`/`:167`; `_module.cryo:33`).

**Generated shapes** for `async function f(p0: A, p1: B) -> T { body }` (name `Q = <ns>::f$Future`, minted
like `lambda_synth.cryo:234`):
1. Struct `f$Future`: field `state: u32` (resume discriminant) + one field per param (`p0: A`, `p1: B`),
   captured like closure captures. (Await increments add promoted across-await locals + per-await sub-future
   storage fields.)
2. Method `poll(mut &this, cx: Context*) -> Poll<T>`: an arg-shadow prelude (`const p0 = this.p0;` …, exactly
   `lambda_synth.cryo:283-317`) + the already-typed original body, with every `return E` rewritten to
   `return Poll::Ready(E)` (and `return;` / fall-through in a `-> ()` async fn → `return Poll::Ready(())`).
   (Await increments: a `switch(this.state)` resume dispatcher + suspend states — see below.)
3. `implement trait Future<T> for struct f$Future` (`ImplBlockNode`) holding `poll`, registered manually (the
   recon recipe): `trait_annotation = Generic(Named("Future"),[T])`; `add_assoc_binding("Output", T)`;
   `poll.set_origin_trait("Future")`; register via the two funnels
   (`SpecializationPasses::register_impl_block(impl, gr, ctx)` + `TypeResolutionPasses::register_decl_in_index(
   impl, ctx, /*is_source_decl=*/false, module_sym)`) PLUS an explicit `gr.register_trait_impl(Future_leaf, Q,
   impl)` (bare table under Q — required for `.poll()` dispatch; the funnel only writes the typed entry when
   target==canonical). Struct side mirrors `lambda_synth.cryo:239-423` (create_struct/set_fields/
   register_type_with_module/register_methods_with_module/StructType.add_method).
4. Rewrite `f` itself into the constructor: return type → `f$Future`; body → `return f$Future { state: 0u32,
   p0: p0, p1: p1 };`. Callers `block_on(f(..))` / `await f(..)` then drive the state machine.

**Await desugar (the state machine).** For the k-th `await e` (e: `Future<A>`):
```
this.fut_k = e;                              // stash the sub-future
loop {                                       // resume label = state k
    match (this.fut_k.poll(cx)) {
        Poll::Ready(v) => { <result> = v; break; }
        Poll::Pending  => { this.state = k; return Poll::Pending; }
    }
}
```
`poll` opens with `switch (this.state) { 0 => <start>; k => <resume at state k>; … }`. Any local live across
await k is promoted to a `this.` field (written before the `return Pending`, read on resume) — the live-set is
computed the same way `move_check` tracks liveness, and is ALSO where the "reference-live-across-await" ban
(§4) is enforced.

**Referencing stdlib types.** `poll`/`Poll::Ready`/`Context` need TypeRefs for `Poll<T>`, `Context`, and the
`Future` trait. v1: require the module to `import std::future::…`; look them up via the DI. Follow-up: auto-
import (parser sets a `used_async` flag like `used_fstring` → AutoImport injects the imports, mirroring
`pass_registry.cryo:1018-1030`).

**Increments** (each: build → validate under `block_on` → leave green → check `.ll` diff → repin only if IR
moved):
- **1b — no await → immediately-Ready.** `async fn answer() -> i32 { return 42; }` ⇒ `block_on(answer())==42`.
  Validates the whole synth+trait-impl-registration+constructor-rewrite path (the riskiest unknown). NO state
  machine yet (poll runs the body once). **← implement first.**
- **2 — one await, straight-line.** 2 states; stash the sub-future; bind its result; no cross-await locals.
- **3 — several straight-line awaits.** N states; promote cross-await locals to fields.
- **4 — awaits across branches/loops (HARD — plan + Jake sign-off first).** A resumable `switch` must re-enter
  the MIDDLE of a loop/branch, which a naive source rewrite can't express. Two options to choose with Jake:
  (a) full CFG-to-state-machine flattening (split the body CFG at await points into basic blocks, emit a flat
  `loop { switch(state){…} }` — how real MIR-based stackless lowerings work; most correct, most work); or
  (b) staged: support await only in straight-line + simple if/else first, hard-error await-in-loop, expand
  later. Recommend deciding before coding Inc 4.

**Repin.** The lowering is inert for async-free code (neither compiler nor stdlib uses `async fn`), so selfhost
IR should be unchanged — verify the `win-s2` vs `win-s3` `.ll` diff at each boundary; repin only if non-zero.

---

## 9. Progress Log (append-only; newest last — the cross-session memory)

- _2026-07-22_ — Doc created. Baseline HEAD `2248a22d` (Track 2 done). Async is greenfield beyond
  `await`-expr parsing. Nothing implemented yet. NEXT: Phase 0 design lock-down with Jake.

- _2026-07-22_ — **Phase 0 DONE — design locked (Jake signed off).** Baseline now HEAD `db1bbd4c`
  (doc-cleanup on `2248a22d`), pin verifies OK, tree clean. All seven §7 questions locked (see §7). Two
  one-way doors went to Jake via explicit choice: (a) `Future` trait shape → associated `Output` + `cx:
  Context*`; (b) await syntax → prefix. **Pinning was re-decided:** Jake first picked "build a real `Pin<T>`",
  but recon established that is **infeasible in current Cryo** — Rust's Pin needs lifetimes + a borrow checker
  + an enforcing `unsafe`, and Cryo deliberately has **none** (`docs/cryo.md:2314` "no borrow checker, no
  lifetimes"; `:2369`; `:1030-1041` "`unsafe` is a documentation marker and nothing more"). A `Pin<T>` here
  would enforce nothing. Re-locked to **forbid borrows across `await`**, enforced by `move_check.cryo` (the one
  ownership mechanism Cryo actually enforces) → sound by construction, no Pin type. Key lesson for future
  phases: **Cryo's enforcement surface = the move-checker only; references/raw pointers are unchecked
  (C++-like). Any async safety guarantee must route through moves, not through references/lifetimes/unsafe.**

  Substrate ground-truth from three recon sweeps (durable — grep the symbols):
  - **Language surface.** Traits: `type trait Name<G> : Super { ... }`; `;`=required, block=default; `This`=
    impl type. **Associated types supported** (`iter.cryo:29` `type Item;` + `This::Item`); positional-sugar
    binding only when the trait has no generics of its own else E0310 → use `type Output = …;` body. Receivers:
    `&this`(731) / `mut &this`(421, the mutable-self form) / `this`(87) / `mut this`(14); **`&mut this` does not
    exist**; `mut &Type` is the mutable-ref *param* form. Generic enum payloads: `type enum Poll<T>{Ready(T);
    Pending;}`, constructed/matched fully-qualified (`Poll::Ready(x)`, no bare `Ready`). Generic impls
    `implement<T> trait X for struct Foo<T> where …`; **NO blanket impls** (each concrete type needs its own).
    Fn-ptr type is `(Params)->Ret` (no `fn` kw); **callbacks must be non-capturing** → thread state via explicit
    `void*` ctx (the `CatchCtx`/`Payload` idiom = the Waker shape). `static match (T){ Type => {…} }` for
    compile-time type dispatch.
  - **Pipeline / lowering.** Real driver order is the `set_passes(...)` sequence in `instance.cryo
    compile_project_with_ctx` (NOT the static `pass_registry.cryo` list): DirectiveProcessing → **typecheck_bodies
    (FunctionBodyTypeCheck, mono-after-sema)** → Monomorphization+GenericExpressionResolution →
    route_specializations → [GenericValidation, FunctionBodyTypeCheck re-check, **MoveCheck**, DeadCodeAnalysis,
    **DropInsertion**, TypeLowering] → IRGeneration → Opt/ObjEmit/Link. MoveCheck+DropInsertion run **post-mono**
    on specialized AST. **`sema/lambda_synth.cryo:239-423` is the canonical decl-injection precedent** (synth
    struct+method → register in arena+DeclarationIndex → `root.add_statement`). `FunctionDeclNode`@
    `declaration.cryo:226-393` (add `is_async`, mirror `is_synth_default`); sema intercept `sema.cryo:261`;
    codegen fn-body `decl_visit_emitter.cryo:56/162`; the `await` hard-error to kill `ir_generator.cryo:1944`
    (sits with `YieldExprNode`/`TypeofExprNode` stubs). `KwAsync` lexes but **no parser path consumes `async fn`**.
  - **Runtime substrate.** `thread::spawn<C,T>(ctx, body:(C)->T)->JoinHandle<T>` where C:Send,T:Send (heap
    `Shared` control block + `Atomic<u8>` handshake — mirror for async JoinHandle/worker pool). Sync: `Atomic<T>`
    (LLVM atomics), `Mutex<T,A>`, `CondVar` (park/wake), `mpsc` channel (ready task queue) — `stdlib/sync/`. No
    lock poisoning / no cross-thread panic-catch. **epoll fully wired** (`libc.cryo:1721`, `syscall.cryo:883`)
    + `eventfd` + `timerfd`; **IOCP/kqueue absent** (Windows reactor = new bindings, defer). Net sockets = `i32`
    fd via `TcpStream::raw_fd()`/`from_fd()`; **no `set_nonblocking`** (add via `fcntl`+`O_NONBLOCK`, pattern at
    `process/command.cryo:796`). `catch_unwind<T>(f:()->T)->Result<T,PanicInfo>` takes a **non-capturing fn-ptr +
    ctx**, requires `--panic=unwind` (compile error under abort) → poll-boundary isolation needs a
    `run_poll(ctx:void*)` trampoline mirroring `run_catch_body`. Clock: `Instant`(CLOCK_MONOTONIC)/`Duration`/
    `sleep`/`timerfd` — `stdlib/time/`.

  NEXT: **Phase 1** — write `stdlib/async/` core types (`Poll`/`Future`/`Context`/`Waker`) + a trivial single-
  thread `block_on` + two hand-written futures (`Ready<T>`; pending-N-then-Ready), validate one end-to-end under
  `block_on`. Generic/lazy → default IR untouched until instantiated → **no repin** for Phase 1.

- _2026-07-22_ — **Phase 1 DONE + validated (no repin).** Module lives at **`stdlib/future/`** (NOT `async/` —
  `async` is a reserved keyword `KwAsync`; consumer path is `import std::future::…`). Registered in
  `stdlib/lib.cryo`. Structure (types in sub-modules so consumers import them **unqualified** — see the gap
  below):
  - `future/poll.cryo` — `type enum Poll<T> { Ready(T); Pending; }` + `is_ready`/`is_pending`.
  - `future/waker.cryo` — `Waker { wake_fn: (u8*)->void; data: u8* }` (`::noop`/`::new`/`wake`) + `Context { waker }`.
  - `future/traits.cryo` — `type trait Future { type Output; poll(mut &this, cx: Context*) -> Poll<This::Output>; }`.
  - `future/ready.cryo` — sample futures `Ready<T>` (Option-backed, single-use) and `PendingThenReady<T>`.
  - `future/_module.cryo` — `public module` decls + `function block_on<F, R>(fut: F) -> R where F: Future<R>`
    (single-thread driver: `loop { match f.poll(&cx) { Ready(v)=>return v; Pending=>{} } }`, no-op waker).
  **Validation:** scratchpad probe (`…/scratchpad/async_probe`, cryoconfig `stdlib_root` → repo stdlib) drives
  three futures through `block_on` — stdlib `Ready<i32>`→7, stdlib `PendingThenReady<i32>`(3 pends)→35, and a
  **consumer-defined** `UserCountdown`(5 pends)→100 — asserting via exit code (bitmask). Built with `bin/cryo.exe`
  (`CRYO_CC=gcc`) → `EXIT 0`. `make stdlib` (147 modules) green; `verify-pin.py` OK; tree = `M lib.cryo` +
  `?? stdlib/future/`.
  **Design facts learned (durable):** (1) Cryo bindings are `const`/`mut`, **there is no `let`**. (2) **enums cannot
  have inline methods** in the `type enum` body — put them in a separate `implement enum X { … }` block (structs
  DO allow inline methods). (3) The `Future` trait's associated `Output` is bound in impls via positional sugar
  `implement<T> trait Future<T> for struct Ready<T>` (mirrors `Iterator<A>`); `block_on<F,R> … where F: Future<R>`
  uses the same positional bind rather than an `F::Output` projection (which has no free-fn precedent).
  **⚠ COMPILER BUG FOUND — NOW FIXED (uncommitted; selfhost-clean, no repin).** A **module-qualified generic
  static ctor** — `future::Ready<i32>::new(…)` — failed codegen with **E0636 "no such associated function"**,
  while the **unqualified** form `Ready<i32>::new(…)` (like `Atomic<u8>::new`) worked. Root cause was TWO gaps
  in resolving a `module::Type<T>::method` scope: (1) **parser** — the chained-`::` handler
  (`expr_parser.cryo` ~488-502) folded the intermediate `future::Ready` node into the scope path but passed
  `no_gen_args`, **dropping the `<i32>`** that had been parsed as `future::Ready`'s *member* generic_args (the
  unqualified path stores them as scope args directly); so owner-arg inference saw no turbofish and, after the
  parser fix alone, tripped a **spurious E0307**. (2) **sema** — `resolve_scope_owner_template` /
  `scope_is_generic_template` (`call_resolver.cryo`) resolved the scope only via `resolve_scoped_or` (BARE-leaf
  only), so the module-qualified `future::Ready` never mapped to its registered `std::future::ready::Ready`
  template → no owner-arg stash → mono never emitted the spec. **Fix:** parser carries the intermediate node's
  generic_args forward as `scope_generic_args`; both sema helpers gain a `resolve_cross_module_name` fallback
  (leaf-canonicalize, exactly as `resolve_generic_scope_name` already did). Sibling of the already-fixed
  non-generic case [[cross-module-struct-static-resolution-fix]]. **Validation:** probe now drives BOTH qualified
  (`future::Ready<i32>::new(70)`, `future::PendingThenReady<i32>::new(3,35)`) and unqualified + user futures →
  EXIT 0. `make selfhost-check` → exit 0, TWO FIXED POINT OK (Linux md5 + Windows 234-module); **win-s2 vs
  win-s3 = 0 differing `.ll`** → default-path IR unchanged → **no repin** (the fix is inert for all existing
  code — the compiler's own source never uses the qualified-generic-static-ctor construct). General fix (helps
  every user), not async-specific.

  NEXT: **Phase 2** — `async fn` parse (`is_async` on `FunctionDeclNode`) + state-machine lowering + `await`
  desugar (mirror `lambda_synth.cryo`). Moves default IR → **will repin**. Build up by control-flow complexity;
  get a written implementation plan before the across-branches/loops lowering (the hard part).

- _2026-07-22_ — **Phase 2 STARTED — parse done+validated; full lowering design written (§10).** Baseline
  HEAD `ed679a50` (Phase 1 + compiler fix committed + repinned by Jake), pin OK.
  - **Parse (Inc 1a) DONE + validated.** Added `is_async` to `FunctionDeclNode` (field + `set_async` + init +
    cloned in BOTH cloner sites `cloner.cryo:682`/`:1097`); `KwAsync` added to `is_declaration_start()`
    (`parser_base.cryo`); `async function` branch in `parse_declaration` (`parser.cryo` after the `KwFunction`
    branch) sets `is_async`. Pre-lowering an `async function` compiles as a plain fn. Rebuilt (`make cryo` OK);
    probe `async function answer(x:i32)->i32 { return x+40; }` + `main{ return answer(2); }` → EXIT 42. Tree
    green (compiler builds; no selfhost run yet — parse-only change, but re-run before repin).
  - **Trait-impl-synthesis recon COMPLETE** (the make-or-break unknown). A synthesized-during-sema
    `implement trait Future<T> for struct F$Future` must SELF-REGISTER (no post-sema pass registers root-injected
    impls). Recipe: struct side mirrors `lambda_synth.cryo:239-423`; impl side = `new ImplBlockNode(Q)` +
    `set_trait_annotation(Generic(Named("Future"),[T]))` + `add_assoc_binding("Output",T)` +
    `poll.set_origin_trait("Future")` + register via `SpecializationPasses::register_impl_block` +
    `TypeResolutionPasses::register_decl_in_index(impl,ctx,false,mod)` (is_source_decl=false skips coherence
    E0308) PLUS explicit `generic_registry.register_trait_impl(Future_leaf, Q, impl)` (bare table under Q — the
    funnel skips it when target==canonical, but `.poll()` dispatch keys on it). Bound-check reads it via
    `OwnershipQuery::resolve_trait_impl_for_type` (typed path on `(Future_leaf, leaf, Q.id)`); `Output` projects
    off `trait_annotation`'s generic arg (`resolver.cryo:454-461`).
  - **Full lowering design → §10** (state-machine shape, await desugar, increments, the Inc-4 CFG decision).
  NEXT: implement **Inc 1b** (no-await → immediately-Ready future via the recipe; `block_on(answer())==42`) —
  validates the whole synth+registration path. Then Inc 2/3 (awaits, straight-line). **Inc 4 (awaits across
  branches/loops) needs Jake to pick the CFG approach (§10) before coding.**

- _2026-07-22_ — **Inc 1b DONE + validated + reviewed. No repin (0 `.ll` diff).** Baseline HEAD `a6833342`
  (parse committed+repinned). Files (UNCOMMITTED): NEW `sema/async_lower.cryo` (~340 lines, `AsyncLower`);
  `sema/sema.cryo` (field+init+wire+hook, gated `is_async && !is_generic`, after body typed); `sema/_module.cryo`
  (`public module AsyncLower`); `decl_index.cryo` (+`reset_function_overloads`). Mirrors `lambda_synth` for the
  struct; adds the trait-impl half. **Validated:** probe drives no-arg `answer()→42`, one-arg `inc(41)→42`, and a
  **branchy** `classify(x)` (if-returns) through `future::block_on` (no turbofish — F/R inferred) → EXIT 0.
  `make selfhost-check` exit 0 + TWO FIXED POINT OK (235 modules); `win-s2` vs `win-s3` = 0 differing `.ll`;
  pin OK.
  **Implementation notes for later increments (durable):**
  - **Stale-overload trap:** rewriting the async fn's return type mid-sema leaves the OLD Output-return overload
    registered; the free-call path reads `lookup_func_type_overloads` (NOT `func_returns`), so it picked the stale
    `i32` signature → `answer(): i32` → `i32: Future` E0306. Fix: `decl_index.reset_function_overloads(name)` for
    BOTH bare + qualified names, then `register_decl_in_index(node, ctx, false, mod)` with the struct return.
  - **Trait-impl registration is DIRECT, not the funnels:** `register_impl_block` (wants qualified target) and
    `register_decl_in_index` (re-qualifies, wants bare) conflict on `target_type`. So register the two keys
    directly: `register_trait_impl(Future_leaf, Q, impl)` (`.poll()` dispatch, bare table under Q) +
    `register_trait_impl_typed(Future_leaf, registry_name_of(Q), named_qualified_id(Q), impl)` (`F: Future<R>`
    bound). Both consumers resolve.
  - **`Poll::Ready(E)` hand-typed** (`ScopeResolutionNode(Poll,Ready)` + `CallExprNode`, both
    `resolved_type = Poll<Output>`); the moved body is already typed, so no re-resolution pass is needed. Codegen
    accepts it directly.
  - **`block_on(fut)` needs no turbofish** — F inferred from the (anonymous) future arg, R through the
    `F: Future<R>` bound. The unnameable generated future type is drivable without the caller naming it.
  - `rewrite_returns` recurses through block/if/while/do-while/for/loop/match containers (all return sites), NOT
    into nested lambda bodies (a lambda is an expression the statement walk never enters).
  NEXT: **Inc 2** — one `await`, straight-line (2 states; stash the awaited sub-future in a field; bind its
  result; suspend on `Pending`). Rewrite `AwaitExprNode` (kill the `ir_generator.cryo:1944` hard-error) +
  emit the `switch(this.state)` resume dispatcher.

- _2026-07-23_ — **Inc 2 DONE + validated (both OSes). No repin (0 `.ll` diff).** Baseline HEAD `61ad4320`
  (Inc 1b committed+repinned), pin verifies OK, tree = 4 modified source files (uncommitted). A single
  straight-line `await` now lowers to a two-state resumable `poll`. Files:
  - `stdlib/future/poll.cryo` — added `Poll<T>::into_ready(&this) -> T` (`![sink]`; aborts on `Pending`); the
    lowering calls it only after an `is_pending` check already returned on the Pending path.
  - `sema/sema.cryo` — `resolve_expr` now types `AwaitExpression` via new `resolve_await`: resolves the
    operand `F`, projects `F::Output` with `type_resolver.resolve_concrete_member(F, "Output", &rc)` (the
    same assoc-projection `check_assoc_decl_bounds` uses at `sema.cryo:469`), leaving `F` cached on the
    operand and `A=Output` on the await node so `AsyncLower` reads both without re-projecting. Non-`Future`
    operand → clean E0306.
  - `sema/async_lower.cryo` — `lower()` now computes an await plan (count via `block_await_count`; locate the
    single supported carrier via `stmt_toplevel_await`); 0 awaits → the Inc-1b body; exactly 1 supported
    top-level await → the state machine; anything else → loud E0600. Added `fut_0: Option<F>` field
    (`Option::None` in the ctor — the sub-future is created lazily on first poll), and
    `build_poll_body_single_await` emitting: `if (this.state==0u32){ <pre>; this.fut_0=Option::Some(<e>);
    this.state=1u32; }` then `mut __sub_0 = this.fut_0.take().unwrap(); mut __poll_0 = __sub_0.poll(cx); if
    (__poll_0.is_pending()){ this.fut_0=Option::Some(__sub_0); return Poll::Pending; } const __await_0 =
    __poll_0.into_ready();` then the carrier (its `await e` rewritten to read `__await_0`) + post statements
    (returns wrapped `Poll::Ready`). Carriers: `const/mut x = await e;`, `return await e;`, `await e;`.
  - `codegen/visit/ir_generator.cryo` — `visit(AwaitExprNode*)` hard-error message reworded: a surviving
    `AwaitExprNode` at codegen now means the lowering did NOT rewrite it (await outside `async fn`, or async
    control flow this increment doesn't handle) → it is a **defensive backstop**, deliberately kept (NOT
    deleted), so the imperfect `block_await_count` can only degrade the diagnostic, never miscompile.
  - **Validation:** scratchpad `async_inc2/` drives three async fns through `future::block_on` (no turbofish)
    → EXIT 0: `delayed(3,41)→42` (decl-init await, 3 Pending polls then `+1`), `passthru(2,7)→7` (`return
    await`), `withpre(1,20)→40` (pre-statement builds the future into a local, then awaits it). Negative
    probes: two awaits and a nested `(await e)+5` both hit the E0600 gate loudly. `CRYO_CC=gcc make
    selfhost-check` → exit 0, BOTH fixed points (Linux s3==s4; Windows FIXED POINT OK, 235 modules);
    `win-s2` (pinned-built) vs `win-s3` (new-built) = **0 differing `.ll`** → lowering inert for async-free
    code → **no repin**.
  **Storage-model decision (durable):** the awaited sub-future is `fut_0: Option<F>`, NOT a plain `F`.
  Cryo futures are lazy (body runs only on first poll), so `e` must be built on the FIRST poll (state 0),
  not at construction — a plain field would need a constructor value that doesn't exist yet. `Option<F>`
  = "not created yet" (`None`) → created (`Some(e)`) in state 0 → take/poll/put-back each poll. On `Ready`
  the sub-future is left `None` (moved out), so a stray re-poll `unwrap()`s `None` → a clean
  "polled-after-completion" abort, and dropping the completed machine drops `None` (no double-free).
  **Implementation notes (durable):**
  - **The whole poll body is method-calls + `if` + assignment + enum-ctor exprs — NO hand-built `match`/
    pattern AST.** Chose `Option::take().unwrap()` + `Poll::is_pending()`/`into_ready()` over a synthesized
    `match (fut.poll())` precisely to avoid authoring `MatchStmtNode`/`PatternElement`/mut-bindings by hand.
    The injected impl is re-type-checked POST-mono (`sema.cryo:3051` sets `post_mono_verify` when
    `MonomorphizationComplete`), and `resolve_expr` re-resolves from scratch in that mode (ignores cached
    types, `sema.cryo:1147`), so every synthesized node must resolve BY NAME — method calls/idents/members
    do; the pre-set `resolved_type`s are only pre-mono hints.
  - **No-cross-await-locals is enforced by SCOPING, not a check:** `<pre>` lives inside the `if
    (state==0)` block, so a pre-local used in `<post>` (outside the if) is a plain "not found" (E0201).
    That is the Inc-2 boundary; Inc 3 promotes cross-await locals to `this.` fields.
  - **`Poll::Pending` / `Option::None` construct fine as a bare typed `ScopeResolutionNode`** (no CallExpr);
    the return/field-init expected-type context supplies the generic arg (`PendingThenReady::poll` already
    returns a bare `Poll::Pending` in stdlib, so the shape is proven).
  ✅ **RETRACTED false alarm (2026-07-23): the qualified generic static ctor is NOT broken.** An earlier note
  here claimed `future::PendingThenReady<i32>::new(...)` was broken (E0233). Root cause of the E0233 was an
  **under-imported probe**, not a compiler bug: the probe imported only `import std::future;` (the parent),
  which does NOT expose types defined in re-exported submodules. Importing the defining submodule
  (`import future::ready;` or `import std::future::ready;`) makes BOTH qualified forms work on the **pinned
  `bin/cryo`** — verified: `future::Ready<i32>::new(70)`→70 and `future::PendingThenReady<i32>::new(3,35)`→35.
  So the Phase-1 "qualified-generic-static-ctor" fix DID land and works. This behavior is consistent across
  the whole stdlib (`import std::collections;` alone likewise can't reach `collections::HashMap::new`; adding
  `import collections::hashmap;` fixes it) — i.e. `public module <sub>;` in a parent `_module.cryo` does not
  flatten the submodule's types into the parent for qualified `parent::Type::method` resolution. Whether it
  SHOULD (an ergonomics/re-export design question) is open and separate; it is NOT a regression and NOT
  async-specific. The trap for future sessions: a `future::Type` reference needs the submodule imported.
  NEXT: **Inc 3** — several straight-line awaits (N states; real cross-await local promotion to `this.`
  fields; per-await `fut_k`). Generalize `build_poll_body_single_await` to a sequence: fold the body into
  segments split at each carrier, emit one take/poll/suspend block per await, promote any local live across
  a later await to a struct field. Then **Inc 4 (awaits across branches/loops) — needs Jake to pick the CFG
  approach (§10) BEFORE coding.**

- _2026-07-23_ — **Qualified `parent::Type` re-export resolution — FIXED (general module-system fix, not
  async).** Refines the retraction above with the precise behavior + a real fix Jake asked for. Ground truth:
  `import std::future;` (parent only) ALREADY exposes **bare** re-exported submodule types (`Ready`,
  `PendingThenReady`, `Poll`, …) — the `public module future::ready;` re-exports flatten bare names, and
  `Ready<i32>::new(70)`→70 works. What did NOT work was the **qualified** `future::Ready<i32>::new(...)` and
  the annotation `future::Ready<i32>` (E0233 / E0203), because the qualified-name resolver
  (`Resolver::resolve_type_qualified_name`, `resolver.cryo`) canonicalizes a `M::Leaf` name by resolving the
  LEAF through the current scope chain, which misses a leaf that only entered as a re-export.
  **Fix (committed-pending, +66 lines, resolver.cryo only):** when that leaf-scope resolution fails, a new
  fallback `resolve_qualified_type_via_exports` scans the cross-module export table for a PUBLIC type whose
  leaf is `Leaf` and whose declaring module is consistent with the `M` prefix (segment-aligned check
  `module_ns_matches_prefix`, so `future::Ready`→`std::future::ready::Ready` but `collections::Ready` /
  `bogus::Ready` correctly still fail E0233). One funnel fixes BOTH the annotation path
  (`TypeResolver::resolve_named` → `resolve_type_qualified_name`) and the ctor path
  (`TypeUtils::resolve_cross_module_name` → same). **Validated:** with ONLY the parent import,
  `future::Ready<i32>::new(70)`→70, `future::PendingThenReady<i32>::new(3,35)`→35,
  `collections::HashMap<i32,i32>::new()`→42, the qualified annotation compiles, unqualified unchanged, wrong
  prefixes rejected. **Gate:** `make selfhost-check` exit 0, BOTH fixed points; `win-s2` vs `win-s3` = **0
  differing `.ll`** and Linux `s3` vs `s4` = 0 → the change is inert for all existing code (the compiler's own
  qualified names already resolved; the fallback never fires for them) → **NO REPIN**. This resolves the
  "open ergonomics question" from the retraction: a `future::Type` reference now works with just
  `import std::future;`.

- _2026-07-23_ — **Inc 3 DONE + validated (both OSes). No repin (0 `.ll` diff). UNCOMMITTED.** Baseline HEAD
  `b1414145`, pin verifies OK, one modified file: `sema/async_lower.cryo` (+538/−130). `AsyncLower` now
  lowers **N straight-line `await`s with cross-await local promotion** (generalizes Inc 2's single-await
  path). Shape:
  - **`build_plan(node, &plan)`** computes an `AsyncPlan` (new file-scope struct): the carrier statement
    indices, per-await `fut_i`/`F_i`/`Option<F_i>`/`F_i::Output`, and the promoted-local set. Gate:
    `total_awaits == carrier_count && count >= 1` — an `await` nested in an expression (carriers=1,
    awaits>1) or inside a branch/loop (counted, never a carrier) fails `E0600` (Inc 4). `plan.m == 0` ⇒ the
    Inc-1b no-await path.
  - **`build_poll_body_multi_await`** emits `m+1` state-guarded blocks `if (this.state == i) { … }`. B0 runs
    the pre-await statements + builds `fut_0`; Bi (i≥1) resumes await i-1 (`take/poll/is_pending→suspend/
    into_ready`), runs the carrier at `carriers[i-1]` (await rewritten to `__await_{i-1}`) + the statements
    up to the next await, then builds `fut_i` + `this.state = i+1`. **Each block sets state forward at its
    end, so a poll finding consecutive sub-futures immediately ready falls straight through them; a resume
    poll's state guard skips every completed block.** A trailing `return Poll::Pending` after the blocks is
    an unreachable fallback required only because the state guards are opaque to control-flow analysis
    (without it → `E0403` "reaches end without returning").
  - **Cross-await promotion (the real new work).** A local declared in block Bi and READ in a later block
    (`block_of(stmt) = #carriers ≤ index`) is promoted to a synthesized field `__cross_<blk>_<name>`,
    **stored** (`this.__cross… = name`) at the end of its declaring block and **reloaded**
    (`const name = this.__cross…`) at the top of each later block that reads it (so reads resolve to the
    shadow, not the out-of-scope original; the synthetic field name also dodges param-name collisions). A
    local read only in the **operand** of the next await stays a plain local (the operand is built lazily at
    the END of the previous block, attributed there — **not** to the carrier's own block). Read-detection is
    a by-name expr/stmt walker: a miss can only UNDER-promote → a loud "not found", never a silent
    miscompile.
  - **Storage model:** promoted locals are **plain `T` fields, scalar-Copy only** (Int/Bool/Float/Pointer),
    zero-initialized in the ctor (the field is always written before read, so the value is never observed;
    Copy ⇒ the `this.x = x` store never drops garbage). Sub-futures stay `fut_i: Option<F_i>` (lazy, `None`
    in ctor). **Deferred/banned with loud diagnostics:** `mut` local across await → `E0600`; non-scalar
    aggregate across await → `E0600`; **reference across await → `E0455`** (the §4 self-reference ban,
    enforced at the promotion site — routing through `move_check` for the un-promoted reference-param case is
    a later refinement).
  - **Validation (inline direct-poll driver — see the `block_on` bug note in §1):** scratchpad `async_inc3/`
    drives four async fns to EXIT 0 — `add_two` (two awaits, `a` promoted, await 0 pends 2×) → 42;
    `compute(3)` (pre-await `scaled` + first-await `x` both promoted, param threaded, await 1 pends 3×) → 42;
    `chain` (three awaits, mid-chain resume, `a`+`b` promoted) → 42; `dep` (`a` read only in the next await's
    operand → stays local) → 42. Negative probes all fire the intended diagnostic: `mut`-cross-await→E0600,
    ref-cross-await→E0455, await-in-branch→E0600, nested-await-in-expr→E0600.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4`; Windows
    `FIXED POINT OK`, 235 modules) after an environmental exit-15 SIGTERM retry on the first Windows run.
    `win-s2` (pinned-built) vs `win-s3` (new-built) = **0 differing `.ll`** → **NO REPIN** (the Linux
    `build/` vs `s3` comparison shows only `__FILE__`-path `FILE.str` string diffs from the absolute-vs-
    relative stage invocation, which normalize to 0 — a stage artifact, not IR movement).
  **Durable notes for Inc 4:** (1) the `if (state==i)` fall-through structure is straight-line-only; a
  resumable `switch` re-entering the MIDDLE of a loop/branch is what Inc 4 must solve (per §10, a one-way
  door — get Jake's CFG-approach sign-off first). (2) The reference-across-await ban lives in `build_plan`'s
  promotion check; a reference **param** used after an await (not a promoted local) is NOT yet caught — that
  needs the move-checker route (§4). (3) `mut`-across-await needs load-at-entry/store-at-exit writeback in
  every block of the live range (deferred). (4) Promoted non-scalar/Copy-struct locals need either
  `Option<T>` fields with non-consuming reads or init-flag-guarded plain fields (deferred). (5) **`block_on`
  is broken without a turbofish (§1) — validate with inline `poll` loops, not `block_on`.**

  NEXT: **Inc 4 — awaits across branches/loops. ONE-WAY DOOR: bring §10's two options (full CFG-to-state-
  machine flattening vs. staged hard-error-await-in-loop-first) to Jake and get sign-off BEFORE coding.**
  Separately, the `block_on` where-bound-inference bug (§1) blocks the ergonomic async driver and is worth
  fixing (generic-inference layer, not async) — Jake to triage.

- _2026-07-23_ — **`block_on` where-bound-only inference — FIXED (general generic-inference fix, not async).
  No repin. UNCOMMITTED.** Baseline HEAD `5f2776af` (Inc 3, committed by Jake). Two files: NEW `PASS D` in
  `sema/call_resolver.cryo` (+82; `project_where_bound_params` + `param_name_index` + `where_arg_param_index`,
  wired into both `check_generic_free_call` and `infer_free_call_bindings`) and `InferCtx::bind_slot` in
  `types/inference.cryo` (+10). **Root cause:** for `f<F, R>(fut: F) -> R where F: Future<R>`, `R` is a
  functional dependency of `F` (positional sugar `Future<R>` = `Output = R`) that appears in NO value
  parameter; PASS A bound `F` from the arg but nothing derived `R`, and the `all_bound` gate bailed to
  `invalid` WITHOUT stashing type args → mono skipped the specialization → codegen `E0636`. Expected-type
  context (`const r: i32 = block_on(fut);`, return position) already recovered `R` via PASS B, so the bug only
  bit in context-free position (`block_on(fut) != 42`). **Fix:** after PASS A/B/C, project each still-unbound
  where-bound arg param off its now-bound subject — resolve the trait decl (`get_trait_decl`), map the
  positional arg slot `ai >= trait.generic_params.length` to `trait.assoc_types[ai - ngp]`, and
  `resolve_concrete_member(subject, assoc_name)` (the same projection `resolve_await` uses). **Landmine paid:**
  the subject guard must be `!contains_generic_param(subject)`, NOT `free_infer_type_concrete` — the latter
  rejects a *pre-mono* `InstantiatedType` (`Ready<i32>`) whose monomorphized form doesn't exist yet
  (`has_resolved_type()==false`), which is exactly the state the arg is in during sema; `resolve_concrete_member`
  handles the pre-mono instantiation fine. **Validation:** `block_on(Ready<i32>::new(42))` (the bug),
  `future::block_on(…)`, `block_on(PendingThenReady<…>)`, **`block_on(add_two())` on an unnameable async
  future** (the real payoff — retires the inline-poll workaround), `String`-output, and `from_iter(it)` (the
  other in-tree instance: `T = I::Item`) all → EXIT 0; turbofish + expected-type unchanged; non-`Future` arg
  still rejected (E0306/E0636, no miscompile). **Gate:** `make selfhost-check` → exit 0, **BOTH fixed points**
  (Linux `s3`==`s4`; Windows `FIXED POINT OK`, 235 modules — after freeing an OOM-pressuring `cryolsp` that
  environmentally SIGTERM'd the wine build twice). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0
  differing `.ll`**; Linux normalized pinned-vs-new = 0 residual (only `FILE.str` `__FILE__`-path artifacts)
  → the fix is inert for all existing code (every call that compiles today already binds all params, so PASS D
  fills nothing) → **NO REPIN**. **Follow-up (investigated, deferred):** tried to mirror this onto the
  generic-METHOD path and reverted — `infer_generic_method_bindings` is **not reached** for static
  generic-method calls (`Owner::drive(fut)` resolves via a different scope-resolution path), and generic
  methods of this shape fail even WITH a turbofish (E0633) — even trivial `Bo::id<i32>(42)` fails E0200. The
  generic-method-with-where-bound path has multiple independent pre-existing gaps beyond inference; it needs
  its own effort (see §1). Reverted cleanly to the validated free-fn-only fix (call_resolver +82, inference +10).

- _2026-07-23_ — **Inc 4 design SIGNED OFF by Jake (one-way door closed). Building Inc 4a.** Baseline HEAD
  `ce7456c6`, pin OK, tree clean. Took §10's two options to Jake; he chose the **dispatch-loop state machine
  (option a)** — with one refinement: the dispatcher is a **`match (this.state)`, NOT a C-style `switch`**
  (there is no `switch` in Cryo; `switch` in §10 was pseudocode). Also a standing style note from Jake:
  **emit bare integer literals (`1`), never `1u32`** — the compiler infers the integer type from context
  (the existing `u32_lit` helper already emits bare text `"1"` with a u32 `resolved_type` hint, so generated
  code is already compliant; keep it that way). Feedback memory [[prefer-bare-int-literals]].
  **The agreed generated shape** (replaces Inc-3's forward-only `if (this.state==i)` fall-through chain):
  ```
  poll(mut &this, cx: Context*) -> Poll<Out> {
      <arg-shadow prelude>                         // const p = this.p; …  (outside the loop)
      loop {
          match (this.state) {
              0 => { <block 0>;  this.state = k; }   // transition: set state, FALL THROUGH the match →
              …                                      //   the enclosing loop re-iterates → re-matches new state
              j => { … return Poll::Pending; }       // suspend: real return exits poll
              n => { return Poll::Ready(v); }         // completion
              _ => { return Poll::Pending; }          // unreachable state guard (exhaustiveness)
          }
      }
  }
  ```
  **Key realizations (durable, drove the design):**
  - **No `continue`/`break` synthesis needed for the dispatcher.** A `match` arm is a statement whose body,
    if it doesn't return, falls through to the end of the `loop` body → the `loop` re-iterates → re-matches
    the (just-updated) `this.state`. So a *transition* is just `this.state = k;` with no terminator; only
    *suspend* (`return Poll::Pending`) and *completion* (`return Poll::Ready`) are real returns. This makes
    every edge — forward, branch, and (Inc 4b) back-edge — the uniform `this.state = k;`.
  - **`loop {}` with no `break` DIVERGES per Cryo's checker** (proof: stdlib `block_on` is
    `fn -> R { loop { … return v … } }` with no trailing return). So the dispatch loop needs NO trailing
    `return Poll::Pending` fallback for E0403 (the Inc-3 fall-through chain needed one because it wasn't a
    loop). The `_ =>` arm supplies match exhaustiveness and a safe no-progress result for a corrupted state.
  - **`match` on a raw integer with literal + `_` arms is first-class and used in stdlib** (errno match
    `stdlib/thread/_module.cryo:413`: `1 => …  11 => …  _ => …`). Synthesis recipe mirrors the parser:
    `MatchStmtNode(subject=this.state, span)` + `add_arm`; each arm `MatchArmNode(span)` +
    `add_pattern(PatternNode(PatternKind::Literal))` with `set_value(intern("0"))` +
    `set_literal_kind(LiteralKind::Integer)` (bare, no suffix), or `PatternNode(PatternKind::Wildcard)` for
    `_`; `set_body(block)`. `LoopStmtNode(body, span)`. This is the ONE new-AST risk (Inc 2/3 deliberately
    avoided synthesizing `match` — used `if` + `Option::take().unwrap()` — because the injected `poll` is
    re-type-checked POST-mono and every node must resolve by name; the errno precedent proves the shape
    type-checks from source, so it should re-resolve).
  **Staged rollout (each = build → validate under `block_on` → selfhost gate → `.ll` diff → no repin):**
  - **Inc 4a — acyclic (if/else across a suspend) + the whole `loop { match }` machine.** Internally
    two-stepped to isolate risk: (4a-1) *replace* the Inc-3 fall-through builder with the `loop { match }`
    dispatcher preserving EXACT straight-line semantics — re-validate the Inc 2/3 probes (proves the match/loop
    synthesis on known-good cases); (4a-2) generalize `build_plan` + the builder to allocate states for
    if/else branches + a join, with generalized cross-await promotion (a local live from one state into a
    different state → field; acyclic + single-assignment const ⇒ decl-state dominates all read-states ⇒ the
    Inc-3 store-after-decl / load-at-reader model still holds). Loops still hard-error E0600.
  - **Inc 4b — loops** (`while`/`for`/`loop`/`do-while`): back-edges + `break`/`continue` → state transitions
    + **mut loop-carried promotion** (deferred by Inc 3). Plan: a promoted `mut` local → rewrite every
    occurrence (read AND write) to its `this.field` in place, so the field is the single source of truth (no
    load/store-placement dance). Still scalar-Copy only; aggregate/non-Copy stays E0600.
  - **Inc 4c (deferred) — `await` inside a `match` arm** (pattern bindings live across a suspend). Hard-error
    past 4b.
  **Risks to validate during 4a** (flagged, not hidden): (1) synthesized `MatchStmtNode`+literal patterns
  re-resolving cleanly post-mono; (2) `_` satisfying the exhaustiveness/`no_match` checker (leave
  `unwind_cleanup=[]`; abort path anyway); (3) synthesized `loop`+`match` control flow not confusing post-mono
  MoveCheck/DropInsertion liveness (Inc 3 already runs `if`-blocks through them; loop+match is more CF);
  (4) `-> ()` fall-through end of the last state needs an explicit `return Poll::Ready(())` terminator (the
  loop won't fall off; a non-returning last arm would spin) — value-returning fns already return on all paths
  (pre-lowering E0403), so only the unit case needs the synthesized tail.
  NEXT: implement **Inc 4a-1** (swap fall-through → `loop { match }`, re-validate Inc 2/3 straight-line probes),
  then **Inc 4a-2** (if/else). Do NOT commit; no repin unless the `win-s2` vs `win-s3` `.ll` diff moves.

- _2026-07-23_ — **Inc 4a DONE + validated (both OSes). No repin (0 `.ll` diff). UNCOMMITTED.** Baseline HEAD
  `ce7456c6`, pin verifies OK. One modified source file: `sema/async_lower.cryo` (+404/−353). The whole poll
  lowering was **re-architected** from Inc-3's forward-only `if (this.state==i)` fall-through chain onto the
  agreed **`loop { match (this.state) { i => <block i>, _ => Poll::Pending } }`** dispatcher, and a **recursive
  state-machine builder** now handles `await`s across `if`/`else`. Done in two internal steps (each a green
  boundary):
  - **4a-1 (swap dispatcher, same surface).** Replaced the fall-through wrapping with `loop { match }`
    (`match_arm_int`/`match_arm_wild` synthesize bare integer-literal + `_` patterns, mirroring
    `expr_parser.cryo:1939`; `LoopStmtNode` wraps it). A transition is now a bare `this.state = k;` that falls
    off its arm → the `loop` re-dispatches (no `continue` synthesis needed — a non-returning `match`-statement
    arm falls through, exactly like stdlib `block_on`'s `Pending => {}`). Removed the Inc-3 trailing
    `return Poll::Pending` (the `loop` diverges, so E0403 is satisfied — `block_on` precedent). Re-validated
    the Inc 2/3 straight-line probes → EXIT 0.
  - **4a-2 (branches).** Replaced the flat `build_plan` + `build_poll_body_multi_await` + `emit_loads`/
    `emit_stores` + `AsyncPlan` with: a **`PollSm` accumulator** (config + `blocks` + `ok` flag + discovered
    `fut_*`/`prom_*` inventory) and a recursive builder — `build_poll_body_sm` → `lower_block_sm` →
    `lower_stmt_sm` dispatching to `lower_carrier_sm` (top-level await → resume state) / `lower_if_sm`
    (`if`/`else` → per-branch entry states + a join; else-if via recursion; no-else → false path goes straight
    to join). `sm_alloc` mints states on demand; `sm_goto` emits a forward `this.state = k;`.
    `stmt_diverges`/`block_diverges` suppress the join transition after a branch that returns (else it would be
    dead code after a `return`). `lower` was reordered: **create the struct `TypeRef` → build the body
    (discovering the inventory) → set the fields** (the field types are known only after the build; member
    accesses resolve by name post-mono, so fields need only exist by then).
  - **Promotion re-done as build-then-scan** (`promote_cross_state`): after the blocks are built, a local read
    in a state other than its declaring one is promoted — store appended to the declaring block, a
    `const <name> = this.<field>;` load **prepended** (`prepend_load` rebuilds `blk.statements`) to each other
    reading block. This subsumes Inc-3's carrier-operand attribution *for free* (an await operand is physically
    emitted into the previous state's block, so a local read only there is same-state → not promoted) and
    naturally covers a pre-`if` local read inside a branch (over-promotes across a branch dispatch even without
    a suspend on that path — harmless, the store dominates). Same deferrals as Inc 3: `mut`-cross-state →
    E0600, aggregate → E0600, reference → E0455 (guard **moved verbatim** into `promote_cross_state` — same
    code path; the 4 E0600 negatives were re-probed, E0455 was not separately re-probed this session).
  - **Durable realizations:** (1) `match`-statement arms **don't fall through C-style**; a non-returning arm
    runs to its end then control leaves the `match` → the enclosing `loop` re-iterates → this IS the transition
    mechanism (no `goto`/`continue`). (2) `loop {}` with no `break` **diverges** per Cryo's checker (stdlib
    `block_on` proof), so no trailing return is needed; the `_ =>` arm is only for exhaustiveness + a corrupted
    state. (3) An `async fn` with awaits that can **fall off its end** (implicit unit return) is rejected here
    (E0600) — it would spin the loop / re-poll a taken sub-future; value fns always return (pre-lowering
    E0403), so this only bites the deferred unit case. (4) Pushing to a `PollSm*` array field
    (`sm.blocks.push`) mutates in place across recursive calls (same pattern as `BlockStmtNode.add_statement`).
  - **Validation (via `block_on`, no turbofish):** straight-line probe (`add_two`/`compute`/`chain`/`dep` +
    no-await `answer`) → EXIT 0 (no Inc 2/3 regression). Branch probe → EXIT 0: `pick` (await in one branch),
    `both` (await in both, each returns), `prelocal` (pre-`if` local promoted across the branch dispatch AND a
    suspend), `grade` (else-if chain), `guard` (no-else `if` whose then awaits+returns, with further awaits
    after the `if` carried by the join). Negatives all fire: await-in-loop / await-in-`if`-condition /
    `mut`-cross-state / nested-await-in-expression → E0600.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4`; Windows
    `s3`==`s4` byte-identical IR). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0 differing `.ll`** →
    the lowering is inert for async-free code → **NO REPIN**. Pin verifies OK; tree = `M async_lower.cryo` +
    `M ASYNC_IMPL.md`.
  NEXT: **Inc 4b — loops** (`while`/`for`/`loop`/`do-while` with an await in the body). The `loop { match }`
  dispatcher already supports back-edges (a back-edge is just `this.state = <header>;` — the same forward-edge
  primitive), so 4b = add `lower_while_sm`/`lower_for_sm`/`lower_loop_sm` (header/body/after states + the
  back-edge) + rewrite user `break`/`continue` to state transitions + **`mut` loop-carried promotion** (the one
  genuinely new piece Inc 3/4a deferred: a promoted `mut` local → rewrite every read AND write to `this.field`
  in place, so the field is the single source of truth — no load/store dance). Still scalar-Copy only;
  aggregate/non-Copy stays E0600. [4c: `await` inside a `match` arm, deferred.]

- _2026-07-23_ — **Inc 4b DONE + validated (both OSes). No repin (0/235 `.ll` diff). UNCOMMITTED.** Baseline
  HEAD `61f5beaa` (Inc 4a committed+repinned by Jake), pin verifies OK. One modified source file:
  `sema/async_lower.cryo`. `AsyncLower` now lowers **awaits across all four loop forms** (`while`/`for`/`loop`/
  `do-while`) with `break`/`continue` and **`mut` loop-carried promotion** — completing common-control-flow
  async lowering (only Inc 4c, `await` inside a `match` arm, remains). Built on Inc 4a's `loop { match
  (this.state) {…} }` dispatcher: a back-edge is just another `this.state = <header>;` forward-edge, so no new
  dispatch mechanism was needed.
  - **Loop lowering** (`lower_while_sm`/`lower_for_sm`/`lower_loop_sm`/`lower_do_while_sm`): each allocates
    header/body-entry/[update]/after states. `while` = header tests cond (dispatch body-entry vs after) + body
    + back-edge to header. `for` = init lowered into the current state, then header/body/update/after; the
    update state runs the update expr then → header (so `continue` → update → re-test, correct). `loop{}` =
    body-entry with a self back-edge (infinite; only `break` exits to after). `do-while` = body-entry (run
    unconditionally) → tail-test state → after. Shared `emit_cond_dispatch` appends `if (cond) { state=t } else
    { state=f }`. Await in a loop condition/update/`for`-init → E0600.
  - **`break`/`continue`** → `this.state = <target>;` edges via a **loop-target stack** on `PollSm`
    (`loop_cont`/`loop_brk`, pushed/popped around each exploded loop's body). A native `break`/`continue` would
    break the dispatch `loop`/`match`, so they must lower to edges. **The stack top is always the innermost
    EXPLODED loop:** an await-free nested loop is emitted wholesale (its break/continue stay native), so
    break/continue only *reach* the state-machine lowerer when they target a loop currently being exploded.
    Continue targets: `while`/`loop` header, `for` update state, `do-while` tail test; break target: the after
    state.
  - **`stmt_needs_explode`** now gates wholesale emission on `await OR a free break/continue` (not await
    alone): an await-free `if (c) { break; }` inside an await-carrying loop must still be exploded (its `break`
    can't stay native inside the dispatcher). `has_free_edge` walks if/block/match but NOT nested loops (they
    capture their own break/continue).
  - **Divergence moved to SOURCE AST.** `stmt_diverges` now covers `break`/`continue` (transfer control
    elsewhere) and an infinite `loop{}` (`!has_free_break`). `lower_if_sm`'s join-suppression AND the loop
    back-edges now test `stmt_diverges(SOURCE branch/body)`, not the lowered block's tail — a break-terminated
    branch leaves a `this.state=<after>;` edge in its final state block, which is NOT a divergence marker, so
    inspecting the lowered block would wrongly append a second (overwriting) edge and lose the break.
    `lower_block_sm` also stops after a source-diverging statement (unreachable-after-terminator would
    otherwise be emitted PAST the edge assignment and run before the arm falls off).
  - **`mut` loop-carried promotion (the genuinely new piece).** A scalar-Copy `mut` local read/written across
    states becomes a struct field that is the **single source of truth**: `promote_cross_state` (build-then-
    scan) branches on mutability — const keeps the Inc-3 store-at-decl / load-at-reader model; **mut** replaces
    its declaration with `this.<field> = <init>;` (`rewrite_mut_decl`) and rewrites every read AND write of the
    name to `this.<field>` in place (`subst_name_expr`/`subst_name_stmt`, the mutating twins of the read-
    detection walker — an assignment's LHS is an identifier, so one rewrite handles reads and writes uniformly,
    incl. `i++` via the unary-operand case). Same guards: reference across suspend → E0455; aggregate/
    non-scalar → E0600. New guard: a `mut` name re-declared in another state → E0600 (the by-name rewrite
    can't disambiguate two live ranges).
  - **Validation (via `future::block_on`, no turbofish):** scratchpad `async_inc4b/` drives 9 cases → EXIT 0:
    `while`+mut counter/accumulator (immediate-Ready await), `while` with a genuine 2-Pending-per-iteration
    suspension, `for`+`continue`(skip)+`break`(early exit)+suspend, `loop{}`+`break`, `do-while`, a **nested**
    await-loop (`for` inside `while`; `total`/`i`/`j` all promoted), plus straight-line (Inc 3) and if/else
    (Inc 4a) regressions. Negatives (`async_neg4b/`) fire clean E0600: await-in-`while`-condition, aggregate-
    mut-across-await. Gotcha: a `for`-init needs an explicit type annotation (`for (mut i: i32 = 0; …)`) —
    Cryo E0104; statement-level `mut x = 0` infers, a for-init does not.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4` md5
    `5bacb46b83c166bc759794ee338a8c33`; Windows `s3`==`s4`, 235 modules). `win-s2` (pinned-built) vs `win-s3`
    (new-built) = **0/235 differing `.ll`** → the lowering is inert for async-free code → **NO REPIN**. Pin
    verifies OK; tree = `M async_lower.cryo` + `M ASYNC_IMPL.md`.
  - **Durable limitation (shared with the const path since Inc 3/4a — NOT introduced by 4b) — ✅ RESOLVED
    2026-07-23 by the scope-aware alpha-rename (see the next Progress-Log entry):** cross-state promotion WAS
    **by-name**. It under-promotes safely in most cases (a missed read surfaces as a loud "not found"), but a
    promoted local that **shadowed a same-named parameter** (or a sibling-scope local of the same name) could be
    conflated by the global rewrite → a silent wrong value. Now fixed: a pre-flattening alpha-rename makes every
    local's name globally unique first, so the by-name promotion is exact for both paths.
  NEXT: **Inc 4c — `await` inside a `match` arm** (pattern bindings live across a suspend); currently E0600.
  After 4c, Phase 2 is complete → **Phase 3** (executor + `spawn`/`JoinHandle` + multi-thread + poll-boundary
  `catch_unwind` isolation).

- _2026-07-23_ — **Cross-state promotion is now scope-aware — the by-name param/sibling conflation is FIXED. No
  repin (0/235 `.ll` diff). UNCOMMITTED.** Baseline HEAD `ad20987e` (Inc 4b committed+repinned by Jake), pin OK.
  One modified file: `sema/async_lower.cryo` (+~230). Closes the durable limitation flagged in the Inc 4b entry.
  - **Root cause:** promotion (`promote_cross_state` + `subst_*`) rewrites by NAME, and `IdentifierNode` carries
    only a `name` (no resolved-decl link), so after the body is flattened into state blocks — where lexical
    scope structure is gone — a param read (`mut sum = p`) and a shadowing local (`mut p` inside a loop) both
    read as `Identifier(p)` and were rewritten to the same field. Confirmed: `shadow(100)` returned 6 (0 + 1+2+3)
    instead of 106 (100 + 1+2+3). Sibling same-name locals hit the `mut` re-declared guard (spurious E0600).
  - **Fix = a pre-flattening scope-aware alpha-rename** (`disambiguate_locals`, first thing in
    `build_poll_body_sm`, BEFORE the body is flattened). It walks the original NESTED body with a lexical scope
    stack (`RenameCtx`: parallel `orig`/`repl` binding stack + `marks` scope boundaries) and renames every local
    `VarDecl` (block + `for`-init) to a globally-unique name (`<orig>$L<n>`), rewriting uses via innermost-first
    scope lookup. **Parameters are intentionally NOT renamed** — their uses fall through unchanged and resolve to
    the shadow prelude (`const <param> = this.<param>`), so a local shadowing a param no longer shares its name
    once flattened. After the rename every binding is uniquely named → the by-name promotion is EXACT and the
    `mut` re-declared guard never fires for legitimate siblings. (Alpha-rename, not decl-identity keying, because
    identifiers have no decl link — renaming reconstructs the disambiguation the lost scope structure provided.)
  - **In-place + total coverage:** the walk mutates `IdentifierNode.name` / `VarDeclNode.name` in place (no slot
    reassignment). `rn_expr` covers EVERY expression form that can hold a local read (the full cloner kind list:
    identifier/binary/unary/ternary/if-expr/call/new/cast/struct-lit/array-lit/tuple-lit/array-access/member/
    typeof/delete/await/try/match-expr/lambda + the `desugared_call` operator-overload slots); `rn_stmt` covers
    block/unsafe-block/if/while/do-while/for/loop/match/switch + decl/expr/return. Scopes: block, branch (a bare
    non-block branch gets its own frame), `for`-init (encloses cond/update/body), match-arm (pattern
    `binding_name`s shadow), lambda (params shadow + captured-name rewrite). An unwalked form leaves a renamed
    local's use un-renamed → a loud "not found", never a silent miscompile.
  - **Validation:** `async_shadow/` → EXIT 0: `shadow(100)=106` (was 6), `sib(true/false)=7/13` (sibling case
    now compiles + correct, was spurious E0600). `async_shadow2/` → EXIT 0: a `for`-init local shadowing a param
    (param read first) and an `if`-branch local shadowing a param. Inc 4b regression (`async_inc4b/`, 9 cases)
    and negatives (`async_neg4b/`, E0600) unchanged.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, BOTH fixed points (Linux `s3`==`s4` md5
    `5bacb46b83c166bc759794ee338a8c33`; Windows `s3`==`s4`, 235 modules). `win-s2` (pinned-built) vs `win-s3`
    (new-built) = **0/235 differing `.ll`** → the rename is inert for async-free code (the compiler has no
    `async fn`) → **NO REPIN**. (The s3/s4 fixed-point IR size grew vs the Inc 4b baseline only because the
    compiler SOURCE grew +~230 lines — win-s2 and win-s3 grew equally, so they still match byte-for-byte.)
  - **Discovered gap (separate, NOT fixed here):** a BARE block `{ … }` containing an `await` is not lowered
    (`lower_stmt_sm` → E0600); only `if`/loops/carriers explode. Trivial to add (`lower_block_sm` on a nested
    `BlockStatement`) — noted as an Inc 4b follow-up.
  NEXT: unchanged — **Inc 4c** (`await` in a `match` arm), then Phase 3.

- _2026-07-23_ — **Inc 4c DONE + validated (both OSes). No repin (0/235 `.ll` diff). UNCOMMITTED. Phase 2 is
  now COMPLETE.** Baseline HEAD `5e28a74f` (scope-aware fix committed+repinned by Jake), pin verifies OK. Two
  modified source files: `sema/async_lower.cryo` (+304/−27) and `sema/sema.cryo` (+1, the wire). `AsyncLower`
  now lowers **`await` inside a `match` arm**, plus the **bare-block-with-await** warm-up — closing all common
  control flow. Jake signed off the binding-lifetime approach via the question tool: **"promote scalars, defer
  aggregates"** (Option 1).
  - **Warm-up (bare block):** `lower_stmt_sm` gained a `BlockStatement` case → `lower_block_sm` (a nested
    `{ … await … }` block is just a statement sub-sequence; its locals already get their own scope frame from
    `disambiguate_locals`). Was E0600 before.
  - **`lower_match_sm` (the increment):** in `cur`, a **dispatch `match (subj)`** (reusing the original
    patterns + guards) captures each awaiting arm's scalar pattern bindings into fields and sets
    `this.state = <arm entry>`; each arm body runs as its own state sequence and, unless it diverges,
    converges on a shared **join** state — structurally `lower_if_sm` generalized to N arms. An **await-free
    arm runs wholesale** in the dispatch arm (its bindings stay native → aggregate/ref bindings in a non-await
    arm are unrestricted). A **non-exhaustive** match gets a synthesized `_ => { this.state = join; }` so a
    non-matching subject continues past the `match` rather than respinning the (unchanged) dispatch state.
    Await in a match **subject** or **guard** → E0600 (bind it first / restructure).
  - **Pattern-binding promotion (the genuinely new problem).** A payload binding (`v` in `Some(v)`) is an
    lvalue-pointer into the scrutinee with **no cached type** and lives in `cur`'s scope, but its arm body runs
    in a different state — so a scalar binding **read** in the arm body (`name_read_in_stmt`) is promoted to a
    field: the dispatch arm captures `this.__bind_<entry>_<name> = <name>;` (reading the pattern binding, valid
    in the dispatch arm's scope — a scalar read copies the value out), and the arm body's reads are rewritten
    in place to `this.__bind…` via the existing `subst_name_stmt` (reused verbatim from the `mut`-local path;
    field registered in `sm.prom_field`/`prom_tys` like any promoted scalar, zero-init in the ctor). An
    **unused** binding is skipped (no field, no error → an unused aggregate binding in an awaiting arm is
    fine). **Binding TYPE is re-derived** via `PatternResolver::resolve_variant_payload_types` (patterns carry
    no `resolved_type`): `AsyncLower.wire` now takes the already-wired `&this.patterns` (sema.cryo:194); the
    collectors mirror `bind_arm_patterns`/`bind_enum_pattern` (top-level identifier binding → subject type;
    enum payload → `payload_types[j]`; nested `Sub` → recurse). Guards: aggregate binding across a suspend →
    E0600, reference → E0455 (same limits as `mut`/aggregate/reference locals — Inc 3/4b).
  - **Pattern bindings are now alpha-renamed too (soundness).** `subst_name_stmt` is by-name, so a nested
    `match` re-binding the same name (`Some(v) => { await…; match(x){ Some(v)=>use(v) } }`) would be conflated
    → silent wrong value. Fix: `rn_arm` (in `disambiguate_locals`) now renames every pattern binding to a
    globally-unique `<orig>$L<n>` (mirroring the `VarDecl` rename), sharing one fresh name across OR-alternatives
    (`A(v) | B(v)`); const-value identifier patterns (their `binding_name` cleared by name resolution) are not
    renamed. New helpers `collect_pat_binding_names` + `rn_pat_apply` (mutate `PatternBinding.name` /
    `PatternNode.binding_name` through the AST — `binding_at`/`sub_at` return the real payload pointers).
  - **`stmt_diverges` gained a `MatchStatement` case:** a match never falls through iff `match_is_exhaustive`
    (via the wired `PatternResolver`) AND every arm body diverges. Without it, an async body ending in an
    exhaustive all-arms-return `match` (no `_`) tripped a spurious "must return on every path" E0600.
  - **Validation (via `future::block_on`, no turbofish):** scratchpad `async_match/` drives 11 cases → EXIT 0:
    scalar match with awaiting arms + no bindings; enum payload binding read after a suspend; mixed
    wholesale+awaiting arms; binding read before AND after a suspend; the **`nested_bind` case (=105)** that
    directly proves the alpha-rename (a nested `match` re-binding `v` — without the rename it returns 5); and a
    match ending the body with all arms returning. `async_bareblock/` (2 cases) covers the warm-up incl.
    cross-state promotion through a nested block. Negatives (`neg_subj`/`neg_guard`/`neg_agg`) fire clean E0600
    for await-in-subject / await-in-guard / aggregate-binding-across-await. The E0455 reference-binding branch
    mirrors the locals path and is present but not separately probed (a reference match binding is hard to
    construct — same note as Inc 4a's E0455).
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4`; Windows
    `s3`==`s4` byte-identical IR). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0/235 differing `.ll`**
    → the lowering is inert for async-free code (the compiler has no `async fn`) → **NO REPIN**. Pin verifies
    OK; tree = `M async_lower.cryo` + `M sema.cryo` + `M ASYNC_IMPL.md`.
  - **Durable notes:** (1) pattern bindings carry NO cached type — re-derive via
    `resolve_variant_payload_types`; they are lvalue-pointers into the scrutinee, so a scalar read in the
    dispatch arm copies the value out (the subject need NOT stay alive across the suspend). (2) Cryo has NO
    tuple patterns — positional bindings exist only as enum-variant payload elements. (3) The dispatch match
    reuses the original patterns/guard (the source `match` node is discarded), so its exhaustiveness == the
    original's; enum non-exhaustiveness is already E0405 pre-lowering, so only integer/partial matches reach
    the synth-`_` path.
  NEXT: **Phase 3 — executor.** `block_on` real single-thread executor + task queue; a `Waker` that re-enqueues
  its task; `spawn(future) -> JoinHandle`; then multi-thread (worker pool over `pthread`) with `catch_unwind`
  at the poll boundary (sound because of Track 2's thread-local panic state). Validate task isolation (a
  panicking task yields an error, siblings survive) under `--panic=unwind`. Remaining Phase-2 tails (all
  deferred with loud diagnostics, none blocking Phase 3): `await` nested in an expression (E0600), aggregate/
  reference locals **and** match bindings across a suspend (E0600/E0455), a `-> ()` async fn whose awaited body
  falls off its end (E0600), `await` in a `match`/loop-condition/guard (E0600).

- _2026-07-23_ — **Phase 3 STARTED — executor surface LOCKED by Jake (one-way door closed).** Baseline HEAD
  `10414486` (Phase 2 / Inc 4c committed+repinned by Jake), pin verifies OK, tree clean. Brought Jake a
  recon-grounded surface proposal (recon sweep: `stdlib/future/*`, `stdlib/thread/_module.cryo`,
  `stdlib/sync/{atomic,mpsc,condvar,mutex}.cryo`, `stdlib/core/panic_unwind.cryo`). Two rounds of sign-off
  (the second after Jake brought a design-review second opinion framed as "Rust's semantics, C#'s allocation
  strategy"). **The seven locked decisions:**
  1. **Module home → `stdlib/future/executor.cryo`** (`namespace std::future::executor`; `public module
     future::executor;` in `future/_module.cryo`). One `std::future` import surface, cohesive with
     Poll/Future/Waker. (`stdlib/async/` is impossible — `async` lexes as `KwAsync`, can't be a namespace
     segment; this is why Phase 1 used `future`.)
  2. **Executor surface → explicit `Executor` handle; `spawn` is a METHOD; free `block_on` stays.** No implicit
     global runtime for v1 (§7-5) ⇒ `spawn` can't be an ambient free-fn (nothing to find) ⇒ it's
     `exec.spawn(fut)`. `exec.block_on(root)` drives root + spawned tasks; free `future::block_on(fut)` remains
     the trivial single-future driver (stack slot, no heap). The explicit handle is ALSO the allocation hook:
     `spawn` boxes into executor-owned storage → `Executor<A: Allocator = GlobalAlloc>` is a natural extension
     (mirror `Mutex<T,A>`) — carry an allocator field from day one. This is the "C# allocation" boundary:
     `block_on` = stack, `spawn` = box.
  3. **`JoinHandle` drop = detach** (mirror `thread::JoinHandle` — reuse the `Shared<T>` + `Atomic<u8>`
     LIVE/DONE/DETACHED 2-actor handshake verbatim). **REFINEMENT (design review): add `JoinHandle::abort()`**
     — detach-only leaves a spawned task uncancellable. `abort()` sets a CANCELLED state; the worker drops the
     boxed future before its next poll (sound TODAY: promoted locals are scalar-Copy-only, so no aggregate
     drops; the only owned fields are `Option<sub-future>`s, which drop correctly via field glue). ⇒ **`join`
     becomes `join(mut this) -> Result<T, JoinError>`** with `JoinError { Cancelled; Panicked(PanicInfo); }`
     (tokio shape; `Panicked` only occurs under `--panic=unwind` — under abort a task panic aborts the process).
     Note: `select`/`timeout` (Phase 4) cancel by DROPPING an un-spawned future one level down (a normal Cryo
     drop), NOT via `abort` — `abort` is specifically stopping an already-*spawned* task from outside.
  4. **Waker wakeup model → synchronous self-wake + re-enqueue.** `Waker.wake(data)` re-enqueues the task;
     validation futures call `cx.waker.wake()` before returning `Pending`. **REFINEMENT (design review): reserve
     the Rust `RawWakerVTable` shape NOW** even though Phase 3 won't exercise it — expand `Waker` to
     `{ wake_fn: (u8*)->void; clone_fn: (u8*)->u8*; drop_fn: (u8*)->void; data: u8* }` with **noop clone/drop**
     (the *correct* semantics for a non-owning Copy waker, not a stub), inline fn-ptrs (NOT a `static` vtable —
     the "non-zero global inits ignored" gotcha makes a static-vtable-const a landmine). Keeps `Waker` POD/Copy
     in Phase 3; provide `wake`/`wake_by_ref`/`clone` methods and use `clone()` at duplication sites so Phase 4
     only fills in the fn-ptr bodies + adds a `Drop` impl (flipping Copy→non-Copy) — no field-layout change mid
     epoll-bring-up. **Design-review correction (durable): "detach forces task refcounting in Phase 3" is
     FALSE** — `thread::spawn` detaches with a 2-state swap, no refcount; Phase-3 self-wake-only wakers never
     escape a `poll`, so no waker outlives a task. What Phase-3 *multi-thread* genuinely needs is a per-task
     **atomic state machine** (IDLE/SCHEDULED/RUNNING/NOTIFIED) to stop two workers polling the same task after
     a mid-poll self-wake — that lives on the task control block and does NOT touch the Waker shape. The
     waker-held **refcount** (clone bumps / drop decrements) is exercised only when a waker is STORED beyond a
     poll = the Phase-4 reactor.
  5. **`poll` stays a public trait method — and that is SOUND for Cryo** (design-review correction, durable).
     Rust gates `poll` behind `Pin<&mut Self>` because Rust futures ARE self-referential (borrows across
     `await` are allowed). Cryo BANNED borrows across `await` (§4, enforced in `move_check`) ⇒ futures have no
     self-references ⇒ unconditionally movable ⇒ the no-Pin soundness rests on the *move-checker ban*, NOT on
     hiding `poll`. A user calling `fut.poll(&cx)` directly and moving the future between calls is sound; even
     polling an aliased copy is memory-safe (panics on double-`take()`, not UB). `poll` is inherently public
     anyway (trait method; hand-written futures impl it; the executor calls it cross-module).
  6. **Async I/O → owned-handle style for v1** (design-review confirmation of §4). The idiomatic Rust
     `stream.read(&self).await; use(stream)` IS a self-referential future (stored read-future field points at
     the `stream` field) → E0455 under the ban. §4 already priced this in ("owned-value rewrites exist"): async
     I/O uses `const (stream, n) = stream.read(buf).await;` (the read future OWNS the stream, hands it back) or
     an `Arc<Resource>` clone — both sound + movable. Ban stays for v1; relax (real Pin/lifetimes) post-v1. Not
     a Phase-3 concern (the executor hardens nothing either way), but consciously accepted now rather than
     discovered at the first `TcpStream` adapter.
  7. **Type erasure (locked mechanism)** — heterogeneous futures share one ready-queue via the `Waker`/
     `catch_unwind`/`thread::spawn` vtable trick: heap-box the concrete `F`, plus a monomorphized poll thunk
     `task_poll<F,O>(fut: u8*, shared: u8*, cx: Context*) -> u8` (0=Pending/1=Ready; stores `v` into the
     type-erased `Shared<O>` via raw `O*`, same no-drop-synth idiom as `thread_body`), addressable via the
     generic-function-reference mechanism (proven by `thread_trampoline<C,T>`). `JoinHandle<O>` wraps the
     `Shared<O>`.
  **Build plan (each: build → validate under `block_on`/inline driver → `make stdlib` green + `verify-pin` OK
  → NO repin — Phase 3 is PURE STDLIB, touches no compiler source, so selfhost IR is definitionally unchanged;
  a full `selfhost-check` is unnecessary unless compiler source is touched):**
  - **(a)** single-thread `Executor` core: mutex-guarded ready-queue + type-erased `Task` + the re-enqueueing
    `Waker` (expanded shape) + `block_on(root)`/`run()` draining on the calling thread. Validate self-waking
    futures complete (observe via atomic counter).
  - **(b)** `spawn<F>(fut) -> JoinHandle<F::Output>` via the `Shared<O>` handshake + type-erasure thunk;
    `join -> Result<T, JoinError>`; `detach`; `abort`; drop=detach. Validate single-thread spawn/join outputs.
  - **(c)** N `pthread` workers (mirror `thread::spawn`) + per-task atomic state machine (no concurrent poll) +
    `catch_unwind` at the poll boundary → task isolation. Validate under `--panic=unwind` in WSL: a panicking
    task yields `Err(Panicked)`, siblings survive.
  NEXT: build stage (a). Start with the expanded `Waker` (grep `Waker {`/`Waker::` callers first), then the
  `Executor`/`Task`/ready-queue in a new `stdlib/future/executor.cryo`.

- _2026-07-23_ — **Phase 3 stage (a) DONE + validated. Pure stdlib, NO repin. UNCOMMITTED.** Baseline HEAD
  `10414486`, pin verifies OK. Two files: `stdlib/future/waker.cryo` (expanded Waker) + NEW
  `stdlib/future/executor.cryo`; `future/_module.cryo` registers the new module (148 stdlib modules).
  - **Waker expanded to the reserved Rust-vtable shape** (decision 4): `{ wake_fn: (u8*)->void; clone_fn:
    (u8*)->u8*; drop_fn: (u8*)->void; data: u8* }`, still POD/Copy, with `wake`/`wake_by_ref`/`clone` methods,
    `noop()` (all-noop), `new(4 args)`, and **`new_nonowning(wake_fn, data)`** (fills identity clone + noop drop
    — the Phase-3 executor constructor; encapsulates the private `waker_noop_clone`/`waker_noop_drop`). Blast
    radius was tiny (only `block_on`'s `Waker::noop()` + the ctors themselves construct a Waker; sample futures
    only receive `Context*`). `make stdlib` green in isolation before layering the executor.
  - **`stdlib/future/executor.cryo` (the single-thread core):** `Task` (type-erased: `poll_fn (u8*,Context*)->u8`
    [1=Ready/0=Pending] + `drop_fn (u8*)->void` + `fut: u8*` box + `next`/`exec`/`queued` queue linkage);
    `ExecInner` (singly-linked FIFO `head`/`tail`); free fns `exec_enqueue` (with a `queued` guard so a self-wake
    during a poll can't double-link) / `exec_dequeue`; `task_wake(data)` (data=Task*, re-enqueues via `exec`);
    monomorphized `task_poll_thunk<F,O> where F: Future<O>` + `task_drop_thunk<F>` + `spawn_task<F,O>` (boxes the
    future via `*fbox = fut` raw-write [no drop-synth, thread_body idiom], takes the generic-fn refs — proven
    addressable-generic-fn shape); `Executor { inner }` with `new`/`spawn_detached<F,O>`/`run` + a `Drop` that
    reclaims still-queued tasks.
  - **`run()` ownership fix (real-solution, not a leak):** a task that returns `Pending` and **did not** arrange
    a wake (its `queued` flag is still false after poll) is **unreschedulable** in this stage (no reactor, no
    stored waker) → reclaimed immediately (its future's drop runs) rather than dequeued-then-leaked. The `!task.
    queued` check is exactly where Phase 4's stored-waker/refcount ownership will move. A self-woke task
    (`queued==true`) is owned by the queue and re-polled.
  - **Validation (`scratchpad/async_exec_a`, built with pinned `bin/cryo.exe` + `CRYO_STDLIB`):** one `Executor`
    + `run()` drives three **distinct** future structs in one type-erased queue → EXIT 0: `WakeCounter`
    (self-wakes N times then adds to a shared `Atomic<i64>`; 0/3/7 pends → 1+20+300), `Immediate` (different
    type, first-poll Ready → +1000), `Parked` (Pending-forever-no-wake, with a `Drop` that bumps a drop-counter).
    Asserts `counter==1321 && parked_drops==1` — proving self-wake→re-enqueue→re-poll, heterogeneous-type erasure,
    round-robin draining, AND reclaim-runs-drop for the unreschedulable task. `make stdlib` green (148 modules),
    `verify-pin` OK. Pure stdlib (no compiler source touched) ⇒ selfhost IR definitionally unchanged ⇒ **NO
    REPIN** (no selfhost-check needed).
  - **Durable notes:** (1) `(*fut).poll(cx)` (method call on a deref'd raw pointer) type-checks — same shape as
    `thread_body`'s `(*shared).state.swap(...)`. (2) A generic **method** (`Executor::spawn_detached<F,O>`)
    delegating to a generic **free fn** (`spawn_task<F,O>`) that holds the generic-fn reference works — no
    method-generic-fn-ref pitfall hit. (3) The probe's u32 arithmetic uses explicit `0u32`/`1u32` suffixes
    (mirroring `ready.cryo`) to dodge literal-inference round-trips; committed stdlib code stays bare per
    [[avoid-suffixed-numeric-literals]] where inference allows.
  NEXT: **stage (b)** — `spawn<F,O>(fut) -> JoinHandle<O>` returning the future's Output via a `TaskShared<O>`
  control block (non-generic `TaskCtl` header at offset 0 so the type-erased executor reads state/cancel without
  knowing O); `Executor::block_on<F,O>(root) -> Result<O, JoinError>` (single-thread: pump the queue on the
  calling thread until root's outcome is set); `JoinHandle` `join`/`detach`/`abort`/`is_finished`, drop=detach,
  with `JoinError { Cancelled; Panicked(PanicInfo); }`. Single-thread `join`/`block_on` **pump** the queue
  (there are no workers yet); stage (c) replaces the pump with a condvar wait + a per-task RUNNING state + the
  `catch_unwind` poll boundary.

- _2026-07-23_ — **Phase 3 stage (b) DONE + validated. Pure stdlib, NO repin. UNCOMMITTED.** Baseline HEAD
  `10414486`, pin verifies OK. `stdlib/future/executor.cryo` rewritten (+~200 lines) to return the future's
  Output via a control block; `waker.cryo` unchanged from stage (a). `make stdlib` green (148 modules),
  `verify-pin` OK.
  - **Control block:** `TaskShared<O> { ctl: TaskCtl; result: O }` where **`TaskCtl` (non-generic: 3×`Atomic<u8>`
    — `outcome`/`hs`/`cancel`) sits at offset 0**, so the type-erased executor reads/writes state through a
    `task.shared as TaskCtl*` without knowing `O`. `Task` gained `dispose_fn: (u8*, u8)->void` (the one
    shared-block op the erased side can't do without `O`: optionally drop the stored `result`, then free the
    `TaskShared<O>` — monomorphized `dispose_shared_thunk<O>`), and `shared: u8*`. `poll_fn` is now
    `(fut, shared, cx)->u8`: on Ready it stores `v` into `(*sh).result` through a raw `O*` (thread_body
    no-drop-synth idiom).
  - **Two-actor handshake (mirror `thread::spawn`)** on `ctl.hs`: `HS_LIVE`→`HS_TASK_FIN` (task finishes) /
    `HS_HANDLE_GONE` (handle detaches/drops). Whoever swaps in last frees `TaskShared`. `ctl.outcome`
    (`PENDING`/`READY`/`CANCELLED`) is published by `finish_task` before its `hs.swap`; a joining handle reads
    it to build `Result<O, JoinError{Cancelled; Panicked(PanicInfo)}>`. `ctl.cancel` is the handle→executor
    abort request; `drive_task` checks it BEFORE polling (so `abort` stops even a forever-self-waking task
    without polling it). A `Pending`-no-reschedule task → `finish_task(CANCELLED)` (unreschedulable — same
    reclaim rule as stage (a), now routed through the handshake).
  - **Surface (locked decisions realized):** `Executor::spawn<F,O>(fut)->JoinHandle<O>`,
    `spawn_detached<F,O>` (= spawn+detach sugar; kept so the stage-(a) probe still regresses),
    `block_on<F,O>(root)->O` (spawns root, **pumps the queue on the calling thread** via `pump_until`, unwraps —
    panics if the root was cancelled), `run()` (drive-to-empty). `JoinHandle<O>`: `join(mut this)->Result<O,
    JoinError>` (single-thread: `pump_until` then move the result out + free the block — destructures `this` to
    suppress its `Drop`, like `thread::JoinHandle::join`), `detach(mut this)`, `abort(&this)` (non-consuming),
    `is_finished(&this)`, **drop=detach** (`detach_shared<O>` in `Drop`). `Executor::drop` reclaims still-queued
    tasks as CANCELLED (runs the handshake → no leak).
  - **Validation (`scratchpad/async_exec_b`):** four tests → EXIT 0 — (A) `block_on(Compute(3,42))==42` (+ a
    second `block_on` to prove the first didn't corrupt the heap); (B) spawn two `Compute` + `join` each →
    10/20; (C) `abort` a forever-self-waking task → `join` returns `Err(Cancelled)` (no infinite poll — cancel
    checked pre-poll); (D) detach a task with an **owned Output** (`Tracked` with a `Drop`) → the result is
    dropped **exactly once** by the dispose path (`drops==1`). Stage-(a) probe (`async_exec_a`,
    `spawn_detached`) still EXIT 0 (regression).
  - **Durable gotchas:** (1) **A zero-sized future can't be boxed** — `Layout::of<F>()` for a fieldless struct
    has size 0 and `GlobalAlloc.allocate(0)` returns `Err` → "future allocation failed" panic (on Windows
    `abort()` exits **code 3**, which looked like a probe-branch return — watch for that). REAL lowered async
    futures always carry a `state: u32` field so they're never zero-sized; a hand-written fieldless future is
    the only trigger. **TODO (robustness, deferred):** handle ZST futures in `spawn_on` (Rust uses
    `NonNull::dangling()` for ZSTs; skip the box + the dealloc when size==0). (2) A generic **method**
    delegating to a generic **free fn** that returns a generic struct (`Executor::spawn<F,O>` → `spawn_on<F,O>
    -> JoinHandle<O>`) works. (3) Storing the Ready value `*rptr = v` where `v` is a by-value `match` binding
    moves it (no double-drop) — same as `mpsc::send`'s `*slot = value`.
  NEXT: **stage (c)** — multi-thread. N `pthread` workers (mirror `thread::spawn`) pulling from a **mutex+condvar
  guarded** ready-queue; a **per-task atomic RUNNING/NOTIFIED state** so a mid-poll self-wake (or a cross-thread
  wake) can't let two workers poll one task; `join`/`block_on` **block on a condvar** (workers drive) instead of
  pumping; **`catch_unwind` at the poll boundary** (a `run_poll(ctx)` trampoline mirroring `run_catch_body`) so a
  panicking task yields `Err(Panicked)` and siblings survive. Validate task isolation under `--panic=unwind` in
  WSL (`bin/cryo` is the Linux ELF). Keep the `--panic=abort` path working (task panic aborts, per §7-6).

- _2026-07-24_ — **Phase 3 stage (c) DONE + validated: multi-thread worker pool + `catch_unwind` task
  isolation.** Environment: fresh **Linux codespace** (not Jake's Windows box — the HANDOFF warned to verify;
  `--panic=unwind` is native here, no WSL needed). **State discrepancy found at start:** commit `b159494e`
  ("Implement multi-threaded executor … Phase 3 Stage c") + repin `b3ab958d` were already on `ll-impl`, but
  `b159494e` actually committed the stage-(a)/(b) **single-thread** executor under a stage-(c) label (its own doc
  said "single-thread core … no worker pool yet"; no pthread/atomic-state/`catch_unwind` in the code). Stage (c)
  was genuinely still to do; this entry does it.
  - **Jake's two decisions (asked up front):** (1) **compiler atom + full stage (c) with isolation** (over a
    pure-stdlib-executor-first split); (2) **`Executor::new()` always spins workers** (over single-thread `new()`
    + opt-in `with_threads`).
  - **Compiler prerequisite (Phase 1, both-OS REPIN):** `catch_unwind`/`intrinsics::try_catch` is a hard compile
    error under `--panic=abort` (`call_emitter.cryo` `emit_catch_unwind`, gated on `ctx.project_panic_unwind`),
    and ConfigGating had no panic-strategy atom. Added **`panic_unwind` as a feature atom** to `CfgEnv`
    (`config_gating.cryo`: field + `from_ctx` from `ctx.project_panic_unwind` + `is_feature_atom` +
    `feature_active`) and to the `directive_processing.cryo` flavor validator. The `not(...)` combinator + bare
    `config(atom)` already dispatch feature atoms generically, so `![config(panic_unwind)]` /
    `![config(not(panic_unwind))]` both work with ZERO evaluator changes. selfhost-check GREEN (win s3==s4
    byte-identical, 235 modules) → `make pin` both OS, `verify-pin` OK. This is the **2-phase repin ritual**: the
    pinned compiler must understand the atom BEFORE the stdlib that uses it can build (old pin would keep both
    gated `poll_boundary` variants → duplicate-def).
  - **Executor rewrite (Phase 2, pure stdlib, NO further repin):** `stdlib/future/executor.cryo`. `ExecInner`
    now embeds two pthread mutex+condvar pairs (`u8[40]`/`u8[48]`, mpsc idiom): a **queue** lock/cond (workers
    park in `take()` until work or shutdown) and a **done** lock/cond (joiners park until a task finishes).
    `Task` gains an `Atomic<u8> run_state` (**IDLE/SCHEDULED/RUNNING/NOTIFIED**) replacing the stage-a/b `queued`
    bool: `task_wake` CAS `IDLE→SCHEDULED`(+enqueue) or `RUNNING→NOTIFIED`(defer); a worker CAS `SCHEDULED→RUNNING`
    (done under the queue lock at dequeue), then on Pending CAS `RUNNING→IDLE` (quiet → reclaim CANCELLED,
    unreschedulable in Phase 3) or `NOTIFIED→SCHEDULED`(+reenqueue). `join`/`block_on` **block on the done-cond
    until `hs == HS_TASK_FIN`** (NOT merely `outcome != PENDING` — the finishing worker's hs.swap is its LAST
    touch of the block, so waiting on hs closes the read-vs-free race), then read+free exactly as stage (b).
    `finish_task` broadcasts the done-cond only on the HS_LIVE path (a live joiner), after the hs.swap.
    `TaskCtl` gains a `panic: PanicInfo` field for the PANICKED path.
  - **Poll-boundary isolation:** two file-scope `poll_boundary(poll_fn, fut, shared, cx, out_panic)` free fns
    (ConfigGating strips decls, not method bodies): `![config(panic_unwind)]` runs the erased poll inside
    `intrinsics::try_catch` via a `run_poll(void*)`/`PollCtx` trampoline (mirrors `run_catch_body`), and on a
    caught panic reads `panic_unwind::taken_panic_info()` (NEW gated helper in `panic_unwind.cryo` that reads the
    `__cryo_panic_taken_*` externs — gated so they stay unreferenced under abort) → status 2 → `finish PANICKED`
    with the info stashed in `TaskCtl.panic`; the `![config(not(panic_unwind))]` twin is a plain direct poll
    (task panic aborts the process — accepted, §7-6). The panic runtime tier `runtime/.bin/libcryort-panic-unwind.a`
    must exist (`cd runtime && cryo build` — it was missing on this box; the `[[lib]] cryort-panic-unwind` member
    builds it).
  - **Self-contained (no `thread` dep):** `import thread` brought `thread::JoinHandle` into scope and its leaf
    clashed with `executor::JoinHandle` — misresolving the destructure-annotation form AND (in a consumer's
    dependency-build) `spawn_on`'s return type. Fix: **dropped `import thread` entirely**; the executor owns its
    pool via gated `worker_tramp`/`exec_spawn_worker`/`exec_join_worker`/`exec_cpu_count` free fns (same
    pthread/Win32 primitives `thread` uses) and stores worker tids in a `u64[]`; `Executor::drop` sets shutdown +
    broadcasts + joins each worker + `drain_cancelled` + frees. `Executor::new()` = `with_threads(cpu_count())`.
  - **Landmines hit + durable fixes:** (1) **`import thread` JoinHandle leaf-clash** → self-contain (above);
    qualifying the annotation (`future::executor::JoinHandle`) does NOT help — the resolver binds the bare leaf to
    the import. (2) `const {..}: T = this;` destructure REQUIRES the `: T` annotation (un-annotated is a parse
    error), so where `T` clashes, read fields + null `this.shared` to neutralize the Drop instead. (3) **A static
    method on a GENERIC owner reached only through another generic's instantiation is not monomorphized (E0636 at
    codegen)** — `TaskShared<O>::detach` compiled in the lib build but failed in the probe; reverted to a free
    `detach_shared<O>` (the stage-b proven form; `Atomic<u8>::new` works only because its owner arg is concrete).
    (4) Verified empirically (scratchpad probe): **`ptr.drop()` auto-derefs a raw `F*` and runs drop glue even
    when `F` has no `Drop`** — so `task_drop_thunk<F>` needs NO `where F: Drop` (a bound would wrongly reject
    non-Drop futures), matching `thread_body`; executor now uses auto-deref style throughout per Jake.
    (5) Empirically (probe): **`&this` inside a method IS the object's box address** (not a hidden ref-param
    addr), and **a whole-struct move with a non-Copy `Atomic` field (`*p = T::new(...)`) works**. So `finish`
    means every `Task` method is instance/constructor: `Task::new(...)` (constructor, `*task = Task::new(...)`,
    passing the monomorphized thunks as fn-ptr args); `drive`/`finish`/`free` are `mut &this` — `drive` hands
    `&this as u8*` to the waker + re-enqueues `&this`, `free` self-deallocs `&this` (sound: `Task` has no `Drop`,
    so freeing the borrowed receiver synthesizes no double-drop). Behaviorally proven by the self-wake path
    (30/30). No static+raw-pointer `Task` methods remain.
  - **Validation (Linux, pinned `bin/cryo` + `CRYO_STDLIB`):** `make stdlib` green (148 modules) under default
    abort; regression probe (block_on + spawn/join + self-wake + abort→Cancelled) exit 103 **30/30**; isolation
    probe under `--panic=unwind` (3 compute siblings + 1 panicker) exit 61 **30/30** — the panicker's `join`
    returns `Err(Panicked)`, siblings complete, process does not abort. Probes in scratchpad (`exec_probes/`).
  - **Repin status:** Phase-1 compiler atom REPINNED both OS (`bin/cryo` f67be7b9…, `bin/cryo.exe` d79abcb9…),
    `verify-pin` OK. Phase-2 stdlib needs NO repin — the compiler links neither `future::executor` (no future IR
    in its build) nor `core::panic_unwind` (no import; the added helper is abort-stripped). UNCOMMITTED (only Jake
    commits). **The committed `b159494e` mislabels stages (a)+(b) as stage (c) — Jake may want to amend the
    message/history.**
  NEXT: **Phase 4** — reactor (epoll/IOCP) + async I/O over `std::net` + timers + `async fn main` + combinators.
  At that point wakers outlive a poll (stored in reactor registrations) → the `Waker` `clone_fn`/`drop_fn` gain
  real refcount bodies + a `Drop` impl, and the per-task run-state gains a waker refcount (see waker.cryo doc).

- _2026-07-24_ — **Phase 4 STARTED — three design forks LOCKED by Jake (one-way doors closed). Building Inc
  4a.** Baseline HEAD `8e5a7694` (Phase 3 stage (c) committed+repinned by Jake), pin verifies OK, tree clean,
  `make stdlib` = 148 modules green. New session on a **Linux codespace** (verified — not Jake's Windows box).
  Reconciled a doc/git discrepancy: the stage-(c) §9 entry says "UNCOMMITTED", but the multi-thread executor +
  `panic_unwind` atom ARE committed in `8e5a7694` (789-line `executor.cryo` rewrite +
  `config_gating.cryo`/`directive_processing.cryo`/`panic_unwind.cryo` + repin) — the note predated the commit.
  - **Recon-confirmed Phase-4 substrate (grep the symbols):** epoll (`sys_epoll_create1`/`_ctl`/`_wait`),
    `eventfd2`, `timerfd_create`/`_settime`, `clock_gettime`+`CLOCK_MONOTONIC` all bound (`sys/syscall.cryo`);
    `Arc<T, A=GlobalAlloc>` is a full thread-safe refcount (`alloc/arc.cryo`: `ArcInner{strong,weak:
    Atomic<u64>}` + `new`/`clone`/`get`/`get_mut` + `Weak`/`upgrade`); `TcpStream` (`net/socket/tcp.cryo`) is
    blocking (`sock_read`/`sock_write`), has `from_fd`/`raw_fd`/`Drop`(closes fd)/`set_read_timeout` but **NO
    `set_nonblocking`** (add `fcntl`+`O_NONBLOCK`; Windows `ioctlsocket`+`FIONBIO` already bound). **IOCP
    bindings ABSENT** — `OVERLAPPED`+`FILE_FLAG_OVERLAPPED`+`ioctlsocket` exist but
    `CreateIoCompletionPort`/`GetQueuedCompletionStatus`/`WSARecv`/`WSASend` do NOT → "both OSes" means writing
    them. `![thread_local]` is compiler-supported but **unused in `stdlib/`** (only `runtime/` panic state uses
    it) → the per-Executor current-reactor handle is the first stdlib use (validate at 4b).
  - **The Phase-4 one-way-door seam (in the committed executor):** `Task::drive` builds
    `Waker::new_nonowning(task_wake, &this as u8*)` (data = raw `Task*`, clone/drop = noop); on a quiet `Pending`
    (`RUNNING→IDLE`) it **reclaims the task as CANCELLED and frees the box** — because Phase 3 has no reactor to
    ever re-wake it. Phase 4 changes exactly this: a task that parked on the reactor must survive that `Pending`
    because a *stored* waker wakes it later. `run_state` (IDLE/SCHEDULED/RUNNING/NOTIFIED) stays as the
    scheduling/concurrent-poll guard; the new refcount governs LIFETIME (orthogonal).
  - **Jake's three locked decisions (via the question tool):**
    1. **Waker lifetime → separate `Arc`-style wrapper** (NOT a refcount field embedded in the `Task` box). Wrap
       the (non-generic, erased) `Task` in an `Arc<Task>`; the waker's `data` carries the raw `ArcInner<Task>*`
       and `clone_fn`/`drop_fn` become real free fns that bump/drop the strong count (last drop → drop `Task` +
       free). Reuses the tested `alloc/arc.cryo` machinery instead of hand-rolling a refcount. **4a design detail
       to resolve:** whether `arc.cryo` exposes `into_raw`/`from_raw` (reconstruct an `Arc` from the raw inner so
       its `Drop` does the decrement) or the free fns manipulate `ArcInner.strong` directly.
    2. **Reactor → dedicated thread, per-`Executor`.** One background thread per `Executor` runs the
       `epoll_wait`/IOCP loop; async I/O + timer futures reach it via a `![thread_local]` **current-reactor
       handle** the worker sets around each poll (so `Context` — a Phase-0 lock — stays unchanged; no ambient
       global runtime, honoring §7-5). Composes with `Executor::drop` (shutdown → join the reactor thread too,
       alongside the worker join it already does).
    3. **Platform → both OSes (epoll + IOCP) from day one** (NOT Linux-first-Windows-stub). ⇒ the reactor
       interface must accommodate BOTH the readiness model (epoll: wake when the fd is *ready*, then the future
       does the syscall) and the completion model (IOCP: the OS does the overlapped op and wakes on *completion*).
       **This is itself the next one-way door (bring to Jake at the 4b boundary):** unify on a readiness
       abstraction with Windows AFD emulation (mio's approach — complex) vs. a thin per-OS reactor interface with
       OS-gated I/O futures (`![config(...)]` free fns) sharing only "register interest → get woken". Recon +
       recommendation owed before coding 4b.
  - **Refined increment breakdown (each: build → validate under `block_on`/inline driver → gate → repin only if
    default-path `.ll` moves):** **4a** Arc waker refcount evolution (OS-independent; pure stdlib, no repin) —
    real `clone_fn`/`drop_fn` + `Arc<Task>` ownership + `Drop`; `RUNNING→IDLE` stops freeing. **4b** the reactor
    (epoll + IOCP behind the chosen interface; `![thread_local]` current-reactor handle; eventfd/IOCP-post kick).
    **4c** async I/O over `std::net` (`set_nonblocking` + read/write/accept/connect futures, owned-handle style
    §7-6). **4d** timers (`sleep`/`timeout` via `timerfd`/IOCP timer). **4e** `async fn main` (compiler lower →
    **selfhost-check + repin**). **4f** combinators (`join!`/`select!`/`try_join!`).
  - **Flagged dependency (bites at 4c, not before):** the idiomatic read-loop `const (s,n) = s.read(buf).await;
    use(s)` keeps an **aggregate (`TcpStream`) live across a suspend** → currently `E0600` (a deferred Phase-2
    tail: aggregate-across-await promotion). 4a/4b + hand-written I/O futures validate fine under `block_on`
    without loops; *looping* async I/O inside an `async fn` needs that promotion first. Separable — schedule when
    real I/O ergonomics are reached; not a 4a/4b blocker.
  NEXT: implement **Inc 4a** — study `arc.cryo` (`into_raw`/`from_raw`?), wrap `Task` in `Arc<Task>`, give the
  `Waker` real `clone_fn`/`drop_fn` (bump/drop the strong count) + a `Drop` impl, make `RUNNING→IDLE` release
  the queue's Arc ref instead of finishing-CANCELLED. Validate: a hand-written future that clones its waker,
  returns `Pending`, is woken later from another thread → re-polled to completion; strong count reaches 0
  exactly once (no leak, no double-free), stress ≥25×. Pure stdlib → `make stdlib` + `verify-pin`, no repin.

- _2026-07-24_ — **Inc 4a DONE + validated (both OSes). COMPILER fix (owner-default backfill) → REPINNED both
  OS. UNCOMMITTED.** Baseline HEAD `8e5a7694`, pin now updated (new SHAs `188bc76e…` / `44fd1963…`),
  `verify-pin` OK. The Arc-refcounted waker (the Phase-4 one-way door) is in and stress-proven. Files: NEW raw API
  in `alloc/arc.cryo`; `future/waker.cryo` (Copy→non-Copy); `future/executor.cryo` (`Arc<Task>` lifetime);
  `sema/call_resolver.cryo` (the compiler fix).
  - **The compiler fix (root cause, not a workaround — Jake's call).** The recurring "static on a generic owner
    reached through another generic's instantiation → E0636" (the same class that forced `detach_shared<O>` in
    Phase 3) was, for this case, an **owner-arg inference gap**, NOT a mono-registration gap. `Arc::try_new`
    (`static try_new(v: T) -> Result<Arc<T, GlobalAlloc>, AllocError>`) failed because owner param **`A` is
    hardcoded `GlobalAlloc` in the signature** (appears in NO parameter and NO free return position) AND the
    owner is **nested** in the expected type (`Result<Arc<Thing>, …>`), so `compute_static_owner_bindings`
    step 2's base-match misses it → `A` never binds → empty stash → mono never emits the spec → codegen E0636.
    `Arc::new` escaped ONLY because its expected type IS the owner (`Arc<Thing>`, step 2 base-matches). **Bisected
    with a minimal repro** (`use_new` OK vs `use_try_new` E0636, same concrete owner) — the trigger is the
    return SHAPE (owner-is-return vs owner-nested-in-return), NOT the generic-caller context (both non-generic and
    generic callers failed identically). **Fix (`call_resolver.cryo`, +74):** a new `infer_static_owner_prefix`
    (binds the leading owner params the args + expected-return-unify determine, returns the contiguous concrete
    PREFIX) + a step **2b** in `compute_static_owner_bindings` that backfills the trailing DEFAULTED params from
    their declared defaults via the existing `expand_default_type_args` (the same primitive the turbofish path
    already used). Runs AFTER step 2 so a non-default owner arg from a base-matching expected type still wins;
    fires only when every prior source left a defaulted param unbound → **inert for all existing code**. Fixes the
    whole `Owner::try_new`-style family, not just async. **Gate:** `make selfhost-check` → exit 0, BOTH fixed
    points (Linux s3==s4; Windows s3==s4 byte-identical, 235 modules); **win-s2 vs win-s3 = 0/235 differing
    `.ll`** → the compiler produces identical IR for all existing code. Compiler source changed ⇒ **REPINNED both
    OS** (`make pin`, `verify-pin` OK). NOTE for the record: the earlier turbofish `Arc<Task,GlobalAlloc>::try_new`
    is a SEPARATE still-open gap (E0200 in a generic body — the static-on-generic-owner turbofish cluster from §1);
    the fix here makes the NON-turbofish inference path work, which is all Cryo idiom needs. And a THIRD, unrelated
    gap surfaced in bisection: `Arc::try_new(x)` with NO expected type doesn't infer owner `T` from the arg (E0200)
    — also left open (async always has an expected type).
  - **The 4a stdlib rewrite (Arc<Task> lifetime).** `arc.cryo`: `into_raw`/`from_raw`/`increment_strong_count`/
    `decrement_strong_count` (standard raw-Arc API; the manual-vtable waker needs to bump/drop the count from a
    `void*`). `waker.cryo`: `Waker` gains a `Drop` (runs `drop_fn(data)`) → **Copy→non-Copy**; `Context::waker()`
    now returns `.clone()` (a plain field copy would alias one `clone_fn` under two `drop_fn`s). `executor.cryo`:
    `Task` gains `self_arc: ArcInner<Task, GlobalAlloc>*` (back-pointer, so `task_incref`/`task_decref` reach the
    count from any `Task*` — no container-of). `spawn_on` boxes the `Task` in `Arc::new` (count 1 = the scheduling
    ref, handed to the queue by `into_raw`). The owning poll `Waker` = `{task_wake, waker_arc_clone,
    waker_arc_drop, &this}`; `drive` bumps a ref for `cx` (balanced when `cx` drops at method end). `task_wake`
    bumps on `IDLE→SCHEDULED` (the queue's scheduling ref, distinct from the waker's own). **The finish-vs-park
    decision is now IMPLICIT:** on a quiet `Pending` the worker just `task_decref`s the scheduling ref — if a
    stored waker holds a ref the task PARKS (survives), else the last decref runs **`Task::drop`**, which finishes
    an unfinished task CANCELLED (drops its future, handshake) before the `Arc` frees the box. No `strong_count`
    inspection, no race. `finish` nulls `fut` so `Task::drop` knows finishing already ran. `Task::free()` deleted
    (the `Arc` frees). `wake`/`wake_by_ref` stay by-ref (no signature change) — the waker always keeps its own
    ref, released by its `Drop`.
  - **Refcount protocol (durable):** scheduling ref (spawn `Arc::new` → queue → worker, one ref that persists
    until finish/park) + one ref per stored waker (`clone_fn`+1 / `drop_fn`−1) + a transient `cx`-waker ref per
    poll (bumped in `drive`, released at `cx` scope-exit). Self-wake = `RUNNING→NOTIFIED`, no ref change, `drive`
    reschedules (transfers the scheduling ref back). Park+foreign-wake = the waker's stored ref keeps the box
    alive; the wake `IDLE→SCHEDULED` bumps a fresh scheduling ref. This is the textbook `async-task` model.
  - **Validation:** minimal E0636 repro → EXIT 0; full 4a probe (`scratchpad/exec4a`) — block_on / spawn+join /
    self-wake (`WakeCounter`) / **stored-waker park → foreign-thread wake → complete** (`Parker` + main-as-reactor)
    / abort→`Cancelled` / `Parker` dropped exactly once — **625 rounds across 25 process runs + 250 more via the
    repinned `bin/cryo`, zero failures** (no race/leak/double-free). Also proven: a `Pending`-without-a-waker
    future now correctly parks-then-CANCELS on the Executor (it can never be woken) — so Executor probes must
    self-wake or store the waker (`block_on`'s free driver still re-polls unconditionally, so `PendingThenReady`
    works there). `make stdlib` = 148 green.
  - **Durable compiler-fix landmine:** `compute_static_owner_bindings` sources were all-or-nothing; a defaulted
    owner param bound by NO inference source (not in params, not free in the return, owner nested in the expected
    type) needs the default-backfill (step 2b). Any future `Owner<..., A = Default>::ctor -> Wrapper<Owner<...>>`
    depends on it.
  NEXT: **Inc 4b — the reactor.** The three forks are locked, but "both OSes from day one" opens the **next
  one-way door: the reactor interface must span readiness (epoll: wake when the fd is ready, then syscall) AND
  completion (IOCP: OS does the overlapped op, wakes on completion).** Bring Jake the unification fork —
  (a) readiness abstraction + Windows AFD emulation (mio's way; complex) vs. (b) thin per-OS reactor interface
  with `![config(...)]`-gated I/O futures sharing only "register interest → get woken" — with a recon-grounded
  recommendation BEFORE coding. Also: write the IOCP bindings (`CreateIoCompletionPort`/
  `GetQueuedCompletionStatus`/`WSARecv`/`WSASend`, absent today) and the per-Executor `![thread_local]`
  current-reactor handle (first stdlib use of the directive). `set_nonblocking` (fcntl/ioctlsocket) for 4c.
