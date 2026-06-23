# CryoLang v1.0.0 Readiness Tracker

Living tracker for the v1.0.0 release audit (started 2026-06-23). Findings come from the
multi-agent deep audit covering compiler, stdlib, tests, CI/DevOps, and docs.

Legend: ☐ todo · ◐ in progress · ☑ done · ⊘ won't-fix / deferred

---

## Tier 1 — Correctness blockers (silent miscompiles / data loss)

- ☑ **T1.1 Float digit-separator miscompile.** Lexer accepts `_` in float literals
  (`lex/lexer.cryo:235,244,268`) but the value is parsed from the raw lexeme with no
  `_`-stripping (`codegen/.../ir_generator.cryo:679`). `1_000.5` silently compiles to `1.0`.
  - **Fix:** added `CodegenUtil::float_literal_clean` (`codegen/util.cryo`) mirroring
    `int_literal_clean`; `codegen_literal` now strips `_` before `sscanf`
    (`codegen/visit/ir_generator.cryo`). *(pending build validation)*
- ☑ **T1.2 Dropped generic instantiations (order-dependent miscompile).**
  `mono/call_specializer.cryo:1430-1437` — `specialize_method` calls
  `request_nested_instantiations` without resetting `nested_visited` first, violating the
  documented contract (`mono/state.cryo:240-247`).
  - **Fix:** reset `this.state.nested_visited = []` once before the return/param discovery
    block in `specialize_method` (matches the `monomorphizer`/`ast_resolver` convention; one
    reset per signature intentionally dedupes a type shared by return + a param).
    *(pending build validation)*
- ☑ **T1.3 Invalid field/variant types baked into the type arena.**
  `passes/type_resolution.cryo:2751/:2814` (struct/class fields) and `:2984` (enum payloads)
  push `resolved_type` into `FieldInfo`/`payload_types` with no `is_valid()` guard.
  - **Fix:** guard all three sites; on an invalid resolved type emit `E0203` (with the
    field/variant span) and skip the push. The pass returns `from_snapshot`, so the emit
    fails the pass → Phase 5 `run_all` returns false → `instance.cryo:1384` aborts before
    Phase 7 codegen, so the poisoned type never reaches layout. *(pending build validation)*
- ☑ **T1.4 Pass exit-code swallow — CI false-green (generalized C2).**
  A non-fatal pass returning `failure(n)` with no diagnostic was swallowed by the
  `has_errors()`-only return in `run_all` (`passes/pass_registry.cryo:210`).
  - **Fix:** fold `!result.success` into a `any_failed` flag; `run_all` now returns
    `!any_failed && !has_errors()`. `stub()`/`ok()` are `success=true`, so stubs/skips are
    unaffected. Also hardened the test-main caller (`instance.cryo:1930`) with a defensive
    `emit_error` fallback. *(pending build validation)*
  - **NOTE — original C1 ("`cryo test` exits 0") was a FALSE POSITIVE.** The audit agent read
    only the literal `failure(1)` lines (`test_main_codegen.cryo:146,154`); the helpers they
    call (`build_test_main:335`, `emit_object:442/457`) DO emit `E0900` on every failure path,
    so `has_errors()` was already set and the build already exited 1. The real, general bug is
    the `run_all` swallow above, which is now fixed.

---

## Tier 2 — Security / DoS on untrusted input  *(all fixed — pending final test run)*

- ◐ T2.1 WebSocket message reassembly unbounded (`net/ws/conn.cryo`). **Fix:** `MAX_MESSAGE`
  (64 MiB) cap checked after each fragment append in the Text/Binary/Continuation arms.
- ◐ T2.2 WebSocket Ping-flood amplification (`net/ws/conn.cryo`). **Fix:** `MAX_CONTROL_PER_RECV`
  (256) counter; Ping/Pong beyond it errors out of `recv`.
- ◐ T2.3 HTTP/1 no header-*count* cap. **Fix:** `MAX_HEADER_COUNT` (100) enforced in both the
  request (`request.cryo`) and response (`response.cryo`) header-parse loops.
- ◐ T2.4 Header CRLF injection / response-splitting. **Fix:** `strip_crlf` helper; `Headers::insert`
  strips CR/LF from name and value (infallible void API preserved). Tests in `headers_regression.cryo`.
- ◐ T2.5 HTTP/2 WINDOW_UPDATE applied unchecked. **Fix:** reject 0 increment (also covers short
  payload) and window > 2^31-1 per RFC 7540 §6.9.1 (`http2/connection.cryo`).
- ◐ T2.6 `HashMap::with_capacity` hangs on large hints. **Fix:** `next_pow2` saturates at 2^63
  instead of wrapping to 0 and spinning; resize then fails the alloc loudly. `should_panic` test.
- ◐ T2.7 JSON parser leaks whole tree on trailing data. **Fix:** `owned.drop()` before the
  `TrailingData` error (`json/parser.cryo`). Heap-tree regression test in `json.cryo`.
- ◐ T2.8 base64 accepts misplaced cross-group padding. **Fix:** reject padding outside the final
  group and >2 pad chars (`encoding/base64.cryo`). 4 reject tests in `base64.cryo`.
