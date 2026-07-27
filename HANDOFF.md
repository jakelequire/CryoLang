# HANDOFF — refactor the stdlib socket stack onto async, and delete the blocking surface

**Your mission:** convert every socket consumer in `stdlib/net/` to the async I/O traits, IN PLACE, and
remove the blocking socket surface as you go. ~3,300 non-comment lines of consumer code, plus tests.

The compiler work is done. Three defects that blocked the design were fixed and pinned last session, and
the whole gate is green. What remains is stdlib refactoring — no compiler changes are expected, which
means **the §5 port should need no repin.**

`ASYNC_IMPL.md` is the design + full history and is authoritative. This file is the INIT document — do not
edit it as you work, and do not write a new one at session end unless Jake asks. Running status goes in
`ASYNC_IMPL.md` (Status Dashboard + §9 Progress Log) and in agent memory.

Jake's words, still the standard: *"my goal is to get a full 100% completed working async without any
cheap workarounds, half baked solutions, or the easy way out on something."* Take that literally. If you
are about to special-case a shape, narrow a test to dodge an error, or defer a sub-case behind a loud
error just to get green: stop, and either solve it properly or bring Jake the design question. **He will
notice, and he will push back.** A test narrowed to avoid a diagnostic is the exact anti-pattern — last
session that dodge, once reverted, exposed a bug strictly larger than the one being avoided.

---

## 0. Baseline — verify before you start (2 minutes, saves hours)

- Branch `ll-impl`, HEAD **`672a33cc`**. **The working tree is DIRTY and that is expected:** last
  session's compiler fixes + tests are staged in the working copy, not committed. Only Jake commits.
  Expect these modified: `ASYNC_IMPL.md`, `bin/cryo*`, `bin/*.pin.txt`, three `compiler/src/compiler/sema/`
  files, `tests/test-roster.txt`, two `tests/tests/lang/async_*.cryo`. **Do not revert any of it.**
- `python scripts/verify-pin.py` → **OK**, and the two `.pin.txt` files must be a **matched pair** —
  compare their `git-describe:` lines, not just each hash. Both should read `672a33cc-dirty`.
  `verify-pin.py` checks each binary against its OWN recorded hash and so cannot tell a half-finished
  repin from a good one.
- Gate numbers as left: unit **1798**, compile-fail **159**, projects **12 on Windows** (15 exist; Windows
  skips 3 — `requires cxx`, `requires os:linux`, `requires display`). `make roster-check` → OK.
- **Linux discovers one fewer unit test (1797)** — `ProcessCommand::output_large_stderr_no_deadlock_win`
  is Windows-only — and runs more projects. Benign; do not "fix" them.
- Selfhost + repin were run by Jake at the end of last session and were green. The pin CONTAINS the three
  compiler fixes, so a stdlib-only change from here needs no repin.

Sanity-check the substrate before writing stdlib code:

```
make test ARGS="IoAsyncTraits"    # 10
make test ARGS="NetTcpConn"       # 2
make test ARGS="AsyncTraitMethod" # 8
make test ARGS="AsyncTryOperator" # 11
```

Those cover async trait methods with default bodies, `?` anywhere in an async body, `await` in a match
arm, a generic owner mutating through `mut &this` across suspends, a trait impl ON a generic owner, and
the real socket path end to end. If any of it is red, stop and diagnose before refactoring.

---

## 1. Jake's standing rules (mirror exactly)

1. **NO MIRRORS.** His ruling, verbatim: *"For this whole thing of adding async to the stdlib, I don't
   ever want to do mirrors. Just update tests and whatever you need to do for this transition, it will be
   better in the long term."* This overrides the previous plan in two places:
   - There is **no `TlsConn` twinning `TcpConn`.** One generic buffered connection, both transports.
   - There is **no async path added alongside a blocking one** to be demolished in a later phase. Each
     layer converts IN PLACE and deletes the blocking code it replaces, in the same increment, with its
     tests updated in that same increment.
   He accepted the cost of this deliberately (rewriting the landed+tested `TcpConn`, moving tests onto an
   `Executor`). Do not re-litigate it, and do not "temporarily" duplicate something.
2. **Only Jake commits.** Never `git commit`, never co-author. You MAY `make pin` at a clean green
   boundary, and you MUST leave the tree ready for him.
3. **Repin BOTH OSes** with plain `make pin` — **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `python scripts/verify-pin.py`. Repin only when a change moves default-path (`--panic=abort`) compiler
   IR — **measure it (§2), don't guess.** A stdlib-only change never needs a repin.
4. **Right decision over green. No workarounds, no half-baked solutions.** If a correct change breaks the
   build, FIX the build — never revert the correct change to stay green. Equally: never leave a feature
   HALF-landed. **If you cannot make a change work, revert it rather than leaving an unverified change in
   the tree**, and report the diagnosis.
5. **Comments describe the logic** — the invariant, and the failure mode it prevents. Never project
   narrative. No dated/audit/phase/batch labels in code. `ASYNC_IMPL.md` and this file are the exception.
6. Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
   over type-suffixed names; bare integer literals (`1`, not `1u32`); pass owning aggregates BY POINTER.
7. **When a decision has two defensible answers, ASK Jake** (use the question tool) — for
   language-semantics and soundness-contract calls, not routine judgement. He answers fast and picks the
   thorough option; do not pre-emptively scope work down for him. He also asks questions mid-turn; answer
   them directly and let the answer change the plan.
8. **Don't add prose to `REPORT.md`** — tick items off only.
9. **Keep shell commands SIMPLE and readable.** Jake watches gate output live. Say what you are about to
   run, and **offer to let him drive the two long gates** — he frequently does, then repins and commits.

---

## 2. Build / gate / probe recipe

**Windows**: run `make` from **PowerShell** with `$env:CRYO_CC='gcc'`, not from the Bash tool.
**Linux**: plain `make` from bash. **Serial only — never two heavy builds at once**, and **never edit
compiler or stdlib sources while a gate is running.** Editing markdown during a gate is fine.

```
make cryo            # ~2 min. `make test` does NOT rebuild the compiler.
make test            # ~12 min. Expect OVERALL PASS (see §0 for the numbers)
make test ARGS="<substring>"   # filters -- the fast inner loop
make selfhost-check  # ~15 min; needs exit 0 AND *TWO* `FIXED POINT OK`
make roster-check
make pin             # plain; then: python scripts/verify-pin.py
```

### THE TIMEOUT TRAP

**Both the Bash and PowerShell tools clamp their timeout to 600000 ms (10 minutes).** `make test` (~12 min)
and `make selfhost-check` (~15 min) both exceed it, so running either normally — **or with
`run_in_background: true` on the PowerShell tool** — gets it **KILLED part-way**, and the log's last line
looks identical to "still running".

**You can still cover the entire `make test` gate under the cap by running it in pieces**, which is what
last session did:

```
make test ARGS="Tests::Lang"     # 883
make test ARGS="Tests::Stdlib"   # 915      -> 1798 unit, the full roster
make test ARGS="E0"              # 153 compile-fail
make test ARGS="W00"             #   6 compile-fail  -> 159
make test ARGS="async_main"      #   3 projects
# then the remaining 9 projects by name: collect_multimod, compile_fail_typeerror,
# ffi_c_import, generic_name_collision, known_fail_canary, match_expr_arm_drop,
# native_alloc_gate, never_return_diverges, run_exit_and_stdout
```

The pattern is a plain substring filter over test/project names (`cryo help test`). This is a legitimate
full-gate substitute; `selfhost-check` has no such split, so **ask Jake to drive that one.**

### Gate holes that have bitten before

- `selfhost-check` exit 0 is **not** sufficient — require `FIXED POINT OK` **count == 2**. On Windows read
  it with PowerShell `Select-String` (the tee log can be UTF-16, `grep` will miss it).
- **A stage failing in ~14 s with an EMPTY stage log means contention, not a miscompile.**
- `selfhost-check` **deletes `compiler/build/cryo.exe`**, and `make pin` needs it. Run `make cryo` between
  them. (`bin/cryo.exe`, the pin itself, is untouched.)
- **`make pin` writes its two halves ~3 minutes apart.** A `git status` in that window shows only
  `bin/cryo` modified and looks exactly like the forbidden linux-only repin — re-check before acting.
- **PIN-DELTA PATHS — verify the FILE COUNTS, not just the diff count.** The IR lives under an `ir/`
  subdirectory:
  ```
  compiler/build/self/win-s{2,3}/target/release/host-windows/local/ir   # 161 .ll
  compiler/build/self/win-s{2,3}/target/release/host-windows/std/ir     #  74 .ll
  ```
  A count that is not 161 / 74 means you compared the wrong tree — that mistake once produced a cheerful
  `files=0 differing=0`. **A pin-vs-new comparison also has a NON-SEMANTIC delta you must normalize:** the
  `FILE` macro embeds the stdlib path as the invoking stage passed it (`bin/../stdlib/core/mem.cryo` vs
  `stdlib/core/mem.cryo`), which also changes the `[N x i8]` length. Strip lines matching `@FILE.str`
  from both sides before diffing.
- **THE ROSTER IS PLATFORM-SENSITIVE — never blind-`--update` it.** **MERGE**: union of the committed
  golden and `cryo test --list`, sorted, written back with `newline="\n"`, using `roster-check.py`'s own
  filter (`": test" in ln`). **Print the golden-only count — it must be 0.** Then check
  `git diff --numstat tests/test-roster.txt` shows `N 0`; a whole-file rewrite means you flipped the line
  endings. (A merge script that does exactly this is easy to re-derive; last session's lived in the
  scratchpad and is gone.)

### Probe setup

