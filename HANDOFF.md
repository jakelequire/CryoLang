# HANDOFF — Dead-code warnings for Cryo

Continuation notes for picking this up on another machine / fresh agent.
Goal: Rust-style dead-code detection, but as **warnings, not errors** (Jake
explicitly wants warnings — never fail a build).

Plan file (this machine only): `~/.claude/plans/melodic-shimmying-crane.md`.

---

## TL;DR — where we are

Four lints were scoped. Two are committed, one is staged, one is deferred:

| Lint | Code | Status |
|------|------|--------|
| Unreachable statements | W0009 | ✅ committed (`df740a9e`) |
| Unused local variables | W0001 | ✅ committed (`df740a9e`) |
| Unused private functions/methods | W0002 | 🟡 **implemented, gated, repinned — UNCOMMITTED in working tree** |
| Unused imports | W0003 | ⛔ not started (deferred) |

Everything lives in **one new pass**: `compiler/src/compiler/passes/dead_code.cryo`
(`Compiler::Passes::DeadCode::DeadCodePass`). It is diagnostic-only: `run` always
returns `PassResult::ok()`, warnings increment the sink's separate `warning_count`,
and each code is suppressible via `DiagConfig`. Warnings are **on by default**
(Jake's choice, matching Rust).

### Git state right now
- **HEAD `df740a9e`** = checkpoint 1 (W0009 + W0001), fully wired + gated + repinned.
- **Working tree (uncommitted)** = checkpoint 2 (W0002 / lint 3):
  ```
   M compiler/src/compiler/passes/dead_code.cryo   (adds lint 3)
   M bin/cryo  bin/cryo.exe  bin/cryo.pin.txt  bin/cryo.exe.pin.txt   (repin)
  ```
  This passed the full gate (see below). **First action on the new machine:**
  Jake commits this as checkpoint 2. (Commit policy: ONLY Jake commits — the
  agent never commits. See memory `commit-policy.md`.)

---

## The remaining plan (agreed sequence)

1. **Commit checkpoint 2** (lint 3) — Jake. *(already gated + repinned; just needs the commit)*
2. **Step 3 — CLEANUP**: fix the real dead code the lints found in the compiler's
   own source, so a build is warning-clean. Then repin → Jake commits.
3. **Follow-ups** (separate, optional): static-method usage for W0002; unused
   imports (W0003).

### Step 3 cleanup — what to fix
Re-run the dogfood (below) to get the authoritative current list, then delete the
dead code. From the last dogfood the counts were **~111 W0001 + 8 W0009**, all
verified TRUE positives (no false positives). Highlights:

- **~56× `const h: u64 = hash_str(...)` that is never used** — a leftover from the
  hashmap-API refactor (the maps now hash the key internally). Delete the dead
  `h` line. Confirmed examples: `resolver/intern_table.cryo:49`,
  `types/Registry.cryo:99` (and ~207), and many more. `grep -rn "hash_str" compiler/src`
  then check each for an unused `h`.
- Other unused locals: `blue`, `start`, `name`, `rh`, `inner_ann`, `discard`,
  `base_ann`, `snap`, `old_span`, `old_name`, etc. (re-dogfood for the full list).
- **8× W0009 unreachable** (defensive `return`s after exhaustive `match`, etc.):
  `mono/Specializer.cryo:276,314`, `AST/Substituter.cryo:156,365`,
  `AST/node_locator.cryo:614,656,1813`, `CLI/commands.cryo:642`. These are stripped
  by DropInsertion anyway, so deleting them is cosmetic (no codegen change).

`_`-prefixing a local also silences W0001 if the binding must stay.

### Follow-up A — static methods (W0002)
Currently **static methods are skipped** (see the `m.is_static` guard in
`report_method`). Reason: a static call `Type::m()` pins `CallExprNode.resolved_callee`
(a name), NOT `resolved_method`, so the harvest can't see the use → flagging them
would false-positive (proved on `Lexer::is_digit` etc. — 18 static methods were
wrongly flagged before the guard). Jake's codebase is static-heavy, so this is the
highest-value follow-up. To do it properly: in `walk_expr`, when a `CallExprNode`'s
`callee` is a `ScopeResolutionNode`, harvest the (owner-type, method-name) pair into
a `static_used` set keyed precisely, and check it in `report_method` for static
methods. (A name-only set is a safe conservative fallback — over-marks as used,
never false-positives.)

### Follow-up B — unused imports (W0003)
Imports create Private `SymbolKind::Import` symbols (`resolver/symbol.cryo:237`),
and value-position refs resolve to them — BUT a **type-annotation-only** import
(used solely as a param/return/field type) is resolved later in TypeResolution and
is NOT recorded against the import symbol, so a naive scan false-positives. Correct
fix: record a reference against the import symbol when a type annotation resolves
through it (a small hook in the TypeResolution pass). Also **skip wildcard imports**
(they fan out to one symbol per export → all look unused). Group `{A,B}`/aliased
imports by `declaring_span == ImportDeclNode.span`.

---

## How the pass works (architecture)

Pass runs in the **SemanticAnalysis stage, AFTER monomorphization, BEFORE
DropInsertion**. Placement is load-bearing:
- After mono ⇒ generic call sites are resolved (`resolved_method` is set).
- **Before DropInsertion** ⇒ critical: `drop_insertion.cryo` (~line 557-608) STRIPS
  statements after a divergence and rebuilds `block.statements`. If the lint ran
  after it, unreachable code would already be gone and W0009 would fire on nothing.

`run(ctx)` for each module (skips `std::*` modules — only user code is linted):
1. `build_used()` — scans `resolver.resolution_map.singles`; records the declaring-span
   key of every referenced symbol into `used` (excludes each decl's self-entry).
   Covers **local variables** and **free-function callees** (identifier references).
2. `harvest_module(root)` — complete expression walk collecting every
   `CallExprNode.resolved_method` (and operator overloads via `desugared_call`) into
   `method_used`, keyed by the method's `func.span`. This is the **method-usage**
   signal (member calls are type-directed, NOT in the resolution map).
3. `check_module(root)` — W0009 (unreachable) + W0001 (unused locals) body walk.
4. `report_module(root)` — W0002 (unused private fns/methods).

**Usage key convention:** everything is keyed by `ResolutionMap::make_key(declaring_span)`.
A decl is "used" iff its span key is in `used` OR `method_used`.

### W0002 exclusions (never flagged)
public; static (follow-up A); generic templates (`is_generic()`); mono
specializations (`is_specialization`); constructors/destructors; virtual/override;
trait-impl methods (`func.origin_trait.is_valid()`); `main`; `![entry]`/`![test]`/
`![ignore]`/`![should_panic]`; `![symbol(...)]` link-named (`has_link_name`); and
non-private (only `MethodNode.visibility == Visibility::Private` methods are
candidates — structs default public, so most methods are never flagged).

### Files changed
- **NEW** `compiler/src/compiler/passes/dead_code.cryo` — the whole pass.
- `compiler/src/compiler/passes/pass_id.cryo` — `PassID::DeadCodeAnalysis` variant +
  `name()`/`stage()`/`order()`/`metadata()` arms (`fatal_on_failure` uses the
  `_ => false` default; requires `Provision::BodiesTypeChecked`, provides `[]`).
- `compiler/src/compiler/passes/pass_registry.cryo` — `run_pass` dispatch arm,
  import of `DeadCode`, and (harmlessly) the `build_standard_pipeline`/`build_raw_pipeline`
  lists.
- `compiler/src/compiler/passes/_module.cryo` — `public module DeadCode;`.
- `compiler/src/compiler/instance.cryo` — **the pass list that actually runs**: the
  batch at ~line 1720 (`set_passes([GenericValidation, FunctionBodyTypeCheck,
  MoveCheck, DeadCodeAnalysis, DropInsertion, TypeLowering])`). NOTE: the real
  pipeline is built here via `set_passes`, NOT via `build_standard_pipeline()` — a
  gotcha that cost time.

Dormant error/warning codes reused (already defined in `compiler/src/compiler/diag/_module.cryo`):
`W0001_UNUSED_VARIABLE`, `W0002_UNUSED_FUNCTION`, `W0009_DEAD_CODE`. (Emit template
copied from the W0012 lint in `sema/literal_resolver.cryo:93-100`.)

---

## Build / test / dogfood recipe (READ — several traps)

**The pinned `bin/cryo(.exe)` is the bootstrap; the freshly-built compiler lands at
`compiler/build/cryo.exe`.** After `make cryo`, TEST WITH `compiler/build/cryo.exe`,
NOT `bin/cryo.exe` — the pinned one won't have your latest un-repinned changes, so
you'll see NO warnings and think it's broken. (This wasted a lot of time.)

```powershell
# Build the compiler (uses pinned bin/cryo to compile the source with your changes):
$env:CRYO_CC='gcc'; make cryo          # -> compiler/build/cryo.exe

# Single-file test (must pass --stdlib since build/ is not next to stdlib/):
$env:CRYO_CC='gcc'; & "compiler\build\cryo.exe" build path\to\test.cryo `
    --stdlib="C:\Programming\apps\CryoLang\stdlib" -o "$env:TEMP\out.exe"

# DOGFOOD (false-positive audit on the compiler itself; isolated build-dir so
# you don't clobber the real cache — the final E0900 link error is EXPECTED,
# it's just the relative toolchain path in the temp dir; all modules compile):
$env:CRYO_CC='gcc'; & "compiler\build\cryo.exe" build "C:\Programming\apps\CryoLang\compiler" `
    --stdlib="C:\Programming\apps\CryoLang\stdlib" --build-dir="$env:TEMP\dogfood" 2>&1 |
    Out-File -Encoding utf8 "$env:TEMP\dogfood.log"
# then: Select-String -Path "$env:TEMP\dogfood.log" -Pattern "warning\[W0002\]"  (etc.)
```
Note: single-file `build` only prints warning COUNTS on failure; on success it prints
`Compiled -> ...`. The warnings themselves DO render (they flush regardless). If you
see none, you're probably running the pinned `bin/cryo.exe` — use `compiler/build/cryo.exe`.

### Cryo authoring gotchas hit while writing the pass
- Boolean type is `boolean`, not `bool`.
- Free functions use the `function` keyword: `function f() -> int { ... }`. `->` IS
  valid for return types. `main() -> int`.
- Parser ambiguity: `ident < <literal>` (e.g. `sc < 1`) is parsed as a generic-arg
  list and errors. Rewrite (`sc == 0`, or put the non-ident on the left). `ident <
  path` (e.g. `i < arr.length`) is fine.
- `.length` is a FIELD (no parens) on arrays. Enums support `==` and `!=`.
- Match on `NodeKind`/enums; a wrong variant NAME is a compile error (good), but an
  OMITTED variant silently misses (bad) — the harvest walker enumerates every
  expression node with children for exactly this reason.
- Private free fn / method syntax: `private function foo()`; class methods in a
  `private:` block or inline `private` (structs default public).

---

## Validation gate + repin (the checkpoint procedure)

Run from **PowerShell** (not Git Bash). Respect memory `wsl-wedge-hazard.md`:
NEVER `wsl --shutdown` while WSL procs are alive (wedges WSLService; recovery needs
admin `Restart-Service WSLService -Force`). Do NOT force `CRYO_CC=gcc` on `make pin`.

```powershell
$env:CRYO_CC='gcc'; make cryo          # build first (make test does NOT rebuild)
$env:CRYO_CC='gcc'; make test          # expect OVERALL PASS
$env:CRYO_CC='gcc'; make selfhost-check # REQUIRE 2x "FIXED POINT OK" (Linux+Windows)
make pin                                # repin BOTH bin/cryo(ELF) + bin/cryo.exe(PE) via WSL
```
- `make test` last run: OVERALL PASS (1444 unit, 126 compile-fail, 4 projects, 0 fail)
  — the new warnings did NOT break the exact-output compile-fail tests.
- `make selfhost-check`: warnings don't change codegen, so byte-identity holds — got
  **2× FIXED POINT OK**. (Verify `grep -c 'FIXED POINT OK' == 2`; the check can exit
  0 while skipping Windows — see memory `selfhost-check-windows-gate-holes.md`.)
- `make pin` on Windows delegates to WSL (`scripts/pin-windows.cmd`) and refreshes
  BOTH pins. **Trap seen this session:** a repin got killed after writing only the
  Linux pin (`bin/cryo` + `.pin.txt`), leaving the Windows pin stale → inconsistent.
  If a repin is interrupted, just re-run `make pin` (WSL build cache makes it faster)
  and verify all four pin files are modified before committing.
- "worktree is dirty" pin warning is expected (uncommitted changes). Since selfhost
  is byte-identical, the pin IS reproducible from Jake's eventual commit.

---

## Test fixtures (recreate — they were in a session-temp scratchpad)

Small fixtures that validated each lint (put them anywhere and compile with
`build/cryo.exe --stdlib=...`):

```cryo
// W0009 + W0001
namespace Fixture;
function after_return() -> int { return 1; const dead: int = 2; return dead; }   // W0009 on `dead`
function if_both_diverge(c: boolean) -> int {
    if (c) { return 1; } else { return 2; }
    const after: int = 3; return after;                                          // W0009 on `after`
}
function locals() -> int {
    const unused_local: int = 41;   // W0001
    const used_local:   int = 42;
    const _ignored:     int = 43;   // silent (underscore opt-out)
    return used_local;
}
function main() -> int { return after_return() + if_both_diverge(true) + locals(); }
```
```cryo
// W0002
namespace L3;
private function unused_helper() -> int { return 1; }   // W0002 (private, unused)
private function used_helper()   -> int { return 2; }
function public_unused() -> int { return 3; }           // silent (public)
type class Foo {
public:
    Foo() {}
    pub_method(&this) -> int { return this.used_method(); }
private:
    used_method(&this)   -> int { return 10; }          // silent (used via this.used_method())
    unused_method(&this) -> int { return 11; }          // W0002 (private, unused)
}
function main() -> int {
    mut f: Foo = Foo();
    const r: int = used_helper() + f.pub_method();
    f.drop();
    return r;
}
```

---

## Key source references
- Emit shape: `sema/literal_resolver.cryo:93-100` (W0012 lint template).
- Warning codes: `diag/_module.cryo` (W0001/W0002/W0009 dormant slots).
- Divergence/analysis patterns cribbed from `passes/move_check.cryo`.
- DropInsertion truncation (why placement matters): `passes/drop_insertion.cryo:557-608`.
- Real pipeline list: `instance.cryo` ~line 1720 (`set_passes([...])`).
- `resolved_method` set by sema (`sema/call_resolver.cryo` 1667/1736/1775/1866) and
  mono (`mono/call_specializer.cryo`) — both before this pass.
- Expression node shapes: `AST/expression.cryo`; statement shapes: `AST/statement.cryo`;
  `NodeKind` + `Visibility`: `AST/_module.cryo`.
