# HANDOFF — Drop insertion finish-up (Phase 2)

**Date:** 2026-05-06
**Branch:** `new-stdlib`
**Baseline:** the previous milestone (warn-only `MoveCheck`) plus this
session's analyzer-only `DropInsertion` pass.  Working tree only — nothing
committed yet.

---

## Context — read first

Before doing anything else, read:

- `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/MEMORY.md`
  (auto-loaded; index of all prior context)
- `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/project_drop_insertion_phase_1_5.md`
  (this session's milestone — what landed, what's gated off, why)
- `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/feedback_bootstrap_pop_returns_garbage.md`
- `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/feedback_match_over_if.md`
- The original Phase 1 handoff text (in the conversation that produced
  the prior milestone) — locked-in design decisions: opt-out `Copy`,
  reject partial moves, reverse declaration order for drop, `Copy` is a
  real user-facing trait.  The previous session's design choices for
  Phase 1.5 are on file as user answers in this session's transcript:
  - Conditional moves → reject with `E0456_CONDITIONAL_MOVE` (currently
    warn-level until synthesis is on)
  - Don't pre-fix the 107 use-after-move warnings on `compiler/src/`
    before tightening MoveCheck — keep as warnings for now

---

## What the previous session set out to do

Land the **second half** of Phase 1 ownership work: the AST-mutating
`drop_insertion` pass plus the stdlib migration that strips the manual
`.drop()` calls auto-drop replaces.  Originally framed as a single
bundled PR.

## What actually shipped (working tree, uncommitted)

The analyzer half of `drop_insertion`, with **AST synthesis gated off**:

```
compiler/src/compiler/passes/drop_insertion.cryo           (new, ~770 lines)
compiler/src/compiler/passes/pass_id.cryo                  (modified)
compiler/src/compiler/passes/pass_registry.cryo            (modified)
compiler/src/compiler/passes/_module.cryo                  (modified)
compiler/src/compiler/instance.cryo                        (modified — Phase 6b list)
compiler/src/compiler/diag/_module.cryo                    (E0456 added, warn-level)
```

The pass runs in the pipeline between `MoveCheck` and `TypeLowering`,
performs flow-sensitive ownership analysis with proper branch-join, and
emits `E0456_CONDITIONAL_MOVE` *warnings* (not errors) on
possibly-moved bindings.  It does **not** mutate the AST — `const
SYNTHESIZE_DROPS: boolean = false;` at the top of the new file
short-circuits `maybe_append_drop`.  Flipping that to `true` re-enables
synthesis.

## Verify the baseline before touching anything

```bash
make -C /home/phock/Programming/apps/CryoLang cryo               # builds clean
cd /home/phock/Programming/apps/CryoLang/tests
/home/phock/Programming/apps/CryoLang/compiler/build/bin/cryo test
# expect: 232 passed; 0 failed; 1 ignored
make -C /home/phock/Programming/apps/CryoLang selfhost-check     # 6/6 stages
```

If any of those fail before you change code, stop and diagnose.

---

## Why synthesis was gated off

The Phase 1 handoff assumed the strip would be straightforward.  The
prior session discovered three blockers that need language-level work
*before* synthesis can ship without breaking things:

### 1. Stripping is multi-PR work, not multi-session

`rg -c '\.drop\(\)' stdlib/` — 158 call sites across 20 files, with
notable concentrations in `process/command.cryo` (36),
`net/http/request.cryo` (28), `net/http/response.cryo` (24).  Most are
straightforward (pattern bindings, field drops, scope-end cleanup), but
each file needs careful classification + per-file `selfhost-check`
verification.  Realistically 3-5 sessions just for the strip.

### 2. Reference-passing helpers drop without the analyzer noticing

`stdlib/process/command.cryo` has

```cryo
function drop_cstring_array(array: &Array<CString>) -> void { … }
```

…called as `drop_cstring_array(&argv_storage)` on four error paths in
`spawn_impl`.  The function logically drops `argv_storage`'s contents,
but the analyzer sees a by-reference call and leaves the binding live —
auto-drop would then synthesize a second drop on top of the helper's
work.  Either rewrite the helper to take `mut argv: Array<CString>` by
value (which moves it in, so the analyzer sees the consume) or surface
the consume via a future attribute.

### 3. Methods that consume semantically but borrow syntactically

`stdlib/alloc/box.cryo`:

```cryo
into_raw(&this) -> T* {
    return this.ptr.as_ptr();
}
```

`b.into_raw()` is a borrow at the call site, but the caller takes
ownership of the raw pointer and the original `Box` is now stale.  The
analyzer can't detect this without per-method metadata.  `tests/stdlib/box.cryo`
exercises exactly this pattern — auto-drop of the now-stale `Box` would
double-free.

The clean fix is `into_raw(mut this) -> T*` plus a `mem::forget`-style
mechanism so the body can return a derived pointer without `this` being
auto-dropped on function exit.  Cryo doesn't have `mem::forget` /
`ManuallyDrop` today.

---

## What's left (in suggested order)

