# HANDOFF — REPORT.md reduction session (2026-07-20)

For a fresh agent picking up the **REPORT.md deep-review findings** work. Read
this top-to-bottom before touching anything. Jake's working rules are at the end
— follow them exactly (especially: **REPORT.md is tick-only**, and **only Jake
commits**).

---

## 0. TL;DR — where things stand

- This session closed **7 REPORT.md findings** (all committed by Jake) and
  **implemented an 8th** (struct field defaults) that is **done + tested but not
  yet ticked** — it needs a selfhost gate + repin, which are blocked on the
  tree issue below.
- REPORT.md is now **63 ticked / 27 open**.
- ⚠️ **The working tree is entangled** with a *separate, parallel* "runtime"
  project's uncommitted compiler changes (not this session's work). One project
  test — `native_alloc_gate` — fails under `make test`, and the likely cause is
  that runtime work, **not** field defaults. See §2. **Resolve this first.**

---

## 1. Immediate next step (field defaults → tick)

**Struct field defaults are fully implemented and validated in isolation.** What
remains to close the finding:

1. **Untangle the tree** (§2) — confirm with Jake whether the parallel runtime
   changes should be here, and get `native_alloc_gate` green under `make test`.
2. `make selfhost-check` → require **2× `FIXED POINT OK`** (Linux target-IR +
   Windows native-PE). Count with PowerShell `Select-String` (tee log is UTF-16).
3. `make pin` (Windows host auto-delegates to WSL; refreshes both ELF+PE pins),
   then `python scripts/verify-pin.py` → `verify-pin: OK`.
