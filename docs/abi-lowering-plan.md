# ABI Lowering — Design & Implementation Plan

Status: **design only, not yet implemented.** Written 2026-05-14 alongside the
codegen signedness fix and the `Option`/`Result` `![sink]` fix. This is the
scoped plan for the one critical-audit item too large to land in a single
session.

---

## 1. The problem

The codegen path declares every Cryo function with the **direct** LLVM type of
each parameter and the return value. For a struct-by-value parameter that means
the LLVM signature literally contains `%MyStruct` as the parameter type, and a
struct return literally returns `%MyStruct`.

See `compiler/src/compiler/codegen/decl_codegen.cryo`:
- `declare_function` (~line 317): `slots[i] = param_ty.raw` where `param_ty`
  comes straight from `cg.type_mapper.map_type(param.resolved_type)` — so a
  struct parameter's slot is the struct type itself.
- `declare_method` (~line 829): same shape for the implicit receiver + params.
- `map_return_type`: returns the mapped type directly.

There is **no System V x86-64 ABI classification layer**:

- No `sret` — a large struct return is not lowered to a hidden pointer
  parameter.
- No `byval` — a large struct argument is not lowered to a pointer-with-attribute.
- No eightbyte INTEGER/SSE classification — a small struct is not split into
  the register pair the ABI requires (e.g. `{i64, i64}` passed in two GPRs, or
  `{double, double}` in two XMMs).
- `va_list` is hard-coded as `[24 x i8]` (`intrinsics_codegen.cryo` ~line 941,
  `decl_codegen.cryo` ~line 159). That layout is SysV-x86-64-specific; it is
  wrong for AArch64 (a 32-byte struct with different fields) and for Win64
  (`va_list` is `char*`).
- The `format()` runtime calls `vasprintf`, which is glibc/BSD-only.

### Why it "works" today

The entire compiled program is **LLVM-internally consistent**: every Cryo
caller and every Cryo callee agree on the same (non-ABI) lowering, so within a
single Cryo program structs-by-value round-trip correctly. The selfhost-check
fixed point holds precisely because the compiler only ever calls itself.

### Why it is still a bug

1. **C interop is broken for struct-by-value.** Any `extern "C"` function that
   takes or returns a struct by value will receive/return it in the wrong
   place. Cryo↔C struct ABI only happens to work for structs that fit the
   "single pointer / single integer" shape by accident.
2. **Cross-compilation is broken.** `--target=TRIPLE` is wired through
   `codegen/passes.cryo`, but the hard-coded SysV `va_list` and `vasprintf`
   dependency mean any non-Linux-x86-64 target produces a binary with a broken
   `format()` and broken variadic forwarding.
3. It is a latent correctness landmine: the moment any pass stops being
   "internally consistent" (e.g. a future incremental-compilation or
   separate-object-file optimization), struct passing silently corrupts.

---

## 2. Scope decision for this plan

Full multi-target ABI is a large, multi-week effort. This plan covers the
**SysV x86-64** ABI only — the host target — done correctly, with a clean
seam so AArch64 / Win64 classifiers can be added later behind the same
interface. Getting one target *correct* and *abstracted* is worth far more
than four targets done by hand-waving.

Explicitly **out of scope** here: AArch64, Win64, the `vasprintf` replacement
(tracked separately — `format()` should be reimplemented over a Cryo-side
integer/float formatter rather than libc `vasprintf`; the heap-free writers in
`stdlib/fmt` already exist and most of the work is routing `format()` through
them).

---

## 3. Design

### 3.1 New module: `compiler/src/compiler/codegen/abi.cryo`

`namespace Compiler::Codegen::Abi;`

The classifier turns a Cryo function signature into an **ABI-lowered LLVM
signature** plus a per-parameter/return **lowering plan** that codegen consults
at every declaration site, call site, prologue, and `return`.

```
type enum ArgClass {
    Direct;              // pass the value as-is in register(s)
    DirectPair(LType, LType);  // struct split into two eightbytes
    Indirect;            // pass a pointer (byval for args, sret for return)
    Ignore;              // zero-sized / unit
}

type struct ParamPlan {
    cryo_type:  TypeRef;
    class:      ArgClass;
    llvm_slots: LType[];  // 0, 1, or 2 LLVM types this param expands to
    attr:       AbiAttr;  // None | ByVal | SRet
}

type struct SignaturePlan {
    return_plan:  ParamPlan;     // class Indirect ⇒ sret hidden first param
    param_plans:  ParamPlan[];
    llvm_fn_type: LType;         // the fully-lowered LLVM function type
    sret_slot:    boolean;       // true ⇒ param 0 is the hidden sret pointer
}
```

The core entry point:

```
function classify_signature(cg: CodegenContext*, params: TypeRef[],
                             return_type: TypeRef, is_variadic: boolean)
    -> SignaturePlan;
```

### 3.2 SysV x86-64 classification rules (the subset we need)

For each parameter / return type, after unwrapping references (a `&T` /
`mut &T` receiver is already a pointer — class `Direct`):

1. **Scalars** (int, float, bool, pointer, enum-as-tag) → `Direct`.
2. **Aggregates > 16 bytes** → `Indirect` (`byval` for a parameter, `sret`
   hidden pointer for a return value).
3. **Aggregates ≤ 16 bytes** → run the eightbyte classifier:
   - Split the struct into two 8-byte chunks.
   - Each chunk is INTEGER unless *every* field overlapping it is a float →
     then SSE.
   - Map INTEGER eightbyte → `i64` (or `i32`/`i8` for a 1-eightbyte struct
     sized < 8), SSE eightbyte → `double`/`float`.
   - One eightbyte → `Direct` with a single `llvm_slot`.
   - Two eightbytes → `DirectPair`.
