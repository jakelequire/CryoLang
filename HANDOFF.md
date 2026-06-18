# HANDOFF — Finish the mono-after-sema flip to 100% (Linux + Windows in lockstep)

## The mission (unchanged — do not stop until this is true)

The compiler is mid-migration from **Monomorphization-before-Semantic-Analysis**
to **mono-after-sema**. The structural flip landed and self-hosts, but the
migration is **not finished**. Finishing means ALL of:

1. **Sema is the single source of truth for resolved types** — inferred generic
   call/method returns, turbofish returns, static-constructor returns,
   associated-type projections — every `resolved_type` mono sees is already correct.
2. **Mono's private inference engine is DELETED.** That deletion is the entire
   justification for this project. It still backstops every specialization today.
3. **The transitional dual sema pass collapses** to the minimum the architecture needs.
4. The suite is green at **O0 and O2 on BOTH Linux and Windows**, the self-host is
   a **byte-identical fixed point on both**, and the tree is **repinned**.

You are removing a duplicated subsystem and making one component authoritative.
Anything short of mono's inference engine being **gone (not dormant)** is unfinished.

## How we work here (read this first — it is the point, non-negotiable)

Jake's standard:

- **Attack root causes, never symptoms.** This project exists *because* the old
  design accumulated workarounds. Do not add new ones to limp past a failure.
- **No hacky workarounds. No green-by-skip.** A silently-deferred check that should
  resolve, a test deleted/relaxed/renamed to pass, a special-case for one shape — all
  are regressions even if the suite turns green. **Honest signal over a green
  checkmark, always.** (Concrete example from last session: there are two test-local
  `identity<T>` causing the last 2 errors — do NOT rename one to fake-green it; the
  compiler must handle the collision.)
- **A fix that introduces a miscompile is worse than the error it replaced.** Last
  session a mono PASS-C override turned 2 clean compile-errors into a `<null operand>`
  LLVM miscompile — that was reverted on sight. Prefer a clean red over a green lie.
- **High-quality code.** Match the surrounding idiom; comment the *why* when non-obvious.
- **Don't give up when it fights back.** Regressions are signal. Reduce to the
  smallest repro, understand it fully, fix the cause, re-validate on both platforms.
- **Small, validated steps.** The tree now COMPILES (to 2 known errors) and self-hosts,
  so you finally have a near-green bisect handle again — use it. Land one fix, rebuild,
  re-run the suite, confirm the error count only goes DOWN.
- **The self-host fixed point is sacred.** If it breaks, you changed observable
  behavior somewhere unexpected. Root-cause it; don't suppress it.
- **Report the tree truthfully.** If a doc/commit claims a state, it must be the real
  state on the platform claimed. Keep THIS file and `pipeline-reorder-progress.md` honest.

## Read these for the deep history (do not re-derive)

- `pipeline-reorder-progress.md` — the full session log. The **`2026-06-17 (cont. 4)`**
  section is the most important: it has all nine fixes from last session with file +
  cause, the precise remaining root cause, and the self-host md5. Start there.
- Git log around the flip: `f9a1ffa1` → `6e074d37` (orchestrator flip) → `a8110cdd`
  (8 regression-class fixes) → **the commit Jake just made carrying last session's nine
  more fixes** (sema/checker/monomorphizer; repinned).

---

## CURRENT STATE (verified 2026-06-17, end of session) — start from THIS

### The flip COMPILES the whole suite to 2 errors (down from 149) and SELF-HOSTS.
- **Self-host: byte-identical FIXED POINT on Linux** —
  `python3 scripts/selfhost-check.py --no-windows`, IR md5
  `3b7396b71606a4ce108ab0b0c3b7e1df`, 49,883,221 bytes. The session's fixes did NOT
  break self-compilation. (Windows self-host NOT separately re-run.)
- The PIN remains the green oracle: it compiles the identical sources **1234 passed /
  0 failed**. Diff its behavior against yours when stuck.

### The "heap corruption wall" was SOLVED (it was misdiagnosed). 
It was NOT a startup/global-init miscompile. The unit binary runs 1234/0 fine. The crash
was in the COMPILER during in-process `compile_project`, in Monomorphization: a
**use-after-free** — `Monomorphizer::emit_call_bound_failure` took `tref: TraitRef` BY
VALUE; the shallow copy shared the AST's `Array<SymbolStr>` `path` buffer and the
param-drop freed it (crash on Windows `0xC0000374`, silently tolerated by Linux glibc —
so "parity" can hide it). **Found with valgrind on the Linux build** (`--track-origins=yes`).
Fix: take `tref: TraitRef*`. See memory `cryo-authoring-gotchas` gotcha #4.

