# Cryo Name Mangling Specification

**Version:** 0.2 
**Status:** Implemented in `compiler/src/compiler/resolver/`

---

## 1. Overview

This document specifies the Cryo name mangling scheme used to encode
fully-qualified symbol names into linker-compatible identifiers. Compared
to v0.1, this revision has a single hard constraint driving every
character choice:

> **Every mangled symbol must be a legal unquoted LLVM identifier.**

LLVM's unquoted identifier grammar is `[-a-zA-Z$._][-a-zA-Z$._0-9]*`.
That means the entire separator alphabet is drawn from
`a-z A-Z 0-9 $ _ . -` - no angle brackets, commas, tildes, bangs,
ampersands, octothorpes, braces, or at-signs. Symbols appear directly
in IR (`@C$foo$Fv$Rv`) with no `@"..."` quoting.

Design goals, in priority order:

1. **LLVM-unquoted.** No character ever forces IR quoting.
2. **Unambiguous.** Every mangled name decodes to exactly one source-level symbol.
3. **Demangler-friendly.** Parseable by a small, linear state machine.
4. **Stable.** Deterministic. Monomorphization and ABI changes produce distinct symbols.
5. **Readable under pressure.** Recognizable in stack traces and linker
   errors without running a demangler. Length is explicitly not optimized.

FFI symbols declared `extern "C"` are **not** mangled and pass through
to the linker verbatim.

---

## 2. Design Principles

1. **`C$` prefix** marks every Cryo-mangled symbol. `$` is the only
   non-alphanumeric character load-bearing in the scheme and is reserved
   for structural markers.
2. **Structural markers are two-character tokens** of the form `$X`
   where `X` is a single ASCII letter. This is the one syntactic form
   the demangler needs to recognize for structure; everything else is
   either a length-prefixed identifier, a primitive code, or a type
   prefix letter.
3. **Length-prefixed identifiers** handle arbitrary user identifiers
   (digits, underscores, unicode) without escape rules.
4. **Separator-per-role** - each structural token has exactly one
   meaning. No multiplexing, no context-sensitive tokens.
5. **No substitution compression.** Readability over symbol length.
   Modern linkers handle long names.
6. **Deterministic.** Same input always produces the same output,
   independent of compilation order.

---

## 3. Separator Alphabet

| Token | Role                                         | Where it appears |
|-------|----------------------------------------------|------------------|
| `C$`  | Cryo symbol prefix                           | Start only |
| `$XX$`| Kind tag (`vt`, `ti`, `ct`, `dt`, `op`, `mi`, `cl`, `tr`) | Immediately after `C$`, trailing `$` separates tag from path |
| `.`   | Namespace / path segment separator           | Between path segments |
| `-`   | Type-member separator (method belongs to type) | Between owner type segment and its member |
| `$L`  | Generic / named-type list open (replaces `<`) | Around type lists |
| `$G`  | Generic / named-type list close (replaces `>`) | Around type lists |
| `_`   | Type-list separator (replaces `,`)           | Inside `$L ... $G` and parameter lists |
| `$F`  | Parameter list start (replaces `~`)          | After path, before params |
| `$R`  | Return type marker (replaces `!`)            | After parameter list |
| `$s`  | Self receiver - immutable `&this`            | First parameter of a non-static method |
| `$m`  | Self receiver - mutable `mut &this`          | First parameter of a mutating method |
| `$f`  | Trait `for` separator                        | Between trait name and implementing type in `#tr` symbols |
| `$O`  | Overload disambiguator                       | Trailing suffix followed by decimal digits |
| `$V`  | Variadic marker (trailing)                   | After the last fixed parameter type |

Type prefixes in type position (never preceded by `$`):

| Prefix | Role |
|--------|------|
| `P`    | Pointer - `P<T>` |
| `R`    | Reference - `R<T>` |
| `A`    | Array / slice - `A<T>` |
| `N`    | Named type - `N$L<Path>$G` |
| `F`    | (Reserved) Function type - `F$L<ParamList>$G<ReturnType>` |
| `K`    | (Reserved) Closure type - `K$L<ParamList>$G<ReturnType>` |

> **Note:** `P`, `R`, `A`, `N`, `F`, `K` only ever start a type.
> `$R` (structural return marker) is always preceded by `$` and is
> not confusable with `R` (reference prefix).

---

## 4. Grammar

