# Handoff — `for x in iter` desugaring

**Status (2026-05-28):** in flight, uncommitted. Last edit applied but **not rebuilt or tested**. Continue from "Next step" below.

> Delete this file before tagging v1.0.0 — the 2026-05-27 audit flagged
> root-level `HANDOFF.md` as a ship-blocker.

---

## Goal

Audit finding **B4** (v1-audit-2026-05-27-pm): the parser accepted `for (x in expr) body` and built a malformed `ForStmtNode` where the collection sat in the *condition* slot — no `Iterator::next` desugaring. `docs/cryo.md §21` claimed it was "reserved", but the parser accepted it and silently miscompiled. "Either error E0XXX at parse time, or actually wire it to `Iterator::next`."

Pick: **wire it to `Iterator::next`.**

Owner instruction at the start of the session, verbatim: *"do things the right way, fix this compiler if needed."* No workarounds. If we hit a separate compiler bug, fix the compiler.

---

## Design — the desugaring

`for (x in <expr>) <body>` lowers (at parse time) to:

```cryo
{
    mut __cryo_for_iter_N = <expr>;
    loop {
        match (__cryo_for_iter_N.next()) {
            Option::Some(x) => { <body> }
            Option::None    => { break; }
        }
    }
}
```

Properties:
- `<expr>` is evaluated **exactly once** (it's the binding's initializer).
- The iterator binding is `mut` because `Iterator::next` takes `mut &this`.
- `N` is a per-call-site counter (`g_for_in_counter`) so nested / sibling
  loops don't alias.
- `break` / `continue` / `return` inside `<body>` work as expected — they
  bind to the synthesized `loop`, not the user's call site.
- Drop of `__cryo_for_iter_N` is handled by the existing scope-exit drop
  synthesis on the outer block; no new drop logic was required.
- The `Some(x)` pattern binding makes `x` available inside the body via
  the existing pattern-binding scope rules.

The iterator binding's type comes from the **initializer expression**,
not from a user-written annotation. The plumbing for that is the
trickier part (see "The hard bit" below).

---

## What's been changed

```
compiler/src/compiler/parser/expr_parser.cryo     (+7)
compiler/src/compiler/parser/parser.cryo          (+138, -8)
compiler/src/compiler/passes/sema.cryo            (+19, -2)
compiler/src/compiler/passes/type_resolution.cryo (+40, -6)
compiler/src/compiler/types/monomorphizer.cryo    (+85, -3)
tests/tests/lang/for_in.cryo                       (new, 217 LOC)
```

### 1. Parser (`parser.cryo` + `expr_parser.cryo`)

- Added a global counter `g_for_in_counter: u32` next to the existing
  `g_coalesce_counter` in `expr_parser.cryo`.
- Rewrote the for-in branch of `parse_for_statement` in `parser.cryo`
  (around line 2491) to call a new `build_for_in_desugar(...)` helper.
- The helper synthesizes the block-loop-match shape described above
  using `BlockStmtNode`, `LoopStmtNode`, `MatchStmtNode`, `MatchArmNode`
  + `EnumPatternNode` (`Option::Some(x)` / `Option::None`), and a
  `BreakStmtNode` in the None arm. Pattern follows the spaceship-op
  desugar at `expr_parser.cryo:266–287` and the `??` desugar at
  `expr_parser.cryo:177–215`.
- The synthesized iterator binding sets `is_mutable = true` and
  `is_auto = true`. The latter is a previously-dormant flag on
  `VarDeclNode` that now means "this binding's type comes from its
  initializer". See sema/mono changes below.

### 2. Sema (`sema.cryo`)

Two small carve-outs:

- **Around line 1681:** the "needs a `: T` annotation" check (E0104)
  now also skips when `var_node.is_auto`. The same gate already
  excused `has_resolved_type()` for closure-shadow bindings and
  drop-flag locals; `is_auto` joins them as "compiler-synthesised,
  don't yell at the user."
- **Around line 1751:** the existing back-fill that copies
  `init_type` into `var_node.resolved_type` for lambda-bound `const f
  = (...) -> ... { }` now also fires for `is_auto`. This is the path
  that pins the binding's type once the initializer is resolved.

