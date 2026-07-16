# HANDOFF — Low-Level Plan, Stage 1 (fp classification off libc)

Continuation notes for a fresh agent on a new machine. **Stage 1 is code-complete
but BLOCKED on a pre-existing compiler bug that the change exposed. The immediate
job is to fix that compiler bug, then finish Stage 1 validation.**

> Roadmap: `LOW_LEVEL_PLAN.md` (repo root). This file is the *working* handoff.
> Prior context: Stage 0 (the `sys` seam + `native_syscalls` switch) is **already
> committed** at HEAD (`d9f8ee35`). Only Jake commits; the agent never commits
> (may repin).

---

## TL;DR — where we are

- **Goal (Stage 1):** move IEEE-754 floating-point *classification* off libc into
  pure Cryo, via a new compiler intrinsic `fpclass` → LLVM `llvm.is.fpclass.f64`.
  Maintainer (Jake) chose the **maximum-soundness** design: a real `classify() ->
  FpCategory` core + the `is.fpclass` intrinsic (not a bit-twiddle-only library
  version).
- **Phase 1 (compiler `fpclass` intrinsic): DONE, REPINNED, validated BOTH OS.**
- **Phase 2 (stdlib migration): code-complete, but `make test` reveals ONE
  failure** — `Lang::PatternMatching::nested_match_neg` — which bisected to a
  **pre-existing compiler "same-leaf free-function collision" bug**, NOT a defect
  in the Stage-1 logic itself.
- **Jake's decision: FIX THE COMPILER BUG** (no rename workaround — he wants this
  low-level code very sound and is willing to make compiler changes + repin).
  Keep the public name `std::math::classify`.
- **All Stage-1 source changes are in the working tree, UNCOMMITTED.** The pins
  (`bin/cryo`, `bin/cryo.exe`) are the Phase-1 repin (they already know `fpclass`).

> **IMPORTANT for the PC switch:** the uncommitted working-tree changes and the
> repinned `bin/` binaries must reach the new machine (commit them, or otherwise
> transfer). Scratchpad backups referenced below are LOCAL to the old PC and will
> NOT transfer — but they are redundant with the working tree, which already holds
> the full Stage-1 change. Verify with `git status --short` on the new PC (see
> "Working-tree state" below for the expected file list).

---

## Repo orientation

- Self-hosted compiler in Cryo, targets LLVM 20. Source `compiler/src/`, stdlib
  `stdlib/`, tests `tests/tests/`.
- Bootstrap chain: pinned `bin/cryo` (Linux ELF) + `bin/cryo.exe` (Windows PE)
  compile the stdlib + compiler.
- Dev on **Windows** (native PE, `CRYO_CC=gcc`); **Linux** side via **WSL**.
- `boolean` lowers to LLVM `i1` (`compiler/src/compiler/codegen/type_map.cryo:177`).

---

## Phase 1 — the `fpclass` compiler intrinsic (DONE + REPINNED + VALIDATED)

A new intrinsic `fpclass(value: f64, test: i32) -> boolean` lowering to
`call i1 @llvm.is.fpclass.f64(double, i32 <immarg mask>)`. Exception-free,
branchless, backend-optimal. It **replaces** the old exception-raising `IsFinite`
`(x-x)==0` trick, which is now deleted.

