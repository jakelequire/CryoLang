# Cryo Standard Library Rebuild — Plan

> **Status:** Phase 1 in progress (as of 2026-04-22).
> **Context:** The original `stdlib/` feels like it was written in the shape of a
> traits-based stdlib (think Rust) but without the compiler support to back it
> up — resulting in inconsistent APIs, unsound smart pointers, null-terminated
> strings, a HashMap that makes the caller supply the hash, and a pervasive
> "C translated to Cryo" flavor. This directory (`new_stdlib/`) is the rebuild.
> Follow this document when picking up the work again.

---

## 1. What the audit found (for future reference)

Full audit: `/tmp/cryo_stdlib_audit.md` (if the tmpfile is gone, re-run the audit
agent against `stdlib/`). Load-bearing findings that drove the decisions in this
plan:

**Ship-blockers in the old tree:**
- `core/ptr.cryo:223-246` — `Rc<T>::clone()` does non-atomic refcount ops.
- `core/ptr.cryo:285-293` — `Weak<T>::upgrade()` has a TOCTOU race.
- `core/result.cryo:320-327` — `Result::flatten()` returns `Ok(inner)` where it
  should return `inner`. The dead code after the exhaustive match hints it was
  never tested.
- `core/mem.cryo:239-248` — `transmute` has no size validation; OOB read/write.
- `collections/hashmap.cryo:149-162` — `get/insert/remove` all take `key_hash` as
  a parameter. The caller computes the hash. This is the single most un-stdlib
  thing in the old tree.
- `collections/hashmap.cryo:250-254` — `remove` marks `BUCKET_DELETED` without
  dropping the value. Silent memory leaks for `V = String`, `V = Array`, etc.
- `core/option.cryo:45-54` — `contains()` disabled with a TODO about codegen
  failing when `T = ()`. Symptom of the "methods-instantiated-for-all-T"
  compiler limitation.

**Pervasive structural weaknesses:**
- No iterator protocol; every collection invents its own iteration.
- No allocator abstraction; everything goes through libc `malloc`.
- No formatting protocol; everything is printf-style `sprintf` splicing.
- `collections/string.cryo` stores a trailing `\0` and has O(n) `nth_char` with
  no caching or iterator fallback.
- `ffi::NullTerminatedArray` is the poster child: every op is O(n) because the
  type refuses to store a length.
- `io::IoResult<T>` is a parallel universe to `core::result::Result`. Confusing.
- Inconsistent naming (`len` vs `length`, overloaded `new()`, overloaded `push`).

---

## 2. Locked-in design decisions

These are the decisions agreed with the user on 2026-04-22. Do not re-litigate
without a good reason.

### 2.1 Traits: deferred to `cryoc` stage 3

**The current C++ bootstrap compiler cannot handle a trait system, and the user
is migrating off it.** Adding traits happens in `cryoc` after stage 3. So the
Phase 1 stdlib is written *without* traits.

Practical implications:
- No `Iterator`, `Hash`, `Display`, `Ord`, `Clone`, `Default`, `Eq`, `Drop`,
  `Allocator` as traits.
- Polymorphism is either (a) concrete monomorphized generics with implicit
  operation requirements on `T`, or (b) class inheritance + virtual dispatch.
- Collections rely on monomorphization: methods that need `==` on `T` simply
  use `==` and depend on it being valid for the instantiated `T`. Methods that
  would break under certain `T` (`T = ()`, non-equality types) get documented
  with "requires T supports ==" and provided alongside `_by(fn)` alternatives
  that take a predicate.

