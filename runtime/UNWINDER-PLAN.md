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
- [ ] **C. invoke/landingpad/resume + personality** — DESIGN (grounded, ready to implement):
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
- [ ] **E. runtime `panic/unwind/` tier** — new `[[lib]]` workspace member
  (`runtime/cryoconfig` + `runtime/panic/unwind/src/lib.cryo`, ns `CryoRt::Panic::Unwind`).
  Defines raising `__cryo_panic` (`_Unwind_RaiseException`), `__cryo_personality_v0` (LSDA
  ULEB128 parse; cleanup + catch), the four check handlers funneling through `__cryo_panic`,
  and a `![thread_local]` `thread_panicking` flag. `core/` declares `__cryo_panic` (unchanged
  by strategy).
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
