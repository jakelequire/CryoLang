# CryoLang Stage-3 Self-Host — Handoff

**Date:** 2026-04-28, end of session.
**Branch:** `main`. Tip: `26e0266`.
**Working tree:** unstaged fixes in 5 cryoc files (see §3). User handles commits.

---

## 1. State

🟢 **Stage-3 cryoc compiles the stdlib end-to-end.** All 53 modules → `stdlib/.bin-s3/libcryo.a` (645 KB). Steps 1–4 of the build chain are green. This is a new milestone — the prior handoff bottomed out at module 52 with `E0167: unresolved generic instantiation`.

Four cryoc-source bugs were diagnosed and fixed this session (details in §3). All four are real codegen / pass-pipeline bugs, no workarounds. The build chain `bootstrap → stage-2 → stage-3 → stdlib` is fully closed.

**Not yet verified this session:** stage-3 cryoc compiling cryoc itself (a "stage-4" run). The session's focus was the stdlib path; rerunning `stage-2 build` against the cryoc source with the stage-3 binary (or just `STAGE3 build` from `cryoc/`) would close the loop. See §7.

---

## 2. Build chain (do not "fix" the layout)

```
bin/cryo (C++ bootstrap)  ──┐
                             ▶  cryoc/build/cryoc          (stage 2)
                             │
stage-2 cryoc           ────┴──┐
                                ▶  cryoc/build/bin/cryoc   (stage 3)
                                │
stage-3 cryoc          ────────┴──▶ stage-3 self-host of stdlib  ✅
```

Resume script (lives only in `/tmp` on this machine — **uses separate build dirs for stage-2 vs stage-3 stdlib output**, so the IRs can be diffed without overwriting):

```bash
cat > /tmp/full_build.sh <<'EOF'
#!/bin/bash
set +e
ROOT=/workspaces/CryoLang
BOOT=$ROOT/bin/cryo
STAGE2=$ROOT/cryoc/build/cryoc
STAGE3=$ROOT/cryoc/build/bin/cryoc

run_stage3_stdlib() {
    echo "=== STAGE-3 SELF-HOST: stdlib via stage 3 (build-dir=.bin-s3) ==="
    [ -x "$STAGE3" ] || { echo "  stage-3 missing — skipping"; return; }
    cd "$ROOT/stdlib"
    rm -rf .bin-s3 && mkdir -p .bin-s3/obj
    "$STAGE3" build --build-dir=.bin-s3 > /tmp/stage3_stdlib.log 2>&1
    local rc=$?
    echo "stage3-stdlib exit $rc"
    if [ "$rc" -ne 0 ]; then
        echo "  highest module reached:"
        grep 'Phase 2: Processing module' /tmp/stage3_stdlib.log | tail -1 | sed 's/^/    /'
        echo "  last 10 lines:"
        tail -10 /tmp/stage3_stdlib.log | sed 's/^/    /'
    fi
}
[ "$1" = "stage3" ] && { run_stage3_stdlib; exit; }

echo "=== STEP 1: stdlib via bootstrap (.bin) ==="
cd "$ROOT/stdlib" && rm -rf .bin && mkdir -p .bin/obj
"$BOOT" build > /tmp/step1.log 2>&1; echo "step1 exit $?"

echo "=== STEP 2: cryoc via bootstrap ==="
cd "$ROOT/cryoc"
"$BOOT" build > /tmp/step2.log 2>&1; echo "step2 exit $?"

echo "=== STEP 3: stdlib via stage-2 (.bin-s2) ==="
cd "$ROOT/stdlib" && rm -rf .bin-s2 && mkdir -p .bin-s2/obj
"$STAGE2" build --build-dir=.bin-s2 > /tmp/step3.log 2>&1; echo "step3 exit $?"

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
```

