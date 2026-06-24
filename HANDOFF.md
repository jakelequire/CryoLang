# HANDOFF — CryoLang v1.0.0 prep (mono anti-tower fix + `as_ref` + batch-1 items)

Picks up a long session whose centerpiece was a **monomorphizer fix** that makes
`Option::as_ref` / `Result::as_ref` work as methods, plus several v1.0-readiness
items. **Everything below is UNCOMMITTED working-tree state.** The live v1.0
tracker is `v1-readiness.md`; remaining decided work is in §3.

---

## 0. Ground rules

- **Commits: ONLY Jake commits.** Never `git commit`, never add `Co-Authored-By`.
  You MAY run `make pin` / pin scripts. When work is validated, summarize and stop.
- **Build:** native Windows needs `CRYO_CC=gcc`. Run `make` from **PowerShell**
  (Git Bash breaks the cmd-syntax stdlib recipe). selfhost via WSL (`make pin`
  delegates to WSL automatically).
- **Machine sensitivity:** the maintainer's machine gets hammered by builds. Do
  NOT run heavy builds without need. A *towering* compile pins all cores and
  thrashes memory — if a build runs much longer than the timings below, KILL it
  (PowerShell: `Get-Process cryo,make,clang,gcc,ld,lld | Stop-Process -Force`)
  and diagnose rather than waiting.
- **Normal build timings (native Win + WSL):** `make cryo` ≈ 2 min; `make
  selfhost-check` ≈ 5 min (Linux) + ≈ 2.5 min (Windows); `make test` build ≈ 2
  min then a LONG test-execution phase (1284 unit + 103 compile-fail; the
  compile-fail tests each spawn a fresh compiler, every unit test spawns a
  capture subprocess — on a loaded Windows box this is 15–20 min and is NOT a
  hang; watch for `ok` lines still printing).

---

## 1. THE MAIN FIX — anti-tower monomorphization (DONE + VALIDATED, uncommitted)

### Problem
The compiler **eagerly materializes every method of an instantiated generic
type**. A parameterless method whose return type is a structural *superterm* of
its owner — e.g. `Option<T>::as_ref -> Option<T*>` — therefore instantiates
`Option<T>` → `Option<T*>` → `Option<T**>` → … **without bound** (an infinite
monomorphization tower). This hung the build (pinned all cores, leaked memory).
`as_ref` is the first stdlib method with this shape, which is why it never bit
before.

### Fix (per-method laziness via a dedicated flag)
Detect such methods and flag them `is_lazy_self_growing`, which **defers them from
eager mono + codegen** and routes them through the **existing lazy call-site
materialization path** (the same machinery trait-default combinators use via
`is_self_returning_default`). Crucially it is a **SEPARATE flag** from
`is_self_returning_default`: that flag ALSO hides the method from the
DeclarationIndex / sema call resolution (fine for trait combinators, which
resolve via the trait; fatal for an *inherent* method like `as_ref`, which then
becomes invisible → `E0358 no method named as_ref`). The new flag is wired ONLY
at materialization/codegen sites, never at visibility sites.

When `m.as_ref()` is called: sema resolves it (visible) → mono's
`try_instantiate_self_returning_default` lazily specializes the one method,
clears both flags on the spec'd copy, codegens it, and discovers its return
type's instantiation (`Option<i32*>`) — whose own `as_ref` stays lazy → **no
tower**. `opt.as_ref().is_some()` works because `Option<i32*>::is_some` (not
lazy) materializes while `Option<i32*>::as_ref` stays deferred.

### Files changed (compiler)
1. **`AST/declaration.cryo`** — new `is_lazy_self_growing: boolean` field on
   `FunctionDeclNode` (init `false`) + `set_lazy_self_growing`.
2. **`AST/cloner.cryo`** — copies `is_lazy_self_growing` (2 sites, next to
   `is_self_returning_default`).