```
<scratch>/probe/cryoconfig:
  [project]
  project_name = "probe"
  output_dir = "build"
  target_type = "executable"
  source_dir = "src"
  entry_point = "src/main.cryo"
  stdlib_root = "C:/Programming/apps/CryoLang/stdlib"
  [compiler]
  [dependencies]
```

Build with the **freshly built** `compiler/build/cryo.exe` plus `CRYO_STDLIB=<repo>/stdlib`; use
`bin/cryo.exe` to compare against the pin — **that comparison is how you prove a bug pre-dates your
change.** `rm -rf build` first when you need fresh IR. `--emit-llvm` writes `.ll` under
`build/target/release/host-windows/local/ir/`. **`--ast` dumps the POST-LOWERING AST**, which is the
fastest way to see what the async lowering actually generated.

**Running a probe on Windows** — GNU `timeout` cannot launch the exe (exit 127) and
`Start-Process -PassThru` does not populate `ExitCode`. This works:
```powershell
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "<path>\probe.exe"; $psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$p = [System.Diagnostics.Process]::Start($psi)
$out = $p.StandardOutput.ReadToEnd()
if ($p.WaitForExit(60000)) { "$out EXIT=$($p.ExitCode)" } else { $p.Kill(); "TIMEOUT" }
```
Also: **`Remove-Item -Recurse -Force build` is intercepted in this environment.** Use Bash `rm -rf`.

### Probe gotchas that cost real time

- **The control that proves it is the lowering, not the language:** compile the identical body **without**
  `async`. Run that first on anything in this area. The sibling control that proved most valuable last
  session was **inherent method vs. trait-impl method** on the same generic struct.
- **A green probe is not evidence, and neither is a green COMPILE.** The generic-owner bugs are a
  SILENT-MISCOMPILE class: writes through `mut &this` are simply never seen again. Every probe in this
  area must **print an expected number** covering writes at each level, and a drop-counting global when
  ownership is involved. "It compiled" has been wrong three times.
- **Isolate one variant per build.** The compiler can stop after the first error in a phase, so "only
  variant 1 failed" is not evidence variants 2-4 pass.
- **Print numbers, don't infer from exit codes** — `printf("%lld", x)` from `std::fmt`. **A hard crash
  loses whatever printf has buffered**, so on a segfault hunt call `libc::fflush(null)` after each marker.
  (`format(...)` does NOT do `{}` substitution; interpolation is f-strings.)
- **Always run under a timeout** — async bugs present as hangs.
- **Import narrowly.** `import std::net;` drags in `net::tls` → OpenSSL link errors unless the project
  lists `ssl`/`crypto` in `link_libs`.
- `block_on(f()) as i32` mis-binds the `R` of `block_on<F,R>` and reports a confusing mismatch **inside
  `stdlib/future/_module.cryo`**. Bind to a typed local first, and **do not turbofish it** —
  `future::block_on<i64>(f())` binds `F`, not `R`. **Ten sessions have lost time to this.**
- **`PendingThenReady` returns `Pending` WITHOUT waking**, so it only works under `future::block_on`.
  On a real `Executor` it parks forever ("root future did not complete"). Use `Sleep` for Executor work.
  `PendingThenReady::new(pending_count, value)` — **count first**.
- `Array<T>` has **no `[]` operator** — `.get(i)` (returns `Option<T>`) and `.set(i, v)`.
  `String::length()` returns **`u64`**. Cryo spells the boolean type **`boolean`**.
- **`else if` is NOT valid in expression position.** Use a `mut` plus a statement-level `if`. (A two-armed
  `if`/`else` expression with block tails IS fine.)
- `Result::Ok(v)` / `Result::Err(e)` take **no turbofish**. An untyped `Option::Some(3)` leaves its payload
  as `T` — bind to a typed local before matching, and before applying `?`.
- No block-with-tail-expression for match arms — use a ternary. A `match` may have **at most one
  irrefutable arm** (E0114), counting only UNGUARDED `_` arms.
- Trait impls are `implement trait Foo for Bar { … }` — the `trait` keyword is required (E0101).
  A `where` clause on a trait-impl BLOCK does not exist; the bound goes on the METHOD
  (`async fill(mut &this) -> R where S: AsyncTransport { … }`), which parses and works.
- **`Executor` has a `Drop` impl**, so a local tears the runtime down at scope exit, and **`Executor::drop`
  CANCELS a task still parked on a deadline** — a test that spawns work must `join` first.
  `Reactor::current()` is null off an executor worker, so `future::block_on` has **no reactor**; that is
  why `async fn main` uses an `Executor`.
- **`make cryo` printing `cryo is up to date` is NOT proof the incremental cache is clean.** Check
  `ls -l compiler/build/cryo*` against your edited source.
- **`cdebug(fmt, …)` (`Utils::Logger`) is the ONLY reliable way to see what a sema pass does.** A
  `--debug`-gated stderr printf; `import Utils::Logger;`. **Remove every one before gating** and grep to
  prove it. Use a unique tag and **redirect the whole run to a FILE** — a tail-limited view hid the
  decisive traces once.