### The nine fixes that landed last session (all root-caused; details in progress log)
1. **UAF** (the "heap corruption") — by-value `TraitRef` → by-pointer. `monomorphizer.cryo`.
2. **mono bound-check on abstract bindings** — added the `type_contains_generic_param`
   defer guard to `check_function_bounds_at_call`. `monomorphizer.cryo`.
3. **InstantiatedType structural equality** — `checker.cryo` `check_compatibility` now
   compares two non-deduped instantiations by base + args (like Reference/Tuple already do).
4. **for-iter adapter-chain demand** — mono enqueues the for-in iterator local's
   sema-resolved instantiation so `a.iter_ref().copied()` specializes (was E0900). `monomorphizer.cryo`.
5. **generic STATIC-CONSTRUCTOR inference** — `infer_static_owner_return_from_args` in
   `sema.cryo`: `Range::new(0,5)` now types as `Range<i32>`, not abstract `Range<T>`
   (cleared 97 E0200). Wired into `try_resolve_static_method` as an abstract-result refinement.
6. **for-in over a CONTAINER** — `lookup_type_sym` falls back to an unresolved
   instantiation's `generic_base` so the `iter()`/`next()` probe agrees pre/post-mono
   (cleared E0358 + residual E0200). `sema.cryo`.
7. **PASS B vs abstract expected** — gated `check_generic_free_call` PASS B on
   `!contains_generic_param(expected_type)` (cleared the `max_of` E0636s). `sema.cryo`.
8. **lambda double-lowering** (36× E0201 `cannot find value this`) — (a) idempotency guard
   in `resolve_lambda`; (b) `resolve_identifier` honors a `this` node that already carries
   a resolved type. `sema.cryo`.

