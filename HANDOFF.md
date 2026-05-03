# HANDOFF — drop the C++ bootstrap via stdlib swap

**Branch**: `new-stdlib` · **Working tree at handoff**: clean (this file is
the only untracked addition). The session goal next time is the **swap
itself**: point `stdlib/` at `experimental/stdlib-next/`, get the
self-hosted compiler to build itself + the new stdlib, then verify the
C++ bootstrap is no longer load-bearing.

## State at handoff

Last commits, in order:
- `315bd39d` stdlib-next: rename hash_map/hash_set → hashmap/hashset; refresh pin
- `9a2a1391` fix: module-qualified mangle for global constants
- `3e59eb90` stdlib-next: declare `format` intrinsic in core/intrinsics
- `49420090` fix: enum payload layout for empty-marker fields
- `167bca27` build: refresh pinned cryo binary after alignof fix
- `646210d6` fix: codegen_alignof falls back to LLVMAlignOf for InstantiatedType

Selfhost: `make selfhost-check` → 8/8 stages green, stage-4 ↔ stage-5
byte-identical, IR md5 **`192ca68a4de4e0c715cf75cee8f52fb3`**, IR size
18,911,303 bytes.

Bridge smokes (`/tmp/cryo-bridge-test/`): 24/24 green at last check
(directory has since been wiped by /tmp turnover; that's fine — the
selfhost-check is the load-bearing gate).

`bin/cryo` pinned at commit `9a2a1391` (size 1,542,288, sha256
`35d16eaedf434139c82498b037497c36523821d2818ef2b709f1fb2352c5d315`,
stripped). This pin **already has the qualified-global-mangle fix**, so
it can build stdlib-next cleanly without going through the bootstrap.

`experimental/stdlib-next/` builds clean (70 modules → libcryo.a) via
the pinned binary.

## Confidence in the swap: **MEDIUM**

The structural blockers from the previous handoff are gone:
- `format` extern declared (commit `3e59eb90`).
- Module-qualified global mangle landed (`9a2a1391`) — `INITIAL_CAPACITY`
  collisions cleared.
- HashMap file/namespace name now matches what compiler/src/ imports
  (`315bd39d`).
- Pin refreshed (`315bd39d`).
- `test_string` passes — the previous handoff's `String<A=GlobalAlloc>`
  default-expansion claim turned out to be a misattribution; once the
  qualified-mangle cascade was fixed, `String::new()` works.

What's still in the way is **API divergence inside `HashMap`'s methods**
(see "Blockers", §1). That's the largest single chunk of work next
session. The other items (vestigial `alloc::heap` import, swap mechanics,
verification) are mechanical.

## The plan, step by step

Each phase has a verification gate. **Stop and diagnose if a gate
fails** — don't paper over with a stub.

### Phase 1 — already done in the previous session

- ✅ `format` intrinsic declared in stdlib-next.
- ✅ Module-qualified global mangle live end-to-end.
- ✅ `hash_map.cryo` → `hashmap.cryo` (file + namespace + internal refs).
- ✅ Pin refreshed.

### Phase 2 — migrate `compiler/src/` HashMap callers to the trait API

stdlib-next's `HashMap` is **API-different** from the legacy stdlib.
The shapes are:

| Method      | Legacy (compiler/src/ uses)        | stdlib-next (target)            |
|-------------|------------------------------------|---------------------------------|
| `insert`    | `(K, V, key_hash: u64) -> Option<V>` | `(K, V) -> Option<V>`          |
| `get`       | `(K, key_hash: u64) -> Option<V>`  | `(&K) -> Option<V>`             |
| `contains_key` | `(K) -> boolean`                | `(&K) -> boolean`               |
| `remove`    | `(K, key_hash: u64) -> Option<V>`  | `(&K) -> Option<V>`             |
| `length`, `is_empty`, `clear` | unchanged          | unchanged                       |

stdlib-next routes hashing through the `Hash` trait (already implemented
for `u8 / u16 / u32 / u64 / i8 / i16 / i32 / i64 / boolean / char /
string / String / Str`); callers don't pass an explicit hash anymore, and
key arguments are passed by reference.

