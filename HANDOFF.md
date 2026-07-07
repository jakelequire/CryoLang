# HANDOFF — Multi-threaded codegen: audit & further optimization

**Your mission (fresh agent):** audit multi-threading in the Cryo compiler's
codegen and find what can still be optimized or improved. The core feature —
**true in-process multi-threaded object emission** — is DONE, validated, and the
default. This document tells you exactly what shipped, why, and where the
remaining levers are so you don't re-derive a long investigation.

Read this whole file first. The maintainer (Jake) wants **correct, principled**
solutions, never "just make it work" hacks. Only Jake commits; you may repin.

---

## 0. TL;DR

| Item | State |
|---|---|
| In-process **multi-threaded** object emit | **DONE**, ZERO-COPY (live modules, no bitcode round-trip, no arena pause) |
| Correctness | byte-identical serial-vs-threaded on **Windows + Linux**; `make selfhost-check` FIXED POINT; `make test` PASS; context auditor 0-foreign |
| Config | `--codegen-threads=N` flag + `[compiler] codegen_threads` cryoconfig (default = CPU count; `1` = serial). NOT env-driven. |
| Wall-clock win | compiler build 40s → 32s (~19%); stdlib ~26% |
| Committed? | **NO** — uncommitted working tree (7 code files). Repin pending (from a clean tree, after Jake commits). |
| Everything below §3 | **the audit targets — your job** |

Design doc with the full journey+log: `.todo/plans/PARALLEL-CODEGEN.md`
(read the top "UPDATE (c)" and "(b)" blocks). Memory:
`memory/parallel-codegen-project.md`.

---

## 1. Orientation (5-minute version)

- **Self-hosted compiler** written in Cryo, targets LLVM 20 via the **LLVM-C
  API**. Compiler source: `compiler/src/compiler/` (~123 `.cryo`). Stdlib:
  `stdlib/`. Build = bootstrap: pinned `bin/cryo`(+`.exe`) compiles stdlib +
  compiler.
- 9-stage pipeline per module: Frontend → ModuleResolution → DeclCollection →
  TypeResolution → SemanticAnalysis → Specialization(mono) → CodegenPrep →
  IRGeneration → (ObjectEmission). Object emission is the parallelized phase.
- **Windows** needs `CRYO_CC=gcc`; build from PowerShell. **Linux/WSL** is where
  self-host + `make test` run. The agent "Bash" tool here is Git Bash (no
  `/mnt/c`); reach WSL via `wsl.exe -e bash -lc '...'` with `MSYS_NO_PATHCONV=1`.

---

## 2. What shipped — the multi-threaded emitter (how it works now)

**Model:** the per-module pipeline runs IR-generation SERIALLY on the main
thread (it reads shared compiler state — arena, intern table, decl index,
lazily-declared cross-module externs). Only the **backend object emit**
(`LLVMTargetMachineEmitToFile`: SelectionDAG instruction selection, register
allocation, scheduling at O2) runs in PARALLEL, one module per worker.

**Zero-copy, allocation-free workers:**
- The producer (main thread) builds each module's IR in its **own
  `LLVMContext`** (`set_codegen_context_new_keep_prior`), then builds that
  module's **target machine on the main thread** and pushes an `EmitJob`
  carrying `{ live LLVMModuleRef, its LLVMContextRef, its TargetMachineRef,
  emit_path, … }`. The module stays alive (owned by the job).
- Jobs accumulate to a batch of `cg_batch_cap` (= `cg_threads`) and are emitted
  concurrently via `thread::Scope::new_with_stack(64 MiB)` (SelectionDAG recurses
  deep — the default ~1 MiB thread stack overflows). Workers take **disjoint
  round-robin slices**; the only write is `job.ok`, so no lock.
- A worker's body (`codegen_emit_job`) makes the **raw** `LLVMTargetMachineEmitToFile`
  call and defers error reporting to the main thread — it is **allocation-free on
  the Cryo side**. That's deliberate (see §2.2).
- After join, the main thread disposes module+context+TM (`dispose_emit_jobs`).
  LLVM disposal must not interleave with emit.

**Config resolution** (in `compile_project_with_ctx`): CLI
`--codegen-threads` (`CompilerConfig.codegen_threads_override`) > cryoconfig
`[compiler] codegen_threads` (`ProjectConfig.codegen_threads`) > env
`CRYO_CODEGEN_THREADS` (debug-only) > auto (`available_parallelism`). Then
`codegen_thread_count(n, requested)` clamps to the module count.
`CRYO_CODEGEN_MODE=process` still selects the legacy multi-**process** emitter
(child `cryo __emit-obj` from bitcode) as a fallback; `=serial` forces serial.

