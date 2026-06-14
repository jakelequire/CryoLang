# CryoLang v1.0.0 Readiness — Audit Findings & Checklist

> Generated from a full-repo deep audit (CI/DevOps, compiler, stdlib, docs, tests, tooling).
> Severities: **Blocker** (must fix before tag) · **Major** (strongly recommended) · **Minor** · **Nit**.
> Every item cites `file:line` so it can be actioned directly.

## Scope decision (owner: Jake)

**TLS, UDP, HTTP/2, WebSocket, `time`, and `random` are IN 1.0.** They ship as working,
tested code and are part of the semver-frozen surface. Documentation that calls them
"Beyond 1.0" is wrong and must be corrected (see D1/D2). macOS remains post-1.0.

**Decisions (2026-06-13):**
- **Platform contract = Linux-x86_64 + Windows only.** Gate the Linux-x86_64-specific stdlib
  internals to `![target(linux)]` (not `unix`), document Windows as supported-but-partial, and
  defer macOS/BSD to post-1.0. Drives M3/M4/M5.
- **B3 (tuple drop leak): deferred by owner** — not being worked now. Still an open blocker for
  the freeze; tracked, not fixed.
- **Trait-foundation gaps (M6/M7/M8): implement all now** — container `Clone`, a float-ordering
  story, and sync locks returning `SyncError`, before the semver freeze.
- **Active queue:** Windows CI validation (M1/M2), trait foundation (M6–M8), doc/cleanup sweep.

## Progress — first cleanup pass (2026-06-13)

Completed (working tree, uncommitted):

- **B1 done** — CI smoke-test path fixed to `compiler/build/cryo` (`ci.yml`); stale
  `build/bin/cryo` comments corrected in `compiler/cryoconfig` and `instance.cryo`.
- **B2 done** — added a `make examples` target (compiles all 14 `examples/*/`) and a CI
  step that runs it. **Found and fixed a real breakage in the process:** `examples/14-threads`
  used the removed `AtomicU8` type → updated to the generic `Atomic<u8>`. All 14 now build.
- **B4 done** — README roadmap rewritten (TLS/UDP/HTTP2/WS, `time`, `random`, `thread::Builder`,
  guard clauses, fs ops moved into "What's in 1.0"); `cryo.md §19.2` net row corrected;
  `CONTRIBUTING.md` cross-compilation + `process::Command` lines reconciled with reality.
- **New finding fixed — atomics API drift.** The public atomics API was unified from concrete
  `AtomicU8`/`U32`/`U64`/`I32`/`I64`/`Bool` into a generic `Atomic<T>` (static-match dispatch),
  but README, `cryo.md`, `CHANGELOG`, an example, and several stdlib doc comments still named the
  old types. Updated all of them.
- **M15 done** — wrote `docs/testing.md` (the file referenced by `Makefile`, `tests/cryoconfig`,
  and the negative README but previously missing).
- **M17 done** — `git rm --cached tools/CryoAnalyzer/dist/extension.js` (regenerated bundle).
- **m8 partial** — fixed the `consumes_self` → `sink`/`implicit`/`transparent` directive bug in
  both tmLanguage grammars. (Did NOT delete the orphan `docs/Cryo.tmLanguage.json` — left for a
  decision on whether to keep one canonical grammar.)
- **Guard clauses fully documented** (part of m9) — new `cryo.md §7.4 Guard Clauses`
  (Exhaustiveness renumbered to §7.5) and a `Guard` production added to `grammar.md`'s `MatchArm`.
- **m9 partial** — fixed `cryo.md` math function names, the 7→12 prelude list, and guard-clause
  drift. (Tuple-literal §2.6/§21 claim — deferred until B3 is resolved, since tuples currently
  compile but leak on drop — and the `grammar.md` precedence table NOT yet done.)
- **n4 done** — corrected the stale "102 error codes (E0001–E0900)" → "over 200 (E0000–E0999)".
- **n6 done** — fixed the broken `§1.4` anchor in `cryo.md`.