### A. Strip stdlib `.drop()` call sites

Same plan as the previous handoff, just with the synthesis gate
*staying off* until it's done.  Recommend doing it in size-ordered
batches:

1. Files with 1-2 drops (`collections/hashset.cryo`, `io/buf.cryo`,
   `fmt/display.cryo`, `test/assert.cryo`, `test/error.cryo`,
   `collections/string.cryo`, `fs/path.cryo`).  Most of these turn
   out to be field drops (`this.buffer.drop()`) or pattern-binding
   drops (`Result::Ok(mut s) => { s.drop(); }`) that are correctly
   left alone — the analyzer doesn't synthesize for fields or pattern
   bindings, so they don't conflict with auto-drop.  Verify by reading
   each site rather than blindly deleting.
2. Files with 3-7 drops (`hashmap.cryo`, `io/stdio.cryo`,
   `process/child.cryo`, `net/http/{server,client,router,headers}.cryo`,
   `fs/file.cryo`, `env/_module.cryo`, `test/runner.cryo`).
3. Files with 24-36 drops (`process/command.cryo`,
   `net/http/{request,response}.cryo`).  These have the helper-via-
   reference patterns — handle the helpers (B below) first.

After each batch: `cryo test` then `make selfhost-check`.  Selfhost
*will* produce a new fixed-point md5 once stdlib changes — what you
verify is that two consecutive rounds produce the same md5, not that
it matches the prior baseline.

### B. Fix the reference-passing drop helpers

`stdlib/process/command.cryo`'s `drop_cstring_array` is the clearest
case.  Convert to value receiver:

```cryo
function drop_cstring_array(mut array: Array<CString>) -> void {
    mut i: u64 = 0;
    while (i < array.length()) {
        match (array.get(i)) {
            Option::Some(mut c) => { c.drop(); }
            Option::None => { break; }
        }
        i++;
    }
    array.drop();   // explicit; param scope doesn't auto-drop
}
```

…and update the four call sites to pass `argv_storage` by value
(without `&`).  The previous session attempted this and got a clean
`make cryo`, but reverted it because we weren't enabling synthesis in
that session.

Similar audit needed across stdlib for any `function … (x: &T)` that
internally drops contents — search for `\.drop\(\)` inside functions
whose first parameter is `&Something`.

### C. Land `![consumes_self]` (or equivalent attribute)

Tag methods like `Box::into_raw` that consume the receiver despite
declaring `&this`.  Touch points:

- Parser: accept the attribute on method declarations.
- AST: store a flag on `MethodNode` (or the underlying `FunctionDeclNode`).
- DropInsertion's `read_call`: when the resolved method (set by sema in
  `c.resolved_method`) has the consume-self flag, treat the receiver
  identifier as moved instead of borrowed.

Once the attribute is plumbed, the rewriting of `into_raw` etc. to
`mut this` becomes optional — the attribute carries the same
information without needing a `mem::forget`-equivalent.

### D. Add `mem::forget` (only if you reject the attribute approach)

If the team prefers value-receivers (`mut this`) over an attribute,
some way to suppress the implicit drop of `self` inside the function
body is needed.  Rust's `mem::forget` is the obvious shape.  More
invasive than (C), but more honest about ownership at the type level.

### E. Flip the synthesis gate

```diff
-const SYNTHESIZE_DROPS: boolean = false;
+const SYNTHESIZE_DROPS: boolean = true;
```

Then promote `E0456_CONDITIONAL_MOVE` from warning to error (one-line
change at `emit_conditional_move_by_id`).  Verify selfhost — there is
*one* known false-positive site:

```cryo
// compiler/src/compiler/passes/type_resolution.cryo:662
mut bounds: TypeRef[] = [];
for (…) { … bounds.push(...); }
mut param_ref: TypeRef = TypeRef::invalid();
if (bounds.length > 0) {
    param_ref = arena.create_bounded_param(gp.name, i as u64, bounds);   // moves
} else {
    param_ref = arena.create_generic_param(gp.name, i as u64);            // doesn't move
}
refs.push(param_ref);
```

`bounds` is conditionally moved by the call to `create_bounded_param`,
not moved on the else branch.  This *is* a real conditional move, but
it's harmless: `bounds` goes out of scope at the end of the
loop-iteration body anyway.  Phase 2 should either improve the analysis
(scope-end of binding cancels the conflict) or rewrite the call site
to lift the move out of the branch.

### F. Add `tests/lang/moves.cryo`

Once synthesis is enabled, write the test file the original handoff
called for: basic move on `let`, function-arg by-value move, return
move, struct-field-init move, tuple element move, Copy types don't
move, use-after-move rejected, conditional move rejected,
end-to-end `Box<T>` auto-drop with sentinel counter.

### G. Refresh `bin/cryo`

After everything passes:

```bash
make -C /home/phock/Programming/apps/CryoLang selfhost-check
make -C /home/phock/Programming/apps/CryoLang pin-cryo
```

Then commit `bin/cryo` and `bin/cryo.pin.txt`.

