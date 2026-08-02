# ABI Lowering - As-Built

This document describes how Cryo lowers function signatures and call sites
to LLVM IR, as actually implemented in the compiler.

Target scope today: **SysV x86-64 and Win64 (Microsoft x64)**, both shipping.
`AbiClassifier::kind_for_triple` picks between them from the target triple, and
`classify_param` / `classify_return` dispatch to the matching rules; nothing in
the rest of codegen knows what platform it is generating for. Sections 1-N below
describe the SysV path, which is the more elaborate of the two; section "Win64"
covers where Microsoft x64 differs. Further targets (e.g. AArch64 AAPCS) plug in
as additional `AbiKind` branches behind the same seam.

## 1. Seam architecture

The single source of truth for "how does this parameter / return value
cross the function-call boundary" is `AbiClassifier`, defined in
`compiler/src/compiler/codegen/abi.cryo`.

```
            +-----------------------------------------+
            |  CodegenContext::abi  (AbiClassifier)   |
            |   - type_mapper: TypeMapper*            |
            |   - sret_kind, byval_kind (cached IDs)  |
            +---------------+-------------------------+
                            | back-pointer
       +--------------------+--------------------+----------------+
       v                    v                    v                v
DeclarationEmitter      ExprOps          SymbolResolver    CallEmitter
(declare_function,   (codegen_call,   (build_function_   (post-call
 declare_method,      codegen_return,   ltype for cross-   coercion at
 prologues)           coercion paths)   module externs)    call sites)
```

Every site that needs to know "how is this value laid out at the LLVM
boundary?" routes through `AbiClassifier`. Codegen never inspects Cryo
types directly to make that decision.

### Plan types

`ParamPlan` describes a single parameter or return value:

```cryo
type struct ParamPlan {
    cryo_type:  TypeRef;     // original source type
    cls:        ArgClass;    // Direct | DirectPair | Indirect | Ignore
    llvm_slots: LType[];     // 0, 1, or 2 LLVM-level slots
    attr:       AbiAttr;     // None | ByVal | SRet
    pointee_ty: LType;       // for Indirect: the pointee type
                             // for Direct-with-coercion: the source struct
}
```

`SignaturePlan` collects the return plan and per-parameter plans plus
the assembled LLVM function type:

```cryo
type struct SignaturePlan {
    return_plan:  ParamPlan;
    param_plans:  ParamPlan[];
    llvm_fn_type: LType;
    sret_slot:    boolean;  // true => LLVM param 0 is the result pointer
    is_variadic:  boolean;
}
```

## 2. Classification rules (SysV x86-64)

Driven by the aggregate's `size_bytes()` after unwrapping `TypeAlias`
and `InstantiatedType` to the concrete kind. Aggregate kinds are
`Struct`, `Class`, `Enum`, `Tuple`, and `Optional`. Primitive scalars
(`Int`, `Float`, `Pointer`, `Reference`, ...) and `Array`/`String`/etc.
are always Direct.

### Returns

| Source kind         | Size      | Plan                                                                          |
|---------------------|-----------|-------------------------------------------------------------------------------|
| `void` / `Unit`     | -         | `Ignore` (LLVM `void`, no slot)                                               |
| Scalar / primitive  | -         | `Direct` (one LLVM slot = source type)                                        |
| Aggregate           | 0 / inv.  | falls back to `Direct` (whatever `map_type` says)                             |
| Aggregate           | 1-8       | `Direct` with coercion - one register-sized slot (slot type per the eightbyte rules below) |
| Aggregate           | 9-16      | `DirectPair` - two register-sized slots, wrapped in `{lo, hi}` literal struct |
| Aggregate           | > 16      | `Indirect` with `SRet` - hidden first `ptr sret(%T)` parameter, LLVM returns `void` |

### Parameters

The parameter classifier is unified: `classify_param` applies the same
rules for **both** Cryo-internal and `extern "C"` callees, so the table
below is the single rule for parameters.
`classify_param_extern_c` / `classify_return_extern_c` /
`classify_signature_extern_c` survive only as thin aliases that delegate
to the unified classifiers.

| Source kind         | Size      | Plan                                                                          |
|---------------------|-----------|-------------------------------------------------------------------------------|
| Scalar / primitive  | -         | `Direct` (one LLVM slot = source type)                                        |
| Aggregate           | 1-8       | `Direct` with coercion - one register-sized slot (slot type per the eightbyte rules below) |
| Aggregate           | 9-16      | `DirectPair` - two register-sized slots                                       |
| Aggregate           | > 16      | `Indirect` with `ByVal` - single `ptr byval(%T)` slot                         |

The prologue (`codegen_function_prologue` and `codegen_method_prologue`)
and `declare_method`'s LLVM slot assembly consume these plans
symmetrically: they query `classify_param` per source param, walk LLVM
slot indices by `plan.llvm_slots.length` (so a DirectPair param advances
the index by two), and reconstruct the source struct from `(lo, hi)`
slots via store-at-+0 / store-at-+8.

