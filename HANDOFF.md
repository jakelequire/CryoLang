# HANDOFF — finish the carried-local drop-flag collision, then resume the socket port

**Your mission, in one line:** several state blocks of one `async` function can bind the same carried
local under the same name; codegen gives that name ONE drop flag; the states own SEPARATE storage; the
same heap block gets released twice. **Half of this is fixed. Fix the other half.**

Everything you need to reproduce it in 40 seconds is in §2. The diagnosis is done — §1 tells you exactly
where the collision is made and exactly why the obvious fix does not work on the remaining half. Do not
re-derive it.

Jake's words, still standing:

> *"I want to tackle the bugs that are coming up and fix the root of the issue and **not** work around it.
> I really want this feature to be very sound and complete."*
>
> *"Make sure that this next agent is doing better testing to really stress test these things, and if there
> is a failing test, that is okay, because that just shows the gaps that need to be filled, not worked
> around to get an unstable green."*

**A red test is a finding, not a failure.** Do not delete, narrow, `![ignore]`, or weaken a test to get a
green gate. If you cannot fix the root, leave the test red and say so plainly.

---

## 0. Baseline — all of this was verified, not assumed

- Branch `ll-impl`, HEAD **`40f9dcfd`** ("fix: resolve double free issues in async state handling and
  improve local variable binding"). **Tree CLEAN.**
- `make verify-pin` → **OK**. Both sidecars read `48a4c50d-dirty` as a matched pair. That is the normal
  pattern, not a stale pin: the pin was taken from a dirty tree that already contained the fix, and Jake
  committed it afterwards as `40f9dcfd`.
- **The pin really does carry the fix** — checked directly by compiling the minimal repro with
  `bin/cryo.exe` (not the freshly built compiler) and getting the correct answer. Do this again after any
  repin; a sidecar name proves nothing about behaviour.
- `make test` on Windows: **unit 1853 passed / 0 failed**, **compile-fail 159**, **projects 12**.
  Expect **1852** unit on Linux — `ProcessCommand::output_large_stderr_no_deadlock_win` is Windows-only,
  so plain `roster-check` reports 1 missing there. PRE-EXISTING and correct.
- Roster golden `tests/test-roster.txt` = **1853** entries.
- `make selfhost-check` was run by Jake for this checkpoint.

Sanity-check the async substrate before changing anything:

```
make test ARGS="AsyncBranchOwningLocal"   # 2   <- the one this handoff is about
make test ARGS="AsyncStress"              # 35
make test ARGS="NetHttpServer"            # 4
make test ARGS="Conn"                     # 12
make test ARGS="IoAsyncTraits"            # 10
make test ARGS="FutureExecutor"           # 5
```

---

## 1. THE BUG

### 1.1 The mechanism, exactly

`compiler/src/compiler/passes/drop_insertion.cryo:2375`:

```cryo
/// Register a binding -> flag mapping (idempotent on the binding
/// key; the first flag wins).
register_drop_flag(mut &this, binding: SymbolStr, flag: SymbolStr) -> void {
    ...
    for (mut i: i64 = 0; i < this.flag_binding_keys.length; i++) {
        if (this.flag_binding_keys[i] == binding.id) { return; }   // <-- keyed by NAME
    }
```

A local that may or may not be initialized on a given path gets a `<name>__dropflag<seq>` boolean, and
the mapping is keyed by the interned **name**. `fresh_drop_flag_name` does hand out a unique `seq` each
time, so the flags themselves are not the problem — `register_drop_flag` refusing to make a second one
for the same name is.

Now the async side. An `async fn` lowers to a `poll` whose state blocks all live in ONE function, and a
value carried across a suspend is bound in more than one of them:

- a state that **produces** the value binds it via `decl_at_first_assignment`
  (`sema/async_lower.cryo:3617`) — its first touch is a top-level write, so the write becomes a
  declaration;
- a state that **takes** it binds it via `prepend_agg_take` (`:3703`) — `mut x = this.<field>.take().unwrap()`.

Before the fix, every one of those bound the SAME alpha-renamed name. So N state blocks owned N separate
allocas and shared ONE drop flag. A flag raised where one state initialized its copy then enabled the
scope-exit drop in a state that never did.

**The arithmetic is the tell, and it is why an `if`/`else` is the shape that breaks:** one such state is
fine, two survive, three — the entry state plus one per arm — release the same block twice.

### 1.2 What is already fixed (do not redo)

`decl_at_first_assignment` now mints a fresh name per declaring state, rewrites that state onto it with
`subst_name_stmt` under `subst_to_local`, and **returns** the name so the caller hands the value back
under it. Both call sites (`carry_params` :1718, `promote_cross_state` :4247) thread the returned name
into `agg_store_stmt`.

`tests/tests/stdlib/async_branch_owning_local.cryo` was red for this and is now green.

### 1.3 What is NOT fixed — your job

**The taking side.** Every state that opens with `mut x = this.<field>.take().unwrap()` still declares
`x` under the shared name (`prepend_agg_take`, `sema/async_lower.cryo:3703`; call sites at :1713 and
:4240).

**I tried the obvious fix — minting a fresh name per taking state — and it does not work. Do not spend
the session rediscovering that.** The state blocks **SHARE AST STATEMENT NODES**: a loop body reached
from several states is ONE node list. Renaming per state rewrites a node that another state also reads,
so the second state's take declares a name nothing refers to while the shared statements name a binding
that is not in their scope.

Evidence, so you can trust it: tracing the rebinds in `ass_try_in_loop` showed blocks 2/3/4 of one
function handed `c$L10` / `c$L11` / `c$L12` over shared statements, and the build failed with
`error[E0201]: cannot find value c$L10 in this scope`. That change is reverted; a note recording it sits
directly above `prepend_agg_take`.

### 1.4 Two ways out, neither attempted

**(a) Key drop flags per DECLARATION instead of by name** — `drop_insertion.cryo:2375`. This is the
smaller and more principled change: it fixes the class at its root, needs no renaming in `async_lower`
at all, and would likely let `decl_at_first_assignment`'s renaming be reverted too (keeping it is
harmless either way). The work is finding a per-declaration key — `VarDeclNode` identity is the obvious
candidate — and checking every consumer of `flag_binding_keys` still resolves the right flag when one
name maps to several. **Start here.**

