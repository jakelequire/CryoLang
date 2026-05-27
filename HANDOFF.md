# Threading + Sync Stdlib Work — Handoff

This file hands off an in-progress multi-step implementation of `std::sync`
+ `std::thread` to a fresh agent. The plan, design rationale, and full step
breakdown live at `~/.claude/plans/lexical-brewing-shell.md`. Project context
(audit results, conventions) lives in
`~/.claude/projects/-home-kiji-Programming-apps-CryoLang/memory/`.

## Status (as of handoff)

| # | Step | State |
|---|---|---|
| 0 | Wire existing `atomic_fence` intrinsic | done, selfhost byte-identical |
| 1 | LLVM atomic C-API bindings + LBuilder wrappers | done, selfhost byte-identical |
| 2 | 27 per-type atomic intrinsics (load/store/RMW/cmpxchg) | done, selfhost byte-identical |
| 3 | `std::sync::atomic` stdlib module | done, smoke + tests pass |
| 4 | Send/Sync auto-derive + call-site enforcement | done, selfhost byte-identical |
| 5 | `Arc<T, A>` in `stdlib/alloc/arc.cryo` | done, selfhost byte-identical |
| 6 | Mutex / RwLock / CondVar / Once / Barrier | done, selfhost byte-identical |
| 7 | `ThreadLocal<T>` via `pthread_key` | done, tests pass (selfhost not run — stdlib-only) |
| **8** | **`move` keyword + closure Drop synthesis** | **in progress, no edits yet** |
| 9 | `thread::spawn` + `JoinHandle` + `Builder` | pending |
| 10 | Tests, docs, example, CHANGELOG | pending |

The committed `bin/cryo` pin was refreshed twice during this work (after
Step 4, after Step 6). It currently reflects the compiler with atomic
intrinsics + Send/Sync enforcement landed; Step 8 will require another
pin refresh.

User design choices already locked in (from
`AskUserQuestion` interactions during planning):
- **Atomics**: proper LLVM atomic intrinsics in the compiler.
- **Spawn ergonomics**: full `move` keyword carve-out, not function-pointer fallback.
- **Channels (mpsc)**: deferred to a follow-up after Step 10.

OnceCell + Lazy were deferred from Step 6 by my call (atomic state-machine
+ heap-boxed value is meaningfully more complex than the other primitives,
and `Once` + a mutable global covers the common lazy-init pattern). Note
this in the CHANGELOG when Step 10 lands.

## Current task — Step 8 (move keyword + closure Drop synthesis)

The plan's Step 8 section + the Plan agent review in
`~/.claude/plans/lexical-brewing-shell.md` are the authoritative spec.
The work is five files, in order:

### 8.1 — Lexer (`compiler/src/compiler/lex/lexer.cryo`)
Register `move` as a recognized keyword token. There may already be a
`Move` variant in `TokenKind` (it's listed as reserved in `docs/cryo.md`
§ 1.2); if not, append a new variant.

Verify with a grep first:
```bash
grep -n '"move"\|TokenKind::Move\|MOVE\b' \
  /home/kiji/Programming/apps/CryoLang/compiler/src/compiler/lex/lexer.cryo \
  /home/kiji/Programming/apps/CryoLang/compiler/src/compiler/lex/_module.cryo
```

### 8.2 — Parser (`compiler/src/compiler/parser/expr_parser.cryo`)
Lambda parsing entry points found during scoping:
- Line 791 — `parse_lambda_expression(start: Token) -> ExpressionNode*`
- Line 818 — `parse_lambda_after_params(params, start) -> ExpressionNode*`
- Line 745, 754 — call sites

When the current token is `move`, consume it, then parse the lambda
normally, then set `lambda.is_move = true` on the resulting node.
`move` may appear in primary-expression position; arrange the dispatch
so a bare `move` token is unambiguously a lambda prefix (not its own
expression).

