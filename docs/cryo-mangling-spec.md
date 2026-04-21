# Cryo Name Mangling Specification

**Version:** 0.2 (Draft)
**Status:** Design specification for implementation in `cryoc`

**Changes from 0.1:** Added variadic parameter support (§3, §4, §6, §8.17–8.18, §9.3, §10 state machine, §12.8, Appendix A).

---

## 1. Overview

This document specifies the Cryo name mangling scheme used to encode fully-qualified symbol names into linker-compatible identifiers. The scheme is designed to be:

- **Unambiguous** — every mangled name decodes to exactly one source-level symbol.
- **Demangler-friendly** — parseable by a small state machine (~300 LOC).
- **Readable under pressure** — recognizable in stack traces, linker errors, and `objdump` output without running a demangler.
- **Stable** — monomorphization and ABI changes produce distinct symbols.
- **Non-compressing** — no back-reference substitutions (Itanium-style) that hurt readability.

All Cryo symbols begin with the prefix `C$`. FFI symbols declared `extern "C"` are **not mangled** and pass through to the linker verbatim.

---

## 2. Design Principles

1. **`C$` prefix** marks all Cryo-mangled symbols. This is the only place `$` appears in the scheme.
2. **Structural separators are distinct tokens** — each character has exactly one role based on position.
3. **Length-prefixed identifiers** handle arbitrary user identifiers (digits, underscores, etc.) without escape rules.
4. **Separator-per-role** — the character alone tells the demangler what structural element is next.
5. **No substitution compression** — readability over symbol length. Modern linkers handle long names.
6. **Deterministic** — same input always produces the same output, independent of compilation order.

---

## 3. Separator Alphabet

| Character | Role                                           | Position                           |
| --------- | ---------------------------------------------- | ---------------------------------- |
| `C$`      | Cryo symbol prefix                             | Start only                         |
| `#`       | Special-kind tag                               | Immediately after `C$`             |
| `.`       | Namespace / path separator                     | Between path segments              |
| `::`      | Type-member separator (method belongs to type) | Between owner type and member      |
| `<` `>`   | Generic type argument delimiters               | Around type lists                  |
| `,`       | Type list separator                            | Inside `<...>` and parameter lists |
| `~`       | Parameter list start                           | After path, before params          |
| `!`       | Return type marker                             | After parameter list               |
| `&`       | Reference receiver / reference type            | In parameter lists                 |
| `*`       | Pointer type                                   | Prefix on type codes               |
| `@`       | Overload disambiguator                         | Optional suffix                    |
| `E`       | Variadic parameter marker                      | Last entry in parameter list       |

---

## 4. Grammar

```ebnf
MangledName   = "C$" [ KindTag ] Path "~" ParamList "!" ReturnType ;

KindTag       = "#" ( "vt" | "ti" | "ct" | "dt" | "op" | "mi" | "cl" | "tr" ) ;

Path          = Segment { "." Segment } [ "::" MemberSegment ] ;
Segment       = Ident | GenericSegment ;
GenericSegment= Ident "<" TypeList ">" ;
MemberSegment = Ident | GenericSegment ;

Ident         = Length RawChars ;
Length        = Digit { Digit } ;         (* decimal, length in bytes *)
RawChars      = <exactly `Length` bytes of UTF-8> ;

TypeList      = Type { "," Type } ;

ParamList     = "v"                              (* empty param list *)
              | FixedParams [ "," Variadic ]     (* fixed params, optional variadic tail *)
              | Variadic ;                       (* variadic only *)

FixedParams   = Type { "," Type } ;

Variadic      = "E"                              (* untyped C-style: ...        *)
              | "E" Type ;                       (* typed:            T...       *)

ReturnType    = Type ;

Type          = PrimType
              | "P" Type                   (* pointer *)
              | "R" Type                   (* reference *)
              | "A" Type                   (* array/slice *)
              | "N{" Path "}"              (* named type *)
              | "&" [ "m" ]                (* receiver: & or &m for mut & *)
              ;

PrimType      = "a" | "s" | "i" | "l"      (* signed ints i8..i64 *)
              | "h" | "t" | "j" | "m"      (* unsigned ints u8..u64 *)
              | "f" | "d"                  (* f32, f64 *)
              | "b" | "c" | "S" | "v" | "u" ;  (* bool, char, string, void, unit *)

Digit         = "0" | "1" | ... | "9" ;
```