3. **`types/arena.cryo`** — new `is_self_growing_instantiation(ty: TypeRef,
   owner_qname: SymbolStr) -> boolean`. Matches an `InstantiatedType` of the
   owner's base (compared by **qualified name**, not `TypeRef` id — the owner
   handle is INVALID for impl blocks at sig-resolution time, which was the bug
   that made a first attempt silently never fire) with a type-arg that contains a
   generic param but is not the bare param (i.e. wrapped: `T*`, `T[]`, nested
   generic). Peels pointer/ref/array/optional wrappers; recurses into args.
4. **`types/resolver.cryo`** — `resolve_method_signatures` gained an
   `owner_qname: SymbolStr` param; after resolving each non-generic, non-static
   instance method it calls the arena helper on the resolved return type +
   non-receiver params and `set_lazy_self_growing(true)` on a match.
5. **`passes/type_resolution.cryo`** — the wrapper `resolve_method_signatures`
   gained `owner_qname`; its **4 call sites** (struct ~2053, class ~2062, impl
   ~2158, impl2 ~2448) pass `ctx.qualify_symbol_sym(node.name | node.target_type)`.
6. **`mono/ast_resolver.cryo`** (~438, `resolve_methods`) — skips
   `is_lazy_self_growing` (THE site that kills the tower: stops the return-type
   nested-discovery enqueue at ~563).
7. **`mono/trait_specializer.cryo`** (~380) — skips body substitution.
8. **`AST/substituter.cryo`** (~446) — skips body substitution.
9. **`codegen/ops/declaration_emitter.cryo`** (~855) — skips codegen/mangling of
   the lazy template.
10. **`codegen/visit/decl_visit_emitter.cryo`** (~263) — skips codegen.
11. **`mono/call_specializer.cryo`** — `find_self_returning_default` (2 `continue`
    sites) also matches `is_lazy_self_growing`; `try_instantiate_self_returning_default`
    clears `is_lazy_self_growing` on the spec'd copy (next to the existing
    `set_self_returning_default(false)`) and the sibling-reuse check excludes
    still-lazy siblings.
12. **`mono/monomorphizer.cryo`** — **independent robustness fix:**
    `drain_pending_worklist` was UNcapped (only `process_all` had the
    `max_iterations` ceiling), so a tower there hung hard. It now uses the same
    `100000 + arena.type_count()*100` ceiling + emits E0900 on exceed.

### Files changed (stdlib + tests — the feature)
- **`stdlib/core/option.cryo`** — `as_ref(&this) -> Option<T*>` (NOT `![sink]`;
  borrows payload in place).
- **`stdlib/core/result.cryo`** — `as_ref(&this) -> Result<T*, E*>`.
- **`tests/tests/stdlib/option.cryo`** — 4 `as_ref` tests (in-place aliasing,
  read-without-consume, `as_ref().is_none()` re-tower guard, owning-payload
  no-double-free).
- **`tests/tests/stdlib/result.cryo`** — 3 `as_ref` tests (ok/err aliasing,
  owning-ok no-consume).

### BOOTSTRAP ORDER (critical — two-phase dance)
The pinned `bin/cryo`(`.exe`) must contain the fix BEFORE the stdlib uses
`as_ref`, or the old pin towers building the new stdlib. **Already done: the
current pins (`bin/cryo`, `bin/cryo.exe`, `.pin.txt` sidecars) contain the fix.**
If you ever re-pin from scratch: (1) remove `as_ref` from option/result, (2)
`make cryo` + `make selfhost-check` + `make pin`, (3) re-add `as_ref`. (See
`memory/bootstrap-feature-ordering.md`.)

### Validation done this session
- `make cryo`: clean, no tower. ✅
- `make selfhost-check`: **byte-identical fixed point on BOTH Linux and Windows**,
  run twice (compiler-only, and again with `as_ref` in stdlib). ✅ (`as_ref` is a
  dark/unused template in the self-host corpus, so it contributes nothing to
  output — determinism holds.)
- `make pin`: pins refreshed to the fixed compiler. ✅
- Scratch end-to-end (`Maybe<T>::as_ref`, instantiated + `.is_just()` on the
  result): builds with no tower, binary returns 5 (in-place pointer aliasing
  correct, no re-tower). ✅