When traits land in cryoc, the transition path is:
1. Retroactively annotate method signatures with the required bounds.
2. Convert `_by(fn)` predicate methods into trait-bound methods where
   applicable (keep the `_by` forms — they're still useful).
3. Replace `Hasher` and similar free-function pattern with trait impls.

### 2.2 Drop / RAII: deferred to `cryoc` stage 3

Until the compiler auto-inserts destructors at scope exit, resource types
(`Box`, `String`, `Array`, `File`) expose a `drop(mut &this) -> void` method
that the user must call manually. Every such method must be documented with
**"ownership transfers out / must be called"** language so it's obvious.

When Drop lands:
1. Remove the manual `drop()` calls throughout stdlib and examples.
2. Mark `drop(mut &this)` as the auto-inserted destructor via whatever
   `#[drop]` or trait mechanism cryoc adopts.
3. Audit for double-drop bugs — the manual-drop era code may have patterns
   that break once drop is automatic.

### 2.3 Allocator parameterization

Collections are *eventually* generic over allocator: `Array<T, A = GlobalAlloc>`.
Without traits, `A` is a concrete type that must expose specific methods
(`allocate`, `deallocate`, `reallocate`). Documented by contract, not enforced
by the compiler. This is acceptable — monomorphization makes it work.

**Phase 1 does NOT introduce the allocator generic.** Collections come in
Phase 4 and the allocator comes with them. Phase 1 is pre-collection.

### 2.4 API naming: prefer the longer form

User preference. Specifically:
- `length()` — **not** `len()`.
- `capacity()` — good, already longer.
- `is_empty()`, `is_some()`, `is_none()` — keep, these aren't abbreviations.
- `push()` / `pop()` — these are idiomatic verbs, keep.
- `unwrap`, `expect`, `map`, `and_then`, etc. — these are proper names of
  operations, not abbreviations. Keep.
- Never use `cap`. Always `capacity`.
- Never use `buf` in public API; `buffer` in public API.
- Never use `idx`; `index`.
- Naming is a public-API concern — `mut i: u64` as a loop counter is fine.

### 2.5 `IoResult` dies

Old stdlib has `io::IoResult<T>` as its own enum. The rebuild uses
`Result<T, io::IoError>` everywhere. One Result type, module-specific error
types.

### 2.6 Breaking changes are fine

The user has confirmed there is no external code depending on the current
stdlib API. Aggressively break whatever needs breaking. No compat shims, no
deprecation aliases.

### 2.7 Panic vs. Result boundary

Firm rule:
- **Panic = invariant violation.** `array.get_unchecked(i)` with i >= length.
  Explicit unwrap. Assertion failure. Internal bug.
- **Result = runtime condition the caller is expected to handle.**
  I/O errors. Allocation failures (once allocators are parameterizable).
  Parse errors. Not-found lookups should return `Option`.
- **Allocation failure in pre-Allocator code:** for now, panic. The TODO
  above the malloc site says "panics on OOM; will become Result when
  allocators arrive."

### 2.8 No null-terminated native types

NUL is a C-ABI thing. It lives only in `ffi::CStr` / `ffi::CString`. Every
native type stores `(data, length)`. `String` has no trailing `\0`. If the
user wants a `c_str` view they call `string.to_cstring()` which allocates.

### 2.9 Doc comments document WHY, never WHAT

`/// Returns the length.` over `length(&this) -> u64` is noise. Delete.
Doc comments must earn their space by stating:
- Non-obvious invariants (e.g., "O(1); see Section 4.2 for why").
- Ownership and lifetime expectations ("caller takes ownership of the buffer").
- Requirements on the type parameter ("requires T supports == — see
  Section 2.1 on traits").
- Surprising edge cases ("returns None if self was already empty, not 0").

Module-level `///!` comments are different: they summarize what the module is
*for*, which is genuinely useful context. Keep those.

### 2.10 Private by default

Struct fields default to private. Public API is method-based. No user-mutable
invariants (no public `ptr`, `length`, `capacity` on `Array`).

---

## 3. Architecture (target shape)

```
new_stdlib/
├── PLAN.md                      # this file
├── lib.cryo                     # std namespace root; declares top-level modules
├── prelude.cryo                 # auto-imported: Option, Result, Array, String, Box, print, panic, assert
│
├── core/                        # foundation, no deps outside of core and intrinsics
│   ├── _module.cryo
│   ├── intrinsics.cryo          # compiler intrinsics: malloc/free/memcpy/panic/etc.
│   ├── panic.cryo               # panic, assert, debug_assert, unreachable, unimplemented, todo
│   ├── primitives.cryo          # implement blocks on i8..i128, u8..u128, f32/f64, boolean, char
│   ├── option.cryo              # Option<T>
│   ├── result.cryo              # Result<T, E>
│   ├── error.cryo               # Error struct — the "standard" error; used by io, fs, etc.
│   ├── slice.cryo               # Slice<T> — (ptr, length) view. Load-bearing.
│   ├── ptr.cryo                 # NonNull<T>, raw ptr arithmetic helpers
│   ├── mem.cryo                 # size_of, align_of, copy, zero, swap, replace, take, forget
│   ├── cmp.cryo                 # [Phase 2] Ordering, min, max, clamp, compare helpers
│   ├── ops.cryo                 # [Phase 2] Range<T>, RangeInclusive<T>
│   ├── iter.cryo                # [Phase 2] Iterator "protocol" (convention-based until traits land)
│   ├── convert.cryo             # [Phase 2] numeric conversion helpers
│   └── num.cryo                 # [Phase 2] numeric utilities (abs, signum, gcd, log, pow)
│
├── alloc/                       # [Phase 3]
│   ├── _module.cryo
│   ├── allocator.cryo           # GlobalAlloc concrete type + contract doc for custom allocators
│   ├── layout.cryo              # Layout struct with checked arithmetic
│   ├── box.cryo                 # Box<T, A = GlobalAlloc>
│   ├── arena.cryo               # Arena<A>
│   ├── pool.cryo                # Pool<T, A>
│   ├── rc.cryo                  # Rc<T> — single-threaded refcounted (document loudly)
│   └── arc.cryo                 # [Deferred] Arc<T> — needs atomic intrinsics in cryoc
│
├── collections/                 # [Phase 4]
│   ├── _module.cryo
│   ├── array.cryo               # Array<T, A> — private fields; slice-based algorithms
│   ├── slice_ops.cryo           # sort, binary_search, split, chunks, windows on Slice
│   ├── string.cryo              # String — UTF-8, (data, length), no trailing NUL
│   ├── str.cryo                 # Str — borrowed UTF-8 slice view
│   ├── hashmap.cryo             # HashMap<K, V, A> — internal hash function, not caller-supplied
│   ├── hashset.cryo             # built on HashMap
│   ├── btree.cryo               # BTreeMap / BTreeSet
│   └── deque.cryo               # ring-buffer Deque
│
├── fmt/                         # [Phase 5]
│   ├── _module.cryo
│   ├── formatter.cryo           # Formatter struct (width, precision, fill, align)
│   ├── write.cryo               # Write contract (methods on the formatter)
│   └── impls.cryo               # concrete format methods for primitives
│
├── io/                          # [Phase 5]
│   ├── _module.cryo
│   ├── error.cryo               # IoError — reuses core::error::Error
│   ├── stdio.cryo               # stdin/stdout/stderr; real locking; buffered
│   ├── buffered.cryo            # BufReader, BufWriter
│   └── cursor.cryo              # in-memory Read/Write over Slice
│
├── fs/                          # [Phase 5]
│   ├── _module.cryo
│   ├── file.cryo                # File: read/write/seek
│   ├── path.cryo                # Path / PathBuf with platform-aware separators
│   ├── dir.cryo                 # DirEntry, read_dir → iterator
│   └── metadata.cryo
│
├── math/                        # [Phase 5]
├── time/                        # [Phase 5]
├── env/                         # [Phase 5]
├── process/                     # [Phase 5]
├── os/                          # [Phase 5]
└── ffi/                         # [Phase 5]
    ├── _module.cryo
    ├── cstr.cryo                # CStr (borrowed), CString (owned). The ONLY NUL-terminated types.
    ├── c_types.cryo             # c_int, c_long, c_size_t, etc.
    └── raw.cryo                 # unsafe FFI helpers
```

Notes on what's intentionally *gone* from the old tree:
- `core/ptr::Rc`, `Weak`, `Shared` — move to `alloc/rc.cryo` where they belong.
- `ffi::NullTerminatedArray` — deleted outright. If you need to walk a
  `char**`, use `Slice<CStr>` explicitly.
- `io::IoResult<T>` — replaced by `Result<T, io::IoError>`.
- `core::ops::Fn0..Fn3` — deleted. Cryo has first-class function types;
  `(T) -> U` does the job.

---

## 4. Phase roadmap

### Phase 0 — Compiler prerequisites (deferred, gates Phase 2+)

Not blocking Phase 1. These are what `cryoc` stage 3 needs to deliver:
- `trait` + `impl Trait for Type` syntax (static dispatch only, no dyn).
- Trait bounds on generic parameters (checked at monomorphization).
- `Drop` trait with compiler-emitted destructor calls at scope exit.
- Atomic intrinsics (`atomic_load`, `atomic_store`, `atomic_add`,
  `atomic_cas`) for Arc.
- Fix the "methods instantiated for every `T` including impossible ones"
  codegen limitation, or provide a mechanism to gate methods on bounds.

### Phase 1 — Foundation (trait-free, active now)

No traits, no allocator generic, no Drop. Just clean the existing core.

Deliverables:
- `lib.cryo`, `prelude.cryo`
- `core/_module.cryo`, `core/intrinsics.cryo`, `core/panic.cryo`
- `core/primitives.cryo`
- `core/option.cryo` (fix dead code, remove disabled `contains`, add
  `contains_by(predicate)`)
- `core/result.cryo` (fix `flatten()` bug, single `Error::new`)
- `core/error.cryo` (extracted from result)
- `core/slice.cryo` (new; load-bearing for the whole rebuild)
- `core/ptr.cryo` (NonNull + raw helpers only; no smart pointers)
- `core/mem.cryo` (fix `transmute` to assert sizes; redesign MaybeUninit
  without the boolean flag)

Success criteria: each module compiles standalone in a dummy project; API
is documented; all known bugs from the audit for these modules are fixed.

### Phase 2 — Trait wave (blocked on Phase 0)

Add once traits exist:
- `core/cmp.cryo` — `Ord`, `PartialOrd`, `Eq`, `PartialEq` traits + primitive
  impls.
- `core/ops.cryo` — `Range`, `RangeInclusive` with `Iterator` trait impl.
- `core/iter.cryo` — `Iterator` trait + adapters (`Map`, `Filter`, `Take`,
  `Skip`, `Zip`, `Chain`, `Enumerate`).
- `core/clone.cryo` — `Clone`.
- `core/default.cryo` — `Default`.
- `core/hash.cryo` — `Hash` + `Hasher`; default hasher (SipHash or FxHash).
- `core/convert.cryo` — `From`, `Into`, `TryFrom`, `TryInto`.
- `core/error.cryo` — upgrade to `Error` trait with source-chain support.
- `core/marker.cryo` — `Copy`, `Send`, `Sync`.

Retrofit Phase 1 types with trait impls.

### Phase 3 — Alloc (blocked on Phase 0 Drop)

`alloc/allocator.cryo`, `alloc/layout.cryo`, `alloc/global.cryo`,
`alloc/box.cryo`, `alloc/arena.cryo`, `alloc/pool.cryo`, `alloc/rc.cryo`.
`alloc/arc.cryo` waits on atomic intrinsics.

### Phase 4 — Collections (blocked on Phase 3)

`Array<T, A>`, `String`/`Str`, `HashMap<K, V, A>`, `HashSet`, `BTreeMap`,
`Deque`. Every collection: private fields, slice-based algorithms,
allocator parameterized, iterator trait impl, Drop trait impl.

### Phase 5 — Formatting + I/O + the rest

`fmt`, `io` (buffered stdio, real locking), `fs`, `math`, `time`, `env`,
`process`, `os`, `ffi/cstr`.

### Phase 6 — Migration

- Swap `stdlib/` → `new_stdlib/` in the compiler's search path.
- Run full test suite; fix fallout.
- Update examples to new API.
- Delete old `stdlib/`.

---

## 5. Conventions locked for the rebuild

Codify these in every file so the stdlib reads as one voice, not fifteen.

### 5.1 File structure

Every `.cryo` file in the stdlib follows this structure:

```cryo
///! Module-level doc: what this module is FOR.
///!
///! One or two paragraphs max. Describe the role the module plays in the
///! stdlib; point to the relevant section of docs/cryo.md if there's a
///! language feature users need to understand the module.

namespace std::path::to::module;

import <whatever>;

// ============================================================================
// <section title>  — use these banners sparingly; only when the file is long
// ============================================================================

// <declarations — types, constants, functions, implement blocks in that order>
```

### 5.2 Naming

- Types: `PascalCase`. `Array`, `HashMap`, `IoError`.
- Functions/methods/fields: `snake_case`. `push_back`, `read_line`.
- Module names: `snake_case` singular. `collections/array.cryo`.
- Constants: `SCREAMING_SNAKE_CASE`. `POINTER_SIZE`, `MAX_ALIGN`.
- Generic parameters: single uppercase letter. `T`, `E`, `K`, `V`, `A`.
- Predicates: start with `is_`. `is_empty`, `is_some`, `is_aligned`.
- Getters: no `get_` prefix unless disambiguation demands it.
  `array.length()` not `array.get_length()`.
- Fallible lookups: return `Option`, do not prefix `try_`.
  `hashmap.get(key) -> Option<V>`.
- Fallible anything-else: return `Result`, prefix `try_` if there's a
  panicking version alongside. `array.get(i) -> Option`, `array.try_index(i) -> Result`,
  `array[i]` panics.

### 5.3 Methods

- Immutable self: `name(&this, ...)`.
- Mutable self: `name(mut &this, ...)`.
- Static (no self): `static name(...)`.
- Consuming self (rare): `name(this, ...)`. Document the ownership transfer.
- Always put `&this` / `mut &this` / `this` first; never after a regular
  parameter.
- Return types are always explicit, even `-> void`.

### 5.4 Generic parameter requirements

Because there are no trait bounds to enforce, each method that requires
operations on `T` must state the requirement in its doc comment:

```cryo
/// Requires `T` to support `==`.
contains_eq(&this, value: T) -> boolean { ... }
```

Where possible, provide a `_by(fn)` alternative that takes a predicate and
has no requirement:

```cryo
contains_by(&this, predicate: (T) -> boolean) -> boolean { ... }
```

When traits land, `contains_eq` becomes `contains(...) where T: Eq` and the
`_by` form stays around for custom predicates.

### 5.5 Panic messages

Always include the call site's type and method in the panic message:

```cryo
panic("called Option::unwrap() on a None value", FILE, LINE);
```

Not: `panic("unwrap failed", FILE, LINE);`

### 5.6 Match exhaustiveness

Match statements in stdlib are always exhaustive. No trailing dead code
(`return Option::None;` after an exhaustive match over `Option`). If the
compiler complains about missing return, use `unreachable()` from panic.cryo.

### 5.7 Comments

- Never document the obvious. `/// Returns the length.` above
  `length(&this) -> u64` is noise.
- Do document non-obvious invariants, ownership, edge cases, performance
  surprises.
- Do not reference PRs, issues, or the current refactor in code comments.
  That belongs in commit messages.

---

## 6. Tracking: what's done

- [x] Phase 0 compiler prerequisites — **deferred to cryoc stage 3**
- [ ] Phase 1 — foundation
  - [ ] `lib.cryo`
  - [ ] `prelude.cryo`
  - [ ] `core/_module.cryo`
  - [ ] `core/intrinsics.cryo`
  - [ ] `core/panic.cryo`
  - [ ] `core/primitives.cryo`
  - [ ] `core/option.cryo`
  - [ ] `core/result.cryo`
  - [ ] `core/error.cryo`
  - [ ] `core/slice.cryo`
  - [ ] `core/ptr.cryo`
  - [ ] `core/mem.cryo`
- [ ] Phase 2 — trait wave
- [ ] Phase 3 — alloc
- [ ] Phase 4 — collections
- [ ] Phase 5 — formatting, I/O, the rest
- [ ] Phase 6 — migration

When resuming: check this section first, check the most recent commit,
and look for any TODOs in the code that mark "wait for traits" or "wait
for Drop" spots.