- Running `cryo test` directly from the agent shell can fail `EnvT::vars_finds_path_variable`. Artifact of
  the invocation, not a regression.
- **The Bash tool's cwd drifts** when PowerShell `Set-Location` is used in the same session. Use absolute
  paths, or the Grep tool. **Native Windows Python cannot see Git-Bash `/tmp` paths** — a comparison
  script written that way silently reports every file as missing (i.e. "all different").

---

## 3. What is DONE — do not redo, do not regress

**Lowering:** straight-line; `if`/`else`; all four loop forms + `break`/`continue`; `match` statements and
expressions; **`await` in a `match`-arm GUARD**; aggregates and droppable parameters across suspends;
scope-aware alpha-renaming; `async fn` awaiting `async fn`; `-> void`; declaration order irrelevant;
recursion rejected; a frame address held across a suspend rejected (`E0455`); **generic `async function`**;
**`async` METHODS**; **`async function main`**; **`async` TRAIT methods with default bodies**; **`await`
anywhere an expression may appear**; **the `?` operator anywhere in an `async` body**. No rejected `await`
position remains.

**Runtime:** `Executor` (`spawn`/`join`/`abort`/detach, `Arc<Task>`, worker pool, poll-boundary
`catch_unwind`); `Reactor` (epoll on Linux, IOCP + `\Device\Afd` on Windows, **validated 30/30 on real
Windows**) with a deadline-sorted timer chain; `Sleep`; `Futures::join` / `select` / `timeout` /
`timeout_at`.

**Async I/O primitives:** `TcpConnect` / `TcpAccept` / `TcpRead` / `TcpWrite`, and async TLS —
`TlsHandshake` / `TlsRead` / `TlsWrite` / `TlsIo` plus `TlsConnector::connect_async` and
`TlsAcceptor::accept_async`. All own their transport AND their buffer. Concrete hand-written `Future` impls.
**`stdlib/net/tls/future.cryo` is the ASYNC TLS file — keep it.**

**The async I/O traits** — `stdlib/io/async_traits.cryo`, `AsyncRead` / `AsyncWrite` (10 tests).
**The connection owns a persistent buffer; no buffer crosses the API.**

```cryo
type trait AsyncRead {
    async fill(mut &this) -> Result<u64, IoError>;   // pull more; 0 = EOF
    buffered(&this) -> Slice<u8>;                    // SYNC
    consume(mut &this, count: u64) -> void;          // SYNC
    // defaults: scan_for, take_front (both SYNC), ensure, read_exact,
    //           read_some, read_until, read_line, skip
}
type trait AsyncWrite {
    pending(mut &this) -> Array<u8>*;                // SYNC
    async flush(mut &this) -> Result<(), IoError>;
    // defaults: queue<T> (SYNC, static match over Slice<u8>/Str/string/u8), send<T>
}
```

Three invariants to rely on and not "simplify":
- **Never hold a `buffered()` slice across an `await`** — a fill can grow the buffer and move its heap
  block. The default bodies factor scanning and copying into the SYNC helpers `scan_for` / `take_front`,
  so no slice is ever in scope at a suspension point. Keep any new default body to that discipline.
- **A successful `fill` only APPENDS to what `buffered()` reports.** That is what lets `read_until` resume
  its scan at an offset. An implementor may compact its storage — that moves bytes within the backing
  array, not within the view.
- **Queuing is synchronous.** An encoder builds a whole frame with zero suspension points, and the layer
  suspends once, in `flush`. `pending()` is exposed rather than wrapped precisely so a frame can be built
  in place instead of into a scratch array that is then copied.

**`TcpConn`** — `stdlib/net/socket/conn.cryo`, owns a `TcpStream`, implements both traits (2 tests, real
loopback on a real `Executor`). **§4 replaces this with a generic; read it first, it is the reference
implementation of every rule below.**
- **Compaction happens at the START of a fill, never the end** — an `rpos` cursor keeps `consume` O(1),
  and the consumed prefix is dropped on the next fill. Compacting after a read would shift `buffered()`
  under a caller mid-parse.
- **A failed flush leaves the unsent bytes queued**; a write moving 0 bytes is `WriteZero`, not a spin.
- **Cancellation, deliberate:** while an operation is in flight the connection holds a closed placeholder
  and the future owns the real socket, so **dropping the future closes the connection.** Coherent —
  nothing else holds the socket — but a caller wanting to keep the connection must not cancel it.

### Compiler fixes landed last session (all three pinned; do not regress)

1. **A generic owner's params never reached the future of an `async` method in a trait-impl block.**
   `sema.cryo` passed `ib.generic_params` to `declare_async_methods`; for `implement trait X for struct
   Buf<S>` those live on the TARGET, so the list is empty, the future was declared non-generic, its impl
   block was walked CONCRETELY, and an abstract `S::PullFut::Output` in its `poll` body failed every check
   instead of deferring (`E0200`). Fixed via `impl_owner_params`, falling back to the target template's
   params. **The tell:** the identical body as an INHERENT method always worked.
