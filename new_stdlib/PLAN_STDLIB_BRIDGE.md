# cryoc → new_stdlib Bridge Plan

> **Goal.** Make `cryoc` compile every file under `/workspaces/CryoLang/new_stdlib/`.
> The new stdlib is intentionally written to a future Cryo spec
> (see `new_stdlib/PLAN.md` §2.1: *"write to the spec, not the current
> compiler"*). This plan enumerates the language features cryoc must
> grow, in dependency order, and maps each one to concrete
> `cryoc/src/compiler/` change sites.

---

## 0. Scope

**In scope.** Parser, AST, type-system, and codegen changes inside
`cryoc/` that are required for `new_stdlib/` to traverse the full
9-stage pipeline (lex → parse → resolve → check → specialize →
codegen → IR → optimize → link).

**Out of scope.** Anything `new_stdlib` already designed around as
"deferred until cryoc lands it" — `Drop` auto-insertion, atomic
intrinsics, variadic format strings, closures with captures. The
stdlib uses manual `drop(mut &this)`, single-threaded `Rc`, static
print/println helpers, and bare function pointers in their place
(per `new_stdlib/PLAN.md` §2.2 and Phase 5 deferred list).

**Not breaking.** Existing `cryoc` self-host and the legacy
`/workspaces/CryoLang/stdlib/` build must keep passing. Every
parser/AST change must be additive.

---

## 1. Feature gap (verified against source)

Each row cites the exact `cryoc` file:line that today rejects (or
ignores) the feature, and the `new_stdlib` file:line that depends
on it.

| # | Feature | cryoc today | `new_stdlib` use site |
|---|---|---|---|
| **G1** | `implement trait Foo for Bar { … }` | `parser.cryo:1198-1228` only consumes `implement [enum\|struct] Name<T>`; no `KwTrait`/`KwFor` branches | `core/cmp.cryo:47` (and 63 more sites) |
| **G2** | `implement<T> trait Foo<T> for struct Bar<T> where T: Bound` | (depends on G1, plus `where` after the `for` target) | `core/ops.cryo:47-59`, `core/option.cryo:128`, `io/buf.cryo:152` |
| **G3** | Default type parameters `<T, A = GlobalAlloc>` | `parser.cryo:1435-1466` `parse_generic_params` reads name + `:` constraints only — no `=` branch | `collections/array.cryo:27`, `string.cryo:24`, `hash_map.cryo:41`, `hash_set.cryo:18`, `ffi/cstr.cryo:78`, `fs/path.cryo:111`, `alloc/box.cryo:14` |
| **G4** | Multi-bound `where T: Hash + Eq` | `parser.cryo:1469-1489` `parse_trait_bounds` reads exactly one trait identifier | `collections/hash_map.cryo:110,140,149`, `hash_set.cryo:43,51,59,73` |
| **G5** | Path-qualified bounds (`where W: io::Write`) | `parse_trait_bounds` consumes a single `Identifier`, no `::` walk | `fmt/display.cryo:74`, `io/buf.cryo:61`, `net/http/headers.cryo:111` |
| **G6** | `&This` as a type annotation | `KwThisType` exists in lexer (line 140) but is not threaded into `parse_type_annotation` resolution path; `This` does not lower to the implementing type at use sites | `core/cmp.cryo:44` (`other: &This`), `core/clone.cryo:14` (`-> This`), `core/default.cryo:10` (`static default() -> This`), `core/convert.cryo:26` (`from(value: T) -> This`) |
| **G7** | Trait-impl registry + bound-aware method dispatch | sema has `TraitBound` AST node but no `(Trait, Type) → ImplBlock` table and no method-resolution path that consults bounds for a generic `T` | every method call on a generic-bounded receiver, e.g. `t.equals(&other)` where `T: Eq` |
| **G8** | Trait-bound enforcement at instantiation | Monomorphizer accepts any `<T>` argument | `Range<f32>` should reject because `f32` doesn't impl `Step`; today this would silently expand |
| **G9** | Default trait-method bodies | Parser captures bodies (`parser.cryo:1006`) but specialization does not synthesize them when an impl block omits an override | `core/iter.cryo:24-57` (`count`/`fold`/`for_each`), `io/traits.cryo:45-181` (`read_all`, `write_all`, etc.), `alloc/allocator.cryo:65-81` (`reallocate`) |

**Verified non-issues.** Cryoc already handles, and `new_stdlib` already
relies on:
- `type trait Name : Base { … }` declarations with supertrait colon
  (`parser.cryo:965-1029`).
- `type struct/class/enum Name<T> { … }` `type`-prefixed forms
  (`parser.cryo:240-264`).
- Generic monomorphization pipeline (`monomorphizer.cryo`).
- `&this`, `mut &this`, `&T`, `*T`, function-pointer params, `extern
  "C"`, `sizeof`/`alignof`, length-typed `T[]` arrays.
- `for x in 0..n` for-in syntax (`parser.cryo:1689`) — present, but
  `new_stdlib` does **not** depend on it; iterators are driven by
  `match (iter.next())` loops.

**Explicitly deferred by stdlib design** — do not implement as part of
this bridge:
- Drop trait + auto-inserted destructors (manual `drop(mut &this)`
  used everywhere in the stdlib).
- Atomic intrinsics (`Arc` is gated on this).
- Variadic functions / format-string parsing.
- Closure capture (function pointers used in the few callback sites).
- Operator overloading via traits — `core/cmp.cryo:4` is explicit:
  *"Cryo does not support operator overloading; generic code calls
  `.equals()` or `.compare()`."*

---

## 2. Phased plan

Phases are dependency-ordered. Each phase ends with a concrete
test target — a subset of `new_stdlib/` that should compile once
the phase lands.

### Phase A — Pure parser additions (no AST shape change beyond optional fields)

**A1. Default type parameters (G3).**
- File: `cryoc/src/compiler/parser/parser.cryo` `parse_generic_params`
  (line 1435-1466).
- Change: after the optional `: Constraint+...` block, accept
  `match_tok(Equal)` and call `parse_type_annotation()`. Store on
  `GenericParamNode`.
- AST: add `default_annotation: TypeAnnotation*` field on
  `GenericParamNode` (`AST/declaration.cryo`). Plumb through
  `cloner.cryo`, `substituter.cryo`, `dumper.cryo`.
- Risk: low — purely additive, only triggers when `=` is present.

**A2. Multi-bound `+` in where clauses (G4).**
- File: `parse_trait_bounds` (line 1469-1489).
- Change: after consuming the first trait name, loop on
  `match_tok(Plus)` and append additional trait names. Promote
  `TraitBound.trait_name: SymbolStr` to
  `TraitBound.trait_names: SymbolStr[]`.
- Touch sites: any sema code that reads `bound.trait_name` (audit;
  most uses are downstream of monomorphization which currently
  does not enforce bounds anyway).
- Risk: low.

**A3. Path-qualified bounds (G5).**
- File: `parse_trait_bounds`.
- Change: replace single `Identifier` consume with the path-walk
  pattern from `parse_namespace_declaration` (line 1130-1158).
  Store as a `QualifiedName` rather than a bare `SymbolStr`.
- Risk: medium — interacts with name resolution. Defer the
  resolution step to Phase C; for now just record the path.

**A4. `implement trait Foo for Bar { … }` form (G1).**
- File: `parse_implementation_block` (line 1198-1228).
- Change: at entry, peek for `KwTrait`. If present:
  - consume `KwTrait`, parse trait name + optional `<…>` generic args
  - consume `KwFor`
  - parse optional `KwEnum`/`KwStruct`/`KwClass` keyword
  - parse target type name + optional `<…>` args
  - the existing `where`-clause and method-body code below stays
- AST: add `trait_annotation: TypeAnnotation*` (or split into
  `trait_name: QualifiedName` + `trait_args: TypeAnnotation*[]`)
  on `ImplBlockNode` (`AST/declaration.cryo:515-542`). Inherent
  impls leave it null.
- Risk: medium — touches a hot AST node, but the change is
  additive. The big care item is: **don't break existing
  `implement enum Optional<T>` parsing**.

**A5. Generic impl-trait-for-type with `where` (G2).**
- File: `parse_implementation_block`.
- Change: after the leading `implement`, accept optional `<T, …>`
  generic params (already there at line 1220-1226). After the
  target type, accept optional `KwWhere` and call
  `parse_trait_bounds`. Store on `ImplBlockNode`.
- AST: add `where_bounds: TraitBound[]` on `ImplBlockNode`.
- Risk: medium — interacts with A4. Land them as a single change.

**A6. `&This` in `parse_type_annotation` (G6).**
- File: `parser/parser.cryo` (`parse_type_annotation` — find the
  ref/ptr branch).
- Change: accept `KwThisType` as a valid type-name token and emit
  a `TypeAnnotation` of kind `ThisType` (or set a sentinel that
  resolves later in sema).
- Risk: low for parser; the resolution happens in Phase B.

**Phase A exit criterion.** All `.cryo` files under `new_stdlib/core/`
and `new_stdlib/alloc/` parse without errors. Run with
`--parse-only` (or equivalent dump) and confirm no parser
diagnostics fire. **This is purely a parsing milestone** —
type-checking and codegen will still fail.

### Phase B — Type resolution

**B1. Resolve `This` to the implementing type (G6 follow-up).**
- File: `cryoc/src/compiler/types/checker.cryo` and
  `types/resolver.cryo`.
- Inside a trait declaration body or an impl block, `This` resolves
  to:
  - inside `type trait Foo { method(&this, x: This) }` — a
    placeholder type bound by the impl
  - inside `implement trait Foo for Bar { method(&this, x: This) }`
    — `Bar` (with appropriate generic substitution if the impl is
    generic, e.g. `Range<T>`)
- Touch: `TypeRef` resolution in trait method signatures.

**B2. Default-type-parameter substitution (G3 follow-up).**
- File: `types/monomorphizer.cryo` (or wherever
  `InstantiatedType` argument lists are filled).
- Change: when an instantiation gives fewer args than the template
  has params, walk the missing tail and substitute each param's
  `default_annotation` (resolved in the template's environment).
- Test target: `Array<i32>` resolves to `Array<i32, GlobalAlloc>`.

**B3. Trait-impl registry (G7).**
- New: a per-module table keyed by `(TraitID, TargetTypeID) →
  ImplBlockID`.
- Populated in Stage 3 (`DeclarationCollection`) or Stage 4
  (`TypeResolution`) — wherever impl blocks get walked. Lookups
  must succeed across modules (cross-module table).
- For generic impls (`implement<T> trait Iter<T> for Range<T>`)
  the key is `(IterID, RangeID)`; the args become specialization
  inputs at call resolution time.

**B4. Method resolution through trait bounds (G7).**
- When typing `t.method(args)` where `t: T` and `T` has a bound
  `T: Eq`, look up the trait method `Eq::method` and emit a
  symbolic call that the monomorphizer will later resolve to the
  concrete impl once `T` is bound to a real type.
- This is the **load-bearing** sema piece for traits to work.
- File: `types/checker.cryo` method-call typing.

**B5. Trait-bound enforcement at instantiation (G8).**
- File: `types/monomorphizer.cryo`.
- When instantiating `Range<f32>`, walk bounds, look up
  `(Step, f32)` in the registry. Report error if missing.
- Until B3 lands this is a no-op.

**B6. Default trait-method synthesis (G9).**
- File: specialization / Stage 6.
- For each `(Trait, Type)` impl, fill in any methods that the impl
  block did not override by cloning the trait's default body and
  substituting `This → Type`.
- Reuse `ASTSpecializer` / `ASTTypeSubstituter`.

**Phase B exit criterion.** `new_stdlib/core/` type-checks end-to-end.
A small synthetic test like:
```cryo
import core::cmp;
function main() -> i32 {
    const a: i32 = 1; const b: i32 = 2;
    if (a.equals(&b)) { return 1; }
    return 0;
}
```
resolves and type-checks (no codegen yet).

### Phase C — Codegen

The good news from the audit: **no new codegen IR is required.**
Once monomorphization picks the right `ImplBlockNode` for
`(Trait, ConcreteType)`, the existing impl-block codegen path
(`codegen/ir_generator.cryo:155-161`) emits the same IR as today's
inherent methods. Static dispatch, no vtables.

**C1. Symbol mangling for trait-impl methods.**
- Use the new name mangling v2 scheme.
- File: `codegen/decl_codegen.cryo`, mangling helpers.
- Important: the C++ bootstrap will need to agree on this scheme
  if the bootstrapped path also encounters trait impls — but if
  trait impls are gated to cryoc-only files, the bootstrap can
  remain trait-blind.

**C2. Cross-module impl visibility.**
- An impl in `core/cmp.cryo` for `i32` must be visible from
  `collections/array.cryo` once `core::cmp` is imported. Make
  sure the impl-registry persists across module-boundary type
  registries (`ModuleTypeRegistry`).

**C3. Default-trait-method codegen.**
- After B6 synthesizes the AST, codegen falls through normally.
  No new code path.

**Phase C exit criterion.** `new_stdlib/core/` compiles to LLVM IR
end-to-end. Smoke test: a binary that imports `core::cmp::Ord`
and prints `Ordering::Less`.

### Phase D — Stdlib rollout

**D1.** Compile `core/` (1,461 lines, 18 files). All Phase 1+2
files except those that depend on later modules.

**D2.** Compile `alloc/` (785 lines). Depends on `core/marker`,
`core/clone`, `core/default`. `Box<T>`, `Rc<T>`, `Arena`, `Pool`,
`GlobalAlloc`.

**D3.** Compile `collections/` (1,222 lines). Depends on `alloc/`
and `core/hash`. **First test of default type params + multi-bound
`where T: Hash + Eq` in anger.**

**D4.** Compile `fmt/`, `io/`, `fs/`, `math/`, `env/`, `process/`,
`ffi/`, `net/` (~6,400 lines combined).

**D5.** Migration step (`new_stdlib/PLAN.md` Phase 6): swap cryoc's
stdlib search path from `stdlib/` to `new_stdlib/`, fix fallout,
delete the old tree.

---

## 3. Test strategy

A real test corpus matters for this — the changes touch parser,
sema, and codegen, and regressions in any single one will silently
break the self-host.

**Per phase:**
- Phase A: snapshot/golden tests over `--dump-ast` for a curated
  set of `new_stdlib/core/` files. Diff regressions catch parser
  drift.
- Phase B: small `.cryo` test files in `cryoc/sandbox/traits/`
  that exercise each of B1-B6 in isolation. Each should
  type-check; each should also have a negative twin
  (e.g. `Range<f32>` for B5).
- Phase C: end-to-end `cryoc build → run` of trait-using programs.
  Compare output to expected.

**Regression guards.**
- Stage 3 self-host (`./build/bin/cryoc build` from `cryoc/`) keeps
  passing after every commit. The active `string::append → strlen`
  crash from `HANDOFF.md` is independent of this work but is the
  pre-existing baseline.
- The legacy `stdlib/` keeps building under `bin/cryo` (C++
  bootstrap). No source-format changes that the bootstrap can't
  parse.

**Bootstrap interaction.** Per `HANDOFF.md` Critical Principles:
*"Bootstrap C++ Cryo is the immovable obstacle until cryoc
self-hosts. Source-level changes that break the bootstrap are out."*
Trait impls and default type params are net-new syntax — the
bootstrap won't see them as long as cryoc's source itself doesn't
adopt them. **Keep `cryoc/src/` free of `implement trait Foo for
Bar` and `<T = Default>` until after cryoc successfully self-hosts
through these features.**

---

## 4. Open questions

1. **`This` resolution inside generic impls.** In
   `implement<T> trait Iterator<T> for Range<T> { next(mut &this) -> Option<T> }`,
   does `This` resolve to `Range<T>` (the generic), or are uses of
   `This` disallowed once you're inside an impl block (since the
   target type is already named)? `new_stdlib` only uses `This`
   inside trait *declarations*, not inside impls — so a permissive
   "resolve to declared target" rule should be safe.

2. **`&This` vs `&T`.** The stdlib writes `equals(&this, other:
   &This)` in the trait, and `equals(&this, other: &i8)` in the
   impl — already substituted. So the impl-block parser doesn't
   need to handle `This` at all; only the trait declaration does.
   Confirm by greping new_stdlib for `: &This` inside `implement`
   blocks (expected: zero).

3. **Bound enforcement timing.** Should `Range<f32>` fail at
   instantiation (eager) or at the first method call (lazy)?
   Eager matches Rust's behavior and produces clearer errors;
   lazy is cheaper to implement. Recommend eager (B5).

4. **Mangling for `Iterator<i32> for Range<i32>`.** Two
   instantiations of the same trait+target pair. Mangling must
   include the trait's type args. Suggest:
   `std::core::iter::Iterator__i32__for__std::core::ops::Range__i32::next`
   (or a hashed/shortened variant if linker symbol limits hit).

5. **Path resolution for bounds (G5).** When the bound is
   `W: io::Write`, the resolver needs to find `io::Write` from
   the current module's import scope. Reuse the same path
   resolution that `import` uses. New machinery? Probably already
   in `ModuleLoader`.

6. **Cross-module impl pickup.** If `core/cmp.cryo` provides
   `implement trait Eq for i32` and `collections/hash_map.cryo`
   uses `K: Eq`, does the import of `core::cmp` automatically
   bring the impl into the current module's resolution scope, or
   must impls be re-imported explicitly? Recommend implicit (Rust
   "orphan rule" style — impls follow the trait or the type
   wherever either is in scope).

---

## 5. Estimated effort

Rough order-of-magnitude, assuming the existing cryoc internals
are sound:

| Phase | Files touched | Approx. LOC | Risk |
|---|---|---|---|
| A (parser) | ~5 (parser.cryo, declaration.cryo, cloner, substituter, dumper) | 200-400 | Low |
| B (sema) | ~6 (checker, resolver, monomorphizer, registries) | 500-900 | High — load-bearing |
| C (codegen) | ~3 (decl_codegen, mangling, ir_generator) | 100-300 | Low (mostly mangling) |
| D (stdlib) | rollout-only | n/a | Medium — surfaces latent bugs |

**Risk concentration.** Phase B is the load-bearing piece. Phases
A and C are mechanical. Suggest landing A first as a self-contained
PR, B as a series of small commits each with a focused test, and
C alongside D module-by-module.

---

## 6. Cross-references

- `new_stdlib/PLAN.md` — design intent for the rebuild; supersedes
  this doc on stdlib design questions.
- `cryoc/HANDOFF.md` (root `HANDOFF.md`) — current cryoc self-host
  state and the active `string::append` crash. Independent of this
  bridge work.
- Memory: `feedback_codegen_architecture_rules.md`,
  `feedback_no_bare_name_lookups_for_cryo_types.md`,
  `feedback_codegen_style.md` — rules to honor while editing
  cryoc's codegen.
- Memory: `feedback_overloaded_virtual_dispatch.md` — flag for the
  trait-method-dispatch work in Phase B.

---

## 7. Definition of done

A single command from the repo root:

```bash
cd /workspaces/CryoLang/new_stdlib && /workspaces/CryoLang/cryoc/build/bin/cryoc build
```

produces a `libcryo.a` (or equivalent), and a hello-world binary
that imports from the new stdlib (`import collections::string;`,
`import io::stdio;`) compiles, links, and runs to completion. At
that point Phase 6 of `new_stdlib/PLAN.md` (the legacy-stdlib
swap) is unblocked.
