# TODO: Vendored Libraries (C + C++) for Cryo

Status: **planning / not started** · Owner: Jake · Last updated: 2026-06-16

A roadmap for letting Cryo projects **vendor C and C++ libraries** — drop a library
into a project and use it directly, *without* hand-writing bindings and *without* a
separate build step. Inspired by Odin's `vendor:` collection.

## Confirmed scope

| Axis | Decision |
| --- | --- |
| **Language** | Full C++ (classes, templates, overloads, mangling, vtables) — not just C |
| **Binding strategy** | **Both**: an auto-bindgen engine *and* a curated, Odin-style `vendor/` collection of hand-written bindings layered on top |
| **Build model** | Cryo **compiles** bundled C/C++ sources itself (invokes cc/clang/c++), in addition to linking pre-built libs |

> **Honest framing.** "Full C++ (templates/vtables/exceptions)" is *not* the realistic
> finish line of this roadmap. The realistic deliverable is **"call C/C++ libraries that
> expose a mostly-non-virtual, exception-free, explicitly-instantiated surface"** — which
> covers most real-world vendor use while being honestly bounded. M1–M3 below are tractable
> "wire existing capabilities together" work; M5+ is a multi-quarter effort dominated by
> *ABI fidelity and silent-corruption testing*, not parsing.

---

## What already exists (the foundation)

- **C header importer** — `compiler/src/compiler/passes/c_header_import.cryo` (723 lines).
  A hand-rolled string scanner that shells to `clang-20 -E -P` (`pick_c_preprocessor`,
  override via `$CRYO_CC`) and extracts **only function prototypes**. It **explicitly
  skips** `typedef`/`struct`/`union`/`enum` (lines 331-337) and `#define` constants;
  unknown types → `void*`; function pointers → `void*`. C only.
  Syntax: `c := extern "C" { #include <stdio.h> }`. Registered as `PassID::CHeaderImport`
  in `passes/pass_registry.cryo`.
- **FFI / extern + ABI** — `extern "C"` blocks (`parser.cryo` ~1944), `ExternBlockNode`
  (`AST/declaration.cryo` ~944), codegen `declare_extern_block`
  (`declaration_emitter.cryo` ~1276), overload-aware extern declaration
  (`codegen/ops/symbol_resolver.cryo` ~106-253), SysV C ABI in `codegen/abi.cryo`
  (`classify_signature_extern_c`: sret / byval / small-aggregate coercion).
  `![link("sym")]`, `![repr(C)]`, `![repr(packed)]`, `![align(N)]` all exist.
- **libLLVM precedent (the model for libclang)** — the compiler already FFIs libLLVM-20
  via `compiler/llvm_bindings.h`, consumed by
  `llvm := extern "C" { #include "../../../llvm_bindings.h" }` in `codegen/_module.cryo:36`.
  A `compiler/clang_bindings.h` is the same move.
- **Deps / build system** — `compiler/src/compiler/deps/{Cache,Git,Semver,Lockfile,DepResolver}`;
  `DepKind = Path|Git|Invalid` (`project_config.cryo` ~89); the cryoconfig INI parser
  (`project_config.cryo`); per-profile/per-origin build layout
  `<out>/target/<profile>/<triple>/{std,local,<dep>}/{deps,ir}`; incremental builds via
  `build_manifest.cryo` FNV `.modkeys`; linking in `codegen/passes.cryo` `run_linking` (~1165).
  **Cryo currently never compiles C/C++ — it only *links* pre-built archives** via `[link]`
  `system`/`search`/`static` (+ `[link.unix]` / `[link.windows]` overlays).

### Language-capability findings that shape the plan

- ✅ **Function-pointer types are NOT a gap.** `TypeAnnotation::Function` exists with
  `(T, U) -> R` syntax (`expr_parser.cryo` ~2548) and a backing arena type. Mapping C
  function pointers to it is "wire up the emitter," not type-system work.
- ❌ **Unions ARE a gap.** No `union` keyword, no `UnionDeclNode`, no union type anywhere.
  Real union support is genuine new language surface (see Phase 1b).
- ⚠️ **Enums** support `![repr(C)]` + an explicit discriminant (`type enum Foo : i32`);
  ADT-style C-ABI enums are post-1.0 → bindgen should emit C enums as integer `const`s.
- ✅ **Type aliases** (`TypeAliasDeclNode`) exist → fine for `typedef`.

### ⚠️ Dominant cross-cutting risk: the bootstrap / self-host landmine