2. **`abstract_receiver_method_return` had no `InstantiatedType` arm** (`method_binding.cryo`). A receiver
   typed `H<S>` — a generic owner instantiated at a still-abstract arg — is unresolved only in its
   ARGUMENTS; the base is a known template. Returning invalid made the call type as nothing and an
   enclosing `await` report `E0306`. Routed to `resolve_method_return_via_template`. **The tell:** a
   non-async method on the same receiver resolved fine.
3. **`mark_last_use_expr` had no `TryExpression` arm** (`async_lower.cryo`) — the `try_live` family again.
   A droppable binding given away inside a `?` was classified as a BORROW (the catch-all reaches
   `name_read_in_expr`, which does walk the desugar), so the state handed a value it no longer owned back
   to its carrier field: `E0452`, and had it compiled, a double drop. Fixed by forwarding to `try_live(e)`
   with `by_value` preserved. **The other five `NodeKind` dispatchers with no `TryExpression` arm were
   audited and are each correct as written** — see `ASYNC_IMPL.md` for which and why, so you don't redo it.

**Guard tests:** `async_trait_method.cryo` (8, incl. 2 for a trait impl on a generic OWNER),
`async_try_operator.cryo` (11, incl. one asserting a drop COUNT of 1),
`async_receiver_refresh.cryo` (7 + two `E0455` negatives). **Do not weaken any of these sets.** The
receiver-refresh tests drive futures with a hand-written `block_on` that dirties the stack between polls —
that is what makes a stale receiver pointer observable at all; plain `block_on` reuses the frame and a
broken compiler looks green.

---

## 4. THE MISSION, part 1 — one generic buffered connection

**There is no `TlsConn`.** Collapse `TcpConn` and its would-be TLS twin into a single generic connection
over a transport seam. This is the first increment and everything else sits on it.

The shape, validated by probe last session (`4244/4244`, every `mut &this` write at both levels observed):

```cryo
/// What a transport hands back: the buffer it was given, plus how many bytes moved.
type struct Transfer { buf: Array<u8>; result: Result<u64, IoError>; /* take_buf() */ }

type trait AsyncTransport {
    async read_into(mut &this, buf: Array<u8>) -> Transfer;
    async write_from(mut &this, buf: Array<u8>) -> Transfer;
}

type struct BufConn<S> { inner: S; rbuf: Array<u8>; rpos: u64; scratch: Array<u8>; wbuf: Array<u8>; }

implement trait AsyncRead  for struct BufConn<S> { async fill(mut &this) -> … where S: AsyncTransport { … } }
implement trait AsyncWrite for struct BufConn<S> { async flush(mut &this) -> … where S: AsyncTransport { … } }
```

Key design points, each load-bearing:
- **Put the move-out/await/move-back swap INSIDE each transport's impl.** `TcpStream`'s `read_into` knows
  its own closed placeholder (`TcpStream::from_fd(-1)`); `TlsStream`'s knows its own
  (`TlsStream::from_parts(null, TcpStream::from_fd(-1))`). The buffered connection then never holds a
  placeholder and the trait needs **no `static closed() -> This`.**
- **The bound goes on the METHOD, not the impl block** — a `where` on an `implement trait … for` block
  does not exist in this language.
- `compact()` must stay reachable from the trait impl blocks, so it cannot be `private` (E0353) — say in
  the comment that it is internal.
- `Transfer` carries fields, so it dodges §6c. Do not make it empty.
- `BufConn<S>` belongs in `io/` (transport-agnostic), with `AsyncTransport` alongside the other two traits
  in `io/async_traits.cryo`; the two impls live with their transports. **There are no type aliases in
  Cryo**, so consumers spell `BufConn<TcpStream>` / `BufConn<TlsStream>`.
- Register any new module in its directory's `_module.cryo` — a file not listed there is not compiled.

`TcpConn`'s 2 tests and `io_async_traits.cryo`'s 10 must keep passing (updated to the new spelling), and
the TLS side needs equivalent coverage it does not have today.

---

## 5. THE MISSION, part 2 — convert the consumers, delete the blocking surface

Counts are **non-comment** occurrences of `TcpStream|TcpListener|TlsStream|TlsConnector|TlsAcceptor`,
re-verified at handoff time.

**Direct consumers (6):**

| File | hits |
|---|---|
| `stdlib/net/https.cryo` | 5 |
| `stdlib/net/http/server.cryo` | 5 |
| `stdlib/net/ws/conn.cryo` | 4 |
| `stdlib/net/http2/server.cryo` | 4 |
| `stdlib/net/http2/client.cryo` | 2 |
| `stdlib/net/http/client.cryo` | 2 |

**The two BORROWED-transport holders — the structural reason the trait work existed.** Both must become
owning (`inner: S`, not `inner: S*`), because a borrowed transport held across an `await` is `E0455`:

- `stdlib/net/http2/connection.cryo:76` — `Http2Connection<S> { inner: S*; … }`, ~18 methods each
  `where S: Read + Write`. Its `drop` documents *"The borrowed transport `inner` belongs to the caller and
  is not dropped here"* — once `inner` is OWNED, that comment and the drop both change.
- `stdlib/net/ws/conn.cryo:49` — `WebSocket<S> { inner: S*; … }`, ~7 methods, plus free functions
  `connect(stream: TcpStream*, …)` (`:189`) / `accept(stream: TcpStream*)` (`:196`).
  `http2/client.cryo:30`'s `connect<S>(stream: S*)` is the same shape.

**Generic entry points taking the transport by reference (9).** Every one is a `mut &R` / `mut &W`
parameter and cannot stay in that shape once its callers are async — they become methods on the owning
type, or take the transport by value. **With the sync `queue`/`flush` split, the four `write_to` /
`encode_frame` / `write_frame` ones should instead become pure byte-builders over an `Array<u8>`** (build
straight into `pending()`), which also removes free functions — Jake's preference.

| File | Entry points |
|---|---|
| `stdlib/net/ws/frame.cryo` | `encode_frame<W>` (:51), `read_frame<R>` (:106) |
| `stdlib/net/http2/frame.cryo` | `write_frame<W>` (:66), `read_frame<R>` (:90) |
| `stdlib/net/http/request.cryo` | `parse<R>` (:106), `write_to<W>` (:210), `read_line<R>` (:270) |
| `stdlib/net/http/response.cryo` | `write_to<W>` (:90), `parse<R>` (:122) |

**`net/dns.cryo`, `net/addr/ip.cryo` and `net/socket/udp.cryo` name `TcpStream` ONLY in prose — 0 code
hits each. They need no port.**

**A bonus the port collects, not a side quest.** `http/request.cryo:270`'s `read_line` carries an explicit
PERF note: it reads **one byte per `read(2)` syscall**, and its own comment says the correct fix is a
buffered reader whose buffer is SHARED with the body reader, deferred because framing was hard to
destabilise. `AsyncRead` *is* that shared buffer — `read_line` then `read_exact` come out of one buffer by
construction. `net_tcp_conn.cryo` already tests exactly that handover. Say so when you port it.

**The blocking surface to delete as its consumers convert:** `TcpStream::connect` / `read` / `write`,
`TcpListener::bind` / `accept`, `TlsConnector::connect`, `TlsAcceptor::accept`, `TlsStream`'s `Read` /
`Write` impls, and `context.cryo::drive_handshake`. Definition sites: `stdlib/net/socket/tcp.cryo` (44),
`stdlib/net/tls/context.cryo` (20), `stdlib/net/tls/stream.cryo` (6).
**Note `io::Read` / `io::Write` themselves are NOT going away** — files, cursors and stdio still use them.
Only the socket/TLS implementations of them do.

This is a one-way API break: every `HttpClient::get`-shaped call becomes `async`, and every test that calls
one moves onto an `Executor`. **Expect the test tree to change as much as the stdlib** — Jake has
explicitly accepted that.

Slice it so each layer plus its tests is one complete green increment. Anything you port needs a test that
would fail if the port were wrong: the async socket layer had **zero tests and zero consumers** before
2026-07-25, and the "30/30 on real Windows" validation was a scratch probe that did not survive its
session. Do not let that recur.

Known constraint while porting: a value rebuilt after an `await` must be assigned at the **top level** of
that step, not inside a branch (clear `E0600` if you get it wrong).

---

## 6. Open items and known bugs — repros included, none fixed

**6a is the one most likely to block a bidirectional layer. Fix each properly when the port produces a
real repro** — a repro from the actual call site is worth more than a synthetic one.

### 6a. `Futures::join` + `Executor` + two cooperating connections → `Option::unwrap` on None

A `TcpConn` "flood" test (N length-prefixed records written without reading, receiver reading them one at
a time, then an ack in the reverse direction) panics with **`called Option::unwrap() on a None value`**.
The test was **removed rather than left red or weakened**; rebuild it as your repro.

Ruled out already:
- The panic can only be `combinator.cryo:117-118` (`Join`'s `a_out.take().unwrap()` /
  `b_out.take().unwrap()`). Every other reachable `unwrap()` is guarded by an explicit range/`is_ok` panic
  with a *different* message, or is not in this path.
- **Not volume or compaction** — reproduces identically at 3 records and at 400.
- **Not `Futures::join` with staggered completion in general** — a no-socket probe joining two futures that
  finish 8 polls apart returns the correct pair under `future::block_on`.
- The distinguishing factor is the real **multi-threaded `Executor`**. Both `Join` fields are written in
  the same `poll` call that reads them back, so **a lost write across polls is the shape to look at
  first**, and `Join<A,B,OA,OB>` is a generic struct — compare against the generic-owner receiver-refresh
  bug, which was exactly "writes through the receiver silently lost on a generic owner".