---

## Architectural notes from this session

### Bootstrap quirks to avoid

- **`i64[].pop()` returns garbage** in the bootstrap C++ codegen path
  while still correctly decrementing length.  Read by index, then pop
  for side-effect.  See `feedback_bootstrap_pop_returns_garbage.md`.
  This is what bit drop_insertion's scope-stack pop_scope until I
  switched to `arr[arr.length - 1]` then `arr.pop()`.
- **HashMap with `remove()` corrupts cross-key lookups** under
  drop_insertion's access pattern.  Switched to parallel arrays for
  the `name → type` map (`types_keys: u32[]; types_refs: TypeRef[]`).
  MoveCheck doesn't hit this because it never calls remove() on its
  HashMap.
- **`error[E0456]` displays as `E0112`** — pre-existing bootstrap
  bug where explicit enum values aren't honored.  Don't try to fix
  here; the message text is correct, the rendered code is wrong.
  Same bug already affected E0452 (displayed as E0108).

### Style preferences (user-confirmed this session)

- Prefer `match (k) { ... }` over chained `if (k == X) { ... return; }`
  for enum dispatch.  See `feedback_match_over_if.md`.
- Don't commit one-off verification scripts.  Verify ad-hoc.

### Multi-module pipeline reminder

`pass_registry.cryo`'s four `build_*_pipeline()` builders are *not*
the source of truth for the Phase 6b multi-module pipeline.  That
pipeline is hand-rolled at `instance.cryo:817-822` (or wherever it
ends up after edits).  Adding a pass to the registry without
updating `instance.cryo` means the pass registers but doesn't run
in real builds.

---

## How to verify progress

After every meaningful change:

```bash
# 1. Compiler builds itself via the pin
make -C /home/phock/Programming/apps/CryoLang cryo

# 2. Full test suite passes
cd /home/phock/Programming/apps/CryoLang/tests
/home/phock/Programming/apps/CryoLang/compiler/build/bin/cryo test
# expect: 232+ passed; 0 failed; 1 ignored

# 3. Self-host fixed point holds
make -C /home/phock/Programming/apps/CryoLang selfhost-check

# 4. After Phase 2 fully lands, refresh the pin
make -C /home/phock/Programming/apps/CryoLang pin-cryo
```

Don't skip any step.  Selfhost has caught silent miscompilations
twice during this work.

---

## Open design questions

These weren't decided this session and the next agent should surface
them before committing to a path:

- **Attribute (C) vs `mem::forget` (D)** for consuming methods.  The
  attribute is less invasive and maps onto sema's existing
  `resolved_method` plumbing; `mem::forget` is more architecturally
  honest.  No pre-decision.
- **Strip-then-flip vs incremental enablement** of synthesis.  This
  session's scope-skipping default is "strip-then-flip" (single
  global gate flip after stdlib is clean).  An alternative is a
  `![auto_drop]` opt-in at the function level so synthesis lights
  up incrementally.  The user asked to keep going strip-then-flip
  this session, but as the strip surface area becomes clearer that
  trade-off may want revisiting.

---

## Operating instructions

Same as the prior handoff:

- **Root-cause fixes over workarounds.**  See `feedback_codegen_style.md`.
- **Substantive memory entries** for non-obvious decisions, under
  `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/`,
  with a one-liner index entry in `MEMORY.md`.
- **Terse status updates.**  No multi-paragraph "what I did" summaries.
- **Don't commit unless asked.**  Land changes in the working tree,
  describe what changed, let the user choose when to commit.
- **Don't break selfhost-check.**  Diagnose any divergence — silent
  miscompilation almost always sits behind it.

---

## Quick orientation

```
compiler/src/compiler/
├── passes/
│   ├── move_check.cryo          — warn-only ownership pass (Phase 1)
│   ├── drop_insertion.cryo      — NEW: flow-sensitive analyzer; mutation gated
│   ├── pass_id.cryo             — PassID + Provision + metadata
│   ├── pass_registry.cryo       — pipeline builders + run_pass dispatch
│   └── …
├── types/
│   ├── ownership.cryo           — is_copy + has_inherent_drop + has_drop_impl
│   └── …
├── diag/
│   └── _module.cryo             — ErrorCode enum incl. E0456_CONDITIONAL_MOVE
└── instance.cryo                — multi-module orchestrator (Phase 6b list)

stdlib/
├── alloc/                       — Box, Rc, … (into_raw lives here)
├── collections/                 — Array, HashMap, String, Str, …
├── process/command.cryo         — drop_cstring_array helper lives here
├── net/http/                    — heaviest .drop() call concentrations
└── …

tests/tests/
├── lang/
│   ├── copy_bound.cryo          — Copy bound tests (Phase 1)
│   └── moves.cryo               — TODO: write once synthesis is on
└── stdlib/                      — stdlib API tests (box.cryo etc.)
```

Good luck.  The user is technically deep, prefers collaboration over
one-shot dumps, and will redirect early if you head off the path —
surface design questions when you hit them.