```
MangledName   = "C$" [ KindTag ] Path Signature [ Overload ] ;

KindTag       = "$" KindCode "$" ;
KindCode      = "vt" | "ti" | "ct" | "dt" | "op" | "mi" | "cl" | "tr" ;

Path          = Segment { "." Segment } [ "-" MemberSegment ]
              | TraitPath ;                    (* only valid under #tr *)
TraitPath     = Segment "$f" Segment { "." Segment } [ "-" MemberSegment ] ;

Segment       = Ident | GenericSegment ;
GenericSegment= Ident "$L" TypeList "$G" ;
MemberSegment = Ident | GenericSegment ;

Ident         = Length RawChars ;
Length        = Digit { Digit } ;              (* decimal, length in bytes *)
RawChars      = <exactly `Length` bytes of UTF-8> ;

Signature     = [ "$F" ParamList "$R" ReturnType ] ;   (* data symbols omit this *)
ParamList     = "v" | Param { "_" Param } [ "$V" ] ;
Param         = "$s" | "$m" | Type ;          (* $s/$m only valid as first param *)
ReturnType    = Type ;

TypeList      = Type { "_" Type } ;

Type          = PrimType
              | "P" Type                       (* pointer *)
              | "R" Type                       (* reference *)
              | "A" Type                       (* array/slice *)
              | "N$L" Path "$G"                (* named type *)
              | "F$L" ParamList "$G" Type      (* reserved: fn type *)
              | "K$L" ParamList "$G" Type      (* reserved: closure type *)
              ;

PrimType      = "a" | "s" | "i" | "l"          (* signed ints i8..i64 *)
              | "h" | "t" | "j" | "m"          (* unsigned ints u8..u64 *)
              | "f" | "d"                      (* f32, f64 *)
              | "b" | "c" | "S" | "v" | "u" ;  (* bool, char, string, void, unit *)

Overload      = "$O" Digit { Digit } ;
Digit         = "0" | "1" | ... | "9" ;
```

---

## 5. Primitive Type Codes

Identical to v0.1. Primitive codes are single lowercase letters except
`S` (string, uppercase to avoid clashing with `s` = i16).

| Cryo Type  | Code | | Cryo Type  | Code |
|------------|------|-|------------|------|
| `i8`       | `a`  | | `u8`       | `h`  |
| `i16`      | `s`  | | `u16`      | `t`  |
| `i32`/`int`| `i`  | | `u32`      | `j`  |
| `i64`      | `l`  | | `u64`      | `m`  |
| `f32`      | `f`  | | `f64`      | `d`  |
| `boolean`  | `b`  | | `char`     | `c`  |
| `string`   | `S`  | | `void`     | `v`  |
| `()` unit  | `u`  | |            |      |

> **Note on `int`:** `int` and `i32` collapse to the same code `i`.
> The spec pins `int` to 32 bits. If `int` ever becomes
> target-dependent, this collapse must be revisited - it is an ABI
> decision, not a cosmetic one.

---

## 6. Composite Type Encoding

| Cryo Type            | Mangled Form                     | Example |
|----------------------|----------------------------------|---------|
| Pointer `T*`         | `P` + `<T>`                      | `int*` → `Pi` |
| Reference `&T`       | `R` + `<T>`                      | `&Vec2` → `RN$L4Vec2$G` |
| Array / slice `[T]`  | `A` + `<T>`                      | `Array<int>` → `AN$L5Array$Li$G$G` |
| Named type           | `N$L` + `<Path>` + `$G`          | `Math::Vec2` → `N$L4Math.4Vec2$G` |
| Generic instance     | Path with `$L`-`$G` on segment   | `Pair<int>` → `N$L4Pair$Li$G$G` |
| Receiver `&this`     | `$s`                             | - |
| Receiver `mut &this` | `$m`                             | - |
| Function type        | `F$L` + `<Params>` + `$G` + `<R>`| (reserved) |
| Closure type         | `K$L` + `<Params>` + `$G` + `<R>`| (reserved) |

Receiver markers (`$s`, `$m`) **only** appear as the first parameter
of a non-static method. References in any other position use `R<T>`.

---

## 7. Special Kind Tags

Kind tags appear only immediately after `C$`, framed as `$XX$` where
`XX` is a two-letter code. The trailing `$` disambiguates the tag from
a path segment (path segments always begin with a decimal digit).

