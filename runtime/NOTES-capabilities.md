# `runtime/NOTES-capabilities.md` — Phase 0 capability audit

> **Phase 0 deliverable** for the `./runtime` project (see `HANDOFF` at repo root).
> This is the reviewed artifact that gates all later phases: it records, for every
> capability the runtime needs, whether the compiler `HAVE`s it, `PARTIAL`ly has it,
> or is `MISSING` it — with a `file:line` + symbol citation for each.
>
> **Method.** Seven parallel investigators read the compiler and stdlib source on
> branch `ll-impl` and cited what they confirmed. Line numbers are as-of this audit
> and may drift; the symbol names are the durable anchors. No runtime code was
> written (per HANDOFF §8, Phase 0 = this file + `PROPOSALS.md` only).
>
> Every `PARTIAL`/`MISSING` item below has a numbered entry in `PROPOSALS.md`.

---

## 0. Executive summary — what changed about the plan

Five findings materially reshape the HANDOFF's assumptions. Read these first.

1. **There is essentially nothing to "port to Cryo" (HANDOFF §0.4, §1.7, §3 Phase 3).**
   A full-repo scan finds **zero `.c`/`.S`/`.cpp`/`.asm` files under `compiler/` or
   `stdlib/`.** The only C in the tree is the dead `legacy/bootstrap/` C++ compiler and
   two *test-only* helpers (`tests/helpers/abi_helpers.c`, `cpp_link_helper.cpp`). The
   runtime is already inline-lowered LLVM IR. **Consequence:** the Phase-3 "port the
   crash/signal/backtrace code" task is empty by construction. That subsystem does not
   exist in *any* language yet — `stdlib/process/signal.cryo` is signal-*number*
   constants only, with no `sigaction`/handlers. So it is **greenfield new Cryo work**,
   not a port. The "no C files" acceptance criterion is met today and only needs a CI
   guard to keep it met.

2. **The machine-level layer is in far better shape than feared (HANDOFF §1.8).**
   The two capabilities the HANDOFF called out as *expected proposals* — inline-asm
   operand constraints/clobbers, and atomics with real memory orderings — **already
   exist, are exercised by tests, and self-host** (the native x86-64 syscall path
   depends on both). The genuine machine-level gaps are narrower: `![naked]` functions,
   `llvm.trap`, compiler-emitted TLS, `frameaddress`/`returnaddress`, and the
   overflow-checked-arithmetic intrinsic family. **Crucially, `![naked]` only gates the
   *freestanding* `_start`, which the HANDOFF (§5) explicitly defers** — so it does not
   block Phases 1–4.

3. **The drop model is mature, not "lexical-only, no flags" (HANDOFF §1.4).** Cryo
   already implements flow-sensitive drops with **synthesized drop flags** for
   conditional move-out, cumulative reverse-order drops at *every normal* control-flow
   exit, a **fatal** move-checker that guarantees the drop-set is double-free-free, and
   full recursive glue into fields/enum-payloads/tuples/arrays/closures, all
   per-monomorphization. The unwinder is still a real, separate compiler project, but
   the *analysis* foundation it needs is already built and correct for the abort path.

4. **`no_runtime` is genuinely distinct from `no_std`, and does not exist yet
   (HANDOFF §2).** `no_std` is a real driver+loader+sema+link flag, but it only removes
   the *standard library* (modules, prelude, `libcryo.a`). It leaves crt0/`_start` and
   the `-lm -lstdc++` / `-lm -lws2_32` system-lib line **unconditionally on**, and never
   emits `-nostartfiles`/`-nostdlib`. So `no_runtime` must add capability that is
   currently absent, exactly as the HANDOFF anticipated.

5. **The current panic ABI does not match the HANDOFF's "walk a slice and a `str`"
   premise (HANDOFF §1.3, §3.1).** Today `@panic` takes **thin, NUL-terminated `i8*`**
   for both message and file (the primitive `string` type is `i8*`, not a fat pointer),
   plus a `u32` line. "Format from raw operands" is therefore a *design decision*
   (change the signature to pass `Str{ptr,len}`, or keep consuming C strings), not an
   existing fact. See §D.

### Capability matrix

