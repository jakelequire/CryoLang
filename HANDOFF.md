# HANDOFF — finish making sema authoritative, then DELETE mono's inference engine

## TL;DR (where we are RIGHT NOW)

The mono-after-sema flip is green and self-hosting. The **sema→mono type-arg stash channel** now
covers **free calls 100%** (ABSENT 0) AND **the combinator half of method calls** (56/175 method
specs in the test corpus). The remaining job is to **finish the method-call stash** (cover the other
119), then **delete mono's private inference engine** — the entire point of the project.

- Tree is **GREEN**: `make test` and `make test ARGS="--opt-level=2"` (unit ok / compile-fail 99/0),
  and the full `cd tests && cryo test` project passes.
- **Self-host byte-identical fixed point**, IR md5 `27d3ca4a27fd3d3cf4d1907278acd713`
  (`python3 scripts/selfhost-check.py --no-windows`).
- **Define-symbol set unchanged** vs pre-M1 baseline (16697, 0 new / 0 lost) — the MISMATCH oracle.
- **REPINNED by Jake.** About to commit. (Going forward Jake still owns every `git commit` /
  `make pin`.) Safe to pin — the pinned compiler builds the new source (verified by the self-host
  pinned→build stages).
- Changed vs HEAD `2b1b772a`: `compiler/src/compiler/types/monomorphizer.cryo` (C1d + #8 + M1 reader),
  `compiler/src/compiler/passes/sema.cryo` (#8 + M1 producer), `pipeline-reorder-progress.md` (notes).

Deep, dated history: **`pipeline-reorder-progress.md`** at repo root — read the `2026-06-18 (cont. 9)`
… `(cont. 14)` sections. Memory index `MEMORY.md`, esp. `flip_8_literal_adopt_2026_06_18`,
`flip_step1_absent_zero_2026_06_18`, `mono_after_sema_flip_2026_06_17`.

## THE MISSION (unchanged) — do not stop until ALL of this is true

**Mono's private type-inference engine is DELETED (gone, not dormant).** Mono still carries a full
parallel "mini-sema" that re-infers generic call return/arg types. It now only *backstops* sema, but
it is dead weight and the latent source of the sema/mono divergence bugs the whole project exists to
kill. Finishing means ALL of:

1. **Sema is the SOLE source of truth for resolved types/bindings.** Mono only *specializes* what sema
   resolved + *discovers* instantiation demand; it never re-derives a type.
2. **Mono's inference engine is removed** — `try_infer_function_call`'s PASS A/B/C body,
   `try_infer_method_call`'s inference body, `resolve_arg_type_for_inference`,
   `resolve_static_call_return_type`, `collect_locals_*`, `lookup_local_type`, the
   `inference_bindings`/`inference_param_ids` scratch, the `unify_for_inference` helpers.
   KEEP `discover_inferred_calls_*` (the demand-discovery AST walk) — only the type *derivation* goes.
3. **Mono's call-site diagnostics that sema now owns are removed** (E0214/E0306/E0307) — ONLY after
   verifying sema emits each (compile-fail tests `tests/tests/negative/E0214_*`, `E0306*`, `E0307_*`
   are the oracle and must stay green).
4. **The transitional dual sema pass is collapsed** to its justified minimum (decide whether Phase
   6b's `FunctionBodyTypeCheck`/`post_mono_verify` is still required).
5. Suite **green O0+O2 on Linux AND Windows**, self-host **byte-identical fixed point on both**, tree
   **repinned by Jake**.

## How we work here (Jake's standard — NON-NEGOTIABLE, this is the point)

- **Be AGGRESSIVE about the refactor; prefer a hard short-term struggle over a long-term hacky green.**
  Jake said this explicitly. Do NOT pile up fallbacks, call-site special-cases, or compensating shims
  to go green. Fix the cause in the SHARED layer (the InferCtx unifier, sema's resolution), not
  per-site.
- **Attack root causes, never symptoms.** This project exists because the old design accumulated
  workarounds. Do not add new ones.
- **No green-by-skip.** A silently-deferred check that should resolve, a test deleted/relaxed/renamed,
  a one-shape special-case — all regressions even if the suite goes green. Honest signal over a green
  checkmark, always.
- **A fix that introduces a miscompile is worse than the error it replaced.** Prefer clean red over a
  green lie. If you hit a wall, say so plainly and reduce it.
- **Small, validated steps.** Land one change, rebuild, re-run suite, confirm green. When a change
  destabilizes and pulls in unrelated bugs, REVERT and bank the solid part rather than ship something
  half-understood (we did exactly this in cont. 12, and chose to bank M1-step1 in cont. 14 rather than
  rush the rest).
- **The self-host fixed point is sacred — BUT understand "byte-identical."** Any change to the
  compiler's OWN source moves the self-host md5 (the compiler compiles itself). That is EXPECTED. The
  real oracle is: **fixed point holds (stage-3 == stage-4) + identical defined-symbol set + IR diffs
  confined to the functions you edited.** Verify a "neutral" change by diffing the `define` symbol
  lists (0 new / 0 lost specs), NOT by expecting the md5 to be unchanged.
- **Report the tree truthfully.** Append a dated section to `pipeline-reorder-progress.md` each
  session: what landed, self-host md5, failing set, any wall mid-fight.
- **Jake owns every `git commit` and every `make pin`.**
- **STALE-GOTCHA TO IGNORE:** old notes say "no new fields on vtable `type class`es" — FIXED. Adding
  fields to vtable classes works (we added `CallExprNode.resolved_type_args`).

## Architecture map — the stash channel

The duplication being killed: both **sema** and **mono** run the **identical `InferCtx::unify`**
machinery to compute a generic call's concrete type-arg bindings. Sema stashes its bindings on the
call; mono reads them.

- **`CallExprNode.resolved_type_args: TypeRef[]`** (`AST/expression.cryo`) — sema's stash, in
  generic-param declaration order. cloner copies it element-wise; substituter SUBSTITUTES it (applies
  the spec subst). A CallExpr is **free XOR method**, never both, so the field is shared safely.
- **Free-call producers** (`passes/sema.cryo`): `check_generic_free_call` (keystone ~6405),
  `infer_free_call_bindings`+`resolve_module_qualified_function`, `stash_scope_resolution_call_bindings`.
- **Free-call reader (C1d, DONE)**: `try_infer_function_call` (`types/monomorphizer.cryo` ~4260) —
  seeds `inference_bindings` from the stash (applying `subst`), sets `from_stash`, skips PASS A/B/C.
- **Method-call producer (M1, DONE)**: sema `resolve_generic_method_return` (~10462) stashes its
  already-concrete `method_bindings`.
- **Method-call reader (M1, DONE)**: `try_infer_method_call` (~5948) seeds `inference_bindings`,
  `if (m_from_stash) {}` / `else if` turbofish / `else` unify.

## What landed THIS session (cont. 13–14) — both UNCOMMITTED, both validated neutral

### #8 — PASS-C literal adoptability aligned (handoff prereq, was blocking #7)
Both sema `check_generic_free_call` PASS C and mono PASS C (free + method-owner) used a raw
`check_compatibility(i32_default, bnd) == Incompatible` to flag a polymorphic literal against a pinned
binding — wrongly conflicting **i32-vs-i64** and **i32-vs-enum**. Replaced with a shared
`literal_adopts_binding(bnd)` helper (added to BOTH sema after `free_infer_type_concrete` and mono
after `infer_type_is_concrete`): a width-less int/float literal adopts any `Int`/`Float`/`Enum`
binding, NOT string/bool/struct. `second(5,"hello")`→E0214 preserved; `pick(i32,i64)` is caught in
PASS A (unaffected). Neutral (a diagnostic, not a spec — can't change the symbol set); proven by full
green suite.

### M1 step 1+2 — mono reads sema's method-call stash (the method analogue of free-call C1d)
Producer + reader as in the architecture map above. Behaviorally neutral: self-host symbol-set 0/0.
**Coverage measured** (temp `MSTASH`/`MABSENT` probe at the specialize point — STRIPPED, tree clean):
- Full `cryo test` corpus: **MSTASH 56 / MABSENT 119**. Stashed = `map` 22, `fold` 15, `zip` 8,
  `chain` 7, `next` 2, `wrap` 2 (the combinator / return-carrying-method-generic family).
- ABSENT 119 = `fmt` 38, `cast` 38, `hash` 9, `run` 7, `sample` 6, `next` 6, `try_spawn` 4, `spawn` 4,
  `relay` 4, `write_to` 2, `wrap` 1.
- Compiler corpus: MSTASH 0 / MABSENT 86 (72 `cast`, 8 `hash`, 6 `fmt`).

## NEXT STEPS, in dependency order

### Step 0 (RESUME HERE) — finish M1: cover the 119 ABSENT method calls
sema's `resolve_method_call` routes the ABSENT cases AROUND `resolve_generic_method_return`, so the
M1 producer never sees them. Two buckets:
1. **Turbofish** (`cast<U>`, `spawn`/`try_spawn`): `member.generic_args.length > 0` → sema uses
   `resolve_method_return_with_explicit_args`, never `resolve_generic_method_return`. Bindings ARE the
   turbofish args — fully authoritative, trivially stashable.
2. **Arg-only generics** (`hash<H> -> void`, `fmt<W>`, `write_to`, `relay`, `run`, `sample`): the
   method generic appears ONLY in a parameter, never the return, so `contains_generic_param(refined)`
   is false and `resolve_generic_method_return` is never called.

**Plan:** refactor the binding-computation out of `resolve_generic_method_return` into a
`compute_method_bindings(recv_type, member, call) -> TypeRef[]` (the turbofish + formal/actual-unify +
implicit-return branches already exist there, lines ~10353–10444), returning a complete concrete set
or empty. Then call a single `stash_method_call_bindings(member, call, recv_type)` for EVERY generic
method call in `resolve_method_call` (regardless of return shape), stashing whenever the full set is
concrete. This subsumes the current `resolve_generic_method_return` stash (remove that one to avoid
double-write, or leave it — stashing the same concrete values twice is harmless).
- **Method resolution is receiver-anchored**, so the cross-module same-leaf hazard (#9, below) that
  blocks the FREE-call relaxation does NOT apply here — confirm by measurement, not assumption.
- **Validate:** re-run the `MSTASH`/`MABSENT` probe (recipe below) → drive MABSENT toward 0 over BOTH
  `cd tests && cryo test` and `cd compiler && build/cryo build`. Then suite + self-host + symbol-set
  diff. The genuinely-irreducible residual (if any) is the derived/combinator element-binding case
  (`map<B>`/`fold` where element `A` comes from the impl where-bound at mono spec-time) — if it can't
  be bound from the receiver's CONCRETE element type in sema, it stays a NARROW justified mono residual
  (specialization-time, not redundant re-derivation). Surface that to Jake as an honest finding.

### Step 1 — #9: cross-module same-leaf call mis-resolution (prereq for #7)
A bare free call whose leaf exists in two modules (nested_patterns `classify -> i32` vs
PatternMatching `classify -> Sign`) can mis-resolve to the wrong module's overload in the inference
path. Masked today by `free_infer_arg_reliable` deferring call-expr args. Root-cause sema's bare-name
call resolution (`find_fn_template_for_call`) to prefer the in-scope/local definition.

### Step 2 — #7 (C2-free): delete mono's free-call inference body
With #8 done and #9 fixed, relax `free_infer_arg_reliable` (`passes/sema.cryo` ~6378) to trust a
CONCRETE call-expr `resolved_type` (re-measure with the FBPROBE recipe: fallback must hit 0 over tests
AND compiler). Then delete `try_infer_function_call`'s PASS A/B/C body + its `emit_infer_conflict`/
`emit_cannot_infer` calls. Validate: compile-fail E0214/06/07 green (= sema emits) + symbol-set
identical. NOTE `from_iter` (the `where I: Iterator<T>` case) may need its own sema handling, not the
call-expr relaxation — measure it specifically.

### Step 3 — C2: delete the shared inference engine
Once free + method both read the stash with ABSENT≈0, delete `resolve_arg_type_for_inference`,
`unify_for_inference`, `inference_bindings`/`inference_param_ids`, `collect_locals_*`,
`lookup_local_type`, `resolve_static_call_return_type`. Each removal: rebuild → suite → self-host. A
red names a thing the engine was secretly backstopping → trace to the sema result it should read and
fix THAT; do NOT restore the deleted code.

### Step 4 — C3: delete mono's call-site diagnostics (E0214/E0306/E0307)
Only after confirming sema emits each (the compile-fail tests are the oracle). Sema emitters:
`emit_free_infer_conflict`/`emit_free_cannot_infer`/`emit_free_call_bound_failure`. Mono:
`emit_infer_conflict`/`emit_cannot_infer`/`emit_call_bound_failure`.

### Step 5 — C4: collapse the dual pass + finish
Decide whether 6b's `FunctionBodyTypeCheck`/`post_mono_verify` is still needed (`instance.cryo`).
Then Windows (suite O0+O2 + cross self-host), repin, done.

## Measurement harness (re-create when you need numbers — STRIP before any commit; tree must end CLEAN)

- **Method stash coverage (`MSTASH`/`MABSENT`)**: add `import std::fmt;` to monomorphizer.cryo, and in
  `try_infer_method_call` just before `specialize_method` (~6079):
  `if (m_from_stash) { eprintf("MSTASH %s\n", this.intern_table.resolve(ma.member)); } else { eprintf("MABSENT %s\n", this.intern_table.resolve(ma.member)); }`
  Build, run `(cd compiler && build/cryo build) 2>/tmp/mc.log` and `(cd tests && <abs>/build/cryo test) 2>/tmp/mt.log`,
  `grep -c MSTASH/MABSENT`, `grep … | awk '{print $2}' | sort | uniq -c | sort -rn`. STRIP after.
- **Free-call fallback reliance (`FBPROBE`)**: in `try_infer_function_call` just before
  `call.set_resolved_callee(spec_sym)` (~4467): `if (!from_stash) eprintf("FBPROBE %s\n", this.intern_table.resolve(base_sym));`
- **Symbol-set neutrality (the real oracle for a "neutral" change)**: after a self-host run,
  `grep '^define' compiler/build/self/s3/cryo.ll | sed -E 's/^define[^@]*@"?([^"(]*)"?\(.*/\1/' | sort`
  for baseline and your build, then `comm -13`/`-23` — must be 0 new / 0 lost. Capture the baseline
  BEFORE rebuilding (the existing `build/self/s3/cryo.ll` from the last self-host run, before
  `make cryo` overwrites `build/cryo`; self-host itself wipes `build/`).
  ⚠ Caveat learned cont. 14: the compiler self-build has 0 stashed method calls, so a 0/0 method-path
  symbol diff mainly proves the READER is inert when nothing stashes — the 56 STASHED test-corpus
  calls' neutrality is proven by the FULL GREEN suite (wrong-method stash → miscompile → red test).

## Validation loop & gotchas (these bite)

- `export CRYO_CC=gcc` everywhere (Linux). Diagnostics go to **stderr**.
- **Build:** `make cryo` (rebuilds stdlib + self-hosted compiler → `compiler/build/cryo`, ~1.5 min).
- **Suite:** `make test` / `make test ARGS="--opt-level=2"`; full project test is
  `cd tests && "$PWD/../compiler/build/cryo" test` (compile-fail 99/0). ⚠ `make test` uses a STALE
  stage-2 if you skip `make cryo` first — always `make cryo` before `make test`.
- **Self-host:** `python3 scripts/selfhost-check.py --no-windows` — must stay a byte-identical fixed
  point. ⚠ It **WIPES `compiler/build/`** — re-run `make cryo` afterward. ⚠ `| tail -N` CUTS the
  `IR md5:` line; grep for `md5|fixed|broken` instead.
- ⚠ **The Bash tool's cwd persists across calls.** Always `cd /workspaces/CryoLang` first or wrap cd
  in a subshell `( … )`.
- ⚠ **Piped exit codes:** check `${PIPESTATUS[0]}`, not the pipe's status.
- ⚠ **Serial builds only.** Never two builds at once — shared `.bin`/`.o` cache corrupts. Recover:
  `rm -rf compiler/.bin` then rebuild alone.
- ⚠ **Do NOT spawn `while…sleep` polling loops to wait on a build** (lingering procs / OOM). Run the
  build/self-host in the background and let the harness notify you; do read-only work meanwhile. Do
  NOT edit compiler source while a build/self-host is running (it reads `compiler/src`).
- **Reproduce a compiler bug as a PROJECT** under a scratch dir (NOT in `tests/`):
  `build/cryo build foo.cryo --stdlib=stdlib -o out && ./out`. A bare test file fails with E0151.
- **`set_resolved_type_args(x)` MOVES `x`** — capture `.length` before the call (Cryo E0452).
- Symbolic-walk kill-switch env `CRYO_NO_SYMBOLIC_CHECK` (default-ON).
- Key TypeKind ordinals: Bool 2, Int 3, Float 4, Char 5, String 6, Pointer 8, Reference 9, Enum 16,
  InstantiatedType 21. NodeKind: Literal 3, Identifier 4, CallExpression 11, MemberAccess 21.

## Windows (you're switching to your Windows PC)

- Windows self-host & pin are wired: `make pin-windows` / `make pin-all`; `selfhost-check.py` has a
  Windows stage (drop `--no-windows`). Memory `windows_*` entries document the toolchain: Windows-gnu
  + `ld.lld`, `CRYO_CC`/`CRYO_AR` → llvm-mingw clang/ar; net/sync/thread/process/test::runner are
  ported. On Linux these ran under wine; on a native Windows box use the native toolchain.
- The flip work so far is all in shared `sema.cryo`/`monomorphizer.cryo` (OS-independent), so it
  should behave identically — but the Definition of Done REQUIRES Windows green O0+O2 + cross
  self-host, so validate there as you go, not just at the end.
- ⚠ valgrind lesson: passing an owning aggregate BY VALUE frees the shared buffer at param-drop →
  Linux glibc tolerates the corruption, **Windows aborts on it**. If a change is green on Linux but
  crashes on Windows, suspect a by-value owning-array/`*ptr`-struct copy.

## Task list (in the harness TaskList, if it survives the session)
M1-rest (dedicated method producer for the 119) · #9 same-leaf misresolution · #7 C2-free ·
C2 delete engine · C3 delete diagnostics · C4 collapse dual pass.
Recommended order: **M1-rest → #9 → #7 → C2 → C3 → C4 → Windows → repin.**

## Definition of done
`make test` green O0+O2 on **Windows and Linux**; `selfhost-check` byte-identical fixed point on
**both**; **mono's inference engine DELETED**; sema the sole source of truth for resolved types; the
dual pass collapsed to its justified minimum; tree repinned by Jake. Anything short of that is a
checkpoint, not the finish. Reduce, root-cause, fix the cause in sema/the shared layer, re-validate,
no shortcuts.
