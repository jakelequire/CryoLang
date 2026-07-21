# `runtime/PROPOSALS.md` — compiler/language changes the runtime needs

> Numbered proposals for every `PARTIAL`/`MISSING` capability in
> `NOTES-capabilities.md`. Each follows the HANDOFF §0.3 shape: **what** is missing,
> **surface syntax** (if any), the **sema** change, the **codegen** change, and **why the
> alternative is worse**. These are requests for the repo owner to approve/scope — not
> work started. Per HANDOFF §0.3, the owner prefers a real feature over an `unsafe`
> workaround.
>
> **Tiers** reflect *this* project's scope (HANDOFF §5/§6: hosted, abort-panic; unwinding
> and freestanding `_start` deferred):
>
> - **Tier 1 — blocks Phases 1–4** (the hosted, abort-panic runtime). Do these to ship the
>   project as scoped.
> - **Tier 2 — completes the lang-item set** (bounds/overflow checks, init hooks). Needed
>   for full acceptance but each is an independent, self-contained feature.
> - **Tier 3 — deferred scope** (freestanding `_start`, unwinding, backtrace, thread
>   ergonomics). Recorded so the seam is honest; **not** to be built in this project.
>
> Items marked **⚠ ESCALATE** match a HANDOFF §10 escalation trigger — raise with the repo
> owner before starting, not after.

---

## Tier 1 — blocks Phases 1–4

<a id="p1"></a>
### P1 — Implement `![no_mangle]` / `![export]` (preferred), fixing free-function *definitions*  ✅ APPROVED (Jake, 2026-07-20)