### Compiler edits (all in place, working tree)
- `compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo`
  - Replaced `IntrinsicKind::IsFinite` with `IntrinsicKind::FpClass` (1:1, same
    enum position — no ordinal shift).
  - `from_name`: `"isfinite"` → `"fpclass"`.
  - `src_type()` arm: `FpClass => LType::f64()` (arg0 is f64; the i32 mask arg is
    handled in the emitter, not auto-deref'd).
- `compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo`
  - Added `get_or_decl_fpclass(...)` → declares `i1 @llvm.is.fpclass.f64(f64, i32)`.
  - Added `emit_fpclass(x, mask_val)`: reads the constant mask out of `mask_val`
    (`LLVMIsConstant` / `LLVMConstIntGetZExtValue`, same pattern as the atomic
    ordering `extract_ordering_arg`), rebuilds it as a fresh i32 constant (satisfies
    the `immarg` requirement), and emits the call. Returns i1 directly (== boolean).
    Non-constant mask degrades to 0 (unreachable in practice).
  - Arg-count map: `FpClass => 2` (before the `_ => 1` default).
  - Dispatch arm: `FpClass => this.emit_fpclass(a0, a1)` (replaced the `IsFinite`
    `fsub`/`fcmp` arm).
  - Header doc comment: `isfinite` → `fpclass`.
- `compiler/src/compiler/codegen/visit/call_emitter.cryo` and
  `compiler/src/compiler/codegen/passes.cryo`: illustrative comments `isfinite`
  → `fpclass` (cosmetic).

### Phase 1 validation (all GREEN, on the repinned binaries)
- Windows `make cryo` + WSL `make cryo`: both build clean.
- `wsl make pin`: repinned BOTH pins (Linux native + Windows cross). The pins now
  know `fpclass`. `bin/cryo.pin.txt` / `bin/cryo.exe.pin.txt` updated.
- Linux `make selfhost-check`: **FIXED POINT OK**. Windows `make selfhost-check`:
  **2× FIXED POINT OK** (target-IR + native PE, 230 modules).
- `verify-pin` OK on both OSes (sha256 match).

> The Phase-1 compiler change is **innocent** of the blocker below (proven by
> bisection experiment "B2").

---

## Phase 2 — stdlib migration (code-complete; in the working tree)

fp classification now runs in pure Cryo via `fpclass`; the libc classification
surface is deleted. `-lm` is still linked (transcendentals + `fabs`/`copysign`/
rounding stay on libc — out of Stage-1 scope). `infinity()`/`nan()` were already
pure arithmetic and are unchanged.

### `fpclass` test masks (mirror LLVM `is.fpclass` test bits — verified)
```
0x001 SNaN  0x002 QNaN  0x004 -Inf  0x008 -Normal  0x010 -Subnormal
0x020 -Zero 0x040 +Zero  0x080 +Subnormal  0x100 +Normal  0x200 +Inf
```
- `is_nan`       = `0x003`  (SNaN|QNaN)
- `is_infinite`  = `0x204`  (-Inf|+Inf)
- `is_finite`    = `0x1F8`  (everything except NaN/Inf)
- `is_normal`    = `0x108`  (-Normal|+Normal)
- classify Zero  = `0x060`  (-Zero|+Zero)
- classify Sub   = `0x090`  (-Subnormal|+Subnormal)

### stdlib edits (working tree)
- `stdlib/core/intrinsics.cryo` — declares `intrinsic function fpclass(value: f64,
  test: i32) -> boolean;` (with the mask-bit doc comment). NB: `core::intrinsics`
  is re-exported by the prelude.
- `stdlib/math/_module.cryo`
  - Added imports `core::intrinsics`, `core::mem`.
  - Rewrote `is_nan`/`is_infinite`/`is_finite` as `fpclass` calls (dropped the
    per-OS `![target(...)]` split entirely — now one platform-independent impl each).
  - Added `type enum FpCategory { Nan; Infinite; Zero; Subnormal; Normal; }`,
    `is_normal`, `is_sign_negative` (`transmute<f64,u64> >> 63`), and
    `classify(f64) -> FpCategory` (if-chain of `fpclass` tests).
- `stdlib/fmt/float.cryo` — file-local `fp_is_nan`/`fp_is_inf` now call `fpclass`
  (dropped the per-OS split); header comment updated. Still uses `libc::strtod`
  etc. (float parse/format — separate concern).
- `stdlib/ffi/libc.cryo` — DELETED the classification externs (`__isnan`/`__isinf`/
  `__finite`/`__signbit`/`__fpclassify` on Linux; `_isnan`/`_finite` on Windows)
  and the `FP_NAN`…`FP_NORMAL` consts. Updated the two nearby comments.
- `tests/tests/stdlib/math.cryo` — extended the classification section: `is_normal`,
  `is_sign_negative`, and `classify` edge cases (NaN, ±Inf, ±0, subnormal, normal),
  building edge values via `mem::transmute<u64,f64>` of exact bit patterns. Added
  `import std::core::mem`.

---

## ⛔ THE BLOCKER — a pre-existing compiler "same-leaf free-function" bug

**Symptom:** `make test` fails exactly ONE unit test:
`CryoTests::Tests::Lang::PatternMatching::nested_match_neg` — asserts
`classify(Option::Some(-3))` is `Sign::Neg` (code `-1`) but gets `Sign::Pos`
(code `1`). i.e. the `Option<i32>` payload `-3` is read as a POSITIVE value
(a lost sign-extension: `-3` = `0xFFFFFFFD`, zero-extended → large positive →
`n > 0` true). `nested_match_pos` (Some(3)) and `nested_match_none` still PASS.

**Root cause (bisected, high confidence):** naming the new stdlib free function
`std::math::classify` collides with the test suite's own non-generic free function
`CryoTests::Tests::Lang::PatternMatching::classify` when BOTH are compiled in a
SINGLE compilation unit (which `cryo test` does — all 230 modules + stdlib source
in one `cryo` process). The collision miscompiles the test's `Option<i32>`
negative-payload extraction. Renaming the stdlib function makes it pass.

### Bisection evidence (each row = one `make cryo` + `make test ARGS="nested_match_neg"`)
| Experiment | State | Result |
|---|---|---|
| Baseline | my changes stashed → Stage-0 HEAD | **PASS** → my change is the trigger |
| B2 | ONLY compiler change (fpclass); stdlib+test at HEAD | **PASS** → compiler change innocent |
| B1 | compiler+stdlib mine; test math.cryo reverted | **FAIL** → test additions innocent |
| T-A | math+core/intrinsics mine; fmt/libc HEAD | **FAIL** → trigger is in `math` |
| T-A2 | as T-A minus `is_sign_negative`+`core::mem` | **FAIL** → not that |
| T-A3 | ONLY `is_nan`/`is_infinite`/`is_finite` rewrite | **PASS** → the core deliverable is fine |
| T-A4 | add `FpCategory` + `classify` (no `is_normal`) | **FAIL** |
| T-A5 | `FpCategory` enum ALONE (no `classify` fn) | **PASS** |
| T-A6 | `classify` fn RENAMED → `classify_fp` | **PASS** → confirms it's the leaf NAME `classify` |

**Conclusion:** the `FpCategory` enum is fine; the `is_nan/is_infinite/is_finite`
rewrite is fine; the trigger is specifically the FUNCTION NAMED `classify`.

### Key facts about the bug
- It is **NOT a mangling/link collision**. `std::math::classify` mangles fully
  qualified & signature-typed: `C$3std.4math.8classify$Fd$RN$L3std.4math.10FpCategory$G`
  (verified via `nm stdlib/.bin/libcryo.a`). Distinct from the test's classify.
  The collision is in some compile-TIME, leaf-name-keyed compiler cache/state.
- **Standalone single-file repro does NOT reproduce it.** When you `cryo build` a
  single file, `std::math` is linked from the prebuilt `libcryo.a` (NOT recompiled),
  so `math::classify`'s source is not co-compiled with the local one. Tried twice
  (with `import std::math`, and with `transmute<u64,f64>`) — both PASS. The bug
  needs BOTH `classify` sources compiled in one `cryo` invocation.
- **Puzzle to respect:** the test suite ALREADY has FOUR non-generic free functions
  named `classify` (`tests/tests/lang/{enum_discriminant_base,nested_patterns,
  pattern_matching,pattern_guards}.cryo`) that COEXIST fine at baseline. Adding a
  FIFTH in `std::math` breaks `pattern_matching`'s. So it is NOT simply "any two
  same-leaf free fns collide" — order / count / cross-phase (stdlib compiled before
  tests) likely matters. Whatever the fix, re-check that all five coexist.
