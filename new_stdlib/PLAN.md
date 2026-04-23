# Cryo Standard Library Rebuild — Plan

> **Status:** Phase 1 complete, Phase 2 in progress (2026-04-22).
> **Why this exists:** The original `stdlib/` feels hacked together —
> null-terminated strings, `HashMap::get(key, hash)` with caller-supplied
> hashes, non-atomic `Rc`, O(n) `NullTerminatedArray`, O(n) `nth_char`. The
> root cause is that the old code was written as if Cryo had Rust's trait
> system and then patched when it didn't. This directory is the rebuild,
> designed around where Cryo is going.

---

## 1. Design principles

These are load-bearing. Follow them when adding anything new.

1. **A helper earns its place or it doesn't exist.** The old stdlib is full
   of methods that add noise without adding power: `x.is_zero()` when
   `x == 0` works, `some(x)` when `Option::Some(x)` works, `b.and(other)`
   when `b && other` works, `x.default()` when `0` works. Phase 1 deletes
   those. Every method must either aggregate a non-obvious check, express
   something operators can't, or be used often enough that inlining it
   would make call sites worse.

2. **Ownership is explicit and documented.** Every method that transfers
   or releases ownership says so in its doc comment. No hand-waving.

3. **Panic is for broken invariants; Result is for conditions callers
   handle.** `option.unwrap()` panics. `file.open()` returns `Result`.
   `array[i]` out of range panics; `array.get(i)` returns `Option`.
   Mixing these within a single operation is a bug.

4. **No null-terminated native types.** NUL lives only at the C ABI
   boundary, in `ffi::CStr` / `ffi::CString`. Every other type is
   length-typed: `(data, length)`.

5. **Length-typed slices are load-bearing.** Algorithms operate on
   `Slice<T>`, not on `Array<T>`. Every collection exposes `as_slice()`.

6. **Private fields, method-based access.** Structs do not publish their
   internals unless there is a specific reason. Users should not be able
   to mutate `array.length` directly.

7. **One name per concept.** `length`, not `len`. `capacity`, not `cap`.
   `buffer`, not `buf`. `index`, not `idx`. User preference.

8. **Doc comments document WHY, not WHAT.** `/// Returns the length.`
   above `length(&this) -> u64` is noise. Delete. Document non-obvious
   invariants, ownership contracts, performance surprises, and
   requirements on type parameters.

9. **Module-level `///!` comments are different** — they describe what
   the module is for and should exist on every file.

---

## 2. Key decisions (locked 2026-04-22)

### 2.1 Traits: write to the spec, not the current compiler

The stdlib uses traits from Phase 2 onward even though the current C++
bootstrap compiler can't parse them. `cryoc` stage 3 adds trait support;
until then, this code doesn't compile. That's accepted — the stdlib is
the spec, not a 2026-Q2 snapshot.

**Assumed trait syntax** (subject to final cryoc decisions):

```cryo
type trait Eq {
    equals(&this, other: &Self) -> boolean;
}

type trait Ord : Eq {                // supertrait bound
    compare(&this, other: &Self) -> Ordering;
}

implement Eq for i32 {
    equals(&this, other: &i32) -> boolean {
        return this == *other;
    }
}

function sort<T>(arr: Slice<T>) -> void
    where T: Ord {                   // inline where clause, per cryo.md
    ...
}

implement<T> Iterator<T> for Range<T> where T: Step {
    ...
}
```

Notes:
- `Self` inside a trait refers to the implementing type.
- Default methods are trait methods with bodies; required methods have
  none.
- Trait objects / dynamic dispatch are **not** used. Everything is
  static dispatch via monomorphization.
- No associated types in Phase 2. Traits that need "an associated type"
  (e.g., `Iterator`) take it as a type parameter instead
  (`Iterator<Item>`).

When cryoc's actual trait syntax diverges from the above, the stdlib
updates to match. The design doesn't change, just the spelling.

### 2.2 No Drop in Phase 1

`cryoc` stage 3 also adds auto-inserted destructors. Until then,
resource types expose `drop(mut &this)` that callers must invoke
manually. Every such method's doc comment makes that obligation
explicit.

### 2.3 Allocator parameterization

Collections will be generic over allocator (`Array<T, A = GlobalAlloc>`)
in Phase 4. Phase 1 is pre-collection, so this decision has no
immediate effect.

### 2.4 Naming

- `length`, not `len`.
- `capacity`, not `cap`.
- `buffer`, not `buf`.
- `index`, not `idx`.
- Predicates: `is_empty`, `is_digit`, `is_aligned`.
- Getters: no `get_` prefix unless disambiguation demands it.
  `array.length()`, not `array.get_length()`.

### 2.5 `IoResult` dies

Old stdlib has its own `io::IoResult<T>`. The rebuild uses
`Result<T, io::IoError>` everywhere.

### 2.6 Breaking changes are fine

No external code depends on the old stdlib API. Break whatever needs
breaking. No compatibility shims.

---

