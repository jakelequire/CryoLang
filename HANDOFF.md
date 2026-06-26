# HANDOFF — libclang C-import engine: **Phase 2** (emit records/enums/typedefs)

**You are picking up the libclang migration after Phases 0 + 1 shipped.** The hand-rolled C-header
parser is gone; an honest libclang-driven engine (`Compiler::Bindgen`) now drives every
`extern module name := "C" { #include ... }`. It currently emits **function prototypes only** — your job
(**Phase 2**) is to make it emit **structs, unions, enums, and typedefs as real Cryo declarations** so the
inline `#include` path stops silently dropping them.

> Read first, in order: this file → the memory entry
> `improvement_md_audit_item1_const_2026_06_25.md` (the full blow-by-blow of Phases 0/1 + Windows/CI) →
> `IMPROVEMENT.md` §6 → `PLAN.md` (the vendor/binding-generator plan this feeds). Then the source
> landmarks in §3.

---

## 0. State of the tree you are inheriting (everything below is COMMITTED)

`HEAD = 82bc6609` ("Refactor CHeaderImport pass to use Compiler::Bindgen::Importer"). Working tree is
clean except minor `IMPROVEMENT.md` / `PLAN.md` doc edits. **Both pins were refreshed** (Linux
`bin/cryo.pin.txt` + Windows `bin/cryo.exe.pin.txt`). Jake commits and repins, not you — but he has
authorized you to RUN `make pin*` to produce the artifacts for him to commit.

What shipped in Phases 0/1 (+ the Windows/CI/testing follow-up):

- **New subsystem `compiler/src/compiler/bindgen/`** (namespace `Compiler::Bindgen`, seeded by
  `public module Bindgen;` in `compiler/_module.cryo`):
  - `clang.cryo` (`::Clang`) — the libclang FFI surface. Hand-written `extern "C"` declarations (NOT via
    `#include`, to avoid bootstrap recursion): the by-value structs `CXString` (16B), `CXCursor` (32B),
    `CXType` (24B), `CXUnsavedFile`; ~20 `clang_*` externs; `CXTypeKind`/`CXCursorKind` constants;
    `intern_spelling` / `intern_arg_name` helpers.
  - `type_map.cryo` (`::TypeMap`) — `map_type(CXType, ctx) -> TypeAnnotation*` over the **canonical**
    type. Today: scalars→primitive, `T*`→const-aware pointer (§1), enum→`i32`, **record→`void*`**,
    **fn-pointer pointee→`void*`**. `make_prim` / `make_ptr` helpers live here.
  - `importer.cryo` (`::Importer`) — the pass body. `Importer::run(ctx)` walks CImport `ExternBlockNode`s;
    `collect_from_tu` builds a TU (`clang_parseTranslationUnit`, `-x c`) and `clang_visitChildren`-walks
    it; the free function **`visit_decl`** is the bare C callback that emits one `FunctionDeclNode` per
    top-level `FunctionDecl`. Local (real file) + system (`#include <h>` via in-memory `CXUnsavedFile`)
    header paths. Path helpers ported from the deleted scanner.
- **`passes/c_header_import.cryo` is DELETED.** `pass_registry.cryo` dispatches
  `PassID::CHeaderImport => Importer::run(ctx)` (the `PassID` scheduler identity was kept on purpose —
  renaming it is pure churn). Phase 4 of the original plan ("delete the hand-rolled path") is therefore
  **already done**.
- **`compiler/llvm_bindings.h` gained `#include <stddef.h>` + `<stdint.h>`** — REQUIRED now that libclang
  semantically *parses* it (see §2, the size_t bug). `compiler/cryoconfig` links libclang
  (`[link.unix] static = [".../libclang-20.so.1"]`, `[link.windows] system = ["LLVM-C","clang"]`).
- **Win64 by-value-struct ABI** in `codegen/abi.cryo` was found to be **already implemented**
  (`classify_win64_param` / `classify_win64_return`); only stale doc comments were fixed.
