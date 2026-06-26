# Compiler & Language Improvements — Groundwork for Vendor / FFI

Standalone improvements that reduce shortcuts and pave the way for the vendor library system
(see `PLAN.md`). Each is independently useful, ordered roughly by leverage-vs-effort. File paths
are from the current self-hosted compiler. Anything touching codegen/stdlib needs the usual gates:
`make` (PowerShell, `CRYO_CC=gcc`), `make test` (99/99), self-host fixed point (WSL), and a repin to
ship.

---

## 1. FFI `const` qualifier on pointer types  *(you flagged this — good first target)*

**Problem.** The C-header path drops `const` entirely: `const int*` and `int*` both become a plain
Cryo pointer (`c_header_import.cryo` strips `const` in `c_type_to_annotation`, ~lines 558-563). The
language has no way to express a const C pointer, so generated bindings can't preserve API intent and
the type system can't help catch writes-through-const.

**Direction.** Add a `const T*` pointer form to the type system (a flag on the pointer type, not a
new kind) that:
- parses in type annotations (`PointerAnnotation` gains a `is_const` flag),
- is ABI-identical to a normal pointer (no codegen change — it's a frontend/type-checker concept),
- is accepted where a non-const pointer is expected only in the safe direction (`T*` → `const T*`),
  rejecting the unsafe one.

**Touch points.** `AST` pointer annotation node, parser pointer parsing, `TypeArena`/`TypeKind`
pointer construction (carry the const bit or intern a distinct const-pointer type), type-checker
assignability rules, and `c_type_to_annotation` (stop stripping `const`). Start minimal: parse +
represent + the one assignability rule; defer deep const-propagation.

**Why it helps vendoring.** Generated bindings become faithful (`const char*`, `const T*` params),
and it's a self-contained type-system exercise that doesn't depend on libclang.

---

## 2. C-string ergonomics: `.as_cstr()` for non-literal strings  *(scope reduced — literals already work)*

**Correction (verified 2026-06-25).** The original premise here was wrong. A Cryo `string` is **not**
a fat pointer — it lowers to a null-terminated `i8*` (`codegen/type_map.cryo:224`; the `{ptr,len}`
fat-pointer form is an explicitly *future* plan). That is why `printf("...")` already works. A direct
test (`extern "C" { function strlen(s: u8*) -> u64; function puts(s: u8*) -> i32; }` called with a
string literal) **compiles and runs**, so `RayLib::InitWindow(800, 450, "Cryo")` works verbatim today.
The headline demo is not blocked.

**What actually remains.**
- **`const char*` params** — handled by §1. Once the FFI `const` qualifier exists, a string literal
  must still coerce in the safe direction to `const u8*`. Fold this into §1's assignability rule.
- **Non-literal runtime strings** — a heap `String` (the `stdlib/collections` type) is a struct, not
  an `i8*`, so it can't be passed to a C `u8*` directly. Add a `.as_cstr()` accessor on
  `String`/`str` that yields a NUL-terminated `u8*` (the buffer is already NUL-terminated, or it
  allocates). This is the only genuine new work in this item.
- **`c"..."` literal** — now optional sugar (explicit C-string spelling), not a blocker. Defer unless
  there's appetite; the literal path already does the right thing.

**Touch points.** Pure stdlib for `.as_cstr()` (no codegen). The `const` interaction lives in §1.

**Why it helps vendoring.** Most C string APIs already "just work"; `.as_cstr()` closes the
runtime-string gap for the rest.

---

## 3. First-class function-pointer parameter types in the C path  *(DONE — language path verified 2026-06-25)*

**Verified.** The language-level callback round-trip works end-to-end with **no compiler change**.
Note the surface syntax is `(Args) -> Ret` (parenthesized params + arrow), *not* `fn(...) -> T`.
Confirmed against real libc `qsort` and pure-Cryo probes:
- `extern "C"` function-pointer params (`(const void*, const void*) -> i32`) call correctly;
- a named Cryo function is passed **by bare name** (address-of is implicit, no `&`);
- function-pointer **locals** (`const fp: (i32) -> i32 = f;`) work;
- **typed null** callbacks pass and compare (`f == null`);
- the **`void*` client-data** pattern (the `clang_visitChildren` visitor shape) works;
- `const void*` callback params come for free from §1.

Regression tests: `tests/tests/lang/fn_pointer_callback.cryo` (4 `![test]`s). Documented in
`docs/cryo.md` §18.4 (bare-function-pointer contract: no closures; client-data `void*` carries state).

**Remaining (deliberately deferred).** The *inline* `#include` C importer still collapses
function-pointer parameters to `void*` (`c_header_import.cryo`). Per §6 / PLAN.md, the inline parser
is not grown to emit `(...) -> T`; libclang is authoritative for vendoring. This is an honesty gap to
report (§6), not a language gap.

**Why it helps vendoring.** Callback-based C APIs (and `clang_visitChildren` itself) work today.

---

## 4. Struct layout control & assertions (`repr(C)` faithfulness)  *(DONE — static_assert added 2026-06-25)*

**Shipped.** Compile-time `static_assert(cond)` / `static_assert(cond, "message")` at module scope,
evaluated during TypeLowering once layouts are final; a false or non-constant condition is **E0237**.
Supports integer/boolean literals, `sizeof(T)`, `alignof(T)`, and arithmetic/comparison/logical/
bitwise operators. Implemented without a new `NodeKind` or lexer keyword: a contextual top-level
`static_assert(` parses into a side list on `ProgramNode`; TypeResolution resolves the `sizeof`/
`alignof` operand types; TypeLowering folds + checks. Tests: `tests/tests/lang/static_assert.cryo`
(positive) + `tests/tests/negative/E0237_static_assert.cryo`. Documented in `docs/cryo.md` §18.5.

Note (already-existing, per audit): `sizeof`/`alignof` and the `![repr(c)]`/`packed`/`transparent`
attribute syntax pre-date this work; fixed-size array struct fields already lower correctly. Making
`repr_c` actually drive layout (vs. the de-facto C layout) remains future work, low priority.

<details><summary>original direction</summary>

**Problem.** Generated bindings must match C struct layout exactly. Today there's no way to (a)
assert a struct's size/alignment, or (b) insert explicit padding when Cryo's natural layout would
disagree with the C ABI.

**Direction.**
- A compile-time `static_assert(sizeof(T) == N)` / `alignof` check so generated bindings can
  self-verify against the numbers libclang reports.
- Explicit padding fields are already expressible (`_pad0: [N x u8]`); make sure fixed-size array
  fields in structs lower correctly and document the pattern.
- Optionally a `![repr(c)]` attribute on `type struct` to lock field order / default-C alignment as
  intent (even if it's the de-facto behavior).

**Touch points.** A const-eval `sizeof`/`alignof` usable in a static assertion; type-checker;
`codegen` struct lowering for fixed-size array fields.

**Why it helps vendoring.** Turns "we hope the layout matches" into "the binding fails to compile if
it doesn't" — the single biggest silent-miscompile risk in FFI.
</details>

---

## 5. Diagnostics for FFI symbol collisions  *(DONE 2026-06-25)*

**Shipped.** A second `extern "C"` block claiming an already-extern-owned bare C symbol now emits
**W0011** (warning, non-fatal), naming both owning files and pointing at the duplicate, with a help
line suggesting the qualified form. Crucially it stays **silent** on the intentional cases — an
intrinsic or a regular Cryo function owning the bare slot (verified: extern `malloc`/`free` produce
no warning; zero spurious W0011 across the whole stdlib+compiler self-build). Mechanism: a new
`decl_index.extern_bare_owner` map records bare slots claimed *by extern blocks specifically*, so the
resolver tells extern-vs-extern collisions apart from intrinsic-wins (`type_resolution.cryo`).

Note: W0011 is a warning (build succeeds), so it isn't expressible in the negative compile-fail test
framework; verified manually. The intrinsic-wins silence is the real correctness property and is
covered by the clean self-host build.

---

## 6. Grow the built-in C parser's honesty (report, don't drop)

**Problem.** The inline `#include` parser silently skips structs/unions/enums/typedefs and rejects
decls over 500 chars (`c_header_import.cryo`). Even though libclang supersedes it for *vendoring*, the
inline path will keep surprising users.

**Direction (low effort, high honesty):** make the inline path *report* what it skipped (count +
kinds) instead of dropping silently, and lift the 500-char cutoff to something generous or
streaming-based. Do **not** invest in growing it to parse structs — that's libclang's job per the
plan; just stop it from lying.

---

## 7. Registry format: JSON (reuse `stdlib/json`)  *(decided — no new parser needed)*

**Decision (2026-06-25).** The vendor registry is **JSON** (`registry.json`), not TOML. The audit
confirmed `project_config.cryo`'s parser is hardcoded per-section (9 match blocks, read-only, no
serializer) and there is **no general TOML reader/writer** in stdlib — so a TOML registry would mean
writing a parser *and* a serializer from scratch. Meanwhile stdlib already ships a general-purpose,
RFC-8259 JSON parser **and** serializer (`stdlib/json/parser.cryo`, `stdlib/json/serializer.cryo`).
Using JSON means the registry reader/writer is essentially free.

**Direction.** In Stage 1, build the registry read/write on `stdlib/json` (parse on read, serialize
on write). Leave `cryoconfig` as-is (its TOML-ish format is fine for the project-local `[vendor]` pin;
that path is read-only and already implemented). No new parser, no codegen.

---

## 8. Extern-import syntax: `extern module name := "C" { ... }`  *(DONE 2026-06-25)*

**Shipped.** The aliased C-import spelling is now `extern module name := "C" { #include ... }`; the old
`name := extern "C" { ... }` form is removed. Parser dispatch keys on `KwExtern` + `KwModule`
(`parse_extern_module_cimport` + shared `finish_cimport_block`); the compiler's own usage
(`codegen/_module.cryo`) migrated; docs updated (`docs/grammar.md` CHeaderImport, `docs/cryo.md`
§4.4/§18.2, code comments). Done via the two-phase repin: Phase 1 added the new form alongside the old
+ repinned (Linux+Windows) so the pins learned it; Phase 2 migrated the source, deleted the old form,
rebuilt + repinned to fixed point. Legacy `tier6_ffi` bootstrap tests still use the old spelling but
are dead code (not built by `make test`).

<details><summary>original direction</summary>

**Change.** Replace the aliased C-import spelling `name := extern "C" { #include ... }` with
`extern module name := "C" { #include ... }`. Same semantics (an `#include`-only CImport block that
binds into a named child namespace); the new form reads as a declaration keyword first and reuses the
already-existing `module` keyword (`KwModule`).

**Touch points.**
- Parser dispatch (`parser.cryo:289-303`): recognize `KwExtern KwModule Identifier ':' '=' StringLiteral`
  instead of `Identifier ':' '=' KwExtern`. The unaliased `extern "C" { ... }` block (`:306`) is
  unchanged.
- `parse_extern_block_cimport` (`parser.cryo:2000-2077`): adjust which tokens are pre-consumed.
- Update doc comments referencing the old form (`c_header_import.cryo:3`, `AST/declaration.cryo:985`).
- **Bootstrap landmine:** the compiler's own source uses the old form once
  (`codegen/_module.cryo:36`, `llvm := extern "C" { #include ... }`). New syntax = the standard
  two-phase repin: the *pinned* compiler must accept the new form before the source is rewritten to
  use it. Sequence: teach the parser the new form (still accept the old form during the transition or
  do a stash-build), repin, then migrate `codegen/_module.cryo` and any examples/docs, then repin
  again to fixed point.
- Grammar/docs: `docs/cryo.md` if it documents the old form; grep examples/tests for `:= extern`.

</details>

**Why.** Cosmetic/ergonomic; keyword-leading reads more clearly and groups with other `extern`
declarations. Not a vendor-path blocker (the vendor generator emits *unaliased* `extern "C"` per
PLAN.md §2), but it's a small language-surface cleanup worth doing before the surface ossifies.

---

## Notes
- Items 1–3 most directly unblock `PLAN.md` and are the best "meantime" work; 4 is the highest-value
  safety net once generation exists; 5–8 are small hygiene wins.
- **Audit corrections (2026-06-25):** §2 was rescoped (string literals already pass as `u8*`); §4 is
  narrower than written (`sizeof`/`alignof` and `![repr(c)]`/`packed`/`transparent` syntax already
  exist — only a compile-time `static_assert` and `repr_c`-drives-layout are new); §5 must distinguish
  *extern-vs-extern* collisions from the intentional *intrinsic-wins* bare-slot skip; §7 is decided
  (JSON). §1, §3, §6 verified accurate.
- None of these require libclang, so they can all land and be repinned before the vendor feature
  starts.
