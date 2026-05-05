# Handoff — remaining mangle-fallback warnings (279 stdlib / 1504 compiler)

**Branch:** `new-stdlib`
**Last verified:** `make selfhost-check` passes 6/6 stages, stage-3/stage-4
byte-identical (md5 stable across runs). Pin (`bin/cryo`) was just
refreshed (commit `597e81d1`). `examples/http-server` still builds and
serves real HTTP responses on `127.0.0.1:8080`.

## Scope of this handoff

Continue closing out `encode_type_ref` / `encode_type` "v"-fallback
warnings. The original Bug 1 ("silent `v` fallback") was wired to a
loud `printf+fflush+abort` panic, then downgraded to a `printf+'v'`
warning (still loud, but build-survivable) so we could ship
`make selfhost-check` without blocking on the deeper resolution gaps
that the panic surfaces.

Each remaining warning indicates a real upstream resolution gap — a
spec'd type/method whose `param.resolved_type` or
`func.resolved_return_type` is left at `TypeRef::invalid()` (id=0) by
the time the mangler walks it. Symbols emitted with `v` placeholders
are junk that no other module defines, so any cross-module call that
hits one would be a link-time failure. We need them down to zero.

**Do not** revert these warnings to silent. **Do not** "drop and work
around till later." Per the user's standing direction: when the panic
fires, root-cause it; when a warning fires, root-cause that too.

## Current state — what's already landed (read these commits in order)

```
597e81d1 fix: bind trait outer generics during spec'd-impl method resolution
763546b3 fix: skip DI registration of bounds-disabled spec methods
816666fb fix: skip DI registration of bounds-disabled spec impls
8e10f5c6 fix: bind trait outer generics for impl-block default-method resolution
59b98668 fix: align FQN/bare mangle for primitive impls; refresh pin to consistent state
```

Together these took the warning count from 588 stdlib / 2125 compiler
down to 279 / 1504 (~53% / ~29% reductions). Each commit:
- Fixes one specific upstream resolution gap.
- Adds doc comments on the affected function explaining the failure
  mode and the design intent.
- Refreshes the pin from a clean stage-2 build.
- Verified by `make selfhost-check`.

**Critical:** the pin (`bin/cryo`) at `597e81d1` is a working
self-hosting pin built from current source. If it ever stops working,
recover the previous pin from git via
`git show <prev-commit>:bin/cryo > bin/cryo && chmod +x bin/cryo`.
The pin BEFORE this campaign (committed at `2ad897e6`) had asymmetric
mangling baked in and could not self-host — do **not** restore that
one.

### What kind of bugs the existing fixes addressed

The pattern across all 5 commits: spec'd code paths in the
monomorphizer don't carry the same resolution context that the
non-spec FunctionSignature/TypeResolution passes do. Specifically:

1. **Trait outer generics didn't reach impl-block default-method
   resolution.** `Iterator<Item>::fold<Acc>` cloned into impls had
   `f: (Acc, Item) -> Acc` re-resolve with only `Acc` bound.
   Fix: `bind_trait_args_for_impl` in
   `compiler/src/compiler/passes/type_resolution.cryo:118-150`
   (called from both FunctionSignature and Phase 2 ImplementationBlock
   sites).
2. **Spec'd `ThisType` resolution didn't have `this_type` plumbed.**
   The receiver's `Reference(ThisType)` annotation had nowhere to
   resolve to. Fix: `sig_ctx.set_this_type(this_type)` in
   `monomorphizer.cryo:resolve_func_and_body`.
3. **Spec'd `&this` parameters had no annotation at all** (parser
   elides it). `resolve_func_signature` skipped them entirely. Fix:
   back-fill loop in `resolve_func_and_body` mirroring
   `resolver.cryo:308-317`'s back-fill in the non-spec path.
4. **Param fallback resolution.** Extended the existing return-type
   fallback (which re-resolves through a `gen_ctx` built from `subst`
   when initial resolution failed) to also re-resolve params.
5. **Impl-level `where`-bounds violations produced junk DI.** Added
   `ImplBlockNode.bounds_disabled: boolean` flag set by the
   monomorphizer; registration site (`specialization.cryo:run_generic_expression_resolution`)
   skips disabled impls.
6. **Method-level `where`-bounds (e.g. `Option::contains where T: Eq`).**
   Added `FunctionDeclNode.bounds_disabled: boolean` with parallel
   skip in `decl_index.cryo:register_methods_with_module_aliased`.
7. **Spec'd-impl resolution context didn't bind trait-level
   generics.** ASTTypeSubstituter only rewrites names in the impl's
   `param_names`; trait names like `Item` survive substitution. Fix:
   `bind_trait_args_for_spec_impl` in monomorphizer mirroring the
   TypeResolution helper.

## What's still firing (the open work)

