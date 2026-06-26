# HANDOFF — libclang C-import engine (`Compiler::Bindgen`): finish Phase 2.1 → Phase 3 → vendor

**You are continuing the libclang C-import engine.** It already emits real Cryo **types** (structs,
unions, enums, typedefs), **anonymous-enum constants**, and **object-like `#define` constants** from an
inline `extern module name := "C" { #include ... }` block. Your job: finish the remaining Phase 2.1
coverage gaps, then Phase 3 (layout self-verification), then feed the **vendor library system**
(`PLAN.md`) — the end goal this all exists for.

> **Read first, in order:** this file → the memory entries (under
> `~/.claude/projects/.../memory/`): `bindgen-phase2-libclang-typeemit.md` (Phase 2 design/traps/gating),
> `bindgen-phase21-anon-enum-consts.md` (the const-under-alias channel), `bindgen-phase21-define-macros.md`
> (the macro tokenizer + the predefined-macro-leak trap) → `IMPROVEMENT.md` (§4 static_assert) →
> `PLAN.md` (the vendor generator — the north star). Then the source landmarks in §6.

---

## 0. State of the tree you are inheriting

`HEAD = adec2c0a` ("Implement object-like macro support in FFI bindings"). The two most recent commits
are Phase 2.1: `89cf29a4` (anon-enum consts + the binding-namespace global channel) and `adec2c0a`
(object-like `#define` macros). **Working tree is clean** (one untracked design doc, unrelated).

**Gate state (all green, Linux/WSL):**
- `make cryo` — clean.
- `make test` — **OVERALL PASS**, 113 compile-fail + the C-import unit tests
  (`tests/tests/lang/c_import_types.cryo` + `tests/helpers/bindgen_types.h` — now cover struct/typedef/
  enum, anon-enum consts incl. a negative, and `#define` int/hex/negative/float).
- Linux self-host — **FIXED POINT OK**, byte-identical, IR md5 `2e9d4681b8d421b23a027248f28c7c03`.
- Windows — not re-verified this session (the wine 6-stage needs `unzip` + has a RAM wall; the native
  `bin/cryo.exe` smoke path is documented in §8). Confirm on a native host / CI before a release pin.

**⚠️ NOT REPINNED.** `bin/cryo`(+`.exe`) still predate Phase 2.1. The compiler source does **not** use
anon-enum/`#define` emission (its only C-import is the `![functions_only]` `llvm` block), so the
self-host is a stable fixed point WITHOUT a repin — but the shipped pin does not yet embed these
features. A repin is needed to SHIP. Repin from a CLEAN tree for a release pin (see §8).

---

## 1. What's done (the foundation you build on)

For `extern module probe := "C" { #include "h.h" }`:
- **struct** → `![repr(c)]` `type struct probe::Name` (recursive field mapping, pointers, fixed arrays,
  fn-pointer fields). **union** → `![repr(c)]` `type struct` with one `_storage: u8[sizeof]` blob +
  `min_alignment` (pass-by-value, no field access). **named enum** → `type enum probe::Name : <int>`
  with explicit discriminants. **typedef** → `type alias probe::Name`. **function** → `FunctionDeclNode`.