4. Tick **exactly one** REPORT.md line (Tier-4 design-debt section):
   `- [ ] **Frozen speculative/no-op surface**: pipeline operators …` →  `- [x]`.
   (Jake's decisions on that finding are all made — see §4.)

Do **not** repin until `native_alloc_gate` is green and the tree is sorted —
repinning now would bake the parallel runtime WIP into the pinned binary.

---

## 2. ⚠️ Working-tree entanglement (READ FIRST)

The tree was **clean at session start** but now carries **two independent sets of
uncommitted changes**:

- **This session (field defaults):** `types/user_defined.cryo`,
  `mono/type_populator.cryo`, `passes/type_resolution.cryo`, `sema/sema.cryo`,
  `AST/declaration.cryo` (only the pre-existing `default_value` field — untouched
  by me), `docs/cryo.md`, `CONTRIBUTING.md`, `README.md`, `REPORT.md`,
  `tests/test-roster.txt`, `tests/tests/lang/struct_field_defaults.cryo` (new).
- **A PARALLEL "runtime" project (NOT this session):**
  `codegen/abi.cryo`, `codegen/ops/intrinsic_emitter.cryo`,
  `codegen/ops/intrinsics_codegen.cryo`, `codegen/ops/declaration_emitter.cryo`,
  `codegen/visit/decl_visit_emitter.cryo`, `passes/directive_processing.cryo`,
  `compiler/llvm_bindings.h`, `stdlib/core/intrinsics.cryo`, and
  `.todo/RUNTIME_HANDOFF.md` (its own handoff). See memory note
  `runtime-project-phase0-2026-07-20` — that effort has its own P1/P2/… task
  list, some "built-unverified."

**Why this matters for `native_alloc_gate`:** that project test compiles a
`[low_level] native_alloc = true` binary and stresses Box/Array/String/HashMap/
format under the mmap heap — i.e. it exercises **intrinsics / intrinsic_emitter /
abi**, exactly the surface the parallel runtime work is editing. Field defaults
don't touch any of that, and the project doesn't use a single field default.

**Evidence field defaults is NOT the cause:**
- `cd tests/tests/projects/native_alloc_gate && cryo run --stdlib=<repo>/stdlib`
  → **PASSES** ("all native alloc checks passed", exit 0) under the freshly-built
  compiler.
- All 1589 unit tests pass; all 143 compile-fail tests pass (incl.
  `E0355_missing_field_init`); only the `native_alloc_gate` *project* fails under
  the `make test` meta-runner.

**To isolate (do this before blaming anything):**
```
git stash push -- compiler/src/compiler/codegen/abi.cryo \
  compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo \
  compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo \
  compiler/src/compiler/codegen/ops/declaration_emitter.cryo \
  compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo \
  compiler/src/compiler/passes/directive_processing.cryo \
  compiler/llvm_bindings.h stdlib/core/intrinsics.cryo
# then: make cryo && make test   (native_alloc_gate should go green if runtime WIP was the cause)
```
Also try a clean project rebuild in case it's a stale incremental artifact:
`rm -rf tests/tests/projects/native_alloc_gate/build` then re-run `make test`.
**Confirm the runtime work's status with Jake** — it may not be meant to ship
with the field-defaults change.

---

## 3. What field defaults actually does (so you can review/extend it)

Turns the previously **parse-only** `field: T = expr` into a real feature via a
**sema-desugar** (no codegen changes):

- **`FieldInfo` (`types/user_defined.cryo`)** gained `default_value:
  ExpressionNode*` (+ `has_default()` / `set_default_value()`). Populated from the
  AST `FieldDeclNode.default_value` at the 6 type-building sites
  (`type_populator.cryo` ×3 struct/union/class via `replace_all`,
  `type_resolution.cryo` ×3). `import Compiler::AST::Expression` was added.
- **Sema `resolve_struct_literal` (`sema/sema.cryo`, the E0355 site ~line 2246):**
  an omitted field that `has_default()` is filled by `fill_field_default()` — it
  **clones** the default (`ASTCloner().clone_expr`), resolves + type-checks it
  against the field type (E0200 on mismatch, mirroring the per-field loop), and
  **appends it as a synthetic `FieldInit`** so codegen materializes it. E0355 now
  fires only for an omitted field with **no** default. Added `Cloner` to sema's
  AST import.
- **Semantics** (chosen; Jake didn't object): default is a **standalone
  expression** (can't reference other fields/`this`), **re-evaluated at each
  construction** that omits the field, **type-checked at the use site**. Unions
  already return early before the E0355 block, so they're unaffected.
- **Tests:** `tests/tests/lang/struct_field_defaults.cryo` — 5 `![test]`s
  (omit-uses-default, expression default `2*3`, provided-overrides, all-omitted,
  fresh-per-construction). All pass. `E0355_missing_field_init.cryo` still passes
  (its omitted field has no default).
- **Docs:** `docs/cryo.md` §8.2 rewritten as a working feature; the field-defaults
  row removed from the §21 reserved-syntax table.
- **Known limitation to note if extended:** a default that references the struct's
  own generic type param (e.g. `y: T = T::default()`) isn't exercised; standalone
  concrete defaults are what's tested. Decl-time type-checking of an unused default
  isn't done (checked at first omitting use).

---

## 4. Jake's decisions this session (all made — act on them)

**"Frozen speculative/no-op surface" finding — case-by-case (all decided):**
- `async` / `await` / `yield` → **KEEP reserved** (forward-compat for future
  async). No code. Already documented in §21.
- Pipeline `|>` / `<|` → **KEEP** (working desugar). No code.
- `unsafe` → **KEEP** (transparent no-op, documented committed behavior). No code.
- `switch` → **KEEP** (it is a *fully working, tested* feature — E0200/E0209/E0401,
  ~8 tests — NOT actually "no-op"; the audit mis-bucketed it). No code.
- Struct field defaults → **IMPLEMENT** (done — §3). This is the only one needing
  code, and once gated+repinned the whole finding ticks.

**CryoFormat** → Jake **deleted** `tools/CryoFormat/` himself; I removed the
dangling refs (CONTRIBUTING.md, README.md) and **ticked** the finding. 1.0 ships
with no `cryo fmt` (documented gap).

**Arithmetic overflow (Tier-4, still OPEN, NOT started):** Jake chose **"add
`checked_`/`wrapping_`/`saturating_` now"** — a real stdlib feature. This is the
next substantial piece of work after field defaults lands. Nothing done yet.
The finding: `- [ ] No checked/wrapping/saturating arithmetic opt-in`.

---

## 5. Other things discovered this session (record, don't lose)

- **Roster golden was STALE by ~103 tests.** Recent commits (DateTime, URL, net,
  Cursor, Seek, Path, Rc/Arc Weak, `chars`, F32-literal-args, tuple-drop-glue,
  generic-lookahead) added tests without re-pinning `tests/test-roster.txt`. I ran
  `python scripts/roster-check.py <abs-path-to-cryo.exe> --update` (needs an
  **absolute** path — it `cd`s into `tests/`), which regenerated it to **1589**
  entries (was 1481, +103 drift +5 mine). `roster-check` now passes. Flag to Jake:
  `make roster-check` clearly wasn't being run as a gate, so it drifted. The 108
  new entries are all legitimate recent tests.
- **7 of 13 warning codes are DEAD** (declared, never emitted): W0003, W0004,
  W0005, W0006, W0007, W0008, W0010 — zero `ErrorCode::W####` refs outside
  `diag/_module.cryo`. Only W0001/W0002/W0009/W0011/W0012/W0013 are emittable
  (all 6 now have negative tests). Candidate D12-style cleanup: wire them up or
  delete. (This is why the diagnostics finding's "0/12 W-codes" could only reach
  6.)
- **`unsafe` had 0 real uses** in the whole tree (the audit's "used once" was an
  overcount). Pipes: 0 uses except 6 lines in `tests/tests/lang/operators.cryo`.

---

## 6. Findings closed this session (7 ticked — for context, already committed)

1. Tier-1 `static match` arm pruning raw `TypeRef.id` — **stale** post-D1
   (hash-consing made raw-id == semantic identity).
2. Tier-1 **Mono fixpoint gaps** — Part A: two-metric fixpoint in
   `passes/specialization.cryo` (added `spec_owner_module_count()` so
   method-spec-only progress doesn't break early). Part B: deleted the dead
   `in_progress` cycle detection (`monomorphizer.cryo`).
3. Tier-1 **DirectPair** — the cited coercion-divergence bug was already fixed;
   only the self-correcting arity heuristic remained. Jake: tick as resolved.
4. Tier-1 **Bindgen flat-namespace collision** — colliding same-leaf externs are
   now **renamed** (`add`→`add_2`) with their true `![symbol("…")]` pinned, via
   the `approximated` honesty bucket, instead of dropped. Verified end-to-end with
   a `cryo vendor` C++ overload project.
5. Tier-3 **Diagnostics pinned by error code only** — added the rustc-style
   `//~ ERROR[CODE] message` mechanism to the negative harness
   (`CLI/commands.cryo`: `NegExpect`/`NegDiag` + `parse_neg_expectations`/
   `parse_diagnostics`/`check_annotations`/`neg_arrow_line`/`neg_line_contains`).
   Two-way matching (each annotation matches; every in-file diagnostic is
   annotated → catches cascades). **120 of 143** negative tests annotated.
   README documents it.
6. Tier-4 stdlib **io::Seek/Cursor/URL/calendar/env::vars/Weak/Path** — all
   present now (recent commits + pre-existing); only mutex-poisoning absent
   (intentional documented non-feature).
7. Tier-4 LSP **CryoFormat** — deleted (see §4).

---

## 7. Remaining REPORT.md open items (27) — the map

**Tier-2 structural big rocks (7):** D3 (name-keyed tables → DefId), D4
(CallEmitter fallback tower), D6 (silent-null error model), D7 (inference story),
D8 (parallel-table codegen — best effort:leverage, "cheap and pays forever"), D9
(pass manager), D10 (size_bytes vs DataLayout / ABI), D11 (duplication hotspots).

**Tier-3 test/validation (5):** ASAN/valgrind nightly; compiler-internal unit
tests; lexer/parser fuzzing; LSP automated tests; pinned-binary git growth
(explicitly "fine for 1.0" — a defer decision).

**Tier-4 design decisions (some settled above; still open):** `![sink]` receiver
signatures; partial auto-drop; **checked/wrapping/saturating arithmetic (Jake:
implement — §4)**; examples/idiom drift; naming idiosyncrasies (`string`/`boolean`/
`function`/`char`). The "Frozen speculative surface" one ticks once field defaults
land.

**Tier-4 stdlib gaps (2):** collections variety (Deque/BTree/binary_search/sum/rev
— min/max/skip already landed); ABI-by-offset brittleness (struct stat / dirent /
pthread magic sizes — subsumed by the libc-weaning/runtime plan).

**Tier-4 LSP roadmap (6):** table-stakes features (documentSymbol/references
highest value); incrementality/cancellation; semantic-tokens full-doc O(n²);
non-ASCII position desync (a real bug); link overlays hand-mirror the compiler's.

---

## 8. Build / gate / repin procedure (Windows host) + traps

- Run `make` from **PowerShell**, not Git Bash. Always `export CRYO_CC=gcc` (or
  `$env:CRYO_CC="gcc"`).
- **`make test` does NOT rebuild the compiler** — run **`make cryo` FIRST** or you
  gate a stale binary.
- `make cryo` = full bootstrap (pinned `bin/cryo` compiles stdlib + compiler →
  `compiler/build/cryo.exe`).
- `make test`: unit + compile-fail (negative) + projects. Does **not** run
  `roster-check` (separate: `make roster-check`, or the `--update` flow in §5).
- `make selfhost-check`: host-aware; delegates the Linux 6-stage chain to WSL +
  Windows byte-identity smoke. **Require 2× `FIXED POINT OK`** (count via
  PowerShell `Select-String` — the tee log is UTF-16, grep miscounts).
- `make pin`: Windows host auto-delegates to WSL, refreshes **both** ELF+PE pins.
  Then `python scripts/verify-pin.py` → `verify-pin: OK`. `verify-pin.py`
  needs an **absolute** path to the cryo binary.
- **WSL wedge hazard:** never `wsl --shutdown` with detached WSL procs alive.
- Adding a test to `tests/tests/lang/` changes the roster → regenerate the golden
  (§5). Adding a negative test does NOT (compile-fail suite isn't in the roster).

---

## 9. Jake's working rules (non-negotiable)

1. **A more-correct change that breaks the build → FIX THE BREAKAGE**, don't
   revert/carve-out. A wave of errors after a correctness fix is that fix
   surfacing existing sloppiness.
2. **When something genuinely needs Jake's opinion, ASK** (use the question
   tool) — language-semantics / semver calls, and before a LARGE Tier-2 rock.
3. **REPORT.md is TICK-ONLY.** The only edit it may receive is `- [ ]` → `- [x]`.
   No fix summaries, no corrections, no notes. Findings worth recording go in the
   memory files, not REPORT.md.
4. **Comments describe the logic** (invariant + failure mode), not the project
   narrative — no `D1 §2.4`, `Batch A`, dated "audited" stamps, "used to be X".
5. **Do NOT commit** — Jake commits. You **may** repin. If asked to commit, omit
   both `Co-Authored-By` and `Claude-Session` trailers.
6. Recurring prefs: no free functions (methods / namespaced statics); no
   type-suffixed method names (one generic + `static match (T)`); avoid suffixed
   numeric literals; proper solutions over workarounds.

Memory index: `~/.claude/projects/C--Programming-apps-CryoLang/memory/MEMORY.md`
(see especially `report-md-cheap-compiler-batch-2026-07-20.md` for this session's
detail, and `runtime-project-phase0-2026-07-20.md` for the parallel effort).
