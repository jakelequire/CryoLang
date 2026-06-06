# Compiler gaps found while building `std::time` / `std::random`

Date: 2026-06-05. All are worked around in the stdlib; none required a
compiler change to ship the libraries. Listed worst-first.

---

## 1. Same-named global `const` across files resolves first-wins (the "fold quirk")

**Symptom.** `E0200: mismatched types — expected u64, found i64` on a
`SOME_CONST - 1u64` expression, where `SOME_CONST` is declared
`const SOME_CONST: u64` in the *same* file.

**Root cause (not constant folding).** Two files under the same parent
module declared a global const with the **same leaf name but different
types**:

- `stdlib/time/duration.cryo`:  `const NANOS_PER_SEC: u64 = 1000000000;`
- `stdlib/time/clock.cryo`:      `const NANOS_PER_SEC: i64 = 1000000000;`

When `duration.cryo` references `NANOS_PER_SEC`, leaf-name resolution
(`register_leaf_name`, first-wins) binds it to **clock's `i64`** const, so
`NANOS_PER_SEC - 1u64` types as `i64` and fails to assign into a `u64`
slot. This is the same first-wins leaf-name limitation already documented
for types (see memory `project_inference_conflict_fixed_2026_05_27`) —
here applied to global constants.

**Why it looked like a fold quirk.** It only reproduces in the *full
multi-module build*; an isolated single-module project with identical code
compiles, because there is no second `NANOS_PER_SEC` to collide with.

**Minimal repro (two files, same module):**
```
// a.cryo
namespace demo::a;
const K: i64 = 1000;
// b.cryo
namespace demo::b;
const K: u64 = 1000;
function f() -> u64 { mut x: u64 = 0; x = K - 1u64; return x; }  // E0200 here
```

**Workaround applied.** Renamed clock's constant to `NSEC_PER_SEC` so the
`u64` and `i64` constants no longer share a leaf name. (Renaming or making
leaf names unique is the general workaround.)

**Proper fix (deferred — cross-module coherence).** Global-const symbol
resolution should be module-qualified rather than first-wins on the leaf
name, the same coherence work scoped out of 1.0 for types. Likely lives
near `register_global_with_module` / `lookup_global` (DeclarationIndex)
and the leaf-name registration in the name resolver.

---

## 2. Generic default trait method with its own type parameter mis-codegens

**Symptom.** `LLVM: Incorrect number of arguments passed to called
function` for a default method like
`shuffle<T>(mut &this, items: Slice<T>)` declared on a (non-generic)
trait and inherited by an implementor.

**Workaround.** Moved `shuffle` / `choose` off the `RandomSource` trait
into free generic functions (`shuffle<T,R>(rng, items)`), which
monomorphize correctly. Non-generic default methods (`next_u32`,
`next_below`, `fill`, ...) are unaffected and stay on the trait.

---

## 3. Passing a generic 2-field struct (`Slice<T>`) **by value** to a generic fn

**Symptom.** Same `Incorrect number of arguments` verifier error: the
caller passes the 16-byte `Slice<T>` as one `{i64,i64}` aggregate while
the callee expects it expanded. A concrete `Slice<u8>` by-value is fine.

**Workaround.** Pass generic slices by reference (`items: &Slice<T>`).

---

## 4. Transitive monomorphization not discovered through an instantiated generic fn

**Symptom.** Link error `undefined reference to ...mem::swap...`: an
instantiated generic function (`shuffle<i32,Rng>`) calls `mem::swap<i32>`,
but that instantiation is never emitted. (`mem::offset<i32>` linked,
because it was instantiated elsewhere.)

**Workaround.** Inlined the swap in `shuffle` so no separate `mem::swap<T>`
instantiation is needed.

---

## 5. Generic trait method on a **match-arm-bound** receiver not discovered

**Symptom.** Link error `undefined reference to ...WeightedIndex...sample...`:
`wi.sample(&r)` where `wi` is bound by `match (...) { Result::Ok(wi) => {...} }`
does not queue `sample<Rng>` for monomorphization. The same call on a
plain `const`/`mut` local works.

**Workaround.** Unwrap the match payload into a local first, then call the
generic method on the local.

---

## Note on enum `Eq`

Not a compiler bug, but a gotcha: in an enum method, match the receiver
with `match (this)` (the working idiom, e.g. `json::error`), **not**
`match (&this)`. The `&this` form silently mis-evaluates a no-payload enum
comparison (returns wrong results, no diagnostic).
