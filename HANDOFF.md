# HANDOFF — Iterator combinator chaining (CHANGELOG known-limitation)

**Date:** 2026-06-06
**Goal for this task:** Get the *"Iterator combinators are partial"* bullet OFF the
known-limitations list in `CHANGELOG.md` (lines ~184–200) before v1.0.0.

The user (Jake) explicitly scoped today's work to **"Fix chaining first"** — i.e.
fix the *combinator-following-combinator* failures (the "type-mismatch
diagnostic" part of the bullet). The `zip`/`chain`/`enumerate`/`collect`
features ("not yet implemented") are **deferred to a follow-up** and are NOT in
scope here.

This is the single most intricate area of the compiler
(`compiler/src/compiler/types/monomorphizer.cryo`, ~6400 lines, dozens of prior
surgical fixes). Tread carefully and verify with selfhost + the full test suite.

---

## TL;DR status

- **Bug 2 (`r.map(f).map(g)` etc.) — FIXED** via a stdlib rename in
  `stdlib/core/iter.cryo` (`MapIter<I,A,B>` → `MapIter<I,A,O>`). This is the
  ONLY change currently in the working tree. **Not yet fully verified** (the
  verification build was interrupted — see "Verification still owed").
- **Bug 1 (`r.take(n).map(f)`, `r.take(n).filter(p)`) — NOT FIXED.** Root cause
  is fully understood (below). My first fix attempt worked functionally but
  **crashed the compiler** (SIGABRT) on a re-entrancy path, so I reverted it.
  A safer approach is described in "Next steps for Bug 1."
- Working tree: `git status` shows only `stdlib/core/iter.cryo` modified.
  The compiler source is back to **baseline** (all my debug instrumentation and
  the failed Bug-1 helper were removed). Nothing committed. Pin NOT refreshed.

---

## The real failing matrix (this is more nuanced than the CHANGELOG wording)

Test shape: a user `Rng` struct implementing `Iterator<i32>`, then
`r.<inner>.<outer>.count()`. Results **at baseline** (pinned `bin/cryo`):

```
inner ↓  \  outer →   .take(n)   .map(f)   .filter(p)
.take(n)               OK         FAIL       FAIL      <- Bug 1
.map(f)                OK         FAIL       OK        <- Bug 2 (the map.map cell)
.filter(p)             OK         OK         OK
```

**Governing rule:** `.take` as the OUTER combinator always works because
`TakeIter<This>` only references `This`, never the element type. The failures
are exactly the cells where the OUTER combinator (`map`/`filter`) must recover
the receiver adapter's **element type (`Item`)**, and that recovery fails for
**two independent reasons**:

- **Bug 1** — `Item` can't be recovered from a `TakeIter` receiver (breaks
  `take.map`, `take.filter`).
- **Bug 2** — name collision specific to `map` on a `MapIter` receiver (breaks
  `map.map` only; `map.filter` works because `filter` has no own generic).

There is extensive existing test coverage and a written description of this
limitation in **`tests/tests/stdlib/iter.cryo`** (see the comment block around
lines 461–474 and the tests below it). The CHANGELOG bullet to remove is in
**`CHANGELOG.md`** ~lines 184–200.

---

## Bug 2 — name collision (FIXED, needs verification)

### Root cause (fully confirmed)
The `map<B>` trait default in `stdlib/core/iter.cryo` returns
`MapIter<This, Item, B>`. The adapter was declared `MapIter<I, A, B>` — its
**third struct param was also named `B`**.

When `map` is applied to a `MapIter<...>` receiver (`r.map(f).map(g)`), the
inherited `map` default is materialized onto `MapIter`'s impl, and
`ASTTypeSubstituter` (run during impl specialization with the struct's
`param_names = [I, A, B]`) rewrites **every** `B` in the cloned default —
**including `map`'s own generic `<B>`** — to the receiver's concrete element
type. So `map`'s method generic `B` is never an inferable generic anymore; the
formal becomes `(i32) -> i32` instead of `(i32) -> B`, unification never binds
the method generic, `m_all_bound` fails, and `try_infer_method_call` bails
before specialization → codegen sees the unsubstituted `MapIter<This,Item,B>`
template → **E0636 "no method 'count' found"**.