**(b) De-share the statement nodes across state blocks** — clone a loop body per state that needs its
own binding. Bigger, touches the state-machine construction, and risks changing what
`promote_cross_state` sees. Only if (a) turns out to be unworkable.

⚠️ **If you find a third way, or (a) is bigger than it looks, ASK JAKE** rather than picking for him. He
answers fast and picks the thorough option.

### 1.5 What lands when you fix it

`HttpServer::with_read_timeout` is currently **stored and NOT enforced** — the server has no slow-loris
defence, and `stdlib/net/http/server.cryo` says so at the top of the module and on the setter. Enforcing
it wraps the per-request read in `Futures::timeout`, which writes the framed request into a local from
inside a branch and trips exactly the remaining half.

So, after the fix:

1. Restore the timeout in `serve_connection` (`server.cryo`) — the `mut got: Result<Request, IoError>` +
   `if (this.read_timeout_secs > 0) { … Futures::timeout … } else { … }` shape. I had this written and
   working against the deadline path; it failed only on the taking-side collision.
2. Undo the two "NOT ENFORCED" doc blocks in `server.cryo`.
3. Re-add `server_read_timeout_closes_an_idle_connection` to `tests/tests/stdlib/net_http_server.cryo` —
   the file has a comment where it belongs, saying why it is absent. Assert `ms >= 900`, so a server that
   closes immediately cannot pass.
4. Update the last paragraph of `async_branch_owning_local.cryo`'s module doc, which currently says only
   the producing half is fixed.

---

## 2. Reproduce it in 40 seconds

The minimal repro is three functions, no loop, no receiver, no method, no combinator. **A / C pass, B
crashes** with heap corruption (`0xC0000374` on Windows):

```cryo
async function give() -> Result<String, i64> {          // owning payload across a suspend
    await Sleep::new(Duration::from_millis(2u64));
    mut out: String = String::from_str(Str::new("owned"));
    out.push_byte(0x42u8);
    return Result::Ok(out);
}

async function b_branch(flag: boolean) -> u64 {
    mut got: Result<String, i64> = Result::Err(0);
    if (flag) { got = await give(); } else { got = await give(); }   // BOTH arms write it
    mut n: u64 = 0;
    match (got) {
        Result::Ok(s)  => { mut owned: String = s; n = owned.length(); }
        Result::Err(_) => { }
    }
    return n + churn();     // churn() = 200 small alloc/free, to surface the double free
}
```

