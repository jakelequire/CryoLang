# HANDOFF — Cryo `async`/`await` (Track 3), Phase 2 in progress

You are picking up Cryo's stackless, poll-driven `async`/`await` implementation. Phases 0 (design) and 1
(stdlib core types) are done; Phase 2 (compiler lowering) is underway — `async fn` **parsing** and the
**no-await lowering (Inc 1b)** are done and validated. **Next is Inc 2: the first real `await`.**

---

## 0. START HERE

1. **Read `ASYNC_IMPL.md` (repo root) top-to-bottom.** It is the single source of truth: the locked design,
   the phase plan, **§10 = the detailed state-machine lowering design**, the build/gate procedure, the
   landmines, and the **Progress Log** (append-only) whose latest entries carry durable, hard-won
   implementation notes you MUST read before touching the lowering.
2. **Keep `ASYNC_IMPL.md` current** every session — update its Status Dashboard and append to its Progress
   Log. It is both the plan and the cross-session memory.
3. There is also file-based memory at `~/.claude/projects/.../memory/` (index `MEMORY.md`); the entry
   `async-track3-phase0-lock-2026-07-22.md` summarizes this effort.

## 1. Verify the baseline at session start

```
git log --oneline -6          # confirm HEAD
git status --short            # expect clean (Inc 1b was committed with this handoff)
python scripts/verify-pin.py  # expect: verify-pin: OK
```
Branch `ll-impl`. As of writing, Inc 1b + this handoff are being committed by Jake. **Inc 1b was a
NO-REPIN change** (0 `.ll` diff — the lowering is inert for async-free code); confirm the pin verifies.

## 2. The non-negotiables (Jake's standing rules — mirror exactly)

- **Only Jake commits.** NEVER `git commit`, never co-author. You MAY `make pin` at a clean green boundary
  and MUST leave the tree ready for Jake. Repin with **plain `make pin`** (writes both `bin/cryo` ELF +
  `bin/cryo.exe` PE), **NEVER `CRYO_CC=gcc make pin`** (landmine); verify with `python scripts/verify-pin.py`.
  **Repin ONLY when a change moves the default-path (`--panic=abort`) selfhost IR** — check the `.ll` diff
  (§4); the async lowering is inert for async-free code, so it has needed no repin so far.
- **Real solutions, not workarounds.** Fix root causes in the shared layer; if a correct change breaks the
  build, fix the build. Resist special-casing.
- **Comments describe logic** (invariant + failure mode prevented), never project narrative — no
  dated/audit/phase/batch labels in code. `ASYNC_IMPL.md` is the exception (it IS the narrative).
- **Never run two heavy builds at once** (`make cryo`/`test`/`selfhost`/`pin`, or a background build) →
  environmental **exit -15 (SIGTERM)** mid-compile. Serial only. **Never blind `git stash pop`.**
- Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
  over type-suffixed names; avoid suffixed numeric literals; pass owning aggregates BY POINTER.

## 3. What exists now (the async substrate you build on)

- **`stdlib/future/`** (committed) — `Poll<T>`, `Future` trait (`type Output`; `poll(mut &this, cx: Context*)
  -> Poll<This::Output>`), `Waker`, `Context`, `block_on<F,R>(fut) -> R where F: Future<R>`, and sample
  futures `Ready<T>` / `PendingThenReady<T>`. Module is `future` (NOT `async` — `async` is a keyword);
  `import std::future::{poll,waker,traits,ready};`.
- **`async fn` parsing** (committed) — `is_async` on `FunctionDeclNode`; `async function f() -> T { … }`
  parses and carries the flag.
