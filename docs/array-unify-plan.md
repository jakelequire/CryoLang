# One dynamic array type

Goal (roadmap "Order to v1.0", item 2): `[T]` is `Array<T>` with the
default allocator, one type, so the class removal writes its thousands of
array types against it, and the nested-array crash goes with the second
type.  Deleted when the change is finished; what stays open moves to the
roadmap.

## 1. The differences, measured

Before any of this plan was built (one probe file, 15 shapes, the pin of
the array-syntax change):

| shape | `[i64]` (builtin) | `Array<i64>` |
|---|---|---|
| array literal `[1, 2]` initialises it | yes | E0200 mismatched types |
| `x[0]`, `x[0] = 5` | yes | E0200 "cannot be indexed with `[]`" |
| `x.length` (field) | yes | yes (the field is public) |
| `x.push(..)`, `x.length()` | yes (borrowed from `Array<T>`) | yes |
| `for (e in x)` | E0306 "`[i64]` does not implement `Iterator`" | yes |
| passed by value to the other's parameter | E0214 both directions | |
| assigned to the other | E0200 | |
| passed by reference to the other's parameter | yes, both directions | |
| `[i64; 2]` converted to it | yes (implicit) | E0200 |
| `Array<T, A>` with another allocator | not expressible | yes |
| `p[i]` with `p` a pointer to it | indexes the array (auto-deref) | pointer arithmetic |

- **Symbols.**  `[i64]` mangles as `Al`; `Array<i64>` as
  `N$L3std.11collections.5array.5Array$Ll_N$L3std.5alloc.9allocator.11GlobalAlloc$G$G$G`.
- **Layout.**  The builtin is the literal struct `{ ptr, i64, i64 }`;
  `Array<T, A>` is `{ ptr, length, capacity, alloc }` with `GlobalAlloc`
  empty: the same size and offsets, a different LLVM type.
- **Methods and drop.**  The builtin has none of its own: methods resolve
  against `Array<elem>` through an element-keyed bridge
  (`DeclIndex::lookup_array_type`, filled by
  `register_array_specialization` after monomorphization), and a builtin
  local is released by `Array<elem, GlobalAlloc>`'s `Drop::drop`.  The
  bridge is filled too late for sema's `for` lowering, which is why `for`
  over a builtin is refused.
- **Branches.**  28 sites in 21 files of `compiler/src` test for a dynamic
  `ArrayType` (`is_dynamic()` / `is_fixed()`); `TypeKind::Array` itself is
  tested at 79 sites.
- **Nested (bug 0045).**  `[[i32]]` with one row pushed exits 0xC0000005.
  The outer push is `Array<[i32]>::push` taking the row as a builtin by
  value; why it faults is not established.

## 2. Decided (rulings 363-368)

- `[T]` is exactly `Array<T>` with the default allocator;
  `Array<T, A>` is written out for another allocator (363).
- `Array<T>` is indexed through `Index` implementations in the standard
  library, not by extending the compiler's built-in indexing (364).
- `.length` stays readable; its visibility is the stdlib visibility pass's
  (365).
- `&[T]` stays a reference to the owning array; slices stay `Slice<T>`
  (366).
- The symbol rename is proved by a one-off comparison of compiled output
  with old names mapped to new ones, deleted in the next commit (367).
- The compiler and stdlib prefer Cryo's own language features (368).
- Editor and diagnostic text spells a type as the annotation wrote it;
  with no written spelling, `[T]` for the default allocator,
  `Array<T, A>` otherwise (369).
- `![inline]` functions are copied into every calling module; `Array`'s
  `index` and `element_at` are marked (370).  Built.
- `p[i]` on a pointer to an array is pointer arithmetic (371).  Built.
- An attribute reports the caller's location in a panic, on the index
  bounds check (372).
- Emitting only the used methods of an instantiated generic is scouted
  (373).
- A fill literal is refused unless its element is plain-copy (374).
  Built.

## 3. Units

### 3.1 Additive, proved by identity (`--require-identical`, 0 moved)

Built:

- `Array<T, A>` implements `Index<Idx, T>` once per integer type (`i8` to
  `i64`, `isize`, `u8` to `u64`, `usize`), each answering through one
  private bounds-checked `element_at`.  `Index` returns a pointer, so the
  one implementation serves reads, writes and compound assignment; no
  index-mut trait is needed.