**Scope of change**: ~135 method calls across compiler/src/, of which
~26 pass an explicit `key_hash` arg. The rest are `length()`,
`is_empty()`, `clear()` — those compile unchanged. Mechanical migration:

```
this.func_returns.insert(name.id, return_type, h);
   ↓
this.func_returns.insert(name.id, return_type);
```

```
const x: Option<TypeRef> = this.cache.get(key.id, h);
   ↓
const x: Option<TypeRef> = this.cache.get(&key.id);
```

The local `const h: u64 = hash_int(name.id as u64);` declarations
become dead — delete them as you go.

**Non-mechanical bits to watch for:**
- `HashMap<string, V>::new()` keys: stdlib-next's `Hash for string` does
  a NUL-terminated byte walk (`feedback_bootstrap_string_concat.md`'s
  parent investigation already added this). Should just work.
- Some callers hold a key locally and pass it to multiple HashMap calls.
  Just inline `&local_key` at each call.

**Recommended order**: start with `compiler/src/compiler/decl_index.cryo`
(busy file, ~30 call sites), then `types/arena.cryo`,
`types/generic_registry.cryo`, `module_loader.cryo`,
`passes/default_expansion.cryo`. Build with `bin/cryo` after each file
to keep the diff bisectable.

**Gate**: `compiler/` builds against the legacy stdlib (still in place).
If selfhost-check is green after Phase 2 you've done the migration
without regressing the legacy path — that's the cleanest possible
checkpoint.

### Phase 3 — drop the vestigial `alloc::heap` import

In `compiler/src/compiler/instance.cryo:31` and
`compiler/src/compiler/compilation_context.cryo:38`:

```
import std::alloc::heap;
```

Grep confirms zero `heap::*` usage; the import is dead. Two options:

- **Preferred**: replace with `import std::alloc::box;` so `Box<T>` is
  visible by its idiomatic stdlib-next path. compiler/src/ does use
  `Box<T>::new(...)` widely (see `instance.cryo:340–355`). Today that
  resolves through the auto-prelude `Box` re-export from the legacy
  stdlib's `alloc/heap`; after the swap it'll resolve through
  stdlib-next's prelude → `alloc/box`. Either way, the explicit import
  isn't doing real work — but switching it to `box` aligns the file.
- **Acceptable**: just delete the import line. The auto-prelude still
  brings `Box` into scope.

**Gate**: same as Phase 2 — selfhost-check green against legacy stdlib.

### Phase 4 — perform the swap

The build infrastructure resolves stdlib via `<project_root>/../stdlib`
(see `compiler/src/compiler/instance.cryo:359` and
`legacy/bootstrap/src/Compiler/ModuleLoader.cpp:79–86`). Easiest
non-destructive swap is a symlink:

```bash
mv stdlib stdlib.legacy.tmp
ln -s experimental/stdlib-next stdlib
```

Then build the renamed stdlib + the migrated compiler:

```bash
# 1. stdlib (= stdlib-next via symlink) using the pinned binary
cd stdlib && rm -rf .bin && /home/phock/Programming/apps/CryoLang/bin/cryo build
cd ..

# 2. compiler/src/ against the new stdlib
cd compiler && rm -rf build && /home/phock/Programming/apps/CryoLang/bin/cryo build
```

Expect the first attempt to surface remaining mismatches (missing
methods, signature drift, default-expansion edge cases). Fix at the
root: the rule from `feedback_codegen_style.md` and
`feedback_bridge_quality.md` still holds — no `_compat_*` shims, no
`if (compiler_pass) { use_legacy } else { use_new }` branches.

**Verification ladder** once the build succeeds:

1. The new compiler/build/bin/cryo runs `cryo --version` and `cryo help`
   without crashing.
2. The new compiler/build/bin/cryo can rebuild stdlib-next (i.e.,
   stdlib via the symlink): `cd stdlib && /path/to/cryo build`.
3. The new compiler/build/bin/cryo can rebuild **itself**:
   `cd compiler && /path/to/cryo build`. This is the stage-2 of the new
   selfhost cycle.
