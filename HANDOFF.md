# ABI Lowering — Handoff (round 3)

**Branch:** `new-stdlib`
**Goal:** Close §3.5 param-side (and via that, the last load-bearing
piece of the original Phase 3 plan). The seam, return-side §3.5,
§3.4 Tuple/Optional aggregation, and §3.3 multi-float SSE all shipped
in rounds 1 and 2. Round 3 was meant to ship §3.5 param-side; it hit
a self-bootstrap heap-corruption bug that we partially diagnosed but
didn't isolate. Forward-looking prologue and `declare_method` work
landed (dormant); the classifier flip itself is what's blocked.

Read this top-to-bottom before touching code.

---

## 1. State on disk

### Last commit on branch

```
2d941bdc Enhance ABI handling for multi-float SSE types and add related tests
1998d646 Implement C-side ABI test helpers and enhance ABI classification
01a18ea4 Refactor ABI handling in code generation
```

### Uncommitted in working tree

```
compiler/src/compiler/codegen/abi.cryo                          (~46 lines)
compiler/src/compiler/codegen/ops/declaration_emitter.cryo      (~230 lines)
docs/abi.md                                                     (~13 lines)
```

The user handles commits — do NOT commit on their behalf.

### What the uncommitted changes are

1. **`abi.cryo`:** `classify_param`'s docstring updated to record the
   §3.5 param-side follow-up and the suspected failure mode. The
   classifier body is back to byval-only (>16 → Indirect/ByVal,
   everything else plain `Direct(struct)`) because the flip broke
   selfhost. **Note: the docstring describes what we tried, not the
   current behavior — keep that in mind when reading the file.**

2. **`declaration_emitter.cryo`:** Three substantive changes, all
   forward-looking (no-op until `classify_param` flips to produce
   DirectPair plans):
   - `codegen_function_prologue` walks `mut llvm_idx: u32` alongside
     source param index, advances by `plan.llvm_slots.length`. Direct
     branch (≤ 8 byte coerced) stores the scalar at a source-typed
     alloca (opaque ptr — bytes match). DirectPair branch stores lo
     at +0 and hi at +8 of a source-typed alloca via an i8-typed GEP.
   - `codegen_method_prologue` does the same for non-receiver params.
     Receivers (`&this`, by-value `this`, `this: T*`) are handled by
     the existing override above the loop, untouched.
   - `declare_method`'s open-coded slot assembly was restructured to
     pre-classify each non-receiver param (`user_plans` array) and
     push **all** `llvm_slots` per param rather than just slot 0.
     `total_user_slots` and `total_params` are computed from the
     plans; `registry_arity` is now `user_param_count + (implicit_this
     ? 1 : 0)`, source-level and decoupled from the LLVM slot count
     so call-site arity lookups in `call_emitter.cryo` still match.

3. **`docs/abi.md`:** Section 2 (Parameters) updated to explain that
   the prologue and `declare_method` are DirectPair-ready, the
   classifier hasn't flipped, and the suspected failure mode is an
   LBuilder ≤ 8 byte coercion mismatch surfacing in
   `StringCache::get_or_create`.

### Validation gates currently green

