# HANDOFF — finish Cryo async/await: generic `async fn` first

**Your mission, in priority order:** make **generic `async function` genuinely work** (§4), then finish the
remaining async work (§5) until async is **100% complete**.

Jake's words: *"my goal is to get a full 100% completed working async without any cheap workarounds, half
baked solutions, or the easy way out on something."* Take that literally. There is currently a stopgap
`E0600` for generic async in the tree — **Jake has explicitly rejected it as an end state. Replace it, do not
keep it.** If you find yourself about to special-case, narrow a diagnostic to dodge a hard shape, or defer a
sub-case with a loud error just to get green: stop, and either solve it properly or bring Jake the design
question.

`ASYNC_IMPL.md` is the design + full history and is authoritative. Its **§9 Progress Log, newest entry**, is
the detailed write-up of everything below — read it before touching `async_lower.cryo`.

---

## 0. Baseline — verify before you start (2 minutes, saves hours)

Jake committed and **repinned** the 2026-07-24 async work after that session, so you should find:

- Branch `ll-impl`, **tree clean**, `python scripts/verify-pin.py` OK.
- The pin **includes** the new async diagnostics (it did not before the repin). If a probe below does not
  reproduce, check `bin/cryo.exe.pin.txt`'s `git-commit` against `HEAD` before blaming anything else.
- `make cryo` green, `make stdlib` = 149 modules green.
- Unit tests **1648**, compile-fail **150**, projects **9**. `tests/test-roster.txt` has 1648 entries and
  `make roster-check` passes.

Sanity-check the feature you are about to work on:

```cryo
async function identity<T>(v: T) -> T { return v; }   // expect: error[E0600] "a generic `async function`
                                                      // is not supported yet" — this is what you delete
```

## 1. Jake's standing rules (mirror exactly)

1. **Only Jake commits.** Never `git commit`, never co-author. You MAY `make pin` at a clean green boundary,
   and you MUST leave the tree ready for him.
2. **Repin BOTH OSes** with plain `make pin` — **NEVER `CRYO_CC=gcc make pin`** (landmine). Verify with
   `python scripts/verify-pin.py`. Repin only when a change moves default-path (`--panic=abort`) IR.
3. **Right decision over green. No workarounds, no half-baked solutions.** If a correct change breaks the
   build, FIX the build — never revert the correct change to stay green.
4. **Comments describe the logic** (the invariant, and the failure mode it prevents) — never project
   narrative. No dated/audit/phase/batch labels in code. `ASYNC_IMPL.md` and this file are the exception.
5. Preferences: methods / namespaced statics over free functions; one generic method + `static match (T)`
   over type-suffixed names; bare integer literals (`1`, not `1u32`); pass owning aggregates BY POINTER.
6. **When a decision has two defensible answers, ASK Jake** (use the question tool) — for language-semantics
   and soundness-contract calls, not for routine judgement.
7. **Don't add prose to `REPORT.md`** — tick items off only.

## 2. Build / gate / probe recipe

Run `make` from **PowerShell**, not the Bash tool (Git Bash). **Serial only — never two heavy builds at
once** (→ environmental exit ‑15 SIGTERM mid-compile).

```
$env:CRYO_CC='gcc'; make cryo           # ~2 min. `make test` does NOT rebuild the compiler.
$env:CRYO_CC='gcc'; make test           # ~35-60 min. Expect 1648 unit / 150 compile-fail / 9 projects
$env:CRYO_CC='gcc'; make selfhost-check # ~6 min; needs exit 0 AND *TWO* `FIXED POINT OK` markers
make pin                                # plain; then: python scripts/verify-pin.py
```

**Gate holes that have bitten before:**
- `selfhost-check` exit 0 is **not** sufficient — require `FIXED POINT OK` **count == 2** (target-IR +
  native-PE). Its tee log is UTF-16, so read it with PowerShell `Select-String`, not `grep`.