Because a source parameter can now contribute more than one LLVM slot,
per-parameter attribute attachment (`byval(%T)`) computes the parameter's
starting LLVM slot via `llvm_slot_index_of_param` (which accounts for the
hidden sret slot and earlier DirectPair expansions) rather than the raw
source ordinal - attaching at the ordinal would land the attribute on an
incompatible slot once any earlier parameter is a DirectPair.

The call site recovers a coerced aggregate's size from the **lowered LLVM
type** (`agg_register_size`), not the Cryo `size_bytes()`. This is
load-bearing: an argument can carry an unresolved `InstantiatedType`
whose `size_bytes()` is 0 even though `map_type` lowers it to a correct
16-byte struct, and a `string` literal can be implicitly coerced to a
`Str` parameter as a bare `i8*`. Reading the size off the value/lowered
type (with a slot-budget fallback when the source carries no struct size)
keeps the call site's slot count in agreement with the callee.

## 2a. Classification rules (Win64 / Microsoft x64)

Win64 aggregate classification is markedly simpler than SysV, and lives in
`classify_win64_param` / `classify_win64_return`. For an aggregate of size `sz`:

| `sz` | Class | Lowering |
|---|---|---|
| 1, 2, 4, 8 | Direct | coerced to an integer of that exact size (`i8`/`i16`/`i32`/`i64`), passed in one GP register |
| 3, 5, 6, 7, > 8 | Indirect | caller spills to its own stack and passes a pointer — `byval` for params, `sret` for returns (hidden pointer in RCX) |
| 0 | Direct | falls through to the plain-Direct path, identical to SysV |

Two consequences worth internalising, because they are where SysV intuition
misleads:

- **There is no SSE, HFA, or DirectPair branch.** Win64 passes *every* aggregate
  in GP registers, float-bearing or not — an 8-byte `struct { double x; }` rides
  through RAX, not XMM0.
- **Size alone decides.** Field types never affect the classification, so the
  eightbyte machinery in section 3 is SysV-only.

Reference: [x64 calling convention — Parameter Passing](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).

## 3. Eightbyte slot classification

For each <= 8 byte half of an aggregate ("eightbyte bucket"),
`AbiClassifier::eightbyte_slot_type` picks the LLVM type that occupies
the slot:

1. Walk the struct/class fields whose offset falls in `[start, end)`.
2. **One float-class field that fully covers the bucket**: emit `double`
   (for F64) or `float` (for F32) - SSE class.
3. **Two F32 fields packing into one 8-byte eightbyte**: emit
   `<2 x float>` via `LLVMVectorType` - multi-float SSE.
4. **Anything else** (mixed int/ptr, multiple non-float fields, F32+F64
   sharing a bucket, ...): emit the smallest power-of-two integer
   container >= `bucket_size` (`i8` / `i16` / `i32` / `i64`) - INTEGER
   class.

This matches what clang emits for the same source layouts. The
multi-float SSE branch in particular is required for C interop with
functions returning `{float, float}` - the SysV ABI rides those in a
single XMM register packed as `<2 x float>`, not in two scalar slots
or `i64`.

## 4. Attribute attachment

`sret(%T)` and `byval(%T)` are type-carrying attributes since LLVM 15
made opaque pointers mandatory. `AbiClassifier` caches the named-attribute
kind IDs (`LLVMGetEnumAttributeKindForName`) once per process and exposes:

- `apply_sret_attribute(fn_val, pointee_ty)` - attach to LLVM param slot 0
- `apply_sret_call_attribute(call_val, pointee_ty)` - call-site mirror
- `apply_byval_attribute(fn_val, llvm_idx, pointee_ty)` - attach to a param slot
- `apply_byval_call_attribute(call_val, llvm_idx, pointee_ty)` - call-site mirror
- `apply_call_site_attrs_from_plan(call_val, plan*)` - bulk apply from a `SignaturePlan`, for function-pointer call sites where attribute queries don't work

Function-side and call-site attributes are **independent in LLVM** and
must both be set for the verifier and optimizer to treat the slot
correctly. Direct calls to named functions use the attribute-query
path (`sret_pointee_of`, `byval_pointee_of_param`, both gated by
`LLVMIsAFunction`); function-pointer calls use the plan-driven
`apply_call_site_attrs_from_plan` helper.

## 5. Coercion semantics

When the LLVM-level call/return signature uses a different shape than
the Cryo source-level type, the two sides need explicit reshape:

### Post-call coercion (caller)

`call_emitter.cryo` after `codegen_call`. When the call result is a
literal struct (DirectPair shape) or a register-shaped scalar/vector
and the AST node's resolved type is a struct, the result is spilled to
a stack temp typed as the *call result* and reloaded at the source
struct's LLVM type. Both shapes describe the same source-level value
byte-for-byte, so the round-trip is correct.