### 3. Type resolution Phase 3 (`type_resolution.cryo`, around line 1819)

**This fix is independent of for-in and worth keeping on its own merits.**

Phase 3 of `run_type_resolution` walks function/method bodies and
resolves type annotations on local VarDecls (e.g. `mut acc: Acc = ...`
inside a trait default-method body that's been cloned into an impl
block). Before this change it walked with an empty `res_ctx`, so any
body annotation that referenced a method-level generic param fired
**E0203 "cannot find type Acc in this scope"** even though the
method's signature pass had bound the param correctly.

Reproduce on a clean tree:
```cryo
import std::core::iter;
type struct CountDown { ... }
implement trait Iterator<i32> for struct CountDown {
    next(mut &this) -> Option<i32> { ... }
    // No override for fold<Acc> — synthesize_default_trait_methods
    // clones it from the trait, and Phase 3 walks the clone's body.
}
```

The fix walks each method body with a per-method `res_ctx` that has:
- the owner's generic params (`Struct`/`Class`/`Trait`/`Impl`),
- for impl blocks, the trait's outer params bound to the impl's
  trait args via `bind_trait_args_for_impl` (so `Item` resolves to
  the impl's concrete `T`),
- the method's own generic params (the `<Acc>` in `fold<Acc>`).

Without this fix, **the whole class of "user impls a trait without
overriding every default" is broken**, and would have stayed broken
for v1.0.0 with or without for-in.

### 4. Monomorphizer (`monomorphizer.cryo`)

This is the bit that actually makes `for (i in Range::new(0, 5))`
produce a `Range<i32>`-typed iterator instead of a `Range<T>` template.

Sema resolves `Range::new(0, 5)` to the template `Range<T>` because
there's no expected-type hint to drive value-arg → type-arg inference.
The monomorphizer DOES do that inference, in
`try_infer_static_method_on_generic_template` (around line 2900). It
produces `inst_ref = Range<i32>` and pins `call.resolved_callee` to
the spec'd `Range_i32::new` — but it does not surface `inst_ref` to
any caller.

Changes I made:

- Added two helpers:
  - `return_type_matches_owner_template(method_func, entry)` — true
    when the static method's syntactic return-type leaf name equals
    the owner template's bare name. This is the constructor-shaped
    pattern (`Range::new -> Range<T>`,
    `Array::with_capacity -> Array<T, A>`). Excludes
    `Layout::array<T>(n) -> Layout` and other non-constructor statics
    so we don't retype them.
  - `type_annotation_base_name(ann)` — extracts the leaf name from a
    `Named` or `Generic(Named, ...)` annotation. Returns
    `SymbolStr::empty()` for unnameable shapes.
- Added a transient field `last_static_inst_ref: TypeRef` on the
  Monomorphizer struct.
- Inside `try_infer_static_method_on_generic_template`, after the
  successful instantiation + enqueue, if the method matches the
  constructor pattern: stash `inst_ref` into `last_static_inst_ref`.
  **No mutation of `call.resolved_type`.** (See "Dead end" below.)
- In `discover_inferred_calls_in_stmt`'s `DeclarationStatement`
  branch (around line 1738), save/clear `last_static_inst_ref` before
  recursing into the initializer, capture it after, then — only when
  `vd.is_auto` — set `vd.resolved_type` from it.

This lets the for-in iterator local pick up the spec'd type without
disturbing any other pass.

### 5. Test file (`tests/tests/lang/for_in.cryo`)

Twelve `![test]` cases covering:
- `Range<i32>` ascending / empty / start-≥-end / `RangeInclusive`
- `break`, `continue`, early `return` inside the body
- Nested for-in; inner `break` only breaks inner
- A user-defined `CountDown` struct that `implement trait Iterator<i32>` —
  exercises the for-in sugar against non-stdlib iterators **and**
  inherits the trait's default `count` / `fold<Acc>` / `for_each`,
  which is exactly the path the Phase 3 fix unbreaks.

---

## Next step (where to pick up)

Last edit applied was the side-channel `last_static_inst_ref` approach in
`monomorphizer.cryo`. **It has not been built or tested yet** — I was
about to run `make cryo && make test` when the user interrupted to write
this handoff.

Action list, in order:

1. `make cryo` — should compile clean. If the parser/AST/match shape I'm
   using is wrong, errors will be obvious.
2. `make test ARGS="for_in"` — exercise just the for-in suite. If for-in
   tests pass, move to (3); if `Range<T>` codegen errors come back,
   the side-channel isn't catching the constructor case — investigate
   `try_infer_static_method_on_generic_template`'s match guard or
   whether `discover_inferred_calls_in_expr` is even visiting the
   initializer (which it should: DeclStmt branch was already there).
3. `make test` — full repo suite. The Phase 3 type-resolution fix
   touches every non-generic struct/class/trait/impl method body; if
   anything regresses it will surface here.
4. `make selfhost-check` — 3-round / 6-stage byte-identity gate. The
   compiler is built by the compiler; if my changes break that, this
   is where it shows.
5. **Re-pin** with `make pin-cryo` once selfhost is green, since the
   parser change is real-syntax. Check the diff on `bin/cryo.pin.txt`.
6. Documentation:
   - `docs/cryo.md §6.3`: remove "reserved syntax; not yet wired to
     `Iterator`" and document the actual desugaring.
   - `docs/cryo.md §21`: remove `for x in iter` from the reserved list.
   - `CHANGELOG.md`: add a `for x in iter` entry under "Compiler".
   - `README.md` Status & Roadmap: remove `for x in iter` from
     "Beyond 1.0".
7. Add a compile-fail test for "scrutinee doesn't implement Iterator"
   once you know what error fires (probably E0636 from codegen or
   E0214 from sema — let the failing case tell you, then pin it).
8. Update memory: write a new entry noting the Phase 3 type-resolution
   bug + fix as a separate landed item, since it stands on its own.

---

## Dead end: do not retry

I tried an earlier approach where
`try_infer_static_method_on_generic_template` called
`call.set_resolved_type(inst_ref)` directly. This caused a **silent
segfault** in `cryo test` builds across the entire suite (not just
for-in tests). Diagnosis: other passes treat the call's
`resolved_type` as load-bearing in subtle ways; overwriting it from
the late-running inference path destabilises downstream codegen.

The side-channel `last_static_inst_ref` approach replaces it: the
information flows out of the inference function without touching the
call node. If the segfault returns, that's a regression, not a new
problem.

---

## Open questions / non-goals for this session

- **Iterator combinators** (`.map`, `.filter`, `.collect`, ...) — out of
  scope. CHANGELOG already says they're post-1.0. for-in alone is the
  ship requirement.
- **`for x in &collection`** (iterating by reference) — not in scope.
  Users can call a method that returns an iterator.
- **Loop-variable mutability** — Cryo's `match` pattern bindings are
  immutable by default and that's how the body sees `x`. Matches
  Rust's default; users can rebind inside the body if they need mut.
- The `is_auto` flag was previously dormant. After this work it has
  exactly one source (the for-in desugar). If we later want to use it
  for other compiler-synthesised vars, the sema carve-outs already
  generalise.

---

## How to verify "good enough to ship"

Independent of the test suite, run this smoke test:

```bash
mkdir -p /tmp/forin/src && cd /tmp/forin
cat > cryoconfig <<'EOF'
[project]
project_name = "forin"
target_type = "executable"
entry_point = "src/main.cryo"
EOF
cat > src/main.cryo <<'EOF'
namespace Forin;
import std::core::ops;

function main() -> int {
    mut sum: i32 = 0;
    for (i in Range::new(0, 5)) { sum = sum + i; }
    printf("sum=%d\n", sum);   // expect: sum=10
    return 0;
}
EOF
CRYO_STDLIB=/workspaces/CryoLang/stdlib \
    /workspaces/CryoLang/compiler/build/bin/cryo run
```

If that prints `sum=10`, the user-visible part is working.
