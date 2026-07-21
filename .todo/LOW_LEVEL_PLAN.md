# Low-Level Plan: Cryo Weaning Off libc

> Status: **proposal / roadmap**. Nothing here is committed work. This document
> lays out a careful, staged path for Cryo to take on more of its own low-level
> responsibilities and depend less on libc over time.
>
> The current-state claims below (§3, and the per-stage "today" notes) were
> verified against the tree on 2026-07-15 and carry `file:line` anchors where
> they point at concrete code. A few places were found to be *more* favorable
> than first written — the `sys_call0..6` hatches and a libc-free `isfinite`
> intrinsic already exist — and are noted inline.

## 1. Purpose & scope

Cryo currently relies on libc for its runtime substrate: the C startup files
(crt0), the memory allocator, the syscall trampoline, math, and program
entry/exit. This plan describes how to **incrementally** move those
responsibilities into Cryo itself.

**This is explicitly NOT a plan to remove libc soon, or maybe ever.** The goal
is to *own the primitives that matter* and to have a *clean, switchable seam* to
the operating system — not to hit a "no libc" milestone. Every stage below is
independently valuable and shippable; the roadmap can stop at any stage and
still leave the tree better than it found it.

**Non-goals (intentional, may change later):**

- Reimplementing `libm` transcendentals (`sin`/`cos`/`pow`/`log`). Correctly
  rounded implementations are a research problem with near-zero payoff. Keep
  linking `-lm`. (Cheap bit-twiddling ops — `isnan`, `fabs`, `floor` — are fair
  game; see Stage 1.)
- Making the **compiler binary** freestanding. `bin/cryo` links `libLLVM` (C++),
  so it will always pull in a C/C++ runtime. Only *user programs* can plausibly
  go freestanding. Keep this distinction sharp.
- Dropping `-lc` before the very last, opt-in stage. Surface reduction and
  primitive ownership come first; unlinking libc is a separate, deliberate act.

## 2. Guiding principles (the "be very careful" part)

These are hard rules, not aspirations. They are what make an ambitious,
long-horizon change safe to pursue incrementally.

1. **The self-host is the canary. Never regress it.** Every stage must end with
   a green Linux **and** Windows 6-stage byte-identity self-host
   (`scripts/selfhost-check.py`) plus the full test suite (unit + compile-fail +
   projects). The compiler links this stdlib and exercises the allocator, I/O,
   and threading harder than any test — a broken primitive shows up as a
   self-host failure immediately.
2. **Switches are migration aids, not product features.** *(Revised 2026-07-16
   — "burn the boats, keep the tests".)* A high-risk primitive (the allocator)
   may land behind a temporary `[low_level]` config switch so a regression is
   bisectable to a flag flip while it bakes — but the switch and the libc arm
   are **deleted at the stage boundary once the native implementation is
   proven** (differential tests + dual-OS self-host fixed point). At most one
   migration switch is live at a time. Low-risk mechanical stages (4, 6) use
   no switch at all: direct rewires, verified by the permanent test
   discipline. Per-OS `![target(...)]` splits are not switches — they encode
   genuine platform differences and are permanent. The caution that stays
   forever is principle #4's verification, not runtime escape hatches; git
   history is the rollback lever after a switch is gone. (`native_syscalls`
   completed this lifecycle in Stage 2: landed switched, proven, switch
   deleted — raw syscalls are simply THE Linux implementation now.)
3. **One OS, one arch first.** **x86-64 Linux is the beachhead.** Windows is a
   *different problem* (see §4) and stays on Win32/NT — which is already not
   glibc. aarch64 is deferred until the compiler emits aarch64 codegen at all
   (today it only initializes X86 targets; see `tests/tests/lang/asm_inline.cryo`).
4. **Behavior parity, not just "it compiles."** A native primitive must match
   the libc one on edge cases (errno values, `EINTR` retries, alignment,
   zero-size, overlap). Where feasible, add a differential test that runs the
   same operation through both backends and asserts identical results.
5. **Keep crt0 until the end.** Freestanding (own `_start`, drop `-lc`) is the
   final, opt-in stage. Everything before it keeps libc linked.
6. **Each stage stands alone.** Never leave the tree "half-migrated and broken."
   A stage either lands complete-and-green or doesn't land.

## 3. Where we're starting from (the good news)

Exploration of the current tree shows Cryo is already unusually well-positioned
for this. The seams exist; most of the work is *flipping their backend*, not
building from scratch.