- This matches the maintainer's known, deliberately-DEFERRED issue described in
  memory as **"mono cache-key / same-leaf"**.

### Existing machinery (the GENERIC case is handled; the NON-generic hole is not)
There is already same-leaf disambiguation, but it only covers GENERIC templates:
- `compiler/src/compiler/mono/monomorphizer.cryo:210-213` — `process_all()` calls
  `this.generic_registry.finalize_disambiguation(this.intern_table)` "before any
  specialization so their spec symbols don't collide."
- `compiler/src/compiler/types/generic_registry.cryo`:
  - `finalize_disambiguation()` (~:339) — one-shot O(N²) scan over `this.entries`
    (GENERIC templates) that sets `module_disambig` on same-leaf free-function
    templates in different modules.
  - `spec_base_name()` (~:80) — folds the module/owner into the spec base name for
    flagged entries so `read_frame<TcpStream>` in two modules don't collapse.
  - `module_disambig` field (~:50).
`classify` is NON-generic, so it is not a GenericRegistry template and never enters
this path — and its mangled name is already unique — so this machinery neither
helps nor is the collision point. **The colliding leaf-keyed structure for
non-generic free functions has NOT yet been located.**

### Leads for locating the actual collision (start here)
1. **Get the IR diff — most direct.** Build the failing state, dump the LLVM IR of
   `PatternMatching::classify`, then dump it again with the stdlib `classify`
   renamed (passing), and diff. The wrong instruction (zext vs sext on the
   `Option<i32>` payload, or an unsigned vs signed `icmp` for `n > 0`) pinpoints
   the miscompiled codegen path; trace back to why it's chosen when a same-leaf
   `classify` is co-compiled. Find the compiler's IR-emit flag (grep the CLI/driver
   for `emit`/`llvm`/`--ir`).
