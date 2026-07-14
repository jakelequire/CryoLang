# Cryo Name Mangling Specification

**Version:** 1.0.0
**Status:** Normative.
**Implementation:** `compiler/src/compiler/resolver/mangled_name.cryo` (encoder),
`compiler/src/compiler/resolver/demangler.cryo` (decoder).

---

## 1. Scope

This document specifies how Cryo encodes a fully-qualified, post-monomorphization
symbol into a single linker identifier. Encoding is deterministic: identical input
produces identical output within a spec version, independent of compilation order.

The scheme has one hard constraint that governs every character choice:

> Every mangled symbol is a legal **LLVM unquoted identifier**, matching
> `^[-a-zA-Z$._][-a-zA-Z$._0-9]*$`.

Consequences:

- Separators are drawn only from `A-Z a-z 0-9 $ . _ -`.
- `$` is reserved for structural markers and never appears in an identifier body.
- Symbols appear verbatim in IR, `nm`, `objdump`, and linker diagnostics - no
  `@"..."` quoting is ever required.

Symbols declared `extern "C"` are **not** mangled: the source name passes through to
the linker verbatim. Any input that does not begin with `C$` is, by definition,
unmangled, and the decoder returns it as such rather than failing.

---

## 2. Grammar

```ebnf
MangledName = "C$" ( TraitImpl | Tagged | Plain ) ;

TraitImpl   = "tr$" Path "$f" Path Signature ;                 (* trait-impl method *)

Tagged      = ( "ct" | "dt" | "op" ) "$" Path Signature        (* ctor / dtor / operator *)
            | ( "vt" | "ti" | "mi" ) "$" Path                  (* vtable / type-info / module-init *)
            | "cl" "$" Path Overload ;                         (* compiler closure *)

Plain       = Path [ Signature ] [ Overload ] ;                (* functions, methods, data symbols *)

Path        = Segment { "." Segment } [ "-" Segment ] ;        (* "." namespaces, "-" type member *)
Segment     = Ident [ "$L" TypeList "$G" ] ;                   (* optional generic argument list *)
Ident       = Length Bytes ;                                   (* Length-prefixed, see section 5 *)
Length      = Digit { Digit } ;                                (* decimal UTF-8 byte count *)

Signature   = "$F" ParamList "$R" Type ;
ParamList   = "v" | Param { "_" Param } [ "$V" ] ;             (* "v" = empty; "$V" = variadic tail *)
Param       = "$s" | "$m" | Type ;                            (* "$s"/"$m" only as the first param *)

TypeList    = Type { "_" Type } ;
Type        = Prim
            | "P" Type                                         (* pointer    *)
            | "R" Type                                         (* reference  *)
            | "A" Type                                         (* array/slice*)
            | "F$L" ParamList "$G" Type                        (* function   *)
            | "N$L" Path [ "$L" TypeList "$G" ] "$G" ;         (* named [generic] type *)

Overload    = "$O" Length ;
Digit       = "0" ... "9" ;
```

The grammar is unambiguous with two-character lookahead at `$`-marker positions: a
path `Segment` always begins with a decimal digit (its length prefix), whereas every
structural marker and kind code begins with `$` or a letter, so the decoder never
backtracks.

---

## 3. Token Alphabet