### 8.3 — AST (`compiler/src/compiler/AST/expression.cryo`)
`LambdaExprNode` definition starts around line 471. Add:
```cryo
is_move: boolean;
```
Default to `false` in `new`/`new_with_*` constructors. Already-present
fields to mirror style: `captured_names`, `captured_types`.

### 8.4 — Sema move-aware capture (`compiler/src/compiler/passes/sema.cryo`)
Key locations (line numbers from current source):
- L3032: `record_lambda_capture(mut &this, ident, outer_ty)` — currently
  rejects non-Copy with `E0457_NON_COPY_CAPTURE`. Wrap the rejection
  in `if (this.current_lambda == null || !this.current_lambda.is_move)`.
- L3060: `this.current_lambda.add_capture(...)` — when the lambda is
  move AND the capture is non-Copy, this is a move site for the outer
  binding. Look at how `MoveCheck` records move sites (probably via
  `OwnershipQuery` and a sema-side moved-set the move-check pass
  consumes) and emit the appropriate move record.
- L2856: `resolve_lambda(mut &this, node: LambdaExprNode*) -> TypeRef`
  — the entry that sets `current_lambda`. Make sure this respects
  `node.is_move` when comparing capture rules.

### 8.5 — Sema closure-struct Drop synthesis (`compiler/src/compiler/passes/sema.cryo`)
`synthesize_closure_struct` starts at L3099. After the `__call__`
method is built (currently around L3140 onwards), check whether any
captured field is non-Copy via `OwnershipQuery::is_copy`. If so,
synthesise a `Drop` method whose body invokes `.drop()` on each
non-Copy field:
- Build a `FunctionDeclNode` named `drop`, no params, receiver
  `mut &this`, return `void`.
- Body: a `BlockStmtNode` containing one `ExpressionStatementNode`
  per non-Copy field, each calling `this.<field>.drop()`.
- Attach to the struct's MethodNode list before
  `register_methods_with_module` runs.
- Once attached, `OwnershipQuery::has_inherent_drop` will mark the
  closure struct as non-Copy, drop-insertion picks it up at scope
  exit automatically — no separate drop-insertion change needed.

### Validation cadence for Step 8

After each sub-step (8.1 → 8.5):
```bash
make cryo                 # incremental: build with current pin
# write a tiny smoke test that triggers the new path
```

Once 8.1–8.5 build:
```bash
make pin-cryo             # refresh bin/cryo
make cryo                 # rebuild stdlib+compiler with the new pin
make test                 # 737 tests + 64 negative tests
make selfhost-check       # 3-round byte-identity gate (~90s)
```

A specific smoke test for Step 8: a Cryo file that uses
`move (n: i32) -> i32 { return n + bias; }` where `bias` is an
i32 local (Copy, passes today) and a separate test where the captured
value is a non-Copy type like `Box<i32>`. Negative test: the same
non-Copy capture WITHOUT `move` should still produce E0457.

## After Step 8

### Step 9 — `std::thread`

Files to create under `stdlib/thread/`:
- `error.cryo` — `ThreadError`, `ThreadErrorKind` (mirror
  `stdlib/sync/error.cryo`'s shape; can reuse `SyncError` or keep
  distinct — plan recommends merging).
- `thread.cryo` — top-level `spawn<F, T>(f: F) -> JoinHandle<T>` where
  `F: () -> T + Send, T: Send`; `current_id()`, `sleep(secs, nanos)`,
  `yield_now()` free functions.
- `builder.cryo` — `Builder { stack_size, name }` builder pattern;
  `spawn` consuming the closure.
- `join_handle.cryo` — `JoinHandle<T> { tid: u64, finished: boolean }`;
  `join() -> Result<T, ThreadError>` uses `pthread_join`'s `void*`
  return path (the trampoline `malloc`s a `T`, writes the result,
  returns the box pointer; `join` reads it and frees). `Drop` calls
  `pthread_detach` if `!finished`.