This is why `filter.map` works (FilterIter has no `B` param) but `map.map`
doesn't, even though both receivers carry their element as a struct param.

### Fix applied
Renamed `MapIter`'s result-type param `B` → `O` (Output) so it can't collide
with the `map<B>` default's own generic. Positional, so the default
`MapIter<This, Item, B>` still maps correctly (`B` → the 3rd param `O`). See the
diff in `stdlib/core/iter.cryo`. After this, `map.map` compiled and (via a
later all-fixes build) produced the right count value.

### Why this is the right fix (vs a compiler fix)
The proper-but-risky compiler fix would be to make `ASTTypeSubstituter`
scope-aware so a struct param doesn't rewrite a nested method's own generic of
the same name. That's invasive and dangerous for selfhost. The stdlib rename is
safe and sufficient for the *shipped* combinators (the only thing the CHANGELOG
limitation is about). **Worth a one-line note** that a user-defined adapter with
a colliding param name would still hit this — acceptable for v1.0.0.

---

## Bug 1 — `Item` not recoverable from a `TakeIter` receiver (NOT FIXED)

### Root cause (fully confirmed via instrumentation)
`TakeIter<I>` carries its element type **only in the where-clause**
(`implement<I,A> Iterator<A> for TakeIter<I> where I: Iterator<A>`), not as a
struct param. Struct-param-element adapters (`FilterIter<I,A>`,
`MapIter<I,A,O>`) get their element substituted to a concrete type by
`ASTTypeSubstituter` during impl spec (so the default's `Item`/impl-param
annotation is cached as concrete in `Named.pre_resolved`). `TakeIter`'s element
param (`A`) is **not** in the struct's `param_names`, so it's never substituted
and stays an unbound `Named("A")`.

Concretely: when `filter`/`map` is lazily specialized for a `TakeIter<Rng>`
receiver (`specialize_method`), resolving the signature
`FilterIter<This, Item>` / `(Item) -> B` produces an **invalid** TypeRef for the
`Item` slot. The "refuse to hand back a spec whose signature didn't fully
resolve" guard then returns null → method "vanishes" → E0636.

**Key subtlety discovered the hard way:** the default's element reference in the
cloned signature is named after the **impl's** trait param (`A` for `TakeIter`,
because the impl is written `Iterator<A>`), **NOT** the trait *decl*'s param
name (`Item`). Binding `Item` alone does nothing; you must bind `A`. (For
`MapIter` it's `O`, already concrete, so no-op.)

### What I tried, and exactly why it crashed (DO NOT repeat naively)
I added a helper `bind_receiver_trait_params(owner_type, orig_func, res_ctx)`
that derived the receiver's concrete trait args via
**`concrete_trait_args_for(owner_type, "Iterator")`** and bound both the trait
decl's param name (`Item`) and the impl's trait-arg name (`A`) into the
resolution context, called from `specialize_method` and the inference
`method_ctx`.

**It worked functionally** — `take.map`/`take.filter` all compiled — BUT it
**crashed the compiler with SIGABRT** during the build of even previously-
working programs. Backtrace:

```
TypeResolver::resolve  -> abort
  <- concrete_trait_args_for
    <- derive_impl_where_generics
      <- concrete_trait_args_for
        <- bind_receiver_trait_params
          <- specialize_method
            <- try_instantiate_self_returning_default
              <- try_infer_method_call  (during monomorphization)
```

**Why:** `concrete_trait_args_for` / `derive_impl_where_generics` call back into
`this.type_resolver.resolve(...)` and mutate shared monomorphizer/resolver
state. Calling them **re-entrantly from inside `specialize_method`** (which is
itself deep inside a monomorphization resolve) corrupts that shared state /
re-enters the resolver in a context it doesn't tolerate → abort. This machinery
was designed to run during `resolve_specialized_ast` (eager impl resolution,
see `monomorphizer.cryo` ~lines 1288–1290), **not** from the lazy
method-specialization hot path.

I reverted the helper and both call sites entirely.

### CRITICAL testing gotcha that hid this crash
My first "all 9 OK" matrix run was **WRONG**. I used `cryo run` + `grep error[`.
That misses crashes: a SIGABRT (exit 134) prints **no `error[` diagnostic**, and
`cryo run` may show output from a **stale binary** from a previous iteration.
**Always**:
1. `rm -f build/bin/<proj>` first,
2. run **`cryo build`** and check **`$?`** (134 = SIGABRT/crash, non-zero = diag
   or crash, 0 = success),
3. only then run the binary explicitly and check the **value**.

A clean failure should be a diagnostic (E0636), never exit 134. Treat any
SIGABRT as a hard regression.

---

## Next steps for Bug 1 (ideas, in rough order of safety)

The fix must bind the receiver adapter's element type (`A`/`Item`) into the
resolution context used by `specialize_method` (for `filter`, the zero-generic
path) **and** the inference `method_ctx` (for `map`, the own-generic path) —
**without** re-entering `concrete_trait_args_for` from the hot path.

1. **Cache the element type at impl-spec time (preferred).** When a `TakeIter`
   (or any where-clause-element adapter) is specialized, the eager path
   (`resolve_specialized_ast` → `derive_impl_where_generics`, ~line 1288)
   **already computes `A → i32`** into its `res_ctx`. Persist that mapping
   somewhere cheap keyed by the spec'd type (e.g. on the spec entry / a side
   table `spec_type_id -> [trait_arg TypeRefs]`). Then `specialize_method`
   binds the impl's trait-arg names from that cached table — a pure lookup, no
   resolver re-entry, no recursion. This mirrors how struct-param adapters get
   their element "for free."