- **Status:** PARTIAL. **`![no_mangle]` / `![export]` are already RESERVED** in the language
  (`docs/cryo.md` §18.3 + reserved-directives table ~`:2884`: "will suppress Cryo name
  mangling so the function ships under its declared identifier") but **unimplemented** —
  parsed as an unknown directive today (warning, no semantics; not in `is_known_builtin`,
  `directive_processing.cryo:209`). The only *implemented* unmangling mechanism is
  `![symbol("name")]`, and it is **silently ignored on top-level `function` definitions**
  (honored on extern decls + methods only; `declare_function` never reads `has_link_name`).
- **Decision (Jake):** **prefer `![no_mangle]`** over `![symbol("…")]`. The common case is
  "emit this under its own name" (`__cryo_panic`, `__cryo_alloc`), which `![no_mangle]`
  expresses with no argument; `![symbol("other")]` stays for the rarer *rename* case. So a
  lang-item definition is written `function __cryo_panic(...) { ... } ![no_mangle]`.
- **Why it blocks:** every lang-item *definition* in `runtime/` is a free function that must
  emit under an exact, unmangled symbol. Today only a function literally named `main` escapes
  mangling (special-cased, `declaration_emitter.cryo:394`).
- **Surface syntax:** `![no_mangle]` (alias `![export]`) on a function → emit under the
  source identifier verbatim. Already-reserved spelling; nothing to bikeshed.
- **Sema:** add `no_mangle`/`export` to `is_known_builtin` (`directive_processing.cryo:209`,
  `:718`); in `apply_effects`, set `fn.link_name = <source name>` + `fn.has_link_name = true`,
  reusing the existing `symbol` path (`:963` fn / `:1066` method) for both `FunctionDeclNode`
  and `MethodNode`.
- **Codegen (the one real fix):** `declare_function` (`declaration_emitter.cryo:347`) must
  read `func.has_link_name`/`func.link_name` as the emitted symbol, the way `declare_method`
  (~`:965`) and `declare_extern_block` (~`:1374`) already do. This single site makes **both**
  `![no_mangle]` and `![symbol]` work on free-function definitions.
- **Docs:** flip the `![no_mangle]`/`![export]` row in `docs/cryo.md` (§18.3 + table
  ~`:2884`) from "reserved / post-1.0" to implemented once landed.
- **Alternative rejected:** `![symbol("__cryo_panic")]` on a function named `panic` also
  works, but is redundant — Jake prefers naming the function `__cryo_panic` with the
  argument-free `![no_mangle]`, matching the already-reserved language surface.
- **Effort:** small. Prerequisite for Phase 3.

<a id="p2"></a>
### P2 — User-facing weak-linkage directive `![weak]` / `![linkage("weak")]`

- **Status:** PARTIAL (`NOTES` §1.1-Q3). `LLinkage` enum + `set_linkage` + WeakODR/COMDAT
  all exist and are used for monomorphizations; there is **no directive** to request weak
  linkage on a user symbol. The intended spelling **`![weak]` is already RESERVED** in
  `docs/cryo.md` (reserved-directives table) — syntax decided, implementation missing.
- **Why it blocks:** HANDOFF §3/§4 require a **user-overridable** global allocator and a
  selectable `__cryo_panic` — i.e. the runtime defines them `weak` so a user object can
  override. Not expressible today.
- **Surface syntax:** `![weak]` on a definition → `WeakAny` (overridable);
  `![weak]` on an `extern` declaration → `ExternalWeak` (absent-symbol-tolerant).
  Optionally `![linkage("weak"|"weak_odr"|"linkonce")]` for the full set.
- **Sema:** add the directive to `is_known_builtin` (`directive_processing.cryo:209`) +
  a validator; set a `link_weak` flag on the decl in `apply_effects`.
- **Codegen:** in `declare_function`/`declare_method`, after creating the value, call the
  existing `set_linkage(LLinkage::WeakAny)` (+ COMDAT via the existing
  `apply_spec_linkage`/`attach_runtime_comdat` path) when the flag is set. Reuses all
  existing machinery.
- **Alternative rejected:** shipping a non-overridable allocator/panic contradicts the
  Rust-model design (`#[global_allocator]`, `__rust_alloc` weak) the HANDOFF is explicitly
  built on; post-hoc symbol interposition via linker flags is fragile and non-portable.
- **Effort:** small–medium. Needed for Phase 4 (weak `__cryo_alloc`) and to keep the panic
  symbol overridable.

<a id="p9"></a>
### P9 — `no_runtime` cryoconfig flag + freestanding `LinkerConfig` profile + dependency diagnostic  ✅ APPROVED (Jake, 2026-07-20)

- **Decision (Jake, 2026-07-20):** APPROVED — add `no_runtime` so the runtime *and* stdlib
  can be built truly free of any compiler-emitted runtime, enabling a proper tiered runtime
  implementation.
- **Status:** MISSING (`NOTES` §1.6-A2). `no_std` exists but only removes the stdlib; it
  leaves crt0 + `-lm -lstdc++`/`-lm -lws2_32` unconditionally on and never emits
  `-nostartfiles`/`-nostdlib`. No `no_runtime` anywhere.
- **Why it blocks:** This is the Phase-2 deliverable and the mechanism that breaks the
  bootstrap cycle — `runtime/core` is Cryo, but compiling Cryo emits calls *into* core, so
  core must build with the lang-item calls suppressed.
- **Surface syntax:** `[compiler] no_runtime = true` (mirrors `no_std`), plus `--no-runtime`
  CLI flag. Document the four-way `no_std`×`no_runtime` matrix; recommend `no_runtime=true`
  ⇒ `no_std=true`.
- **Sema/driver:** add `no_runtime` to `ProjectConfig` (`project_config.cryo:228` pattern)
  and carry it on `ctx`/`CompilerInstance` like `no_std`. Gate lang-item emission on it via
  the `CfgEnv` pattern (`passes/config_gating.cryo:118`).
- **Codegen:** (1) when `no_runtime`, the bounds/overflow/check paths emit `llvm.trap`
  ([P19]) or nothing instead of `__cryo_*` calls, and no `@panic` runtime is emitted; (2) a
  freestanding `LinkerConfig` profile (`-nostartfiles -nostdlib -static`, empty
  `extra_system_libs`) in `linker_config_for_triple` (`passes.cryo:85`); (3) no implicit
  entry shim.
- **Diagnostic:** a `no_runtime=false` package transitively depending on a `no_runtime=true`
  package (or the reverse mismatch) is a hard error, not a warning (HANDOFF §2). Hook the
  `DepResolver` walk (`deps/dep_resolver.cryo:178`).
- **Alternative rejected:** overloading `no_std` (HANDOFF explicitly warns against this) —
  `no_std` legitimately means "no standard library modules," which is not the same as "no
  compiler-emitted runtime calls"; a `no_std` freestanding kernel might still want bounds
  checks that trap.
- **Effort:** medium. The Phase-2 core deliverable.

<a id="p19"></a>
### P19 — `llvm.trap` / `llvm.debugtrap` intrinsics

- **Status:** MISSING (`NOTES` §1.8, §1.2). No trap intrinsic; the only divergence path is
  `@panic` → libc `abort`. Bare `unreachable` is deliberately avoided (lowers to nothing on
  x86, slides into the next function).
- **Why it blocks:** The `no_runtime` check-failure path ([P9]) must abort **without
  touching libc**. `llvm.trap` (→ `ud2` on x86, guaranteed SIGILL) is exactly that.
- **Surface syntax:** none required for the runtime's use — a compiler-internal intrinsic is
  enough. Optionally expose `intrinsics::trap()` for user freestanding code.
- **Sema:** none (compiler-internal), or add `trap`/`debugtrap` to
  `IntrinsicKind::from_name` (`intrinsics_codegen.cryo:165`) if user-exposed.
- **Codegen:** add `IntrinsicKind::Trap`/`DebugTrap` emitting `llvm.trap`/`llvm.debugtrap`
  (a one-arm addition to the intrinsic emitter). Route the `no_runtime` check-failure and
  the mute internal traps ([P6]) to it under `no_runtime`.
- **Alternative rejected:** calling libc `abort` in a `no_runtime` build defeats the whole
  point (core must not reference libc); a bare `unreachable` is unsafe here per the existing
  codegen comments.
- **Effort:** small.

<a id="p7"></a>
### P7 — One runtime-library `__cryo_panic` symbol (replace per-module in-IR emission)

- **Runtime side DONE (2026-07-21).** `panic/abort/` (its own `cryoconfig`, archive
  `libcryort-panic-abort.a`) *defines* `__cryo_panic(msg, file, line)` freestanding and
  dual-OS: Linux `write(2)`+`exit_group` syscalls, Windows kernel32 `WriteFile` + ntdll
  `NtTerminateProcess`; register-pinned asm; zero undefined symbols (no libc). Prints
  `panicked at <file>:<line>: <msg>` and exits 101. Verified both OSes
  (`verify-freestanding.sh`, wine for Windows). **The compiler codegen swap below is
  deferred** — per the repo owner, build the runtime out first, keep the compiler on its
  libc `@panic`, integrate once the runtime is complete.
- **Status:** PARTIAL (`NOTES` §1.2, §6). `@panic` is re-emitted `linkonce_odr` into *every*
  module (`emit_panic_runtime`, `intrinsic_emitter.cryo:1103`; body = printf+fflush+abort),
  collapsed by the linker — not linked from a runtime object.
- **Why it blocks:** HANDOFF §6 requires the panic behavior to live behind **one** symbol
  defined in `panic/abort/`, taking structured arguments and formatting inside the runtime.
  This is the seam that lets `--panic=unwind` be added later with no `core/` change.
- **Surface syntax:** none — `core/` *declares* `__cryo_panic` (extern, [P1]/[P4] operand
  shape); `panic/abort/` *defines* it (link-resolved, [P4] contract).
- **Sema:** none.
- **Codegen:** stop emitting the `linkonce_odr` in-IR `@panic` body; instead emit a plain
  `declare` for `__cryo_panic` and link the definition from the runtime archive. Delete
  `emit_panic_runtime`'s body-emission; keep the call-site emitter (`emit_panic_call`
  `:684`) pointed at the external symbol.