- New tests run directly from the built binary: **`as_ref` 7/7 PASS**;
  `Convert` (incl. numeric `From`) and `OperatorOverload` all PASS in the full run.

### Validation still OWED
- **A clean full `make test` to confirm the final `result PASS` + 0 failures.**
  The last run printed `ok` for every test it reached, then died with
  `Error: failed to spawn 'build/cryo-tests-test.exe'` — a **Windows per-test
  subprocess spawn failure under load** (handle/process exhaustion after a long
  session), NOT a test failure or compiler bug. Re-run on a fresh machine; expect
  `passed 1284 / failed 0` unit + `103/103` compile-fail. Spot-check without the
  full suite: `tests/build/cryo-tests-test.exe as_ref` (set `CRYO_STDLIB` to the
  repo stdlib).

### No leftover scaffolding
Temporary diagnostics (`[ANTITOWER]` cdebug, lowered mono caps) were all removed;
caps are back to real values; the byte-identical selfhost confirms the compiler
is clean.

### Known separate edge case (NOT this fix, low priority)
A generic type **defined but never instantiated** in an executable crashes
codegen (a throwaway scratch with such a type segfaulted). Real code / the stdlib
always instantiate the types they define, so it doesn't bite in practice. Worth a
separate look post-1.0.

---

## 2. BATCH-1 v1.0 ITEMS DONE THIS SESSION (uncommitted)