2. **Pre-resolve the skipped self-returning defaults' element annotations at
   impl-spec time.** At ~line 1314 the defaults are `continue`d (skipped)
   because resolving their *return* (`TakeIter<This>`) recurses forever. But you
   can resolve **only the bare element-type `Named` references** in their
   param/return annotations using the `res_ctx` that already has `A → i32`,
   caching `pre_resolved` on those nodes — WITHOUT instantiating the adapter
   return type. Caveat: make sure each spec'd impl has its **own clone** of the
   default methods (per-instantiation), else you'd cache `i32` on a template
   annotation shared by `TakeIter<String>` etc. (corruption). Verify whether
   spec'd impls carry their own method clones before going this route.

3. **Compute the element cheaply inline** (no resolver): the element of
   `TakeIter<Inner>` is the element of `Inner`. Walk the receiver's type args to
   the inner iterator and read its already-resolved Iterator element from a
   cached table (same table as idea 1). Avoid `concrete_trait_args_for`.

Whatever you do: it must be a no-op for non-iterator trait methods (`hash<H>`,
`fmt<W>`, `clone`, …) — the broad version broke those. Gate tightly.

Relevant code landmarks in `monomorphizer.cryo`:
- `try_infer_method_call` (~4900): entry; resolves receiver, dispatches.
- `try_instantiate_self_returning_default` (~4740): zero-own-generic combinators
  (`take`, `filter`) → calls `specialize_method`.
- the own-generic inference path (~5100–5340): `map<B>` etc.; builds `method_ctx`,
  unifies formal vs actual to bind the method generic, then `specialize_method`.
- `specialize_method` (~5390): clones default, substitutes method generics,
  `resolve_func_signature` with a `res_ctx` that today only has `this_type` set.
  This is where `Item`/`A` fails to resolve for `TakeIter`.
- `concrete_trait_args_for` (~1430) + `derive_impl_where_generics` (~1514) +
  `bind_where_arg_param` (~1556): the where-clause element-derivation machinery.
  Works, but **do not call re-entrantly from `specialize_method`** (crashes).
- `bind_trait_args_for_spec_impl` (~1380): binds trait-decl param names from the
  impl's (already concrete) trait annotation.
- Resolution: `TypeResolver::resolve_named` (resolver.cryo ~630) resolves a bare
  name ONLY via `ctx.lookup_binding` — there is no "consult this_type's trait
  impl" fallback. `resolve()` Named branch checks `n.pre_resolved` first
  (resolver.cryo ~160). `ResolutionContext::set_this_type` only sets `this_type`;
  it does NOT seed trait-param bindings.

---

## How to reproduce / test (repro project template)