- **Alternative rejected:** keeping the per-module in-IR body means panic behavior can never
  be swapped at link time (the abort→unwind selection) and duplicates the formatter into
  every object; it also hardcodes libc `printf`/`abort` into every module, blocking
  `no_runtime`.
- **Effort:** medium. Core Phase-3 work; pairs with [P6].

<a id="p6"></a>
### P6 — Consolidate the four mute internal traps onto the panic funnel

- **Status:** PARTIAL/HAVE-but-mute (`NOTES` §1.2). Div-by-zero, `new T[n]` size overflow,
  and both non-exhaustive-match sinks each open-code `call @abort(); unreachable` with no
  message or location.
- **Why:** HANDOFF §0.2 explicitly rejects "panic implemented by `printf`+`abort` inline at
  every call site." These four should route through the single funnel ([P7]) with a
  message/location, so a div-by-zero and a bad discriminant are distinguishable.
- **Surface syntax:** none.
- **Sema:** none.
- **Codegen:** replace the four inline `abort` emissions (`emit_int_div_guard`
  `expr_ops.cryo:1712`, `emit_array_mul_guard` `:1782`, `no_match_bb`
  `ir_generator.cryo:732`, `default_bb` `:1783`) with typed calls to the corresponding lang
  items (`__cryo_panic_div_zero`, `__cryo_panic_overflow`, `__cryo_panic_no_match`) — or, in
  `no_runtime`, `llvm.trap` ([P19]).
