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

## 2. C-string ergonomics: string-literal → `u8*` at extern boundaries  *(unblocks the headline demo)*

**Problem.** A Cryo `string` literal is a fat pointer (ptr+len), but C wants a null-terminated
`u8*`/`const char*`. So `RayLib::InitWindow(800, 450, "Cryo")` from `PLAN.md` won't compile verbatim
today — the literal won't coerce to `u8*`.

**Direction (pick one, ideally a `c"..."` literal + a coercion):**
- A **C-string literal** `c"Cryo"` that lowers directly to a null-terminated `[N x i8]` global and
  decays to `u8*` — explicit, zero ambiguity. (Cleanest; recommended.)
- And/or an **implicit coercion** of a string *literal* (compile-time known, can guarantee NUL) to
  `u8*` specifically at `extern "C"` call argument positions.
- A `.as_cstr()` method on `string`/`str` as the runtime path for non-literal strings (needs a NUL
  guarantee — either the buffer is already NUL-terminated or it allocates).

**Touch points.** Lexer (new literal token) or call-site coercion in the type checker / call emitter
(`codegen/visit/call_emitter.cryo`), plus codegen for the NUL-terminated global. The literal route is
the smallest and most predictable.

**Why it helps vendoring.** Without it, every C API taking a string is awkward; with it the demo and
most C libraries "just work."

---

## 3. First-class function-pointer parameter types in the C path

**Problem.** The primitive C parser collapses function-pointer parameters to `void*`
(`c_header_import.cryo`). The *language/FFI* already supports `fn(...) -> T` pointer types (the
codegen call emitter handles them, `call_emitter.cryo:346-417`), but the C importer can't emit them,
and the ergonomics of taking the address of a Cryo function to pass as a C callback should be
confirmed and documented.

**Direction.** Independent of libclang: write a small test that declares an `extern "C"` function
taking a `fn(i32) -> i32` parameter, passes a Cryo function to it, and confirms it round-trips. Fix
any gaps (address-of on a named function, typed null callbacks). Document the bare-function-pointer
contract (no closures — client-data `void*` carries state, which is exactly the libclang visitor
shape).

**Why it helps vendoring.** Callback-based C APIs (and `clang_visitChildren` itself) depend on this.

---

## 4. Struct layout control & assertions (`repr(C)` faithfulness)

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

---

## 5. Diagnostics for FFI symbol collisions

**Problem.** When two vendor libs (or extern blocks) export the same bare C symbol, the second silently
skips claiming the bare slot (`type_resolution.cryo:2198-2200`). Harmless today (the qualified
`Lib::sym` always registers) but invisible.

**Direction.** Emit a low-severity diagnostic when a bare extern symbol is claimed twice, naming both
owners. Small, self-contained sema change with a clear test.

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

## 7. TOML read/write helper for the registry (reuse audit)

**Problem.** The vendor registry is TOML (`registry.toml`) and `cryoconfig` is already TOML-ish
(`project_config.cryo` parses `[link]`, `[dependencies]`, etc.). Before Stage 1 of the plan, confirm
whether the existing config parser is reusable for a general read/write, or whether a small dedicated
TOML helper in stdlib is warranted.

**Direction.** Audit `project_config.cryo`'s parser; if it's section-specific, factor a minimal
general `[section] key = value / array` reader+writer into stdlib so the registry and config share one
implementation. Pure stdlib/compiler-support work, no codegen.

---

## Notes
- Items 1–3 most directly unblock `PLAN.md` and are the best "meantime" work; 4 is the highest-value
  safety net once generation exists; 5–7 are small hygiene wins.
- None of these require libclang, so they can all land and be repinned before the vendor feature
  starts.
