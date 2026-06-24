# Cryo v1.0.0 — Release TODO

Tracking sheet for the v1.0.0 audit (2026-06-24). Findings come from a
multi-agent deep audit (compiler frontend, sema/resolver, types & memory-safety
passes, codegen/ABI, driver/CLI/deps, stdlib, tests, docs/CI) plus direct
verification. Severity reflects v1.0.0 impact, not effort.

Legend: ✅ done · ⬜ open. File:line are starting points, not exhaustive.

---

## ✅ Done this cycle (2026-06-24)

- **B1 — Example golden files.** Created `examples/{02-fizzbuzz,07-shapes,10-expr-interpreter,13-closures}/expected.out` + `.gitignore` exception. Unblocks `make examples-golden` / CI / release.
- **B2 — Parser recursion guards completed.** Added `brace/bracket/paren_depth > 256 → E0111` to `parse_inner_block` / `parse_block_statement` (parser.cryo) and `parse_array_literal` / `parse_call_args` / `parse_array_access_expr` (expr_parser.cryo). Nested blocks/calls/index/array-lits/if-match no longer SIGSEGV.
- **H1 — Intrinsic name capture.** DI `intrinsic_names` set + `is_intrinsic_decl_name()`; codegen (call_emitter Identifier + ScopeResolution) skips inline-emit when a resolved callee is a real non-intrinsic function. User functions sharing a leaf name (e.g. `rotl32`) now run their own body.
- **H2 — HTTP/2 `SETTINGS_MAX_FRAME_SIZE`.** `settings.cryo:apply_one` rejects out-of-range values (RFC 16384..16777215); kills the value-0 infinite-loop DoS.
- **H3 — `math::abs` MIN wrap.** Documented the wrap on `abs_i64`/`abs_i32`; added `checked_abs_i64`/`checked_abs_i32`. (Also removed duplicate `F32_MIN_POSITIVE` — audit item M8.)
- **H4 — CHANGELOG date + release verification.** Date → 2026-06-24; `release.yml` now per-file-verifies the Linux tarball (bin/cryo, stdlib/lib.cryo, VERSION, LICENSE) like the Windows zip.
- **H5 — Valgrind gate is now real.** Added `scripts/valgrind-check.sh` + `make valgrind-check` (examples under valgrind, definite-leak/invalid-free → fail), wired into `ci.yml`; corrected the 4 misleading "valgrind gate" comments.
- **H6 — Cloner preserves impl-block associated-type bindings.** `visit(ImplBlockNode*)` now copies `assoc_binding_*` / `derived_param_*` (deep-cloning annotations). **Defensive:** verified by experiment that no currently-reachable input triggers the bug (the lazy call-site path masks it, and the user-type path is blocked by the limitation in "Open — Medium" below). Correct to keep; selfhost byte-identical.
- **Local type inference (removed from Known Limitations).** A `const`/`mut` binding with no `: T` now infers its type from the initializer's *concrete* type (sema `visit(DeclStmtNode*)`): broadened the `resolved_type = init_type` step to the general no-annotation case and replaced the eager lambda-only `E0104` with a deferred check (only when there's no initializer, or the initializer is `void`). Because the inferred type is concrete, the documented iterator consequence dissolves too — `mut it = arr.iter(); it.take(3)...` works (no opaque erasure). CHANGELOG limitation removed; `docs/cryo.md` §1/§2/§3 updated; +`tests/tests/lang/local_type_inference.cryo`.

Validation: `make test` green (1287 unit + 103 compile-fail), `make valgrind-check` clean, `make selfhost-check` byte-identical fixed point on Linux + Windows.

---

## ⬜ Release checklist (before tagging)

- ⬜ **Tag `v1.0.0`.** No git tag exists yet (`release.yml` is tag-triggered; CHANGELOG/README reference `releases/tag/v1.0.0`). Run `make verify-pin-clean` first.
- ⬜ **Repin if source changed since last pin** (`make pin-all`) so `make verify-pin-clean` passes.
- ⬜ **Decide on committed internal files** (see docs items below).

---

## ⬜ Open — High

- ⬜ **Demangler can't round-trip namespace-qualified trait-impl symbols.** `resolver/demangler.cryo:483` parses the trait head with a single `parse_segment()`, but the mangler emits dotted multi-segment (`core::ops::Add` → `4core.3ops.3Add`). Hits ~every trait-impl method → `(malformed)`. **Diagnostics / `cryo demangle` only** — the mangler is self-consistent so linked output is correct. Pair with the `$MG` generic-method case (sema M5 below).

