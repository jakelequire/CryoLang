# Handoff — silent-mangle fallback + remaining mono gaps

**Branch:** `new-stdlib`
**Last verified:** selfhost-check passes; pin (`bin/cryo`) is current and was
built by the self-hosted compiler (the C++ bootstrap is archived and no
longer participates in the chain). All bootstrap-bug workarounds you may
see in source (`HashMap<string,…>` notes, cloner kind-switching,
substituter `memcpy`, etc.) are dead weight from the bootstrap era and
are no longer load-bearing — leave them for now, focus on the bugs below.

## Scope of this handoff

Two compiler-mechanics bugs from the pre-0.1.0 audit. Both are silent —
they produce link-time failures or wrong code with no compile-time
diagnostic. Fixing them in the order below is recommended because **(1)
turns (2)'s symptoms into compile-time errors with usable stack traces**
instead of cryptic linker `undefined reference` lines.

1. **`encode_type_ref` returns `"v"` for invalid TypeRefs** — silent
   miscompile generator. Fix first.
2. **Static-method ScopeResolution on non-generic owners** isn't reached
   by the monomorphizer's inference paths. Together with a related
   trait-method H-binding bug, this is why a minimal HTTP server still
   fails to link. Fix second.

Stay out of `README.md`, `CHANGELOG.md`, `docs/cryo.md`, `.github/`, and
the `tools/` tree — those are owned by the user for a separate cleanup
pass.

---

## Bug 1 — `encode_type_ref` silent `"v"` fallback

### What's wrong