**Note on `E`:** `E` is reserved as the variadic marker and is not used as a primitive type code. The demangler distinguishes typed vs. untyped variadic by lookahead: if the character after `E` begins a valid type (a primitive code, `P`, `R`, `A`, `N`), the variadic is typed; otherwise (end-of-input for the param list — i.e. `!` next) it is untyped C-style.

---

## 5. Primitive Type Codes

| Cryo Type     | Code |     | Cryo Type | Code |
| ------------- | ---- | --- | --------- | ---- |
| `i8`          | `a`  |     | `u8`      | `h`  |
| `i16`         | `s`  |     | `u16`     | `t`  |
| `i32` / `int` | `i`  |     | `u32`     | `j`  |
| `i64`         | `l`  |     | `u64`     | `m`  |
| `f32`         | `f`  |     | `f64`     | `d`  |
| `boolean`     | `b`  |     | `char`    | `c`  |
| `string`      | `S`  |     | `void`    | `v`  |
| `()` unit     | `u`  |     |           |      |

---

## 6. Composite Type Encoding

| Cryo Type                   | Mangled Form           | Example                         |
| --------------------------- | ---------------------- | ------------------------------- |
| Pointer `T*`                | `P` + `<T>`            | `int*` → `Pi`                   |
| Reference `&T`              | `R` + `<T>`            | `&Vec2` → `RN{4Vec2}`           |
| Array / slice               | `A` + `<T>`            | `Array<int>` → `AN{5Array<i>}`  |
| Named type                  | `N{` + `<Path>` + `}`  | `Math::Vec2` → `N{4Math.4Vec2}` |
| Generic instance            | Path with `<TypeList>` | `Pair<int>` → `N{4Pair<i>}`     |
| Receiver `&this`            | `&`                    | —                               |
| Receiver `mut &this`        | `&m`                   | —                               |
| Variadic (untyped, C-style) | `E`                    | `...` → `E`                     |
| Variadic (typed)            | `E` + `<T>`            | `int...` → `Ei`                 |

**Note:** Receiver markers (`&`, `&m`) only appear as the **first parameter** of a non-static method. Elsewhere, references in parameters use `R<Type>`. Variadic markers (`E`, `E<T>`) only appear as the **last parameter** of a function.

---

## 7. Special Kind Tags

Kind tags appear only immediately after `C$`. They identify symbols that aren't regular functions.

| Tag   | Meaning              | Example                                |
| ----- | -------------------- | -------------------------------------- |
| `#vt` | Vtable for a type    | `C$#vt6Animal`                         |
| `#ti` | Type info / RTTI     | `C$#ti6Animal`                         |
| `#ct` | Constructor          | `C$#ct3Dog::3Dog~S!v`                  |
| `#dt` | Destructor           | `C$#dt3Dog::3Dog~&!v`                  |
| `#op` | Operator overload    | `C$#op4Vec3::3add~&,N{4Vec3}!N{4Vec3}` |
| `#mi` | Module initializer   | `C$#mi4Math.6Vector`                   |
| `#cl` | Closure              | `C$#cl8identity@0`                     |
| `#tr` | Trait implementation | `C$#tr7Display#for3Dog::3fmt~&!v`      |

### Operator Codes

For `#op`, the method name is replaced with a canonical short code:

| Operator | Code   | Operator  | Code   |
| -------- | ------ | --------- | ------ |
| `+`      | `add`  | `==`      | `eq`   |
| `-`      | `sub`  | `!=`      | `ne`   |
| `*`      | `mul`  | `<`       | `lt`   |
| `/`      | `div`  | `<=`      | `le`   |
| `%`      | `mod`  | `>`       | `gt`   |
| `&`      | `band` | `>=`      | `ge`   |
| `\|`     | `bor`  | `&&`      | `land` |
| `^`      | `xor`  | `\|\|`    | `lor`  |
| `<<`     | `shl`  | `!`       | `lnot` |
| `>>`     | `shr`  | `[]`      | `idx`  |
| `()`     | `call` | unary `-` | `neg`  |

---

## 8. Worked Examples

### 8.1 Free Function

```cryo
namespace Hello;
function main() -> int { ... }
```
→ **`C$5Hello.4main~v!i`**

### 8.2 Function with Parameters

```cryo
namespace Util;
function add(a: int, b: int) -> int
```
→ **`C$4Util.3add~i,i!i`**

### 8.3 Struct Instance Method

```cryo
// Rect.area(&this) -> int
```
→ **`C$4Rect::4area~&!i`**

### 8.4 Struct Static Method