| Tag    | Meaning                | Example |
|--------|------------------------|---------|
| `$vt$` | Vtable for a type      | `C$vt$6Animal` |
| `$ti$` | Type info / RTTI       | `C$ti$6Animal` |
| `$ct$` | Constructor            | `C$ct$3Dog-3Dog$FS$Rv` |
| `$dt$` | Destructor             | `C$dt$3Dog-3Dog$F$s$Rv` |
| `$op$` | Operator overload      | `C$op$4Vec3-3add$F$s_N$L4Vec3$G$RN$L4Vec3$G` |
| `$mi$` | Module initializer     | `C$mi$4Math.6Vector` |
| `$cl$` | Closure                | `C$cl$8identity$O0` |
| `$tr$` | Trait implementation   | `C$tr$7Display$f3Dog-3fmt$F$s$Rv` |

### Operator Codes

For `$op$`, the method name is replaced with a canonical short code
(unchanged from v0.1):

| Operator | Code | | Operator | Code |
|----------|------|-|----------|------|
| `+`      | `add`| | `==`     | `eq` |
| `-`      | `sub`| | `!=`     | `ne` |
| `*`      | `mul`| | `<`      | `lt` |
| `/`      | `div`| | `<=`     | `le` |
| `%`      | `mod`| | `>`      | `gt` |
| `&`      | `band`| | `>=`    | `ge` |
| `\|`     | `bor`| | `&&`     | `land` |
| `^`      | `xor`| | `\|\|`   | `lor`  |
| `<<`     | `shl`| | `!`      | `lnot` |
| `>>`     | `shr`| | `[]`     | `idx`  |
| `()`     | `call`| | unary `-` | `neg` |

---

## 8. Worked Examples

### 8.1 Free Function
```
namespace Hello;
function main() -> int
→ C$5Hello.4main$Fv$Ri
```

### 8.2 Function with Parameters
```
namespace Util;
function add(a: int, b: int) -> int
→ C$4Util.3add$Fi_i$Ri
```

### 8.3 Struct Instance Method
```
// Rect.area(&this) -> int
→ C$4Rect-4area$F$s$Ri
```

### 8.4 Struct Static Method
```
// Rect::new(w: int, h: int) -> Rect
→ C$4Rect-3new$Fi_i$RN$L4Rect$G
```

### 8.5 Mutating Method
```
// Rect.scale(mut &this, factor: int) -> void
→ C$4Rect-5scale$F$m_i$Rv
```

### 8.6 Generic Struct Instantiation
```
// Pair<int>::new(a: int, b: int) -> Pair<int>
→ C$4Pair$Li$G-3new$Fi_i$RN$L4Pair$Li$G$G
```
Each monomorphization produces a distinct symbol:
```
// Pair<string>::new(a: string, b: string) -> Pair<string>
→ C$4Pair$LS$G-3new$FS_S$RN$L4Pair$LS$G$G
```

### 8.7 Nested Namespaces
```
// Math::Vector::Vec2.dot(&this, other: Vec2) -> f64
→ C$4Math.6Vector.4Vec2-3dot$F$s_N$L4Math.6Vector.4Vec2$G$Rd
```

### 8.8 Class Virtual Method
```
// Animal.speak(&this) -> void  (virtual)
→ C$6Animal-5speak$F$s$Rv
```
Associated vtable: `C$vt$6Animal`
Associated type info: `C$ti$6Animal`

### 8.9 Constructor / Destructor
```
// Dog::Dog(_name: string)
→ C$ct$3Dog-3Dog$FS$Rv

// Dog::~Dog(&this)
→ C$dt$3Dog-3Dog$F$s$Rv
```

### 8.10 Inherited Method (Override)
```
// Dog overrides Animal.speak
→ C$3Dog-5speak$F$s$Rv
```
Each class gets its own symbol. Virtual dispatch happens via the vtable
at runtime, not via name mangling.

### 8.11 Generic Function
```
// identity<int>(x: int) -> int
→ C$8identity$Li$G$Fi$Ri
```

### 8.12 Operator Overload
```
// Vec3.operator+(&this, other: Vec3) -> Vec3
→ C$op$4Vec3-3add$F$s_N$L4Vec3$G$RN$L4Vec3$G
```

