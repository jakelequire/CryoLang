# HANDOFF — Fix #3: make dynamic `T[]` a fully-owning (droppable) type

**Audience:** the next agent/developer continuing this work (on a different PC).
**Goal:** finish the "full-owning `T[]`" refactor so a dynamic array `T[]`
(heap-backed `{ptr,len,cap}` fat pointer) is dropped at end of life — fixing
the long-standing leak where `mut a: T[] = [...]` (and every `T[]` field/local)
is never freed. **Must be done before the v1.0.0 tag** (owner's decision).

This document is self-contained. Read §0 first (how to get the WIP), then §1.

---

## 0. ⚠️ HOW TO GET THE WORK (read first — it is UNCOMMITTED)

All of this session's work lives as **uncommitted changes in the working tree**
(`git status` shows ~42 modified files) plus an identical snapshot patch at
**`build-logs/fix3_wip_refactor_progress.patch`** (2773 lines).

**`build-logs/` is in `.gitignore`** and nothing is committed, so a plain
`git clone`/`git pull` on the new PC will **NOT** carry any of it. To transfer:

- **Best:** on the OLD PC, commit the working tree to a branch and push it
  (`git checkout -b fix3-wip && git add -A && git commit && git push -u origin fix3-wip`),
  then check that branch out on the new PC. (The owner deferred committing; do
  this when ready.)
- **Or:** copy `build-logs/fix3_wip_refactor_progress.patch` to the new PC out of
  band and `git apply` it onto `c1ef407e`.

Baseline commit the patch applies onto: **HEAD `c1ef407e`** ("fix: update pinned
compiler"). The committed `#1+#2` tree at `c1ef407e` is green and tag-ready on
its own — none of this WIP has been committed to it.

After getting the tree: `make cryo` (builds the #3 compiler via the pinned
compiler; must succeed).

---

## 1. TL;DR — what state we're in

A dynamic `T[]` is now a droppable, fully-owning type (the `needs_drop` flip +
sentinel are in place — see §3). The hard part — getting the compiler's own
source to survive that flip — is mostly done:

- ✅ **Move-check refactor COMPLETE.** The compiler self-compiles cleanly
  (selfhost stage 4 / "inner loop"): the original **235 → 0** move-errors are
  all resolved (borrows / clones).
- ✅ **Two compiler-assisted `E0453` checks added to `move_check.cryo`** that
  surface the *runtime* double-frees the move-checker historically missed, as
  **compile-time errors** (this was the owner's chosen strategy — turn an
  open-ended valgrind hunt into a bounded, deterministic worklist):
  - `check_array_move_out` — flags `const x: T = arr[i]` element value-copies of
    droppable elements. **All 54 surfaced sites are FIXED.**
  - `check_array_field_move_out` — flags `x = node.fields` whole-owning-array
    value-copies through a pointer. **Surfaced 179 real sites; 9 fixed, 170
    REMAIN — this is the work left.**
- ✅ Several runtime double-frees fixed by hand (token flow, `ModuleInfo`
  cluster, base-ctor over-drop, `declare_generics`). **selfhost stages 1–4 are
  green**; it lexes/parses/loads-modules cleanly and reaches NameResolution.
- ⏳ **selfhost stage 5/6 still blocked** until the remaining **170 whole-array
  sites** are fixed (the inner loop can't self-compile while the new check fires
  on open sites — that's expected, the check is doing its job).

**Your job: clear the 170 remaining `check_array_field_move_out` sites** (same
mechanical borrow pattern as everything already cleared), then run the
validation gates in §7, then valgrind (§6) and add a regression test.

---

## 2. Why this is the shape it is (essential background)

- A dynamic `T[]` is a 24-byte fat pointer `{ptr,len,cap}` (`types/compound.cryo`
  `ArrayType`, `size < 0` ⇒ dynamic). It is **byte-identical** to the stdlib's
  owning `Array<T, GlobalAlloc>` (`GlobalAlloc` is zero-sized), so **no new drop
  codegen was needed** — a dynamic-array `.drop()` already routes to
  `Array<T>::drop` (`codegen/visit/call_emitter.cryo`). The only change was
  flipping `needs_drop(heap-dynamic T[])` to `true`.
- That flip changes **move-checking**: every place the compiler passed a `T[]`
  (or a struct/array that transitively owns one) **by value and reused it**
  became a use-after-move. The compiler pervasively used `T[]` as a **copyable
  view**, which only worked because `T[]` was never drop-tracked. This is the
  "global blast radius" — it spans every pass.
- The move-checker catches the cases where a **local** is reused after a
  by-value pass. It does **NOT** catch two classes that only double-free at
  runtime (because it allows move-out through pointers): (a) reading an element
  out of an array by value, and (b) reading a whole owning array out of a
  struct field **through a pointer**. The two new `E0453` checks make BOTH
  classes hard compile errors so they show up in the inner loop instead of as
  valgrind crashes.

---

## 3. Mechanism already in place (DO NOT regress)

- **Heap-dynamic vs fixed-pending sentinel.** `ArrayType.size`: `-1` =
  heap-dynamic `T[]` (owns a buffer, droppable); `-2` = fixed-pending
  `T[CONST]`/computed (no heap buffer). Both still report `is_dynamic()`
  (`size < 0`) so codegen/layout are unchanged. Exposed as **static methods**
  `ArrayType::heap_dynamic_size()`/`fixed_pending_size()` and
  `ArrayAnnotation::…` (NOT module consts — see §5 gotcha). `is_heap_dynamic()`
  ⇒ `size == -1`. `OwnershipQuery::needs_drop_at_depth` Array branch:
  heap-dynamic ⇒ true; other dynamic ⇒ false; else recurse element.
- **Monomorphizer clone-on-snapshot.** `MonomorphRequest::clone()` deep-copies
  `type_args`; the worklist loop snapshots `this.pending[cursor].clone()` because
  `pending` grows during processing (a borrow would dangle). Keep it.
- **The two `E0453` checks** in `passes/move_check.cryo`
  (`check_array_move_out`, `check_array_field_move_out`, wired into
  `walk_expr_move` when `real`). They are **precise**: `read_call` autoref-gates
  (`!arg.autoref` → `walk_expr_move(real)`), so borrow arguments are NOT flagged
  — verified no false positives. Leave these in; they are the worklist tool.

---

## 4. THE REMAINING WORK — 170 whole-array sites

Run the **iterate loop** (§ below). The inner loop will report 170
`error[E0453]: cannot move an owning array out of a value that owns a
destructor`. Each is a real double-free: a `T[]` (owning array) read **by value**
out of a struct field (through a pointer) or copied into a local, where the
source still owns the buffer. **Fix = make it a borrow** (and make the receiving
function/local borrow too).

### Fix patterns (all already proven on the cleared sites)

1. **Function param that only reads an array → borrow.** `f(xs: T[])` →
   `f(xs: &T[])`. Inside, `xs.length` auto-derefs but `xs[i]` does **NOT** —
   rewrite to `(*xs)[i]`. **Call sites need no change** (autoref passes a value
   to a `&` param). This is the dominant fix; one signature change clears every
   `f(node.array)` call site at once. (Example done: the whole
   `ownership.cryo` `all_copy`/`all_fields_copy`/`all_thread_safe`/
   `all_fields_thread_safe`/`all_variants_thread_safe` cluster — 179→170.)
2. **Local copy of an array field → borrow.** `const xs: T[] = node.fields;` →
   `const xs: &T[] = &node.fields;` then `(*xs)[i]` for indexing (field reads on
   the result auto-deref). Use `&T[]` (reference), not `T[]*` (pointer), when the
   value is later passed to a `&T[]` param (so types match — a raw `T*` does NOT
   auto-coerce to `&T`).
3. **Borrowing an owning struct/array element** (`const v: T = arr[i]` for the
   *element* class) → `const v: &T = &arr[i]` (reference) or `const v: T* = &arr[i]`
   (pointer) — both auto-deref for field reads; use `&T` when `v` is passed to a
   `&T` param.
4. **Element pushed into ANOTHER owning array can't borrow → clone.** When you
   `dst.push(src[i])` and the element is droppable, borrowing won't work (you
   can't store a borrow in an owning array). Add/Use a `clone()` and
   `dst.push(src[i].clone())`. Clone methods already added this session:
   `TraitRef::clone` / `TraitBound::clone` (`AST/_module.cryo`),
   `ParamPlan::clone` (`codegen/abi.cryo`), `Suggestion::clone`
   (`diag/suggestion.cryo`), `Diagnostic::clone` (recursive; `diag/diagnostic.cryo`),
   plus `ProjectConfig::clone` (`project_config.cryo`). Add more as needed
   (deep-copy the owning fields; Copy element arrays are element-wise pushes).
5. **A function that returns `node.array` (or `found[0].param_types`) by value
   aliases it** → return an independent copy (loop-push, since elements are
   usually Copy like `TypeRef`), or restructure to return a borrow.
6. **Pointer-list refactor.** When a function builds a working list of structs
   purely to read them (e.g. overload candidates), collect `T*[]` (pointers into
   the source arrays) instead of copying `T[]` — no clone, no alias. (Example
   done: the overload-hint function in `sema.cryo` was rewritten to
   `MethodInfo*[]`.)

> Cascade note: making a function borrow `&T[]` means **its callees may also
> need to borrow** if it passes the array on. Chase the cascade with the rebuild
> loop; the count only goes down.

### Remaining site map (by file — will shift as you go)

```
 29  AST/node_locator.cryo
 25  passes/sema.cryo
 23  passes/type_resolution.cryo
 21  types/monomorphizer.cryo
 11  passes/specialization.cryo
 11  passes/ast_validation.cryo
 10  AST/substituter.cryo
  7  AST/cloner.cryo
  6  passes/drop_insertion.cryo
  4  types/ownership.cryo
  4  passes/move_check.cryo
  4  codegen/ops/declaration_emitter.cryo
  3  passes/default_expansion.cryo
  2  parser/expr_parser.cryo
  2  codegen/visit/call_emitter.cryo
  1 each: types/arena.cryo, module_graph.cryo, instance.cryo,
          codegen/visit/ir_generator.cryo, codegen/state/diag_sink.cryo,
          AST/specializer.cryo
```

Recommended order: start with the **AST walkers** (`node_locator` 29,
`ast_validation` 11, `cloner` 7, `substituter` 10) — they're the most uniform
(`f(node.children)` style, one signature fix clears many). Estimated ~60–80
function-signature borrows clear all 170.

### ONE KNOWN FALSE-POSITIVE SHAPE (handle, don't "fix" blindly)

`Pair<LValue,TypeRef>` (and any generic container of all-`Copy` members that has
an inherent structural `Drop`) reads as droppable (`has_inherent_drop` short-
circuits `needs_drop`) but its drop is a **no-op**, so copying it is safe. It
cannot be cheaply distinguished from `String`/`Box` (also all-`Copy`-fields but
free their `ptr`) without drop-body analysis. The element check hit exactly one
such site (`codegen/ops/expr_ops.cryo` arg marshalling); it was handled by
**reconstructing** the value from its Copy members
(`Pair::new(args[i].first, args[i].second)`). If the whole-array check throws a
similar false positive (a droppable-but-no-op array element/field), reconstruct
or special-case it rather than forcing a borrow. Spot-check that a flagged site
is a REAL alias (the source genuinely frees a heap buffer) before assuming so —
but in practice every site cleared so far has been real.

---

## 5. GOTCHAS (will cost you hours if unknown)

- **A module-level `const` referenced FROM A METHOD silently resolves to 0.**
  Real compiler bug. Use **static methods** for named constants used in methods
  (the sentinels do this). Don't "tidy" them into module consts.
- **`is_dynamic()` must keep meaning `size < 0`** (not `== -1`); 31 call sites
  depend on it. The `-2` sentinel exists so fixed-pending arrays stay
  `is_dynamic()` (codegen untouched) but are excluded from drop.
- **`(*arr)[i]` to index a `&T[]`** — `arr[i]` won't compile (`E0200 Cannot
  index into type Reference`). `arr.length` is fine (auto-derefs). Struct field
  reads through a `&T`/`T*` auto-deref.
- **`&T` (reference) vs `T*` (pointer):** both auto-deref for field/index
  access, but a `T*` does **not** auto-coerce to a `&T` function parameter. When
  the borrowed value is passed onward to a `&T` param, declare it `&T`.
- **Pinned-compiler `E0633` ("left N of M basic block(s) unterminated; body
  discarded")** on a newly-added/edited function: a `bin/cryo` codegen
  fragility, NOT your logic. Workaround: pull array-index/deref reads into a
  `const` local first (don't inline `func.arr[i]` inside a call/condition).
  (Hit this on `drop_insertion.walk_function`.)
- **Leaks pass the gates; double-frees crash.** A missed drop is memory-safe, so
  `selfhost-check`/`make test` stay green even with a leak. The new `E0453`
  checks now catch the *aliasing* cases at compile time, but you **must still
  valgrind** the final compiler (§6) to confirm no double-free slipped a class
  the checks don't model.
- **Never run `make selfhost-check` and `make test` concurrently** — they share
  `build/` and race (bogus `ld: file truncated`). Run each ALONE.
- **`read_call` autoref gate** (`passes/move_check.cryo`, ~line 932:
  `if (!arg.autoref && type_needs_drop(...))`) is what keeps the new checks from
  flagging borrow arguments. If you ever see the whole-array check firing on an
  arg that lands on a `&T[]` param, the arg's `autoref` flag wasn't set — that's
  a sema bug, not a real site.

---

## 6. Validation already done (preserve) + valgrind recipe

With the `needs_drop` flip, valgrind was **0-leak / 0-double-free** for: `i32[]`
and `String[]` array-literal locals; owning `String` elements; `T[]` struct
fields; move-into-callee; move-out-return. The drop semantics are correct
end-to-end; the refactor is purely about getting the compiler's OWN source to
pass move-checking + not alias.

Reproduce / re-validate (throwaway project):
```
mkdir -p /tmp/p/src && cd /tmp/p
printf '[project]\nproject_name="p"\noutput_dir="build"\ntarget_type="executable"\nsource_dir="src"\nentry_point="src/main.cryo"\n[compiler]\n[dependencies]\n' > cryoconfig
# put test in src/main.cryo, then:
CRYO_STDLIB=<repo>/stdlib <repo>/compiler/build/bin/cryo build
valgrind --leak-check=full ./build/bin/p
```

To debug a selfhost stage-5 crash directly (the `s3` compiler — the one that
actually EMITS the new drop glue — compiling stdlib):
```
cd <repo>/stdlib
valgrind --leak-check=no <repo>/compiler/build/self/s3/bin/cryo build --build-dir=.bin/self/s3val
```

---

## 7. The iterate loop (how to make progress)

```
cd <repo>
make cryo                                # build #3 compiler VIA PINNED (must stay 0 errors)
# inner loop (== selfhost stage 4):
cd compiler && CRYO_STDLIB=$(pwd)/../stdlib ./build/bin/cryo build > ../build-logs/iter.log 2>&1; cd ..
grep -c 'error\[' build-logs/iter.log                          # remaining count
grep -A1 'cannot move an owning array out of' build-logs/iter.log \
  | grep -oE -- '--> src/[^:]+\.cryo' | sort | uniq -c | sort -rn   # by file
# fix a file's signatures per §4, then `make cryo` and re-run the inner loop.
```
- `make cryo` builds via the **pinned** compiler (no `E0453` array checks in
  *its* codegen), so it always succeeds unless you wrote a real syntax/type
  error (e.g. forgot `(*arr)[i]`).
- The **inner `./build/bin/cryo build`** is the #3 compiler compiling the
  compiler source — this is what fires the `E0453` worklist.
- **Re-snapshot whenever `make cryo` is green:**
  `git diff > build-logs/fix3_wip_refactor_progress.patch` (so you never lose
  ground). Keep `build-logs/iter.log` for the by-file view.

---

## 8. Definition of DONE (all required, run ALONE)

1. Inner loop: `./build/bin/cryo build` on the compiler source → **0 errors**.
2. `make selfhost-check` → fixed point OK (stage-3 == stage-4 byte-identical).
   The IR md5 WILL differ from the committed baseline (`a7b3712d…`) — that's
   fine; the fixed point holding is what matters. (Pre-#3 committed fixed-point
   md5 for reference: `a7b3712d913aea3457692363d2e90fc7`.)
3. `make test` → unit OK + compile-fail all pass.
4. **Valgrind** the final compiler on the §6 probes → 0-leak / 0-double-free.
   Add ≥1 regression test under `tests/tests/lang/` asserting an array-literal
   local's drop runs (template: `tests/tests/lang/discarded_temp_drops.cryo`),
   ideally also a `String[]`/owning-element case.
5. Update the memory file
   `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/project_fix3_progress_2026_06_03.md`
   to "LANDED" and refresh the `MEMORY.md` index line.
6. Then `make pin-cryo` + commit (owner usually does the pin/commit).

---

## 9. Inventory of what changed this session (so you can navigate the diff)

- **Sentinel + flip:** `types/compound.cryo`, `AST/_module.cryo`,
  `types/ownership.cryo`, `parser/expr_parser.cryo`, `AST/substituter.cryo`.
- **The two E0453 checks:** `passes/move_check.cryo`
  (`check_array_move_out`, `check_array_field_move_out`,
  `emit_array_move_out_error`, the wiring in `walk_expr_move`).
- **Arena/registry borrow-and-copy factories:** `types/arena.cryo`
  (`get_function`, `create_instantiation`, `propagate_instantiated_resolution`
  borrow + copy-on-store), `types/generic_registry.cryo`
  (`instantiate`/`make_cache_key`/`is_monomorphized`/`instantiate_for_module`),
  `types/substitution.cryo` (`from_params` borrow+copy).
- **Mangler family → borrow `params`:** `resolver/mangled_name.cryo`
  (`encode_params`, `mangle_with_path`, `mangle_function_like`, `for_*`,
  `encode_path_with_leaf_generics`, `specialized_identifier`).
- **Drop-insertion base-ctor fix:** `passes/drop_insertion.cryo`
  (`walk_function` now takes `owner_method: MethodNode*` and walks
  `base_ctor_args` in move context — base-init args were double-dropping params;
  note `base_ctor_args` lives on **MethodNode**, not FunctionDeclNode).
- **Token-flow / module-loading runtime fixes:** `passes/pass_registry.cryo`
  (lexer `final_tokens` independent copy), `parser/parser.cryo`
  (`from_context` independent token copy), `compilation_context.cryo` +
  `instance.cryo` + `module_graph.cryo` (`ModuleInfo` cluster → borrow;
  `get_module` returns `ModuleInfo*`).
- **Diagnostic builders rewritten** to clone-then-mutate (`diag/diagnostic.cryo`,
  `diag/suggestion.cryo`); `diag/renderer.cryo`/`sink.cryo`/`lsp.cryo` render
  fns borrow.
- **Clone methods added** (see §4.4 list).
- **ownership.cryo `all_*` cluster** already borrowed (the whole-array-class
  demo). Everything else flagged by `check_array_field_move_out` is still
  by-value — that's your 170.

---

## 10. Fallback

The committed tree at `c1ef407e` (`#1+#2`) is green and tag-ready on its own. If
#3 proves too risky to finish before the tag, the leak it fixes is memory-safe
with a clean workaround (use `Array<T>` instead of bare `T[]` for owned growable
storage). Parking #3 post-tag is a legitimate option — but the owner asked to
complete it before 1.0, and the remaining work is now a bounded, deterministic
worklist (the 170 sites), so default to finishing it.
