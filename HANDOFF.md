# HANDOFF — fix the async-lowering blocker, then finish the socket port

**Your mission, in one line:** an `async` method on a GENERIC struct whose suspensions are trait
DEFAULT methods called on `this` miscompiles — the carried-value hand-back is lost and a later state
takes an empty carrier (`unwrap() on a None value`). That blocks the last two consumers of the socket
port. **Fix the compiler first. Do not start `net/http2` before it is fixed.**

§2 has everything needed to put the failing code back in front of you. §1 says exactly what is proven
and what is still inference — do not treat the inference as settled, because narrowing it is your first
real task.

Jake's words, still standing:

> *"I want to tackle the bugs that are coming up and fix the root of the issue and **not** work around
> it. I really want this feature to be very sound and complete."*
>
> *"Make sure that this next agent is doing better testing to really stress test these things, and if
> there is a failing test, that is okay, because that just shows the gaps that need to be filled, not
> worked around to get an unstable green."*

**A red test is a finding, not a failure.** Do not delete, narrow, `![ignore]`, or weaken a test to get a
green gate. If you cannot fix the root, leave the finding written down and say so plainly.

---

## 0. Baseline — verified, not assumed

- Branch `ll-impl`, HEAD **`da2a7e75`**. **Tree CLEAN** except this file, which is untracked on purpose.
- Recent history (all landed this session):
  - `1ea2b2a0` — drop flags keyed per DECLARATION, not by name; `HttpServer::with_read_timeout` enforced. **Compiler change + repin.**
  - `392e4cbb` — async HTTPS over `BufStream<TlsStream>`; blocking TLS entry points deleted. **stdlib only.**
  - `da2a7e75` — the findings below, written into `ASYNC_IMPL.md`. **Docs only.**
- `make test` on Windows at `392e4cbb`: **unit 1855 / 0 failed**, **compile-fail 159**, **projects 12**.
  Expect **1854** on Linux — `ProcessCommand::output_large_stderr_no_deadlock_win` is Windows-only.
  `da2a7e75` is documentation, so the numbers still hold.
- Roster golden `tests/test-roster.txt` = **1855** entries.
- `make selfhost-check` at `1ea2b2a0`: **two `FIXED POINT OK`** (Linux + native Windows).
- `make verify-pin` → **OK**.
  ⚠️ Both sidecars read `git-describe: 40f9dcfd-dirty`. **That is correct, not stale.** The pin was taken
  from a dirty tree that already contained the `1ea2b2a0` changes, and the commit was made afterwards —
  the same pattern the previous handoff described. Nothing since has touched `compiler/`, so
  `bin/cryo{,.exe}` still corresponds to the compiler source at HEAD. Verify behaviour, never a name.

Sanity-check the async substrate before changing anything:

```
make test ARGS="AsyncBranchOwningLocal"   # 4
make test ARGS="AsyncStress"              # 35
make test ARGS="NetHttpServer"            # 5
make test ARGS="NetWs"                    # 2
make test ARGS="NetHttps"                 # 1
make test ARGS="NetTls"                   # 2
make test ARGS="Conn"                     # 12
make test ARGS="IoAsyncTraits"            # 10
```

---

## 1. THE BUG

### 1.1 What is PROVEN

`BufStream<S>::read_frame`, written as an `async` inherent impl on the generic connection and issuing
several `await this.read_exact(n)` calls, **compiles and then panics at runtime**:

```
panic: called Option::unwrap() on a None value
  at stdlib/core/option.cryo:63
```

That `unwrap` is the async lowering's `this.<field>.take().unwrap()` — a state took a carried value out
of its carrier field and found it empty. Something on an earlier path failed to hand the value back.

Isolated, in this order, each step ruling one thing out:

- **Not `WebSocket`.** A plain `mut c: BufStream<TcpStream>` local calling `await c.read_frame()`
  reproduces it with no wrapper type involved.
- **Not the handshake.** A probe that only did `from_upgraded` + framing (no HTTP upgrade) still panicked.
- **Not the write side.** `send_text` — `encode_into(this.conn.pending(), …)` then
  `await this.conn.flush()` — works; the server received a well-formed frame and read its header.