### 8.13 Trait Implementation
```
// impl Display for Dog { fn fmt(&this) -> void }
→ C$tr$7Display$f3Dog-3fmt$F$s$Rv
```

### 8.14 Pointer Parameters
```
// function write(buf: int*, len: int) -> int
→ C$5write$FPi_i$Ri
```

### 8.15 Complex Nested Generics
```
// Result<Option<int>, string>::unwrap(&this) -> Option<int>
→ C$6Result$LN$L6Option$Li$G$G_S$G-6unwrap$F$s$RN$L6Option$Li$G$G
```

### 8.16 Variadic Function
```
// function printf(fmt: string, ...) -> int
→ C$6printf$FS$V$Ri
```
`$V` is a marker, not a type. It always appears immediately after the
last fixed parameter and immediately before `$R`. A variadic function
with **only** a variadic tail (no fixed params) is not currently
supported by the spec; at least one fixed parameter must precede `$V`.

### 8.17 Closure (compiler-generated)
```
// Closure #0 inside `identity`
→ C$cl$8identity$O0
```

### 8.18 Function / Closure Type in Type Position (Reserved)
```
// fn(int, int) -> int - reserved encoding
→ F$Li_i$Gi

// |int| -> int as a closure type - reserved encoding
→ K$Li$Gi
```
These forms are reserved. The spec guarantees that no other form
collides with `F` or `K` as the leading character of a type.

### 8.19 FFI Pass-through
```
extern "C" { function puts(s: string) -> int; }
→ puts   (unmangled)
```

---

## 9. Mangling Algorithm

### 9.1 High-Level Procedure
```
mangle(symbol):
    out = "C$"

    if symbol is special-kind:
        out += "$" + kind_code(symbol) + "$"

    if symbol is trait_impl:
        out += encode_segment(symbol.trait)
        out += "$f"
        out += encode_path(symbol.impl_path)    # Dog-3fmt etc.
    else:
        out += encode_path(symbol.path)

    if symbol is function-like:
        out += "$F" + encode_params(symbol.params)
        out += "$R" + encode_type(symbol.return_type)

    if symbol.overload_index is not None:
        out += "$O" + str(symbol.overload_index)

    return out
```

### 9.2 Path Encoding
```
encode_path(path):
    segs = [encode_segment(s) for s in path.namespace_segments]
    result = ".".join(segs)
    if path.has_type_owner:
        result += "-" + encode_segment(path.member)
    return result

encode_segment(seg):
    s = str(byte_len(seg.name)) + seg.name
    if seg.has_generic_args:
        s += "$L" + "_".join(encode_type(a) for a in seg.args) + "$G"
    return s
```

### 9.3 Parameter Encoding
```
encode_params(params):
    if params is empty and not variadic:
        return "v"

    parts = []
    for i, p in enumerate(params.fixed):
        if i == 0 and p.is_receiver:
            parts.append("$m" if p.is_mut else "$s")
        else:
            parts.append(encode_type(p.type))
    out = "_".join(parts)
    if params.is_variadic:
        out += "$V"
    return out
```

### 9.4 Type Encoding
```
encode_type(t):
    if t is primitive:    return PRIM_CODE[t]
    if t is pointer:      return "P" + encode_type(t.pointee)
    if t is reference:    return "R" + encode_type(t.referent)
    if t is array:        return "A" + encode_type(t.element)
    if t is named:        return "N$L" + encode_path(t.path) + "$G"
    if t is fn_type:      return "F$L" + encode_params(t.params) + "$G" + encode_type(t.ret)
    if t is closure_type: return "K$L" + encode_params(t.params) + "$G" + encode_type(t.ret)
    error("unknown type")
```

---

## 10. Demangling Algorithm

The demangler is a linear state machine with two-character lookahead
at structural-marker positions (`$X`). No backtracking.

