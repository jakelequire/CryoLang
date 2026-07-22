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
- **Phase D (catch_unwind + auto root catch + PanicInfo), verified green.** The full gate suite
  passes: `make selfhost-check` exit 0, TWO `FIXED POINT OK`, Linux IR md5 STILL
  `efc42c0605a181dda8f93848d0d3c828` (the gated feature leaves the default abort path byte-identical
  → NO repin) · `verify-freestanding.sh` OK (both OSes; D3 accessors + `__cryo_panic_finish` compile;
  abort tier still passes) · `make test` OVERALL PASS (unit ok, 143 compile-fail, 8 projects incl.
  `native_alloc_gate`). WSL end-to-end (`--panic=unwind`, compiler full link pipeline): (1) explicit
  `catch_unwind<i32>(work)` around a panicking chain with an intermediate droppable local → returns
  `Err(PanicInfo{msg:"boom", file, line:25})`, drop ran exactly once (`drops==1`), exit 0; (2) an
  UNCAUGHT panic through the auto `main` wrapper → the intermediate frame's cleanup pad drops its
  local FIRST (stderr `DROP probe ran on unwind`), THEN the wrapper reports `panicked at …:28:
  uncaught boom` and exits 101 — proving phase-2 destructors run on an otherwise-uncaught panic; (3)
  `--panic=abort` build of a `catch_unwind` program → clean `E0600` ("requires `--panic=unwind`"),
  not a cryptic link error. Nothing committed (Jake commits). Clean base for G. **DEVIATION (stated):
  catch_unwind is `--panic=unwind`-only for now** — under abort it is a compile error rather than a
  no-op-catch, because the payload accessors live only in the unwind tier and a default abort build
  links no runtime tier at all (see G/H notes). Making abort a linkable no-op-catch (via `![weak]`
  stdlib fallback accessors the tier overrides) is a follow-up.

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
- [x] **D. catch_unwind / root catch** — DONE + validated (see the Phase-D checkpoint above). What
  landed: the `try_catch(body,data)` compiler primitive (`call_emitter.cryo::emit_catch_unwind`,
  name-intercepted in both the Identifier and ScopeResolution branches) emits `invoke body(data)` +
  a catch-all `landingpad {ptr,i32} catch null` + a caught-flag phi; off `--panic=unwind` it is a
  clear `E0600`. The stdlib `core::panic_unwind` (all generic/lazy) provides `PanicInfo{msg,file,
  line}`, `CatchCtx<T>`, the trampoline `run_catch_body<T>`, and `catch_unwind<T>(f:()->T) ->
  Result<T,PanicInfo>`; `intrinsics::try_catch` declared in `core::intrinsics`. The auto root catch
  is a synthesized `main` wrapper (`declaration_emitter.cryo::emit_main_unwind_wrapper`): under
  `--panic=unwind` the user body runs under `__cryo_user_main` (renamed in BOTH `declare_function`
  and `codegen_function_prologue`) and `main` invokes it inside the catch pad, calling the tier's
  `__cryo_panic_finish` (report stashed payload + exit 101) on catch. Tier D3: `__cryo_panic` stashes
  msg/file/line into i64 globals before raising (NOT `string` globals — a freestanding string-literal
  global initializer SIGSEGVs the compiler; store pointers as zero-init i64 and cast) + accessors
  `__cryo_panic_taken_message/_file/_line` + `__cryo_panic_finish`. Original design notes retained
  below for reference.
  Personality returns HANDLER_FOUND (phase 1) / installs (phase 2) for the catch frame.
  **Jake decided (this session):** SURFACE = compiler intrinsic + stdlib wrapper
  `catch_unwind<T>(f: fn() -> T) -> Result<T, PanicInfo>` (fn-ptr, not a rich closure — Cryo closure
  limits). INJECTION = auto root catch at `main` AND every thread entry.
  Personality catch path is already coded (cs_action!=0 → HANDLER_FOUND phase 1 / install selector 1
  phase 2); the new work is compiler-side: the `@catch_unwind` intrinsic emitting the catch pad, the
  stdlib wrapper + `PanicInfo`, propagating the panic payload from `__cryo_panic` via the exception
  object, and auto-wrapping main + thread entries. First validation target: **drops actually run on
  panic** (a root catch → phase-1 handler → phase-2 runs intermediate cleanup pads).
  **Implementation notes (grounded, from this session's exploration):**
  - **Intrinsic `@catch_unwind` (codegen):** inline in the caller, mirror Phase-C's
    `emit_cleanup_landingpad` but with a CATCH clause: `invoke <body>() to %cu.cont unwind %cu.lpad`;
    `cu.lpad`: `landingpad {ptr,i32}` + `add_clause(null-ptr-const)` (catch-all, NOT set_cleanup);
    extract exn (`extractvalue …,0`), read the payload the exception object carries, branch to a
    `caught` result; `cu.cont` = normal result. Attach `__cryo_personality_v0` (reuse
    `ExprOps::get_or_decl_personality`). Phi the caught-flag. The EH wrappers (`add_clause`) already
    exist from Phase A; `LType::const_pointer_null` exists (used in `intrinsic_emitter`).
  - **Root catch = lang_start-style wrapper (NOT body surgery).** `declaration_emitter.cryo`
    emits the user `main` directly under the C symbol `main` (`:428/:430`) with an argc/argv/envp
    prologue (`emit_set_args_call`/`emit_set_env_call` at `:1896`). Under `--panic=unwind`: emit the
    user body under an internal symbol (e.g. `__cryo_user_main`), and generate a `main(argc,argv,envp)`
    wrapper that runs the set_args/set_env prologue then `@catch_unwind(__cryo_user_main)` → return
    its i32 on Ok / report + `return 101` on Err. Thread entries: same wrapper around each thread
    trampoline (the executor's spawn path, later — no thread spawns exist to wrap yet, so main is the
    only live injection site today; wire the thread-entry variant when spawn lands).
  - **Payload:** `PanicInfo` (stdlib) built from what `__cryo_panic` stashed (msg/file/line — today
    plain globals in the unwind tier; the exception object could carry a pointer). Simplest first cut:
    the catch reads the tier's stashed msg/file/line globals.
  - **Cheapest path to the drops-run validation:** the `@catch_unwind` intrinsic + an EXPLICIT test
    (`@catch_unwind` around a call chain with an intermediate droppable local that panics) proves
    phase-2 cleanups run WITHOUT the main-wrapper surgery. Do that first, then the auto-main wrapper.
  - `PanicInfo` + selector: the personality currently sets selector=1 for the catch frame WITHOUT
    reading the action-table ttypeFilter; make the catch landing pad catch-all UNCONDITIONAL (don't
    compare the selector) so it is robust regardless of the exact selector value.
  **DESIGN LOCKED (this session, grounded in the stdlib `thread` precedent):** the intrinsic takes an
  OPAQUE void-returning trampoline (no generic-T knowledge in codegen), mirroring Rust's `try`
  intrinsic — the value is transported in a per-T context struct, not by the intrinsic. Facts that
  forced this: intrinsics CANNOT be generic and cannot take fn-ptr params; `mut x: T;` (uninit local)
  written through a raw `T*` is not drop-synthesized (used by `mem::transmute` AND `thread::thread_body`
  `*rptr = body(c)`); fn-ptr type syntax is `(Args) -> Ret`, a generic free fn is referenceable as a
  fn-ptr (`thread_trampoline<C,T>` → `as void*`). Pieces:
  - **Intrinsic (non-generic, name-intercepted in `CallEmitter::emit`, NOT via `IntrinsicKind`):**
    `intrinsic function try_catch(body: void*, data: void*) -> boolean`. Codegen `emit_catch_unwind`:
    `invoke body(data) to cu.cont unwind cu.lpad` (fn ty `void(ptr)`); `cu.cont` → br done;
    `cu.lpad`: `landingpad {ptr,i32}` + `add_clause(null-ptr)` catch-all (NO set_cleanup) → br done;
    `cu.done`: `%caught = phi i1 [false,cont],[true,lpad]`. Attaches `__cryo_personality_v0`. Gated:
    off `--panic=unwind` it emits a PLAIN `call body(data)` + `false` (abort strategy = a panic aborts
    the process, never returns; keeps abort builds linkable, no personality needed). Intercepted in
    BOTH the Identifier and ScopeResolution branches by leaf name `try_catch`.
  - **stdlib** (new `stdlib/core/panic_unwind.cryo`, ns `std::core::panic_unwind`; all generic/lazy so
    the default selfhost IR is untouched until instantiated): `PanicInfo { msg, file, line }`;
    `CatchCtx<T> { body: () -> T, out: T* }`; free trampoline
    `run_catch_body<T>(data: u8*) { const c = data as CatchCtx<T>*; *((*c).out) = ((*c).body)(); }`;
    static `Panic::catch_unwind<T>(f: () -> T) -> Result<T, PanicInfo>` = `mut result: T;` + build ctx
    + `try_catch(run_catch_body<T> as void*, &ctx as void*)` → Err(PanicInfo from accessors) / Ok(result).
    `extern "C"` decls for the three accessors below.
  - **Runtime tier D3:** `__cryo_panic` stashes msg/file/line into plain globals BEFORE raising;
    `![no_mangle]` accessors `__cryo_panic_taken_message/_file/_line()` read them back for the Err arm.
- [ ] **G. P15 match-arm leak** — STILL OPEN (deliberately deferred from the Phase-D batch: it is
  independent of the unwinder AND ungated, so unlike everything above it WOULD change the default-path
  IR — i.e. move the selfhost fixed point off `efc42c06…` and require a repin. Kept separate so the
  Phase-D increment stays a clean byte-identical no-repin unit; do G as its own change so its md5
  delta is attributable). In `drop_insertion.cryo` `walk_block_body_impl` (value-block branch
  ~`:659`), drop the arm's non-yielded locals after the value expr while keeping the value statement
  last (coordinate with codegen arm-value extraction). Then `make selfhost-check` will move the md5;
  repin (both OSes) at clean green.
- [~] **H. Gates** — DONE for the Phase-D (D1/D2/D3) increment: `make cryo` clean · `make test`
  OVERALL PASS · `make selfhost-check` TWO `FIXED POINT OK`, Linux md5 `efc42c06…` UNCHANGED (default
  path byte-identical) · `runtime/verify-freestanding.sh` OK (both OSes) · WSL `--panic=unwind` e2e
  (explicit catch + auto root catch + abort compile-error) all pass. **No repin done or needed** (the
  feature is gated → default path is a fixed point). **Not committed** (Jake commits). Re-run these
  gates + repin AFTER G (which does change the fixed point).

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