| Token  | Role                                                        |
|--------|-------------------------------------------------------------|
| `C$`   | Cryo-symbol prefix; begins every mangled name               |
| `.`    | Namespace / path-segment separator                          |
| `-`    | Type-member separator (a method's owning type from its name)|
| `$L`   | Generic / named-type argument-list open                     |
| `$G`   | Generic / named-type argument-list close                    |
| `_`    | Separator inside an argument list or parameter list         |
| `$F`   | Parameter-list start                                         |
| `$R`   | Return-type marker                                           |
| `$s`   | Receiver `&this` (immutable), first parameter only          |
| `$m`   | Receiver `mut &this` (mutable), first parameter only        |
| `$f`   | Trait-`for` separator (between trait path and impl path)    |
| `$V`   | Variadic marker; trailing, after the last fixed parameter   |
| `$O`   | Overload / index suffix, followed by decimal digits         |

Type-position prefixes (a single leading letter, never preceded by `$`):

| Prefix | Role                                   |
|--------|----------------------------------------|
| `P`    | Pointer - `P` + pointee                |
| `R`    | Reference - `R` + referent             |
| `A`    | Array / slice - `A` + element          |
| `N`    | Named type - `N$L` Path [args] `$G`    |
| `F`    | Function type - `F$L` params `$G` ret  |

The structural return marker `$R` is always preceded by `$`; the reference prefix `R`
never is. They are not confusable.

---

## 4. Kind Tags

A kind tag is a two-letter code placed immediately after `C$` and followed by `$`
(yielding, e.g., `C$ct$...`). It marks symbols that are not plain functions. The
trailing `$` separates the tag from the path, whose first segment always begins with
a digit.

| Tag    | Symbol                  | Shape                        |
|--------|-------------------------|------------------------------|
| `ct`   | Constructor             | `C$ct$` Path Signature       |
| `dt`   | Destructor              | `C$dt$` Path Signature       |
| `op`   | Operator overload       | `C$op$` Path Signature       |
| `tr`   | Trait-impl method       | `C$tr$` Trait `$f` Path Signature |
| `vt`   | Vtable                  | `C$vt$` Path                 |
| `ti`   | Type info / RTTI        | `C$ti$` Path                 |
| `mi`   | Module initializer      | `C$mi$` Path                 |
| `cl`   | Compiler-generated closure | `C$cl$` Path `$O` Index   |

Plain functions, methods, global variables, and named-type symbols carry no kind tag.

---

## 5. Paths and Identifiers

A path is a chain of length-prefixed identifiers joined by `.` for namespace nesting,
with an optional final `-` segment naming a member of the preceding type:

```
Math::Vector::Vec2          ->  4Math.6Vector.4Vec2
Rect.area  (method)         ->  4Rect-4area
```

Each identifier is encoded as its **UTF-8 byte length** in decimal followed by the raw
bytes (`<length><bytes>`). Lengths count bytes, not code points, so Unicode identifiers
require no escaping: `café` (5 bytes) -> `5café`. Length-prefixing also makes embedded
`_` unambiguous, so identifiers need no escape rules of their own.

A constructor's and destructor's member segment duplicates the owning type's leaf name
(e.g. `C$ct$3Dog-3Dog...`).

---

## 6. Generic Argument Lists

Generic arguments are written `$L` Type {`_` Type} `$G` and attach to the segment they
specialize:

```
Pair<int>                   ->  4Pair$Li$G
Pair<int>::new              ->  4Pair$Li$G-3new
Math::Vec2<int>             ->  4Math.4Vec2$Li$G
```

Each distinct monomorphization therefore produces a distinct symbol.

---

## 7. Signatures

Function-like symbols carry a signature: `$F` ParamList `$R` ReturnType. Data symbols
(vtables, type info, module initializers, globals, named-type labels) omit it entirely.

- An empty parameter list is the literal `v`.
- A non-static method encodes its receiver as the first parameter: `$s` for `&this`,
  `$m` for `mut &this`. These markers are valid only in first position; a reference in
  any other position uses the `R` prefix.
- Remaining parameters are encoded types joined by `_`.
- A variadic function appends `$V` after its last fixed parameter (at least one fixed
  parameter is required before `$V`).

```
noop() -> void                      ->  C$4noop$Fv$Rv
Rect.scale(mut &this, int) -> void  ->  C$4Rect-5scale$F$m_i$Rv
printf(string, ...) -> int          ->  C$6printf$FS$V$Ri
```

---

## 8. Type Encoding

### 8.1 Primitive codes

| Type        | Code | | Type      | Code |
|-------------|------|-|-----------|------|
| `i8`        | `a`  | | `u8`      | `h`  |
| `i16`       | `s`  | | `u16`     | `t`  |
| `i32` / `int` | `i` | | `u32`     | `j`  |
| `i64`       | `l`  | | `u64`     | `m`  |
| `f32`       | `f`  | | `f64`     | `d`  |
| `boolean`   | `b`  | | `char`    | `c`  |
| `string`    | `S`  | | `void`    | `v`  |
| `()` unit   | `u`  | |           |      |

Codes are single lowercase letters except `S` (string), uppercased to avoid clashing
with `s` (`i16`). Additional rules:

- `int` is pinned to 32 bits and collapses to `i32`'s code `i`.
- `i128` and `u128` collapse to the 64-bit codes `l` and `m` respectively.
- `never` has no dedicated code and maps to `v`.

### 8.2 Composite types

| Cryo type            | Encoding                                | Example                          |
|----------------------|-----------------------------------------|----------------------------------|
| Pointer `T*`         | `P` + `T`                               | `int*` -> `Pi`                    |
| Reference `&T`       | `R` + `T`                               | `&Vec2` -> `RN$L4Vec2$G`          |
| Array / slice        | `A` + element                           | `int[]` -> `Ai`                   |
| Named type           | `N$L` Path `$G`                         | `Math::Vec2` -> `N$L4Math.4Vec2$G`|
| Generic instance     | `N$L` Path `$L` args `$G$G`             | `Pair<int>` -> `N$L4Pair$Li$G$G`  |
| Optional `T?`        | `N$L8Optional$L` T `$G$G`               | `int?` -> `N$L8Optional$Li$G$G`   |
| Tuple `(T0, T1, ...)`  | `N$L5Tuple$L` T0 `_` T1 ... `$G$G`        | `(i32, i32)` -> `N$L5Tuple$Li_i$G$G` |
| Function `fn(...)->R`  | `F$L` params `$G` R                     | `fn(int,int)->int` -> `F$Li_i$Gi` |

Notes:

- `A` encodes only the element type; an array's length is not part of the mangling.
- `Optional` and tuples are encoded as named generics over the reserved names
  `Optional` and `Tuple`, so distinct element shapes never share a symbol.
- A generic parameter that survives to mangling (a resolution leak) is encoded as a
  named type over its parameter name, making the leak visible in IR rather than
  silently colliding.

---

## 9. Operator Codes

For `op` symbols the member name is replaced by a canonical code:

| Operator | Code  | | Operator   | Code   |
|----------|-------|-|------------|--------|
| `+`      | `add` | | `==`       | `eq`   |
| `-`      | `sub` | | `!=`       | `ne`   |
| `*`      | `mul` | | `<`        | `lt`   |
| `/`      | `div` | | `<=`       | `le`   |
| `%`      | `mod` | | `>`        | `gt`   |
| `&`      | `band`| | `>=`       | `ge`   |
| `\|`     | `bor` | | `&&`       | `land` |
| `^`      | `xor` | | `\|\|`     | `lor`  |
| `<<`     | `shl` | | `!`        | `lnot` |
| `>>`     | `shr` | | `[]`       | `idx`  |
| `()`     | `call`| | unary `-`  | `neg`  |

---

## 10. Overload Suffix

`$O` followed by a decimal index disambiguates symbols that would otherwise collide.
It is used by the closure encoding (`cl`) for the closure index, and is available to
distinguish function overloads whose signatures alone do not (e.g. overloads differing
only in return type).

```
C$3foo$Fi$Ri          first foo
C$3foo$Fi$RS$O1       second foo, index 1
```

---

## 11. Data Symbols

Data symbols carry no signature. Their forms:

| Symbol                     | Encoding                          | Example                       |
|----------------------------|-----------------------------------|-------------------------------|
| Vtable                     | `C$vt$` Path                      | `C$vt$6Animal`                |
| Type info                  | `C$ti$` Path                      | `C$ti$6Animal`                |
| Module initializer         | `C$mi$` Path                      | `C$mi$4Math.6Vector`          |
| Closure                    | `C$cl$` Path `$O` Index           | `C$cl$8identity$O0`           |
| Global / constant          | `C$` Path                         | `Math::PI` -> `C$4Math.2PI`    |
| Named-type (LLVM type name)| `C$` Path                         | `Math::Vec2` -> `C$4Math.4Vec2`|
| Generic-instance label     | `C$` Path-with-leaf-generics      | `Pair<int>` -> `C$4Pair$Li$G`  |

A root-namespace global with no enclosing namespace takes the bare `C$<name>` form
(e.g. `C$3foo`). Module initializers are linked into a global init list run before
`main`.

---

## 12. Examples

| Source                                              | Mangled |
|-----------------------------------------------------|---------|
| `Hello::main() -> int`                              | `C$5Hello.4main$Fv$Ri` |
| `Util::add(a: int, b: int) -> int`                  | `C$4Util.3add$Fi_i$Ri` |
| `Rect.area(&this) -> int`                           | `C$4Rect-4area$F$s$Ri` |
| `Rect::new(w: int, h: int) -> Rect`                 | `C$4Rect-3new$Fi_i$RN$L4Rect$G` |
| `Pair<int>::new(int, int) -> Pair<int>`             | `C$4Pair$Li$G-3new$Fi_i$RN$L4Pair$Li$G$G` |
| `Math::Vector::Vec2.dot(&this, Vec2) -> f64`        | `C$4Math.6Vector.4Vec2-3dot$F$s_N$L4Math.6Vector.4Vec2$G$Rd` |
| `identity<int>(x: int) -> int`                      | `C$8identity$Li$G$Fi$Ri` |
| `write(buf: int*, len: int) -> int`                 | `C$5write$FPi_i$Ri` |
| `Dog::Dog(name: string)`                            | `C$ct$3Dog-3Dog$FS$Rv` |
| `Dog::~Dog(&this)`                                  | `C$dt$3Dog-3Dog$F$s$Rv` |
| `Vec3 + Vec3 -> Vec3` (operator)                    | `C$op$4Vec3-3add$F$s_N$L4Vec3$G$RN$L4Vec3$G` |
| `impl Display for Dog { fmt(&this) -> void }`       | `C$tr$7Display$f3Dog-3fmt$F$s$Rv` |
| `extern "C" puts(string) -> int`                    | `puts` (unmangled) |

---

## 13. Stability Guarantees

Within a single spec version:

- Changing a function's parameters or return type changes its mangled name.
- Adding, removing, or changing generic arguments changes the mangled name.
- Renaming a namespace, type, or member changes the mangled name.
- Reordering items within a file does not change any mangled name.
- Identical input always produces identical output, independent of compilation order
  or compiler build.

`extern "C"` symbols are exempt: their linker name is the source name and is fixed by
the foreign ABI, not by this scheme.

---

## 14. LLVM Disambiguator Suffix

LLVM may append `.` followed by a decimal integer to a symbol name when it resolves a
collision at module-link time. A correctly mangled Cryo symbol never collides, so this
suffix should not appear in practice. The decoder nonetheless tolerates an optional
trailing `.<decimal>`, exposing it separately rather than rejecting the input, so it
remains robust against partially linked IR dumps.