Detection uses `LTypeKind` to dispatch into shape-aware branches and
raw `void* != void*` to catch named-vs-literal struct identity
mismatches in the equal-kinds case.

### Arg-pass coercion (caller, per arg)

`codegen_call_direct` in `expr_ops.cryo`. For each call argument, if
the LLVM-actual and LLVM-expected types disagree:

- `Integer | Float | Double | Vector` expected, `Struct` actual:
  <= 8 byte aggregate param - spill struct, reload as scalar/vector.
- `Integer | Float | Double | Vector` expected, `Pointer` actual where
  the arg is an <= 8 byte aggregate by *address* (an lvalue, or a `T*` /
  `&T` to the aggregate, detected via `agg_register_size`): load the
  register through the pointer. This takes priority over the generic
  `Integer <- Pointer` `ptrtoint` below - `ptrtoint`-ing the address
  would pass the pointer where the callee expects the aggregate's bytes
  (e.g. an 8-byte `LBuilder` reached through `this.builder: LBuilder*`).
- `Struct` expected, `Integer | Float | Double | Vector` actual:
  the arg came from a register-shaped aggregate return; reshape into
  the named struct.
- `Struct` actual+expected, actual is literal: the arg came from a
  DirectPair return whose `{lo, hi}` shape doesn't match the receiving
  param's named struct; reshape.
- Various `Integer <-> Pointer`, `Struct <-> Pointer` cases for receiver /
  reference plumbing (pre-existing, not ABI-driven).

DirectPair param expansion (a single source aggregate expanding into two
LLVM register slots) is handled by `codegen_call_direct_dp_expand`,
dispatched from `codegen_call_direct` when `expected_count > n`. It
handles all three argument shapes - struct value (spill to temp), lvalue
address, and pointer/reference to the aggregate - loading `lo` at +0 and
`hi` at +8, with a slot-budget fallback for sources that carry no struct
size (e.g. a `string` literal coerced to a `Str` parameter).

### Defining-side coercion (callee)

`codegen_return` in `expr_ops.cryo`. When the function's LLVM return
type is register-shaped (scalar/vector for <= 8 byte returns, literal
`{lo, hi}` struct for DirectPair returns) but the source return value
is a struct, spill the struct and reload as the declared return
type before `ret`. Inverse of the post-call coercion.

`codegen_return` runs before the void-fallback so sret-returning
functions take their dedicated branch first (`store value through
sret slot; ret void`).

## 6. va_list

`va_list` is per-ABI, behind two `AbiClassifier` seams.

`va_list_alloca_type()` gives the storage the variadic prologue allocates:

| Target | Alloca type | What it holds |
|---|---|---|
| SysV x86-64 | `[24 x i8]` | `__va_list_tag[1]` — gp_offset, fp_offset, overflow_arg_area*, reg_save_area* |
| Win64 | `i8*` | a single `char*` pointing at the next variadic argument |

Both sizes are exactly what `@llvm.va_start.p0` expects to initialize, so the
backend writes the right number of bytes per target — no over-allocation.

`forward_va_list()` handles passing a `va_list` **by value** to a C consumer
(`vsnprintf`, `vfprintf`, …). The two ABIs differ in a way that is easy to get
wrong:

- **SysV**: `va_list` is `__va_list_tag[1]`, which decays to `__va_list_tag*`.
  Forwarding the alloca pointer directly is correct.
- **Win64**: `va_list` is `char*`. The alloca *holds* that pointer, so it must be
  **loaded** before forwarding. Passing the alloca address would hand the callee
  a `char**` and crash on the first variadic read.

`@llvm.va_start` / `va_end` always take the alloca address itself and so bypass
this seam on both targets.

## 7. Adding a further target

`AbiKind` (`SysVAmd64`, `Win64`) is selected per-process by
`AbiClassifier::kind_for_triple(triple)`, and every ABI-shaped decision already
routes through a `match` on it. Adding a third target (most likely AArch64
AAPCS) means extending those matches, not restructuring the seam:

1. Add the `AbiKind` variant and its `kind_for_triple` branch.
2. Add `classify_<target>_param` / `classify_<target>_return`, and dispatch to
   them from `classify_param` / `classify_return`.
3. Add the target's arms to `va_list_alloca_type()` and `forward_va_list()`.
4. For AArch64 specifically, `eightbyte_slot_type` needs HFA/HVA rules —
   homogeneous floating-point aggregates pass in vector registers, and larger
   ones spill differently than on x86-64.
5. Re-pin the compiler on the new target before changing default behavior.

Nothing in `DeclarationEmitter`, `ExprOps`, `SymbolResolver`, or the
visit-side emitters needs to know about the target - they all already
go through `this.abi.X` for every ABI-shaped decision.