```cryo
// Rect::new(w: int, h: int) -> Rect
```
→ **`C$4Rect::3new~i,i!N{4Rect}`**

### 8.5 Mutating Method

```cryo
// Rect.scale(mut &this, factor: int) -> void
```
→ **`C$4Rect::5scale~&m,i!v`**

### 8.6 Generic Struct Instantiation

```cryo
// Pair<int>::new(a: int, b: int) -> Pair<int>
```
→ **`C$4Pair<i>::3new~i,i!N{4Pair<i>}`**

Each monomorphization produces a distinct symbol. `Pair<string>::new` → `C$4Pair<S>::3new~S,S!N{4Pair<S>}`.

### 8.7 Nested Namespaces

```cryo
// Math::Vector::Vec2::dot(&this, other: Vec2) -> f64
```
→ **`C$4Math.6Vector.4Vec2::3dot~&,N{4Math.6Vector.4Vec2}!d`**

### 8.8 Class Virtual Method

```cryo
// Animal.speak(&this) -> void  (virtual)
```
→ **`C$6Animal::5speak~&!v`**

Associated vtable:
→ **`C$#vt6Animal`**

Associated type info:
→ **`C$#ti6Animal`**

### 8.9 Constructor / Destructor

```cryo
// Dog::Dog(_name: string)
```
→ **`C$#ct3Dog::3Dog~S!v`**

Destructor:
→ **`C$#dt3Dog::3Dog~&!v`**

### 8.10 Inherited Method (Override)

```cryo
// Dog overrides Animal.speak
```
→ **`C$3Dog::5speak~&!v`**

Each class gets its own symbol. Dispatch resolution happens via the vtable at runtime, not via name mangling.

### 8.11 Generic Function

```cryo
// identity<int>(x: int) -> int
```
→ **`C$8identity<i>~i!i`**

### 8.12 Operator Overload

```cryo
// Vec3.operator+(&this, other: Vec3) -> Vec3
```
→ **`C$#op4Vec3::3add~&,N{4Vec3}!N{4Vec3}`**

### 8.13 Trait Implementation

```cryo
// impl Display for Dog { fn fmt(&this) -> void }
```
→ **`C$#tr7Display#for3Dog::3fmt~&!v`**

### 8.14 Pointer Parameters

```cryo
// function write(buf: int*, len: int) -> int
```
→ **`C$5write~Pi,i!i`**

### 8.15 Complex Nested Generics

```cryo
// Result<Option<int>, string>::unwrap(&this) -> Option<int>
```
→ **`C$6Result<N{6Option<i>},S>::6unwrap~&!N{6Option<i>}`**

### 8.16 FFI Pass-through

```cryo
extern "C" { function puts(s: string) -> int; }
```
→ **`puts`** (unmangled)

Note: FFI variadic functions like C's `printf(fmt, ...)` are **not mangled** — they are declared `extern "C"` and pass through by name. The `E` variadic marker applies to Cryo-native variadic functions only.

### 8.17 Typed Variadic

```cryo
// function sum(first: int, rest: int...) -> int
```
→ **`C$3sum~i,Ei!i`**

All trailing `int` arguments are collected into the variadic slot. The single element type follows the `E`.

Heterogeneous variadic over a named type:

```cryo
// function log_all(prefix: string, items: Any...) -> void
```
→ **`C$7log_all~S,EN{3Any}!v`**

### 8.18 Untyped Variadic (Cryo-native C-style)

For rare cases where a Cryo function needs C-style untyped variadic semantics (e.g., a Cryo wrapper sitting directly atop a `va_list`-based intrinsic):

```cryo
// function trace(fmt: string, ...) -> void
```
→ **`C$5trace~S,E!v`**

The bare `E` with no following type denotes untyped variadic. The demangler recognizes it by seeing `!` immediately after `E`.

### 8.19 Variadic-only Parameter List

```cryo
// function raw(...) -> void
```
→ **`C$3raw~E!v`**

A function whose only parameter is a variadic uses `~E` (no leading `v` for emptiness — `v` means *no* parameters, and this function has one).

### 8.20 Method with Variadic

```cryo
// Logger.log(&this, args: string...) -> void
```
→ **`C$6Logger::3log~&,ES!v`**

Receiver comes first, variadic comes last, as always.

---

## 9. Mangling Algorithm

### 9.1 High-Level Procedure

```
mangle(symbol):
    output = "C$"

    if symbol is special-kind:
        output += "#" + kind_tag(symbol)

    output += encode_path(symbol.path)

    if symbol is function-like:
        output += "~"
        output += encode_params(symbol.params)
        output += "!"
        output += encode_type(symbol.return_type)

    return output
```

