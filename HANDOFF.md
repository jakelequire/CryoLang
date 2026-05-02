# HANDOFF — stdlib bridge, take 2

**Status as of this handoff.** Today's branch (`new-stdlib`) was reverted in
its entirety. The repo is back to the state where:

- `./stdlib/` is the **legacy** stdlib (the one `bin/cryo` is built against and
  links cleanly).
- The **new stdlib** lives at its experimental location and `cryoc build` (run
  inside that directory) compiles it cleanly into a `libcryo.a`.
- The compiler at `compiler/src/` builds against the legacy `./stdlib/`. Self-
  host loop (bootstrap C++ → stage-2 → stage-3, or PIN → stage-2 → stage-3) is
  presumed working in this state. Verify with `make cryo-fast` before doing
  anything else.

No commits land from yesterday. The lessons below are the only things worth
keeping.

## TL;DR — the real shape of the bridge work

The goal is to swap the new stdlib into `./stdlib/` and have the compiler
consume it. **The previous handoff's reported blocker (`error[E0147]:
codegen: no method 'push' found on type '9[]'`) was a misdiagnosis.** The
actual root cause was: the new prelude (`stdlib/prelude.cryo`) doesn't
`public module collections::array;`. PIN's `T[]` → `Array<T>` bridge in
`monomorphizer.cryo` and `specialization.cryo` already exists and is
correct; it just silently no-ops when `generic_registry.get_template(
"std::collections::array::Array")` returns null because the array module
was never loaded. **Fix in one line.** The same applies to anything else
the language desugars to a stdlib type — if the type's module isn't in the
prelude, the bridge has nothing to find. The legacy prelude pulled
`collections::array`, `collections::string`, `alloc::heap`, `io::stdio`,
plus the panic helpers. The new prelude is intentionally slim — but the
slim line has to be drawn around things the *language itself* needs, not
just things the user code names explicitly.

After that one-liner, the actual work is real but contained. It's a sequence
of adapter changes in the new stdlib + a few specific PIN bugs that need to
be fixed *first* (in committed source, with a PIN refresh) before the swap
can land cleanly.

## What yesterday actually was

Yesterday's session attempted the swap end-to-end in a single push: swap the
stdlib directories, sweep compiler source for API compatibility, refresh PIN,
ship. It got within sight of green:

- `cd compiler && ../bin/cryo build` linked cleanly with **0 unresolved
  symbols** at the end of the day.
- Trivial `mut arr: i32[] = []; arr.push(42);` compiled, linked, ran
  (`exit 0`).
- `bin/cryo` was successfully refreshed once (PIN → stage-2 → stage-3 chain
  completed).

Then `make stdlib-fast` against the *refreshed* PIN failed with an LLVM
verification error in `std::io::stdio` — distinct symptom from anything
seen earlier — and the chain broke. By that point the workarounds had
piled up (~10 stdlib files modified, ~3 compiler-source edits, the previous
PIN binary overwritten with no backup). Reverting was the right call.

## Why the all-at-once approach didn't work

1. **The sweep cascades.** Each new-stdlib API change (`HashMap` rename,
   `libc::` prefix, dropping the precomputed-hash arg, requiring explicit
   `, GlobalAlloc`) propagates to every compiler-source caller. By the time
   one set of edits is in place across 40 source files, you can't easily
   bisect which change introduced which symptom.

2. **PIN refresh is a one-way door.** Until the new PIN is committed, the
   *old* `bin/cryo` is the only thing that can produce a working binary
   from current sources. `cp` over it without backing it up first means
   recovery requires the C++ bootstrap path, which itself is in flux.

3. **The new stdlib's shape exercises codegen paths PIN wasn't pressure-
   tested against.** Default allocator type args (`HashMap<K, V, A =
   GlobalAlloc>`), generic free functions called through `ScopeResolution`
   (`mem::offset(...)`), generic methods on primitive receivers
   (`primitive.hash<H>(...)`), and one-element struct array literals
   (`[scope_value]`) all surface codegen bugs that legacy stdlib never hit.
   You can't avoid these by API design alone — they need fixing in PIN.

4. **Workarounds and real fixes look identical from the outside.** Adding
   `mem::offset_bytes` so that `Slice::subslice` doesn't depend on
   monomorphizing `mem::offset<T>` through a `ScopeResolution` callee is a
   workaround. Fixing `try_infer_function_call` to handle `ScopeResolution`
   callees is the real fix. Mixing the two without committing each
   separately means the next session can't tell which is which.

## The specific PIN bugs that need fixing FIRST

These are the things that bit yesterday and that should be fixed in
committed source + PIN refresh **before** attempting the swap. Each is a
real compiler bug, not a stdlib API question. Each can be reproduced and
fixed against the *legacy* stdlib in isolation, then verified before any
swap is attempted.

### 1. `visit_dynamic_array_literal` falls back to `elem_size = 1`

`compiler/src/compiler/codegen/ir_generator.cryo:2503-2507` (in yesterday's
state — line numbers may shift):

```cryo
const elem_t: Type* = this.cg.lookup_type_by_id(arr_type.element.id);
mut elem_size: u64 = 1;
if (elem_t != null && elem_t.size_bytes() > 0) {
    elem_size = elem_t.size_bytes();
}
```

When `Type::size_bytes()` returns 0 — which happens for a struct whose
layout hasn't been computed yet at the point a `[ X ]` literal is codegen'd
— this falls back to `1`. Then `malloc(N * 1)` is emitted for an N-element
array of structs. For `[global]: Scope[]` in `Resolver::new`, the
overrun is hundreds of bytes; valgrind catches it as `Invalid write of
size 4/8/8 at offsets 7/15/16 of a 1-byte block alloc'd by Resolver::new`
and glibc abort with `malloc(): corrupted top size` at the next
allocation.

