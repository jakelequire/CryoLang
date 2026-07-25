# HANDOFF — Cryo `async`/`await`: fix the `MatchExpression` gap, then do the async-only socket port

Two root-cause compiler fixes landed last session and are **validated, gated, and pinned but UNCOMMITTED**
on `ll-impl` (HEAD still `4927f327`). While validating them a **new blocker** surfaced that sits directly
on the critical path for the socket port Jake ordered. That blocker is your first job.

---

## 0. START HERE

1. **Read `ASYNC_IMPL.md` (repo root) top-to-bottom.** Single source of truth: locked model + surface
   (§2/§4/§7), phased plan (§5), lowering design (§10), build/gate procedure + landmines (§6), and the
   append-only **Progress Log (§9)** whose **two newest entries (both about last session) are your
   immediate context**.
2. **Keep `ASYNC_IMPL.md` current** — update the Status Dashboard (§1), append to §9. This HANDOFF is
   init-only: it boots you, it is never a running log. Replace it at your session's end.
3. Memory lives at `~/.claude/projects/.../memory/` (index `MEMORY.md`). Freshest:
   `async_match_expression_gap_2026_07_25`, `async_track3_cond_rebuild_2026_07_24`,
   `drop_insertion_initflag_bug_2026_07_24`, `cryo_drop_semantics_facts`.
   **Trust `ASYNC_IMPL.md` §9 over any memory summary** if they disagree.

## 1. Environment — VERIFY IT YOURSELF

Last session: Linux codespace (`bin/cryo` = Linux ELF; wine 9.0; llvm-mingw cross-toolchain present).
**You may be elsewhere — check and adapt.** On Windows run `make` from PowerShell with `CRYO_CC=gcc`.

```
git branch --show-current            # ll-impl
git log --oneline -1                 # 4927f327
git status --short                   # expect the UNCOMMITTED set in §2 below
python scripts/verify-pin.py         # expect OK  (a870e6ac… / 2ab3cfbd…)
CRYO_CC=gcc make stdlib              # 149 modules, "Project compilation succeeded"
```

## 2. What is uncommitted (do NOT lose it; Jake commits)

```
 M ASYNC_IMPL.md                                     (§1 dashboard + 2 new §9 entries)
 M bin/cryo, bin/cryo.exe, *.pin.txt                 (repinned both OS — a870e6ac… / 2ab3cfbd…)
 M compiler/src/compiler/passes/drop_insertion.cryo  (fix 1)
 M compiler/src/compiler/sema/async_lower.cryo       (fixes 2 + 3)
 M tests/tests/lang/conditional_init_drop.cryo       (+6 tests)
?? tests/tests/lang/async_carry_across_await.cryo    (+13 tests — NEW FILE, don't lose it)
```

All gates green at this state: `make test` **OVERALL PASS**, both selfhost fixed points
(**Linux 0/236, Windows 0/235** differing `.ll`), repin delta **0/235 compiler + 0/149 stdlib on both OS**,
`verify-pin: OK`.

## 3. The non-negotiables (Jake's standing rules — mirror exactly)

- **Only Jake commits.** NEVER `git commit`, no co-author, no trailers. You MAY `make pin` at a clean green
  boundary and MUST leave the tree ready.
- **Repin with plain `make pin`** (never `CRYO_CC=gcc make pin` — a landmine); verify with
  `python scripts/verify-pin.py`. It builds BOTH `bin/cryo` and `bin/cryo.exe`; the rule is **both OS**.
- **Right decision over green. Fix root causes; no workarounds.**
- **A warning on generated code is a bug report about the generator, not noise to filter.** Jake called
  this out explicitly last session and it turned up a real dead-code bug. Do not dismiss a diagnostic
  because it is "pre-existing".
- **When something genuinely needs Jake's opinion, ASK** (the question tool) — surface/semantics/one-way
  doors. Present options honestly and let him pick.
- **Comments describe logic** (invariant + failure mode prevented), never project narrative — no
  dated/phase labels in code. `ASYNC_IMPL.md` is the exception.
- **Never run two heavy builds at once** (`make stdlib`/`cryo`/`test`/`selfhost`/`pin`) → environmental
  **exit -15**. Serial only. **Never blind `git stash pop`.**