4. Stage-2 binary rebuilds stdlib + compiler again → stage-3.
5. Stage-3 ↔ stage-4 IR byte-identity (the new fixed point).

The selfhost-check script today expects a 1+8 chain rooted at
`legacy/bootstrap`. Either teach `scripts/selfhost-check.py` about a
"no-bootstrap" mode (use `bin/cryo` as stage-1 and run the same chain)
or just run the chain manually for now and document the byte-identity
comparison.

### Phase 5 — confirm bootstrap is no longer load-bearing

Once Phase 4 reaches a fixed point, the C++ bootstrap is dispensable
from the build path. Two checkpoints to confirm before retiring it:

1. **Refresh the pin from the new chain's stage-3** (or whatever the
   first byte-identical stage is). `make pin-cryo` reads from
   `compiler/build/bin/cryo`. After the refresh, `bin/cryo` itself was
   produced without ever invoking `legacy/bootstrap/bin/cryo`. Commit
   the pin so a fresh clone can reach a working compiler in one step.
2. **Smoke test the bootstrap-free build**: `rm -rf
   legacy/bootstrap/bin && make cryo-fast && make selfhost-check-fast`
   (or the no-bootstrap equivalent). The chain should run clean. At
   that point the bootstrap can be moved to `legacy/` history and
   eventually deleted. Don't delete it in the same commit as the
   verification — leave one commit between for safe rollback.

The Makefile's `make cryo` target currently depends on `$(BOOT)` (the
C++ binary). Update it to depend on `$(PIN)` instead, and rename the
target to reflect the new bootstrap source. Mention this in the commit
message so the change is visible in `git log`.

## Blockers and divergences

### 1. HashMap method API — the only substantive code work

Already detailed in Phase 2. The user's instruction was clear: don't
re-add the explicit `key_hash` arg as a workaround on the stdlib-next
side; migrate the compiler. This is the bulk of the next session's
work — budget half a day.

The user's constraint "shouldn't need to change compiler code using the
hashmap" referred to the **type-arg defaulting** (`HashMap<u32, TypeRef>`
keeps working, no need to write `HashMap<u32, TypeRef, GlobalAlloc>`).
That part is already proven by the bridge smokes. The method-signature
migration is separate and required.

### 2. Default-arg expansion — proven for HashMap, watch for edge cases

`HashMap<u32, TypeRef>::new()` lowers to
`HashMap<u32, TypeRef, GlobalAlloc>::new()` via the default-expansion
pass. Bridge smokes confirm this for `<i32, i32>`, `<string, i32>`,
`<i32, ()>`. The shapes compiler/src/ uses are
`<u32, TypeRef>`, `<u64, TypeRef>`, `<u32, i64>`, `<u32, u32>`,
`<u64, u32>`, `<string, i64>`, `<string, string>` — all single-default
shapes, all already covered by the smoke surface.

If you do hit a default-expansion failure, look at the new compiler's
output of `--ast` for the affected file; the
`Compiler__Passes__DefaultExpansion` pass annotates the AST with
expanded type args, and a missing annotation is the smoking gun.

### 3. Bootstrap-C++ HashMap<string, V> pointer-compare bug — moot

The previous handoff flagged this as a hazard. It applies only when
the bootstrap-C++ compiler runs (its lowering of `string == string`
keys hits a pointer compare). After Phase 5 the bootstrap is no longer
in the chain, so the bug doesn't bite. The self-hosted compiler does
the right thing already (proven by `test_hashmap_str` passing).

### 4. `module_loader.cryo` and friends use small bespoke HashMap shapes

Watch `compiler/src/compiler/module_loader.cryo:47` —
`HashMap<string, string>` for namespace lookups. The string-keyed
codepath went through the bootstrap-bug-avoidance dance in earlier
sessions. With the self-hosted compiler in the driver's seat, that
pressure goes away — but the migration to the trait-based `get(&K)`
still needs `&key`, which for strings means `&import_path` etc. The
local pattern is straightforward; just be deliberate.

### 5. Nothing else, hopefully