**Fix:** compute element stride from the LLVM type at codegen time,
not from `size_bytes()`. The standard idiom is GEP-on-null: index
`(elem_ty*) null` by 1, cast to `i64`, and that's `sizeof(elem_ty)`
in bytes — works regardless of whether layout was computed in the
arena.  Backup is to add `LLVMABISizeOfType` to `llvm_bindings.h` and
use it via the target data layout.  Either is a one-function change
to ir_generator.cryo.

This bug is *latent* with the legacy stdlib (legacy `Resolver::new` uses
`[global]` too, and it presumably hits this path) but doesn't manifest
because legacy struct layouts happen to be computed by the time codegen
runs on Resolver::new. The new-stdlib swap reorders module processing
enough that Scope's layout isn't ready in time. Fix the codegen, the
ordering question goes away.

### 2. `Type::size_bytes()` returning 0 for layout-not-yet-computed structs

This is the *cause* the codegen falls back to 1. Even if (1) is fixed,
audit `compute_layout` in `compiler/src/compiler/passes/type_lowering.cryo`
to make sure it iterates to fixed-point: a struct whose field's size is 0
returns false ("try later") but the outer iteration may not actually
retry. With the new stdlib, `Scope` contains `HashMap<u32, i64,
GlobalAlloc>` — an `InstantiatedType` — and *its* layout depends on
`Entry<u32, i64>` and `GlobalAlloc`. Any link in that chain not getting
laid out means the whole chain returns 0.

### 3. `try_infer_function_call` only handles bare-Identifier callees

`compiler/src/compiler/types/monomorphizer.cryo:1257-1262`:

```cryo
if (callee_node.kind != NodeKind::Identifier) { return; }
if (call.generic_args.length > 0) { return; }
```

Both bail-outs are wrong. `mem::offset(this.ptr, count)` is a
`ScopeResolution` callee. `Layout::array<T>(count)` is `ScopeResolution`
*plus* explicit generic_args. Both fall through the inference path
without setting `call.resolved_callee`, so codegen mangles the call
against the unspecialized template's `1T` placeholder, and the linker
fails to resolve.