- **Alternative rejected:** leaving them as bare `abort` keeps the "two disjoint panic
  paths" problem and the mute-SIGABRT debugging pain the HANDOFF calls out.
- **Effort:** small–medium (mechanical once [P7] and the lang-item slots exist).

<a id="p8"></a>
### P8 — Consolidate the six open-coded allocator call sites behind one resolver helper

- **Status:** HAVE-but-scattered (`NOTES` §1.2). Six codegen sites each independently try
  `std::alloc::allocator::alloc` then fall back to bare `malloc`, with an AST-arena divert.
- **Why:** Turns "the allocator" into a single lang-item lookup (`__cryo_alloc`/`__cryo_dealloc`,
  size+align) instead of six copies of the fallback logic — a precondition for the weak,
  overridable allocator ([P2], Phase 4) and for `no_runtime`.
- **Surface syntax:** none.
- **Sema:** none.
- **Codegen:** factor the "resolve `allocator::alloc` else `malloc`, divert AST nodes to
  arena" logic into one helper the six sites (`new_delete_emitter.cryo:224`,`:472`,`:146`,
  `:633`; `array_lit_emitter.cryo:192`,`:349`; `expr_ops.cryo:2268`,`:1961`) call. Point the
  helper at the `__cryo_alloc`/`__cryo_dealloc` lang items.
- **Alternative rejected:** six divergent copies make the weak-override and `no_runtime`
  wiring six separate edits, each a chance to drift.
- **Effort:** medium (refactor, behavior-preserving).

---

## Tier 2 — completes the lang-item set

<a id="p4"></a>
### P4 — Emit bounds checks + `__cryo_panic_bounds_check` lang item

- **Status:** MISSING (`NOTES` §1.2). `codegen_array_access` (`place_emitter.cryo:275`) GEPs
  with **no** length comparison on any indexing form — out-of-bounds is silent UB. No check,
  no callee.
- **Why:** HANDOFF §3.1 lists `panic_bounds_check` as a minimum lang item, taking **raw**
  index+length+location (not a pre-formatted string). Two parts: create the lang item, and
  teach codegen to emit the check.
- **Surface syntax:** none (implicit on `a[i]`). Possibly a `[profile]` toggle to disable in
  release, matching Rust's `-C debug-assertions`.
- **Sema:** none (codegen has the slice/array length already).
- **Codegen:** at each indexing form in `codegen_array_access`, when the length is known
  (fat pointer/`Array`/`Str` carry it; fixed arrays are compile-time), emit `icmp uge idx,
  len` → conditional branch to a check-fail block calling
  `__cryo_panic_bounds_check(file, line, idx, len)` (or `llvm.trap` under `no_runtime`). Raw
  `T*`/`i8*` indexing has no length and stays unchecked (document it).
- **Runtime side:** `panic/abort/` (or `core/checks.cryo` per HANDOFF §4) defines the
  handler; it formats `index N out of bounds for length M` **from the raw operands**.
- **Alternative rejected:** passing a caller-formatted string (HANDOFF §0.2 explicitly bans
  this) bloats every call site and pulls formatting into codegen.
- **Effort:** medium. This is a real safety feature, independently valuable.

<a id="p5"></a>
### P5 — Overflow-checked-arithmetic intrinsics + emit overflow checks + `__cryo_panic_overflow`

- **Status:** MISSING (`NOTES` §1.2, §1.8). `codegen_binary` emits bare add/sub/mul (no
  `nsw`/`nuw`), silent two's-complement wrap. The `llvm.{s,u}{add,sub,mul}.with.overflow`
  family is not exposed anywhere (the memory note claiming otherwise is wrong).
- **Why:** HANDOFF §3.1 lists `panic_overflow` taking operation-kind + operand values +
  location. Requires exposing the intrinsics *and* an opt-in checked-lowering path.
- **Surface syntax:** none for the default; a `[profile] overflow_checks = true|false`
  toggle (checked in debug, wrapping in release, like Rust).