The plan's "Step 9" section in the plan file has the trampoline pseudocode.
Critical detail: the trampoline must be a per-(F, T) monomorphized
`extern "C"` function. Cryo's existing closure-to-fn-pointer path produces
one for non-capturing lambdas; for capturing closures (after Step 8) the
trampoline is a thin generic wrapper that calls `f.__call__()`.

Update `stdlib/thread/_module.cryo` to add the new modules.
Update `stdlib/lib.cryo`'s `thread` section.

### Step 10 — Tests, docs, example, link config

Tests under `tests/tests/stdlib/`:
- `sync_atomic.cryo` — load/store/fetch_add/cmpxchg correctness per
  AtomicXxx + per-`MemoryOrder` smoke.
- `sync_mutex.cryo` — lock/unlock; try_lock; nested via
  `PTHREAD_MUTEX_RECURSIVE` (Mutex::new uses NORMAL — add a
  constructor for the recursive case if exposed).
- `sync_rwlock.cryo` — multi-reader, single-writer.
- `sync_condvar.cryo` — producer/consumer one-shot.
- `sync_once.cryo` — init runs exactly once.
- `sync_barrier.cryo` — N threads, leader election.
- `sync_arc.cryo` — refcount; cross-thread clone+drop.
- `thread_spawn.cryo` — spawn + join; spawn + detach via drop;
  `Builder::stack_size`.
- `thread_local.cryo` — already validated single-threaded; add a
  multi-thread test.

Negative tests under `tests/tests/negative/`:
- `negative_send_on_rc.cryo` — `thread::spawn` capturing `Rc<T>` must
  fail with the trait-bound error. (Already verified manually during
  Step 4; codify here.)
- `negative_move_required.cryo` — non-`move` lambda capturing a
  non-Copy must still fail with E0457.

Link config: `stdlib/cryoconfig` currently has no `link_libs`. On
glibc 2.34+ pthread is folded into libc so the link works as-is; on
older glibc / non-glibc systems add `link_libs = ["pthread"]`. Probably
add it defensively now.

Docs:
- New `§ 23 Threading and Synchronization` section in `docs/cryo.md`
  covering: `spawn`, `move` keyword, atomics, `MemoryOrder`, Send/Sync
  rules (auto-derive + deny-list of `Rc`/MutexGuard/RwLock*Guard),
  Mutex/RwLock/CondVar discipline, ThreadLocal cleanup limitation.
- Update `docs/grammar.md`: add optional `move` to the `Lambda`
  production.
- `CHANGELOG.md`: new entry (probably unreleased `1.1.0`) listing the
  surface. Mention OnceCell + Lazy + mpsc channels still pending.
- `README.md` "Beyond 1.0" list: remove `Threading, atomics, channels`;
  mention channels still pending.

Example:
- `examples/14-parallel-sum/` — a parallel sum-of-squares over an
  `Arc<Mutex<Array<i64>>>` using N worker threads. Plan calls this
  out explicitly.

## What was actually edited / created (Steps 0–7)

### Compiler files modified
- `compiler/llvm_bindings.h` — added `LLVMBuildAtomicRMW`,
  `LLVMBuildAtomicCmpXchg`, `LLVMSetOrdering`, `LLVMSetAlignment`,
  `LLVMConstIntGetZExtValue` declarations; added "Builder — Atomic
  Operations" section header.
- `compiler/src/compiler/codegen/llvm_types.cryo` — added LBuilder
  methods `build_fence`, `build_atomic_load`, `build_atomic_store`,
  `build_atomic_rmw`, `build_atomic_cmpxchg`. `build_extract_value`
  was already present.
- `compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo` —
  appended 27 `IntrinsicKind` variants (AtomicLoadU8/U32/U64,
  AtomicStoreU8/U32/U64, AtomicAdd/Sub/And/Or/Xor/Swap × U8/U32/U64,
  AtomicCmpxchgU8/U32/U64) + matching lookup entries.