**Fix:** widen the function to accept ScopeResolution callees and to
infer + queue specialization when generic_args is non-empty.  The
existing unification logic (the `inference_bindings` loop in
`try_infer_function_call`) is reusable; what it needs is a path that
extracts the function-template's `TemplateEntry` from a
ScopeResolution like `Mod::fn` or `Type::method`, plus a path that
seeds `inference_bindings` from the explicit `generic_args` rather
than from arg-type unification.  Maybe ~80 lines.

This is the gap memory entry `project_generic_method_mono.md` calls
"scope-res callees". It's the thing that forces stdlib internals like
`Slice::subslice`, `NonNull::offset`, `Array::reserve_to`, every
allocator-using method, to either inline the generic body or route
through a non-generic `_raw` sibling.  Fix it once, the workarounds
across the new stdlib become deletable.

### 4. `try_infer_method_call` doesn't handle primitive receivers

Same file, the method-call equivalent of (3). When a generic method
like `string::hash<H>` is invoked on a primitive receiver
(`key.hash(&hasher)` where `key: &string`), the inference doesn't
fire. Result: the call mangles against the unspecialized template's
`1H`, and the linker fails.

This is the reason yesterday's stdlib needed a parallel
`hash_default(&this, mut &DefaultHasher)` non-generic method in the
`Hash` trait, with overrides for every primitive impl. Fix this,
those overrides become unnecessary, and `HashMap::hash_key<K>` can
keep calling `key.hash(&hasher)` directly.

### 5. The `<null operand!>` regression on zero-field struct receivers

The thing that took down yesterday's PIN refresh. Stdin/Stdout/Stderr in
`stdio.cryo` are zero-field structs; their methods take `mut &this`.
With the legacy stdlib + old PIN, codegen passed *something* (likely a
dummy pointer) for the receiver. With the refreshed PIN, codegen passes
literal null, and LLVM verification rejects the call.

Reproducer minimum: a zero-field struct with at least one method whose
body calls `this.other_method()`.  Triggered when stdio.cryo's
`Stdin::read_byte` body does `this.read(...)`.

**Cause** is unclear from yesterday — the symptom showed up only after
the PIN refresh, and bisecting the three compiler-source edits I'd made
(`Scope::insert` 3-arg fix, loop-no-break detector, strip→diagnostic)
didn't pin it on any single one.  This needs a focused look at where
the receiver `LValue` for trait-method dispatch gets resolved, and what
"zero-sized receiver" path used to do.  My best guess is the receiver
allocation is being elided when sizeof(self) == 0, and a downstream
codegen path expects to load a pointer from where the receiver alloca
should be.

### 6. Mangler emits `$V` (variadic) for non-variadic methods in some path

Same regression as (5) — yesterday's failed stdio compile showed
`Stdin-9read_byte$F$m$V$RN...` in the IR.  `read_byte(mut &this) ->
Result<...>` has zero non-self params and is not variadic.  The `$V`
got into the mangle for some reason.  Possibly an interaction with the
`set_variadic(true)` call at `parser.cryo:1490` (which fires when the
last param has no type annotation) — does `&this` get parsed as a
typeless parameter and trigger that branch in some method shape?
Audit the parser path that constructs FunctionDeclNodes for trait
methods.

## Process recommendations for take 2

1. **Pin the legacy state with backups before anything else.**
   `cp bin/cryo bin/cryo.bak` *before* any session that might refresh
   PIN. The committed `bin/cryo` is older than the working PIN; once
   you overwrite `bin/cryo`, the only remaining route to the previous
   PIN is the C++ bootstrap, and that path may have its own issues
   with current source.

2. **Land the PIN bug fixes first, in their own focused sessions.**
   Each of (1)–(6) above is a real compiler bug that can be reproduced
   *against the legacy stdlib*. Fix one, refresh PIN (with backup),
   verify legacy + bootstrap chain still green, commit. Then the next
   one. Do **not** combine them with the stdlib swap. By the time the
   swap is attempted, PIN should already handle ScopeResolution generic
   callees, primitive-receiver generic methods, zero-field receivers,
   and `[ X ]: Struct[]` literals correctly.