- Preferences: methods / namespaced statics over free functions; `&this` IS the object's box address;
  `T::new(...)` constructors; one generic method + `static match (T)` over type-suffixed names; **bare
  integer literals** (`1`, not `1u32`); owning aggregates BY POINTER; no `let` (`const`/`mut`). A callback
  stored as a raw fn-ptr (waker/reactor/pthread/`try_catch` slot) **MUST be a free function**.

---

## 4. TASK 1 (do this first) — `MatchExpression` is invisible to the async lowering

**Fully diagnosed, not started. Blocks the socket port.**

Every async socket op returns a `Result`, and the idiomatic Cryo unwrap is a match **expression** — so this
will be hit on essentially every file of the port.

```cryo
async function f(r: Result<i64, i64>) -> i64 {
    const a = await something();
    const v: i64 = match (r) {                 // <-- E0201: cannot find value `r`
        Result::Ok(x)  => { x }
        Result::Err(_) => { return -1i64; }    // <-- E0200: expected `Poll<i64>`, found `i32`
    };
    return v + (a as i64);
}
```

The identical body compiles clean when not `async`.

**Root cause.** `MatchExpression` appears **once** in `sema/async_lower.cryo` (only in the alpha-renamer
`rn_expr`); `MatchStatement` appears **11** times. One gap, two symptoms:

1. **E0201** — the use-analysis (`stmt_first_use`/`expr_first_use`, `name_read_in_*`) never descends into a
   `MatchExpression`, so a value read ONLY as a match subject is not seen as used by that state and is
   never carried across the suspend.
2. **E0200** — `rewrite_returns` walks statements only. It has **no `DeclarationStatement` arm at all** and
   handles only `MatchStatement`, so a `return` inside a match-expression arm is never rewritten to
   `Poll::Ready(…)`. The diagnostic also **leaks the internal `Poll<T>`** to a user who wrote `-> i64`.

**Fix shape.** Audit EVERY walker in `async_lower.cryo` for a `MatchExpression` arm beside its
`MatchStatement` one: `rewrite_returns` (plus the missing `DeclarationStatement`/`ExpressionStatement`
expression descent), `stmt_first_use`/`expr_first_use`, `name_read_in_stmt`/`name_read_in_expr`,
`mark_last_use_stmt`/`mark_last_use_expr`, `subst_name_*`, `stmt_await_count`, `stmt_diverges`. Treat an
`await` inside a match-expression arm as a separate increment (it is a nested-in-expression await, which is
its own documented E0600).