| # | Capability | Verdict | Proposal |
|---|---|---|---|
| 1.1-1 | Symbol-name mangling (`C$` scheme) | **HAVE** | — |
| 1.1-2 | `![no_mangle]`/`![symbol]` on **free-function definitions** | **PARTIAL** (approved) | [P1](PROPOSALS.md#p1) |
| 1.1-3 | Weak symbols (user-facing directive) | **PARTIAL** | [P2](PROPOSALS.md#p2) |
| 1.1-4 | Declare-without-define (extern, link-resolved) | **HAVE** | — |
| 1.1-5 | Section placement / `.init_array` / ctors | **MISSING** | [P3](PROPOSALS.md#p3) |
| 1.1-6 | Link-command construction | **HAVE** (cc-driver + `system()`) | [P16](PROPOSALS.md#p16) |
| 1.2 | Single panic funnel symbol | **PARTIAL** (two disjoint paths) | [P6](PROPOSALS.md#p6),[P7](PROPOSALS.md#p7) |
| 1.2 | Bounds check (emit + lang item) | **MISSING** | [P4](PROPOSALS.md#p4) |
| 1.2 | Integer-overflow check (emit + lang item) | **MISSING** | [P5](PROPOSALS.md#p5) |
| 1.2 | Div-zero / array-mul / no-match traps | **HAVE** but mute | [P6](PROPOSALS.md#p6) |
| 1.2 | Allocator call sites | **HAVE** (scattered ×6) | [P8](PROPOSALS.md#p8) |
| 1.2 | Program entry point (`main`) | **HAVE** | — |
| 1.3 | Slice/str/array/enum/class/closure layouts | **HAVE** | — (pin in `layout.def`) |
| 1.3 | Vtable drop/size/align slot | **MISSING** (fn-ptrs only) | see §D |
| 1.4 | Drop model (flow-sensitive + flags) | **HAVE** | — |
| 1.4 | Conditional-**init** drop-flag tracking | **MISSING/unverified** | [P13](PROPOSALS.md#p13) |
| 1.4 | Unwinding (landing pads / personality) | **MISSING** | [P12](PROPOSALS.md#p12)–[P15](PROPOSALS.md#p15) |
| 1.5 | Compiler-native TLS | **MISSING** (pthread_key only) | [P10](PROPOSALS.md#p10) |
| 1.5 | Mutex poisoning | **MISSING** (by design) | [P18](PROPOSALS.md#p18) |
| 1.6 | `cryoconfig` parse / `no_std` | **HAVE** | — |
| 1.6 | `no_runtime` flag | **MISSING** (approved) | [P9](PROPOSALS.md#p9) |
| §7 | Archive without C driver (`llvm-ar`) | **HAVE** | — |
| §7 | Link without C driver (`ld`/`lld`) | **MISSING** | [P16](PROPOSALS.md#p16) |
| 1.7 | Crash / backtrace / signal infra | **MISSING** (greenfield) | [P17](PROPOSALS.md#p17) |
| 1.8 | asm operands / clobbers / volatile / global-asm | **HAVE** | — |
| 1.8 | Naked functions (`![naked]`) | **MISSING** (approved) | [P11](PROPOSALS.md#p11) |
| 1.8 | Atomics with real orderings + fences | **HAVE** | — |
| 1.8 | `llvm.trap` / `debugtrap` | **MISSING** | [P19](PROPOSALS.md#p19) |
| 1.8 | Overflow-checked arithmetic intrinsics | **MISSING** | [P5](PROPOSALS.md#p5) |
| 1.8 | `frameaddress` / `returnaddress` | **MISSING** | [P20](PROPOSALS.md#p20) |
| 1.8 | bswap/clz/ctz/popcount/rotate intrinsics | **HAVE** (stdlib surface omits) | [P21](PROPOSALS.md#p21) |
| 1.8 | Syscall via asm (Linux x86-64) | **HAVE** | — |

---

## 1.1 Symbol control and linkage

**Q1 — Symbol-name mangling. `HAVE`.** Every emitted symbol is mangled with a `C$`
prefix scheme. Encoder: `MangleContext` (`compiler/src/compiler/resolver/mangled_name.cryo`);
decoder: `Demangler` (`resolver/demangler.cryo`). Format: `C$` prefix; optional
two-letter kind tag (`vt` vtable, `ti` typeinfo, `ct` ctor, `dt` dtor, `op` operator,
`mi` module-init, `cl` closure, `tr` trait-impl); length-prefixed `.`-joined path
(`encode_path`, ~`:549`); generics `$L…$G`; signature `$F<params>$R<ret>` (empty =
`v`, receiver `$s`/`$m`, variadic `$V`); primitive codes (`int_code`/`float_code`,
~`:930`); overload `$O<N>`, method-spec `$MG<arg>_…$`. Unmangled pass-through already
exists: `MangledName::extern_c` (`mangled_name.cryo:196`).

**Q2 — Unmangled / extern-C on definitions. `PARTIAL` → [P1].** The directive is
`![symbol("name")]` (validated in `directive_processing.cryo` ~`:561`; applied via
`fn.link_name`/`has_link_name` ~`:963`). Codegen honors it on **external declarations**
(`declare_extern_block`, `declaration_emitter.cryo:1374`) and **method definitions**
(`declare_method`, ~`:965`), but **NOT on free-function definitions** —
`declare_function` (`declaration_emitter.cryo:347`) computes the name from the mangled
symbol and never reads `has_link_name`. The only unmangled free function that works is
one literally named `main` (special-cased ~`:394`). **This blocks Phase 3:** lang-item
definitions in `runtime/` are free functions that must emit under exact symbols
(`__cryo_panic`, …). **`![no_mangle]`/`![export]` are already RESERVED** in `docs/cryo.md`
(§18.3 + reserved-directives table ~`:2884`) but unimplemented (parsed as unknown → warning,
no semantics). **Decision (Jake, 2026-07-20): implement and PREFER `![no_mangle]`** over
`![symbol("…")]` — see [P1] (APPROVED).

**Q3 — Weak symbols. `PARTIAL` → [P2].** The linkage machinery is complete but has no
user surface. `LLinkage` mirrors LLVM including `WeakAny=5`/`WeakODR=6`/`ExternalWeak=12`
(`llvm_types.cryo:91`); `LValue::set_linkage` wraps `LLVMSetLinkage` (~`:823`);
monomorphizations already get `WeakODR`+COMDAT (`apply_spec_linkage`,
`declaration_emitter.cryo:337`). But no directive maps to weak linkage
(`is_known_builtin` has no entry), so a user-overridable weak allocator/panic symbol
(HANDOFF §3/§4) is not expressible without a directive. Low-risk: only surface syntax
is missing — and the spelling **`![weak]` is already RESERVED** in `docs/cryo.md`.

**Q4 — Declare-without-define. `HAVE`.** `extern "C" { function name(...) -> T; }`
(parser ~`:2070`) lowers to a bodyless LLVM `declare` (`declare_extern_block`,
`declaration_emitter.cryo:1336`), resolved by the linker from another object. This is
exactly the `core` → `panic` contract mechanism (declare `__cryo_panic` in `core`,
define it in `panic/abort`).

**Q5 — Section placement. `MISSING` → [P3].** No `LLVMSetSection` binding, no
`llvm.global_ctors`/`.init_array` emission anywhere; `Appending` linkage is defined but
unused. The `C$mi$` module-init name form is reserved (`MangledName::for_module_init`,
~`:353`) but has **zero emitters**. This gates "hook runtime init without owning `main`"
— though §5's plan has the runtime *own* `main` directly, making this optional for the
near term. The spellings **`![section("name")]` and `![constructor]`/`![destructor]` are
already RESERVED** in `docs/cryo.md`.

**Q6 — Link command. `HAVE` (with a caveat → [P16]).** Assembled in
`CodegenPasses::run_linking` (`codegen/passes.cryo:972`); toolchain in `LinkerConfig`
(`:62`, `linker_config_for_triple` `:85`). Driver = **`cc`** (via `pick_link_driver`
`:1656`, `CRYO_CC` override; auto-detect cc→gcc→clang; mingw for windows-gnu), invoked
through libc `system(3)` with an `@response` file. No linker script, no direct `ld`/`lld`.
Archiving uses `ar`/`llvm-ar` directly (`pick_archiver` `:1619`, `CRYO_AR`).

---

## 1.2 Codegen lang-item call sites (seed for `abi/lang_items.def`)

**The central shape: two disjoint trap paths.** User/stdlib panics funnel through a
single symbol `@panic(msg:i8*, file:i8*, line:i32)` (prints, then `abort`). The four
*compiler-internal* traps each open-code a **mute** `call @abort(); unreachable` with no
message or location. Consolidating the latter onto the funnel is Phase-3 work
([P6]/[P7]).

| Purpose | Site | Callee today | Defined by | Verdict |
|---|---|---|---|---|
| Bounds check (index) | `place_emitter.cryo:275` `codegen_array_access` | **none** — raw GEP, silent UB | — | **MISSING** [P4] |
| Integer overflow (add/sub/mul) | `expr_ops.cryo:1480` `codegen_binary` | **none** — bare add/sub/mul, wraps | — | **MISSING** [P5] |
| Array-size mul overflow (`new T[n]`) | `expr_ops.cryo:1782` `emit_array_mul_guard` | `abort` | libc | **HAVE**, mute [P6] |
| Div/mod by zero (+ `INT_MIN/-1`) | `expr_ops.cryo:1712` `emit_int_div_guard` | `abort` | libc | **HAVE**, mute [P6] |
| Unwrap empty Option/Result | stdlib `core::panic` → `@panic` | `@panic(msg,file,line)` | compiler in-IR | **HAVE** (message) |
| Non-exhaustive match (stmt) | `ir_generator.cryo:732` `no_match_bb` | `abort` | libc | **HAVE**, mute [P6] |
| Non-exhaustive match (expr) | `ir_generator.cryo:1783` `default_bb` | `abort` | libc | **HAVE**, mute [P6] |
| Heap alloc (`new`, `new[]`, array-lit, box) | 6 sites, e.g. `new_delete_emitter.cryo:224`, `array_lit_emitter.cryo:192` | `allocator::alloc(size,align)` else `malloc` | stdlib / libc | **HAVE**, scattered [P8] |
| `delete` (dealloc + drop) | `new_delete_emitter.cryo:633` | `allocator::free(ptr,align)` else `free` | stdlib / libc | **HAVE** [P8] |
| Program entry | `declaration_emitter.cryo:396` (+ prologue `:1800`) | defines `i32 @main(i32,ptr,ptr)`; calls `env::set_args`/`set_env` | codegen + stdlib | **HAVE** |

Supporting libc externs declared by codegen (`intrinsic_emitter.cryo`): `abort` `:1085`,
`free` `:840`, `memcpy` `:823`, `printf` `:891`, `fflush` `:906`; `malloc` resolved by
bare name.

**Key notes.**
- `@panic` is re-emitted `linkonce_odr` into **every module** (`emit_panic_runtime`,
  `intrinsic_emitter.cryo:1103`; body = `printf` + `fflush(NULL)` + `abort` +
  `unreachable`), collapsed by the linker — **not** linked from a runtime object.
  Moving it behind one runtime-library symbol is HANDOFF §6's goal ([P7]).
- The `never` return type is what auto-emits the trailing `unreachable` at call sites
  (`call_emitter.cryo:1206`) — no name-based special-casing.
- **`llvm.trap` is deliberately never used.** Comments (`ir_generator.cryo:724`,
  `expr_ops.cryo:1753`) explain a bare `unreachable` lowers to *no instruction* on x86
  and would slide into the next function; `abort` gives a clean SIGABRT. **Consequence
  for HANDOFF §2.1:** the `no_runtime` "compile checks to a trap intrinsic" plan needs
  `llvm.trap` added ([P19]) — the naive `unreachable` is unsafe here.
- The driver assumes user `main` is written zero-arg `-> int`
  (`declaration_emitter.cryo:396`, `param_count == 0`), then silently widens to the
  C-ABI three-arg form and injects `set_args`/`set_env` prologue (no-op under
  `--no-std`).

---

## 1.3 Layout facts (seed for `abi/layout.def`)

All read from codegen type-lowering (`type_map.cryo`, `abi.cryo`, `type_lowering.cryo`),
not stdlib comments. Pointer/`u64` = 8 bytes.

| Type | Layout | Size/Align | Source |
|---|---|---|---|
| `Slice<T>` | `{ptr@0, len@8}` fat | 16 / 8 | `slice.cryo:15`; `type_map.cryo` |
| `Str` (borrowed UTF-8) | `{bytes@0, len@8}` fat, **no NUL** | 16 / 8 | `collections/str.cryo:26` |
| primitive `string` | `i8*` thin, **NUL-terminated** | 8 / 8 | `type_map.cryo:229` `map_string` |
| `Array<T>` / legacy `T[]` | `{ptr@0, len@8, cap@16}` | 24 / 8 | `array.cryo:39`; `type_map.cryo:271` |
| `RawBuffer<T>` | `{ptr@0, cap@8}` (+ZST alloc) | — / 8 | `raw_buffer.cryo:25` |
| `String` (owned) | `{ptr@0, cap@8, len@16}` | 24 / 8 | `string.cryo:30` |
| fixed `[N x T]` | LLVM array, no header | N·sz | `type_map.cryo:277` |
| enum ADT | `{i32 tag@0, payload@align_up(4,payload_align)}`; payload@8 if align≥8; **no niche** | max(4,palign) | `type_lowering.cryo:611` `compute_enum_layout`; `type_map.cryo:572` |
| simple enum | tag only; `i32` or explicit base width | base | `type_map.cryo:564` |
| `Optional<T>` | `{T@0, i1 has@sizeof(T)}`, **no niche** | — | `type_map.cryo:326` |
| class instance | `[vtable i8**@0 iff polymorphic][root→leaf fields]` | — | `type_map.cryo:447` `map_class`; `type_lowering.cryo:546` |
| class vtable | `[N x i8*]` fn-ptrs only, root→leaf; **no drop/size/align/typeinfo slot** | — | `declaration_emitter.cryo:864` |
| closure (capturing) | per-lambda named struct, one field per capture; **no uniform `{fn,env}` ABI** | — | `sema/lambda_synth.cryo:239` |
| closure (non-capturing) | bare fn-ptr `i8*` | 8 | `lambda_emitter.cryo:74` |

**ABI passing of a 16-byte fat pointer (Slice/Str) by value:** SysV x86-64 →
**DirectPair** (two GP registers) (`abi.cryo:519`); Win64 → **Indirect+ByVal** (pointer
to the `{ptr,len}` struct) (`abi.cryo:582`). A 24-byte `Array`/`T[]` is ByVal-pointer on
both. **A panic formatter reading operands "raw" must branch on target ABI.**

**No trait objects** (`dyn` deferred post-1.0; `TypeKind::Trait` lowers to placeholder
`i8`, `type_map.cryo:197`). **No vtable carries drop/size** — a future unwinder cannot
recover drop glue or size from a vtable; those come from monomorphized `drop` + `sizeof`.

**Arena allocator (`stdlib/alloc/arena.cryo`).** Bump allocator over mmap/VirtualAlloc
chunks (`Chunk{data,capacity,offset,next}` `:57`, `Arena{head,current,chunk_size}`
`:143`); raw API `bump`/`grow`/`reset`/`release`/`owns`. Reachable from generated code
only via `GlobalAlloc`/`GlobalArena` routing (the compiler-self-host bracket) or by
explicitly typing a collection `Array<T, Arena>`; no ambient arena in an ordinary
binary.

---

## 1.4 Destructors / drop model

**Q1 — Concept + discovery + emission. `HAVE`.** `Drop` trait `drop(mut &this)->void`
(`stdlib/core/drop.cryo:22`); inherent `drop` methods also recognized. Discovery via
`OwnershipQuery::needs_drop` (`types/ownership.cryo:532`): inherent drop
(`has_inherent_drop` `:820`), trait impl keyed by qualified-name identity
(`type_has_drop_impl` `:741`), or transitive aggregate. Emission: `DropInsertion`
synthesizes `.drop()` AST nodes (`make_drop_call`, `passes/drop_insertion.cryo:2270`);
codegen lowers via `call_emitter.cryo` (Copy receiver elided ~`:911`; non-Copy aggregate
→ `emit_drop_glue` `:1255`).

**Q2 — Drops at all scope exits. `HAVE`, one gap.** Covered: fall-through
(`append_top_scope_drops` `:1759`), `return` incl. nested/early (`append_cumulative_drops`
`:1788`, sequenced after the return value; `wrap_early_exit` `:954`), `break`/`continue`
(loop-scoped only, `:1795`), `?`/`??` and match-arm early exits (`:1044`), switch
(`:900`), discarded owning temporaries (`:2063`). **Documented leak:** a `let` local
declared and not yielded inside a **match-expression arm** is not auto-dropped
(`drop_insertion.cryo:522`).

**Q3 — Drop flags. `HAVE` for conditional MOVE; `MISSING`/unverified for conditional
INIT → [P13].** Classic drop-flag technique (`:2112`): a binding moved on some paths gets
a synthesized `<name>__dropflag: boolean` before the branch (`make_drop_flag_decl`
`:2148`), each moving arm sets it (`:2211`), scope exit becomes
`if (!flag) { binding.drop(); }` (`:2183`). Branch join = intersection (definite) +
symmetric difference (flag) (`merge_branches_if` `:1410`). **But** flags track
conditional move-out, **not** conditional *initialization*: `mut x: T;` assigned in only
one `if` branch appears to get an unconditional scope-exit drop and could drop
uninitialized storage on the other path (no definite-init machinery found; whether sema
forbids it upstream is unverified). This is a latent normal-path hole *and* an
unwinding-soundness blocker.

**Q4 — Move model. `HAVE`, flow-sensitive.** `move_check.cryo` does branch-join unions
(`:441`), switch merge (`:834`), two-walk loop-carried detection (`:931`). Records moves
at initializers, `=` RHS, aggregate-literal elements, by-value call args, `return`,
consuming receivers (`mut this`/`![sink]`), `.drop()`, move-captures. DropInsertion
mirrors the identical move-set and suppresses moved bindings' drops
(`is_moved_id`/`maybe_append_drop` `:2020`). MoveCheck is **fatal** (`pass_id.cryo:144`)
— a proven use-after-move aborts before codegen. Partial (field-level) moves tracked
unconditionally only; conditional partial moves are hard `E0453`.

**Q5 — Drop order. `HAVE`.** Reverse declaration order within a scope
(`append_top_scope_drops` iterates `end-1..start`, `:1762`); cumulative drops
innermost-scope-first; parameters registered first so they drop last (`:466`).

**Q6 — Generics/mono. `HAVE`, per-monomorphization.** DropInsertion skips generic
functions (`:436`) and runs strictly after `MonomorphizationComplete` (pipeline
`MoveCheck → DeadCode → DropInsertion → TypeLowering`, `instance.cryo:1786`).
`needs_drop` returns false for unresolved/generic types (`ownership.cryo:625`).

**Q7 — Recursion. `HAVE`.** `emit_drop_glue` (`call_emitter.cryo:1255`) recurses into
struct/class fields (`emit_field_drops` `:1381`), enum payloads (switch on discriminant,
GEP at layout offsets, `:1412`), tuples (`:1316`), fixed arrays (`:1348`), dynamic `T[]`
(→ `Array<T>::drop`), and closures (synthesized `drop`, `lambda_synth.cryo:326`). Unions
are **not** auto-dropped unless they declare their own drop. A user-written drop
short-circuits recursion.

**Implications for unwinding.** The *analysis* is done and correct for the abort path.
Unwinding is a separate compiler project because: (1) **no cleanup path exists** — zero
`invoke`/`landingpad`/`personality`/`resume` in codegen; every `.drop()` sits only on
the normal edge; (2) drop schedules are **transient**, computed inline at hard-coded exit
sites, not persisted as a per-scope table a landing pad could reference on demand; (3)
the two normal-path gaps (match-arm local, conditional-init) become unwinding gaps. See
[P12]–[P15]. Per HANDOFF §6, unwind is out of this project's scope; per HANDOFF §10 this
is an **escalation item** — the repo owner should scope it deliberately before any
landing-pad work.

---

## 1.5 Threads and TLS

**Q1 — TLS concept. `PARTIAL` (stdlib type only).** No `thread_local` keyword/attribute
(grep-clean across the compiler). `ThreadLocal<T>` (`stdlib/thread/local.cryo:86`) is a
runtime library over OS per-thread keys: POSIX `pthread_key_create`/`getspecific`/
`setspecific` (`os_tls_*` `:58`), Windows `TlsAlloc`/`TlsGetValue` (`:63`). Value is
heap-boxed per thread; the key is created with a **null destructor**, so per-thread
allocations **leak at thread exit** unless the caller `clear()`s (`:16`, `:134`) → [P18].

**Q2 — Compiler TLS relocations. `MISSING` → [P10].** No `LLVMSetThreadLocal(Mode)`,
`thread_local`, `.tbss`/`.tdata`, or TLS-model handling anywhere in `compiler/`. The
runtime is **forced** through `pthread_key_create`/`TlsAlloc`. A freestanding runtime
needing real TLS (no pthread) — e.g. the `thread_panicking` flag §5 reserves — has no
compiler support.

**Q3 — Mutex assumptions; poisoning. `HAVE` (pthread/SRWLOCK); poisoning `MISSING` by
design → [P18].** `stdlib/sync/mutex.cryo` wraps a pthread mutex (POSIX, shims `:61`) /
Win32 SRWLOCK (`:63`); heap-pinned `MutexInner{pthread_buf: u8[40]}` (`:96`) because
moving a live pthread mutex is UB; `MutexGuard` is `!Send`. **No poisoning** (`:24`): Cryo
has no cross-thread panic-catch, so a mutex is never marked poisoned; panicking while
holding it leaves it held forever. **This is the reported trigger for the whole runtime
project.** Siblings same pattern (`once.cryo` pthread_once / Windows atomic 3-state;
`rwlock`/`condvar` pthread/Win32), none with poisoning. Note: `sys_futex` + all `FUTEX_*`
constants exist (`stdlib/sys/syscall.cryo:257`,`:922`) but have **zero call sites** —
futex-based sync is future work (LOW_LEVEL_PLAN Stage 5).

**Q4 — Thread spawn. `HAVE` via `pthread_create`.** `thread::spawn` →
`os_create_thread` (`stdlib/thread/_module.cryo:226`): POSIX `pthread_create` (+ optional
`pthread_attr` stack size), Windows `CreateThread`. **No raw `clone`.** Native spawn
(clone + stack + TLS + trampoline) is the hardest deferred item; acceptable to leave on
pthread.

**Q5 — Test-harness fork. `HAVE` — fork+execv per test.** Two layers: a pthread worker
pool (`TestRunner::run_parallel`, `stdlib/test/runner.cryo:1797`) pulling test indices
off a lock-free atomic cursor, and per-test **`fork()` immediately followed by
`execv()`** of the same binary with `--test-runner-child=<idx>` (`run_one` `:727`; fork
`:779`; child is async-signal-safe only, `dup2`+`execv`+`_exit`). The child never touches
the forking thread's heap → no malloc-lock-at-fork deadlock (`:702`). Windows uses
`CreateProcessA` re-exec (`:930`). **Isolation is a full fresh process image**, chosen
precisely because Cryo has no in-process panic-catch (`test/_module.cryo:38`). This
constrains what "process teardown" means for the runtime: teardown is `_exit(code)` after
one test, OS reclaims everything, parent reaps via `waitpid`.

---

## 1.6 Config and driver · §7 Build and bootstrap · 1.7 Existing code

**1.6-A1 — `cryoconfig`. `HAVE`.** File literally named `cryoconfig` (no extension),
found by upward walk (`ProjectConfig::find_cryoconfig`, `project_config.cryo:929`).
Parser is a **hand-written INI/TOML-subset** (`ProjectConfig::parse`, `:489`) — no real
TOML library. Sections: `project`, `low_level`, `compiler`, `experimental`, `link`(+`.unix`/
`.windows`), `lib`, `[[bin]]`, `dependencies`, `profile`, `test`, `vendor.<Name>`.
Unknown keys warn; removed keys hard-error. Adding `no_runtime` fits the existing
`[compiler]`/`[low_level]` pattern.

**1.6-A2 — `no_std`. `HAVE` (but shallow).** `no_std: boolean` field
(`project_config.cryo:228`, default false), CLI `--no-std` OR-in (`CLI/commands.cryo:1115`),
carried on `ctx.project_no_std` + `CompilerInstance.no_std`. Suppresses: (1) stdlib module
loading (`module_loader.cryo:186`), (2) prelude + auto-imports (`pass_registry.cryo:989`),
(3) `libcryo.a` on the link line (`codegen/passes.cryo:1146`), (4) no-ops `env::set_args`.
**Does NOT touch:** crt0/`_start` emission (nothing emits `_start`), and
`extra_system_libs` (`-lm -lstdc++` / `-lm -lws2_32`) stays **unconditional**; no
`-nostartfiles`/`-nostdlib` ever emitted (`passes.cryo:1157`). **No `no_runtime` flag
exists** (grep-clean) → [P9]. The live migration-switch pattern is `[low_level]
native_alloc`, gated by `![config(native_alloc)]` via `CfgEnv` (`passes/config_gating.cryo:118`)
— the template for wiring `no_runtime` gating.

**1.6-A3 — Multi-package builds. `HAVE`.** In-repo deps via `[dependencies] <name> = {
path = "<rel>" }` (`project_config.cryo:117`). **Path/git deps are compiled as unified
source** — `DepResolver` harvests each dep's source *roots* into one module-loader scan
list (`deps/dep_resolver.cryo:402` `harvest_roots`), then one topological sort over the
module graph (`module_graph.cryo`, driven at `instance.cryo:1369`). **The stdlib is the
one genuinely separate pre-built artifact** (`libcryo.a`, its own `cryoconfig` with
`no_std=true`). **Design consequence:** runtime tiers must be **archive-per-tier like
stdlib**, each with its own `cryoconfig` (`core` = `no_runtime=true`), **not** path-deps —
because source-harvest would flatten distinct per-tier configs into one build. See §C.

**§7-B4 — Bootstrap. `HAVE`.** Committed pins `bin/cryo`(ELF)+`bin/cryo.exe`(PE) are the
seed (`Makefile:2`). Chain: `make stdlib` → `stdlib/.bin/libcryo.a`; `make cryo` (depends
on stdlib) → stage-2 `compiler/build/cryo`; `make pin` (`:274`) rebuilds + writes pins
via `scripts/cryo-pin.py` (Windows host delegates to WSL); `make selfhost-check` (`:371`)
runs the 6-stage byte-identity chain. **No `stage0` target — "stage 0" is the committed
pin.**

**§7-B5 — C toolchain in the build. `HAVE` (link + archive shell out).** Linking
**always** goes through a `cc`/gcc/clang (or mingw) driver via `system(3)`
(`passes.cryo:1070`) — no built-in linker → [P16]. Archiving shells `ar`/`llvm-ar`
(`bundle_object_list` `:1385`, `pick_archiver` `:1619`); **`llvm-ar` alone suffices** for
`.a` production without a C compiler (no `LLVMWriteArchive` API is used). **No assembler**
— inline `asm{}` lowers to LLVM IR; objects are emitted in-process via LLVM. The Makefile
only calls `cc`/`c++` for the two *test helpers* and mingw for the Windows cross-build.

**§7-B6 — stdlib's lower layers. `HAVE`.** stdlib rests on (a) compiler intrinsics that
lower to LLVM IR (memcpy/memset/format/atomics — Cryo-owned, no C object) and (b) libc via
`stdlib/ffi/` + the `stdlib/sys/` seam; managed heap funnels through `GlobalAlloc`. Built
as `target_type="stdlib"` → `libcryo.a`, appended to user link lines unless `no_std`.

**1.7-C7/C8 — Crash infra + `.c`/`.S` inventory. Crash infra `MISSING`; C files
effectively none.** `stdlib/process/signal.cryo` is **signal-number constants only** (no
`sigaction`, no handler install; header `:8` says so). Panic is the in-IR `@panic`
(printf+fflush+libc-`abort`). **No backtrace/symbolization/SIGSEGV handler exists
anywhere** → [P17] (greenfield). **Zero `.c`/`.S`/`.cpp`/`.asm` under `compiler/` or
`stdlib/`**; the only C is dead `legacy/bootstrap/**` and two test helpers
(`tests/helpers/abi_helpers.c`, `cpp_link_helper.cpp`). **The Phase-3 "port to Cryo" task
list is empty by construction.**

---

## 1.8 Machine-level capabilities

**Inline asm — 1 operands `HAVE`, 2 clobbers `HAVE`, 3 volatile `HAVE`, 4 dialect
`HAVE`, 5 symbols/labels `PARTIAL`→[P22], 6 global-asm `HAVE`.** `asm { }` is a genuine
extended-asm feature. Parsed `parser.cryo:3221` (`parse_asm_block`); AST
`AsmOperand`/`AsmConstraintKind{Reg,Pinned,Memory,Imm}`/`AsmOpDir{In,Out,InOut}`
(`AST/statement.cryo:82`); lowered `ir_generator.cryo:287`. Operand forms: `${x}`→`r`
input, `${=x}`→`=r` output, `${+x}`→tied in-out, `${x:"rax"}`→register-pinned `{rax}`,
`${x:m}`→memory `*m` with `elementtype(T)`, `${x:i}`→immediate, multi-output→struct+
`extractvalue`. Clobbers `![clobber(...)]` accept register names + `flags` + `memory`
(`directive_processing.cryo:683`), emitted as `~{name}`. Every block is
`hasSideEffects=true` (`ir_generator.cryo:464`) — never DCE'd/reordered (not
user-toggleable; the safe default). Dialect via `![arch(<arch>, att|intel)]`, template
passed to LLVM as-is (only `$N` operands spliced; literal `$` = `$$`). Module-scope
`asm{}` (operand-free, enforced) → `LLVMAppendModuleInlineAsm` (`ir_generator.cryo:348`)
— a viable path for a text-only freestanding `_start`. **Proven, not just parseable:**
`tests/tests/lang/asm_inline.cryo` (incl. `pinned_multi_register_spread`) and the real
`sys_call0..6` bodies (`stdlib/sys/syscall.cryo`) self-host. Gap (5): no symbol-operand
class (bind a Cryo global/fn as a named operand); no unique-label (`%=`) mechanism.

**Naked functions — `MISSING` → [P11] (APPROVED by Jake, 2026-07-20).** No `![naked]`/no-prologue
path; every function goes through `codegen_function_prologue` (`declaration_emitter.cryo:1629`)
unconditionally. Every "naked" in the compiler refers to *module-level global asm*, not a
naked function. Unlike `![no_mangle]`/`![weak]`/`![section]`, `![naked]` is **not** yet in
the `docs/cryo.md` reserved table — genuinely new surface. Approved as the enabling
capability — but it **only gates the freestanding `_start`** (referencing params) and register-exact setjmp/longjmp, both
**deferred** per HANDOFF §5. A text-only `_start` via module-level global asm is a partial
stand-in.

**Atomics with real orderings — `HAVE`.** `Atomic<T>` (`stdlib/sync/atomic.cryo`) exposes
load/store/fetch_*/swap/compare_exchange with `MemoryOrder{Relaxed,Acquire,Release,
AcqRel,SeqCst}` (`:53`, values mirroring LLVM `AtomicOrdering`). **Genuine per-instruction
orderings, not a seq_cst/memory-clobber hack** — the constant order threads into
`LLVMSetOrdering`/`LLVMBuildAtomicRMW`/`LLVMBuildAtomicCmpXchg` (`llvm_types.cryo:653`);
CAS carries independent success/failure orderings. Satisfies the `thread_panicking` flag
and futex needs. **Fences `HAVE`** (`atomic_fence`→`LLVMBuildFence`; `Relaxed` clamped to
`SeqCst`, correct since a relaxed fence is a no-op).

**`llvm.trap`/`debugtrap` — `MISSING` → [P19].** Not emitted; the only divergence path is
`@panic`→libc `abort`. A `no_runtime` check-failure path that must not touch libc needs
`llvm.trap` (recall §1.2: bare `unreachable` is unsafe on x86).

**Bit intrinsics — `HAVE` at compiler; stdlib surface omits → [P21].** `bswap16/32/64`,
`popcount32/64`, `clz32/64`, `ctz32/64`, `rotl/rotr32/64` fully lower
(`intrinsics_codegen.cryo:78`; → `llvm.bswap`/`ctpop`/`ctlz`/`cttz` with
`is_zero_undef=false`; `llvm.fshl/fshr` for rotates). But the *current*
`stdlib/core/intrinsics.cryo` doesn't declare them (only `legacy/`). Reachable today by
re-declaring `intrinsic function clz64(x: u64) -> u32;`.

**Overflow-checked arithmetic (`llvm.{s,u}{add,sub,mul}.with.overflow`) — `MISSING` →
[P5].** Not in `from_name`, emitted nowhere. **The memory hint "already behind overflow
checks" is wrong** — `checked_add` in stdlib is hand-rolled range comparison;
`const_table.cryo:167` is compile-time only. Needed to implement `panic_overflow` (§1.2).

**`frameaddress`/`returnaddress` — `MISSING` → [P20].** No `IntrinsicKind`; needed for
backtrace. **Prefetch/`expect` — `MISSING`, low priority.**

**Compiler-emitted TLS — `MISSING` → [P10].** (Same finding as §1.5-Q2.)

**Syscall via asm — `HAVE`.** `sys_call0..6` (`stdlib/sys/syscall.cryo`) issue raw
`syscall` pinning `rax`+`rdi/rsi/rdx/r10/r8/r9`, `![clobber(rcx, r11, memory)]` —
ABI-correct, self-hosting (LOW_LEVEL_PLAN Stage 2, DONE). Linux x86-64 only; Windows uses
kernel32/ntdll.

---

## D. Cross-cutting design decisions to settle before Phase 1

These are **decisions**, not capabilities — the HANDOFF calls for several of them and the
audit surfaces more. They should be resolved (with the repo owner where flagged) before
`abi/*.def` is written.

> **Settled by Jake (2026-07-20):** (a) implement and **prefer `![no_mangle]`** over
> `![symbol("…")]` for unmangled lang-item symbols — [P1] APPROVED; (b) **add the `![naked]`
> attribute** — [P11] APPROVED, as the enabling capability for a future freestanding
> `_start` (the `_start` itself stays deferred); (c) **add the `no_runtime` cryoconfig
> option** — [P9] APPROVED, so the runtime *and* stdlib can be built truly free-standing for
> a proper tiered runtime. **Bonus finding:** `![no_mangle]`/`![export]`, `![weak]`,
> `![section]`, and `![constructor]`/`![destructor]` are all **already RESERVED** in
> `docs/cryo.md` — the language design anticipated these runtime hooks; only implementations
> are missing. The items below remain open.

1. **Panic operand ABI (HANDOFF §3.1).** Current `@panic` passes thin NUL-terminated
   `i8*` (message, file) + `u32` line. HANDOFF wants the runtime to *format from raw
   operands* (walk a slice/`str`). **Decision:** either (a) keep C-string operands (zero
   compiler change, but "formats inside the runtime" is trivially true only because
   there's nothing to format), or (b) change lang-item signatures to pass `Str{ptr,len}`
   (16-byte, ABI-split per §1.3) and have codegen build them zero-alloc. The bounds/
   overflow lang items ([P4]/[P5]) *must* pass raw operands (index, len, operands) —
   settle their signature here.

2. **Source-location representation (HANDOFF §3.1 — "before writing any handler").** Must
   be codegen-constructible with **zero allocation and no stdlib type**. Today it's
   effectively `(file: i8*, line: u32)`. Recommend a fixed `{ file: i8*, line: u32, col:
   u32 }` POD passed by value or by pointer-to-static — decide and pin in `layout.def`.
   Escalation per HANDOFF §10 if any proposed form needs a heap allocation.

3. **`__cryo_` symbol prefix, defined once (HANDOFF §3.1).** Confirmed available: the
   `![symbol("__cryo_...")]` path works for extern decls + methods today, and will work
   for free-function *definitions* once [P1] lands. Define the prefix once in
   `lang_items.def`; never spell it literally elsewhere.

4. **Tiering as archive-per-tier, not path-deps (§1.6-A3).** Runtime tiers follow the
   **stdlib archive model** (each tier built into its own `libcryort-<name>.a`), because
   in-repo path-deps are source-harvested into one unified build and would not honor a
   per-tier config. One archive per tier keeps the selectable/overridable boundaries
   (panic strategy, weak allocator) and points link errors at a tier.
   **Implemented as a `[[lib]]` workspace (2026-07-21):** one `runtime/cryoconfig`
   declares each tier as a `[[lib]]` array-of-tables member (`name` + `source_dir`),
   mirroring `[[bin]]`; one `cryo build` emits every `libcryort-<name>.a` into `.bin/`,
   members inheriting the project `[compiler]` config. Distinct from the singular `[lib]`
   (a project that also emits one library). Compiler side: `LibTarget[] libs` in
   `project_config.cryo` + `make_lib_member_view`/workspace loop in
   `compile_project_multi` (`instance.cryo`). Each member flows through the normal
   library-target archive path — no new codegen. The driver still does not auto-order the
   tier archives onto a user link line (outstanding P9-arc work; the workspace now knows
   its members, which is what a fix would consume).

5. **`no_runtime` ⇒ `no_std` matrix (HANDOFF §2).** With both flags real and orthogonal,
   document the four-way matrix. Recommended implication: `no_runtime=true` ⇒
   `no_std=true` (you cannot have the stdlib without the runtime it calls into), stated
   explicitly, plus the dependency diagnostic (a `no_runtime=false` package must not
   depend transitively on a `no_runtime=true` one, and vice-versa is the real constraint).

6. **Allocator relocation (HANDOFF §4 vs existing `stdlib/alloc/heap.cryo`).** The native
   mmap/VirtualAlloc allocator already exists in stdlib (LOW_LEVEL_PLAN Stage 3, DONE).
   Runtime §4 wants a tier-1 `alloc/` with weak `__cryo_alloc`. **Decision:** whether the
   existing `heap.cryo` *moves down* into `runtime/alloc/` (and stdlib's `GlobalAlloc`
   becomes a thin policy layer over it), or the runtime tier wraps it. This interacts with
   [P2] (weak symbol) for user-overridability. Recommend deciding at Phase-4 planning, not
   Phase 1.

7. **Panic strategy: abort now, unwind later — but decide the seam now (HANDOFF §6, §10).**
   The abort path needs none of the unwinding stack ([P12]–[P15]). But the acceptance
   criterion "adding `--panic=unwind` later requires no change to any file in `core/`"
   means `core/` must only *declare* `__cryo_panic` and the check helpers must call it —
   never inline `abort`/`trap` in a way that assumes abort. Keep that discipline from
   Phase 3.

8. **Coroutine model = stackless. LOCKED IN by Jake (2026-07-21).** The concurrency /
   `async`-`await` model is **stackless** (compiler lowers an `async fn` to a poll-driven
   state machine, Rust-style), **not** stackful (no per-coroutine stack, no stack-switch
   trampoline, no green threads). Async itself is **future work — not built now or soon**;
   the call is locked early because reversing it late is expensive (it reshapes codegen,
   the unwinder, and TLS). Consequences that constrain *this* runtime project:
   - **No stackful machinery is designed in.** Threads stay OS threads
     (`pthread_create`/`CreateThread`, §1.5-Q4); there is no `makecontext`/`swapcontext`,
     no `![naked]` stack-switch trampoline, no separate coroutine stacks. `![naked]` [P11]
     stays scoped to `_start`/setjmp, not coroutine switching.
   - **Unwinding [P12]–[P15] stays ordinary native-stack DWARF unwinding.** A panic inside
     an `async fn` unwinds the *native* frame of whoever called `poll()` — there is no
     coroutine stack to walk. So the stackless choice does **not** enlarge the unwinder
     escalation; it keeps it to the standard landing-pad/personality design already
     recorded. Record this in [P12]'s scope so it is not re-litigated.
   - **No impact on the current panic funnel.** `__cryo_panic`'s ABI (§D.1) and the
     abort/unwind seam (§D.7) are unchanged — stackless async will surface panics through
     the same funnel via the poll boundary, whenever it is built.
   - **TLS ([P10]):** stackless async keeps task-local state as ordinary struct fields in
     the state machine, so it does not add a *new* TLS requirement beyond §1.5's; the
     `thread_panicking` flag story is unchanged.