**Key dirs after a green build:**
- `stdlib/.bin/`     — bootstrap stdlib build (step 1)
- `stdlib/.bin-s2/`  — stage-2 stdlib build (step 3) — `obj/*.ll` for diff
- `stdlib/.bin-s3/`  — stage-3 stdlib build (final) — `obj/*.ll` for diff
- `cryoc/build/cryoc.ll` — IR of stage-2 (bootstrap-emitted)
- `cryoc/build/obj/*.ll` — IR of stage-3 (stage-2-emitted)

`--build-dir=...` was added by the user mid-session so stage-2 and stage-3 stdlib outputs don't overwrite each other. Use it.

Bootstrap rebuild (only after editing C++ in `src/` or `include/`):

```bash
rm -rf bin/.o && rm -f bin/cryo && make compiler
```

The makefile doesn't track header dependencies — clean the .o tree on header changes.

---

## 3. Bugs fixed this session (uncommitted)

All four are diagnosed root causes, no workarounds. Order is the order they were unblocked.

### Bug A — `E0167: unresolved generic instantiation after monomorphization`
**File:** `cryoc/src/compiler/passes/specialization.cryo`

The `GenericValidation` pass walks every `InstantiatedType` in the arena and reports any whose `resolved_type` is unset. It used to skip "template-internal" instantiations by checking only the **direct** type args for `GenericParam`/`BoundedParam`. That missed nested cases like `Option<Result<T, E>>` — the direct arg is an `InstantiatedType` (Result), and Result's own args are `GenericParam`s.

