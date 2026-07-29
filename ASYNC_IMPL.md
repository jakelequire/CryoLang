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
| 2 | Compiler: `async fn` parse + state-machine lowering + `await` desugar | ✅ DONE+validated. **Address stability COMPLETE 2026-07-29 — the in-place carrier is UNIVERSAL and `carrier_can_live_in_place` is DELETED.** Every carried aggregate is BUILT INTO its `Option<T>` field and stays there for the life of the future; the moving carrier is gone, not bypassed. The two shapes that used to force it — a state that GIVES the value away, one that REBUILDS it — are now expressible because `subst_name_expr`/`subst_name_stmt` carry a **`by_value` position flag**: a mention that reaches into the value becomes `*<nm>$p`, one that hands it over becomes `take().unwrap()`, one that rebuilds it republishes the field. A by-value argument is classified from `arg_binding`, and an `Unclassified` slot reads as a BORROW (the opposite default from the move passes — over-reporting strands the next `unwrap_ptr` on a `None`, under-reporting is a loud `E0453`). Needed three non-async root-cause fixes: synthesized `Option::Some`/`Poll::Ready` payloads were `Unclassified`; trait `MethodInfo.param_types` wrongly included the receiver, breaking its documented non-self contract; and argument classification never reached a bare generic param bounded by a `where` clause. `make test` **1903 / 165 / 14, 0 failed**. **FINDING — the frame-address subsystem and `emit_recv_refresh` are NOT dead and were NOT deleted:** residency applies only to values that are actually CARRIED, and both `E0455_async_*` negatives address a local that is never read after the suspend, so it is not carried and still dies with the poll frame. Inverting them would accept unsound code. The payoff is already collected anyway — `addr_place_root` stops at the first indirection, so `&c.canary` on a carried local becomes `&(*c$p).canary` and is allowed (proven: `got=99 drops=1`), now pinned by a test. **OPEN (Jake's call):** use-after-give-away *within one state* no longer reports `E0452`, because the take and the later borrow are unrelated expressions to MoveCheck; see the newest §9 entry for the two candidate fixes. Earlier (2026-07-28): the acceptance probe reads `55` (it reads `0` under the old pin) and is `lang/async_carried_local_address_stable.cryo`; two `E0455` negatives were INVERTED into positive tests. Drop TIMING is restored explicitly by `release_before_ready` — deferring a carried socket's close to future-drop deadlocked a real test. The design IS settled and does **not** need flag-gated drops — keep the `Option<T>` field and reach the payload in place via `unwrap_ptr`. **Its blocker is now REMOVED (2026-07-28, later).** In-place residency turns a missed give-away from a loud `unwrap`-on-`None` into a **silent double free**, so it needed a rejection of by-value moves out of `*p`, which in turn needed sound argument classification. Both landed: arguments are now classified from real **parameter types** (`ArgBinding`, set in sema, consumed in lockstep by all four argument loops), `!autoref` is gone as a by-value proxy, and `check_deref_move_out` is in the tree and does **not** misfire on the lowering's own `*this$recv`. `unsafe` became the escape hatch and `core::mem::take_ptr` was **deleted** (Jake's call — it was byte-identical to `*p` and existed only to be invisible to the checker). Two pre-existing defects fixed on the way: `unsafe { }` silently disabled move checking wholesale, and a generic call reachable only from inside an `unsafe` block was never specialized. See the newest §9 entry. Earlier: **Two carried-aggregate soundness bugs fixed 2026-07-27** — (1) `await <method>` on an owning local inside a LOOP left its carrier empty (`unwrap` on `None`): the pass asked "did this state give the value away?" AFTER inserting its own by-value hand-back store, and mistook that store for the answer; (2) an `async fn` with NO awaits COPIED an owning parameter out of its field instead of moving it, double-freeing it. Both fixed at the root in `async_lower.cryo`; the `Join` diagnosis recorded under `TcpConn` was wrong and is corrected. New permanent coverage: `async_stress_shapes.cryo` (31 tests, asserted numbers + counted drops, every shape paired with a control) and the flood test. See the newest §9 entry. Earlier: DONE+validated; **the deferred aggregate-across-await tail LANDED 2026-07-24 — an `async function` can now hold an aggregate across a suspend, take a droppable aggregate parameter, and be `spawn`ed on an `Executor`** (see the newest §9 entry). Earlier: DONE (2026-07-23) — parse + no-await (1b) + single await (2) + N straight-line (3) + awaits across `if`/`else` (4a) + awaits across `while`/`for`/`loop`/`do-while` + `break`/`continue` + `mut` loop-carried promotion (4b) + scope-aware alpha-rename, all committed through `5e28a74f`. **Inc 4c DONE (2026-07-23): `await` inside a `match` arm — dispatch `match(subj)` → per-arm entry states → join; scalar pattern bindings captured to fields (aggregate→E0600, ref→E0455); pattern bindings alpha-renamed for by-name soundness; non-exhaustive match gets synth `_ => join`. Plus the bare-block-with-await warm-up. Both-OS fixed point, `win-s2`/`win-s3` = 0/235 `.ll` → NO REPIN, UNCOMMITTED.** All common control flow now lowers → Phase 2 complete. |
| 3 | Executor + `Waker` + `spawn`/`JoinHandle`; multi-thread; poll-boundary `catch_unwind` isolation | ✅ DONE+validated (2026-07-24) — surface LOCKED 2026-07-23. **(a)** single-thread executor + ready-queue + re-enqueueing Waker; **(b)** `spawn`/`JoinHandle` (Output via `TaskShared<O>`), `block_on`, `join`/`detach`/`abort`, drop=detach; **(c)** pthread worker pool + per-task atomic run-state (IDLE/SCHEDULED/RUNNING/NOTIFIED) + condvar `join`/`block_on` + `catch_unwind` poll-boundary isolation. Needed a NEW compiler `![config(panic_unwind)]` gating atom (see §9) → Phase-1 both-OS REPIN (selfhost fixed point, 235 `.ll`). Executor is self-contained (own pthread wrappers, no `thread::Scope` dep). Validated on Linux: regression 30/30, isolation 30/30. UNCOMMITTED. |
| 4 | Reactor (epoll/IOCP) + async I/O over `std::net` + timers + `async fn main` + combinators | ◐ in progress 2026-07-24. **Inc 4b DONE on Linux (reactor + async TCP, stress+leak clean); Windows AFD backend written but UNVALIDATED (wine 9.0 cannot service `IOCTL_AFD_POLL` — needs a real Windows box).** Interface fork LOCKED by Jake: **one readiness interface both OSes, Windows via real AFD** (not WSAPoll, not a per-OS split). Sockets are to become **async-only with every consumer ported** (Jake, 2026-07-24) — that port is **UNBLOCKED as of 2026-07-24**: the aggregate-across-await tail landed, and the follow-on "rebuilt from inside a branch" E0600 is now removed too (newest §9 entry). Forks LOCKED earlier: waker lifetime = **separate `Arc`-wrapper**; reactor = **dedicated thread, per-`Executor`** (`![thread_local]` current-reactor handle); platform = **both OSes (epoll + IOCP)**. **Inc 4a DONE+validated (2026-07-24): Arc-refcounted `Waker` (Copy→non-Copy) + `Arc<Task>` lifetime; finish-vs-park now implicit via `Task::drop` on the last decref. Needed a COMPILER fix (owner-generic static with a defaulted owner param + nested return → E0636; `call_resolver.cryo` default-backfill) → selfhost fixed point BOTH OS, win-s2 vs win-s3 = 0/235 `.ll`, REPINNED both OS. UNCOMMITTED.** NEXT = Inc 4b (the reactor: epoll+IOCP interface — bring Jake the readiness-vs-completion unification fork). |

| 4d | Timers — reactor deadline chain + `Sleep` | ✅ DONE+validated 2026-07-26. Deadline-sorted chain on `Reactor` (`register_timer`/`cancel_timer`), reactor thread bounds its wait by the earliest deadline instead of `-1`, fires expired wakers after the wait, kicks on a new-earliest arming. `stdlib/future/timer.cryo`: `Sleep::new(dur)` / `Sleep::until(instant)`, absolute deadlines, saturating construction, `Drop` disarms. Self-wakes under a reactor-less driver so `future::block_on` still completes it. Also fixed a real teardown UAF: `Executor::drop` freed the `Reactor` before `drain_cancelled()`, so a task woken by readiness but not yet driven dereferenced freed memory in its future's `drop` — split into `Reactor::stop` / `Reactor::free`. 10 tests. |
| 4f | Combinators — `join` / `select` / `timeout` | ✅ DONE+validated 2026-07-26. `stdlib/future/combinator.cryo`; built through the `Futures` namespace (`Futures::join`/`select`/`timeout`/`timeout_at`). Cancellation is a plain drop — the `Select` loser and the `Timeout` victim are released with the combinator, which is what disarms a `Sleep` and releases an I/O registration. Arity 2, higher arities nest (tested). 9 tests. **`try_join` NOT shipped:** blocked on an inference gap — a generic static whose return type mentions the future params AND params reachable only by destructuring a nested generic in the bound leaves the nested ones abstract (minimal repro in §9). |

| 4g | `async function main` | ✅ DONE+validated 2026-07-25. Renamed out of the entry-point slot (`main` → `main$async`) with a synchronous `main` synthesized in its place, driving it on an `Executor` LOCAL to `main` — a runtime scoped to the entry point, whose `Drop` tears it down at exit. `-> i32` and `-> void` both land on an `i32` entry point; parameters are forwarded, so argc/argv behaviour is identical to a synchronous `main`. New `E0365` for a generic `main` or a return type that is neither. `AsyncLower::desugar_async_main`, called before `declare`. 3 projects + 2 negative tests. |
| 5a | Buffer-owning `TcpRead` / `TcpWrite` | ✅ DONE+validated 2026-07-25. Both futures now OWN an `Array<u8>` as they already owned the socket, handing it back via `TcpIo::take_buf()`; a read truncates it to the bytes that arrived, a write returns it untouched. Makes the async socket API sound BY CONSTRUCTION — a future moves between polls, so a caller-frame `u8*` was written through a stale address. Needed the missing `Array<T>::resize`. **Closed a total test gap: there were zero tests AND zero consumers of the async socket futures in the tree.** 2 tests (loopback echo round trip + read-timeout cancellation). |
| 6 | `await` in a `match`-arm guard | ✅ DONE+validated 2026-07-25 — the last rejected `await` position. Such a `match` lowers to a DECISION CHAIN (one test state per arm, a false guard falling to the next test) instead of the single dispatch, which every other `match` keeps. Subject evaluated once into a chain-owned field; fall-through arm emitted only when the arm can fail to match; `hoist_match_expr`'s matching bail-out removed so the expression form works too. 14 tests. **One sub-case rejected with a precise diagnostic rather than lowered:** an arm binding an OWNING payload out of the subject, which a later test would then re-match hollowed-out. |

| 5b-tls | Async TLS (the port's prerequisite) | ✅ DONE+validated 2026-07-25. `stdlib/net/tls/future.cryo`: `TlsHandshake` / `TlsRead` / `TlsWrite` / `TlsIo`, plus `TlsConnector::connect_async` and `TlsAcceptor::accept_async`. Each `WANT_READ`/`WANT_WRITE` becomes a reactor registration rather than a spin; the interest to arm is read off the SSL error code (a read CAN want writability), and each future records the one direction it armed so its `Drop` cancels only that half. Sound only because §5a made the read/write futures own their buffer — OpenSSL requires a `WANT_*` retry to repeat the same call with the SAME arguments, and a future moves between polls. 1 test: loopback handshake + encrypted echo with **both sides as tasks on ONE `Executor`**, which the blocking TLS test cannot do (it needs a thread, or `SSL_connect` blocks before `SSL_accept` runs). |
| 5b-port | Socket port + delete the blocking surface | ✅ **DONE 2026-07-29 — `net/http2` ported and the blocking surface DELETED. `net` is fully async.** `Http2Connection<S>` now OWNS a `BufStream<S>` (it borrowed an `S*`), reads are `async` inherent methods on `BufStream<S>` (`read_h2_frame`, named apart because `ws::frame` already owns `read_frame` on that type), and the whole write side is SYNCHRONOUS — `queue_settings` / `queue_header_block` / `queue_ack_data` / `process_control` only append to `pending()`, so a HEADERS block plus its CONTINUATION fragments is assembled with no suspension inside it and goes out in one `flush`. `send_body` takes the body BY VALUE for a real reason: a `Slice<u8>` into a caller's array would be a borrowed view crossing every window wait, so the slice is re-derived from the future's own storage after each one. DELETED: `TcpStream::connect`, both `Read`/`Write` impls for `TcpStream`, both for `TlsStream`, `TcpListener::accept`, `TcpStream::set_read_timeout`/`set_write_timeout` (they bounded a blocking read that no longer exists), `Headers/Request/Response::write_to`, `Request/Response::parse`, `request::read_line`. **`TcpListener::bind` KEPT** (binding does not wait). **One compiler bug found and fixed at the root — see the newest §9 entry:** an owning value handed BY VALUE to an `async` method of a GENERIC owner was refused with `E0453`, because `lookup_method_param_types` returned nothing for a receiver whose type is a symbolic `InstantiatedType`, so every such argument stayed `Unclassified` — and the two consumers of that default disagree by design. **2-PHASE REPIN.** Phase 1 (compiler alone, clean stdlib): `make test` **1920 / 166 / 14, 0 failed**, TWO `FIXED POINT OK`, pinned + verified. Phase 2 (the port): `make test` **1924 / 166 / 14, 0 failed**. 5 new h2c loopback tests over REAL sockets — 100 KB echoed through connection-window exhaustion, a header block split across CONTINUATION frames, 4 sequential requests on one connection, and a clean client close read as end-of-stream; 3 blocking-surface tests ported to the async path rather than deleted. Earlier: **increment 5 (`net/ws`) LANDED 2026-07-28, 5/5 green, and the two borrow-across-a-suspend defects that blocked it are FIXED.** (a) A non-owning view (`Str`/`Slice`) borrowed from a local and handed to an awaited call was dropped with the block that built the future, not with the local's source scope — silent use-after-free, pre-existing, reproducible under the pin in 40 lines, and reaching every `async` fn with a view parameter. `promote_cross_state` used "read in a LATER state" as its liveness proxy, and the last mention of `msg` in `await consume(msg.as_str())` sits in the DECLARING block. Fixed by recording each awaited operand (`PollSm.borrow_ops`) and carrying any mentioned local that has a destructor, so its drop moves past the suspension — Jake's calls: conservative promotion rather than a diagnostic, scoped to frame locals and parameters. (b) A pointer taken through a CALL (`ws.connection()`) addressed a STALE copy once its owner was carried: writes landed on a copy the later states had stopped reading. Reduced to 40 lines returning `11012011` instead of `11012013`. No silent repair exists — the address changes, not the lifetime — so `call_frame_addr_root` now treats such a call as the address-of it is, the existing `&local` rule applied to a form the syntactic walk could not see (Jake's call). New negative `E0455_async_pointer_from_accessor.cryo`. **LANDMINE: the previous session's hermetic ws verification (mock `AsyncTransport`) passed all five shapes while the real-socket pipelined test HUNG — a transport double cannot exhibit (b) at all.** 9 new stress tests + 5 ws tests; `make test` **1875 / 160 / 14, 0 failed**. Remaining: `net/http2/{client,server,connection}` (`connection.cryo` is 855 lines and BORROWS its transport, so (b) applies directly), then delete the blocking surface (KEEP `TcpListener::bind`). Earlier that day: **its blocker was root-caused and TWO async-lowering bugs were fixed.** The previously-recorded axis (a generic-owner method whose suspensions are DEFAULT trait methods on `this`) is **WRONG** — every part of it reduces GREEN. The real triggers were (1) **the `?` operator**: sema builds the `?` desugar with the match's SUBJECT and the `?`'s `operand` as the SAME node, and the async lowering's two in-place rewrites (`hoist_expr` lifting the `await` out, `subst_name_expr` promoting a local) replaced the subject and left `operand` naming the pre-rewrite tree, which the resolver still walks — so `(await f(local))?` was rejected outright (E0201). Fixed by `TryExprNode::resync_operand()`. (2) **a carried value with no destructor was never handed back**: `last_use_consumes` read "gave the value away" off the syntax alone, so a by-value READ of a payload-free enum / scalar-only struct / `Slice` — a COPY, not a move — suppressed the hand-back and the next state's `take().unwrap()` found `None`. Fixed by gating on `OwnershipQuery::needs_drop`, conservative for anything abstract. 9 new tests. Finding 2 (E0636 on a generic method whose receiver mentions the enclosing generic param) is reproduced, reduced to 50 SYNC lines, traced to two specific places (sema never stashes the method type args; mono cannot recover the receiver type in a spec body) and deliberately left unfixed — independent of async, and not on the port's critical path. Findings 4 and 5 could NOT be reproduced. See the newest §9 entry. Earlier: **increment 4 DONE 2026-07-28: `net/https.cryo` async on `BufStream<TlsStream>`, and the blocking TLS twins deleted in the same increment** (`TlsConnector::connect` / `TlsAcceptor::accept` ARE the async ones; the old `*_async` pair is renamed `start_connect` / `start_accept` for callers that must own the handshake future; `drive_handshake` and `client::send_over` deleted). `net_https.cryo` rewritten onto the async stack and now covers the shipped `HttpsClient`, which previously had NONE. `net_tls.cryo` deleted — its whole subject was the blocking surface and its behaviour is already asserted by `net_tls_async`. Remaining: `net/ws/conn.cryo`, then `net/http2/*`. Earlier: **increment 3 DONE 2026-07-28: `net/http/server.cryo` on the async transport.** `async run` / `run_on(listener)` / `serve_connection(conn)` taking the connection BY VALUE; `BufConn<S>` renamed **`BufStream<S>`**; `HttpConn` trait replaced by a cross-module inherent impl (Jake's call — protocol state ⇒ owning wrapper, stateless framing ⇒ inherent impl). Two compiler fixes: the **transitive receiver refresh** (a combinator-wrapped `async` method silently skipped it — the field path comes from the constructing static's DECLARATION, since the awaited `Timeout` is an `InstantiatedType` whose template has zero fields at lowering time) and a **scalar narrowing of the frame-address check**. **selfhost-check: TWO `FIXED POINT OK`; repin taken and verified — the pin now carries the compiler fixes.** 4 new server tests. Of the three findings that increment opened, (1) is **CLOSED**: `async_branch_owning_local` was red because several state blocks binding one carried local shared ONE drop flag while owning separate storage. Both halves are fixed — declaring states bind a fresh name each, and drop flags are now keyed per DECLARATION rather than by name (`binding_dropflags`, `passes/drop_insertion.cryo`), which closes the taking side too. **`HttpServer::with_read_timeout` is ENFORCED again.** Still open: (2) the transitive refresh does not reach generic contexts (`resolved_template` unset in a symbolic walk) — rejects loudly rather than miscompiling; (3) `this` in an if-EXPRESSION initializer resolves to the generated Future. Still to port: `net/https.cryo`, `net/ws/conn.cryo`, `net/http2/*`, then delete the blocking surface. Earlier: **increment 2 DONE 2026-07-28: `net/http` HTTP/1.1 framing on `BufStream<S>`.** Sync encoders (`Headers`/`Request`/`Response::encode_into` into an `Array<u8>*`), grammar extracted once (`from_request_line` / `from_status_line`, shared with the blocking `parse` during the migration), async readers as METHODS on the connection (`net/http/conn.cryo`, `type trait HttpConn` implemented for `BufStream<S>`), and `Client::get`/`post`/`send` now `async` over `TcpConnect::start`. **Two async-lowering bugs fixed at the root** — (1) a `return` in ONE `if`/`match` branch made a state disown a carried aggregate on paths that never ran (unwrap-on-`None`); (2) **PRE-EXISTING and the real blocker: the receiver refresh was silently skipped for an `async` TRAIT method on a GENERIC receiver, so NO `BufStream<S>` could survive more than one write-then-read cycle** (socket swapped back into dead stack memory → `EBADF`). Nothing caught it because every existing test flushed at most once per connection. 8 new tests in `net_http_conn.cryo`, all asserting numbers (incl. a FILL COUNT of exactly 1 for two pipelined responses, with a one-byte-per-fill control). `make test` 1846/1846 + 159 compile-fail + 14 projects PASS. **Repin owed (compiler changed); selfhost-check not yet run.** Still to port: `net/http/server.cryo`, `net/https.cryo`, `net/ws/conn.cryo`, `net/http2/*`, then delete the blocking surface. Earlier: **increment 1 DONE 2026-07-27** (one generic `BufStream<S>` over an `AsyncTransport` seam; `TcpConn` deleted; `io::async_traits` and `io::buf_conn` dissolved into `io::traits` / `io::buf` per Jake's module-structure ruling; needed one compiler fix — a trait `async` default body could not await a required method when the impl was on a GENERIC owner. See the newest §9 entry). Consumers NOT yet ported. Earlier: gated on 5c (async trait methods), per Jake's 2026-07-26 call. Remaining: port `net/http/{client,server}`, `net/http2/{client,server,connection}`, `net/https.cryo`, `net/ws/conn.cryo`, then delete `TcpStream::connect/read/write`, `TcpListener::bind/accept` and the blocking TLS entry points. One-way API break: every `HttpClient::get`-shaped call becomes `async`. **Re-derived consumer list (2026-07-26): far smaller than "19 files" — most of the greps that produced that count are doc-comment mentions. `net/dns.cryo`, `net/addr/ip.cryo` and `net/socket/udp.cryo` name `TcpStream` ONLY in prose and need no port.** **Scope correction (2026-07-26): there are no `AsyncRead`/`AsyncWrite` traits in the tree yet** — the async surface is concrete futures (`TcpRead`/`TcpWrite`/`TlsRead`/`TlsWrite`), so step 1 of the port is to DESIGN those traits, not to retarget onto existing ones. The shape follows from two settled constraints: the future must OWN the buffer (§5a — a caller-frame `u8*` is written through a stale address), while the transport may now stay in `mut &this` (§5d makes a receiver held across a suspend sound), i.e. `async read(mut &this, buf: Array<u8>) -> AsyncIo` handing the buffer back the way `TcpIo::take_buf()` already does. The blocking `io::Read`/`io::Write` take `u8*` / `Slice<u8>`, so they cannot simply be mirrored. ~3,300 non-comment lines of consumer code plus tests. **Unblocked 2026-07-26 by 5f** — `?` did not work in any `async` function, which no amount of stdlib care would have worked around; the trait shape above is now proven to compile AND run (see 5f). Writing the traits into `stdlib/` is the next step and has NOT been done. |
| 5c | Async trait methods (the port's prerequisite) | ✅ DONE+validated 2026-07-26. First the **three blocking projection gaps — six defects** — were fixed across parser/sema/type-resolver/mono, so projection-typed dispatch, `block_on` on a projection and `await` on a projection all work. Then the feature itself: `E0364` lifted for trait methods (`virtual`/`override` still rejected), `desugar_async_trait_methods` synthesizing the implicit `<Method>Fut` + projection return, `bind_async_trait_assoc` binding each impl's own future, and E0309 taught the binding is implicit. **Default bodies included** — they cost almost nothing because `synthesize_default_trait_methods` already clones them per-impl, so no future generic over `This` was needed. 6 tests; the obsolete `E0364_async_trait_method` negative deleted. Two §9 entries. **Jake chose this** over poll-traits-plus-adapters and over de-genericizing to an `AsyncStream` union. Forced by two facts: `Http2Connection<S>` / `WebSocket<S>` hold `inner: S*` (BORROWED — `E0455` rejects that across a suspend, so the transport must be owned), and `async` on a trait method is currently rejected outright (`E0364`, parser). Design: `async read(&this, n) -> T` desugars to an implicit associated type plus a projection return (`type ReadFut; read(&this, n) -> This::ReadFut`), each impl binding it to its own synthesized future via the existing positional trait-arg sugar. Jake's scope calls (2026-07-26): default bodies ARE in scope (one future generic over `This`), and `S: AsyncRead` alone must IMPLY the future's bound — the latter already works as a consequence of the gap fixes, verified by a probe carrying only `where S: AsyncRead`. |

| 5d | Receiver-pointer refresh at resume (soundness hole opened by 5c) | ✅ DONE+validated 2026-07-26. An `async` method with a `&this` / `mut &this` receiver stores the receiver's ADDRESS in a `this$recv` field of the future it lowers to, written once at construction. That contradicted §4's load-bearing invariant (no self-references ⇒ futures freely movable ⇒ **no address-stability requirement**) and design-review item 6, which says this exact shape must be `E0455`. It was also wrong for a reason stronger than movability: an owning receiver promoted across states is **taken into a fresh block-local on every poll** and handed back on the way out, so the address legitimately changes each poll. Jake chose refresh-at-resume over making the sugar consuming or adopting an address-stability contract. `lower_carrier_sm` now re-addresses `this$recv` from the enclosing frame's own storage immediately before every poll (single site — there is exactly one sub-future stash/poll point). A receiver reached through a pointer is passed through rather than addressed twice. Two shapes that cannot be re-addressed are now rejected instead of silently corrupting: a receiver that names no storage (temporary / call result), and awaiting a method future the awaited expression did not itself produce. 4 tests + 2 negatives. **Proof:** a probe polling by hand and dirtying the stack between polls returns `15`/garbage pre-fix and the correct values post-fix (pre-fix `FAILMASK=11`, post-fix `0`), and the emitted IR shows `store ptr %h, ptr %fieldptr` into `this$recv` ahead of the poll. |

| 5e | Receiver refresh for a GENERIC owner (silent miscompile) | ✅ DONE+validated 2026-07-26. An `async` method on a **generic** struct lost every write made through `mut &this` — the exact shape of every consumer §5b-port has to port (`Http2Connection<S>`, `WebSocket<S>`), so the port would have been built on sand. Distinct from 5d, not a gap in it: the axis is the owner being generic, **not** the suspend (generic+sync ✅, concrete+async ✅, generic+async ❌ with or without a suspend). Cause: `awaited_recv_ptr_type` looked for the `this$recv` slot on the awaited future's arena struct, but a future generic in its owner arrives as an **`InstantiatedType`, not a `Struct`** (specialization fills its fields, and that runs after sema), so the guard rejected it and 5d's refresh was silently skipped — the method then wrote through a pointer to a block-local the frame had stopped using. Fix: resolve to the TEMPLATE via `arena.inst_generic_base` for the "is there a slot" question, and take the pointer's TYPE from the receiver place (the template's still mentions the owner's parameters, so it cannot type a node in an already-concrete caller). `PendingThenReady<i64>` is an `InstantiatedType` too and keeps being rejected — its base has no `this$recv` — which guards against over-firing. 3 tests. **Proof:** the 4 existing 5d tests only READ through the receiver and cannot catch this; the new ones observe a WRITE from the caller, and reverting the compiler change makes 2 of the 3 FAIL (`81` vs `82`) and restoring it makes them pass. IR confirms one `store ptr %"<local>", ptr %fieldptr` per awaited method receiver where the generic ones previously had none. |

| 5f | `?` inside `async` (blocker found while designing §4's traits) | ✅ DONE+validated 2026-07-26. **The `?` operator did not work in ANY `async` function** — `E0235` on the simplest possible form, in the pinned compiler too, because no test and no stdlib code had ever used `?` in an `async` fn. A hard blocker on §5b-port, whose ~3,300 consumer lines are `?`-dense. Five defects: (1) the shape gate re-judged an already-typed `?` against the lowered `poll`'s `Poll<Output>` return, since sema walks each module twice and `lower` moves the body into `poll` in between; (2) `TryExpression` was a blind spot in NINE of the lowering's ten expression walkers — an uncounted `await` in a `?` operand sent the body down the no-await path, the desugar's `Err`-arm `return` was never wrapped in `Poll::Ready`, and `rn_expr` alpha-renamed the operand twice; (3) `ASTCloner` SHARED `PatternElement::Binding` pointers between a trait default body and its per-impl clones while the lowering renames them in place, so a bound `match` arm in an `async` default body with an `await` failed `E0201` in every impl; (4) a `?` in a GENERIC body is first desugared INSIDE the already-lowered `poll` (its operand type is abstract until specialization), so that desugar now wraps its own `return`; (5) cloning split the `desugared.subject IS operand` invariant that call specialization (walks `operand`) and codegen (emits `desugared`) both depend on — which broke `hashmap.cryo`'s `alloc_entry(...)?` at codegen. 10 tests. **Proves the §4 shape**: a trait with sync `buffered`/`consume` + `async fill`, async default bodies using `?`/loops/match bindings, and a `Codec<S>` that owns its transport and mutates through `mut &this` across suspends. |

| 5g | `AsyncRead` / `AsyncWrite` in `stdlib/` (§4 of the handoff) | ✅ DONE+validated 2026-07-26. **PATH CORRECTION (2026-07-27): `stdlib/io/async_traits.cryo` NO LONGER EXISTS — these traits now live in `stdlib/io/traits.cryo` beside `Read`/`Write`, and there is no `io::buf_conn`; `BufStream<S>` is in `stdlib/io/buf.cryo`.** As originally written: `stdlib/io/async_traits.cryo`, registered in `io/_module.cryo`. Shape is Jake's 2026-07-26 call — the connection owns a persistent buffer and **no buffer crosses the API**. `AsyncRead` requires `async fill` + sync `buffered`/`consume`; `ensure` / `read_exact` / `read_some` / `read_until` / `read_line` / `skip` are default bodies over them, with the scan-and-copy steps factored into SYNC helpers (`scan_for`, `take_front`) so a `buffered()` slice is provably never live at a suspension point. `AsyncWrite` inverts the split: sync `pending()` + generic `queue<T>` (`static match` over `Slice<u8>`/`Str`/`string`/`u8`) so an encoder builds a whole frame without suspending, and one `async flush`. 10 tests against in-memory doubles whose fill chunk is 1–4 bytes, so every read spans several fills and moves the buffer's heap block. Found and fixed a pre-existing compiler miscompile on the way (5h). |

| 5h | Method turbofish ignored for a literal argument (silent miscompile, PRE-EXISTING) | ✅ DONE+validated 2026-07-26. `recv.m<u8>(0x42)` — explicit turbofish, bare integer literal — bound to a **sibling specialization** of the same method and passed the literal in that spec's parameter shape: observed as `queue<u8>(0x42)` dispatched to `queue<Slice<u8>>` with `inttoptr (i32 66 to ptr)`, i.e. 66 handed over as a pointer → access violation. No diagnostic; a typed local worked, so only a literal argument exposed it. Reproduced identically under the PINNED compiler, so it long predates the async work — it had simply never been hit, because the one stdlib method of this shape (`io::Write::write<T>`) is never called with a literal at a type its `static match` also has a `Slice<u8>` arm for. Cause: `substitute_explicit_generic_param_types` handled only the free-function form (turbofish on the CALL node, callee an identifier). A method call carries its turbofish on the MEMBER ACCESS, so the formal `data: T` stayed abstract, no expected type reached the argument, and the literal fell back to `i32` — and for a generic callee the argument's type is what SELECTS the specialization. Fix: substitute a method call's turbofish into the method's formals by re-resolving each parameter annotation with the owner args and the method's own params bound (handles a param nested in another type for free), and force the expected type onto exactly the arguments whose formal came from a turbofish — the literal-clearing default is survivable for a non-generic callee (the call boundary has an integer width coercion) but not for a generic one. 5 tests; pre-fix they return `71` instead of `70`. |

Legend: ☐ not started · ◐ in progress · ✅ done+validated · ⏸ blocked (note why in the Progress Log).

**Current HEAD baseline:** `eda79ddc` (the two root-cause compiler fixes of 2026-07-24 — `drop_insertion`
no longer running destructors on uninitialized memory, and the async "rebuilt from inside a branch" E0600
removed — committed by Jake). Branch `ll-impl`. `make stdlib` = 149 modules green. Phases 0–3 done +
committed.

**✅ The Windows AFD reactor is VALIDATED (2026-07-25) — 30/30 runs, 0 failures, 0 hangs.** Real Windows
host, async TCP echo over loopback on a 2-thread `Executor` driving `TcpAccept`/`TcpRead`/`TcpWrite`/
`TcpConnect` through IOCP + `\Device\Afd` + `IOCTL_AFD_POLL`. **The hang really was a wine limitation, not a
bug in the backend** — the backend needed no changes. Getting there required one root-cause compiler fix
(qualified-global resolution, see the newest §9 entry), because `set_nonblocking` was silently using the
Linux `FIONBIO` on Windows.

**UNCOMMITTED on top of that (2026-07-25, see the newest §9 entry):** the async lowering's
**`MatchExpression` blind spot is closed** — every walker in `async_lower.cryo` that descends into a
`MatchStatement` now descends equally into a `MatchExpression`. That clears both reported symptoms (a value
read only as a match subject was never carried across a suspend → E0201; a `return` inside a match-expression
arm was never rewritten to `Poll::Ready` → E0200 leaking the internal `Poll<T>`) **and a third, unreported
silent miscompile**: `has_free_edge` was blind too, so a `break` in a match-expression arm inside an awaiting
loop compiled to a native `break` that escaped the state-dispatch loop and trapped at runtime. `make test`
OVERALL PASS (1633 unit / 144 compile-fail / 9 projects), both selfhost fixed points, repin delta **0/235
compiler + 0/149 stdlib on both OS**, REPINNED both OS. **This was the last blocker on the async-only socket
port** — the idiomatic Cryo unwrap of every async socket `Result` is a match expression.

**UNCOMMITTED (2026-07-24) — four correctness/usability items on top of the above; see the newest §9 entry
for the reasoning behind each.** (1) A raw pointer into the poll frame held across an `await` was a **silent
miscompile** and is now `E0455`, with the sound shapes (pointer parameter, dynamic-array heap block, global)
kept legal and tested. (2) `async function … -> void` now works — it was broken in *every* form, not just
with awaits, and lowers with `Output = ()`. (3) **Declaration order no longer matters**: `lower` is split, and
a new `declare` gives every `async fn` in the module its future type before any body is typed, which retires
the misleading "`i64` does not implement `Future`" `E0306`. (4) That split exposed **recursive async fns as a
silent miscompile** (they compiled, then re-polled a completed sub-future); direct and mutual recursion are
now rejected at the `await` that closes the cycle. `make test` OVERALL PASS **1648 / 150 / 9** (from
1633/144/9). Compiler-only — `stdlib/` has zero `async function`s, so no stdlib IR moves.

**✅ `async` METHODS WORK (2026-07-25) — `async` is a method modifier, not only a function one.** All three
receiver forms (`this`, `&this`, `mut &this`), the implicit receiver, `static async`, methods delivered by an
impl block, methods on a `type class`, and methods on a GENERIC owner (including a generic method on one, so
the future carries the owner's parameters and its own). Declaration order is irrelevant for methods too, and
an async method's future runs on a real multi-thread `Executor`. `async` on a constructor, destructor, field,
trait method, or `virtual`/`override` method is rejected as **`E0364`**, a new code, with a message that says
why. 17 permanent tests in `tests/tests/lang/async_method.cryo` + 5 negative tests. See the newest §9 entry.

**✅ Generic `async function` WORKS (2026-07-25) — the stopgap `E0600` and its negative test are deleted.**
Declared, instantiated at several types (including a struct and `f64`), inferred without a turbofish,
awaited, `block_on`-able, awaiting concrete futures, awaiting futures named by its own parameter
(`Ready<T>`), and awaiting other generic `async function`s. `-> void`, branches, loops, and a droppable
aggregate carried across a suspend all work generically. 12 permanent tests in
`tests/tests/lang/async_generic_function.cryo`. Two root causes, both fixed in a shared layer rather than at
a call site — see the newest §9 entry. **The reusable lesson stands and is now enforced by construction:
a synthesized annotation built by `make_type_ann` is pre-resolved and therefore invisible to
monomorphization's substitution**, so `AsyncLower::type_ann_for` now spells any type mentioning a parameter
BY NAME and pre-resolves only fully-concrete subtrees.

**✅ FIXED (2026-07-23) — `block_on` (and every where-bound-only-param generic) now infers without a
turbofish.** Was: `block_on(fut)` — and any `f<F, R>(fut: F) -> R where F: Future<R>` — failed **codegen
`E0636`** when `R` appeared only in the return type + where-bound (never a parameter), because argument
inference bound `F` but nothing projected `R = F::Output`; the "all bound" gate then bailed without stashing,
so mono couldn't specialize. Fix = a **bound-directed projection pass ("PASS D")** in
`CallResolver::check_generic_free_call` + its qualified twin `infer_free_call_bindings`: after arg inference,
for each where-bound `P: Trait<…, R, …>` whose subject `P` is bound to a concrete type, project the trait's
positional-sugar associated type off `P` (`resolve_concrete_member(P, "Output")`) and fill the still-unbound
arg param `R`. Mirrors the impl-side `TypeResolver::derive_where_assoc_bindings`. Now `block_on(fut)` (incl.
on an **unnameable async future** — `block_on(add_two())`), `future::block_on(…)`, and `from_iter(it)` all
resolve from arguments alone; turbofish / expected-type / non-`Future`-rejection paths unchanged. Gates: both
OS fixed points, `win-s2` vs `win-s3` = 0 `.ll`, Linux normalized pinned-vs-new = 0 residual → **no repin**.
**The inline `loop { match f.poll(&cx) }` async-validation workaround is retired — use `block_on`.**

**Generic-METHOD analogue — deferred (NOT a simple mirror; deeper pre-existing blockers).** A method
`m<F, R>(fut: F) -> R where F: Future<R>` (static `Owner::drive(fut)` or instance `obj.run(fut)`) is a bigger
job than the free-fn fix: (1) `infer_generic_method_bindings` (the function the free-call fix's twin lives
next to) is **not even reached** for these calls — static generic-method calls resolve through a different
scope-resolution path; and (2) generic methods of this shape **fail even WITH a turbofish** (E0633/E0636
cascade), and even a trivial `Bo::id<i32>(42)` on a non-generic owner fails **E0200** (return-type
substitution) — so the generic-method-with-where-bound machinery has multiple independent gaps, of which
inference is only one. Fixing it means untangling the static/instance generic-method resolution+mono paths,
not adding one projection pass. Left as a separate effort. The free-function fix (which covers `block_on`,
`from_iter`, and any `f<F,R> where F: Trait<R>` free call) fully lands the async driver.

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

- _2026-07-23_ — **`block_on` where-bound-only inference — FIXED (general generic-inference fix, not async).
  No repin. UNCOMMITTED.** Baseline HEAD `5f2776af` (Inc 3, committed by Jake). Two files: NEW `PASS D` in
  `sema/call_resolver.cryo` (+82; `project_where_bound_params` + `param_name_index` + `where_arg_param_index`,
  wired into both `check_generic_free_call` and `infer_free_call_bindings`) and `InferCtx::bind_slot` in
  `types/inference.cryo` (+10). **Root cause:** for `f<F, R>(fut: F) -> R where F: Future<R>`, `R` is a
  functional dependency of `F` (positional sugar `Future<R>` = `Output = R`) that appears in NO value
  parameter; PASS A bound `F` from the arg but nothing derived `R`, and the `all_bound` gate bailed to
  `invalid` WITHOUT stashing type args → mono skipped the specialization → codegen `E0636`. Expected-type
  context (`const r: i32 = block_on(fut);`, return position) already recovered `R` via PASS B, so the bug only
  bit in context-free position (`block_on(fut) != 42`). **Fix:** after PASS A/B/C, project each still-unbound
  where-bound arg param off its now-bound subject — resolve the trait decl (`get_trait_decl`), map the
  positional arg slot `ai >= trait.generic_params.length` to `trait.assoc_types[ai - ngp]`, and
  `resolve_concrete_member(subject, assoc_name)` (the same projection `resolve_await` uses). **Landmine paid:**
  the subject guard must be `!contains_generic_param(subject)`, NOT `free_infer_type_concrete` — the latter
  rejects a *pre-mono* `InstantiatedType` (`Ready<i32>`) whose monomorphized form doesn't exist yet
  (`has_resolved_type()==false`), which is exactly the state the arg is in during sema; `resolve_concrete_member`
  handles the pre-mono instantiation fine. **Validation:** `block_on(Ready<i32>::new(42))` (the bug),
  `future::block_on(…)`, `block_on(PendingThenReady<…>)`, **`block_on(add_two())` on an unnameable async
  future** (the real payoff — retires the inline-poll workaround), `String`-output, and `from_iter(it)` (the
  other in-tree instance: `T = I::Item`) all → EXIT 0; turbofish + expected-type unchanged; non-`Future` arg
  still rejected (E0306/E0636, no miscompile). **Gate:** `make selfhost-check` → exit 0, **BOTH fixed points**
  (Linux `s3`==`s4`; Windows `FIXED POINT OK`, 235 modules — after freeing an OOM-pressuring `cryolsp` that
  environmentally SIGTERM'd the wine build twice). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0
  differing `.ll`**; Linux normalized pinned-vs-new = 0 residual (only `FILE.str` `__FILE__`-path artifacts)
  → the fix is inert for all existing code (every call that compiles today already binds all params, so PASS D
  fills nothing) → **NO REPIN**. **Follow-up (investigated, deferred):** tried to mirror this onto the
  generic-METHOD path and reverted — `infer_generic_method_bindings` is **not reached** for static
  generic-method calls (`Owner::drive(fut)` resolves via a different scope-resolution path), and generic
  methods of this shape fail even WITH a turbofish (E0633) — even trivial `Bo::id<i32>(42)` fails E0200. The
  generic-method-with-where-bound path has multiple independent pre-existing gaps beyond inference; it needs
  its own effort (see §1). Reverted cleanly to the validated free-fn-only fix (call_resolver +82, inference +10).

- _2026-07-23_ — **Inc 4 design SIGNED OFF by Jake (one-way door closed). Building Inc 4a.** Baseline HEAD
  `ce7456c6`, pin OK, tree clean. Took §10's two options to Jake; he chose the **dispatch-loop state machine
  (option a)** — with one refinement: the dispatcher is a **`match (this.state)`, NOT a C-style `switch`**
  (there is no `switch` in Cryo; `switch` in §10 was pseudocode). Also a standing style note from Jake:
  **emit bare integer literals (`1`), never `1u32`** — the compiler infers the integer type from context
  (the existing `u32_lit` helper already emits bare text `"1"` with a u32 `resolved_type` hint, so generated
  code is already compliant; keep it that way). Feedback memory [[prefer-bare-int-literals]].
  **The agreed generated shape** (replaces Inc-3's forward-only `if (this.state==i)` fall-through chain):
  ```
  poll(mut &this, cx: Context*) -> Poll<Out> {
      <arg-shadow prelude>                         // const p = this.p; …  (outside the loop)
      loop {
          match (this.state) {
              0 => { <block 0>;  this.state = k; }   // transition: set state, FALL THROUGH the match →
              …                                      //   the enclosing loop re-iterates → re-matches new state
              j => { … return Poll::Pending; }       // suspend: real return exits poll
              n => { return Poll::Ready(v); }         // completion
              _ => { return Poll::Pending; }          // unreachable state guard (exhaustiveness)
          }
      }
  }
  ```
  **Key realizations (durable, drove the design):**
  - **No `continue`/`break` synthesis needed for the dispatcher.** A `match` arm is a statement whose body,
    if it doesn't return, falls through to the end of the `loop` body → the `loop` re-iterates → re-matches
    the (just-updated) `this.state`. So a *transition* is just `this.state = k;` with no terminator; only
    *suspend* (`return Poll::Pending`) and *completion* (`return Poll::Ready`) are real returns. This makes
    every edge — forward, branch, and (Inc 4b) back-edge — the uniform `this.state = k;`.
  - **`loop {}` with no `break` DIVERGES per Cryo's checker** (proof: stdlib `block_on` is
    `fn -> R { loop { … return v … } }` with no trailing return). So the dispatch loop needs NO trailing
    `return Poll::Pending` fallback for E0403 (the Inc-3 fall-through chain needed one because it wasn't a
    loop). The `_ =>` arm supplies match exhaustiveness and a safe no-progress result for a corrupted state.
  - **`match` on a raw integer with literal + `_` arms is first-class and used in stdlib** (errno match
    `stdlib/thread/_module.cryo:413`: `1 => …  11 => …  _ => …`). Synthesis recipe mirrors the parser:
    `MatchStmtNode(subject=this.state, span)` + `add_arm`; each arm `MatchArmNode(span)` +
    `add_pattern(PatternNode(PatternKind::Literal))` with `set_value(intern("0"))` +
    `set_literal_kind(LiteralKind::Integer)` (bare, no suffix), or `PatternNode(PatternKind::Wildcard)` for
    `_`; `set_body(block)`. `LoopStmtNode(body, span)`. This is the ONE new-AST risk (Inc 2/3 deliberately
    avoided synthesizing `match` — used `if` + `Option::take().unwrap()` — because the injected `poll` is
    re-type-checked POST-mono and every node must resolve by name; the errno precedent proves the shape
    type-checks from source, so it should re-resolve).
  **Staged rollout (each = build → validate under `block_on` → selfhost gate → `.ll` diff → no repin):**
  - **Inc 4a — acyclic (if/else across a suspend) + the whole `loop { match }` machine.** Internally
    two-stepped to isolate risk: (4a-1) *replace* the Inc-3 fall-through builder with the `loop { match }`
    dispatcher preserving EXACT straight-line semantics — re-validate the Inc 2/3 probes (proves the match/loop
    synthesis on known-good cases); (4a-2) generalize `build_plan` + the builder to allocate states for
    if/else branches + a join, with generalized cross-await promotion (a local live from one state into a
    different state → field; acyclic + single-assignment const ⇒ decl-state dominates all read-states ⇒ the
    Inc-3 store-after-decl / load-at-reader model still holds). Loops still hard-error E0600.
  - **Inc 4b — loops** (`while`/`for`/`loop`/`do-while`): back-edges + `break`/`continue` → state transitions
    + **mut loop-carried promotion** (deferred by Inc 3). Plan: a promoted `mut` local → rewrite every
    occurrence (read AND write) to its `this.field` in place, so the field is the single source of truth (no
    load/store-placement dance). Still scalar-Copy only; aggregate/non-Copy stays E0600.
  - **Inc 4c (deferred) — `await` inside a `match` arm** (pattern bindings live across a suspend). Hard-error
    past 4b.
  **Risks to validate during 4a** (flagged, not hidden): (1) synthesized `MatchStmtNode`+literal patterns
  re-resolving cleanly post-mono; (2) `_` satisfying the exhaustiveness/`no_match` checker (leave
  `unwind_cleanup=[]`; abort path anyway); (3) synthesized `loop`+`match` control flow not confusing post-mono
  MoveCheck/DropInsertion liveness (Inc 3 already runs `if`-blocks through them; loop+match is more CF);
  (4) `-> ()` fall-through end of the last state needs an explicit `return Poll::Ready(())` terminator (the
  loop won't fall off; a non-returning last arm would spin) — value-returning fns already return on all paths
  (pre-lowering E0403), so only the unit case needs the synthesized tail.
  NEXT: implement **Inc 4a-1** (swap fall-through → `loop { match }`, re-validate Inc 2/3 straight-line probes),
  then **Inc 4a-2** (if/else). Do NOT commit; no repin unless the `win-s2` vs `win-s3` `.ll` diff moves.

- _2026-07-23_ — **Inc 4a DONE + validated (both OSes). No repin (0 `.ll` diff). UNCOMMITTED.** Baseline HEAD
  `ce7456c6`, pin verifies OK. One modified source file: `sema/async_lower.cryo` (+404/−353). The whole poll
  lowering was **re-architected** from Inc-3's forward-only `if (this.state==i)` fall-through chain onto the
  agreed **`loop { match (this.state) { i => <block i>, _ => Poll::Pending } }`** dispatcher, and a **recursive
  state-machine builder** now handles `await`s across `if`/`else`. Done in two internal steps (each a green
  boundary):
  - **4a-1 (swap dispatcher, same surface).** Replaced the fall-through wrapping with `loop { match }`
    (`match_arm_int`/`match_arm_wild` synthesize bare integer-literal + `_` patterns, mirroring
    `expr_parser.cryo:1939`; `LoopStmtNode` wraps it). A transition is now a bare `this.state = k;` that falls
    off its arm → the `loop` re-dispatches (no `continue` synthesis needed — a non-returning `match`-statement
    arm falls through, exactly like stdlib `block_on`'s `Pending => {}`). Removed the Inc-3 trailing
    `return Poll::Pending` (the `loop` diverges, so E0403 is satisfied — `block_on` precedent). Re-validated
    the Inc 2/3 straight-line probes → EXIT 0.
  - **4a-2 (branches).** Replaced the flat `build_plan` + `build_poll_body_multi_await` + `emit_loads`/
    `emit_stores` + `AsyncPlan` with: a **`PollSm` accumulator** (config + `blocks` + `ok` flag + discovered
    `fut_*`/`prom_*` inventory) and a recursive builder — `build_poll_body_sm` → `lower_block_sm` →
    `lower_stmt_sm` dispatching to `lower_carrier_sm` (top-level await → resume state) / `lower_if_sm`
    (`if`/`else` → per-branch entry states + a join; else-if via recursion; no-else → false path goes straight
    to join). `sm_alloc` mints states on demand; `sm_goto` emits a forward `this.state = k;`.
    `stmt_diverges`/`block_diverges` suppress the join transition after a branch that returns (else it would be
    dead code after a `return`). `lower` was reordered: **create the struct `TypeRef` → build the body
    (discovering the inventory) → set the fields** (the field types are known only after the build; member
    accesses resolve by name post-mono, so fields need only exist by then).
  - **Promotion re-done as build-then-scan** (`promote_cross_state`): after the blocks are built, a local read
    in a state other than its declaring one is promoted — store appended to the declaring block, a
    `const <name> = this.<field>;` load **prepended** (`prepend_load` rebuilds `blk.statements`) to each other
    reading block. This subsumes Inc-3's carrier-operand attribution *for free* (an await operand is physically
    emitted into the previous state's block, so a local read only there is same-state → not promoted) and
    naturally covers a pre-`if` local read inside a branch (over-promotes across a branch dispatch even without
    a suspend on that path — harmless, the store dominates). Same deferrals as Inc 3: `mut`-cross-state →
    E0600, aggregate → E0600, reference → E0455 (guard **moved verbatim** into `promote_cross_state` — same
    code path; the 4 E0600 negatives were re-probed, E0455 was not separately re-probed this session).
  - **Durable realizations:** (1) `match`-statement arms **don't fall through C-style**; a non-returning arm
    runs to its end then control leaves the `match` → the enclosing `loop` re-iterates → this IS the transition
    mechanism (no `goto`/`continue`). (2) `loop {}` with no `break` **diverges** per Cryo's checker (stdlib
    `block_on` proof), so no trailing return is needed; the `_ =>` arm is only for exhaustiveness + a corrupted
    state. (3) An `async fn` with awaits that can **fall off its end** (implicit unit return) is rejected here
    (E0600) — it would spin the loop / re-poll a taken sub-future; value fns always return (pre-lowering
    E0403), so this only bites the deferred unit case. (4) Pushing to a `PollSm*` array field
    (`sm.blocks.push`) mutates in place across recursive calls (same pattern as `BlockStmtNode.add_statement`).
  - **Validation (via `block_on`, no turbofish):** straight-line probe (`add_two`/`compute`/`chain`/`dep` +
    no-await `answer`) → EXIT 0 (no Inc 2/3 regression). Branch probe → EXIT 0: `pick` (await in one branch),
    `both` (await in both, each returns), `prelocal` (pre-`if` local promoted across the branch dispatch AND a
    suspend), `grade` (else-if chain), `guard` (no-else `if` whose then awaits+returns, with further awaits
    after the `if` carried by the join). Negatives all fire: await-in-loop / await-in-`if`-condition /
    `mut`-cross-state / nested-await-in-expression → E0600.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4`; Windows
    `s3`==`s4` byte-identical IR). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0 differing `.ll`** →
    the lowering is inert for async-free code → **NO REPIN**. Pin verifies OK; tree = `M async_lower.cryo` +
    `M ASYNC_IMPL.md`.
  NEXT: **Inc 4b — loops** (`while`/`for`/`loop`/`do-while` with an await in the body). The `loop { match }`
  dispatcher already supports back-edges (a back-edge is just `this.state = <header>;` — the same forward-edge
  primitive), so 4b = add `lower_while_sm`/`lower_for_sm`/`lower_loop_sm` (header/body/after states + the
  back-edge) + rewrite user `break`/`continue` to state transitions + **`mut` loop-carried promotion** (the one
  genuinely new piece Inc 3/4a deferred: a promoted `mut` local → rewrite every read AND write to `this.field`
  in place, so the field is the single source of truth — no load/store dance). Still scalar-Copy only;
  aggregate/non-Copy stays E0600. [4c: `await` inside a `match` arm, deferred.]

- _2026-07-23_ — **Inc 4b DONE + validated (both OSes). No repin (0/235 `.ll` diff). UNCOMMITTED.** Baseline
  HEAD `61f5beaa` (Inc 4a committed+repinned by Jake), pin verifies OK. One modified source file:
  `sema/async_lower.cryo`. `AsyncLower` now lowers **awaits across all four loop forms** (`while`/`for`/`loop`/
  `do-while`) with `break`/`continue` and **`mut` loop-carried promotion** — completing common-control-flow
  async lowering (only Inc 4c, `await` inside a `match` arm, remains). Built on Inc 4a's `loop { match
  (this.state) {…} }` dispatcher: a back-edge is just another `this.state = <header>;` forward-edge, so no new
  dispatch mechanism was needed.
  - **Loop lowering** (`lower_while_sm`/`lower_for_sm`/`lower_loop_sm`/`lower_do_while_sm`): each allocates
    header/body-entry/[update]/after states. `while` = header tests cond (dispatch body-entry vs after) + body
    + back-edge to header. `for` = init lowered into the current state, then header/body/update/after; the
    update state runs the update expr then → header (so `continue` → update → re-test, correct). `loop{}` =
    body-entry with a self back-edge (infinite; only `break` exits to after). `do-while` = body-entry (run
    unconditionally) → tail-test state → after. Shared `emit_cond_dispatch` appends `if (cond) { state=t } else
    { state=f }`. Await in a loop condition/update/`for`-init → E0600.
  - **`break`/`continue`** → `this.state = <target>;` edges via a **loop-target stack** on `PollSm`
    (`loop_cont`/`loop_brk`, pushed/popped around each exploded loop's body). A native `break`/`continue` would
    break the dispatch `loop`/`match`, so they must lower to edges. **The stack top is always the innermost
    EXPLODED loop:** an await-free nested loop is emitted wholesale (its break/continue stay native), so
    break/continue only *reach* the state-machine lowerer when they target a loop currently being exploded.
    Continue targets: `while`/`loop` header, `for` update state, `do-while` tail test; break target: the after
    state.
  - **`stmt_needs_explode`** now gates wholesale emission on `await OR a free break/continue` (not await
    alone): an await-free `if (c) { break; }` inside an await-carrying loop must still be exploded (its `break`
    can't stay native inside the dispatcher). `has_free_edge` walks if/block/match but NOT nested loops (they
    capture their own break/continue).
  - **Divergence moved to SOURCE AST.** `stmt_diverges` now covers `break`/`continue` (transfer control
    elsewhere) and an infinite `loop{}` (`!has_free_break`). `lower_if_sm`'s join-suppression AND the loop
    back-edges now test `stmt_diverges(SOURCE branch/body)`, not the lowered block's tail — a break-terminated
    branch leaves a `this.state=<after>;` edge in its final state block, which is NOT a divergence marker, so
    inspecting the lowered block would wrongly append a second (overwriting) edge and lose the break.
    `lower_block_sm` also stops after a source-diverging statement (unreachable-after-terminator would
    otherwise be emitted PAST the edge assignment and run before the arm falls off).
  - **`mut` loop-carried promotion (the genuinely new piece).** A scalar-Copy `mut` local read/written across
    states becomes a struct field that is the **single source of truth**: `promote_cross_state` (build-then-
    scan) branches on mutability — const keeps the Inc-3 store-at-decl / load-at-reader model; **mut** replaces
    its declaration with `this.<field> = <init>;` (`rewrite_mut_decl`) and rewrites every read AND write of the
    name to `this.<field>` in place (`subst_name_expr`/`subst_name_stmt`, the mutating twins of the read-
    detection walker — an assignment's LHS is an identifier, so one rewrite handles reads and writes uniformly,
    incl. `i++` via the unary-operand case). Same guards: reference across suspend → E0455; aggregate/
    non-scalar → E0600. New guard: a `mut` name re-declared in another state → E0600 (the by-name rewrite
    can't disambiguate two live ranges).
  - **Validation (via `future::block_on`, no turbofish):** scratchpad `async_inc4b/` drives 9 cases → EXIT 0:
    `while`+mut counter/accumulator (immediate-Ready await), `while` with a genuine 2-Pending-per-iteration
    suspension, `for`+`continue`(skip)+`break`(early exit)+suspend, `loop{}`+`break`, `do-while`, a **nested**
    await-loop (`for` inside `while`; `total`/`i`/`j` all promoted), plus straight-line (Inc 3) and if/else
    (Inc 4a) regressions. Negatives (`async_neg4b/`) fire clean E0600: await-in-`while`-condition, aggregate-
    mut-across-await. Gotcha: a `for`-init needs an explicit type annotation (`for (mut i: i32 = 0; …)`) —
    Cryo E0104; statement-level `mut x = 0` infers, a for-init does not.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4` md5
    `5bacb46b83c166bc759794ee338a8c33`; Windows `s3`==`s4`, 235 modules). `win-s2` (pinned-built) vs `win-s3`
    (new-built) = **0/235 differing `.ll`** → the lowering is inert for async-free code → **NO REPIN**. Pin
    verifies OK; tree = `M async_lower.cryo` + `M ASYNC_IMPL.md`.
  - **Durable limitation (shared with the const path since Inc 3/4a — NOT introduced by 4b) — ✅ RESOLVED
    2026-07-23 by the scope-aware alpha-rename (see the next Progress-Log entry):** cross-state promotion WAS
    **by-name**. It under-promotes safely in most cases (a missed read surfaces as a loud "not found"), but a
    promoted local that **shadowed a same-named parameter** (or a sibling-scope local of the same name) could be
    conflated by the global rewrite → a silent wrong value. Now fixed: a pre-flattening alpha-rename makes every
    local's name globally unique first, so the by-name promotion is exact for both paths.
  NEXT: **Inc 4c — `await` inside a `match` arm** (pattern bindings live across a suspend); currently E0600.
  After 4c, Phase 2 is complete → **Phase 3** (executor + `spawn`/`JoinHandle` + multi-thread + poll-boundary
  `catch_unwind` isolation).

- _2026-07-23_ — **Cross-state promotion is now scope-aware — the by-name param/sibling conflation is FIXED. No
  repin (0/235 `.ll` diff). UNCOMMITTED.** Baseline HEAD `ad20987e` (Inc 4b committed+repinned by Jake), pin OK.
  One modified file: `sema/async_lower.cryo` (+~230). Closes the durable limitation flagged in the Inc 4b entry.
  - **Root cause:** promotion (`promote_cross_state` + `subst_*`) rewrites by NAME, and `IdentifierNode` carries
    only a `name` (no resolved-decl link), so after the body is flattened into state blocks — where lexical
    scope structure is gone — a param read (`mut sum = p`) and a shadowing local (`mut p` inside a loop) both
    read as `Identifier(p)` and were rewritten to the same field. Confirmed: `shadow(100)` returned 6 (0 + 1+2+3)
    instead of 106 (100 + 1+2+3). Sibling same-name locals hit the `mut` re-declared guard (spurious E0600).
  - **Fix = a pre-flattening scope-aware alpha-rename** (`disambiguate_locals`, first thing in
    `build_poll_body_sm`, BEFORE the body is flattened). It walks the original NESTED body with a lexical scope
    stack (`RenameCtx`: parallel `orig`/`repl` binding stack + `marks` scope boundaries) and renames every local
    `VarDecl` (block + `for`-init) to a globally-unique name (`<orig>$L<n>`), rewriting uses via innermost-first
    scope lookup. **Parameters are intentionally NOT renamed** — their uses fall through unchanged and resolve to
    the shadow prelude (`const <param> = this.<param>`), so a local shadowing a param no longer shares its name
    once flattened. After the rename every binding is uniquely named → the by-name promotion is EXACT and the
    `mut` re-declared guard never fires for legitimate siblings. (Alpha-rename, not decl-identity keying, because
    identifiers have no decl link — renaming reconstructs the disambiguation the lost scope structure provided.)
  - **In-place + total coverage:** the walk mutates `IdentifierNode.name` / `VarDeclNode.name` in place (no slot
    reassignment). `rn_expr` covers EVERY expression form that can hold a local read (the full cloner kind list:
    identifier/binary/unary/ternary/if-expr/call/new/cast/struct-lit/array-lit/tuple-lit/array-access/member/
    typeof/delete/await/try/match-expr/lambda + the `desugared_call` operator-overload slots); `rn_stmt` covers
    block/unsafe-block/if/while/do-while/for/loop/match/switch + decl/expr/return. Scopes: block, branch (a bare
    non-block branch gets its own frame), `for`-init (encloses cond/update/body), match-arm (pattern
    `binding_name`s shadow), lambda (params shadow + captured-name rewrite). An unwalked form leaves a renamed
    local's use un-renamed → a loud "not found", never a silent miscompile.
  - **Validation:** `async_shadow/` → EXIT 0: `shadow(100)=106` (was 6), `sib(true/false)=7/13` (sibling case
    now compiles + correct, was spurious E0600). `async_shadow2/` → EXIT 0: a `for`-init local shadowing a param
    (param read first) and an `if`-branch local shadowing a param. Inc 4b regression (`async_inc4b/`, 9 cases)
    and negatives (`async_neg4b/`, E0600) unchanged.
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, BOTH fixed points (Linux `s3`==`s4` md5
    `5bacb46b83c166bc759794ee338a8c33`; Windows `s3`==`s4`, 235 modules). `win-s2` (pinned-built) vs `win-s3`
    (new-built) = **0/235 differing `.ll`** → the rename is inert for async-free code (the compiler has no
    `async fn`) → **NO REPIN**. (The s3/s4 fixed-point IR size grew vs the Inc 4b baseline only because the
    compiler SOURCE grew +~230 lines — win-s2 and win-s3 grew equally, so they still match byte-for-byte.)
  - **Discovered gap (separate, NOT fixed here):** a BARE block `{ … }` containing an `await` is not lowered
    (`lower_stmt_sm` → E0600); only `if`/loops/carriers explode. Trivial to add (`lower_block_sm` on a nested
    `BlockStatement`) — noted as an Inc 4b follow-up.
  NEXT: unchanged — **Inc 4c** (`await` in a `match` arm), then Phase 3.

- _2026-07-23_ — **Inc 4c DONE + validated (both OSes). No repin (0/235 `.ll` diff). UNCOMMITTED. Phase 2 is
  now COMPLETE.** Baseline HEAD `5e28a74f` (scope-aware fix committed+repinned by Jake), pin verifies OK. Two
  modified source files: `sema/async_lower.cryo` (+304/−27) and `sema/sema.cryo` (+1, the wire). `AsyncLower`
  now lowers **`await` inside a `match` arm**, plus the **bare-block-with-await** warm-up — closing all common
  control flow. Jake signed off the binding-lifetime approach via the question tool: **"promote scalars, defer
  aggregates"** (Option 1).
  - **Warm-up (bare block):** `lower_stmt_sm` gained a `BlockStatement` case → `lower_block_sm` (a nested
    `{ … await … }` block is just a statement sub-sequence; its locals already get their own scope frame from
    `disambiguate_locals`). Was E0600 before.
  - **`lower_match_sm` (the increment):** in `cur`, a **dispatch `match (subj)`** (reusing the original
    patterns + guards) captures each awaiting arm's scalar pattern bindings into fields and sets
    `this.state = <arm entry>`; each arm body runs as its own state sequence and, unless it diverges,
    converges on a shared **join** state — structurally `lower_if_sm` generalized to N arms. An **await-free
    arm runs wholesale** in the dispatch arm (its bindings stay native → aggregate/ref bindings in a non-await
    arm are unrestricted). A **non-exhaustive** match gets a synthesized `_ => { this.state = join; }` so a
    non-matching subject continues past the `match` rather than respinning the (unchanged) dispatch state.
    Await in a match **subject** or **guard** → E0600 (bind it first / restructure).
  - **Pattern-binding promotion (the genuinely new problem).** A payload binding (`v` in `Some(v)`) is an
    lvalue-pointer into the scrutinee with **no cached type** and lives in `cur`'s scope, but its arm body runs
    in a different state — so a scalar binding **read** in the arm body (`name_read_in_stmt`) is promoted to a
    field: the dispatch arm captures `this.__bind_<entry>_<name> = <name>;` (reading the pattern binding, valid
    in the dispatch arm's scope — a scalar read copies the value out), and the arm body's reads are rewritten
    in place to `this.__bind…` via the existing `subst_name_stmt` (reused verbatim from the `mut`-local path;
    field registered in `sm.prom_field`/`prom_tys` like any promoted scalar, zero-init in the ctor). An
    **unused** binding is skipped (no field, no error → an unused aggregate binding in an awaiting arm is
    fine). **Binding TYPE is re-derived** via `PatternResolver::resolve_variant_payload_types` (patterns carry
    no `resolved_type`): `AsyncLower.wire` now takes the already-wired `&this.patterns` (sema.cryo:194); the
    collectors mirror `bind_arm_patterns`/`bind_enum_pattern` (top-level identifier binding → subject type;
    enum payload → `payload_types[j]`; nested `Sub` → recurse). Guards: aggregate binding across a suspend →
    E0600, reference → E0455 (same limits as `mut`/aggregate/reference locals — Inc 3/4b).
  - **Pattern bindings are now alpha-renamed too (soundness).** `subst_name_stmt` is by-name, so a nested
    `match` re-binding the same name (`Some(v) => { await…; match(x){ Some(v)=>use(v) } }`) would be conflated
    → silent wrong value. Fix: `rn_arm` (in `disambiguate_locals`) now renames every pattern binding to a
    globally-unique `<orig>$L<n>` (mirroring the `VarDecl` rename), sharing one fresh name across OR-alternatives
    (`A(v) | B(v)`); const-value identifier patterns (their `binding_name` cleared by name resolution) are not
    renamed. New helpers `collect_pat_binding_names` + `rn_pat_apply` (mutate `PatternBinding.name` /
    `PatternNode.binding_name` through the AST — `binding_at`/`sub_at` return the real payload pointers).
  - **`stmt_diverges` gained a `MatchStatement` case:** a match never falls through if `match_is_exhaustive`
    (via the wired `PatternResolver`) AND every arm body diverges. Without it, an async body ending in an
    exhaustive all-arms-return `match` (no `_`) tripped a spurious "must return on every path" E0600.
  - **Validation (via `future::block_on`, no turbofish):** scratchpad `async_match/` drives 11 cases → EXIT 0:
    scalar match with awaiting arms + no bindings; enum payload binding read after a suspend; mixed
    wholesale+awaiting arms; binding read before AND after a suspend; the **`nested_bind` case (=105)** that
    directly proves the alpha-rename (a nested `match` re-binding `v` — without the rename it returns 5); and a
    match ending the body with all arms returning. `async_bareblock/` (2 cases) covers the warm-up incl.
    cross-state promotion through a nested block. Negatives (`neg_subj`/`neg_guard`/`neg_agg`) fire clean E0600
    for await-in-subject / await-in-guard / aggregate-binding-across-await. The E0455 reference-binding branch
    mirrors the locals path and is present but not separately probed (a reference match binding is hard to
    construct — same note as Inc 4a's E0455).
  - **Gate:** `CRYO_CC=gcc make selfhost-check` → exit 0, **BOTH fixed points** (Linux `s3`==`s4`; Windows
    `s3`==`s4` byte-identical IR). `win-s2` (pinned-built) vs `win-s3` (new-built) = **0/235 differing `.ll`**
    → the lowering is inert for async-free code (the compiler has no `async fn`) → **NO REPIN**. Pin verifies
    OK; tree = `M async_lower.cryo` + `M sema.cryo` + `M ASYNC_IMPL.md`.
  - **Durable notes:** (1) pattern bindings carry NO cached type — re-derive via
    `resolve_variant_payload_types`; they are lvalue-pointers into the scrutinee, so a scalar read in the
    dispatch arm copies the value out (the subject need NOT stay alive across the suspend). (2) Cryo has NO
    tuple patterns — positional bindings exist only as enum-variant payload elements. (3) The dispatch match
    reuses the original patterns/guard (the source `match` node is discarded), so its exhaustiveness == the
    original's; enum non-exhaustiveness is already E0405 pre-lowering, so only integer/partial matches reach
    the synth-`_` path.
  NEXT: **Phase 3 — executor.** `block_on` real single-thread executor + task queue; a `Waker` that re-enqueues
  its task; `spawn(future) -> JoinHandle`; then multi-thread (worker pool over `pthread`) with `catch_unwind`
  at the poll boundary (sound because of Track 2's thread-local panic state). Validate task isolation (a
  panicking task yields an error, siblings survive) under `--panic=unwind`. Remaining Phase-2 tails (all
  deferred with loud diagnostics, none blocking Phase 3): `await` nested in an expression (E0600), aggregate/
  reference locals **and** match bindings across a suspend (E0600/E0455), a `-> ()` async fn whose awaited body
  falls off its end (E0600), `await` in a `match`/loop-condition/guard (E0600).

- _2026-07-23_ — **Phase 3 STARTED — executor surface LOCKED by Jake (one-way door closed).** Baseline HEAD
  `10414486` (Phase 2 / Inc 4c committed+repinned by Jake), pin verifies OK, tree clean. Brought Jake a
  recon-grounded surface proposal (recon sweep: `stdlib/future/*`, `stdlib/thread/_module.cryo`,
  `stdlib/sync/{atomic,mpsc,condvar,mutex}.cryo`, `stdlib/core/panic_unwind.cryo`). Two rounds of sign-off
  (the second after Jake brought a design-review second opinion framed as "Rust's semantics, C#'s allocation
  strategy"). **The seven locked decisions:**
  1. **Module home → `stdlib/future/executor.cryo`** (`namespace std::future::executor`; `public module
     future::executor;` in `future/_module.cryo`). One `std::future` import surface, cohesive with
     Poll/Future/Waker. (`stdlib/async/` is impossible — `async` lexes as `KwAsync`, can't be a namespace
     segment; this is why Phase 1 used `future`.)
  2. **Executor surface → explicit `Executor` handle; `spawn` is a METHOD; free `block_on` stays.** No implicit
     global runtime for v1 (§7-5) ⇒ `spawn` can't be an ambient free-fn (nothing to find) ⇒ it's
     `exec.spawn(fut)`. `exec.block_on(root)` drives root + spawned tasks; free `future::block_on(fut)` remains
     the trivial single-future driver (stack slot, no heap). The explicit handle is ALSO the allocation hook:
     `spawn` boxes into executor-owned storage → `Executor<A: Allocator = GlobalAlloc>` is a natural extension
     (mirror `Mutex<T,A>`) — carry an allocator field from day one. This is the "C# allocation" boundary:
     `block_on` = stack, `spawn` = box.
  3. **`JoinHandle` drop = detach** (mirror `thread::JoinHandle` — reuse the `Shared<T>` + `Atomic<u8>`
     LIVE/DONE/DETACHED 2-actor handshake verbatim). **REFINEMENT (design review): add `JoinHandle::abort()`**
     — detach-only leaves a spawned task uncancellable. `abort()` sets a CANCELLED state; the worker drops the
     boxed future before its next poll (sound TODAY: promoted locals are scalar-Copy-only, so no aggregate
     drops; the only owned fields are `Option<sub-future>`s, which drop correctly via field glue). ⇒ **`join`
     becomes `join(mut this) -> Result<T, JoinError>`** with `JoinError { Cancelled; Panicked(PanicInfo); }`
     (tokio shape; `Panicked` only occurs under `--panic=unwind` — under abort a task panic aborts the process).
     Note: `select`/`timeout` (Phase 4) cancel by DROPPING an un-spawned future one level down (a normal Cryo
     drop), NOT via `abort` — `abort` is specifically stopping an already-*spawned* task from outside.
  4. **Waker wakeup model → synchronous self-wake + re-enqueue.** `Waker.wake(data)` re-enqueues the task;
     validation futures call `cx.waker.wake()` before returning `Pending`. **REFINEMENT (design review): reserve
     the Rust `RawWakerVTable` shape NOW** even though Phase 3 won't exercise it — expand `Waker` to
     `{ wake_fn: (u8*)->void; clone_fn: (u8*)->u8*; drop_fn: (u8*)->void; data: u8* }` with **noop clone/drop**
     (the *correct* semantics for a non-owning Copy waker, not a stub), inline fn-ptrs (NOT a `static` vtable —
     the "non-zero global inits ignored" gotcha makes a static-vtable-const a landmine). Keeps `Waker` POD/Copy
     in Phase 3; provide `wake`/`wake_by_ref`/`clone` methods and use `clone()` at duplication sites so Phase 4
     only fills in the fn-ptr bodies + adds a `Drop` impl (flipping Copy→non-Copy) — no field-layout change mid
     epoll-bring-up. **Design-review correction (durable): "detach forces task refcounting in Phase 3" is
     FALSE** — `thread::spawn` detaches with a 2-state swap, no refcount; Phase-3 self-wake-only wakers never
     escape a `poll`, so no waker outlives a task. What Phase-3 *multi-thread* genuinely needs is a per-task
     **atomic state machine** (IDLE/SCHEDULED/RUNNING/NOTIFIED) to stop two workers polling the same task after
     a mid-poll self-wake — that lives on the task control block and does NOT touch the Waker shape. The
     waker-held **refcount** (clone bumps / drop decrements) is exercised only when a waker is STORED beyond a
     poll = the Phase-4 reactor.
  5. **`poll` stays a public trait method — and that is SOUND for Cryo** (design-review correction, durable).
     Rust gates `poll` behind `Pin<&mut Self>` because Rust futures ARE self-referential (borrows across
     `await` are allowed). Cryo BANNED borrows across `await` (§4, enforced in `move_check`) ⇒ futures have no
     self-references ⇒ unconditionally movable ⇒ the no-Pin soundness rests on the *move-checker ban*, NOT on
     hiding `poll`. A user calling `fut.poll(&cx)` directly and moving the future between calls is sound; even
     polling an aliased copy is memory-safe (panics on double-`take()`, not UB). `poll` is inherently public
     anyway (trait method; hand-written futures impl it; the executor calls it cross-module).
  6. **Async I/O → owned-handle style for v1** (design-review confirmation of §4). The idiomatic Rust
     `stream.read(&self).await; use(stream)` IS a self-referential future (stored read-future field points at
     the `stream` field) → E0455 under the ban. §4 already priced this in ("owned-value rewrites exist"): async
     I/O uses `const (stream, n) = stream.read(buf).await;` (the read future OWNS the stream, hands it back) or
     an `Arc<Resource>` clone — both sound + movable. Ban stays for v1; relax (real Pin/lifetimes) post-v1. Not
     a Phase-3 concern (the executor hardens nothing either way), but consciously accepted now rather than
     discovered at the first `TcpStream` adapter.
  7. **Type erasure (locked mechanism)** — heterogeneous futures share one ready-queue via the `Waker`/
     `catch_unwind`/`thread::spawn` vtable trick: heap-box the concrete `F`, plus a monomorphized poll thunk
     `task_poll<F,O>(fut: u8*, shared: u8*, cx: Context*) -> u8` (0=Pending/1=Ready; stores `v` into the
     type-erased `Shared<O>` via raw `O*`, same no-drop-synth idiom as `thread_body`), addressable via the
     generic-function-reference mechanism (proven by `thread_trampoline<C,T>`). `JoinHandle<O>` wraps the
     `Shared<O>`.
  **Build plan (each: build → validate under `block_on`/inline driver → `make stdlib` green + `verify-pin` OK
  → NO repin — Phase 3 is PURE STDLIB, touches no compiler source, so selfhost IR is definitionally unchanged;
  a full `selfhost-check` is unnecessary unless compiler source is touched):**
  - **(a)** single-thread `Executor` core: mutex-guarded ready-queue + type-erased `Task` + the re-enqueueing
    `Waker` (expanded shape) + `block_on(root)`/`run()` draining on the calling thread. Validate self-waking
    futures complete (observe via atomic counter).
  - **(b)** `spawn<F>(fut) -> JoinHandle<F::Output>` via the `Shared<O>` handshake + type-erasure thunk;
    `join -> Result<T, JoinError>`; `detach`; `abort`; drop=detach. Validate single-thread spawn/join outputs.
  - **(c)** N `pthread` workers (mirror `thread::spawn`) + per-task atomic state machine (no concurrent poll) +
    `catch_unwind` at the poll boundary → task isolation. Validate under `--panic=unwind` in WSL: a panicking
    task yields `Err(Panicked)`, siblings survive.
  NEXT: build stage (a). Start with the expanded `Waker` (grep `Waker {`/`Waker::` callers first), then the
  `Executor`/`Task`/ready-queue in a new `stdlib/future/executor.cryo`.

- _2026-07-23_ — **Phase 3 stage (a) DONE + validated. Pure stdlib, NO repin. UNCOMMITTED.** Baseline HEAD
  `10414486`, pin verifies OK. Two files: `stdlib/future/waker.cryo` (expanded Waker) + NEW
  `stdlib/future/executor.cryo`; `future/_module.cryo` registers the new module (148 stdlib modules).
  - **Waker expanded to the reserved Rust-vtable shape** (decision 4): `{ wake_fn: (u8*)->void; clone_fn:
    (u8*)->u8*; drop_fn: (u8*)->void; data: u8* }`, still POD/Copy, with `wake`/`wake_by_ref`/`clone` methods,
    `noop()` (all-noop), `new(4 args)`, and **`new_nonowning(wake_fn, data)`** (fills identity clone + noop drop
    — the Phase-3 executor constructor; encapsulates the private `waker_noop_clone`/`waker_noop_drop`). Blast
    radius was tiny (only `block_on`'s `Waker::noop()` + the ctors themselves construct a Waker; sample futures
    only receive `Context*`). `make stdlib` green in isolation before layering the executor.
  - **`stdlib/future/executor.cryo` (the single-thread core):** `Task` (type-erased: `poll_fn (u8*,Context*)->u8`
    [1=Ready/0=Pending] + `drop_fn (u8*)->void` + `fut: u8*` box + `next`/`exec`/`queued` queue linkage);
    `ExecInner` (singly-linked FIFO `head`/`tail`); free fns `exec_enqueue` (with a `queued` guard so a self-wake
    during a poll can't double-link) / `exec_dequeue`; `task_wake(data)` (data=Task*, re-enqueues via `exec`);
    monomorphized `task_poll_thunk<F,O> where F: Future<O>` + `task_drop_thunk<F>` + `spawn_task<F,O>` (boxes the
    future via `*fbox = fut` raw-write [no drop-synth, thread_body idiom], takes the generic-fn refs — proven
    addressable-generic-fn shape); `Executor { inner }` with `new`/`spawn_detached<F,O>`/`run` + a `Drop` that
    reclaims still-queued tasks.
  - **`run()` ownership fix (real-solution, not a leak):** a task that returns `Pending` and **did not** arrange
    a wake (its `queued` flag is still false after poll) is **unreschedulable** in this stage (no reactor, no
    stored waker) → reclaimed immediately (its future's drop runs) rather than dequeued-then-leaked. The `!task.
    queued` check is exactly where Phase 4's stored-waker/refcount ownership will move. A self-woke task
    (`queued==true`) is owned by the queue and re-polled.
  - **Validation (`scratchpad/async_exec_a`, built with pinned `bin/cryo.exe` + `CRYO_STDLIB`):** one `Executor`
    + `run()` drives three **distinct** future structs in one type-erased queue → EXIT 0: `WakeCounter`
    (self-wakes N times then adds to a shared `Atomic<i64>`; 0/3/7 pends → 1+20+300), `Immediate` (different
    type, first-poll Ready → +1000), `Parked` (Pending-forever-no-wake, with a `Drop` that bumps a drop-counter).
    Asserts `counter==1321 && parked_drops==1` — proving self-wake→re-enqueue→re-poll, heterogeneous-type erasure,
    round-robin draining, AND reclaim-runs-drop for the unreschedulable task. `make stdlib` green (148 modules),
    `verify-pin` OK. Pure stdlib (no compiler source touched) ⇒ selfhost IR definitionally unchanged ⇒ **NO
    REPIN** (no selfhost-check needed).
  - **Durable notes:** (1) `(*fut).poll(cx)` (method call on a deref'd raw pointer) type-checks — same shape as
    `thread_body`'s `(*shared).state.swap(...)`. (2) A generic **method** (`Executor::spawn_detached<F,O>`)
    delegating to a generic **free fn** (`spawn_task<F,O>`) that holds the generic-fn reference works — no
    method-generic-fn-ref pitfall hit. (3) The probe's u32 arithmetic uses explicit `0u32`/`1u32` suffixes
    (mirroring `ready.cryo`) to dodge literal-inference round-trips; committed stdlib code stays bare per
    [[avoid-suffixed-numeric-literals]] where inference allows.
  NEXT: **stage (b)** — `spawn<F,O>(fut) -> JoinHandle<O>` returning the future's Output via a `TaskShared<O>`
  control block (non-generic `TaskCtl` header at offset 0 so the type-erased executor reads state/cancel without
  knowing O); `Executor::block_on<F,O>(root) -> Result<O, JoinError>` (single-thread: pump the queue on the
  calling thread until root's outcome is set); `JoinHandle` `join`/`detach`/`abort`/`is_finished`, drop=detach,
  with `JoinError { Cancelled; Panicked(PanicInfo); }`. Single-thread `join`/`block_on` **pump** the queue
  (there are no workers yet); stage (c) replaces the pump with a condvar wait + a per-task RUNNING state + the
  `catch_unwind` poll boundary.

- _2026-07-23_ — **Phase 3 stage (b) DONE + validated. Pure stdlib, NO repin. UNCOMMITTED.** Baseline HEAD
  `10414486`, pin verifies OK. `stdlib/future/executor.cryo` rewritten (+~200 lines) to return the future's
  Output via a control block; `waker.cryo` unchanged from stage (a). `make stdlib` green (148 modules),
  `verify-pin` OK.
  - **Control block:** `TaskShared<O> { ctl: TaskCtl; result: O }` where **`TaskCtl` (non-generic: 3×`Atomic<u8>`
    — `outcome`/`hs`/`cancel`) sits at offset 0**, so the type-erased executor reads/writes state through a
    `task.shared as TaskCtl*` without knowing `O`. `Task` gained `dispose_fn: (u8*, u8)->void` (the one
    shared-block op the erased side can't do without `O`: optionally drop the stored `result`, then free the
    `TaskShared<O>` — monomorphized `dispose_shared_thunk<O>`), and `shared: u8*`. `poll_fn` is now
    `(fut, shared, cx)->u8`: on Ready it stores `v` into `(*sh).result` through a raw `O*` (thread_body
    no-drop-synth idiom).
  - **Two-actor handshake (mirror `thread::spawn`)** on `ctl.hs`: `HS_LIVE`→`HS_TASK_FIN` (task finishes) /
    `HS_HANDLE_GONE` (handle detaches/drops). Whoever swaps in last frees `TaskShared`. `ctl.outcome`
    (`PENDING`/`READY`/`CANCELLED`) is published by `finish_task` before its `hs.swap`; a joining handle reads
    it to build `Result<O, JoinError{Cancelled; Panicked(PanicInfo)}>`. `ctl.cancel` is the handle→executor
    abort request; `drive_task` checks it BEFORE polling (so `abort` stops even a forever-self-waking task
    without polling it). A `Pending`-no-reschedule task → `finish_task(CANCELLED)` (unreschedulable — same
    reclaim rule as stage (a), now routed through the handshake).
  - **Surface (locked decisions realized):** `Executor::spawn<F,O>(fut)->JoinHandle<O>`,
    `spawn_detached<F,O>` (= spawn+detach sugar; kept so the stage-(a) probe still regresses),
    `block_on<F,O>(root)->O` (spawns root, **pumps the queue on the calling thread** via `pump_until`, unwraps —
    panics if the root was cancelled), `run()` (drive-to-empty). `JoinHandle<O>`: `join(mut this)->Result<O,
    JoinError>` (single-thread: `pump_until` then move the result out + free the block — destructures `this` to
    suppress its `Drop`, like `thread::JoinHandle::join`), `detach(mut this)`, `abort(&this)` (non-consuming),
    `is_finished(&this)`, **drop=detach** (`detach_shared<O>` in `Drop`). `Executor::drop` reclaims still-queued
    tasks as CANCELLED (runs the handshake → no leak).
  - **Validation (`scratchpad/async_exec_b`):** four tests → EXIT 0 — (A) `block_on(Compute(3,42))==42` (+ a
    second `block_on` to prove the first didn't corrupt the heap); (B) spawn two `Compute` + `join` each →
    10/20; (C) `abort` a forever-self-waking task → `join` returns `Err(Cancelled)` (no infinite poll — cancel
    checked pre-poll); (D) detach a task with an **owned Output** (`Tracked` with a `Drop`) → the result is
    dropped **exactly once** by the dispose path (`drops==1`). Stage-(a) probe (`async_exec_a`,
    `spawn_detached`) still EXIT 0 (regression).
  - **Durable gotchas:** (1) **A zero-sized future can't be boxed** — `Layout::of<F>()` for a fieldless struct
    has size 0 and `GlobalAlloc.allocate(0)` returns `Err` → "future allocation failed" panic (on Windows
    `abort()` exits **code 3**, which looked like a probe-branch return — watch for that). REAL lowered async
    futures always carry a `state: u32` field so they're never zero-sized; a hand-written fieldless future is
    the only trigger. **TODO (robustness, deferred):** handle ZST futures in `spawn_on` (Rust uses
    `NonNull::dangling()` for ZSTs; skip the box + the dealloc when size==0). (2) A generic **method**
    delegating to a generic **free fn** that returns a generic struct (`Executor::spawn<F,O>` → `spawn_on<F,O>
    -> JoinHandle<O>`) works. (3) Storing the Ready value `*rptr = v` where `v` is a by-value `match` binding
    moves it (no double-drop) — same as `mpsc::send`'s `*slot = value`.
  NEXT: **stage (c)** — multi-thread. N `pthread` workers (mirror `thread::spawn`) pulling from a **mutex+condvar
  guarded** ready-queue; a **per-task atomic RUNNING/NOTIFIED state** so a mid-poll self-wake (or a cross-thread
  wake) can't let two workers poll one task; `join`/`block_on` **block on a condvar** (workers drive) instead of
  pumping; **`catch_unwind` at the poll boundary** (a `run_poll(ctx)` trampoline mirroring `run_catch_body`) so a
  panicking task yields `Err(Panicked)` and siblings survive. Validate task isolation under `--panic=unwind` in
  WSL (`bin/cryo` is the Linux ELF). Keep the `--panic=abort` path working (task panic aborts, per §7-6).

- _2026-07-24_ — **Phase 3 stage (c) DONE + validated: multi-thread worker pool + `catch_unwind` task
  isolation.** Environment: fresh **Linux codespace** (not Jake's Windows box — the HANDOFF warned to verify;
  `--panic=unwind` is native here, no WSL needed). **State discrepancy found at start:** commit `b159494e`
  ("Implement multi-threaded executor … Phase 3 Stage c") + repin `b3ab958d` were already on `ll-impl`, but
  `b159494e` actually committed the stage-(a)/(b) **single-thread** executor under a stage-(c) label (its own doc
  said "single-thread core … no worker pool yet"; no pthread/atomic-state/`catch_unwind` in the code). Stage (c)
  was genuinely still to do; this entry does it.
  - **Jake's two decisions (asked up front):** (1) **compiler atom + full stage (c) with isolation** (over a
    pure-stdlib-executor-first split); (2) **`Executor::new()` always spins workers** (over single-thread `new()`
    + opt-in `with_threads`).
  - **Compiler prerequisite (Phase 1, both-OS REPIN):** `catch_unwind`/`intrinsics::try_catch` is a hard compile
    error under `--panic=abort` (`call_emitter.cryo` `emit_catch_unwind`, gated on `ctx.project_panic_unwind`),
    and ConfigGating had no panic-strategy atom. Added **`panic_unwind` as a feature atom** to `CfgEnv`
    (`config_gating.cryo`: field + `from_ctx` from `ctx.project_panic_unwind` + `is_feature_atom` +
    `feature_active`) and to the `directive_processing.cryo` flavor validator. The `not(...)` combinator + bare
    `config(atom)` already dispatch feature atoms generically, so `![config(panic_unwind)]` /
    `![config(not(panic_unwind))]` both work with ZERO evaluator changes. selfhost-check GREEN (win s3==s4
    byte-identical, 235 modules) → `make pin` both OS, `verify-pin` OK. This is the **2-phase repin ritual**: the
    pinned compiler must understand the atom BEFORE the stdlib that uses it can build (old pin would keep both
    gated `poll_boundary` variants → duplicate-def).
  - **Executor rewrite (Phase 2, pure stdlib, NO further repin):** `stdlib/future/executor.cryo`. `ExecInner`
    now embeds two pthread mutex+condvar pairs (`u8[40]`/`u8[48]`, mpsc idiom): a **queue** lock/cond (workers
    park in `take()` until work or shutdown) and a **done** lock/cond (joiners park until a task finishes).
    `Task` gains an `Atomic<u8> run_state` (**IDLE/SCHEDULED/RUNNING/NOTIFIED**) replacing the stage-a/b `queued`
    bool: `task_wake` CAS `IDLE→SCHEDULED`(+enqueue) or `RUNNING→NOTIFIED`(defer); a worker CAS `SCHEDULED→RUNNING`
    (done under the queue lock at dequeue), then on Pending CAS `RUNNING→IDLE` (quiet → reclaim CANCELLED,
    unreschedulable in Phase 3) or `NOTIFIED→SCHEDULED`(+reenqueue). `join`/`block_on` **block on the done-cond
    until `hs == HS_TASK_FIN`** (NOT merely `outcome != PENDING` — the finishing worker's hs.swap is its LAST
    touch of the block, so waiting on hs closes the read-vs-free race), then read+free exactly as stage (b).
    `finish_task` broadcasts the done-cond only on the HS_LIVE path (a live joiner), after the hs.swap.
    `TaskCtl` gains a `panic: PanicInfo` field for the PANICKED path.
  - **Poll-boundary isolation:** two file-scope `poll_boundary(poll_fn, fut, shared, cx, out_panic)` free fns
    (ConfigGating strips decls, not method bodies): `![config(panic_unwind)]` runs the erased poll inside
    `intrinsics::try_catch` via a `run_poll(void*)`/`PollCtx` trampoline (mirrors `run_catch_body`), and on a
    caught panic reads `panic_unwind::taken_panic_info()` (NEW gated helper in `panic_unwind.cryo` that reads the
    `__cryo_panic_taken_*` externs — gated so they stay unreferenced under abort) → status 2 → `finish PANICKED`
    with the info stashed in `TaskCtl.panic`; the `![config(not(panic_unwind))]` twin is a plain direct poll
    (task panic aborts the process — accepted, §7-6). The panic runtime tier `runtime/.bin/libcryort-panic-unwind.a`
    must exist (`cd runtime && cryo build` — it was missing on this box; the `[[lib]] cryort-panic-unwind` member
    builds it).
  - **Self-contained (no `thread` dep):** `import thread` brought `thread::JoinHandle` into scope and its leaf
    clashed with `executor::JoinHandle` — misresolving the destructure-annotation form AND (in a consumer's
    dependency-build) `spawn_on`'s return type. Fix: **dropped `import thread` entirely**; the executor owns its
    pool via gated `worker_tramp`/`exec_spawn_worker`/`exec_join_worker`/`exec_cpu_count` free fns (same
    pthread/Win32 primitives `thread` uses) and stores worker tids in a `u64[]`; `Executor::drop` sets shutdown +
    broadcasts + joins each worker + `drain_cancelled` + frees. `Executor::new()` = `with_threads(cpu_count())`.
  - **Landmines hit + durable fixes:** (1) **`import thread` JoinHandle leaf-clash** → self-contain (above);
    qualifying the annotation (`future::executor::JoinHandle`) does NOT help — the resolver binds the bare leaf to
    the import. (2) `const {..}: T = this;` destructure REQUIRES the `: T` annotation (un-annotated is a parse
    error), so where `T` clashes, read fields + null `this.shared` to neutralize the Drop instead. (3) **A static
    method on a GENERIC owner reached only through another generic's instantiation is not monomorphized (E0636 at
    codegen)** — `TaskShared<O>::detach` compiled in the lib build but failed in the probe; reverted to a free
    `detach_shared<O>` (the stage-b proven form; `Atomic<u8>::new` works only because its owner arg is concrete).
    (4) Verified empirically (scratchpad probe): **`ptr.drop()` auto-derefs a raw `F*` and runs drop glue even
    when `F` has no `Drop`** — so `task_drop_thunk<F>` needs NO `where F: Drop` (a bound would wrongly reject
    non-Drop futures), matching `thread_body`; executor now uses auto-deref style throughout per Jake.
    (5) Empirically (probe): **`&this` inside a method IS the object's box address** (not a hidden ref-param
    addr), and **a whole-struct move with a non-Copy `Atomic` field (`*p = T::new(...)`) works**. So `finish`
    means every `Task` method is instance/constructor: `Task::new(...)` (constructor, `*task = Task::new(...)`,
    passing the monomorphized thunks as fn-ptr args); `drive`/`finish`/`free` are `mut &this` — `drive` hands
    `&this as u8*` to the waker + re-enqueues `&this`, `free` self-deallocs `&this` (sound: `Task` has no `Drop`,
    so freeing the borrowed receiver synthesizes no double-drop). Behaviorally proven by the self-wake path
    (30/30). No static+raw-pointer `Task` methods remain.
  - **Validation (Linux, pinned `bin/cryo` + `CRYO_STDLIB`):** `make stdlib` green (148 modules) under default
    abort; regression probe (block_on + spawn/join + self-wake + abort→Cancelled) exit 103 **30/30**; isolation
    probe under `--panic=unwind` (3 compute siblings + 1 panicker) exit 61 **30/30** — the panicker's `join`
    returns `Err(Panicked)`, siblings complete, process does not abort. Probes in scratchpad (`exec_probes/`).
  - **Repin status:** Phase-1 compiler atom REPINNED both OS (`bin/cryo` f67be7b9…, `bin/cryo.exe` d79abcb9…),
    `verify-pin` OK. Phase-2 stdlib needs NO repin — the compiler links neither `future::executor` (no future IR
    in its build) nor `core::panic_unwind` (no import; the added helper is abort-stripped). UNCOMMITTED (only Jake
    commits). **The committed `b159494e` mislabels stages (a)+(b) as stage (c) — Jake may want to amend the
    message/history.**
  NEXT: **Phase 4** — reactor (epoll/IOCP) + async I/O over `std::net` + timers + `async fn main` + combinators.
  At that point wakers outlive a poll (stored in reactor registrations) → the `Waker` `clone_fn`/`drop_fn` gain
  real refcount bodies + a `Drop` impl, and the per-task run-state gains a waker refcount (see waker.cryo doc).

- _2026-07-24_ — **Phase 4 STARTED — three design forks LOCKED by Jake (one-way doors closed). Building Inc
  4a.** Baseline HEAD `8e5a7694` (Phase 3 stage (c) committed+repinned by Jake), pin verifies OK, tree clean,
  `make stdlib` = 148 modules green. New session on a **Linux codespace** (verified — not Jake's Windows box).
  Reconciled a doc/git discrepancy: the stage-(c) §9 entry says "UNCOMMITTED", but the multi-thread executor +
  `panic_unwind` atom ARE committed in `8e5a7694` (789-line `executor.cryo` rewrite +
  `config_gating.cryo`/`directive_processing.cryo`/`panic_unwind.cryo` + repin) — the note predated the commit.
  - **Recon-confirmed Phase-4 substrate (grep the symbols):** epoll (`sys_epoll_create1`/`_ctl`/`_wait`),
    `eventfd2`, `timerfd_create`/`_settime`, `clock_gettime`+`CLOCK_MONOTONIC` all bound (`sys/syscall.cryo`);
    `Arc<T, A=GlobalAlloc>` is a full thread-safe refcount (`alloc/arc.cryo`: `ArcInner{strong,weak:
    Atomic<u64>}` + `new`/`clone`/`get`/`get_mut` + `Weak`/`upgrade`); `TcpStream` (`net/socket/tcp.cryo`) is
    blocking (`sock_read`/`sock_write`), has `from_fd`/`raw_fd`/`Drop`(closes fd)/`set_read_timeout` but **NO
    `set_nonblocking`** (add `fcntl`+`O_NONBLOCK`; Windows `ioctlsocket`+`FIONBIO` already bound). **IOCP
    bindings ABSENT** — `OVERLAPPED`+`FILE_FLAG_OVERLAPPED`+`ioctlsocket` exist but
    `CreateIoCompletionPort`/`GetQueuedCompletionStatus`/`WSARecv`/`WSASend` do NOT → "both OSes" means writing
    them. `![thread_local]` is compiler-supported but **unused in `stdlib/`** (only `runtime/` panic state uses
    it) → the per-Executor current-reactor handle is the first stdlib use (validate at 4b).
  - **The Phase-4 one-way-door seam (in the committed executor):** `Task::drive` builds
    `Waker::new_nonowning(task_wake, &this as u8*)` (data = raw `Task*`, clone/drop = noop); on a quiet `Pending`
    (`RUNNING→IDLE`) it **reclaims the task as CANCELLED and frees the box** — because Phase 3 has no reactor to
    ever re-wake it. Phase 4 changes exactly this: a task that parked on the reactor must survive that `Pending`
    because a *stored* waker wakes it later. `run_state` (IDLE/SCHEDULED/RUNNING/NOTIFIED) stays as the
    scheduling/concurrent-poll guard; the new refcount governs LIFETIME (orthogonal).
  - **Jake's three locked decisions (via the question tool):**
    1. **Waker lifetime → separate `Arc`-style wrapper** (NOT a refcount field embedded in the `Task` box). Wrap
       the (non-generic, erased) `Task` in an `Arc<Task>`; the waker's `data` carries the raw `ArcInner<Task>*`
       and `clone_fn`/`drop_fn` become real free fns that bump/drop the strong count (last drop → drop `Task` +
       free). Reuses the tested `alloc/arc.cryo` machinery instead of hand-rolling a refcount. **4a design detail
       to resolve:** whether `arc.cryo` exposes `into_raw`/`from_raw` (reconstruct an `Arc` from the raw inner so
       its `Drop` does the decrement) or the free fns manipulate `ArcInner.strong` directly.
    2. **Reactor → dedicated thread, per-`Executor`.** One background thread per `Executor` runs the
       `epoll_wait`/IOCP loop; async I/O + timer futures reach it via a `![thread_local]` **current-reactor
       handle** the worker sets around each poll (so `Context` — a Phase-0 lock — stays unchanged; no ambient
       global runtime, honoring §7-5). Composes with `Executor::drop` (shutdown → join the reactor thread too,
       alongside the worker join it already does).
    3. **Platform → both OSes (epoll + IOCP) from day one** (NOT Linux-first-Windows-stub). ⇒ the reactor
       interface must accommodate BOTH the readiness model (epoll: wake when the fd is *ready*, then the future
       does the syscall) and the completion model (IOCP: the OS does the overlapped op and wakes on *completion*).
       **This is itself the next one-way door (bring to Jake at the 4b boundary):** unify on a readiness
       abstraction with Windows AFD emulation (mio's approach — complex) vs. a thin per-OS reactor interface with
       OS-gated I/O futures (`![config(...)]` free fns) sharing only "register interest → get woken". Recon +
       recommendation owed before coding 4b.
  - **Refined increment breakdown (each: build → validate under `block_on`/inline driver → gate → repin only if
    default-path `.ll` moves):** **4a** Arc waker refcount evolution (OS-independent; pure stdlib, no repin) —
    real `clone_fn`/`drop_fn` + `Arc<Task>` ownership + `Drop`; `RUNNING→IDLE` stops freeing. **4b** the reactor
    (epoll + IOCP behind the chosen interface; `![thread_local]` current-reactor handle; eventfd/IOCP-post kick).
    **4c** async I/O over `std::net` (`set_nonblocking` + read/write/accept/connect futures, owned-handle style
    §7-6). **4d** timers (`sleep`/`timeout` via `timerfd`/IOCP timer). **4e** `async fn main` (compiler lower →
    **selfhost-check + repin**). **4f** combinators (`join!`/`select!`/`try_join!`).
  - **Flagged dependency (bites at 4c, not before):** the idiomatic read-loop `const (s,n) = s.read(buf).await;
    use(s)` keeps an **aggregate (`TcpStream`) live across a suspend** → currently `E0600` (a deferred Phase-2
    tail: aggregate-across-await promotion). 4a/4b + hand-written I/O futures validate fine under `block_on`
    without loops; *looping* async I/O inside an `async fn` needs that promotion first. Separable — schedule when
    real I/O ergonomics are reached; not a 4a/4b blocker.
  NEXT: implement **Inc 4a** — study `arc.cryo` (`into_raw`/`from_raw`?), wrap `Task` in `Arc<Task>`, give the
  `Waker` real `clone_fn`/`drop_fn` (bump/drop the strong count) + a `Drop` impl, make `RUNNING→IDLE` release
  the queue's Arc ref instead of finishing-CANCELLED. Validate: a hand-written future that clones its waker,
  returns `Pending`, is woken later from another thread → re-polled to completion; strong count reaches 0
  exactly once (no leak, no double-free), stress ≥25×. Pure stdlib → `make stdlib` + `verify-pin`, no repin.

- _2026-07-24_ — **Inc 4a DONE + validated (both OSes). COMPILER fix (owner-default backfill) → REPINNED both
  OS. UNCOMMITTED.** Baseline HEAD `8e5a7694`, pin now updated (new SHAs `188bc76e…` / `44fd1963…`),
  `verify-pin` OK. The Arc-refcounted waker (the Phase-4 one-way door) is in and stress-proven. Files: NEW raw API
  in `alloc/arc.cryo`; `future/waker.cryo` (Copy→non-Copy); `future/executor.cryo` (`Arc<Task>` lifetime);
  `sema/call_resolver.cryo` (the compiler fix).
  - **The compiler fix (root cause, not a workaround — Jake's call).** The recurring "static on a generic owner
    reached through another generic's instantiation → E0636" (the same class that forced `detach_shared<O>` in
    Phase 3) was, for this case, an **owner-arg inference gap**, NOT a mono-registration gap. `Arc::try_new`
    (`static try_new(v: T) -> Result<Arc<T, GlobalAlloc>, AllocError>`) failed because owner param **`A` is
    hardcoded `GlobalAlloc` in the signature** (appears in NO parameter and NO free return position) AND the
    owner is **nested** in the expected type (`Result<Arc<Thing>, …>`), so `compute_static_owner_bindings`
    step 2's base-match misses it → `A` never binds → empty stash → mono never emits the spec → codegen E0636.
    `Arc::new` escaped ONLY because its expected type IS the owner (`Arc<Thing>`, step 2 base-matches). **Bisected
    with a minimal repro** (`use_new` OK vs `use_try_new` E0636, same concrete owner) — the trigger is the
    return SHAPE (owner-is-return vs owner-nested-in-return), NOT the generic-caller context (both non-generic and
    generic callers failed identically). **Fix (`call_resolver.cryo`, +74):** a new `infer_static_owner_prefix`
    (binds the leading owner params the args + expected-return-unify determine, returns the contiguous concrete
    PREFIX) + a step **2b** in `compute_static_owner_bindings` that backfills the trailing DEFAULTED params from
    their declared defaults via the existing `expand_default_type_args` (the same primitive the turbofish path
    already used). Runs AFTER step 2 so a non-default owner arg from a base-matching expected type still wins;
    fires only when every prior source left a defaulted param unbound → **inert for all existing code**. Fixes the
    whole `Owner::try_new`-style family, not just async. **Gate:** `make selfhost-check` → exit 0, BOTH fixed
    points (Linux s3==s4; Windows s3==s4 byte-identical, 235 modules); **win-s2 vs win-s3 = 0/235 differing
    `.ll`** → the compiler produces identical IR for all existing code. Compiler source changed ⇒ **REPINNED both
    OS** (`make pin`, `verify-pin` OK). This same default-backfill ALSO fixed the "no expected type" bisection case
    (`Arc::try_new(x)` inferring owner `T` from a concrete arg then backfilling `A`) — confirmed post-commit. The
    turbofish variant was fixed in the follow-up below.
  - **The 4a stdlib rewrite (Arc<Task> lifetime).** `arc.cryo`: `into_raw`/`from_raw`/`increment_strong_count`/
    `decrement_strong_count` (standard raw-Arc API; the manual-vtable waker needs to bump/drop the count from a
    `void*`). `waker.cryo`: `Waker` gains a `Drop` (runs `drop_fn(data)`) → **Copy→non-Copy**; `Context::waker()`
    now returns `.clone()` (a plain field copy would alias one `clone_fn` under two `drop_fn`s). `executor.cryo`:
    `Task` gains `self_arc: ArcInner<Task, GlobalAlloc>*` (back-pointer, so `task_incref`/`task_decref` reach the
    count from any `Task*` — no container-of). `spawn_on` boxes the `Task` in `Arc::new` (count 1 = the scheduling
    ref, handed to the queue by `into_raw`). The owning poll `Waker` = `{task_wake, waker_arc_clone,
    waker_arc_drop, &this}`; `drive` bumps a ref for `cx` (balanced when `cx` drops at method end). `task_wake`
    bumps on `IDLE→SCHEDULED` (the queue's scheduling ref, distinct from the waker's own). **The finish-vs-park
    decision is now IMPLICIT:** on a quiet `Pending` the worker just `task_decref`s the scheduling ref — if a
    stored waker holds a ref the task PARKS (survives), else the last decref runs **`Task::drop`**, which finishes
    an unfinished task CANCELLED (drops its future, handshake) before the `Arc` frees the box. No `strong_count`
    inspection, no race. `finish` nulls `fut` so `Task::drop` knows finishing already ran. `Task::free()` deleted
    (the `Arc` frees). `wake`/`wake_by_ref` stay by-ref (no signature change) — the waker always keeps its own
    ref, released by its `Drop`.
  - **Refcount protocol (durable):** scheduling ref (spawn `Arc::new` → queue → worker, one ref that persists
    until finish/park) + one ref per stored waker (`clone_fn`+1 / `drop_fn`−1) + a transient `cx`-waker ref per
    poll (bumped in `drive`, released at `cx` scope-exit). Self-wake = `RUNNING→NOTIFIED`, no ref change, `drive`
    reschedules (transfers the scheduling ref back). Park+foreign-wake = the waker's stored ref keeps the box
    alive; the wake `IDLE→SCHEDULED` bumps a fresh scheduling ref. This is the textbook `async-task` model.
  - **Validation:** minimal E0636 repro → EXIT 0; full 4a probe (`scratchpad/exec4a`) — block_on / spawn+join /
    self-wake (`WakeCounter`) / **stored-waker park → foreign-thread wake → complete** (`Parker` + main-as-reactor)
    / abort→`Cancelled` / `Parker` dropped exactly once — **625 rounds across 25 process runs + 250 more via the
    repinned `bin/cryo`, zero failures** (no race/leak/double-free). Also proven: a `Pending`-without-a-waker
    future now correctly parks-then-CANCELS on the Executor (it can never be woken) — so Executor probes must
    self-wake or store the waker (`block_on`'s free driver still re-polls unconditionally, so `PendingThenReady`
    works there). `make stdlib` = 148 green.
  - **Durable compiler-fix landmine:** `compute_static_owner_bindings` sources were all-or-nothing; a defaulted
    owner param bound by NO inference source (not in params, not free in the return, owner nested in the expected
    type) needs the default-backfill (step 2b). Any future `Owner<..., A = Default>::ctor -> Wrapper<Owner<...>>`
    depends on it.
  NEXT: **Inc 4b — the reactor.** The three forks are locked, but "both OSes from day one" opens the **next
  one-way door: the reactor interface must span readiness (epoll: wake when the fd is ready, then syscall) AND
  completion (IOCP: OS does the overlapped op, wakes on completion).** Bring Jake the unification fork —
  (a) readiness abstraction + Windows AFD emulation (mio's way; complex) vs. (b) thin per-OS reactor interface
  with `![config(...)]`-gated I/O futures sharing only "register interest → get woken" — with a recon-grounded
  recommendation BEFORE coding. Also: write the IOCP bindings (`CreateIoCompletionPort`/
  `GetQueuedCompletionStatus`/`WSARecv`/`WSASend`, absent today) and the per-Executor `![thread_local]`
  current-reactor handle (first stdlib use of the directive). `set_nonblocking` (fcntl/ioctlsocket) for 4c.

- _2026-07-24_ — **Compiler follow-up: the two remaining owner-generic-static gaps CLOSED (Jake asked for the
  root fixes before dispatching the Inc-4b agent). REPINNED both OS. UNCOMMITTED.** Baseline HEAD `3e62234e`
  (Inc 4a committed by Jake). Re-characterized the "two open gaps" the 4a entry flagged, with the committed
  compiler:
  - **Gap A (owner `T` from a concrete arg, NO expected type) — already fixed by 4a.** `match (Arc::try_new(Thing
    {…}))` with no annotation now compiles+runs (the default-backfill's prefix source binds `T` from the arg, then
    backfills `A`). No new work; confirmed by probe.
  - **Gap B (turbofish `Owner<Args>::static` with a NESTED-owner return) — FIXED here.** `Arc<Thing,
    GlobalAlloc>::try_new(…)` typed as **`Result<Thing, GlobalAlloc>`** (E0200) instead of `Result<Arc<Thing,
    GlobalAlloc>, AllocError>`. Root cause in `sema/call_resolver.cryo::try_resolve_generic_return`: the turbofish
    branch did `generic_registry.instantiate(ret_inst.generic_base, turbofish_args)` — i.e. it substituted the
    OWNER's turbofish args POSITIONALLY into the return's OUTERMOST generic. Valid ONLY when the return IS the
    owner (`new() -> Arc<T, A>`); for a nested owner (`try_new() -> Result<Arc<T,GlobalAlloc>, E>`) it landed the
    owner args on `Result`. It fired only because the arg counts coincidentally matched (2 owner args vs 2 outer
    return args). **Fix:** map the turbofish args to the OWNER template's generic params
    (`resolve_scope_owner_template` + `expand_default_type_args` for a partial turbofish) and
    `TypeSubstitution::apply` them throughout the return — correct for BOTH return-is-owner and nested-owner, and
    identical to the old behavior for the return-is-owner case. Guarded by
    `generic_static_owner_binding.cryo::turbofish_binds_nested_owner_return` (+ the 4a
    `nested_default_owner_param_binds`). **Gate:** `make selfhost-check` → exit 0, BOTH fixed points (Windows
    s3==s4 byte-identical, 235 modules); **win-s2 vs win-s3 = 0/235 differing `.ll`** → inert for existing code;
    `make test` → OVERALL PASS. Compiler source changed ⇒ **REPINNED both OS** (`bin/cryo` `3c28aa32…`,
    `bin/cryo.exe` `fe981b8e…`), `verify-pin` OK. **Owner-generic static calls now resolve across all four shapes
    (inference-from-args, no-expected-type, turbofish, nested-owner-return) — the Inc-4b agent needn't worry about
    them.** UNCOMMITTED (only Jake commits). A fresh `HANDOFF.md` for Inc 4b is written.

- _2026-07-24_ — **Inc 4b: the reactor + async TCP. Linux DONE + validated; Windows AFD backend WRITTEN but
  UNVALIDATABLE HERE. Two COMPILER fixes → REPINNED both OS. UNCOMMITTED.** Baseline HEAD `beecc8da`; pin now
  `e0e5aaed…` / `591c8f12…`, `verify-pin` OK, `make test` OVERALL PASS, both selfhost fixed points green
  (Linux s3==s4; Windows s3==s4 byte-identical, 235 modules), **win-s2 vs win-s3 = 0/235 differing `.ll`** (the
  compiler produces identical IR for all existing code — the repin exists to carry the new link flag and the
  async fix, not an IR change). Files: NEW `stdlib/future/reactor.cryo`; `future/executor.cryo` (+reactor
  lifecycle); `future/_module.cryo`; `net/socket/tcp.cryo` (async futures + `set_nonblocking`); `net/sys.cryo`
  (+4 per-OS primitives); `sys/syscall.cryo` (IOCP/AFD bindings); `sema/async_lower.cryo` + `codegen/passes.cryo`
  (the compiler fixes).
  - **Jake's interface decision (the 4b one-way door):** presented readiness+WSAPoll vs readiness+AFD vs a per-OS
    readiness/completion split, with WSAPoll recommended. **Jake chose readiness + real Windows AFD** (mio's
    backend). Also asked for 4b scope = reactor core **plus** async TCP I/O in one increment. Recorded because the
    recommendation was NOT the choice: the AFD route is the right long-term Windows answer and the reason the
    interface is uniform, but it is what left Windows unvalidated (below).
  - **Reactor core (`future/reactor.cryo`, both backends behind one interface).** `Reactor::start()` opens the OS
    backend and runs a dedicated thread; `register(fd, direction, waker)` / `cancel(fd, direction)` /
    `deregister(fd)` / `poll_once(timeout)`. Registrations are heap blocks in a 64-bucket table keyed `fd & 63`,
    existing exactly while a direction is armed. Backends are `![target(...)]` free fns (`rt_backend_open/close`,
    `rt_arm`, `rt_disarm`, `rt_kick`, `rt_wait`) reporting `(fd, ready-bits)` pairs, so the shared core is
    OS-agnostic. **Linux:** epoll + eventfd kick, `EPOLLONESHOT` re-armed explicitly. **Windows:** IOCP + an
    `\Device\Afd` helper handle, one `IOCTL_AFD_POLL` per arm, `PostQueuedCompletionStatus` as the kick.
  - **Three invariants that are correctness, not taste** (all in the module doc): (1) **level-triggered, never
    edge** — a future attempts its syscall *before* registering, so an arm must report already-ready descriptors
    or a late registration parks forever; (2) **dispatch by descriptor number, never by a pointer to the
    registration** — a stale event for a closed/recycled fd then costs one spurious poll instead of a
    use-after-free; (3) **no waker is woken or dropped while the table lock is held** — dropping a waker releases
    a task ref, which can run the task's destructor → the future's destructor → `deregister`, re-entering the
    non-recursive lock. Every op moves displaced wakers into caller locals released after the unlock. (1) and (3)
    were found by reasoning, not by a failing test; (3) was a real self-deadlock in the first draft.
  - **Executor wiring.** `ExecInner` gains `reactor: Reactor*`; `worker_body` publishes it once per thread via the
    `![thread_local]` handle (**first stdlib use of `![thread_local]` — it links and works cross-module**);
    `Executor::drop` orders teardown join-workers → **stop reactor** → drain queue, because stopping the reactor
    releases the wakers that are the last reference to tasks parked on I/O (reclaiming them), and draining first
    would strand anything woken in the interim.
  - **Async TCP in `net/socket/tcp.cryo`** (Jake: no separate `tcp_async` module). `TcpConnect` / `TcpAccept` /
    `TcpRead` / `TcpWrite`, owned-handle style per §7-6: each future **owns** the socket and hands it back in
    `TcpIo` / `TcpAccepted`, which expose it through `take_stream()` / `take_listener()` / `take_result()`.
    Dropping a parked future cancels only its own direction and closes the socket it owns. Plus
    `set_nonblocking` on both types and four per-OS primitives in `net::sys` (`os_set_nonblocking`,
    `os_connect_pending`, `os_connect_done`, `os_sock_error`).
  - **Validation (Linux).** Pipe park→wake, 8 concurrent parks, drop-while-parked, 20 re-park rounds, and a real
    async echo (connect/accept/write/read over loopback, 11 round trips per run): **25–30 process runs each, zero
    failures, and valgrind clean — 151 allocs / 151 frees, 0 errors**, including the drop-while-parked path where
    a stranded registration or task ref would show.
  - **COMPILER FIX 1 (the big one) — an `async function` could only await futures whose `Output` equalled its own
    return type.** `async_lower.cryo` typed the sub-future's poll call and its `__poll_k` temp with `sm.poll_out`
    (**this** function's `Poll<Output>`) instead of `Poll<awaited Output>`, so any other await died with E0200
    ("expected `Poll<i64>`, found `Poll<Pair>`"). Every Phase-2 probe awaited `Ready<i64>` inside an `-> i64`
    function, which is exactly the case where the two coincide — that is why four increments of validation missed
    it. Fix: carry the `Poll<>` base in `PollSm` and instantiate `Poll<ao>` from the awaited expression's own type.
    Now awaiting struct- and generic-enum-output futures compiles AND runs correctly (probe `awaitagg`).
  - **COMPILER FIX 2 — Windows link line.** The reactor's backend needs `ntdll` (AFD lives under the NT layer) and
    mingw auto-links it no more than it does Winsock, so `-lntdll` joins `-lws2_32` in both Windows
    `LinkerConfig`s (`codegen/passes.cryo`), the same precedent and for the same reason.
  - **⚠ WINDOWS IS UNVALIDATED — and this is a wine limitation, not a code defect.** The Windows stdlib compiles,
    links (with `-lntdll`) and runs under wine 9.0; `CreateIoCompletionPort` works, `\Device\Afd\Cryo` **opens
    successfully** (all three path spellings), sockets set up fine. But **`NtDeviceIoControlFile(IOCTL_AFD_POLL)`
    never returns** — tried both via a `\Device\Afd` helper handle (wepoll/mio's design) and directly on an
    IOCP-associated socket handle. Both hang, so an executor doing async I/O deadlocks under wine (the arm is
    issued from `register()` on a worker). mio is likewise documented as not working under wine. **Verifying the
    AFD backend requires a real Windows machine.** Two bugs WERE found and fixed along the way, so the path is
    partly exercised: `INVALID_HANDLE_VALUE` must be the full-width all-ones pointer (`(0 - 1) as u8*` widens
    wrong and `CreateIoCompletionPort` then fails), and the NT structures must be **8-byte aligned** — they are
    now `u64`-backed buffers, since a struct of `u8` arrays is only byte-aligned off the heap and the NT layer
    rejects a misaligned `IO_STATUS_BLOCK`.
  - **Durable language/semantics facts established by probe** (all cost real debugging; the first four are the
    reason the reactor and the socket futures are shaped the way they are):
    1. **Assignment through a pointer does NOT drop the value it overwrites** — storing over a live `Waker` would
       strand its task reference. Read the old value into a local first; that local's scope-exit drop releases it.
    2. **A `_`-prefixed local still drops** (the underscore silences the lint only), and an explicit `.drop()`
       does **not** double-drop (it marks the local consumed). Both idioms are safe.
    3. **A `drop` method in a type body IS the destructor** — no `implement trait Drop` needed. `TcpStream` and
       `TcpListener` are therefore droppable, which is why moving a field out of `TcpIo` is rejected and the
       `take_*` (`mem::swap` with a closed placeholder) accessors exist.
    4. **A by-value parameter that is not moved on drops at function exit.** `TcpRead::start(stream, …)` first
       read `raw_fd()` and let the parameter die — closing the socket immediately. The futures now genuinely own
       the handle. The probe caught this; nothing else would have.
    5. **A future handed to `Executor::spawn` must implement `Drop`** (`task_drop_thunk` calls `fut.drop()`).
    6. `join()` consumes its `JoinHandle`, so handles kept in an array must be taken out (`remove`), not indexed.
  NEXT — **Jake has directed that sockets become async-only and every consumer be ported** (`net::http`,
  `net::http2`, `net::tls`, `net::ws`, `net::https`, + the 4 net tests). Sequencing, in dependency order:
  **(a)** the **E0600 aggregate-across-await** compiler increment — an `async function` still cannot hold an
  aggregate across a suspend, and every ported protocol function must hold a `TcpStream` across awaits, so this
  blocks the whole port. Design is clear: promote the aggregate to a struct field the `mut` way (single source of
  truth, no copies), initialize it as `Option<T>` = `None` in the constructor (the proven `fut_k: Option<F_k>`
  precedent) and take/put it around each block, which sidesteps both the missing zero value and any double-drop.
  **(b)** port the protocol layers — they are mostly generic over `S: Read + Write` (`http2/connection.cryo`,
  855 lines, has zero concrete socket uses), so the concrete touchpoints are few, BUT `net::tls` hands its fd to
  OpenSSL's blocking BIO and needs a non-blocking BIO + `WANT_READ`/`WANT_WRITE` handling — its own design problem.
  **(c)** only then delete the blocking surface from `tcp.cryo` and rename the async ops onto `read`/`write`/
  `accept`/`connect`. **(d)** validate the AFD backend on real Windows. Keeping the blocking API until (c) is what
  keeps the tree green; deleting it first would leave the whole net stack red across (a) and (b).

- _2026-07-24_ — **The Phase-2 aggregate-across-await tail is DONE. REPINNED both OS. UNCOMMITTED.**
  Baseline HEAD `ee2d284e` (Inc 4b committed by Jake); pin now `f8f58193…` / `9b856d78…`, `verify-pin` OK,
  `make test` OVERALL PASS, both selfhost fixed points green, **win-s2 vs win-s3 = 0/235 differing `.ll`**
  (inert for all existing code). Files: `sema/async_lower.cryo` (+~330), `future/executor.cryo`
  (`task_drop_thunk`). **This is what unblocks the async-only socket port Jake ordered.**
  - **What an `async function` can do now that it could not:** hold an aggregate live across a suspend
    (`const io = await …;` then await again), take a **droppable aggregate parameter**
    (`async function serve(l: TcpListener)`), and **be spawned on an `Executor`** at all.
  - **The carrier.** A value that must survive a suspend has no zero value the constructor could give its
    field, and the const-scalar strategy's copy-per-reader would drop an owning value once per copy. So it
    lives in an **`Option<T>` field, `None` until first stored** — which is exactly what makes a future
    dropped before the value exists have nothing to drop — and moves to a plain block-local via the same
    take/put protocol the sub-future slots already use. Inside a state the value is an ordinary local, so
    every use of it keeps its normal meaning; the hand-back is also inserted before `return Poll::Pending`,
    which leaves a state before its tail runs.
  - **Parameters.** The shadow prelude (`const p = this.p;`) is fine for a `Copy` parameter and wrong for
    any other: it is a move out of a field, executed inside the dispatch loop, which the move checker
    rightly rejects (E0452 "moved inside a loop"). Non-`Copy` parameters (`OwnershipQuery::is_copy`) are
    therefore skipped there and carried in `Option<T>` slots like any other carried value.
  - **Three rules that make it correct — each one was a real bug first:**
    1. **Liveness, not mentions.** Only a state that *reads* a name before writing it needs the value
       carried. The owned-handle I/O idiom hands the socket to the operation and takes it back out of the
       result, so every state writes first and the socket is never carried at all — carrying it tried to
       store a name the state had already moved away (E0452).
    2. **A state that gives the value away must not hand it back** (`last_use_consumes`): the state that
       passes the socket to an operation stays silent; the state that takes it back publishes the new one.
    3. **Never synthesize an uninitialized droppable binding.** A state that produces its own value
       declares it *at its first assignment*. The first attempt prepended `mut s: T;` at the top of the
       state, and dropping that never-written binding ran `TcpListener::drop` on zeroed memory — calling
       **`close(0)`**, closing stdin, which the kernel then handed out as the next socket. It presented as
       an intermittent hang with sockets reporting fd 0, and **strace found it** (`close(0)`, then
       `accept4(…) = 0`, then `close(0) = EBADF`) after several wrong theories. A first assignment nested
       in a branch is now a clear E0600 diagnostic rather than unsafe code.
  - **`task_drop_thunk` no longer calls `fut.drop()`.** Its comment claimed `.drop()` was drop glue valid
    for any `F`; it is not — it requires `F` to *declare* a `drop` method, which a compiler-generated
    future never does, so `spawn(async_fn())` could not compile (E0358). It now moves the boxed future
    into a local and lets scope-exit glue run, which works for an explicit `Drop` impl, per-field drops, or
    nothing at all. **A `where F: Drop` bound would have been the wrong fix** — it rejects exactly the
    generated futures this needs to accept. Sound because futures are never self-referential (§4), so
    relocating one cannot dangle an internal pointer.
  - **Validated:** aggregate across an await, `mut` aggregate reassigned after a suspend, an aggregate
    carried around a suspending loop, one held across a suspend inside an `if` branch (and not built in the
    untaken branch), **cancellation** (a future dropped mid-flight drops its aggregate exactly once),
    spawning a generated future on an executor, carried droppable parameters, and a **real async-function
    TCP echo client + server** over loopback: 25 runs × 10 round-trips, zero failures, valgrind clean
    (123 allocs / 123 frees, 0 errors).
  - **Durable language facts established by probe** (add to the Inc-4b list): **reassigning a local does
    NOT drop the overwritten value** (same as a field assignment — only the final value is dropped at scope
    exit); **a struct with droppable fields but no explicit `Drop` impl DOES drop its fields at scope
    exit** (which is why the `Option<T>` carrier is reclaimed correctly); **`Option::as_ref()` genuinely
    aliases the payload in place** (`*opt.as_ref().unwrap()` is an assignable place — an alternative
    field-resident design, not needed by the model chosen); and an uninitialized droppable local is guarded
    by the P13 init-flag machinery in hand-written code but must never be *synthesized* by a lowering.
  NEXT: the port itself (`net::http`, `net::http2`, `net::tls`, `net::ws`, `net::https` + the 4 net tests),
  then delete the blocking socket surface. Known limitation to work around while porting: a value rebuilt
  after an `await` must be assigned at the top level of that step, not inside a branch (clear E0600).
  `net::tls` still needs its own design pass (OpenSSL blocking BIO → non-blocking + `WANT_READ`/`WANT_WRITE`).

- _2026-07-24_ — **Two compiler bugs fixed at the root: the `drop_insertion` init-flag disqualifier, and the
  async conditional-rebuild restriction (E0600 removed). REPINNED both OS. UNCOMMITTED.**
  Baseline HEAD `4927f327`. Files: `passes/drop_insertion.cryo`, `sema/async_lower.cryo`, plus two test
  files. `make test` OVERALL PASS, both selfhost fixed points green, valgrind clean.

  **(1) `drop_insertion` ran destructors on uninitialized memory** — ordinary hand-written Cryo, nothing to
  do with async. A droppable local declared without an initializer lost its init-flag guard whenever a
  field of it was **read** (`r.id`) or a method called on it (`r.method()`), so its scope-exit drop was
  emitted UNGUARDED and fired on `return` paths that never assigned it. For a socket type that is `close()`
  on a garbage descriptor — it surfaced as `close(0)`, closing stdin, which the kernel then handed back out
  as the next socket.
  - **Root cause:** `disq_scan_expr`'s `MemberAccess`/`ArrayAccess` arms disqualified a local from
    init-flag tracking on ANY access, reads included. The guard machinery itself was fine.
  - **Fix:** `disq_scan_expr` is now the READ context and disqualifies nothing on its own; a new
    `disq_scan_place(expr, escaping)` is entered ONLY from an assignment's LHS (`TokenType::Equal`) and
    from `&`'s operand. It disqualifies the base local of a field/element chain, and at a bare identifier
    only when the address escapes — so `r = …`, the whole-binding write the flag exists to observe, never
    disqualifies itself. Exactly two shapes disqualify: a plain `=` to a sub-place and an address-of.
  - **LANDMINE — compound assigns must NOT be treated as writes** (the pre-fix plan said to; it is wrong).
    `r.f += v` and `r.f++` read the target first, so they cannot be the initializing write — the same
    reasoning `init_flag_for_assignment_target` already applies on the positive side. Routing them to the
    place-scan would re-introduce the unguarded drop for every local whose field is incremented after a
    normal assignment. Cryo keeps `PlusEqual`/`PlusPlus` as distinct token kinds through to codegen (no
    desugar to `Equal`), so only `Equal` reaches the place-scan.
  - **Inert for existing code:** `win-s2` vs `win-s3` = **0/235** compiler `.ll` and **0/149** stdlib `.ll`
    on BOTH OS — nothing in the repo tripped the path. Repinned anyway (the pinned compiler is what user
    code is built with, and it still carried the bug).
  - **KNOWN RESIDUAL, deliberately left:** piecemeal init (`mut r: Res; if (early) { return; } r.f = 5;`)
    still disqualifies → unguarded drop → destructor on uninit memory on the early path. Not disqualifying
    would leak instead. A real fix needs **per-field init tracking**, not a per-binding flag. The new test
    asserts the leak-prevention property (a piecemeal-initialized value still drops exactly once) rather
    than enshrining the uninit-drop count, so tightening this later will not fight the test.

  **(2) The async "rebuilt from inside a branch" E0600 is gone** — and NOT for the reason the plan predicted.
  - **The prediction was wrong.** Fixing (1) does not make "declare the name empty at the top of the state"
    sound. The blocker was never the drop — it is the **carrier store**: a state that declares the value
    empty and writes it only inside a branch runs `this.__agg_x = Option::Some(x);` at block exit on the
    path that skipped the branch, publishing uninitialized memory into the field for the NEXT state to
    take. No drop guard touches that.
  - **Actual root cause = a misclassification.** `block_first_use` reported "write" for a write nested in an
    `if`/loop/`match` arm, i.e. "this state produces its own value, no carry needed". False: on the path
    that skips the branch the name must still hold what the previous state left, so the value IS live
    across the suspend. Fix = `block_cond_write(blk, name)` (first use is a write AND
    `top_level_assignment_index(blk, name) < 0`); such a state is routed through the existing carry path
    (`prepend_agg_take` + `store_before_suspends`) exactly like a reader. Far lighter than the
    field-resident-projection fallback. Applied to locals AND by-value parameters (same gap in both).
  - **LANDMINE — ordering:** `last_use_consumes(blocks[ds])` must be read BEFORE the hand-back store is
    appended to that block; the store passes the value to `Option::Some` BY VALUE, so asking afterwards
    reports every declaring state as having given the value away.
  - Locals keep a guard (declaring state gave the value away → nothing for the skipped path to keep → stays
    E0600, rather than a runtime `unwrap()` on an empty carrier). Parameters do not need it: the
    constructor always publishes a parameter into its field.

  **(3) A dead hand-back store made every aggregate-carrying `async fn` emit `W0009`.** The store was
  appended AFTER a state block's `return` — genuinely dead code that never ran; and because every
  synthesized node carries the async function's own span, the dead-code lint reported an unreachable
  statement against the **user's** `async function`, pointing at source that looks perfectly reachable.
  Fix = `needs_handback(blk, name)` = `!last_use_consumes && !stmt_diverges(blk)`, used at all three append
  sites. Behaviour-identical (the store never executed) and the emitted IR is strictly smaller. **A warning
  on generated code is a bug report about the generator, not noise to filter.**

  **Validated:** 13 async shapes — `if` taken/skipped, `if/else` both arms, `while` 0/2 iterations, `match`
  arm with/without rebuild, top-level (owned-handle) rebuild unchanged, a suspend BETWEEN the carry-in and
  the rebuild, and a droppable `mut` parameter taken/skipped — all with exact drop counts, valgrind 0
  errors / 0 leaks, zero warnings. Plus an 11-case drop matrix for (1).
  **New permanent tests:** `tests/tests/lang/async_carry_across_await.cryo` (13 tests — **the suite had NO
  async tests at all before this**) and +6 in `tests/tests/lang/conditional_init_drop.cryo`.
  Gotcha: a by-value parameter needs `mut v: T` to be reassignable (plain `v: T` → E0218).

  NEXT: the async-only socket port (§5 TASK 3) — `net::http`, `net::http2`, `net::ws`, `net::https` + the 4
  net tests, then delete the blocking surface. `net::tls` still needs its own design pass first (OpenSSL
  blocking BIO → non-blocking + `WANT_READ`/`WANT_WRITE`). Windows AFD reactor still UNVALIDATED (needs a
  real Windows box; wine 9.0 cannot service `IOCTL_AFD_POLL`).

- _2026-07-25_ — **OPEN BLOCKER found while validating the above: `MatchExpression` is invisible to the
  async lowering's walkers.** Not caused by the fixes above (`rewrite_returns` and the use-analysis walkers
  are untouched by that diff) — a pre-existing gap, but it sits **directly on the critical path for the
  async-only socket port**, because every async socket op returns a `Result` and the idiomatic Cryo unwrap
  is a match EXPRESSION.
  - **Evidence.** `MatchExpression` appears **once** in `sema/async_lower.cryo` (only in the alpha-renamer
    `rn_expr`); `MatchStatement` appears 11 times. The identical function body compiles clean when not
    `async`.
  - **Two distinct failures from the one gap**, both on
    `const v = match (r) { Result::Ok(x) => { x } Result::Err(_) => { return …; } };` inside an
    `async function`:
    1. **E0201 "cannot find value `r`"** — the use-analysis (`stmt_first_use`/`expr_first_use`,
       `name_read_in_*`) never descends into a `MatchExpression`, so a parameter/local read ONLY as a match
       subject is not seen as used by that state and is never carried across the suspend.
    2. **E0200 "expected `Poll<i64>`, found `i32`"** — `rewrite_returns` walks statements only (it has no
       `DeclarationStatement` arm at all and handles only `MatchStatement`), so a `return` inside a
       match-expression arm is never rewritten to `Poll::Ready(…)`. The diagnostic also **leaks the
       internal `Poll<T>` type** to a user who wrote `-> i64`.
  - **Fix shape:** audit every walker in `async_lower.cryo` for `MatchExpression` alongside its
    `MatchStatement` arm — `rewrite_returns` (plus the missing `DeclarationStatement`/`ExpressionStatement`
    expression descent), `stmt_first_use`/`expr_first_use`, `name_read_in_stmt`/`name_read_in_expr`,
    `mark_last_use_stmt`/`mark_last_use_expr`, `subst_name_*`, `stmt_await_count`, `stmt_diverges`. Treat an
    `await` inside a match-expression arm as its own increment (it is a nested-in-expression await, which is
    a separate documented E0600).
  - **Workaround until fixed** (used by the validation probe): write the unwrap as a match STATEMENT that
    assigns a pre-declared local — `mut s: T = <placeholder>; match (r) { Ok(v) => { s = v; } … }`.
  - Also confirmed still-documented behaviour, hit while writing the probe: an `await` nested in an
    expression (e.g. a match SUBJECT, `match (await f())`) is E0600 — hoist it to its own statement. The
    diagnostic is preceded by ~7 lines of `codegen failed for module N` noise before the real E0600.

  **Real-socket end-to-end validation of the fixes above (Linux):** async echo server + client over
  loopback on a 2-thread `Executor` — `TcpAccept`/`TcpRead`/`TcpWrite`/`TcpConnect`, owned-handle style,
  with a droppable value conditionally rebuilt from inside a branch carried across the same suspends.
  **30/30 runs pass**, exact drop count, valgrind clean (15 allocs / 15 frees, 0 errors).

- _2026-07-25_ — **The `MatchExpression` blind spot in the async lowering is CLOSED, and it hid a third,
  unreported miscompile. REPINNED both OS. UNCOMMITTED.** Baseline HEAD `eda79ddc`; tree was clean at start,
  `verify-pin` OK. Files: `sema/async_lower.cryo` (+~300), `tests/tests/lang/async_carry_across_await.cryo`
  (13 → 24 tests). **This unblocks the async-only socket port.**

  **Environment note:** this session ran on **real Windows** (PowerShell 5.1, msys64 gcc), not the Linux
  codespace of prior sessions. `make selfhost-check` on a Windows host runs the Windows 6-stage chain
  natively and delegates the Linux chain to WSL — both need `wsl.exe` + `python.exe` on PATH, both present.
  Consequence worth planning around: the Windows AFD reactor is finally testable on this host.

  **The invariant, stated once:** a `match` used as an EXPRESSION is a `match` statement that also yields a
  value. Every walker that descends into a `MatchStatement` must descend equally into a `MatchExpression` —
  it is the one expression form that contains *statements*, so it is the one place a statement-shaped walker
  can silently lose an entire subtree. Before this, `MatchExpression` appeared **once** in the file (the
  alpha-renamer) against 11 `MatchStatement` sites.

  Walkers given a `MatchExpression` arm: `rewrite_returns` (plus a new expression half `rewrite_returns_expr`,
  plus the entirely missing `DeclarationStatement`/`ExpressionStatement`/condition descent), `expr_await_count`,
  `name_read_in_expr`, `subst_name_expr`, `expr_first_use`, `mark_last_use_expr` (+ `mark_last_use_arm`),
  `stmt_diverges` (+ `expr_diverges`), and `has_free_edge` (+ `expr_has_free_edge`).

  **(1) The two reported symptoms.** `const v = match (r) { Ok(x) => { x } Err(_) => { return -1; } };` inside
  an `async function`: E0201 "cannot find value `r`" (the use-analysis never saw the subject read, so a value
  read ONLY as a match subject was never carried across the suspend) and E0200 "expected `Poll<i64>`, found
  `i32`" (`rewrite_returns` walked statements only, so an arm's `return` was never wrapped — and the
  diagnostic leaked the internal `Poll<T>` to a user who wrote `-> i64`). The discriminator that proves it is
  the lowering and not the language: **the identical body compiles clean and returns the right answer when
  not `async`.** Run that control first on anything in this area.

  **(2) The unreported one — a silent miscompile, found by probing rather than from the audit list.**
  `has_free_edge` (which decides whether an await-free statement must still be exploded into states because it
  holds a `break`/`continue` targeting an enclosing loop) was blind to `MatchExpression` too. A `break` in a
  match-expression arm inside an awaiting loop was therefore emitted as-is, and a native `break` inside the
  generated `loop { match (state) { … } }` **escapes the state-dispatch loop, not the user's loop**. Probe:
  sync returned 10, async **trapped** (`0xC0000003`). `break` in a match-expression arm is legal Cryo and
  works correctly in a non-async function, so this was reachable, wrong, and silent.
  - **Fix = diagnose, not lower.** A match-expression arm is part of an expression, so its edge cannot become
    a state transition the way a `match` STATEMENT arm's can — lowering it is the same job as "await nested in
    an expression", a separate increment. It now emits a distinct E0600 naming the remedy (write the `match`
    as a statement assigning a pre-declared local), gated on `stmt_await_count == 0` so the pre-existing
    await-shape message is not reused for a case with no `await` in it. Remedy verified: it compiles, and
    async then matches sync exactly.

  **(3) `await` inside a match-expression arm is now a clean E0600** at the right span. Previously
  `expr_await_count` returned 0 for the whole subtree, so the shape was emitted as-is and the surviving
  `await` hit codegen's "not implemented" hard error behind ~7 lines of `codegen failed for module N` noise.

  **Two `mark_last_use` refinements, both semantic rather than cosmetic:** an arm body's TRAILING expression
  IS the arm's value, so it inherits the match's own by-value position (`mark_last_use_arm`) — an arm yielding
  a carried value into a by-value consumer really does give it away; and the `MatchStatement` arm now walks
  arm GUARDS (a guard runs before its body and only reads, so a name last mentioned there is still owned by
  the state), which it previously skipped.

  **A warning that was NOT ours — reported, not folded in.** The guard probe drew `W0001 unused variable` on a
  local read only by a match-arm guard. Probed before assuming: it fires identically for a `match` STATEMENT
  guard and in a **non-async** function, while a read in an arm BODY is seen correctly. So **a name read only
  in a match-arm guard is missing from the resolution map** that `dead_code.cryo`'s unused-local lint is built
  from (`build_used`); `sema.cryo:resolve_match_expr` DOES resolve the guard, so the gap is in what the
  resolution map records, not in whether the guard is visited. Pre-existing, language-wide, unrelated to
  async, and codegen is correct (the guard evaluates). Left for Jake to scope; the permanent guard test
  `_`-prefixes its binding so the suite does not enshrine the false positive.

  **A language fact confirmed, not a bug:** conditionally moving a value out through one arm
  (`const got = match (c) { true => { t } false => { Tag{…} } };`) warns `E0456` and drops exactly ONE value
  on BOTH paths — `t` is treated as moved on every path once it is moved on any. Sync and async give
  identical counts (1/1), which is the property that matters. An initial probe expectation of 2 was wrong
  about the language, not about the lowering.

  **Validated:** 21 assertions across 9 async shapes (subject-carry; arm-return; whole-match in return
  position; guard-only read; arm-rebuild taken and skipped with exact drop counts; nested inside a `match`
  STATEMENT arm; by-value arm yield; carry across TWO suspends), plus the droppable-aggregate subject
  (`Result<Handle, u32>` — the `TcpConnect::Output` shape). **11 permanent tests added**, all confirmed to
  actually execute (grep the run for `match_expr_`; do not trust a bare "unit: ok").
  **Gates:** `make test` OVERALL PASS — 1633 unit / 144 compile-fail / 9 projects, 0 failed. Both selfhost
  fixed points verified by DIRECT directory diff with denominators: Linux `s3` vs `s4` **0/236**, Windows
  `win-s3` vs `win-s4` **0/235**. Repin delta `win-s2` vs `win-s3` = **0/235** compiler and **0/149** stdlib,
  Linux stdlib `s2` vs `s3` = **0/149** — inert for all existing code.
  **Repinned anyway, and here the reason is forward-looking rather than precedent:** `make stdlib` builds with
  `bin/cryo`, so the first `match` expression the socket port writes inside an `async function` would fail the
  build against a pinned compiler lacking this fix.

  **Housekeeping note for Jake:** the pin sidecars record `worktree: dirty`, correctly — this work is
  uncommitted. `python scripts/verify-pin.py --require-clean` will keep failing until the tree is committed
  and repinned from clean; plain `verify-pin` passes.

  NEXT: the async-only socket port (`net::http`, `net::http2`, `net::ws`, `net::https` + the 4 net tests),
  then delete the blocking surface from `tcp.cryo`. Concrete socket touchpoints confirmed few outside
  `tcp.cryo` itself (54): `http` 3+5+1+1, `http2` 3+5+1+1+1, `ws` 5+1+1, `https` 1 — and `tls` 12
  (`context` 7 + `stream` 5), which still needs its own design pass (OpenSSL blocking BIO → non-blocking +
  `WANT_READ`/`WANT_WRITE`) before anyone touches it.

- _2026-07-25_ — **Windows AFD reactor VALIDATED (30/30), unblocked by a root-cause fix to qualified-global
  resolution. REPINNED both OS. UNCOMMITTED.** Files: `decl_index.cryo`, `sema/type_utils.cryo`,
  `sema/sema.cryo`, `codegen/ops/symbol_resolver.cryo`, `codegen/visit/ir_generator.cryo`. Jake chose the
  root-cause fix over the narrower stdlib one when asked.

  **Track 3 is DONE.** Async TCP echo over loopback on a 2-thread `Executor` — `TcpAccept`/`TcpRead`/
  `TcpWrite`/`TcpConnect`, owned-handle style, every `Result` unwrapped with a **match EXPRESSION** (so this
  doubles as end-to-end proof of the lowering fix above). **30/30 runs, 0 failures, 0 hangs.** The AFD
  backend itself needed no changes: IOCP + `\Device\Afd` + `IOCTL_AFD_POLL` work exactly as designed on a
  real Windows host. The wine hang was purely wine.

  **The blocker it surfaced: a qualified `mod::CONST` reference silently bound to the WRONG module.**
  `set_nonblocking` failed with `WSAEOPNOTSUPP`, because `syscall::FIONBIO` resolved to `libc::FIONBIO` —
  the Linux ioctl `0x5421` instead of the Winsock `0x8004667E`. Proven by probe: `![target(windows)]` gating
  works (a local marker returned its windows value), the windows-gated SIBLING `syscall::WSAEISCONN`
  resolved correctly (nothing else declares that leaf), and `ioctlsocket` with the literal returned `rc=0`.
  So: not gating, not the syscall, not the reactor — name resolution.
  - **Root cause, in TWO places.** `sema.cryo:resolve_scope_resolution` and
    `ir_generator.cryo:visit(ScopeResolutionNode*)` both ended in a **bare-leaf global lookup that discards
    the qualifier**. `decl_index.global_vars` is keyed by bare name and is last-write-wins across every
    module declaring that leaf, so the qualifier was decorative. The codegen fallback even stated the false
    premise in a comment — *"Globals are unique LLVM symbols at link time"* — which is untrue precisely
    because `register_global_with_module` mangles `C$<ns>.<name>` so that same-leaf globals DON'T collide.
    Sema picking wrong gave the wrong TYPE (`u64` vs `i32`); codegen picking wrong gave the wrong VALUE.
  - **Fix** uses data the index already recorded but nothing consulted: `module_global_namespaces`. Added a
    parallel `module_global_types` (the bare map cannot answer "the `FIONBIO` declared in `sys::syscall`")
    and `DeclarationIndex::find_global_in_scope`, which matches the scope segment against the declaring
    namespace's last `::` segment — reusing `CallResolver`'s existing allocation-free module-suffix idiom
    rather than inventing one. Both call sites try it BEFORE their bare fallback. **Strictly additive:** no
    match, or ambiguity between two namespaces ending in the same segment, returns invalid and the old path
    runs unchanged. Codegen caches under the namespace-qualified key, since the bare cache key belongs to
    whichever module bound first. Type/enum scopes (`Result::Ok`) short-circuit earlier and are untouched.
  - **Blast radius — I over-stated this when asking Jake, and the correction matters.** A scan found 9
    cross-file const leaves with numerically differing values, and I said the 7 errno ones were "likely
    wrong the same way". **They were not.** They are referenced BARE within their own module, where
    `lookup_global_var` already prefers the current module via `qualify_symbol_sym`. Grepping for qualified
    uses, `net/sys.cryo:200` is the **only** qualified reference among all 9 — so `FIONBIO` was the only
    site actually miscompiling. The IR delta confirms it exactly: **one** stdlib `.ll` changed, and the diff
    is two lines swapping `C$3std.3ffi.4libc.7FIONBIO` for `C$3std.3sys.7syscall.7FIONBIO`. The fix is still
    worth having — it closes the class — but the damage was 1 site, not 8.
  - **Gates:** `make test` OVERALL PASS (1633 / 144 / 9, 0 failed). Both fixed points by direct diff: Linux
    `s3`vs`s4` **0/236**, Windows `win-s3`vs`win-s4` **0/235**. Compiler pin delta `win-s2`vs`win-s3`
    **0/235**. stdlib Windows **1/149** (`std/net/sys.ll`); stdlib Linux **0/149** — expected, since the
    reference sits in a `![target(windows)]` function stripped on Linux, which is exactly why this survived
    every Linux-only session.

  **MEASUREMENT TRAP — the repin recipe in §6 is right for the COMPILER and WRONG for stdlib.** For the
  compiler, `win-s2` is built by the PINNED compiler and `win-s3` by the new one, so diffing them measures
  the pin delta. For stdlib it does NOT: `.bin/self/win-s2` is stdlib built by stage-2 and `win-s3` by
  stage-3, and **both stages are the current source**, so that diff is vacuously 0 no matter what changed.
  The stdlib pin delta is **`win-s1` (via pinned) vs `win-s2` (via new)**. On Linux there is no `self/s1`
  at all — stage 1 writes to `.bin/target/release/host/local/ir`, so the Linux pair is that vs
  `.bin/self/s2/...`. Diffing the wrong pair here reports `0/0` or a vacuous `0/149` and reads exactly like
  a clean pass.

---

- **2026-07-24 — soundness of pointers across a suspend (`E0455`), `-> void`, declaration order,
  recursion; plus a scoped investigation of generic `async fn`.** Five items, four landed and gated.
  `make test` OVERALL PASS **1648 unit / 150 compile-fail / 9 projects**, up from 1633/144/9 — 15 new unit
  tests and 6 new compile-fail cases, all async. Every one is a new file; nothing existing changed except
  `sema.cryo` and `async_lower.cryo`.

  1. **A raw pointer into the poll frame held across an `await` was a SILENT miscompile — now `E0455`.**
     Confirmed live before touching anything: `mut buf: u8[8]; const p = &buf[0]; await …; *p = 42;` moved
     `&buf[0]` from `…FB30` to `…FB18` and the write landed on dead stack (result 0, not 42), while the
     identical body without `async` returned 42. The cause is not a bug but the carry protocol itself:
     `promote_cross_state` re-materializes every carried local per state (aggregates via `take().unwrap()`
     into a fresh block-local, scalars via a `const x = this.field` copy), and each poll runs on a **new
     native frame** — so no address of a frame local can outlive the state that took it.
     Jake chose **diagnose now + buffer-owning I/O futures** over making carried locals address-stable;
     address-stability would only hold while the future never moves after its first poll, which is exactly
     what `Pin` enforces and `Pin` was already ruled infeasible here (§4). The check rejects the two ways an
     address escapes its state — a carried value whose initializer or later assignment takes a frame address
     (`reject_frame_addr_carry`, called from both the aggregate and the scalar promotion paths), and an
     address handed to the awaited future itself (in `lower_carrier_sm`, which is the
     `TcpRead::start(s, &buf[0], 64)` shape the socket port would otherwise have baked in everywhere).
     It deliberately does **not** reject the transient `f(&local)` form, whose pointer never leaves the
     state — passing owning aggregates by pointer is ordinary Cryo and banning it would be worse than no
     check. Frame-rootedness is decided by walking the place and stopping at the first indirection
     (`addr_place_root` / `place_leaves_frame`), so `&dyn_array[0]` (heap block), `&*ptr_param` (caller's
     frame) and `&GLOBAL` stay legal — each verified by a test that writes through the pointer AFTER a
     suspend and reads the value back. `frame_names` is populated from `mint_local`, the single funnel every
     local and match-binding rename passes through, so the check cannot under-report by missing a nested
     declaration.
  2. **`async function … -> void` now works — it was broken in EVERY form, not just with awaits.** §5 listed
     only "a body with awaits that falls off its end"; in fact even `async function f() -> void { }` failed
     `E0403` ("`poll` reaches the end of its body without returning"), because a future has to hand a value
     to `Poll::Ready` and `void` is the absence of one. `-> void` now lowers with **`Output = ()`**: unit is
     a value, so the terminal return is well-formed, and a body that ends without a `return` gets that unit
     completion synthesized. The append happens **after** `promote_cross_state`/`carry_params`, because both
     append their hand-back stores to the end of a state and a `return` placed first would strand them
     behind it. The old `E0600` survives only for a value-returning body the return analysis thinks can fall
     off — a real disagreement with the pre-lowering check, not the void case.
  3. **Declaration order no longer matters, and the misleading `E0306` is gone.** Lowering needs a typed
     body so it runs at the end of each function's visit, but `await` resolves the callee through the
     declaration index — so a caller visited first saw the callee's pre-lowering signature and was told its
     Output "does not implement `Future`", naming a type the user never wrote. `lower` is now split: a new
     **`declare`** establishes everything a CALLER needs (the state-machine struct, `poll`'s signature, the
     `Future` impl, and the function's registered return type) and a module-wide pre-pass
     (`SemaVisitor::declare_async_futures`) runs it for every `async fn` before any body is typed. The node's
     own `resolved_return_type` deliberately keeps the DECLARED type — `enter_function` types the body's
     returns against it and `check_function_returns` validates them, both before `lower` runs — so only the
     index entry is repointed at the struct. Forward, backward and mixed awaits all resolve, as does a plain
     function calling an `async fn` declared below it.
  4. **Recursive async fns were a silent miscompile that this exposed, and are now diagnosed.** Before the
     split, recursion failed with the same accidental `E0306`; removing that made `countdown(n-1)`
     **compile** and then panic at runtime with "PendingThenReady polled after completion" — the
     self-referential future has no finite size, and the layout that results overlaps the nested future with
     the outer one. There is no boxing escape hatch to offer (`dyn` is post-1.0), so the recursion is
     rejected at the `await` that closes the cycle. Detection walks an **awaits graph** over generated future
     types (`edge_from`/`edge_to`, `future_reaches`) rather than comparing a pair of types, which is what
     catches the mutual case: for a `ping`/`pong` pair the first future's fields are still empty when it
     needs the answer, so a field-based check would miss it. Reported in whichever function lowers second.
  5. **Generic `async fn`: INVESTIGATED, NOT LANDED — it still emits the clean `E0600` at the declaration.**
     Jake asked for this working, not merely diagnosed. The design below is validated and got most of the way;
     the remaining step is inside the monomorphizer and I stopped rather than keep guessing. **The full WIP is
     preserved** as `WIP-async_lower-generic.cryo` and `WIP-sema-generic.cryo` (path in the handoff); the tree
     was reverted to the gated-green state so nobody inherits a half-lowered compiler. Findings in the order
     they will be needed:
     - **`lambda_synth` is a dead end as a model.** HANDOFF suggested mirroring how closures inside generic
       fns are handled; they are **rejected**, not supported (`lambda_synth.cryo:478-488`, `:544`).
     - **The design that is right:** the future is generic in the SAME parameters as the function —
       `identity$Future<T>` with `implement<T> trait Future<T> for identity$Future<T>` — registered as a
       `TemplateEntry` (`NodeKind::StructDeclaration`, `base_type` = the `create_struct` base) so the
       monomorphizer specializes `Fut<i32>` from the same demand that specializes `f<i32>`. Pass order
       permits it: TemplateRegistration → TypeResolution → **sema** → **Monomorphization**, so a template
       registered during sema is in time.
     - **What already works with the WIP applied:** the template registers, `Fut<i64>` forms, and
       `block_on`'s `F` binds to `main::identity$Future_0<i64>`.
     - **The trap that cost the most, and the general lesson:** `make_type_ann` pre-resolves, which is what
       lets a synthesized annotation skip name lookup — and pre-resolution makes it **opaque to
       substitution**, because the monomorphizer specializes a template by rewriting the generic parameters
       that appear in its ANNOTATIONS. Every synthesized type mentioning `T` must therefore be spelled by
       name: the rewritten return annotation (`Fut<T>` as a `GenericAnnotation`), the ctor literal's
       `generic_args`, `poll`'s `Poll<T>`, the impl's trait args and `Output` assoc binding, and each
       parameter field (clone the parameter's own source annotation). Fixing the return annotation and then
       the ctor args each visibly moved the error, which is how the diagnosis was confirmed.
     - **Where it stops:** `Fut<i64>` is created but never SPECIALIZED — unsubstituted member type (`E0900`
       "a member type of type #N did not resolve"), no `poll` on it (`E0358`), and `Output` still `T`
       (`E0200`). All three are one fact: **nothing demands the specialization.**
       `MonoState::enqueue_from_type_ref` would accept it (base is a Struct, `is_template` true, args
       concrete), so the gap is upstream — whichever free-function specialization path should call
       `request_nested_instantiations` on the spec's `resolved_return_type`. The METHOD path does exactly
       that at `mono/ast_resolver.cryo:601`; the free-function analogue is around
       `mono/monomorphizer.cryo:718-727`. **Start there.**
     - **Two design consequences already settled, worth keeping:** (a) the symbolic generic-body walk becomes
       load-bearing rather than additive, since the lowering consumes the resolved types it leaves on the
       body — so it has to run even when `CRYO_NO_SYMBOLIC_CHECK` disables it (the WIP adds
       `symbolic_check_body_forced`); (b) a carried local whose type is a bare type parameter cannot pick a
       promotion strategy, because `zeroable_kind` cannot tell a scalar from an aggregate — route those to
       the `Option<T>` carrier, which is correct for both.
  - **Gates:** `make test` OVERALL PASS (1648 / 150 / 9, 0 failed) — run twice, once on the landed state and
    again after the generic-async WIP was reverted out, so the tree as left is the gated one.
    `make selfhost-check` exit 0 with **both** `FIXED POINT OK` markers (target-IR + native-PE). Pin deltas
    by the §6 pairs: compiler `win-s2` vs `win-s3` = **0/235 `.ll`**, stdlib `win-s1` vs `win-s2` =
    **0/149 `.ll`** (the correct stdlib pair; `win-s2` vs `win-s3` is the vacuous one — see the measurement
    trap above). **So NO REPIN is needed**, which is the expected result for a compiler-only async change:
    neither `stdlib/` nor the compiler's own source contains an `async function`, so nothing downstream of
    the pin moves. The consequence to remember is that the pinned `bin/cryo` does not carry the new
    diagnostics until someone repins.

---

### 2026-07-25 — Generic `async function` LANDED (the §4 mission)

The previous session's design was right and is unchanged; what was missing was two root causes, both one
layer below the async lowering. The stopgap `E0600` at the declaration and its negative test
(`E0600_async_generic_function.cryo`) are **deleted**.

**The design (as previously specified, now built).** The future is generic in the same parameters as the
function — `identity$Future<T>` with `implement<T> trait Future<T> for identity$Future<T>` — registered as a
`TemplateEntry` (`NodeKind::StructDeclaration`, `base_type` = the `create_struct` base), so the monomorphizer
specializes `Fut<i64>` from the same demand that specializes `identity<i64>`. `AsyncDecl` gained `self_ty`
(the instantiated `Fut<T>`, what a VALUE of the future is) and `generic`; `struct_ref` stays the
uninstantiated base that the template registers against and that carries the arena field table. `self_ty`
is used for the function's registered and final return type, the constructor literal's resolved type, and
`sm.struct_ref` (the type of `this` inside `poll`). The impl block gets `generic_params` + `target_args`
(so `This` resolves to the full `Fut<T>`) and is hung off the template with `register_impl_block`, so the
specialization gets its `Future` impl rather than existing without a `poll`. Everything the lowering builds
that can mention a parameter runs under `arena.set_symbolic_no_demand(true)`, restored on every exit path.

**Root cause 1 — nothing demanded the specialization.** The previous session left this pointing at the
monomorphizer's free-function `request_nested_instantiations`. That was a red herring: that path is fine.
Once the *annotations* were spelled correctly the demand arrived on its own, because the specialized
function's return annotation resolves to `Fut<i64>` and the existing signature walk enqueues it. The real
work was the annotation spelling, generalized:

  * **`type_ann_for(ty)` replaces per-site annotation building.** A subtree with no generic parameter is
    pre-resolved (cheap, and skips a name lookup a synthesized name might not survive); a subtree that
    mentions one is rebuilt structurally down to the parameter, which is spelled by NAME. It returns null
    for a shape with no annotation spelling, and `add_future_field` reports that rather than emitting a
    field that would silently stay generic in the specialization. This covers the future's fields, the
    `Option<F_k>` sub-futures, promoted locals, `Poll<Output>`, and the impl's `Output` binding uniformly —
    the previous plan's list of hand-written cases.
  * **Why it must be by name at all:** the ASTCloner deliberately drops every `resolved_type`
    (`clone_field_decl`: "resolved_type left as TypeRef::invalid()"), and `resolve_fields` only re-resolves a
    field that HAS an annotation. A specialization therefore sees annotations and nothing else.

**Root cause 2 — `subst_free_call_return` refused to answer inside a symbolic body.** `await inner<T>(v)`
reported "`await` requires an expression whose type implements `Future`; `T` does not", because
`CallResolver::subst_free_call_return` returns invalid whenever a binding or the result still contains a
generic param. That guard is right for a CONCRETE caller typed pre-mono and is now narrowed to exactly that
(`!state.in_symbolic_check`). Inside the symbolic walk the caller's own parameters are abstract, so
`mk<T>(v)` genuinely HAS type `W<T>`. **This was pre-existing and not async-specific** — the control
(`const w: W<T> = mk<T>(v);` in a plain generic fn) appeared to work only because the explicit annotation
supplied the type and nothing consulted the call's own; `await` is the first construct that needs it.

**A pre-existing bug found on the way:** `param_field_type` built the `Option<T>` carrier from `sm.opt_base`
without checking it is valid. `opt_base` is only looked up when the body has an `await`, so a **no-await**
async fn with a droppable parameter minted a garbage `?<T>` instantiation. A body with no suspend has
nothing to carry, so the carrier predicate is now `param_is_carried` = `opt_base.is_valid() &&
param_needs_carrier`, used by the field type, the constructor, and `carry_params` alike.

**Two things the previous session predicted that did NOT materialize.** (a) The `zeroable_kind` concern —
a carried local of bare parameter type — needed no special casing; the existing promotion already routes it
correctly, and `via_local<String>` (droppable `T`) round-trips and drops exactly once. (b) `lambda_synth`
was correctly identified as a dead end and was not touched.

**Also required:** `symbolic_check_body_forced` (the walk is load-bearing for async — it puts the resolved
types the state-machine build consumes on the body — so `CRYO_NO_SYMBOLIC_CHECK` must not switch it off
there), and `declare_async_futures` no longer skips generic fns, so declaration order stays irrelevant.

**Known adjacent limitation, NOT introduced here and NOT async-specific:** a generic stdlib type reached
through a re-export (`import std::future;` → `public module future::ready;`) does not resolve
`Ready<T>::new` inside a generic body; importing `std::future::ready` directly works. The same body with
`Ready<i64>::new` resolves under either import, so it is the re-export + type-parameter combination.

**Gates (Linux host).** `make test` **OVERALL PASS** — unit 12/12 new + full suite ok, compile-fail 149
(150 − the deleted negative test), projects 11. `make selfhost-check` Linux stage-3 ≡ stage-4 byte-identical
IR. `tests/test-roster.txt` regenerated by MERGE, not `--update`: `--update` on Linux would silently DELETE
`ProcessCommand::output_large_stderr_no_deadlock_win`, which Linux cannot discover — the roster is
platform-sensitive, and that entry is in the committed golden at HEAD. Net +12 entries (1648 → 1660).

`make selfhost-check` exit 0 with **both** `FIXED POINT OK` markers (Linux target-IR + Windows native-PE).

**Pin deltas by the §6 pairs — all zero, so NO REPIN.** Compiler `win-s2` vs `win-s3` = **0/161 `.ll`**;
stdlib `win-s1` vs `win-s2` = **0/149**; Linux stdlib `target` vs `self/s2` = **0/149**. The Linux compiler
pair (`build/target` vs `build/self/s3`) shows 57 files differing **for a non-semantic reason worth
recording**: the `FILE` macro embeds the stdlib path as the invoking stage passed it — absolute
(`/workspaces/CryoLang/bin/../stdlib/…`) from the pinned `bin/cryo`, relative (`./../stdlib/…`) from
`compiler/build/cryo` — so the string and its `[N x i8]` length differ. Normalizing just that path yields
**0/161 residual**. This is expected for a compiler-only async change: neither `stdlib/` nor the compiler's
own source contains an `async function`, and the `call_resolver` narrowing fires only under
`in_symbolic_check`, which is a check-only walk that emits no code.

**Pin note (pre-existing, not caused here).** `bin/cryo` / `bin/cryo.exe` are pinned at `89a57b30`, **two
commits behind HEAD** (`776fc805`). Since no IR moves, a repin is not required by the repin rule — but the
pinned compiler cannot itself COMPILE a generic `async function` until someone repins, which will matter the
moment `stdlib/` grows one.

### 2026-07-25 — `async` METHODS LANDED (the §4 / handoff mission)

`async` was a top-level-function modifier only: `KwAsync` was consumed in exactly one place, and
`async fetch(&this)` was a parse error. It is now a method modifier too, on every owner kind.

**Surface.** All three receiver forms are allowed and `E0455` is NOT extended to the receiver (Jake's
call, taken in the previous handoff): `&this` / `mut &this` make the future hold a pointer into the caller's
object — the same unenforced contract `E0455` already blesses for pointer parameters — and banning them would
leave no natural spelling for an async method. `static async`, the implicit receiver, impl-block methods,
`type class` owners, generic owners and generic methods on generic owners all work.

**The receiver is the whole new idea.** A method's body reads its receiver as a bare `this`, and inside the
generated `poll` a bare `this` is the FUTURE — so leaving it alone is a silent miscompile, not a cosmetic
issue. The receiver is captured into a future field named `this$recv` (a `$` name no user can write) and
every `this` in the body is rewritten to the poll-frame binding the shadow prelude declares for it.

  * **The rewrite must run FIRST**, before the poll body is built. The shadow prelude, the cross-state
    promotion and the two carry passes all synthesize `this.<field>` accesses of their own; a receiver pass
    run after them would rewrite the `this` inside those too.
  * **A pointer receiver is read back through a deref** (`*(this$recv)`), not as a bare field read. Every use
    in the body was typed by sema against the owner BY VALUE, so the deref keeps the body's types exactly as
    they were — `f(this)` still copies, `this.m()` still binds an lvalue receiver. Spelling it as `T*` and
    leaning on member-access auto-deref would have typed `f(this)` as passing a pointer. `subst_name_expr`
    grew a `subst_mode` for this rather than a `deref` parameter threaded through ~30 recursive call sites.
  * **The receiver is routed through the ORDINARY parameter machinery**, via three accessors
    (`param_field_name` / `param_slot_type` / `param_has_slot`) used by all six parameter walks. That is what
    makes a droppable by-value receiver carry across a suspend in an `Option<T>` slot and drop exactly once,
    for free — it is just a non-Copy parameter as far as `carry_params` is concerned.
  * **An implicit receiver has no parameter node to walk**, so `declare` synthesizes an explicit `&this` and
    rotates it to the head of the list. `method_receiver_kind` already maps explicit-immutable-`&this` and
    implicit to the same `ReceiverKind::ImmutRef`, so nothing downstream can tell the difference.
  * The constructor the method becomes initializes the field with `&this` for a pointer receiver and `this`
    for a by-value one. `&this` as an expression yields the true address of the receiver object (probed:
    writes through it are visible to the caller, and `a == &c`).

**Registration — the part with the real teeth.** A free function's callers resolve through the declaration
index, so `declare` repoints the index and hands the node straight back to the body check with its DECLARED
return type. **A method's callers resolve through the NODE**, so that trick silently reintroduced
order-dependence: a caller typed before its callee (the callee declared lower in the type) saw the declared
Output and reported "`i64` does not implement `Future`". Fixed by leaving the FUTURE type on a method's node
permanently and lending the declared type back only for the body walk
(`AsyncLower::begin_body_check` / `end_body_check`, called at the three sema sites that walk a method body).
Alongside that, `repoint_method` rebuilds the owner's arena `MethodInfo.function_type` **in place** (found by
AST identity — `add_method` appends, so re-adding would leave two entries) and re-registers the declaration
index under the canonical owner name plus, for an impl block, the alias forms — through the *aliased* helper,
so every form keeps one mangled symbol. Cross-module + impl-block calls were probed end-to-end precisely
because a mismatch there is a link error, not a compile error.

**A generator bug found on the way, affecting free functions too.** The generated `poll` body was being run
through the unused-local lint (W0001), which is built from the RESOLVER's map — a pass that ran long before
the body existed. Every shadow binding was therefore "unused", reported against the async function's own
span. Free functions escaped only by accident: the shadow's span is the function's span, and any *call* to
the function records that same span key as used. Methods have no such accident (method calls are
type-directed and never enter the resolution map), so they warned. Fixed honestly with a new
`FunctionDeclNode.is_synthesized_body`, set on `poll` and consulted by `DeadCodeChecker::is_body_resolved`
next to the existing mono-clone and trait-default cases.

**Rejections, all `E0364` (a new code) with a message that says why.** Constructor (must produce a fully
initialized object before any caller can observe it), destructor (called by drop insertion where there is no
executor), field (holds a value, not a computation), trait method and `virtual`/`override` method (each
implementation lowers to its OWN future struct, so there is no common return type to declare or dispatch
through). The first three are parser-side and use a new `report_invalid_at` that does NOT enter panic mode —
the token stream is still synchronized, and panicking would abandon the member's body mid-expression and bury
the real shape of the declaration. `virtual`/`override` is sema-side, because each keyword is individually
fine and only the combination has no lowering.

**Parser.** `at_method_modifier()` is the guard that makes this safe: a keyword may legally NAME a method
(`as<T>()`, `type()`), so `static` / `async` read as modifiers only when the next token is neither `(` nor
`<`. `consume_method_modifiers` takes a run in either order, so `static async f()` and `async static f()` are
the same declaration — and `static async()` is still a static method *named* `async`. A leading `async` is
consumed ahead of the member head so every member kind sees it and none drops it silently; `static` is
deliberately NOT hoisted the same way, because `static Name()` has always meant a static method named after
the type rather than a constructor.

**Confirmed working (probes + tests):** `&this` with and without suspends; `mut &this` writing through to the
caller across a suspend; by-value `this` (droppable, dropped exactly once); implicit receiver; `static async`;
an async method awaiting another async method on the same object; branches and loops holding suspends inside
a method; `-> void`; declaration order irrelevant on both structs and classes; impl-block methods;
cross-module calls; generic owners at several type arguments including a struct payload; a generic method on
a generic owner; `static async` on a generic owner; and an async method's future driven by a real
2-thread `Executor`.

**Known adjacent limitation, NOT introduced here and NOT async-specific:** a `&this` method returning an
owning field BY VALUE (`get(&this) -> T { return this.v; }` on `Cell<String>`) double-frees — the returned
value and the field both own the block. The control (the identical body without `async`) reproduces it
exactly, so it is an ownership gap in the language, not in the lowering.

**Two silent-miscompile paths found in review and closed, both in the `declare` walk.**

  1. **An owner whose type could not be looked up was skipped quietly.** The method then reached `lower` with
     no pending record, and that fallback declares a node as a FREE function — where the body's `this` would
     resolve to the generated future instead of the receiver. It now reports (`E0203`) and clears `is_async`,
     so the bad path is unreachable rather than merely unlikely.
  2. **`CRYO_NO_SYMBOLIC_CHECK` switched off the lowering of an async method on a GENERIC owner.** A generic
     owner's methods reach sema only through `symbolic_check_owner_methods`, which was gated on the
     killswitch at five places. That is the same hazard `symbolic_check_body_forced` was introduced for: the
     walk is what puts resolved types on the body, and the state-machine build consumes them, so switching it
     off leaves the method unlowered while every caller has been repointed at its future. A new
     `owner_methods_need_walk(methods)` keeps the gate honored for everything it was meant for and forces the
     walk when the list holds an `async` method. **Verified by inspection only:** `CRYO_NO_SYMBOLIC_CHECK=1`
     currently fails to build `stdlib/` for an unrelated pre-existing reason (`value.hash(&h)` on `&string`,
     `codegen: no method 'hash'`), so that path cannot be exercised end to end today.

**`E0455` and the receiver — both directions are now tested.** A pointer into a receiver held BY POINTER is
sound across a suspend (the pointee is the caller's object, which does not move), and `addr_place_root` sees
that correctly because it stops at the deref the receiver rewrite introduces. A pointer into a BY-VALUE
receiver is NOT sound — that receiver is bound into a fresh poll frame on every poll — and is reported. The
first is `async_method_holds_a_pointer_into_a_pointer_receiver`; the second is
`negative/E0455_async_method_value_receiver_address.cryo`.

**Gates (Linux host).** `make test` **OVERALL PASS** — unit ok, compile-fail **155** (149 + 6 new negatives),
projects **11**. `tests/test-roster.txt` regenerated by MERGE, not `--update`: **1660 → 1678** (+18), with
`ProcessCommand::output_large_stderr_no_deadlock_win` and all 14 `should_panic` entries preserved.
`make roster-check` reports the documented Linux-only `1 missing, 0 new`.
`make selfhost-check` exit 0 with **both** `FIXED POINT OK` markers (Linux target-IR + Windows native-PE).

**Pin deltas by the §6 pairs — all zero, so NO REPIN.** Compiler `win-s2` vs `win-s3` = **0/161** local +
**0/74** std `.ll`; stdlib `win-s1` vs `win-s2` = **0/149**. Expected: neither `stdlib/` nor the compiler's
own source contains an `async` method, and every new parser/sema path is reached only by `async` code.
`make test` builds with `compiler/build/cryo` (the stage-2 self-host), not with `bin/cryo`, so the new tests
do not need a repin to run from a clean tree — but as with generic `async function`, the PINNED compiler
cannot itself compile an `async` method until someone repins, which matters the moment `stdlib/` grows one.

### 2026-07-26 — Timers (4d) + combinators (4f) LANDED; two compiler bugs found, one fixed

**Timers (`stdlib/future/timer.cryo`, `reactor.cryo`).** The reactor grew a deadline-sorted chain of
`Timer` blocks (`register_timer(deadline, waker) -> id` / `cancel_timer(id)`), mirroring the
`Registration` table's shape and its no-waker-under-the-lock rule — `fire_expired` unlinks and frees
ONE timer per lock acquisition and wakes it after the unlock, which avoids collecting an unbounded
number of wakers into caller-owned locals. `reactor_body` now calls `run_once`, which bounds
`poll_once` by the earliest deadline (rounding UP, so a wait never ends early) instead of passing
`-1`, and `register_timer` kicks the reactor when the new timer becomes the head. `Sleep` holds an
absolute deadline, so re-polling never extends it; `Drop` disarms, which is what makes it usable as a
`select` loser. With no reactor on the thread (`future::block_on`) it self-wakes rather than parking
with nowhere to register — that driver's documented contract is "makes progress on every poll", and
checking the clock is progress.

**A real teardown use-after-free, fixed on the way.** `Executor::drop` called `Reactor::destroy`
(which freed the block) BEFORE `inner.drain_cancelled()`. A task that readiness had already woken is
still in the ready queue at that point; draining it runs its future's `Drop`, which cancels against
the reactor — freed memory. Split into `Reactor::stop` (signal, join the thread, release every
registration AND timer, close the backend, leave the block and its lock alive) and `Reactor::free`,
with the free moved after the drain. The pre-existing ordering rationale (stop the reactor before
draining, or a task woken in the interim sits in the queue forever) is preserved.

**Combinators (`stdlib/future/combinator.cryo`).** `Join`, `Select`, `Timeout`, built through the
`Futures` namespace — a namespace rather than free functions because a child produced by an
`async function` has a compiler-generated type no caller can name, so the constructors' bounds have
to infer the whole parameter set. Cancellation needs no mechanism: a combinator owns its children, so
the `Select` loser and the `Timeout` victim are released when the combinator drops, and that drop is
exactly where a `Sleep` disarms and an I/O future deregisters. Arity is 2 and higher arities nest
(`Futures::join(a, Futures::join(b, c))`, tested) — Cryo has no variadic generics and a `Join3/4/...`
tower would say nothing new. `Timeout` polls the OPERATION before the deadline, so a future that
becomes ready on the same poll the timer fires reports its value rather than a timeout; it did
complete, and for an I/O future the side effect has already happened.

**Compiler bug #1 — a zero-sized type as a `Future`'s `Output` payload MISCOMPILES. ✅ FIXED
2026-07-27.** Discriminated minimally: `Result<i64, Empty>` as a plain value is correct, and
`Result<T, NonEmpty>` as a future's `Output` is correct, but `Result<T, Empty>` as a future's `Output`
handed back a garbage `T` and a misread discriminant (a `Timeout` that should have said `Err` said
`Ok` with junk).

Root cause was NOT async and not unit-vs-empty-struct special-casing. `compute_enum_layout` deferred
an enum whose payload type still had `size_bytes() == 0`, using that as "layout not computed yet".
For a legitimately zero-sized payload that is *permanently* true, so `Result<i64, Empty>` never got a
layout and kept size 0. `TypeMapper::map_enum` recomputes from the leaf field types, which is why one
level looked fine — but the OUTER `Poll<Result<i64, Empty>>` read the inner enum's uncomputed 0 as a
real width and lowered its payload to `[0 x i8]`.

`layout_settled` already existed for exactly this hazard (its doc comment describes it) but was only
consulted by struct lowering, and only knew about Struct and Class. It now covers every layout-bearing
kind, and `compute_enum_layout` asks it instead of testing the size. Tests:
`async_zero_sized_type_in_output_payload` + a sized control + a sync control, in
`async_stress_shapes.cryo`.

`Elapsed` keeping its `deadline` field is now a design choice rather than a workaround — it is more
useful than an empty marker — but it is no longer load-bearing.

**Compiler bug #2 — nested-bound inference gap; `try_join` is NOT shipped because of it.**

```cryo
static direct<A,B,OA,OB>(a:A,b:B) -> W4<A,B,OA,OB> where A: Future<OA>, B: Future<OB>          // infers
static nested<A,B,T1,T2,E>(a:A,b:B) -> W5<A,B,T1,T2,E>
    where A: Future<Result<T1,E>>, B: Future<Result<T2,E>>                                     // does NOT
```

Every call to the second fails `E0200 expected Result<(i64,i64),boolean>, found Result<(T1,T2),E>`.
Narrowed: a nested bound infers fine when the return type does NOT also mention `A`/`B`; two bounds
sharing `E` is fine; the Output shape `Result<(T1,T2),E>` is fine on its own — it is the combination.
The compiler already does exactly this recovery for generic OWNERS in
`infer_static_owner_bindings_from_expected` (unify the declared return against the expected type);
the same applied to a method's own parameters would unblock `try_join`. Rather than ship an
unconstructible type, `try_join` was left out and the module documents the stand-in: `join` over two
`Result` futures gives the concurrency and both results, just not sibling-cancellation on first
error.

**Validation.** 10 timer tests + 9 combinator tests, all passing. Probes confirmed three parallel
300ms sleeps take ~300ms (not 900ms), a `select` loser sleeping for an hour neither blocks the
`select` nor wedges teardown, and a timer armed at teardown is released rather than waited out.
No repin taken this session — the compiler is unchanged since `3e84658b`; everything here is stdlib
plus tests.

**Still open from this session:** the same-leaf caller-scope resolution bug (`Executor::spawn`
returning `thread::JoinHandle` when a program imports both `std::future` and `std::thread`) is PARKED
at Jake's direction, diagnosed in agent memory with a 3-file minimal repro. It is why the timer tests
reach for `spawn_detached` in one place.

---

### 2026-07-25 — `async fn main`, buffer-owning I/O futures, and `await` in a `match`-arm guard

Three things landed: the entry point, the last unsound corner of the async socket API, and the last
rejected `await` position. Compiler + stdlib + tests; UNCOMMITTED.

#### `async function main` (Jake's decision: `Executor` + `block_on`)

An `async function` lowers to a constructor returning its future, which for `main` would leave the
program's entry point returning a struct. So the async `main` is **renamed out of the entry-point
slot** (`main` -> `main$async`) and a synchronous `main` is synthesized in its place:

```
function main() -> i32 {
    mut ex: Executor = Executor::new();
    const main$out: i32 = ex.block_on(main$async());
    return main$out;
}
```

`-> void` drops the binding and returns `0`, so the synthesized entry point returns `i32` either way.
An `Executor` rather than `future::block_on` because `Reactor::set_current` is only called in
`worker_body`: under `future::block_on` there is no current reactor and every I/O or timer future
would park with nowhere to register. The executor is a LOCAL of `main`, so its `Drop` tears the
runtime down at exit — a runtime scoped to the entry point, not the ambient global one §7-5 ruled
out — and it drops only after the root's value has been bound.

Lives in `AsyncLower::desugar_async_main`, called from `Sema::declare_async_futures` BEFORE
`declare`: `declare` registers the future under whatever name the node carries, so renaming
afterwards would key that registration to `main`.

Details worth keeping:

- **Parameters are forwarded, not rejected.** The wrapper takes the same parameter list and passes it
  through, so an `async main` behaves exactly as a synchronous one: codegen widens a ZERO-parameter
  `main` to `(argc, argv, envp)` and publishes them to `std::env`, and a `main` that declares
  parameters opts out of that on either path. The cloned parameters need
  `set_resolved_type(p.resolved_type)` re-stamped — a clone keeps the annotation and DROPS the
  resolved type, and type resolution has already run, so without it codegen cannot lower the
  parameter and **skips the entry point's body entirely** (an LLVM "block has no terminator"
  failure).
- **`set_synthesized_body(true)`** on the wrapper: name resolution has already run, so the
  unused-local lint has no evidence for the locals this body declares and would report the executor
  and the root's value as unused.
- **New code `E0365_INVALID_ASYNC_MAIN`** for a `main` that is generic, or returns anything but
  `i32` / `void`. A missing `import std::future;` is left to `declare`'s existing message so one
  cause produces one diagnostic.
- Codegen needed NO change: it keys everything off the name `main` (unmangled symbol, argc/argv
  widening, the `--panic=unwind` `__cryo_user_main` wrapper, `cryo test`'s `skip_user_main`), and the
  synthesized wrapper is the thing named `main`.

Tests: three PROJECTS (`async_main`, `async_main_void`, `async_main_params`) — the entry point cannot
be a unit test, since the unit-test binary supplies its own `main`. Plus two negative tests
(`E0365_async_main_bad_return`, `E0365_async_main_generic`).

#### Buffer-owning `TcpRead` / `TcpWrite`

`TcpRead`/`TcpWrite` took `buf: u8*` + `len`. A future is moved between polls, so a buffer named by a
raw pointer into the caller's frame is written through a stale address — and since `E0455` now
rejects `&local[0]` into an awaited future, the API could not be used in its idiomatic spelling at
all. They now OWN an `Array<u8>` exactly as they own the socket, and hand it back through `TcpIo`
(`take_buf()`). The bytes land in the array's heap block, which the moves do not disturb, so the API
is sound BY CONSTRUCTION rather than by discipline.

- A read hands the buffer back TRUNCATED to what arrived, so its length is the byte count.
- A write hands it back untouched, so a caller with a short write still has the bytes it has not
  sent.
- Both `Drop`s free the buffer alongside closing the socket.
- Needed `Array<T>::resize(new_length, value) where T: Copy + Drop` (+ `try_resize`), which the type
  was simply missing: an array exposes only `[0, length)`, so `with_capacity` alone yields nothing to
  read INTO.

**There were ZERO tests and ZERO consumers of the async socket futures in the tree** — the "30/30 on
real Windows" validation was a scratch probe that did not survive its session. That gap is now
closed: `tests/tests/stdlib/net_tcp_async.cryo` runs a real loopback echo round trip
(`TcpConnect`/`TcpAccept`/`TcpRead`/`TcpWrite` joined on one `Executor`) and a cancellation test where
a read times out, exercising the drop path that deregisters the waker, closes the socket and frees
the buffer.

#### `await` in a `match`-arm guard (Jake chose "build it properly")

The last rejected `await` position. A guard runs after its own pattern matched and before the next
arm is tried, so it cannot be lifted above the `match`, and it cannot live inside the dispatch
`match` either — a `match` has no way to resume into the middle of itself.

Such a `match` now lowers to a **decision chain** (`AsyncLower::lower_match_chain_sm`), one test
state per arm:

```
t_k : match (subj) { pat_k => { capture; -> g_k }   _ => { -> t_k+1 } }
g_k : <guard, may suspend>;  true -> entry_k,  false -> t_k+1
t_n : -> join
```

which is exactly what "a false guard tries the next arm" means once the guard is no longer something
the dispatch can evaluate in one go. Every other `match` keeps the single dispatch, which costs one
state rather than one per arm. An arm whose guard does NOT suspend keeps that guard on its pattern,
where a false guard already falls through to the same `_ =>`.

Three things this had to get right, each of which a plausible chain gets wrong:

1. **The subject is evaluated once.** It is stored in a field of the future that the chain OWNS —
   deliberately not left to `promote_cross_state`, which treats a `match` subject as CONSUMED by the
   match (true of the single dispatch, which matches it once) and would therefore publish nothing,
   leaving the second test to take from an empty carrier. That was the first failure mode observed:
   `called Option::unwrap() on a None value`. A scalar subject sits in a plain field and each test
   reads a copy; anything else rides the `Option` carrier, taken at the top of a test into a local
   named per-state (so the generic promotion sees a single-block local and leaves it alone) and
   handed back at the bottom.
2. **The fall-through arm is only emitted when the arm can fail to match.** An irrefutable pattern
   whose guard was lifted out always matches, so a synthesized `_ =>` beside it is both dead code and
   a second wildcard arm — `E0114 duplicate _ arm`, which is what the first build reported.
3. **`hoist_match_expr` had a matching bail-out** that left a match EXPRESSION with an awaiting guard
   in place so the old diagnostic could name it. Removed, so the expression form goes through the
   chain like the statement form.

`capture_arm_bindings` was factored out of `lower_match_sm` and is now shared by both shapes; it
additionally rewrites the GUARD's reads of a binding to the promoted field when the guard was lifted
into a state of its own.

**One sub-case is rejected rather than lowered, precisely and with a stated rewrite:** an arm that
binds an OWNING payload out of the subject. A scalar binding copies and leaves the subject whole; an
owning one moves it, and the next test would match a hollowed-out value (and the chain would hand a
moved-from value back to a field the future still drops). The message points at binding inside the
arm body with a nested `match`, where the subject is no longer re-tested. Negative test
`E0600_async_guard_moves_owning_payload`. Lowering this properly means testing the pattern WITHOUT
binding and re-matching in the entry state once the guard passes; it is the one piece of guard
support not built.

Tests: `tests/tests/lang/async_match_guard.cryo`, 14 cases — guard true/false, a false guard falling
to a later arm with the SAME pattern, a guard reading its own pattern's binding, two guards, a guard
plus a suspending body, a guard inside a loop, a plain guard beside an awaiting one, the match
EXPRESSION form, and the two ORDER properties (the subject is evaluated exactly once; a guard on an
arm whose pattern did not match never runs).

#### Async TLS — the §5B prerequisite (Jake chose "do TLS first, then delete")

`net::tls` was the reason the blocking socket surface could not be deleted: OpenSSL is handed the
raw fd through a socket BIO, and the blocking path spins on `WANT_READ`/`WANT_WRITE` because its
socket blocks and those codes only appear transiently mid-record. Putting the socket in non-blocking
mode — which async requires — turns them into the normal "transport not ready" answer, and the spin
becomes a busy-loop that never yields.

`stdlib/net/tls/future.cryo` (new) mirrors the TCP futures exactly:

- `TlsHandshake` (Output `Result<TlsStream, IoError>`) — re-issues `SSL_connect`/`SSL_accept` on
  every poll, which is precisely what OpenSSL wants after a `WANT_*`. Carries the blocking path's
  belt-and-suspenders `SSL_get_verify_result` gate.
- `TlsRead` / `TlsWrite` (Output `TlsIo`) — own the stream AND an `Array<u8>`, handed back through
  `TlsIo::take_stream()` / `take_buf()`. A read truncates the buffer to what arrived; a write returns
  it untouched. `SSL_ERROR_ZERO_RETURN` is reported as `Ok(0)` — the TLS equivalent of EOF.
- `TlsConnector::connect_async` / `TlsAcceptor::accept_async` set the socket non-blocking, mint the
  `SSL*` with the same SNI + verification wiring as the blocking constructors, and return the
  handshake future. Everything up to the first `SSL_connect` happens there rather than on first poll,
  so a setup failure reaches the caller directly instead of being carried inside the future.
- `TlsStream` gained `ssl_ptr()` and `raw_fd()` so the futures can drive `SSL_*` and register
  readiness without reaching into its fields.

Two things this had to get right:

1. **The interest to arm is OpenSSL's to choose, not ours.** An `SSL_read` can return `WANT_WRITE`
   (a renegotiation needs to send) and an `SSL_write` can return `WANT_READ`. `interest_for(code)` is
   the single place that decides, and each future records the one direction it armed so its `Drop`
   cancels exactly that half — never the other operation's registration on the same socket.
2. **OpenSSL requires a `WANT_*` retry to repeat the same call with the SAME arguments.** A future is
   moved between polls, so this is only sound because the read/write futures OWN their buffer: an
   array's heap block keeps its address across those moves. The buffer-ownership change was therefore
   a genuine prerequisite for TLS, not merely a soundness tidy-up.

**Test:** `tests/tests/stdlib/net_tls_async.cryo` — a loopback handshake plus an encrypted echo round
trip. The blocking TLS test has to spawn a THREAD for the client, because `SSL_connect` would block
before the server ever reached `SSL_accept`; the async test runs **both sides as tasks on ONE
`Executor`** with no thread at all. That is the result under test, not an incidental simplification —
a handshake that failed to interleave would deadlock rather than fail. 8/8 clean runs.

**Still to do for §5B:** nothing has been ported and nothing deleted yet. The blocking surface
(`TcpStream::connect/read/write`, `TcpListener::bind/accept`, `TlsConnector::connect`,
`TlsAcceptor::accept`, `TlsStream`'s `Read`/`Write` impls) is all still in place, which is what keeps
the tree green. The remaining work is the consumer port — `net/http/*`, `net/http2/*`, `net/ws/*`,
`net/https.cryo`, `net/dns.cryo`, `net/socket/udp.cryo` (~7,400 lines over 19 files) — and it is a
one-way API break: every `HttpClient::get`-shaped call becomes `async`. `http2/connection.cryo` (855
lines) is generic over `S: Read + Write` with no concrete socket use, so it needs an async
`Read`/`Write` equivalent decided before it can move.

#### Also found, not fixed

- **`Executor::block_on` mis-codegens when one program instantiates it at BOTH a unit `Output` and a
  non-unit one, in that order. ✅ FIXED 2026-07-27.** `%calltmp = call void @...` — a void call given
  a name, which LLVM rejects (`Instruction has a name, but provides a void value!`). Order-dependent:
  the unit instantiation FIRST failed, i32-first compiled, and either alone compiled.

  Codegen was not at fault, and neither was mono — the callee was mangled correctly (`$Ru`, specialized
  at unit) in both orders. Sema was handing the call node the SIBLING specialization's return type. A
  generic method's return is looked up by NAME, and once the method exists at several instantiations
  that lookup can return another one's; `call_resolver` re-derives the right type per call and then
  decides whether to adopt it. It only adopted a CANONICAL return — one whose arena TypeID cannot
  change under re-derivation — and the accepted set was Int/Float/Bool/Char. So the re-derivation
  produced the correct `()`, the adoption test rejected it for not being a scalar, and the leaked
  `i64` stayed on a call whose callee returns void.

  `generic_scalar_return` is now `generic_canonical_return` and accepts Unit/Void/Never alongside the
  scalars. Compound / instantiated returns stay excluded for the original reason (re-deriving them
  mints a distinct TypeID from mono's). Tests: all three orderings in `future_executor.cryo`.
- **`PendingThenReady` returns `Pending` without waking**, so it cannot be driven by a real
  `Executor` — only by `future::block_on`, which re-polls immediately. Worth a doc note; it silently
  turns an Executor-based test into a hang or a "root future did not complete" panic.
- **`--panic=unwind` does not link on Windows at all**, with or without async (`__cryo_panic`,
  `__cryo_panic_finish`, `__cryo_personality_v0` undefined). Confirmed against a plain non-async
  `main`, so it is Track 4 (Win SEH) still being open, not anything from this session.

### 2026-07-26 — Generic-literal typing bug FIXED; async trait methods chosen and scoped

**Jake's design call, asked and answered.** For getting the generic transport consumers onto async he
chose **async trait methods** (§5c) over two cheaper options: `AsyncRead`/`AsyncWrite` poll-traits
plus generic owning adapter futures, and de-genericizing to a single `AsyncStream` union. He also
chose to FIX the monomorphization bug found on the way rather than route around it.

**Two facts, both probed, that decide the design.** `Http2Connection<S>` and `WebSocket<S>` hold
`inner: S*` — the transport is BORROWED — and `E0455` rejects constructing that from a local across
an `await` ("`k` holds the address of the local or parameter `c` and is live across an `await`"). So
the transport has to be OWNED by the connection whatever else changes. And `async` on a trait method
is rejected today (`E0364`, parser) for exactly the reason this phase has to solve: each impl lowers
to its own future struct, so there is no single return type to declare.

#### The compiler bug (FIXED)

A generic struct literal written INSIDE a generic body was typed as the **bare base**: `Wrap<S> { .. }`
denoted `Wrap`, not `Wrap<S>`. `Sema::resolve_struct_literal_type` built a `ResolutionContext` with no
generic bindings, so resolving the argument `S` failed and it bailed (there was an explicit
`contains_generic_param` early-out too), leaving `resolve_struct_literal` to fall back to
`lookup_type_by_sym` = the base.

It stayed invisible almost everywhere because a literal's type is normally supplied by the declared
type it flows into. `AsyncLower::lower_carrier_sm` is the exception — it reads
`aw.operand.resolved_type` DIRECTLY — so a generic `async function` awaiting a literal-built future
got the sub-future field `Option<Wrap>`, which mentions no generic parameter for specialization to
rewrite, while the body still minted `Wrap<Counter>` that nothing then monomorphized:
`E0900 unresolved generic instantiation after monomorphization`.

**Fix:** call `symbolic.symbolic_bind_params(&rc)` before resolving the args and return
`create_instantiation(base_ref, &arg_refs)` even when an arg mentions a parameter. `symbolic_bind_params`
is a no-op unless `state.in_symbolic_check`, which scopes the change to template bodies, and it sets
`rc.symbolic_no_demand`, so an abstract instantiation registers no monomorphization demand.

**Why the existing coverage missed it:** `async_generic_function.cryo`'s `gf_echo` awaits
`Ready<T>::new(v)` — a static CALL, which resolved correctly all along. Only the LITERAL form was
broken. Two tests added there (`generic_async_awaits_a_future_built_by_a_literal`, and the bounded
variant whose `poll` dispatches through a trait bound on the parameter).

Four controls are what located it, each PASSING and so eliminating a suspect: the same body without
`async`; a generic `async fn` awaiting a non-generic future; a NON-generic `async fn` awaiting the
generic future; and a nested generic field (`Option<Cell<S>>`) in both a hand-written struct and a
promoted async frame slot.

#### Async trait methods — design settled, three gaps block the build

The desugaring, using machinery that already exists: an `async` trait method gets an implicit
associated type for the future it returns and a projection return type.

```
type trait AsyncRead { type ReadFut; read(&this, n: i64) -> This::ReadFut; }
implement trait AsyncRead<TickFut> for struct Sock { ... }   // positional assoc sugar
```

Associated types are already modelled as trailing positional trait arguments (the same sugar
`implement trait Iterator<i32>` uses; `MethodBinding` computes `generic_params.length + assoc_idx`),
`ImplBlockNode::add_assoc_binding` already exists, and `async_lower` already calls it for `Output`.

**Probed GREEN:** a trait associated type used as a method's return type, bound positionally by an
impl, projected in generic code over an abstract base (`mut m: S::Made = s.make();`), and dispatched
through a projection bound (`where S: Maker, S::Made: Tick` → `m.tick()`).

**Probed RED — the three gaps, in build order:**

1. **A method return resolved through a projection bound drops the bound's own type arguments.**
   With `f: S::ReadFut` and `where S::ReadFut: Future<i64>`, `f.poll(&cx)` types as **`void`**.
   `MethodBinding::scan_projection_bounds` returns `types.lookup_method_return(leaf, method_name)`
   raw; `Future::poll` returns `Poll<This::Output>` and nothing substitutes `Output := i64` from the
   bound. The green `Maker`/`Tick` probe above passed only because `tick` returns a plain `i64` — no
   associated type in its return — so it never exercised this.
2. **A projection-typed value cannot bind another generic function's parameter.**
   `future::block_on(f)` with `f: S::ReadFut` gives `E0633 ... left 1 of 1 basic block(s)
   unterminated; body discarded` plus an LLVM verifier failure — the same silent whole-body-skip
   signature as the ASTCloner `resolved_type` trap.
3. **`await` on a projection-typed operand** — `E0306 async: could not resolve an awaited future's
   type`. Independently, `AsyncLower::type_ann_for` has no `TypeKind::AssocProjection` case, so the
   `Option<S::ReadFut>` frame field could not be spelled even once the type resolved.

Then the desugaring itself (trait side), the impl-side assoc binding, and lifting the `E0364`
parser rejection. **Async trait DEFAULT bodies** — one future generic over `This` — are a further
sub-case: ASK Jake before assuming they are in or out of scope.

#### Correction to gap 1 above — the real chain (investigated 2026-07-26, NOT fixed)

Gap 1 was characterised from black-box probes as "`scan_projection_bounds` returns the method return
raw". Instrumenting the compiler showed that is only the THIRD link, and the first two have to be
fixed before it is even reachable. In order:

1. **`resolve_method_call` returns early for a projection receiver.** `call_resolver.cryo` bails at
   `if (symbolic.symbolic_type_unresolved(obj_type)) { stash_abstract_receiver_method(...); return
   invalid; }` — an `AssocProjection` receiver is "unresolved", so the call yields no type and the
   use site reads `void`. This PREEMPTS the `AssocProjection` rescue further down (past the
   type-symbol lookup) that was plainly written for exactly this receiver. A rescue placed before the
   early return does make `lookup_method_through_projection_bounds` fire (verified by trace).
2. **A where-bound subject `S::Member` is not a projection annotation.** With the rescue in place the
   scan runs but matches nothing: `projection_ann_member(b.subject_type)` is EMPTY for
   `where S::ReadFut: Future<i64>`. `parse_trait_bounds` does take the projection branch for
   `Identifier ::` (parser.cryo ~2499), but `parse_base_type` then builds a qualified **Named**
   annotation rather than a `Projection` for a non-`This` base — `This::Item` works only because
   `KwThisType` is unambiguous. So the bound never matches the receiver's member.
3. **Only then** does the original gap-1 statement apply: the return looked up on the bound trait is
   `Poll<This::Output>` and nothing substitutes `Output := i64` from `Future<i64>`. A fix for this
   was written and compiles (a `subst_bound_assoc_args` + a `subst_assoc_by_member` walker mirroring
   `reduce_assoc_projections`), but it cannot be validated until 1 and 2 land, so it was REVERTED
   rather than left in the tree unverified.

The lesson worth keeping: the earlier `Maker`/`Tick` probe that "proved projection-bounded dispatch
works" proved no such thing — instrumentation showed `m.tick()` resolving on a CONCRETE `Ticker`,
because specialization had already reduced `S::Made`. It never exercised the projection path at all.
Any future claim that this machinery works needs a trace, not a green probe.


### 2026-07-26 — All THREE projection gaps FIXED; async trait methods scoped by Jake

The §4 mission of the handoff. Every gap that blocked projection-typed dispatch is fixed, each one
diagnosed by instrumenting the compiler rather than inferred from a probe's exit code. Six defects,
not three — the chain was longer than the handoff's characterisation.

**Gap 1a — the abstract-receiver early return preempts BOTH rescues.** `resolve_method_call` bailed
at `symbolic_type_unresolved(obj_type)`, which is true for a projection receiver AND for a
bounded/where-constrained param, so the `AssocProjection` and `BoundedParam` rescues written further
down (past the type-symbol lookup) were unreachable in a symbolic walk and every use site read
`void`. Replaced with `MethodBinding::abstract_receiver_method_return`, called AFTER
`stash_abstract_receiver_method` so the existing method-type-arg stash is untouched and the change is
strictly additive: previously invalid, now a type.

**Gap 1b — a where-bound subject `S::Member` was not a Projection annotation.** `parse_trait_bounds`
already commits to the projection reading for `Identifier ::`, but delegated to `parse_base_type`,
which flattens an identifier base into a qualified `Named` ("S::ReadFut"). FOUR independent decoders
(`projection_ann_member` ×2, `projection_member_of`, `is_projection_annotation`) pattern-match
`TypeAnnotation::Projection` and silently read that flat form as "not a projection", so the bound was
inert; only `This::Item` worked, because `This` is a keyword taking `parse_base_type`'s Projection
branch. Fixed at the parser with `parse_projection_subject`, scoped to where-clause subject position —
the one place the grammar can disambiguate a projection from a module path, and the fix that repairs
all four decoders at once instead of duplicating string-splitting into each with no scope to judge it.

**Gap 1c — the bound-trait method return came back RAW.** `Future::poll` declares
`-> Poll<This::Output>` and nothing substituted `Output := i64`. Added `subst_bound_assoc_args`
(associated types are trailing POSITIONAL trait args, `generic_params.length + assoc_idx`) plus a
`subst_assoc_by_member` walker. It deliberately does NOT cache onto the `AssocProjectionType`:
`This::Output` is trait-relative and interned once, so pinning one bound's answer would leak into
every other bound over that trait.

**Also 1c — `This` was never rebased onto the receiver.** `AsyncRead::read` declares
`-> This::ReadFut`; on a receiver typed `S` the answer must be `S::ReadFut`, the same projection the
caller's bound resolves to. `subst_this_in_trait_return` substitutes the canonical
`GenericParam("This", sentinel)` (one deduplicated arena entry, per `resolve_trait_method_signatures`)
via a plain `TypeSubstitution` — no third copy of the structural walker.

**Also 1a — a where-clause-bounded param is a plain `GenericParam`, not a `BoundedParam`.** Its
bounds live on the enclosing function/impl node, so `lookup_method_through_bounds` (which reads a
`BoundedParamType`'s own list) never saw them, and `s.read(3)` under `where S: AsyncRead` resolved to
nothing. Added `lookup_method_through_param_bounds` / `scan_param_bounds`, the bare-subject sibling of
the projection scan.

**Gap 2 — a projection-typed value could not bind another generic fn's param.** Two defects. In sema,
`project_where_bound_params_into` skipped an abstract subject and demanded a concrete derivation, so
`block_on<F, R>`'s `R` never bound, the caller discarded the whole binding set, and the call reached
codegen unspecialized (E0636 — not the E0633 the handoff recorded; gap 1's fix moves it further
along). Inside the symbolic walk `R = (S::ReadFut)::Output` is the correct answer, so a `symbolic`
flag now admits it — the same reasoning already written into `subst_free_call_return`. In mono, the
stash was substituted but never REDUCED: substitution rewrites a projection's base
(`S::ReadFut` → `Ticker::ReadFut`) and a projection over a concrete base contains no generic param, so
it survived the `contains_generic_param` guards and got MANGLED into a callee name nothing emits.
`CallSpecializer::concretize_stashed_arg` now applies-then-reduces at all FOUR stash-consuming sites.

**Gap 3 — `await` on a projection-typed operand.** `resolve_await` used only
`resolve_concrete_member`, which has no impl to read for `S::ReadFut`. Inside the symbolic walk the
Output is the nested projection `(S::ReadFut)::Output`; a concrete operand that fails the lookup
genuinely is not a `Future` and still gets the clean E0306. Plus the missing
`TypeKind::AssocProjection` case in `AsyncLower::type_ann_for`, spelled as a Projection annotation so
the substituter rewrites its base on specialization.

**Validation.** Six scratch probes, each RUN (not merely compiled) with its value checked, and each
built on the evidence rule from the previous entry — behaviour was confirmed by `cdebug` traces
showing the intended function running, and gap 1c was proved by a probe that FAILS without the fix:
two projections over one trait with different Outputs (`Future<i64>` vs `Future<boolean>`) collide
into a single arena type when the return is raw. A negative variant confirms the substitution TIGHTENS
typing — `expected i64, found boolean`, where it previously said `found This::Output`.
`make test` OVERALL PASS at the exact baseline (unit 1753, compile-fail 158, projects 12). All traces
removed before gating.

**Jake's two scope calls for §5** (asked 2026-07-26; he took the thorough option on both):

1. **Async trait DEFAULT bodies ARE in scope.** A default body synthesizes ONE future struct generic
   over `This` with a per-impl assoc binding, so `AsyncRead` can carry `read_exact`/`read_all` written
   once over `read`, mirroring `io::Read`.
2. **The future's bound is IMPLIED by the trait.** `where S: AsyncRead` alone must typecheck
   `await s.read(3)`; writing `where S::ReadFut: Future<i64>` at each use site was the cheaper option
   and was rejected as verbose and prone to drifting from the trait. **This already works as a
   consequence of the gap fixes above** — `scan_param_bounds` types the call through the trait bound
   and `resolve_await` projects the Output symbolically — verified by a probe carrying only
   `where S: AsyncRead`.

**Known design risk for §5, not yet resolved:** a default-bodied async trait method takes `&this`, so
its future holds the receiver across a suspend. Whether `E0455`'s `frame_addr_root_expr` rejects
`await this.read(n)` inside such a body needs checking before that piece is built.


### 2026-07-26 — Async TRAIT METHODS landed (§5), default bodies included

`E0364` no longer rejects an `async` trait method. It desugars to an implicit associated type plus a
projection return, and each impl binds that type to the future its own `async` method lowers to — so
two implementations return two different concrete future types while the trait still declares one
signature:

```cryo
type trait AsyncRead {
    async read(&this, n: i64) -> i64;          // => type ReadFut;  read(..) -> This::ReadFut
    async read_twice(&this, n: i64) -> i64 {   // DEFAULT body, awaits through `this` twice
        const a: i64 = await this.read(n);
        return await this.read(a);
    }
}
async function pump<S>(s: S) -> i64 where S: AsyncRead { return await s.read(3); }
```

Four pieces:

1. **Parser** — the trait-method `E0364` is lifted and `is_async` kept on the node. The
   `virtual`/`override` rejection STAYS: those genuinely share a vtable slot and each override would
   return its own future. Its negative test still passes.
2. **TypeResolution — `desugar_async_trait_methods`** synthesizes `<Method>Fut` (`read` -> `ReadFut`,
   `read_exact` -> `ReadExactFut`) unless the trait declares it explicitly, and rewrites the method's
   **resolved** return to `This::<Method>Fut`. **The ANNOTATION deliberately keeps the user's
   `-> i64`** — see the trap below. Name-casing goes through explicit alphabet tables, not `c - 32`:
   `CharType` is 4 bytes here while the stdlib treats a char as 1.
3. **`AsyncLower::bind_async_trait_assoc`** — after an impl's `async` method lowers, its future is
   bound to `<Method>Fut` on the enclosing trait impl via the existing `add_assoc_binding`, which
   `resolve_concrete_member` already consults (`impl_node.lookup_assoc_binding`) after the positional
   sugar. `AsyncOwner` gained an `impl_node` field to carry the block down. An explicit hand-written
   binding wins, so `implement trait AsyncRead<MyFut>` stays authoritative.
4. **E0309** — `impl_binds_assoc_via_async` teaches the unbound-associated-type check that this
   binding is implicit. Without it EVERY async trait impl is an error, because E0309 runs in
   TypeResolution and the binding is added by the sema-time lowering long afterwards.

**The trap worth keeping.** The desugaring must NOT rewrite the return ANNOTATION.
`synthesize_default_trait_methods` clones a default-bodied trait method into each impl *carrying its
annotations*, and that clone has to lower as an ordinary `async` method whose future's Output is
`i64`. Rewriting the annotation would make the clone's future's Output be its own future type —
circular. Rewriting only the resolved return keeps callers seeing `This::<Method>Fut` while the impl
side still sees the Output.

**Default bodies cost almost nothing**, which was not obvious up front: because
`synthesize_default_trait_methods` already CLONES them into each impl during TypeResolution, each
clone lowers exactly like an ordinary impl-side async method and binds its own future. No future
generic over `This` was needed — the design risk recorded in the previous entry dissolved. The
related E0455 worry also proved unfounded: an `async` method with a `&this` receiver awaiting THROUGH
`this` twice compiles and runs (probed directly), because the receiver is a parameter pointer the
existing async-method machinery already carries, not a frame address.

**Jake's "implied bound" call needed no work at all.** `where S: AsyncRead` alone types
`await s.read(3)`, as a consequence of the §4 gap fixes: `scan_param_bounds` types the call through
the trait bound and `resolve_await` projects the Output symbolically.

**Tests.** `tests/tests/negative/E0364_async_trait_method.cryo` DELETED — it asserted the rejection
that is now lifted. New `tests/tests/lang/async_trait_method.cryo` with 6 tests: dispatch to the
impl's own future, a DISTINCT future per impl (a shared one would make both answers equal), an
`async` body with no `await` at all, the default body running for the non-overriding impl, override
precedence, and a call on a concrete receiver. Gates: `make test` OVERALL PASS (unit **1759**,
compile-fail 157 — one fewer by the deletion, projects 12); roster regenerated **by merge** with a
golden-only count of **0**; `roster-check: OK (1759 tests)`.

**Not done, and next:** §6 (port every socket consumer onto these traits) and §7 (delete the blocking
surface). The §6 surface is now mapped precisely: `Http2Connection<S>` and `WebSocket<S>` hold
`inner: S*` (must become owned, and their `drop` must then drop it); 8 generic entry points across
`ws/frame.cryo`, `http2/frame.cryo`, `http/request.cryo`, `http/response.cryo`; and six direct
consumers (`http/client`, `http/server`, `http2/client`, `http2/server`, `https`, `ws/conn`).
Re-derived from code hits with doc comments excluded, confirming `net/dns.cryo`, `net/addr/ip.cryo`
and `net/socket/udp.cryo` name `TcpStream` only in prose and need no port.

---

### 2026-07-26 — Receiver-pointer refresh at resume (5d): the soundness hole 5c opened, FIXED

**The question, and why it was blocking.** The handoff asked whether a `&this` receiver held across a
suspend stays valid once the enclosing future moves. It does not, and the repo already said so in three
places that had drifted apart:

- `async_lower.cryo` keeps a by-pointer receiver in a `this$recv` field (`AsyncDecl.recv_ptr`), written
  ONCE at construction — the ctor sets `this$recv = &this` from the callee's own receiver.
- §4 (DECIDED) rests the whole no-`Pin` argument on futures having **no self-references**, hence being
  "freely movable ⇒ no address-stability requirement"; design-review item 5 explicitly blesses polling a
  future by hand and **moving it between calls**.
- Design-review item 6 says the idiomatic `stream.read(&self).await` "**IS** a self-referential future
  (stored read-future field points at the `stream` field) → **E0455** under the ban."

So 5c shipped exactly the shape item 6 says must be rejected. `frame_addr_root_expr` misses it by
design: its `!u.is_synthetic` guard skips compiler-built `&` nodes because those "never outlive the state
that built them" — true for every synthesized `&` EXCEPT an async method's receiver, which now does.

**It is worse than a move hazard, and that is what makes it reachable.** An owning receiver promoted
across states does not live in the frame while it is in use: `promote_cross_state` gives it an
`Option<T>` field and it is **taken into a fresh block-local on entry to each state and handed back on
the way out**. The IR is unambiguous — state 0 does `Option::take` → `unwrap` → `store … ptr %h` and
calls the method with `ptr %h`, an `alloca`. So the receiver's address legitimately CHANGES on every
poll, and the stored `this$recv` is stale from the second poll onward regardless of whether anything
moved the future.

**Why a first probe looked green — recorded so it is not re-learned.** State 0 and the resume state are
both reached from the same `loop.body` dispatch within ONE poll call, and `future::block_on` re-polls in
a tight loop, so poll 2's frame is laid out at the same addresses as poll 1's and the dead `alloca` still
holds the right bytes. The bug reads stale-but-intact memory. A probe only exposes it once something
dirties that stack between polls.

**Jake's call (asked, since §4 and 5c genuinely contradicted): refresh the receiver pointer at resume**
— over (b) making the sugar consuming like `TcpRead`/`TcpIo`, and over (c) adopting an address-stability
contract, which Cryo cannot enforce and §4 already rejected as safety theater.

**The fix.** There is exactly ONE sub-future stash/poll site (`lower_carrier_sm`), so the change is
single-point: after `__sub_k` is materialized and before it is polled, re-address its receiver from this
frame's own storage. `awaited_recv_ptr_type` decides whether there is anything to refresh by testing
that the awaited future has a `this$recv` field whose type is a POINTER — a by-value receiver lands in a
slot of the same name, so pointer-ness is the test, not the name. `awaited_recv_place` pulls the receiver
out of `Call(callee = MemberAccess(object = …))`; the `&` is implicit in the call ABI, so the place
itself is what gets re-addressed. `rebuild_place` makes a FRESH copy (identifier / member / `*p` only):
the original already has a parent in the state that built the sub-future, and only pure reads may be
re-evaluated once per poll. A receiver reached through a pointer already IS the address the callee
stored, so it is passed through instead of addressed twice.

Two shapes cannot be re-addressed and are now rejected instead of corrupting silently, each naming its
rewrite: a receiver that names no storage (temporary or call result → bind it to a local), and awaiting
a method future the awaited expression did not itself produce (`mut f = h.bump(5); await f;` → await the
call directly). Both rewrites were verified to compile and run.

**Proof (a probe that FAILS without the fix, per the handoff's standard).** A hand-written `block_on`
that dirties the stack between polls with a `format`-based recursive churn — polling by hand is
documented-sound, item 5. Four shapes, same source, only the compiler differing:

| Shape | pinned (pre-fix) | fixed |
|---|---|---|
| owning aggregate PARAMETER receiver, 3 suspends | `15` (base read as 0) | `115` |
| owning aggregate LOCAL receiver | `1489753375663` | `115` |
| receiver through a pointer | `115` | `115` |
| nested field chain (`this.inner.bump(n)` in an `async` method) | `140696103232269` | `122` |

`FAILMASK=11` pre-fix, `0` post-fix. The pointer case passing in BOTH is the control: it names stable
caller storage, and the `via_ptr` branch must not double-address it. The emitted IR confirms the
mechanism rather than the outcome only — drive's resume state now contains
`%fieldptr28 = getelementptr %Holder$twice$Future_0, ptr %__sub_0, i32 0, i32 1` /
`store ptr %h21, ptr %fieldptr28` (field 1 is `this$recv`) immediately ahead of the poll.

Note `negative/E0455_async_method_value_receiver_address.cryo`'s comment says the `&this` counterpart is
legal because "the caller's object … does not move". That is now true BY CONSTRUCTION (the frame
re-addresses it) rather than by assumption; left as-is.

Files: `compiler/src/compiler/sema/async_lower.cryo` (4 helpers + the refresh at the single await site);
NEW `tests/tests/lang/async_receiver_refresh.cryo` (4 tests); NEW
`tests/tests/negative/E0455_async_temporary_receiver.cryo`,
`tests/tests/negative/E0455_async_stored_method_future.cryo`.

**Gates (Windows host).** `make test` **OVERALL PASS** — unit **1763** (1759 + 4), compile-fail **159**
(157 + 2), projects **12**, exit 0. Roster regenerated **by merge**, golden-only count **0**, +4 entries
(1759 → 1763), `git diff --numstat` = `4 0` (no line-ending flip); `roster-check: OK (1763 tests)`.
**Repin expectation: NONE.** The compiler contains no `async fn` and — verified this session — neither
does the stdlib (0 hits), so the lowering is inert for both and the `win-s2` vs `win-s3` pair should be
0/235, as it has been for every prior async increment. `make selfhost-check` + the pin-delta measurement
NOT yet run this session (it exceeds the 10-minute tool cap; left for Jake per the handoff).

**Next is unchanged: §6 the socket port, then §7 delete the blocking surface.** One scope correction
found while surveying: **there are no `AsyncRead`/`AsyncWrite` traits in the tree** — the async surface
is concrete futures, so the port's first step is to DESIGN them. The shape is pinned by two settled
constraints: the future must OWN the buffer (§5a), while the transport may now stay in `mut &this`
(§5d), i.e. `async read(mut &this, buf: Array<u8>) -> AsyncIo` handing the buffer back the way
`TcpIo::take_buf()` does. `io::Read`/`io::Write` take `u8*` / `Slice<u8>` and so cannot be mirrored
directly.

- _2026-07-26_ — **5e: the generic-owner half of the receiver refresh (a silent miscompile) — FIXED.**
Baseline HEAD `9ad93caf`, clean, pins a matched pair at `2ec4dedf-dirty`, `verify-pin.py` OK; Jake
confirmed he ran `selfhost-check` green at that HEAD, closing the §7a gap the handoff flagged.

Found while probing the shape §5b-port needs, BEFORE writing any of it. **An `async` method on a
GENERIC struct lost every write made through `mut &this`.** That is precisely the shape every consumer
to port has (`Http2Connection<S>`, `WebSocket<S>`), so the port would have been built on sand.

The axis is the owner being generic, **not** the suspend — which is what makes it a different defect
from 5d rather than a gap in it:

| owner | method | write lands? |
|---|---|---|
| generic | sync | yes |
| concrete | async, no suspend | yes |
| concrete | async + suspend | yes |
| generic | async + suspend | **NO** |
| generic | async, no suspend | **NO** |

**Root cause.** `awaited_recv_ptr_type` answered "does this awaited future store a receiver pointer to
refresh?" from the future's arena struct. A future generic in its owner arrives as an
**`InstantiatedType` (kind 22), not a `Struct` (kind 15)** — specialization fills its fields, and that
runs after sema — so the `t.kind != TypeKind::Struct` guard rejected it, 5d's refresh was skipped, and
the method wrote through a pointer to a block-local the enclosing frame had stopped using. Confirmed by
`cdebug` trace (kind 22 rejected, `inst_generic_base` resolving to the right template) and by the IR:
the caller's poll emits one `store ptr %"<local>", ptr %fieldptr` per awaited method receiver, and the
generic ones had none. Future struct layouts and poll bodies are byte-for-byte equivalent
concrete-vs-generic — the whole difference is at the caller.

**Fix**, both in `async_lower.cryo`: (1) `awaited_recv_ptr_type` falls back to
`arena.inst_generic_base(fi)` and looks for the slot on the TEMPLATE, which `lower` did populate;
(2) the template's slot type still mentions the owner's parameters and so cannot type a node in an
already-concrete caller, so `lower_carrier_sm` now takes the pointer type from the receiver PLACE —
already-a-pointer passes through, otherwise `get_pointer_to(place_ty)`. That also replaced the identity
test `place_ty.id == recv_ptr_ty.id` with a Pointer-kind test, provably equivalent because the concrete
slot type is `get_pointer_to(place_ty)` by construction. `PendingThenReady<i64>` is an
`InstantiatedType` too and must keep being rejected; resolving to its base finds no `this$recv`, so it
still is — a built-in guard against over-firing.

**Why the existing tests missed it:** all four 5d tests only READ through the receiver, and a read
mostly still looks right (the stale copy holds what the receiver held when the future was built). The
3 added tests observe a WRITE from the caller after completion. **Verified the hard way** — reverted
the compiler change, rebuilt, re-ran: 2 of the 3 FAIL (`81` vs `82`, the lost increment), then restored
and rebuilt to green. The pointer-receiver test passes either way; it is a guard on the `via_ptr`
branch, not a discriminator, because a pointer parameter into storage owned outside the async frame is
stable regardless.

Files: `compiler/src/compiler/sema/async_lower.cryo` (`awaited_recv_ptr_type` + the place-derived
pointer type at the single await site); `tests/tests/lang/async_receiver_refresh.cryo` (+3 tests).

Cryo authoring trap paid for here: **`else if` is not valid in expression position** (`const x = if (a)
{..} else if (b) {..}` → "expected expression, found ';'"); use a `mut` plus a statement-level `if`.

**§5b-port design — Jake's call, 2026-07-26: the connection owns a persistent internal buffer.** The
trait primitive fills a buffer the transport-owner already holds (reached through the refreshed
`this`), so no buffer crosses the API and no layer threads an `Array<u8>` in and out; the hand-back
`TcpIo::take_buf()` does is confined to the one place that calls `TcpRead::start`. Rejected: the
caller-supplied `read(buf) -> AsyncIo` mirroring `TcpIo` exactly (a looping `read_exact` then needs a
cursor in the outcome), and a both-ways surface.

```cryo
type trait AsyncRead {
    async fill(mut &this) -> Result<u64, IoError>;                  // fills this.buf
    async read_exact(mut &this, n: u64) -> Result<(), IoError> { }  // default body
}
type struct Conn<S> { inner: S; buf: Array<u8>; }
```

**And the constraint that forces owned transports, now verified by probe rather than inferred:** a
borrowed transport through a generic entry point is a hard error. `async function via_ref<R>(r: mut &R)`
awaited as `await via_ref<Src>(&s)` from an async fn is **`E0455`**; the identical code with a
non-async caller compiles and runs. So the 9 generic entry points (`ws/frame.cryo`,
`http2/frame.cryo`, `http/request.cryo`, `http/response.cryo`) cannot stay free functions taking
`mut &R` once their callers are async, and `Http2Connection<S>` / `WebSocket<S>` must own `inner: S`
rather than borrow `inner: S*` — which also retires `Http2Connection::drop`'s "the borrowed transport
belongs to the caller" comment. E0455 names one escape hatch ("have the caller own the storage and pass
a pointer parameter"), but it needs the transport owned outside every async frame, which a
task-per-connection server cannot do.

### 2026-07-26 — `?` inside `async` did not work AT ALL; three compiler defects fixed (unblocks §4/§5)

**The finding, and why it stopped the port before it started.** The §4 trait shape was written out as a
probe — an `AsyncRead` trait with two sync required methods, one `async` required primitive, and async
default bodies over them — and it did not compile. Minimizing produced something much larger than a
probe bug: **the `?` operator was rejected in EVERY `async` function.** The smallest form is

```cryo
async function outer(n: i64) -> Result<i64, IoError> {
    const a: i64 = plain(n)?;          // error[E0235]: the `?` operator can only be
    return Result::Ok(a);              // used in a function that returns `Result`
}
```

with the identical body in a NON-async function compiling. It is not a regression: the pinned compiler
rejects it identically. It had simply never been exercised — **no test under `tests/tests/lang/` used
`?` in an `async` function**, and `stdlib/` had no `async` code to catch it. Since the §5 consumers are
~3,300 lines of `?`-dense code, this was a hard blocker on the whole port, and working around it (an
explicit `match` per fallible call) is exactly the "cheap workaround" §0 forbids.

**Root cause 1 — the shape gate judged the WRONG function.** `cdebug` on `resolve_try_expr` showed
`state.return_type` at the rejected `?` was `Poll<Result<i64, IoError>>`. Sema walks each module TWICE:
pass 1 types the async function against its declared Output (the `?` resolves fine there), then `lower`
moves those very statements into the generated `poll`; pass 2 walks that `poll`, whose return is
`Poll<Output>`, and re-judges the same `TryExprNode` against it. So a `?` that was correct where it was
written is rejected by the mechanical transform of the function containing it.

Fixed by giving sema the source function's Output while it is inside a lowered `poll`:
`SemaState.async_poll_output`, set in `enter_function` from `async_poll_output_of(func)`. That reads the
Output off **`Poll::Ready`'s payload**, not the return type's generic argument — after monomorphization
`Poll<Output>` is a plain enum with no instantiation left to peel — and it identifies a lowered `poll` by
the three marks `declare` puts on it (`is_synthesized_body`, `origin_trait == Future`, name `poll`),
never by shape, so a hand-written `-> Poll<Result<..>>` keeps the ordinary rule. Saved/restored around a
lambda body, which returns for itself.

**Root cause 2 — a `?` node was invisible to nine of the lowering's ten expression walkers.** Exactly the
blind-spot class §8 of the handoff warns about, and `TryExpression` was handled in `rn_expr` only.
Consequences, each real: `expr_await_count` did not see an `await` in a `?` operand, so `(await f())?`
sent the function down the **no-await path** and codegen met an `await` with no state to resume;
`rewrite_returns_expr` never reached the `return` the desugar carries in its `Err` arm, so it stayed
un-wrapped in a body where every return must be `Poll::Ready(...)`; and `rn_expr` walked BOTH `operand`
and `desugared`, alpha-renaming the same subtree twice, because the desugared `match` takes the operand
node **itself** as its subject.

Fixed with one helper, `try_live`, and a `TryExpression` arm in each of the nine walkers, following the
idiom every other pass already uses (`move_check`, `drop_insertion` and codegen all read `desugared` and
never `operand`). `hoist_expr` hoists INTO the node rather than replacing it with its desugar — a bare
`match` left in its place has to re-derive its type from its arms after a specialization clone has
dropped every `resolved_type`, and an arm that fails to re-type yields the SUBJECT's type, i.e. the
un-unwrapped `Result`. That was observed, not theorised: it is what the first attempt did.

**Root cause 3 — the cloner SHARED a `PatternBinding` across a trait default body and its impl clones.**
Found while proving the trait shape: a `match` arm binding read after an `await` inside an `async` trait
DEFAULT body failed with `E0201 cannot find value 'v'`, in the pinned compiler too. `ASTCloner`
deliberately shared `PatternElement::Binding` pointers as "immutable parse metadata" — a premise the
async lowering falsifies, because `rn_pat_apply` alpha-renames arm bindings **in place** (`pb.name = …`).
So lowering any one copy renamed the binding for every copy, while each copy's arm BODY was renamed only
in its own — the pattern bound `v$L2`, the body still read `v`. The rule this produced is exact and
matches every observation: an async trait default body + any `await` + any NAMED arm binding fails, in
whatever order, and a wildcard arm is fine. `Binding` is now deep-cloned like `Sub`; `Wildcard`/`Literal*`
introduce no name and stay shared.

**A fourth defect, found by the fix and fixed with it: a desugar built AFTER lowering.** In a GENERIC
body the `?` cannot be shaped during the symbolic walk — awaiting a trait method yields
`S::Fut::Output`, which is genuinely abstract, so `resolve_try_expr` bails before desugaring. The desugar
is then first built inside the already-lowered `poll`, where nothing will come back to wrap its `return`.
It now wraps its own: `build_try_desugar` emits `Poll::Ready(...)` when `async_poll_output` is valid.

**And a fifth, which the first attempt exposed: cloning split an invariant two consumers depend on.**
Reusing an existing desugar instead of rebuilding it broke `stdlib/collections/hashmap.cryo`'s
`alloc_entry(this, h, key, value)?` with `E0636 cannot resolve function` at codegen. Call specialization
and dispatch annotation walk `te.operand`; codegen emits `te.desugared`. Those are the same node —
the desugared `match`'s subject IS the operand — but `ASTCloner` cloned the two halves independently, so
a specialization emitted the copy nobody had specialized. Sema had been hiding it by rebuilding the
desugar from the operand on every visit. The cloner now re-points the clone's subject at the clone's
operand when the original still holds the sharing (the async lowering deliberately breaks it when it
hoists an `await` out of the subject). `resolve_try_expr` also resolves the operand on EVERY visit,
including one that reuses a desugar, for the same reason.

**Validation.** `tests/tests/lang/async_try_operator.cryo`, 10 tests, all of which fail without these
changes: `?` with no await, `?` in a state machine, `await` INSIDE the `?` operand, `?` in a loop, `Err`
propagation out of a lowered `poll` (both flavours), `Option`-flavoured `?`, `?` in a trait default body,
a `match` arm binding after an `await` in a default body, and `?` in an `async` method on a GENERIC owner
(value and error paths, checking that the post-`?` statement did not run). Roster regenerated **by merge**
— golden-only count 0, `git diff --numstat` = `10 0` — 1766 → 1776.

**What this unblocks.** The §4 design shape now compiles AND runs: an `AsyncRead` trait with sync
`buffered`/`consume` plus an `async fill`, async default bodies (`ensure`, `take_u16`) using `?`, loops
and match bindings over them, and a `Codec<S>` protocol layer that OWNS its transport, mutates through
`mut &this` across suspends, and propagates EOF as an `Err` through the whole `?` chain. §4 can be
written against it.

---

### 2026-07-26 — `AsyncRead` / `AsyncWrite` LANDED in `stdlib/` (§4); a pre-existing turbofish miscompile found and fixed

**§4 is done.** `stdlib/io/async_traits.cryo` (registered in `io/_module.cryo`), built to Jake's settled
shape: the transport-owning connection keeps ONE persistent buffer and no buffer crosses the API.

**`AsyncRead`** requires three primitives — `async fill(mut &this) -> Result<u64, IoError>` (pull more
bytes; 0 = EOF), sync `buffered(&this) -> Slice<u8>`, sync `consume(mut &this, count)`. Everything else is
a default body: `ensure` (leaves the bytes in the buffer, so a fixed-size header is parsed in place with no
copy), `read_exact`, `read_some`, `read_until`, `read_line`, `skip`.

**The discipline that makes it sound, stated in the trait's own doc comment:** a `buffered()` slice must
never be held across an `await`, because a `fill` can grow the buffer and move its heap block. The default
bodies do not merely avoid it by inspection — the scan and the copy are factored into SYNC helpers
(`scan_for`, `take_front`) so no slice is even in scope at a suspension point. The one place resumption
state is carried across a fill is an integer offset (`read_until`'s `scanned`), which is why the trait
documents that a successful `fill` only APPENDS to what `buffered()` reports: that is what lets the scan
resume instead of rescanning, and it stays true under an implementor that compacts its storage.

**`AsyncWrite`** inverts the split deliberately: sync `pending(mut &this) -> Array<u8>*` and a generic sync
`queue<T>` (`static match` over `Slice<u8>` / `Str` / `string` / `u8`), plus one `async flush`. An encoder
therefore builds an entire frame — header, length prefix, payload — with zero suspension points, and the
layer suspends once. `pending()` is exposed rather than wrapped so a frame can be built directly in the
outgoing buffer instead of into a scratch array that is then copied.

**Tests: 10**, `tests/tests/stdlib/io_async_traits.cryo`, against in-memory doubles (no reactor, so they run
under `future::block_on`). The doubles' fill chunk is 1–4 bytes, so every read spans SEVERAL fills — which
is what grows the buffer and moves its block, the condition a stale slice would need to go wrong. The
source also counts fills, so an over-reading default body is visible as a count rather than only as a wrong
answer. Coverage includes a `MockLayer<S>` that owns its transport, parses a header in place and
accumulates through `mut &this` across suspends (the §5e generic-owner receiver-refresh shape).

#### The compiler bug this uncovered — an explicit method turbofish was ignored for a literal argument

Writing the write side's `queue<u8>(0x32)` produced an **access violation**. Isolating it took the full
bisect, and the answer was not async at all.

`recv.m<u8>(0x42)` — explicit turbofish, **bare integer literal** argument — bound to a **sibling
specialization of the same method** and passed the literal in THAT specialization's parameter shape. The
IR is unambiguous: four call sites in source order `Str`, `u8`, `string`, `Slice<u8>` emitted calls to
`Str`, **`Slice<u8>`**, `string`, `Slice<u8>`, with the second passing
`ptr byval(%Slice) inttoptr (i32 66 to ptr)` — the number 66 handed to the callee as a pointer, which then
reads a `Slice` header out of address 66. Silent: no diagnostic, and a **typed local worked**, so only a
literal argument exposed it.

**It is pre-existing.** The same minimal repro miscompiles identically under the PINNED compiler, so it
predates every line of the async work. It had simply never been hit: the one stdlib method of this shape
(`io::Write::write<T>`) is never called with a bare literal, and a repro needs a sibling `Slice<u8>`
instantiation in the same program for the mis-dispatch to have somewhere to land.

**Cause.** `substitute_explicit_generic_param_types` (`sema/call_resolver.cryo`) makes a call's explicit
type args concrete in the callee's formals so expected-type propagation can push a concrete type onto each
argument. It handled only the FREE-function form: turbofish on the call node, callee an identifier. A
method call carries its turbofish on the **member access**, so the function returned immediately, the
formal `data: T` stayed abstract, and — since a bare integer literal only receives an expected type in a
few special cases — the literal fell back to its `i32` default. For a non-generic callee that is harmless
(the call boundary has an integer width coercion, which is exactly why the existing code comments say the
integer path "survives the same clearing"). For a **generic** callee it is fatal, because the argument's
type is what SELECTS the specialization.

**Fix, in two halves — both needed, the first alone changes nothing.**
1. `substitute_explicit_method_param_types`: locate the method via the existing
   `MethodBinding::find_generic_method_for_call`, resolve the turbofish in the CALLER's scope (an owner
   context would shadow an enclosing param sharing the receiver's param name — the same choice
   `solve_method_bindings` documents), then **re-resolve each parameter annotation** with the owner args
   and the method's own params bound. Re-resolving rather than substituting into the already-resolved
   types handles a param nested inside another type (`Array<T>`, `Option<T>`) for free.
2. Force the expected type onto exactly those arguments whose formal came from a turbofish. The
   literal-clearing default in `resolve_call` is deliberate and stays for everything else; a
   `forced_expected` flag marks the substituted indices, and the free-function path sets it too — the same
   latent hole existed there.

**Tests: 5**, `tests/tests/lang/method_generic_turbofish_literal.cryo`. Each passes a bare literal and
checks WHICH `static match` arm ran, with sibling instantiations kept live in the same program so a
mis-dispatch has somewhere to go; trait-impl and inherent methods are covered separately (different
specialization paths), plus the by-value payload path where the corruption was originally observed.
Pre-fix they fail with a wrong VALUE (`71` where `70` is correct) rather than a crash, which is the
honest characterisation of the bug.

**Relevance to §5.** This is squarely on the port's path: an HTTP/1 writer queuing `0x0D` / `0x0A`, or a
WebSocket encoder queuing an opcode byte, is exactly `queue<u8>(<literal>)`.

---

### 2026-07-26 — `TcpConn`: the concrete transport for the port (§5 foundation)

`stdlib/net/socket/conn.cryo`, registered in `net/socket/_module.cryo`. `TcpConn` owns a `TcpStream` and
implements both `AsyncRead` and `AsyncWrite`, so it is the concrete thing every protocol layer will be
generic over (`Conn<S> where S: AsyncRead + AsyncWrite`).

**What it exists to absorb.** `TcpRead::start` / `TcpWrite::start` take the socket AND the buffer BY VALUE
and hand both back in a `TcpIo`, because a future is moved between polls and cannot hold a pointer into
anyone's frame. Sound, but every single operation is a move-out / await / move-back. `TcpConn` writes that
dance once, with `mem::swap` against placeholders, and every exit path — including both error paths —
restores the socket before returning.

**Two decisions worth keeping.**
- **Compaction happens at the START of a fill, never at the end.** The read buffer keeps an `rpos` cursor
  so `consume` is O(1); the consumed prefix is dropped on the next fill. Doing it after a read would shift
  `buffered()` underneath a caller part-way through parsing a record, which is precisely what the trait's
  "a fill only APPENDS to what `buffered()` reports" contract forbids.
- **A failed flush leaves the unsent bytes queued**, so a caller that can retry still has them; a write
  that moves 0 bytes is reported as `WriteZero` rather than spun on.

**Cancellation semantics, stated because it is a real consequence rather than an oversight:** while an
operation is in flight the connection holds a closed placeholder and the future owns the real socket, so
dropping the future closes the connection. That is coherent — nothing else holds the socket, and a
half-consumed read cannot be resumed against a peer that has already sent the bytes — but it means a
cancelled read cancels the connection, and a caller wanting to keep the connection must not cancel it.

**Tests: 2**, `tests/tests/stdlib/net_tcp_conn.cryo`, real loopback sockets on a real `Executor`. A framed
round trip (CRLF header line + fixed-length body, both read through ONE buffer) and a truncated record
surfacing as `UnexpectedEof`. The framed shape is deliberate: reading a header line and then the body out
of the same buffer is exactly what the blocking `http::request::read_line` cannot do — its own doc comment
records that limitation — so it is the behaviour the port depends on.

#### RESOLVED 2026-07-27 — the "flood" failure was the carrier bug, NOT `Join`

A "flood" scenario (N length-prefixed records written without reading, receiver reading them one at a time,
then an ack in the reverse direction) panicked with **`called Option::unwrap() on a None value`**. It was
written, failed, and was removed rather than left red.

**The diagnosis recorded here was wrong.** It read:

> The panic can only be `combinator.cryo:117-118` (`Join`'s `a_out.take().unwrap()` /
> `b_out.take().unwrap()`). Every other `unwrap()` reachable from this path is either guarded […]

`Join` had nothing to do with it. The same panic reproduces in 40 lines with **no `Join`, no `Executor`
and no socket** — an `async fn` that awaits a method on an owning local inside a `while` loop. What was
right in the old entry is that it is not volume and not staggered completion; what was wrong is the
conclusion, reached by elimination over the `unwrap()` call sites that were *visible in source*. The
failing `unwrap` was in code the compiler had SYNTHESIZED, so it was not in that list at all. A gdb
backtrace names the frame in seconds and would have settled it immediately.

Root cause and fix: see the 2026-07-27 entry "Carried aggregates across a suspend inside a loop" in §9.

The flood scenario is now a landed test — `tcp_conn_flood_of_records_read_one_at_a_time` in
`tests/tests/stdlib/net_tcp_conn.cryo`, 16 framed records over a real `Executor`. It fails on the
pre-fix compiler with exactly the original message and passes on the fixed one, which is what confirms
the two failures were the same bug.

---

### Two generic-owner resolution defects fixed (blocking the no-mirror connection design)

Jake ruled that the stdlib async port must **never build a mirror**: no `TlsConn` twinning `TcpConn`, and
no async path added alongside a blocking one to be demolished later. Each layer converts in place and its
tests move with it. That makes the buffered connection GENERIC in its transport, which in turn requires it
to IMPLEMENT `AsyncRead`/`AsyncWrite` (protocol layers are generic over `S: AsyncRead`) rather than merely
carry inherent methods. Two defects blocked exactly that, both now fixed.

**1. A generic owner's params never reached the future of an `async` method in a trait-impl block.**
`sema.cryo`'s `ImplementationBlock` arm passed `ib.generic_params` to `declare_async_methods`. For
`implement trait Sink for struct Buf<S>` the parameters are written on the TARGET, not on the block, so
that list is empty — the lowered future was declared non-generic, its own impl block was then walked
CONCRETELY, and the still-abstract `S::PullFut::Output` in its `poll` body failed every check instead of
deferring to monomorphization (`E0200`, `expected Transfer, found S::PullFut::Output`). The identical body
as an INHERENT method was always fine, which is what made it look like a projection bug rather than a
declaration one. Fixed with `impl_owner_params`, which falls back to the target template's own parameters
— the same shape as the existing bare-impl-on-template-target rescue a few lines below it.

**2. `abstract_receiver_method_return` had no `InstantiatedType` arm.** During a symbolic walk a receiver
typed `H<S>` (a generic owner instantiated at a still-abstract argument, e.g. built inside a generic
function) is "unresolved" only in its ARGUMENTS; its base is a known template whose method list types the
call exactly. With no arm for it the symbolic path returned invalid, the call typed as nothing, and an
enclosing `await` reported `E0306` "operand must implement `Future`". A non-async method on the same
receiver resolved fine, which is why this surfaced only through `await`. Fixed by routing that case to the
existing `resolve_method_return_via_template`, which deliberately leaves the return abstract while the
receiver's args are abstract — precisely what a symbolic walk wants.

Both verified by observing VALUES, not exit codes: the generic-owner class is a silent-miscompile class,
so each probe reports writes made through `mut &this` at both levels and is checked against an expected
number. Guarded permanently by two tests in `tests/tests/lang/async_trait_method.cryo`
(`async_trait_method_on_a_generic_owner_normalizes_the_projection`,
`async_trait_impl_on_a_generic_owner_satisfies_a_second_bound`) — that file previously covered generic
FUNCTIONS only, never a trait impl on a generic OWNER.

Unit suite green in full (882 `Tests::Lang` + 915 `Tests::Stdlib` = 1797, the merged roster); roster
merged, not `--update`d, golden-only count 0. Compiler changed, so the pin delta is UNMEASURED — that
needs `selfhost-check` plus the §2 file-count/`@FILE.str` normalization before any repin.

**Still open (unchanged by this work):** a droppable binding moved inside a `?` in an earlier state than
its drop lands in is rejected `E0452`. Each ingredient passes alone; `await` placed BEFORE the `?` is
accepted; the no-`?` variant is correct at runtime (verified with a drop counter), so it is a real defect
and not a correct refusal. `match` expresses the same thing, which is what `TcpConn::connect` already does.

### `?` that gives a droppable binding away before a suspend — fixed

The last open defect from that session. A droppable binding moved inside a `?` in an EARLIER state than
the one its drop lands in was rejected `E0452`, pointing at the `async` keyword:

```cryo
async function combo(r: Res) -> Result<i64, i64> {
    const seed: i64 = (try_consume(r))?;   // gives `r` away
    const w: i64 = await nothing();        // suspend AFTER the move
    return Result::Ok(seed + w);
}
```

**Root cause: `mark_last_use_expr` had no `TryExpression` arm** — one more member of the `try_live` family.
The lowering carries an aggregate parameter in an `Option` field, each state taking it out on entry and
handing it back on the way out; a state that gave the value away must stay silent instead, which
`needs_handback` decides from `last_use_consumes` → `mark_last_use_expr`. For `expr?` the real expression
lives in `desugared`, not on the `?` node, so the walk fell through to its catch-all — which calls
`name_read_in_expr` (that one DOES walk the desugar). So the mention was seen, but classified as a BORROW
rather than a give-away. The state then appended a hand-back reading a moved-from binding: rejected as a
use-after-move, and had it compiled it would have dropped the value a second time.

Fixed by forwarding to `try_live(e)` with `by_value` preserved. The desugar's subject is the operand, so
the existing `MatchExpression` arm classifies it exactly as it would without the `?`.

This also explains the shape of the symptom set: `?` with no suspend never split into states, and a suspend
placed BEFORE the `?` left the move in the same state as the drop — only a move landing in an earlier state
than its drop reached the faulty hand-back.

**Audited the rest of the family** rather than assuming this was the last one. Five other `NodeKind`
dispatchers in `async_lower.cryo` have no `TryExpression` arm and each is correct as written:
`expr_diverges` (only all-arms-diverge exhaustive matches; a `?` desugar's Ok arm yields a value, so
`false` either way), `expr_first_use` (catch-all reaches `name_read_in_expr` and reports a READ, the right
carry-in answer), `awaited_recv_place` (requires exactly a `CallExpression`, else null), `await_transparent`
(defaults `false`, the conservative direction), and `top_level_assignment_index` /
`assigned_frame_addr_root` (scan for bare `name = expr` statements; the latter delegates to
`frame_addr_root_expr`, which already handles `?`).

Guarded by `async_try_operator_moves_a_droppable_param_before_a_suspend`, which asserts the VALUE and a
drop COUNT of exactly 1 — a wrong fix here double-drops silently rather than erroring.

Whole `make test` gate green, run in pieces: unit 1798 (883 `Tests::Lang` + 915 `Tests::Stdlib`),
compile-fail 159, projects 12/12 Windows-eligible. `selfhost-check` and the pin-delta measurement are still
outstanding — the compiler changed in three places, so the delta is NOT known to be zero.

---

### 2026-07-27 — §5b-port increment 1: ONE generic buffered connection; the `async_traits` module dissolved

**Jake's ruling this session, which is stronger than the "no mirrors" rule the handoff carried:** the
no-mirrors rule applies to MODULE STRUCTURE, not only to implementations. *"The IO shouldn't have `traits`
and `async_traits`, it should just be one `traits` module. Only if it really makes sense to have two
implementations like `Read` and `AsyncRead` should be fine but should just be in the same `traits` module,
but only if it's not just two implementations with almost identical code."* So `io::async_traits` and the
freshly-written `io::buf_conn` were both DISSOLVED into the modules that already existed:

- `AsyncRead` / `AsyncWrite` / `AsyncTransport` / `Transfer` now live in **`io::traits`**, under a section
  banner stating why they are not expressible as `Read`/`Write`: those take a caller-supplied `u8*` /
  `Slice<u8>`, and neither a pointer into the caller's frame nor a borrowed receiver survives a suspension
  point. `BYTE_LF` was already declared there for the blocking `read_line`, so the async half reuses it and
  contributes only `BYTE_CR`.
- **`BufStream<S>` now lives in `io::buf`**, beside `BufReader<R>` / `BufWriter<W>` / `LineWriter<W>`. Same
  idea (coalesce many small transport operations into few large ones), one forced difference: those BORROW
  the inner reader/writer by pointer, and that borrow is exactly what `E0455` rejects across a suspend, so
  `BufStream` OWNS its transport.

`stdlib/io/` is therefore 152 modules where it was 154, and there is no parallel `async` module tree. No
cycle risk: nothing under `future/` imports `io::`.

**The design (§4 of the handoff), landed:**

```cryo
type struct Transfer { buf: Array<u8>; result: Result<u64, IoError>; /* take_buf / outcome */ }
type trait AsyncTransport {
    async read_into(mut &this, buf: Array<u8>) -> Transfer;
    async write_from(mut &this, buf: Array<u8>) -> Transfer;
}
type struct BufStream<S> { inner: S; rbuf: Array<u8>; rpos: u64; scratch: Array<u8>; wbuf: Array<u8>; }
implement trait AsyncRead  for struct BufStream<S> { async fill(...)  where S: AsyncTransport { … } }
implement trait AsyncWrite for struct BufStream<S> { async flush(...) where S: AsyncTransport { … } }
```

`TcpStream` and `TlsStream` each implement `AsyncTransport`, and **each does the move-out/await/move-back
swap inside its own impl** — `TcpStream` knows its closed placeholder is `from_fd(-1)`, `TlsStream` knows
its is `from_parts(null, TcpStream::from_fd(-1))`. The buffered connection therefore never holds a
placeholder and the trait needs no `static closed() -> This`. The bound sits on the METHOD, not the impl
block (a `where` on an `implement trait … for` block does not exist). `compact()` cannot be `private` —
the trait impl blocks are separate scopes (E0353).

**`stdlib/net/socket/conn.cryo` (`TcpConn`) is DELETED**, not deprecated: it is `BufStream<TcpStream>` now,
and a TLS connection is `BufStream<TlsStream>` rather than the `TlsConn` twin the older plan called for.
`TcpConn::connect` is gone with it; a caller composes `TcpConnect::start` + `BufStream<TcpStream>::of`, which
is two clear lines and no new API surface.

**A COMPILER BUG blocked the whole design and had to be fixed first.** A trait's `async` DEFAULT BODY could
not await the trait's own required method when the impl was on a GENERIC owner — `E0306` at every
`await this.fill()` / `await this.flush()`, i.e. at all eight stdlib default bodies (`ensure`, `read_exact`,
`read_some`, `read_until`, `read_line`, `skip`, `send`), which is precisely the `BufStream<S>` shape.

- **Cause:** `resolve_method_call` (`call_resolver.cryo`) branches on
  `symbolic_is_generic_owner_receiver` FIRST, and that branch resolves the return through
  `symbolic_find_owner_method` (`symbolic_checker.cryo`), which walks only the owner TEMPLATE's own method
  list — its doc even says *"Trait-impl/inherited methods are intentionally not resolved here"*. A method
  delivered by `implement trait AsyncRead for struct BufStream<S>` is not in that list, so the branch returned
  invalid, and its early return made every later rescue unreachable.
- **Why it reads as async-only:** a SYNC call in the same position is survivable — monomorphization retypes
  it once `S` is bound. An awaited one is not: `lower_carrier_sm` needs the sub-future's type at sema time
  to give it a carrier field, so an unresolved receiver method surfaces at the `await` as "operand does not
  implement `Future`".
- **Controls run before touching the compiler** (the handoff's rule that a green probe proves nothing):
  the identical body on a NON-generic owner passes, and the identical body made SYNCHRONOUS on the generic
  owner passes. Both isolate the axis to generic-owner + await.
- **Fix:** fall back to `MethodBinding::resolve_method_return_via_template` when the owner-template lookup
  finds nothing. That function already reads the declaration index AND `lookup_method_through_trait_impls`,
  so this reuses the existing shared resolver rather than adding a special case — one edit, at the single
  method-call resolution point.
- **Guard test:** `async_trait_method.cryo` gains `AtmDrain::drain_twice` (a default body) plus
  `async_trait_default_body_awaits_a_required_method_on_a_generic_owner`, which reports the accumulated
  `seen` field as well as the value, because a write lost through `mut &this` on a generic owner is silent.
  Suite 8 → 9.

**Validation.** A scratch probe of the whole shape returns `834001085`, the exact predicted checksum
covering the line read, the `read_exact` bytes, the fill count, the write-call count and the bytes on the
wire — every level's `mut &this` writes observed from the caller after the awaits, per the
silent-miscompile discipline. Permanent coverage: `IoAsyncTraits` 10/10 (default bodies, now over the
merged module), `NetTcpConn` 2/2 (rewritten onto `BufStream<TcpStream>`), and a NEW `NetTlsConn` 1/1 —
`BufStream<TlsStream>`, real handshake plus a CRLF-framed round trip with both sides as tasks on ONE
`Executor`, deliberately the same framing as the TCP test so a divergence between the two transports shows
up as one failing while its twin passes. That closes the TLS coverage gap the handoff flagged.

**Two-phase repin was REQUIRED and is done.** The new stdlib cannot compile under a pin lacking the
compiler fix, and `make stdlib` builds via `bin/cryo`. So: stdlib reverted to HEAD → `make pin` (both OS,
plain, from the Linux host with the mingw cross-toolchain) → stdlib work restored → rebuild. The phase-1
pin was taken at exactly the state the full gate had just passed at. `verify-pin: OK`, matched pair
(`75d915ce-dirty` on both), and both sha256s changed (`0c0f3c11…` / `655021aa…`). **A FINAL repin is still
owed** once §5 settles, because the compiler binary links the stdlib archive.

**Gate at increment 1:** full `make test` OVERALL PASS before the module merge — 1798 unit / 159
compile-fail / 14 projects on Linux (1797 baseline + the new guard test; `vendor_raylib` skips). Re-run
after the merge. `selfhost-check` and the pin-delta measurement are still owed.

**Not started:** §5 proper — the six direct consumers, the two borrowed-transport holders
(`Http2Connection<S>` / `WebSocket<S>`, both of which must become owning), the nine by-reference generic
entry points, and the deletion of the blocking socket/TLS surface.

---

### 2026-07-27 — Carried aggregates across a suspend inside a loop: two soundness bugs, both fixed at the root

Baseline HEAD `40c31609`, pin current and a matched pair at `2ab5347e` (clean), tree otherwise clean.
Both fixes are in `compiler/src/compiler/sema/async_lower.cryo`. No stdlib change, so no two-phase repin.

#### Bug 1 — a state's own hand-back store was mistaken for the user giving the value away

`await c.m()`, where `c` is an owning local and the `await` is inside a loop, panicked with `called
Option::unwrap() on a None value` from the AGGREGATE carrier. One iteration was enough; the same three
awaits written straight-line were fine.

An aggregate held across a suspend lives in an `Option<T>` field: taken on entry to a state, handed back
as the state falls off. Two things decide whether the hand-back is emitted — `needs_handback`, which asks
whether the state still owns the value at its tail, and `store_before_suspends`, which inserts an extra
hand-back before each `return Poll::Pending` because a suspend leaves the state before its tail runs.

`promote_cross_state` ran them in that order: insert first, ask second. The inserted store is
`this.__agg_c = Option::Some(c);`, which passes `c` to `Option::Some` **by value**, so the "did this state
give the value away?" walk found the compiler's own store as the last mention of `c` and answered yes.
The tail hand-back was then skipped, `c` fell out of scope and was dropped, and the next state's
`take().unwrap()` met a `None`.

The declaring state already guards exactly this hazard — its `needs_handback` is read before its own store
is appended, with a comment saying why. The guard was simply missing on the other two paths. Both now read
the answer before any synthesized store exists: `promote_cross_state` for promoted locals, and
`carry_params` for aggregate parameters, which had the identical ordering.

Why the straight-line control passed: there, the block after the suspend goes on to build the NEXT
`c.bump()`, and a receiver is a borrow, so the last mention re-marked `c` as still owned. In a loop the
next `await` lives in a different state, so nothing after the suspend mentions `c` at all.

**Tell, for next time:** the failing `unwrap` was in synthesized code, so eliminating over the `unwrap()`
call sites visible in source — which is how the `Join` misdiagnosis above was reached — cannot find it.
`gdb -q -batch -ex run -ex bt` named the frame immediately, and `--emit-llvm` on a 40-line probe showed
the generated `poll` with the store missing from the fall-through path and a `Counter::drop` in its place.
`--ast` was useless (post-lowering tree, identifier names blank), as recorded.

#### Bug 2 — an `async fn` with NO awaits copied an owning parameter out of its field

A body with no suspension point runs to completion in one poll and read its parameters with the shadow
prelude's `const p = this.p;`. For a value with a destructor that is a COPY, not a move: the future's
field and the poll frame each own it, and it is destroyed twice. Broader than first reported — it needs
neither a move-out nor a return, a read-only owning parameter double-frees just the same.

The state-machine path never had this: `carry_params` gives such a parameter an `Option<T>` slot and
`take`s it out. The no-await path skipped that for one incidental reason — the carrier is built out of
`Option`, and `Option` was only looked up when the body contained an `await`.

Fixed by making the model uniform rather than by special-casing the no-await path: `Option` is now looked
up for every `async fn`, so a parameter with a destructor gets a carrier slot whether or not the body
suspends, and the shadow prelude emits `mut p = this.p.take().unwrap();` for it. Field types, the
constructor's `Option::Some` wrap and the prelude all consult `param_is_carried`, so they stay in
agreement. The `Option`-not-found error now fires when a carrier is actually needed, not only on `await`.

#### Coverage — the hole, not just the bug

The reason both survived is that the async tests exercised *async* rather than *code that happens to
suspend*: nothing looped over a connection (`read_line` was called once per connection), and nothing
awaited a method on an owning local inside a loop at all.

**New: `tests/tests/stdlib/async_stress_shapes.cryo`, 31 tests.** The cross product of what is awaited
(free fn, method on a local, on `this`, on a struct FIELD, trait required method, trait DEFAULT body,
generic owner, three-deep nesting), what is carried (scalar, non-droppable aggregate, droppable
aggregate, `Option`-wrapped, generic owner, the awaited call's own receiver, a value given away and
retaken) and the control flow around it (straight-line, `if`/`else`, `while`, `loop`+`break`, `for`,
`match` arm, nested loops, early `return`, `continue`, `?`). Every test asserts a NUMBER, every ownership
test COUNTS destructor calls, and every shape is paired with a control one step simpler — no loop, no
await, or a non-generic owner — so a red cell names the variable responsible.

**New: `tcp_conn_flood_of_records_read_one_at_a_time`** in `net_tcp_conn.cryo` — the flood scenario that
was removed as unexplained (see the correction above), now landed.

Verified red-before / green-after by building the same sources with `bin/cryo` (the pre-fix pin) and with
the rebuilt compiler: the flood test panics with the original message on the pin and passes after.

#### A pre-existing defect found while writing the tests, NOT async and NOT fixed

Calling `.drop()` explicitly on a FIELD of a live owner that still has drop glue runs the destructor
twice — the owner's glue drops the field again. It behaves identically in SYNC code, so it is the known
partial-move tracking limit rather than anything to do with suspension, and is out of scope here. It is
worth knowing because it looks exactly like an async carrier bug when it happens inside an `async fn`.

---

### 2026-07-27 (later) — Two codegen-visible defects closed: ZST enum payloads, and the unit/non-unit `block_on` leak

Both were open items carried by every recent handoff; both are fixed at the root and test-guarded.
Neither is in `async_lower.cryo` — they sit under it, which is why the async work kept tripping on them.

#### Zero-sized payload → wrong enum layout (the SILENT one)

`size_bytes() == 0` was doing double duty: "layout not computed yet" and "computed, and genuinely
empty". `compute_enum_layout` deferred on the first meaning, so any enum with a legitimately zero-sized
payload variant deferred forever and kept size 0. One level of nesting hid it — `TypeMapper::map_enum`
recomputes from the leaf field types, so `Result<i64, Empty>` itself lowered correctly — but the outer
`Poll<Result<i64, Empty>>` read the inner enum's uncomputed 0 as a real width and emitted `[0 x i8]`.
Compiles clean, returns garbage.

`layout_settled` already existed for this hazard, with a doc comment describing it, but was consulted
only by struct lowering and only knew Struct and Class. It now answers for every layout-bearing kind
off the `computed_align` sentinel that each `compute_*_layout` already guards itself with, and
`compute_enum_layout` asks it instead of testing the size. Widening it is the point: a kind left off
that list is reported pending forever, so its container never gets a layout, and anything nesting the
container is then sized from a 0 — silently.

#### Sibling-specialization leak on a unit return (the LOUD one)

`ex.block_on(unit_task()); ex.block_on(int_task());` failed to build; the same two lines in the other
order compiled, and either alone compiled. The callee mangling was right in both orders, so mono was
fine — sema was putting the sibling's `i64` on the unit call's node. `call_resolver` already knows this
leak class (its comment names `next_range<u64>` reading `next_range<i64>`'s `i64`) and re-derives the
type per call, but only ADOPTS the re-derivation when the result is canonical, and the accepted set
omitted unit. So the right answer was computed and discarded. `generic_scalar_return` →
`generic_canonical_return`, now accepting Unit/Void/Never; compound returns stay excluded for the
original arena-TypeID reason.

#### Tests

`async_stress_shapes.cryo` 31 → 35 (ZST payload, sized control, ZST as the whole Output, sync control);
`future_executor.cryo` 2 → 5 (all three orderings — unit-first, non-unit-first, unit alone — because
only one of them ever failed and a single test would not have caught it).

Red-before confirmed: the pinned pre-fix compiler cannot BUILD the suite with these tests, failing with
exactly `Instruction has a name, but provides a void value!`. The ZST case was confirmed separately on
a probe (garbage payload, then an `abort` out of a `match` whose discriminant matched no arm).

#### Method note

Both were found by reading emitted IR against a control, not by reasoning about the passes. The ZST bug
showed up as a one-line type diff (`{ i32, [0 x i8] }` vs `{ i32, [2 x i64] }`); the leak showed up by
printing the call node's resolved type under `--debug` in both orderings and seeing `Int` where the
working order said `Unit`. In both cases the fix location was then obvious, and in both cases a comment
already in the file described the hazard — the guard just did not cover the case.

### 2026-07-28 — §5b-port increment 2: HTTP/1.1 framing on `BufStream<S>`; two async-lowering bugs fixed at the root

**Shipped.** `net/http` now frames messages over the async connection seam:

- **Sync encoders.** `Headers::encode_into`, `Request::encode_into`, `Response::encode_into` build a whole
  message into a caller-supplied `Array<u8>*` — normally the connection's `pending()`. A message is
  therefore assembled with **no suspension point inside it**, and the caller suspends exactly once, in
  `flush`. `headers::push_all` is the one OOM-checked append they share. `Headers::content_length` reads
  the declared body size once, reporting a non-numeric value as `InvalidData` rather than guessing zero.
- **Grammar extracted once.** `Request::from_request_line` / `Response::from_status_line` parse a single
  line into a message with empty headers and no body. The blocking `parse<R: Read>` and the new async
  reader now share them, so the grammar has exactly ONE implementation while both paths coexist during the
  migration.
- **Async readers are methods on the connection.** New `net/http/conn.cryo` declares `type trait HttpConn`
  with `read_request` / `read_response`, implemented for `BufStream<S>`. This is forced, not stylistic: a
  parser written as a static taking `mut &BufStream<S>` cannot be called by an async caller holding the
  connection as a local (E0455 — only the RECEIVER is re-supplied on every poll).
- **`Client::get` / `post` / `send` are now `async`**, over `TcpConnect::start` + `BufStream<TcpStream>`.
  `send_over` is untouched this increment; the server still uses the blocking path, so `write_to` / `parse`
  stay until increment 3 retires them.

**Bug 1 — a `return` in one branch made a state disown a value it still held (found by the new tests).**
`last_use_consumes` walks a state block's statements in source order and keeps the last answer, but
alternatives are not sequential: it walked BOTH arms of an `if` and EVERY arm of a `match`. One arm that
returned the carried value therefore set the marker for paths that never execute it, `needs_handback`
concluded the state had given the value away, the tail hand-back was skipped, and the next state's
`take().unwrap()` found a `None`. Shape:

```
mut resp: Response = ...;            // carried across the header loop
match (declared) {
    Option::Some(n) => { length = n; }
    Option::None    => { return Result::Ok(resp); }   // <-- poisoned the other path
}
if (length > 0) { mut body = (await this.read_exact(length))?; resp.body = body; }
```

Fixed by skipping branches that `return`: a path that never reaches the tail cannot describe what the
state does on the way out. The new `stmt_returns` is deliberately NARROWER than `stmt_diverges` — the
latter also reports `break`/`continue`, which land at the end of a loop that is still inside the same
block, so a value given away just before one of those DOES reach the tail and must not be handed back.

**Bug 2 — the receiver refresh was silently skipped for an `async` trait method on a generic receiver
(PRE-EXISTING; confirmed identical under the pinned compiler).** This is the one that mattered: **no
`BufStream<S>` could survive more than one write-then-read cycle**, on any protocol. Nothing caught it
because every existing test flushes at most once per connection.

`await this.inner.write_from(out)` inside a method of `BufStream<S>` awaits `S::write_from`, whose future
`desugar_async_trait_methods` expressed as the projection `S::WriteFromFut`. `awaited_recv_ptr_type`
answers "does this future keep a receiver pointer?" from the future's own layout — and a projection over a
generic parameter has no layout yet, so it answered "no" and `lower_carrier_sm` emitted no refresh.
Monomorphization then produced a future that DOES store one. The sub-future kept the `&this.inner` it
recorded when it was built; the caller's carried `BufStream` moves between polls (taken out of its `Option`
carrier into a fresh local each time), so `write_from` swapped the socket back into **dead stack memory**
while the live connection kept the closed placeholder — `EBADF (9)` on the next write, and a hang on the
peer.

The declaration settles what the abstract type cannot: `awaited_keeps_recv_ptr` recognises the awaited
projection as this method's own future (via the canonical `async_fut_assoc_name`), then reads the receiver
form off the trait's declaration. `resolved_method` is NOT usable here — overload resolution needs a
concrete owner, so it is null for exactly these calls.

**Method.** Bug 2 was found by bisecting downward, not by reading passes: 4 write/read cycles failed
through `BufStream<TcpStream>` (0/6) but passed driving `AsyncTransport` directly (3/3), which put the fault
between them; the emitted IR then showed the resume path going `take` → `unwrap` → `poll` with no receiver
store between, and a `--debug` print of the awaited type named `AssocProjection` / `GenericParam` /
`WriteFromFut`. Comparing against `bin/cryo` proved it pre-dated the session's changes.

**Coverage** — `tests/tests/stdlib/net_http_conn.cryo`, 8 tests, every one asserting a number:
two pipelined responses out of ONE fill assert a **fill count of exactly 1**, with the same pair split
**one byte per fill** as the control (fill count == byte count); truncated body → `UnexpectedEof`;
non-numeric `Content-Length` → `InvalidData`; encode→parse round trip; **four exchanges on one
connection** over a real `Executor` and reactor, with the one-exchange control beside it; and
`Client::get` end to end over loopback.

**LANDMINE recorded.** A pipe swallows a hung probe's output even after `fflush` — `timeout 15 ./probe |
tail` printed NOTHING while `> out.log` captured every marker. Redirect to a FILE when diagnosing a hang.

---

## 2026-07-28 — increment 3: `net/http/server` on the async transport; three findings

**Landed.**

- **`BufConn<S>` renamed to `BufStream<S>`** (Jake's call). It sits in `io/buf.cryo` beside
  `BufReader` / `BufWriter` / `LineWriter`, and the abbreviation was the odd one out. Mechanical;
  no behaviour change.
- **`HttpConn` is gone.** The framing reads are an ordinary cross-module inherent impl —
  `implement struct BufStream<S> { async read_request ... }`. Being a METHOD is forced (E0455 refuses
  `await f(&conn)` on a caller's local); being a TRAIT never was. Jake's call after the alternatives
  were laid out: a trait with one implementor and no bound naming it bought nothing. The rule that
  falls out and that ws/http2 should follow: **protocol state ⇒ owning wrapper** (`WebSocket<S>` has
  `is_client`/`closed`, so it owns a `BufStream<S>`), **stateless framing ⇒ inherent impl**.
- **`net/http/server.cryo` ported.** `async run` binds and delegates to a new
  `async run_on(listener)`; `TcpAccept` consumes the listener and hands it back, so the loop reclaims
  it each iteration rather than borrowing across the suspension. Each connection is served by
  `async serve_connection(mut &this, conn: BufStream<TcpStream>)`, which takes the connection **by
  value** — a future may only re-address its own RECEIVER between polls, so lending the accept loop's
  local is not expressible. `run_on` exists because a test needs a known port: binding inside `run()`
  and reporting the port afterwards races another process for it.
- **Transitive receiver refresh** (the §3.0(iii) miscompile). `awaited_recv_ptr_type` answered "does
  this future keep a receiver pointer" from the future's OWN layout, so `Timeout<F,O>` / `Join<A,B,..>`,
  which hold it in a NESTED field, answered "no" and got no refresh **and no diagnostic**. The layout
  cannot answer at lowering time — the awaited `Timeout` is an `InstantiatedType` whose template has
  **zero fields** until specialization — so the path comes from the DECLARATION, the same lesson as the
  trait-method fix above: `CallExprNode.resolved_template` names the constructing static, and its
  returned struct literal maps parameter → field. Mapping through the PARAMETER rather than the field's
  TYPE is what keeps siblings apart: `Futures::join(a.read(), b.read())` builds two futures of the same
  type and only the parameter each was passed as says which receiver belongs to which field. An
  unprovable path is a hard E0455, never a skip.
- **The frame-address check no longer fires on scalars.** `reject_frame_addr_carry` searches the
  initializer syntactically for a user-written `&`, which is right without lifetimes but is an
  over-approximation when the carried value could not hold an address:
  `const keep_alive: boolean = should_keep_alive(&req, n);` was refused. A `boolean`/number/`char` has
  no room for a pointer, so for those the address provably died with the call. Everything else stays
  conservative; all eight E0455 negatives still fire.

**Gates.** `make selfhost-check` exit 0 with **TWO** `FIXED POINT OK` (Linux 3m57s + native Windows).
Repin taken and verified — both sidecars a matched pair at `fa18f84f`, `verify-pin: OK`. **The pin now
carries the compiler fixes**, which it did not before: every `make stdlib` was building the archive with
a compiler that miscompiles async connection code.

**Coverage** — `tests/tests/stdlib/net_http_server.cryo`, 4 tests: four exchanges on ONE connection with
the one-exchange control beside it, TWO SEPARATE connections through one listener (the only test that
re-enters the accept arm), and a truncated request answered with 400. Each is driven under
`Futures::select(s.run_on(l), client)` — `run_on` is an `async` METHOD, so a green result here is also
the regression test for the refresh above.

### Three findings, none worked around silently

**1. An owning local written inside a BRANCH is miscompiled. RED TEST IN TREE.**
`tests/tests/stdlib/async_branch_owning_local.cryo`. An owning aggregate assigned from an `await` inside
an `if`/`else` arm produces heap corruption (double drop) or `unwrap() on a None value` (missing
hand-back), depending on which side of `needs_handback` / `last_use_consumes` gets it wrong. The control
— identical code with the branch removed — passes, which is what identifies the branch rather than the
loop, the await, or the owning payload as the variable that matters. **PRE-EXISTING**: it reproduces with
no combinator anywhere near it, and neither compiler change above touches the hand-back path. Minimised
from the server outward: `got` alone is fine, `dispatch` is fine, the by-value connection parameter is
fine; adding the `if`/`else` is what flips it.

*Consequence, stated plainly:* **`HttpServer::with_read_timeout` is stored and NOT enforced.** Wrapping
the per-request read in `Futures::timeout` forces exactly this shape. The server therefore has no
slow-loris defence; it is documented at the top of `server.cryo` and in `with_read_timeout` itself rather
than left for someone to discover. The cap returns with the fix.

**2. The transitive refresh does not reach generic contexts.** `ctor_field_for_param` needs
`resolved_template`, and sema leaves it unset for a generic static called from a body walked
symbolically — so `await Futures::timeout(d, this.read_request())` inside `implement struct BufStream<S>`
is REJECTED rather than refreshed. Loud, not silent, but it blocks combinators inside generic impls.
Fix: resolve the callee from its `ScopeResolution` spelling when `resolved_template` is empty.

**3. `this` in an if-EXPRESSION initializer resolves to the Future.** Inside an `async` method,
`const cap = if (this.field > 0) { ... } else { ... };` gives E0204 naming
`HttpServer$serve_connection$Future_2` — the receiver rewrite does not reach into that position.
Statement-level `if` works. Same walker-coverage family as the standing warning about adding a node kind
to ALL walkers.

---

## 2026-07-28 (later) — the branch-owning-local double free, half fixed

**Fixed: states that PRODUCE a carried value now bind their own name.**

A state whose first touch of a carried local is a top-level write does not take the value out of the
carrier — it produces its own — so `decl_at_first_assignment` turns that write into a declaration. It
bound the SAME alpha-renamed name in every such state, and **codegen keys a local's drop flag by
name**, so several states shared one flag while owning separate storage. A flag raised where one state
initialized its copy then enabled the scope-exit drop in a state that never did.

The arithmetic is why an `if`/`else` is the shape that broke: one declaring state is fine, two survive,
three — the entry state plus one per arm — release the same heap block twice. `decl_at_first_assignment`
now mints a fresh name per declaring state, rewrites that state onto it, and returns it so the caller
hands the value back under the right name.

`tests/tests/stdlib/async_branch_owning_local.cryo` was RED and is now GREEN. `make test`:
**1853 unit / 159 compile-fail / 12 projects, zero failures.**

**Method.** The IR named it, not reasoning about the passes. Counting `alloca`s for the local against
`load i1 ... dropflag` sites per function: control 3/1, one-arm branch 4/2, both-arms branch 5/3 — drop
sites scaling with declaring states while the flag count stayed at one. (A first pass at this miscounted
because the `awk` window spilled into the next function and made it look like one variable had become
two; bounding each `define` fixed that.)

**NOT fixed: states that TAKE a carried value still share one binding.**

The same collision exists on the take side — every state opening with
`mut x = this.field.take().unwrap()` declares `x` — and it cannot be fixed the same way. The state
blocks SHARE AST statement nodes: a loop body reached from several states is one node list, so renaming
per state rewrites a node another state also reads. The second state's take then declares a name nothing
refers to, while the shared statements name a binding that is not in their scope (E0201 in
`ass_try_in_loop`, `cannot find value c$L10`). Confirmed by tracing the rebinds: blocks 2/3/4 of one
function were handed `c$L10`/`c$L11`/`c$L12` over shared statements.

Two ways out, neither attempted: de-share the statement nodes across state blocks, or key drop flags per
DECLARATION rather than by name in codegen. The note above `prepend_agg_take` records this at the site.

**Consequence, unchanged:** `HttpServer::with_read_timeout` remains stored and NOT enforced — wrapping
the read in `Futures::timeout` needs the framed request written into a local from inside a branch, and
that trips the remaining half. The server therefore still has no slow-loris defence, and says so at the
top of `server.cryo` and on the setter.

---

## 2026-07-28 (later still) — the other half: drop flags keyed per DECLARATION

**Fixed at the root, in `passes/drop_insertion.cryo`.** The conditional-move drop flag was recorded in a
function-wide, NAME-keyed side table (`flag_binding_keys`/`flag_flag_keys`). It is now
`binding_dropflags: u32[]`, parallel to `binding_names` and truncated in lock-step on scope pop —
exactly the shape `binding_initflags` already had. `get_drop_flag` resolves through the innermost live
binding slot; `register_drop_flag` stamps that slot; and `maybe_append_drop`, which already receives the
slot index, reads its own slot directly rather than looking the name up again.

Name-keying is wrong the moment one function declares a name twice: the declarations own SEPARATE
storage and must not share a flag. An `async` function makes that routine rather than exotic — its state
blocks all live in one lowered `poll`, and every state that takes a carried value across a suspend
declares it under the same name. `flag_binding_keys` is gone; nothing else consumed it.

**This closes the taking side**, which the previous entry recorded as unfixable by renaming (the state
blocks share AST statement nodes, so renaming per state rewrites a node another state reads → E0201).
No renaming was needed. `decl_at_first_assignment`'s per-state fresh name is kept — it is independently
reasonable and already validated — with its rationale corrected, since it no longer rests on a claim
about how codegen keys flags.

**Method — the diagnosis, not the guess.** The first theory was that `register_drop_flag` was REFUSING a
second registration; a temporary `cdebug` in that branch proved it never fired. The real path is
`get_drop_flag`: the first state's registration persists function-wide, so every later state's
`synth_flag_for_*` takes the "already has a flag" early-out and REUSES a flag belonging to another
declaration's storage. The IR named it exactly — in the repro's `poll`, `got$L34` had **three allocas,
one `__dropflag0`, and three `load i1` guard sites**, the two extra guards reading a flag raised in a
state that did not own their storage. Post-fix: one guard, on the one declaration with a genuine
conditional move; the other two declarations are governed by their own move state.

**Minimal repro** (crashes `0xC0000374` on the pinned compiler, correct after the fix), with three
control shapes that pass either way — a flat take, a `?`-in-loop take, and a take on both arms of a
branch — so the variable is isolated to *how many states carry the value in*:

```cryo
async function t_timeout(use_timeout: boolean, iters: i64) -> u64 {
    ... mut got: Result<String, i64> = Result::Err(0);
    if (use_timeout) {
        const r = await Futures::timeout(Duration::from_millis(500u64), give());
        match (r) { Result::Ok(inner) => { got = inner; } Result::Err(_) => { got = Result::Err(7); } }
    } else { got = await give(); }
    match (got) { Result::Ok(s) => { mut o: String = s; ... } Result::Err(_) => { } } ...
}
```

**`HttpServer::with_read_timeout` IS NOW ENFORCED.** `serve_connection` wraps the per-request read in
`Futures::timeout` when `read_timeout_secs > 0`; hitting the cap returns, closing the connection, because
an abandoned read leaves the transport holding a closed placeholder while the read future owns the real
socket — so nothing after it can reply on that socket. The two "NOT ENFORCED" doc blocks in
`server.cryo` are gone, as is the stale `SO_RCVTIMEO` wording on the field.

**New tests.** `carried_owning_local_taken_by_several_states` + its control
`carried_owning_local_taken_by_one_state` (same capped read written from inside a `match` arm, differing
only by the `if`/`else` that multiplies the binding states), and
`server_read_timeout_closes_an_idle_connection` — a client that connects and says nothing, asserting the
server took `>= 900ms` to hang up against a 1-second cap, so a server that closes immediately cannot
pass. Both new async tests were verified to FAIL on the pinned compiler and pass on the fixed one; the
controls pass on both.

---

## 2026-07-28 — §5b-port increment 4: `net/https.cryo`, and the blocking TLS twins die

**`HttpsClient` is `async` and runs on `BufStream<TlsStream>`.** `get` / `post` / `send` are `async`;
`send` dials with `TcpConnect::start`, hands the socket to `TlsConnector::connect`, wraps the result in
the one connection type, encodes the whole request into its outgoing buffer, flushes once, and reads the
response back through the SAME buffer — byte for byte the plaintext path in `http/client.cryo`, because
a `TlsStream` is an `AsyncTransport` like any socket. `Connection: close` moved into `send` with it.

**The blocking TLS surface is gone, in the same increment, so no twin ever coexisted with the real one**
(the standing rule). `TlsConnector::connect` and `TlsAcceptor::accept` ARE the async ones now;
`connect_async` / `accept_async` are renamed `start_connect` / `start_accept` and keep the two-step form
for a caller that must own the handshake future (to select over it, or give it a deadline). The blocking
bodies and `drive_handshake` are deleted. Nothing was lost with them: the belt-and-suspenders
`SSL_get_verify_result` gate the blocking `connect` carried already lives in `TlsHandshake::poll`.

`client::send_over` — the transitional generic over `Read + Write` that existed only to serve the HTTPS
path — is deleted too.

**Tests.** `net_https.cryo` is rewritten onto the async stack and now drives the SHIPPED `HttpsClient`:
before this, `HttpsClient` had **no coverage at all** — the old test open-coded the client half against
the blocking TLS API and never constructed one. Both halves run as tasks on one `Executor`, and the
response body is asserted by value so a truncated read cannot pass as an empty success.
`net_tls_async` / `net_tls_conn` move to `start_connect` / `start_accept`.

**`net_tls.cryo` is DELETED, and that deserves saying out loud.** Every line of it was the blocking
surface — `TlsConnector::connect`, `TlsAcceptor::accept`, `TlsStream::read_all`/`write`, and a thread per
side because a bidirectional handshake cannot run single-threaded on blocking sockets. Its subject no
longer exists, and its behaviour ("an encrypted byte channel carries data both ways over loopback") is
already asserted by `net_tls_async::async_tls_loopback_round_trip`, which does it on one executor with no
thread and no port-guessing. This is a test whose SUBJECT was deliberately removed, not a test removed to
get green. Its roster line was deleted by hand rather than by `roster-check --update`, so the golden kept
the other platform's entries (`git diff --numstat` = `0 1`).

Still to port: `net/ws/conn.cryo` (borrows its transport — must become owning), then `net/http2/*`.

---

## 2026-07-28 — §5b-port increment 5 (`net/ws`): ATTEMPTED, REVERTED, five findings

**Status: NOT landed. The tree is at increment 4; the ws port is reverted.** The design is right and the
code was written, but `BufStream<S>::read_frame` miscompiles at runtime and I could not restructure
around it. Per the standing rule, an unverified change does not stay in the tree. The WIP diff is not
preserved in-repo; the findings below are what matters, and each has a reduction.

**The design (unchanged, and still the right one).** `WebSocket<S>` OWNS a `BufStream<S>`: it carries
per-connection protocol state (`is_client`, `closed`), so by the settled rule it is the owning wrapper,
and an `async` method may only re-address its own receiver between polls. Framing is stateless, so
`read_frame` becomes an `async` inherent impl on `BufStream<S>` beside HTTP/1.1's `read_request`, and
`encode_frame` becomes a SYNC `encode_into(out: Array<u8>*, ...)` writing straight into the connection's
outgoing buffer — the same shape as the HTTP encoders. Owning the connection is also what kills the
byte-at-a-time handshake read: the buffer outlives the handshake, so frame bytes that arrived in the same
fill as the blank line survive into the first `recv`, and the head can be read a line at a time.

**FINDING 1 — the blocker. `BufStream<S>::read_frame` miscompiles: `unwrap() on a None value`.**
An `async` inherent method on the GENERIC `BufStream<S>` that issues several `await this.read_exact(n)`
calls panics at runtime — a carried value's `Option` carrier was empty when a later state took it.
Isolated all the way down: not `WebSocket`, not the handshake, not `send_text`. A plain
`mut c: BufStream<TcpStream>` local calling `await c.read_frame()` reproduces it on its own.

What is NOT the trigger — each of these passes as a free `async function`, so the control flow is
exonerated: pre-declaring an owning local and assigning it from an `await` inside a branch; an `await`,
then an `if`/`else` whose BOTH branches `await`, then a further `await` after the join with scalars from
before the branch still live. The remaining axis is the one `read_request` does not exercise the same
way: a generic-owner method whose suspensions are `AsyncRead` DEFAULT methods called on `this`.

**FINDING 2 — a DEFAULT trait method does not resolve on a generic-instantiated receiver.**
`c.queue<Str>(...)` on a `BufStream<S>` inside a function generic over `S` is `E0636: no method 'queue'
found on type BufStream<TcpStream>` at CODEGEN, once `S` is instantiated. `queue` is a default method on
`AsyncWrite`; the REQUIRED `pending()` resolves fine, and the same `queue` call works on a concrete
`BufStream<TlsStream>` outside a generic function.

**FINDING 3 — a scalar declared before an awaiting branch is lost after the join.**
Inside that generic method, `const h1: u8 = ...;` before an `if`/`else` whose branches `await`, then read
after the join, is `E0201: cannot find value h1$L11 in this scope`. Same for a `u64` written in a branch
(`key_len$L22`). Loud, not silent.

**FINDING 4 — an if-EXPRESSION initializer in an `async` method CRASHES THE COMPILER.**
`const ext_len: u64 = if (len == 126) { 2 } else { ... };` inside an `async` method killed the build with
heap corruption (exit `-1073740940`) after printing a diagnostic that had substituted an UNRELATED
local's name into the source line (`if (fin$L11 == 126)` where the source says `len`). Same position as
the standing open item about `this` in an if-expression initializer, but the symptom is a crash and a
corrupted diagnostic rather than an error. Statement-level `if` is the workaround, and is what the tree
uses elsewhere. **This one is worth fixing on its own account** — a compiler that corrupts its own heap
on a legal-looking construct is worse than one that rejects it.

**FINDING 5 — a fixed-size array carried across a suspend is unrepresentable.**
`mut key: u8[4];` live across an `await` is promoted into an `Option<u8[4]>` carrier field, and
`Option<T>` cannot hold one — `E0200` inside `stdlib/core/option.cryo` (it assigns through `*this`).
Loud. Use a heap `Array<u8>` instead, or reject the shape with a real diagnostic.

**Where to resume.** Finding 1 is the blocker and Finding 4 is the most severe. Both want a compiler
session, not a stdlib one. `net/http2/*` is the other remaining consumer and BORROWS its transport too,
so it will meet Finding 1 the moment it is converted — worth fixing the compiler first rather than
discovering it a third time.

## 2026-07-28 (later) — increment 5's blocker ROOT-CAUSED and fixed; a second carrier bug fixed; the ws port still blocked, on a THIRD (pre-existing) defect

**Two compiler fixes landed and gated. The ws port is written but still NOT in the tree** — it is
verified BROKEN by a defect that is older than any of this work and larger than the port. The WIP diff is
preserved this time, at **`.todo/ws-async-port.patch`** (applies to `stdlib/net/ws/*` +
`tests/tests/stdlib/net_ws.cryo`).

### Finding 1's axis was WRONG. The trigger is the `?` operator.

The previous entry named "a generic-owner method whose suspensions are `AsyncRead` DEFAULT methods on
`this`". That is not it — the reduction comes out GREEN on every part of that axis. Built as standalone
probes and all passing before any fix: a generic `Holder<S>` awaiting a default trait method on `this`
two and three times over; the same returning an owning aggregate; the same with the aggregate read
between the awaits. The axis survived only because the previous attempt's own reduction hit a DIFFERENT
unsupported shape first (`await this.inner.chunk(4)`, a method on a FIELD) and never tested the real one.

The actual trigger is one construct, and it needs neither a generic owner nor a trait:

```cryo
async function e() -> Result<i64, IoError> {
    mut len: i64 = 0;
    len = 3;
    mut body: Array<u8> = (await grab(len))?;   // error[E0201]: cannot find value `len$L7`
    ...
}
```

A **local read anywhere inside an `await` that `?` is applied to** loses its name. A parameter is fine, a
literal is fine, hand-hoisting (`mut r = await grab(len); mut body = r?;`) is fine. Finding 3 of the
previous entry is the same bug — its `$L`-suffixed E0201 is this one, reached through a branch.

**Root cause.** `resolve_try_expr` builds the `?` desugar as `new MatchExprNode(node.operand, ...)` — the
match's SUBJECT and the `?`'s `operand` are literally the same node, and `rn_expr` already documents that
("renaming both would alpha-rename that subtree twice"). The async lowering then rewrites the desugar in
place and REPLACES that subject: `hoist_expr` lifts the `await` into its own statement, and
`subst_name_expr` turns a promoted local into `this.<field>`. Both left `operand` pointing at the
pre-rewrite tree — which is still walked, because `resolve_try_expr`, call specialization and dispatch
annotation all read `operand` and never `desugared`. The resolver therefore met an `await` that had moved
to an earlier state, naming a local that no longer exists where it sits.

**Fix.** `TryExprNode::resync_operand()` (`AST/expression.cryo`) re-points `operand` at the desugar's
subject; the two in-place rewrites in `async_lower.cryo` call it. That restores the documented
`operand IS desugared.subject` invariant at every point that can break it. 4 tests
(`async_try_*` in `async_stress_shapes.cryo`), including a hand-hoisted control.

### The second bug: a carried value with NO destructor was never handed back

With the `?` fix in, the ported `read_frame` compiled and then panicked `unwrap() on a None value` —
i.e. the symptom Finding 1 described, but reached through a different door. Reduced to 8 lines, no
generics, no traits, no I/O:

```cryo
type enum Tag : u8 { Zero = 0x0; One = 0x1; Two = 0x2; }

async function t5() -> u64 {
    const tag: Tag = Tag::Two;
    if (tag_num(tag) > 99) { return 0; }        // by-value read, last use in this state
    const _a: u64 = await tick(1);
    return tag_num(tag);                        // take().unwrap() on a None
}
```

`zeroable_kind` covers only Int/Bool/Float/Pointer, so a payload-free enum — or any struct of scalars, or
a `Slice` view — rides the AGGREGATE carrier (`Option<T>` + take-on-entry / hand-back-on-exit) rather
than a plain scalar field. `needs_handback` suppresses the hand-back when the state "gave the value
away", and `last_use_consumes` read that off the syntax alone: ANY by-value use counted. For a value with
a destructor that is right — it is a move. For one without, a by-value use is a COPY and the state still
holds its own, so suppressing the hand-back left the carrier empty for the next state.

**Fix.** `last_use_consumes` now takes the carried type and answers `false` outright when the type cannot
be given away — `OwnershipQuery::needs_drop`, the same authority `move_check::type_needs_drop` uses for
reuse after a by-value move. Anything whose ownership is not READABLE at lowering time (a generic
parameter, an instantiation specialization has not filled in yet) stays conservative and counts as
givable: guessing wrong that way costs only the hand-back, whereas guessing wrong the other way forces a
hand-back of a value the state really did move and the move checker rejects it (E0452 — which is exactly
what a first, ungated version of this fix produced on `async_trait_method.cryo`). 5 tests, including both
sides of a branch and a no-early-read control.

### The ws port is still blocked — on a THIRD defect, and it is not an async-lowering one

With both fixes in, every hermetic ws probe passes: framing round-trips masked and unmasked,
`read_frame` decodes over a mock, the handshake runs, `recv` yields the message. The loopback test still
failed, and the diagnosis is this, reduced to 40 lines with no sockets and no ws:

```cryo
async function consume(text: Str) -> u64 { /* suspends, THEN reads `text` */ }

async function run() -> u64 {
    mut msg: String = String::from_str(Str::new("m"));
    msg.push_byte(0x30u8);
    const got: u64 = await consume(msg.as_str());   // reads freed memory
    return got;
}
```

**A non-owning view handed to an awaited call outlives the local it borrows.** `msg` is not mentioned
after the `await`, so the lowering ends its live range in that state and the drop lands at the end of the
state's block — but the future that holds the view is polled in a LATER state. The freed block is then
handed straight back out by the next allocation, which in the ws case was the connection's own outgoing
buffer: `send_text`'s payload silently became the frame header it had just written, so the frame went out
masked with one key and carrying another. Silent corruption, no diagnostic.

**PRE-EXISTING**: the reduction reproduces identically under the pinned `bin/cryo`. It is not caused by
anything in this session; the ws port is simply the first consumer to pass a view of a heap local into an
`async` call. Note the scope — this is not a ws problem and not a `Str` problem. **Every `async`
function taking a `Str` or `Slice` parameter is unsound whenever the caller's backing local is not live
after the await**, which includes `AsyncWrite::send<T>` / `queue<T>` as written today.

That is why the port is reverted rather than shipped: its API (`send_text(text: Str)`,
`client_handshake(host: Str, path: Str)`) is exactly the shape the defect breaks, and shipping it would
ship a landmine. Splitting every send into a sync `queue_*` plus an `async flush` would dodge it, but
that is routing the stdlib around a compiler bug, not fixing it.

**A fix has to extend the live range of any local a suspended future borrows from.** The lowering already
has the machinery to notice an address of a frame local escaping (`frame_addr_root_expr`,
`reject_frame_addr_carry`); what it cannot see is a borrow taken through a CALL (`msg.as_str()`), which
is not syntactically an address-of. The conservative shape of the fix is to treat every local mentioned
in an awaited call's arguments as live into the resume state.

### Finding 2 (E0636) — reproduced, reduced, diagnosed, NOT fixed

It is real and it is NOT the same root as Finding 1. It is not an async bug at all — here it is fully
synchronous, and it reproduces under the pinned compiler:

```cryo
type struct Inner<S> { seed: S; }
implement trait Sink for struct Inner<S> { room(&this) -> u64 { return 10; } }
// `put<T>` is a generic DEFAULT on `Sink`; `own<T>` a generic INHERENT method on Inner.

function a<S>(i: Inner<S>*) -> u64 { return i.put<u32>(7u32); }   // E0636 at codegen
function b<S>(i: Inner<S>*) -> u64 { return i.own<u32>(7u32); }   // E0636 at codegen
```

The trigger is **a GENERIC method called on a receiver whose type mentions the ENCLOSING generic
parameter** — trait-default or inherent alike, receiver a parameter or a field. A concrete receiver
inside a generic function is fine; a NON-generic method on `Inner<S>` is fine.

Two independent halves, both traced:

  * **sema never stashes the method's type args.** `find_generic_method_for_call`
    (`sema/method_binding.cryo`) bails outright when any receiver type-arg
    `contains_generic_param`, so `stash_method_call_bindings` never runs.
    `stash_owner_generic_method_bindings` covers the case where the receiver IS the enclosing owner
    (`this.try_push(...)` in `String<A>::push<T>`) and only that case; `Inner<S>` inside `Owner<S>`, or
    inside a generic free function, matches neither, and lands in the abstract-receiver branch whose
    lookup is off the receiver's TRAIT BOUNDS and finds nothing.
  * **mono cannot recover the receiver type.** Traced with `cdebug`:
    `specialize_method_call` reports `recv_valid=0 derived=1 stash=0`. The clone reset
    `ma.object.resolved_type`, and `derive_receiver_type` bottoms out because it only walks
    `MemberAccess` — the chain ends at `this` or at a parameter identifier, neither of which it can type.

Fixing it means touching both, in the most delicate part of the compiler, and it is independent of the
async work — so it was left out of this session's repin rather than bolted onto it. It does not block the
port: the stdlib's established encoder idiom is `Array<u8>*` + `push_all` (what `Request::encode_into`
does), not `queue<T>`, and `queue<T>` has no stdlib callers at all today.

### Findings 4 and 5 — could NOT be reproduced

Probes for both come out green under the fixed compiler AND under the pin: an if-EXPRESSION initializer
(including the nested `if (len == 126) { 2 } else { if (len == 127) { 8 } else { 0 } }` form) inside an
`async` method on a generic owner compiles and returns the right value, and a `mut key: u8[4];` held
live across two suspends round-trips. Either they were secondary manifestations of the `?` bug, or they
need a shape the probes did not hit. **They are not closed, but they are no longer reproducible as
written** — treat the previous entry's descriptions as unconfirmed.

### Gates

`make test` **1863** unit (1854 Linux baseline + 9 new) / 0 failed, **159** compile-fail, **14**
projects. Roster merged with `--merge` (`9 0`, the Windows-only entry kept). `make selfhost-check`
exit 0 with **TWO** `FIXED POINT OK` (Linux, and Windows via wine — a Linux host runs BOTH halves).
Repin taken and verified. Compiler diff is two files:
`AST/expression.cryo` (+`resync_operand`) and `sema/async_lower.cryo` (two resync calls, the
type-aware `last_use_consumes`).

### Where to resume

1. **The dangling borrow** is the ws blocker and the most severe thing open — it is silent, it is
   pre-existing, and it reaches every `async` fn with a view parameter. `.todo/ws-async-port.patch`
   re-applies the port on top of the fix; the port's own tests (incl. the pipelined-handshake one) are in
   the patch and were written to assert numbers.
2. **Finding 2 (E0636)** next: `net/http2/*` uses no generic default methods on generic receivers today,
   so it is not on the critical path, but it is a sharp edge with a 50-line repro.

### 2026-07-28 — Borrowed views across a suspend FIXED; `net/ws` LANDED (§5b-port increment 5)

Two soundness defects of the **same family** — a borrow the type system does not record, taken from a
frame-resident local and handed across a suspend. Cryo has no lifetimes, so nothing links the derived
value back to the storage it names; what made both invisible is that `frame_addr_root_expr` only
recognizes a **syntactic `&`**, and neither of these writes one.

They are NOT the same fix. The first is repaired by keeping the owner alive; the second cannot be
repaired at all and is now rejected. The distinction is what decides which: a view into a **heap block**
the local owns stays valid as long as the local does, whereas a pointer into the local's **own storage**
goes stale because a carried local is re-materialized at a new address on every poll.

**(a) A view (`Str` / `Slice`) borrowed from a local outlived its owner.** `promote_cross_state` decided
a local "crosses" a suspend only when a LATER state block reads its name. In `await consume(msg.as_str())`
the last mention of `msg` sits inside the await operand, which lives in the DECLARING block — so the local
was left on the native stack and drop insertion ran its destructor at the end of that block, freeing the
buffer before the sub-future was polled even once. The freed block came straight back out of the next
allocation, so the payload of a ws frame silently became the header it had just written. **Pre-existing;
reproduces under the pinned compiler in 40 lines.** Every `async` fn taking a `Str`/`Slice` was affected,
`AsyncWrite::send<T>`/`queue<T>` included.

Fixed at the root, in the lowering that broke it: "read in a later state" was only ever a proxy for "still
live there", and the source scope of `msg` plainly outlives the await. `PollSm.borrow_ops` now records
each awaited operand as it is stashed, and a local with a **destructor** that any operand mentions is
carried — its drop moves past the suspension and it dies with the future. Jake's calls (2026-07-28):
**conservative promotion**, not a diagnostic (Cryo cannot tell a borrow from a derived value, so the
predicate would reject sound code); scoped to **frame locals and parameters**, not fields or temporaries
(those are the live ranges the state split shortened — a field belongs to a receiver that outlives the
suspend, and a temporary dangles identically in SYNC code).

Gated on `needs_drop`, and deliberately WITHOUT the `ownership_unreadable` guard that
`carried_can_be_given_away` uses — the polarity is opposite. A destructor `needs_drop` reports is real; the
cost of an abstract type is one it FAILS to report. Under-reporting only leaves the drop where the split
put it (the old behaviour), whereas treating every unreadable type as owning would carry values a generic
`async function` cannot carry at all, turning working code into a hard error. `String` is an
`InstantiatedType` whose fields are not materialized at lowering time and it still answers `needs_drop`
correctly — which is exactly why the first version of this fix silently did nothing.

**(b) A pointer taken through a CALL addressed a stale copy — now `E0455`.** Found by landing the port:
`pipelined_upgrade_and_frame_round_trip` HUNG. The cause was not ws framing at all but
`mut c: BufStream<TcpStream>* = ws.connection();` held across the handshake's awaits. `ws` is carried, so
each poll takes it into a fresh block-local — and `c` kept addressing the copy the later states had
stopped using. Reduced to 40 sync-looking lines (`o.conn()` returning `Inner*`): the writes land on the
stale copy and are simply lost, reporting **11012011 instead of 11012013**. A wrong value, not a crash.

`call_frame_addr_root` now treats a call whose result is a pointer/reference and whose receiver roots in a
frame-resident place as the address-of it is. **This is not a new restriction — it is the existing `&local`
rule applied to a form the syntactic walk could not see**, and there is no silent repair available:
the ADDRESS changes, not the lifetime. A receiver that is itself an indirection stops the walk, so
`arr.as_ptr()` and `this.conn.pending()` are untouched. Jake's call (2026-07-28). New negative
`E0455_async_pointer_from_accessor.cryo`.

**`net/ws` LANDED, 5/5.** The port is otherwise as designed and was not relitigated: `WebSocket<S>` OWNS
its `BufStream<S>`, framing is an inherent impl on the connection, encoding is sync into `pending()` and
flushed once, the masking key crosses the payload suspension as a `u32` scalar. The pipelined client is
now written the way the language supports — the handshake is driven on the CONNECTION and wrapped in a
`WebSocket` only once the upgrade completes, which also states the design claim more directly: the
connection owns the buffer, so frame bytes that arrived in the same fill as the blank line survive into
the first `recv`. The old blocking `masked_text_frame_loopback_round_trip` is gone with the blocking
surface it tested; its roster line was removed BY HAND (`--merge` cannot tell "deleted" from "other
platform" and had kept it).

**LANDMINE for the next agent.** The previous session's hermetic ws verification (a mock `AsyncTransport`,
no sockets) passed all five shapes while the real-socket pipelined test hung. A mock transport cannot
exhibit (b) at all, because nothing there holds a pointer into a carried connection. **A transport double
is not a substitute for the loopback test** — it validates framing, not lifetime.

Coverage: 9 new tests in `async_stress_shapes.cryo` (borrowed view not mentioned again; the same mentioned
after, as the control; moved-by-value, which must NOT be carried twice; borrow-then-move; borrow in a
loop; `Slice` off an `Array` local; `Str` off a `String` local; a static-literal view; a scalar derived
from a local). Every one asserts a number and counts destructor runs. `probe3` returns `FAILMASK=257`
under the pinned compiler and `0` after the fix, so the new cells are load-bearing rather than decorative.

`make test`: **1875 unit / 160 compile-fail / 14 projects, 0 failed.**

Still open, unchanged: **Finding 2 (E0636)** — a generic method whose receiver mentions the enclosing
generic param; reduced to 50 SYNC lines, traced to two places (sema never stashes the method type args;
mono cannot recover the receiver type), both of which must be fixed together. Not on any critical path.
Then `net/http2/{client,server,connection}` — `connection.cryo` is 855 lines and BORROWS its transport,
so (b) above is directly relevant to porting it. Then delete the blocking surface (keep
`TcpListener::bind`).

---

## 2026-07-28 (later) — `Pin` mission, step 0: an in-place accessor could not be written at all

Jake's call on the address-stability forks, taken before any code was written:

1. **No `Pin` type.** Address stability is enforced by a **move-check rule** — a future may not be moved
   once it has been polled. `Future::poll` keeps `mut &this`, so there is **no API break** and no `Unpin`
   analogue is needed (the rule is a restriction, never a hole, so it is safe for the self-reference-free
   futures too).
2. **Uniform in-place carrier.** A droppable local carried across a suspend stops being taken out of its
   `Option<T>` field into a block-local each poll; it lives in the field, with a P13-style init flag
   carrying the bit the `Option` discriminant carries today.
3. **No migration flag** — land increment by increment.

### The substrate is in far better shape than §1's "three movers" implied — VERIFIED, not assumed

Every poll site in the tree was read before any of the above was proposed:

| Poll site | Moves the future between polls? |
|---|---|
| `block_on` (`future/_module.cryo:55`) | **No** — `mut f: F = fut;` once, then polls `f` in place forever |
| `spawn` → `task_poll_thunk` (`executor.cryo:531`) | **No** — `const fut: F* = fut_raw as F*; fut.poll(cx)` |
| `Join` / `Select` / `Timeout` (`combinator.cryo:105,149,184`) | **No** — `this.a.poll(cx)` through the field |
| compiler-generated sub-future (`async_lower.cryo:2300`) | **YES** — `take().unwrap()` out, put back on `Pending` |
| compiler-generated carried aggregate (`prepend_agg_take`) | **YES** — into a fresh block-local each poll |

The entire hand-written runtime is already address-stable. **Only the lowering moves anything**, which is
why the chosen mechanism is a move-check rule rather than a type: there is nothing to retrofit onto the
API, only two emitted patterns to stop emitting.

### Increment 1 (poll sub-futures in place) is blocked on a defect BELOW async, now fixed

Polling the sub-future where it lives needs an `F_k*` into the payload of `this.fut_k: Option<F_k>`.
Every route was blocked, and the two obstacles are worth recording because neither is async-specific:

- **`Option::as_ref` is invisible on a lowering-created instantiation.** It returns `Option<T*>`, a
  structural superterm, so `is_self_growing_instantiation` marks it `lazy_self_growing` and defers it to a
  mono call site (`types/resolver.cryo:932`). Sema then reports *no method named `as_ref` found on type
  `Option<…$Future_0>`* while `as_ref` on a user-written `Option<Cell>` resolves normally. **Still open**
  — it did not need fixing once the defect below was.
- **The return-escape check rejected every in-place accessor.** `check_return_stack_address`
  (`sema/sema.cryo:3197`) rejects `return &<ident>` whenever the ident is a local and not a parameter, and
  a `match`-arm payload binding registers as a local. So `payload_ptr(&this) -> T*` could not be written.
  Writing it via `as_ref` instead **towers infinitely** (`Option<T*>` → `Option<T**>` → …, killed the
  stdlib build twice — as an instance method AND as a static, so the anti-tower guard is not the escape).

**The check's premise is false for a subject reached through a reference, and the stdlib already depends
on that.** A payload binding over `match (&this)` *aliases the receiver's payload* — proven at
`--opt-level=0` as well as `-O2`, by three separate calls each mutating through a returned interior
pointer and the caller observing `1,2,3` rather than `1,1,1`. `Option::as_ref` relies on exactly this
aliasing and only compiles because wrapping in `Option::Some(...)` hides the `return &binding` from a
syntactic check. `ScopeManager::is_payload_binding`'s doc asserted the opposite ("Such a binding aliases a
stack temporary, so returning a pointer/reference to it dangles") and was wrong for that case.

**Fixed at the root.** The `match` subject's provenance is now threaded into pattern binding
(`bind_arm_patterns` / `bind_enum_pattern` / `bind_subpattern` take a `caller_backed` flag), bindings taken
from caller-owned storage are recorded in `SemaState.alias_binding_ids`, and `check_return_stack_address`
exempts them. The rule is deliberately narrow — **the subject must be reached through a by-reference
receiver (`&this` / `mut &this`, tracked by the new `this_is_by_ref`) or a pointer/reference parameter**.
It is the same caller-backed reasoning that already exempts `&param`. A **by-value** parameter does NOT
qualify: it is copied into this frame like any local. `check_return_payload_escape` is untouched.

Coverage: `lang/match_binding_alias.cryo` (4 tests; each writes through a returned interior pointer and
reads the ORIGINAL back, so a copy would be observable as a short total), plus three negatives locking the
boundary — `E0455_return_local_subject_binding`, `E0455_return_temporary_subject_binding`,
`E0455_return_byvalue_param_binding`. `make test`: **1879 / 163 / 14, 0 failed**.

**Not yet done:** increment 1 itself. The accessor is now writable, so the next step is `Option::unwrap_ptr`
plus the `lower_carrier_sm` rewrite (poll through the field; suspend without the put-back; take the
completed sub-future out on the ready path so a loop's rebuild does not overwrite a live value — an
assignment does not drop what it replaces).

---

## 2026-07-28 (later still) — increment 1 LANDED: sub-futures are polled in place

The state machine no longer moves an awaited sub-future between polls. `lower_carrier_sm` used to open
each resume state with `mut __sub_k = this.fut_k.take().unwrap();`, poll that block-local, and store it
back with `this.fut_k = Option::Some(__sub_k);` on `Pending`. The sub-future therefore had one address
while running and a different one while suspended, so any pointer into its storage that outlived a single
poll named a dead stack slot.

Now the resume state takes an interior pointer (`Option::unwrap_ptr`, new — see the previous entry for why
`as_ref` cannot be used here), polls through it, and on `Pending` writes only the state and returns. The
sub-future stays in its field, so its address is the field's.

**Two consequences that had to be handled, not just the poll site:**

- **`Pending` no longer stores anything back.** The value never left, so the put-back would now be a
  self-assignment — and, more importantly, the whole point is that the storage is not rebuilt.
- **`Ready` now takes the sub-future OUT** (`__done_k`), which the old code got for free from the take at
  the top. Without it a loop's next `this.fut_k = Option::Some(<new>)` would overwrite a live value, and
  **an assignment does not drop what it replaces** — a leak of one sub-future per iteration, which for a
  `Sleep` or a socket future is a leaked registration, not just memory. Drop timing is unchanged: the
  taken value dies at the end of the resume block, exactly where `__sub_k` used to.

### The test that matters, and why the obvious one is worthless

**An address comparison cannot see this bug.** A future that records `&this.field` on each poll and
compares reports "stable" under BOTH lowerings — a driver re-entering `poll` from the same loop reuses the
same stack region, so the block-local lands at the same address every time. That probe was written first
and reported `subfuture_moves=0` against the pinned compiler too; it is recorded here because it is the
trap this class of bug sets.

What does discriminate: a **third party writing through a pointer the sub-future published, while the
sub-future is suspended**. `lang/async_future_address_stable.cryo` drives the future by hand (`block_on`
offers no hook between polls) and writes `55` through the published pointer at every suspend. **New
compiler: 55. Pinned compiler: 0** — the write landed on the dead stack copy and the next poll's
`take().unwrap()` overwrote it. Two more tests count destructor runs to guard the release path: one per
loop iteration, so removing the `Ready`-path take shows up as a short count rather than a silent leak.

`make test`: **1883 / 163 / 14, 0 failed.**

`emit_recv_refresh` is NOT yet dead and was not removed. It re-addresses the receiver a sub-future points
AT, and a carried receiver still lives in a block-local taken from its field on every poll — that is
increment 4 (the uniform in-place carrier), not this one. Its comment has been corrected to say so rather
than claiming the frame itself may have moved.

---

## 2026-07-28 (later still) — increment 2 LANDED: E0459, a polled future may not move

Jake's fork-1 answer implemented: **no `Pin` type**. `Future::poll` is untouched (`mut &this`), no `Unpin`
analogue exists, and no hand-written future changed — **zero API break**. Address stability is enforced by
a new move-check rule instead.

**The rule.** `MoveChecker` gains a `polled` set beside its `moved` set. A `<local>.poll(cx)` on a receiver
whose type implements `Future` marks that local polled; moving or copying it afterwards is
**E0459**. Before the first poll nothing points inward yet, so the construct-then-hand-over moves stay
legal — `block_on(f)`, `spawn(f)`, `Futures::join(a, b)` all take a future by value and are how one is
meant to be handed over. The rule is "not after a poll", not "never".

**Two things that were NOT obvious and would each have made the rule useless:**

- **It cannot ride on the move-set.** `handle_ident` returns early for `Copy` types, and a generated state
  machine whose fields are all scalars **is Copy** — so the first version compiled, ran, and detected
  nothing at all. Worse, Copy is not a reason to allow it: copying a polled future is precisely the address
  change the rule exists to stop. The check therefore sits in `check_polled_move`, called from
  `walk_expr_move` on every storage-duplicating site AND on every non-autoref call argument, ahead of the
  Copy guard.
- **The `Future` question must be asked of a concrete type.** MoveCheck runs *after* monomorphization
  (`pass_registry.cryo:311-315`), which is what makes `TraitChecker::type_implements_trait` answer at all —
  and what stops a user type that merely has a method named `poll` from being caught. A pointer or
  reference receiver is excluded up front: moving the POINTER does not move the future.

**Deliberate conservatism, recorded so it is not mistaken for an oversight.** The polled set is
append-only — never truncated by `restore_moves`, never revived. Accumulating across sibling branches IS
the union a join needs; the only imprecision bought is that a poll in one branch is visible to a move in
the other, over-reporting on code that polls and moves the same future in mutually exclusive branches.
That is the safe direction and it keeps this out of `merge_branches`, whose meet-over-paths reasoning has
already produced one double-free bug.

**Known limit:** only LOCALS are tracked, so `this.a.poll(cx)` in a combinator marks nothing. The
combinators do not move their children, and a field is not a binding this pass can see moved — but a rule
covering fields would need partial-move-style tracking and is not attempted here.

Coverage: `negative/E0459_future_moved_after_poll.cryo`, plus a positive
`future_may_be_handed_over_before_its_first_poll` stating the boundary from the other side. **Zero false
positives across stdlib, tests and projects** — the full suite was run specifically looking for E0459 in
the output before any test was added. `make test`: **1884 / 164 / 14, 0 failed.**

---

### 2026-07-28 — In-place carrier: design validated, then BLOCKED on a give-away hole; groundwork landed

**Mission was increment 4** (make a droppable local carried across an `await` live in its field instead of
being taken into a fresh block-local on every poll). It is **not done.** What follows is what was
established, what landed, and exactly what stops it — none of it inherited, all of it reduced here.

**The discriminating probe exists and is red.** A carried aggregate that publishes a pointer into its own
storage from one of its own methods, with a third party writing `55` through that pointer while the future
is parked, reads **0**. Nothing in it takes an address syntactically, so `reject_frame_addr_carry` never
sees it: it compiles today and is wrong today. (Kept out of the suite deliberately — a knowingly-red test
would break `make test` for Jake, and there is no expected-fail registry for unit tests; `known_fail_canary`
is a whole project driven with `fails: true`.)

**The stated first design step was the wrong question.** The handoff framed it as "how do we emit
flag-gated drops for a bare `T` field". We do not need any: keep the field as `Option<T>` and stop *moving*
the payload — reach it in place through `Option::unwrap_ptr`, exactly as increment 1 already does for
sub-futures. The discriminant IS the init flag, `Option`'s glue already drops conditionally, generated
futures still declare no `drop` (so `task_drop_thunk`'s assumption holds), `take()` on an emptied field is a
harmless no-op which makes a release idempotent, and `subst_to_local_deref` already exists as the
substitution spelling. Re-assignment must take-then-store (an assignment does not drop what it replaces),
and a release before every `Poll::Ready` return preserves today's drop timing.

**What the corpus actually contains** (whole test suite, instrumented): **175 carried aggregates, 435 use
sites** — 239 plain read-carry, 21 re-assignment, and **58 give-away** (40 declaring states + 18 later
uses). Give-away is the owned-handle I/O idiom `net/ws` and `net/http` are built on, not a corner case.

**Why it is blocked.** Under take/hand-back, a give-away site that `last_use_consumes` misses fails LOUDLY
(`unwrap` on `None`). Under in-place residency the same miss is a **silent double free** — the field still
owns the value and so does the callee. Proven synchronously, no async involved:

```
consume(*p)  ->  drops = 2      // silent double free
consume(a)   ->  drops = 1      // move tracked correctly
```

So the whole construction would rest on `last_use_consumes`, a syntactic last-mention heuristic in the same
family that has produced a soundness bug in each of the last three sessions. **Jake's call: fix that hole
first**, so a missed give-away is a compile error instead.

**The attempt, and the precise reason it was reverted.** A `check_deref_move_out` in `move_check.cryo`
(reject a by-value move out of `*p` when the pointee needs drop) works on user code but misfires on the
async lowering's own `*this$recv` receiver rebinding — 16 occurrences, all synthetic. Root cause:
`read_call` classifies an argument as a borrow via `!arg.autoref`, and `flag_autoref_from_params`
(`sema/call_resolver.cryo`) sets `autoref` only for **reference** parameters, never **pointer** ones. So
`mem::swap<T>(a: T*, b: T*)` gets `autoref == false` on arguments it never moves. **`!autoref` is not a
sound proxy for "passed by value".** Making it sound means giving MoveCheck the callee's parameter types
and classifying each argument from the parameter — and changing `drop_insertion`'s `read_call` in LOCKSTEP,
since its comment states the two passes must track the identical move set. That is the next increment; it
was not rushed, and the partial rule is out of the tree.

**Two pre-existing defects found on the way, both independent of async:**

1. **`consume(*p)` silently double-frees** for any droppable `T` (above). Latent today only because every
   site that does it is one where the source is immediately overwritten, freed, or leaked.
2. **`subst_name_expr` dropped `autoref`.** Substitution builds a fresh node, which defaults `autoref` to
   false, so any argument slot sema had marked was silently downgraded to "by value" while codegen still
   passed the address — a standing disagreement between MoveCheck/DropInsertion and codegen. **Fixed.**
   Same class as `TryExprNode.resync_operand`: a recorded axis goes stale when a pass rewrites the node
   under it.

**What landed (green, uncommitted).**

- **`core::mem::take_ptr<T>(src: T*) -> T`** — the explicit destructive pointer read. Copies bytewise into
  fresh storage (`mut result: T;` + `memcpy`, the `transmute` trick) rather than reading `*src` by value,
  which keeps the source out of move-checked position and means the primitive needs **no exemption, no
  directive, no intrinsic**. This matters beyond the rule that motivated it: `*p`-by-value had quietly
  become the stdlib's sanctioned escape hatch, with several sites carrying comments saying the pointer
  indirection was chosen *specifically* to get past the move checker. That convention is now a named,
  greppable function stating its own contract.
- **10 migrations** to it: `mem::swap`, `array::sort`'s inline swap, `hashmap` insert/remove, `reactor`
  take_waker/swap_waker, `mpsc` recv/try_recv, `executor::task_drop_thunk`, and `compilation_context`
  (sound only because its config box is deliberately leaked). Measured radius of flagging the idiom was
  **13 stdlib + 1 compiler + 0 test** sites — all sound, none a live bug.
- **The `autoref` preservation fix** in `async_lower.cryo`.

`make test`: **1884 / 164 / 14, 0 failed** — the baseline exactly.

**Next session starts here:** replace the `!autoref` proxy in `move_check::read_call` and
`drop_insertion::read_call` (lockstep) with real parameter-type classification; then re-land
`check_deref_move_out`; then increment 4b as designed above. The probe in the handoff is the acceptance
test — it must read **55**, and it must still read **0** under the pinned compiler.

---

### 2026-07-28 (later still) — the give-away hole CLOSED: sound argument classification, and `unsafe` given teeth

The in-place carrier's blocker is gone. Increment 4b itself is **not** done — the probe still reads `0` —
but everything it was waiting on is in the tree and green.

**1. Argument classification is sound.** A new `ArgBinding` axis (`Unclassified` / `ByValue` /
`Borrowed`) on `ExpressionNode`, set in sema at the one point that holds the callee's parameter types
(`classify_args_from_params`, formerly `flag_autoref_from_params`), and consumed by **all four** argument
loops — `read_call` and `read_new` in both `move_check` and `drop_insertion` — through one shared
`walk_argument` per pass. `!autoref` is gone as a by-value proxy: it is a *codegen directive* meaning
"take this argument's address", so it fires only for `&T` and is blind to every `T*` parameter, which
made `mem::swap<T>(a: T*, b: T*)` look like a by-value transfer of values the callee never takes.
`subst_name_expr` preserves the new axis alongside `autoref`, for the reason recorded there.

**2. The two consumers need OPPOSITE defaults.** This was a real design error, made and then corrected
by measurement. An unclassified slot must read as a *transfer* for the move set and as *permitted* for
the rejection, because the costs are opposite:

| | over-report | under-report |
|---|---|---|
| move set | spurious E0452 (loud) | silent double free |
| hard rejection | breaks a correct build | the pre-existing hazard |

Getting this wrong is exactly what reproduced the previous session's `*this$recv` misfire, and the
instrumentation showed why: the async lowering rewrites `&this` into `*this$recv`, and it lands in
`mem::swap<T>(&s, this)` — a qualified generic free call, for which `resolve_call` produces **no
parameter types at all**. So the slot is `Unclassified`, not `Borrowed`. `arg_binding_unknown` carries
that distinction into `check_deref_move_out`.

**3. `check_deref_move_out` is back**, as `E0453`, and the defect it names is real. Measured on a
four-shape reduction, one value with a destructor freed **twice**, identical under the pin:

```
A_ptr_param_drops=2      consume(*p), p names caller storage
B_local_control_drops=1  consume(a)              <- the control
C_same_frame_drops=2     p = &local; consume(*p)
D_deref_to_local_drops=2 const copy: Res = *p;
```

**4. `unsafe` now means something — and `take_ptr` is deleted.** Jake's call, and the right one:
`take_ptr` was byte-identical to `*p` (`mut result: T; memcpy(&result, src, sizeof(T))`) and existed
solely to keep the source out of move-checked position — a workaround wearing a name. The distinction it
stood for is real (Cryo has no lifetimes, so `*p` cannot say whether the owner is finished with the
storage), but the place to say it is the language. All 11 call sites migrated to `unsafe` blocks.

`unsafe` is **not** a move-checking off-switch. It stands down the raw-pointer rule and nothing else; a
use-after-move inside one is still `E0452` (new negative test). The block is the right granularity
because the soundness argument usually spans more than the read — in `mem::swap` it is the store that
replaces what was taken.

**Two pre-existing defects found and fixed on the way, both independent of async:**

1. **`unsafe { }` silently disabled move checking wholesale.** `move_check::walk_stmt` had no
   `UnsafeBlockStatement` case at all, so it never descended — while `drop_insertion`, which *does* walk
   the block, went on suppressing drops for moves the checker never saw. That is precisely the
   leak-or-double-free divergence both passes' comments warn about. Proven with identical code in two
   containers: `if (true) {…}` → `E0452`, `unsafe {…}` → *No errors found*.
2. **A generic call reachable only from inside an `unsafe` block was never specialized**, so codegen
   failed to resolve the bare template name (`E0636`). `mono/call_specializer.cryo` and
   `mono/dispatch_annotator.cryo` both lacked the case; the AST cloner and substituter have it (via
   visitor overrides) and were fine. Reduced to 12 lines. This is the "add a statement kind to ALL the
   walkers" trap again, in a subsystem the async work had not touched.

**New finding: classification does not reach a qualified generic free call.**
`mem::swap<Res>(&a, &b)` leaves *every* argument `Unclassified`, because `lookup_callee_function_type`
yields nothing for that callee shape and the `ScopeResolution` fallback only handles enum-variant
payloads. Not unsound — the conservative defaults hold — but it means the sound classification does not
yet *reach* the call shape the async lowering most often generates, which weakens the very guarantee
increment 4b wants to rest on (a missed give-away becoming a compile error rather than a silent double
free). Fixing it is its own increment; deliberately not bundled.

**Gates.** `make test`: **1889 unit / 166 compile-fail / 12 projects, 0 failed** (from 1885 / 164 / 12 —
4 new lang tests and 2 new negatives). New coverage: `lang/deref_take_in_unsafe.cryo` (counts destructor
runs, so a duplication would show as an extra drop; includes the pointer-parameter-is-a-borrow and
loop-borrow shapes that the old `!autoref` proxy mis-tracked), `negative/E0453_deref_move_out.cryo`,
`negative/E0452_use_after_move_in_unsafe.cryo`.

**Where increment 4b stands.** The design is unchanged and still settled (keep the `Option<T>` field,
reach the payload in place via `unwrap_ptr`, no flag-gated drops). What is now available that was not:
a missed give-away in the generated body surfaces as `E0453` instead of a silent double free — subject
to the classification gap above, which is why closing that gap is the natural next step before the
carrier rewrite.

### 2026-07-28 (later still) — classification reaches module calls; the in-place carrier LANDS for the shapes it can express

Two things, in the order the previous handoff asked for.

#### 1. Argument classification now reaches a module-qualified call — and it was hiding a silent LEAK

The gap was wider than it had been written down as. It is not "a qualified *generic* free call": it is
**every call whose scope names a MODULE by a suffix of its namespace**, generic or not — which is how
nearly every stdlib free function is spelled (`mem::swap`, `hash::digest`, `intrinsics::frexp`).
`resolve_call` had three sources for the callee's parameter types; two ask for a type-scoped name and
the third for an enum-variant payload, so a module scope fell through all three and **every argument of
such a call stayed `Unclassified`**.

`Unclassified` reads as a *transfer* to the move passes — the right default when the answer is a move
set — so a value merely **lent** to such a function was recorded as moved and its scope-exit drop
suppressed. Same file, both compilers:

```
hash::digest<Res>(r)     // digest<T>(value: &T) — a borrow
  pinned   hash_drops=0     <- leaked
  fixed    hash_drops=1

mem::drop<Res>(*p)       // drop<T>(_v: T) — by value
  pinned   No errors found
  fixed    error[E0453]
```

**The fix** is a fourth source, `lookup_module_qualified_param_types`, resolved through the module
graph. The module-selection walk (suffix match, then arity, then longest shared namespace prefix) was
extracted out of `resolve_module_qualified_function` into `resolve_module_qualified_symbol` and is now
**shared**, so parameter-type lookup and return-type resolution cannot pick different modules —
classifying an argument against one module's signature while the call resolves to another's would hand
the move passes an ownership answer for a function that is not being called.

Verified where it mattered: instrumenting `walk_argument` and building the whole stdlib, all **16**
`*this$recv` deref-arguments report `BORROWED`, having previously reported `Unclassified`.

New coverage: `lang/qualified_module_call_arg_binding.cryo` (5 tests, all counting destructor runs) and
`negative/E0453_deref_move_out_qualified_call.cryo`. The lending test is rejected outright by the pinned
compiler (`E0452: use of moved value`) — that is its discrimination proof.

#### 2. Increment 4b: a carried aggregate lives in its field

`promote_cross_state`'s aggregate branch now has an in-place path. The value is **built directly into**
its `Option<T>` carrier field, and every state — the declaring one included — opens by binding
`mut <nm>$p: T* = this.__agg_<ds>_<nm>.unwrap_ptr();` and reads through `*<nm>$p`. No take, no
hand-back, no `store_before_suspends`. One pointer name across all states, because the state blocks
share AST statement nodes; safe here in a way an owning local is not, since the binding owns nothing.

**The acceptance test passes.** The probe is promoted into
`lang/async_carried_local_address_stable.cryo` (3 tests) and `.todo/async_carrier_address_probe.cryo` is
deleted. Same source, both compilers:

```
                      pinned      fixed
one_suspend_saw          0          55      drops=1 / drops=1
untouched_saw            0           0      drops=1 / drops=1     <- control
two_suspends_saw         0          77      drops=1 / drops=1
```

The control reads 0 under both, as it must: what discriminates is a **third party writing through a
pointer the value published while the future is parked**, never an address comparison — a driver
re-entering `poll` from the same loop reuses the same stack region, so a block-local copy lands at the
same address every time and the moving carrier reports "stable" too.

**Two `E0455` negatives were INVERTED, not deleted.** `E0455_async_pointer_to_carried_local` and
`E0455_async_pointer_from_accessor` now compile and are correct, so both became positive tests at the
end of `lang/async_pointer_across_await.cryo`, each asserting the value written through the pointer.
The other frame-address rejections still fire and are still needed.

**What is NOT in place, and why.** Two shapes still take the moving path, recognised by
`carrier_can_live_in_place`:

- a state that **gives the value away**. In place, the field owns the value outright, so a by-value use
  must **empty the field at that site** (`take().unwrap()`); a bare alias would leave the callee and the
  field both owning it.
- a state that **rebuilds** it (a write before any read, top-level or branch-nested). Publishing a
  second value has to drop the first, and an assignment does not.

Both need a position-aware substitution — one that knows whether a mention sits in a by-value or a
borrow position. `subst_name_expr`/`subst_name_stmt` have 47 call sites between them and thread no such
flag; that is its own increment and was deliberately not bundled. **Until it lands, none of the
remaining §4 deletions are possible** — `emit_recv_refresh` and the rest of the frame-address subsystem
are still live and still needed.

#### Three findings, each measured rather than reasoned

**1. §2's guarantee worked the first time it fired.** The first build with an *unconditional* in-place
carrier failed with

```
error[E0453]: cannot move a value that owns a destructor out of a pointer
  --> stdlib/net/ws/conn.cryo:211  (async recv)
```

— a give-away surfacing as a loud compile error instead of a silent double free. That is what exposed
the hole in the first eligibility predicate: `last_use_consumes` answers *"does the state still own the
value as it falls off its end"*, and in-place residency needs the stronger *"does ANY mention hand it
over"*. Hence `any_use_gives_away`, which drives the same walker with a sticky flag — a by-value mention
no longer downgraded by a later borrow, and no branch skipped for returning.

**2. Drop TIMING is load-bearing, and a test said so out loud.** Leaving the value in its field defers
its destructor to the future's own drop. `NetTcpConn::tcp_conn_reports_a_truncated_record_as_eof`
**deadlocked** on that: the sending half ends `return 1;   // dropping c here closes the socket
mid-record`, and with the close deferred the receiving half waited on an EOF that never came. So the
release is not cosmetic and is now implemented — `release_before_ready` rewrites each completion return
to

```
mut <rv>: <Output> = <value>;
this.<field>.take();          // one per in-place carrier
return Poll::Ready(<rv>);
```

Three things about that shape were each learned by breaking it:

- **The payload must be HOISTED first.** Releasing before it is evaluated empties the very field the
  value is read through, and the address-stability probe went straight back to `0`.
- **The PAYLOAD is hoisted, not the whole `Poll::Ready(...)`.** Binding the wrapper needs to spell
  `Poll<Output>`, and the lowering's `poll_out` is the bare `Poll` in generic and trait-method contexts
  — fine as a return expression, not as a declared local type.
- **The release is a DISCARDED temporary.** Binding it to `mut <tmp>: Option<T>` made the declaration
  the authority on the type and sema then resolved the `take` call to a different `Option`
  instantiation. See finding 3.

The only payload released *ahead of* the return is a bare literal, which names no storage. Asking
instead whether the payload mentions a carrier pointer is a **different and wrong** question: a pointer
derived from the carrier earlier in the function (`p = owner.conn()`) reads that storage without naming
it, and the accessor test returned 2 instead of 3. A payload whose type is still abstract is not hoisted
at all and simply keeps the old timing — the previous behaviour, not a new hazard.

**3. NEW, pre-existing, and NOT an async defect: `Option<u8[8]>` and `Option<u8[]>` collide.**
Instantiating `Option` over both a sized and a dynamic array in one compilation unit produces
`expected u8[8]*, found &u8[]` from inside `option.cryo`. **Reproduces identically under the pinned
compiler**, with no async involved:

```
mut a: Option<u8[8]> = Option::Some([0; 8]);   mut ta: Option<u8[8]> = a.take();
mut b: Option<u8[]>  = Option::Some([1,2,3]);  mut tb: Option<u8[]>  = b.take();
```

Either alone is fine. It only surfaced now because carrying a `u8[8]` across an `await` used to be
rejected outright, so the instantiation was never reached. The inverted test wraps its array in a struct
to avoid provoking an unrelated bug; the shape itself is untouched. Type identity for array types is its
own increment.

## 2026-07-29 — the in-place carrier is UNIVERSAL: `carrier_can_live_in_place` is gone

**Every carried aggregate now lives in its `Option<T>` carrier field for the whole life of the future.**
The moving carrier — copy the value out into a block-local at the top of each state, hand it back on the
way out — is deleted, not merely bypassed. `make test` **1903 unit / 0 failed, 165 compile-fail, 14
projects** on Linux (1898 before, +5 new tests).

### What made the two remaining shapes expressible

`subst_name_expr` / `subst_name_stmt` now carry a **`by_value` position flag**, mirroring the positions
`mark_last_use_expr` already distinguishes for the eligibility walk — the two must agree, or a mention
counted as a give-away by one is aliased by the other. Three spellings come out of it:

| position | spelling |
|---|---|
| reaches into the value (receiver, field, address-of, operand, condition) | `*<nm>$p` |
| hands it over (by-value argument, initializer, `return`, `match` subject, struct-literal field, `await` operand) | `this.__agg_<ds>_<nm>.take().unwrap()` |
| rebuilds the whole value (`<nm> = <new>`) | `this.__agg_<ds>_<nm> = Option::Some(<new>)` |

A **by-value argument is classified from `arg_binding`, and an `Unclassified` slot reads as a BORROW** —
the opposite default from the move passes, deliberately. Over-reporting empties the field for a parameter
that only borrows and strands the next `unwrap_ptr` on a `None`; under-reporting surfaces as a loud
`E0453` at compile time. That asymmetry is the whole safety argument for the default.

The take is armed **only when the carrier's type can be given away at all** (`carried_can_be_given_away`).
A value with no destructor is COPIED by a by-value use, so emptying its field would strand every later
state. The republish is armed unconditionally — it is correct for both.

**Rebuild timing.** A state whose FIRST touch of the name is a top-level assignment may find the field
EMPTY (it is the state that refills it — the other half of the owned-handle idiom), so its pointer is
bound *after* that store rather than before. The index is read BEFORE substitution rewrites the
assignment into the store. Same treatment for a declaration with no initializer, which now opens the
field as `Option::None`: the discriminant IS the init flag.

**A republish does not drop what it replaces, and that is correct.** Verified against the language rather
than assumed: `mut r: Res = Res::of(1); r = Res::of(2);` reports `drops=0` at the assignment and `drops=1`
at scope exit. Overwriting a binding does not run the old value's destructor, so storing a second payload
the same way keeps the lowered code faithful to the source.

`bind_carrier_ptr` skips the pointer binding entirely when substitution left no reader — a state whose
every mention was a give-away or a republish. Not merely untidy: synthesized nodes carry the async
function's own span, so an unused local is reported by the dead-code lint against the user's source.

### Three root-cause fixes it needed, none of them async-specific

1. **A synthesized variant constructor left its payload `Unclassified`.** `Option::Some(x)` and
   `Poll::Ready(x)` are built by the lowering and never pass through `classify_args_from_params`, so the
   axis was unset and each consumer guessed differently. Both now go through one `variant_call1` helper
   that classifies the payload `ByValue` — which is what a variant payload slot actually is.

2. **`MethodInfo.param_types` is by contract the NON-SELF parameters; trait registration included the
   receiver** (`passes/type_resolution.cryo`). An instance trait method therefore looked like it took one
   parameter too many, so `lookup_method_param_types` matched nothing and every argument of a call on a
   bounded receiver stayed `Unclassified`. Fixed to filter the receiver exactly as the struct and class
   method tables already do. Only two readers exist and both want the non-self list.

3. **Argument classification did not reach a bare generic parameter bounded by a WHERE clause.** `S` in
   `where S: AsyncTransport` stays a plain `GenericParam` whose bounds live on the enclosing function/impl
   node, so `this.inner.write_from(buf)` found no signature. New
   `MethodBinding::param_types_through_param_bounds` — the parameter-list sibling of the existing
   `lookup_method_through_param_bounds`, sharing its bound-scanning shape rather than mirroring it.

E0453 was the guide throughout, exactly as the previous session predicted: each miss surfaced as a loud
compile error naming the site, never as a silent double free.

### FINDING: the frame-address subsystem is NOT dead. Do not delete it.

The previous handoff scheduled `reject_frame_addr_carry` / `frame_addr_root_expr` / `addr_place_root` /
`call_frame_addr_root` / `place_leaves_frame` and `emit_recv_refresh` for deletion once the carrier went
universal, and both `E0455_async_pointer_outlives_local` and `E0455_async_address_into_awaited_future` for
inversion into positive tests. **That premise is wrong, and inverting those tests would make the compiler
accept genuinely unsound code.**

In-place residency applies only to values that are actually CARRIED, and a value is carried only when a
later state READS it (or it is a droppable local a borrowed view was taken from —
`borrow_outlives_suspend`, i.e. `needs_drop`). Both negatives are the opposite shape: the addressed local
is *never read after the suspend*, so it is not carried at all and its storage still dies with the poll
frame. The first test's own comment says exactly this — *"the check must key on the pointer being live
across the suspend, not on whether its referent happens to be carried"* — and it is still right.

`emit_recv_refresh` survives for the same reason: a receiver with **no destructor** whose only mention is
the awaited operand fails the `needs_drop` gate at `async_lower.cryo:4669`, so it is never carried, stays
frame-resident, and still needs re-addressing each poll.

**The checks are already precisely calibrated, and the payoff is already collected.** `addr_place_root`
stops at the first indirection a place passes through, so once a value IS field-resident the substitution
turns `&c.canary` into `&(*c$p).canary`, which is rooted in the future's own storage and correctly
allowed. Proven end-to-end, not argued: a probe taking the address syntactically returns `got=99
drops=1`. New test `source_taken_address_of_carried_local_survives` pins it.

### New tests — and they discriminate

`lang/async_giveaway_carrier_address_stable.cryo` (4 tests) covers the two shapes that were on the moving
path, each paired with a control that drives the same future without writing, each asserting the
destructor count. Verified against **both** compilers, which is the bar this class of bug keeps failing:

```
                 OLD pin (56894861)              NEW
giveaway         seen=0   drops=1        seen=55  drops=1
rebuild          seen=0   drops=1        seen=77  drops=1
```

The drop counts are identical under both, so the tests are measuring address stability and nothing else.

### Gates

`make test` **1903 / 165 / 14, 0 failed**. `make selfhost-check` **TWO `FIXED POINT OK`** — this session
ran on a Linux container where the Windows 6-stage chain DOES complete under wine (unlike the WSL box of
2026-07-28, which lacked `wine32`). Plain `make pin` taken with both halves built (`mingw` +
`.toolchains/llvm-win` present), `make verify-pin` OK, sidecars a matched pair at `004c7918-dirty`.

The pinned binary is proven to BEHAVE, not just to be named right: `bin/cryo` itself compiles the
give-away/rebuild probe to `55`/`77` with `drops=1` (the previous pin gives `0`/`0`), compiles the
syntactic-address probe to `99`, and still fires `E0455` on both async pointer negatives.

Dead code removed in the same increment and re-gated: `any_use_gives_away` lost its only caller when
`carrier_can_live_in_place` went, and `mark_giveaway_sticky` existed only to serve it. Both deleted;
`mark_last_use_stmt` no longer has a sticky mode. `last_use_consumes` / `needs_handback` stay — the
PARAMETER carrier and the match-binding carrier still use the moving protocol.

### Not done

- `net/http2/{client,server,connection}` still unported — now genuinely unblocked, since
  `connection.cryo` borrows its transport and that is exactly what in-place residency makes tractable.
- `mark_last_use_expr` still marks EVERY call argument `by_value=true`. The previous handoff flagged this
  as worth correcting from `arg_binding`, but it is no longer the shared hazard it was: its only
  remaining consumers are `last_use_consumes` / `needs_handback`, which serve the two carriers that still
  move. Correcting it would change THEIR hand-back decisions and buys nothing for the in-place carrier,
  which reads position directly.

### OPEN: use-after-give-away inside ONE state is no longer a compile error

With the value in a field, a by-value use becomes `this.<field>.take().unwrap()` and a later borrow
becomes `*<nm>$p` — two unrelated expressions to MoveCheck, which runs long after the lowering. So:

```cryo
mut c = make();
foo(c);        // give-away  -> take().unwrap()
bar(&c);       // same state, after the move -> &(*c$p), reads moved-from storage
await ...;
```

no longer reports `E0452`. Scope is narrow — it needs the give-away and the reuse in the same state, i.e.
with no `await` between them — and the CROSS-state form is unchanged (both old and new lower to an
`unwrap` on `None`, a loud panic). But it is a real loss of a real check and it is not a workaround away:
once the value lives behind an `Option` reached by method calls, flow-sensitive move tracking of it is
gone by construction. The two defensible fixes are (a) hoist the take into a named local at the give-away
site so MoveCheck sees an ordinary move — sound, but hoisting out of an arbitrary expression context
(`return`, nested call argument, match arm) is real work; or (b) reject a mention that follows a
give-away in the lowering, with branch-precision so a give-away on a returning path does not
false-positive. **This is a soundness-contract call and it is Jake's.**

## 2026-07-29 (later) — array LENGTH is part of a type's identity: `T[N]` vs `T[]` no longer collide

Jake's named ask, and it was **pre-existing and fully synchronous** — not an async defect.

### The defect

`MangleContext::encode_type_ref` encoded every array as `A` + element and threw `a.size` away, so
`u8[8]`, `u8[16]` and `u8[]` all encoded as `Ah`. `specialized_identifier` — *the* key for every
monomorphized type and function — is built from that encoding, so the three types shared ONE key: one
specialization, and whichever instantiation was registered first answered for all of them.

Twelve lines reproduce it under the old pin; either declaration ALONE is fine:

```cryo
mut a: Option<u8[8]> = Option::Some([0; 8]);
mut ta: Option<u8[8]> = a.take();      // error[E0200]: expected `Option<u8[8]>`, found `Option<u8[]>`
mut b: Option<u8[]>  = Option::Some([1, 2, 3]);
mut tb: Option<u8[]> = b.take();
```

The arena was never at fault: `get_array_of` interns on the raw two's-complement `size` bits, so the
three types were always three distinct `TypeRef`s — the diagnostic even printed them differently. Purely
a key collision.

### The fix — two sites, in lockstep

- `resolver/mangled_name.cryo`: a FIXED array encodes as `A$L<N>$G` + element; a dynamic `T[]` keeps the
  bare `A` + element. The length is bracketed in the existing `$L`/`$G` pair rather than written bare
  because no type code may begin with `$`, so the form is self-delimiting and round-trips — a bare `A8h`
  could not be told from an element code beginning with a digit.
- `resolver/demangler.cryo`: the mirror. `A$L<N>$G<elem>` renders `elem[N]`, bare `A<elem>` renders
  `elem[]`.
- `docs/cryo-mangling-spec.md`: both rows updated.

Keeping the dynamic form byte-identical is what made this cheap: **the stdlib's symbol set is unchanged.**
Verified, not assumed — `nm` over `libcryo.a` built by the old pin and by the new compiler diffs to
**zero lines** (7905 symbols), because the stdlib never puts a fixed-size array in a mangled position.
So there was no bootstrap hazard and no 2-phase repin was needed.

### The `-2` fixed-pending trap, reasoned about and closed

A `T[NAME]` parses with `size = ArrayAnnotation::fixed_pending_size()` (-2) until its size expression is
folded. Had that reached the mangler, a length-bearing key would split ONE type across two keys
(pending vs resolved) and turn a collision bug into a "no specialization found" bug.

It cannot. `arena.get_array_of` is the only constructor of an arena `ArrayType`, and the only annotation
path into it (`types/resolver.cryo:281`) goes through `TypeResolver::array_size_of`, which folds `-2` to
a concrete length or reports E0239 rather than leaving it pending. The parser always pairs the `-2`
marker with a `size_expr`, so the one pass-through branch is unreachable in a program that compiles. The
other `get_array_of` callers are substitution walkers that copy an existing arena size. The reasoning is
recorded in the comment at the encode site. `ArrayType::fixed_pending_size()` is defined but read by
nothing — left alone deliberately; `is_dynamic()`'s grouping of -1 and -2 was NOT touched.

### Tests — and they discriminate

`tests/lang/array_length_type_identity.cryo`, 7 tests asserting lengths and element values rather than
merely compiling: fixed+dynamic together, two fixed lengths (`u8[8]` vs `u8[16]` — the case Jake named),
all three spellings at once, the same collision through a user generic rather than `Option`, plus three
controls — same length different elements, one length written twice (must stay ONE type, guarding against
over-splitting), and a zero-length `u8[0]` (so `0` is not confused with the dynamic marker).

The file draws **17 errors under the old pin** and passes 7/7 under the new compiler. Round-trip proven
on real generated symbols: `takes_fixed` → `A$L8$Gh`, `takes_big` → `A$L16$Gh`, `takes_dyn` → `Ah`, and
`cryo demangle` renders `u8[8]` / `u8[16]` / `u8[]` respectively.

### Gates

`make test` → **1910 unit / 0 failed, compile-fail 165, projects 14** (Linux; 1903 baseline + 7 new).
Roster merged with `--merge`, 1911 entries, the Windows-only entry kept.

### DECIDED by Jake — the E0452 gap gets fix (a)

The use-after-give-away-inside-one-state gap from the entry above is to be closed by **hoisting the take
into a named local at the give-away site**, so MoveCheck sees an ordinary move and its existing
flow-sensitive tracking reports E0452 unchanged. Rejected: re-implementing flow reasoning inside the
lowering, which would be a second, weaker move analysis in a different pass.

## 2026-07-29 (later still) — the E0452 give-away gap is CLOSED for the straight-line case

`d32f4771`. Jake's decision above, implemented — with one correction found before writing any of it.

### Hoisting ALONE does not close the gap

A by-value mention becomes the take, but a BORROW mention returns `subst_target`, which in
`subst_to_local_deref` mode is `*<nm>$p` **unconditionally**, with no knowledge that a give-away already
happened. So `foo(c); bar(&c);` would lower to `mut __gv = ...take().unwrap(); foo(__gv); bar(&(*c$p));`
— the borrow never mentions the hoisted local and MoveCheck still cannot connect them. The hoisted local
has to BECOME the representation for the rest of the run, which also fixes a latent bad READ: after a
take the field is empty, so `*c$p` was reading emptied storage. Jake approved the expanded version.

### What landed

- `hoist_giveaway` records `mut <nm>$gv<N> = this.<carrier>.take().unwrap();` for the enclosing statement
  list to splice in ahead of the current statement, and returns the local in the mention's place.
- `subst_hoist_name` then stands in for the value in **every** position, so a second by-value use is an
  ordinary double move and a later borrow an ordinary use-after-move. `E0452` comes from the existing
  pass rather than being re-derived.
- A whole-value rebuild republishes the field and CLEARS the hoist.
- `subst_can_hoist` is a structural guard: a hoist exists only while `subst_stmt_list` is the immediate
  driver, i.e. exactly when a list is standing by to receive the declaration.

**Two conservative limits, each forgoing an improvement and neither able to break working code:** a
statement containing an `await` is skipped (it is torn across state blocks), and the rebinding never
crosses into a nested construct. The conditional and cross-state forms are therefore exactly as before —
cross-state remains a loud `unwrap`-on-`None`, not silent.

### The bug that nearly killed it, and the belief it corrected

The first build failed the stdlib with `cannot find value 'got$L17$gv3'`. I attributed it to
`prepend_agg_take`'s documented *"state blocks share AST statement nodes"* and reverted. **Measured, that
was wrong here:** dumping the address of every top-level statement in each state block of
`serve_connection` gave all-distinct addresses across blocks 1, 5, 6, 7.

The real cause was in the new helper. `subst_stmt_list` nulls the pending-declaration slot before each
statement, so when the `match (got)` subject hoisted and the walk descended into an arm body, the INNER
`subst_stmt_list` cleared the OUTER statement's pending declaration — dropped, while `m.subject` had
already been rebound to it. Fixed by saving and restoring the caller's pending slot across the walk.
Lesson recorded in the handoff: instrument before theorising, and **stash rather than revert**.

### Diagnostic quality

The first cut reported at `async function drive()`, because every synthesized node inherits the async
function's span and MoveCheck reports a move at the span of the node that made it. The hoisted
declaration and the rebound identifier now carry the **mention's** span, so the error lands on the
offending line with the caret under the value.

### Tests

`negative/E0452_async_giveaway_then_borrow_same_state.cryo` (0 diagnostics under the old pin), and
`lang/async_giveaway_use_after_move_controls.cryo` — five shapes that must all still compile AND run with
asserted drop counts: give-away with no later use, borrow-only, give-away then rebuild, and give-away on
one branch driven both ways. The controls are the load-bearing half: over-rejecting is the easy way to
make the negative pass and the wrong one.

### Gates

`make test` → **1915 unit / 0 failed, compile-fail 166, projects 14** (Linux). `make selfhost-check` →
exit 0, **two** `FIXED POINT OK`. `make pin` taken, both halves, sidecars a matched pair at
`5de98212-dirty` (the pin predates Jake's commit of this work; content is correct).
**`make verify-pin` was NOT run** — the harness's shell went unavailable at that moment. It is the first
action in the handoff.

## 2026-07-29 (later still) — `net/http2` ported; the blocking surface is GONE; one compiler bug root-caused

`net` is now async end to end. The last consumer was `net/http2/{client,server,connection}`.

### The shape, and why each part of it is forced

`Http2Connection<S>` **owns** a `BufStream<S>` where it used to hold `inner: S*`. Owning is not a
preference: the type carries per-connection protocol state (the HPACK tables, both flow-control windows,
the next stream id), an `async` method may only re-address its OWN receiver between polls, and handing a
caller's local address to a future that outlives the current poll is refused outright (E0455). A borrowed
transport would be a second address the state machine has no way to refresh. Same conclusion `WebSocket<S>`
reached, so `http`, `ws` and `http2` now present ONE connection shape.

Reads are `async` inherent methods on `BufStream<S>`, declared from the http2 module — `read_h2_frame`,
named apart from `ws::frame`'s `read_frame` because `BufStream<S>` hosts the framing for three protocols
and two inherent methods cannot share a name on one type. The nine header bytes are copied into scalars
and consumed before the payload's suspension, so no `buffered()` view is live when a fill can move it.

The whole write side is **synchronous**: `queue_settings`, `queue_header_block`, `queue_ack_data` and
`process_control` only append to `pending()`. Two things fall out. A HEADERS block and every CONTINUATION
fragment it needs are assembled with no suspension between them, so a peer can never see a header block
interleaved with anything else. And `process_control` — which owes the peer a SETTINGS or PING ack — no
longer needs its borrowed `&Frame` to survive a suspension: it queues, the caller drops the frame, and the
caller flushes. Every read loop calls it without one.

`send_body` takes the body **by value**. A `Slice<u8>` into the caller's array would be a borrowed view
crossing every iteration of the window wait; owning the bytes means the slice is re-derived from this
future's own storage after each suspension and consumed by a synchronous queue before the next. It also
flushes unconditionally, including for an empty body, because the caller's header block is queued behind it
— which is what lets `request`/`send_response` cost ONE suspension for a bodyless message.

### Deleted

`TcpStream::connect`, `implement trait Read/Write for TcpStream`, both impls for `TlsStream`,
`TcpListener::accept`, and `TcpStream::set_read_timeout`/`set_write_timeout` — those last two existed only
to bound a blocking read, and documented an effect that is now impossible. Plus the transitional HTTP/1.1
duplicates: `Headers/Request/Response::write_to`, `Request/Response::parse`, `request::read_line`.
**`TcpListener::bind` is KEPT**, per Jake: binding does not wait.

### The compiler bug: an owning argument to an async method of a generic owner

The port hit four `E0453` "cannot move a value that owns a destructor out of a pointer", every one reported
at the enclosing `async` function's HEADER rather than at any statement — the synthesized-node span
landmine again. Bisecting by construction across six probes isolated the axis exactly:

| owner | callee | result |
| --- | --- | --- |
| non-generic | method on `this` | compiles |
| generic | free function | compiles |
| **generic** | **method on `this`** | **E0453** |

Not the `where` bound, and not method-vs-free-function: the combination. `cdebug` on
`lookup_method_param_types` showed the receiver's type arriving as `TypeKind::InstantiatedType` and falling
into the final `else { return [] }`. Peeling to `generic_base` was not enough — the TEMPLATE's arena node
carries **zero** methods (`probe5::Gen` id 206, 0 methods; `Gen<TcpStream>` id 2550, 8), because
`TypeResolution` skips generic declarations outright: their field and parameter types still mention unbound
parameters and baking those in would size the type wrongly.

So `classify_args_from_params` was never reached and every argument of `this.m(x)` inside a generic owner's
body stayed `Unclassified` — and `async_lower.cryo` says in its own comment that the two consumers of that
default disagree deliberately: the substitution reads an unclassified by-value argument as a **borrow** and
emits a pointer read, "whereas leaving an owning argument aliased surfaces as `E0453` at compile time".
**The diagnostic was the designed signal that sema had a hole**, so the fix belongs in the classification,
not in http2 and not in the lowering.

`param_types_from_template_decl` reads the signature from the template's own AST through the
`GenericRegistry` — its declaration body and any `implement` block registered against it. The types may
still mention the template's parameters, which is fine for the only question asked here: whether a
parameter is a reference or a pointer, which substitution never changes. A parameter that has not been
resolved comes back invalid, which the caller already skips, so such a slot is left exactly as unclassified
as before. This is the same failure, cause and fix as the `GenericParam` arm one level out, which
`f9e7eebc` added for the case where the receiver IS a generic parameter.

### Gates — 2-phase repin, because the stdlib now needs the fix

Phase 1, the compiler alone against a clean stdlib: `make test` **1920 unit / 0 failed**, compile-fail 166,
projects 14; `make selfhost-check` exit 0 with **two** `FIXED POINT OK`; `make pin` both halves,
`make verify-pin` OK, sidecars a matched pair at `d32f4771-dirty`; and the pinned binary itself proven by
compiling the repro and running it — `a=6 b=11 c=6 d=6 e=6 drops=5`, one drop per shape.

Phase 2, the port: `make test` **1924 unit / 0 failed**, compile-fail 166, projects 14.

Regression test `tests/lang/async_generic_owner_byvalue_arg.cryo`: 2 `E0453` under the old pin, green after,
with the controls carrying the weight — a non-generic owner, a free callee reached from a generic owner, and
a BORROWED argument that must not read as a move. Drop counts asserted throughout, because classifying a
borrow as a transfer is a double free and the reverse is a leak, and neither is a compile error.

**A control I got wrong, worth recording:** the first borrow control was `await this.peek(&r)`, which E0455
rightly refused — an address may not be handed to a future at all. An `async` borrow control cannot be
written; it has to be a synchronous borrow plus a carried read after the suspend.

### Testing the port — 5 h2c shapes over REAL loopback sockets

Both ends run as two futures joined inside ONE `Executor` on an ephemeral port. A mock could not have
carried this: the `ws` port passed every hermetic mock test and then hung on loopback.

- a plain GET: preface, SETTINGS handshake, HPACK, empty body;
- **100 KB echoed both ways** — DATA split across many frames and, because the connection-level send window
  is fixed at 65535 octets regardless of SETTINGS, an `ensure_send_window` wait that must read and apply the
  peer's WINDOW_UPDATEs mid-send. Every byte verified against a prime-stride pattern;
- a header block past the 16384-octet max frame size: HEADERS + CONTINUATION, reassembled;
- **4 sequential requests on one connection** — the connection is an owning local awaited on repeatedly
  from inside a loop, which is the shape that hid the carrier bug this whole layer was built on;
- a client that handshakes and closes: the server's read loop must report a clean end, which is also the
  proof the client's connection was really DROPPED — a leaked one holds the socket open and the server waits
  forever.

Three of the five were initially red on their header COUNTS (2 vs 1, 302 vs 301, 8 vs 4) — all three my
arithmetic, not the protocol. The server synthesizes exactly **two** headers onto every request it rebuilds:
`host` mapped back from `:authority`, and `content-length` installed by `set_body` when the decoded body is
attached. Now a named constant, so a change in either place moves the counts and is noticed.

The three tests that exercised the deleted blocking surface were **ported, not deleted**: `net_tcp.cryo`
onto `TcpConnect`/`TcpAccept` (keeping the IPv6 `[::1]` dial path and the refused-connect case, which
nothing else covers), and `http_buffered_parse.cryo` / `net_http_response_cap.cryo` onto `BufStream` over an
in-memory `AsyncTransport`. `http_buffered_parse` now asserts a FILL COUNT — two pipelined requests in
exactly ONE transport read, with a one-byte-per-fill control — which is strictly stronger than what it
asserted before, and is the property the blocking parser could not provide at all.

Roster merged with `--merge`, 1925 entries. **`--merge` kept the renamed `loopback_h2c_round_trip` as an
"other platform" entry** — it cannot tell a deleted test from a Windows-only one — so that one line was
removed by hand and `ProcessCommand::output_large_stderr_no_deadlock_win` left alone.