The compiler is **self-hosted**. Adding FFI surface (`clang_bindings.h` + extern block),
new AST nodes (`union`), or new cryoconfig keys (`vendor = ...`) requires the classic
**fix-then-repin two-phase**: the *pinned* `bin/cryo` must be able to build the new source
before a new pin can use it. An older pin **silently drops** unknown cryoconfig sections.
`scripts/selfhost-check.py` must stay byte-identical (6/6 stages).

**Mitigation:** **feature-gate libclang** so the default self-host build does *not* link
`-lclang`. Bindgen-from-headers becomes a capability of a clang-enabled compiler build
(invoked as a tool), keeping `selfhost-check` clean and de-risking the repin.

---

## Phases

### Phase 0 — Bindgen engine decision (pivotal) ⬜

**Decision: switch to libclang. Do _not_ extend the hand-rolled scanner.**

Given full-C++ is the goal, extending a string scanner to parse C++ (templates, overload
sets, nested namespaces, member functions, default args, `decltype`, ref-qualified methods)
is rebuilding a C++ front-end by hand — not realistic. libclang hands you exactly the
cursors needed: `CXCursor_StructDecl`, `EnumDecl`, `TypedefDecl`, `FunctionDecl`,
`ClassDecl`, `CXXMethod`, `Namespace`, `ClassTemplate`, `FieldDecl`, `MacroDefinition`
(filter with `clang_Cursor_isMacroFunctionLike`). Critically,
**`clang_Cursor_getMangling`** gives the exact Itanium/MSVC mangled symbol — *never*
reimplement mangling. `clang_getCursorType` + `clang_getTypeKind` give canonical types for
reliable mapping. The libLLVM FFI in `llvm_bindings.h` is the exact precedent.

- [ ] Create `compiler/clang_bindings.h` (mirror `llvm_bindings.h`; declare only the
      libclang functions used).
- [ ] New pass file `compiler/src/compiler/passes/c_header_import_clang.cryo`
      (keep the old scanner as the no-libclang fallback — do not delete yet).
- [ ] Register a new `PassID` in `passes/pass_registry.cryo`.
- [ ] Add `-lclang` to the compiler's own cryoconfig link config **behind a build feature**
      (this is the repin-triggering edit).
- [ ] Fix-then-repin two-phase; verify `make selfhost-check` byte-identity (6/6).

**Risks:** the repin/selfhost dance; libclang version skew (pin clang-20 to match the
`clang-20 -E` already chosen by `pick_c_preprocessor`); libclang parses but does **not**
decide ABI (still classified in Phase 5); the extern surface grows with every API used.

### Phase 1 — Emit C bindings from libclang cursors ⬜

Turn C declarations into real Cryo AST (replacing function-only scanner output).

- [ ] `FunctionDecl` → `FunctionDeclNode` with `![link(<mangling>)]` (bare name for C).
- [ ] `StructDecl` → `StructDeclNode` with `![repr(C)]` (layout already supported).
- [ ] `EnumDecl` → emit each enumerator as a `const` of the discriminant integer type
      (ABI-trivial; sidesteps the post-1.0 ADT-enum caveat).
- [ ] `TypedefDecl` → `TypeAliasDeclNode`; fn-ptr typedef → `TypeAnnotation::Function`.
- [ ] Object-like `#define` whose value is an integer/float/string literal → `const`.
      Function-like macros: **unsupported** (not ABI symbols).
- [ ] Fn-ptr params / fields / returns → `TypeAnnotation::Function` (no new type-system
      work — single biggest fidelity win over the old scanner).
- [ ] Replace string-match `c_type_to_annotation` with a `CXType`-driven mapper keyed on
      `clang_getTypeKind`; `void*` only for genuinely opaque types.

**Files:** `passes/c_header_import_clang.cryo`; reuse `AST/declaration.cryo` constructors;
`codegen/type_map.cryo` if new primitive mappings surface.
**Risks:** anonymous nested structs/unions; bitfields (Cryo has no bitfield layout —
pad-and-hide or refuse); flexible array members; forward decls / recursive struct pointers
(two-pass: declare opaque, then fill); `#define`s that are expressions, not literals.

#### Phase 1b — Union support (language work) ⬜

- [ ] **Stopgap first (no language change):** emit C unions as a `![repr(C)]` byte-array
      struct sized to the largest member, with typed accessor helpers (bitcast). Unblocks
      bindgen immediately.
