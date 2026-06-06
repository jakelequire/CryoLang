# HANDOFF — net library (UDP/DNS/TLS/WebSocket/HTTP2) + compiler bug

Date: 2026-06-05. (Root `HANDOFF.md` is a DIFFERENT task — iterator combinators —
left untouched.) Status: mid-flight on a **compiler monomorphization bug** that
blocks the HTTPS client and WebSocket. Everything below is UNCOMMITTED. The
pinned compiler `bin/cryo` is NOT refreshed; the fixed compiler lives at
`compiler/build/bin/cryo` (STAGE2).

> NOTE: a concurrent agent was editing `stdlib/random/` and `stdlib/time/`
> during this session. If `make stdlib`/`make test` shows errors in those
> modules, they are not ours.

---

## 1. Objective

Lift the deferred `net` items from CHANGELOG (`TLS, UDP, HTTP/2, WebSocket`).
Decision (with you): build UDP, a minimal DNS resolver, TLS (system OpenSSL),
WebSocket (RFC 6455), and HTTP/2 — in that dependency order. Plan file:
`/home/phock/.claude/plans/jaunty-beaming-hellman.md`.

---

## 2. What WORKS and is TESTED (green)

- **UDP** — `stdlib/net/udp.cryo` + extracted `stdlib/net/sockaddr.cryo`
  (`pack_sockaddr_in`/`unpack_sockaddr_in`/`apply_so_timeout`/`pack_i64_le`,
  moved out of `tcp.cryo`). Test: `tests/tests/stdlib/net_udp.cryo` (loopback
  datagram round-trip + connected send/recv). PASS.
- **DNS** — `stdlib/net/dns.cryo` (`resolve`/`lookup`/`resolve_one` over
  `getaddrinfo`, walks `addrinfo` by offset, `freeaddrinfo` on every path).
  Test: `tests/tests/stdlib/net_dns.cryo`. PASS.
- **TLS core** — `stdlib/ffi/openssl.cryo` (FFI; every symbol verified via
  `nm -D`; SNI via `SSL_ctrl(ssl,55,0,name)` because the macro isn't an exported
  symbol) + `stdlib/net/tls/{stream,context}.cryo` (`TlsStream` impl Read+Write,
  `TlsConnector`, `TlsAcceptor`). Drop order: `SSL_shutdown → SSL_free →
  inner.drop`. Test: `tests/tests/stdlib/net_tls.cryo` (loopback handshake +
  encrypted byte round-trip). PASS.
  - Linking: a project using `net::tls` adds `link_libs = ["ssl","crypto"]` to
    its `cryoconfig` (done in `tests/cryoconfig`). Confirmed a non-TLS binary
    (compiler, http-server example) still links with NO `-lssl`.

## 3. Compiler/stdlib fixes already made (KEEP these)

1. **Unterminated-BB sweep (real fix)** —
   `compiler/src/compiler/codegen/ops/declaration_emitter.cryo`
   `codegen_function_epilogue`. Infinite `loop{}` as last stmt + owning local
   needing drop left `loop.end` unterminated (E0633 / sometimes segfault). The
   orphaned-block sweep now also terminates **no-predecessor** blocks (not just
   empty ones) with `unreachable`. Added 2 LLVM-C bindings for it:
   `LLVMBasicBlockAsValue`, `LLVMGetFirstUse` (+ `LLVMUseRef`) in
   `compiler/llvm_bindings.h`.
2. **dp-expand null-arg guard (robustness)** —
   `compiler/src/compiler/codegen/ops/expr_ops.cryo`
   `codegen_call_direct_dp_expand`: bail on a null arg instead of
   `LLVMTypeOf(null)` segfault, so the queued diagnostic surfaces.
3. **Cross-module const patterns don't match in `match`** (stdlib fix, correct):
   `match (code) { openssl::SSL_ERROR_X => ... }` silently never fires (match
   patterns require LOCAL consts). Converted to `if`-chains in
   `stdlib/net/tls/stream.cryo` (`ssl_error`, the read/write loops) and
   `stdlib/net/dns.cryo` (`eai_to_io`). Latent compiler limitation worth a real
   fix later (path-qualified const should be a constant pattern).
4. **SIGPIPE** — `stdlib/net/tls/context.cryo` `ignore_sigpipe()` (signal(SIGPIPE,
   SIG_IGN)) in `TlsConnector::new`/`TlsAcceptor::new`.
5. **THE BIG ONE, partial** — `compiler/src/compiler/AST/substituter.cryo`
   `visit(CallExprNode*)`: reset `resolved_method=null` +
   `set_resolved_callee(SymbolStr::empty())` so a specialized body re-resolves
   its nested calls. PARTIAL — see §4.

