# HANDOFF — finish the in-place carrier, then collect the payoff

**Your mission, in one line:** teach the async lowering's name substitution to tell a **by-value**
position from a **borrow** position, so the two carrier shapes that still MOVE can live in their field
like every other one — and then delete the frame-address subsystem the moving carrier existed to police.

The previous session landed the in-place carrier for the shapes it could already express, and the
acceptance probe is green. What is left is one well-understood mechanism, not a search.

Jake's words, still standing:

> *"I want to tackle the bugs that are coming up and fix the root of the issue and **not** work around
> it. I really want this feature to be very sound and complete."*
>
> *"Make sure that this next agent is doing better testing to really stress test these things, and if
> there is a failing test, that is okay, because that just shows the gaps that need to be filled, not
> worked around to get an unstable green."*

**A red test is a finding, not a failure.** Do not delete, narrow, `![ignore]`, or weaken a test to get
a green gate. If you cannot fix the root, leave the finding written down and say so plainly.

---

## 0. Baseline — everything below is COMMITTED-READY, GATED and PINNED

HEAD `56894861`, branch `ll-impl`. **Jake commits; the work is uncommitted in the tree.**

**The pin is CURRENT and carries everything.** `make selfhost-check` gave **two** `FIXED POINT OK`
(Linux + Windows), `make pin` was taken plain (both halves), `make verify-pin` reports OK with both
sidecars a matched pair at `git-describe: 56894861-dirty`. The pinned binary was proven to **BEHAVE**,
not just to be named right — `bin/cryo.exe` itself compiles the address-stability repro to `55`/`77`
(the previous pin gave `0`), keeps a borrowed value's drop (`hash_drops=1`, previously `0`), and fires
`E0453` on `mem::drop<Res>(*p)`. After Jake commits, the sidecars will name the pre-commit describe —
that is expected, not drift; `verify-pin` checks content by sha256.

Numbers measured on **Windows**:

```
make test   ->  unit 1899 / 0 failed,  compile-fail 165,  projects 12 passed (3 skipped)
```

Counts moved from 1889 / 166 / 12: **+10 unit** (5 classification, 3 address-stability, 2 inverted
E0455s) and **compile-fail 166 → 165** (+1 new negative, −2 negatives INVERTED into positive tests).

The unit count is **platform-sensitive**: `tests/test-roster.txt` has 1899 entries, one of which
(`ProcessCommand::output_large_stderr_no_deadlock_win`) is Windows-only, so **Linux reports 1898**.
`projects` reports 12 on Windows and 14 on Linux (`ffi_cpp_link`, `native_syscalls_gate` skip here).

`ARGS=` filters **unit** tests only; `E0453_*` / `E0455_*` negatives are compile-fail cases and never
appear there.

### One environment change was made this session

`.toolchains/llvm-mingw` was provisioned (`scripts/fetch-windows-llvm.sh`, ~1 GB, gitignored) and
`unzip` was installed in WSL. **This is only needed for the wine path, which you do not need.** See §8
— the correct invocation runs the Windows chain natively.

---

## 1. What landed — all verified against BOTH compilers

### Argument classification reaches a module-qualified call

`resolve_call` had three sources for the callee's parameter types; two ask for a type-scoped name and
the third for an enum-variant payload. A scope naming a **MODULE by a SUFFIX of its namespace** — `mem`
for `std::core::mem`, `hash` for `std::core::hash`, i.e. how nearly every stdlib free function is
spelled — fell through all three, so **every argument stayed `Unclassified`**. Generic or not; the
previous handoff's "qualified *generic* free call" framing was too narrow.

`Unclassified` reads as a *transfer* to the move passes, so a value merely **lent** to such a function
was recorded as moved and its scope-exit drop suppressed. That is a **silent leak**, not just a
stood-down check:

```
hash::digest<Res>(r)     // digest<T>(value: &T) — a borrow
  pinned(old)  hash_drops=0     <- leaked        fixed  hash_drops=1
mem::drop<Res>(*p)       // drop<T>(_v: T) — by value
  pinned(old)  No errors found                   fixed  error[E0453]
```