- Also express it with `Executor::spawn` + `JoinHandle::join`, which is how a real server writes it and
  sidesteps `Join` entirely. Doing both is the fastest way to localize whether the fault is in `Join` or
  in the connection.

### 6b. `Executor::block_on` mis-codegens with both a unit and a non-unit `Output`

One program instantiating `ex.block_on` at BOTH a unit `Output` and a non-unit one, **unit first**, emits
`%calltmp = call void @…` — a void call given a name, which LLVM rejects. Order-dependent; i32-first
compiles. The free-function `future::block_on` is unaffected. Pre-existing.

```cryo
async function work() -> void { const _z: i64 = await PendingThenReady<i64>::new(1, 7); }
async function val()  -> i32  { const z: i64 = await PendingThenReady<i64>::new(1, 7); return z as i32; }
function main() -> i32 {
    mut ex: Executor = Executor::new();
    ex.block_on(work());                       // unit FIRST -> E0900
    const r: i32 = ex.block_on(val());
    return r - 7;
}
```

The call-site guard that should catch it is `call_emitter.cryo` ~1189-1214 (it already keys `is_void` off
the source-level return kind for exactly this reason), so the interesting question is why
`node.resolved_type` is not `Unit` for the first call when a sibling specialization exists. Smells like the
sibling-spec leak class. **Expect this the moment a server harness spawns both unit-returning and
value-returning tasks.**

### 6c. A zero-sized type as a `Future`'s `Output` payload MISCOMPILES

`Poll<Result<T, EmptyStruct>>` hands back a garbage `T` and a misread discriminant. Unit `()` is FINE, so
the asymmetry between unit and a user-declared empty struct is probably where the fix lives. **This is why
`combinator::Elapsed` carries a `deadline: i64` field — do not "simplify" it away.** **Relevant to §5:** a
ported API returning `Result<(), SomeEmptyError>` from a future will hit this. `IoError` has fields, so the
common path is safe.

### 6d. `try_join` is not shipped

A generic static whose return type mentions the future params AND params reachable only by destructuring a
nested generic in the bound leaves the nested ones abstract. Minimal repro in `ASYNC_IMPL.md`.
`Futures::join` over two `Result` futures is the documented stand-in; what it does not give is cancelling
the sibling on first error.

### 6e. `--panic=unwind` does not link on Windows at all

`__cryo_panic`, `__cryo_panic_finish`, `__cryo_personality_v0` all undefined — **confirmed against a plain
non-async `main`**, so it is Track 4 (Win SEH) still being open, not anything async. The `async fn main`
unwind wrapper path is therefore unverified on Windows; verify it on Linux instead.

### 6f. One `await`-shape sub-case rejected rather than lowered (deliberate)

A `match` with an awaiting arm guard cannot bind an OWNING payload out of its subject: a scalar binding
copies and leaves the subject whole, an owning one moves it, and the chain re-matches that same subject on
the next test. The diagnostic names the rewrite (bind inside the arm body with a nested `match`); negative
test `E0600_async_guard_moves_owning_payload`.

---

## 7. Pitfalls paid for in blood (do not rediscover)

- **In an `async` function, NEVER call `.drop()` explicitly on a local.** Auto-drop still fires and you get
  `E0452` "use of moved value" pointing at the *function* line, not the drop. The same explicit-drop idiom
  is fine and idiomatic in a SYNC function (`ws/frame.cryo` does `payload.drop(); return Result::Err(e);`).
  **Sync: explicit drop on an early-error path is fine. Async: let auto-drop do all of it.**
- **`Str::new(<c-string literal>)` measures with `strlen`, so a literal cannot carry binary data.** A
  `"\x00\x05hello"` fixture silently becomes EMPTY and the test fails somewhere far away. Build
  length-prefixed or binary fixtures byte-by-byte into an `Array<u8>`. This WILL bite you writing HTTP/2
  and WebSocket frame fixtures — both are binary protocols.
- A `private` inherent method is **not reachable from a separate `implement trait … for …` block** on the
  same type (E0353). Drop the modifier and say in the comment that it is internal.
- `codegen/visit/ir_generator.cryo`'s `visit(AwaitExprNode*)` hard-error is a deliberate backstop — a
  surviving `AwaitExprNode` means the lowering did not rewrite it. Do not delete it.
- If you add an expression kind to the hoisting pass, **add it to `expr_await_count` in the same edit.** An
  uncounted `await` sends the function down the *no-await* path and codegen meets it with no state to
  resume. Same for `stmt_await_count`, `has_free_edge`, `rewrite_returns_expr`, `name_read_in_expr`,
  `subst_name_expr`, `frame_addr_root_expr`, `rn_expr`, `mark_last_use_expr` and every other walker in
  `async_lower.cryo`. **`TryExpression` has now been the missing kind in TEN of them** — grep `try_live`,
  and when you add a walker, add its `?` arm in the same edit.