### 10.1 State Machine
```
demangle(s):
    expect(s, "C$")

    kind = None
    if peek2(s) == "$" + one of KINDS:
        expect(s, "$")
        kind = read_chars(s, 2)
        expect(s, "$")

    if kind == "tr":
        trait = parse_segment(s)
        expect(s, "$f")
        path = parse_path(s)
    else:
        path = parse_path(s)

    result = {kind: kind, trait: trait if kind=="tr" else None, path: path}

    if peek2(s) == "$F":
        advance(s, 2)
        result.params = parse_params(s)
        expect(s, "$R")
        result.return_type = parse_type(s)

    if peek2(s) == "$O":
        advance(s, 2)
        result.overload = read_number(s)

    # Optional LLVM disambiguator suffix - consumed but preserved separately.
    if peek(s) == "." and rest_is_digits(s):
        advance(s, 1)
        result.llvm_suffix = read_number(s)

    expect_eof(s)
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
    if peek(s) == "-":
        advance(s, 1)
        member = parse_segment(s)

    return {segments: segments, member: member}

parse_segment(s):
    length = read_number(s)
    name = read_chars(s, length)
    generics = None
    if peek2(s) == "$L":
        advance(s, 2)
        generics = parse_type_list(s)
        expect(s, "$G")
    return {name: name, generics: generics}
```

### 10.3 Parameter Parsing
```
parse_params(s):
    if peek(s) == "v" and peek2(s) != "vX":    # 'v' alone = empty list
        advance(s, 1)
        return {fixed: [], variadic: False}

    fixed = [parse_param_or_receiver(s, first=True)]
    while peek(s) == "_":
        advance(s, 1)
        fixed.append(parse_param_or_receiver(s, first=False))

    variadic = False
    if peek2(s) == "$V":
        advance(s, 2)
        variadic = True

    return {fixed: fixed, variadic: variadic}

parse_param_or_receiver(s, first):
    if first and peek2(s) in ("$s", "$m"):
        is_mut = peek2(s) == "$m"
        advance(s, 2)
        return Receiver(is_mut)
    return parse_type(s)
```

### 10.4 Type Parsing
```
parse_type(s):
    c = peek(s)
    if c in PRIM_CODES:
        advance(s, 1)
        return prim_type(c)
    if c == "P": advance(s, 1); return Pointer(parse_type(s))
    if c == "R": advance(s, 1); return Reference(parse_type(s))
    if c == "A": advance(s, 1); return Array(parse_type(s))
    if c == "N":
        advance(s, 1)
        expect(s, "$L")
        path = parse_path(s)
        expect(s, "$G")
        return NamedType(path)
    if c == "F":
        advance(s, 1); expect(s, "$L")
        params = parse_params(s); expect(s, "$G")
        return FnType(params, parse_type(s))
    if c == "K":
        advance(s, 1); expect(s, "$L")
        params = parse_params(s); expect(s, "$G")
        return ClosureType(params, parse_type(s))
    error("unknown type prefix at " + position(s))
```

### 10.5 Pretty Printing

The demangler output can be rendered back to Cryo-like syntax:

```
C$4Math.6Vector.4Vec2-3dot$F$s_N$L4Math.6Vector.4Vec2$G$Rd
  → Math::Vector::Vec2.dot(&this, Math::Vector::Vec2) -> f64
```

---

## 11. Integration with Debug Info

Use LLVM's `DIBuilder` to expose both forms:

- `linkageName` - the mangled symbol (e.g., `C$4Rect-4area$F$s$Ri`)
- `name` - the pretty-printed form (e.g., `Rect.area(&this) -> int`)

Debuggers (GDB, LLDB) and profilers (perf, VTune) will automatically
show the human-readable form while the linker uses the mangled form.
This is the same pattern C++ uses with DWARF.

```cpp
DIBuilder.createFunction(
    scope,
    prettyName,       // "Rect.area(&this) -> int"
    mangledName,      // "C$4Rect-4area$F$s$Ri"
    file, line, type, /*...*/);
```

---

## 12. Edge Cases and Rules

### 12.1 Empty Parameter Lists
A function with no parameters uses a literal `v` for the param list:

```
function noop() -> void   →   C$4noop$Fv$Rv
```

### 12.2 Variadic Functions
Variadics are marked by a trailing `$V` on the parameter list. The
marker always appears **after** the last fixed parameter and **before**
`$R`. At least one fixed parameter is required.

```
printf(fmt: string, ...) -> int   →   C$6printf$FS$V$Ri
```

### 12.3 Length Prefixes and UTF-8
Length prefixes count **bytes**, not code points. Unicode identifiers
are supported but their length is their UTF-8 byte length.

```
identifier "café" (5 bytes in UTF-8)   →   5café
```