- **No-await lowering (Inc 1b)** (committed) — `compiler/src/compiler/sema/async_lower.cryo` (`AsyncLower`).
  Lowers a **non-generic, no-await** `async fn` into a synthesized state-machine struct (`state: u32` +
  one field per param) implementing `Future<Output=T>`, moving the body into `poll` (`return E` →
  `return Poll::Ready(E)`, recursing through all control-flow containers) and rewriting the fn into a
  constructor returning the struct. Hooked in `sema/sema.cryo:261 visit(FunctionDeclNode*)`, gated
  `is_async && !is_generic`, AFTER the body is typed. Registers the `Future` impl directly (two keys — see
  the Progress Log). Callers `block_on(f(...))` with NO turbofish (F/R inferred).
- **Pinning story: forbid references live across an `await`** (design-locked; enforced in the lowering's
  live-across-await computation once awaits land). Cryo has no `Pin` and cannot soundly have one (no
  lifetimes / no enforcing `unsafe`); the move-checker is the only real enforcement. See `ASYNC_IMPL.md` §4.
- The **panic seam** async plugs into (Tracks 1 & 2, committed): drop-completeness on unwind + thread-local
  panic state → poll-boundary `catch_unwind` on worker threads is sound (Phase 3). `--panic=unwind` is
  Linux/hosted only; async must NEVER require unwind (works under `--panic=abort`, just no task isolation).

## 4. YOUR NEXT TASK — Inc 2: one `await`, straight-line

Full design is `ASYNC_IMPL.md` §10 ("await desugar / the state machine"). In short, extend `AsyncLower` so a
straight-line body with a single `await e` (where `e: Future<A>`) lowers to a resumable `poll`:
- `poll` opens with `switch (this.state) { 0 => <start>; k => <resume at await k>; … }`.
- Each `await e` at state index k: stash `e` into a new struct field `fut_k`, then
  `loop { match (this.fut_k.poll(cx)) { Poll::Ready(v) => { <bind v>; break; } Poll::Pending => { this.state
  = k; return Poll::Pending; } } }`.
- Locals live across the await become `this.` fields (write before the `return Pending`, read on resume).
  For a single straight-line await, promotion may be minimal — build up from there (Inc 3 generalizes).