A scratch project was at `/tmp/itp` (won't survive a machine change). Recreate:

`cryoconfig`:
```
[project]
project_name = "itp"
output_dir = "build"
target_type = "executable"
source_dir = "src"
entry_point = "src/main.cryo"
[compiler]
[dependencies]
```

`src/main.cryo` (vary the chained expression):
```
import std::core::iter;
import std::fmt;
type struct Rng { cur: i32; end: i32;
    static new(s: i32, e: i32) -> Rng { return Rng { cur: s, end: e }; } }
implement trait Iterator<i32> for struct Rng {
    next(mut &this) -> Option<i32> {
        if (this.cur >= this.end) { return Option::None; }
        const v: i32 = this.cur; this.cur = this.cur + 1; return Option::Some(v); } }
function dbl(x: i32) -> i32 { return x * 2; }
function isodd(x: i32) -> boolean { return x % 2 == 1; }
function add(a: i32, x: i32) -> i32 { return a + x; }
function main() -> i32 {
    mut r: Rng = Rng::new(0, 10);
    fmt::println("%d", r.take(5).map(dbl).count());   // <- vary this line
    return 0;
}
```

Build the compiler under test and run a case (exit-code aware!):
```
make cryo                        # ~83s; builds compiler/build/bin/cryo via pinned bin/cryo
STAGE2=$PWD/compiler/build/bin/cryo
cd /tmp/itp && rm -f build/bin/itp
"$STAGE2" build --stdlib=$OLDPWD/stdlib; echo "exit=$?"   # 134 = CRASH, !=0 = diag
./build/bin/itp                                            # check the value
```

Cryo syntax reminders (not Rust): use `function`, `const x: T =` / `mut x: T =`
(no `:=`), no `fn`. Debug logging: the compiler has `cdebug(fmt, args...)` gated
by `--debug` (`compiler/src/utils/logger.cryo`); args are only evaluated inside
the `if (g_compiler_debug)` you wrap around the call — `LOG_DEBUG` is a hard
no-op. `--debug` on the pinned `bin/cryo` shows nothing (stripped); you must
rebuild `make cryo` to get instrumentation.

Expected correct values for spot-checks (source `Rng::new(0,10)`):
- `r.take(5).map(dbl).count()` → 5
- `r.take(5).map(dbl).fold(0, add)` → 20  (2*(0+1+2+3+4))
- `r.take(5).filter(isodd).count()` → 3
- `r.map(dbl).map(dbl).count()` → 10
- `r.map(dbl).filter(isodd).count()` → 0  (all even after dbl)

---

## Verification still owed (before this can be called done)

1. **Re-run the full 3×3 matrix with the exit-code-aware method** on the current
   tree (Bug-2-only). Confirm `map.map`/`map.filter` produce correct **values**
   and no SIGABRT. (The verifying build was interrupted; treat as unverified.)
2. **`make selfhost-check`** must pass (ideally byte-identical IR md5; a stdlib
   change may legitimately shift it since the stdlib is part of the build —
   confirm it still reaches a fixed point, 6/6).
3. **`make test`** — full unit + compile-fail suite must stay green (was ~1017
   unit / 89 compile-fail per recent memory). NOTE: `make test` uses a STALE
   stage2 unless you `make cryo` / `make selfhost-check` first.
4. Add **regression tests** to `tests/tests/stdlib/iter.cryo` for the newly
   working chains (`map.map`, and `take.map`/`take.filter` once Bug 1 lands).
   There's already a comment block there documenting these as NOT-yet-supported
   (~lines 461–474) — update it.
5. **CHANGELOG.md** (~lines 184–200): once chaining is fully fixed, rewrite/trim
   the bullet. If only Bug 2 lands (map.map) and Bug 1 stays open, the bullet
   must be **re-scoped honestly** (still mention `take.map`/`take.filter` as the
   remaining gap) — do NOT remove the bullet while `take.*` still fails.
6. `make pin-cryo` + commit are the **user's** actions (Jake commits on `main`,
   no feature branch). Don't refresh the pin or commit unless asked.

---

## Honest assessment

- Bug 2 is a clean, low-risk win (stdlib-only). High confidence.
- Bug 1 is real but fiddly; the obvious fix re-enters the resolver and crashes.
  The cached-element-at-impl-spec-time approach (idea 1) is the most promising
  safe route but needs care. Budget real time for it.
- Fully clearing the CHANGELOG bullet ALSO requires `zip`/`chain`/`enumerate`/
  `collect` (deferred). Until those land too, the bullet can at most be
  *narrowed*, not deleted. Set expectations accordingly.