---

## 4. THE BUG WE'RE ON (root cause + exact current state)

### Symptom
`HttpsClient.get` / `client::send_over` over `TlsStream` writes **garbage
plaintext** to the TLS socket and gets `EBADF (os=9)`. WebSocket will hit the
same. Direct `req.write_to(&tls)` WORKS; `send_over(&tls, req)` does NOT.

### Root cause (confirmed via IR — `stdlib/.bin/obj/std__net__http__request.ll`)
`Request::write_to<W>` (generic METHOD on non-generic `Request`) calls
`this.headers.write_to(writer)`. In `write_to<TlsStream>`, all the
`writer.write_all(...)` calls correctly target `write_all<TlsStream>`, **but the
nested `headers.write_to(writer)` targets `Headers::write_to<TcpStream>`** (the
WRONG spec). It passes a `TlsStream*` to the TcpStream impl, which reads
`this.fd` from the low bytes of the SSL pointer → garbage fd → EBADF + plaintext.

Mechanism: a nested generic call inside a generic method body gets its
`resolved_callee`/`resolved_method` pinned the FIRST time the outer method
specializes (for `TcpStream`, via the existing http client/server). The cloner
COPIES that pin (`compiler/src/compiler/AST/cloner.cryo:220-221`) into every
later specialization, so `write_to<TlsStream>` inherits the `<TcpStream>` pin.

### Minimal non-TLS reproduction — `/tmp/tlsdbg/src/main.cryo` (current contents)
```
trait Sink { put(mut &this, b: u8) }
struct A { fd: i32 }              impl put { this.fd += 1000 }   // "TcpStream-like"
struct B { ssl: void*; val: i32 } impl put { this.val += 1 }    // "TlsStream-like"
struct Inner { tag; relay<W>(&this, w: W*) where W: Sink { w.put(9) } }            // "Headers::write_to"
struct Outer { h: Inner; run<W>(&this, w: W*) where W: Sink { this.h.relay(w) } }  // "Request::write_to"
main: o.run(&a); o.run(&b);  // expect a.fd=1000, b.val=1
```
Build + run:
```
cd /home/phock/Programming/apps/CryoLang && make stdlib && make cryo    # builds STAGE2
cd /tmp/tlsdbg && rm -rf build && \
  /home/phock/Programming/apps/CryoLang/compiler/build/bin/cryo build && ./build/bin/tlsdbg
# See which relay each run calls:
objdump -d -r /tmp/tlsdbg/build/obj/Main.o | awk '/Outer-3run.*1A\$G/,/ret/' | grep -i relay
```

### Where the fix stands NOW
- BEFORE §3.5: only ONE `relay` spec existed; both `run<A>`/`run<B>` called it.
- AFTER §3.5: `nm /tmp/tlsdbg/build/obj/Main.o` shows DISTINCT specs
  `Inner-relay<A>`, `Inner-relay<B>`, `Outer-run<A>`, `Outer-run<B>` — progress!
- **BUT still wrong**: `objdump` shows `run<A>` CALLS `relay<B>` (should be
  `relay<A>`); `run<B>` correctly calls `relay<B>`. Runtime: `run<A>: a.fd=0`
  (wrong; want 1000), `run<B>: b.val=1` (right).

So resetting the pin made the compiler MINT both specializations, but the
re-inference of `run<A>`'s nested call picks `W'=B` instead of `W'=A`.

### NEXT DEBUG STEP (start here)
Re-inference is in `monomorphizer.cryo` `try_infer_method_call` (~line 4931),
which uses a **shared member field `this.inference_bindings`** (reset to `[]`
~line 5145). Hypotheses, priority order:
1. **Shared `this.inference_bindings` / discovery order** — `run<B>` specialized
   last; its bindings or relay-spec request leak so `run<A>`'s nested `relay`
   resolves to `B`. Check `resolve_arg_type_for_inference(w)`: in `run<A>`'s
   specialized body, is param `w`'s `resolved_type` actually `A*`? If still `W*`
   or `B*`, the substituter didn't fix the PARAM type / discovery walked the
   wrong body copy.
2. **Nested call discovered against the WRONG body copy** (template vs per-spec
   clone). Confirm `discover_inferred_calls` runs on the freshly-specialized
   `run<A>` body.
3. The mangled spec symbols are correct (nm proved distinct), so it's the CALL
   SITE's `resolved_callee` in `run<A>`'s body that's wrong, i.e.
   `try_infer_method_call` pins `relay<B>` onto `run<A>`'s relay call.