- **Sema:** none.
- **Codegen:** (1) add `IntrinsicKind` arms for the six `.with.overflow` intrinsics
  (`intrinsics_codegen.cryo`/`intrinsic_emitter.cryo`), producing the `{iN, i1}` aggregate
  and `extractvalue`; (2) in `codegen_binary` (`expr_ops.cryo:1480`), under the checks
  profile, lower `+`/`-`/`*` to the checked intrinsic + branch-on-overflow-flag to a call to
  `__cryo_panic_overflow(file, line, op_kind, lhs, rhs)` (or `llvm.trap` under `no_runtime`).
- **Alternative rejected:** hand-rolled range comparisons (what stdlib `checked_add` does
  today) are per-type, error-prone, and don't cover signed `INT_MIN` edge cases the
  intrinsics handle for free.
- **Effort:** medium. The intrinsic exposure alone is small and independently useful
  (unblocks `bswap`-style [P21] uses too).

<a id="p3"></a>
### P3 — Section placement: `![section("…")]` + `LLVMSetSection` binding + `.init_array`/`global_ctors` emitter

- **Status:** MISSING (`NOTES` §1.1-Q5). No `LLVMSetSection` binding, no ctor-array emission;
  `Appending` linkage defined but unused; the reserved `C$mi$` module-init name has no
  emitter. The intended spellings **`![section("name")]` and `![constructor]`/`![destructor]`
  are already RESERVED** in `docs/cryo.md` — syntax decided, implementation missing.
- **Why:** HANDOFF §1.1 asks whether runtime init can be hooked *without owning `main`*.
  Today it cannot. **Lower priority for this project** because §5 has the runtime own `main`
  directly — but required if init hooks (e.g. per-module constructors, TLS-key setup) are
  ever wanted without an explicit call from `start.cryo`.
- **Surface syntax:** `![section("name")]` on a global/function; optionally a
  `![constructor]` / `![init]` attribute that appends to `llvm.global_ctors`.
- **Sema:** add the directive(s) to `is_known_builtin` + validators.
- **Codegen:** add an `LLVMSetSection` FFI binding to `llvm_types.cryo`; apply it from the
  directive; for constructors, emit an `Appending`-linkage `llvm.global_ctors` array (wire
  the reserved `C$mi$` module-init form to it).
- **Alternative rejected:** requiring every init to be an explicit call in `start.cryo` is
  fine for the hosted runtime but doesn't compose for libraries that want to self-register;
  hand-rolled `.init_array` via inline asm is non-portable across object formats (ELF vs
  COFF).
- **Effort:** medium. Deferrable within this project.

<a id="p21"></a>
### P21 — Re-expose bit intrinsics in the current stdlib surface

- **Status:** HAVE at the compiler layer; stdlib surface omits them (`NOTES` §1.8).
  `bswap*/clz*/ctz*/popcount*/rotl*/rotr*` fully lower, but only `legacy/stdlib` declares
  them.
- **Why:** The runtime's LSDA/DWARF/ELF byte-parsing (backtrace, [P17]) and hashing want
  `clz`/`bswap`/`popcount` without reaching into `legacy/`. Zero compiler change needed.
- **Surface syntax:** re-declare `intrinsic function clz64(x: u64) -> u32;` etc. in
  `stdlib/core/intrinsics.cryo` (the `intrinsic function` keyword is already user-accessible
  — same mechanism as `malloc`).
- **Sema/codegen:** none — the lowering exists (`intrinsics_codegen.cryo:78`).
- **Alternative rejected:** each runtime consumer re-declaring the intrinsic ad hoc works but
  scatters the surface; one stdlib declaration is cleaner.
- **Effort:** trivial (declarations only). Not a compiler change.

---

## Tier 3 — deferred scope (record the seam; do not build in this project)

<a id="p11"></a>
### P11 — `![naked]` function attribute (no prologue/epilogue)  ✅ APPROVED (Jake, 2026-07-20)

- **Status:** MISSING (`NOTES` §1.8). Every function goes through `codegen_function_prologue`
  (`declaration_emitter.cryo:1629`) unconditionally; there is no naked path. Unlike
  `![no_mangle]`/`![weak]`/`![section]`, **`![naked]` is NOT in the `docs/cryo.md`
  reserved-directives table** — it is genuinely new surface (add it there).