- **CI / install / Windows toolchain** wired: `scripts/ci/install-llvm.sh` (libclang1-N + cache),
  `scripts/fetch-windows-llvm.sh` (provisions Windows `libclang.dll` + import lib + clang resource
  headers), `Makefile` / `selfhost-check.py` / `build-release.sh` (gating + DLL staging +
  `CRYO_CLANG_RESOURCE_DIR`).
- **Test:** `tests/tests/lang/c_import_libclang.cryo` (+ `tests/helpers/bindgen_probe.h` + fn in
  `tests/helpers/abi_helpers.c`) — imports a local header via `#include` and CALLS the imported fn.

**Gate state at handoff:** `make test` = **1335 unit / 113 compile-fail, 0 fail**. Linux self-host is a
byte-identical **fixed point** (IR md5 `35d489a9461215336428086e41926df3`). Windows: the libclang engine
is **functionally validated under wine** (links, loads, parses, runs, by-value ABI all proven; win-stage-2
compiled all of stdlib under wine) — but the full 6-stage **wine self-host is environment-blocked in the
codespace** (building 152 compiler modules under wine hits a ~2-minute RAM/stability wall; **not** a
libclang bug). Confirm Windows full self-host on CI / a native host, not here.

---

## 1. Phase 2 — the goal

Today `visit_decl` ignores every cursor that isn't `CXCursor_FunctionDecl`, and `TypeMap::map_type`
collapses records and function pointers to `void*`. That means `extern module x := "C" { #include "h" }`
silently drops every `struct` / `union` / `enum` / `typedef` in `h`, and a function taking
`struct Foo` by value or a `void (*cb)(int)` parameter loses its real type. **Phase 2 makes them real.**

Concretely, emit into the `ExternBlockNode` (or the program — see §3 for where Cryo puts these):

1. **`TypedefDecl`** → a Cryo type alias to the mapped underlying type
   (`clang_getTypedefDeclUnderlyingType`). The common `typedef struct Foo Foo;` and
   `typedef void *Handle;` shapes must round-trip.
2. **`StructDecl` / `UnionDecl`** → a Cryo `struct` with each field mapped via the cursor type mapper
   (walk field cursors; `clang_getCursorType` per field). Unions need a representation decision (Cryo may
   not have native unions — check; a `![repr(c)]`-sized opaque or a tagged representation may be needed).
   Anonymous and nested records need a naming/`![repr(c)]` story.
3. **`EnumDecl`** → emit the constants (`clang_getEnumConstantDeclValue` per child) as `i32`/typed
   constants (today enums already map to `i32` at the *type* level; Phase 2 adds the *constants*).
4. **Type mapper upgrades** in `type_map.cryo`:
   - record/typedef references → the **named** Cryo type you emit (not `void*`).
   - function-pointer types → the real Cryo `(Args) -> Ret` form (§3 already supports this syntax;
     Phase 1 deliberately kept fn-ptr→`void*` for parity — now emit the real type).
   - fixed arrays (`clang_getArrayElementType` / `clang_getArraySize`) → Cryo fixed-array fields.
   - `const` already handled (§1 `const T*`); keep it.

**Order matters:** a struct may reference a typedef declared later; libclang visits in source order, but
the cursor mapper should resolve references to *named* types you've registered, and forward references
need handling (emit the name, let name-resolution bind it; or two-pass: collect all record/typedef names
first, then emit bodies).

### ⚠️ Phase 2's load-bearing bootstrap risk — the compiler's OWN `llvm` import
`compiler/src/compiler/codegen/_module.cryo` does `extern module llvm := "C" { #include "...llvm_bindings.h" }`.
`llvm_bindings.h` contains ~20 typedefs (`LLVMModuleRef` = `void*`, etc.), 7 anonymous enums
(`LLVMIntEQ = 32`, …), and **no struct bodies**. Phase 1 keeps that import **functions-only** (records /
typedefs map to `void*`/`i32` and the enum *constants* are not imported — codegen defines its own
constants). The moment Phase 2 starts emitting typedefs + enum constants, the `llvm::*` namespace gains
`LLVMModuleRef`, `LLVMIntEQ`, … which **can collide** with how codegen already refers to those, or change
resolution, and **break the self-host fixed point or the build**.

