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

## ✅ Done this cycle (2026-06-24, cont.) — medium correctness batch

- **`new T[n]` multiply-overflow guard.** Added `ExprOps::emit_array_mul_guard` (`codegen/ops/expr_ops.cryo`, mirrors `emit_int_div_guard`'s abort-trap shape): before the `count * elem_size` multiply in `new_delete_emitter.cryo:emit_new_array`, compares `count` (unsigned) against the compile-time-folded `U64_MAX / elem_size` and `abort()`s on overflow. Elided for ≤1-byte elements. Closes the unchecked-`mul`→too-small-`malloc`→heap-overflow hole; same class as `Layout::array`/`Pool::grow`.
- **`0x_` / `0b_` / `0o_` no longer evaluate to a silent 0.** `lex/lexer.cryo` — the "cursor moved" guard was defeated by a lone `_`; now tracks a `saw_digit` flag per base and rejects (E0007/E0008/E0009) when no real digit was seen. +3 negative tests.
- **`atomic::fence(Relaxed)` no longer emits invalid IR.** `sync/atomic.cryo` — `fence` and `compiler_fence` clamp the sub-Acquire ordering up to SeqCst (LLVM's `fence` rejects Relaxed/Monotonic); docs reconciled. +`fence_relaxed_is_clamped_not_invalid` test.
- **`HashMapIter` `Iterator` impl Copy-gated.** `collections/hashmap.cryo` — the public struct's `next()` field-copied owning `K`/`V` without a bound, so a hand-built iterator could duplicate owning values out (double-free). Gated the impl `where K: Copy, V: Copy`; propagated `where T: Copy` to `HashSetIter`'s impl (`collections/hashset.cryo`). Sealing the fields was rejected — `resolve_struct_literal` doesn't enforce per-field visibility, so a `private` marking wouldn't block construction.
- **Demangler round-trips namespace-qualified trait-impl heads.** `resolver/demangler.cryo:483` — parse the trait head with `parse_path()` (dotted multi-segment, e.g. `4core.3ops.3Add`) instead of a single `parse_segment()`. Verified: `cryo demangle 'C$tr$4core.3ops.3Add$f6MyType-3add$Fv$Rv'` → `<core::ops::Add for MyType>::add() -> void` (was `(malformed)`). Completed by the generic-owner-blob fix below.

Validation: `make test` green (1293 unit + 106 compile-fail), `make selfhost-check` byte-identical FIXED POINT on Linux (Windows wine 6-stage skipped — toolchain absent in env; covered by native Windows build + full `make test` PASS). Not yet repinned/committed (maintainer's call).

## ✅ Done this cycle (2026-06-24, cont.) — demangler generic owners + visibility docs

- **Demangler now decodes generic-instance owner symbols.** `resolver/demangler.cryo` `try_parse_generic_blob`: a monomorphized owner leaf reaches the demangler double-length-prefixed (`encode_path` wraps the already-mangled `5Array$L…$G` blob in another length, and the blob starts with its own digit, so greedy `read_decimal` fuses the counts — `53`+`5Array…` reads as `535`). The new helper splits the leading digit run so the inner blob re-parses as a complete segment of exactly the outer length; the exact-consume + `<…>` check makes the split unique (no overrun heuristic — a long signature tail can hide the overrun). Swept all **10,595** real `C$` symbols: 8,202 render generics, 0 generic-owner regressions; the 47 still malformed are unrelated pre-existing gaps (46 `$MG`, 1 `F32_MIN_POSITIVE.1` global-suffix edge). Diagnostics-only — no symbol change, no repin.
- **`cryo.md §8.2` field-visibility doc corrected.** Now matches the implementation: struct fields default **public** (`parser.cryo:885`); a `private` field is **type-scoped** (`E0353`, hidden even from same-module free functions); distinguished from a top-level `private` type (module-scoped, `E0503`, §14.4). The struct-literal field-visibility *bypass* is left as a maintainer model decision (see Open — Low) — adding enforcement would reject currently-valid programs.

Validation: `make test` green (1293 + 106), `make selfhost-check` byte-identical FIXED POINT on Linux (IR md5 `04e2c337…`). Not repinned/committed (maintainer's call).

## ✅ Done this cycle (2026-06-24, cont.) — iterator adapters on concrete user structs

- **Adapter combinators (`take`/`map`/`filter`/`enumerate`/chains) now resolve on a concrete user-iterator struct receiver.** Root cause: these defaults return a wrapper parameterized by `This` (`take -> TakeIter<This>`), so they're flagged `is_self_returning_default` and instantiated lazily at the call site by `mono/call_specializer.cryo`. On a *concrete* user receiver, sema resolves and pins the unspecialized default into `call.resolved_method`; because it has 0 own generic params, mono's `specialize_method_call` early-returned ("already concrete") before reaching `try_instantiate_self_returning_default` → codegen `E0636`. Fix: the early-return now makes an exception for un-instantiated self-returning / lazy-self-growing defaults. (Terminal defaults `fold`/`for_each`/`count` already worked — they're not self-returning, so materialized eagerly with the impl.)
- **Cross-module bare-name collision (exposed by the above).** Two modules each defining a same-leaf `struct UpTo` implementing the same trait, one using an adapter or a generic terminal (`map<B>`/`fold<Acc>`), miscompiled: mono's method lookup keyed only on the bare leaf and specialized against whichever impl registered first → `E0636` on the real receiver. Fixed with qualified-name disambiguation (prefer `qualified_target_name`, bare fallback unchanged) in `find_self_returning_default` + `find_trait_impl_method_for_target`. Regression test `tests/lang/iter_adapter_name_collision.cryo` deliberately collides with `impl_trait_assoc_binding_spec.cryo`'s `UpTo`.
- Tests: `tests/lang/iter_adapters_on_user_struct.cryo` (take/filter/map/chain) + the collision test. Docs: `stdlib/core/iter.cryo` header + CHANGELOG note the capability; the existing `impl_trait_assoc_binding_spec.cryo` "not yet" comment removed.

Validation: `make test` green (1298 unit + 106 compile-fail), `make selfhost-check` byte-identical FIXED POINT on Linux (IR md5 `ddcce15b…`). A/B vs the old pin confirms `it.take(3)` on a concrete user struct went `E0636` → works. Not repinned/committed (maintainer's call).

## ✅ Done this cycle (2026-06-24, cont.) — medium stdlib bugs + type-cache reset + negative tests

- **TLS verify assert** (`net/tls/context.cryo`): `connect` now asserts `SSL_get_verify_result == X509_V_OK` after a successful handshake when `verify` is on (belt-and-suspenders behind the existing fail-closed `SSL_VERIFY_PEER`).
- **fmt scientific mantissa carry** (`fmt/float.cryo`): `write_scientific` renormalizes when half-up rounding at `decimals` carries the mantissa to 10.0 (`9.9999999e19` → `1.000000e+20`, was `10.000000e+19`). +`scientific_mantissa_carry` test.
- **process stopped-child** (`process/child.cryo`): `os_proc_try_wait`/`os_proc_wait` treat `WIFSTOPPED`/`WIFCONTINUED` as "still alive" via a new `wstatus_is_alive` helper, instead of `decode_wstatus` reporting a stopped child as clean exit-0.
- **Type-cache reset in raw mode** (`compiler/instance.cryo`): `compile_raw` runs a full codegen pipeline but was the one entry missing `reset_shared_type_cache()`; added it so a multi-compile-per-process embedder can't alias `TypeRef.id` cache entries across compiles.
- **5 negative tests** for common user-facing codes with no prior coverage: `E0201`/`E0202`/`E0216`/`E0359`/`E0361`.
- **Verified (no change):** `mpsc` Drop (valgrind-clean for `Receiver<i32>` and `Receiver<String>` dropped mid-stream); `TypeKind::Error` mangler panic (codegen is gated off after any sema error, so it's a correct loud-ICE).
- **Assessed/deferred** (real but selfhost-gated or not-a-v1.0-target): ambiguity diagnostics for static/module-qualified free calls; conversion-aware free-fn overload matching; `insert_import` local-vs-import; fs per-arch struct offsets. See Open items for the per-item analysis.

Validation: `make test` green (1299 unit + 111 compile-fail), `make selfhost-check` byte-identical FIXED POINT on Linux (IR md5 `5bc5aa98…`), `valgrind` clean on the mpsc repros. Not repinned/committed (maintainer's call).

---

## ⬜ Release checklist (before tagging)

- ⬜ **Tag `v1.0.0`.** No git tag exists yet (`release.yml` is tag-triggered; CHANGELOG/README reference `releases/tag/v1.0.0`). Run `make verify-pin-clean` first.
- ⬜ **Repin if source changed since last pin** (`make pin-all`) so `make verify-pin-clean` passes.
- ⬜ **Decide on committed internal files** (see docs items below).

---

## ⬜ Open — High

- ✅ **Demangler can't round-trip namespace-qualified trait-impl symbols.** Fixed this cycle (see Done) — trait head now parsed as a full `parse_path()`. Remaining demangler gap (specialized symbols) tracked under Open — Low.

---

## ⬜ Open — Medium

### Compiler
- ✅ **`new T[n]` array alloc has no multiply-overflow guard.** Fixed this cycle (see Done) — `emit_array_mul_guard`.
- ✅ **`0x_` / `0b_` / `0o_` accepted, evaluate to silent 0.** Fixed this cycle (see Done) — `saw_digit` flag.
- ✅ **Combinators don't resolve on a concrete user-iterator struct.** Fixed this cycle (see Done) — mono's `specialize_method_call` early-return no longer short-circuits a sema-pinned self-returning adapter default (it was treated as "already concrete" because it has 0 own generics, so it was never lazily instantiated → codegen `E0636`). Also fixed a cross-module bare-name collision the fix exposed (two same-leaf `UpTo` structs) by qualified-name disambiguation in `find_self_returning_default` + `find_trait_impl_method_for_target`.
- ✅ **Process-wide type cache keyed on `TypeRef.id` needs reset per compile.** Fixed this cycle (see Done) — `compile_raw` was the one codegen entry that ran a full IRGen/ObjectEmission pipeline without `reset_shared_type_cache()`; added it (every other codegen entry already reset). Audited all entries: `compile_project_with_ctx`, `compile_for_lsp_content_into`, and now `compile_raw` reset; the frontend-only entries (`check`/`lsp`/`lsp_content`) don't run codegen so don't touch the cache.
- ⬜ **Missed-ambiguity diagnostics for static + module-qualified free calls.** `sema/call_resolver.cryo:2655,2926` silently picks registration-order `first_i` instead of emitting an ambiguity error (trait path at :1316 does emit). **Assessed (deferred — selfhost-gated):** the fix needs a candidate-count tracker + emit when `count>1 && arity_count!=1 && prefix_ties!=1`. Additive, but it *newly rejects* programs, so it can surface a latent silently-resolved ambiguity in stdlib/compiler that breaks the self-build — needs its own selfhost cycle (possibly iteration). Not a drop-in.
- ⬜ **Free-fn overload matching is exact-id only (+Ptr↔Ref).** `sema/call_resolver.cryo:1372-1413` — a conversion-needing overloaded call matches nothing → unpinned callee → codegen name+arity guess. **Assessed (deferred — selfhost-gated):** widening overload match to allow implicit conversions changes resolution broadly (which overload wins); high regression surface across the self-build. Needs careful design + differential selfhost.
- ✅ **`TypeKind::Error` → mangler panic.** Verified this cycle (no change needed) — `pass_registry.run_all` returns `!has_errors()`, and Phase-7 codegen is gated on `!phase6_failed`, so **codegen never runs after any sema error**. The mangler `panic` is therefore a correct loud-ICE for a should-be-impossible *silent* Error sentinel (no diagnostic) reaching mangling — the documented invariant holds.
- ⬜ **`insert_import` conflates "two imports" with "local + import".** `resolver/scope.cryo:166-188` — order-dependent false-positive ambiguity / silently-skipped genuine clashes. **Assessed (deferred — selfhost-gated):** `insert_import` only receives `new_module_id`/`existing_module_id` (both `u32`); it can't tell whether the existing entry is a *local* decl (which should shadow, no ambiguity) vs another import. Fixing it needs the caller to thread an is-local flag and rework the ambiguity bookkeeping — a real resolver change with order-dependence and self-build risk.

### stdlib
- ✅ **`HashMapIter` is publicly constructible with an ungated `Iterator` impl.** Fixed this cycle (see Done) — Copy-gated the impl.
- ⬜ **fs metadata/dir hardcode glibc x86_64 struct offsets.** `fs/metadata.cryo`, `fs/dir.cryo` (`STAT_OFF_*`, `DIRENT_OFF_TYPE_LINUX=18`, `SIZEOF_STAT=144`) → garbage on musl/other arches. Prior dirent-offset bug lived here. **Assessed (deferred — not a v1.0 target):** correct for the supported targets (glibc x86_64 + windows-gnu); already documented in-source. A real fix = per-arch/libc offset gating (like the existing `![target]` split) or a C `offsetof` shim — both gated on adding a non-glibc/non-x86_64 target, which v1.0 doesn't have, and untestable without that environment. Speculative offsets would be worse than the documented status quo.
- ✅ **`atomic::fence(Relaxed)` emits illegal LLVM IR.** Fixed this cycle (see Done) — clamp to SeqCst.
- ✅ **mpsc `Sender`/`Receiver` Drop bounds asymmetric.** Verified this cycle (no leak; no change needed) — Cryo's `where T: Drop` on a Drop impl does NOT gate the inner free (confirmed empirically: `Array<T,A>::drop` is also `where T: Drop` yet `valgrind-check` is clean). Valgrind on `Receiver<i32>` (non-Drop T) and `Receiver<String>` dropped mid-stream with queued payloads both show **0 bytes in use at exit** — the inner channel + queued nodes + in-flight String payloads are all released.
- ✅ **TLS omits belt-and-suspenders `SSL_get_verify_result`.** Fixed this cycle (see Done) — added the `X509_V_OK` assert after a successful handshake when `verify` is on.
- ✅ **process `try_wait` maps a stopped child to clean exit-0.** Fixed this cycle (see Done) — `os_proc_try_wait`/`os_proc_wait` now treat a `WIFSTOPPED`/`WIFCONTINUED` `wstatus` as "still alive" (return `None` / keep blocking) instead of decoding it to exit-0. (The `kill()`+drop zombie-reap is a documented non-blocking-destructor trade-off — `kill`'s doc already says to call `wait` afterwards; not changed.)
- ✅ **fmt scientific-notation rounding can carry the mantissa to 10.** Fixed this cycle (see Done) — `write_scientific` renormalizes when half-up rounding at `decimals` carries the mantissa to 10.0. +`scientific_mantissa_carry` test.

### tests
- 🔄 **Negative-test coverage.** Added 5 this cycle for common user-facing codes that lacked any negative test: `E0201`/`E0202`/`E0216`/`E0359`/`E0361`. **Finding:** the audit's named codes are mostly NOT testable as negatives — `E0454`/`E0457`/`E0502`/`E0504` have **zero emission sites** (reserved-but-dead; the move-checker emits `E0452`/`E0453`, closure-capture emits `E0458`), `E0456` is a **warning** (not a compile-fail), and `E0501` (circular import) is **multi-file** while the negative harness runs each file single-file via `cryo check`. Separately found: `Result`/`Option` don't resolve under single-file `cryo check` even with a direct `import std::core::result` (only transitively via a collection module) — a `check`-mode prelude gap worth its own look.

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
- ✅ **Demangler chokes on generic-instance owner leaves — fixed + validated (selfhost byte-identical).** Root cause (corrected from the earlier "disambiguator" guess): in path/owner position the mangler emits a monomorphized leaf as an *already-mangled* segment (`5Array$L…$G`) and then `encode_path` length-prefixes the WHOLE blob; since the blob starts with its own length digit, greedy `read_decimal` fuses the counts (`53`+`5Array…` → reads `535`) → `(malformed)`. Fixed in `resolver/demangler.cryo` `try_parse_generic_blob`: split the leading digit run so the inner blob re-parses as a *complete* segment of exactly the outer length (exact-consume + `<…>` validation makes the split unique), then render recursively. Diagnostics only — no symbol change/repin. Validated by sweeping all **10,595** real `C$` symbols from the test objects: 8,202 now render generics (`<…>`); the 47 still `(malformed)` are unrelated pre-existing gaps (46 `$MG` generic-method symbols — see item above; 1 global-with-LLVM-`.N`-suffix edge `F32_MIN_POSITIVE.1`). **Remaining (separate, Low):** the underlying encoding is non-canonical per the mangling spec (generic args should attach to the segment with no extra length wrapper); a future mangler cleanup to emit the spec form would need a repin.
- ✅ **Doc contradiction on field visibility — fixed.** `cryo.md §8.2` claimed "fields private to their module by default"; the compiler defaults struct fields **public** (`parser.cryo:885`) and enforces a `private` field as **type-scoped** (accessible only in the declaring type's own methods, `E0353` — confirmed by `tests/negative/E0353_private_field_access.cryo`, where a same-module free function is still rejected). Rewrote §8.2 to match and to distinguish field privacy (type-scoped, `E0353`) from a top-level `private` type (module-scoped, `E0503`, §14.4).
- ⬜ **Struct-literal construction bypasses per-field visibility — needs a model decision (NOT auto-fixed).** `sema/sema.cryo:resolve_struct_literal` checks the *type*'s visibility (E0503) but never calls `enforce_field_visibility`, so a `private` field is settable cross-module via a struct literal (only *reads* are guarded). Latent today (no stdlib struct relies on it; it's why `HashMapIter` was Copy-gated rather than field-sealed). **Why deferred:** adding enforcement *rejects currently-valid programs* (a breaking semantic change), and the visibility model itself is unsettled — the doc intends module-scope, the impl enforces type-scope, and the default is public not private. Pick the model first (module-scope-private-default vs type-scope-public-default), then enforce it consistently on both access (`E0353`) and construction. Maintainer call.
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