- **Decision (Jake):** APPROVED as the *enabling capability*. NOTE: the freestanding `_start`
  that ultimately consumes it stays deferred (HANDOFF §5) — `![naked]` lands ahead of it so
  the seam exists. Module-level global `asm{}` remains a partial stand-in for a text-only
  `_start` in the meantime.
- **Why:** gates a true freestanding `_start` that reads `argc`/`argv` off the initial stack
  before any prologue shifts it, and register-exact setjmp/longjmp.
- **Surface syntax:** `![naked]` on a function: no prologue/epilogue, body restricted to a
  single `asm{}` block, no implicit return, params not accessible by name (per HANDOFF §1.8).
- **Sema:** add `naked` to `is_known_builtin` (`directive_processing.cryo:209`); validate the
  body is a single asm block, forbid named-param access, forbid implicit return.
- **Codegen:** skip `codegen_function_prologue`/epilogue for the flagged function; set LLVM's
  `naked` fn-attr on the value.
- **Docs:** add `![naked]` to the `docs/cryo.md` reserved-directives table, then flip to
  implemented once landed.
- **Alternative rejected:** module-level global asm can emit a text-only `_start` but cannot
  reference Cryo values/params by operand, so setjmp/longjmp and a param-referencing `_start`
  remain impossible; a `fs:`-relative hand-rolled TLS/stack read breaks across models/targets.
- **Effort:** small–medium.

<a id="p10"></a>
### P10 — Compiler-emitted TLS (`thread_local` global + selectable TLS model)

- **Status:** MISSING (`NOTES` §1.5-Q2, §1.8). All TLS goes through
  `pthread_key_create`/`TlsAlloc`; no `.tbss`/`.tdata`, no TLS relocations.
- **Why (deferred):** The `thread_panicking` flag §5 reserves can live in a `pthread_key` for
  now; real compiler TLS is only *needed* for the freestanding target (no pthread) and to
  drop the `ThreadLocal<T>` indirection + null-destructor leak.
- **Surface syntax:** `thread_local static X: T = …;` (or `![thread_local]` on a static).
- **Sema:** recognize the modifier; restrict to statics.
- **Codegen:** add `LLVMSetThreadLocalMode` binding; emit the global thread-local with a
  target-appropriate TLS model (initial-exec for the static-linked freestanding case).
- **Alternative rejected:** a hand-rolled `fs:`-relative asm load (HANDOFF §1.8 explicitly
  rejects this) breaks across TLS models, static vs dynamic linking, and targets.
- **Effort:** medium. Deferred with the freestanding/threading work.

<a id="p12"></a>
### P12 — Landing-pad / `invoke` codegen + `__cryo_personality_v0` + cleanup emitter ⚠ ESCALATE

- **Status:** MISSING (`NOTES` §1.4). Zero `invoke`/`landingpad`/`personality`/`resume` in
  codegen; panics abort.
- **Why (deferred):** This is the unwinding feature itself — **explicitly out of scope**
  (HANDOFF §6). Recorded so the seam is honest. Gated on [P13]–[P15] and a panic-strategy
  decision.
- **Shape (per HANDOFF §6):** emit `invoke` at panic-capable call sites; landing pads run
  `drop` on live locals (reading the persisted cleanup schedule, [P14]); emit
  `.gcc_except_table` (LSDA); write `__cryo_personality_v0` in Cryo against the
  libgcc/libunwind `_Unwind_*` ABI (C-ABI signature + LSDA ULEB128 byte parsing — no C
  needed); a `thread_panicking` TLS flag.
- **Alternative recorded:** setjmp/longjmp + a runtime cleanup-list is cheaper but worse
  (per-frame overhead on the happy path, and it hard-requires `![naked]` [P11]).
- **Scope note (stackless, `NOTES` §D.8):** the coroutine model is stackless, so a panic
  inside an `async fn` unwinds the *native* frame of the `poll()` caller — there is **no
  coroutine stack to walk**. This unwinder therefore stays ordinary native-stack DWARF
  unwinding; the stackless decision does not enlarge its scope.
- **Escalate:** HANDOFF §10 — the drop findings show unwinding needs drop-emission rework;
  the owner should scope this deliberately.

<a id="p13"></a>
### P13 — Definite-initialization / init-flag tracking for droppable locals ⚠ ESCALATE