**Therefore: gate type emission.** Land + prove records/enums/typedefs on a *test* header first, with
the compiler's own `llvm` block still effectively prototype-only, and only enable type emission for it
once self-host stays byte-identical with types included. Options: a per-extern-block opt-out, a
"functions-only" flag the `llvm` import sets, or proving the emitted `llvm` types are inert
(unreferenced ⇒ dropped). **Re-run the standalone parity harness (§2) against `llvm_bindings.h` after
every change** to see exactly what the new emission produces before you rebuild the compiler.

---

## 2. The two facts that will bite you (learned the hard way in Phase 1)

1. **libclang needs SEMANTICALLY-COMPLETE headers.** The old scanner only *preprocessed* (`clang -E -P`)
   and mapped type *spellings* (`"size_t"` → `u64`). libclang *parses*, so an undeclared type name
   defaults to implicit-`int` → `i32`. `llvm_bindings.h` used `size_t`/`uint64_t` without including
   `<stddef.h>`/`<stdint.h>` → libclang silently mapped them to `i32`, a 64→32-bit ABI truncation in the
   DIBuilder calls. Fix was the two `#include`s (verified parity-neutral for the old scanner). **Any C
   header you parse must include the headers that declare the types it uses** — and for Phase 2 you'll be
   resolving *more* types, so this matters more.
2. **De-risk with a standalone parity harness BEFORE touching the compiler.** The size_t bug was caught
   by a tiny `.cryo` that links libclang and parses `llvm_bindings.h`, printing each mapped signature +
   a tally of fallback types. Reproduce that pattern for Phase 2: parse a header, print each emitted
   record/enum/typedef as you'd lower it, and eyeball it before you rebuild the self-hosting compiler.
   (Phase-0/1 harnesses were left under `scratch/` — `libclang-parity/`, `cimport-local/`,
   `cimport-sys/` — if still present; if not, they're ~80-line standalone `.cryo` files, trivial to
   recreate. Link with `[link.unix] static = ["/usr/lib/llvm-20/lib/libclang-20.so.1"]`.)

---

## 3. Source landmarks

- **Engine:** `compiler/src/compiler/bindgen/{clang,type_map,importer}.cryo` (+ `_module.cryo`). This is
  where ~all your Phase 2 work lands. Add the cursor kinds you need (`StructDecl=2, UnionDecl=3,
  EnumDecl=5, FieldDecl=6, EnumConstantDecl=7, TypedefDecl=…`) + the libclang externs
  (`clang_getTypedefDeclUnderlyingType`, `clang_getEnumConstantDeclValue`,
  `clang_Type_getSizeOf`/`getAlignOf`/`getOffsetOf`, `clang_getArrayElementType`/`getArraySize`,
  `clang_getNumArgTypes`/`getArgType`/`getResultType` for fn-ptrs, `clang_getCanonicalType`,
  `clang_getPointeeType`) to `clang.cryo`.
- **AST you must emit into:** `compiler/src/compiler/AST/declaration.cryo` — `ExternBlockNode`
  (`add_function`, `is_c_import`, include arrays, `namespace_alias`). Find how Cryo represents
  `type struct` / `type enum` / `type alias` declaration nodes and whether an extern block can hold them,
  or whether they must be injected as top-level program statements. The parser
  (`compiler/src/compiler/parser/`) is the reference for how those nodes are built from real Cryo source —
  mirror that node shape.
- **Type annotations:** `TypeAnnotation` / `PrimitiveAnnotation` / `PointerAnnotation` (used by
  `type_map.cryo`). For Phase 2 you'll also emit *named* type references and `(Args)->Ret` function-type
  annotations — find the annotation variants the parser produces for those.