`compiler/src/compiler/resolver/mangled_name.cryo:575` — `encode_type_ref`
falls back to returning `"v"` (the spec's primitive code for `void`)
when the TypeRef it's handed isn't valid:

```
function encode_type_ref(table: InternTable*, arena: TypeArena*,
                          ty: TypeRef) -> string {
    if (!ty.is_valid()) { return "v"; }
    ...
}
```

`encode_type` at `:584` has the same shape — when it bottoms out on a
type-kind it doesn't know how to encode, it returns `"v"` too.

This is wrong because:

- A method with a broken or unresolved parameter type produces a
  syntactically-valid mangled symbol that **collides with any genuine
  `void`-param method**.
- It hides upstream bugs. The handoff's `Rv` / `1R` / `1W` / `1H`
  placeholders in mangled symbols are leaks of `GenericParam`s that
  weren't substituted before mangling — but the user only finds out at
  link time, far from the source location that produced the bad mangle.
- Any future refactor that introduces a new `TypeKind` and forgets to
  add a mangle case silently emits `void` instead of failing loudly.

There's a related comment on the GenericParam encoding path at `:660`
that says it deliberately emits `N$L<param_name>$G` "so any leaks are
easy to spot in IR rather than a silent collision" — exactly the right
instinct, but `encode_type_ref` itself short-circuits before that path
ever runs when the TypeRef is invalid.

### What to change

In `compiler/src/compiler/resolver/mangled_name.cryo`:

1. **Replace the `"v"` fallback in `encode_type_ref`** (`:577`) with a
   diagnostic abort. Plumb `DiagnosticSink*` (or `CompilationContext*`)
   into the mangler if it doesn't already have one — or, if you'd
   rather not change the signature now, use `panic(...)` from
   `core::panic` with a descriptive message. Either is fine for this
   stage; the rule is "fail loudly, don't silently emit `void`."
2. **Replace the `"v"` fallback in `encode_type`** (`:584`'s default
   arm) the same way. Note that `Never → 'v'` (line 592) is
   intentional and should stay — that's a deliberate ABI-level
   collapse, not a fallback.
3. **At the `GenericParam` / `BoundedParam` arm** (`:660`), keep the
   current "emit name as N$L…$G" behavior as a leak-detector, but also
   call out a diagnostic. The leak-detector encoding stays in place so
   any in-flight links don't mysteriously change behavior; the
   diagnostic is what tells the developer where the unresolved param
   came from.

Suggested diagnostic: a new code (e.g., `E0901_MANGLE_UNRESOLVED_TYPE`
or reuse `E0900_INTERNAL_COMPILER_ERROR`) with text like:

```
internal compiler error: cannot mangle <kind> type
  --> <span if available>
   |
   | <call-site or decl source>
   |
   = note: this almost always means a generic parameter was not
           substituted before mangling. The mangler emitted a
           placeholder symbol; check the upstream specialization /
           inference path that produced this declaration.
```

`encode_type_ref` doesn't currently take a span. Easiest path: pull it
from the `Type*` itself if available, or take a `SourceSpan` argument
at every callsite (there are ~30; tractable). Failing that, emit the
diagnostic without a span — it's still strictly more useful than the
silent fallback.

### How to verify

Re-run `make selfhost-check`. With the abort wired in, the chain
should still pass byte-identity if all real callers are well-formed.
If any stage of the chain trips the abort, that is the signal that
Bug 2 (or some other unresolved-type bug) is firing — root-cause it
rather than restoring the fallback.

### Useful entry points

- `mangled_name.cryo:575` — `encode_type_ref`, the function to change
- `mangled_name.cryo:584` — `encode_type`, same kind of fallback
- `mangled_name.cryo:660` — `GenericParam` / `BoundedParam` encoding
  (the leak-detector path that should also fire a diagnostic)
- `mangled_name.cryo:592` — `Never → 'v'` (intentional, leave alone)
- `compiler/src/compiler/diag/diagnostic.cryo` — `Diagnostic::error`
- `compiler/src/compiler/diag/_module.cryo` — error code registry

---

## Bug 2 — generic-method-on-non-generic-owner: remaining gaps

### What's already fixed

Since the previous handoff, the **instance-method** lookup path was
added: `find_inherent_method` exists at
`compiler/src/compiler/types/monomorphizer.cryo:2180` and is wired into
`try_infer_method_call` at `:2348`. Inherent owners are registered by
`register_inherent_owner_if_has_generic_methods` in
`compiler/src/compiler/passes/specialization.cryo:182`, which runs over
struct/class declarations during specialization. Calls of the form
`obj.method(args)` where `obj`'s type is a non-generic struct/class
with inline generic methods now specialize correctly.

This means `req.method(...)` for `Request`/`Response` works.

### What's still broken

Three concrete symptoms remain. They share the same root family but
hit different inference paths.

#### 2a. Static `Owner::method(args)` on non-generic owners

`try_infer_function_call` at `monomorphizer.cryo:1770` handles
ScopeResolution callees in the branch starting around `:1790`. After
the qualified-name lookup misses, it falls through to:

- `find_function_template_for_call` (`:1726`) — only finds free
  functions in the template registry.
- `try_infer_static_method_on_generic_template` (`:1976`) — only
  triggers when the scope resolves to a **generic** template (e.g.
  `Slice<T>`); it explicitly returns false otherwise.

Neither covers a static method on a non-generic struct/class. So calls
like:

```cryo
Response::text(StatusCode::ok(), Str::from_raw("hi" as u8*, 2))
```

— where `Response` is non-generic and `text` is a static method —
fall through every inference path and reach codegen with a null
`resolved_callee`. ir_generator then builds the symbol from the
template's mangled form, which contains placeholder generic-param
encodings. Linker fails.

**Fix direction:** mirror `find_inherent_method`'s shape for static
calls. After the existing fallthrough at `:1871` (the gate that calls
`try_infer_static_method_on_generic_template`), add a sibling that:

1. Resolves `qualified_scope` (or `callee_scope` if not qualified)
   against the type registry — looking for a non-generic struct or
   class declaration.
2. If found, walks `inherent_owner` registration in the
   GenericRegistry (the same storage `find_inherent_method` uses) and
   selects the method by leaf name.
3. Walks the method's `func.parameters` for inference (no implicit
   receiver, since it's a static call) — the existing per-arg
   unification loop in `try_infer_function_call` should be reusable
   with minor tweaks.
4. Calls `specialize_method` (`:2503`) to clone+substitute the body,
   pin the spec'd callee symbol on the call node, and append the
   spec'd sibling to the owner's `methods[]`.

The instance-method path already does this end-to-end; the static
path needs the same plumbing for `is_method_call=false`.

#### 2b. `String<GA>::hash<H>` H-binding leak

This one is shape-different and more involved. `String<A>` is a
*generic* type, so `String<GlobalAlloc>` lives in `spec_entries`. Its
inherent `hash<H>(...)` method ought to spec through
`find_spec_impl_method`. But the emitted symbol contains `Rv` where
`&DefaultHasher` should be — i.e. `H`'s `resolved_type` reaches the
mangler as `Reference(invalid)` instead of `Reference(GenericParam(H))`
or `Reference(DefaultHasher)`.

The `Rv` encoding originates at `mangled_name.cryo:577` — once Bug 1
is fixed, this symptom converts to a compile-time abort with a stack
trace pointing at the spec'd method declaration. That should make the
upstream cause traceable.

Hypothesis (from the audit): the trait-method template's resolution
context never binds `H`. Either:

- The template's `H` GenericParam is registered with no resolution
  context, so its `resolved_type` stays `invalid`.
- `find_spec_impl_method` at `:2147` finds `String<GA>`'s `hash<H>`
  but doesn't fold `H` into its substitution — it only substitutes
  the owner's `A` param, leaving the method's own generic params
  unbound.
- `specialize_method` at `:2503` (the per-method specializer) needs
  to allocate and bind the method's own generic params *before*
  invoking the substituter on the body. Look for whether the
  current code does this when `original.func.generic_params.length
  > 0` — it likely doesn't.

This is the right order to investigate: with Bug 1's abort firing,
trace from the abort site upward into the substituter / specializer
pair to find the unbound H.

#### 2c. After-fix smoke

Once 2a + 2b are fixed, the headline check is:

```bash
cd /home/phock/Programming/apps/CryoLang/examples/http-server
/home/phock/Programming/apps/CryoLang/bin/cryo build
./build/bin/http-server   # should listen on :8080
```

This example exercises every codepath in the family:
- instance methods on non-generic owners (`req.drop()`, `body.push_slice(…)`)
- static methods on non-generic owners (`Response::text(…)`, `StatusCode::ok()`)
- generic methods on generic spec'd types (`String<GA>` chain)
- the router's borrowed `Str` patterns

If the link succeeds and the server accepts a request, both 2a and
2b are fixed. If linker errors remain, the symbol names point at
which gap is still open.

### Useful entry points

- `monomorphizer.cryo:1770` — `try_infer_function_call` (extend with
  the static-non-generic-owner path)
- `monomorphizer.cryo:1790-1881` — the ScopeResolution branch where
  the fallthrough chain currently lives
- `monomorphizer.cryo:2180` — `find_inherent_method` (template for the
  static-method walker; instance shape, adapt to no-receiver)
- `monomorphizer.cryo:2147` — `find_spec_impl_method` (where the
  String<GA>::hash<H> spec is supposed to be found)
- `monomorphizer.cryo:2503` — `specialize_method` (where method-level
  generic params should be bound before body cloning)
- `compiler/src/compiler/passes/specialization.cryo:182` —
  `register_inherent_owner_if_has_generic_methods` (referenced for
  the registration shape; static-method path uses the same storage)
- `compiler/src/compiler/types/generic_registry.cryo:347` —
  `register_inherent_owner` (the storage write)
- `compiler/src/compiler/codegen/ir_generator.cryo:2175` — Strategy 0
  for instance-method codegen reads `node.resolved_method`. The
  static-call codegen has its own strategy ladder; once you pin
  `resolved_callee` on the CallExprNode (analogous to
  `resolved_method` for instance), Strategy 0 should fire and emit
  the correct mangled name.

---

## Suggested order of work

1. **Bug 1 first.** Replace the two `"v"` fallbacks with a diagnostic
   abort. Run `make selfhost-check`. If it passes, Bug 1 is landed
   and the codebase is now self-checking against silent mangle leaks.
2. **Smoke the http-server example.** Run the verify command in §2c.
   The abort from Bug 1 will likely fire during the build — the stack
   trace and the diagnostic's source location are now your map for
   Bug 2.
3. **Fix Bug 2a** (static ScopeResolution path). After this, instance
   and static calls on non-generic owners both spec correctly; the
   only remaining symptom should be `String<GA>::hash<H>`.
4. **Fix Bug 2b** (H-binding). After this, http-server links.
5. **Re-run `make selfhost-check`.** It must still pass byte-identity.
6. **Optional but recommended:** add e2e tests under
   `legacy/bootstrap/tests/e2e/tier3_generics/` for:
   - `t02xx_static_method_non_generic_owner.cryo` — `Foo::bar<T>(x)`
     where `Foo` is non-generic.
   - `t02xx_inherent_generic_method.cryo` — `obj.bar<T>(x)` where
     receiver is non-generic.
   - `t02xx_generic_method_on_spec_owner.cryo` — exercises the
     `String<GA>::hash<H>` shape.
   These regressions all hit the same family and have no test
   coverage today.

---

## Things NOT to do

- **Don't restore the `"v"` fallback** if the abort fires during
  selfhost-check. Every legitimate firing is a real upstream bug. Fix
  the upstream, not the abort.
- **Don't re-introduce printf-based debug output**. The codebase is
  swept to `cdebug` (gated on `--debug` / `cryoconfig` `debug = true`)
  and `Utils::Logger`. New diagnostic prints should use those.
- **Don't rename or reshape any stdlib API to work around the bug.**
  `Request::parse<R>`, `Response::write_to<W>`, `String::hash<H>`
  are deliberately polymorphic. The compiler is what's broken.
- **Don't touch the bootstrap-era workaround comments yet.** Things
  like "do NOT use `==` on `void*`", "the C++ Cryo compiler can't
  deref this struct", chained-string-concat avoidance, cloner
  kind-switching in `AST/cloner.cryo:128-160`, and substituter
  `memcpy` in `AST/substituter.cryo` are all dead weight from the
  bootstrap era. They no longer affect correctness, but cleaning
  them up is its own pass and out of scope here.
- **Don't extend `examples/http-server`** to work around the link
  failure. The example is the test for the fix.
- **Don't skip `selfhost-check`.** It's the gate.

---

## How to know you're done

- `make selfhost-check` passes (stage-3 IR == stage-4 IR byte-identical).
- `cd examples/http-server && ../../bin/cryo build` produces a binary.
- That binary, when run, accepts a request on `127.0.0.1:8080` and
  returns the expected response (try `curl -i http://127.0.0.1:8080/`
  and `curl -i http://127.0.0.1:8080/health`).
- The `encode_type_ref` / `encode_type` `"v"` fallbacks are gone
  from `compiler/src/compiler/resolver/mangled_name.cryo`.
- No code path in the compiler can produce a mangled symbol
  containing a `GenericParam`-name leak without firing a diagnostic
  first.

Good luck.