Fixed by a fourth source, `lookup_module_qualified_param_types`, resolved through the module graph. The
module-selection walk was extracted into `resolve_module_qualified_symbol` and is **shared** with
`resolve_module_qualified_function` — classifying an argument against one module's signature while the
call resolves to another's would hand the move passes an answer for a function that is not being called.

All **16** `*this$recv` deref-arguments in the stdlib now report `BORROWED`.

### The in-place carrier

`promote_cross_state`'s aggregate branch builds the value **directly into** its `Option<T>` field; every
state, the declaring one included, opens with
`mut <nm>$p: T* = this.__agg_<ds>_<nm>.unwrap_ptr();` and reads `*<nm>$p`. No take, no hand-back, no
`store_before_suspends`. One pointer name across all states — the state blocks share AST statement
nodes, and that is safe here in a way an owning local is not, because a pointer owns nothing.

Drop timing is restored explicitly by `release_before_ready` (§3 below explains why it is load-bearing).

Two `E0455` negatives were **INVERTED** into positive tests at the end of
`lang/async_pointer_across_await.cryo`, not deleted.

### New tests (do not weaken these)

- `lang/qualified_module_call_arg_binding.cryo` — 5 tests, all counting destructor runs. The
  lend-twice test is rejected outright by the OLD pin (`E0452`), which is its discrimination proof.
- `negative/E0453_deref_move_out_qualified_call.cryo`
- `lang/async_carried_local_address_stable.cryo` — 3 tests, the promoted probe plus a control.
- two inverted cases appended to `lang/async_pointer_across_await.cryo`.

---

## 2. YOUR TASK — position-aware substitution, then the payoff

### What still moves, and why

`carrier_can_live_in_place` (`sema/async_lower.cryo`) sends two shapes down the old moving path:

1. **a state that GIVES the value away** (hands it to a callee by value). In place, the field owns the
   value outright, so a by-value use must **empty the field at that site**; a bare alias would leave the
   callee and the field both owning it.
2. **a state that REBUILDS it** (a write before any read, top level or branch-nested). Publishing a
   second value has to drop the first, and an assignment does not.

### The mechanism

Both need the substitution to know **where** a mention sits. Today `subst_name_expr` /
`subst_name_stmt` rewrite every mention identically and thread no position flag; between them they have
**47 call sites**. The work is to add a `by_value` parameter (or an instance field, as
`mark_giveaway_sticky` does) and emit:

- **borrow position** → `*<nm>$p`, as now;
- **by-value position** → `this.__agg_<ds>_<nm>.take().unwrap()`;
- **re-assignment** `<nm> = <new>` → take-then-store, since an assignment does not drop what it
  replaces.

**`mark_last_use_expr` is your map.** It already tracks exactly this by_value/borrow distinction across
every node kind, and `any_use_gives_away` already answers "does ANY mention hand it over". Mirror its
positions rather than inventing them.

**One correction to make while you are there:** `mark_last_use_expr` marks **every** call argument
`by_value=true`. That predates sound classification. A `Borrowed` argument is *not* a give-away, and the
`arg_binding` axis can now say so. Fixing it makes more carriers eligible for in-place — but it also
changes `last_use_consumes`, which the *moving* carrier's hand-back decisions depend on, so **do it in
its own increment with its own gate run**, not as a drive-by.

**Defaults matter and they are OPPOSITE for the two consumers.** For give-away detection, an
`Unclassified` slot must read as NOT-a-give-away: over-reporting empties the field when it should not
and the next `unwrap_ptr` panics, while under-reporting surfaces as `E0453`. That is the reverse of the
move-set default. Read `arg_binding_unknown`'s doc comment in `passes/move_check.cryo` before touching
either.

### THEN the payoff — what gets deleted, and how

Each of these exists ONLY because addresses moved. Their tests must be **inverted** (from "rejects" to
"compiles and returns the right number"), **not deleted** — two already were, follow that pattern:

- `E0455_async_pointer_outlives_local.cryo`
- `E0455_async_address_into_awaited_future.cryo`
- the "a reference cannot be held live across an `await`" rejection in `promote_cross_state`
- then the subsystem itself: `reject_frame_addr_carry`, `frame_addr_root_expr`, `addr_place_root`,
  `call_frame_addr_root`, `place_leaves_frame`

**`emit_recv_refresh` should die with this — but PROVE it.** It exists because a carried receiver's
address changes per poll. Delete it in its own increment, with its tests green.

**Keep green throughout:** the 9 borrowed-view tests in `async_stress_shapes.cryo`. That carry
(`PollSm.borrow_ops`) is a **separate concern** — a destructor running too early, not an address moving.

---

## 3. Findings from this session you must not re-learn the hard way

**1. The `E0453` guarantee works — it fired on its first real give-away.** The first build with an
*unconditional* in-place carrier failed with

```
error[E0453]: cannot move a value that owns a destructor out of a pointer
  --> stdlib/net/ws/conn.cryo:211  (async recv)
```

a loud compile error instead of a silent double free. That is what exposed the hole in the first
eligibility predicate: `last_use_consumes` answers *"does the state still own the value as it falls off
its end"*, while in-place residency needs *"does ANY mention hand it over"*. Hence `any_use_gives_away`,
which drives the same walker with a **sticky** flag — a by-value mention is not downgraded by a later
borrow, and no branch is skipped for returning. **When you implement the give-away rewrite, expect this
error to be your guide again.**

**2. Drop TIMING is load-bearing, and a test said so out loud.** Leaving a value in its field defers its
destructor to the future's own drop. `NetTcpConn::tcp_conn_reports_a_truncated_record_as_eof`
**deadlocked** on that: the sender ends `return 1;   // dropping c here closes the socket mid-record`,
and with the close deferred the receiver waited on an EOF that never came. `release_before_ready` now
rewrites each completion return to

```
mut <rv>: <Output> = <value>;
this.<field>.take();          // one per in-place carrier
return Poll::Ready(<rv>);
```

Three things about that shape were each learned by breaking it:

- **The payload must be HOISTED first.** Releasing before it is evaluated empties the very field the
  value is read through — the probe went straight back to `0`.
- **Hoist the PAYLOAD, not the whole `Poll::Ready(…)`.** Binding the wrapper needs to spell
  `Poll<Output>`, and `sm.poll_out` is the bare `Poll` in generic and trait-method contexts: fine as a
  return expression, not as a declared local type.
- **The release is a DISCARDED temporary.** Binding it to `mut <tmp>: Option<T>` made the declaration
  the type authority and tripped finding 3.

Only a **bare literal** payload is released ahead of the return. "Does the payload mention a carrier
pointer" is a **different and wrong** question: a pointer derived earlier (`p = owner.conn()`) reads
that storage without naming it, and the accessor test returned 2 instead of 3.

**3. `Option<u8[8]>` and `Option<u8[]>` collide. PRE-EXISTING, not async, reproduces under the pin.**

```cryo
mut a: Option<u8[8]> = Option::Some([0; 8]);   mut ta: Option<u8[8]> = a.take();
mut b: Option<u8[]>  = Option::Some([1,2,3]);  mut tb: Option<u8[]>  = b.take();
```

gives `expected u8[8]*, found &u8[]` from inside `option.cryo`. Either alone is fine. It only surfaced
now because carrying a `u8[8]` across an `await` used to be rejected outright. The inverted test wraps
its array in a struct to avoid provoking it. **Type identity for array types is its own increment.**

---

## 4. Older open findings, still open

1. **`unsafe` is statement-only.** `const x: T = unsafe { *p };` does not parse — there is no unsafe
   *expression*. Several stdlib sites read `mut x: T; unsafe { x = *p; }`. Tolerable (11 sites). An
   unsafe expression is a language feature: parser + AST node + visitor + **all ten `async_lower`
   walkers** + codegen.