- **Downstream registration:** `compiler/src/compiler/passes/type_resolution.cryo` (~2150–2270) registers
  extern functions into `decl_index` and runs the §5 `W0011` collision logic. New emitted *types* must be
  registered so name-resolution/type-resolution see them — trace how a normal `type struct` reaches
  `decl_index` / the type arena and make the emitted ones follow the same route.
- **Type interning / layout:** `compiler/src/compiler/types/{compound,arena,checker}.cryo`. `const T*` is
  a frontend-only flag (§1), same LLVM type as `T*`.
- **Bootstrap site:** `compiler/src/compiler/codegen/_module.cryo` — the `extern module llvm := "C"` block
  (the thing that makes building the compiler depend on libclang + drives the §1 risk above).

---

## 4. Phase 3 (next, after Phase 2) — layout self-verification

Once records carry real fields, emit `static_assert(sizeof(T) == N)` / `alignof` from
`clang_Type_getSizeOf` / `getAlignOf` so a generated binding fails to compile if Cryo's computed layout
disagrees with C's. `static_assert` already exists (IMPROVEMENT.md §4, folded in TypeLowering). This is a
small, high-value add that makes the bindings self-checking.

---

## 5. Build / test / self-host / repin — commands & GOTCHAS

Self-hosted compiler; builds are slow. **libclang (`libclang1-20` / `/usr/lib/llvm-20/lib/libclang-20.so.1`)
is now a hard build dependency.**

```bash
make cryo                      # build compiler (~3 min) -> compiler/build/cryo
make test                      # unit + compile-fail. Baseline 1335u / 113cf, 0 fail
python3 scripts/selfhost-check.py --no-windows   # Linux-only fixed-point gate (~4 min)
make pin-linux-impl            # Linux repin only (Jake commits)

# ad-hoc compile with the fresh binary (needs a cryoconfig with [project] entry_point):
export CRYO_STDLIB=$PWD/stdlib && $PWD/compiler/build/cryo build
```

- **Use `--no-windows` for the self-host gate.** The full `make selfhost-check` also runs the Windows
  wine 6-stage, which is **environment-blocked here** (RAM wall building the compiler under wine — see §0).
  It is NOT a code failure; don't chase it in the codespace. Windows full self-host belongs on CI / a
  native host. The Windows *cross-build* (`make cryo-exe`) and *runtime under wine* DO work and are worth
  a smoke test (`wine compiler/build/cryo.exe --version` after staging `LLVM-C.dll` + `libclang.dll` next
  to it).
- **NEVER edit compiler source while a self-host check runs** — it builds stage-3 then stage-4 from the
  tree; a mid-run edit makes them differ → a *false* "FIXED POINT BROKEN".
- **Background-task completion reports the WRAPPER exit, not `make`'s** — read the `... EXIT:` line your
  wrapper echoes into the log, not the harness's "exit 0".
- **No surface-syntax change** is needed for Phase 2 (you're emitting AST, not adding grammar), so no
  two-phase repin for the engine itself. But the bootstrap two-step still applies if emitted `llvm` types
  change what the pinned compiler must understand — prove self-host holds (§1 gate).
- Keep `docs/cryo.md` / `docs/grammar.md` in sync with shipped behavior, same change.
- Test-file gotcha: the suite compiles all test files together — don't name a test struct with a stdlib
  leaf name (e.g. `Pair`); it shadows the real type and breaks unrelated tests.

## 6. Project conventions
- `legacy/bootstrap` and `experimental/stdlib-next` are **dead code** — don't audit/fix.
- Canonical paths: `docs/cryo.md`, `stdlib/lib.cryo`, `compiler/src/compiler/instance.cryo`,
  `scripts/selfhost-check.py`.
- Owner (Jake) is self-taught, ~4y, on a capstone — wants **honest signal**, the right long-term call over
  a quick green, and **no hacky workarounds just to pass**. When something is environment-limited (like the
  wine self-host), say so plainly rather than forcing a green.