- `make cryo` — builds, ~132 pre-existing move-checker warnings (see
  the round-1 handoff §5.4 if you don't already know about these).
- `cd tests && /workspaces/CryoLang/compiler/build/bin/cryo test` —
  **491 passed, 0 failed, 1 ignored**.
- `make selfhost-check` — `✓ FIXED POINT OK`, md5
  `20cbcba9aae2a24a6f547cb52732190a`, IR size 30,257,894 bytes,
  stage-3 == stage-4 byte-identical.

If your numbers don't match this, do `git diff` and confirm you're at
the state described above. The forward-looking prologue / declare_method
changes are dormant code paths until `classify_param` produces DirectPair
plans, so the IR md5 holds.

---

## 2. The work you're picking up

### What's done

The full §3.5 (uniform classification covering Phase 2 sret/byval +
Phase 3 ≤ 8 / DirectPair) on the **return side** is shipped end-to-end.
`classify_return` applies the Phase 3 rules uniformly for both extern-C
and Cryo-internal callees; `codegen_return` does the defining-side
coercion; `call_emitter.cryo` does the post-call coercion;
`declare_method` correctly wraps DirectPair returns in `{lo, hi}`
literal structs. §3.4 (Tuple / Optional / Result joining the aggregate
match) is also in. §3.3 multi-float SSE (`<2 x float>` for two-F32
buckets) shipped in commit `2d941bdc`.

### What's not done

`classify_param`'s Phase 3 flip — the **param side** of §3.5. The
prologue and `declare_method` are already wired to consume DirectPair
plans correctly (see §1 above). Flipping the classifier surfaces a
self-bootstrap heap-corruption bug we did not isolate.

### Specifically: the open bug

When `classify_param` returns `Direct`-with-coercion (≤ 8 byte
aggregate → single eightbyte slot) and/or `DirectPair` (9–16 byte
aggregate → two eightbyte slots) for Cryo-internal parameters, the
following sequence reproduces it:

1. `make cryo` succeeds (the pinned compiler at `bin/cryo` has the
   OLD ABI, so it compiles current source with old ABI — stage-2
   binary has the NEW classifier code internally, but stage-2 itself
   uses the OLD ABI in its own function signatures).
2. `cd tests && cryo test` passes **all 491 tests** (because the
   stage-2 binary uses old ABI internally, even though it generates
   new ABI for the compiled output).
3. `make selfhost-check` FAILS at **stage 5/6** (`stdlib via stage-3
   → .bin/self/s3`). Stage-3 is the compiler binary built by stage-2
   from current source — i.e., the first binary that has the **new
   ABI in its own internal signatures**. Stage-3 segfaults when
   building stdlib.

GDB backtrace of the stage-3 crash:

```
#0  llvm::IntegerType::get(LLVMContext&, unsigned int)
#1  llvm::ConstantDataArray::getString(LLVMContext&, ...)
#2  llvm::IRBuilderBase::CreateGlobalString(...)
#3  LLVMBuildGlobalStringPtr ()
#4  LBuilder::build_global_string_ptr(...)
#5  StringCache::get_or_create(this, text, builder)
#6  DeclarationEmitter::codegen_global_var(...)
```

Translation: the compiler's `StringCache::get_or_create` calls
`builder.build_global_string_ptr(text, name)`. Inside LLVM, fetching
the LLVMContext from the builder crashes — the LLVMBuilderRef has bad
bits.

`StringCache::get_or_create`'s signature:

```cryo
get_or_create(mut &this, text: string, builder: LBuilder) -> LValue
```

`LBuilder` is a single-ptr struct (`type struct LBuilder { raw: void*; }`)
— 8 bytes. With Phase 3 param classification, it lowers to a single
`i64` LLVM slot. The prologue allocates an `%LBuilder`-typed slot,
stores the incoming `i64` (opaque pointer — bytes match), and binds
`builder` to that slot. The body's `builder.build_global_string_ptr`
takes the lvalue address and dereferences `.raw` via struct GEP.

In **isolation** this round-trip works. The simple repro:

```cryo
type struct LB { raw: void*; }
function consume(b: LB) -> boolean { return b.raw != null; }
function main() -> i32 { ... }
```

produces correct IR (`i64` param, alloca of `%LB`, store, load,
deref). Runs correctly.

But in the self-bootstrap context (stage-3 compiling stdlib), it
crashes. The bug surfaces only when:

- The compiler's internal `StringCache::get_or_create` was itself
  compiled with the new ABI (i.e., stage-2's codegen of current
  compiler source).
- Stage-3 actually runs.

The previous round flagged something similar — a real `void* !=`
comparison issue that turned out to be a stale comment, not a bug
(see the closing notes in commit `2d941bdc`'s prior diff). This one
is different: it's a real self-bootstrap mismatch that doesn't
reproduce in single-project compilation.

---

## 3. Recipe for the next session

### Step 1 — Confirm the baseline

```bash
git status                       # should show the 3 modified files
git log -3 --oneline             # last commit: 2d941bdc
cd /workspaces/CryoLang && make cryo
cd tests && /workspaces/CryoLang/compiler/build/bin/cryo test    # 491 passed
cd /workspaces/CryoLang && make selfhost-check                   # ✓ FIXED POINT OK
```

If selfhost is red here, **stop** and figure out why before flipping
anything — the forward-looking changes in `declaration_emitter.cryo`
shouldn't affect IR in this state, so a red baseline means something
upstream regressed.

### Step 2 — Re-enable the classifier flip

In `compiler/src/compiler/codegen/abi.cryo`, find `classify_param`
(around line 287). Replace the body's "≤ 16 byte aggregates: legacy
plain Direct(struct)" block with the Phase 3 rules from
`classify_param_extern_c` — the structure looks like:

```cryo
if (is_aggregate) {
    const sz: u64 = concrete.size_bytes();
    if (sz > 16) { /* byval, same as today */ }
    if (sz > 0 && sz <= 8) {
        const orig: LType = tm.map_type(t);
        const slot: LType = this.eightbyte_slot_type(concrete, 0, sz);
        if (slot.is_valid()) {
            mut slots_d: LType[] = [];
            slots_d.push(slot);
            return ParamPlan { cls: Direct, llvm_slots: slots_d,
                               attr: None, pointee_ty: orig, ... };
        }
    }
    if (sz > 8 && sz <= 16) {
        const orig: LType = tm.map_type(t);
        const lo_slot: LType = this.eightbyte_slot_type(concrete, 0, 8);
        const hi_slot: LType = this.eightbyte_slot_type(concrete, 8, sz);
        if (lo_slot.is_valid() && hi_slot.is_valid()) {
            mut slots_dp: LType[] = [];
            slots_dp.push(lo_slot);
            slots_dp.push(hi_slot);
            return ParamPlan { cls: DirectPair, llvm_slots: slots_dp,
                               attr: None, pointee_ty: orig, ... };
        }
    }
}
```

(Look at `classify_return` in the same file for an exact template
that's already shipping in production — Phase 3 for returns is
already live.)

Then `make cryo`, `cryo test` (should still pass 491), and
`make selfhost-check` (will fail at stage 5/6 — that's the bug).

### Step 3 — Isolate the heap corruption

This is the actual hard part. Suggestions in priority order:

**A. Diff stage-2's vs stage-3's IR for `StringCache::get_or_create`.**

Stage-2 was built by the pinned compiler (OLD ABI). Stage-3 was built
by stage-2 (NEW ABI). Both have current-source's `StringCache::get_or_create`
as the source-level definition, but the LLVM signatures differ
because stage-2 and stage-3 use different ABIs.

```bash
make selfhost-check        # let it fail at stage 5
# stage-2 binary: compiler/build/bin/cryo
# stage-3 binary: compiler/build/self/s3/bin/cryo
# stage-2 IR:    compiler/build/obj/*.ll  (Note: pinned-built, OLD ABI)
# stage-3 IR:    compiler/build/self/s3/obj/*.ll  (NEW ABI in callee sigs)
```

Look at the IR for `StringCache::get_or_create` in stage-3:
- What is the LLVM signature of `get_or_create`?
- How is `builder: LBuilder` declared at the LLVM level? Is it `i64`
  (Direct-with-coercion) or `%LBuilder` (legacy)? It should be `i64`
  after the flip.
- What does the prologue look like? Where does the alloca live? Is
  the store correct?

**B. Diff what calls `StringCache::get_or_create` between stage-2 and
stage-3.**

The callers are inside the compiler (e.g.,
`DeclarationEmitter::codegen_global_var`). The call site marshals an
`LBuilder` value through `codegen_call_direct`. With the flip, the
arg-pass should hit the `(Integer expected, Struct actual)` branch
in `codegen_call_direct` (look around line 666 in
`compiler/src/compiler/codegen/ops/expr_ops.cryo`), spill the struct
to a slot, and reload as `i64`.

- Does the call site emit the spill-and-reload correctly?
- Is the spilled value the right `LBuilder` instance?

**C. Cross-module declaration agreement.**

When module A defines `StringCache::get_or_create` and module B
re-declares it (via `SymbolResolver::declare_extern_function_overload`
→ `classify_signature`), do both sides see the same plan?

If module A applies the new classifier and module B applies the old
(for whatever reason — e.g., one passes a different FunctionType
through), the two declare with different signatures. LLVM might
accept the redundant declarations (one will silently win); call sites
in B will use B's shape but the function body in A was compiled with
A's shape. Heap corruption follows.

```bash
grep -rn "StringCache.*get_or_create" compiler/build/self/s3/obj/*.ll
```

Look for a `define` of `get_or_create` and any `declare` of the same
symbol. They should match. If they don't — that's the smoking gun.

**D. The hypothesis I most want falsified.**

The simple repro (single project, no self-bootstrap) shows the
round-trip works for an 8-byte single-ptr struct. The self-bootstrap
fails. The difference is that in self-bootstrap, the compiler
binary's OWN `StringCache::get_or_create` is running while it
compiles code that ALSO uses `StringCache::get_or_create`. If the
LLVMContext or LLVMBuilder handle being passed in by the compiler's
own caller is somehow swapped or aliased — e.g., because a Cryo array
or vector storage holding a `Pair<string, void*>` reads/writes through
an aliased pointer — the bits stored as the `LBuilder.raw` could be
garbage from somewhere else's allocation. That's what the
`corrupted double-linked list` glibc check is catching: malloc's
freelist got walked into.

I would start by adding a `cdebug` print in
`StringCache::get_or_create` (line ~33 of
`compiler/src/compiler/codegen/state/string_cache.cryo`):

```cryo
cdebug("[StringCache] text=%p builder.raw=%p\n", text, builder.raw);
```

then re-run `make selfhost-check`. If `builder.raw` prints sensibly
for the first N calls and then suddenly becomes a bad pointer, you
have a localized failure window to bisect.

### Step 4 — Once isolated, fix

The fix shape depends on what's broken. Don't guess — confirm with
the diagnostic from Step 3 first. The most likely fixes:

- **Plan disagreement across modules:** route every cross-module
  extern declaration through `SymbolResolver::build_function_plan`
  (which already calls `classify_signature`), and make sure no
  hand-rolled `LLVMFunctionType` constructions exist for compiler-
  internal functions.
- **Prologue store doesn't preserve bits:** double-check that
  `build_store(arg, alloca)` writes the right size. With opaque
  pointers, the alloca's element type is irrelevant for the store;
  what matters is `arg`'s type width. For an `i64` arg into a
  `%LBuilder` (8-byte) alloca, this should be sound. If you see a
  `store i8` or `store i32` instead, the bug is in how the seam
  picks the slot type.
- **Call-site coercion uses wrong type:** at
  `codegen_call_direct`'s Integer/Float/Double/Vector ← Struct
  branch, the spill alloca is sized to `expected_ty` (the LLVM
  param's scalar type). For an 8-byte source `LBuilder` spilling
  into an `i64` slot, this is fine. For DirectPair (9–16 byte
  source), `codegen_call_direct_dp_expand` (around line 757) does
  the expansion. Either should be sound; an off-by-one in the
  source-vs-expected slot iteration is plausible.

### Step 5 — Re-validate

```bash
make cryo
cd tests && /workspaces/CryoLang/compiler/build/bin/cryo test    # 491 passed
cd /workspaces/CryoLang && make selfhost-check                   # ✓ FIXED POINT OK
```

If both gates green, you've closed §3.5 param-side. The full Phase 3
rollout is complete. Retire `classify_param_extern_c` and
`classify_signature_extern_c` (they become aliases). Update
`docs/abi.md` Section 2 (Parameters) to reflect that
Cryo-internal classifies the same as extern-C.

### Step 6 — Re-pin (optional)

`make pin-cryo`, commit `bin/cryo`. The pin is currently older than
the round-2 commit (`1998d646`); refreshing it after Phase 3 ships
end-to-end is a reasonable hygiene step, but not required for
correctness.

---

## 4. Validation protocol

Same as round 2:

1. `make cryo` — must succeed, ~132 pre-existing warnings are fine.
2. `cd tests && cryo test` — must report **491+ passed, 0 failed,
   1 ignored**. Run **from inside `tests/`** — the runner expects
   `cryoconfig` in cwd or a parent.
3. `make selfhost-check` — must end with `✓ FIXED POINT OK` and
   report a stage-3 == stage-4 IR md5. The md5 will change as you
   land behavior; what matters is stage-3 and stage-4 agree.

The handoff invariant: selfhost is the load-bearing gate. The test
suite can pass while selfhost fails (this round demonstrated that —
491 tests passed with the broken classifier flip in place; selfhost
caught it at stage 5/6).

### Crash-debugging recipe

Same as round 2:

1. `make selfhost-check` fails → check which stage.
2. `cat build-logs/selfhost-check/stage-05.log` for the captured
   output. (Stage 5 = stdlib-via-stage-3; logs were empty in this
   session because the segfault was immediate — no output buffered.)
3. `/workspaces/CryoLang/compiler/build/self/s3/bin/cryo build` from
   `stdlib/` to reproduce manually.
4. `gdb -batch -ex run -ex 'bt 25' --args
   /workspaces/CryoLang/compiler/build/self/s3/bin/cryo build` from
   `stdlib/`.

The mangled names look like
`C$8Compiler.7Codegen.5State.11StringCache.11StringCache-13get_or_create...`
— map them back to source with the `MangledName` decoder pattern
(prefix `C$<len><namespace-segment>...` is straightforward to read
once you've seen it).

---

## 5. File map

```
compiler/src/compiler/codegen/abi.cryo
  └─ `classify_param` (~line 287): currently byval-only.  Docstring
     describes the §3.5 follow-up.  The Phase 3 logic to put back is
     in `classify_param_extern_c` immediately below (~line 425) —
     copy that body, adjust comments.

compiler/src/compiler/codegen/ops/declaration_emitter.cryo
  └─ `declare_method` (~line 818): slot assembly walks `user_plans`
     and pushes all `llvm_slots` per param.  `registry_arity` is
     source-level (`user_param_count + implicit_this`), independent
     of LLVM slot count.
  └─ `codegen_function_prologue` (~line 1277): walks `mut llvm_idx`
     by `plan.llvm_slots.length`.  DirectPair branch: alloca source
     struct, store lo at +0, hi at +8 via i8-typed GEP.
  └─ `codegen_method_prologue` (~line 1532): same DirectPair handling
     for non-receiver params, with `param_idx` rather than `llvm_idx`
     as the name.

compiler/src/compiler/codegen/state/string_cache.cryo
  └─ `get_or_create` (~line 27): the function that crashes inside
     LLVM in the stage-3 segfault.  Single-line body: `format` +
     `builder.build_global_string_ptr(text, name)` + cache.  The
     `builder: LBuilder` param is the suspect.

compiler/src/compiler/codegen/ops/expr_ops.cryo
  └─ `codegen_call_direct` (~line 559): arg-pass coercion lives here.
     Lines 666+ handle the Integer/Float/Double/Vector ← Struct case
     used for §3.2 / §3.3 small-aggregate params; lines 689+ handle
     the Struct ↔ Struct (literal vs named) case for §3.5 DirectPair
     return coercion.
  └─ `codegen_call_direct_dp_expand` (~line 757): 1-source-arg →
     2-LLVM-slots expansion path.  Already in use for extern-C
     DirectPair params; would fire for Cryo-internal too after the
     classifier flip.

docs/abi.md
  └─ As-built doc.  Section 2 (Parameters) calls out that prologue
     and `declare_method` are DirectPair-ready, classifier is not,
     and describes the suspected failure mode.

docs/abi-lowering-plan.md
  └─ Original design doc.  Don't edit — keep as the historical
     intent.  When §3.5 closes, the as-built doc (above) gets the
     update.
```

---

## 6. Cryo language quirks the next agent will hit

(Same as round 2; re-stated for self-containment.)

- `else if` is **not** an expression-form. `if (a) { … } else if (b) { … }`
  works as a statement but not as an expression init.
- `==` on `string` works as string compare; `==` on `void*` works as
  pointer compare (the comment in `expr_ops.cryo` claiming it's broken
  was stale — round 3 confirmed it works correctly).
- `class` is a reserved keyword; use `cls`.
- Move-checker `E0452 use of moved value` is a **warning**, not an
  error. ~132 of these pre-exist; don't chase.
- References don't index arrays — `&Foo[]` can't be indexed; pass by
  value.
- Cryo `string` is `i8*` at the LLVM level.
- Method receivers are `&this` / `mut &this` / `this`. The
  parser-synthesized `&this` reference annotation does NOT always
  round-trip through the type arena correctly — `declare_method` has
  override logic that forces `T*` for these. When you touch
  `declare_method`, keep that override; do **not** route the
  receiver through `classify_param(param.resolved_type)`.
- `cryo test` filter is **positional**, not `--filter`:
  `cryo test AbiCInterop` works; `--filter AbiCInterop` does not.
- `cryo test` ignores `emit_llvm = true` in `tests/cryoconfig`.
  Use `cryo build --emit-llvm` if you need IR; for the test runner
  output binary's IR look in `tests/build/obj/*.o` (disassemble with
  `objdump -d -M intel`).

---

## 7. Useful one-liners

```bash
# Full build chain.
make cryo

# Tests (must run from tests/ or use `make test`).
cd tests && /workspaces/CryoLang/compiler/build/bin/cryo test
make test
make test ARGS="AbiCInterop"        # positional filter

# Byte-identity gate.  Load-bearing.
make selfhost-check

# Stage logs (often empty if the stage segfaulted immediately).
ls build-logs/selfhost-check/

# Manually reproduce stage-3's stdlib build crash.
/workspaces/CryoLang/compiler/build/self/s3/bin/cryo build  # from stdlib/

# Compare stage-2 vs stage-3 IR for one function.
grep -B1 -A30 'StringCache.*get_or_create' \
  compiler/build/obj/*.ll \
  compiler/build/self/s3/obj/*.ll

# Inspect a test project's IR.
cd tests && cryo build --emit-llvm   # emits .ll under tests/build/obj/

# Pin after Phase 3 lands cleanly.
make pin-cryo
```

---

## 8. Sanity-check numbers

As of this handoff:

- `make cryo` succeeds, ends with `==> Self-hosted cryo built: ...`.
- `cd tests && cryo test` reports **491 passed**, 0 failed, 1 ignored.
- `make selfhost-check` reports `✓ FIXED POINT OK`, md5
  `20cbcba9aae2a24a6f547cb52732190a`, IR size 30,257,894 bytes.
- `git status` shows 3 modified files (none staged); HEAD is
  `2d941bdc`.

If you match all four, you're at the round 3 endpoint. The dormant
prologue and `declare_method` changes are forward-looking — they let
the next attempt at flipping the classifier go through without
re-deriving the slot-walking and slot-assembly work.

---

## 9. Recommended next-session scope

In order of risk-adjusted value:

1. **Isolate the stage-3 segfault** (recipe steps 2–3). Even if you
   don't fully fix it, narrowing the failure mode to one of the four
   hypotheses in step 3 would massively unblock the next attempt.
2. **Land the §3.5 param-side flip** if the diagnosis points at a
   localized fix.
3. **Retire `_extern_c` classifier variants** once both sides are
   unified.
4. **Re-pin** `bin/cryo` after the dust settles.

Out of scope (per the original plan in `docs/abi-lowering-plan.md`
§5): multi-target ABI, `va_list` per-target, `vasprintf` replacement
— these are Phase 5 and orthogonal to §3.5 param-side.

Good luck. Round 3 closed the seam shape for the param side
(prologue + `declare_method`) and confirmed the failure mode of the
classifier flip; round 4 should be able to focus on isolating one
specific code path and landing the flip cleanly.