2. **Build a FAST minimal repro (not yet done):** a 2-module `cryo` *project*
   (`cryoconfig` + `src/`), module A defines `classify(f64)->EnumA`, `src/main.cryo`
   defines `classify(Option<i32>)->Sign` + the nested match, built with `cryo build`
   so BOTH are compiled from source in one invocation. Mimic
   `tests/tests/projects/collect_multimod/` (has `cryoconfig`, `src/main.cryo`,
   `src/MathLib.cryo`, `src/MathTest.cryo`). If it reproduces, iteration is seconds
   instead of a ~2-3 min `make test`. If it does NOT reproduce, the trigger needs the
   stdlib-compiled-before-tests cross-phase ordering — reproduce via `make test`.
3. **Grep for leaf-name-keyed per-function state.** Candidates: any
   `HashMap<SymbolStr/u32, …>` in codegen / sema / `CompilerInstance` /
   `decl_index` keyed by BARE function name rather than qualified/mangled name.
   Precedent to pattern-match on: memory "global-const bare-name collision fix —
   module-level consts keyed by BARE name collided → E0200" (there may be an
   analogous free-function bare-name index). Also inspect the mono call-specializer
   / `resolved_type_args` stash and the P4 function-emission pass for a name-keyed
   "already emitted / cached body/signature" map.