- ◐ T2.9 JSON serializer has no depth cap. **Fix:** `MAX_DEPTH` (256, mirrors parser) guard in
  `write_value_inner`; over-deep subtree emits `null` instead of unbounded recursion.

---

## Tier 3 — Robustness (compiler crashes / CLI)

- ☐ T3.1 Unguarded recursion → SIGSEGV: `parse_binary_expression` (`expr_parser.cryo:272-338`),
  `parse_type_annotation` (`expr_parser.cryo:2217`), `rewrite_this_type_annotation` family
  (`type_resolution.cryo:~1401`).
- ☐ T3.2 Config-gating misclassifies unknown `--target` triples (`passes/config_gating.cryo:59-66`).
- ☐ T3.3 CLI: unknown flags silently accepted; empty `--output=` drops `-o`; failure summaries to
  stdout not stderr (`CLI/_module.cryo`, `commands.cryo`).
- ◐ T3.4 Flaky `Stdlib::NetHttp2::loopback_h2c_round_trip` ("h2 server bind failed"). **Fix:**
  replaced the single fixed PID-derived port with a bind-retry loop (walks up to 50 ports) in
  `tests/tests/stdlib/net_http2.cryo`. (Root cause: fixed port busy / in TIME_WAIT.)

---

## Tier 4 — API-freeze decisions (lock deliberately)

- ☐ T4.1 Integer overflow contract: silent wrapping, no `checked_/wrapping_/saturating_` API.
- ☐ T4.2 `Option`/`Result` have no `as_ref` (every accessor consumes).
- ☐ T4.3 Default `HashMap` hasher is unseeded FNV-1a (HashDoS); `bucket_count()` freezes policy.
- ☐ T4.4 NTSTATUS constants typed as positive `i32` overflow (`ffi/syscall.cryo:1323-1328`).
- ☐ T4.5 `unix`-gated glibc-internal symbols should be `linux` (`ffi/libc.cryo`, `time/clock.cryo`).
- ☐ T4.6 Operator-trait overloading & `From`/`Into` shipped but untested — add tests.

---

## Tier 5 — Docs / CI / dead code

- ⊘ T5.1 CHANGELOG `[Unreleased]` holds shipped-in-1.0 features (associated types, opaque-iter
  re-adaptation, Iterator→assoc-type). **NEEDS MAINTAINER DECISION at tag time:** fold `[Unreleased]`
  into `[1.0.0]` (ship as 1.0.0 — likely, since VERSION=1.0.0 and the code has them) OR cut a 1.1.0.
  Left unedited deliberately — it's a release-versioning call, not a factual fix. (Re-checked: no
  live contradiction remains in `[1.0.0]`; "Iterator" now appears only under `[Unreleased]`.)
- ☑ T5.2 `docs/cryo.md` `macro` claim corrected — `macro` is NOT a reserved word (verified: no
  `macro`/`KwMacro` token in `lex/_module.cryo`); it lexes as an ordinary identifier.
- ☐ T5.3 `docs/grammar.md` omits `for (x in …)`, range `../..=`, `await`/`yield`/`delete`.
- ☑ T5.4 README stdlib table updated: `net` now lists TCP/UDP/DNS/TLS/HTTP2/WebSocket; added
  `encoding`, `time`, `random` rows (descriptions sourced from `stdlib/lib.cryo`).
- ☐ T5.5 No native-Windows CI job (only mingw cross + wine).
- ☐ T5.6 Confirm `cryo-lang.org` serves install scripts at tag time.
- ☐ T5.7 Delete/gate dead public surface: `diag/lsp.cryo`, `resolver/symbol_key.cryo` widen ladder,
  `sema/outcome.cryo` ResolveOutcome, `emit_static_owner_literal_default`.
- ☐ T5.8 Correct stale comments: arena "does NOT dedupe" (it now does); `ResolutionMap` u32→16-bit
  key packing (write-only today).

---

## Notes / decisions log

- 2026-06-23: Tracker created from the 8-agent deep audit. Starting on Tier 1 (T1.1–T1.4).
- Dirty pin is expected during pre-release work; will be clean at tag time (not a finding).
- 2026-06-23: Applied T1.1–T1.4 edits. Self-hosted compiler rebuilds clean via the pinned
  toolchain (stdlib 131 modules + compiler 152 local/58 std → `compiler/build/cryo.exe`).
- 2026-06-23: **T1.1 validated end-to-end** — `1_000.5`→1000.5, `1_0.0e1_0`→1e11,
  `1_234_567.89`→1234567.89, plain `3.14159` unaffected. Added regression test
  `tests/tests/lang/float_literals.cryo` (5 cases). Pre-fix these parsed to truncated prefixes.
- 2026-06-23: **Tier 1 COMPLETE.** Full `make test` green: 1260 unit pass / 0 fail, 103/103
  compile-fail pass (incl. the 5 new FloatLiterals tests). No regressions from T1.2/T1.3/T1.4.
- 2026-06-23: Observed flaky `Stdlib::NetHttp2::loopback_h2c_round_trip` ("h2 server bind
  failed") on one of three runs; passed on the other two. Transient loopback bind, unrelated to
  the Tier-1 changes — but worth a CI-reliability follow-up (ephemeral-port/retry on h2c bind).
  Filed as **T3.4** below.