- **The runtime is already inline-lowered.** There is no Cryo-specific C runtime
  object to replace — intrinsics like `format`, `memcpy`-class ops, atomics, and
  `bswap`/`clz` are emitted directly as LLVM IR
  (`compiler/src/compiler/codegen/passes.cryo`). Only libc's crt0 and the
  `-lm -lstdc++ -lc` link line stand between today's output and a freestanding
  link.
- **Allocation has a single chokepoint.** `GlobalAlloc`
  (`stdlib/alloc/allocator.cryo`) is a stateless singleton implementing a clean
  3-method `Allocator` trait (`allocate`/`deallocate`/`reallocate` over
  `Layout`). Every `Box`/`Rc`/`Arc`/`Array`/`String`/`HashMap` is generic over
  `A: Allocator = GlobalAlloc` and routes through it; the compiler lowers
  `new`/`delete`/array-literals/`format` to two bridge functions
  (`std::alloc::allocator::alloc`/`free`) that also hit `GlobalAlloc`. Swap its
  three methods + the two bridges and the *entire* managed heap moves.
- **An mmap allocator backend already exists.** `stdlib/alloc/arena.cryo` gets
  its chunks straight from `mmap`/`MAP_ANONYMOUS` (POSIX) and `VirtualAlloc`
  (Windows) — code to lift from for a native `GlobalAlloc`. Note the POSIX path
  currently calls `libc::mmap` (`arena.cryo:98`), not the raw `sys_mmap` wrapper;
  Stage 3's native backend swaps that one call to `sys::mmap`, the plumbing
  around it is reusable as-is.
- **Networking already funnels through one file.** `stdlib/net/sys.cryo` is the
  single socket chokepoint; all of `net/{tcp,udp,http,http2,ws,...}` is pure
  Cryo above it.
- **The syscall layer exists — it's just wired to libc.** `stdlib/ffi/syscall.cryo`
  has the full x86-64 Linux syscall-number table and **118 typed `sys_*`
  wrappers** (`sys_read`/`sys_write`/`sys_mmap`/`sys_exit_group`/...). They
  currently forward through libc's `extern "C" syscall(...)` variadic trampoline
  and rely on glibc for errno translation. **Zero `asm {}` blocks today.** The
  generic `sys_call0..sys_call6` escape hatches (`syscall.cryo:1127`+) **already
  exist**, also forwarding through the libc trampoline — so Stage 2 *replaces
  their bodies*, it does not introduce them.