4. **Unit / zero-sized** → `Ignore` (no LLVM slot at all).

This is the 90% subset. The full spec has more (`__int128`, x87, mixed
SSE/INTEGER edge cases, bitfields) — none of which Cryo's type system can
currently even express, so they are genuinely unreachable.

### 3.3 Integration points

Everything routes through `SignaturePlan`; no codegen site classifies inline.

| Site | File | Change |
|---|---|---|
| Function decl | `decl_codegen.cryo` `declare_function` | Build `SignaturePlan`; emit `plan.llvm_fn_type`; attach `byval`/`sret` attributes to the LLVM function. |
| Method decl | `decl_codegen.cryo` `declare_method` | Same; the receiver is always `Direct` (already a pointer). |
| Return type | `decl_codegen.cryo` `map_return_type` | Replaced by `plan.return_plan`. |
| Prologue | `decl_codegen.cryo` `codegen_function_prologue` | When a param is `Indirect`, the LLVM param *is* the pointer — bind it directly. When `DirectPair`, store the two incoming SSA values into a stack slot and bind that. When `sret`, param 0 is the result pointer. |
| Call site | `expr_codegen.cryo` `codegen_call` | Marshal each argument per its `ParamPlan`: `Indirect` ⇒ pass a pointer to a (copied) stack temp with `byval`; `DirectPair` ⇒ load the two eightbytes; `sret` ⇒ allocate the result slot, pass it as param 0, the call "returns" void. |
| `return` | `stmt_codegen.cryo` `codegen_return` / `coerce_return_value` | When the signature is `sret`, store the value through the hidden pointer and `ret void` instead of `ret %struct`. |

`coerce_return_value` already exists as the single return-coercion choke point
(it was just extended for signedness) — the `sret` branch belongs there.

### 3.4 Caching

`SignaturePlan` is pure function of `(params, return_type, is_variadic,
target_triple)`. Cache it keyed on the `FunctionType` TypeRef id (the type
arena already deduplicates these) so a function called N times classifies
once. This also fixes the existing inefficiency where `declare_function`
re-mangles and re-mallocs on every call.

---

## 4. Phased implementation

Each phase is independently testable and keeps `make test` +
`make selfhost-check` green.

**Phase 0 — seam, no behavior change.**
Add `abi.cryo` with `classify_signature` that classifies *everything* as
`Direct` (i.e. reproduces today's behavior exactly). Route `declare_function`,
`declare_method`, `map_return_type`, `codegen_call`, the prologue, and
`codegen_return` through `SignaturePlan`. Selfhost-check must still produce
byte-identical IR — this proves the seam is wired correctly before any ABI
logic exists.

**Phase 1 — `sret` for large struct returns.**
Implement rule 2 for return values only. This is the highest-value, lowest-risk
ABI case and the easiest to verify (a struct-returning function's LLVM
signature visibly changes to `void @f(ptr sret(%S))`). Add a focused test:
return a 32-byte struct from a function, read every field at the call site.

**Phase 2 — `byval` for large struct parameters.**
Rule 2 for parameters. Test: pass a 32-byte struct by value, mutate a copy
inside the callee, assert the caller's copy is unchanged.

**Phase 3 — eightbyte classification for small structs.**
Rules 3–4. This is the fiddly one; gate it behind a thorough test matrix:
`{i32,i32}`, `{i64,i64}`, `{f64,f64}`, `{i32,f32}`, `{i8,i8,i8}`, `{ptr}`,
`{i64}`, unit. Verify against `clang -S` output for the equivalent C struct
(the C compiler is the oracle for ABI correctness).

**Phase 4 — C-interop test.**
A test that links a tiny C object file (`tests/` already drives `cc`) which
passes and returns structs across the boundary both ways. This is the
end-to-end proof that the classification matches the real C ABI.

**Phase 5 (separate effort) — multi-target.**
Make `classify_signature` dispatch on the target triple; add AArch64 and Win64
classifiers behind the same `SignaturePlan` interface. Replace the hard-coded
`va_list` with a per-target shape. Reimplement `format()` over `stdlib/fmt`'s
heap-free writers to drop the `vasprintf` dependency.

---

## 5. Verification strategy

- **Oracle:** `clang -S -emit-llvm` (or `-S` for asm) on the equivalent C
  struct signatures. The SysV ABI is exactly what clang implements; diffing
  parameter/return lowering against clang's output is the ground truth.
- **Regression:** `make selfhost-check` must stay byte-identical through
  Phase 0, and stay *self-consistent* (still reaches a fixed point) through
  every later phase — the compiler passes plenty of structs by value, so it is
  itself the largest ABI test.
- **New tests:** `tests/tests/lang/abi_structs.cryo` built up phase by phase;
  the C-interop test in Phase 4.

---

## 6. Risks / open questions

- **Method receiver mangling.** `declare_method` derives the receiver type
  independently from the call site (audit finding H4). The ABI seam should
  *also* be the place that finally makes receiver lowering go through one
  function, so the two sides can't drift.
- **`new T { ... }` partial init** (audit H10) interacts with `byval`: a
  `byval` copy of a partially-initialized struct copies heap garbage. Worth
  fixing zero-init at the same time.
- **The type cache `LLVMTypeRef` sharing** (audit H2) means the lowered
  function types must also be created in the shared LLVM context — already the
  case, but the `SignaturePlan` cache must be reset at the same project
  boundary as `g_shared_type_cache`.