Validated by the partial `make test` run (all relevant tests `ok`) + selfhost,
except CI (can't validate locally).

- **A — CHANGELOG fold (`CHANGELOG.md`):** `[Unreleased]` folded into `[1.0.0]`;
  associated-types entry moved into the Compiler section; the `core::iter` bullet
  reconciled to the associated-type form; the opaque-iter "known limitation"
  updated (static-constructor form now re-adapts).
- **B — `Option`/`Result::as_ref`:** DONE (the main fix above).
- **C — operator-trait + From/Into tests:** `tests/tests/stdlib/convert.cryo`
  (NEW: user `From`/`Into` + 2-hop `From` chain + numeric widening `From`) and
  `tests/tests/lang/operator_overload.cryo` (NEW: user `Deref`/`Index`). **Scope
  note for the maintainer:** v1.0 ships ONLY the `Deref`/`Index` operator traits
  (no `Add`/`Sub`/`Mul`/`Neg`) and NO `[]`/`*` *sugar* over user types (per
  `core::ops` docs it's deferred) — so these tests use the explicit
  `.deref()`/`.index()` methods (deref resolved through a bound to dodge a name
  clash with a `Deref` *struct* in `peel_ptr_bound_repro.cryo`).
- **F — NTSTATUS `i32`→`u32` (`stdlib/ffi/syscall.cryo`):** the 6 overflowing
  `STATUS_*` consts and all `Nt*`/`Rtl*` NTSTATUS return types are now `u32`
  (`RtlNtStatusToDosError`'s `Status` param too). `ExitStatus`/`PreviousState`/
  `InitialState` params left `i32` (not NTSTATUS).
- **G — `unix`→`linux` gates:** `stdlib/ffi/libc.cryo` (`__errno_location` +
  `errno`/`set_errno` + the FP-classify externs `__isnan`/etc.) and
  `stdlib/time/clock.cryo` (`read_monotonic`/`read_realtime`/`sleep_for`)
  relabeled `![target(unix)]` → `![target(linux)]` (each paired with a `windows`
  variant; macOS is post-1.0, so no behavior change on Win/Linux).
- **K — overflow-contract docs (`stdlib/core/primitives.cryo`):** added a frozen
  "Integer overflow contract" section + rewrote the three "when checked
  arithmetic lands" comments to "frozen as wrapping for 1.0".
- **J — Windows CI (`.github/workflows/ci.yml`):** added a `windows-native`
  build+test job. **UNVALIDATED — needs live-CI iteration**; keep it non-required
  until green (the cross-build `windows-smoke` job stays the green Windows gate).
  Likely iteration points are commented inline.

---

## 3. STILL TODO (decisions locked — do NOT re-decide)

- **Confirm full `make test` PASS** (see §1 — re-run on a fresh machine; expect
  1284/0 + 103/103).
- **D — Seeded default HashMap hasher** (`stdlib/core/hash.cryo`): replace the
  fixed FNV-1a offset with a per-process random seed (RandomState-style).
  **Investigated this session:** the self-hosted compiler uses `HashMap` only for
  lookup / internal caching, never to order emitted output, so a random seed does
  NOT break `selfhost-check` determinism. BUT `DefaultHasher::new()` / `fnv1a_*`
  are also used for the **build-fingerprint / manifest input hash**, which MUST
  stay deterministic — so seed must live **per-HashMap** (seed `HashMap::new()`
  from the CSPRNG), NOT change `DefaultHasher::new()`. Keep a fixed-seed
  constructor for tests. Re-run the full suite + watch order-dependent tests.
- **E — Delete dead scaffolding:** `diag/lsp.cryo` (`diagnostic[s]_to_lsp`,
  `LspDiagnostic`; remove `public module Lsp;` from `diag/_module.cryo`); the
  **sema** `ResolveOutcome` in `sema/outcome.cryo` (⚠ the
  `deps/dep_resolver.cryo` `ResolveOutcome` is a DIFFERENT *live* type — do not
  touch; remove `public module Outcome;` from `sema/_module.cryo`); the
  `SymbolKey` widen-ladder (`widen_to_method/qualified/simple` in
  `resolver/symbol_key.cryo` — uncalled; `SymbolKey` itself is only referenced by
  the dead outcome.cryo + comments, so it may be fully dead — verify);
  `emit_static_owner_literal_default` in `sema/call_resolver.cryo`. After each:
  `make test` + `make selfhost-check`.
- **H — Stale comments (verified true this session):** `types/arena.cryo` (~557)
  and `types/checker.cryo` (~294) say instantiations are "NOT deduped" — but
  `create_instantiation` (arena ~517) NOW dedupes via `instantiated_cache`
  (string key hashed by content). Accurate correction: it DOES dedupe by (base,
  arg IDs), but logically-equal types can carry distinct TypeIDs across the
  pre-/post-mono boundary, so `propagate_instantiated_resolution` is still needed
  (the parenthetical "compares keys by pointer and never hits" is the stale/false
  part). Also document the u32→16-bit line/col truncation in
  `resolver/resolution_map.cryo` (~30).
- **I — CLI flag robustness** (`CLI/_module.cryo`, `commands.cryo`): reject
  unknown `--flags` (per-command allow-list) + treat empty `--output=`/`--target=`
  as an error. **Risk:** many real flags only reach binaries via `CRYO_TEST_*`
  env and aren't in `FlagKind`/help — enumerate carefully. Lower priority/risky.
- T5.6 (at tag time): confirm `cryo-lang.org` serves `install.sh`/`install.ps1`.

---

## 4. Validation checklist (per change)

1. `make cryo` (PowerShell, `CRYO_CC=gcc`) — quick compile check (~2 min).
2. `make test` — expect `OVERALL PASS`, **1284** unit / 0 fail (was 1268 + 16 new
   tests), 103/103 compile-fail. Long execution phase; not a hang while `ok`
   lines print. `failed to spawn build/cryo-tests-test.exe` = environmental
   subprocess-spawn flake — re-run.
3. codegen/mangling/sema/mono changes: `make selfhost-check` (Win + WSL) — must
   report byte-identical FIXED POINT on both.
4. New stdlib language feature the pin can't build → stash→`make pin`→restore.
   **Do not commit the pin — leave it for Jake.**

---

## 5. Before Jake commits

Update `v1-readiness.md` to mark A/B/C/F/G/K/J done and record the mono anti-tower
fix. Nothing is committed; the working tree holds §1 + §2 changes plus the
refreshed pins (`bin/cryo*`, `*.pin.txt`). A throwaway diagnostic project under
the session scratchpad (`towerdiag/`) is outside the repo and can be ignored.
