# Associated Types — design & migration plan

> Decided 2026-06-15. Drives the v1.0 decision to switch `Iterator<Item>` from a
> generic type parameter to an associated type. This adds **associated types as a
> new language/type-system feature**; the initial rollout converts `Iterator`
> only (`From`/`Hash`/`Display` stay generic-param for now — convertible later,
> additively).

## Why

A generic-param `Iterator<Item>` lets a type implement the trait at multiple
`Item` types, so the item is ambiguous and uninferable — the root cause of "combinators
can't take capturing closures" and the opaque-typed-local re-adaptation limit.
An associated `Item` is *functional* (one per implementor), so the compiler can
infer it from the receiver, and adapter structs shed the input-item type param
they currently thread everywhere.

## Syntax (6 decisions)

1. **Declaration — reuse the `type` prefix.** Inside a trait body:
   ```cryo
   type trait Iterator {
       type Item;            // associated type
       // type Item: Copy;   // with a bound (declaration-site bound)
       next(mut &this) -> Option<This::Item>;
   }
   ```
2. **Projection — path style `X::Item`.** Refer to an associated type with `::`,
   matching existing path syntax (`Option::Some`, `core::iter`):
   ```cryo
   where I: Iterator, I::Item: Copy
   ```
3. **In-trait reference — always qualify with `This::Item`** (no bare `Item`).
   Associated types are therefore *always* spelled `X::Item` wherever they're
   read — no "inside vs outside the trait" scoping rule.
4. **Opaque-return binding — keep positional `Iterator<T>`** as sugar for
   `Iterator<Item = T>` (zero migration churn vs. today's positional form):
   ```cryo
   iter(&this) -> implement Iterator<Pair<K, V>> { ... }
   mut it: implement Iterator<Pair<u32,i32>> = map.iter();   // caller unchanged
   ```
5. **Sugar scope — positional applies everywhere** (impl headers + `where` bounds
   + opaque returns), so existing code barely changes.
6. **Disambiguation — positional fills generic params only; associated types are
   named when the trait has generic params.** The combined rule:

   > Positional `<...>` arguments fill the trait's declared **generic parameters**
   > in order. **If the trait has zero generic parameters and exactly one
   > associated type, a single positional argument binds that associated type**
   > (this is the `Iterator` case). When a trait has generic parameters, or more
   > than one associated type, associated types **must** be bound by name
   > `<Name = T>`. Named bindings are always allowed.

   ```cryo
   // Iterator: 0 generic params, 1 associated type -> positional works
   implement Iterator<Pair<K,V>> for HashMapIter<K,V>     // Item = Pair<K,V>

   // hypothetical trait Foo<T> { type Out; } -> assoc must be named
   implement trait Foo<i32, Out = bool> for X            // T=i32, Out=bool
   ```

### Desugaring summary
`Iterator<T>` (any site) ≡ `Iterator<Item = T>`. At an impl header the positional
arg *is* the `type Item = …` binding (no body binding needed); equivalently you may
write the explicit body form `implement trait Iterator for Foo { type Item = T; }`.

## What the migration looks like (minimal, by design)

Existing:
```cryo
type trait Iterator<Item> { next(mut &this) -> Option<Item>; map<B>(this, f: (Item) -> B) -> MapIter<This, Item, B> { ... } }
implement<I, A, O> trait Iterator<O> for struct MapIter<I, A, O> { ... }
implement<K, V>    trait Iterator<Pair<K, V>> for struct HashMapIter<K, V> { ... }
```
After:
```cryo
type trait Iterator { type Item; next(mut &this) -> Option<This::Item>; map<B>(this, f: (This::Item) -> B) -> MapIter<This, B> { ... } }
implement<I, O> trait Iterator<O> for struct MapIter<I, O> where I: Iterator { ... }   // A dropped (= I::Item)
implement<K, V> trait Iterator<Pair<K, V>> for struct HashMapIter<K, V> { ... }        // header unchanged (positional sugar)
```

**Adapter structs shed their input-item param** (it's `I::Item` now):
`MapIter<I, A, O>` → `MapIter<I, O>`, `FilterIter<I, A>` → `FilterIter<I>`,
`EnumerateIter<I, A>` → `EnumerateIter<I>`, `ZipIter<I, J, A, O>` → `ZipIter<I, J>`.

### Touchpoints
- **Trait decl:** `stdlib/core/iter.cryo:22` (+ all default methods to `This::Item`).
- **16 `implement … trait Iterator<…>` sites:** `core/iter.cryo` (6 adapters),
  `collections/array.cryo` (RefIter/CopiedIter/ClonedIter), `collections/hashmap.cryo`
  (HashMapIter/KeysIter/ValuesIter), `collections/hashset.cryo`, `collections/str.cryo`
  (SplitIter), `core/ops.cryo` (Range/RangeInclusive), `core/slice.cryo` (SliceIter),
  `fs/dir.cryo` (ReadDir).
- **Adapter struct defs** in `core/iter.cryo` + `array.cryo` (drop the input-item param).
- **`where … : Iterator<…>` bounds** and **`implement Iterator<…>` opaque returns**
  (these keep working via positional sugar; only adapter-param arity changes).
- **for-in lowering** already calls `.iter()`/`.next()` generically — no change expected.

## Compiler work (the real cost — this is a new type-system feature)

1. **Parse** `type Item;` / `type Item: Bound;` in trait bodies; parse `X::Item`
   type-projection in type position (annotations, bounds, returns, generic args).
2. **Type system:** represent associated-type members on a trait; an
   associated-type **projection** type (`I::Item`) that resolves once `I`'s impl is
   known; **binding** an associated type in an impl (from positional sugar or the
   `type Item = …` body form); enforce declaration-site bounds.
3. **Inference:** resolve `This::Item` within trait default methods; project
   `I::Item` through generic params during monomorphization.
4. **Opaque returns** (`implement Iterator<T>`): carry the bound `Item` so callers
   can name `implement Iterator<T>` and the loop/`from_iter` machinery sees the item type.
5. **Desugaring:** positional `<...>` → param fills + (0-param/1-assoc) lone-assoc
   binding, per the rule above; reject positional assoc binding when the trait has
   generic params with a clear diagnostic.
6. **Diagnostics:** "associated type `Item` not bound in this impl"; "trait `Foo`
   has generic parameters — bind `Out` by name (`Out = …`)".

## Validation
Stage-2 self-host rebuild + full suite at O2 and O0; the existing iterator tests
(`tests/tests/stdlib/iter.cryo`, hashmap/array/slice iteration) are the regression
net. Add a test that re-adapting an opaque iterator local now works (the limitation
this change is meant to lift), and a negative test for unbound / wrongly-positional
associated types.