2. **`unsafe` still gates only ONE rule.** `check_deref_move_out` consults `unsafe_depth`; nothing else
   does. Whether `p.field` / `p[i]` move-out should come under the same rule is open and deliberately
   unbundled. Jake's target is "a decent middle ground — not as strict as Rust, not as unsafe as C/C++."
3. **`Option::as_ref` is invisible on a lowering-created instantiation.** It returns `Option<T*>`, a
   structural superterm, so it is `lazy_self_growing` and deferred to a mono call site. Worked around by
   `Option::unwrap_ptr` (a bare `T*` is not a superterm). **Do NOT "fix" this by making `unwrap_ptr` call
   `as_ref` — it towers infinitely and kills the stdlib build. Verified twice.**
4. **`check_return_payload_escape` may be over-approximate. NOT TOUCHED — Jake's call.**
5. **E0459 covers LOCALS only.** `this.a.poll(cx)` in a combinator marks nothing. Combinators do not
   move their children, so nothing is unsound today.
6. **E0459's polled set is append-only, deliberately.** If you make it precise, do it in its own
   increment with tests.
7. **E0636, fully SYNCHRONOUS, pre-existing.** A generic method on a receiver whose type mentions the
   enclosing generic parameter fails codegen. Two places must be fixed together:
   `find_generic_method_for_call` (`sema/method_binding.cryo`) and `specialize_method_call`
   (`mono/call_specializer.cryo`).
8. **Transitive receiver refresh does not reach generic contexts** — `ctor_field_for_param` needs
   `CallExprNode.resolved_template`, unset for a generic static called from a symbolically-walked body.
   Loud (E0455), not silent.
9. **`this` in an if-EXPRESSION initializer** inside an `async` method resolves to the generated Future
   (E0204 naming `…$Future_N`).
10. `try_join` still not shipped; `--panic=unwind` still does not link on Windows.
11. **Remaining port work after the carrier:** `net/http2/{client,server,connection}` —
    `connection.cryo` is 855 lines and **BORROWS its transport**, exactly what address stability makes
    tractable. Then delete the blocking surface: `TcpStream::connect`, the `Read`/`Write` impls for
    `TcpStream`/`TlsStream`, `TcpListener::accept` (**KEEP `bind`** — Jake ruled), plus the transitional
    duplicates `Headers/Request/Response::write_to`, `Request/Response::parse`, `request::read_line`.

---

## 5. Testing this class of bug — read before writing a single test

**A test that passes under BOTH the old and the new compiler proves nothing.** This has now caught four
sessions in different forms. Run every new test against `bin/cryo` as well as the freshly built
compiler. (`bin/cryo` is now the NEW pin — to discriminate against the old behaviour you need a stashed
copy or `git stash` + rebuild.)

**An address comparison cannot see an address-stability bug.** A future that records `&this.field` each
poll and compares reports "stable" under both lowerings, because a driver re-entering `poll` from the
same loop reuses the same stack region.

**What discriminates:** a **third party writing through a pointer the value published, while the future
is parked.** That is what `lang/async_carried_local_address_stable.cryo` does, and why its control case
(nobody writes) reads the same under both compilers — that is correct, not a weak test.

**Also:** pair every shape with a control, assert NUMBERS not booleans, and **count destructor runs**.
`lang/qualified_module_call_arg_binding.cryo` and `lang/deref_take_in_unsafe.cryo` are the models. Beware
measuring drops before `main` exits — a local in `main` has not been dropped yet when you print the
counter, which silently halves a count.

---

## 6. Jake's standing rules (mirror exactly)

1. **Root cause, never a workaround.** If a shape is broken, fix the shape. Do not special-case a call
   site, do not narrow a test, do not add an error telling the user to write it differently unless the
   restriction is genuinely principled and Jake has agreed to it.
2. **Only Jake commits** — unless he says otherwise in the session. Never co-author, no trailers. You
   may pin.
