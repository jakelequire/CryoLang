# IMPROVEMENTS.md — road to dogfooding a production backend in Cryo

Working checklist for closing the gap between Cryo today and a multi-tenant,
PII-holding production backend (the Rapidex control plane: `api` + `worker` +
provisioning) written entirely in Cryo. Derived from grading the stdlib and
language against a Go-reference implementation plan.

Tick items as they land. Plan refs like `(#4–6)` point at the 27-row capability
checklist from that reference plan.

---

## Architecture commitments (what makes this tractable)

These are the decisions that turn "match Go" (multi-month, unbounded) into a
bounded build. Change one and the estimates below stop holding.

- **Thread-per-request + blocking IO** as the baseline server model. libpq is
  blocking anyway, so it composes cleanly, and this scales fine to a control
  plane's load (hundreds–low-thousands of connections). No async runtime is
  required for the baseline to ship.
- **Async runtime is a separate active track.** When it lands it replaces the
  thread-per-request accept loop and adds await points at the socket/DB seams.
  Everything here is written so those seams are swappable — see §N for the
  integration contract to keep intact.
- **FFI-wrap the mature C libraries** (`libpq`, `libcrypto`, `libssl`) rather
  than hand-rolling wire protocols. `libssl` is already wrapped; the FFI +
  bindgen path is proven against raylib and a C++ callback API.
- **Function-pointer middleware + explicit per-request context**, not captured
  closures (a capturing closure into a generic fn cannot call a generic fn —
  E0636/E0458). Thread a context struct explicitly. See §L / §N.
- **Author the capability catalog in JSON**, not YAML — dodges building a YAML
  parser (the JSON stack is mature).

Effort legend: **S** ≈ 1–3 days · **M** ≈ 3–7 days · **L** ≈ 1–2 weeks.
⚠ = load-bearing for correctness/security; do not compress.

---

## What already exists (baseline — do not rebuild)

- [x] Native single static binaries, LLVM 20, self-hosted; per-target gating
      (`![target(unix|windows|linux)]`) → cross-compile story (#23, verify Linux
      cross-build from the dev host).