**279 stdlib + 1504 compiler `encode_type_ref` warnings remain.**
They are reproducible and stable across selfhost-check rounds (every
stdlib build emits exactly 279, every compiler build exactly 1504).
The byte-identity gate is unaffected, so the fallback is at least
deterministic — but the underlying junk symbols are still being
pinned in DI.

### What I've ruled out

I instrumented `decl_index.cryo:register_methods_with_module_aliased`
to print `[reg-fail]` whenever a method reaches the registration site
with `func.resolved_return_type.id == 0` or any
`method_param_refs[i].id == 0`. **No reg-fail output fires** after
the 5 commits. So the failures are **not** at the top-level method
registration anymore. Every method that reaches that point has fully
resolved signatures.

The warnings instead come from **nested `encode_type_ref` calls
inside `encode_type`** for compound type kinds (Function, Tuple,
Optional, Reference, etc.). For example, encoding
`Function((Acc, Item) -> Acc)` walks into the function-type's
`param_types` array — even though the FunctionType's TypeRef is
itself valid, one of its internal params is invalid.

This means: an arena-allocated FunctionType (or other compound type)
contains an invalid TypeRef in its inner field. The compound type was
built via `arena.get_function(ret, params, …)` where one of `params`
was already invalid at construction time, and `arena.get_function`
doesn't validate.

### Next step — find the actual remaining call sites

The mechanical move is to add a per-call printf to encode_type_ref's
fallback path that includes the **caller's path/owner** (something
like the surrounding `mangle_with_path` path argument), then group
the output by path. I tried this once during this session and found
two patterns:

```
[mangle-trace] invalid params[0]/1, buf-so-far=''                    (18 hits)
[mangle-trace] invalid params[1]/2, buf-so-far='N$L3Acc$G_'         (6 hits)
```

But that was BEFORE the trait-args-for-spec-impl fix. The 24 hits
were at the top-level `encode_params`. That count is now likely
zero — the remaining warnings are nested deeper.

**Concrete diagnostic plan for the next agent:**

1. Edit `compiler/src/compiler/resolver/mangled_name.cryo`'s
   `encode_type_ref` invalid-fallback branch to take an extra `caller_label: string`
   parameter (default `""`) and print it alongside the warning.
2. At each call site of `encode_type_ref` inside `encode_type` (Pointer,
   Reference, Array, Function, Optional, Tuple, etc.), pass a label
   describing which compound type's inner slot is being encoded —
   e.g. `"Function.param[i]"`, `"Reference.referent"`,
   `"InstantiatedType.arg[i]"`. Same trick at call sites in
   `encode_params`, `encode_path_with_leaf_generics`,
   `encode_instantiated`.
3. Rebuild via `make cryo`, run `cd stdlib &&
   /workspaces/CryoLang/compiler/build/bin/cryo build --build-dir=.bin/self/s2`,
   `grep -c warning` should show 279.
4. The grouped output will tell you which compound type kind has the
   most invalid inner slots. That's where the upstream fix needs to
   be made.

**Hypotheses worth checking first** (each produces a distinct
signature so the diagnostic above will quickly disambiguate):

- **InstantiatedType type_args**: spec'd Option/Result/etc. have
  `InstantiatedType` references whose `type_args` array might
  contain invalid TypeRefs. `arena.lookup` on an
  InstantiatedType + recursing into `type_args` is a likely hit
  site.
- **FunctionType internal params on still-generic methods.** Even
  with the spec'd-impl trait-arg fix, the trait declaration's own
  `fold<Acc>` still has `f: (Acc, Item) -> Acc`. When the trait
  declaration's methods are mangled (e.g. for trait_impl table
  lookups), the function-type carries `Item` as a GenericParam.
  Recall: `Item` is a real GenericParam at trait-decl time (it's a
  trait generic). It SHOULD be encoded as `N$L4Item$G` (the
  `GenericParam` arm of `encode_type` at line 656-665), not
  produce an invalid-fallback. If invalids reach here, it's because
  the FunctionType's `param_types` slot is genuinely id=0 (not a
  GenericParam at id=N).
- **Body-cloned vs. signature-cloned discrepancy.** The cloner
  clears `resolved_type` on EVERY parameter. The substituter
  rewrites the annotation tree but doesn't touch `resolved_type`.
  Re-resolution depends on `resolve_func_signature` walking each
  param, which it does — but maybe one of the spec-time helpers
  (e.g. `find_inherent_method`, `specialize_method`) builds a
  FunctionType from a partially-resolved signature.

### Useful entry points

- `compiler/src/compiler/resolver/mangled_name.cryo` — the mangler.
  - `encode_type_ref:586-606` — the warning fires here.
  - `encode_type` — recurses into compound kinds; each `encode_type_ref`
    call inside is a candidate caller site to instrument.
  - `encode_params:560-571`, `mangle_with_path:441-470`,
    `encode_instantiated:739-757` — top-level callers.