- `compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo` — added
  helpers `extract_ordering_arg`, `emit_atomic_load`,
  `emit_atomic_store`, `emit_atomic_rmw`, `emit_atomic_cmpxchg`;
  added arity-table entries and emission match arms for the 27 new
  intrinsics. Ordering int is extracted at IR-construction time via
  `LLVMConstIntGetZExtValue` — atomic ops require the ordering to be
  a compile-time constant.
- `compiler/src/compiler/types/ownership.cryo` — added `is_send`,
  `is_sync`, shared worker `is_thread_safe_at_depth`, helpers
  `all_thread_safe`, `all_fields_thread_safe`,
  `all_variants_thread_safe`, deny-list `is_thread_safety_denied`
  (Rc + MutexGuard + RwLockReadGuard + RwLockWriteGuard).
- `compiler/src/compiler/types/monomorphizer.cryo` — dispatcher in
  `type_implements_trait` (around L1249–L1289) now routes `Send` and
  `Sync` to `OwnershipQuery::is_send` / `is_sync`, mirroring the
  existing Copy/Drop branches.

### Stdlib files created
- `stdlib/sync/_module.cryo`
- `stdlib/sync/atomic.cryo` — `MemoryOrder` enum (NOT `Ordering`, see
  Gotchas), `AtomicU8/U32/U64/I32/I64/Bool` per-type wrappers, `fence`,
  `compiler_fence` free functions.
- `stdlib/sync/error.cryo` — `SyncError` + `SyncErrorKind`.
- `stdlib/sync/mutex.cryo` — `Mutex<T, A>` + `MutexGuard<T, A>` with
  RAII unlock on guard drop. Heap-pinned `MutexInner` mirrors `Rc`.
- `stdlib/sync/rwlock.cryo` — `RwLock<T, A>` + two guard types.
- `stdlib/sync/condvar.cryo` — `CondVar` with `wait(mutex, guard)`,
  `notify_one`, `notify_all`.
- `stdlib/sync/once.cryo` — `Once::call_once(f)` wrapping `pthread_once`.
- `stdlib/sync/barrier.cryo` — `Barrier::new(count)`, `wait()` returning
  `BarrierWaitResult { is_leader }`.
- `stdlib/alloc/arc.cryo` — `Arc<T, A>` mirroring `Rc<T, A>` with
  `AtomicU64` refcount; canonical fetch_sub(Release) + fence(Acquire)
  drop pattern.
- `stdlib/thread/_module.cryo`
- `stdlib/thread/local.cryo` — `ThreadLocal<T>` via `pthread_key`;
  null destructor (per-thread values leak unless `clear()` is called).

### Stdlib files modified
- `stdlib/core/intrinsics.cryo` — added `atomic_fence` declaration +
  27 per-type atomic intrinsic declarations.
- `stdlib/alloc/_module.cryo` — added `public module alloc::arc;`.
- `stdlib/lib.cryo` — module map updated for `sync` and `thread`.

### Pinned compiler
- `bin/cryo` was refreshed via `make pin-cryo` after Step 4 (Send/Sync
  enforcement) and again after Step 6 (deny-list expansion). The
  current pin includes atomic intrinsics + Send/Sync + deny-list.
- `bin/cryo.pin.txt` updated by `scripts/cryo-pin.py` at each refresh.

## Gotchas learned in this session

1. **Don't parallelize `make selfhost-check`** with `make test` or
   `make cryo`. They share `stdlib/.bin/` and `compiler/build/`.
   Parallel writes manifested as a "file truncated" linker error
   that masquerades as a build failure.

2. **`new` is a reserved keyword** — Cryo's heap-alloc expression
   keyword. Can't use as a parameter name even in typed-param
   position when the body references `if (new)`. The `compare_exchange`
   methods in `stdlib/sync/atomic.cryo` use `next` instead.

3. **`Ordering` clashes with `core::cmp::Ordering`** (Less/Equal/Greater).
   The atomic ordering enum is renamed `MemoryOrder` to avoid the
   collision. C++ uses the same naming pattern.