The previous handoff's items 4 (nested `for { for { … } }` miscompile)
and 5 (string default-expansion) are the only other remembered hazards.
Item 5 is gone (test_string passes). Item 4 only triggers in specific
shapes that bridge smokes haven't hit yet; if it surfaces in the swap,
the workaround is `while` instead of inner `for` — but only after
opening an issue against the codegen.

## Order of operations + verification gates

| Step | Action | Gate |
|------|--------|------|
| 1 | Phase 2 — migrate HashMap call sites in compiler/src/ | `make selfhost-check` green at md5 X |
| 2 | Phase 3 — drop vestigial alloc::heap import | selfhost-check still green at md5 X (md5 may shift if you rewrite imports) |
| 3 | Phase 4 — symlink swap, build stdlib + compiler | new compiler binary builds; stage-2 self-rebuild succeeds |
| 4 | Phase 4 verification ladder | stage-3 ↔ stage-4 byte-identical |
| 5 | Phase 5 — pin refresh, bootstrap-free smoke | clean build from `bin/cryo` alone |
| 6 | Commit each phase separately | each commit individually selfhost-check-green where possible |

If any gate fails: stop, diagnose, fix the root cause. The bridge
smokes have been a reliable signal across the last two sessions; if
they break unexpectedly, the regression is most likely in the change
you just made, not pre-existing.

## Process notes (carry-forward)

- Selfhost gate: `make selfhost-check` (~3:30). Run after every
  codegen-source change, every stdlib API change, and before each
  commit. Catches both bootstrap-C++ regressions (until Phase 5) and
  compiler-source codegen bugs.
- Self-built cryo lives at `compiler/build/bin/cryo` after a build.
  After a fresh `selfhost-check`, the Makefile's `cryo-fast` target
  expects an intermediate `compiler/build/cryo` (STAGE2 location); a
  manual `cp compiler/build/bin/cryo compiler/build/cryo` re-seeds it.
  Filed mentally as a Makefile bug — fix as part of Phase 5's Makefile
  cleanup.
- **Bootstrap-C++ traps** (apply through Phase 4):
  - `(ty as SomeType*).field` chained access fails on Optional / Tuple
    / Enum types. Use a typed local first.
  - `string + lit + string` chained concat fails (any two-step `+` on
    strings in one expression). Build piecewise via `=` `+` or use
    `format()`. Both these shape constraints disappear once Phase 5
    lands and the self-hosted compiler is the only one in play.
  - `HashMap<string, V>` pointer-compare on keys; doesn't matter once
    the swap is complete.

## Memory pointers

`/home/phock/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/`

Most relevant for next session:

- `project_qualified_global_mangle.md` — full writeup of the mangle fix
  (this session, 2026-05-02).
- `feedback_bootstrap_string_concat.md` — the chained-concat trap.
- `feedback_codegen_style.md` — fix root causes, no inline string
  manipulation, no hacky workarounds.
- `feedback_bridge_quality.md` — verify selfhost-check each commit.
- `feedback_stdlib_api_stance.md` — historical: don't reshape stdlib-next
  APIs during bridge. The Phase 1 rename is consistent with the spirit
  of this (alignment, not a new API). Phase 2's HashMap migration is on
  the **compiler** side, not the stdlib side, so it doesn't violate the
  stdlib-API constraint.
- `feedback_test_whole_stdlib.md` — for stdlib changes, run
  `cd experimental/stdlib-next && cryo build` (or, post-swap,
  `cd stdlib && cryo build`).
- `project_pinned_binary.md` — pin discipline.

## TL;DR for next session

1. Migrate the ~26 explicit-hash `HashMap` calls in compiler/src/ to
   the trait API (`(K, V)` for insert, `&K` for get/contains_key/remove).
2. Drop the vestigial `import std::alloc::heap;` (or change to
   `alloc::box`).
3. Symlink `stdlib` → `experimental/stdlib-next`. Rebuild. Iterate.
4. When stage-3 ↔ stage-4 are byte-identical, refresh the pin and
   confirm `bin/cryo` alone can drive the chain. The C++ bootstrap is
   then dispensable.