- **Not the control flow.** All of these PASS as free `async function`s, so none of them is the trigger:
  - an owning local pre-declared and then assigned from an `await` **inside a branch**;
  - two of those in sequence;
  - `await` → `if`/`else` where **both** branches `await` → a further `await` after the join, with a
    scalar from before the branch still live afterwards.

### 1.2 What is INFERENCE — narrow this FIRST

By elimination the remaining axis is: **a method on a GENERIC owner whose suspension points are trait
DEFAULT methods invoked on `this`** (`read_exact` / `read_line` are `AsyncRead` defaults). That is the
one thing `read_frame` does which `net/http/conn.cryo`'s working `read_request` does not do the same way.

**I did not reduce this to a standalone repro, and you should not assume it is right.** My attempt hit a
DIFFERENT unsupported shape first and never tested the real one:

```cryo
// E0306: async: could not resolve an awaited future's type
mut got: Array<u8> = await this.inner.chunk(4);   // awaiting a method on a FIELD of a generic receiver
```

So when you build the reduction, the probe must call a default trait method **on `this` directly**, not
through a field. A minimal shape to aim at: a generic `Holder<S>`, a trait with a required `async` method
plus a DEFAULT `async` method built on it, an impl for `Holder<S>`, and an inherent `async` method on
`Holder<S>` that awaits the default method two or three times in a row.

If that reduction comes out green, the axis is wrong and the next candidates are: the cross-module
inherent impl (`implement struct BufStream<S>` from inside `net/ws/frame.cryo`, a third such block after
`io/buf.cryo`'s own and `net/http/conn.cryo`'s), or the interaction of several sequential awaits with the
`?` operator in that position.

### 1.3 Four more findings, all reduced

**(a) A DEFAULT trait method does not resolve on a generic-instantiated receiver.**
`c.queue<Str>(…)` where `c: BufStream<S>` inside a function generic over `S`:

```
error[E0636]: codegen: no method 'queue' found on type 'io::buf::BufStream<net::socket::tcp::TcpStream>'
```

at CODEGEN, once `S` is instantiated. `queue` is a DEFAULT method on `AsyncWrite`. The REQUIRED
`pending()` resolves fine, and the identical `queue` call works on a concrete `BufStream<TlsStream>`
outside a generic function (`tests/tests/stdlib/net_tls_conn.cryo` does it). Likely the same root as §1.2.

**(b) A scalar declared before an awaiting branch is lost after the join.**
Inside that same generic method, `const h1: u8 = …;` declared before an `if`/`else` whose branches
`await`, then read after the join, gives `error[E0201]: cannot find value h1$L11 in this scope`. Same for
a `u64` assigned inside a branch (`key_len$L22`). The `$L<n>` suffix is the alpha-rename, so the
declaration and the use disagree about the name, or sit in state blocks that cannot see each other.
Loud, not silent.

**(c) An if-EXPRESSION initializer inside an `async` method CRASHES THE COMPILER.**

```cryo
const ext_len: u64 = if (len == 126) { 2 } else { if (len == 127) { 8 } else { 0 } };
```

inside an `async` method killed the build with **heap corruption** (`make` reported exit
`-1073740940` = `0xC0000374`) *after* printing a diagnostic that had substituted an unrelated local's
name into the echoed source line — it rendered `if (fin$L11 == 126)` where the source says `len`.

This is the same position as the standing open item about `this` in an if-expression initializer
(E0204), but the symptom is a crash plus a corrupted diagnostic, not an error. **Worth fixing on its own
account, independently of the port** — a compiler that corrupts its own heap on a legal-looking
construct is worse than one that rejects the construct. Statement-level `if` is what the tree uses
elsewhere and is unaffected.

**(d) A fixed-size array carried across a suspend is unrepresentable.**
`mut key: u8[4];` live across an `await` is promoted into an `Option<u8[4]>` carrier field, and
`Option<T>` cannot hold one — `error[E0200]` inside `stdlib/core/option.cryo`, because `take()` assigns
through `*this`. Loud. Either reject the shape with a real diagnostic naming the local, or promote such
a local into a heap `Array<u8>`.

---

## 2. Putting the failing code back in front of you