## 3. Architecture

```
new_stdlib/
├── PLAN.md
├── lib.cryo
├── prelude.cryo
│
├── core/                        # foundation, no external deps
│   ├── _module.cryo
│   ├── intrinsics.cryo          # malloc/free/memcpy/... + panic intrinsic
│   ├── panic.cryo               # panic, assert, unreachable
│   ├── primitives.cryo          # small method set on bool/char/ints/floats
│   ├── option.cryo              # Option<T>
│   ├── result.cryo              # Result<T, E>
│   ├── error.cryo               # Error struct (upgrades to trait in Phase 2)
│   ├── slice.cryo               # Slice<T> = (ptr, length)
│   ├── ptr.cryo                 # NonNull<T>
│   ├── mem.cryo                 # size_of, copy, zero, swap, align_*, transmute
│   │
│   │ # Phase 2 (traits):
│   ├── marker.cryo              # Copy, Send, Sync — marker traits
│   ├── clone.cryo               # Clone trait
│   ├── default.cryo             # Default trait
│   ├── cmp.cryo                 # Ordering, Eq, Ord; min/max/clamp
│   ├── convert.cryo             # From, Into, TryFrom, TryInto
│   ├── iter.cryo                # Iterator<Item> trait
│   ├── ops.cryo                 # Step, Range<T>, RangeInclusive<T>
│   └── hash.cryo                # Hash, Hasher, DefaultHasher (FNV-1a)
│
├── alloc/           [Phase 3]
├── collections/     [Phase 4]
├── fmt/             [Phase 5]
├── io/              [Phase 5]
├── fs/              [Phase 5]
├── math/            [Phase 5]
├── time/            [Phase 5]
├── env/             [Phase 5]
├── process/         [Phase 5]
├── os/              [Phase 5]
└── ffi/             [Phase 5]
```

Deliberately gone from the old tree: `NullTerminatedArray` (deleted outright);
`IoResult` (folded into `Result<T, IoError>`); `Rc`/`Weak`/`Shared` from
`core::ptr` (move to `alloc/rc.cryo` in Phase 3 with real atomics or a
loud single-threaded-only warning); `core::ops::Fn0..Fn3` (Cryo has
first-class function types; `(T) -> U` is enough); `MaybeUninit` with a
boolean flag (design is wrong without language support — defer).

---

## 4. Phase roadmap

- **Phase 0 — Compiler prerequisites** (cryoc stage 3): trait parsing +
  static-dispatch monomorphization with bounds, `Drop` with auto-
  inserted destructors, atomic intrinsics. Must land before the
  stdlib compiles, but not before it's written.
- **Phase 1 — Foundation** (complete): `core/` trait-free leaf modules.
- **Phase 2 — Traits wave** (active): `marker`, `clone`, `default`,
  `cmp`, `convert`, `iter`, `ops`, `hash`. Retrofit Phase 1 types.
- **Phase 3 — Alloc** (blocked on Drop): allocator, Layout, Box,
  Arena, Pool, Rc, Arc (Arc blocked on atomics).
- **Phase 4 — Collections**: Array, String + Str, HashMap, HashSet,
  BTreeMap, Deque.
- **Phase 5 — fmt, io, fs, math, time, env, process, os, ffi/cstr.**
- **Phase 6 — Migration**: swap stdlib/ → new_stdlib/ in the compiler's
  search path, fix fallout, delete old tree.

---

## 5. Phase 1 deliverables (complete)

- [x] `lib.cryo`
- [x] `prelude.cryo`
- [x] `core/_module.cryo`
- [x] `core/intrinsics.cryo`
- [x] `core/panic.cryo`
- [x] `core/option.cryo`
- [x] `core/result.cryo`
- [x] `core/error.cryo`
- [x] `core/slice.cryo`
- [x] `core/ptr.cryo`
- [x] `core/mem.cryo`
- [x] `core/primitives.cryo`

## 6. Phase 2 deliverables (complete)

- [x] `core/marker.cryo` — Copy, Send, Sync
- [x] `core/clone.cryo` — Clone
- [x] `core/default.cryo` — Default
- [x] `core/cmp.cryo` — Ordering, Eq, Ord, min/max/clamp
- [x] `core/convert.cryo` — From, Into, TryFrom, TryInto, ConversionError
- [x] `core/iter.cryo` — Iterator<Item>
- [x] `core/ops.cryo` — Step, Range, RangeInclusive + Iterator impls
- [x] `core/hash.cryo` — Hash, Hasher, DefaultHasher (FNV-1a)
- [x] Retrofit `option` / `result` / `slice` with trait impls
- [x] Upgrade `core/_module.cryo` to expose Phase 2 modules

Not included by design: `Iterator` adapters (`Map`/`Filter`/`Take`),
`PartialEq` / `PartialOrd` (floats use `==` directly; fine until someone
asks), `core::num` (pow/log/gcd), and an upgraded trait-based `Error`.
Each earns its module when a caller actually needs it.
