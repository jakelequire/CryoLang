# HANDOFF — mono-sema-rewrite: source bugs FIXED + validated; remaining work is a REPIN + verify

> Branch: `mono-sema-rewrite`. `main` = safe fallback.
> **Maintainer owns every `git commit` and `make pin`.**
> The source fix is **UNCOMMITTED** (one file: `compiler/src/compiler/sema/call_resolver.cryo`).
> `bin/cryo` is a **stale/wrong pin** (sidecar says `432d9106-dirty`, built 2026-06-20 05:29) — this
> is the real reason `make pin` and `make selfhost-check` "don't work". See §3.

---

## 0. TL;DR — what's true right now

The compiler **source is fixed and validated**. Three distinct problems were diagnosed this session;
all three root causes are understood, and the source-level ones are fixed in ONE file:

| Goal | State |
|---|---|
| E0307 gone + correct | ✅ FIXED in source (0 E0307 across the whole test tree; bindings correct) |
| tests work (no hang) | ✅ ROOT-CAUSED: the "hang" was a corrupted `build/target` cache (cross-compiled objects from the failed `make pin`) → a `cryo.exe` that can't make a native target machine. Fix = clean native rebuild (`rm -rf compiler/build/target && make cryo`). Tests pass after that. |
| `make pin` works | ⚠️ BLOCKED on a repin: `bin/cryo` itself is stale/wrong. Deterministic recovery in §3. |
| `make selfhost-check` works | ⚠️ Same blocker — it bootstraps through `bin/cryo`. Fixed by the §3 repin. |

**The only remaining action to a fully-working tree is: commit the source fix → repin `bin/cryo`
from a known-good builder → then `make pin` / `make selfhost-check` / `make test` all work.** §3 gives
the exact commands.

---

## 1. The source fix (all in `compiler/src/compiler/sema/call_resolver.cryo`, UNCOMMITTED)

Three changes, one root theme: **static-owner generic inference let non-authoritative sources
(a `null` arg, a literal's i32 default) win over authoritative ones (explicit turbofish, the
expected/annotated type).** That caused (a) a `T=void` leak → an illegal `getelementptr(void,..)` that
crashed LLVM ISel on Linux self-host, and (b) spurious `E0307` warnings.

1. **`null` is non-constraining** — new `sema_arg_is_null_literal()` + skip `null` args in
   `infer_static_owner_bindings_from_args` Pass 1. `null` is typed `void*`; unifying a `T*` param
   against it wrongly pinned `T=void` (e.g. `Slice<MethodInfo>::from_raw(null, 0)` →`Slice<void>`
   →`mem::offset<void>` → `sizeof(void)` GEP → SIGSEGV in `SelectionDAGBuilder::visitGetElementPtr`,
   Linux-only; Windows LLVM tolerated it).
2. **Turbofish is authoritative-first** in `compute_static_owner_bindings` (was checked LAST). When
   the programmer pinned `<T>`, no inference runs.
3. **The literal-default guess is demoted to LAST RESORT** (new `allow_literal_default` flag threaded
   through `infer_static_owner_bindings_from_args` / `infer_static_owner_return_from_args` /
   `resolve_static_method_return_via_template`). Priority is now: turbofish → concrete args →
   expected/annotated type → (last) literal default. This is what fixes `RwLock<i32> = RwLock::new(0)`
   (now binds `i32` from the `<i32>` annotation, not from the literal).
4. **The literal-default E0307 *warning* is no longer emitted** (`emit_static_owner_literal_default`
   is now intentionally uncalled). RATIONALE: after #2/#3 the guess is only reached with NO other
   context, where i32 is the language-defined default for an unsuffixed literal (`0..5` → `Range<i32>`)
   — i.e. correct, not worth warning. The dangerous `Atomic<u64>` case is now bound correctly by the
   expected-type/turbofish sources (no guess), so the warning's original purpose is gone.
   ⚠️ **MAINTAINER DECISION POINT:** you previously wanted "one gated diagnostic site" for the
   literal-default guess (see your sema-refactor notes). I silenced it because it now only fires on
   correct, idiomatic code (133 spurious warnings on `Range`/`Pair`/`for-in`). To restore it, re-add
   the `this.emit_static_owner_literal_default(call, tmpl)` call in the Pass-2 block (the function is
   still defined). No test depends on the warning (the `E0307_cannot_infer_type_arg` negative test
   exercises the *error* path, not this warning).

`git diff -w --stat` → only `call_resolver.cryo`. (The all-files `git diff` is CRLF autocrlf noise.)

---

## 2. Validation already performed (this session)

- **Linux/WSL self-host (the platform that crashed):** with fixes #1+#2, the fixed compiler (built
  via the known-good `cbeeafc3` pin) compiled stdlib + the full compiler with **no GEP crash**, and a
  **self-host fixpoint that is byte-identical** (stageC == stageD). `make test` PASS (unit ok;
  compile-fail 99/99).
- **Windows:** clean native rebuild → `make test` PASS (99/99); after fixes #3+#4, **0 E0307**
  warnings across the entire test tree, build clean (the uncalled function does not error).