- [x] OS threads (pthread/CreateThread), scoped threads, `Builder`; full `sync`
      suite (Mutex, RwLock, Condvar, `mpsc`, Barrier, Once, Atomic); `Send`/`Sync`
      computed structurally with a deny-list (#2, substrate).
- [x] TCP/UDP sockets, DNS, IP/`SocketAddr`.
- [x] TLS client **and** server via OpenSSL FFI — `TlsConnector`/`TlsAcceptor`,
      cert loading, SNI, ALPN, peer + hostname verification (#8).
- [x] HTTP/1.1 server (⚠ blocking, one connection at a time), `Router` with path
      params, request/response/headers/status/method (#1, partial — see §A).
- [x] HTTP/2 (frames, HPACK, Huffman, connection, server, client); WebSocket.
- [x] HTTP client — one-shot, no pooling/retries/URL parser (#13, partial — §D).
- [x] JSON: `JsonValue`, parser, serializer, generic `from<T>`/`as<T>` (#9).
- [x] `SecureRng` — OS CSPRNG (`getrandom` / `RtlGenRandom`) (#14, verify-only).
- [x] base64 (standard alphabet, padded), SHA-1.
- [x] `process::signal` — signal **numbers** only (no handler install — see §J).
- [x] Process/Command, fs, env, time/clock/duration.
- [x] Test framework (`std::test`, `TestRunner`).
- [x] Mature C FFI + bindgen (C & C++) + `cryo vendor` (raylib-proven).

---

## A. HTTP server → concurrent (#1, #2)

- [ ] **Concurrent accept loop.** Wrap the existing blocking `handle_connection`
      in a thread-per-connection or bounded worker pool (accept → `mpsc` → pool).
      Router is read-only after setup → share by pointer. **M**
- [ ] **Full timeout set.** Read timeout exists; add write, idle/keep-alive, and
      header-read timeouts (slow-loris hardening). **S**
- [ ] **Streaming bodies.** Chunked transfer + large-body cap on both request and
      response; today it is Content-Length + buffered. **M**
- [ ] **Graceful-shutdown hook.** Stop-accept + drain in-flight; wired to §J. **S**
- [ ] Keep the accept/handle seam swappable for the async runtime (§N). **S**

## B. PostgreSQL: driver + pool (#4, #5, #6, #7, #25) ⚠ — the crux

- [ ] **libpq FFI binding.** `PQconnectdb`, `PQexecParams`, `PQprepare`/
      `PQexecPrepared`, `PQgetvalue`/`PQgetisnull`, `PQresultStatus`, `PQerrorMessage`,
      `PQfinish`, `PQstatus`, `PQreset`. Opaque `PGconn*`/`PGresult*`. **M**
- [ ] **RAII handle wrappers.** `Conn`/`ResultSet` that free `PGconn`/`PGresult`
      on drop — make leaks structurally hard (see §M soak risk). **S**
- [ ] **Typed row scan.** Column → typed field; NULL handling; JSONB comes back as
      text → hand to the JSON parser; array decoding. **M**
- [ ] **Parameterized queries only.** No string-built SQL anywhere; bound params via
      `PQexecParams`. Lint/convention to enforce (#25). **S**
- [ ] **Transactions.** begin/commit/rollback + savepoints. **S**
- [ ] **Connection pool.** Max conns, checkout/checkin, health check (`PQstatus`),
      reconnect on failure, per-conn max lifetime. **M**
- [ ] ⚠ **Per-connection `search_path`** set on checkout, reset on return — the
      schema-per-tenant isolation boundary. Must be impossible to check out a conn
      without a tenant bound. Test in §M. **M**
- [ ] **Migration runner.** Fan out versioned SQL across `control` + every tenant
      schema; transactional, idempotent (#7). **M**
- [ ] `LISTEN`/`NOTIFY` (nice-to-have). **S**

## C. Crypto (#10, #11, #12, #14)

- [ ] **libcrypto FFI binding** (already linking OpenSSL for TLS; the current
      binding is scoped to the TLS subset). Add: EVP SHA-256, HMAC-SHA256, RSA
      verify (RS256), ECDSA verify (ES256), `CRYPTO_memcmp` (constant-time). **M**
- [ ] **base64url** encode/decode (`-_`, no padding) — variant of existing base64.
      Needed by JWT/JWKS and SigV4. **S**
- [ ] `SecureRng` (#14) — **verify-only** (already OS CSPRNG). **S**
- [ ] **SigV4 request signing** (#12) — canonical request + HMAC-SHA256 chain, on
      top of the libcrypto binding. For DO Spaces / S3. **M**

## D. HTTP client hardening (#8 client, #13)

- [ ] **URL parser** — scheme/host/port/path/query (client currently takes a raw
      `SocketAddr` + path). **S**
- [ ] **TLS client wiring** into the client (`TlsConnector` exists; wire it in). **S**
- [ ] **Connection reuse / pooling, timeouts, retries + backoff, redirects.** **M**
- [ ] Streaming request/response bodies. **S**

## E. Auth — wrap the external IdP (#10) — composed from C + D

- [ ] Bearer-token extraction; validation pipeline (verify sig, check
      `exp`/`aud`/`iss`, map claims → identity). **M**
- [ ] **JWKS fetch + cache** with invalidation; JWK `n`/`e` → RSA public key. ⚠ the
      fiddliest crypto plumbing. **M**
- [ ] Three isolated identity contexts (admin / client-owner / shopper) that cannot
      cross session boundaries. **S**

## F. Payments, webhooks & outbound integrations (#8 §3.8, #3.9)

- [ ] **Webhook verification** — HMAC-SHA256 over the **raw body** (raw-body access
      before JSON parse), constant-time compare. **S**
- [ ] **Idempotency store** — dedupe by event id; transactional state update. **S**
- [ ] **Stripe / billing adapter** — authenticated outbound HTTPS, retries, timeouts,
      error mapping (hand-rolled over §D; no SDK exists). **M**
- [ ] **Email / SMS adapters** — same shape as billing. **S**

## G. Object storage — DO Spaces / S3 (#12)

- [ ] SigV4-signed requests (from §C): upload/download/delete. **M**
- [ ] Presigned URLs for direct client upload. **S**
- [ ] Multipart upload for large files; per-tenant key prefixes. **M**

## H. Worker & scheduling (#20)

- [ ] **DB-backed job queue** consumer — thread pool, at-least-once, retries +
      exponential backoff + dead-lettering. **M**
- [ ] **Cron-style scheduler** — timer-driven periodic jobs (backups, cleanups). **S**

## I. Observability (#15, #16)

- [ ] **Structured JSON logging** — levels + request correlation IDs (build on the
      JSON writer + `io` traits). **S**
- [ ] **Metrics** — registry + Prometheus text-format endpoint (request rate,
      latency, error rate, pool saturation). **S**
- [ ] Error-reporting + tracing hooks (nice-to-have). **S**

## J. Config, secrets, graceful shutdown (#21, #22)

- [ ] **Config → typed structs** from env + file; secret **references** (never
      store secret values). **S**
- [ ] ⚠ **Signal handler installation** — `process::signal` is numbers-only today.
      Add `sigaction` (POSIX) + the async-signal-safe self-pipe/flag pattern so a
      handler only sets a flag; the accept loop observes it. **M**
- [ ] **Graceful shutdown** — SIGTERM/SIGINT → stop accept, drain in-flight, close
      the pool, exit (#5 reconnect + #21). **S**

## K. Capability codegen (#19)

- [ ] Author the capability catalog in **JSON**; build-step codegen → generated
      types + a registry (both backend and frontend consume it). **S**
- [ ] (Optional) YAML parser if the catalog must be YAML — otherwise skip. **M**

## L. Middleware & request pipeline (#17, #18, request lifecycle)

- [ ] **Typed, ordered middleware** over function pointers + an explicit
      per-request **context struct** (request id, tenant, capability set, checked-out
      DB conn). Closures can't carry this today (§N). **M**
- [ ] **Capability gate** middleware — reject if the route's required capability
      isn't enabled for the resolved tenant. **S**
- [ ] **Tenant resolution** middleware — identity + host header → tenant + caps
      (in-memory cache with invalidation). **M**
- [ ] Rate limiting (token bucket, per tenant / per IP; shared counters). **S**
- [ ] ⚠ **Per-request fault isolation** (#18) — a handler crash returns 500 without
      killing the process. Depends on the panic decision in §N. **M**

## M. Testing & validation (#13, #24, #26, #27) ⚠ — the trust layer

- [ ] **HTTP test client / in-memory server** harness for handler tests. **M**
- [ ] **Integration tests vs. real Postgres** — per-schema fixtures (schema-per-
      tenant makes these clean). **M**
- [ ] ⚠ **Tenant-isolation concurrency test** — hammer the `search_path` seam under
      parallel load; prove no cross-tenant bleed. This is the one you stake the
      business on; do not skip. Gate a second customer's PII on it passing. **M**
- [ ] ⚠ **Long-run soak test** — FFI handle leaks (PGconn/PGresult/SSL), fd/conn
      exhaustion, reconnect across a DB failover, over days of uptime (#26). **L**
- [ ] Data-race review of all shared mutable state — no borrow checker backs this,
      only `Send`/`Sync` + discipline (#27). **M**

## N. Language / compiler gaps to watch (the multipliers)

Directly actionable since the compiler is in-house. These are what turn a clean
estimate into a slow one; each fixed at the language level removes a whole class
of backend friction.

- [ ] **Closure-into-generic** (E0636: a lambda passed to a generic fn can't call a
      generic fn; E0458: capturing closure into a generic fn rejected). Decision:
      keep middleware function-pointer + explicit-context (works today), **or** fix
      mono-discovery-inside-lambda to unlock closure middleware. **L if fixed**
- [ ] **Per-request panic/fault isolation** — decide/verify panic semantics: is a
      panic catchable, and does a panicking worker thread abort only itself or the
      process? Thread-per-request gives isolation only if a worker panic doesn't
      take the process down. Blocks §L fault boundary. **M**
- [ ] **Async runtime integration contract** (separate active track) — keep these
      seams swappable: `io::Read`/`io::Write` over sockets, the server accept loop,
      and the DB-pool checkout as await points. Don't let handler signatures bake in
      thread-per-request assumptions. **track-owned**
- [ ] **FFI handle lifetime ergonomics** — RAII wrappers (§B) so PGconn/PGresult/SSL
      frees can't be forgotten; the soak test (§M) is the backstop. **S**
- [ ] Known ergonomic friction to expect: `&mut expr` doesn't parse (use `mut &this`
      / pointers); no capturing-closure sugar; `.length` is a field.

---

## Milestones (suggested sequencing)

De-risk before committing the full build. M1 exercises the three hardest items
and nothing else — its actual pace is the real predictor of the rest.

- [ ] **M1 — Vertical slice (~2–3 wk).** Concurrent server (§A) + libpq + pool +
      `search_path` (§B) + CMS CRUD on **one** tenant, in a transaction. Proves
      server concurrency, the driver, and the tenant seam. Calibrates the
      compiler-gap multiplier empirically.
- [ ] **M2 — Auth & pipeline.** Crypto (§C) + JWT/JWKS (§E) + middleware pipeline
      + tenant resolution + capability gate (§L) + config (§J config).
- [ ] **M3 — Outbound & integrations.** Client hardening (§D) + Stripe/email/SMS +
      webhooks (HMAC + idempotency) (§F) + object storage / SigV4 (§G).
- [ ] **M4 — Worker & ops.** Job runner + scheduler (§H) + migration fan-out (§B) +
      observability (§I).
- [ ] **M5 — Hardening.** Graceful shutdown (§J) + soak test + tenant-seam
      concurrency proof (§M). Gate a second tenant's PII on M5 passing.

---

## The one gate that isn't negotiable

A data race in the `search_path` seam is a cross-tenant PII leak, not a 500, and
the type system does not close it for you. Don't put a second customer's data
behind that seam until §M's concurrency test is something you'd stake the
business on.
