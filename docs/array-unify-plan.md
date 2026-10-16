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

## 2. Decided (rulings 363-378)

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
- A fill literal is refused unless its element is plain-copy (374), as
  E0453 (377).  Built.
- `Array`'s `length` and `capacity` are `i64` (375).
- A `![caller_location]` function taken as a value is refused before
  code generation (376), by the type check where a name or path is
  typed as a value; the attribute keeps its name (378).  Built.

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
Not done additively, on purpose:

- **`for` over a builtin `[T]`.**  It failed because the element-keyed
  bridge was empty when sema lowered the loop; it works since the switch.
- **`[T; N]` converted to `Array<T>`.**  Refused since the switch
  (section 3.2); whether to build it is open.

### 3.2 The type switch

`[T]` resolves to `TypeResolver::dynamic_array_type(T)`; the dynamic
`ArrayType`, the element-keyed bridge (`lookup_array_type*`,
`register_array_specialization`, the monomorphizer's arena scan for
dynamic arrays) and the codegen paths for the builtin are deleted.
`TypeKind::Array` keeps only the fixed array.

The type of `length` (375): the builtin's `.length` was an implicit
`i64` field and `Array<T>`'s a `u64`, so `[T]` resolved to `Array<T>`
reported 2,348 W0005 implicit `i64`/`u64` widenings, 177 E0200 and ~40
E0358 in the compiler.  `Array`'s `length` and `capacity` (fields and
methods) are `i64` now; its index and count parameters stay `u64`.

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

Built.  `[T]` resolves to `Array<T>` with the default allocator, the
instance demanded as a written `Array<T>` is (`TypeResolver::
dynamic_array_type`), out of the shape the template registration records
in the arena (`TypeArena::default_array_element` is the one test).  The
method bridge in sema is deleted; the remaining builtin paths go in the
cleanup.  Measured before and after on probes:

- **Fixed to dynamic conversion** was accepted and miscompiled: the
  fixed array's bytes were read as the dynamic one's pointer, length and
  capacity (`[10, 20, 30]` gave a length of 30, and indexing it crashed).
  It is refused now (E0200), as `Array<T>` refused it.
- **Generic bodies** over `&[T]` were an internal error (E0900, no
  resolved function); they compile and index through `Index`.
- **Calling convention**: a `[T]` passed or returned by value was split
  into three registers; `Array<T>` follows the aggregate rules.  Made
  alike before the switch, so the switch itself moved names only.
- **Display** (369): `[T]` for an inferred type, the written spelling
  where an annotation declared it (declaration mismatches in the
  compiler, hover and signature help in the editor).
- **Symbols**: proved by `scripts/array-rename-proof.py` (367).

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
- The nested-array crash (`[[i32]]` with a row pushed) was the
  pointer-to-array look-through: `Array<T>`'s own `this.ptr[i]`, with `T`
  an array, indexed the first row instead of stepping over rows.  Gone
  with the look-through; `nested_dynamic_array` pins it.

## 5. Emitting every method of an instantiated generic: accepted

Every `Array<T>` instantiation emits all its methods, the eleven `index`
and `element_at` among them, used or not.  Measured: the stripped
Windows compiler grew 8,128,512 -> 8,227,328 bytes (+1.2%) when they were
added, the Linux one 6,966,096 -> 7,060,304; the 1.8 MB growth of the
unstripped binary is almost all symbol names.  Build time did not move
measurably.  Linking with `--gc-sections` frees 0.24 MB unstripped and
20 KB stripped and keeps all 1,500 `index` definitions (the PE linker
keeps them).  Emitting only used methods needs reachability over every
reference to a specialization's methods, codegen's own synthesized
calls (drop glue, language-item calls) among them, where a missed one is
a link failure: much work for about 1% of the binary.