4. The miscompile is specifically **enum-payload sign-extension** — so also look at
   `enum_variant_emitter.cryo` (memory: "enum payload int widening … Fix in
   enum_variant_emitter.cryo") and match-arm binding codegen, for any per-function
   or per-type cache that could be shared across two same-leaf functions.

**Fix bar:** proper, root-cause fix in the compiler (Jake: no rename workaround).
Keep `std::math::classify`. Any compiler change → repin. After fixing, re-verify all
FIVE `classify` free functions coexist and `nested_match_neg` passes.

---

## Working-tree state (verify on the new PC with `git status --short`)

Expected modified/untracked (Stage 0 already committed at HEAD `d9f8ee35`):
```
 M bin/cryo                 (Phase-1 repin — knows fpclass)
 M bin/cryo.exe             (Phase-1 repin)
 M bin/cryo.pin.txt
 M bin/cryo.exe.pin.txt
 M compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo
 M compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo
 M compiler/src/compiler/codegen/passes.cryo
 M compiler/src/compiler/codegen/visit/call_emitter.cryo
 M stdlib/core/intrinsics.cryo
 M stdlib/ffi/libc.cryo
 M stdlib/fmt/float.cryo
 M stdlib/math/_module.cryo   (FULL change incl. `classify` — REPRODUCES the bug)
 M tests/tests/stdlib/math.cryo
?? LOW_LEVEL_PLAN.md
?? HANDOFF.md   (this file)
```
- `stdlib/math/_module.cryo` currently holds the **real** `classify` name (the
  bug-reproducing state) — correct; do not rename it to "fix" the test.
- **`build/` (and `compiler/build/`) are STALE** — the last experiment built the
  `classify_fp` source. Run `make cryo` fresh on the new PC before anything.
- Scratchpad backups on the OLD PC only (do NOT rely on these):
  `…/scratchpad/stdlib_mine/`, `…/scratchpad/math_test_mine.cryo`,
  `…/scratchpad/bin_backup/`, `…/scratchpad/neg_repro.cryo`. Redundant with the
  working tree.

---

## Build & verify rituals (READ before any gate run)

- **`make test` does NOT rebuild the compiler.** Run `make cryo` FIRST.
- **Windows:** run `make` from **PowerShell** with `$env:CRYO_CC = "gcc"`.
  `make cryo` / `make test` / `make selfhost-check`. Filter one test with
  `make test ARGS="nested_match_neg"`.
- **Linux:** via **WSL** — `wsl -e bash -lc "cd /mnt/c/Programming/apps/CryoLang
  && make <target>"`. Do NOT set `CRYO_CC` there.
- Cross-OS ELF↔PE clobber: a WSL build overwrites the Windows artifacts and
  vice-versa. Run the two OSes **serially**; rebuild for whichever you need.
- **Self-host canary:** require **2× FIXED POINT OK** on a Windows host (target-IR
  + native PE) and **1×** on Linux/WSL (the wine-Windows gate is skipped there —
  expected). Both OSes must be green.
- **Repin** (needed because this change touches BOTH the compiler AND stdlib):
  `wsl make pin` refreshes both pins. **Do NOT force `CRYO_CC=gcc` on `make pin`.**
  A "worktree dirty" warning during pin is expected while uncommitted.
- **CRLF trap:** mixed endings, `autocrlf=true`, no `.gitattributes`. Use the Edit
  tool / binary Python for bulk edits — NOT `sed -i` via Git Bash (strips CR →
  phantom "modified").

### Reproduce the blocker (fast confirm)
```
# Windows / PowerShell
$env:CRYO_CC = "gcc"; make cryo; make test ARGS="nested_match_neg"
# Expect: nested_match_neg [FAIL] (left 1, right -1) on the current tree.
```

---

## After the compiler bug is fixed — finish Stage 1

1. Confirm `make test ARGS="nested_match_neg"` PASSES with `std::math::classify`
   present (name kept). Confirm all five same-leaf `classify` fns coexist.
2. Full `make test` on Windows (expect OVERALL PASS: ~1450 unit · 126 compile-fail
   · 5 projects) AND `make selfhost-check` → **2× FIXED POINT OK**.
3. WSL: `make cryo` → `make test` → `make selfhost-check` → **1× FIXED POINT OK**.
4. `wsl make pin` (the compiler bug fix + stdlib change both go into the pin), then
   `verify-pin` OK on both OSes.
5. Hand back to Jake to commit (agent never commits). Suggested scope: Stage 1
   (fpclass intrinsic + stdlib classification migration) and the compiler same-leaf
   fix — Jake may want them as separate commits.

## Pointers
- Roadmap: `LOW_LEVEL_PLAN.md`. Stage 1 section describes the fp-classification move.
- Agent memory dir: `low-level-plan-stage0-2026-07-15` (Stage 0 detail + traps);
  the "mono cache-key/same-leaf" deferred issue is referenced in the pipeline-reorder
  memory notes.
- Preferences: only Jake commits; NO workarounds / path-of-least-resistance (fix
  root causes); prefer methods/namespaced statics EXCEPT at the OS/FFI boundary
  (free functions OK there); avoid gratuitous suffixed numeric literals.
