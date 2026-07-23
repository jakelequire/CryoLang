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
| 2 | Compiler: `async fn` parse + state-machine lowering + `await` desugar | ◐ in progress — parse + no-await (Inc 1b) + single await (Inc 2) + **N straight-line awaits + cross-await local promotion (Inc 3) DONE+validated**; next = Inc 4 (branches/loops — **needs Jake sign-off**) |
| 3 | Executor + `Waker` + `spawn`/`JoinHandle`; multi-thread; poll-boundary `catch_unwind` isolation | ☐ not started |
| 4 | Reactor (epoll/IOCP) + async I/O over `std::net` + timers + `async fn main` + combinators | ☐ not started |

Legend: ☐ not started · ◐ in progress · ✅ done+validated · ⏸ blocked (note why in the Progress Log).

**Current HEAD baseline:** `b1414145` (`feat: Canonicalizes a Mod::Leaf name …` — the qualified re-export
resolver fix, committed+repinned by Jake, on top of `3cedca8d` Inc-2 repin). Branch `ll-impl`. Pin verifies
OK. Inc 3 is **DONE+validated, UNCOMMITTED** (one modified file: `sema/async_lower.cryo`; no repin).
Tracks 1 (drop-completeness on unwind) and 2 (thread-local panic state) are **done + committed + repinned**
— they are the prerequisites async plugs into (see §3).

**⚠ KNOWN BUG (pre-existing, NOT async-lowering, blocks the `block_on` driver):** `block_on(fut)` — and any
generic free fn `f<F, R>(fut: F) -> R where F: Future<R>` — fails **codegen `E0636`** when called WITHOUT a
turbofish, because `R` appears only in the return type + the where-bound (never in a parameter) and is not
inferred from `F::Output`, so mono emits no specialization. **Turbofish works:** `block_on<ConcreteF,
ConcreteR>(…)`. Reproduces on the committed pinned `bin/cryo` with a pure-stdlib `block_on(Ready<i32>::new(
42))` (zero async) → independent of async. It is in the generic-inference layer, not `async_lower`. Since
`block_on` on an anonymous async future can't be turbofished (the future type is unnameable), **async
validation currently drives futures with an inline `loop { match f.poll(&cx) }` instead of `block_on`.** The
Inc-1b/2 Progress-Log "no turbofish" claim no longer holds at HEAD. Fix is bound-directed inference (project
the where-bound type arg from the receiver's assoc type) — a separate subsystem; left for Jake to triage.

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