- `compiler/src/compiler/types/monomorphizer.cryo`
  - `resolve_specialized_ast:827-895` — the spec'd-AST resolution
    dispatcher (one branch per node kind).
  - `resolve_methods:908-958` — per-method dispatch.
  - `resolve_func_and_body:1133-1240` — the workhorse for spec'd
    method signature resolution. Already has trait-arg, this_type,
    &this back-fill, and param-fallback logic. Look here first if
    a remaining warning maps to a spec'd-method param.
  - `bind_trait_args_for_spec_impl:973-1030` — the helper added in
    `597e81d1` that binds trait outer generics for spec'd impls.
- `compiler/src/compiler/decl_index.cryo`
  - `register_methods_with_module_aliased:382-540` — registration
    site for impl/struct methods. The `bounds_disabled` skip is at
    the top of the loop (line 391-396).
- `compiler/src/compiler/passes/specialization.cryo`
  - `run_generic_expression_resolution:582+` — injects spec'd ASTs
    and registers them. The impl-level `bounds_disabled` skip is at
    line 707-720.

## Verification commands

```bash
# Full byte-identity gate. Must pass 6/6 stages with stable IR md5.
make selfhost-check

# Per-stage warning counts (run after a successful selfhost-check).
for f in build-logs/selfhost-check/stage-*.log; do
    echo "$f: $(grep -c warning $f) warnings"
done
# Currently (commit 597e81d1):
#   stage-01.log: 279 warnings  (pinned-stdlib build)
#   stage-02.log: 1504 warnings (pinned-compiler build)
#   stage-03.log: 279 warnings  (stage-2 → stdlib)
#   stage-04.log: 1504 warnings (stage-2 → compiler)
#   stage-05.log: 279 warnings  (stage-3 → stdlib)
#   stage-06.log: 1504 warnings (stage-3 → compiler)

# Smoke the http-server example.
cd examples/http-server
/workspaces/CryoLang/bin/cryo build
./build/bin/http-server &
SERVER=$!; sleep 1
curl -s -i http://127.0.0.1:8080/        # 200 "Hello, World!"
curl -s -i http://127.0.0.1:8080/health  # 200 application/json
kill $SERVER
```

After landing each fix:
1. Run `make selfhost-check`.
2. If all stages green and warnings drop, run `make pin-cryo` to
   refresh the pin.
3. Commit binary + source together with a focused message describing
   the resolution gap that closed.

## Things NOT to do

- **Do not revert encode_type_ref's warning to silent.** The
  printf+fflush+'v' is the correct shape — it makes the fallback
  visible. The right move is to drive the warning count to zero by
  fixing each upstream gap.
- **Do not "drop the body and continue" without flagging.** That
  pattern was rejected mid-session: dropping a method body but
  leaving its junk signature in DI is what produces the bulk of the
  warnings. If a method/impl can't be specialized, set
  `bounds_disabled = true` (or add an analogous flag) so registration
  skips it.
- **Do not bypass `make selfhost-check`.** It's the only gate that
  actually proves a fix is coherent. Manual stage-2 invocations are
  brittle (different stdout buffering, missing pre-created directories,
  etc.) and lulled me into a false sense of progress mid-session.
- **Do not refresh the pin from a stage-2 you haven't verified end-to-end.**
  An asymmetric pin can compile its own source successfully but
  produce binaries that no longer self-host. The campaign that
  produced `8e10f5c6` had to recover the OLD `42ac3364` pin from git
  to break out of that.
- **Don't add diagnostic printfs to source files and commit them.** I
  cleaned several up over the session; if you add tracing, do it as
  unstaged-and-revertible scratch.
- **Don't reach for `--debug` for diagnosis through `make selfhost-check`** —
  the script doesn't pass it through, and repeated stage runs muddle
  the buffered output. Prefer adding a focused `printf+fflush(null)`
  at the suspected site, then `make selfhost-check`, then `grep` the
  per-stage logs.

## How to know you're done

- `make selfhost-check` passes 6/6 with stage-3 IR == stage-4 IR
  byte-identical (already true).
- `grep -c warning` on every stage log returns **0**.
- `examples/http-server` still builds and serves requests.
- `bin/cryo` has been refreshed from the final fix's stage-2.
- The encode_type_ref / encode_type "v"-fallback printf can be
  swapped back to a hard `panic(...)` (per the original Bug 1 design)
  without breaking selfhost-check. That's the real success criterion:
  the mangler should never see an invalid TypeRef.

Good luck. The remaining work is mechanical — instrument, group,
identify the failing recursion site, fix the upstream binding gap.
The patterns from the 7 fixes already landed are templates for the
fixes that will close the rest.