`a_flat` (same body, no branch) and `c_one_arm` (branch, only ONE arm awaits) are the controls — they
pass, which is what identifies the BRANCH rather than the loop, the await, or the owning payload.

**That producing-side case is FIXED.** For the taking side you want a function where SEVERAL states take
the same carried value — `ass_try_in_loop` in `tests/tests/stdlib/async_stress_shapes.cryo` is the
in-tree example (a carried owning receiver awaited with `?` inside a loop), and the server with its read
timeout restored is the realistic one.

### The diagnostic that actually found this

Read the IR; do not reason about the passes.

```
cd <probe> && rm -rf build && cryo build --emit-llvm
# then, per poll function:
#   count  alloca  for the local          vs
#   count  load i1, ptr ...__dropflag     sites
```

Control **3 allocas / 1 drop site**, one-arm branch **4 / 2**, both-arms branch **5 / 3** — drop sites
scaling with the number of binding states while the flag count stays at ONE. That is the whole bug in two
numbers.

⚠️ **Bound each `define` first.** An `awk` window that spills into the next function makes it look like
one variable became two locals — that mistake cost a round trip and produced a confident wrong
conclusion. Get the ranges with:

```
grep -n "^define\|^}" x.ll | awk '/define/{d=$0} /^[0-9]+:}/{print d" ENDS "$1}'
```

---

## 3. Jake's standing rules (mirror exactly)

1. **Root cause, never a workaround.** If a shape is broken, fix the shape. Do not special-case a call
   site, do not narrow a test, do not add an error telling the user to write it differently unless the
   restriction is genuinely principled and Jake has agreed to it.
2. **Only Jake commits.** Never `git commit`, never co-author. You may pin.
3. **Repin BOTH OSes** with plain `make pin` — **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `make verify-pin` and compare the two `git-describe:` lines as a **matched pair**. Then prove the
   pinned binary behaves, by compiling a repro with `bin/cryo.exe` itself.
4. **If you cannot make a change work, REVERT it** rather than leaving an unverified change in the tree,
   and report the diagnosis. (That is what happened to the taking-side rename; §1.3 is its report.)
5. **Comments describe the logic** — the invariant and the failure mode it prevents. No dated/phase/audit
   labels in code. This file and `ASYNC_IMPL.md` are the exception.
6. Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
   over type-suffixed names; bare integer literals (`1`, not `1u32`); pass owning aggregates BY POINTER.
7. **When a decision has two defensible answers, ASK Jake** (use the question tool) — for
   language-semantics and soundness-contract calls.
8. **Keep shell commands simple.** One command, one line. No `until … do sleep …; done` poll loops, no
   backgrounded gates with log tailing, no inline heredocs driving a build. This is a real complaint:
   *"you always do some crazy ass shell with these commands which makes it take longer or hangs the shell."*
9. **Offer the three long gates** (`make test`, `make selfhost-check`, `make pin`) and say what you are
   about to run first. Jake is happy for you to run them yourself; keep the commands plain.

---

## 4. Build / gate recipe

```
make cryo            # ~2m. `make test` does NOT rebuild the compiler.
make stdlib          # ~15s
make test            # ~12 min
make test ARGS="<substring>"   # plain substring filter -- your fast inner loop
make selfhost-check  # ~15 min; needs exit 0 AND *TWO* `FIXED POINT OK` (one per platform)
python scripts/roster-check.py compiler/build/cryo.exe --merge   # when ADDING tests
make pin             # plain; then: make verify-pin
```

**Windows**: run `make` from **PowerShell** with `$env:CRYO_CC='gcc'`. `make selfhost-check` and
`make pin` delegate to WSL and do BOTH OSes. **Linux**: plain `make` from bash. **Serial only — never two
heavy builds at once**, and **never edit compiler or stdlib sources while a gate is running** (markdown
is fine).

### THE BOOTSTRAP LANDMINE — you WILL hit this

`make stdlib` builds via the **PINNED** `bin/cryo`, and `make cryo` depends on `make stdlib`. So the
moment you write stdlib code that needs your new compiler fix, `make cryo` fails building the stdlib with
the OLD compiler. The ritual:

1. `git diff HEAD -- stdlib/ > /tmp/patch`, plus copy untracked new files aside.
2. `git restore --source=HEAD --worktree -- stdlib/`
3. `make cryo` — compiler now has the fix, built against the old stdlib.
4. `git apply /tmp/patch`, restore the untracked files, `make cryo` again.
5. **A repin is owed afterward**, because the compiler binary links the stdlib archive.

This is also why you gate the COMPILER change alone (stdlib at HEAD) before pinning: it tells you the fix
is clean independently of whatever stdlib work depends on it.

### Gate holes that have bitten

- `selfhost-check` exit 0 is **not** sufficient — require `FIXED POINT OK` **count == 2** (Linux + native
  Windows). On Windows read it with PowerShell `Select-String`; the tee log can be UTF-16.
- `selfhost-check` **deletes `compiler/build/cryo.exe`**, and `make pin` needs it. Run `make cryo`
  between them. (`bin/cryo.exe`, the pin itself, is untouched.)
- **`make pin` writes its two halves minutes apart.** A `git status` in that window shows only `bin/cryo`
  modified and looks exactly like the forbidden linux-only repin — re-check before acting.
- **THE ROSTER IS PLATFORM-SENSITIVE.** Use **`--merge`** when ADDING tests; `--update` only when
  deliberately REMOVING them (it rewrites the golden from THIS host and drops the other platform's
  entries). Then check `git diff --numstat tests/test-roster.txt` shows `N 0`; a whole-file rewrite means
  you flipped line endings.

---

## 5. What else is outstanding

The socket port (`ASYNC_IMPL.md` row `5b-port`) is mid-flight. **Increments 1-3 are done**: the async IO
traits, one generic `BufStream<S>` over an `AsyncTransport` seam, HTTP/1.1 framing as a cross-module
inherent impl on `BufStream<S>`, `Client::get`/`post` async, and `net/http/server.cryo` on the async
transport (`async run` / `run_on(listener)` / `serve_connection(conn)` taking the connection BY VALUE).

Still to port, in order:

| file                               | lines | note                                                              |
| ---------------------------------- | ----- | ----------------------------------------------------------------- |
| `stdlib/net/https.cryo`            | 125   | re-target onto `BufStream<TlsStream>`; then delete `client::send_over` |
| `stdlib/net/ws/conn.cryo`          | 403   | **BORROWS its transport — must become owning**                    |
| `stdlib/net/http2/client.cryo`     | 78    |                                                                   |
| `stdlib/net/http2/server.cryo`     | 70    |                                                                   |
| `stdlib/net/http2/connection.cryo` | 855   | the big one; **BORROWS its transport**; the only real framing state |

Then delete the blocking surface: `TcpStream::connect`, the `Read`/`Write` impls for `TcpStream` and
`TlsStream`, `TcpListener::accept` (**KEEP `bind`** — Jake ruled; it is non-blocking setup and
`TcpAccept::start` needs a listener to exist), `TlsConnector::connect`, `TlsAcceptor::accept`, and
`drive_handshake`. Plus the transitional duplicates `Headers/Request/Response::write_to`,
`Request/Response::parse`, `request::read_line`, `client::send_over`.

**Settled design calls — do not relitigate:**

- **`BufStream<S>`** is the one connection type. There is exactly one and there must stay exactly one.
- **Protocol state ⇒ owning wrapper; stateless framing ⇒ inherent impl.** `WebSocket<S>` has
  `is_client`/`closed`, so it OWNS a `BufStream<S>` (Jake's ruling — this is also what lets the
  byte-at-a-time handshake read die, since post-upgrade bytes already in `rbuf` survive the handover).
  HTTP/1.1 has no per-connection state, so `read_request` is an inherent impl on `BufStream<S>`.
- **`pending()` + `flush()` stays** — zero-copy encode straight into the connection's buffer.
- **NO MIRRORS**: convert in place, never an `_async` twin.
- TLS `connect_async`/`accept_async` become real `async connect`/`accept` **in the same increment the
  blocking twins die** (increment: https), so no twin ever coexists with the real one.

### Other open compiler items

- **Transitive receiver refresh does not reach generic contexts.** `ctor_field_for_param` needs
  `CallExprNode.resolved_template`, which sema leaves unset for a generic static called from a body
  walked SYMBOLICALLY — so `await Futures::timeout(d, this.read_request())` inside
  `implement struct BufStream<S>` is REJECTED (E0455) rather than refreshed. Loud, not silent. Fix:
  resolve the callee from its `ScopeResolution` spelling when `resolved_template` is empty.