- **anonymous enum** → one alias-namespaced global `const probe::CONST = value` per constant (negatives
  via two's-complement, truncated to width at codegen).
- **object-like `#define`** → one alias-namespaced `const probe::NAME = <literal>` per numeric macro
  (decimal/hex/octal/negative integers with narrowest-type inference + `u`/`l` suffixes; floats → `f64`).
  Function-like macros, valueless guards, string/char literals, and compound bodies are
  **reported-and-skipped** (`cdebug`), never silently dropped.

All reachable **only** through the alias (`probe::Vec2`, `probe::CONST`), never bare. Default-on; a block
tagged `![functions_only]` emits prototypes only (the compiler's own `llvm` import uses this).

---

## 2. Architecture — how emission works

### The pipeline seam
`CHeaderImport` pass (`PassID` order 4, Frontend) runs `Compiler::Bindgen::Importer::run(ctx)`. For each
CImport `ExternBlockNode` it builds a libclang TU (`clang_parseTranslationUnit`, `-x c`, **flag
`CXTranslationUnit_DetailedPreprocessingRecord`** so macro cursors appear) and `clang_visitChildren`-walks
it. The free function `visit_decl` is the bare C callback; it dispatches per cursor kind to
`emit_function` / `emit_record` / `emit_enum` / `emit_typedef` / `emit_macro`.

### The load-bearing mechanisms (do not regress these)
1. **System-header filter (`Clang::is_in_system_header`).** libclang exposes the WHOLE TU; without this
   every `#include <stddef.h>` dumps ~100 glibc typedefs into the alias. `visit_decl` drops any cursor
   whose location `isInSystemHeader`.
2. **Anonymity via `Clang::is_anonymous` (NOT empty-spelling).** libclang gives an anonymous tag a
   SYNTHETIC spelling (`enum (unnamed at …)`), so the old empty-spelling test never matched. Both
   `emit_enum` and `emit_record` now use `clang_Cursor_isAnonymous`.
3. **Macro main-file filter (`Clang::is_from_main_file`).** `DetailedPreprocessingRecord` also surfaces
   the compiler's PREDEFINED macros (`__STDC__`, `__GCC_HAVE_*`) — NOT clang "builtins" and NOT in a
   system header, so they leak. `emit_macro` restricts to the TU's main file (the imported header IS the
   main file). Side effect: macros from transitively-included headers and from `<system>` imports are
   excluded (acceptable; avoids flooding).
4. **The binding-namespace channel.** `emit_*` pushes a real top-level decl node into
   `ctx.artifacts.ast.root.statements` and stamps `binding_namespace: SymbolStr` (= the alias) on it.
   `CompilationContext::qualify_binding_sym(sym, binding_ns)` qualifies under the alias. This is a
   DEDICATED channel — NOT `source_module` (the monomorphizer uses that). The field lives on
   `StructDeclNode`/`EnumDeclNode`/`TypeAliasDeclNode` AND now **`VarDeclNode`** (for the consts).

### The const-under-alias channel (anon-enum + `#define` reuse this — `emit_alias_const`)
A binding-ns global const is **qualified-only** and works end-to-end via sites that all honor
`binding_namespace`:
- `passes/type_resolution.cryo` (VariableDeclaration Phase-4 arm): register under `qualify_binding_sym`,
  namespace = alias, **skip the bare registration** (anti-pollution).
- `resolver/name_resolution.cryo` (`forward_declare_node` VariableDeclaration): **skip** the bare
  file-scope decl for binding-ns globals.
- `codegen/ops/declaration_emitter.cryo` (`codegen_global_var`): mangle the LLVM symbol under the alias
  ns, AND register the codegen `this.globals` cache under the **QUALIFIED** key (`resolve_global` checks
  the cache BEFORE its owned-by-current-module short-circuit; a bare cache key is never hit → null).
- Resolution already existed: `sema::resolve_scope_resolution` probes `lookup_global_var("alias::NAME")`
  (built for IntrinsicConst + the bare-name-collision fix). No new compiler capability was needed.

### Gating
`![functions_only]` is a directive (whitelisted in `directive_processing.cryo::is_known_builtin`,
validated extern-block-only). The importer reads it via `extern_node.has_directive("functions_only")`.
visit_decl applies it AFTER the function branch, so functions_only skips types/enums/macros — which is
why the compiler's `llvm` import keeps the self-host fixed point even with the new preprocessing flag.

---

## 3. Phase 2.1 — remaining coverage / honesty gaps (pick up here)

Each is independently testable (add a case to `tests/helpers/bindgen_types.h` + `c_import_types.cryo`).
**Silent-drop is a correctness bug (PLAN.md §3) — everything unmapped must be reported.**

1. **String / char `#define` constants.** Currently reported-and-skipped (`emit_macro`'s
   `macro_lit_is_text`). Needs escape-faithful lowering: strip the C quotes and translate C escapes
   (`\n`, `\t`, `\"`, `\xNN`, …) to the byte sequence the Cryo lexer would produce, then emit a `String`
   `LiteralNode` typed `i8*`/`u8*` (match what `char*` maps to in `TypeMap`; codegen's
   `str_cache.get_or_create` consumes the value). Char literal → the integer code point (type `char`/`u8`).
2. **Anonymous struct/union members** (currently `void*` in `TypeMap`) → flatten with synthesized field
   names (`_anon0`) or emit a nested named type. PLAN.md §3 leans flatten. NOTE: `emit_record` already
   correctly SKIPS a top-level anonymous record via `is_anonymous`; this gap is about anonymous members
   nested INSIDE a named struct/union.
3. **Bitfields** → raw-storage: emit the backing integer field(s); flag that direct field access is
   unavailable (PLAN.md §3). `clang_getFieldDeclBitWidth` / `clang_Cursor_isBitField`.
4. **Unions** are opaque `u8[sizeof]` blobs today (no field access). Decide if accessor methods or a
   tagged representation are worth it, or leave documented-opaque.
5. **A structured translation report.** Collect counts + names of every skipped/approximated construct
   (function-like macros, string macros, anonymous records, bitfields) and surface them — `cdebug` today;
   a real report once `cryo vendor` exists. PLAN.md §3/§6 require this.

---

## 4. Phase 3 (high-value, self-contained) — layout self-verification

Make a generated binding **fail to compile if Cryo's layout disagrees with C's** — the biggest silent
FFI-miscompile risk. `static_assert(cond, "msg")` exists at module scope (IMPROVEMENT.md §4; supports
`sizeof(T)`/`alignof(T)` + arithmetic/comparison; lowered in TypeLowering).

**Do:** when `emit_record`/`emit_enum` emit a type, also emit (into the program's `static_assert`
side-list on `ProgramNode`) one assert per type from libclang's authoritative numbers —
`static_assert(sizeof(probe::Vec2) == <clang_Type_getSizeOf>)` and `alignof(...) == <getAlignOf>`. For
structs, per-field offsets via **`clang_Type_getOffsetOf`** (add this extern to `clang.cryo`). A mismatch
surfaces as a compile error (E0237) at the binding. The size/align numbers are already at hand in
`emit_record` (the union path calls `getSizeOf`). De-risk by printing the asserts first (see §7 repro).

---

## 5. The north star — vendor library system (`PLAN.md`)

`cryo vendor ./raylib` registers a C project once; `import vendor::RayLib` makes `RayLib::InitWindow(...)`,
`RayLib::Vector2`, etc. usable. PLAN.md has the staged plan. **Key reconciliation:** PLAN.md §2 chose
**generated `.cryo` text** for the *vendor* generator (a real `namespace RayLib;` file), whereas the
inline `#include` path you've been extending **injects AST**. Both are consumers of the same
libclang→Cryo translation logic (`TypeMap` + the `emit_*` shapes + the macro tokenizer). Before building
the vendor generator, **factor the cursor→Cryo-decl mapping** so it can drive either AST emission (inline)
or source-text emission (vendor), rather than duplicating it. The type-mapping table is the shared core.

---

## 6. Source landmarks

- **Engine:** `compiler/src/compiler/bindgen/{clang,type_map,importer}.cryo` (+ `_module.cryo`).
  - `clang.cryo` (`::Clang`) — the libclang FFI surface: by-value structs (`CXString`/`CXCursor`/`CXType`/
    `CXSourceLocation`/`CXUnsavedFile`/`CXSourceRange`/`CXToken`), cursor/type/token-kind constants,
    parse flags, ~40 `clang_*` externs, and the helpers `is_in_system_header` / `is_anonymous` /
    `is_from_main_file` / `intern_spelling` / `token_spelling`. **Add new externs here**
    (`clang_Type_getOffsetOf` for Phase 3; `clang_Cursor_isBitField`/`getFieldDeclBitWidth` for bitfields).
  - `type_map.cryo` (`::TypeMap`) — `map_type_aliased(CXType, alias, ctx) -> TypeAnnotation*` +
    `make_prim`/`make_ptr`/`make_named`/`make_array`/`make_function`/`tag_name`. The shared translation core.
  - `importer.cryo` (`::Importer`) — pass body + `visit_decl` + `emit_function`/`emit_record`/`emit_enum`/
    `emit_typedef`/`emit_macro` + nested visitors (`visit_field`/`visit_enum_const`/`visit_anon_enum_const`)
    + `emit_alias_const` (shared const emitter) + the macro literal classifiers
    (`macro_lit_is_text`/`macro_lit_is_float`/`macro_int_is_unsigned`) + `ImportState` (carries `tu`,
    `alias`, `functions_only`, dedup `seen_names`/`struct_names`/`struct_nodes`). **Most Phase-2.1 work
    lands here.**
- **AST nodes:** `compiler/src/compiler/AST/declaration.cryo` — `StructDeclNode`/`EnumDeclNode`/
  `TypeAliasDeclNode`/`VarDeclNode` each carry `binding_namespace`; `ExternBlockNode` holds functions +
  `namespace_alias` + includes. `AST/cloner.cryo` clones `binding_namespace`. `AST/expression.cryo`
  (`LiteralNode`), `AST/_module.cryo` (`LiteralKind`, `TypeAnnotation`).
- **Qualification helper:** `compilation_context.cryo` `qualify_binding_sym`.
- **Const registration/resolution:** `passes/type_resolution.cryo` (VariableDeclaration Phase-4 arm +
  `register_decl_in_index` for types), `resolver/name_resolution.cryo` (`forward_declare_node`),
  `codegen/ops/declaration_emitter.cryo` (`codegen_global_var`), `sema/sema.cryo`
  (`resolve_scope_resolution`), `sema/type_utils.cryo` (`lookup_global_var`), `decl_index.cryo`
  (`register_global*`/`lookup_global`/`get_global_namespace`).
- **`static_assert` (Phase 3):** grep `static_assert` in the parser / `ProgramNode` for the side-list.
- **Bootstrap consumer:** `codegen/_module.cryo` — `![functions_only] extern module llvm := "C" {...}`.

---

## 7. Traps (hard-won — do not re-learn)

1. **`make test` does NOT rebuild the compiler** (`$(STAGE2):` has no prereqs). **Always
   `make cryo && make test`** after a compiler-source edit, or you test the stale binary (caused several
   "identical failure" red herrings).
2. **C-imported types/consts must be qualified-only** (`probe::Vec2`/`probe::FOO`), never bare-registered —
   else they pollute the global namespace and shadow same-leaf symbols in other modules. Guarded at the
   `binding_namespace` sites in §2.
3. **libclang anonymity ≠ empty spelling.** Use `clang_Cursor_isAnonymous`. (An anon tag spells as
   `enum (unnamed at …)`.)
4. **`DetailedPreprocessingRecord` leaks the compiler's predefined macros** (`__GCC_HAVE_*`) — they are
   not "builtins" and not in a system header. Filter macro emission with `is_from_main_file`.
5. **codegen `resolve_global` checks its cache BEFORE the owned-by-current-module short-circuit.** A
   same-module `alias::NAME` reference needs the codegen `this.globals` cache keyed by the QUALIFIED name
   (a single-file `cryo build` will *appear* to succeed but emit a degraded `unreachable` body — only the
   batch test runner's LLVM verification catches the null operand; `cryo check` passing proves only that
   SEMA resolved, not that codegen emitted).
6. **De-risk libclang work with a standalone repro** under WSL: a tiny `main.cryo` + header, built with
   `--emit-llvm`, then inspect `build/target/release/host/local/ir/<NS>.ll`. The session's repro lives at
   `scratch/anon-enum-repro/` (`main.cryo`/`ae.h` + `dbg*.sh`); reproduce the pattern (print the
   `static_assert`s you'd emit for Phase 3). `intrinsics::printf("XDBG …")` in importer/codegen pins the
   exact failing branch fast.
7. **libclang aggregates pass BY VALUE** (`CXString` 16B, `CXType`/`CXSourceRange`/`CXToken` 24B,
   `CXCursor`/`CXSourceLocation` 32/24B). Cryo's SysV + Win64 ABI handles this. Copy the existing
   `clang.cryo` declaration shapes exactly when adding externs.
8. **A header parsed by libclang must `#include` the headers declaring the types it uses** — libclang
   *parses* (not just preprocesses); an undeclared type defaults to implicit-`int` → silent 64→32-bit
   truncation.
9. **CRLF breaks `scripts/*.sh` under WSL** (`autocrlf=true` Windows checkout): strip CR
   (`sed -i 's/\r$//'`) in the SAME shell before running, or add `.gitattributes *.sh text eol=lf`.
10. **The test suite compiles all test files together** — don't name a test type/const with a
    stdlib/other-test leaf name (it shadows). Namespace C-import test aliases uniquely (`cit`).

---

## 8. Build / test / self-host / repin — commands & gotchas

Self-hosted compiler; **libclang (`libclang1-20`) is a hard build dependency.** This is a Windows
checkout; the dev loop runs in **WSL Ubuntu** (has `make`, `libclang-20`, today's `build/cryo`). Native
Windows builds run from **PowerShell** with `CRYO_CC=gcc` (NOT Git Bash — it breaks on the cmd-syntax
stdlib recipe). Invoke WSL from PowerShell as `wsl -d Ubuntu bash -c "..."`.

```bash
# (run inside WSL, cwd = repo root)
make cryo                                       # rebuild compiler (~1.5 min) -> compiler/build/cryo
make cryo && make test                          # ALWAYS pair them (trap #1). Baseline: OVERALL PASS, 113 cf
python3 scripts/selfhost-check.py --no-windows  # Linux fixed-point gate (~5 min); --no-windows skips wine 6-stage
make pin                                         # repin BOTH (Linux native + Windows cross-build) — Jake commits the pins

# ad-hoc single-file compile + dump IR (the de-risk loop, trap #6):
./compiler/build/cryo build path/main.cryo --stdlib=/abs/path/to/stdlib --emit-llvm -o /path/out
# -> IR at path/build/target/release/host/local/ir/<Namespace>.ll
```

- **PowerShell→WSL quoting** is fragile: `$PWD`/`$()`/quoted `|` get mangled. Prefer absolute paths and,
  for anything non-trivial, write a `bash` script to `scratch/` and run it (strip CR first, trap #9).
- **WSL `/tmp` is volatile** (the VM tears down between calls, clearing it) — write outputs under the
  project dir (`scratch/`, on `/mnt/c`) so they persist across `wsl ...` invocations.
- **Windows pin** needs `.toolchains/llvm-win/` with `libclang.dll.a` (`scripts/fetch-windows-llvm.sh`
  provisions it; the llvm-mingw half for the wine self-host needs `unzip` — only for
  `selfhost-check.py`'s wine verification, not the pin cross-build). `bin/` stages `LLVM-C.dll`/
  `libclang.dll` (gitignored) for the pin runtime.
- **No surface-syntax change** is needed for Phase 2.1/3 emission (you emit AST, not grammar) → no
  two-phase repin for the engine. A repin is still needed to SHIP. Release pin wants a CLEAN tree
  (`verify-pin.py --require-clean`).
- **NEVER edit compiler source while a self-host / `make cryo` runs** — it builds stages from the tree; a
  mid-run edit yields a false "FIXED POINT BROKEN".
- Keep `docs/cryo.md` §18.2 (C Header Import) in sync — it already documents types, anon-enum consts,
  `#define` numeric consts, `![functions_only]`, and system-header filtering.

## 9. Conventions
- `legacy/bootstrap` and `experimental/stdlib-next` are **dead code** — don't audit/fix.
- **Commit policy:** ONLY Jake commits / co-authors. You may RUN `make pin*` to produce pin artifacts for
  him to commit.
- Owner (Jake) is self-taught, ~4y, on a capstone — wants **honest signal**, the right long-term call
  over a quick green, **no hacky workarounds just to pass**. When something is environment-limited (the
  wine self-host, the Windows toolchain), say so plainly rather than forcing a green.
