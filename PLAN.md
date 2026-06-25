# Cryo Vendor Library System & Automatic C Binding Generation — Plan

## Context

Cryo today can interop with C two ways: hand-written `extern "C"` blocks, and an aliased
`c := extern "C" { #include <stdio.h> }` import that shells out to `clang -E -P` and feeds the
preprocessed text to a **hand-written function-prototype parser**. That parser only recognizes
function signatures — it **silently skips every struct, union, enum, and typedef**
(`c_header_import.cryo:334-339`). That is fatal for real libraries: raylib's API is built on
`Vector2`, `Color`, `Rectangle`, etc., so the current path cannot bind it at all.

The goal is a **vendor library system**: `cryo vendor ./raylib` registers a C project once; then
`import vendor::RayLib` makes `RayLib::InitWindow(...)`, `RayLib::Vector2`, etc. usable anywhere,
with bindings generated automatically — no hand-written bindings by users or maintainers. Binding
generation uses **libclang** (Clang's stable AST C API), not Cryo's primitive parser. The built-in
parser stays for the lightweight inline `#include` path; it is not the foundation for vendoring.

The investigation below confirms this is **mostly an assembly job over existing machinery**: the
FFI can already call libclang with no compiler changes, the resolver/CLI/linker/triple/LSP seams
all exist, and generated bindings can be plain `.cryo` source that flows through the unmodified
parser → sema → codegen → LSP pipeline. The one genuinely new engine is the libclang→Cryo AST
walker.

**Settled design decisions (from review):**
- Registry is **global + project-local pins** (project pin wins → global → error).
- Binding generation runs **eagerly at `cryo vendor` time** (errors surface at registration; imports
  are fast and offline).
- One-per-machine bindings, but the **cache is keyed on the target triple** so multiple triples
  coexist and cross-compilation regenerates lazily on a cache miss.

---

## 1. Investigation Report (findings vs. brief §4)

All file paths are real and verified by five parallel investigations + one design probe.

### FFI & C-header path
- **Aliased import end-to-end:** parser recognizes `name := extern` at
  `parser.cryo:289-303`; `parse_extern_block_cimport` (`parser.cryo:2002-2077`) records
  `#include` paths into `ExternBlockNode` (`AST/declaration.cryo:964-1015`). The
  **`CHeaderImport` pass** (`passes/c_header_import.cryo`) runs after ASTValidation, before
  NameResolution (`pass_registry.cryo:295-320`); it picks a preprocessor (`$CRYO_CC` →
  clang-20/clang/gcc/cc, lines 183-194), runs `cc -E -P` via `popen` (line 260), and a
  hand-written `parse_c_functions`/`try_parse_c_func` (lines 320-529) extracts **function
  prototypes only**, mapping C types via `c_type_to_annotation` (lines 554-630).
- **Built-in parser coverage:** ✅ functions, variadics, multi-level pointers, fixed-width ints,
  qualifiers, unnamed params. ❌ **structs, unions, enums (definitions), typedefs, function-pointer
  params (→ `void*`), bitfields, anonymous unions** — all skipped. Also a 500-char decl cutoff and
  no const preservation. **This bounds "primitive" and confirms libclang is mandatory for vendoring.**
- **Namespace representation:** the alias becomes a `Namespace` symbol with a child scope
  (`name_resolution.cryo:145-181`); `c::printf` is a `ScopeResolutionNode`
  (`AST/expression.cryo:667-693`); `type_resolution.cryo:2221-2231` registers `c::printf` and maps
  it to the **bare** link symbol `printf` via `register_mangled_name`. **Reusable — but see §2 for
  why the vendor path uses the module-namespace mechanism instead of the alias mechanism.**

### Import / module resolution
- `ModuleLoader` (`module_loader.cryo:25-76`) pre-scans all `.cryo` files into a
  `namespace → path` map (`ns_map`), then resolves imports in `resolve_import_path`
  (**lines 281-321**) via map-lookup → filesystem search. **This is the clean insertion point for a
  `vendor::` rule.**

### Mangling & symbols
- `extern "C"` bypasses mangling: `MangledName::extern_c` (`resolver/mangled_name.cryo:195-199`)
  returns the bare source name. `declare_extern_block` (`declaration_emitter.cryo:1263-1325`) emits
  `add_function(bare_name, fn_type)` with full extern-C ABI classification
  (`classify_signature_extern_c`), `![link("...")]` override at line 1302, and sret/byval attrs.

### Backend & linking
- `LinkerConfig` (`codegen/passes.cryo:26-112`) + `ProjectConfig` `[link]` / `[link.unix]` /
  `[link.windows]` arrays (`project_config.cryo:227-243`, parsed at 554-576) thread into the link
  command in `run_linking` (`passes.cryo:855-900`) as `-L` / `-l` / verbatim archives. **This is
  the link-metadata injection path.**
- **Target triple** lives in `CompilationContext.target_triple` (set from `--target` over
  `[project] target`), resolved via `LTargetMachine::resolve_effective_triple`
  (`llvm_types.cryo:1069-1074`). Accessible everywhere binding generation would run.

### FFI capability for libclang — **the linchpin: no compiler changes needed**
- ✅ **By-value structs** (SysV + Win64): `abi.cryo` classifies ≤8B in register, 9-16B DirectPair,
  >16B sret/byval — covers `CXString`/`CXCursor`/`CXType`/`CXSourceLocation` (all ≤16B).
- ✅ **Function-pointer callbacks** as bare fn pointers + `void*` client data — exactly
  `clang_visitChildren`'s shape (closures not needed). ✅ **enums, opaque pointers, variadic, C
  strings.** Stdlib precedent: `ffi/libc.cryo`, `ffi/openssl.cryo`.

### LSP
- LSP at `tools/CryoLSP/src/` runs the **full compiler pipeline per session** and walks the
  `module_graph` ASTs at query time (`handlers/completion.cryo:235-309`). **Anything resolvable
  through the normal import path autocompletes for free** — so generating real `.cryo` modules (not
  injected AST) means `RayLib::` completion needs zero LSP changes.

### CLI
- Subcommands are a `CommandKind` enum + `Executor` dispatch in `CLI/commands.cryo` (enum 105-118,
  register 427-462, dispatch 524-543, resolve 500-514, handlers `cmd_*`). Adding `cryo vendor` is a
  documented 6-step edit. `CRYO_HOME` + XDG cache chain already exists (`deps/cache.cryo:38-60`).

**No assumptions from the brief were contradicted.** The one nuance: the vendor path should NOT
reuse the *aliased* CImport namespace mechanism — it uses the module-namespace mechanism (§2).

---

## 2. Architecture

### Binding representation — generated `.cryo` source (rust-bindgen model)
The libclang generator emits a real `.cryo` file per (library, triple). A normal
`namespace RayLib;` declaration is the **single unifier**: `qualify_symbol_sym`
(`compilation_context.cryo:358-365`) prefixes BOTH types and unaliased-extern functions with
`RayLib::`. Generated file shape:

```cryo
namespace RayLib;

type struct Vector2 { x: f32; y: f32; }
type struct Color   { r: u8; g: u8; b: u8; a: u8; }
type enum ConfigFlags : u32 { FLAG_VSYNC_HINT = 64; /* ... */ }

extern "C" {
    function InitWindow(width: i32, height: i32, title: u8*) -> void;
    function GetColor(hexValue: u32) -> Color;     // by-value struct return — ABI-handled
}
```

- **Why unaliased `extern "C"` (not `RayLib := extern`):** `parse_extern_block_cimport` only parses
  `#include` directives — it cannot accept hand-written signatures, and the alias creates a
  *separate* namespace from the module, so structs and functions would land in two namespaces. The
  **unaliased** path (`parser.cryo:1981-1992`) parses bodyless `function f(...) -> T;` lines AND
  already does exactly what we need at `type_resolution.cryo:2165-2218`: registers
  `RayLib::InitWindow` (qualified) while `register_mangled_name(qualified, bare)` keeps the LLVM link
  symbol bare `InitWindow`. Types register qualified at `type_resolution.cryo:2636-2677`. **Net: one
  unified `RayLib` module, bare C link symbols, zero new sema mechanism.**
- **Rejected — synthetic AST injection:** more invasive (hand-build `ExternBlockNode`/`StructDeclNode`
  with valid spans/interned names), loses the cache/diff/inspect story, and buys nothing since the
  qualify-by-namespace mechanism is identical regardless of how the AST was produced. Generated text
  is human-inspectable and re-runs the identical, tested pipeline.

### Registry record schema (global + project-local)
A **global** registry plus **project-local pins** that win.
- Global: `$CRYO_HOME/vendor/registry.toml` (fallback `~/.cache/cryo/vendor/registry.toml`, reusing
  the `deps/cache.cryo` root chain). Project pin: a `[vendor]` section in `cryoconfig`.
- Resolution order: **project pin → global → error.**
- Record (designed so a future C++-shim `binding_source` slots in without a rewrite):
  ```toml
  [raylib]
  name          = "RayLib"            # the import namespace
  source_path   = "/abs/path/to/raylib"
  binding_source = "headers"          # "headers" now; "cxx-shim" later (§5)
  headers       = ["raylib.h"]        # entry headers to parse
  include_dirs  = ["/abs/path/to/raylib/src"]
  defines       = ["PLATFORM_DESKTOP"]
  # link metadata -> threads into ProjectConfig link arrays
  link.system   = ["raylib"]
  link.search   = ["/abs/path/to/raylib/lib"]
  link.windows.system = ["raylib", "opengl32", "gdi32", "winmm"]
  # cache index: triple -> generated file + provenance key
  cache.x86_64-linux-gnu = { file = "...", key = "<sha>" }
  ```

### Caching key (correctness-critical)
Cache is keyed on **`(content hash of headers+include graph, target triple, libclang version,
generator version)`** — never content-only. Layout mirrors `deps/cache.cryo`:
`~/.cache/cryo/vendor/<lib>/<triple>/RayLib.cryo` plus a sidecar `.key`. Cache miss ⇒ regenerate
(supports cross-compile: a new triple lazily regenerates without a schema change).

### Resolver insertion point
In `resolve_import_path` (`module_loader.cryo:281-321`), add a branch **before** filesystem search:
if `import_path` starts with `vendor::`, strip the prefix, consult the registry (project→global),
select the cache entry for `ctx.target_triple` (regenerate on miss), and return the cached
`.cryo` path. Downstream discovery/sema/codegen/LSP treat it as any other module.

### Link-metadata propagation
At project setup, the resolver/registry merges each imported vendor entry's link metadata into the
`ProjectConfig.link_*` arrays already consumed by `run_linking` (`passes.cryo:855-900`). Composition
across multiple vendor libs = array concatenation (dedup `-l`/`-L`), platform overlays via the
existing `[link.unix]`/`[link.windows]` split.

### libclang FFI binding set (the one allowed hand-written set)
A new `stdlib/ffi/libclang.cryo` (or `tools/`-local) declares the ~30 libclang functions needed:
`clang_createIndex`, `clang_parseTranslationUnit`, `clang_visitChildren` (callback),
`clang_getCursor*`, `clang_getCursorType`, `clang_Type_getSizeOf`/`getAlignOf`/`getOffsetOf`,
`clang_getEnumConstantDeclValue`, `clang_Cursor_getMangling` (C++ hook, unused now), `clang_getCString`,
`clang_disposeString`, etc. All expressible today (§1 FFI verdict).

### Diagram
```
cryo vendor ./raylib
   │  (eager generation)
   ▼
[VendorGenerator]  --FFI-->  libclang  (parse TU, walk cursors)
   │   emits namespaced .cryo source
   ▼
~/.cache/cryo/vendor/raylib/<triple>/RayLib.cryo   +   registry.toml entry (+ link meta)
                                   ▲
import vendor::RayLib              │ resolve_import_path() vendor:: rule
   │                               │
   ▼                               │
ModuleLoader ──► ModuleGraph ──► sema ──► codegen (declare_extern_block, bare link names)
                     │                          │
                     └─► LSP completion         └─► run_linking() + merged ProjectConfig.link_*
```

---

## 3. Type-translation table (libclang cursor/type → Cryo)

Drive every primitive off libclang's **canonical type + size queries**, never the C spelling.

| C construct (libclang) | Cryo emission | Notes / decision |
|---|---|---|
| `int/long/...` (via `clang_getCanonicalType` + `getSizeOf` + signedness) | `i8/i16/i32/i64`, `u8/...` | `long` = 4B Win / 8B Linux → size-query, not spelling |
| `float`/`double` | `f32`/`f64` | |
| `_Bool` | `boolean` | |
| `T*` | `T*` (Cryo pointer) | |
| `const T*` | `T*` | const dropped (Cryo has no FFI const); document. See IMPROVEMENT.md |
| `char*` / `const char*` | `u8*` | **string-literal→`u8*` coercion is an open item (§5)** |
| `T[N]` in struct | fixed-size field / `T*` in params | params decay to pointer; struct arrays need layout-faithful field |
| `struct`/`union` (definition) | `type struct` with layout-faithful fields | verify each field offset via `clang_Type_getOffsetOf`; emit explicit padding if mismatch |
| opaque / forward-declared struct | `type struct Foo {}` opaque handle, used as `Foo*` | raylib uses some |
| `enum` | `type enum E : <underlying int>` with constants via `clang_getEnumConstantDeclValue` | respect `clang_getEnumDeclIntegerType` |
| `typedef` | resolve to canonical; emit alias only if it names a struct/enum | avoid alias explosion; map `int32_t`→`i32` directly |
| function pointer type | Cryo `fn(...) -> T` pointer type | needed for callback APIs |
| variadic `f(..., ...)` | `function f(..., ...) -> T` | FFI supports it |
| bitfields | **raw-storage**: emit backing integer field(s) + (optional) accessor fns | layout-faithful; flag that direct field access is unavailable |
| anonymous struct/union | flatten with synthesized field names OR nested named type | choose flatten-with-`_anon{N}` |
| object-like `#define INT` | `const NAME: <inferred int/float> = ...;` | only simple numeric/string literals (libclang token scan) |
| function-like macro | **dropped, with a logged report** | cannot bind; document the cutoff |
| name collisions | none — `RayLib::` namespace disambiguates | confirmed via `qualify_symbol_sym` |

**Silent-drop is a correctness bug:** every unmapped symbol/macro/construct is collected and
reported (count + names) at `cryo vendor` time, never dropped silently.

---

## 4. Staged task breakdown (each independently testable)

**Stage 1 — Registry + resolver + link, NO generation (de-risk plumbing).**
- Add `cryo vendor <path>` (+ `list`/`remove`/`rebuild`) to `CLI/commands.cryo` (6-step recipe).
- Implement the global `registry.toml` reader/writer + `cryoconfig [vendor]` project pin
  (project→global order).
- Add the `vendor::` branch in `resolve_import_path` (`module_loader.cryo:281-321`).
- Merge vendor `link.*` into `ProjectConfig.link_*`.
- **Acceptance:** hand-place a stub `RayLib.cryo` (a few `type struct`s + an `extern "C"` block) and
  a stub `libraylib`; `import vendor::RayLib` resolves, `RayLib::InitWindow` calls compile, and the
  binary links against the stub. Proves resolution + link propagation before any libclang work.

**Stage 2 — libclang C generation (target raylib).**
- Hand-write `ffi/libclang.cryo` bindings; add a libclang locator (path discovery + version probe).
- Build `VendorGenerator`: parse TU, walk cursors, emit the namespaced `.cryo` per §3, plus a
  translation report. Wire eager generation into `cryo vendor`.
- **Acceptance:** `cryo vendor ./raylib` produces a `RayLib.cryo` containing raylib's structs +
  functions; the file compiles standalone; spot-check ≥3 struct layouts against
  `getSizeOf`/`getOffsetOf`.

**Stage 3 — Target-keyed caching.**
- Cache on `(content hash, triple, libclang version, generator version)`; sidecar `.key`;
  regenerate on miss; `cryo vendor rebuild` forces regen.
- **Acceptance:** second `cryo vendor`/import is a cache hit (no clang run); editing a header or
  changing `--target` triggers regen; two triples coexist under the lib's cache dir.

**Stage 4 — Generality pass.**
- Validate a header-only lib (an `stb_*.h`) and a small custom local C project; handle the awkward
  §3 constructs (bitfields, anonymous unions, opaque handles, function pointers, `#define` consts);
  ensure the translation report surfaces every drop.
- **Acceptance:** all three (raylib, stb header, custom lib) generate + compile + run; report lists
  unbindable function-like macros rather than dropping silently.

**Stage 5 — (Note seams, defer) C++ shim + LSP polish.**
- Confirm LSP `RayLib::` autocomplete works for free (expected; verify). Leave the `binding_source =
  "cxx-shim"` record field + `clang_Cursor_getMangling` hook in place but unimplemented.

---

## 5. Risks & prerequisites

- **FFI for libclang — RESOLVED:** by-value structs, callbacks, enums, opaque ptrs, variadics, C
  strings all supported today (§1). No compiler prerequisite. *Lowest-risk path:* validate end-to-end
  with one by-value-struct-returning call (e.g. `clang_getCursorSpelling` → `CXString`) very early in
  Stage 2 to confirm the ABI on the real toolchain before building the full walker.
- **C-string ergonomics (OPEN):** the demo writes `RayLib::InitWindow(800, 450, "Cryo")`, but a Cryo
  string literal is a fat pointer, not `u8*`. Decide between (a) implicit string-literal→`u8*`
  coercion at extern-C call boundaries, or (b) requiring `"Cryo".as_cstr()`/`c"Cryo"`. This affects
  whether the headline demo works verbatim. **Recommend deciding before Stage 2 acceptance.** See
  IMPROVEMENT.md.
- **libclang discovery/version:** locate via `$CRYO_LIBCLANG`, then `llvm-config --libdir`, then OS
  defaults; record the libclang version in the cache key; define a supported version window. The
  Clang dependency itself is already accepted (the `#include` path shells out to clang).
- **Struct layout fidelity:** trust `getSizeOf`/`getAlignOf`/`getOffsetOf` and emit explicit padding
  when Cryo's natural layout disagrees; assert total size matches or fail the symbol with a report
  entry. (Cryo gotcha: pass owning aggregates by pointer — but these are POD C structs, low risk.)
- **Bare-symbol collisions across vendor libs:** `type_resolution.cryo:2198-2200` lets the second lib
  skip the bare slot; harmless (qualified `Lib::sym` always registers) but warrants a diagnostic.
- **Self-host bootstrap:** the generator and `ffi/libclang.cryo` are new compiler/stdlib code — a
  repin is required to ship; validate to self-host fixed point (Win + WSL) per the project's standard
  gates. The generator runs at `cryo vendor` time, so it must be in the shipped compiler, not a
  separate tool, unless we make it a `tools/` binary.

---

## 6. Acceptance criteria

1. `cryo vendor ./raylib` then `import vendor::RayLib` **compiles and runs a window demo**
   (`InitWindow` → `BeginDrawing`/`ClearBackground`/`EndDrawing` → `CloseWindow`), linking raylib
   automatically with no hand-written bindings and no manual link flags.
2. A **header-only** library (an `stb_*.h`) vendors, compiles, and runs.
3. A **custom local C project** (a couple of headers + a built `.a`) vendors, compiles, and runs.
4. Re-running `cryo vendor` / re-importing is a **cache hit**; changing `--target` or a header
   **regenerates**; unbindable constructs appear in a **report**, never silently dropped.
5. `RayLib::` **autocompletes in the LSP** (verify; expected for free).

---

## Verification

- **Stage gates:** after each stage, run the project's standard chain from PowerShell — `make`
  (Windows native, `CRYO_CC=gcc`), `make test` (expect 99/99), and self-host to fixed point via WSL
  (byte-identical). New compiler/stdlib code requires a **repin** before the generator is usable in
  the shipped toolchain (stash-stdlib → `make pin` → restore dance if new syntax is involved).
- **Plumbing (Stage 1):** stub-lib resolution + link, no clang.
- **Generation (Stage 2-3):** diff the generated `RayLib.cryo`; assert struct sizes/offsets against
  libclang queries; confirm cache hit/miss and triple coexistence.
- **End-to-end (Stage 4 + acceptance):** build and **run** the raylib window demo, the stb demo, and
  the custom-lib demo on Linux (WSL) and native Windows; confirm the binary links and executes.
- **LSP:** open a file importing `vendor::RayLib`, type `RayLib::`, confirm completion lists generated
  functions/types.