3. **Commit at every working sub-goal.** Yesterday had ~50 files
   modified and zero commits. When something broke, there was no
   bisect target. Even WIP commits with `[temp]` prefix make revert
   surgical.

4. **The new-stdlib *prelude* is the cheapest place to start the
   actual swap.** Before any other change: add `public module
   collections::array;` to the new stdlib's prelude. Verify the
   trivial `i32[].push(42)` case from the previous handoff compiles
   in raw `cryo build` mode (without any of the heavier API
   adaptations). That alone clears the original blocker.

5. **Decide ahead of time about API stability.** Yesterday's session
   added `Hash::hash_default`, `Layout::array_raw`, `Layout::of_raw`,
   `mem::offset_bytes` as "temporary" parallel APIs. They're each
   a legitimate scaffolding decision in isolation, but in aggregate
   they shape the public stdlib API. Either commit to "these stay
   forever" or "these all come out once PIN is fixed" before adding
   them — and make the choice explicit in commit messages. The user
   prefers the latter.

6. **`make selfhost-check` is the right gate.** Anything that goes
   through PIN refresh should be validated by `make selfhost-check`
   (full chain through stage-5 byte-identity check), not just `make
   cryo-fast`. The full check is ~7 min but it's what catches the
   stdio-style "stage-2 builds, stage-3 has runtime regressions"
   class of bugs. Yesterday I never ran `selfhost-check` — every
   "verification" was `cryo-fast` or shorter. That's how the stdio
   regression survived to PIN install.

## Suggested order for next session

If you have a full day:

1. Reproduce trivial `T[].push()` failure on a fresh checkout, just to
   confirm the prelude one-liner is in fact the original blocker. Time
   budget: 15 min.
2. Add `public module collections::array;` to the new stdlib's
   prelude. Verify trivial program now compiles end-to-end via the
   experimental path (`cd experimental/stdlib-next && cryo build` then
   `cryo build trivial.cryo`). Commit. ~30 min.
3. Pick PIN bug (1) — `visit_dynamic_array_literal` elem_size — and
   fix it in compiler/src/. Add a regression test (a single-element
   struct array literal, in the existing test layout). Refresh PIN
   via the bootstrap chain (`make cryo && make pin-cryo`), confirm
   `make selfhost-check`, commit. ~2 hours.
4. Move on to PIN bug (3) — `try_infer_function_call` accepting
   ScopeResolution. Same loop. ~3 hours.
5. By end of day: a refreshed PIN that handles two of the gaps. The
   stdlib swap is now strictly easier and does not need the
   `_raw` workarounds. Land the swap in a follow-up session.

If you have less time, just (1) and (2). Even alone they're a
defensible, committable improvement; the rest can wait.

## Memory entries to consult

The auto-memory under `/home/phock/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/`
captures yesterday's specifics — `project_prelude_array_fix.md`,
`project_compiler_links_green.md`,
`project_pin_refresh_stdio_regression.md`, plus the older
`project_generic_method_mono.md` and `project_codegen_progress.md`.
Read those before starting; they have line numbers and exact symptoms
that are tedious to re-derive.

## What's *not* in scope

- Don't try to commit yesterday's workarounds or `_raw` helpers.
  Those exist to bridge a gap that PIN should close natively. Once
  PIN handles ScopeResolution generic callees and primitive-receiver
  method-generics, the workarounds are net-negative complexity.
- Don't pursue the `make cryo-fast` shortcut for PIN refreshes that
  introduce codegen behavior changes. Use the bootstrap chain via
  `make cryo` so stage-3 self-builds and `selfhost-check` validates.
- 0.1.0 system-install (`bin/cryo` on `$PATH`, `install.sh` etc.) is
  parked until the bridge is solid. There's no point shipping a 0.1.0
  whose stdlib has half-explained workarounds visible to first users.

## One clean takeaway

Yesterday revealed that the swap *isn't* primarily a stdlib-API
problem. It's a PIN-codegen-coverage problem masquerading as one.
Every workaround we wrote in the stdlib was paying interest on a
codegen bug we never serviced.  Pay down the codegen bugs, the
stdlib-side adapter layer disappears.