(Prior session's #2 `inst_template_thread_safe` in `ownership.cryo` still stands.)

### THE 2 REMAINING ERRORS (generics.cryo:158,168) — root-caused, fix DEFERRED
`expect_eq(identity(123), 123)` / `expect_eq(identity(v), v)`. Cause: there are **two**
1-arg `identity<T>` templates — `tests/tests/lang/generics.cryo:148` and
`tests/tests/lang/match_generic_free_fn.cryo:24`. `find_fn_template_for_call` requires a
UNIQUE bare-name+arity match (`count == 1`), so the collision makes it return null →
`check_generic_free_call` bails → `identity(123)` surfaces its template's **abstract**
return `T` → poisons the outer `expect_eq<T>` inference → no spec pinned → **E0636** at
codegen. (Confirmed by debug print: `check_generic_free_call` is never entered for `identity`.)

**Why it's deferred, not patched:** every attempt to disambiguate the collision
**cascades**:
- Disambiguating in the shared `find_fn_template_for_call` broke `resolve_direct_call`'s
  deferral signal → 8 errors.
- Isolating it to a `check_generic_free_call`-only `_local` variant STILL cascaded into
  8 `va.next()` E0636 in `varargs.cryo` — fixing identity's resolution shifts the
  specialization graph and unmasks a VaArgs method-spec gap.
This is precisely the fragile mono-inference interaction Step C is meant to delete. It
wants the holistic treatment, not another point-patch. Treat the 2 errors as the FIRST
work item of the cascade-finish below, and expect to chase what they unmask.

---

## The remaining work, in dependency order

### Step A/B residue — drive the suite to a GENUINELY green `make test`
1. **Fix the `identity` collision properly** (the 2 errors) and chase the VaArgs cascade
   it unmasks. The honest options:
   - Make name resolution disambiguate a same-named generic free fn by the CALLING
     module — but do it where it can't perturb `resolve_direct_call`'s deferral or method
     resolution. Last session's `_local` variant was the right shape but still rippled;
     understand WHY the VaArgs `next` specialization depends on identity's resolution path
     before re-attempting.
   - OR fold it into Step C: once sema creates the demand directly, the collision may
     resolve cleanly without the fragile `find_fn_template_for_call` count==1 gate.
2. **Confirm the suite RUNS green, not just compiles.** It has only ever compiled-to-2-
   errors this session; it has NOT run to 1234/0 on the flip. Once it compiles clean,
   run `make test` and verify pass counts vs the pin. Watch for runtime (not just
   compile) regressions the pin doesn't have.
3. **Both opt levels, both platforms:** `make test ARGS="--opt-level=0"` and
   `--opt-level=2`, on Windows AND Linux (WSL). Plus Windows self-host.
4. Watch for: more mono-staleness where mono re-infers on already-sema-typed nodes and
   disagrees (the recurring pattern); `is_copy` having the same pre-mono unresolved-
   instantiation gap that #2/`is_send`/`is_sync` had.

### Step C — collapse the dual pass and DELETE mono's inference engine (the payoff)
Only after the suite is green. This is the reason the project exists, and last session
PROVED the engine is deeply entangled — every point-patch into it ripples (abstract-actual
skip broke Result; PASS-C override produced a null-operand miscompile; the collision fix
broke VaArgs). So Step C is NOT just "delete code" — it is "delete code AND re-home in
sema everything it was silently backstopping." Expect each deletion to turn things red;
that red IS the inventory of what mono was covering.
- Sema must **create the instantiation DEMAND directly** (today mono discovers it via
  `try_infer_*`). This is the load-bearing change. Last session's for-iter
  `enqueue_from_type_ref` (#4) is a tiny preview of the shape.
- Then delete mono's private inference engine in `monomorphizer.cryo`:
  `try_infer_function_call` / `try_infer_method_call`, `resolve_arg_type_for_inference`
  (+ its static-call guard), `resolve_static_call_return_type`, `collect_locals_*`,
  `lookup_local_type`, the `inference_*` scratch.
- Delete mono's call-site diagnostics sema now owns (E0306/E0307/E0214 —
  `emit_call_bound_failure` / `emit_infer_conflict` / `emit_cannot_infer`). Verify sema
  covers every case FIRST.
- Re-examine `post_mono_verify`: once mono no longer re-infers, decide whether the
  post-mono sema pass is still needed or can be reduced. Don't leave "type-check twice
  ignoring caches" as permanent debt.
- Delete each piece in its own validated step. A deletion turning something red = mono
  still secretly relied on its own inference; trace it to the sema result it should read
  and fix THAT. Do not restore deleted code as a workaround.

### Step D — Repin (Jake's action)
Once green on both platforms at O0+O2 with a byte-identical self-host on both, surface that
a repin is needed. **Do not `make pin` or `git commit` yourself.**

## How close are we, honestly
- **"Suite compiles":** ~99% (2 errors, one root cause).
- **"Suite green-running both platforms, O0+O2":** ~90% — the 2 errors + their cascade +
  unverified full-suite runtime/O2 behavior.
- **"Migration 100% complete (mono engine DELETED, dual pass collapsed)":** ~50–60%.
  Step C is the larger half and has not begun. Do not read "2 errors left" as "2% left."

## Key files
- `compiler/src/compiler/passes/sema.cryo` — the keystone + last session's additions:
  `check_generic_free_call` (free keystone; PASS A/B/C), `infer_static_owner_return_from_args`
  + `resolve_static_method_return_via_template` (static-ctor inference, #5),
  `try_resolve_static_method` (abstract-result refinement), `lookup_type_sym` (generic_base
  fallback, #6), `resolve_lambda` (idempotency guard, #8a), `resolve_identifier`
  (`this`-already-resolved, #8b), `find_fn_template_for_call` (the count==1 collision gate
  behind the 2 remaining errors), `post_mono_verify`, the for-in desugar (~line 2680).
- `compiler/src/compiler/types/monomorphizer.cryo` — the inference engine to DELETE in
  Step C; last session: `emit_call_bound_failure` (#1 by-ptr), `check_function_bounds_at_call`
  (#2 defer), VarDecl-discovery demand enqueue (#4). The mono PASS A/B/C inference is at
  `try_infer_function_call` (~4101); `resolve_arg_type_for_inference` (~3394) is the
  stale "mini-sema" whose CallExpression case (~3515) already trusts sema's resolved_type.
- `compiler/src/compiler/types/checker.cryo` — `check_compatibility`; #3 InstantiatedType
  structural-equality block sits with the Reference/Tuple ones.
- `compiler/src/compiler/types/ownership.cryo` — `inst_template_thread_safe` (prior #2);
  `is_copy_at_depth` may share the same pre-mono unresolved-instantiation gap — watch for it.
- `compiler/src/compiler/types/inference.cryo` — the shared `InferCtx` unifier.
- `compiler/src/compiler/types/substitution.cryo` — `from_params`/`apply`.
- `compiler/src/compiler/instance.cryo` — orchestrator phases (6a-i.5 pre-mono, 6b post-mono).
- `compiler/src/compiler/passes/pass_id.cryo`, `pass_registry.cryo` — provision DAG +
  pipeline order (`FunctionBodyTypeCheck` before `Monomorphization`).

## Validation loop & gotchas (carry-forward + new)
- `export CRYO_CC=gcc` everywhere. Diagnostics → **stderr**.
- **Windows: run `make` from PowerShell** (`$env:CRYO_CC='gcc'; make cryo` then a fresh
  `make cryo` before `make test` — `make test` uses a STALE stage-2 if you skip it). Git
  Bash's `make` breaks on the cmd-syntax stdlib recipe.
- **`cryo test` rebuilds the test project in-process**, so a COMPILER bug surfaces there;
  `--debug` routes pass logging to stderr (that's how the mono crash site was first located).
- **Reproduce compiler crashes/miscompiles as a PROJECT** under a Windows-fs scratch dir:
  `build/cryo.exe build foo.cryo --stdlib=<repo>/stdlib -o out.exe`. (`![config(testing)]`
  / `![test]` only compile inside `tests/`, so a standalone repro must call the stdlib
  generic directly from a plain `fn`, not via `![test]`.)
- **valgrind is your friend for memory bugs** — install it in WSL as root
  (`wsl -u root apt-get install -y valgrind`); Linux glibc tolerates corruption Windows
  aborts on, but `valgrind --track-origins=yes` names the exact bad read + freeing stack.
- **Linux self-host:** `wsl bash -lc "cd /mnt/c/Programming/apps/CryoLang && export
  CRYO_CC=gcc && python3 scripts/selfhost-check.py --no-windows"` — must be a byte-identical
  fixed point. **A WSL self-host run WIPES `compiler/build/` and leaves the LINUX binary
  there; re-run `make cryo` on Windows after** before using `build/cryo.exe`. Last md5:
  `3b7396b71606a4ce108ab0b0c3b7e1df`.
- ⚠ **Serial only.** Never two builds at once against this tree (Windows + WSL included) —
  shared `.bin`/`.o` cache corrupts (`ar: file truncated`). Recover: `rm -rf .bin`, re-run alone.
- **Cryo authoring gotchas** (compile-but-segfault) — see memory `cryo-authoring-gotchas`:
  no new fields on vtable `type class`es; non-zero global inits ignored; `eprintln(String)`
  vs `format()->string`; **and #4: never pass an owning aggregate (struct with an `Array`/
  owning field) BY VALUE — the param-drop frees the shared buffer (UAF). Pass by pointer.**
- For temporary debug, `println(format(...))` reaches stdout and survives; strip it before
  declaring done (last session left zero debug code — keep that bar).
- Symbolic-walk kill-switch env `CRYO_NO_SYMBOLIC_CHECK` exists for triage; default-ON.

## Working agreement
- **Jake owns every `git commit` and every `make pin`.** Surface a needed repin clearly
  with the reason; don't do it yourself.
- **Keep `pipeline-reorder-progress.md` updated** — append a dated section per session:
  what landed, the self-host md5, the failing set on BOTH platforms, any wall mid-fight.
  ⚠ It lives at the **repo root**; watch your shell cwd.
- **Keep this HANDOFF.md honest.** If the state differs from what's written, fix the doc.

## Definition of done
`make test` green at O0 + O2 on **Windows and Linux**; `selfhost-check` a byte-identical
fixed point on **both**; mono's inference engine **DELETED** (gone, not dormant); sema the
sole source of truth for resolved types; the tree repinned by Jake. Anything short of that
is a checkpoint, not the finish.

You've got this. Tomorrow: fix the `identity` collision + chase its cascade to a genuinely
green suite on both platforms, THEN start Step C — the real prize. Reduce, root-cause, fix
the cause, re-validate on both platforms, no shortcuts.