4. **`Layout::size()` not `size_bytes()`** — `stdlib/alloc/layout.cryo`
   exposes `size`, `alignment`, `padded_size`. I tripped on this in
   `stdlib/sync/once.cryo`.

5. **`.drop()` is a move site for the compiler's analysis.** The
   pattern `g.drop(); /* end of scope */` is fine, but
   `if (cond) { ...; return X; } g.drop();` can confuse the analysis
   if drop-insertion synthesizes an extra drop on the early-return
   path. Refactor into helper functions for clean scoping rather
   than relying on explicit `.drop()` calls — let auto-drop handle
   scope exit.

6. **`///!` vs `///`** — Cryo uses `///!` for inner doc comments
   (module-level, on the file's namespace), `///` for outer doc
   comments (on declarations).

7. **Cryo array sizes must be numeric literals**, not named
   constants. `u8[SIZEOF_PTHREAD_MUTEX_T]` won't parse; use `u8[40]`
   directly and comment that it matches `libc::SIZEOF_PTHREAD_MUTEX_T`.

8. **Pin-refresh discipline.** After compiler-modifying steps:
   `make cryo` → manual smoke test → `make pin-cryo`. Don't put
   compiler changes through to user-facing testing without a fresh
   pin — the previous `bin/cryo` doesn't know about new intrinsics,
   so `libcryo.a` built with the old pin has unresolved references
   in any new stdlib module that consumes them. (See: stdlib's atomic.o
   would have undef refs to `atomic_load_u64` etc. without a fresh pin.)

9. **Function-pointer fields in structs**: `init: () -> T` works.
   Cryo non-capturing lambdas convert to function pointers naturally.
   Used in `ThreadLocal::new(init)`.

10. **Send/Sync auto-derive rules** (current state):
    - Primitives, references, function pointers, raw pointers: Send + Sync.
    - Aggregates (Struct/Class/Enum/Tuple/Optional/Array): Send/Sync
      iff every component is, AND not on the deny-list.
    - Deny-list: `Rc`, `MutexGuard`, `RwLockReadGuard`,
      `RwLockWriteGuard` (by leaf name).
    - Raw pointers are intentionally permissive (Box, Arc, HashMap,
      etc. all hide raw pointers internally and need to inherit
      Send/Sync from their generic args). Cryo's "weaker than Rust"
      stance.

11. **CHANGELOG note for OnceCell/Lazy deferral**: Step 6 originally
    listed them; I deferred them because the atomic state-machine +
    heap-boxed value adds meaningful complexity beyond the
    pthread-wrapping pattern shared by the other primitives. `Once`
    + a mutable global is the documented workaround for v1.

## How to continue

1. **Re-read the plan** at
   `~/.claude/plans/lexical-brewing-shell.md` — full design rationale
   and the original Plan-agent review with corrections.
2. **Load memory** — the project memory files at
   `~/.claude/projects/-home-kiji-Programming-apps-CryoLang/memory/`
   are auto-loaded; check `MEMORY.md` for the index. Particularly:
   `project_cryo_compiler.md`, `project_cryo_conventions.md`.
3. **Start Step 8**. Run the grep in § 8.1 to see whether `move` is
   already a lexer token. Proceed through 8.1 → 8.5 with smoke tests
   in between. Validate end-to-end with `make selfhost-check` after
   the pin refresh.
4. **Don't skip the pin refresh** when 8 is done. Step 9 consumes
   the closure-struct Drop synthesis from Step 8, so `libcryo.a`
   needs a compiler that produces it.
5. **Re-read `Gotchas` above** before writing any new Cryo code in
   stdlib — the keyword/syntax surprises bit me more than once.

The plan file lists every critical-file path under each step;
follow it. The user has been working interactively through the
whole sequence and may want to be looped in for design questions
that come up during Step 8 (the move-site emission rule is the
likeliest place to need a clarification).