The validator now uses `type_contains_generic_param_v` (added as a free function to keep the non-virtual-dispatch property required by the C++ codegen's vtable bug), which mirrors `Monomorphizer::type_contains_generic_param` and recurses through `Pointer`, `Reference`, `Array`, `Optional`, `Tuple`, `Function`, and nested `InstantiatedType`.

Diagnosis pivot: enriching the validator's error message with `[id=… kind=…]` for each type arg revealed `id=455: Option<Result<T, E>>` with `T,E` still as `GenericParam`s — exactly the case the shallow filter missed.

### Bug B — `declare_intrinsic` SIGSEGV during Phase 7
**File:** `cryoc/src/compiler/codegen/decl_codegen.cryo`

`declare_intrinsic` was calling `fixed_param_count(node)` where `node` is `IntrinsicDeclNode*`, but `fixed_param_count`'s parameter type is `FunctionDeclNode*`. The two classes both inherit from `DeclarationNode` but have different field layouts. Reading `func.is_variadic` lands past the end of an `IntrinsicDeclNode`'s allocation; on stage-3 that read landed in unmapped memory and SIGSEGV'd inside the very first stdlib codegen module (`std::core::intrinsics`).

Added a separately-typed `fixed_param_count_intrinsic(node: IntrinsicDeclNode*)`. Same logic, correct type.

This is a **latent** bug — the prior pass-6 failure (Bug A) gated codegen, so it never surfaced. With Bug A fixed, it triggered immediately.

### Bug C — Narrow-store miscompile for `i8` array element assignment
**File:** `cryoc/src/compiler/codegen/ir_generator.cryo` (`visit_binary_expr`)

`name_buf[k] = '_'` (where `name_buf: i8*` and `'_'` is a `char` lowering to `i32`) emitted a 4-byte `store i32` into a 1-byte slot, clobbering the next 3 bytes. The existing `chartrunc` branch only fired for `String`-typed indexables — it didn't cover `Pointer-to-i8` or `Array-of-i8`.

The fix decides on truncation by the **destination element type** (`node.left.resolved_type`): truncate to `i8` whenever it's `Char`, `i8`, or `u8`. Catches all three indexable forms (string, ptr-to-i8, array-of-i8) in a single check.

Diagnosis pivot: in-loop probes revealed the corruption pattern. Storing `'_'` (95) at index 3 left bytes 4/5/6 reading as 0:
```
k=3 before=58 (':')  after=95 ('_')
k=4 before=0         after=0      <-- should have been ':' (58)
k=5 before=0         after=0      <-- should have been 'c' (99)
k=6 before=0         after=0      <-- should have been 'o' (111)
```
That's the i32 store stomping the neighbours.

### Bug D — Sign-unaware integer widening in struct-field initialization
**Files:** `cryoc/src/compiler/codegen/expr_codegen.cryo`, `cryoc/src/compiler/codegen/stmt_codegen.cryo`, `cryoc/src/compiler/codegen/ir_generator.cryo` (`visit_new_expr`'s `struct_init` path)

This was the OOM-blow-up that surfaced after Bug C: stage-3's stdlib emit hung in `LLVMTargetMachineEmitToFile` for `std::core::primitives` and the process climbed to 9 GB RSS before Codespaces SIGTERM'd it.

The IR diff between stage-2 (`{ ptr, i64, i64 }`) and stage-3 (`[4294967295 x i32]`) for the parameter type of `string::new(value: char[])` showed stage-3 was producing a 4 GB **fixed** array because `t.is_fixed()` returned `true` for what should have been a dynamic array (sentinel size = `-1`).

Root cause: `new ArrayAnnotation { size: -1, ... }` was being miscompiled. `-1` is parsed as `i32 -1`, and the field-init store was emitted as a literal `store i32 -1` into the `i64` slot — leaving the upper 4 bytes uninitialised. After malloc those 4 bytes happened to be zero, so `size` came out as `0x00000000FFFFFFFF` = **4294967295**, a positive i64. `is_fixed()`'s signed `>= 0` then returned `true`. `map_array` did `t.size as u32` (truncating back to 4294967295), and asked LLVM for `[4294967295 x i32]`.

There were three sites that did integer-width widening with `build_zext`. All three now pick `sext` vs `zext` based on the destination's signedness:
- `expr_codegen.cryo::codegen_struct_literal` — struct literals (`Foo { a: -1 }`)
- `stmt_codegen.cryo::codegen_local_var` — local `mut x: i64 = -1`
- `ir_generator.cryo::visit_new_expr`'s `struct_init` block — `new T { fields }`

Diagnosis pivot: dumping `*.pre.ll` from the object emitter and diffing stage-2 vs stage-3 IR showed the bad parameter type. Then grepping `store i32 -1` in stage-3 IR pointed straight at the `ArrayAnnotation` malloc-init.

This is the same family as the prior `feedback_int_widening_assignment_bug.md` (i32 → i64 leaving upper bits uncleared) but extended: **signed** widening also needs `sext`, not just zero-aware `zext`.

---

## 4. Hard rules (carry forward)

These have come up repeatedly — keep honouring them.

- **No workarounds, fallbacks, or hacks.** Diagnose root causes; never relocate code "where it builds" to dodge a tooling bug. (`feedback_no_workarounds.md`)
- **No safe-fallback defaults for invariant violations.** Bail with a real diagnostic, don't silently substitute placeholders.
- **No fallback chains in lookups.** Bare-name DI lookups are reserved for C externs.
- **No inline string manipulation in codegen.** Add a method to `CodegenContext`/`DeclarationIndex`/`InternTable` instead.
- **Variables must declare their type with `:`.** `const x: int = 10;`, no inference shorthand.
- **Trait `This`, not `Self`.** (`feedback_this_not_self_keyword.md`)
- **`GenericValidation` and similar passes must avoid virtual dispatch on `Type*`.** Use only `t.kind` (plain enum field) plus subclass casts and field reads. The C++ codegen has a known vtable offset bug. The new `type_contains_generic_param_v` helper is a free function for exactly this reason — do not "refactor" it onto `TypeArena` as a virtual method.

---

## 5. Investigation discipline

- **Use IR for debugging — and keep stage-2 vs stage-3 outputs in separate dirs.** `--build-dir=...` is the right tool. The session's biggest unblock came from dumping `*.pre.ll` from the object emitter and `diff -u`-ing the two:
  - `stdlib/.bin-s2/obj/<module>.ll` vs `stdlib/.bin-s3/obj/<module>.o.pre.ll`
  - or `cryoc/build/cryoc.ll` (bootstrap-emitted) vs `cryoc/build/obj/<module>.ll` (stage-2-emitted)
- **Stop iterating blindly.** Each full build chain is ~3 minutes. Prefer to add **many** probes per build cycle, then look — don't add one-line probes one at a time. The user will (correctly) interrupt if you do; this rule was reinforced mid-session.
- **gdb works on stage-3** (the bootstrap is fast enough that signals fire before any timeout). Use `gdb -batch -ex 'set pagination off' -ex 'attach <PID>' -ex 'thread 1' -ex 'bt 30' -ex 'detach' -ex 'quit'` for live attach to a hung run.
- **`ps -o ...| grep VmRSS`-loop** worked well to confirm an OOM-blow-up vs a true hang.
- **The user handles all rebuilds and commits** unless explicitly told otherwise. In auto mode, build/commit yourself, but err on the side of asking when in doubt.

---

## 6. Bootstrap is a trapdoor — but patchable when needed

Long-term aim: replace `bin/cryo` with stage-3 cryoc. **But** specific narrow bugs in the bootstrap can be patched when they block progress (the previous session did this for the Token UAF). The makefile doesn't track header deps — clean the .o tree on header changes (`rm -rf bin/.o`).

C++ source layout:
- `src/Codegen/` — bootstrap codegen (CodegenVisitor.cpp dispatches; per-expression / per-statement subdirs)
- `src/AST/` — bootstrap AST builder
- `src/Parser/Parser.cpp` — bootstrap parser (single 8000-line file)
- `src/Lexer/lexer.cpp` + `include/Lexer/lexer.hpp` — Token, Lexer, tokenization
- `src/Compiler/` — pass manager, compiler instance, module loader

---

## 7. Next steps

In rough priority order:

1. **Verify stage-3 compiles cryoc itself.** The build chain checks stage-3 against stdlib but not against cryoc source. A simple test:
   ```bash
   cd cryoc && rm -rf build/obj build/bin && /workspaces/CryoLang/cryoc/build/bin/cryoc build
   ```
   If that succeeds, the bootstrap is genuinely replaceable.

2. **Audit other `build_zext` sites for sign-awareness.** I found three (codegen_struct_literal, codegen_local_var, visit_new_expr). Possible others:
   - `cryoc/src/compiler/codegen/stmt_codegen.cryo:156` (`ret.zext`)
   - `cryoc/src/compiler/codegen/stmt_codegen.cryo:497` (`match.coerce.zext`)
   - `cryoc/src/compiler/codegen/expr_codegen.cryo:703` (`arg.trunc` is fine; check the matching widen path)
   - `coerce_icmp_operands` always-zext for `match` patterns (line 497 above) — likely wrong for negative subjects too.
   Each `zext`-only call is a latent bug for any signed source whose value happens to look "non-negative" because the upper bits zeroed out by accident. Audit and add the `sext` branch where the destination is signed.

3. **Stop the diff-emit `.pre.ll` dump leak.** I removed it before declaring success; if you re-add it for further debugging, gate it on `ctx.debug_mode` so non-debug runs don't write 17 MB per module to disk.

4. **Strip session-only diagnostic prints once stage-3 is stable across cryoc + stdlib.** The pre-existing prints listed in §8 are still load-bearing for ongoing debugging. Don't strip them yet.

5. **`5 [BCTOR-BAIL]` on `BaseASTVisitor::BaseASTVisitor` arity=1** is still cosmetic — no functional impact. Investigate when convenient; not blocking.

6. **Commit the four fixes.** The user said they'd handle commits; a sensible split:
   - One commit for Bug A (specialization filter) + diagnostic enrichment.
   - One commit for Bug B (`fixed_param_count_intrinsic`).
   - One commit for Bug C (narrow-store chartrunc generalisation).
   - One commit for Bug D (signed widening — three files).

---

## 8. Diagnostic prints to keep / remove

These were already in source before this session and are load-bearing for ongoing debugging:

| File | Prefix | Purpose |
|---|---|---|
| `cryoc/src/compiler/parser/parser.cryo` | `[PARSE-DBG]` | Parser node creation tracking |
| `cryoc/src/compiler/passes/type_resolution.cryo` | `[VT-DBG]` | Vtable/method registration |
| `cryoc/src/compiler/codegen/ir_generator.cryo` | `[BCTOR-*]`, `[CTOR-CHECK]` | Base-ctor call wiring |
| `cryoc/src/compiler/passes/pass_registry.cryo` | `[TypeDecl]` | Pass-stage tracing |

Don't strip them until stage-3 fully self-hosts cryoc itself (i.e. step #1 in §7 is green).

This session added and **already removed** probes in `passes.cryo`, `instance.cryo`, `llvm_types.cryo`, `expr_codegen.cryo`, `ir_generator.cryo` (chartrunc/widen instrumentation, `DISPOSE-DBG`/`OBJ-DBG`/`EMIT-DBG`/in-loop char traces).

---

## 9. Memory entries to consider writing

Worth saving to `~/.claude/projects/-workspaces-CryoLang/memory/` before context turns over:

- **`feedback_widen_signedness.md`** — `i32 -1 → i64` must `sext`, not `zext`. Three sites fixed; audit for more (see §7.2). Same family as `feedback_int_widening_assignment_bug.md` but for signed sources.
- **`feedback_distinct_decl_layouts.md`** — `IntrinsicDeclNode` and `FunctionDeclNode` both inherit from `DeclarationNode` but have different field layouts. Don't pass one where the other is expected; reading past the end is a SIGSEGV waiting to happen.
- **`feedback_validator_recurse.md`** — Pass filters that mirror monomorphizer behaviour must recurse through compound types the same way; shallow direct-arg checks miss `Outer<Inner<T>>` patterns.

Each is a real recurring trap and worth one entry.

---

## 10. Commands cheatsheet

```bash
# Stage-3-only re-test (faster than full chain when stage-2 hasn't changed)
/tmp/full_build.sh stage3

# Full build chain
/tmp/full_build.sh

# Backtrace from a fresh stage-3 hang/segfault
cd $ROOT/stdlib && rm -rf .bin-s3 && mkdir -p .bin-s3/obj
/workspaces/CryoLang/cryoc/build/bin/cryoc build --build-dir=.bin-s3 > /tmp/stage3_stdlib.log 2>&1 &
PID=$!
sleep 20  # wait for it to enter the bug
gdb -batch -ex 'set pagination off' -ex 'attach '$PID -ex 'thread 1' -ex 'bt 30' \
    -ex 'detach' -ex 'quit' 2>&1 | grep -E '^#|signal'
kill -9 $PID

# Watch RSS for OOM diagnosis
while kill -0 $PID 2>/dev/null; do
    grep VmRSS /proc/$PID/status 2>/dev/null
    sleep 3
done

# Compare stage-2 vs stage-3 IR for a specific stdlib module
diff stdlib/.bin-s2/obj/std__core__primitives.ll \
     stdlib/.bin-s3/obj/std__core__primitives.o.pre.ll | head -100

# Find error-emit sites in cryoc
grep -rn 'E0167\|unresolved generic instantiation' cryoc/src/

# Spot stage-2-vs-stage-3 differences in a function's IR
grep -A20 'C\$.*string-3new\$F' cryoc/build/cryoc.ll cryoc/build/obj/*.ll
```

---

## 11. Don'ts

- Don't push `--force` to main.
- Don't delete the bootstrap binary without a working `make compiler` path.
- Don't strip the existing `[BCTOR-*]` / `[PARSE-DBG]` / `[VT-DBG]` / `[TypeDecl]` printfs.
- Don't try to fix bugs by adding "safe defaults" — emit a real diagnostic and bail.
- Don't relocate code to a non-natural file just because the bootstrap is sensitive.
- Don't drop the `--build-dir=...` separation between stage-2 and stage-3 stdlib outputs — without it, IR diffs are useless.
- **Don't add probes one or two lines at a time.** Each cycle is 3 minutes; batch a comprehensive set per build.

Good luck.