### 12.4 Reserved Identifier Characters
The following characters must not appear in Cryo identifiers (enforced
by the lexer): `.`, `-`, `$`, `_` (leading), plus anything outside the
UTF-8 identifier set. If any of these ever becomes legal (e.g., via
raw identifiers), the mangler must escape them via a `Q<hexlen>_<hex>_`
wrapper. Internal `_` in identifiers is fine - length-prefixing makes
it unambiguous.

### 12.5 Overload Disambiguation
Cryo does not currently support ad-hoc function overloading, but if
added, the `$O<N>` trailing marker resolves collisions where parameter
lists alone cannot (e.g., overloads differing only in return type, if
ever permitted).

```
C$3foo$Fi$Ri          (first foo)
C$3foo$Fi$RS$O1       (second foo, disambiguator index 1)
```

### 12.6 Anonymous / Generated Symbols
Compiler-generated symbols (closures, lambdas, synthetic thunks) use
the `$cl$` kind tag with the enclosing path and an index:

```
C$cl$8identity$O0    (first closure inside `identity`)
```

### 12.7 Module Initializers
Each namespace that requires initialization at program startup gets a
module initializer symbol:

```
C$mi$4Math.6Vector
```

These are linked into a global init list processed before `main`.

### 12.8 LLVM Name Uniquification Suffix
LLVM appends `.N` (a decimal integer) to symbol names when it detects
collisions at module-link time. **Correctly mangled Cryo symbols must
never collide**, so this suffix should never appear on a Cryo-produced
symbol in practice. The demangler, however, must tolerate an optional
trailing `\.\d+` and expose it as a separate `llvm_suffix` field rather
than failing the parse. This makes the demangler robust against
partially-linked IR dumps and unexpected collisions during compiler
development.

### 12.9 `extern "C"` Pass-Through
Symbols declared `extern "C"` are emitted with their source name,
**unmangled**. The mangler must refuse to produce `C$`-prefixed output
for such declarations. The demangler must return "unmangled" rather
than error for any input that doesn't start with `C$`.

### 12.10 Stability Guarantees
- Changing a function's signature (params or return type) always
  changes its mangled name.
- Adding or removing generic parameters always changes the mangled name.
- Renaming a namespace always changes the mangled name.
- Reordering items within a file never changes mangled names.
- The scheme must produce identical output for identical input across
  compiler versions within the same spec version.

---

## 13. Implementation Notes

### 13.1 Mangler
The mangler lives in `compiler/src/compiler/resolver/mangled_name.cryo`
and replaces the current underscore-joined scheme. It takes a
fully-resolved, post-monomorphization symbol (where all generics are
concrete types) and produces a string.

Suggested Cryo-side interface:
```
struct Mangler { ... }

implement Mangler {
    static function mangle(sym: &Symbol) -> string;
    static function mangle_vtable(cls: &ClassType) -> string;
    static function mangle_type_info(ty: &Type) -> string;
    static function mangle_ctor(cls: &ClassType, ctor: &Ctor) -> string;
    static function mangle_dtor(cls: &ClassType) -> string;
    static function mangle_operator(op: OperatorKind, on: &Type, args: &ParamList, ret: &Type) -> string;
    static function mangle_trait_impl(tr: &TraitRef, on: &Type, method: string, sig: &Signature) -> string;

    // Pass-through for extern "C":
    static function extern_c(name: string) -> string;  // returns `name` unchanged
}
```

### 13.2 Demangler
The demangler lives alongside the mangler as a sibling module so the
compiler can pretty-print its own symbols for diagnostics. It should
also be exposed as a standalone CLI (`cryo-demangle`) taking stdin /
argv and emitting pretty form to stdout.

```
struct Demangler { ... }
struct DemangleResult {
    kind_tag: Option<string>,
    trait_path: Option<Path>,
    path: Path,
    params: Option<ParamList>,
    return_type: Option<Type>,
    overload: Option<u32>,
    llvm_suffix: Option<u32>,
}

implement Demangler {
    static function demangle(s: string) -> Option<DemangleResult>;
    static function pretty(r: &DemangleResult) -> string;
}
```

### 13.3 Testing Strategy
- **Round-trip tests.** mangle → demangle → compare to original symbol.
- **Golden files.** Record mangled outputs for a fixed set of inputs;
  detect unintended ABI changes across commits.
- **Fuzzing.** Generate random well-formed symbols, mangle, demangle,
  confirm round-trip.
- **Negative tests.** Malformed mangled strings should return `None`,
  not crash.