- **There is exactly ONE sub-future stash/poll site** (`lower_carrier_sm`). Every `await` shape funnels
  through it; that is why the receiver refresh is single-point. Keep it that way.
- **`promote_cross_state` runs AFTER the state blocks are built**, so nodes you add during lowering are
  promoted and alpha-renamed along with the originals. Load-bearing for the receiver refresh.
- **An aggregate parameter is carried in an `Option` field: taken on state entry, handed back on the way
  out.** A state that GIVES the value away must not hand it back — that decision is
  `needs_handback` → `last_use_consumes` → `mark_last_use_expr`, and getting it wrong is a double drop.
- A warning emitted against generated code is a **bug report about the generator**, not noise — every
  synthesized node carries the async function's own span. `FunctionDeclNode.is_synthesized_body` is the
  correct fix when the body postdates name resolution; do not silence it another way.
- **An `ASTCloner` clone keeps annotations and DROPS every `resolved_type`.** A synthesized node carrying
  only a `resolved_type` loses its type at specialization and has it re-inferred — and an instantiation
  minted by that inference never enters the monomorphizer's worklist. When you need a copy of a *resolved*
  expression, rebuild it structurally (see `rebuild_place`).
- **The cloner's "leaf elements are immutable parse metadata" assumption holds only for leaves nothing
  rewrites.** It was false for `PatternElement::Binding` and cost a session.
- **Sema walks each module TWICE.** Anything that re-resolves a node must be idempotent, and anything whose
  legality depends on the enclosing function must remember that `lower` MOVES async bodies into the
  generated `poll` between the two walks.
- **A generic future's type is an `InstantiatedType`, not a `Struct`, during sema, and its fields are
  empty** — specialization fills them later. Any sema-time "does this future have field X" question must
  resolve to the template via `arena.inst_generic_base` first. Likewise a monomorphized `Poll<T>` is a
  plain enum with nothing to peel — read its payload off the `Ready` variant.
- **Whether a body is walked SYMBOLICALLY is decided by whether its impl block is generic.** A body walked
  concretely while an abstract projection is still in it fails every check against that projection instead
  of deferring to monomorphization. That was defect 1 above; if you see `expected X, found S::…`, ask
  first whether the enclosing thing was declared generic at all.
- Anything the lowering builds for a generic future must run under `arena.set_symbolic_no_demand(true)`,
  **restored on every exit path**.
- **`AsyncLower::type_ann_for(ty)` is the general mechanism for ANY new synthesized type.** `make_type_ann`
  pre-resolves, and a pre-resolved annotation is **invisible to monomorphization**.
- **Never synthesize an uninitialized DROPPABLE local** — except where both branches assign it.
- **A method's callers resolve through the NODE, not the declaration index.**
- **The receiver rewrite must run FIRST in `lower`,** before the poll body is built.
- **A keyword may legally NAME a method** (`as<T>()`, `type()`), so `at_method_modifier()` treats
  `static`/`async` as modifiers only when the next token is neither `(` nor `<`.
- `Option::unwrap` is `![sink]`-annotated, so `opt.take().unwrap()` is safe for an owning payload.
- **Case-mapping in the compiler must not use `c - 32`.** `CharType` is 4 bytes here while the stdlib
  treats a char as 1 (a known unresolved contradiction); use explicit alphabet tables.
- Never blind `git stash pop`. There is a **pre-existing stash from `main`**
  (`increment1-static-owner-stash`) that is not yours — leave it.
- `sed -i` through Git Bash **strips CR on CRLF files**. Repo policy (`.gitattributes`) is LF everywhere
  including the working tree, so this is mostly historical — but use the Edit tool or Python with
  `newline=''` regardless, and check `git diff --stat`: a whole-file rewrite means you flipped endings.
- Anchors drift; symbols are durable. Re-grep rather than trusting line numbers in this file.
- **Jake edits the tree while you work** — including repinning and committing mid-session. Check
  `git status` and `git log` before assuming a stray diff (or a vanished one) is yours, and never revert
  one.

---

## 8. Definition of done

Async is **100% complete** when every socket consumer runs on the async traits and the blocking surface is
gone (§4, §5), with permanent tests for each converted layer, and:

- `make test` OVERALL PASS (or the §2 piecewise equivalent: 1798 unit + 159 compile-fail + 12 projects);
- **two** `FIXED POINT OK` from `make selfhost-check`;
- a correct pin-delta measurement (§2 — **check the file counts and normalize `@FILE.str`**), and a repin
  only if it is non-zero (**a stdlib-only port should be zero**);
- `tests/test-roster.txt` regenerated **by merge, not `--update`**, with a golden-only count of 0.

The items in §6 are **not** blockers for that, but each is real — surface them, don't bury them.

Update `ASYNC_IMPL.md` as work lands: Status Dashboard + append to §9 Progress Log. Leave the tree clean
and ready for Jake to commit — and tell him plainly what is done, what is not, and what you chose not to
do.