- **Status:** MISSING/unverified (`NOTES` §1.4-Q3). Drop flags track conditional *move-out*
  but (apparently) not conditional *initialization*; `mut x: T;` assigned in one branch may
  get an unconditional scope-exit drop of uninitialized storage.
- **Why:** A latent normal-path soundness hole *and* a hard prerequisite for any cleanup
  path (a landing pad between decl and conditional init would drop garbage). Also relevant
  independent of unwinding.
- **Sema/pass:** first *verify* whether sema already forbids conditionally-initialized
  droppable locals; if not, add definite-assignment (or init-flag) tracking to
  `drop_insertion.cryo`/`move_check.cryo`, symmetric to the existing move drop-flags.
- **Alternative rejected:** ignoring it risks dropping uninitialized memory — a
  memory-safety bug, not a leak.
- **Escalate:** verify-first; if it's a real hole it should be fixed regardless of the
  runtime project.

<a id="p14"></a>
### P14 — Persist a per-scope cleanup schedule (reusable by landing pads)

- **Status:** MISSING (`NOTES` §1.4). Drop schedules are computed inline at hard-coded exit
  sites, not left behind as a queryable per-scope table.
- **Why (deferred):** A landing pad at an arbitrary call site needs "live droppable bindings
  for the enclosing scopes, reverse order, with their drop-flag references" on demand —
  exactly what `append_cumulative_drops` computes but discards.
- **Pass change:** refactor `DropInsertion` to also record, per lexical scope, the ordered
  `(binding, type, drop-flag?)` cleanup entries — consumed by both the existing normal-exit
  synthesis and future landing pads. Low risk; reuses existing logic.
- **Alternative rejected:** re-deriving the schedule at each landing pad duplicates the
  pass's core logic in codegen.
- **Effort:** medium. Gated with [P12].

<a id="p15"></a>
### P15 — Close the match-expression-arm non-yielded-local leak

- **Status:** MISSING (`NOTES` §1.4-Q2). A `let` local declared and not yielded inside a
  match-expression arm is not auto-dropped (`drop_insertion.cryo:522`).
- **Why:** A normal-path leak today; becomes an unwinding gap once landing pads exist.
- **Pass change:** drop an arm's own locals after the arm's value expression materializes.
- **Alternative rejected:** leaving it means cleanup paths inherit the leak.
- **Effort:** small–medium. Independently valuable (fixes a real leak now).

<a id="p17"></a>
### P17 — Crash/signal/backtrace subsystem (greenfield, Cryo)

- **Status:** MISSING (`NOTES` §1.7). `signal.cryo` is constants only; no `sigaction`, no
  backtrace, no symbolization anywhere. **Nothing to port** — it does not exist in any
  language.
- **Why (deferred):** HANDOFF §5/§1.7 want signal handlers + a pretty stack trace in
  `hosted/`. Because there's no existing code, this is **net-new Cryo** written from scratch,
  not a Phase-3 relocation. The HANDOFF's own caveat (§10) and LOW_LEVEL_PLAN both flag
  signals/async-signal-safety as the hardest libc-coupled area.
- **Shape:** `sigaction` FFI + handler install in `hosted/signal.cryo`; frame walk via
  `frameaddress`/`returnaddress` intrinsics ([P20]) *not* asm; symbolization = ELF + DWARF
  byte parsing (ordinary Cryo, uses [P21] bit intrinsics).
- **Alternative rejected:** none — there is no C to keep; writing it in C would violate §0.4.
- **Effort:** large. A standalone project after the core runtime lands.

<a id="p20"></a>
### P20 — `llvm.frameaddress` / `llvm.returnaddress` intrinsics

- **Status:** MISSING (`NOTES` §1.8). No `IntrinsicKind`; not emitted.
- **Why (deferred):** Needed by the backtrace frame-walk ([P17]). Portable; the asm
  equivalents are not.
- **Codegen:** add two `IntrinsicKind` arms emitting `llvm.frameaddress`/`llvm.returnaddress`
  (one-arm additions, like the existing `bswap` family).
- **Alternative rejected:** reading the frame/return address via inline asm is non-portable
  and invisible to the optimizer (HANDOFF §0.4 prefers intrinsics over asm).
- **Effort:** small. Gated with [P17].

<a id="p18"></a>
### P18 — Thread ergonomics: `ThreadLocal<T>` per-thread destructor + Mutex poisoning story