---

## ⬜ Open — Medium

### Compiler
- ⬜ **`new T[n]` array alloc has no multiply-overflow guard.** `codegen/visit/new_delete_emitter.cryo:352` — `mul(count, elem_size)` feeds malloc unchecked; large `n` wraps → heap overflow. Same class already guarded in `Pool::grow`/`reserve_exact`/`Layout::array`.
- ⬜ **`0x_` / `0b_` / `0o_` accepted, evaluate to silent 0.** `lex/lexer.cryo:179-232` — empty-digit guard defeated by the lone `_`. Track a `saw_digit` flag. (Verified: compiles, prints 0.)
- ⬜ **Combinators don't resolve on a concrete user-iterator struct.** `it.take(...)`/`.map(...)`/`.filter(...)` where `it` is a user struct that `implement`s `Iterator` → `E0636 no method 'take'` (only `Range`/stdlib types and call-expression chains work; `fold`/`for_each`/`count` DO work on user structs). Discovered while building the H6 test. Adapter-returning defaults aren't specialized for concrete user receivers.
- ⬜ **Process-wide type cache keyed on `TypeRef.id` needs reset per compile.** `codegen/type_map.cryo:76-86` — a missed `reset_shared_type_cache()` in a multi-compile-per-process entry (LSP, test runner, batch build) aliases unrelated types. Audit every multi-compile entry; ideally compile-scope the key.
- ⬜ **Missed-ambiguity diagnostics for static + module-qualified free calls.** `sema/call_resolver.cryo:2655,2926` silently picks registration-order `first_i` instead of emitting an ambiguity error (trait path at :1316 does emit). 
- ⬜ **Free-fn overload matching is exact-id only (+Ptr↔Ref).** `sema/call_resolver.cryo:1372-1413` — a conversion-needing overloaded call matches nothing → unpinned callee → codegen name+arity guess.
- ⬜ **`TypeKind::Error` → mangler panic.** `resolver/mangled_name.cryo:807-816` aborts via `intrinsics::panic` if an Error sentinel reaches mangling. Confirm sema strictly halts before codegen on any error (loud-ICE only if that invariant holds).
- ⬜ **`insert_import` conflates "two imports" with "local + import".** `resolver/scope.cryo:166-188` — order-dependent false-positive ambiguity / silently-skipped genuine clashes.

### stdlib
- ⬜ **`HashMapIter` is publicly constructible with an ungated `Iterator` impl.** `collections/hashmap.cryo:521-546` — `HashMap::iter` gates `K,V: Copy`, but the public struct's `next()` field-copies without the bound, so a caller can duplicate owning values out (double-free risk). Seal the constructor or gate the impl.
- ⬜ **fs metadata/dir hardcode glibc x86_64 struct offsets.** `fs/metadata.cryo`, `fs/dir.cryo` (`STAT_OFF_*`, `DIRENT_OFF_TYPE_LINUX=18`, `SIZEOF_STAT=144`) → garbage on musl/other arches. Prior dirent-offset bug lived here.
- ⬜ **`atomic::fence(Relaxed)` emits illegal LLVM IR.** `sync/atomic.cryo:81-92` — doc says it clamps to SeqCst; code passes the raw ordering. LLVM `fence` rejects Relaxed/Monotonic. Fix code or doc.
- ⬜ **mpsc `Sender`/`Receiver` Drop bounds asymmetric.** `sync/mpsc.cryo:256-262,335-336` — verify an in-flight `String` payload is released when the receiver drops mid-stream.
- ⬜ **TLS omits belt-and-suspenders `SSL_get_verify_result`.** `net/tls/context.cryo:80-83` — NOT a bypass (`SSL_VERIFY_PEER`+`SSL_set1_host` fail closed), but add the `X509_V_OK` assert; binding exists at `ffi/openssl.cryo:88`.
- ⬜ **process `try_wait` maps a stopped child to clean exit-0** (`process/child.cryo:171-176`); `kill()`+drop's single best-effort `WNOHANG` reap can zombie-leak (`:307-313`).
- ⬜ **fmt scientific-notation rounding can carry the mantissa to 10** (`fmt/float.cryo:206-237`, e.g. `9.9999996e20`). Display path only; JSON uses the snprintf round-trip and is unaffected.