### 2.1 Why the previous "it's impossible" conclusion was wrong
An earlier attempt concluded concurrent LLVM codegen "corrupts process-global
state, unsafe on both OSes," and shipped a multi-**process** emitter as a
workaround. **That diagnosis was wrong.** A standalone C++ harness driving
concurrent `LLVMTargetMachineEmitToFile` over the exact Cryo modules never faults
(0 crashes, 60+ trials, 12 threads). The linked libLLVM is thread-safe
(`LLVMIsMultithreaded()==1` on both the Linux `libLLVM.so.20.1` and the Windows
`LLVM-C.dll`). Every crash was **Cryo-side**: two context-less LLVM-C calls
leaked objects into the process-**global** `LLVMContext`, shared across every
per-module context, so concurrent emit raced the shared object's use-list:
- `LType::const_struct` → `LLVMConstStruct` (= `…InContext(GlobalContext)`).
- `LBasicBlock::append` → `LLVMAppendBasicBlock` (= `…InContext(GlobalContext)`)
  — the decisive one: every block + its `label` type + `void` terminators leaked.

Both fixed to the `…InContext(LContext::current())` variant. A **context auditor**
(`ctx_audit_module`, env `CRYO_CTX_AUDIT`) walks every module and flags any type
whose `LLVMGetTypeContext` ≠ the module's; it's kept as a permanent invariant
guard. **RULE:** never use a context-less LLVM-C object creator
(`LLVMConstStruct`, `LLVMConstString`, `LLVMAppendBasicBlock`, `LLVMCreateBuilder`,
`LLVMInt*Type()` w/o `InContext`, …) in codegen — always pass
`LContext::current()`.

### 2.2 Why there's no arena "pause" and no thread-safe arena
The compiler uses a per-target **bump arena** (`std::alloc` GlobalArena,
`stdlib/alloc/arena.cryo`; the peak-RSS optimization) that is single-threaded by
design. An interim design paused it during the parallel region (a hack). The
proper fix that shipped: the **workers allocate nothing**, so the arena is simply
never touched off the main thread — no pause, no locks. A region allocator is
single-threaded by design; the correct fix for "touched concurrently" is to stop
touching it concurrently, not to bolt atomics onto the compiler's hottest path. A
genuinely lock-free arena is feasible (`sync::atomic` has no alloc dependency) if
you ever *need* worker-side allocation, but today you don't.

---

## 3. THE AUDIT — where the remaining wins are (your job)

The ~19% wall win is **Amdahl-bound**: only the backend emit is parallel. Measure
with `CRYO_TIMINGS=1 cryo build` (per-phase wall + a frontend/backend split).
Representative per-target profile (O2, one `make cryo` = TWO such invocations —
lib then bin):

| phase | ~time | parallel? |
|---|---|---|
| discovery | 3–4s | no |
| frontend (lex/parse) | 2–3s | no |
| monomorphization | ~1.5s | no |
| sema/lowering | ~2s | no |
| **IR-generation** | ~4–5s | **no (serial floor)** |
| **backend emit (O2)** | ~6–9s serial → ~1.8s parallel | **YES (done)** |
| link | ~0.5s | no |

So ~15s/target is the SERIAL floor and the emit is already parallel. Ranked
opportunities:

### A. Overlap serial IR-gen with parallel emit (tractable, ~10% more)
Today the producer builds a **batch** of `cg_threads` modules' IR (serial), then
emits the batch (parallel, barrier), then the next batch. During emit the main
thread blocks in `join_all` (no IR-gen); during IR-gen the cores idle. Wall ≈
`ΣIR-gen + Σemit_batches`. Replace the batch-barrier with a **bounded
producer/consumer pipeline**: workers emit already-built modules while the main
thread keeps generating the next module's IR. Wall → ≈ `max(ΣIR-gen, Σemit)` +
tail — hides the parallel-emit time behind IR-gen. Needs a small concurrent queue
(stdlib has `sync::mpsc`; watch the memory bound — don't let un-emitted modules
pile up: cap in-flight to ~`cg_threads`). **Caveat:** IR-gen mutates shared
compiler state and MUST stay single-threaded; only the emit consumer is parallel.
Keep registration topo-ordered.

### B. Load-balance the emit (tractable)
Workers get **static** round-robin slices. Module emit times vary a lot (a few
huge modules dominate), so a slice with a giant module stalls the batch. Switch to
a **shared work queue** (atomic index or `mpsc`) workers pull from — near-perfect
balance, especially combined with (A). Verify byte-identity is unaffected (it
won't be: order only matters for linker input, which is registered post-join in
topo order, not completion order).