**The ws port was written and then REVERTED** (Jake's rule: an unverified change does not stay in the
tree), so the repro is not in the working copy. Recreate it — it takes a few minutes.

### 2.1 Add the async frame reader

In `stdlib/net/ws/frame.cryo`, add these imports —

```cryo
import io::buf;
import future::poll;
import future::waker;
import future::traits;
```

— and append this block (the existing sync `read_frame<R>` can stay; nothing calls the new one yet):

```cryo
implement struct BufStream<S> {
    async read_frame_async(mut &this) -> Result<Frame, IoError>
    where S: AsyncTransport {
        mut header: Array<u8> = (await this.read_exact(2))?;
        const h0: u8 = array_at(&header, 0);
        const h1: u8 = array_at(&header, 1);
        if ((h0 & 0x70) != 0) { return Result::Err(IoError::new(IoErrorKind::InvalidData, 0)); }
        const fin: boolean = (h0 & 0x80) != 0;
        const masked: boolean = (h1 & 0x80) != 0;
        const opcode: OpCode = match (op_from_u8(h0 & 0x0F)) {
            Option::Some(op) => { op }
            Option::None     => { return Result::Err(IoError::new(IoErrorKind::InvalidData, 0)); }
        };
        mut len: u64 = (h1 & 0x7F) as u64;
        if (len == 126) {
            mut ext: Array<u8> = (await this.read_exact(2))?;
            len = ((array_at(&ext, 0) as u64) << 8) | (array_at(&ext, 1) as u64);
        } else {
            if (len == 127) {
                mut ext: Array<u8> = (await this.read_exact(8))?;
                len = 0;
                mut i: u64 = 0;
                while (i < 8) { len = (len << 8) | (array_at(&ext, i) as u64); i++; }
            }
        }
        if (is_control(opcode) && (!fin || len > 125)) {
            return Result::Err(IoError::new(IoErrorKind::InvalidData, 0));
        }
        if (len > MAX_PAYLOAD) { return Result::Err(IoError::new(IoErrorKind::InvalidData, 0)); }
        // Each branch is self-contained and returns, so nothing crosses a join.
        if (masked) {
            mut mask_key: Array<u8> = (await this.read_exact(4))?;
            mut mp: Array<u8> = (await this.read_exact(len))?;
            const p: u8* = mp.as_ptr();
            mut i: u64 = 0;
            while (i < len) { p[i] = p[i] ^ array_at(&mask_key, i % 4); i++; }
            return Result::Ok(Frame { fin: fin, opcode: opcode, masked: true, payload: mp });
        }
        mut payload: Array<u8> = (await this.read_exact(len))?;
        return Result::Ok(Frame { fin: fin, opcode: opcode, masked: false, payload: payload });
    }
}

function array_at(a: &Array<u8>, index: u64) -> u8 {
    match (a.get(index)) { Option::Some(b) => { return b; } Option::None => { return 0; } }
}
```

You also need a SYNC encoder to build a frame into a buffer, since the transport-driven `encode_frame`
writes through a `Write` that the async path does not have. Add:

```cryo
function encode_into(out: Array<u8>*, fin: boolean, opcode: OpCode,
                     payload: Slice<u8>, mask: boolean) -> Result<(), IoError> {
    // Byte 0: FIN + opcode. Byte 1: mask bit + 7-bit length (this probe only
    // needs the short form). Then key + masked payload, or raw payload.
    ...
}
```

— the original `encode_frame` body is the reference; replace each `frame.push(x)` with a push into
`out`, drop the staging `Array`, and drop the trailing `writer.write(...)`.

### 2.2 The probe

`cryo build probe.cryo --stdlib=<repo>/stdlib`, then run it. Server uses the new reader, client writes a
hand-encoded frame. **Redirect to a FILE** — a pipe swallows a crashing probe's output.