### tests
- ⬜ **Negative-test coverage ~25% of E-codes.** Add cases for user-facing statics with no negative test: `E0501` circular import, `E0502`/`E0504`, `E0457` non-Copy closure capture, `E0454`/`E0456` leak / conditional-move.

### docs / CI
- ⬜ **Internal dev files committed.** `HANDOFF.md`, `pipeline-reorder-progress.md` (~160KB), `scratch/`. Remove or gitignore before tagging.
- ⬜ **LLVM bootstrap script unpinned.** `scripts/ci/install-llvm.sh` root-runs unpinned `apt.llvm.org/llvm.sh` (the LLVM *version* is pinned, the bootstrap script isn't). Pin/vendor it; rest of the repo verifies sha256.
- ⬜ **Windows `net`/`process` caveats absent from README/CHANGELOG.** `CONTRIBUTING.md` documents the Windows `process::Command` gaps; the user-facing README/CHANGELOG don't note TLS/process are POSIX-first.

---

## ⬜ Open — Low (hardening / polish; see audit notes)

### Compiler
- ⬜ Silent depth caps with no diagnostic: class/vtable inheritance depth-16 (`codegen/type_map.cryo:449`, `place_emitter.cryo:159`), InstantiatedType unwrap depth-5.
- ⬜ Generic-call lookahead capped at 200 tokens → mis-parse of pathological generic-arg lists (`parser/expr_parser.cryo:is_generic_call_ahead`).
- ⬜ Empty char literal `''` → misleading "unterminated" message (`lex/lexer.cryo:411`).
- ⬜ `new T{...}` with omitted fields / ctor-resolution failure leaves heap object partially uninitialized (no zeroing, no diag) (`new_delete_emitter.cryo:222-297`).
- ⬜ Shift-amount ≥ bit-width → LLVM poison (inconsistent with div-by-zero/INT_MIN guards).
- ⬜ Generic-method `$MG…` symbols never demangle (`mangled_name.cryo:471`); demangler `AssocProjection` / `Optional` / `Tuple` naming gaps. Diagnostics only.
- ⬜ `member_resolver.cryo:227` peels one Reference layer (vs 5 elsewhere); `literal_resolver` no i128/u128 literal overflow check; `resolution_map` 16-bit line/col → span-key collisions; `name_resolution` import suffix-match can index `-1`.

### stdlib
- ⬜ base64 decode accepts non-canonical trailing bits (`encoding/base64.cryo:55-95`) — RFC allows rejecting; document the lenience.
- ⬜ JSON exponent `exp_val*10+digit` overflows i32 on hostile input; `value.cryo:50-56` `as_i64`/`as_u64` use `<=` against an f64 that rounds to 2^63.
- ⬜ `math::lcm` / `next_power_of_two` overflow silently to 0.
- ⬜ `sync/once.cryo:47` casts a Cryo fn-ptr to `pthread_once`'s C routine (ABI-fragile); `sync/barrier.cryo:118` leader detection by errno blacklist instead of `PTHREAD_BARRIER_SERIAL_THREAD`.
- ⬜ `fs/file.cryo:228` `flush()` is a no-op and there's no `fsync`/`sync_all`; `fs/dir.cryo` `remove_dir_all` is non-atomic + unbounded-recursive (document `rm -rf` caveats).
- ⬜ `String::try_push` capacity check uses `==` not `>=` (correct under invariant, fragile); HPACK dynamic-table-size-update accepts arbitrary `new_max`; `HashSet` lacks `Clone`/`with_seed_in` (HashMap parity); Arc/Rc refcount has no overflow guard (effectively unreachable).

### Documented v1 non-goals (no action, just confirm documented)
- Thread-local values leak at thread exit; mutex/rwlock have no poisoning. Async/await; macros; macOS target. (All in `docs/cryo.md §21` / CHANGELOG.) — local type inference is now implemented (see Done).

---

## Notes
- **H6 is defensive.** An isolated regression test isn't constructible today: the lazy call-site path masks the eager-clone gap, and the user-type adapter path that would exercise it is blocked by the "combinators on concrete user iterators" limitation above. `tests/tests/stdlib/iter.cryo` comprehensively covers the Range-based adapter + `This::Item` path; `tests/tests/lang/impl_trait_assoc_binding_spec.cryo` (added this cycle) covers `This::Item`-in-signature default methods (`fold`/`for_each`) on a user impl.
- The full per-area audit reports (with "what's solid" sections) are the source of truth for the long tail; this file is the actionable subset.