### 9.2 Path Encoding

```
encode_path(path):
    segments = []
    for segment in path.namespace_segments:
        segments.append(encode_segment(segment))
    result = join(segments, ".")

    if path.has_type_owner:
        result += "::" + encode_segment(path.member)

    return result

encode_segment(seg):
    s = str(len_bytes(seg.name)) + seg.name
    if seg.has_generic_args:
        s += "<" + join(map(encode_type, seg.args), ",") + ">"
    return s
```

### 9.3 Parameter Encoding

```
encode_params(params):
    if params is empty:
        return "v"

    parts = []
    for i, p in enumerate(params):
        if i == 0 and p.is_receiver:
            parts.append("&m" if p.is_mut else "&")
        elif p.is_variadic:
            # Must be the last parameter; verified earlier by the type checker.
            if p.has_element_type:
                parts.append("E" + encode_type(p.element_type))
            else:
                parts.append("E")      # untyped C-style
        else:
            parts.append(encode_type(p.type))
    return join(parts, ",")
```

**Invariant:** at most one variadic parameter per function; it must be the final entry. The mangler should assert this; violation indicates a front-end bug.

### 9.4 Type Encoding

```
encode_type(t):
    if t is primitive:     return PRIM_CODE[t]
    if t is pointer:       return "P" + encode_type(t.pointee)
    if t is reference:     return "R" + encode_type(t.referent)
    if t is array:         return "A" + encode_type(t.element)
    if t is named:         return "N{" + encode_path(t.path) + "}"
    error("unknown type")
```

Note: `encode_type` is *not* called on variadics directly — variadics are a parameter-list construct, not a type, and are handled inside `encode_params`.

---

## 10. Demangling Algorithm

The demangler is a linear state machine. No lookahead or backtracking required beyond a single character peek.

### 10.1 State Machine

```
demangle(s):
    expect(s, "C$")

    kind = None
    if peek(s) == "#":
        advance(s, 1)
        kind = read_chars(s, 2)

    path = parse_path(s)

    result = {kind: kind, path: path}

    if peek(s) == "~":
        advance(s, 1)
        result.params = parse_params(s)
        expect(s, "!")
        result.return_type = parse_type(s)

    return result
```

### 10.2 Path Parsing

```
parse_path(s):
    segments = [parse_segment(s)]
    while peek(s) == ".":
        advance(s, 1)
        segments.append(parse_segment(s))

    member = None
    if peek_2(s) == "::":
        advance(s, 2)
        member = parse_segment(s)

    return {segments: segments, member: member}

parse_segment(s):
    length = read_number(s)
    name = read_chars(s, length)
    generics = None
    if peek(s) == "<":
        advance(s, 1)
        generics = parse_type_list(s)
        expect(s, ">")
    return {name: name, generics: generics}
```

### 10.3 Parameter Parsing

```
parse_params(s):
    # Empty param list sentinel.
    if peek(s) == "v" and peek_at(s, 1) in {"!"}:
        advance(s, 1)
        return []

    params = []
    first = True
    while True:
        if first and peek(s) == "&":
            advance(s, 1)
            is_mut = peek(s) == "m"
            if is_mut: advance(s, 1)
            params.append(Receiver(is_mut))
        elif peek(s) == "E":
            advance(s, 1)
            # Typed vs. untyped: if the next char starts a type, it's typed.
            if peek(s) == "!":
                params.append(Variadic(element=None))
            else:
                params.append(Variadic(element=parse_type(s)))
            # Variadic must be the last parameter.
            if peek(s) != "!":
                error("variadic must be the final parameter")
            break
        else:
            params.append(parse_type(s))

        first = False
        if peek(s) == ",":
            advance(s, 1)
            continue
        break

    return params
```

### 10.4 Type Parsing

```
parse_type(s):
    c = peek(s)
    if c in PRIM_CODES:
        advance(s, 1)
        return prim_type(c)
    if c == "P":
        advance(s, 1)
        return Pointer(parse_type(s))
    if c == "R":
        advance(s, 1)
        return Reference(parse_type(s))
    if c == "A":
        advance(s, 1)
        return Array(parse_type(s))
    if c == "N" and peek_2(s) == "N{":
        advance(s, 2)
        path = parse_path(s)
        expect(s, "}")
        return NamedType(path)
    # Note: "&" and "E" are parameter-list constructs, not types.
    # They are handled in parse_params and should not reach parse_type.
    error("unknown type prefix")
```

