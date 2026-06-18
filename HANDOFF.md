# HANDOFF — mono-after-sema flip SELF-HOSTS; finish the iterator assoc-projection reduction

## Status (2026-06-18): THE FLIP SELF-HOSTS ✅
The mono-after-sema flip is driven all the way through the compiler's own multi-module
build. **`selfhost-check --no-windows` is a byte-identical FIXED POINT** (md5
`83bc582db52cc446e2a654aacd9d7bb2`). stdlib compiles clean under the new order.

- Orchestrator (`instance.cryo`): `FunctionBodyTypeCheck` now runs in **Phase 6a-i.5**
  (pre-mono, between DirectiveProcessing and Monomorphization) AND stays in **Phase 6b**
  (post-mono, authoritative for codegen; detected via `MonomorphizationComplete` →
  `TypeCheckVisitor.post_mono_verify`, which re-resolves rather than trusting the pre-mono
  pass's partial annotations).
- Sema is the source of truth for resolved TYPES; ~10 principled pre-mono
  member-resolution fixes landed in `sema.cryo` (template lookup + type-arg substitution +
  `This`/Self substitution + pin-stranding guards). Full list + rationale in
  `pipeline-reorder-progress.md` (LAST dated section, 2026-06-18) and memory
  `mono_after_sema_flip_2026_06_17.md`.
- UNCOMMITTED (Jake commits). NOT repinned (pinned BRIDGE compiler builds the new source
  fine; repin is available once the suite is fully green — surface it, don't auto-commit).

## THE ONE REMAINING BLOCKER — associated-type-projection reduction in iterator combinators
`make test` = **6 errors, all in `tests/stdlib/iter.cryo`** (filter / zip / enumerate).
Root cause: `next(mut &this) -> Option<This::Item>`. On a concrete combinator receiver
(`FilterIter<Range<i32>>`), `subst_this_in_type` substitutes the projection BASE
(`This -> FilterIter<Range<i32>>`) but the result stays an **`AssocProjection`**
(`FilterIter<Range<i32>>::Item`) — it must be RECURSIVELY REDUCED to `i32` by walking the
combinator's `Iterator` impl `Item` binding (FilterIter's Item = inner I's Item = Range's
Item = i32).

- Mono does this reduction today (`monomorphizer.cryo`, `proj.set_resolved_type(reduced)`).
  Sema must do it PRE-MONO now.
- This is a DISTINCT mechanism from the type-arg substitution already landed:
  `substitution.cryo:apply_assoc_projection` only rebuilds the projection with a new base,
  it does NOT reduce. You need a sema-side `reduce_assoc_projection(proj)` that resolves
  `<ConcreteType as Trait>::Member` → the impl's bound type, recursively.
- Hook it where projections surface: after `subst_this_in_type` /
  `subst_method_return_from_receiver` return a type that still
  `contains_generic_param`/is an `AssocProjection`, reduce it. Canary repro:
  `for (x in r.filter(pred))` then `sum + x` (E0229 Int + AssocProjection);
  `e.next()` where `e: EnumerateIter<Range<i32>>` (E0200).

## After that: delete mono's inference engine (step 6 — the original end goal)
Mono's `try_infer_*` still backstops discovery + specialization (sema deliberately leaves
`resolved_callee`/`resolved_method` unpinned for the inferred case so mono still
specializes + backfills `resolved_type` via `propagate_instantiated_resolution`). Once the
suite is green, delete mono's inference layer (the ~27 fns mapped in earlier sections) and
have sema create the instantiation demand directly. Then repin.

## Validate
```
make cryo            # stage-2 (~90s)
make test            # target: unit ok + compile-fail green; currently 6 iter.cryo errors
unset CRYO_SYMBOLIC_CHECK; CRYO_CC=gcc python3 scripts/selfhost-check.py --no-windows
                     # ✓ FIXED POINT OK (currently green)
# fast new-order probe:
./compiler/build/cryo build stdlib/lib.cryo --stdlib=$(pwd)/stdlib -o /tmp/s2 2>&1 | grep -E "error|-->"
```
A selfhost run wipes `compiler/build/`; `make cryo` to rebuild the stage-2 binary.
NO band-aids — every fix is a principled pre-mono resolution. Honest signal over green.
