# RUNTIME_HANDOFF.md — compiler-prerequisites implementation for `./runtime`

**Audience:** a fresh agent continuing the Cryo runtime work.
**What this is:** the second handoff. The first (`HANDOFF` at repo root) scoped the whole
`./runtime` project. Phase 0 (audit) is done — see `runtime/NOTES-capabilities.md` and
`runtime/PROPOSALS.md`. This document covers the **compiler features** Jake approved and I
started implementing on the night of **2026-07-20**. It records exactly what is done,
what is built-but-unverified, what is next, and the landmines.

**Nothing is committed. Nothing is repinned.** Everything below is UNCOMMITTED working-tree
state (Jake commits; the agent never commits). No repin was needed because the stdlib does
not *use* any of the new features yet — it only gained inert `intrinsic function`
declarations (bodies emit nothing).

---

## 0. Read first — the ground rules that bit me

- **Build:** `make cryo` from **PowerShell** (not Git Bash), with `$env:CRYO_CC='gcc'`.
  This rebuilds stdlib + the stage-2 compiler via the pinned `bin/cryo.exe`. Takes a few
  minutes.
- **Transient failure to expect:** `make cryo` sometimes fails with
  `error: failed to hoist executable to build/cryo.exe (copy exited with 1)`. This is a
  **Windows file lock** on `build/cryo.exe` (usually because a `cryo.exe` you just ran still
  holds a handle), **not** a code error — the Cryo source compiled fine. Just wait ~2s and
  re-run `make cryo`.