### 10.5 Pretty Printing

The demangler output can be rendered back to Cryo-like syntax:

```
C$4Math.6Vector.4Vec2::3dot~&,N{4Math.6Vector.4Vec2}!d
    → Math::Vector::Vec2.dot(&this, Math::Vector::Vec2) -> f64

C$3sum~i,Ei!i
    → sum(int, int...) -> int

C$5trace~S,E!v
    → trace(string, ...) -> void
```

---

## 11. Integration with Debug Info

Use LLVM's `DIBuilder` to expose both forms:

- **`linkageName`** — the mangled symbol (e.g., `C$4Rect::4area~&!i`)
- **`name`** — the pretty-printed form (e.g., `Rect.area(&this) -> int`)

Debuggers (GDB, LLDB) and profilers (`perf`, VTune) will automatically show the human-readable form while the linker uses the mangled form. This is the same pattern C++ uses with DWARF.

```cpp
// In CodeGen, when creating DISubprogram:
DIBuilder.createFunction(
    scope,
    prettyName,       // "Rect.area(&this) -> int"
    mangledName,      // "C$4Rect::4area~&!i"
    file,
    line,
    type,
    /*...*/
);
```

---

## 12. Edge Cases and Rules

### 12.1 Empty Parameter Lists

A function with no parameters uses `~v` (literal "v" for void/empty) rather than `~` followed directly by `!`. This keeps the grammar regular and avoids ambiguity.

```
function noop() -> void   →   C$4noop~v!v
```

### 12.2 Length Prefixes and UTF-8

Length prefixes count **bytes**, not code points. Unicode identifiers are supported but their length is their UTF-8 byte length.

```
identifier "café" (5 bytes in UTF-8) → 5café
```

### 12.3 Reserved Identifier Characters

The following characters must **not** appear in Cryo identifiers (enforced by the lexer): `.`, `:`, `<`, `>`, `,`, `~`, `!`, `&`, `*`, `#`, `@`, `{`, `}`, `$`. If they ever become legal (e.g., via raw identifiers), the mangler must escape them via a `Q{len,hex}` wrapper.

### 12.4 Overload Disambiguation

Cryo does not currently support ad-hoc function overloading, but if added, the `@N` suffix on the final identifier segment resolves collisions where parameter lists alone cannot (e.g., overloads differing only in return type, if ever permitted).

```
C$3foo~i!i       (first foo)
C$3foo@1~i!S     (second foo, disambiguator index 1)
```

### 12.5 Anonymous / Generated Symbols

Compiler-generated symbols (closures, lambdas, synthetic thunks) use the `#cl` kind tag with the enclosing path and an index:

```
C$#cl8identity@0    (first closure inside `identity`)
```

### 12.6 Module Initializers

Each namespace that requires initialization at program startup gets a module initializer symbol:

```
C$#mi4Math.6Vector
```

These are linked into a global init list processed before `main`.

### 12.7 Stability Guarantees

- Changing a function's signature (params or return type) **always** changes its mangled name.
- Adding/removing generic parameters **always** changes the mangled name.
- Renaming a namespace **always** changes the mangled name.
- Reordering items within a file **never** changes mangled names.
- Adding, removing, or changing the element type of a variadic parameter **always** changes the mangled name.
- Converting a typed variadic `T...` to an untyped `...` (or vice versa) **always** changes the mangled name.
- The scheme must produce identical output for identical input across compiler versions within the same spec version.

### 12.8 Variadic Constraints

- At most **one** variadic parameter per function.
- The variadic parameter must be the **last** parameter.
- A function may have zero or more fixed parameters preceding a variadic.
- A pure-variadic function is written `~E` (typed: `~E<Type>`), not `~v,E`. The `v` sentinel is only for *truly* empty parameter lists.
- Untyped variadic (`E`) is primarily intended for wrappers over C-ABI varargs. Cryo code should prefer typed variadic (`E<Type>`) when possible.
- A receiver (`&` / `&m`) and a variadic can coexist — the receiver is always first, the variadic is always last, and any fixed params sit between them (see §8.20).
- FFI (`extern "C"`) functions are never mangled, so C's `printf`-style varargs do not use `E` — they pass through by name.

---

## 13. Implementation Notes

### 13.1 Mangler

The mangler should live in `src/Codegen/Mangler/` (or equivalent). It takes a fully-resolved, post-monomorphization symbol (where all generics are concrete types) and produces a string.