- [ ] **Real impl (deferred):** `union` keyword in lexer + `UnionDeclNode`
      (`AST/declaration.cryo`) + union type (`types/compound.cryo`) + max-size/max-align
      layout + codegen. Most invasive language change in the roadmap (lexer → parser → AST
      → type system → lowering → codegen); requires a repin before the pinned compiler can
      parse `union`.

**Honest call:** ship the byte-array stopgap; defer the real union type to M4.

### Phase 2 — Compile bundled C/C++ sources ⬜

First time Cryo invokes a C compiler. Today it only links pre-built archives.

- [ ] New build step `compile_native_sources`, slotted **before `run_linking`** in
      `codegen/passes.cryo`. Walk each dep's declared sources, invoke the cc/clang/c++
      driver, write `.o` into the existing per-dep dir
      `<out>/target/<profile>/<triple>/<depname>/deps/` (the link step already gathers
      `.o` from there, so objects flow in automatically).
- [ ] Generalize `pick_c_preprocessor` → `pick_c_compiler` / `pick_cxx_compiler`
      (`$CRYO_CC` / `$CRYO_CXX`).
- [ ] cryoconfig per-dep keys: `cflags`, `cxxflags`, `sources` (globs), `include_dirs`,
      `defines` — parsed in `project_config.cryo`.
- [ ] Caching: extend `build_manifest.cryo` modkeys (FNV of source content + effective
      flags + compiler version); skip recompilation when unchanged.
- [ ] Cross-compile: thread the target triple into `-target <triple>` (+ sysroot).

**Files:** `codegen/passes.cryo`, `build_manifest.cryo`, `project_config.cryo`, new
`deps/native_build.cryo` for the driver invocation.
**Risks:** gcc vs clang vs MSVC `cl` flag differences (start with clang/clang-cl only);
C++ runtime linkage (compiling `.cc` means the *user's* link config needs `-lc++`/`-lstdc++`
and a `c++`/`clang++` driver); incremental-cache correctness; header include-path
propagation so bundled sources see their own headers.

### Phase 3 — Tie bindgen to compiled sources (first useful milestone) ⬜

A dependency where Cryo both *binds the headers* (Phase 1) and *compiles the bundled `.c`*
(Phase 2) — a working linked program with zero pre-built artifacts.

- [ ] In the resolver (`deps/DepResolver`): on resolving a vendor/native dep, (a) run
      bindgen on its headers → a **synthetic in-memory** Cryo module in the dep namespace
      (prefer in-memory over a generated file to avoid cache-invalidation and honor
      reproducible builds), and (b) register its sources with `compile_native_sources`.
- [ ] Make bindgen output resolvable by `module_loader.cryo`'s namespace→file map
      (synthetic module identity).

**Files:** `deps/DepResolver`, `module_loader.cryo`, `module_graph.cryo`, `instance.cryo`
(`compile_project_with_ctx` orchestration).
**Risks:** ordering — bindgen must run early enough that name resolution sees the symbols
but late enough that the dep's include graph is resolved; synthetic-module identity/caching;
diagnostic spans pointing into generated code.

### Phase 4 — Vendor dependency kind + curated collection ⬜

- [ ] Extend `DepKind` (`project_config.cryo` ~89, currently `Path|Git|Invalid`) with
      `Vendor`. cryoconfig syntax: `raylib = { vendor = "raylib" }` → resolves `vendor/raylib/`.
- [ ] Vendor package manifest in `vendor/<name>/`: `headers` (to bind), `sources`
      (to compile), `link_system`/`link_static` (libs), `namespace`, `cflags`/`cxxflags`,
      `language = "c" | "c++"`.
- [ ] Layout `vendor/<name>/{manifest, include/, src/, bindings/}`, where hand-written
      `bindings/*.cryo` **take precedence** over auto-generated bindings (Odin model: hand
      bindings authoritative, bindgen is fallback/bootstrap).
- [ ] Curated collection ships in the distribution alongside `std`, resolved by the
      existing per-origin machinery.
- [ ] Lockfile: vendor entries are content-addressed by **manifest + source tree hash**
      (not a git rev) — `deps/Lockfile`.

**⚠️ Bootstrap:** new cryoconfig keys are **silently dropped by an older pinned compiler**.
Add the parser changes (`vendor = ...`, manifest keys) and **repin BEFORE** authoring any
vendor packages, or the pinned compiler quietly ignores them and emits confusing
"unresolved dependency" errors.
**Files:** `project_config.cryo`, `deps/DepResolver`, `deps/Lockfile`, new `vendor/` tree.
**Risks:** hand-vs-generated precedence rules; lockfile semantics for vendored trees;
collection-version vs compiler-version coupling.