- `selfhost-check` **deletes `compiler/build/cryo.exe`** (cross-OS ELF↔PE clobber). Re-run `make cryo` before
  anything that needs the Windows binary.
- **Adding tests requires updating the golden roster:**
  `python scripts/roster-check.py compiler/build/cryo.exe --update`, and commit it with the tests.
- **Pin-delta measurement trap.** The compiler delta is `win-s2` (built by pinned) vs `win-s3` (built by new).
  For **stdlib** that pair is vacuously 0 — both stages are current source; the stdlib delta is
  **`win-s1` vs `win-s2`**. On Linux there is no `self/s1`: compare
  `stdlib/.bin/target/release/host/local/ir` vs `stdlib/.bin/self/s2/...`.
  Stage IR lives in `compiler/build/self/*` and `stdlib/.bin/self/*`.

### Probe setup — how every finding in §4 was established

A scratch project builds in seconds and needs no repo changes:

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

`$env:CRYO_CC='gcc'; & C:\Programming\apps\CryoLang\bin\cryo.exe build`. To test a **freshly built**
compiler instead of the pin, use `compiler\build\cryo.exe` **plus**
`$env:CRYO_STDLIB="C:\Programming\apps\CryoLang\stdlib"` (it cannot find the stdlib otherwise).

Probe gotchas that cost real time:
- **The control that proves it is the lowering, not the language:** compile the identical body **without**
  `async`. Run that first on anything in this area.
- **Always run under a timeout** (`Start-Job` + `Wait-Job -Timeout 30`) — async bugs present as hangs, and a
  hung probe silently eats your session.
- Observe results via **exit code** and `fmt::eprintf` on stderr; native exit does not flush stdio.
- **Import narrowly.** `import std::net;` drags in `net::tls` → OpenSSL link errors on this box. Use
  `import std::net::socket::tcp;` + `import std::net::addr::socket_addr;`.
- `block_on(f()) as i32` mis-binds the `R` of `block_on<F,R>` to `i32` and reports a confusing mismatch
  **inside `stdlib/future/_module.cryo`**. Bind to a typed local first: `const r: i64 = block_on(f());`.
- `PendingThenReady` parks without waking, so on an `Executor` it is correctly cancelled. Executor probes
  must self-wake or store the waker; `future::block_on` re-polls unconditionally, so it works there.

---

## 3. What is DONE — do not redo, do not regress

**Lowering:** straight-line; `if`/`else`; all four loop forms + `break`/`continue`; `match` statements **and**
match expressions; aggregates and droppable parameters carried across suspends; scope-aware alpha-renaming;
`async fn` awaiting `async fn`.
**Executor:** `spawn`/`join`/`abort`/detach, `Arc<Task>` lifetime, worker pool, poll-boundary `catch_unwind`
isolation under `--panic=unwind`.
**Reactor:** epoll (Linux) + IOCP/`\Device\Afd` (Windows) behind one readiness interface. Async TCP validated
on real Windows, 30/30.

