# Async internals

How `async`/`await` is implemented, and the decisions the implementation rests on. For the
*language surface* - what you write and what it means - see [section 19 of the language
reference](./cryo.md#19-asynchronous-programming). This document is for people changing the
lowering, the runtime, or anything they touch.

Every claim here describes the compiler as it stands. Where a decision could reasonably have gone
another way, the reason it did not is recorded, because those are the parts that get re-litigated.

## 1. The model

**Stackless, poll-driven state machines.** An `async function` is compiled into a generated struct
implementing `Future`; its body is split at every suspension point, values that must survive a
suspension become fields, and calling the function builds the struct instead of running the body.
There is no per-coroutine stack, no stack-switching trampoline, and no green threads. Threads stay
OS threads.

Three consequences fall out of that choice, and they are why it was made:

- **The unwinder is unchanged.** A panic inside an `async function` unwinds the native frame of
  whoever called `poll`. There is no coroutine stack to walk, so async surfaces panics through the
  existing `__cryo_panic` funnel.
- **No new thread-local machinery.** Task-local state is ordinary struct fields in the generated
  future.
- **`async` is a compile-time transformation.** The executor is an ordinary library
  (`stdlib/future/`), not part of the language.

## 2. Why there is no `Pin`

Rust needs `Pin` because a future may hold a pointer into its own fields, and moving such a future
dangles that pointer. Cryo forbids the pattern instead of managing it: **a reference may not be held
live across a suspension** (`E0455`). No self-references means nothing to pin, which means futures
are ordinary movable structs and no address-stability contract exists anywhere in the system.

This was not a preference. A `Pin<T>` that actually enforced anything would need lifetimes, a borrow
checker, and an `unsafe` with teeth - Cryo deliberately has none of the three, so a `Pin` written
here would enforce nothing. The move checker is the language's one *enforced* ownership mechanism, so
routing the rule through it is the only soundly-enforced option available.

The cost is real and bounded: an `async function` taking `x: &T` cannot use `x` after an await, and
no `&mut` into a field may cross one. The owned-value rewrite is always available and is the idiom
the standard library follows - see §5.

This is a strict subset of Rust's rules. Adding lifetimes and a real `Pin` later would relax it
without breaking existing code.

## 3. What the lowering guarantees

- **Carried values live in place.** A value held across a suspension is built directly into its
  `Option<T>` field and stays there for the life of the future; it is reached through a pointer
  rather than moved in and out per state. A state that hands the value away takes it
  (`take().unwrap()`); a state that rebuilds it republishes the field.
- **A `&this` / `mut &this` receiver is re-addressed before every poll.** An async method stores the
  receiver's address in a `this$recv` field, and an owning receiver promoted across states is taken
  into a fresh block-local on each poll - so the address legitimately changes. The frame re-writes
  `this$recv` immediately before each poll rather than trusting the value written at construction.

  The alternative was making the method sugar consuming, or adopting an address-stability contract.
  Both were rejected: consuming changes the surface for every caller, and an address-stability
  contract is exactly what §2 buys the freedom *not* to have. Two shapes cannot be re-addressed and
  are rejected rather than silently corrupted - a receiver that names no storage (a temporary or call
  result), and awaiting a method future the awaited expression did not itself produce.
- **Drop timing is preserved explicitly.** A carried value is released before the future completes
  (`release_before_ready`) rather than at future-drop. Deferring a carried socket's close to
  future-drop deadlocks real code.

## 4. Runtime shape (`stdlib/future/`)

| Piece | Notes |
|---|---|
| `Future` / `Poll` / `Context` / `Waker` | `Output` is an associated type - a future has exactly one result type, mirroring `Iterator::Item`. `Waker` is a manual vtable (`wake_fn` / `clone_fn` / `drop_fn` / `data`), sidestepping `dyn`. |
| `Executor` | Worker pool, ready queue, per-task atomic run-state. Under `--panic=unwind`, a `catch_unwind` boundary at each poll keeps one panicking task from taking down a worker; under the default `--panic=abort` the boundary is a plain call and a task panic aborts the process. **Async never *requires* unwind.** |
| `Reactor` | One readiness interface on both platforms: `epoll` on Linux, `\Device\Afd` + `IOCTL_AFD_POLL` + IOCP on Windows. Dedicated thread, one per `Executor`. |
| `BlockingPool` | For work no readiness reactor can drive - `getaddrinfo` has no non-blocking form, and a regular file is always "ready" to `epoll`. Starts at zero threads, grows on demand to a configurable ceiling (default 512). Job blocks are refcounted, not handshaken: releasing a stored waker can reclaim the task, drop the future, and release the same block re-entrantly. |
| `Sleep`, combinators | Deadline chain on the reactor. `join` / `try_join` / `select` / `timeout`, arity 2, nesting for higher arities. |

**Cancellation is a plain drop.** Releasing a `Select` loser or a `Timeout` victim is what disarms a
`Sleep` and releases an I/O registration. There is no cancellation protocol to implement in a future
beyond a correct `Drop`.

## 5. Invariants that bite

- **A future that needs a buffer owns it.** A future moves between polls, so a `u8*` into the
  caller's frame is written through a stale address. Async I/O futures take an `Array<u8>` by value
  and hand it back on completion. This is also what makes the TLS futures sound: OpenSSL requires a
  `WANT_READ`/`WANT_WRITE` retry to repeat the call with the *same* arguments.
- **A pointer obtained through a call is an address-of.** `ws.connection()` returning a pointer into
  a carried value addresses a stale copy once the value is carried. There is no silent repair - the
  address changes, not the lifetime - so it is rejected like `&local`.
- **`E0455` covers frame addresses, not just references.** The address of a local or parameter may
  not be handed to a future that outlives the current step.

## 6. Diagnostics

| Code | Rejects |
|---|---|
| `E0455` | A reference or frame address live across a suspension; an async method awaited on a receiver that names no storage; a method future stored and awaited later. |
| `E0459` | Moving a future after it has been polled. |
| `E0453` | Moving a carried value out from behind its in-place pointer (the give-away rules). |
| `E0364` | `async` combined with `virtual`/`override`; async constructors, destructors, fields. |
| `E0365` | A generic `async function main`, or a return type that is neither `i32` nor `void`. |
| `E0600` | A future that would contain itself; unsupported async control flow. |

## 7. Known gaps

- **Five forms the lowering does not walk:** an `unsafe` block, a tuple literal, `switch`, `static
  match`, and `delete`. An `await` inside one is never counted as a suspension, so the body lowers
  around it and the surviving node is rejected at codegen (`E0600`) rather than in sema. Loud, never
  a miscompile. `rn_expr`/`rn_stmt` know all five; the other eight walkers know none - fixing it is a
  sweep across all nine plus `lower_stmt_sm`, but `unsafe` needs a semantics call first (the block is
  split across states, so the `unsafe` scope does not survive the explosion).
- **Give-away then reuse inside one state** is caught only in the straight-line case. The branchy and
  cross-state forms surface as a runtime `unwrap`-on-`None` panic, not a compile error, because once
  the value lives behind an `Option` reached by method calls, flow-sensitive move tracking of it is
  gone by construction.
- **No async `fs` or `stdio`.** Neither fits a readiness reactor; both need the blocking pool or a
  second, completion-based backend.
- **No async-native synchronisation.** `sync::mpsc` and `Mutex` block the calling worker.

## 8. Testing async code

- **A transport double cannot catch an address bug.** A mock `AsyncTransport` passed all five
  WebSocket shapes while the real-socket pipelined test hung - the defect was a pointer into a
  carried value, which a mock has no way to exhibit. Async I/O changes need a test over a real
  loopback socket.
- **Timing tests must assert ratios, not absolutes.** The `process` concurrency test compares two
  children against one measured the same way (~1.01 concurrent vs ~1.99 serialized); an absolute
  bound encodes the speed of whatever ran it. Pair every ratio with a floor check so a command that
  failed to spawn cannot look concurrent.
- **The Windows AFD reactor is validated** - 30/30 runs, 0 failures, 0 hangs, on a real Windows host
  (2026-07-25). **Do not re-validate it under wine.** wine 9.0 cannot service `IOCTL_AFD_POLL`, so it
  hangs there for reasons that have nothing to do with the backend. That hang was once misread as a
  code defect.
- Assert numbers and drop counts, not just success. Several carrier bugs passed a
  did-it-return-`Ok` test and failed a count-the-drops one.

## 9. Where the history lives

The full implementation record - every root cause, every rejected approach, session by session - is
`ASYNC_IMPL.md`, which was retired once async landed. Recover it with
`git show <sha>:ASYNC_IMPL.md`. This document is the distillate; the tracker is the evidence.