Not started: B3 (tuple drop glue — needs compiler work), all Windows-validation Majors (M1–M5),
the trait-foundation semver items (M6–M8), monomorphizer correctness (M9–M11), and the infra
items (M12–M14, M16). Note: audit item **n1 was inaccurate** — only `tests/helpers/abi_helpers.c`
is tracked (the `.o`/`.a` are already gitignored), so there is nothing to clean there.

## Progress — second pass (2026-06-13)

Per the decisions above (B3 deferred; platform = Linux + Windows; trait foundation now):

- **Doc/cleanup sweep:** `grammar.md` precedence table synced to `cryo.md §5.7` (now 18 levels
  incl. `??`/`|>`/`<|`/range); M4 lying-docs fixed (`process/_module.cryo` now describes the
  partial Windows `CreateProcessA` path + gaps; `fs/path.cryo` flags the Windows file-I/O-works-
  but-path-manipulation-doesn't trap); CryoFormat README gained an "experimental, not in 1.0,
  not wired to the editor" banner (M16); orphan `docs/Cryo.tmLanguage.json` deleted (m8).
- **M2 done — negative suite now runs on Windows (verified: 92 passed, 0 failed).** Root causes
  were deeper than the audit's "/tmp hardcode": (1) the `/tmp` capture path → now `%TEMP%`/`%TMP%`
  via a `neg_tmp_path()` `![target]` pair; (2) the dir-scan filter required `dirent_type == 0`, but
  mingw-w64's `struct dirent` has no `d_type`, so the intrinsic returned garbage and **0 files
  survived → the whole suite was silently suppressed** — removed the `dirent_type` dependency
  (the `.cryo` extension check is the real filter); also taught `dir_of` to split on `\`.
- **Audit correction — M4 `current_exe` claim was WRONG.** The `readlink` intrinsic has a Windows
  shim (`core/intrinsics.cryo:294-302`) that calls `GetModuleFileNameA`, so `env::current_exe()`
  *does* work on Windows. The original doc was correct; I reverted my mistaken "returns None on
  Windows" edit.
- **New observation (not fixed):** the `dirent_type` intrinsic still reads the glibc `d_type`
  offset unconditionally (`intrinsic_emitter.cryo:636`), so it returns garbage on Windows for *all*
  callers (`module_loader`, `fs::read_dir`). `emit_dirent_name` was already win64-fixed;
  `emit_dirent_type` was not. Module loading still works (doesn't hard-depend on it), but
  `fs::read_dir` file-type discrimination is unreliable on Windows. Tracked as a follow-up.

## Progress — third pass / trait foundation (2026-06-13)

Decisions taken in: M7 = total Ord for floats; M8 = poisoning + Result. **Both hit deeper facts
that supersede those choices**, and M6 turned out already-done. Net: the trait-foundation "gaps"
are mostly not what the audit described.

- **M6 (Clone): already implemented** — fixed only the stale doc (see above). Done.
- **M7 (total Ord for floats): BLOCKED by a compiler codegen bug.** I implemented total `Eq`+`Ord`
  for `f32`/`f64` (Rust-style `total_cmp` via `mem::transmute`), but it exposed a latent bug:
  instantiating `Option<f64>`'s conditional `Eq` makes codegen pass a `&f64` argument **by value
  (`double`) instead of by pointer**, failing LLVM verification (`Call parameter type does not match
  function signature`). The callee `f64::equals` is generated correctly (`&f64` → `ptr`); the
  *caller* drops the reference for float-typed args in monomorphized generic trait dispatch. Latent
  because no float type had `Eq`/`Ord` before. **Reverted the stdlib + test changes**; M7 needs the
  compiler bug fixed first. See [cpp_compiler_bugs / selfhost bugs] follow-up.
- **M8 (poisoning + Result): NOT VIABLE as chosen.** Cryo's `panic` calls libc `abort()`
  (`intrinsic_emitter.cryo:944-981`) — it aborts the **process**, not unwinds a thread. Lock
  poisoning is therefore meaningless: a thread that panics holding a lock takes the whole process
  with it, so no surviving thread can ever observe a poisoned lock (Rust's poisoning depends on
  panic=unwind). The existing `lock()`-panics-on-`pthread`-error behavior is correct; the only
  sensible cleanup is the dead `SyncError` type. **Needs a revised decision — see report.**

## Overall assessment

Release-quality project, well above typical new-language standards. Self-hosted compiler
(~95K LOC) with real diagnostics, parser recovery, sound move-checking, only 2 literal TODOs.
Stdlib (~30K LOC) with real TLS/HTTP-2/WS/atomics/threads behind ~1,100 test functions.
Mature bootstrap discipline (6-stage byte-identity gate, honest pin sidecars, checksummed
releases). **Not tag-ready today** — blockers are narrow and specific. Recurring theme: the
**product is often better than its docs admit**, and **everything is validated on Linux only**
while Windows is a shipped, unvalidated release artifact.

---

## BLOCKERS

- [x] **B1 — CI smoke-tests a nonexistent binary path.** ✓ DONE (2026-06-13). The artifact lands at
  `compiler/build/cryo` (hoisted by `project_config.cryo:644`; `Makefile:18`), but CI ran
  `./compiler/build/bin/cryo --version|--help`. CI was red or not gating what it claims.
  - Fixed: `.github/workflows/ci.yml:34-35` → `./compiler/build/cryo`.
  - Fixed the stale `bin/` comments at `compiler/cryoconfig:5` and
    `compiler/src/compiler/instance.cryo:66` so the wrong path isn't reintroduced.

- [x] **B2 — Examples are compiled by nothing.** ✓ DONE (2026-06-13). No `make`/CI/release step
  built the 14 examples. Added a `make examples` target (`Makefile`) and a CI step (`ci.yml`) that
  compiles all of `examples/*`. This caught and fixed a real breakage: `examples/14-threads` used
  the removed `AtomicU8` → now `Atomic<u8>`; all 14 build. (Running the deterministic ones is a
  possible follow-up; currently build-only.)

- [ ] **B3 — Bare tuples of owned elements never drop their contents (memory leak).**
  `codegen/visit/call_emitter.cryo:1266-1270` returns `true` (no-op) for tuple types; structs,
  classes, enums, fixed arrays are handled recursively (`:1239-1264`) and `T?` is safe
  (desugars to `Option`). Either implement tuple drop glue or reject owned-element tuples in
  sema until it exists. RAII is a headline feature; a silent leak on a core composite type
  can't ship.

- [x] **B4 — Docs contradict the shipped 1.0 surface (per scope decision above).** ✓ DONE (2026-06-13).
  - `README.md` roadmap rewritten: TLS/UDP/HTTP2/WS, `time`/`random`, `thread::Builder`, guard
    clauses, and fs ops moved into "What's in 1.0"; "Beyond 1.0" trimmed to async, the remaining
    iterator adapters, dialable IPv6, and macOS.
  - `docs/cryo.md §19.2` net row rewritten to state TLS/UDP/HTTP2/WS ship in 1.0.
  - `CONTRIBUTING.md` cross-compilation line corrected (Linux→Windows via mingw-w64) and
    `process::Command` reframed as POSIX-first with a partial Windows path.
  - Also fixed the related **atomics API drift** (`AtomicU8`/… → generic `Atomic<T>`) across README,
    `cryo.md`, `CHANGELOG`, `examples/14-threads`, and stdlib doc comments.

---

## MAJOR

### Platform / scope honesty (largest cluster — Windows is shipped but unvalidated)

- [x] **M1 — No Windows CI.** ✓ DONE (2026-06-13) — added a `windows-smoke` job to `ci.yml`:
  cross-builds the Windows zip on Ubuntu (mirroring the proven `release.yml` windows job:
  mingw-w64 + `fetch-windows-llvm.sh` + `build-release.sh windows`), then runs `cryo.exe --version`
  and `--help` under **wine**. ⚠️ The build steps are copied from the working release job, but the
  wine-execution step could not be validated locally (no CI/wine access here) — **watch the first
  CI run** and tweak the wine package/invocation if needed. macOS remains intentionally post-1.0.
- [x] **M2 — Negative-test suite is Linux-only.** ✓ DONE (2026-06-13), verified 92 passed on
  Windows. Fixed the `/tmp` capture path (→ `%TEMP%` via `neg_tmp_path()`) AND the real blocker:
  the dir-scan filter depended on `dirent_type`, which is garbage on mingw (no `d_type`), so the
  suite was silently suppressed — removed that dependency. Also taught `dir_of` to split on `\`.
  (See the "second pass" progress note; the audit's M4 `current_exe` premise was disproven.)
- [ ] **M3 — Stdlib "all Unix" really means "Linux-x86_64-glibc".** Hardcoded layouts will read
  garbage/crash on macOS/BSD/ARM: `fs/metadata.cryo:34-37` (`struct stat` offsets),
  `fs/dir.cryo:30-31` (`dirent`), `io/error.cryo:8-10` (errno table), `ffi/libc.cryo` pthread
  buffer sizes (mutex 40 vs macOS 64) + glibc-only `pthread_*_np`. These sit behind
  `![target(unix)]`. Gate to `linux` or scope the release to "Linux-x86_64 + Windows."
- [x] **M4 — Lying platform docs.** ✓ DONE (2026-06-13). `process/_module.cryo` now describes the
  partial Windows `CreateProcessA` path + its gaps; `fs/path.cryo` flags the
  Windows-file-I/O-works-but-path-manipulation-doesn't trap. **The `env::current_exe` claim was a
  FALSE audit finding** — the `readlink` intrinsic has a Windows shim (`core/intrinsics.cryo:294-302`)
  that calls `GetModuleFileNameA`, so `current_exe()` already works on Windows; the original doc was
  correct and I reverted my mistaken edit.
- [ ] **M5 — Windows `process` functional gaps.** `command.cryo:738-742`: `Stdio::Null`/`Stdio::Fd`
  silently fall back to `Inherit`; `env_clear`/per-var env/non-trivial `cwd` not applied;
  `collect_output` (`:928-963`) drains stdout then stderr sequentially → can deadlock a child
  that fills stderr first (the POSIX path fixed this with `poll`).

### Semver-durability of the `core` trait foundation (freezing as-is bakes in breaking changes)

- [x] **M6 — `Clone` missing for containers.** ✗ AUDIT WRONG / ✓ DONE (2026-06-13). `Clone` is
  **already implemented** for `Array<T>` (`array.cryo:732`, `where T: Clone`), `String`
  (`string.cryo:294`), and `Box<T>` (`box.cryo:179`, `where T: Clone`). The audit read the stale
  doc comment in `core/clone.cryo` ("do not yet implement Clone") without checking the container
  modules. Fixed that stale comment; nothing else to do.
- [ ] **M7 — No `PartialEq`/`PartialOrd`; floats excluded from `Eq`/`Ord`.** `core/cmp.cryo:33-36,76`.
  `f32`/`f64` cannot be used in `cmp::min/max/clamp`, `Range<f64>`, sorting, `HashMap<f64,_>`.
  **BLOCKED (2026-06-13):** implementing total `Eq`/`Ord` for floats (the chosen approach) exposed a
  compiler codegen bug — `&f64` args in monomorphized generic trait dispatch (`Option<f64>::equals`)
  are passed by value instead of by pointer, failing LLVM verification. Reverted; **needs the
  compiler bug fixed first.** **Root-caused** to `codegen/ops/expr_ops.cryo:757-759` (a Float/Double-
  expected vs Pointer-actual arg-coercion branch derefs the autoref'd pointer) firing because the
  caller's expected param slot for `&f64` is `double` while the definition takes `ptr` — a
  declaration/definition ABI-classification mismatch for reference-to-float params. The actual fix
  is deep codegen work that **must be gated by `make selfhost-check` on Linux/WSL**, so it was not
  attempted in the Windows session where it was found. Full details in the
  [self-hosted compiler bugs](memory) note (bug #4).
- [x] **M8 — `sync` locks panic on OS error instead of returning `SyncError`.** ✓ DONE (2026-06-13).
  **Reassessed:** poisoning is *not viable* in Cryo (`panic` aborts the process, so no thread can
  observe a poisoned lock), and panicking on the only failure mode (`pthread` programmer errors
  `EINVAL`/`EDEADLK`) is correct. Per the owner decision, **deleted the dead `SyncError` type**
  (`stdlib/sync/error.cryo`, the `sync::error` module, the unused `import` in `mutex.cryo`, and the
  doc references in `lib.cryo`/`sync/_module.cryo`) and **documented the lock-panic / no-poisoning
  behavior** in the sync module header. Removes dead public API before the freeze; `try_lock` /
  `try_read` / `try_write` remain the non-panicking path. Verified: sync example still builds.

### Compiler correctness

- [ ] **M9 — `static match` arm selection: two silent-wrong-code holes.** `types/monomorphizer.cryo`
  ~1614 (single non-wildcard arm with no `_` default returns `-1` without E0645 when `T` mismatches)
  and ~1625-1636 (duplicate-`TypeID` arms silently take the first, no ambiguity diagnostic).
- [ ] **M10 — Trait-bound depth exhaustion silently drops methods/impls.** `monomorphizer.cryo:2230`
  returns `false` at `depth >= 16`; `filter_bounds_violating_methods/impls` (~1011) then *remove*
  the item → confusing "no method"/undefined-symbol far from the cause instead of a clean error.
- [ ] **M11 — `FunctionDeclaration` pass is a no-op that still claims its provision.**
  `pass_registry.cryo:478-481` (`stub()` → `success=true`) marks `FunctionsDeclared`
  (`pass_id.cryo:668`, required by `IRGeneration` `:677`) vacuously. Latent landmine — delete the
  pass or implement it.

### Infra / reproducibility

- [ ] **M12 — Pins built from a dirty worktree.** `bin/cryo.pin.txt:17-18` and
  `bin/cryo.exe.pin.txt:17-18` record `e42894ce-dirty` / `worktree: dirty` — not reproducible from
  any commit. Re-pin from a clean tree at the release commit before tagging.
- [ ] **M13 — Incremental path has no CI coverage.** `scripts/incremental-check.py` (guards against
  silent miscompiles) is referenced by no workflow/Makefile target; `selfhost-check.py:130-161`
  runs `--no-incremental` only. Gate the incremental path in CI.
- [ ] **M14 — No pin self-verification in CI.** `Makefile:122-126` only checks `bin/cryo` exists;
  nothing asserts `sha256(bin/cryo) == sidecar`. Add a cheap CI check; consider a
  rebuild-pin-from-previous-release reproducibility job.
- [x] **M15 — `docs/testing.md` is missing** but referenced from `Makefile:328`, `tests/cryoconfig:6`,
  and `tests/tests/negative/README.md`. ✓ DONE (2026-06-13) — wrote `docs/testing.md` covering the
  framework, directives, `cryo test` flags, project layout, negative tests, and the repo suite.

### Tooling

- [x] **M16 — CryoFormat is orphaned.** ✓ DONE (2026-06-13) — added a prominent "experimental, not
  part of 1.0, not wired into the editor (the `cryolang.cryofmt` instructions don't work)" banner to
  `tools/CryoFormat/README.md`, and labeled it "(experimental; not built by default)" in the
  `README.md` tools tree. Left in-tree (not moved to `legacy/`) since it's a working prototype;
  revisit if you'd rather relocate it.
- [x] **M17 — Committed regenerated artifact.** ✓ DONE (2026-06-13) — `git rm --cached
  tools/CryoAnalyzer/dist/extension.js` (1.2 MB esbuild bundle); now covered by the `dist/` ignore rule.

---

## MINOR

- [ ] **m1 — Math `min`/`max` order-dependent on NaN.** `math/_module.cryo:126-127` use raw `<`/`>`.
  Also missing: `NAN`/`INFINITY`/`F64_MIN` consts, `mul_add`/`powi`/`log1p`/`expm1`, f32 `min/max/clamp`;
  `hypot` (`:53`) naively overflows.
- [ ] **m2 — `net` IPv6 half-wired into the public API but undialable.** `IpAddr::V6` flows through
  `dns::resolve` then dead-ends (`sockaddr.cryo:51-53` Unsupported, `ip.cryo:156-162` no v6 parse,
  `http/client.cryo:124-130` emits a `"::"` placeholder Host). Gate v6 out of 1.0 or freeze the
  `Unsupported`/placeholder behavior as stable.
- [ ] **m3 — Zero tests for `base64`, `sha1`, `sync/condvar`, `sync/barrier`, `sync/mpsc` (unit),
  `process/signal`, `fmt/float`, `time/duration`, `core/convert`/`ops`/`panic`.** Several
  (sha1, fmt/float) are high-bug-risk with trivial known-answer vectors.
- [ ] **m4 — `try_*` constructors can still panic.** `Array::try_with_capacity_in` → `Layout::array`
  panics on size overflow (`alloc/layout.cryo:40-42`); same for `HashMap`/`RawBuffer`. Breaks the
  `try_`-means-no-panic contract.
- [ ] **m5 — Integer-literal `match` arms fall through to wildcard.** Real codegen bug; the test is
  `![ignore]`'d (`tests/tests/lang/control_flow.cryo:148-149`). Fix or document as a known limitation.
- [ ] **m6 — `match`-vs-`switch` move-join gap.** `passes/move_check.cryo:16-18` doesn't join moves at
  `switch` case ends (it does for `if`/`match`) → a use-after-move via `switch` could be missed.
- [ ] **m7 — Negative tests assert via bare `strstr` for `error[CODE]` anywhere in output**
  (`commands.cryo:1434`) — a crash containing the code, or right-code-wrong-reason, false-passes.
  Knowingly-red negative tests (negative README §"intentionally fail") should move to an explicit
  `xfail` bucket so green == implemented.
- [x] **m8 — Duplicate, drifted tmLanguage grammar.** ✓ DONE (2026-06-13) — fixed the bogus
  `consumes_self` → `sink`/`implicit`/`transparent` directive list in the maintained
  `tools/CryoAnalyzer/syntaxes/cryo.tmGrammar.json`, and **deleted** the orphan, drifted
  `docs/Cryo.tmLanguage.json` (referenced by nothing). One canonical grammar now.
- [x] **m9 — Doc/grammar drift.** ✓ DONE (2026-06-13) — guard clauses documented (README,
  `cryo.md §7.4`, `grammar.md` `MatchArm`); math API names fixed (`cryo.md:2119`); prelude list
  corrected (7→12, `cryo.md §19.1`); `grammar.md` precedence table synced to `cryo.md §5.7` (18
  levels incl. `??`/`|>`/`<|`/range). **One deferral:** the tuple-literal `cryo.md §2.6/§21` claim
  is left until B3 is resolved (tuples lower fine but currently leak on drop, so the docs aren't
  simply wrong — fixing the claim should accompany the drop-glue fix).
- [ ] **m10 — `release.yml` reproducibility.** `scripts/llvm-version.env:8` pins `LLVM_MAJOR=20`
  (any 20.x patch from apt) for the Linux static release while Windows pins exact `20.1.8`. Pin the
  exact Linux patch too. No `concurrency:` group on either workflow.
- [ ] **m11 — JSON duplicate-key policy is silent last-wins, undocumented** (`json/value.cryo:128`);
  float parse via `strtod` is `LC_NUMERIC`-locale-sensitive (`json/parser.cryo:307`).
- [ ] **m12 — `Send` not enforced on lock constructors** → `Mutex<Rc<_>>` constructible
  (`core/marker.cryo:19`). Lock poisoning unimplemented (`mutex.cryo:26-32`).
- [ ] **m13 — Network tests use PID-modulo ports** (`net_tcp.cryo:46` `20000 + getpid()%20000`) →
  collision/flakiness. `net_dns` depends on `localhost` resolution (non-hermetic).

---

## NITS

- [x] **n1 —** ~~Committed build artifacts in `tests/helpers/`.~~ ✗ INVALID (2026-06-13) — only
  `tests/helpers/abi_helpers.c` is tracked; the `.o`/`.a` are already gitignored. Nothing to do.
- [ ] **n2 —** Mangling spec labeled "Version: 0.2" (`cryo-mangling-spec.md:3`) for a frozen-ABI
  1.0 contract; §13 presents a "suggested interface" that's now as-built.
- [ ] **n3 —** `abi.md` is SysV-only and lists Win64 as "future" (§7) while Win64 lowering ships
  (`codegen/abi.cryo classify_win64_*`). Add a Win64 section or a clear caveat.
- [x] **n4 —** ✓ DONE (2026-06-13) — CHANGELOG "102 active error codes (E0001–E0900)" corrected to
  "over 200 defined error codes (E0000–E0999)" (the `ErrorCode` enum declares ~209 distinct codes).
- [ ] **n5 —** `never`/`NeverType` implemented + highlighted but absent from `cryo.md §2.1` and
  `grammar.md Primitive`. `i128`/`u128` listed inconsistently across the two.
- [x] **n6 —** ✓ DONE (2026-06-13) — fixed the broken `cryo.md:1851` anchor (`§1.4` → `§2 Type System`).
- [ ] **n7 —** Codegen emits clean `E0600 not implemented` for `await`/`yield`/`typeof`-in-expr
  (`ir_generator.cryo:1603-1625`) — confirm these are intentionally off the 1.0 list.
- [ ] **n8 —** `sema.cryo` is a single 10,407-line god-file (one struct/impl). Maintainability
  liability for contributors; consider splitting post-1.0.
- [ ] **n9 —** Monomorphizer worklist has no iteration/size backstop — a divergent generic producing
  ever-larger distinct types hangs the compiler instead of erroring (`monomorphizer.cryo:342-380`).
- [ ] **n10 —** `install.sh`/`install.ps1` are careful (sha256 + atomic swap + post-install run), but
  the installer *script itself* is fetched over `curl|bash` from `cryo-lang.org` unverified. Consider
  GPG/minisign on release archives (currently SHA256 only).

---

## What's genuinely solid (don't second-guess)

- Selfhost integrity (Linux): the byte-identity gate fails closed on missing IR and compares exactly
  (`selfhost-check.py:747-759`); 3-round rationale is sound.
- Diagnostics: full `ErrorCode` taxonomy, panic-mode parser recovery, fatal-vs-non-fatal pass gating.
- Move-checker rejects partial/field moves as hard errors (E0453) rather than mistracking.
- Stdlib memory safety fundamentals: null-checks, bounds checks, overflow-guarded growth, null-on-drop,
  real FNV-1a/Murmur3, atomic refcounts in `Arc`, real LLVM atomics, hardened JSON parser (depth cap 256).
- Drop glue for aggregates (struct/class/enum/fixed-array) is real and recursive — only bare tuples gap (B3).
- Test depth is release-grade: ~1,100 `![test]` fns, real fork-per-test runner with Windows re-exec path,
  92 curated negative cases, strongest coverage in drop/move semantics.
- Install scripts, release checksum verification, pin sidecar honesty, build hygiene (no tracked build
  artifacts beyond the intended pins).
- Docs: `cryo.md` body, grammar, ABI, and mangling specs are accurate and detailed where spot-checked
  against the compiler; the stdlib is unusually well doc-commented. The gaps are *release-packaging*
  (README roadmap, CONTRIBUTING, a few summary claims), not the specs themselves.

---

## Suggested order of attack

1. ~~**Cheap blockers first:** B1 (CI path), B4 + doc scope reconciliation.~~ ✓ DONE (2026-06-13).
2. ~~**B2** (examples in CI) — highest regression-prevention leverage.~~ ✓ DONE (2026-06-13).
3. **B3** (tuple drop) — pick implement-glue vs reject-in-sema. ← next blocker, needs compiler work.
4. **Windows validation** (M1, M2) — close the "shipped but unvalidated" gap.
5. **Trait-foundation semver** (M6, M7, M8) — these freeze under semver, hardest to change later.
6. **Monomorphizer correctness** (M9, M10, M11).
7. Remaining Majors, then a Minor sweep (prioritize items that change *which inputs are accepted*,
   since those are harder to fix post-freeze: m2, m4, m1).