```cryo
async function serve(listener: TcpListener) -> i64 {
    mut acc: TcpAccepted = await TcpAccept::start(listener);
    mut lst: TcpListener = acc.take_listener();
    lst.drop();
    mut sock: TcpStream = TcpStream::from_fd(-1);
    match (acc.take_result()) { Result::Ok(s) => { sock = s; } Result::Err(_) => { return -1; } }
    mut c: BufStream<TcpStream> = BufStream<TcpStream>::of(sock);
    mut fr: Frame = match (await c.read_frame_async()) {   // <-- panics here
        Result::Ok(f)  => { f }
        Result::Err(_) => { return -3; }
    };
    return fr.payload.length() as i64;
}

async function client(port: u16) -> i64 {
    mut sock: TcpStream = match (await TcpConnect::start(SocketAddr::localhost_v4(port))) {
        Result::Ok(s) => { s } Result::Err(_) => { return -20; }
    };
    mut c: BufStream<TcpStream> = BufStream<TcpStream>::of(sock);
    encode_into(c.pending(), true, OpCode::Text, Str::new("hello ws").as_bytes(), true)?;
    (await c.flush())?;
    return 1;
}
// join the two on ONE Executor, bind port 0 + set_nonblocking(true), block_on.
```

Expect `8`; you get the `unwrap() on a None value` panic.

### 2.3 The diagnostic that works

Read the IR, do not reason about the passes. `cryo build --emit-llvm`, then per `poll` function count
`alloca`s for a local against the `take`/`unwrap` sites on its carrier field.
⚠️ **Bound each `define` first** — an `awk` window that spills into the next function produces a
confident wrong answer:

```
grep -n "^define\|^}" x.ll | awk '/define/{d=$0} /^[0-9]+:}/{print d" ENDS "$1}'
```

`cdebug(...)` (`import Utils::Logger;`, `--debug`-gated) is the only reliable way to see what a sema pass
decided — unique tag, redirect to a file, and **remove every one before gating**. `--ast` prints
identifier names BLANK and is useless for this class of bug.

---

## 3. Jake's standing rules (mirror exactly)

1. **Root cause, never a workaround.** If a shape is broken, fix the shape. Do not special-case a call
   site, do not narrow a test, do not add an error telling the user to write it differently unless the
   restriction is genuinely principled and Jake has agreed to it.
2. **Only Jake commits** — unless he says otherwise in the session. Never co-author. You may pin.
3. **Repin BOTH OSes** with plain `make pin` — **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `make verify-pin` and compare the two `git-describe:` lines as a **matched pair**. Then prove the
   pinned binary behaves, by compiling a repro with `bin/cryo.exe` itself. A sidecar name proves nothing.
4. **If you cannot make a change work, REVERT it** rather than leaving an unverified change in the tree,
   and report the diagnosis. (That is exactly what happened to the ws port; §1 is its report.)
5. **Comments describe the logic** — the invariant and the failure mode it prevents. No dated / phase /
   audit / batch labels in code. This file and `ASYNC_IMPL.md` are the exception.
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

`make selfhost-check` and `make pin` both exceed the 10-minute tool timeout. Launch each detached (a
`.cmd` that redirects to a log, via `Start-Process -WindowStyle Hidden`) and wait on the log.

### THE BOOTSTRAP LANDMINE

`make stdlib` builds via the **PINNED** `bin/cryo`, and `make cryo` depends on `make stdlib`. So the
moment you write stdlib code that needs your new compiler fix, `make cryo` fails building the stdlib with
the OLD compiler. The ritual:

1. `git diff HEAD -- stdlib/ > /tmp/patch`, plus copy untracked new files aside.
2. `git restore --source=HEAD --worktree -- stdlib/`
3. `make cryo` — compiler now has the fix, built against the old stdlib.
4. `git apply /tmp/patch`, restore the untracked files, `make cryo` again.
5. **A repin is owed afterward**, because the compiler binary links the stdlib archive.

This is also why you gate the COMPILER change alone (stdlib at HEAD) before pinning: it tells you the fix
is clean independently of whatever stdlib work depends on it. That sequencing paid off this session.

### Gate holes that have bitten

- `selfhost-check` exit 0 is **not** sufficient — require `FIXED POINT OK` **count == 2** (Linux + native
  Windows). On Windows read it with PowerShell `Select-String`; the tee log can be UTF-16.
- `selfhost-check` **deletes `compiler/build/cryo.exe`**, and `make pin` needs it. Run `make cryo`
  between them. (`bin/cryo.exe`, the pin itself, is untouched.)