3. **Repin BOTH OSes** with plain `make pin` — **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `make verify-pin` and compare the two `git-describe:` lines as a **matched pair**. Then prove the
   pinned binary BEHAVES by compiling a repro with `bin/cryo` itself. A sidecar name proves nothing.
4. **If you cannot make a change work, REVERT it** rather than leaving an unverified change in the tree,
   and report the diagnosis.
5. **Comments describe the logic** — the invariant and the failure mode it prevents. No dated / phase /
   audit / batch labels in code. This file and `ASYNC_IMPL.md` are the exception.
6. Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
   over type-suffixed names; bare integer literals (`1`, not `1u32`); pass owning aggregates BY POINTER.
7. **When a decision has two defensible answers, ASK Jake** (use the question tool) — for
   language-semantics and soundness-contract calls. **Already settled, do NOT re-litigate:** no `Pin`
   type, uniform in-place carrier, no migration flag, the carrier keeps `Option<T>` with **no**
   flag-gated drops, and `unsafe` (not a named stdlib function) is the destructive-read escape hatch.
8. **Keep shell commands simple.** One command, one line. No hand-driven poll loops, no inline heredocs
   driving a build. This is a real complaint: *"you always do some crazy ass shell with these commands
   which makes it take longer or hangs the shell."*
9. **Offer the three long gates** (`make test`, `make selfhost-check`, `make pin`) and say what you are
   about to run first. Jake is happy for you to run them yourself; keep the commands plain.
10. **Do not narrate waiting.** Start a long gate in the background, then STOP. Do not poll a log every
    few seconds and announce "still running" — it wastes the turn and Jake has asked for it to stop.

---

## 7. Build / gate recipe

**Jake is on Windows.** Run `make` from **PowerShell** with `$env:CRYO_CC='gcc'`.

```
make cryo            # ~2m. `make test` does NOT rebuild the compiler.
make stdlib          # ~20s
make test            # ~12 min
make test ARGS="<substring>"   # plain substring filter -- your fast inner loop
make selfhost-check  # ~10 min; needs exit 0 AND *TWO* `FIXED POINT OK`
python scripts/roster-check.py compiler/build/cryo.exe --merge   # when ADDING or RENAMING tests
make pin             # plain; then: make verify-pin
```

`python3` does not exist on this Windows box — use **`python`**.

Standalone repro (no cryoconfig needed):
`CRYO_STDLIB=$PWD/stdlib compiler/build/cryo build probe.cryo --opt-level=0 -o probe`
`cryo check <file>` is faster and **does** run MoveCheck. `eprintln` needs `import fmt;`.

`make test` runs against `compiler/build/cryo`, **not** the pin — so a compiler fix is exercised as soon
as `make cryo` succeeds. But note `make cryo` builds the **stdlib via the PIN**, so a compiler change is
NOT applied to stdlib sources until you repin (or rebuild the stdlib by hand — see below).

**Serial only — never two heavy builds at once**, and **never edit compiler or stdlib sources while a
gate is running** (markdown is fine).

### selfhost-check: run it from WINDOWS, not from inside WSL

This cost a session's worth of detours. `scripts/selfhost-check.py` is host-aware:

- **From Windows** (`make selfhost-check` in PowerShell): runs the Linux 6-stage chain inside WSL, then
  the Windows 6-stage chain **NATIVELY**. Gives **two** `FIXED POINT OK`. **This is the correct
  invocation.**
- **From inside WSL**: runs the Linux chain, then tries the Windows chain **under wine** — which needs
  `.toolchains/llvm-mingw` *and* a working 32-bit wine. This WSL has no `wine32` (needs root
  `dpkg --add-architecture i386`), so cryo.exe crashes with `c0000005` and the gate fails at stage 1/6.
  **You do not need to fix that. Just run the gate from Windows.**

### THE BOOTSTRAP LANDMINE

`make stdlib` builds via the **PINNED** `bin/cryo`, and `make cryo` depends on it. The moment you write
stdlib code that needs your new compiler fix, `make cryo` fails building the stdlib with the OLD
compiler. The ritual:

1. `git diff HEAD -- stdlib/ > /tmp/patch`, plus copy untracked new files aside.
2. `git restore --source=HEAD --worktree -- stdlib/`
3. `make cryo` — compiler now has the fix, built against the old stdlib.
4. Gate it, `make pin` — the pin now carries the fix.
5. `git apply /tmp/patch`, restore the untracked files, `make cryo` again.

A cheap way to test a stdlib change against a new compiler *without* pinning:
`rm -rf stdlib/.bin && cd stdlib && ../compiler/build/cryo build`. Fully reversible via `make stdlib`.
**Use this after any async-lowering change** — it is the only way to exercise your change against the
stdlib's own async code before you pin.

**A build killed mid-way can leave `stdlib/.bin/libcryo.a` missing or stale**, after which every build
fails at link. `make stdlib` restores it.

### Gate holes that have bitten

- `selfhost-check` exit 0 is **not** sufficient — require `FIXED POINT OK` **count == 2**.
- `selfhost-check` **deletes `compiler/build/cryo`**, and `make pin` needs it. Run `make cryo` between
  them. (`bin/cryo`, the pin itself, is untouched.)
- **Gate what you PIN.** If you tidy even a comment after `selfhost-check` passes, rebuild and re-run it
  before `make pin`.
- **THE ROSTER IS PLATFORM-SENSITIVE.** Use **`--merge`** when ADDING tests. It **cannot tell a DELETED
  test from an other-platform one**; `--update` would drop the genuine Windows-only entry
  (`ProcessCommand::output_large_stderr_no_deadlock_win`, which on Linux always reports MISSING —
  expected, not drift). Check `git diff --numstat tests/test-roster.txt` shows a small `N M`; a
  whole-file rewrite means you flipped line endings.
- **The PowerShell tool's working directory drifts** when the Bash tool `cd`s. A `make` that reports
  *"Nothing to be done for 'test'"* or *"No rule to make target 'cryo'"* is running in the wrong
  directory — start PowerShell commands with `Set-Location C:\Programming\apps\CryoLang;`.

---

## 8. Pitfalls paid for in blood

**Diagnosing**

- **A test that passes under both the old and new compiler proves nothing.** §5. The big one here.
- **A pipe swallows a hung or crashing probe's output.** **Redirect to a FILE when diagnosing.**
  `println` is buffered and lost on a crash — use `eprintln` for markers.
- **`cdebug(...)`** (`import Utils::Logger;`, `--debug`-gated) is the only reliable way to see what a
  sema/mono pass decided. Unique tag, redirect to a file, and **remove every one before gating**.
  `--ast` prints identifier names BLANK and is useless for this class of bug. This session used it to
  prove all 16 `*this$recv` arguments were `Unclassified`, in one rebuild cycle, by printing the binding
  for every deref-argument while building the whole stdlib.
- **Bisect by construction, not by deletion.** Build the smallest thing that WORKS, then add one
  construct at a time until it breaks.
- **A mock is not a substitute for the real thing.** The `net/ws` port passed every hermetic
  mock-transport test and then hung on loopback.
- When reading `--emit-llvm`, **bound each `define` first**.

**Writing Cryo**

- **`unsafe` is statement-only** — `const x: T = unsafe { *p };` does not parse.
- **A generated future type is not spellable in source** (`drive$Future_0` is a parse error). Use
  `mut f = make_future();` and let inference name it.
- **In an `async` function, NEVER call `.drop()` explicitly on a local live across a suspend.**
- **Tuple destructuring in a declaration is not supported.** Use `pair.0` / `pair.1`.
- **`Option::Some(x)` as a bare `match` subject** fails with `E0600`. Bind it to a typed local first.
- **`Array<T>::get` / `Slice<T>::get` return `Option<T>`, not `T`.**
- **An f-string passed where a `string` is expected** compiles and prints garbage.
- **`Str::new(<c-string literal>)` measures with `strlen`.**
- **`Executor` has a `Drop` impl** that CANCELS a task still parked on a deadline, so a test that spawns
  work must `join` first. `future::block_on` has **no reactor**. Give each `block_on` of a different
  output type its own `Executor`.