- **Kill the codegen hard-error** for `await`: `codegen/visit/ir_generator.cryo` ~1944 ("await expressions
  are not implemented") — once `AwaitExprNode` is rewritten during the lowering it becomes dead / a defensive
  ICE. `AwaitExprNode` is in `AST/expression.cryo` (~848); sema visits it as a plain expression (~1130).
- **Validate** with a scratchpad probe: an `async fn` that `await`s a stdlib `PendingThenReady<A>` (which
  pends N times then completes), driven through `block_on`; assert the result via exit code.

**Inc 3** = several straight-line awaits (real cross-await local promotion). **Inc 4 = awaits across
branches/loops — a ONE-WAY DOOR that needs Jake to pick the approach (full CFG-to-state-machine flattening
vs. a staged version that hard-errors await-in-loop first). Bring §10's Inc-4 options to Jake BEFORE coding
it.** Everything up to Inc 4 does not need sign-off.

## 5. Build / gate / validate (run `make` from PowerShell, NOT the Bash tool = Git Bash)

- **Build compiler:** `CRYO_CC=gcc make cryo` (~1.5 min). Produces `compiler/build/cryo.exe`. `make test`
  does NOT rebuild the compiler — `make cryo` first.
- **Run a probe:** create a scratchpad project (cryoconfig + `src/main.cryo`); build+run with the freshly-built
  compiler. **A NEW compiler at `compiler/build/cryo.exe` needs `CRYO_STDLIB=<repo>/stdlib` in the env** —
  binary-relative resolution and cryoconfig `stdlib_root` (absolute path) both MISS it. Pattern:
  ```
  $env:CRYO_CC='gcc'; $env:CRYO_STDLIB='C:\Programming\apps\CryoLang\stdlib'
  Set-Location <probe-dir>; & 'C:\Programming\apps\CryoLang\compiler\build\cryo.exe' build
  .\build\<name>.exe; $LASTEXITCODE   # observed value = exit code (native exit doesn't flush stdio)
  ```
  A working Inc-1b probe lives at the scratchpad `async_inc1b/` for reference (imports + cryoconfig shape).
- **Selfhost gate (before declaring green / repin):** `CRYO_CC=gcc make selfhost-check` → exit 0 + **TWO
  `FIXED POINT OK`** (Linux target-IR + Windows native-PE). Then **repin test** =
  `diff -rq compiler/build/self/win-s2 compiler/build/self/win-s3` filtered to `*.ll`: **0 differing `.ll` =
  no repin**; non-zero = repin. (Only the `.exe` binaries differing is expected — non-deterministic PE
  linking.) `make selfhost-check` clobbers `compiler/build/cryo.exe` to a Linux ELF → run `make cryo` again
  before the next Windows probe. Tee'd log is UTF-16 → use PowerShell `Select-String`.
- The async lowering has been **inert for async-free code** every increment so far → 0 `.ll` diff → no repin.
  Expect the same until the compiler/stdlib themselves use `async fn` (they don't).

## 6. Landmines & durable gotchas (also see `ASYNC_IMPL.md` §6 + Progress Log)

- **Stale-overload trap:** rewriting a decl's return type mid-sema leaves the OLD signature registered; the
  free-call path reads `lookup_func_type_overloads` (NOT `func_returns`). Fix with
  `decl_index.reset_function_overloads(name)` for BOTH bare + qualified names before re-registering. (Already
  handled in Inc 1b; relevant if you rewrite signatures again.)
- **Trait-impl registration is DIRECT, not via the funnels** (they conflict on `target_type` qualification):
  `register_trait_impl(Future_leaf, Q, impl)` (`.poll()` dispatch) + `register_trait_impl_typed(Future_leaf,
  registry_name_of(Q), named_qualified_id(Q), impl)` (`F: Future<R>` bound). A synthesized-during-sema impl
  must SELF-register (no later pass registers root-injected impls).
- **`Poll::Ready(E)` is hand-typed** (`ScopeResolutionNode(Poll,Ready)` + `CallExprNode`, both
  `resolved_type = Poll<Output>`) — the moved body is already typed, so no re-resolution pass is needed.
- **Cryo authoring:** NO `let` (use `const`/`mut`); **enums cannot have inline methods** (use a separate
  `implement enum X { … }` block; structs CAN); associated type bound in impls via positional sugar
  (`implement<T> trait Future<T> for struct S`); pass owning aggregates BY POINTER.
- **Incremental-cache staleness:** if a compiler-source edit seems ignored, clear
  `compiler/build/target/release/host*/local/incremental` + `rm compiler/build/cryo*`, then `make cryo`.

## 7. Key files (grep the symbol — line numbers drift)

- `sema/async_lower.cryo` — `AsyncLower` (the lowering). `sema/sema.cryo` — field/wire/`visit(FunctionDeclNode*)`
  hook. `sema/_module.cryo` — `public module AsyncLower`. `decl_index.cryo` — `reset_function_overloads`.
- `AST/expression.cryo` — `AwaitExprNode` (~848). `codegen/visit/ir_generator.cryo` — the `await` hard-error
  (~1944, to kill in Inc 2). `sema/lambda_synth.cryo:239-423` — the synthesis precedent `AsyncLower` mirrors.
- `stdlib/future/` — the core types + `block_on`. `stdlib/thread/`, `stdlib/sync/`, `stdlib/net/`,
  `stdlib/sys/syscall.cryo` (epoll) — substrate for Phase 3 (executor) / Phase 4 (reactor).
- Pipeline: `compiler/src/compiler/instance.cryo compile_project_with_ctx` — real pass order (mono-after-sema;
  the lowering rides `FunctionBodyTypeCheck` → mono → move-check → drop-insertion → codegen).

**First action:** verify the baseline (§1), read `ASYNC_IMPL.md` (esp. §10 + the Progress Log tail), then
implement Inc 2. Do NOT commit; do NOT `CRYO_CC=gcc make pin`. Leave the tree green for Jake.