### Phase 5 — C++ hard parts (aggressively scoped) ⬜

**Do now (cheap, high value):**
- [ ] Name mangling via `clang_Cursor_getMangling` → `![link(<mangled>)]`. Never reimplement.
- [ ] Overloads → distinct Cryo functions with distinct link symbols
      (`symbol_resolver.cryo` already supports overload declaration).
- [ ] Non-virtual member functions → free fn `(this: T*, args) -> R`
      (Itanium: `this` is just the implicit first pointer arg).

**Medium (ABI work):**
- [ ] By-value class passing — add `classify_signature_cxx` in `codegen/abi.cryo`
      implementing the Itanium "non-trivial for purposes of calls → pass by hidden
      reference" rule (detect non-trivial copy/move/dtor via libclang triviality queries);
      trivial classes follow the existing SysV small-aggregate path. **Single most
      error-prone item.**
- [ ] Constructors/destructors → mangled `(this: T*, args) -> void`; caller allocates
      storage and calls ctor/dtor explicitly (no implicit RAII at the boundary initially).

**Defer / punt (explicit non-goals):**
- [ ] ~~Cryo-side vtable synthesis~~ — treat polymorphic objects as opaque; call virtuals
      only through C++-compiled thunks / non-virtual entry points.
- [ ] ~~General templates~~ — support **explicit instantiation only** (vendor author writes
      `template class std::vector<int>;` in a bundled `.cc`; Cryo binds the instantiated
      mangled symbols). No Cryo-driven instantiation.
- [ ] ~~Exceptions~~ — **terminate at the boundary**: wrap C++ calls so an escaping
      exception `abort`s rather than unwinding into Cryo (no foreign unwind tables).
- [ ] ~~MSVC C++ ABI~~, ~~multiple/virtual inheritance~~, ~~RTTI / `dynamic_cast`~~,
      ~~member pointers~~ — ship Itanium/Linux first.

**🔒 Mandatory:** a **differential ABI test harness** — compile a tiny C++ TU with clang,
call it from Cryo, compare observable results for *every* ABI shape (by-value small/large
struct, struct-with-dtor, returned struct). ABI mismatches are **silent memory corruption**,
not compile errors.

**Files:** `codegen/abi.cryo` (`classify_signature_cxx`), `c_header_import_clang.cryo`,
`codegen/ops/symbol_resolver.cryo`, `codegen/type_map.cryo`.

---

## Recommended ship order

| Milestone | Content | Ships |
| --- | --- | --- |
| **M1** | Phase 0 + 1 (libclang engine, C-only, feature-gated) | Dramatically better C header import; needs no new language surface |
| **M2** | Phase 2 + 3 | A dep that bundles `.c`, compiled + linked by Cryo end-to-end |
| **M3** | Phase 4 (config + repin) | `foo = { vendor = "foo" }` working for real C libraries |
| **M4** | Phase 1b (real unions) | Retire the byte-array stopgap, if header coverage demands |
| **M5** | Phase 5 "do now" | Callable C++ free functions, overloads, non-virtual methods (pointer/ref args + trivial types only) |
| **M6** | Phase 5 "medium" | C++ by-value class ABI + ctors/dtors — gated on the differential ABI harness |

**Deferred indefinitely / explicit non-goals:** Cryo-side vtable synthesis; general
templates (explicit instantiation only); exceptions (terminate at boundary); MSVC C++ ABI;
multiple/virtual inheritance; RTTI.

---

## Per-milestone verification

- After every repin: `make selfhost-check` must be **byte-identical (6/6)**.
- `make test` (unit + compile-fail) green.
- M5+: the differential ABI harness must pass for every ABI shape before the milestone is
  considered done.

## Key files (future touch-points)

- `compiler/src/compiler/passes/c_header_import.cryo` — model + the pass to supersede
- `compiler/llvm_bindings.h` — precedent for the new `clang_bindings.h`
- `compiler/src/compiler/codegen/passes.cryo` — `run_linking` (~1165); where
  `compile_native_sources` slots in
- `compiler/src/compiler/project_config.cryo` — `DepKind` (~89); vendor kind + native-build keys
- `compiler/src/compiler/codegen/abi.cryo` — `classify_signature_extern_c`; needs `classify_signature_cxx`
- `compiler/src/compiler/deps/*` — resolver, lockfile, native-build driver
- `compiler/src/compiler/passes/pass_registry.cryo` — new `PassID`