Action: add `intrinsics::printf` in `try_infer_method_call` right before it pins
the callee, printing the inferred bindings + spec symbol + which outer method is
walked. Rebuild STAGE2, run the /tmp repro, watch the order/values.

Relevant code:
- `compiler/src/compiler/AST/cloner.cryo:212-224` (CallExprNode clone — copies pin)
- `compiler/src/compiler/AST/substituter.cryo:517` (CallExprNode — our reset)
- `compiler/src/compiler/types/monomorphizer.cryo`:
  `try_infer_method_call` ~4931 (early-return `if resolved_method != null`,
  inference via `this.inference_bindings`, pin ~4429 region);
  `specialize_method` ~5404 (clones via ASTCloner, runs substituter ~5462,
  re-resolves SIGNATURE only — NOT the body's nested calls);
  `resolve_func_and_body` ~2093 + `discover_inferred_calls_in_block` ~2353.

---

## 5. CLEANUP before committing
1. Remove debug printf in `stdlib/net/tls/stream.cryo` `TlsStream::write`
   (the `[TLS::write] this=... ssl=... fd=...` line) and `import core::intrinsics;`
   there if then unused.
2. `tests/tests/stdlib/net_https.cryo` currently holds DEBUG client code (manual
   TLS + `Request::write_to` isolation). After the bug is fixed, rewrite as the
   real `HttpsClient::insecure().get(...)` round-trip (server: `Request::parse` +
   `Response::write_to`). Structure that worked: main = server (bind+listen, then
   accept), spawned thread = client; cert paths passed INLINE (NOT module-level
   `const Str`).
3. `/tmp/tlsdbg/` is throwaway scratch.

---

## 6. REMAINING TODO
- [ ] **FIX THE COMPILER BUG (§4)** — gate for HttpsClient + WebSocket.
- [ ] **HttpsClient** (`stdlib/net/https.cryo`, written) — verify full round-trip
      once fixed; finalize `net_https.cryo`; re-enable `public module net::https;`.
- [ ] **WebSocket** (`stdlib/net/ws/{frame,handshake,conn}.cryo` +
      `stdlib/encoding/{sha1,base64}.cryo` — ALL WRITTEN, module DISABLED).
      `WebSocket<S>` borrows `inner: S*` (like BufReader — a by-VALUE `inner: S`
      field SEGFAULTED the compiler at mono, separate issue). Hits the SAME §4
      bug (nested `encode_frame`/`as_bytes`); §4 fix should unblock. Re-enable,
      add `tests/tests/stdlib/net_ws.cryo` (frame round-trip + RFC 6455 §1.3
      accept-key vector `dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`).
- [ ] **HTTP/2** — NOT STARTED. HPACK (static+dynamic table + RFC 7541 App. B
      Huffman) first as a hermetic unit (App. C vectors), then framing +
      multiplexed connection over TLS+ALPN. `with_alpn_h2()`/`negotiated_alpn()`
      hooks already in the TLS layer.
- [ ] Trim `CHANGELOG.md:~201` and `stdlib/net/_module.cryo` "out of scope"
      docstring as each lands.
- [ ] Gate: `make selfhost-check` MUST stay green; `make test`; valgrind
      udp/tls/ws (OpenSSL has known still-reachable at-exit allocs — assert "no
      definitely-lost"). Then `make pin-cryo` + commit.

---

## 7. Module wiring (`stdlib/net/_module.cryo`)
ENABLED: ip, socket_addr, sockaddr, tcp, udp, dns, tls, http.
DISABLED (commented; blocked by §4): `net::ws`, `net::https`.
`stdlib/ffi/_module.cryo` has `ffi::openssl`. `stdlib/lib.cryo` has `encoding`.

## 8. Gotchas learned (save time)
- Module-level `const Str = Str::new("...")` returns EMPTY/garbage at runtime
  (fragile init). Pass literals inline. (Cost ~1h on a false cert-load failure.)
- Cross-module integer consts do NOT work as `match` patterns → use `if`-chains.
- `private function` is NOT visibility-enforced (private fns still collide
  cross-module). `rotl` collided with `random`'s → renamed `rotl32` in sha1.cryo.
- Top-level functions are public by default (no `public` keyword needed).
- Tuple literal `(a,b)`; tuple TYPE `[T,U]`; access `t[N]`.
- By-value generic struct FIELD (`inner: S`) instantiation segfaults the compiler
  at mono (LLVMTypeOf null) — use a pointer field `inner: S*`.
