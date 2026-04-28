# CryoLang Stage-3 Self-Host — Handoff

**As of:** 2026-04-27, end of session. Continuing on a different machine
that won't have the previous Claude memory files, so this handoff is
self-contained.

**Working tree:** `/home/phock/Programming/apps/CryoLang/` (or wherever
you've cloned the repo on the new machine).

**Branch:** `main`. Last committed point: `97fe8b06` (vtable structural
equality fix). Everything from this session is in your working tree —
**not yet committed**.

---

## 1. What we're doing

Self-hosting the cryoc compiler. The chain is:

```
bootstrap (bin/cryo, C++)  ──┐
                              ▶  cryoc/build/cryoc          (= "stage 2")
                              │
stage-2 cryoc            ────┴──┐
                                 ▶  cryoc/build/bin/cryoc   (= "stage 3")
                                 │
stage-3 cryoc           ────────┴──▶  next round
```

Goal: **stage-3 cryoc compiles stdlib (and itself) end-to-end without
crashing.** Once stage 3 succeeds, the fixed-point test is "stage 3
output == stage 4 output", but we're not there yet.

`bin/cryo` is the C++ bootstrap. It has known bugs (silent miscompiles,
segfaults on certain syntactic forms — see "Bootstrap hazard zone"
below). Treat it as a trapdoor we want to drop, not a thing to fix.

---

## 2. User preferences (memory snapshot — IMPORTANT)

These came from saved memory on the old machine; recreate locally if you
want them in `~/.claude/projects/.../memory/`.

### Code style (feedback)

- **No inline string manipulation in codegen** (substring extraction,
  char-replace loops, pointer arithmetic on strings). The C++ Cryo
  Compiler suffered from this and the user explicitly forbids it in
  cryoc. When codegen needs name resolution or string ops, add a method
  to CodegenContext / DeclarationIndex / InternTable, or extend the
  upstream pass to provide it. Never patch around an upstream gap with
  inline code in codegen.

- **No "safe fallback" defaults for invariant violations.** If a value
  that should always be set turns out to be 0/null/empty at codegen
  time, that's an upstream bug — emit a real diagnostic via
  `cg.emit_error_at_span(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
  "...")` and bail. Don't substitute a placeholder. Quote: *"I would
  rather throw an error to the Diag vs a cheap fallback."*

- **No fallback chains in lookups.** When a previous session added a
  bare-name fallback in `emit_base_ctor_call`'s type lookup, it was
  reverted with "I didn't like that pattern."

- **`This` (capital), not `Self`**, for trait-self in this language.

- **No type inference on variables** — `const x: int = 10;` not
  `const x = 10;`.

- **Don't add features, refactor, or introduce abstractions beyond what
  the task requires.** Three similar lines is better than a premature
  abstraction. No half-finished implementations.

### About the user

Senior language/compiler developer. Deep systems-programming and LLVM
expertise. They wrote the C++ Cryo bootstrap and are rewriting it in
cryoc itself. Front/middle-end is largely clean; they're particular
about keeping the backend the same way.

### Codegen architecture

- LLVM via a thin wrapper header (`llvm_types.cryo` etc.).
- All generics are resolved before codegen — codegen sees only fully
  monomorphized types.
- Specializations land in the requesting module.
- Per-module codegen — each cryoc module → one LLVM Module → one .o file.
- Vtables emitted with `external` linkage (per Bug #12 fix below) so
  cross-module `new` sites can reference them at link time.

---

## 3. Where we landed this session

### ✅ Fixed (real fixes, working)

**Bug #12 — Cross-module vtable linkage**
- Symptom: stage-3 segfaulted in `NameResolver::visit(ProgramNode)`
  immediately on module 35 (`std/fs/file.cryo`). The crash was an
  indirect call to `0x5555580de1e0` — a glibc `main_arena+96` pointer.
  The crash node had its first 8 bytes (vptr) overwritten with malloc
  fastbin head data because the vtable global was never written to it.
- Root cause: vtables were emitted with `internal` LLVM linkage. With
  per-module .o linking, internal-linkage symbols aren't visible across
  .o files. The linker dead-stripped 92 of the 97 vtables. Cross-module
  `new ProgramNode` (in `parser.cryo`) couldn't reference the vtable
  defined in `node.cryo`'s .o.
- Fix:
  - `cryoc/src/compiler/codegen/decl_codegen.cryo:codegen_vtable_for_class`:
    `set_linkage(0)` (LLVMExternalLinkage) instead of `8`
    (LLVMInternalLinkage).
  - `cryoc/src/compiler/codegen/ir_generator.cryo:visit_new_expr`: when
    `get_named_global(vtbl_name)` returns invalid (cross-module case),
    `add_global(placeholder_ty, vtbl_name)` — declares an extern
    reference. With opaque pointers, the placeholder type doesn't have
    to match the definition; the linker resolves by symbol name.

**Bug #14 — Enum-variant match-subject layout**
- Symptom: stage-3 segfaulted reading `f.return_type` in `resolve_function`
  with `%rsi = 0x5663e70000000000` — exactly the bit pattern you get
  when reading from the wrong byte offset of a 16-byte enum struct as
  if it were 12 bytes.
- Root cause: `map_enum` in `type_map.cryo` was lowering ADT enums to
  `{ i32, [N x i8] }`. With i8-aligned payload, LLVM made the struct
  4-byte aligned (size 12 for ptr-payload). But `compute_enum_layout`
  in `passes/type_lowering.cryo` correctly returned 16 bytes (i32 tag +
  4 padding + 8 ptr). Mismatch → wrong field offsets at every
  match-arm GEP.
- Fix: `cryoc/src/compiler/codegen/type_map.cryo:map_enum` now picks the
  payload-array element type by alignment: `[N/8 x i64]` for ptr-aligned
  payloads, `[N/4 x i32]`, etc. This forces LLVM's struct alignment to
  match the source layout. Verified in IR:
  `%match.subj = alloca { i32, [1 x i64] }, align 8` (was `{ i32, [8 x i8] }, align 4`).

**`--build-dir=PATH` CLI flag (feature)**
- Lets stage 3 / stage 4 compile-passes write to distinct directories so
  the eventual fixed-point comparison is even possible. Default behavior
  unchanged: no flag → uses `output_dir` from cryoconfig.
- Implementation:
  - `CLI/_module.cryo`: parser handles `--key=value` form, new
    `get_value(key)` helper.
  - `CLI/commands.cryo`: `cmd_project` reads `--build-dir`, sets it on
    `CompilerConfig.output_dir_override`.
  - `compiler/instance.cryo`: `compile_project` overrides
    `ProjectConfig.output_dir` after parsing cryoconfig if override is set.
  - `compiler/codegen/passes.cryo`: mkdir the parent directory (custom
    paths don't pre-exist); fixed hardcoded `"build/obj/"` .ll dump
    path that ignored the config.
- Verified: two runs into `build-iso-A` / `build-iso-B` produced
  byte-identical .o files (`diff -rq` empty). Stage 2 is deterministic.

### ⚠ Workaround (revisit later)

**Bug #13 — `LOG_DEBUG` segfaults in stage 3**
- Symptom: stage 3, after Bug #12 was fixed, segfaulted in
  `Scope::find`'s `LOG_DEBUG(...)` inside vfprintf's strlen — bad
  `%s` arg. Tried gating with
  `LogLevel::Debug.meets_threshold(Logger::instance().config.min_level)`
  but stage-3's codegen of that chain crashes inside LOG_DEBUG before
  reaching `format()`. Same family as Bug #14 and Bug #15: stage-3
  miscompiles a field-access chain.
- Workaround: `cryoc/src/utils/logger.cryo:LOG_DEBUG` is now an
  unconditional `return;`. Default `LoggerConfig` has `min_level: None`
  so DEBUG was already filtered in production — functionally equivalent.
  Loses runtime-toggleable debug verbosity; replace with the proper
  level-gated form once enough other bugs are fixed.

### 🚧 In progress (your starting point)

**Bug #15 — `new EnumType::Variant(args)` doesn't construct the variant**
- Symptom: stage-3 reaches `std::math` (module 50) function 75 (`asinh`),
  then segfaults inside a recursive `resolve(ann)` call. `ann` is an
  ASCII-text fragment ("ck point" = bytes from "block point**er**" in a
  comment in `core/intrinsics.cryo`). Diagnostic confirmed every
  TypeAnnotation read by `resolve()` has the same fastbin-head pointer
  pattern at offsets 0 and 8 — i.e., **freed memory** that was reused
  by another allocation.
- Root cause: cryoc's `new EnumType::Variant(args)` was being parsed
  into a `NewExprNode { type_name: "EnumType" }` with the variant name
  thrown away. `visit_new_expr` then tried to look up a class-style
  constructor named `"EnumType::EnumType"` (which doesn't exist for
  enums), failed silently, and returned an uninitialized 16-byte malloc.
  The mallocd chunk's first 16 bytes still held its previous freelist
  link → looked like a bin-head pointer at both offsets 0 and 8.
- **What's done so far:**
  - `cryoc/src/compiler/AST/expression.cryo`: added `variant_name: SymbolStr`
    field to `NewExprNode`, plus `set_variant_name`, `has_variant`,
    and constructor init.
  - `cryoc/src/compiler/parser/expr_parser.cryo`: parser now stores the
    variant name when consuming `::Variant(args)` (one-liner change).
  - `cryoc/src/compiler/codegen/ir_generator.cryo:visit_new_expr`:
    enum-variant dispatch added — calls `build_enum_variant` and
    returns the variant value via `last_value`.
- **What's missing:** the dispatch currently returns the variant
  *value*, but `new T::V(...)` should return a *pointer* (per `new`
  semantics). I need to:
  1. `malloc(et.computed_size)` → raw pointer
  2. `bitcast` to `T*`
  3. `store variant_val` into it
  4. Return the typed pointer.

  The block I had to write this was reproducibly making the C++
  bootstrap (`bin/cryo`) **segfault silently**. See the next section.

### 🐢 Bootstrap hazard zone (BLOCKER for finishing Bug #15)

While iterating on the `new T::V(...)` codegen branch in
`ir_generator.cryo:visit_new_expr`, the bootstrap C++ compiler started
segfaulting (exit 139, no output, no diagnostic). I bisected by adding
code in tiny increments and rebuilding bootstrap-compiled stage-2 each
time:

| Step | Code added on top of working state | Bootstrap |
|---|---|---|
| 9  | `mut vargs: LValue[] = [];` + push loop + `build_enum_variant` + return value | ✓ ok |
| 10 | + `if (variant == null) { emit_error_at_span(..., "literal"); return; }` (literal, no `format`) | ✓ ok |
|    | + `format("...%s::%s...", a, b)` arg in error | ✗ segfault |
| 11 | swap above for: `const enum_size: u64 = et.computed_size;` (unused) | ✓ ok |
| 12 | + `if (enum_size == 0) { ...return; }` (any body) | ✗ segfault |
| 13 | + `if (enum_size == 0) { chosen_size = 16; }` (no return) | ✗ segfault |
|    | even an unused `mut chosen_size: u64 = enum_size;` alone seems borderline |
| 14 | a long-form doc comment with em-dashes ABOVE the `if` block also seems to upset bootstrap somehow |

The same syntactic forms (multi-arg `format(...)`, nested `if (x == 0) { return; }`) work elsewhere in the codebase. Whatever's going wrong is sensitive to context — possibly nesting depth, surrounding state, or position in the file. **I never figured out the exact trigger.**

The current `visit_new_expr` is left at the "step 9" form: variant
construction works, `last_value` is the variant value, but the heap
allocation step is missing. So `new T::V(args)` currently returns a
*value* instead of a *pointer*. Stage-3 still crashes (just slightly
differently than before). The chain still builds end-to-end.

**Three options to finish this on the new machine:**

1. **Move the heap-alloc into a helper function.** Put it in a separate
   private method like `heap_box_enum_value(t, val) -> LValue` so the
   problematic block is inside a *different* function — bootstrap
   crashes seem to be tied to specific positions inside
   `visit_new_expr`. Calling a helper from there should be safe.

2. **Skip the `enum_size == 0` defensive check entirely.** It's
   defensive against a TypeLowering pass bug that doesn't currently
   happen — `compute_enum_layout` always sets `computed_size` for any
   enum that reaches codegen. The check costs us bootstrap stability.
   This conflicts with the no-fallback policy, but the alternative is
   no fix at all. Document why it's omitted with a comment pointing to
   this handoff.

3. **Bisect down to the actual bootstrap trigger and avoid that exact
   form.** Probably the most informative outcome long-term; might be a
   1-line change in the C++ bootstrap or a 1-line workaround in cryoc.
   Time-box it: if 30 minutes of bisecting doesn't isolate it, fall
   back to option 1.

I'd start with option 1.

---

## 4. Files modified this session

```
cryoc/src/CLI/_module.cryo                   | 39 +
cryoc/src/CLI/commands.cryo                  |  6 +
cryoc/src/compiler/AST/expression.cryo       | 12 +    (variant_name field)
cryoc/src/compiler/codegen/decl_codegen.cryo |  5 ±    (vtable linkage)
cryoc/src/compiler/codegen/ir_generator.cryo | 44 +    (vtable cross-module + Bug #15 partial)
cryoc/src/compiler/codegen/passes.cryo       | 18 +
cryoc/src/compiler/codegen/type_map.cryo     | 90 +    (Bug #14 fix)
cryoc/src/compiler/instance.cryo             | 42 +
cryoc/src/compiler/parser/expr_parser.cryo   | 11 +    (variant_name capture)
cryoc/src/utils/logger.cryo                  |  9 +    (Bug #13 workaround)
```

Plus pre-existing edits in `new_stdlib/` that I didn't author this
session (work in progress from before).

`git diff --stat` will tell you everything.

---

## 5. Resume — first 5 minutes on the new machine

```bash
# 1. Get the build script back (it lives in /tmp on this machine, not in repo).
cat > /tmp/full_build.sh <<'EOF'
#!/bin/bash
# Layout (do not "fix"):
#   bootstrap   bin/cryo                 (C++ implementation)
#   stage 2     cryoc/build/cryoc        (bootstrap-compiled cryoc)
#   stage 3     cryoc/build/bin/cryoc    (stage-2-compiled cryoc)
set +e
ROOT=/home/phock/Programming/apps/CryoLang   # <-- adjust path on new box
BOOT=$ROOT/bin/cryo
STAGE2=$ROOT/cryoc/build/cryoc
STAGE3=$ROOT/cryoc/build/bin/cryoc

run_stage3_stdlib() {
    echo "=== STAGE-3 SELF-HOST: stdlib via stage 3 ==="
    [ -x "$STAGE3" ] || { echo "  stage-3 missing — skipping"; return; }
    cd "$ROOT/stdlib"
    rm -rf .bin && mkdir -p .bin/obj
    "$STAGE3" build > /tmp/stage3_stdlib.log 2>&1
    local rc=$?
    echo "stage3-stdlib exit $rc"
    if [ "$rc" -ne 0 ]; then
        echo "  highest module reached:"
        grep 'Phase 2: Processing module' /tmp/stage3_stdlib.log | tail -1 | sed 's/^/    /'
        echo "  last 5 lines:"
        tail -5 /tmp/stage3_stdlib.log | sed 's/^/    /'
    fi
}
[ "$1" = "stage3" ] && { run_stage3_stdlib; exit; }

echo "=== STEP 1: stdlib via bootstrap ==="
cd "$ROOT/stdlib" && rm -rf .bin && mkdir -p .bin/obj
"$BOOT" build > /tmp/step1.log 2>&1; echo "step1 exit $?"

echo "=== STEP 2: cryoc via bootstrap ==="
cd "$ROOT/cryoc"
"$BOOT" build > /tmp/step2.log 2>&1; echo "step2 exit $?"

echo "=== STEP 3: stdlib via stage-2 ==="
cd "$ROOT/stdlib" && rm -rf .bin && mkdir -p .bin/obj
"$STAGE2" build > /tmp/step3.log 2>&1; echo "step3 exit $?"

echo "=== STEP 4: cryoc → stage-3 ==="
cd "$ROOT/cryoc" && rm -rf build/obj build/bin
"$STAGE2" build > /tmp/step4.log 2>&1; echo "step4 exit $?"

echo "=== TRACES (stage 4) ==="
echo "  BCTOR-T6 successes: $(grep -c BCTOR-T6 /tmp/step4.log)"
echo "  BCTOR-BAIL distribution:"
grep BCTOR-BAIL /tmp/step4.log | sort | uniq -c | sed 's/^/    /'

echo
run_stage3_stdlib
echo "=== DONE ==="
EOF
chmod +x /tmp/full_build.sh

# 2. Verify the chain still builds (≈2 minutes).
/tmp/full_build.sh

# Expected outcome: steps 1-4 exit 0, BCTOR-T6 ~90, 5 known
# BaseASTVisitor::BaseASTVisitor BCTOR-BAILs.  Stage-3 self-host
# crashes (this is Bug #15 — that's what we're fixing).
```

Expected last lines of output:

```
stage3-stdlib exit 139
  highest module reached:
    [Compiler] Phase 2: Processing module 52 (index 52)
  last 5 lines:
    [TypeDecl] stmt[72] kind=43
    [TypeDecl] stmt[73] kind=43
    [TypeDecl] stmt[74] kind=43
    [TypeDecl] stmt[75] kind=43
    [Ty
```

(`[Ty` is mid-print of the next `[TypeDecl] stmt[N]` lost when the
process was killed.)

If the chain **doesn't** end up green, something has changed since I
left it — `git diff --stat` should show exactly the files I touched.
First sanity-check before doing anything else.

---

## 6. Diagnostics still in source

These printfs are noisy but **load-bearing** for the next round of
debugging — don't strip them until stage-3 fully self-hosts. They were
already there before this session; I didn't add them, and they came
from the original handoff:

| File | Prefix |
|---|---|
| `cryoc/src/compiler/parser/parser.cryo:104` | `[PARSE-DBG]` |
| `cryoc/src/compiler/passes/type_resolution.cryo:1404` | `[VT-DBG]` |
| `cryoc/src/compiler/codegen/ir_generator.cryo:300+` | `[BCTOR-*]`, `[CTOR-CHECK]` |
| `cryoc/src/compiler/passes/pass_registry.cryo:736` | `[TypeDecl]` |

I left a comment at the new code that says `see HANDOFF.md Bug #15` —
keep it short like that, the C++ bootstrap is sensitive to long
comments with em-dashes (yes really — see hazard zone above).

---

## 7. Known C++ bootstrap (`bin/cryo`) bugs you may hit

From the previous handoff (still live):

- **`array[i].field` on chained array indexing reads garbage** — the
  C++ codegen miscompiles this. Workaround: store the element to a
  local first (`const x = array[i]; x.field`).
- **HashMap<string, V> uses pointer-compare for keys** — many caches
  don't actually dedup. The CLI parser's `Map<string, Argument>` has a
  hand-rolled strcmp loop in `has_arg`/`has_flag` to work around this.
- **Single-element array-literal codegen is broken** — `[size_val]`
  loads garbage from a `[1 x T]` alloca as an `Array<T>` 3-field struct.
  Workaround in existing code: `mut args = []; args.push(size_val);`.
- **`format("...%s%s...", a, b)` may segfault inside `visit_new_expr`** —
  see Bug #15 hazard zone. Use a literal string for now.

---

## 8. Bug list ahead (best guess after Bug #15)

1. Bug #15 finish — heap-alloc + store for `new T::V(...)`. Then stage-3
   should advance further into the pipeline.
2. The `LOG_DEBUG` chain crash is still latent — replace the no-op with
   the proper level-gated form once stage-3 stops crashing on it.
3. The 5 `BaseASTVisitor::BaseASTVisitor` arity=1 BCTOR-BAILs in step 4
   — implicit base-ctor isn't being registered for lookup. Probably
   a small fix in `decl_codegen.cryo`'s default-ctor emission. Doesn't
   block self-host (the BCTOR-BAILs are warnings; vtables are still
   correct after Bug #12).
4. The arena type-canonicalization issue underlying Bug #11
   (`param_types_equal_structural` is a workaround for cross-module
   TypeRef ID divergence). Real fix is in the type arena's pointer-type
   intern — until landed, the structural compare costs us a string
   compare per vtable slot at codegen time.

---

## 9. Useful commands cheatsheet

```bash
# Stage-3 self-host (only) — when you've already got a stage-3 binary
/tmp/full_build.sh stage3

# Backtrace from the current crash
cd $ROOT/stdlib
gdb -batch -ex 'set pagination off' -ex 'run build' -ex 'bt 8' \
    $ROOT/cryoc/build/bin/cryoc 2>&1 | grep -E '^#|signal' | head

# Inspect stage-3 IR for a specific function (mangling format: $F$s_... )
grep -n 'visit_new_expr\$F' $ROOT/cryoc/build/bin/cryoc.ll | head

# Compare emitted IR between two --build-dir runs (deterministic)
$ROOT/cryoc/build/cryoc build --build-dir=build-A
$ROOT/cryoc/build/cryoc build --build-dir=build-B
diff -rq build-A/obj build-B/obj   # should be empty
```

---

## 10. Don't-do list

- Don't commit anything until you've verified stage-3 actually advances
  past Bug #15. The current diff has working pieces (Bug #12, #13, #14,
  --build-dir) AND an incomplete piece (Bug #15) that you'll either
  finish or revert. A pre-Bug-#15 commit point would be a fine cleanup.

- Don't strip the existing `[BCTOR-*]`, `[PARSE-DBG]`, `[VT-DBG]`,
  `[TypeDecl]` printfs. They're how the next bug surfaces. Strip after
  green.

- Don't delete `new_stdlib/` — those are pre-existing edits from before
  this session that I left untouched. Probably another in-progress
  feature.

- Don't push `force` to main. Don't `git reset --hard` without checking
  `git status` first.

Good luck tomorrow.