- **Status:** MISSING by design (`NOTES` §1.5-Q1/Q3). `ThreadLocal<T>` uses a null
  `pthread_key` destructor → per-thread leak. Mutexes are never poisoned (no cross-thread
  panic-catch) — **the reported trigger for this whole project**.
- **Why (deferred):** Both are downstream of the panic/unwind decision. Poisoning specifically
  needs a `panic`-catch/unwind runtime to set a poisoned flag and return `Result`/`PoisonError`
  from `lock()`.
- **Options:** (a) `ThreadLocal<T>`: wire a per-`T` monomorphized `extern "C"` destructor
  through the generic-mono pipeline and pass it to `pthread_key_create` instead of `null`
  (non-blocking leak fix, doable now). (b) Mutex: either build the unwind runtime ([P12]) so
  poisoning is expressible, or formally adopt the "no poisoning; use `try_lock`/`lock_timeout`"
  contract and document it.
- **Escalate context:** decide the poisoning contract *with* the panic-strategy decision (D.7
  in `NOTES`), since it's the question that motivated the runtime.
- **Effort:** (a) medium, doable now; (b) gated on [P12].

<a id="p16"></a>
### P16 — Direct `lld`/`ld` link path + in-process archive writer (toolchain-driver-free build)

- **Status:** PARTIAL (`NOTES` §1.6-B5, §1.1-Q6). Archiving already works with `llvm-ar`
  alone (no C compiler); **linking still hard-requires** a `cc`/gcc/clang driver via
  `system(3)`.
- **Why (deferred):** HANDOFF §7 asks that the build invoke only `cryoc` + the linker.
  Archives already satisfy this; a fully C-toolchain-free *link* would need a direct
  `ld.lld` invocation path. Not blocking (the current `cc`-driver link works), but recorded.
- **Codegen/driver:** add an optional `ld.lld`/`ld` link path to `LinkerConfig`/`run_linking`
  (`passes.cryo:85`,`:972`) selected by config; optionally an in-process `LLVMWriteArchive`
  path to drop the external `ar` too.
- **Alternative rejected:** none needed short-term — `cc`-driver linking is standard and
  works; this is a purity/portability nicety for freestanding CI.
- **Effort:** medium. Low priority.

<a id="p22"></a>
### P22 — asm symbol-operand class + unique-label support

- **Status:** PARTIAL (`NOTES` §1.8-item5). No operand-level symbol binding (`s`/`X` class);
  no `%=`-style unique labels for reused function-scoped blocks.
- **Why (deferred):** Only matters for hand-written trampolines that reference Cryo
  globals/functions as operands or need local labels in an inlined-more-than-once block. The
  module-level global-asm path sidesteps it for `_start`.
- **Codegen:** add a symbol constraint kind to `AsmConstraintKind` + a unique-label
  substitution in `ir_generator.cryo`.
- **Alternative rejected:** raw-text symbol references work for externs; this is an
  ergonomics/safety improvement, not a blocker.
- **Effort:** small–medium. Nice-to-have.

---

## Summary — what to do first

**Approved by Jake (2026-07-20):** **[P1]** `![no_mangle]` ✅ · **[P9]** `no_runtime` ✅ ·
**[P11]** `![naked]` ✅ (enabling capability; the freestanding `_start` that consumes it stays
deferred).

If the goal is to ship the runtime as scoped (hosted, abort-panic, Phases 1–4):

1. **[P1]** `![no_mangle]` on free-function defs ✅ · **[P9]** `no_runtime` + freestanding
   linker profile ✅ · **[P19]** `llvm.trap` — the three that unblock everything.
2. **[P7]** single `__cryo_panic` symbol · **[P6]** consolidate mute traps · **[P8]**
   consolidate alloc sites — the Phase-3 lang-item consolidation.
3. **[P2]** `![weak]` directive (already reserved) — needed for the Phase-4 overridable
   allocator.
4. **[P4]** bounds checks · **[P5]** overflow checks — Tier 2, each an independent safety
   feature completing the lang-item set.

Everything else in Tier 3 (unwinding [P12]–[P15], freestanding-`_start` link work [P10],
backtrace [P17]/[P20], thread ergonomics [P18]) is **recorded but out of this project's
scope**. [P11] `![naked]` is approved as an enabling capability even though the freestanding
`_start` stays deferred. The remaining ⚠ items should be escalated before any work starts.