**Landed 2026-07-24** (details in `ASYNC_IMPL.md` §9, newest entry — read it, the reasoning matters):
1. **A frame address held across a suspend was a SILENT miscompile → now `E0455`.** Each poll runs on a
   **new native frame**, and `promote_cross_state` re-materializes every carried local per state, so no
   address of a frame local survives its state. Rejected: a carried value initialized/assigned from a frame
   address, and an address handed to the awaited future (`TcpRead::start(s, &buf[0], 64)`). Deliberately
   **allowed**: transient `f(&local)` (house style), `&dyn_array[0]` (heap), `&*ptr_param` (caller's frame),
   `&GLOBAL`. Jake chose diagnose-now over address-stable carry, because stability would depend on the future
   never moving after its first poll — i.e. `Pin`, already ruled infeasible (§4 of `ASYNC_IMPL.md`).
2. **`async function … -> void` works**, with or without awaits, via **`Output = ()`**. It had been broken in
   *every* form, not just the awaiting one.
3. **Declaration order is irrelevant.** `lower` was split into **`declare`** + `lower`, with a module-wide
   pre-pass (`SemaVisitor::declare_async_futures`). The misleading "`i64` does not implement `Future`"
   `E0306` is gone.
4. **Recursive async fns are rejected** (they used to compile and then re-poll a completed sub-future).
   Direct and mutual, via an awaits-graph walk over generated future types.

Tests: `tests/tests/lang/async_{pointer_across_await,void_output,declaration_order}.cryo` and
`tests/tests/negative/E0455_async_*.cryo`, `E0600_async_*.cryo`.

---

## 4. THE MISSION — generic `async function`

`async function identity<T>(v: T) -> T { … }` must work: declared, instantiated at several types, awaited,
`block_on`-able, and awaiting other futures. Today it emits a stopgap `E0600` at the declaration
(`sema.cryo`, the `node.is_generic()` branch of `visit(FunctionDeclNode*)`), covered by
`tests/tests/negative/E0600_async_generic_function.cryo` — **delete that diagnostic and that test** as part of
landing this.

A previous session designed and mostly built this, then reverted it rather than leave a compiler that emitted
three internal errors where one clean message had been. **Everything it learned is below.** Do not restart
from zero, and do not re-walk the dead ends.

### 4.1 The design (validated — do not re-litigate)

The future is **generic in the same parameters as the function**, exactly as Rust's generator type is:

```
async function identity<T>(v: T) -> T          ⇒   type struct identity$Future<T> { state: u32; v: T; … }
                                                   implement<T> trait Future<T> for struct identity$Future<T>
                                                   function identity<T>(v: T) -> identity$Future<T>
```

Register the future as a **`TemplateEntry`** (`NodeKind::StructDeclaration`, `base_type` = the
`arena.create_struct` base) so the monomorphizer specializes `Fut<i32>` from the same demand that specializes
`identity<i32>`. Pass order permits registering it during sema: **TemplateRegistration → TypeResolution →
sema → Monomorphization**.

**Already confirmed working** with the reverted WIP applied: the template registers, `Fut<i64>` forms, and
`block_on`'s `F` binds to `main::identity$Future_0<i64>`.

### 4.2 THE TRAP — read this before writing a line

**`make_type_ann` pre-resolves its annotation (`pre_resolved: ty`), and a pre-resolved annotation is
invisible to monomorphization's substitution** — mono specializes a template by rewriting the generic
parameters that appear in its **annotations**. Pre-resolving is exactly why synthesized annotations can skip
name lookup today, and exactly what breaks the generic case.

So every synthesized type that mentions a type parameter **must be spelled by name**:
- the rewritten function's return annotation → `Fut<T>` as a `GenericAnnotation` over
  `NamedAnnotation{name: q_name, pre_resolved: invalid}`;
- the constructor `StructLiteralNode`'s **`set_generic_args([Named(T), …])`** — without it the literal builds
  the bare template and the function's own return type stops matching it;
- `poll`'s return annotation → `Poll<T>`;
- the impl's trait argument **and** its `Output` assoc binding → clone `node.return_type_annotation`;
- each parameter field of the future → clone that parameter's own `p.type_annotation`.

Fixing the return annotation, and then the ctor's generic args, each visibly moved the error. That is how the
diagnosis was confirmed — use the same technique.

### 4.3 The concrete code deltas (what the WIP did)

In `compiler/src/compiler/sema/async_lower.cryo`:
1. `AsyncDecl` gains **`self_ty`** (the instantiated `Fut<T>`) and a **`generic`** flag. `struct_ref` stays
   the *uninstantiated base* — it is what the template registers against and what carries the arena field
   table; `self_ty` is what a *value* of the future is.
2. In `declare()`, when generic: add the function's **own** `GenericParamNode*`s to the future's
   `StructDeclNode` (reuse the same nodes, so the body's `T` and the future's `T` are one arena type), build
   `param_names`/`param_type_ids` from `TypeResolutionPasses::create_generic_param_types`, call
   `generic_registry.register_template(TemplateEntry::new(…, NodeKind::StructDeclaration, struct_ref))`, then
   `self_ty = generic_registry.instantiate(struct_ref, prefs)`.
3. On the impl block set **`generic_params`** and **`target_args`** (`Named(T)`) — `target_args` is what makes
   `This` resolve to the full `Fut<T>` instead of the bare base.
4. Use `self_ty`, not the base, for: the function's registered *and* final return type, the ctor literal's
   resolved type, and **`sm.struct_ref`** (the type of `this` inside `poll`, which every `this.<field>` the
   state machine emits resolves against).
5. All the name-spelled annotations from §4.2.
6. Wrap the generic work in `arena.set_symbolic_no_demand(true)` (restore on **every** exit path): symbolic
   instantiations (`Fut<T>`, `Option<T>`, `Poll<T>`) must not record demand, or GenericValidation sees an
   instantiation nothing ever resolved. The arena tags nodes built in that mode symbolic, which is what makes
   them skippable.

In `compiler/src/compiler/sema/sema.cryo`:
7. In the `is_generic()` branch: run the symbolic body walk **before** lowering, and **unconditionally for
   async** — the walk is what puts resolved types on the body, and the lowering *consumes* them, so it is
   load-bearing rather than the additive check it is everywhere else. `CRYO_NO_SYMBOLIC_CHECK` must not be
   able to switch it off here (the WIP added `symbolic_check_body_forced`). Then call `async_lower.lower(node)`
   and delete the `E0600`.
8. Include generic fns in the `declare_async_futures` pre-pass, so a caller earlier in the file can await one.

### 4.4 Where it stopped, and your first move

With all of the above applied, `Fut<i64>` is created but **never SPECIALIZED**: `E0900` "a member type of
type #N did not resolve", `E0358` "no method named `poll`", and `E0200` `Output` still `T`. Those are one
fact — **nothing demands the specialization.**

`MonoState::enqueue_from_type_ref` (`mono/state.cryo:242`) *would* accept it: the base is a Struct,
`is_template` is true, and the args are concrete. So the gap is upstream. The **method** specialization path
calls `request_nested_instantiations(func.resolved_return_type)` at `mono/ast_resolver.cryo:601`; the
**free-function** analogue is around `mono/monomorphizer.cryo:718-727`. **Start there:** find why a
specialized free function whose return type is a generic instantiation does not request it, and fix that
cause rather than bolting on a demand from the async lowering.

### 4.5 Dead ends — do not spend time here

- **`lambda_synth` is NOT a model.** Closures inside generic fns are **rejected** there
  (`lambda_synth.cryo:478-488`, `:544`), not supported. An older handoff suggested mirroring it; that is wrong.
- Do not try to make `await` on a generic callee resolve "leniently" pre-mono and fix it up later. The caller
  may be a concrete function typed pre-mono; deferral there just moves the failure.

### 4.6 Two sub-problems that arrive with awaits in a generic body

- **A carried local whose type is a bare type parameter cannot choose a promotion strategy**, because
  `zeroable_kind` cannot tell a scalar from an aggregate. Route those to the **`Option<T>` carrier** — it is
  correct for both, and moving a scalar through it is just a copy.
- **Sub-future fields** are `Option<F_k>` where `F_k` is the awaited operand's type. If `F_k` mentions `T` it
  needs a name-spelled annotation too. Awaits on *concrete* futures (`PendingThenReady<i64>`) do not.

### 4.7 Suggested slices (each independently probe-able)

1. `async function identity<T>(v: T) -> T { return v; }` — **no awaits.** Exercises the whole
   template/impl/mono path with none of §4.6. Get this to run before anything else.
2. Two instantiations (`<i64>`, `<u32>`) plus a struct type argument — proves the specializations do not
   collapse onto one another.
3. Awaits on a concrete future inside the generic body.
4. Awaits whose future type mentions `T` (e.g. awaiting another generic async fn).
5. `-> void` generic; generic async fn carrying an aggregate across a suspend.

Land permanent tests for each slice, and delete `E0600_async_generic_function.cryo` when the diagnostic goes.

---

## 5. The rest of the async work — still open

Ordered by dependency. **Async is not "100%" until all of it is done.**

### TASK A — `async` methods

`KwAsync` is consumed in exactly ONE place: `parser.cryo:423`, inside `parse_declaration`. So `async` is a
**top-level-function modifier only** — `async fetch(&this) -> i64 { … }` inside an `implement` block is a
parse error (`E0100: expected '(', found 'fetch'`). Async code can therefore only be written as free
functions, which collides head-on with Jake's no-free-functions preference.

The AST is ready: `MethodNode` (`AST/declaration.cryo:438`) wraps a `FunctionDeclNode*`, and
`FunctionDeclNode.is_async` / `set_async` already exist.
- **Parser:** accept `async` as a method modifier in `parse_method` / `parse_method_with_modifiers`
  (`parser.cryo:1326`, `:1340`; modifier dispatch ~`:1147-1204`) and `set_async(true)` on the inner func.
  Commit `61cf892e` reworked modifier spans here — follow its pattern, and decide `async static` ordering.
- **Sema:** methods are typed via `visit_methods` (`sema.cryo`), which has **no** lowering hook — add the
  equivalent of the one in `visit(FunctionDeclNode*)`.
- **`this` must be REWRITTEN, not merely captured.** Inside the generated `poll`, `this` *is* the future, so
  every `this` in the user's body has to be redirected to the captured receiver field. `subst_name_*` in
  `async_lower.cryo` is the existing machinery for that shape of by-name rewrite.
- **Receiver form is a soundness question — put it to Jake, with this precedent.** By-value `this` is owned by
  the future and plainly sound. `&this` / `mut &this` makes the future hold a reference into the caller's
  object — which is the *same* unenforced contract that the new `E0455` deliberately blessed for pointer
  parameters (the pointee lives in the caller's frame; the caller must keep it alive while polling). On that
  reading `&this` is no less safe than the remedy the diagnostic itself recommends, and banning it leaves no
  natural spelling. Recommend allowing all three and NOT extending `E0455` to the receiver.
- **A generic owner reuses §4's answer** — an async method on `Box<T>` has the same symbolic-body problem.
  Do §4 first and this falls out.

### TASK B — buffer-owning `TcpRead`/`TcpWrite` (must precede TASK C)

Have the I/O futures own their buffer and hand it back via `TcpIo`, exactly as they already own the socket, so
the async socket API is sound **by construction**. This is Jake's chosen half of the TASK-1 decision (the
other half, the `E0455`, has landed). Note it is now *enforced*: the `&buf[0]`-into-an-awaited-future spelling
is a compile error, so the socket port cannot be written the old way even by accident.

### TASK C — the async-only socket port (largest chunk)

Jake directed that sockets become async-only with every consumer ported. `tcp.cryo` currently carries **both**
the blocking API (`read`/`write`/`accept`/`connect`) and the async futures
(`TcpRead`/`TcpWrite`/`TcpAccept`/`TcpConnect`). **Zero `async function`s exist anywhere in `stdlib/`** — the
async I/O layer is all hand-written `Future` structs. 36 blocking call sites remain outside `tcp.cryo`:
1. `net::http` (3+5+1+1), `net::http2` (3+5+1+1+1), `net::ws` (5+1+1), `net::https` (1). Mostly generic over
   `S: Read + Write` (`http2/connection.cryo`, 855 lines, has zero concrete socket uses), so the concrete
   touchpoints are few.
2. `net::tls` (12: `context` 7 + `stream` 5) — **needs its own design pass first**: OpenSSL's blocking BIO
   must become non-blocking with `WANT_READ`/`WANT_WRITE` handling. Do not start this inside the port.
3. Only then delete the blocking surface from `tcp.cryo` and rename the async ops onto
   `read`/`write`/`accept`/`connect`. Keeping the blocking API until last is what keeps the tree green.

Known constraint while porting: a value rebuilt after an `await` must be assigned at the **top level** of that
step, not inside a branch (clear `E0600` if you get it wrong).

### TASK D — the missing library layer

None of this exists; all of it is needed for "fully functional":
- **Timers** — `sleep` / `timeout`. Substrate is bound and ready: `timerfd_create`/`_settime` on Linux, IOCP
  timer on Windows; `Instant`/`Duration` in `stdlib/time/`.
- **Combinators** — `join` / `select` / `try_join`. Note `select`/`timeout` cancel by **dropping** an
  un-spawned future (an ordinary Cryo drop), not via `JoinHandle::abort`.
- **`async fn main`** — desugar to `fn main() { block_on(__async_main()); }`. Compiler change ⇒
  selfhost-check + repin.

---

## 6. Remaining lowering restrictions (loud `E0600` — deferred, not bugs)

Extract the current list with:
`grep -o '"async: [^"]*"' compiler/src/compiler/sema/async_lower.cryo | sort -u`

The notable ones: `await` nested in an expression; `await` in an `if`/`while`/`do-while` condition, `for`
init/update, `match` subject, or match-arm guard; aggregate or reference **match-arm bindings** across a
suspend; a `break`/`continue` inside a match **expression** arm within an awaiting loop; and recursion
(direct or mutual — a self-containing future has no finite size).

**Each of these is a "100% complete" blocker too.** They are deferrals, not design decisions — `await` nested
in an expression in particular is something users will reach for immediately. Once §4 is done, work through
this list rather than treating it as permanent.

## 7. Pitfalls paid for in blood (do not rediscover)

- `codegen/visit/ir_generator.cryo`'s `visit(AwaitExprNode*)` hard-error is a deliberate backstop — a
  surviving `AwaitExprNode` means the lowering did not rewrite it. Do not delete it.
- A warning emitted against generated code is a **bug report about the generator**, not noise — every
  synthesized node carries the async function's own span, so it points at innocent user source.
- Never blind `git stash pop`. Prefer `git checkout <commit> -- <file>` / copy-aside.
- Incremental-cache staleness: if a compiler-source edit seems ignored, clear
  `compiler/build/target/release/host*/local/incremental`, `rm compiler/build/cryo*`, then `make cryo`.
- `cdebug(fmt, …)` (`Utils::Logger`) is a `--debug`-gated stderr printf — the clean way to trace a pass.
  **Remove before repin.**
- `sed -i` through Git Bash strips CR on CRLF files — use the Edit tool instead.
- Anchors drift; symbols are durable. Re-grep rather than trusting the line numbers in this file.

## 8. Definition of done

Async is **100% complete** when:
- generic `async fn` **works** (§4) — declared, multiply instantiated, awaited, awaiting others — with the
  stopgap `E0600` and its negative test deleted;
- async **methods** work, including on a generic owner (§5-A);
- the I/O futures own their buffers (§5-B) and the net stack is **async-only** with the blocking surface
  deleted (§5-C);
- **timers, combinators and `async fn main`** exist (§5-D);
- the §6 restriction list is worked down, not merely documented;
- and every one of those has permanent tests, with `make test` OVERALL PASS, **two** `FIXED POINT OK` from
  `make selfhost-check`, a correct pin-delta measurement (§2), and `tests/test-roster.txt` regenerated.

Update `ASYNC_IMPL.md` as work lands: Status Dashboard + append to §9 Progress Log. Leave the tree clean and
ready for Jake to commit — and tell him plainly what is done, what is not, and what you chose not to do.