- **Inline asm can already express a raw syscall.** `asm { ... }` with `${}`
  operand holes, register pinning (`${x:"rax"}`), `![arch(x86_64, att)]`, and
  `![clobber(rcx, r11, memory)]` is fully implemented and lowered with
  `side_effects=true` (never DCE'd). `docs/cryo.md` §6.13 literally shows the
  `write` syscall as its worked example. The capability is real; the stdlib just
  doesn't use it yet.
- **A `no_std`/freestanding scaffold is partly built.** The `--no-std` flag and
  `no_std` config key already skip stdlib loading (`instance.cryo`,
  `module_loader.cryo`), and the generated `main` prologue no-ops `env::set_args`
  when the stdlib is absent — deliberately, "to keep freestanding single files
  compiling." `sys_exit`/`sys_exit_group` already exist.
- **Most of the tree is already pure Cryo.** All of `core/`, `collections/`,
  `json/`, `encoding/`, most of `fmt/`, the `net/` upper layers, and most of
  `random/` depend only on compiler `intrinsics::` (`memcpy`/`memset`/...) — not
  libc. Those intrinsics are LLVM codegen, i.e. *already Cryo-owned*; leave them
  as intrinsics (a hand-written Cryo `memcpy` loop would be strictly worse than
  `llvm.memcpy`).

**The current libc coupling, honestly sized:**

| Area                            | Where                                                                                                                 | Note                                          |
| ------------------------------- | --------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- |
| **crt0 / entry**                | no `_start` emitted; `main` widened to `(argc,argv)`, relies on `__libc_start_main`                                   | the real wall                                 |
| **allocator**                   | `GlobalAlloc` → `libc::aligned_alloc`/`realloc`/`free`                                                                | one chokepoint                                |
| **fd I/O + fs + process + env** | `io/`, `fs/`, `process/`, `env/` → `libc::read/open/stat/fork/...`                                                    | thin wrappers                                 |
| **threading / sync / time**     | `sync/`, `thread/`, `time/clock.cryo` → **bare-name** `pthread_*`, `clock_gettime`, `nanosleep` (~75 pthread symbols) | *hidden* — a naive `libc::` grep misses these |
| **networking**                  | `net/sys.cryo` → BSD sockets                                                                                          | one chokepoint                                |
| **math**                        | `math/`, `fmt/float.cryo` → `libm` (~37 fns)                                                                          | pure computation, no OS                       |
| **stdio**                       | `test/runner.cryo` (85 of 88 `printf`), `fmt/_module.cryo`                                                            | mostly test infra                             |

The compiler itself barely touches libc directly: it allocates via
`intrinsics::malloc`/`free` (107/111 sites) and has one `fdopen`+`fprintf`
diagnostic path plus a few `libc::read`.

## 4. The architecture: a `sys` seam

The unifying idea is a single, thin, portable **`sys` layer** — the *only* place
the stdlib touches the operating system. Everything above it is pure Cryo;
everything below it is a swappable backend.

```
   pure Cryo:   String · collections · fmt · Box/Rc/Arc · io buffering · net upper
   ───────────────────────────────  sys  ───────────────────────────────  ← the OS line
   backends:    libc (today)  │  native asm-syscalls (Linux)  │  kernel32/ntdll (Windows)
```

`sys` exposes a small, portable API — `read`, `write`, `open`, `close`, `mmap`,
`munmap`, `mprotect`, `exit`, `futex_wait`/`futex_wake`, `clock_mono`,
`sleep`, `thread_spawn` — and each function picks its backend by build config.
This is the switch that Principle #2 requires.

**Realization:** this does not have to be a big new tree. `ffi/syscall.cryo`
already holds the Linux number table + 118 wrappers and the entire Windows
surface; `sys` can be the *portable* wrapper set that consumes them, with a
config-selected backend for the Linux side (libc trampoline vs. native asm). Keep
`ffi/{libc,syscall}` as the raw bindings; introduce `sys` as the seam the rest of
the stdlib is migrated to consume.

**Why Linux and Windows are fundamentally different — and that's fine:**

- **Linux:** the *kernel syscall interface is the stable ABI*. glibc is just a
  wrapper over it. So "not libc" on Linux means **raw syscalls via inline asm** —
  legitimate, stable, and already expressible.
- **Windows:** syscall numbers are **not stable** across builds. Microsoft's
  stable ABI is the DLLs — `kernel32`/`ntdll`. So "not libc" on Windows means
  **calling kernel32/ntdll directly**, which is exactly what msvcrt does
  internally. Cryo already has that surface (`VirtualAlloc`, `CreateFile`,
  `ReadFile`, `ExitProcess`, `CreateThread`, SRW locks, ...). On Windows, most of
  the weaning is *already done* — the work is preferring the Win32/NT path over
  msvcrt, not writing syscalls.

So `sys` has a raw-asm backend on Linux and a kernel32/ntdll backend on Windows.
The errno convention differs and is normalized *at the seam*: raw Linux syscalls
return `-errno` in the result register (no `errno` TLS); Windows uses
`GetLastError`/NTSTATUS. `sys` presents one consistent error model upward.

## 5. Stages

Ordered safest→hardest and by dependency. Each stage lists its **goal**,
**approach**, **exit criteria**, and **risk**.

### Stage at a glance

| #   | Stage                                                 | Blast radius | Risk     | Drops libc?      |
| --- | ----------------------------------------------------- | ------------ | -------- | ---------------- |
| 0   | Guardrails & the `sys` seam skeleton                  | none (infra) | low      | no               |
| 1   | Pure-computation wins (fp classification, cheap math) | tiny         | low      | no (keeps `-lm`) |
| 2   | Native Linux syscall backend (flip the trampoline)    | opt-in       | medium   | no               |
| 3   | Native mmap allocator (`GlobalAlloc`)                 | whole heap   | **high** | no               |
| 4   | Route fd I/O + fs + process + env through `sys`       | broad        | medium   | no               |
| 5   | Threading / sync / time on futex + clock syscalls     | broad        | **high** | no               |
| 6   | Native stdio / printf (formatting already owned)      | medium       | medium   | no               |
| 7   | Freestanding: own `_start`, drop `-lc` (opt-in)       | link line    | **high** | **yes**          |

### Stage 0 — Guardrails & the `sys` seam skeleton

**Goal:** infrastructure only, zero behavior change.

- Introduce the `sys` portable API surface, initially a *pure pass-through to the
  existing libc/`ffi::syscall` implementations*. Nothing changes at runtime.
- Add the backend-selection switch (e.g. a `[low_level]` config section or a
  `![config(native_syscalls)]`-style gate) so later stages can dual-back a
  primitive. Default: everything libc-backed.
- Decide and **document** the errno model `sys` presents upward (recommend: `sys`
  functions return a tagged `Result`/`i64` where negative = `-errno`, normalized
  regardless of backend).
- Establish the per-stage verification ritual: selfhost (both OS) + full tests,
  and stand up a **differential test harness** (run an op through libc vs native,
  assert equal) for use from Stage 2 on.

**Exit:** `sys` compiles, self-hosts both OS, all tests green, no observable
change. The switch exists and defaults to libc.

**Risk:** low. This is scaffolding.

> **Status: DONE** (committed `d9f8ee35`). `sys` seam + `native_syscalls` switch
> landed; both pins refreshed.

### Stage 1 — Pure-computation wins

**Goal:** remove libc surface that has *no OS dependency at all* — the safest
possible starting point.

- Reimplement floating-point classification and cheap ops in Cryo bit-twiddling:
  `isnan`, `isinf`, `isfinite`, `signbit`, `fabs`, `floor`, `ceil`, `trunc`,
  `round`, `copysign`. These are pure integer/bit operations on the IEEE-754
  representation — no libm, no syscalls. The classification coupling is tightly
  contained: **exactly two files** call the glibc/MSVC family — `math/_module.cryo`
  (`__isnan`/`__isinf`/`__finite` on Linux, `_isnan`/`_finite` on Windows) and
  `fmt/float.cryo` (which keeps file-local `fp_is_nan`/`fp_is_inf` copies to avoid
  depending on `std::math`). `__signbit`/`__fpclassify` are *declared but never
  called* — they can simply be dropped.
- **Some of this is already built.** The compiler emits a libc-free `isfinite`
  intrinsic (`intrinsic_emitter.cryo:472`, lowered as `fcmp oeq (fsub x, x), 0.0`)
  that the stdlib does **not** yet consume — wiring `std::math::is_finite` to it
  is most of the `isfinite` work. The tree already uses `mem::transmute<f64,u64>`
  for float bit access (`core/cmp.cryo`, `hash.cryo`), so `isnan` (`x != x` /
  `fcmp uno`), `isinf`, and `signbit` are one transmute-and-mask each.
- Where a hardware instruction exists, prefer an LLVM intrinsic over libm:
  `sqrt` → `llvm.sqrt.f64`, `fma` → `llvm.fma`, `fabs` → `llvm.fabs`. These are
  Cryo-owned (codegen) and often faster. **None are emitted today** — the
  compiler currently emits only integer/bit intrinsics (`bswap`/`ctpop`/`ctlz`/
  `fshl`/...), so this half of Stage 1 is genuinely new codegen, not a rewire.
- **Keep the transcendentals** (`sin`/`cos`/`pow`/`log`/`exp`) on `-lm`.

**Exit:** fp classification is Cryo-native; `math`/`fmt/float` no longer depend on
the libc `isnan`/`finite` family; selfhost + tests green. `-lm` still linked
(for transcendentals) but the surface is smaller.

**Risk:** low. Pure functions, easily differential-tested against libm.

> **Status: classification DONE** (committed `dc8ad4c5`): `fpclass` compiler
> intrinsic → `llvm.is.fpclass.f64` (replaced the exception-raising `isfinite`
> `(x-x)==0` trick); `is_nan`/`is_infinite`/`is_finite`/`is_normal`/
> `is_sign_negative`/`classify() -> FpCategory` in pure Cryo; libc
> classification externs deleted from `ffi::libc`; edge-case tests added.
> The `nested_match_neg` failure this exposed was a PRE-EXISTING compiler bug
> (same-leaf expected-type hint poisoning generic-enum ctor args, Windows-only
> by module-registration order), root-caused and fixed 2026-07-16 in
> `sema/call_resolver.cryo` (locals-shadow + qualified-before-bare) plus an
> E0600 codegen backstop in `enum_variant_emitter.cryo` so a dropped payload
> store can never again be silent.
> **Status: cheap-math half DONE — Stage 1 complete.** Ten new compiler
> intrinsics — `fabs64`/`fabs32`/`floor64`/`ceil64`/`ftrunc64`/`round64`/
> `copysign64`/`sqrt64`/`sqrt32`/`fma64` — lower to the matching LLVM float
> intrinsics (`llvm.fabs.f64`, ...); names carry a width suffix so common
> leaf names (`round`, `sqrt`) are not reserved by the leaf-name intrinsic
> dispatch. `std::math` (`sqrt`/`sqrt_f32`/`mul_add`/`floor`/`ceil`/`round`/
> `trunc`/`fract`/`fabs`/`fabs_f32`/`copysign`) and `fmt/float.cryo` are off
> libm for these; the 11 corresponding externs (`sqrt(f)`, `floor`, `ceil`,
> `round`, `trunc`, `fabs(f)`, `copysign(f)`, `fma`) are deleted from
> `ffi::libc`. Edge-case tests added (round half-away-from-zero both signs,
> sign-bit ops on -0.0/NaN, sqrt domain, an exact 2^-104 fma-fusion witness).
> Note on codegen: on baseline x86-64, `fabs`/`copysign`/`sqrt` legalize to
> instructions, while `floor`/`ceil`/`trunc`/`round`/`fma` legalize to libm
> LIBCALLS (no SSE4.1/FMA3 at the default target) — those references are now
> backend-owned legalization, not stdlib surface, and `-lm` stays linked as
> planned. A later freestanding stage must provide them (compiler-rt or a
> target-feature bump). Transcendentals remain on libm by design.

### Stage 2 — Native Linux syscall backend (the keystone)

**Goal:** make the syscall path genuinely libc-free, behind the switch.

- Give the *existing* `sys_call0..sys_call6` (they already exist as libc-trampoline
  forwarders — see §3) a per-arch inline-asm body (x86-64 first), selected by the
  Stage-0 switch. Swapping their bodies is enough: the 118 `sys_*` wrappers call
  through them, so they reach the kernel directly with no wrapper churn.
- Normalize errno at the seam (raw returns `-errno`).
- Gate behind the Stage-0 switch; the libc trampoline stays as the default
  backend until this is proven.
- **Validate rigorously:** build a small program doing `read`/`write`/`open`/
  `mmap`/`exit` purely via native syscalls, link it `--no-std`, and confirm with
  `nm`/`ldd` that **no libc symbols** are referenced. Differential-test each
  wrapper against its libc equivalent.
- **Add a register-pinning test first.** The `${x:"rax"}` pin syntax is documented
  (`docs/cryo.md` §6.13) and the codegen path exists (`ir_generator.cryo:363`, LLVM
  `{reg}` constraints), but no test exercises it — the syscall ABI pins args to
  specific registers, so land a focused test of the pin form before relying on it.

**Exit:** `sys` can do file/memory/exit syscalls on x86-64 Linux with zero libc
involvement (proven by symbol inspection), switchable, differential-tested;
selfhost + tests green with the switch in **both** positions.

**Risk:** medium. The capability is proven (docs show the `write` syscall), but
the ABI details (clobbers, 6th-arg register, `EINTR`) demand care. Low blast
radius because it's opt-in and nothing consumes it yet.

> **Status: DONE.** `syscall::sys_call0..6` are now dual-backed: the default
> libc-trampoline body (now normalizing the trampoline's `-1`+errno to the
> seam's raw `-errno` contract) and, under `[low_level] native_syscalls`, an
> inline-asm body issuing the x86-64 `syscall` instruction directly (number in
> `rax`, args in `rdi/rsi/rdx/r10/r8/r9`, `rcx`/`r11`/`memory` clobbered).
> The `std::sys` seam (`read`/`write`/`open`/`close`/`exit`) gained matching
> native arms; `exit` uses `exit_group`. Register binding uses the pinned
> operand form `${x:"reg"}` — first covered by two new `asm_inline` tests
> (single- and multi-register spread) before being relied on. `native_syscalls`
> is clamped off for non-Linux targets in `config_gating.cryo` (`CfgEnv::from_ctx`)
> so a flagged Windows build degrades to libc instead of stripping both arms.
> Verified: the `native_syscalls_gate` project is now the ON-backend
> differential suite (native ops cross-checked against libc, `-errno` contract
> on deliberate failures, mmap/getpid round-trips, exit via native `exit_group`
> → exit code 3); a new `sys_seam` stdlib test asserts the identical contract on
> the OFF backend. Object inspection confirms each `sys_callN` emits a raw
> `syscall` (`0f 05`) with the correct ABI registers and no trampoline `call`.
> Both switch positions build and self-host (dual-OS 6-stage fixed point).
> NOTE: `sys::exit` (native `exit_group`) is a raw exit — it runs no libc
> atexit handlers and flushes nothing; callers must flush buffered stdio first
> (the gate project does).
>
> **2026-07-16 follow-up ("burn the boats"):** with the backend proven, the
> `native_syscalls` migration switch completed its lifecycle and was DELETED —
> `sys_call0..6` keep only the inline-asm bodies (`![target(linux)]`), the
> seam functions became per-OS splits (`![target(linux)]` raw /
> `![not(linux)]` libc), `trampoline_result` and the compiler-side atom /
> clamp / `[low_level] native_syscalls` key are gone. The differential gate
> project and `sys_seam` test remain — they verify results, not switch
> positions. See revised principle #2.

### Stage 3 — Native mmap allocator

**Goal:** move the entire managed heap off libc `malloc` — the single
highest-value owned primitive.

- Reimplement `GlobalAlloc`'s three methods + the two `alloc`/`free` bridge
  functions over `sys::mmap`/`munmap` (Linux) and `VirtualAlloc`/`VirtualFree`
  (Windows). Lift the mmap plumbing from `arena.cryo`.
- Design: start simple and correct (segregated free lists / size classes over
  mmap'd regions; a large-allocation path that mmaps directly). Evolve toward a
  real design (simplified mimalloc/dlmalloc) only after correctness is locked.
- Handle the hard parts explicitly: alignment (the trait passes `Layout`),
  `reallocate` (grow-in-place vs. move), zero-size, thread-safety (the allocator
  is process-global and hit concurrently once Stage 5 lands).
- Also patch the one stdlib bypass: `json/parser.cryo` uses `intrinsics::malloc`
  directly for a scratch buffer — route it through the allocator.
- Dual-back it. **The compiler self-host is the stress test** — it allocates
  heavily across a multi-stage build; if the allocator is wrong, the self-host
  won't reach a fixed point.

**Exit:** `GlobalAlloc` (and therefore `Box`/`Rc`/`Arc`/`Array`/`String`/
`HashMap`/`new`/`delete`/array-literals/`format`) runs on the native allocator
under the switch; selfhost byte-identical both OS with the native allocator on;
valgrind-clean; differential/stress tested.

**Risk:** **high.** Allocator bugs are memory-corruption bugs. This is where
Principle #2's temporary switch earns its keep: land behind `[low_level]
native_alloc`, bake against the libc arm, then delete the switch and the libc
arm once proven (per the revised principle, not "opt-in indefinitely").

> **Status: DONE (baking).** `stdlib/alloc/heap.cryo`: a segment
> allocator in the mimalloc family. Every mapping is 4 MiB-aligned, so
> `ptr & ~(4MiB-1)` recovers the owning segment header — required because the
> codegen `free` bridge cannot supply a size. Small path (≤ 32 KiB, align
> ≤ 16): 40 size classes (16-byte steps to 128, then quarter-steps per
> doubling), one class per segment, intrusive free list + bump frontier,
> empty segments unmapped (deterministic RSS return). Large/over-aligned
> path: dedicated aligned mapping, payload at `max(page, align)`. Page
> provider: `sys::mmap` (unix) / `VirtualAlloc` reserve-release-re-reserve
> dance (windows). One global spinlock (`atomic_cmpxchg_u32`) — correctness
> before design. Foreign pointers abort loudly on a magic check. `GlobalAlloc`
> keeps policy (zero-size, arena routing) and delegates to three config-gated
> `backend_*` fns; the libc arms and the switch are deleted at end of bake-in.
> Verified: 7 direct unit tests (flag-independent); `native_alloc_gate`
> project (Array/String/format/new+delete/HashMap churn, flag ON, exit 0;
> object inspection shows `backend_alloc→heap::alloc` and zero `aligned_alloc`
> refs); valgrind 0 errors on the gate (libc heap reduced to 2 stdio allocs)
> AND on the flag-ON compiler compiling a file; **flag-ON dual-OS 6-stage
> selfhost FIXED POINT OK** (the wine side exercises the VirtualAlloc dance).
> The `json/parser.cryo` `intrinsics::malloc` bypass named below no longer
> exists (removed in an earlier cleanup). Known deliberate v1 crudeness:
> single lock, no per-segment cache at the empty boundary, realloc never
> shrinks in place across classes. DISCOVERED (unrelated, default backend):
> `Option<T> != T` silently miscompiles — the scalar side is never
> Some-wrapped and `equals` traps on a garbage tag; minimal repro exists;
> needs its own fix session.

### Stage 4 — Route fd I/O, fs, process, env through `sys`

**Goal:** switch the thin OS wrappers from `libc::` to `sys::` (native on Linux).

- Migrate `io/fd`, `io/stdio`, `fs/*`, `process/*`, `env` to call `sys::read/
  write/open/stat/...` instead of `libc::`. These are largely mechanical
  translations; the chokepoints are already abstracted.
- Preserve errno semantics and `EINTR` retry behavior exactly.
- Networking (`net/sys.cryo`) can follow the same pattern, or stay on libc
  sockets longer — sockets are a big surface with subtle semantics; low priority.
- **No migration switch** (revised principle #2): these are direct rewires onto
  the proven `sys` seam, verified by the test suite and self-host — not
  dual-backed.

**Exit:** the common I/O/fs/process/env paths run through `sys` (native on
Linux); selfhost + tests green both OS.

**Risk:** medium. Broad but mechanical. The danger is subtle behavior drift
(errno, partial reads, retry-on-`EINTR`) — differential tests guard this.

### Stage 5 — Threading, sync, time (the hidden liability)

**Goal:** replace the bare-name `pthread_*` / `clock_gettime` / `nanosleep`
surface (~75 pthread symbols — the part a `libc::` grep *misses*) with native
primitives.

- **Sync** (`Mutex`/`RwLock`/`Condvar`/`Barrier`/`Once`) on **futex**
  (`sys_futex`) directly. Windows already uses SRW locks / condition variables
  (not glibc) — leave it.
- **Time** (`time/clock.cryo`) on `clock_gettime`/`nanosleep`/`clock_nanosleep`
  syscalls. Windows already uses `QueryPerformanceCounter`/`Sleep`.
- **Thread creation** (`pthread_create`) is the hard one: `clone`/`clone3` +
  stack allocation + TLS setup + a thread trampoline. This may keep `pthread`
  the *longest* — it's acceptable to leave thread spawn on pthread even after
  everything else is native.

**Exit:** locks/condvars/time run on native futex+clock syscalls under the
switch; selfhost + tests green. Thread *spawn* may remain on pthread by design.

**Risk:** **high.** Memory ordering, futex wait/wake races, and thread startup
are the subtlest correctness territory in the whole plan. Late stage, slow,
heavily tested.

### Stage 6 — Native stdio / printf

**Goal:** remove the last big stdio dependency.

- The *formatting* is already Cryo-owned (the `format` intrinsic + `fmt`). The
  residual libc dependency is `printf`/`vfprintf`/`fprintf` and FILE* buffering.
  Replace with Cryo buffered writers over `sys::write`.
- `test/runner.cryo` accounts for 85 of 88 `printf` calls — migrating it (plus
  `fmt/_module.cryo`'s `vprintf`/`vfprintf`) clears most of the stdio surface.

**Exit:** stdout/stderr paths run through `sys::write` + Cryo buffering; selfhost
+ tests green.

**Risk:** medium. Buffering/flush-on-exit and interleaving semantics need care.

### Stage 7 — Freestanding (opt-in, explicit)

**Goal:** for a program that wants it, link **without libc**. This is the final
wall and only worth it if freestanding is genuinely desired.

- Emit a Cryo/asm `_start` that reads `argc`/`argv`/`envp` off the initial
  stack, calls the widened `main`, and `exit_group`s the return value. Today
  nothing emits `_start`; the widened-`main`+`set_args` machinery already exists
  to build on (`declaration_emitter.cryo`).
- Add a **freestanding `LinkerConfig` profile**: `-nostdlib -nostartfiles
  -static`, empty `extra_system_libs`. Today `no_std` skips the stdlib archive
  (`passes.cryo:1146`) but does *not* touch entry emission or the crt/system-lib
  line: `extra_system_libs` (`-lm -lstdc++` on host, `-lm -lws2_32` on Windows) is
  still appended unconditionally, and no `-nostartfiles`/`-nostdlib` flag is emitted
  anywhere in `compiler/src/` today — that is the concrete coupling this stage breaks.
- Route `process_exit` and any remaining runtime through `sys`.
- **Scope:** *user programs only.* The compiler binary links `libLLVM` (C++) and
  cannot be freestanding — do not attempt it.

**Exit:** a non-trivial Cryo program builds and runs with `-nostdlib
-nostartfiles` and no `-lc`/`-lm`, verified by `ldd`/`nm`; the libc-linked path
remains the default.

**Risk:** **high**, but well-contained — it's opt-in and touches only entry
emission + the link line, both of which are localized.

## 6. Cross-cutting concerns

- **Per-arch.** x86-64 first, everywhere. aarch64 needs its own syscall numbers
  *and* the compiler to emit aarch64 codegen (it only initializes X86 today —
  `init_native_target` at `llvm_types.cryo:1117` calls only `LLVMInitializeX86*`,
  the sole target-init site in `compiler/src/`). The `Syscall` table is
  x86-64-only and ungated; a future `![arch]`-style gate on the number block is a
  prerequisite for a second arch. Don't pay this cost until a second arch is a
  real target.
- **Windows is mostly already done.** kernel32/ntdll *is* the Windows platform
  ABI; the broad surface already exists in `ffi/syscall.cryo`. Windows weaning ≈
  preferring Win32/NT over msvcrt, which is largely the current state. Windows
  gets no raw syscalls.
- **errno.** Normalized once, at the `sys` seam (Stage 0 decision). Above `sys`,
  code sees one error model regardless of backend.
- **`intrinsics::mem*` stay intrinsics.** `memcpy`/`memset`/`memmove`/`memcmp`
  are LLVM codegen, already Cryo-owned and better than any hand-written loop.
  This plan does not touch them.
- **Compiler allocator.** The compiler's own `intrinsics::malloc`/`free` (107/111
  sites) are separate from the runtime allocator. When Stage 3 lands, decide
  whether to route those through the native allocator too (free stress-testing)
  or leave them on libc.
- **Testing strategy.** Differential tests (native vs libc, same op, assert
  equal) from Stage 2 on; the self-host is the integration test; valgrind for the
  allocator; symbol inspection (`nm`/`ldd`) to *prove* a path is libc-free rather
  than assume it.

## 7. Honest risks & caveats

- **The correctness burden shifts to us.** libc is decades-hardened. Our
  allocator owns alignment/fragmentation/thread-safety; our syscalls own `EINTR`
  and the ABI clobber lists; our futex code owns memory ordering. The self-host +
  tests + valgrind are the ratchet, but the burden is real.
- **Debugging gets harder without libc.** No libc-aware backtraces, gdb knows
  less, sanitizers assume libc. Budget for worse ergonomics on the native paths.
- **Thread startup, TLS, and signals are genuinely hard.** These are the parts
  most likely to stay on libc/pthread the longest, and that's an acceptable
  outcome.
- **This is a long horizon.** The value is in *owning primitives and having a
  clean OS seam*, realized stage by stage — not in reaching a "no libc" finish
  line. It is fine, and by design, to stop after any stage.

## 8. Recommended first moves

1. **Stage 0** (seam + switch + differential harness) — pure infrastructure,
   unblocks everything, risks nothing.
2. **Stage 1** (fp classification in Cryo) — a real, safe surface reduction to
   validate the workflow end to end.
3. **Stage 2** (native Linux syscall backend) — the keystone capability, proven
   in isolation before anything depends on it.

Stage 3 (the mmap allocator) is the first *high-value, high-risk* move and the
natural first "real" consumer of the `sys` seam — but it should not start until
0–2 are solid and the differential/self-host guardrails are in place.

## 9. Open decisions (to make before starting)

- Does `sys` live as a new `stdlib/sys/` tree, or as an evolution of
  `ffi/syscall.cryo`? (Recommend: `sys` = portable seam consuming the existing
  `ffi` raw bindings.)
- Backend selection mechanism: build-config section vs. compile-time
  `![config(...)]` gate vs. runtime dispatch. (Recommend: compile-time, so the
  unused backend is stripped and there's no runtime cost.)
- How long does the native allocator stay opt-in after it works? (Recommend: a
  long bake-in; default stays libc until it's earned trust across many
  self-hosts.)
- Is freestanding (Stage 7) actually a goal, or is "own the primitives, keep libc
  linked" the real destination? This decides whether Stages 5–7 are worth their
  risk.
