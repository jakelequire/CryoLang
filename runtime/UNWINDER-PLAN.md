# Unwinder implementation plan (P14 → P12 → P15 + catch_unwind)

Working tracker for the DWARF-unwinding feature. Authoritative design lives in
`PROPOSALS.md` (P12/P14/P15) and `NOTES-capabilities.md` (§1.4, §D.7, §D.8). This file is
the execution checklist; tick items as they land.

## Locked decisions (Jake, this session)

1. **Full unwinder** — execute P14 → P12 → P15 plus a root `catch_unwind`.
2. **Unwind library = LLVM libunwind.** `__cryo_personality_v0` + the raising `__cryo_panic`
   are written against the shared `_Unwind_*` Level-1 ABI and the GCC `.gcc_except_table`
   LSDA format (LLVM's backend auto-emits the table). Link libunwind standalone (`-lunwind`).
   The `_Unwind_*` ABI is identical to libgcc's, so the choice is link-time only. Windows SEH
   deferred (follow-up, matching PEB-args / `.pdata`-backtrace).
3. **Include `catch_unwind` / root catch.** Required for correctness *and* testability: by the
   Itanium two-phase contract an uncaught panic finds no handler in phase 1 → phase-2 cleanups
   are skipped → destructors would not run. A catch at the program/thread root makes drops
   actually run on panic; it is also the async task-isolation primitive.
4. **Default `--panic=abort`; entire feature gated behind `--panic=unwind`.** The default path
   emits byte-identical IR, so `make selfhost-check` stays a fixed point throughout.
5. **`invoke` only where the current frame has live droppable locals;** plain `call` otherwise
   (nothing to clean up — unwind passes through).

## Verified checkpoints

- **Foundation (A + F + B/P14), verified green.** `make cryo` clean; `make selfhost-check` exit 0
  with TWO `FIXED POINT OK` (Linux-via-WSL stage3==stage4 + Windows native-PE stage3==stage4;
  233 modules, ~73.9 MB IR). Confirms the gated changes leave the default abort path
  byte-identical. Nothing committed. Clean base for Phase C.
- **Phase C (invoke/landingpad/resume + personality), verified green.** `make cryo` clean;
  `--panic=unwind --emit-llvm` on probes shows correct Itanium cleanup-invoke IR (LLVM verifier
  accepts): `personality @__cryo_personality_v0`, `invoke ... to cont unwind lpad`, cleanup-only
  `landingpad {ptr,i32}` replaying drops + `resume`; a local moved into the call is excluded from
  the lpad (no double-free on unwind). Default abort build shows 0 EH constructs (plain `call`).
  `make selfhost-check` exit 0, TWO `FIXED POINT OK` (Linux IR md5 efc42c06…; Windows 233 modules)
  — default path stays byte-identical. Nothing committed. Clean base for Phase E.
- **Phase E (unwind tier + compiler integration), verified green.** `verify-freestanding.sh` OK
  (tier compiles both OSes; abort tier passes). `make selfhost-check` exit 0, TWO `FIXED POINT OK`,
  Linux IR md5 STILL efc42c06… (byte-identical default path across all Phase-C/E changes). WSL
  end-to-end (compiler full link pipeline): `--panic=unwind` panic → raise → phase-1 personality
  LSDA parse → uncaught → `panicked at …: boom` + exit 101; default abort build of the same program
  unchanged (SIGABRT/134). Nothing committed. Clean base for Phase D. NOTE: link wiring drops the
  locked `-lunwind` for gcc/glibc hosts (libgcc_s provides the ABI) — flag for Jake.

## Phase checklist

- [x] **A. LLVM-C EH bindings** — added `LLVMBuildInvoke2`, `LLVMBuildLandingPad`,
  `LLVMBuildResume`, `LLVMAddClause`, `LLVMSetCleanup`, `LLVMSetPersonalityFn` to
  `compiler/llvm_bindings.h`; wrappers `LBuilder::build_invoke/build_landing_pad/build_resume`,
  `LValue::add_clause/set_cleanup/set_personality_fn` in `codegen/llvm_types.cryo`. Compiles.
- [x] **F. `--panic` flag plumbing** — CLI `--panic=<abort|unwind>` (`CLI/_module.cryo`
  `is_known_flag` + `flag_takes_value`; parsed in `CLI/commands.cryo` build + single-file) and
  cryoconfig `panic = "unwind"` (`project_config.cryo` field + `parse_panic` + clone/default).
  `panic_unwind` on BOTH `ProjectConfig` and `CompilerConfig` (CLI-level); →
  `CompilationContext.project_panic_unwind` (instance.cryo + compilation_context.cryo). Default
  abort. Config-gating skipped (tier chosen at link, no `![config(panic_unwind)]` needed). Compiles.
- [x] **B. P14 cleanup schedule** — `CallExprNode.unwind_cleanup: ASTNode*[]` (each a
  `StatementNode*`; `ASTNode*` avoids the Expr<->Stmt import cycle). DropInsertion populates it in
  the `CallExpression` arm of `walk_expr_read` (`:2728`) via `populate_unwind_cleanup` →
  `append_cumulative_drops` — AFTER `read_call` records this call's own receiver/arg moves, so a
  local moved INTO the call is excluded. Gated on `this.ctx.project_panic_unwind`, so default abort
  populates nothing → identical IR. Confirmed `maybe_append_drop` is pure w.r.t. IR-affecting state
  (only bumps the diagnostic `inserted` counter). Compiles.
  **Known first-impl limitation:** only already-declared NAMED locals are cleaned. Anonymous
  argument temporaries mid-construction (`f(a, g(b))` where `g` panics: `a`/`g`'s result leak) are
  NOT tracked — a LEAK on unwind, never a double-free/UAF (memory-safe). Follow-up: arg-temp drop
  flags. Also: only calls routed through `walk_expr_read` get a schedule; codegen-synthesized
  checks (bounds/overflow) are a separate origin — route them through invoke in a follow-up.
- [x] **C. invoke/landingpad/resume + personality** — IMPLEMENTED as designed and IR-verified
  under `--panic=unwind --emit-llvm` (LLVM verifier accepts; default abort path emits 0 EH
  constructs = byte-identical). What landed:
  - ExprOps gained transient invoke state (`invoke_active/invoke_normal/invoke_lpad`) + three new
    helpers: `build_call_maybe_invoke` (wraps the THREE callee `build_call` sites — zero-arg, 1:1,
    dp_expand — emitting `invoke ... to cont unwind lpad` then repositioning at cont when armed,
    consuming the flag on first use), `get_or_decl_personality` (external `i32
    @__cryo_personality_v0(i32,i32,i64,ptr,ptr)`), `landingpad_type` (`{ptr,i32}` literal struct).
  - `call_emitter.cryo` `emit`: `needs_invoke = ctx.project_panic_unwind &&
    node.unwind_cleanup.length>0`; appends `invoke.cont`/`invoke.lpad`, arms ExprOps state around the
    single `codegen_call`, then `emit_cleanup_landingpad` (attaches personality, builds
    cleanup-only `landingpad`, replays `node.unwind_cleanup` drops innermost-first, `resume`s;
    restores builder to cont). `should_unreachable` still terminates cont for never-callees.
  - **Verified:** ordinary call with a live droppable local → correct invoke+cleanup lpad; a local
    MOVED into the call is excluded from the lpad (no double-free on unwind); drops run on both the
    normal (scope-exit) and unwind (lpad) paths = dropped exactly once at runtime.
  - **Discovered limitation (leak-only, memory-safe; extends the §B/§H follow-up):** a *direct*
    `core::panic(...)` call is emitted by `intrinsic_emitter.cryo:759` `emit_panic_call` as a plain
    `call @panic; unreachable`, on the intrinsics-first fast-path that never reaches the general
    call path — so its DropInsertion-populated schedule is ignored and locals in a frame that
    diverges only via a direct panic (or a codegen-synthesized bounds/overflow/div-zero/no-match
    check) are NOT dropped on unwind. Routing these panic origins through `invoke` is the same
    follow-up already noted for the synthesized checks; do it once Phase E's raising `__cryo_panic`
    exists (until then panic printf+aborts and does not unwind at all).
  - Original design (as built):
  - **Decision (call vs invoke):** in `call_emitter.cryo` main emit path (the method that holds
    `visitor` + `node`), `needs_invoke = cg.ctx.project_panic_unwind && node.unwind_cleanup.length>0`.
  - **Invoke swap choke-point:** add ExprOps transient state (`invoke_active/invoke_normal/
    invoke_lpad`) + `build_call_maybe_invoke(fn_ty,fn,args,n,name)` replacing the THREE `build_call`
    sites in `expr_ops.cryo` (`:919` zero-arg, `:965` 1:1, and inside `codegen_call_direct_dp_expand`).
    When `invoke_active`: `build_invoke(...then=invoke_normal, catch=invoke_lpad...)` then
    `position_at_end(invoke_normal)` so post-call work lands in the normal block (NOT after the
    invoke terminator). Re-entrancy-safe: args are fully emitted BEFORE `codegen_call`, so nested
    calls already handled; set state only around the `codegen_call` call, clear after.
  - **call_emitter invoke branch:** create `invoke.cont` (normal) + `invoke.lpad` blocks via
    `LBasicBlock::append(current_fn,...)`; set ExprOps invoke state; call `codegen_call` (emits the
    invoke, positions at normal); clear state; then `emit_cleanup_landingpad(visitor, node,
    current_fn, lpad_bb)` (saves block=normal, positions at lpad, `build_landing_pad({ptr,i32},0)`,
    `set_cleanup(true)`, replay `node.unwind_cleanup[i] as StatementNode*` via `.accept(visitor)`,
    `build_resume(lp)`, restore to normal). Existing `should_unreachable` (`:1206`) already handles
    the noreturn-callee normal edge (panic invoke's normal block = `unreachable`).
  - **Landingpad type:** `{ptr,i32}` via `LLVMStructTypeInContext(ctx,[ptrTy,i32Ty],2,0)` — add a
    cached helper on CodegenContext (ptr=`LLVMPointerTypeInContext(ctx,0)`, i32=`LLVMInt32TypeInContext`).
  - **Personality:** declare external `i32 @__cryo_personality_v0(...)` (via the function-registry
    get-or-declare path); `current_fn.set_personality_fn(pers)` inside `emit_cleanup_landingpad`
    (idempotent). Under `--panic=unwind` the linker pulls the definition from the `panic/unwind` tier.
- [x] **E. runtime `panic/unwind/` tier** — BUILT, compiler-integrated, and end-to-end validated
  on WSL (uncaught-panic path). What landed:
  - **Tier** `runtime/panic/unwind/src/lib.cryo` (ns `CryoRt::Panic::Unwind`) + `[[lib]]` member
    `cryort-panic-unwind` in `runtime/cryoconfig`. Defines: raising `__cryo_panic`
    (`_Unwind_RaiseException`; on return = uncaught → print + exit 101); `__cryo_personality_v0`
    (LSDA `.gcc_except_table` ULEB128 parse — header + call-site table walk, IP→landing-pad,
    two-phase cleanup/handler install; handles LLVM's encodings [lpStart omit, call-site uleb128],
    other encodings bail-safe to CONTINUE_UNWIND); the four check handlers (mirror abort, funnel
    through `__cryo_panic`); `_Unwind_*` `extern "C"` decls. Windows funnel is an abort-style stub
    (SEH deferred). **NOTE (deviation, stated):** used plain globals for the in-flight exception
    object, NOT `![thread_local]` — single-threaded first cut; becomes thread-local when the async
    executor unwinds on worker threads. **NOTE:** action-table ttypeFilter not yet read (single
    catch-all root uses selector 1); typed catches are a follow-up (Phase D refines).
  - **Compiler reroute (gated on `project_panic_unwind`):** `IntrinsicEmitter` gained a
    `panic_unwind` flag (wired from `ctx.project_panic_unwind` in `context.cryo`); `emit_panic_call`
    now routes to an external `declare __cryo_panic` (via `get_or_decl_cryo_panic`) instead of the
    per-module libc `linkonce_odr @panic`, so a panic RAISES. `passes.cryo` skips the eager
    `emit_panic_runtime` under panic_unwind (the linkonce body would be dead). **IR-verified:** under
    `--panic=unwind`, a panic origin emits `call @__cryo_panic`; the enclosing ordinary call is an
    `invoke` with `personality @__cryo_personality_v0` + cleanup lpad; both symbols are `declare`d;
    NO dead `@panic`. Default abort build unchanged (linkonce `@panic`, no EH).
  - **Link wiring (gated):** `CodegenPasses::append_panic_unwind_libs` appends
    `<stdlib_root>/../runtime/.bin/libcryort-panic-unwind.a -lunwind` in BOTH link paths
    (`run_linking` + `run_linking_singlefile`); returns cmd unchanged off panic_unwind. Archive
    locator is stdlib-root-relative (repo layout); a distribution-time locator is a follow-up.
  - **`-lunwind` correction (deviation from locked flag, e2e-justified — flag for Jake):** the
    link wiring links the tier archive but does NOT add `-lunwind`. On a standard gcc/glibc host
    `libunwind.so.8` ships without a `libunwind.so` dev symlink, so a hardcoded `-lunwind` breaks
    the link; the identical `_Unwind_*` ABI is already provided by gcc's always-linked `libgcc_s`
    (the locked decision itself says the provider is a link-time-only choice). An explicit
    unwinder for an LLVM-libunwind / freestanding combo is a `[link]`-config follow-up.
  - **Validated:** `verify-freestanding.sh` OK (tier compiles both OSes; abort tier still passes).
    `make selfhost-check` exit 0, TWO `FIXED POINT OK`, Linux IR md5 unchanged (efc42c06…) across
    all Phase-C/E changes → default path byte-identical. End-to-end on WSL, BOTH via a manual
    libgcc_s link AND the compiler's own full link pipeline: a `--panic=unwind` program panicking
    through an ordinary call → raises, personality parses `main`'s LSDA in phase 1 (clean run =
    correct call-site walk), uncaught → `panicked at …: boom` + exit 101. Default abort build of
    the same program still links + runs (SIGABRT/134). **Phase-2 cleanup (drops actually running
    on unwind) is validated with Phase D** — by the Itanium contract phase 2 is skipped until a
    phase-1 handler (the root catch) exists.
- [ ] **D. catch_unwind / root catch** — catch landing pad (`landingpad … catch ptr null`) at
  program/thread root; a `catch_unwind`-style seam returning ok/err. Personality returns
  HANDLER_FOUND (phase 1) / installs (phase 2) for the catch frame.
- [ ] **G. P15 match-arm leak** — in `drop_insertion.cryo` `walk_block_body_impl` (value-block
  branch ~`:659`), drop the arm's non-yielded locals after the value expr while keeping the
  value statement last (coordinate with codegen arm-value extraction).
- [ ] **H. Gates + repin** — compile-and-run tests under `--panic=unwind` (Linux/WSL): drops run
  on panic, `catch_unwind` returns err, no double-free/leak. `make cryo && make test &&
  make selfhost-check` (default path, must stay fixed point). `runtime/verify-freestanding.sh`
  for the new tier. `make pin` (plain) at clean green. **Do not commit** (Jake commits).

## Key anchors (from the code map; may drift)

- Call emission: `codegen/visit/call_emitter.cryo:1133` → `codegen/ops/expr_ops.cryo:919/965`
  (`build_call`). `never`-return terminates via `build_unreachable` `:1206`.
- BB idiom: `LBasicBlock::append` (`llvm_types.cryo:1056`); save/restore in
  `codegen/ops/flow_emitter.cryo:192-230` (`create_entry_alloca`).
- Personality attach: `codegen/ops/declaration_emitter.cryo:1722` (`codegen_function_prologue`,
  entry block at ~:1778); attribute-helper pattern in `codegen/abi.cryo:1120`.
- Drop schedule: `passes/drop_insertion.cryo` `append_cumulative_drops:1870` /
  `append_drops_to_depth:1851` / `maybe_append_drop:2080`; `synth_drops` precedent
  `AST/statement.cryo:213`, replayed `codegen/visit/ir_generator.cryo:512`.
- Panic emit (current, per-module `panic`/libc-abort): `codegen/ops/intrinsic_emitter.cryo:1190`
  (`emit_panic_runtime`), call at `:759` (`emit_panic_call`); driven `codegen/passes.cryo:342`.
- Link/archive selection: `codegen/passes.cryo:981` (`run_linking`), `no_runtime` profile `:1094`,
  stdlib archive `:1175`, verbatim `link_static` `:1112`.
- `no_runtime` flag plumbing (template): `CLI/_module.cryo:324/306`, `CLI/commands.cryo:1146/2738`,
  `project_config.cryo:673/235/366`, `instance.cryo:755/1045`,
  `compilation_context.cryo:143`, `codegen/context.cryo:130`, `passes/config_gating.cryo:139/153`.
- Runtime workspace: `runtime/cryoconfig` (`[[lib]]` members); `LibTarget`
  `project_config.cryo:62/318`, parse `:581`, build loop `instance.cryo:836`.