**Workaround while it is unfixed** (used by last session's probe): a match STATEMENT assigning a
pre-declared local — `mut s: T = <placeholder>; match (r) { Result::Ok(v) => { s = v; } … }`.

**Validate:** the snippet above, plus a match-expression whose subject is a carried aggregate, one whose
arm returns, one in a non-async fn (must stay unchanged), and add permanent tests to
`tests/tests/lang/async_carry_across_await.cryo`.

## 5. TASK 2 — the async-only socket port (Jake's directive)

Jake chose this knowingly over keeping a blocking API: **sockets become async-only and every consumer is
ported.** Do NOT delete the blocking surface first — the tree would be red for the whole port.

1. Port consumers to `async function`s over the futures in `net/socket/tcp.cryo`: `net::http`
   (client/server/router/request/response), `net::http2` (client/server/connection), `net::ws`,
   `net::https`, and the tests `tests/tests/stdlib/net_{tcp,tls,ws,https}.cryo`. **Good news:** the
   protocol layers are mostly generic over `S: Read + Write` — `http2/connection.cryo` is 855 lines with
   **zero** concrete socket uses — so concrete touchpoints are few (2–5 per file).
2. **`net::tls` needs its own design pass before you touch it:** it hands its fd to OpenSSL's *blocking*
   socket BIO. Async TLS means a non-blocking BIO plus `SSL_ERROR_WANT_READ`/`WANT_WRITE` driving the same
   reactor registrations. **Bring Jake a design before building it.**
3. Only then delete the blocking surface from `tcp.cryo` (the `Read`/`Write` impls, blocking
   `connect`/`accept`) and rename the async ops onto `read`/`write`/`accept`/`connect`.

**Writing async socket code — the rules that are NOT obvious:**
- **Buffers must be parameters, not locals.** A future's locals live on the poll call's stack frame, which
  is gone between polls, so a pointer into one dangles. Parameters become fields of the future and keep a
  stable address (`async function f(s: TcpStream, buf: u8*, cap: u64)`).
- **Owned-handle style**: an operation takes the handle and hands it back
  (`mut io = await TcpRead::start(s, buf, n); s = io.take_stream();`). `TcpIo`/`TcpAccepted` expose their
  contents only via `take_stream()` / `take_listener()` / `take_result()`, because a partial move out of a
  droppable aggregate is rejected.
- **Hoist every `await` to its own statement.** An await nested in an expression (e.g. `match (await f())`)
  is E0600, and its diagnostic is preceded by ~7 lines of `codegen failed for module N` noise.
- A by-value parameter needs `mut v: T` to be reassignable (plain `v: T` → E0218).
- **UNPROVEN, needs thought during the port:** a socket handle *conditionally rebuilt from inside a branch*
  (`if (…) { s = io.take_stream(); }`) leaves the stream owned by `io` on the not-taken path. Last session
  proved the conditional-rebuild carry works for droppable values generally, but deliberately did **not**
  prove this ownership shape.

## 6. TASK 3 — validate the Windows AFD reactor on real Windows

The Windows backend (IOCP + `\Device\Afd` + `IOCTL_AFD_POLL`, mio/wepoll's design, Jake's explicit choice)
is **written, compiles, links and runs — but is UNVALIDATED**: **wine 9.0 cannot service `IOCTL_AFD_POLL`**
(the call never returns, against either a `\Device\Afd` helper handle or an IOCP-associated socket; mio has
the same wine limitation). IOCP creation and opening `\Device\Afd\Cryo` DO work under wine; only the poll
ioctl hangs. Two real bugs were already found and fixed there (`INVALID_HANDLE_VALUE` must be the
full-width all-ones pointer; NT structures must be 8-byte aligned, hence `u64`-backed buffers).
**On a real Windows box:** build the stdlib, build an async echo probe, run it, stress ≥25 runs.

## 7. Known residual from last session (Jake's call to scope)

`drop_insertion` still runs a destructor on uninitialized memory for **piecemeal init**:

```cryo
mut r: Res;
if (early) { return 0; }   // Res::drop RUNS here
r.id = 5;
```

A plain `=` to a sub-place genuinely can be the initializing write and a whole-binding flag cannot observe
it, so the pass disqualifies the local → unguarded drop. Not disqualifying would leak instead. A correct
fix needs **per-field init tracking**, not a per-binding flag — a pass extension, deliberately not smuggled
into last session's fix. The regression test asserts the leak-prevention property (a piecemeal-initialized
value still drops exactly once), NOT the uninit-drop count, so tightening this later will not fight it.

## 8. Build / gate / validate

- **Fast loop:** `CRYO_CC=gcc make stdlib` (149 modules) → `CRYO_CC=gcc make cryo`.
- **Probe projects** go in your **scratchpad dir, NOT the repo tree**: a `cryoconfig` with `[project]`
  (`project_name`/`output_dir`/`target_type`/`entry_point` — `name`/`version` are ignored), `[compiler]`,
  `[dependencies]`; add `[link] system = ["ssl", "crypto"]` if you `import std::net` (it pulls in
  `net::tls`). Build with
  `CRYO_CC=gcc CRYO_STDLIB=<repo>/stdlib <repo>/compiler/build/cryo build` (use `bin/cryo` to test the
  pinned compiler). **Assert via exit code.**
- **Tracing inside a probe must be unbuffered** — a killed process loses buffered output. Use raw
  `libc::write(2, …)`, not `eprintf`.
- **`strace` is available and is the fastest tool for fd/syscall mysteries.** Use it before theorizing —
  it is what found the `close(0)` bug after several wrong theories.
- **valgrind is available** and is clean on every async probe so far — a leak or double-free shows up
  immediately.
- **Concurrency probes: ≥25 process runs.** A single pass never rules out a race.
- **A `0 differing` diff line is meaningless without its denominator.** Always print the file count too,
  and beware that `cd` persists between Bash calls — a stale cwd silently turns a real check into `0 / 0`.
- **Gate boundary:** pure stdlib → `make stdlib` + `verify-pin`, NO repin. Compiler touched →
  `make selfhost-check` (BOTH fixed points; Windows stages run under wine) + `make test` + `make pin`
  (both OS) + `verify-pin`. **Do NOT commit.** Note `make selfhost-check` clobbers
  `compiler/build/cryo.exe` to a Linux ELF → `make cryo` again after. Its tee'd log may be truncated by
  `tail`, so verify fixed points by diffing `compiler/build/self/s3` vs `s4` and `win-s3` vs `win-s4`
  directly rather than trusting a grep of the log.