- **Test:** `make test` from PowerShell, **after** `make cryo` (memory: "make test does NOT
  rebuild the compiler"). A green run prints `OVERALL PASS (unit: ok; compile-fail: 143
  passed; projects: 8 passed)`. It's ~35s.
- **Probe-verify a single feature** without the whole suite: build a scratch `.cryo` file to
  IR and inspect + run it:
  ```bash
  export CRYO_STDLIB="C:/Programming/apps/CryoLang/stdlib"
  C:/Programming/apps/CryoLang/compiler/build/cryo.exe build FILE.cryo --emit-llvm -o OUT
  # emitted IR: <FILE_dir>/build/target/release/host-windows/local/ir/<Namespace>.ll
  # runnable exe: <FILE_dir>/build/main.exe
  ```
  `cryo build FILE.cryo` works on a single file; `cryo run` needs a cryoconfig. Without
  `CRYO_STDLIB` you get "cannot locate the Cryo standard library".
- **Selfhost byte-identity is the real canary and I did NOT run it.** After the whole batch,
  run `make selfhost-check` (via WSL per the Windows notes) and require **two**
  `FIXED POINT OK` lines. Do this before declaring the compiler work landed.

---

## 1. Decisions locked this session (by Jake)

- **`![no_mangle]` is the preferred unmangling directive**, over `![symbol("…")]`.
  `![symbol("X")]` stays only for the *rename* case. Lang items are written
  `function __cryo_panic(...) { … } ![no_mangle]`.
- **`![export]` was DROPPED.** `docs/cryo.md` reserved it as a synonym for `![no_mangle]`,
  but "export" conflates with *visibility* (all Cryo symbols are already `public` by
  default) while mangling is an *object-file* concern. So there is no `![export]`; only
  `![no_mangle]`. (I did **not** yet edit `docs/cryo.md` to remove the reserved `![export]`
  row — that's a small follow-up.)
- **Cryo directive syntax is `![…]`, never `@[…]`.** The `runtime/*.md` docs were corrected
  (`![naked]`).
- **`![naked]` and `no_runtime` approved.** `![naked]` is the enabling capability; its
  consumer (a freestanding `_start`) stays deferred.

---

## 2. DONE and VERIFIED (build + probe + `make test` all green)

All four directives are implemented across the directive pass, the AST, the ABI
attribute plumbing, and codegen. `make test` passed after this batch. Each was probe-checked
by inspecting emitted IR and running the program.

### P1 — `![no_mangle]` / `![symbol]` on free-function *definitions*
- Emits the function under its bare declared identifier instead of `C$…`.
- **The landmine (already fixed, but understand it):** the *definition* side was the easy
  half. The *call site* resolves an identifier call via
  `resolve_function_by_mangled(node.resolved_callee)` (`codegen/visit/call_emitter.cryo:204`),
  and `declare_function` originally registered the `fn_val` only under the **qualified** key.
  So a `no_mangle` free function linked with an **undefined reference** to its old mangled
  name. Fix: `declare_function` now *also* registers the `fn_val` under the **mangled key**
  when `has_link_name` — mirroring `declare_method`, whose mangled-key registration is what
  makes `![symbol]` "invisible to callers" (`codegen/ops/symbol_resolver.cryo:249`).
  Methods and extern blocks already honored `has_link_name`; only `declare_function` didn't.
- Verified: `define i64 @cryo_nm_probe_add(...)` (bare) next to a mangled sibling; links; runs.

### P2 — `![weak]`
- Weak definition (`LLinkage::WeakAny`) + an any-selection COMDAT (same COFF reason
  `apply_spec_linkage` documents). For user-overridable runtime symbols (allocator, panic).
- Verified IR: `define weak i64 @"C$…weak_fn…" comdat { … }`.

### P3 — `![section("name")]`
- Object-file section placement via a **new** LLVM-C binding `LLVMSetSection` added to
  `compiler/llvm_bindings.h` (it was absent). The `extern module llvm := "C"` at
  `codegen/_module.cryo:45` auto-imports it (`![functions_only]`).
- Verified IR: `define i64 @"C$…sectioned_fn…" section ".cryo_probe" { … }`.

### P11 — `![naked]`
- No prologue/epilogue; sets LLVM's `naked` fn attribute; body must be a single `asm { }`
  block (enforced — a non-asm body errors `E0151`).
- Codegen: `generate_function_body` (`codegen/visit/decl_visit_emitter.cryo`) branches on
  `node.is_naked` to a minimal path — `codegen_naked_prologue` opens just the entry block
  (no allocas/args/scope), emits the asm, then caps with `unreachable` (inline asm is not a
  terminator; the asm does the real `ret`/`jmp`/exit).
- Verified IR: `define void @naked_probe() #1 { call void asm sideeffect "retq", ""() ;
  unreachable }` with `#1 = { naked }`; runs (returns through the asm `ret`).

---

## 3. BUILT but NOT YET VERIFIED — do this first

The intrinsics batch **compiled cleanly** (`make cryo` exit 0) but I had **not**
probe-verified it or run `make test` when this handoff was written. **Verify before building
on it.**

### P19 — `trap()` / `debugtrap()` intrinsics
- `intrinsics::trap()` → `call void @llvm.trap()` then `unreachable` (declared `-> never`;
  `ud2`/SIGILL, libc-free — the divergent sink for a `no_runtime` check failure).
- `intrinsics::debugtrap()` → `call void @llvm.debugtrap()` (returns; no unreachable).

### P20 — `frame_address(n)` / `return_address(n)` intrinsics
- `-> void*`, one **constant** level arg (`n=0` = current frame). For the backtrace walker.
- `llvm.frameaddress.p0(i32)` / `llvm.returnaddress(i32)`.

**Verification probe** (write, build to IR, run):
```cryo
namespace TrapProbe;
import std::core::intrinsics;
function main() -> int {
    const fp: void* = intrinsics::frame_address(0);   // expect @llvm.frameaddress.p0(i32 0)
    if (fp == null) { intrinsics::trap(); }            // expect call @llvm.trap ; unreachable
    return 0;
}
```
Grep the emitted `.ll` for `@llvm.trap`, `@llvm.frameaddress.p0`, `unreachable`; run the exe
(should exit 0 since `fp` is non-null). Then run `make test`.

**If it regressed `make test`**, the most likely culprit is the `a0` guard I added in
`intrinsic_emitter.cryo` (changed `mut a0 = args[0]` to
`if (args.length >= 1) { args[0] } else { LValue::null() }` so zero-arg `trap` doesn't index
out of bounds). That change is used by *every* intrinsic, so a mistake there is broad. It
looked correct and byte-safe for the >=1-arg intrinsics.

---

## 4. NEXT — not started

### P9 — `no_runtime` cryoconfig flag  (biggest remaining; the point of the project)
The mechanism that lets `runtime/core` be written in Cryo even though compiling Cryo emits
calls *into* core. `no_std` exists but is shallow — it only removes the stdlib, leaving crt0
and `-lm -lstdc++`/`-lm -lws2_32` on and never emitting `-nostartfiles`/`-nostdlib`.

I had just started and read the `no_std` sites to mirror. Concrete plan:
1. **Config field** — `project_config.cryo`: add `no_runtime: boolean` mirroring `no_std` at
   its four sites: field decl (`:228`), default `false` (`:350`), copy-ctor (`:422`), and the
   `[compiler]` key parse (`:628`, `"no_std" => …` — add `"no_runtime" => …`).
2. **Propagate** like `no_std`: `compilation_context.cryo` (`project_no_std` at `:127`) and
   `instance.cryo` (`no_std` field `:134`, set `:996`). Add `no_runtime` carriers.
3. **CLI:** `--no-runtime` OR-in, mirroring `--no-std` (`CLI/commands.cryo:1115`,`:2440`).
4. **Gating** — the compile-time gate pattern is `CfgEnv` in `passes/config_gating.cryo:118`
   (that's how `native_alloc` gates). Use it so codegen can ask "is this a no_runtime build?".
5. **Codegen suppression under no_runtime:**
   - No implicit entry shim / `main` widening / `set_args`/`set_env` prologue
     (`codegen/ops/declaration_emitter.cryo` — `is_program_main` widening ~`:423`, prologue
     ~`:1800`).
   - No `@panic` runtime emission (`emit_panic_runtime`, `intrinsic_emitter.cryo:1103`);
     bounds/overflow/unwrap check-failure paths compile to `llvm.trap` ([P19]) instead of a
     `__cryo_*`/`abort` call. (Bounds/overflow aren't emitted at all today — see P4/P5 — so
     for now this mainly means: don't emit the `@panic` body and don't reference libc.)
6. **Freestanding `LinkerConfig` profile** — `codegen/passes.cryo`: `LinkerConfig` (`:62`),
   `linker_config_for_triple` (`:85`), the unconditional `extra_system_libs` append
   (`-lm -lstdc++` host `:145` / `-lm -lws2_32` windows `:156`) and `run_linking` (`:972`,
   command assembly ~`:1070`). Under no_runtime: `-nostartfiles -nostdlib -static`, empty
   `extra_system_libs`, no `libcryo.a`.
7. **Dependency diagnostic (hard error, not warning):** a `no_runtime=false` package that
   transitively depends on a `no_runtime=true` one (and the reverse mismatch) is an error.
   Hook `deps/dep_resolver.cryo` `walk_deps` (`:178`).
8. **Docs:** document the four-way `no_std × no_runtime` matrix in `runtime/README.md`
   (recommend `no_runtime=true` ⇒ `no_std=true`).
9. **Acceptance:** an empty package with `no_runtime=true` compiles to an object with **no**
   `__cryo_*`, no `@panic`, no libc references (`nm`/IR inspection).

**Note on the tiering model** (from Phase 0): runtime tiers must be **archive-per-tier like
stdlib `libcryo.a`** (each with its own cryoconfig; `core` = `no_runtime=true`), **not**
in-repo path-deps — path-deps are compiled as *unified source* (`dep_resolver` harvests
roots into one build) and would not honor a per-tier `no_runtime`.

### P5 — overflow-checked-arithmetic intrinsics  (`.with.overflow` family)
Not started; medium size. Expose `llvm.{s,u}{add,sub,mul}.with.overflow.iN` as intrinsics
returning `{iN, i1}`. Recommended stdlib shape mirrors `atomic_cmpxchg`: write the overflow
bit through an out-param — `sadd_ovf_i32(a: i32, b: i32, out_ovf: boolean*) -> i32` — with
the emitter doing `extractvalue 0` → result, `extractvalue 1` → store to `out_ovf`. Do
i32+i64 for signed+unsigned add/sub/mul (12 variants). Consumer (`panic_overflow` emission
in `codegen_binary`) is deferred, so this is speculative-but-useful; the stdlib
`checked_add` (hand-rolled today) could adopt it. **Lower priority than P9.**

---

## 5. DEFERRED (Tier 3 — do NOT build now; recorded in `runtime/PROPOSALS.md`)

Runtime-coupled or out-of-scope: **P4** bounds-check *emission*, **P6** consolidate the 4
mute internal traps onto the panic funnel, **P7** single runtime-library `__cryo_panic`
symbol, **P8** consolidate the 6 alloc sites, **P10** compiler-native TLS, **P12–P15**
unwinding (landing pads / personality / persisted cleanup schedule — ⚠ escalate, needs
Jake to scope drop-emission rework), **P17** crash/backtrace subsystem (greenfield Cryo),
**P21** re-expose bit intrinsics in stdlib, **P22** asm symbol-operands. P4/P6/P7/P8 belong
*with* the runtime code so they can be validated end-to-end.

---

## 6. Files touched this session (all UNCOMMITTED)

Compiler + stdlib:
- `compiler/src/compiler/passes/directive_processing.cryo` — registered `no_mangle`, `weak`,
  `naked`, `section` in `is_known_builtin` (~`:209`), the did-you-mean candidate list
  (~`:718`), `validate_builtin` arms (~`:261`+), and `apply_effects` (function branch ~`:963`,
  method branch ~`:1066`). Naked body-shape guard is inline in the function `apply_effects`.
- `compiler/src/compiler/AST/declaration.cryo` — `FunctionDeclNode` gained `is_weak`,
  `is_naked`, `link_section` (fields + constructor inits). `link_name`/`has_link_name`
  already existed and are reused for `no_mangle`.
- `compiler/src/compiler/codegen/ops/declaration_emitter.cryo` — (a) `declare_function`
  honors `has_link_name` for the emitted symbol AND registers the fn_val under the mangled
  key when `has_link_name` [the P1 landmine]; (b) new `apply_directive_effects` helper
  (weak linkage+COMDAT / naked attr / section) called from `declare_function` and
  `declare_method` inside their body-gated attribute blocks; (c) new `codegen_naked_prologue`.
- `compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo` — `generate_function_body`
  branches to the naked path.
- `compiler/src/compiler/codegen/abi.cryo` — `AbiClassifier` gained `naked_kind` cache
  (field + both constructor inits), `naked_attr_kind()`, `apply_naked_attribute()`.
- `compiler/llvm_bindings.h` — added `void LLVMSetSection(LLVMValueRef, const char*)`.
- `compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo` — `IntrinsicKind` gained
  `Trap; DebugTrap; FrameAddress; ReturnAddress;` + `from_name` rows.
- `compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo` — `a0` zero-arg guard;
  `expected` arg-count arms for Trap/DebugTrap (0); dispatcher arms for all four; new helpers
  `get_or_decl_void_noarg`, `emit_addr_intrinsic`.
- `stdlib/core/intrinsics.cryo` — declared `trap() -> never`, `debugtrap() -> void`,
  `frame_address(u32) -> void*`, `return_address(u32) -> void*` (inert until called).

Docs / memory (from earlier in the session):
- `runtime/NOTES-capabilities.md`, `runtime/PROPOSALS.md` — updated for the decisions
  (P1 `![no_mangle]` ✅, P9 ✅, P11 `![naked]` ✅; `![…]` syntax; the "already reserved in
  docs/cryo.md" finding for no_mangle/weak/section/constructor).
- Memory: `runtime-project-phase0-2026-07-20.md` (+ MEMORY.md pointer).

---

## 7. Codebase navigation cheatsheet (so you don't re-derive it)

- **Add a directive:** four edits in `directive_processing.cryo` — `is_known_builtin` (the
  big `match` returning `true`), the did-you-mean `cands` list, a `validate_builtin` arm
  (arity + `DirectiveTarget` placement), and `apply_effects` (function branch on
  `FunctionDeclNode*`, method branch on `MethodNode*` → set fields on `m.func`). Resolve a
  `SymbolStr` to a string with `ctx.resolve_str(sym)`.
- **Add an intrinsic:** enum variant in `intrinsics_codegen.cryo`; a `from_name` row; (opt)
  a `src_type()` row if arg0 needs auto-deref (default `null()` = no deref); an `expected`
  arg-count arm in `intrinsic_emitter.cryo` (default 1); a dispatcher `match` arm; usually a
  `get_or_decl_*` + `emit_*` helper; and a `stdlib/core/intrinsics.cryo` declaration. A
  `-> never` intrinsic must emit its own `unreachable` in the arm (the dispatcher's early
  return bypasses the divergent-name check) — see `Panic`/`Trap`.
- **Add a function attribute** (like naked): `AbiClassifier` gets a `<name>_kind: u32` cache
  field (+ init `0` in **both** `null()` and `for_triple()` literals), a `<name>_attr_kind()`
  lazily calling `llvm::LLVMGetEnumAttributeKindForName(...)`, and an
  `apply_<name>_attribute(fn_val)` doing `LLVMCreateEnumAttribute` +
  `LLVMAddAttributeAtIndex(fn.raw, 0xFFFFFFFF, attr)`.
- **Expose a new LLVM-C function:** add its prototype to `compiler/llvm_bindings.h`; it's
  auto-imported into the `llvm::` namespace by `codegen/_module.cryo:45`. Call it as
  `llvm::LLVMWhatever(...)`.
- **Three function-definition paths** all need to agree on the emitted symbol:
  `declare_function` (free fns), `declare_method` (impl methods), `declare_extern_block`
  (extern "C" decls). Methods/externs already read `has_link_name`; free fns now do too.
- **Codegen pass order** (memory): declarations first (P0–P2), then bodies (P3/P4). So when a
  body is emitted, every function is already declared+registered.

---

## 8. Suggested order for tomorrow

1. **Verify P19/P20** (probe in §3) + `make test`. ~10 min. Don't skip.
2. **P9 `no_runtime`** (§4) — the highest-value remaining piece; plan is spelled out.
3. `make selfhost-check` (WSL) — confirm byte-identity for the whole batch. **The canary.**
4. Optionally **P5** overflow intrinsics if time.
5. Small doc follow-up: remove the reserved `![export]` row from `docs/cryo.md` §18.3 + the
   directives table, and flip `![no_mangle]`/`![weak]`/`![section]` from "reserved" to
   "implemented".
6. Hand back to Jake to review + commit + repin. **Do not commit or repin yourself.**

Do not start the actual `runtime/` tier code (core/alloc/panic/…) until P9 lands — without
`no_runtime` you can't build `runtime/core`, and the tier archives depend on it.