- **`make pin` writes its two halves minutes apart.** A `git status` in that window shows only `bin/cryo`
  modified and looks exactly like the forbidden linux-only repin — re-check before acting.
- **THE ROSTER IS PLATFORM-SENSITIVE.** Use **`--merge`** when ADDING tests. `--update` rewrites the
  golden from THIS host and drops the other platform's entries — when REMOVING a test, delete its line by
  hand instead. Either way check `git diff --numstat tests/test-roster.txt` shows `N 0` or `0 N`; a
  whole-file rewrite means you flipped line endings.

---

## 5. What else is outstanding

The socket port (`ASYNC_IMPL.md` row `5b-port`). **Increments 1–4 are done**: the async IO traits, one
generic `BufStream<S>` over an `AsyncTransport` seam, HTTP/1.1 framing as a cross-module inherent impl,
`Client::get`/`post` async, `net/http/server.cryo` on the async transport, and `net/https.cryo` async
over `BufStream<TlsStream>` with the blocking TLS entry points deleted.

Still to port:

| file                               | lines | note                                                                |
| ---------------------------------- | ----- | ------------------------------------------------------------------- |
| `stdlib/net/ws/conn.cryo`          | 403   | **BORROWS its transport — must become owning.** BLOCKED on §1.       |
| `stdlib/net/http2/client.cryo`     | 78    |                                                                     |
| `stdlib/net/http2/server.cryo`     | 70    |                                                                     |
| `stdlib/net/http2/connection.cryo` | 855   | the big one; **BORROWS its transport**; the only real framing state |

**Do the compiler fix first.** Both remaining consumers borrow their transport, so both will meet §1 the
moment they are converted — finding it a third time by hand is wasted work.

Then delete the rest of the blocking surface: `TcpStream::connect`, the `Read`/`Write` impls for
`TcpStream` and `TlsStream`, `TcpListener::accept` (**KEEP `bind`** — Jake ruled; it is non-blocking
setup and `TcpAccept::start` needs a listener to exist). Plus the transitional duplicates
`Headers/Request/Response::write_to`, `Request/Response::parse`, `request::read_line`.
(`client::send_over`, `TlsConnector::connect`/`TlsAcceptor::accept` blocking forms, and
`drive_handshake` are already gone.)

### The ws design, already settled — do not relitigate

- **`WebSocket<S>` OWNS a `BufStream<S>`.** It carries per-connection protocol state (`is_client`,
  `closed`), so by Jake's rule (protocol state ⇒ owning wrapper, stateless framing ⇒ inherent impl) it is
  the owner, not a view. An `async` method may only re-address its OWN receiver between polls.
- **Framing is an inherent impl on `BufStream<S>`**, beside HTTP/1.1's `read_request`.
- **Encoding is SYNC into `pending()`**, flushed once — the same shape as the HTTP encoders, and it
  removes the staging `Array` the transport-driven encoder needed.
- **Owning the connection kills the byte-at-a-time handshake read.** The buffer outlives the handshake,
  so frame bytes that arrived in the same fill as the blank line survive into the first `recv`, and the
  head can be read a line at a time. **Write a test that pins exactly that** — client sends the upgrade
  request AND its first frame in ONE flush, server must still decode the frame.

### Other open compiler items (pre-existing)

- **Transitive receiver refresh does not reach generic contexts.** `ctor_field_for_param` needs
  `CallExprNode.resolved_template`, which sema leaves unset for a generic static called from a body
  walked SYMBOLICALLY. Fix: resolve the callee from its `ScopeResolution` spelling when
  `resolved_template` is empty. Loud (E0455), not silent.
- **`this` in an if-EXPRESSION initializer** inside an `async` method resolves to the generated Future
  (E0204 naming `…$Future_N`). See §1.3(c) — the same position can also crash the compiler outright.
- `try_join` still not shipped; `--panic=unwind` still does not link on Windows. Both pre-date this work.

---

## 6. Pitfalls paid for in blood

- **In an `async` function, NEVER call `.drop()` explicitly on a local that is live across a suspend.**
  Auto-drop still fires and you get `E0452` pointing at the *function* line. This bit twice in one
  session: adding an `await` to a `match` arm turned an existing local into a carried one and broke a
  `.drop()` that had been fine for months. Sync code: explicit drop on an early-error path is fine.