- A generic body indexing a container whose type carries the body's
  parameters (`xs: &Array<T>`) builds the `Index` desugar where it is
  written, as for a concrete base; the copies specialize the call it holds.
  It failed codegen before (E0633 "left 1 of 1 basic block(s)
  unterminated") for any user `Index` type, not only `Array`.
- An array literal initialises `Array<T>` with the default allocator in
  every form (listed, empty, fill fixed or run-time): sema types it as the
  expected `Array<T>`, and the literal emitter stores pointer, length and
  capacity as it does for the builtin (the allocator field is empty).
  `TypeResolver::dynamic_array_element` is the one test of "is this
  `Array<T>` with the default allocator".

Not done additively, on purpose:

- **`for` over a builtin `[T]`.**  It fails because the element-keyed
  bridge is empty when sema lowers the loop.  The type switch deletes the
  bridge and `for` over `Array<T>` already works, so `for` over `[T]`
  arrives with the switch; fixing it first would be a second lookup path
  deleted one commit later.
- **`[T; N]` converted to `Array<T>`.**  Needed by the switch (the builtin
  has it implicitly); it is built with the switch, where the conversion's
  target is the only dynamic array.

### 3.2 The type switch

`[T]` resolves to `TypeResolver::dynamic_array_type(T)`; the dynamic
`ArrayType`, the element-keyed bridge (`lookup_array_type*`,
`register_array_specialization`, the monomorphizer's arena scan for
dynamic arrays) and the codegen paths for the builtin are deleted.
`TypeKind::Array` keeps only the fixed array.

Blocked on the type of `length` (open question 1): the builtin's
`.length` is an implicit `i64` field, `Array<T>`'s `length` field is a
`u64`.  With `[T]` resolved to `Array<T>` (the resolver's annotation case
and a run-time fill literal's type, nothing else), building the compiler
reports 2,348 W0005 implicit `i64`/`u64` widenings and 177 E0200, every
one "expected `i64`, found `u64`", and ~40 E0358 where an overload no
longer fits (`push` of a `u64` onto an `[i64]`); nothing else.

Done ahead of the switch:

- **Indexing** through `Index`, for `[T]` as for `Array<T>`, with the
  calls inlined across modules: `cryo check` on the compiler +3%.  Not
  inside a generic body: there `xs[i]` on a `[T]` still takes the
  compiler's built-in check, because the method bridge from `[T]` to
  `Array<T>` is keyed by a concrete element; the switch removes both.
- **The out-of-range report** names the `xs[i]`: the bounds check is
  `![caller_location]`.
- **A pointer to an array, indexed** is pointer arithmetic; the one site
  in the tree reads `(*sched)[i]` and the look-through is deleted.

What must keep working, each with a measurement before and after:
- **Fixed to dynamic conversion**, implicit today.
- **Display**: hover and diagnostics print `[T]` for `Array<T>` with the
  default allocator, or `Array<T>` everywhere - decided in this unit and
  reported.
- **Symbols**: every dynamic array in a signature changes from `A<elem>`
  to the `Array` path.  Proof (367): compile both trees, rewrite old names
  to new in the old IR, compare object for object; a control object whose
  code changed must differ.
- selfhost x2, then `wsl --shutdown`, then a repin.

### 3.3 Bug 0045

Whether the switch fixes the nested crash; if not, fixed on its own.  A
test that crashes on the old compiler and passes on the new.

### 3.4 Cleanup

Dead builtin paths deleted; `cryo.md` 2.4 states one dynamic array type,
`[T]` the spelling of `Array<T>`, indexing through `Index`; the mangling
spec drops the dynamic `A` row; the roadmap item goes.

## 4. Known defects met on the way

- `xs[i] = v` does not release the displaced element, for the builtin and
  for `Array<T>` alike - as `cryo.md` states for a run-time index.  The
  switch must keep this identical, not fix it silently.
- Binary and unary operators on a generic container (`a + b` with
  `a: Vec<T>`) in a generic body have the gap indexing had: only a bare
  bounded parameter is desugared there.

## 5. Open questions (for Jake)

1. **The type of an array's `length`.**  A) `Array<T>`'s `length` and
   `capacity` become `i64`, as every `[T]` in the tree was written
   against, and the library adapts inside `array.cryo`.  B) they stay
   `u64` and the ~2,400 sites in the tree that compare or pass `.length`
   as an `i64` are rewritten.  Lean A.