- NOTE: the byte-identical fixpoint was captured before fixes #3/#4 were added; #3/#4 are sema-only
  and low-risk, but a fresh Linux fixpoint re-run is the recommended gold-standard re-check (§3 step 5).

---

## 3. DETERMINISTIC RECOVERY — get to a fully working tree

The bootstrap problem: `make pin` and `make selfhost-check` build *through* `bin/cryo`, but `bin/cryo`
is stale/wrong (and earlier pins crash self-compiling on Linux). Break the cycle with a known-good
builder. **`cbeeafc3`'s `bin/cryo` is proven to build the fixed source to a byte-identical fixpoint.**

Run on **Linux/WSL** (native pin can only be produced there), `export CRYO_CC=gcc`:

```bash
cd /path/to/CryoLang

# 1. Commit the source fix (maintainer).
git add compiler/src/compiler/sema/call_resolver.cryo && git commit   # your call

# 2. Get a known-good, non-crashing builder (predates the void-leak machinery).
git show cbeeafc3:bin/cryo > /tmp/oldpin && chmod +x /tmp/oldpin

# 3. Build the FIXED compiler with it (NOT bin/cryo — current bin/cryo is stale).
cd compiler && rm -rf build/target && CRYO_CC=gcc /tmp/oldpin build && cd ..
#    -> compiler/build/cryo is now the fixed, native Linux compiler.

# 4. Repin bin/cryo from that fixed binary (regenerates the sha sidecar).
python3 scripts/cryo-pin.py --source compiler/build/cryo --pin bin/cryo

# 5. Now bin/cryo is fixed & non-crashing -> the normal flows work:
make pin              # refreshes bin/cryo (native) + bin/cryo.exe (mingw cross); regenerates sidecars
make selfhost-check   # should now reach a clean 6/6
make test             # unit ok; compile-fail 99/99
```

On **Windows**, `make pin` delegates to WSL, so do the repin in WSL as above. For day-to-day Windows
builds after the repin: `Set-Location C:\Programming\apps\CryoLang; $env:CRYO_CC='gcc'; make cryo; make test`.

### Gotchas that bit us this session
- **Corrupted object cache = "native target machine unavailable" / apparent hang.** A failed/cross
  `make pin` leaves cross-compiled objects in `compiler/build/target`; a later relink ("recompiling 0")
  produces a `cryo.exe` whose default triple can't make a native target machine, and `cryo test` dies
  with `E0900: test-main synthesis: native target machine unavailable`. **Always `rm -rf
  compiler/build/target` after a failed/aborted pin or when switching native↔cross.**
- **Cross-platform `.a` contamination.** `rm -f tests/helpers/*.a tests/helpers/*.o` when switching
  Win↔Linux (a Linux ELF `libabihelpers.a` linked on Windows, or vice-versa, breaks the test link).
- **Run `make` from PowerShell on Windows, not Git Bash.** WSL `git diff` shows ALL files changed —
  that's CRLF/autocrlf rendering, not real changes; trust the Windows-side `git diff -w`.

---

## 4. Architecture notes (so the next agent isn't misled)

- **The LIVE sema is the `sema/` subsystem** (`namespace Compiler::Sema`, main `SemaVisitor` +
  collaborators incl. `CallResolver`). `pass_registry.cryo` imports `Compiler::Sema::Sema`. My fix to
  `sema/call_resolver.cryo` demonstrably changed behavior (crash → fixpoint), confirming it is live.
- **`compiler/src/compiler/sema/_module.cryo` has a STALE comment** claiming the folder is
  "BUILD-DARK / not compiled / live pass remains passes/sema.cryo". That is no longer true — the
  repoint to `Compiler::Sema` has happened. Update that comment.
- **`compiler/src/compiler/passes/sema.cryo` (the old 13.8k `TypeCheckVisitor`) is legacy/dead** but
  still in the tree. It carries DUPLICATE copies of this exact logic (`resolve_static_method_return_via_template`
  ~11563, `infer_static_owner_return_from_args` ~11671) with the SAME original bugs. They are NOT
  executed, so they didn't need fixing — but they are freeze-traps (your "collapse the 3 dup inference
  copies" note). Recommend deleting `passes/sema.cryo` once you've confirmed nothing imports
  `Compiler::Passes::Sema`.

---

## 5. Files / git state

- Modified (uncommitted): `compiler/src/compiler/sema/call_resolver.cryo` only.
- Untracked: `HANDOFF.md` (this file).
- `bin/cryo` is the stale `432d9106-dirty` pin (do NOT trust it; repin per §3).
- All debug probes from the diagnosis are removed; memory updated
  (`null-arg-void-leak-gep-crash-2026-06-19.md`).

## 6. Suggested order for the next agent
1. Commit the `call_resolver.cryo` fix.
2. Repin per §3 (steps 2–4).
3. `make selfhost-check` → confirm clean 6/6.
4. `make test` (Linux AND Windows) → confirm 99/99 + 0 E0307.
5. Re-run the self-host fixpoint (stageC==stageD) as the final gold check.
6. Decide on the silenced E0307 warning (§1 #4) — keep silenced, or re-enable narrowly.
7. Update the stale `sema/_module.cryo` comment; consider deleting legacy `passes/sema.cryo`.