Suggested interface:

```cpp
class Mangler {
public:
    std::string mangle(const Symbol& sym);
    std::string mangleVTable(const ClassType& cls);
    std::string mangleTypeInfo(const Type& ty);
    std::string mangleConstructor(const ClassType& cls, const Ctor& ctor);
    // ...
private:
    void emitPath(const Path& p, std::string& out);
    void emitType(const Type& t, std::string& out);
    void emitParams(const ParamList& params, std::string& out);
    void emitVariadic(const VariadicParam& v, std::string& out);
};
```

### 13.2 Demangler

The demangler should live in `tools/CryoDemangle/` as both a library and a CLI tool (`cryo-demangle`).

Suggested interface:

```cpp
class Demangler {
public:
    struct VariadicInfo {
        std::optional<Type> element_type;  // nullopt = untyped C-style
    };

    struct Result {
        std::optional<std::string> kind_tag;
        Path path;
        std::optional<ParamList> params;
        std::optional<VariadicInfo> variadic;  // set iff final param is variadic
        std::optional<Type> return_type;

        std::string pretty() const;
    };

    std::optional<Result> demangle(std::string_view mangled);
};
```

### 13.3 Testing Strategy

- **Round-trip tests**: mangle → demangle → compare to original symbol.
- **Golden files**: record mangled outputs for a fixed set of inputs; detect unintended ABI changes.
- **Fuzzing**: generate random well-formed symbols, mangle, demangle, confirm round-trip.
- **Negative tests**: malformed mangled strings should return errors, not crash.
- **Variadic-specific**:
  - Typed and untyped variadic round-trip correctly.
  - Variadic in final position parses; variadic not in final position is rejected.
  - Receiver + fixed params + variadic compose correctly.
  - A typed variadic `Ei` and fixed `int` followed by `E` do not collide in decoding.

---

## 14. Comparison to Itanium C++ ABI

| Feature                  | Itanium C++                    | Cryo                           |
| ------------------------ | ------------------------------ | ------------------------------ |
| Prefix                   | `_Z`                           | `C$`                           |
| Length-prefix idents     | Yes                            | Yes                            |
| Substitution compression | Yes (`S_`, `S0_`, ...)         | **No**                         |
| Separator discipline     | Multiplexed (`N`, `E`, digits) | One-role-per-char              |
| Hand-readable            | Difficult                      | Feasible                       |
| Symbol length            | Shorter                        | Longer                         |
| Demangler complexity     | High                           | Low (~300 LOC)                 |
| Variadic encoding        | `z` sentinel (untyped only)    | `E` / `E<T>` (typed + untyped) |

Cryo trades symbol length for readability and implementation simplicity. The primary cost is increased `.rodata`/symbol-table size in large binaries, which is acceptable for a language where developer experience is a priority.

**Note on `E` collision with Itanium:** Itanium C++ uses `E` as a *scope-close* marker (paired with `N`). Cryo's grammar doesn't need a scope-close token — `N{...}` uses explicit braces — so `E` is free for variadic use here. The two schemes are not interoperable anyway (different prefixes), so no confusion at the linker level.

---

## 15. Future Extensions

Reserved for future revisions of this spec:

- **Versioned ABI**: a `V{n}` suffix on the mangled name to allow multiple ABI versions to coexist.
- **Effects / async**: encoding async functions and effect rows once the language supports them.
- **Const generics**: encoding compile-time constant generic parameters.
- **Variance annotations**: if the type system gains variance on generic parameters.
- **Generic variadic (tuple-like packs)**: if Cryo gains template parameter packs (`<Ts...>`), a distinct encoding (likely `Ep<TypeList>`) will be needed.

---

## Appendix A: Quick Reference Card

```
PREFIX         C$
KIND TAGS      #vt #ti #ct #dt #op #mi #cl #tr
PATH SEP       .
MEMBER SEP     ::
GENERIC ARGS   < T1,T2,... >
PARAMS START   ~
RETURN START   !
RECEIVER       &   (&m for mut)       -- first param only
POINTER        P<T>
REFERENCE      R<T>
ARRAY          A<T>
NAMED TYPE     N{Path}
VARIADIC       E<T>  (typed T...)     -- last param only
               E     (untyped ...)    -- last param only
EMPTY PARAMS   v

PRIMS  i8=a  i16=s  i32/int=i  i64=l
       u8=h  u16=t  u32=j      u64=m
       f32=f f64=d  bool=b     char=c
       string=S    void=v      unit=u
```

---

*End of specification.*