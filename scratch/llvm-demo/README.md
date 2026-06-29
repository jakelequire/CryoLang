# llvm-demo

Vendors a broad slice of the **LLVM-C API** (LLVM 20) and uses it to *generate
LLVM IR* — the Cryo compiler driving its own backend through the vendor system.
(Cryo self-hosts on LLVM-C via a hand-written `llvm_bindings.h`; this vendors the
full public C API instead.)

```sh
cd scratch/llvm-demo
cryo run
```
```llvm
=== Cryo built this LLVM IR via the vendored LLVM-C API (verify=ok) ===
; ModuleID = 'cryo_demo'
source_filename = "cryo_demo"

define i32 @add(i32 %0, i32 %1) {
entry:
  %sum = add i32 %0, %1
  ret i32 %sum
}
```

`src/main.cryo` builds an `add(i32, i32)` function in memory (module → function
type → basic block → `LLVMBuildAdd` → `LLVMBuildRet`), runs `LLVMVerifyModule`,
and prints the textual IR with `LLVMPrintModuleToString`.

## The generated binding — the FULL LLVM-C API

`bindings/LLVM.cryo` was generated from an umbrella over **every public LLVM-C
header** (all 29: Core, Analysis, Target, TargetMachine, BitReader/Writer,
IRReader, ExecutionEngine, Object, Linker, DebugInfo, Disassembler, Comdat,
Error, Support, Remarks, lto, blake3, Orc, OrcEE, LLJIT, Transforms/PassBuilder,
…):

| | count |
|---|---|
| functions | **1385** |
| of which ORC/JIT | 92 |
| of which DebugInfo (DIBuilder) | 58 |
| structs (mostly opaque `LLVMOpaque*` handles) | 83 |
| enums | 41 |
| consts | 53 |

Translation report: **0 not bound, 3 approximated** — the only approximations are
three `LLVMDIFlags` enum constants that duplicate sibling values (diverted to
alias consts, since a Cryo `type enum` needs unique discriminants). LLVM-C is
almost entirely opaque-pointer handles (`LLVMValueRef = LLVMOpaqueValue*`, etc.)
plus enums and functions, so even the callback-heavy ORC/JIT headers map cleanly.

## Notes / findings

- **Two header trees, two versioned dirs.** The `llvm-c/` API lives under
  `/usr/include/llvm-c-20/`, but `Target.h` pulls in `llvm/Config/llvm-config.h`
  and `Targets.def` from a *different* tree, `/usr/include/llvm-20/`. Both had to
  be copied locally to parse the full umbrella (system-header filter again).
- **Honesty gap noticed:** with `llvm/Config` missing, libclang *soft-fails* the
  unfound include and keeps parsing — the generator still emitted a binding, but
  with ~130 fewer functions and **still reported "0 not bound"**. A missing
  transitive include silently shrinks the binding; the report doesn't reflect
  libclang parse errors. Worth a follow-up (surface libclang diagnostics →
  "binding may be incomplete").
- `-lLLVM-20` (in `[link]`) links the whole LLVM shared library.
- The friendly `LLVMModuleRef`/`LLVMValueRef`/… typedef aliases the generator
  emitted are used directly in `main.cryo`.

As with the other demos, the `.cryo` binding is checked in only so you can read
it; the real workflow generates it into the cache and resolves `import
vendor::LLVM`.