### C. Parallelize the serial phases (big lever, high effort/risk)
The ~15s serial floor is discovery/frontend/mono/sema/**IR-gen**. IR-gen is the
fattest and the closest to the parallel boundary. Parallelizing any of these means
taming shared mutable state: the **arena**, the **intern table**
(`resolver/InternTable`), the **type registry/arena**, the **decl index**, the
lazily-declared cross-module externs. Options: per-worker arenas + a merge, or
concurrent data structures, or sharding modules across worker *pipelines*. Highest
ceiling, highest risk — only after A/B. This is where a real design doc + Jake
sign-off is warranted.

### D. Adjacent build-time wins (not strictly threading, but compounding)
- **`make cryo` double-compiles** the whole compiler (lib then bin over ~99%
  identical source; the bin closure ⊇ the lib). "Compile-once, link-twice" is
  ~50% off `make cryo`. Fully scoped in `.todo/plans/COMPILE-ONCE.md`. **Not
  started.** Orthogonal to threading but the single biggest build-time lever.
- **IR-level optimization is OFF** in project builds (passes are just
  `[IRGeneration, ObjectEmission]`; no `PassID::Optimization`). Enabling it is
  per-module and rides the same emit parallelism, and makes the compiler + every
  user binary faster. **Blocked** by a latent IR-gen bug: post-opt
  `LLVMVerifyModule` fails `Invalid operands for select instruction!` on
  `std::core::primitives`. Fix that `select` first.
- **Discovery double-lexes** every file (namespace index, then the frontend lexes
  again) — a low-blast-radius serial win.
- **Build-manifest hashing** folds files a byte at a time (`fnv_byte`) and hashes
  each source ≥2×/build (~4.6% in an old callgrind). Word-at-a-time + hash-once.

### E. Peak-RSS under threading (measure, maybe bound)
Zero-copy keeps `cg_threads` live modules (each a full `LLVMContext`) alive per
batch — higher than the interim bitcode design (one live at a time). The arena
project fights ~2.4 GB OOMs. Measure peak RSS at high thread counts
(`/usr/bin/time -v` on Linux); if it regresses, lower the default `cg_batch_cap`
or make it a function of free memory. (A pipeline (A) naturally bounds in-flight.)

### F. Thread-count/default tuning
Default = `available_parallelism()`. Emit parallelism saturates well before
core-count on many machines (memory-bandwidth bound); a smaller default might match
wall time at lower RSS/scheduler overhead. Benchmark the curve.

---

## 4. Traps & gotchas (save yourself the pain)

- **Incremental-cache MISCOMPILE.** After editing spec-owner/codegen modules, an
  incremental `cryo build` can produce a compiler that CRASHES on threaded builds;
  a full `--no-incremental` rebuild fixes it. **Always validate the compiler with
  `--no-incremental`.** (Root: a spec-owner module's cache key folds a global
  digest.) This bit me once and looked like a real regression — it wasn't.
- **`MALLOC_CHECK_=3` HIDES the heap-race** class of bug (it changes libc
  timing). Diagnose concurrency crashes with gdb backtraces + a standalone C++
  repro, not malloc checkers.
- **Context leaks are invisible without assertions.** The linked libLLVM is a
  non-assert (RelWithDebInfo) build, so a cross-context reference corrupts
  silently instead of asserting. Use `CRYO_CTX_AUDIT=1 cryo build` — expect **0**
  `FOREIGN-CTX` lines. If you add codegen that creates LLVM objects, re-run it.
- **Object-dir path differs by host.** Windows native → `…/target/release/
  host-windows/local/deps/*.o`; Linux → `…/host/local/deps`. `find … | head -1`
  may grab a stale `.bin/self/s2` selfhost dir — target the fresh dir explicitly
  when hashing for byte-identity.
- **Background WSL tasks get SIGHUP'd** at turn boundaries in this environment —
  run `make selfhost-check` (~4.5 min) in the FOREGROUND with a long timeout.
- **Windows/Linux artifact contamination:** running Windows builds
  (`CRYO_CC=gcc`) then `make test` on Linux fails linking `tests/helpers/
  abi_helpers.o` (a COFF object). `rm -f tests/helpers/abi_helpers.o
  tests/helpers/libabihelpers.a` before `make test` to force an ELF rebuild.
- **Byte-identity is THE gate**, not "it runs." Check serial-vs-threaded AND
  across selfhost stages. Thread count must never change output (it doesn't —
  only registration order matters, and that's topo-fixed post-join).
- **`![target(...)]` free fns** are the accepted pattern for platform splits /
  thread entry points (`std::thread`'s `available_parallelism`, the trampolines).
  Jake dislikes free functions generally but these are fine.
- **`ffi::syscall` imported directly into a compiler module breaks codegen** —
  that's why CPU detection lives in `std::thread`, and the compiler just calls it.

---

## 5. Files changed (all UNCOMMITTED; the threaded-emit feature)

- `compiler/llvm_bindings.h` — `LLVMAppendBasicBlockInContext` use; auditor
  introspection (`LLVMGetTypeContext`, `LLVMPrintTypeToString`, `LLVMGetFirstGlobal`
  /`GetNextGlobal`/`GetInitializer`).
- `compiler/src/compiler/codegen/llvm_types.cryo` — `const_struct` and
  `LBasicBlock::append` → `…InContext(LContext::current())` (the two leak fixes).
- `compiler/src/compiler/codegen/passes.cryo` — `EmitJob {llvm_module,
  llvm_context, tm, …}`, allocation-free `codegen_emit_job`, `emit_worker_entry` +
  `EmitWorkerCtx`, the context auditor (`ctx_audit_module`/`ctx_audit_type`).
- `compiler/src/compiler/instance.cryo` — codegen loop (build TM on main +
  keep module alive), `emit_jobs_threaded`/`emit_jobs_serial`/`dispose_emit_jobs`,
  `codegen_mode`, `codegen_thread_count(n, requested)`, config resolution,
  `CompilerConfig.codegen_threads_override`, env-gated audit call,
  `CODEGEN_MULTIPROC_MIN_MODULES`, `cg_batch_cap`.
- `compiler/src/compiler/project_config.cryo` — `[compiler] codegen_threads`
  field + `parse_codegen_threads`.
- `compiler/src/CLI/_module.cryo` — `--codegen-threads` in the flag allowlist +
  `flag_takes_value`.
- `compiler/src/CLI/commands.cryo` — `--codegen-threads` parse into config.

Retained but unused-by-default: the multi-process path (`run_multiprocess_emit`,
`emit_object_from_bitcode`, CLI `__emit-obj`) — reachable via
`CRYO_CODEGEN_MODE=process`.

Reminder: **only Jake commits.** Repin from a clean tree after commit
(`make pin` via WSL; do NOT force `CRYO_CC=gcc` on `make pin` — it breaks the
cryo.exe cross-link).

---

## 6. Build & validate

```sh
# Windows host (PowerShell), CRYO_CC=gcc:
cd stdlib   && ../bin/cryo.exe build --no-incremental
cd compiler && ../bin/cryo.exe build --no-incremental   # -> compiler/build/cryo.exe

# Linux/WSL:
cd compiler && ../bin/cryo build --no-incremental        # -> compiler/build/cryo

# Phase timings (the profiler for your audit):
CRYO_TIMINGS=1 cryo build

# Control threading (NO env needed — this is the UI):
cryo build --codegen-threads=1     # serial
cryo build --codegen-threads=8     # cap the pool
#   or cryoconfig:  [compiler]  codegen_threads = N
# Debug knobs: CRYO_CODEGEN_THREADS=N, CRYO_CODEGEN_MODE=process|serial

# Context-isolation invariant (MUST be 0):
CRYO_CTX_AUDIT=1 cryo build 2>&1 | grep -c FOREIGN-CTX

# THE correctness gates (Linux/WSL; run selfhost-check in the FOREGROUND):
make selfhost-check    # 6-stage byte-identical fixed point; must print "FIXED POINT OK"
rm -f tests/helpers/abi_helpers.o tests/helpers/libabihelpers.a && make test

# Byte-identity spot check (serial vs threaded): build with --codegen-threads=1,
# hash <deps>/*.o; rebuild with a high count; diff. Both must match.
```

Everything here is measured, byte-identity-checked, and selfhost-gated. Keep it
that way — proper, not "just works."

---

## 7. Suggested order of work

1. `CRYO_TIMINGS=1` on a clean `--no-incremental` compiler build; confirm the
   serial-floor vs parallel-emit split on THIS machine before optimizing.
2. **(B) load-balance** the emit (shared work queue) — cheap, safe, immediate.
3. **(A) pipeline** IR-gen ∥ emit — hides the parallel-emit time; the best
   threading-side win. Gate on byte-identical selfhost + peak-RSS not worse.
4. **(D) compile-once** (`.todo/plans/COMPILE-ONCE.md`) and the invalid-`select`
   fix → enable IR-opt. Biggest build-time levers, orthogonal to threading.
5. **(C) parallelize the serial phases** — only with a design doc + Jake sign-off.
6. Repin after Jake commits.