- **`await <expr>.m()` where `<expr>` is a call result is E0455.** Bind to a local first. Correct
  behaviour, not a bug.
- **Do not instantiate a generic over both `T[N]` and `T[]` in one compilation unit** — §3 finding 3.

**Working in the compiler**

- **If you add an expression or statement kind to any walker, add it to ALL of them** — and that is not
  just `async_lower`. Grep for the NodeKind across the whole tree.
- **Order matters as much as coverage in `async_lower.cryo`.** Anything that ASKS a question about user
  code must ask it before the pass injects synthesized nodes into that code. `promote_cross_state` reads
  `needs_handback` / `any_use_gives_away` BEFORE adding any store for exactly this reason.
- **A recorded axis goes stale when a pass rewrites the node under it.** `subst_name_expr` carries both
  `autoref` and `arg_binding`; `TryExprNode` needed `resync_operand`.
- **A predicate answered from a TYPE can be silently unanswerable, and polarity matters.** A generic
  future arrives as an `InstantiatedType` whose template has ZERO fields at lowering time. See
  `carried_can_be_given_away` vs `borrow_outlives_suspend` — they consult `needs_drop` in OPPOSITE
  polarities, on purpose, and both say why.
- **`handle_ident` in `move_check.cryo` returns early for `Copy` types.** Any rule that must also catch a
  *copy* has to sit ahead of that guard.
- **`move_check` and `drop_insertion` must agree on the move set.** They share the `ArgBinding` axis and
  each has its own `arg_transfers_value` / `walk_argument` — change one, change the other in the same
  edit.

**Tooling**

- **`git checkout <path>` reverts EVERYTHING in that path.** Use the Edit tool to undo a targeted edit.
- **`sed -i` via Git Bash strips CR** on a CRLF file — use the Edit tool, or Python with `newline=''`.
  Several repo scripts are CRLF and will not run under WSL bash (`/usr/bin/env: 'bash\r'`).
- **PowerShell `Set-Content` mangles files containing backticks.** Use the Write tool.
- **PowerShell `Select-String -Pattern` treats `$` specially**; `this$recv` needs escaping or Bash `grep`.
- **Reading a repo markdown file with Python needs `encoding='utf-8'`** — the default cp1252 throws.
- **`bin/cryolsp` is a STALE build** predating async trait-method parsing. The compiler is authoritative.
- **Jake edits the tree while you work** — including deleting files, repinning and committing
  mid-session. Check `git status` and `git log` before assuming a stray diff is yours, and **never revert
  one.** If a tracked file has vanished, that is a decision, not an accident. (`docs/cryo.md` carries one
  of Jake's edits right now; the previous `HANDOFF.md` was deleted by him and this file replaces it.)

---

## 9. Definition of done

- `subst_name_expr` / `subst_name_stmt` know by-value from borrow position; give-away sites emit
  `take().unwrap()` and re-assignment takes-then-stores.
- `carrier_can_live_in_place` is **gone** — every carried aggregate lives in its field.
- The frame-address rejections of §2 are gone, and each negative test **inverted into a positive one**
  rather than deleted.
- `emit_recv_refresh` / `this$recv` deleted **after** being proven dead, in their own increment.
- The 9 borrowed-view tests in `async_stress_shapes.cryo`, the 5 `net_ws` tests, the 5
  `async_future_address_stable` tests, the 3 `async_carried_local_address_stable` tests and the 7
  `async_pointer_across_await` tests are all still green.
- `make test` green with the §0 numbers plus whatever you add.
- **Two** `FIXED POINT OK` from `make selfhost-check` **run from Windows**, a repin, `make verify-pin`
  OK, and the pinned binary proven by compiling a repro with `bin/cryo` itself.
- `ASYNC_IMPL.md` updated: the dashboard and a new §9 entry.

Leave the tree clean and tell Jake plainly what is done, what is not, and what you chose not to do.