- **A pipe swallows a hung or crashing probe's output.** `timeout 15 ./probe | tail` printed NOTHING
  while `> out.log` captured every marker. **Redirect to a FILE when diagnosing.** `println` is buffered
  and lost on a crash — use `eprintln` for markers.
- **If you add an expression or statement kind to any walker in `async_lower.cryo`, add it to ALL of
  them.** `TryExpression` has been the missing kind in TEN of them.
- **Order matters as much as coverage in `async_lower.cryo`.** Anything that ASKS a question about user
  code must ask it before the pass injects synthesized nodes into that code. `needs_handback` is read
  BEFORE any store is appended for exactly this reason.
- **A predicate answered from a TYPE can be silently unanswerable.** A generic future arrives as an
  `InstantiatedType` whose template has ZERO fields at lowering time. When a lowering decision depends on
  something specialization has not filled in, get it from the DECLARATION.
- **`Str::new(<c-string literal>)` measures with `strlen`**, so a literal cannot carry binary data — a
  `"\x00\x05hello"` fixture silently becomes EMPTY. Build binary fixtures byte-by-byte into an
  `Array<u8>`. **This will bite in the ws/http2 port**, whose frame headers are full of NULs.
- **Moving a field out of a tuple that owns a destructor is E0453.** A helper returning
  `(TcpListener, u16)` cannot have `.0` moved out; return the listener whole and read the port separately.
- **`Executor` has a `Drop` impl** and `Executor::drop` CANCELS a task still parked on a deadline, so a
  test that spawns work must `join` first. `future::block_on` has **no reactor**
  (`Reactor::current()` is null off an executor worker), so `Sleep` and socket futures need an `Executor`.
- **`Futures::join` completes with a TUPLE, not a `Pair`.** Bind as `(A, B)`.
- `block_on(f()) as i32` mis-binds `R` and reports a confusing mismatch **inside
  `stdlib/future/_module.cryo`**. Bind to a typed local first, and do NOT turbofish it. Reusing one
  `Executor` for two `block_on` calls of different output types also mis-binds — give each its own.
- **`await <expr>.m()` where `<expr>` is a call result is E0455** (the receiver names no storage). Bind to
  a local first. This is correct behaviour, not a bug.
- **`sed -i` via Git Bash strips CR** on a CRLF file — use the Edit tool, or Python with `newline=''`.
  Always check `git diff --numstat` afterwards: `N N` means you touched N lines, a whole-file count means
  you flipped line endings.
- **Do not pass PowerShell here-strings (`@'…'@`) to the Bash tool.** It executes the body as shell. This
  session that cost an untracked file (restored) and a garbled commit message (amended). Write commit
  messages to a file with the Write tool and use `git commit -F`.
- **`bin/cryolsp` is a STALE build** predating async trait-method parsing, so the editor reports bogus
  parse errors on known-good `async` trait methods. The compiler is authoritative; `make lsp` clears it.
- **Jake edits the tree while you work** — including repinning and committing mid-session. Check
  `git status` and `git log` before assuming a stray diff is yours, and never revert one.

---

## 7. Definition of done

- The §1.2 reduction exists as a standalone probe, and the axis is either confirmed or corrected in
  writing.
- The blocker is fixed at the root in the lowering — not by restructuring the stdlib around it.
- §1.3(c) (the compiler crash on an if-expression initializer in an `async` method) is fixed or has a
  precise diagnostic, since it is cheap to trigger and severe.
- `net/ws/conn.cryo` ported on the settled design, with the pipelined-handshake test from §5.
- `make test` green with the §0 numbers plus whatever you add; roster updated with `--merge`.
- **two** `FIXED POINT OK` from `make selfhost-check`, a repin, `make verify-pin` OK, and the pinned
  binary proven by compiling a repro with `bin/cryo.exe` itself.
- `ASYNC_IMPL.md` updated: the `5b-port` dashboard row and a new §9 entry.

Leave the tree clean and tell Jake plainly what is done, what is not, and what you chose not to do.