- **LLVM-legality test.** Every mangled output must match the regex
  `^[-a-zA-Z$._][-a-zA-Z$._0-9]*$`. Enforce this in CI so no future
  change silently introduces a quoted-symbol regression.

### 13.4 Migration from v0.1 / Current Scheme
The current mangler joins qualified names with `__` and
produces names like `Compiler__AST__FunctionAnnotation`. The migration
path:

1. Land the new factory behind a flag (`--mangling=v2`).
2. Monomorphizer cache keys include the spec version so v1 and v2
   caches cannot cross-contaminate during development.
3. Flip the default once the new scheme is validated against the
   stdlib + self-hosted compiler test suite.
4. Remove the v1 path after one release cycle.

Symbol-table consumers (`decl_index.mangled_names`,
`context.declare_extern_function_*`, `ir_generator.get_named_global`)
are opaque string-keyed; they require no changes as long as
registration and lookup use the same mangler.

---

## 14. Comparison

| Feature                    | Itanium C++ ABI | Cryo v0.1 | Cryo v0.2 (this doc) |
|----------------------------|-----------------|-----------|----------------------|
| Prefix                     | `_Z`            | `C$`      | `C$`                 |
| Length-prefixed idents     | Yes             | Yes       | Yes                  |
| Substitution compression   | Yes (`S_`, `S0_`, …) | No  | No                   |
| Separator discipline       | Multiplexed (`N`, `E`, digits) | One-role-per-char | Two-char `$X` tokens, one-role-per-token |
| Hand-readable              | Difficult       | Feasible  | Feasible             |
| **LLVM unquoted**          | Yes             | **No** (uses `<>,~!&#{}@`) | **Yes** |
| Symbol length              | Shorter         | Longer    | Longer still         |
| Demangler complexity       | High            | Low (~300 LOC) | Low (~300 LOC) |

Relative to v0.1, v0.2 trades a small amount of syntactic density
(`$L`/`$G` vs. `<`/`>`, `-` vs. `::`, `$F`/`$R` vs. `~`/`!`) for the
guarantee that no symbol ever requires LLVM to emit `@"..."` quotes.
In exchange, every mangled name appears verbatim in IR, `objdump`,
`nm`, stack traces, and linker errors - the readability story that
motivated v0.1 in the first place is preserved, and the
grep/tooling story is strictly better.

---

## 15. Future Extensions

Reserved for future revisions of this spec:

- **Versioned ABI.** A `$Vn` suffix (n ≥ 0) to allow multiple ABI
  versions to coexist on the same system. Conflicts syntactically with
  the variadic marker `$V`; if adopted, variadic will move to a
  different token (`$E` for ellipsis is the leading candidate).
- **Effects / async.** Encoding async functions and effect rows once
  the language supports them. Likely a `$e` section between `$R<ret>`
  and `$O`.
- **Const generics.** Encoding compile-time constant generic
  parameters. Likely as a new segment kind inside `$L ... $G` type
  lists.
- **Variance annotations.** If the type system gains variance on
  generic parameters.
- **Full function / closure types.** Currently reserved as `F` and
  `K` type prefixes; the encoding is fixed but code generation is not
  yet wired up.

---

## Appendix A: Quick Reference Card

```
PREFIX         C$
KIND TAGS      $vt$ $ti$ $ct$ $dt$ $op$ $mi$ $cl$ $tr$
PATH SEP       .
MEMBER SEP     -
GENERIC OPEN   $L
GENERIC CLOSE  $G
TYPELIST SEP   _
PARAMS START   $F
RETURN START   $R
RECEIVER       $s  (&this)      $m  (mut &this)
TRAIT FOR      $f
OVERLOAD       $O<N>
VARIADIC       $V   (trailing, after last fixed param)
EMPTY PARAMS   v

POINTER        P<T>
REFERENCE      R<T>
ARRAY          A<T>
NAMED TYPE     N$L <Path> $G
FN TYPE        F$L <Params> $G <Ret>   (reserved)
CLOSURE TYPE   K$L <Params> $G <Ret>   (reserved)

PRIMS  i8=a  i16=s  i32/int=i  i64=l
       u8=h  u16=t  u32=j      u64=m
       f32=f f64=d  bool=b     char=c
       string=S    void=v      unit=u

LLVM IDENT RULE   ^[-a-zA-Z$._][-a-zA-Z$._0-9]*$
```