## 9. What exists now (the substrate)

- **`stdlib/future/reactor.cryo`** — one readiness interface, per-OS backends behind `![target(...)]` free
  fns (`rt_backend_open/close`, `rt_arm`, `rt_disarm`, `rt_kick`, `rt_wait`). Linux: epoll + eventfd +
  `EPOLLONESHOT`. Windows: IOCP + AFD (unvalidated, §6). `Reactor::current()` is a `![thread_local]` each
  executor worker publishes. **Three invariants are correctness, not taste** (documented in the module):
  level-triggered arming, dispatch by descriptor number (never by a pointer to the registration), and
  **no waker is woken or dropped while the table lock is held**.
- **`stdlib/future/executor.cryo`** — pthread worker pool, `Arc`-refcounted tasks, `catch_unwind` poll
  boundary, reactor lifecycle in `Executor::drop` (join workers → stop reactor → drain queue, in that
  order). `Executor::with_threads(n)`; `spawn` returns a `JoinHandle` whose `join()` yields
  `Result<O, JoinError>`. `task_drop_thunk` moves the boxed future into a local and lets scope-exit glue
  run — do **not** "fix" this by adding a `where F: Drop` bound; generated futures declare no `drop` method
  and the bound would reject exactly them.
- **`stdlib/net/socket/tcp.cryo`** — `TcpConnect`/`TcpAccept`/`TcpRead`/`TcpWrite` + `set_nonblocking`,
  owned-handle style. `TcpConnect::Output` is `Result<TcpStream, IoError>` directly (no wrapper).
  Blocking API still present (deliberately, until §5 step 3).
- **`sema/async_lower.cryo`** — the state-machine lowering: the `Option<T>` aggregate carrier
  (`None` until first stored), liveness-aware promotion, `last_use_consumes`, carried non-`Copy`
  parameters, and (new) `block_cond_write` — a write nested in a branch is carried like a read, because the
  skipped path keeps the previous value.

## 10. Landmines & durable facts (paid for in blood; also `ASYNC_IMPL.md` §6 + §9)

- **Assignment does NOT drop the overwritten value** — not through a pointer, not to a field, **not even to
  a plain local**. Only the final value is dropped at scope exit. To replace an owning value, read the old
  one into a local first and let it die there.
- **A `drop` method in a type body IS the destructor** (no `implement trait Drop` needed) — so
  `TcpStream`/`TcpListener` are droppable, partial moves out of aggregates holding them are rejected, and
  `mem::swap`-with-a-placeholder is the way to extract them.
- **A struct with droppable fields but no explicit `Drop` impl DOES drop its fields at scope exit** — but
  it has no callable `.drop()` method.
- **A by-value parameter that is never moved on drops at function exit.** A constructor that only reads
  `param.raw_fd()` closes the socket on return; store the value to own it.
- **In `drop_insertion`, compound assigns and `++`/`--` must NEVER be treated as initializing writes** —
  they read the target first. Treating them as writes re-introduces an unguarded drop for every local whose
  field is incremented after a normal assignment.
- **In `async_lower`, read `last_use_consumes(blocks[ds])` BEFORE appending the hand-back store** to that
  block — the store passes the value to `Option::Some` by value and would otherwise report every declaring
  state as having given the value away.
- `_`-prefixed locals still drop; an explicit `.drop()` does not double-drop.
- **`![config(...)]`/`![target(...)]` gating strips file-scope declarations only, never method bodies** —
  per-OS code lives in gated free functions.
- **Do NOT `import thread` into `future::*`** — the `JoinHandle` leaf name clashes and misresolves
  differently between the lib build and a consumer build.
- `--panic=unwind` is Linux/hosted only; native exit does not flush stdio; anchors drift, grep symbols.

**First action:** confirm your OS/shell, verify the baseline (§1) and that the §2 uncommitted set is
present, read `ASYNC_IMPL.md` (§9's two newest entries, §5, §7), then reproduce the Task-1 snippet in §4
before changing anything — it takes two minutes and anchors everything that follows. Do NOT commit; leave
the tree green for Jake.