- **`this` in an if-EXPRESSION initializer** inside an `async` method resolves to the generated Future
  (E0204 naming `…$Future_N`); the receiver rewrite does not reach that position. Statement-level `if`
  works.
- `try_join` still not shipped; `--panic=unwind` still does not link on Windows. Both pre-date this work.

---

## 6. Pitfalls paid for in blood

- **In an `async` function, NEVER call `.drop()` explicitly on a local.** Auto-drop still fires and you
  get `E0452` pointing at the *function* line. Sync: explicit drop on an early-error path is fine.
- **A pipe swallows a hung probe's output.** `timeout 15 ./probe | tail` printed NOTHING while
  `> out.log` captured every marker. **Redirect to a FILE when diagnosing a hang.**
- **`--ast` prints identifier names BLANK**, so it is useless for this class of bug. `--emit-llvm` is the
  tool. `cdebug(...)` (`import Utils::Logger;`, `--debug`-gated) is the only reliable way to see what a
  sema pass decided — unique tag, redirect to a file, and **remove every one before gating**.
- **If you add an expression or statement kind to any walker in `async_lower.cryo`, add it to ALL of
  them.** `TryExpression` has been the missing kind in TEN of them.
- **Order matters as much as coverage in `async_lower.cryo`.** Anything that ASKS a question about user
  code must ask it before the pass injects synthesized nodes into that code. `needs_handback` is read
  BEFORE any store is appended for exactly this reason.
- **A predicate answered from a TYPE can be silently unanswerable.** A generic future arrives as an
  `InstantiatedType` whose template has ZERO fields at lowering time. When a lowering decision depends on
  a type specialization has not filled in, get it from the DECLARATION.
- **`Str::new(<c-string literal>)` measures with `strlen`**, so a literal cannot carry binary data — a
  `"\x00\x05hello"` fixture silently becomes EMPTY. Build binary fixtures byte-by-byte into an
  `Array<u8>`. **This will bite in the ws/http2 port**, whose frame headers are full of NULs.
- **Moving a field out of a tuple that owns a destructor is E0453.** A helper returning
  `(TcpListener, u16)` cannot have `.0` moved out; return the listener whole and read the port separately.
- **`Executor` has a `Drop` impl** and `Executor::drop` CANCELS a task still parked on a deadline, so a
  test that spawns work must `join` first. `future::block_on` has **no reactor**
  (`Reactor::current()` is null off an executor worker), so `Sleep` and socket futures need an
  `Executor`.
- **`Futures::join` completes with a TUPLE, not a `Pair`.** Bind as `(A, B)`.
- `block_on(f()) as i32` mis-binds `R` and reports a confusing mismatch **inside
  `stdlib/future/_module.cryo`**. Bind to a typed local first, and do NOT turbofish it.
- **`bin/cryolsp` is a STALE build** predating async trait-method parsing, so the editor reports bogus
  parse errors on known-good `async` trait methods. The compiler is authoritative; `make lsp` clears it.
- **Jake edits the tree while you work** — including repinning and committing mid-session. Check
  `git status` and `git log` before assuming a stray diff is yours, and never revert one.

---

## 7. Definition of done

- Taking states no longer share a drop flag, by whichever of §1.4 you and Jake settle on.
- `make test` green with the numbers in §0 — **and** `HttpServer::with_read_timeout` restored, its two
  "NOT ENFORCED" doc blocks removed, and `server_read_timeout_closes_an_idle_connection` back in the tree
  asserting `ms >= 900`.
- A test that pins the TAKING-side collision directly (several states taking one carried value), added
  next to `async_branch_owning_local`, with a control that differs by one thing.
- **two** `FIXED POINT OK` from `make selfhost-check`, a repin, `make verify-pin` OK, and the pinned
  binary proven by compiling a repro with `bin/cryo.exe` itself.
- `tests/test-roster.txt` regenerated with `--merge`, `git diff --numstat` showing `N 0`.
- `ASYNC_IMPL.md` updated: the `5b-port` dashboard row and a new §9 entry.

Leave the tree clean and tell Jake plainly what is done, what is not, and what you chose not to do.
