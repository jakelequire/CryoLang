# The Cryo Language Reference

> **Version:** 1.0.0 \
> **Last revised:** May 2026

Cryo is a statically-typed, compiled systems language. It targets native machine code through LLVM 20, has a self-hosted compiler, and ships a standard library written entirely in itself. Three principles shape the language:

1. **Explicitness.** Cryo has no implicit conversions and no hidden control flow. The programmer writes out every cast and every loop condition. Type inference is deliberately local — a binding may adopt its initialiser's type, but there is no flow- or program-level inference — so the cost of every operation stays visible in the source and the code is easy to reason about.

2. **One toolchain.** Build, run, test, fetch, init, and check are subcommands of a single `cryo` binary. The package manager, the test runner, and the dependency resolver ship with the compiler.

3. **Pay for what you use.** Cryo provides single-inheritance classes with virtual dispatch *and* a trait system with monomorphic dispatch. Use a class when the program needs runtime polymorphism over a heterogeneous collection; use a trait when you want compile-time abstraction.

The rest of this document is organised by feature area. Examples are runnable against the current compiler; anything aspirational is marked.

---

## Table of Contents

- [1. Lexical Structure](#1-lexical-structure)
- [2. Type System](#2-type-system)
- [3. Variables and Constants](#3-variables-and-constants)
- [4. Functions](#4-functions)
- [5. Operators](#5-operators)
- [6. Control Flow](#6-control-flow)
- [7. Pattern Matching](#7-pattern-matching)
- [8. Structs](#8-structs)
- [9. Classes](#9-classes)
- [10. Enums](#10-enums)
- [11. Traits](#11-traits)
- [12. Generics](#12-generics)
- [13. Implement Blocks](#13-implement-blocks)
- [14. Modules and Imports](#14-modules-and-imports)
- [15. Pointers and Memory](#15-pointers-and-memory)
- [16. Ownership, Copy, and Drop](#16-ownership-copy-and-drop)
- [17. Directives and Attributes](#17-directives-and-attributes)
- [18. Foreign Function Interface](#18-foreign-function-interface)
- [19. The Standard Library](#19-the-standard-library)
- [20. Testing](#20-testing)
- [21. Reserved Syntax](#21-reserved-syntax)
- [22. Grammar Summary](#22-grammar-summary)
- [23. Project Configuration (cryoconfig)](#23-project-configuration-cryoconfig)
- [24. Command-Line Interface](#24-command-line-interface)

---

## 1. Lexical Structure

This section describes the building blocks the lexer recognises before any syntactic or semantic meaning is assigned.

### 1.1 Identifiers

An identifier begins with a letter (`a`-`z`, `A`-`Z`) or underscore, followed by any sequence of letters, digits, and underscores.

```
identifier = letter { letter | digit | "_" }
```

The compiler does not enforce naming, but the standard library and ecosystem use the following conventions, which the bundled TextMate grammar and LSP also assume:

- `snake_case` for variables, functions, methods.
- `PascalCase` for types (struct, class, enum, trait, alias).
- `SCREAMING_SNAKE_CASE` for compile-time constants.

### 1.2 Keywords

Keywords are reserved identifiers. They cannot be used as variable, function, or type names.

| Control flow | Declarations |             | Modifiers   | Operator keywords | Special values | Reserved for future use |
| ------------ | ------------ | ----------- | ----------- | ----------------- | -------------- | ----------------------- |
| `if`         | `function`   | `from`      | `const`     | `new`             | `true`         | `yield`                 |
| `else`       | `class`      | `as`        | `mut`       | `delete`          | `false`        | `async`                 |
| `switch`     | `struct`     | `implement` | `static`    | `sizeof`          | `null`         | `await`                 |
| `case`       | `enum`       | `intrinsic` | `public`    | `alignof`         | `this`         | `auto`                  |
| `default`    | `trait`      | `where`     | `private`   | `typeof`          | `This`         | `unsigned`              |
| `match`      | `type`       | `extern`    | `protected` | `in`              |                | `tuple`                 |
| `while`      | `namespace`  |             | `virtual`   | `as`              |                | `optional`              |
| `for`        | `module`     |             | `override`  |                   |                | `with`                  |
| `loop`       | `import`     |             | `inline`    |                   |                |                         |
| `do`         | `export`     |             | `unsafe`    |                   |                |                         |
| `break`      | `static_assert` |          | `move`      |                   |                |                         |
| `continue`   | `union`      |             |             |                   |                |                         |
| `return`     |              |             |             |                   |                |                         |
| `asm`        |              |             |             |                   |                |                         |

`move` marks a closure that captures its environment by move (see [§ 16.3](#163-move-checking)).

Reserved-for-future-use keywords are recognised by the lexer; the parser may accept them in places that have no semantic implementation. See [§ 21](#21-reserved-syntax).

### 1.3 Comments

Cryo recognises four comment styles. Documentation comments are semantically meaningful: they attach to the declaration that follows them and surface in LSP hovers and generated documentation.

```cryo
// Line comment.

/* Block comment.
   Spans multiple lines. */

/// Outer documentation comment (line form). Attaches to the next declaration.
/// Multiple consecutive /// lines are joined.

/** Outer documentation comment (block form). Attaches to the next declaration. */

///! Inner documentation comment. Attaches to the enclosing module/namespace.
```

### 1.4 Literals

#### Numeric Literals

Integer literals support four bases. Underscores are visual separators that the compiler ignores. A type suffix pins the literal to a width; without one, the type is inferred from context (defaulting to `i32` for integers and `f64` for floats).

```cryo
42                // decimal
1_000_000         // separators: identical to 1000000
0xFF              // hex
0b1010            // binary
0o755             // octal
42u64             // typed: unsigned 64-bit
42i8              // typed: signed 8-bit
3.14              // float (defaults to f64)
3.14f32           // explicit 32-bit float
1.0e10            // scientific notation
2.5e-3f64         // scientific with explicit type
```

**Type suffixes:** `u8` `u16` `u32` `u64` `u128` `i8` `i16` `i32` `i64` `i128` `usize` `isize` `f32` `f64`

> **Trap.** Integer literals exceeding `i64::MAX` (e.g. `0xFFFF_FFFF_FFFF_FFFF`) wrap to negative when used inline against a `u64` operand. Hoist the literal into a `const u64 NAME = ...` binding to compare correctly.

#### String and Character Literals

Strings are enclosed in double quotes; characters in single quotes. Both share the same set of escape sequences.

```cryo
"Hello, world!"
"line one\nline two"
'A'
'\n'
'\x41'                     // hex byte: equivalent to 'A'
```

**Escape sequences:** `\n` `\t` `\r` `\0` `\\` `\'` `\"` `\xHH` (hex byte). Raw strings (`r"..."`) and the additional C escapes `\a \b \f \v` are reserved but not yet implemented — see [§ 21](#21-reserved-syntax).

#### f-strings (string interpolation)

An f-string, prefixed with `f`, builds an owned `String` by interpolating
expressions written inside `{...}`:

```cryo
const x: i32 = 42;
const opt: Option<i32> = Option::Some(7);
const s: String = f"x = {x}, opt = {opt:?}";   // "x = 42, opt = Some(7)"
```

- `{expr}` formats `expr` through the `Display` trait; `{expr:?}` formats it
  through `Debug`. Any type implementing the relevant trait works, including
  `Option`, `Result`, and `Array<T>`.
- The embedded expression is a full expression: `f"{a + b}"`, `f"{p.x}"`,
  `f"{m.get(k)}"`.
- `{{` and `}}` produce literal `{` and `}`. Standard escape sequences in the
  literal text are processed as in a normal string.
- The result is a heap-backed `String` the caller owns (and drops). The
  parser desugars the whole f-string to calls into `std::fmt::interp`, which
  is auto-imported into any module that uses one.

For raw, untyped formatted output (C `printf` semantics, `%d`/`%s`
specifiers, not type-checked), use `print` / `println` from `std::fmt`.

#### Boolean and Null Literals

```cryo
true
false
null            // null pointer; valid in any pointer context
```

There is no implicit conversion between `boolean` and integers; `if (1)` is a type error.

---

## 2. Type System

Every value in Cryo has a known type at compile time. A binding's type is either written explicitly or inferred from its initialiser (local inference only — see [§ 3](#3-variables-and-constants)). There are no implicit conversions between numeric types; when you need a conversion, you write it with `as`.

### 2.1 Primitive Types

| Type                          | Description                                                                                                                                      | Size                     |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------ |
| `void`                        | No value; only valid as a return type.                                                                                                           | 0                        |
| `boolean`                     | `true` / `false`. Not interchangeable with integers.                                                                                             | 1 byte                   |
| `char`                        | 8-bit character (byte).                                                                                                                          | 1 byte                   |
| `string`                      | NUL-terminated raw string (`*u8`). FFI-shaped.                                                                                                   | pointer                  |
| `int`                         | Default signed integer; alias for `i32`.                                                                                                         | 4 bytes                  |
| `i8` `i16` `i32` `i64` `i128` | Signed integers of fixed width.                                                                                                                  | 1 / 2 / 4 / 8 / 16 bytes |
| `uint`                        | Default unsigned integer; alias for `u32`.                                                                                                       | 4 bytes                  |
| `u8` `u16` `u32` `u64` `u128` | Unsigned integers of fixed width.                                                                                                                | 1 / 2 / 4 / 8 / 16 bytes |
| `float`                       | Default float; alias for `f64`.                                                                                                                  | 8 bytes                  |
| `f32` `f64`                   | IEEE 754 floats.                                                                                                                                 | 4 / 8 bytes              |
| `double`                      | Alias for `f64`.                                                                                                                                 | 8 bytes                  |
| `usize` `isize`               | Pointer-width unsigned / signed integers — distinct types whose width tracks the target's pointer size (the natural type for sizes and indices). | 8 bytes on 64-bit        |

In performance-sensitive or cross-platform code, prefer the explicit-width forms (`i32`, `u64`, `f64`) so the layout is unambiguous. The shorthand aliases exist for ergonomics.

`string` is a NUL-terminated raw byte pointer matching the C ABI. Inside the standard library, length-typed UTF-8 is modelled by [`Str`](#str) (borrowed) and [`String`](#string) (owned). The translation between them happens at the FFI boundary in `ffi::cstr`.

### 2.2 Pointer Types

A pointer holds a memory address. Pointer types are written by suffixing the pointee with `*`, mirroring the C convention.

```cryo
const p:  int*  = &x;          // pointer to int
const pp: int** = &p;          // pointer to pointer
const v:  void* = malloc(64);  // type-erased
```

Raw pointers are unchecked. Validity, aliasing, and lifetime are the programmer's responsibility. For a pointer that is statically guaranteed non-null, see `core::ptr::NonNull<T>`. For owned heap allocations, prefer `Box<T>`. See [§ 15](#15-pointers-and-memory).

### 2.3 References

References use `&`. They appear principally as method receivers (`&this` for shared access, `mut &this` for exclusive, mutating access) and as function parameter types.

```cryo
&int               // shared reference to int
&mut int           // exclusive reference to int
```

The receiver shape on a method is part of its signature: a `&this` method may not modify the receiver; a `mut &this` method may. Callers see this distinction without reading the body.

### 2.4 Array Types

Two distinct things share the word "array" in Cryo: the **raw array type** (a low-level fixed buffer) and the **growable `Array<T>`** in the standard library.

```cryo
const buf:    int[16];     // raw fixed-size buffer of 16 ints
const dyn:    int[];       // raw dynamic array (FFI / unsized)
mut   v:      Array<int>;  // growable, heap-backed; from collections::array
```

For everything except FFI and stack-allocated scratch buffers, prefer `Array<T>` from the standard library. The shorthand `T[]` desugars to `Array<T>` in expression position when the prelude is loaded.

### 2.5 Function Types

Functions are first-class values. A function type names its parameter types and its return type.

```cryo
(int, int) -> int
(T) -> U
() -> void
```

Function types appear most often as parameter types for higher-order combinators:

```cryo
function apply(f: (int) -> int, x: int) -> int {
    return f(x);
}
```

This is how `Option::map`, `Result::and_then`, and the iterator combinators take their callback.

A function value can be a named function or a **lambda expression**:
`(params) -> Ret { body }`. The body is always brace-delimited; each parameter
and the return type are written out explicitly.

```cryo
const inc: (int) -> int       = (n: int) -> int { return n + 1; };
const add: (int, int) -> int  = (a: int, b: int) -> int { return a + b; };

apply(inc, 41);                                   // 42 (named-as-value)
apply((n: int) -> int { return n * 2; }, 21);     // 42 (inline)
```

A lambda that references a binding from the enclosing scope **captures** it,
becoming a closure. Copy types (i32, u64, bool, char, references, and any
`![derive(Copy)]` type) are captured by value-copy; non-Copy types are
captured by move - the outer binding is consumed at the lambda's
construction site (subsequent use is E0452) and the closure-struct's
synthesized Drop releases the captured value at scope exit. The `move`
keyword stays valid as an explicit prefix (`move (params) -> T { body }`)
but is no longer required for non-Copy captures: any non-Copy capture
implicitly flips the lambda to move semantics. The compiler synthesises an
anonymous struct holding the captured fields plus a `__call__` method whose
body is the lambda body; the closure value is the struct instance and the
call site dispatches directly through `__call__`. Stack-allocated; no heap
allocation.

```cryo
const bias: i32 = 10;
const add_bias = (x: i32) -> i32 { return x + bias; };  // captures `bias`
add_bias(32);                                            // 42
```

A closure can also be passed to a `(Args) -> Ret`-typed parameter; the
compiler specialises the receiver function per concrete closure type so
the body still issues a direct call, never an indirect one. Named
functions and non-capturing lambdas continue to bind to the same
parameter as bare function pointers, with no overhead change:

```cryo
function apply(f: (i32) -> i32, x: i32) -> i32 { return f(x); }

const bias: i32 = 10;
apply((x: i32) -> i32 { return x + bias; }, 32);  // 42 - capturing closure
apply((x: i32) -> i32 { return x * 2; }, 21);     // 42 - non-capturing lambda
apply(tentimes, 4);                               // 40 - named function pointer
```

A combinator that infers a *new* type parameter from the callback's return
type - for example `Option::map<U>` - does so automatically: `U` is bound from
the callback's signature, so the type argument may be omitted. This works the
same whether the callback is a lambda or a named function:

```cryo
const some: Option<int> = Option::Some(5);
const out:  Option<int> = some.map((n: int) -> int { return n * 10; });  // U = int, inferred

function tentimes(n: int) -> int { return n * 10; }
const out2: Option<int> = some.map(tentimes);                            // U = int, inferred
```

The explicit form is still accepted (`some.map<int>(...)`) and is required only
when the type parameter appears *nowhere* the call can infer it from.

### 2.6 Tuple Types

A tuple groups a fixed number of values — of possibly different types — into one
compound value. Tuple types and tuple literals both use parentheses:

```cryo
type Pair   = (int, string);
type Triple = (int, int, int);

const p: (int, string) = (42, "answer");
const x: int    = p.0;     // positional element access: .0, .1, ...
const s: string = p.1;
const y: int    = p[0];    // `t[N]` indexing is equivalent to `t.N`
```

Because parentheses are also used for grouping, the unit type, and function
types, the forms are:

- `()` — the unit type / unit value, which also serves as the empty tuple (§2.7).
- `(T)` / `(expr)` — **grouping**: just `T` / `expr`, *not* a 1-tuple.
- `(T,)` / `(x,)` — a **1-tuple**: the trailing comma distinguishes it from grouping.
- `(T, U)`, `(T, U, V)`, … — 2-, 3-, … element tuples.
- `(A, B) -> R` — a function type, not a tuple (the `->` disambiguates).

Element access is positional with an integer literal — `t.0`, `t.1`, … (or the
equivalent `t[0]`, `t[1]`) — and the index is checked against the tuple's arity
at compile time. Chained access like `t.1.0` works (element 1, then element 0 of
that).

> **Note:** a pre-1.0 bracket spelling, `[T, U]`, was previously accepted in
> type position. It has been removed — a leading `[` in a type is now an error.
> Write `(T, U)`. (The array suffixes `T[]` / `T[N]` are unaffected: they are
> postfix on an existing type, not a leading bracket.)

### 2.7 The Unit Type

The unit type `()` represents "a value that carries no information." It is distinct from `void`:

- `void`: a function produces no value.
- `()`: a function produces a value, but the value has zero meaningful payload.

`()` appears in generic positions where a type parameter is required but no data is needed. The canonical example is `Result<(), Error>` for an operation that either succeeds (with nothing to return) or fails with an error.

### 2.8 Type Aliases

A `type` alias introduces a new name for an existing type. Aliases are transparent: the alias and the original are interchangeable.

```cryo
type Byte           = u8;
type StringResult<T> = Result<T, string>;
type Callback        = (int) -> void;
```

The `ffi` module aliases C types to their Cryo equivalents:

```cryo
type c_int    = i32;
type c_char   = i8;
type c_size_t = u64;
```

### 2.9 Casting with `as`

Cryo never inserts an implicit numeric or pointer conversion. To convert between types, use the `as` keyword. The compiler does not insert range checks for narrowing casts; this is a deliberate choice that keeps the conversion's cost visible.

```cryo
const a: i64 = 42;
const b: i32 = a as i32;          // narrowing: programmer's responsibility
const p: u8* = some_string as u8*; // pointer reinterpretation
```

### 2.10 Optional Types (`T?`)

`T?` is shorthand for `Option<T>`. It is pure sugar - the parser rewrites `T?`
to `Option<T>`, so the two are the *same* type. A `T?` value carries all of
`Option`'s methods (`is_some`, `is_none`, `unwrap_or`, `map`, …) and is freely
assignable to and from `Option<T>`. The suffix works in every type position:
variable, parameter, return type, struct field, and generic argument.

```cryo
const slot: int? = Option::Some(7);   // int?  is  Option<int>
const back: Option<int> = slot;       // interchangeable both ways

function first(xs: int[]) -> int? {   // optional return
    if (xs.length == 0) { return Option::None; }
    return Option::Some(xs[0]);
}

type struct Config {
    port: u16?;                       // optional field
}
```

`T?` nests like any other type argument: `Option<int?>` is
`Option<Option<int>>`.

---

### 2.11 Opaque Types (`implement Trait`)

`implement Trait` in a return type or a variable annotation is an *opaque
type*: it stands for one specific concrete type without naming it. The
compiler infers the real type and uses it everywhere; you simply do not have
to write it.

```cryo
// The concrete iterator (`SliceIter<T>`) never appears in the signature.
iter(&this) -> implement Iterator<T> where T: Copy {
    return SliceIter<T> { ptr: this.ptr, remaining: this.length };
}

// A caller binds the result without naming the concrete type either.
mut it: implement Iterator<i32> = arr.iter();
const n: u64 = it.count();          // trait methods are available
```

Because Cryo monomorphises, this is a purely static, zero-cost construct: an
`implement Iterator<i32>` value *is* the underlying concrete type (here
`SliceIter<i32>`), method calls dispatch statically, and there is no heap
allocation, vtable, or runtime indirection. It corresponds to "return some
single type that implements this trait", **not** to a dynamically-dispatched
trait object. (`implement` is the same keyword used for implement blocks; in
*type position* it introduces an opaque type, while at the *start of a
declaration* it introduces an implement block — the two never overlap.)

**Where it is allowed.** Two positions:

- **Return type** — the concrete type is inferred from the body's first
  `return` expression. That expression must currently be a *struct literal*
  (e.g. `return SliceIter<T> { … }`), which is how every standard-library
  iterator is written.
- **Variable binding** — `mut it: implement Iterator<i32> = expr;`. The type
  is taken from the initialiser, which is then checked to actually implement
  the named trait; a mismatch is `E0200`:

  ```cryo
  mut it: implement Iterator<i32> = some_non_iterator;  // E0200
  ```

  When the initialiser is a **concrete static constructor**
  (`mut it: implement Iterator<i32> = Range<i32>::new(0, 10);`), you can
  re-adapt the local directly — `it.take(3)` specialises the adapter against
  the recovered concrete receiver. The binding's *visible* type is still the
  opaque trait; the compiler recovers the concrete initialiser type for
  combinator specialisation.

  This recovery only applies when the initialiser names its concrete type.
  When the producer itself returns an opaque iterator
  (`mut it: implement Iterator<i32> = arr.iter();`, where `iter` returns
  `implement Iterator<…>`), the concrete cursor is hidden behind that opaque
  return, so the local has no concrete receiver to specialise against — chain
  the combinator on the producing expression instead
  (`arr.iter().take(2).count()`), or bind to the concrete adapter type when you
  need a named local (`mut z: ZipIter<Range<i32>, Range<i32>> = a.zip(b);`).

To accept *any* iterator as a **parameter**, use a generic with a trait bound
instead — that is the tool for the input side:

```cryo
function sum<I>(mut it: I) -> i32 where I: Iterator<i32> { … }
```

**Multiple bounds** combine with `+`: `implement Iterator<i32> + Clone`.

**One concrete type per site.** An opaque return names a single underlying
type — every `return` in the body must produce the same one. Two iterators of
different concrete types cannot be returned from one `implement Trait`
function (that would require a dynamically-dispatched trait object, which Cryo
does not provide).

The trait argument carries the trait's associated item: `Iterator<i32>` is
sugar for `Iterator<Item = i32>`, so a binding annotated `implement
Iterator<i32>` is verified both to implement `Iterator` *and* to have an
actual `Item` of `i32`. A genuine element-type mismatch (e.g. binding an
iterator of `String` to `implement Iterator<i32>`) is rejected as `E0200`.
The cross-check is category-level: it flags a structurally distinct item
(one user struct vs another, struct vs primitive) but stays silent when the
declared and actual items are implicitly inter-convertible (`i32` vs `i64`)
or differ only in representation (`String` vs `String<GlobalAlloc>`).

---

## 3. Variables and Constants

Every variable declaration in Cryo has three parts: a mutability qualifier (`const` or `mut`), a name with an **optional** type annotation, and an optional initialiser. When the annotation is omitted, the binding's type is inferred from its initialiser.

```cryo
const name: string = "Cryo";   // immutable binding
mut counter: int   = 0;        // mutable binding
counter = counter + 1;         // reassignment requires `mut`

const greeting = "hello";      // inferred: string
mut total      = 3 + 4;        // inferred: i32
mut it         = arr.iter();   // inferred: the concrete iterator type
```

**Immutable by default.** A `const` binding cannot be reassigned after initialisation. Mutability is opt-in via `mut`, which makes mutation visible at the declaration site.

```cryo
mut y: int;                     // declared without an initialiser; assigned later
```

**Globals.** Module-level `const` declares a true compile-time constant; module-level `mut` declares mutable global state. Use the latter sparingly.

```cryo
const VERSION:    string = "1.0.0";
mut   g_counter:  u64    = 0;
```

> **Local type inference.** The type annotation may be omitted when an initialiser is present; the binding adopts the initialiser's *concrete* type (`const x = 10;` infers `i32`, `mut p = Point { ... };` infers `Point`). Because the inferred type is the concrete one the initialiser produces — not an erased `implement Trait` — methods on it stay callable, so `mut it = arr.iter(); it.take(3)...` works without naming the iterator type. Inference is purely local: it reads only the initialiser of the same statement, never later uses. A binding with no initialiser therefore still needs an annotation (`mut y: int;`), and an initialiser that yields no value (`void`) cannot be inferred (both are `E0104`). There are still no implicit conversions and no flow- or program-level inference.

---

## 4. Functions

### 4.1 Function Declarations

```cryo
function add(a: int, b: int) -> int {
    return a + b;
}

function greet(name: string) -> void {
    println("Hello, %s!", name);
}

function main() -> int {
    greet("Cryo");
    return 0;
}
```

The return type follows the parameter list, separated by `->`. If you omit it, the function returns `void`. Parameters take the form `name: Type` and may not be inferred.

Functions can be recursive, and there is no forward-declaration requirement: the compiler collects every function signature in a dedicated pass before type-checking any body, so call ordering in source is irrelevant.

### 4.2 Generic Functions

Type parameters appear in angle brackets after the name. They are available throughout the signature and body.

```cryo
function identity<T>(x: T) -> T {
    return x;
}

function swap<T>(a: T*, b: T*) -> void {
    const temp: T = *a;
    *a = *b;
    *b = temp;
}
```

At the call site, supply the concrete type:

```cryo
const n: int    = identity<int>(42);
const s: string = identity<string>("hello");
```

Each call produces a fully specialised version of the function. See [§ 12.6 Monomorphisation](#126-monomorphisation).

To require capabilities of `T` (such as the ability to compare it with `<`), use a `where` clause:

```cryo
function smaller<T>(a: T, b: T) -> T
    where T: Ord {
    return if (a.compare(&b) == Ordering::Less) { a } else { b };
}
```

### 4.3 Variadic Functions

A trailing `...` marks a function as variadic. The signature mirrors C's variadic calling convention, so variadic functions stay ABI-compatible with the `printf`-family at the FFI boundary.

```cryo
// FFI / intrinsic declarations: a bare `...` bucket, no body.
intrinsic function printf(format: string, args...) -> i32;
```

A user-defined variadic function names the bucket (`args...`). The compiler emits `va_start`/`va_end` around the body and binds `args` to the raw `va_list` pointer. Wrap it in a `VaArgs` (`std::core::varargs`, in the prelude) to read typed values without hand-rolling `va_arg`:

```cryo
function sum(count: i32, args...) -> i64 {
    mut va: VaArgs = VaArgs::new(args);
    mut total: i64 = 0;
    for (mut i: i32 = 0; i < count; i++) {
        const v: i64 = va.next();      // ![implicit]: T inferred from `v`'s type
        total += v;
    }
    return total;
}
```

`va.next<T>()` is the explicit form; `va.next()` infers `T` from the expected type at the call site (see [§ 17.x](#directives) on `![implicit]`). `va.as_ptr()` returns the raw `va_list` pointer for forwarding to a C `v*printf` callee - equivalently, pass the original `args` identifier.

Two limits are inherited from C varargs and no wrapper can remove them:

- **Not count-safe.** Nothing records how many arguments were passed or their types; the callee must learn that out of band (a format string, a leading count, a sentinel).
- **Default argument promotions apply.** A variadic call promotes `i8`/`i16`/`boolean` to `i32` and `f32` to `f64`. `VaArg` is therefore implemented only for the promoted scalar set (`i32`, `u32`, `i64`, `u64`, `f64`, `string`); `va.next<i8>()` is a compile error - read it as `i32` and narrow. Pass `i64`-typed values when reading with `next<i64>()`.

### 4.4 Extern Functions

`extern` declares a function whose body is provided by the linker, typically a C library symbol.

```cryo
extern function exit(code: int) -> void;

extern "C" {
    function puts(s: string) -> int;
    function atoi(s: string) -> int;
}
```

See [§ 18](#18-foreign-function-interface) for full FFI semantics, including `extern "C" { ... }` blocks and the `extern module c := "C" { #include <header.h> }` form.

### 4.5 Intrinsic Functions

`intrinsic function` declares a function that the compiler lowers directly to LLVM IR rather than emitting a real call. The standard library uses intrinsics for primitives such as memory operations, formatted printing, and the panic mechanism.

```cryo
intrinsic function malloc(size: u64) -> void*;
intrinsic function free(ptr: void*) -> void;
intrinsic function memcpy(dest: void*, src: void*, count: u64) -> void*;
intrinsic function strlen(str: string) -> u64;
intrinsic function printf(format: string, args...) -> i32;
```

The user-facing `print` / `println` / `eprint` / `eprintln` are *not* intrinsics - they live in `std::fmt` and forward to the variadic `printf` family. Only the raw C-shaped primitives above are intrinsics.

The complete list of intrinsics is the file [`stdlib/core/intrinsics.cryo`](../stdlib/core/intrinsics.cryo). User code does not typically declare its own intrinsics; they are a contract between the standard library and the compiler.

The compiler also expands two source-location pseudo-constants at the call site:

| Constant | Expands to                               |
| -------- | ---------------------------------------- |
| `FILE`   | The current source file path (`string`). |
| `LINE`   | The current line number (`i32`).         |

These are used by `panic`, `assert`, and the testing framework to report failure locations without the caller passing them by hand.

---

## 5. Operators

### 5.1 Arithmetic

| Operator            | Description                             |
| ------------------- | --------------------------------------- |
| `+` `-` `*` `/` `%` | Add, subtract, multiply, divide, modulo |
| `-` (unary)         | Negation                                |
| `++` `--`           | Pre/postfix increment, decrement        |

Integer division truncates toward zero. The prefix forms `++x`, `--x` evaluate to the new value; the postfix forms `x++`, `x--` evaluate to the old value (C semantics).

**Overflow.** Integer arithmetic **wraps** on overflow using two's-complement modular arithmetic, for both signed and unsigned types: the result is reduced modulo 2<sup>N</sup> for an N-bit type. There is no overflow trap and no automatic widening. For example, with `i32`:

```cryo
mut x: i32 = 2147483647;   // i32::MAX
x = x + 1;                 // wraps to -2147483648 (i32::MIN), no trap
```

This is a defined deterministic result, not undefined behavior, but it is *silent*: the language does not insert checks. Code that must detect overflow has to compare against the type's bounds before the operation. (Unsigned wrap is the usual `mod 2`<sup>`N`</sup>; e.g. `0u8 - 1u8 == 255`.)

**Division and modulo by zero** are *not* checked by the compiler and fault at runtime (on typical targets the CPU raises `SIGFPE`); the signed `i32::MIN / -1` / `i32::MIN % -1` cases overflow the result and fault the same way. Guard the divisor when it can be zero.

### 5.2 Comparison

| Operator          | Description                                                  |
| ----------------- | ------------------------------------------------------------ |
| `==` `!=`         | Equal / not equal                                            |
| `<` `>` `<=` `>=` | Ordering comparisons                                         |
| `<=>`             | Three-way comparison (spaceship); negative / zero / positive |

Comparison operators return `boolean`. On numeric types and pointers they emit native instructions; on user-defined types that implement `Eq`/`Ord` they are **overloaded** — `a == b` becomes `a.equals(&b)` and `a < b` becomes `a.compare(&b).is_lt()` (see operator overloading, [§ 11.6](#116-operator-overloading)).

### 5.3 Logical

| Operator | Description                    |
| -------- | ------------------------------ |
| `&&`     | Logical AND (short-circuiting) |
| `\|\|`   | Logical OR (short-circuiting)  |
| `!`      | Logical NOT (unary)            |

### 5.4 Bitwise

| Operator     | Description                                                    |
| ------------ | -------------------------------------------------------------- |
| `&` `\|` `^` | AND / OR / XOR                                                 |
| `~`          | Bitwise NOT (unary)                                            |
| `<<` `>>`    | Left / right shift (arithmetic on signed, logical on unsigned) |

### 5.5 Assignment

Only `mut` bindings can be assigned to.

| Operator                    | Description         |
| --------------------------- | ------------------- |
| `=`                         | Simple assignment   |
| `+=` `-=` `*=` `/=` `%=`    | Compound arithmetic |
| `&=` `\|=` `^=` `<<=` `>>=` | Compound bitwise    |

### 5.6 Other Operators

| Operator       | Description                                                                                                                                   |
| -------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `->`           | Return-type arrow; pointer member access (`p->field`).                                                                                        |
| `=>`           | Pattern-to-body separator inside `match`.                                                                                                     |
| `::`           | Scope resolution: static methods, enum variants, module members.                                                                              |
| `?` `:`        | Ternary conditional.                                                                                                                          |
| `?` (postfix)  | Error propagation ("try"): on a `Result`/`Option`, yields the `Ok`/`Some` payload, else returns the `Err`/`None` from the enclosing function. |
| `??`           | Null-coalescing: `opt ?? fallback` yields a `Some`'s payload, else `fallback` (evaluated only when `opt` is `None`).                          |
| `\|>` `<\|`    | Pipeline: thread a value into a call (`x \|> f(a)` ⇒ `f(x, a)`; `f(a) <\| x` ⇒ `f(a, x)`).                                                    |
| `as`           | Explicit type cast.                                                                                                                           |
| `.`            | Member access.                                                                                                                                |
| `&`            | Address-of (unary).                                                                                                                           |
| `*`            | Dereference (unary).                                                                                                                          |
| `sizeof(T)`    | Compile-time size of `T` in bytes.                                                                                                            |
| `alignof(T)`   | Compile-time alignment of `T` in bytes.                                                                                                       |
| `typeof(expr)` | Compile-time type of `expr`, used in type position (decltype-style).                                                                          |
| `new` `delete` | Heap allocation / deallocation.                                                                                                               |

> **Reserved.** `?.` and `...` in call position are recognised by the lexer but not yet lowered. See [§ 21](#21-reserved-syntax). (The range operators `..` / `..=` *are* fully lowered — see [§ 5.7](#57-operator-precedence).)

**Pipeline (`|>`, `<|`).** The pipeline operators thread a value into a call. `x |> f` is `f(x)`; with an argument list the piped value is **prepended** - `x |> f(a, b)` is `f(x, a, b)`. The backward form **appends** instead - `f(a, b) <| x` is `f(a, b, x)`. Pipes are left-associative, so `x |> f |> g` is `g(f(x))`. They are a compile-time rewrite to an ordinary call, with no runtime cost.

```cryo
const out: int = data |> parse |> validate(strict);   // validate(parse(data), strict)
```

**Null-coalescing (`??`).** `opt ?? fallback` unwraps an `Option<T>` to its `T`, substituting `fallback` when it is `None`. The left operand is evaluated once and `fallback` only when needed. `??` is right-associative and binds looser than the pipes, so a chain reads as `a ?? (b ?? c)`: every operand but the last is an `Option<T>`, and the final `c` is the bare `T`.

```cryo
const port: u16 = config_port() ?? env_port() ?? 8080;
```

**Error propagation (`?`).** A postfix `?` on a `Result<T, E>` evaluates to `T` when the value is `Ok`, and otherwise returns that `Err(e)` unchanged from the enclosing function; on an `Option<T>` it yields `T` for `Some` and returns `None`. The enclosing function's return type must be a matching `Result` / `Option`. It is the concise form of a `match` that re-returns the error.

```cryo
function load(path: string) -> Result<Config, IoError> {
    const text: String = read_file(path)?;     // returns Err(e) on failure
    return Result::Ok(parse_config(text));
}
```

**Type-of (`typeof`).** `typeof(expr)` resolves to the static type of `expr` and is used **in type position** - anywhere a type annotation is expected: variable bindings, pointer/array/optional wrappers, generic arguments, and `as` cast targets. It is a compile-time construct that names a type, not a value, so it cannot appear where a value is expected. `expr` is only type-checked, never evaluated.

```cryo
const x: i32 = read_count();
const y: typeof(x) = 0;        // y : i32
const p: typeof(x)* = &x;      // p : i32*
const n: i64 = 5;
const back = n as typeof(x);   // back : i32
```

### 5.7 Operator Precedence

From **lowest** to **highest**:

| Level | Operators                                                     | Associativity |
| ----- | ------------------------------------------------------------- | ------------- |
| 1     | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=`      | Right         |
| 2     | `??` (null-coalescing)                                        | Right         |
| 3     | `\|>` `<\|` (pipeline)                                        | Left          |
| 4     | `? :` (ternary)                                               | Right         |
| 5     | `..` `..=` (range)                                            | Left          |
| 6     | `\|\|`                                                        | Left          |
| 7     | `&&`                                                          | Left          |
| 8     | `\|`                                                          | Left          |
| 9     | `^`                                                           | Left          |
| 10    | `&`                                                           | Left          |
| 11    | `==` `!=`                                                     | Left          |
| 12    | `<` `>` `<=` `>=` `<=>`                                       | Left          |
| 13    | `<<` `>>`                                                     | Left          |
| 14    | `+` `-`                                                       | Left          |
| 15    | `*` `/` `%`                                                   | Left          |
| 16    | `as`                                                          | Left          |
| 17    | `-` `!` `&` `*` `~` `++` `--` (unary prefix), `new`, `delete` | Right         |
| 18    | `()` `[]` `.` `->` `?` (postfix try) `++` `--` (postfix)      | Left          |

`as` sits between multiplication and unary, so `x * y as i64` casts `y`, not the product. Use parentheses if you mean `(x * y) as i64`.

The range operators `..` / `..=` bind looser than every arithmetic and comparison operator, so `a + 1 .. b * 2` is `(a + 1) .. (b * 2)`. They desugar at parse time to `Range::new` / `RangeInclusive::new` (see [§ 6.3](#63-for-loops)).

---

## 6. Control Flow

### 6.1 If / Else

Conditions are parenthesised and bodies are always braced; there is no single-statement form.

```cryo
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}
```

`if` may also be used as an **expression** that evaluates to the value of the chosen branch. When used this way, both arms are required and must produce the same type.

```cryo
const is_even: boolean = if (n % 2 == 0) { true } else { false };
```

### 6.2 While Loops

```cryo
mut i: int = 0;
while (i < 10) {
    println("%d", i);
    i = i + 1;
}
```

### 6.3 For Loops

A C-style `for` with three components: declare-initialiser, condition, post-update. The loop variable is scoped to the loop body.

```cryo
for (mut i: int = 0; i < 10; i++) {
    printf("%d\n", i);
}
```

`for (x in <expr>)` iterates a sequence. The expression is evaluated exactly once and bound to a hidden mutable local; the parser lowers the loop to a `loop { match (iter.next()) { Some(x) => { ... } None => break; } }`. `break`, `continue`, and `return` inside the body bind to the synthesised loop, and the iterator binding is dropped at the end of the enclosing block.

The scrutinee may be:

- An **`Iterator`** directly — anything exposing `next(mut &this) -> Option<T>`, including the stdlib's `Range<T>` / `RangeInclusive<T>` and any type that `implement trait Iterator<T>`.
- A **range literal** `a..b` (half-open) or `a..=b` (inclusive). These are sugar for `Range::new(a, b)` / `RangeInclusive::new(a, b)`; see the precedence table in [§ 5.7](#57-operator-precedence).
- An **iterable** that exposes `iter()` returning an iterator — `Array<T>` and `Slice<T>` (their `iter()` is gated `where T: Copy`). The lowering inserts the `.iter()` call.
- A **fixed-size array** `T[N]`. The lowering views it as a `Slice<T>` over its `N` elements.

```cryo
mut sum: i32 = 0;
for (i in 0..5) {            // range literal; 0,1,2,3,4
    sum = sum + i;
}
// sum == 10

const xs: i32[3] = [7, 8, 9];
for (x in xs) {              // fixed-size array
    sum = sum + x;
}
```

Range literals are ordinary expressions and may appear anywhere, not only in a `for` header: `const r: Range<i32> = 2..7;`. `..` binds looser than the arithmetic operators, so `a..b + 1` parses as `a..(b + 1)`.

### 6.4 Loop

`loop { ... }` is an unconditional infinite loop. Prefer it over `while (true)`; it communicates "this runs until something inside breaks out" without misdirection.

```cryo
mut count: int = 0;
loop {
    if (count >= 5) { break; }
    printf("%d\n", count);
    count = count + 1;
}
```

### 6.5 Do-While

```cryo
do {
    body();
} while (condition);
```

The body executes at least once, then the condition is checked.

### 6.6 Break and Continue

`break` exits the innermost loop. `continue` skips to the next iteration.

```cryo
for (mut i: int = 0; i < 20; i++) {
    if (i % 2 == 0) { continue; }
    if (i > 10)     { break; }
    println("%d", i);
}
```

### 6.7 Match: Statement Form

`match` is the language's primary discriminator. It branches on enum variants, integer values, and other patterns; the compiler enforces that every case is covered.

```cryo
match (color) {
    Color::Red   => { println("red"); }
    Color::Green => { println("green"); }
    Color::Blue  => { println("blue"); }
}
```

There is no fallthrough between arms. See [§ 7](#7-pattern-matching) for the full pattern language.

### 6.8 Match: Expression Form

Used as an expression, `match` evaluates to the value of the matching arm. All arms must produce the same type.

```cryo
const name: string = match (n) {
    1 => { "one" }
    2 => { "two" }
    _ => { "other" }
};
```

### 6.9 Switch / Case

A traditional `switch` is also available for **integer, `char`, `bool`, and fieldless enum** values - anything compared by value. There is no implicit fallthrough; each case is independent.

```cryo
switch (value) {
    case 1: { println("one");   }
    case 2: { println("two");   }
    default: { println("other"); }
}
```

A `switch` on anything else is a compile error that points you at the right tool: enums whose variants carry payloads, strings, and other structured types require a `match` (which destructures and checks exhaustiveness), and floating-point values require explicit comparisons.

In idiomatic code, prefer `match`; it supports richer patterns and enforces exhaustiveness. `switch` is provided for familiarity and for low-level integer dispatch.

### 6.10 Ternary

```cryo
const abs: int = x >= 0 ? x : -x;
```

The ternary is right-associative: `a ? b : c ? d : e` parses as `a ? b : (c ? d : e)`.

### 6.11 Return

```cryo
function add(a: int, b: int) -> int {
    return a + b;
}
```

`return` exits the current function. If the function has a non-`void` return type, a value is required.

### 6.12 Unsafe Blocks

`unsafe { ... }` is recognised at parse time and lowers identically to a plain block. It serves as a **documentation marker**: a visible signal that the enclosed code performs raw pointer arithmetic, calls `extern` functions, or otherwise sits at the edge of the language's safety story. The compiler does not currently impose any extra restriction outside an `unsafe` block, and does not relax any check inside one - every operation Cryo permits today is permitted everywhere.

```cryo
unsafe {
    const raw: void* = malloc(64);
    // raw pointer manipulation here
}
```

This is the committed 1.0 behavior: `unsafe` is a documentation marker and nothing more. It is **not** reserved to silently become enforcing — 1.0 code will not break under a future release on account of `unsafe`. Should later versions add safety checks around raw pointer dereference, raw-to-pointer `as`-casts, or `extern` calls, they would arrive compatibly (as an opt-in lint/warning first), not as a breaking change to code that already compiles.

### 6.13 Inline Assembly

`asm { ... }` embeds target assembly directly, lowering to an LLVM inline-assembly call. The block body is **raw assembly** — written without string quoting — and Cryo values are bound into it through `${ ... }` operand holes. Bare `{` and `}` are literal assembly text, so target syntax that uses braces (AVX-512 mask registers such as `{k1}`, for instance) passes straight through.

A mandatory `![arch(<arch>, <dialect>)]` directive must appear immediately above the block. It names the target architecture (which *gates* the block — see below) and the assembly dialect (`intel` or `att`):

```cryo
![arch(x86_64, intel)]
asm {
    mov ${=out}, ${in}
}
```

**Operands.** Each `${ ... }` hole binds a Cryo variable. A prefix selects its direction and an optional `:` suffix pins a register or constraint class:

| Form         | Meaning                                          |
| ------------ | ------------------------------------------------ |
| `${x}`       | input — the value of `x` is read into a register |
| `${=x}`      | output — the result is written back to `x`       |
| `${+x}`      | in-out — `x` is both read and written            |
| `${x:"rax"}` | pin the operand to a specific register           |
| `${x:m}`     | memory operand — `x` is addressed in memory      |
| `${x:i}`     | immediate — `x` must be a compile-time constant  |

Referencing the same variable more than once collapses to a single operand, and a variable used as both an input and an output is promoted to in-out. Operands must be register-sized scalars or pointers.

**Clobbers.** Registers, `flags`, or `memory` that the block overwrites but doesn't name as operands are declared with `![clobber(...)]`, so the compiler doesn't assume their values survive the block:

```cryo
![arch(x86_64, intel)]
![clobber(rcx, r11, flags, memory)]
asm {
    mov rax, ${v}
    add rax, rax
    mov ${=out}, rax
}
```

**Outputs and results.** An `asm` block is a statement; values leave it through `${=x}` / `${+x}` operands, and a block may have any number of outputs.

**Dialects.** `intel` is destination-first and prefix-less (`mov rax, 60`); `att` is source-first with `%` registers and `$` immediates (`movq $60, %rax`). The dialect is always stated explicitly in the `![arch(...)]` directive.

**Arch gating.** `<arch>` is matched against the compile target: a block whose arch differs from the target is dropped, exactly like a `![linux]` / `![windows]` gate. This lets per-architecture blocks sit side by side, each written for its own target:

```cryo
![arch(x86_64, intel)]
asm { syscall }

![arch(aarch64, att)]
asm { svc #0 }
```

**Module-level assembly.** An `asm { ... }` written at module scope (outside any function, with no operands) emits module-level inline assembly — for naked/global stubs, `.globl` symbols, or raw data.

A `write` system call on x86_64 Linux, pulling the buffer and length in as operands:

```cryo
![arch(x86_64, att)]
![clobber(rcx, r11, memory)]
asm {
    movq $1, %rax        // SYS_write
    movq $1, %rdi        // fd = stdout
    movq ${buf}, %rsi    // buffer pointer
    movq ${len}, %rdx    // length
    syscall
}
```

> **Note.** LLVM passes the assembly text through to the target assembler unchanged — Cryo does not parse it — so a typo in a mnemonic or register name surfaces as an assembler error at build time, not a Cryo diagnostic. Only `${ ... }` introduces an operand; a literal `$` (an AT&T immediate such as `$60`) and bare `{` / `}` are emitted verbatim.

---

## 7. Pattern Matching

A pattern describes the shape of a value. When a value matches, any variables in the pattern are bound to the corresponding parts.

### 7.1 Pattern Forms

| Pattern             | Syntax                         | Matches                                 |
| ------------------- | ------------------------------ | --------------------------------------- |
| Literal             | `42`, `"hello"`, `true`, `'A'` | Exactly that value.                     |
| Identifier          | `x`                            | Any value; binds it to `x`.             |
| Wildcard            | `_`                            | Any value; discards it.                 |
| Enum (unit)         | `Color::Red`                   | That variant, no payload.               |
| Enum (with payload) | `Shape::Circle(r)`             | That variant; binds the payload to `r`. |
| Range               | `'0'..'9'`                     | Any value in the range (inclusive).     |
| Or                  | `pat \| pat \| pat`            | Any of the listed patterns.             |

### 7.2 Enum Destructuring

```cryo
type enum Shape {
    Circle(f64);
    Rectangle(f64, f64);
    Point;
}

function describe(s: Shape) -> void {
    match (s) {
        Shape::Circle(r)        => { println("Circle r=%f", r); }
        Shape::Rectangle(w, h)  => { println("Rectangle %f x %f", w, h); }
        Shape::Point            => { println("A point"); }
    }
}
```

In each arm, the variables are introduced for the payload of that variant. The compiler enforces that the count and types match the variant's declaration.

If you don't need a payload, use `_`: `Option::Some(_) => { ... }`.

### 7.3 Range Patterns

Range patterns match values within an inclusive range. They are most useful for character classification. Both spellings — `a..b` and the explicit `a..=b` — are **inclusive** in pattern position (note this differs from a range *expression*, where `a..b` is half-open). Bounds must be integer or char literals of the same kind.

```cryo
match (ch) {
    '0'..='9'                   => { println("digit"); }
    'a'..'z' | 'A'..'Z' | '_'   => { println("ident-start"); }
    _                           => { println("other"); }
}
```

### 7.4 Guard Clauses

An arm may carry a **guard**: a boolean condition written `if (cond)` between the pattern and the `=>`. The guard is evaluated only after the pattern matches; if it is false, matching falls through to the next arm. Any bindings introduced by the pattern are in scope inside the guard.

```cryo
match (n) {
    x if (x > 100) => { 3 }
    x if (x > 10)  => { 2 }
    x if (x > 0)   => { 1 }
    _              => { 0 }
}

match (o) {
    Option::Some(v) if (v > 5) => { v * 10; }
    Option::Some(v)            => { v; }
    Option::None               => { -1; }
}
```

The parentheses around the condition are required. A guarded arm does **not** count toward exhaustiveness (the guard could always be false), so a `match` whose only arm for some case is guarded still needs a fall-through arm.

### 7.5 Exhaustiveness

The compiler checks that every possible value of the matched type is covered. Forgetting a variant of an enum is an error. The wildcard `_` is the explicit way to opt in to a default arm.

---

## 8. Structs

A struct is a value type with named fields and (optionally) methods. Structs live on the stack, are passed by value, and are the right choice for plain data.

### 8.1 Declaration

```cryo
type struct Point {
    x: int;
    y: int;
}
```

### 8.2 Fields and Visibility

Struct fields are **public by default** — readable and writable wherever the struct itself is visible. Restrict a field with `private`; a private field is then accessible only from within the declaring type's own methods (enforced as `E0353`), so it is hidden even from free functions in the same module. Visibility blocks group fields that share an access level:

```cryo
type struct Rect {
private:
    cached_area: int;   // only Rect's own methods may touch this
public:
    width:  int;
    height: int;
}
```

Visibility may also be declared per-field with a leading `private` / `public`. Within a struct, only `public:` and `private:` blocks are valid; `protected:` is reserved for classes (where it extends access to subclasses). Class members carry **no default** — every field and method must appear inside an explicit visibility block.

> Field visibility (a `private` *field* → type-scoped, `E0353`) is a different axis from a top-level type being `private` (module-scoped, `E0503` — see [§14.4](#144-visibility)). A `public` struct may have `private` fields, and a `private` struct's fields are public to the rest of its own module.

Fields may declare **default values** with `= <expr>`.

> **Status: reserved — see [§21](#21-reserved-syntax).** The default-value
> syntax parses, but defaults are **not yet applied** at construction: a
> struct literal must still supply every field, and omitting a field that
> has a default is an error (`E0355`). The syntax below is accepted by the
> parser today purely so the eventual feature is forward-compatible.

```cryo
type struct Config {
    debug:   boolean = false;   // default parsed, but NOT yet applied
    verbose: boolean = false;
}

// Today this is an error (E0355: missing fields `debug`, `verbose`);
// when defaults are implemented it will construct Config { false, false }.
const c: Config = Config {};
```

### 8.3 Methods

Methods are functions inside a struct body whose first parameter declares how the method takes the receiver:

- **`&this`**: shared (read-only) borrow. The body may not modify fields.
- **`mut &this`**: exclusive (mutating) borrow. The body may modify fields.
- **`this`** / **`mut this`**: by-value (consuming) receiver. The receiver is *moved* into the method; the caller's value is consumed and may not be used afterward. Use this for a method that dismantles a value — unwrapping it into its parts, or handing its storage off. `mut this` additionally lets the body reassign the receiver binding.

```cryo
type struct Rect {
    width:  int;
    height: int;

    area(&this) -> int {
        return this.width * this.height;
    }

    scale(mut &this, factor: int) -> void {
        this.width  = this.width  * factor;
        this.height = this.height * factor;
    }
}
```

The receiver shape is part of the signature, so a caller knows whether a method borrows or consumes the receiver without reading the body.

#### Struct-destructuring bindings

A `const`/`mut` binding may use a **destructuring pattern** — `{ field, field, ... }` with a type annotation naming the struct — to move a struct's fields out into individually named locals. Each field is moved into a like-named local (field order need not match the declaration). This is the idiomatic companion to a consuming (`this`) receiver: binding the fields marks the receiver as *fully consumed*, so its automatic drop is suppressed and the fields can be handed off without a double free. Any bound field you don't move onward is dropped normally at the end of scope.

```cryo
type struct Box<T, A = GlobalAlloc> {
    ptr:   T*;
    alloc: A;

    // Consume the Box and return its raw pointer, transferring ownership
    // to the caller. Destructuring `this` moves both fields out, so the
    // Box itself is not dropped (that would free `ptr`); `alloc` is dropped
    // at function end (a no-op for GlobalAlloc).
    into_raw(mut this) -> T* {
        const { ptr, alloc }: Box<T, A> = this;
        return ptr;
    }
}
```

### 8.4 Static Methods

A static method belongs to the type itself, not an instance. It is called with `::`.

```cryo
type struct Point {
    x: int;
    y: int;

    static new(x: int, y: int) -> Point {
        return Point { x: x, y: y };
    }

    static origin() -> Point {
        return Point { x: 0, y: 0 };
    }
}

const p: Point = Point::new(10, 20);
```

For structs there is no `new` keyword; `static new(...)` is the idiomatic constructor and simply returns a struct literal.

### 8.5 Struct Literals

A struct literal names each field inside braces. Field order does not need to match declaration order, but every non-defaulted field must be specified.

```cryo
const p: Point = Point { x: 10, y: 20 };
```

### 8.6 Generic Structs

```cryo
type struct Pair<T> {
    first:  T;
    second: T;

    static new(a: T, b: T) -> Pair<T> {
        return Pair { first: a, second: b };
    }

    swap(mut &this) -> void {
        const temp: T = this.first;
        this.first  = this.second;
        this.second = temp;
    }
}

const ints: Pair<int>    = Pair<int>::new(1, 2);
const strs: Pair<string> = Pair<string>::new("hello", "world");
```

`Pair<int>` and `Pair<string>` are independent types. See [§ 12.6](#126-monomorphisation).

### 8.7 Unions

A `type union` is an untagged, C-style union: all fields occupy the **same** storage, overlapping at offset 0. The union's size is that of its largest member and its alignment that of its most-aligned member.

```cryo
type union Value {
    i: i64;
    f: f64;
    bytes: u8;
}
```

Here `sizeof(Value) == 8` — the size of the largest member (`i64`/`f64`), not their sum. Writing one member and reading another *reinterprets* the shared bytes:

```cryo
mut v: Value = Value { i: 0 };
v.f = 1.5;          // writes the 8-byte storage as an f64
const bits: i64 = v.i;   // reads the same bytes back as an i64
```

A union is **untagged**: it carries no discriminant recording which member is active, so reading a member other than the one last written is the programmer's responsibility (the reinterpretation is well-defined; whether it's *meaningful* is not checked). When you want a tagged, exhaustively-checked sum type, use [`type enum`](#10-enums) instead.

**Literals.** A union literal initialises **exactly one** field; naming zero or more than one is a compile error (`E0363`):

```cryo
const ok:  Value = Value { i: 42 };       // OK
// const bad: Value = Value { i: 1, f: 2.0 };  // error E0363: exactly one field
```

**Methods.** Like structs, unions may declare methods inline (instance and `static`), or in an [`implement` block](#13-implement-blocks):

```cryo
type union Tagged {
    raw: i64;
    handle: i64;

    static from_raw(n: i64) -> Tagged { return Tagged { raw: n }; }
    get(&this) -> i64 { return this.raw; }
}
```

**Generics.** Unions may be parameterised:

```cryo
type union Either<A, B> {
    a: A;
    b: B;
}

const e: Either<i64, f64> = Either<i64, f64> { a: 100 };
```

**Layout control.** `![repr(c)]` and `![align(N)]` apply to unions exactly as they do to structs (see [§ 17](#17-directives-and-attributes)).

**Matching.** A union value is not matched variant-wise the way an enum is (there is no discriminant). You `match` on a *member's value* — e.g. `match (v.i) { 0 => ..., _ => ... }` — and `static match (T)` works inside a generic union's methods.

**Ownership.** A union is treated as a plain-data (`Copy`) value: its members are never auto-dropped, since the active member is unknown. If a union owns a resource, give it an explicit `drop` method and that is honoured.

---

## 9. Classes

Classes are heap-allocated reference types with single inheritance, virtual dispatch, and constructor/destructor support. They cover the design space that genuinely benefits from runtime polymorphism (interpreters with heterogeneous AST nodes, GUI frameworks, plugin systems), while structs remain the default for everything else.

### 9.1 Declaration

A class is declared with `type class`. Members live inside visibility blocks (`public:`, `private:`, `protected:`).

```cryo
type class Person {
public:
    name: string;
    age:  i32;

    Person(_name: string, _age: i32) {
        this.name = _name;
        this.age  = _age;
    }

    greet(&this) -> void {
        println("Hi, I'm %s, age %d", this.name, this.age);
    }
}
```

Class instances are always allocated on the heap with `new`, and the result is a pointer:

```cryo
const p: Person* = new Person("Alice", 30);
p.greet();
```

### 9.2 Constructors and Destructors

Constructors share the class name. They are real language constructs that `new` invokes, not a naming convention as on structs.

A destructor is prefixed with `~`, takes no parameters, and runs when the instance is deallocated. It is the right place to release resources acquired in the constructor (heap memory, file handles, sockets), following the RAII pattern familiar from C++.

```cryo
type class Buffer {
public:
    data: u8*;
    size: u64;

    Buffer(_size: u64) {
        this.data = malloc(_size);
        this.size = _size;
    }

    ~Buffer() -> void {
        free(this.data);
    }
}
```

### 9.3 Inheritance

A class may extend exactly one base class. The derived constructor must chain to the base constructor with `: Base(args)`:

```cryo
type class Animal {
public:
    kind: string;

    Animal(_kind: string) { this.kind = _kind; }

    describe(&this) -> void {
        println("Animal: %s", this.kind);
    }
}

type class Dog : Animal {
public:
    name: string;

    Dog(_name: string) : Animal("Dog") {
        this.name = _name;
    }

    bark(&this) -> void {
        println("%s says: Woof!", this.name);
    }
}
```

Single inheritance avoids the diamond problem and the complexity of multiple inheritance. Composition handles the cases multiple inheritance is sometimes used for.

### 9.4 Virtual Methods and Override

`virtual` marks a method whose dispatch is resolved via a vtable at runtime. `override` is required on a derived method that replaces a base virtual; there is no implicit override.

```cryo
type class Shape {
public:
    Shape() {}
    virtual area(&this) -> f64;
    virtual name(&this) -> string {
        return "Shape";
    }
}

type class Circle : Shape {
public:
    radius: f64;

    Circle(r: f64) : Shape() {
        this.radius = r;
    }

    override area(&this) -> f64 {
        return 3.14159 * this.radius * this.radius;
    }

    override name(&this) -> string {
        return "Circle";
    }
}
```

A `virtual` method without a body declares an interface point that derived classes are expected to implement; a `virtual` method with a body provides a default that derived classes may override.

### 9.5 Polymorphic Dispatch

Code written against a base-class pointer dispatches automatically to the actual derived implementation:

```cryo
function print_area(shape: Shape*) -> void {
    println("%s: area = %f", shape.name(), shape.area());
}

function main() -> i32 {
    const c: Circle* = new Circle(10.0);
    print_area(c);   // Circle::name() and Circle::area()
    return 0;
}
```

### 9.6 Structs vs. Classes

|                  | Struct                          | Class                            |
| ---------------- | ------------------------------- | -------------------------------- |
| Allocation       | Stack (value type)              | Heap via `new` (reference type)  |
| Inheritance      | No                              | Single inheritance               |
| Virtual dispatch | No                              | `virtual` / `override`           |
| Receivers        | `&this` / `mut &this`           | `&this` / `mut &this`            |
| Use when         | Plain data, generics, hot paths | Polymorphism, object hierarchies |

**Default to structs.** Reach for a class only when you need inheritance and virtual dispatch.

---

## 10. Enums

Cryo enums are algebraic data types. Variants may be unit or carry a payload. The compiler enforces exhaustive matching.

### 10.1 Unit Enums

```cryo
type enum Color {
    Red;
    Green;
    Blue;
}

const c: Color = Color::Red;
```

Variants are accessed through the enum name with `::`, so `Color::Red` and `TrafficLight::Red` are unambiguous.

Variants may have explicit integer values for FFI or protocol encoding:

```cryo
type enum ErrorCode {
    Ok       = 0;
    NotFound = 404;
    Internal = 500;
}
```

### 10.2 Variants with Payloads

A variant can carry one or more values:

```cryo
type enum Shape {
    Circle(f64);
    Rectangle(f64, f64);
    Point;
}

const s: Shape = Shape::Circle(5.0);
```

A `Shape` value is always exactly one variant; the compiler tracks which one and enforces exhaustive matching.

### 10.3 Generic Enums

Enums can be parameterised over types. The standard library's `Option` and `Result` are the canonical examples:

```cryo
type enum Option<T> {
    Some(T);
    None;
}

type enum Result<T, E> {
    Ok(T);
    Err(E);
}

const v: Option<int>          = Option::Some(42);
const r: Result<string, int>  = Result::Ok("success");
```

Each instantiation is an independent type at the machine level; `Option<int>` and `Option<string>` share no runtime representation.

### 10.4 Methods on Enums

Enums cannot declare methods inline. Methods are added via an `implement` block, which is also how the standard library gives `Option` and `Result` their rich API:

```cryo
implement enum Option<T> {
    is_some(&this) -> boolean {
        match (this) {
            Option::Some(_) => { return true; }
            Option::None    => { return false; }
        }
    }

    unwrap(&this) -> T {
        match (this) {
            Option::Some(value) => { return value; }
            Option::None        => { panic("unwrap on None", FILE, LINE); }
        }
    }
}
```

See [§ 13](#13-implement-blocks) for the full implement-block grammar.

---

## 11. Traits

A trait names a set of methods that a type may implement. Traits are how generic code expresses requirements on its type parameters and how the standard library models capabilities such as equality, ordering, hashing, formatting, and I/O.

### 11.1 Declaring a Trait

```cryo
type trait Eq {
    equals(&this, other: &This) -> boolean;
}
```

Inside the trait body, `This` refers to the implementing type. Methods may have default bodies:

```cryo
type trait Read {
    read(mut &this, buf: u8*, len: u64) -> Result<u64, IoError>;

    /// Default: keep reading until end-of-stream or error.
    read_all(mut &this, out: mut &Array<u8>) -> Result<u64, IoError> {
        // ... default implementation calls self.read in a loop ...
    }
}
```

A trait may inherit from a base trait; implementations of the derived trait must also implement the base:

```cryo
type trait Ord : Eq {
    compare(&this, other: &This) -> Ordering;
}
```

### 11.2 Implementing a Trait

`implement trait <Trait> for <Type> { ... }` provides the method bodies for a concrete type.

```cryo
implement trait Eq for i32 {
    equals(&this, other: &i32) -> boolean {
        return this == *other;
    }
}

implement trait Ord for i32 {
    compare(&this, other: &i32) -> Ordering {
        if (this < *other) { return Ordering::Less;    }
        if (this > *other) { return Ordering::Greater; }
        return Ordering::Equal;
    }
}
```

You may implement a trait for any type defined in the same crate, including primitive types.

### 11.3 Trait Bounds with `where`

A generic parameter is constrained to types that implement specific traits via a `where` clause:

```cryo
function smallest<T>(xs: &Array<T>) -> Option<T>
    where T: Ord + Clone {
    if (xs.length() == 0) { return Option::None; }
    mut best: T = xs.get(0).clone();
    for (mut i: u64 = 1; i < xs.length(); i++) {
        const next: T = xs.get(i).clone();
        if (next.compare(&best) == Ordering::Less) {
            best = next;
        }
    }
    return Option::Some(best);
}
```

Multiple bounds on the same parameter are joined with `+`. Multiple constrained parameters are separated with `,`:

```cryo
where T: Hash + Eq, V: Clone
```

### 11.4 Standard Library Traits

| Trait                       | Purpose                                                                                                    |
| --------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `Copy`                      | Marker for types that are bitwise-copyable (no `Drop` impl).                                               |
| `Drop`                      | Explicit destructor; types implementing it are non-`Copy`.                                                 |
| `Clone`                     | Explicit deep duplication via `clone()`.                                                                   |
| `Default`                   | A canonical zero value: `static default() -> This`.                                                        |
| `Eq`                        | Equality (backs `==` / `!=`; see [§11.6](#116-operator-overloading)).                                     |
| `Ord` (`: Eq`)              | Total ordering via `compare(...) -> Ordering` (backs `< > <= >=`).                                         |
| `Hash`                      | Type-level hashing into a `Hasher`.                                                                        |
| `Add`/`Sub`/`Mul`/`Div`/`Rem`, `Neg`, `BitAnd`/`BitOr`/`BitXor`/`Shl`/`Shr`, `Not`/`BitNot`, `Index`, `Deref` | Operator overloading — see [§11.6](#116-operator-overloading). |
| `Iterator`                  | Lazy sequence with an associated `Item` and `next() -> Option<Item>` (see [§11.5](#115-associated-types)). |
| `IntoIterator`              | Conversion into an iterator.                                                                               |
| `From<T>` / `Into<T>`       | Infallible conversions.                                                                                    |
| `TryFrom<T>` / `TryInto<T>` | Fallible conversions returning `Result`.                                                                   |
| `Read` / `Write`            | Byte-level I/O.                                                                                            |
| `Display` / `Debug`         | Formatting.                                                                                                |
| `FmtWrite`                  | Sink trait for the formatter.                                                                              |
| `Allocator`                 | Heap allocation strategy.                                                                                  |

Every standard-library trait is declared in [`stdlib/core/`](../stdlib/core/) with the exception of `Read`/`Write` (in `stdlib/io/traits.cryo`), `Display`/`Debug`/`FmtWrite` (in `stdlib/fmt/`), and `Allocator` (in `stdlib/alloc/allocator.cryo`).

The collection iterator entry points (`Array::iter`, `HashMap::keys`/`values`, `Str::split`, …) return [`implement Iterator<…>`](#211-opaque-types-implement-trait) rather than naming their concrete cursor structs, so you consume them through the trait and a `for-in` loop without ever spelling the underlying type.

### 11.5 Associated Types

A trait may declare an **associated type** — a type that each implementation
supplies, named once in the trait and referred to by every method. The
standard `Iterator` is written this way:

```cryo
type trait Iterator {
    type Item;                              // each impl chooses its element type
    next(mut &this) -> Option<This::Item>;  // `This::Item` projects it
    // ... defaults (count, fold, map, filter, …) all in terms of This::Item ...
}
```

`This::Item` is a **projection**: inside the trait it stands for "the `Item`
this implementation bound". Projections also work off a generic parameter — a
generic adapter names its source's element as `I::Item`:

```cryo
type struct MapIter<I, O> { inner: I; f: (I::Item) -> O; }
```

**Binding the associated type.** An implementation binds each associated type
in one of two ways:

```cryo
// 1. Positional sugar - `Iterator<i32>` ≡ `Iterator<Item = i32>`. Available
//    only when the trait has no generic params of its own (Iterator's case).
implement trait Iterator<i32> for struct Counter { ... }

// 2. Explicit body form - always available, and required when a value
//    expression rather than sugar is clearer.
implement trait Iterator for struct Counter {
    type Item = i32;
    ...
}
```

The positional form scales to where-clause adapters, whose element flows from
the source: `implement<I, A> trait Iterator<A> for struct TakeIter<I> where I:
Iterator<A>` binds `Item := A := I::Item`.

**Declaration-site bounds.** An associated type may carry a bound
(`type Item: Copy;`). Every impl's concrete binding is checked against it — an
impl whose `Item` does not satisfy the bound is rejected with `E0306`:

```cryo
type trait Seq { type Item: Copy; next_one(&this) -> i32; }
implement trait Seq<NotCopy> for struct Holder { … }   // E0306: Item not Copy
```

**Diagnostics.** Three errors guard the rules:

- `E0306` — an impl binds an `Item` that does not satisfy a declaration-site
  bound (`type Item: Copy;`).
- `E0309` — an impl of a trait that declares an associated type binds none of
  them (no positional arg and no `type Item = …;` body). The projection could
  never reduce, so it is rejected up front.
- `E0310` — an associated type bound *positionally* on a trait that also has
  generic parameters. Positional args fill the declared generic params in
  order, so an associated type of such a trait must be bound with the explicit
  body form (`type Out = …;`).

Because Cryo monomorphises, a projection is fully resolved at compile time:
`MapIter<Range<i32>, i64>::Item` reduces to `i64` with no runtime cost.

### 11.6 Operator Overloading

Operators on a user-defined type desugar, in the compiler, to a call to the
corresponding **operator trait** method. Implement the trait and the operator
works on your type; the rewrite happens during semantic analysis, so there is
no runtime dispatch and the result monomorphises like any other method call.

The rewrite is **type-directed** and **LHS-driven**: `a OP b` dispatches on the
type of `a`, and it only fires when the built-in rule does not already apply.
Primitive arithmetic (`1 + 2`), pointer stepping, and native integer/pointer
comparison keep emitting raw instructions — primitives deliberately do **not**
implement the arithmetic traits. The operator traits live in
[`stdlib/core/ops.cryo`](../stdlib/core/ops.cryo) (`Eq`/`Ord` are in
[`stdlib/core/cmp.cryo`](../stdlib/core/cmp.cryo)).

| Operator(s)                | Trait (`core::ops` / `core::cmp`) | Method / desugar                                 |
| -------------------------- | --------------------------------- | ------------------------------------------------ |
| `+` `-` `*` `/` `%`        | `Add` `Sub` `Mul` `Div` `Rem`     | `a + b` → `a.add(&b)` (etc.)                      |
| `-` (unary)                | `Neg`                             | `-a` → `a.neg()`                                  |
| `&` `\|` `^` `<<` `>>`     | `BitAnd` `BitOr` `BitXor` `Shl` `Shr` | `a & b` → `a.bitand(&b)` (etc.)              |
| `!` `~` (unary)            | `Not` `BitNot`                    | `!a` → `a.not()`, `~a` → `a.bitnot()`            |
| `==` `!=`                  | `Eq`                              | `a == b` → `a.equals(&b)`, `a != b` → `!a.equals(&b)` |
| `<` `>` `<=` `>=`          | `Ord` (`: Eq`)                    | `a < b` → `a.compare(&b).is_lt()` (`is_gt`/`is_le`/`is_ge`) |
| `a[i]`                     | `Index<Idx, Output>`              | `a[i]` → `*(a.index(i))`                          |
| `*a` (deref)              | `Deref<Target>`                   | `*a` → `*(a.deref())`                             |

Each arithmetic/bitwise trait carries `Rhs` and `Output` type parameters, so an
operator can mix types (add a scalar to a vector, shift by a plain integer) and
choose its result type. The right operand is taken **by reference** (`rhs: &Rhs`)
to avoid moving an owned aggregate.

```cryo
type struct Vec2 { x: i64; y: i64; }

implement trait Add<Vec2, Vec2> for struct Vec2 {
    add(&this, rhs: &Vec2) -> Vec2 { return Vec2 { x: this.x + rhs.x, y: this.y + rhs.y }; }
}
implement trait Mul<i64, Vec2> for struct Vec2 {          // scalar on the right
    mul(&this, rhs: &i64) -> Vec2 { return Vec2 { x: this.x * *rhs, y: this.y * *rhs }; }
}
implement trait Neg<Vec2> for struct Vec2 {
    neg(&this) -> Vec2 { return Vec2 { x: -this.x, y: -this.y }; }
}

const a: Vec2 = Vec2 { x: 1, y: 2 };
const b: Vec2 = Vec2 { x: 3, y: 4 };
const c: Vec2 = a + b;      // Vec2 { 4, 6 }
const d: Vec2 = a * 3;      // Vec2 { 3, 6 }
const e: Vec2 = -a;         // Vec2 { -1, -2 }
```

**Equality and ordering.** Implement `Eq` for `==`/`!=` and `Ord` (which extends
`Eq`) for `< > <= >=`. A single `compare` backs all four relational operators
through the `Ordering` predicates:

```cryo
implement trait Eq for struct Vec2 {
    equals(&this, other: &Vec2) -> boolean { return this.x == other.x && this.y == other.y; }
}
implement trait Ord for struct Vec2 {
    compare(&this, other: &Vec2) -> Ordering {
        if (this.x != other.x) { return this.x.compare(&other.x); }
        return this.y.compare(&other.y);
    }
}
const eq: boolean = a == b;   // a.equals(&b)      -> false
const lt: boolean = a < b;    // a.compare(&b).is_lt() -> true
```

**Compound assignment** routes through the same trait: `a += b` evaluates
`a.add(&b)` and stores the result back into `a`. This holds for every binary
arithmetic, bitwise, and shift operator (`+= -= *= /= %= &= |= ^= <<= >>=`).

**Indexing.** `Index<Idx, Output>::index` returns a **pointer** (`Output*`), so
the desugar `a[i]` → `*(a.index(i))` is a *place*: one implementation serves
reads (`v = a[i]`), writes (`a[i] = v`), and compound assignment (`a[i] += v`).
Built-in array/slice/string/pointer indexing keeps its native path.

```cryo
type struct Grid { cells: i32[9]; }
implement trait Index<u64, i32> for struct Grid {
    index(&this, i: u64) -> i32* { return &this.cells[i]; }
}
mut g: Grid = ...;
g[0] = 42;        // *(g.index(0)) = 42
g[0] += 1;        // index called once
```

**Dereference and auto-deref.** `Deref<Target>::deref` also returns a pointer
(`Target*`), so `*b` → `*(b.deref())` is likewise a place (read/write/compound).
Member access **coerces** through `Deref` too: when a receiver lacks a field or
method but implements `Deref`, `b.field` / `b.method()` retry on the pointee
(transitively), inserting the `.deref()` chain for you — the smart-pointer
pattern.

```cryo
type struct Boxed<T> { value: T; }
implement<T> trait Deref<T> for struct Boxed<T> {
    deref(&this) -> T* { return &this.value; }
}
mut b: Boxed<Vec2> = Boxed<Vec2> { value: Vec2 { x: 1, y: 2 } };
const v: Vec2 = *b;   // *(b.deref())
b.x = 10;             // auto-deref: (*b.deref()).x  — b has no field `x`
```

**Reference operands.** A by-reference operand (`&T`, the common
container/parameter case) overloads exactly like a value — `v[i]`, `a + b`,
`-a`, and `a == b` all work when `a`/`v` is a `&T`. In particular `&x == &y`
(where the referent implements `Eq`) compares **by value** (`x.equals(&y)`),
not by pointer identity. A raw pointer `T*` is never unwrapped this way: `p ==
null` and pointer arithmetic stay on the primitive path.

**Reflected (primitive left operand).** When the left operand is a primitive and
the right is a user type, the operator dispatches to a trait implemented **on
the primitive** — `2 * v` calls `(2).mul(&v)` given `implement trait Mul<Vec2,
Vec2> for i64`. This is the one case where the right operand's type pulls in the
impl; `Eq`/`Ord` are excluded (their operands share `This`).

Generic and bounded-param dispatch work throughout: a `T: Add` parameter
overloads `a + b` in a generic body (Phase 2), and the desugar is carried through
monomorphisation so `Container<i32>::index` (etc.) specialises correctly. When a
type lacks the required impl, the compiler names the missing trait (e.g. *"`Vec2`
does not implement `Mul`; add `implement trait Mul ... for Vec2`"*).

---

## 12. Generics

Generics let you write code once and have the compiler specialise it for each concrete type used. The model is **monomorphisation**: at compile time, every distinct instantiation produces a dedicated copy of the code. There is no boxing, no vtable, and no runtime type information.

### 12.1 Type Parameters

Type parameters are declared in angle brackets after the name. By convention they use single uppercase letters: `T` for a general type, `E` for an error type, `K` and `V` for key / value, `A` for an allocator.

### 12.2 Generic Structs

```cryo
type struct Box<T> {
    ptr: T*;

    static new(value: T) -> Box<T> {
        const p: T* = malloc(sizeof(T)) as T*;
        *p = value;
        return Box { ptr: p };
    }

    deref(&this) -> T {
        return *this.ptr;
    }

    drop(mut &this) -> void {
        free(this.ptr);
    }
}
```

A struct can have multiple type parameters, and a parameter can have a default. The standard library uses defaults extensively to make the common case ergonomic:

```cryo
type struct Array<T, A = GlobalAlloc> { /* ... */ }
type struct HashMap<K, V, A = GlobalAlloc> { /* ... */ }
```

Calling `Array<int>::new()` uses `GlobalAlloc`; calling `Array<int, Arena>::new_in(my_arena)` parameterises the container over a custom allocator.

### 12.3 Generic Enums

```cryo
type enum Result<T, E> {
    Ok(T);
    Err(E);
}
```

Each `Result<T, E>` instantiation is a distinct compile-time type.

### 12.4 Generic Functions

```cryo
function identity<T>(x: T) -> T {
    return x;
}

const n: int = identity<int>(42);
```

### 12.5 Generic Implement Blocks

When you add methods to a generic type via `implement`, the block itself is generic. Methods may introduce additional type parameters:

```cryo
implement enum Result<T, E> {
    is_ok(&this) -> boolean {
        match (this) {
            Result::Ok(_)  => { return true;  }
            Result::Err(_) => { return false; }
        }
    }

    map<U>(&this, op: (T) -> U) -> Result<U, E> {
        match (this) {
            Result::Ok(value) => { return Result::Ok(op(value)); }
            Result::Err(err)  => { return Result::Err(err);      }
        }
    }
}
```

`Result`'s parameters `<T, E>` are fixed by the type; `map` introduces an additional `<U>`.

### 12.6 Monomorphisation

When the compiler sees:

```cryo
const a: Pair<int>    = Pair<int>::new(1, 2);
const b: Pair<string> = Pair<string>::new("x", "y");
```

it generates two independent types and two specialised function bodies, one for each instantiation. There is no shared dispatch; every call is a direct call to a fully-typed function. The trade-off is binary size: each instantiation produces its own code.

The pipeline driver lives in [`compiler/src/compiler/types/monomorphizer.cryo`](../compiler/src/compiler/types/monomorphizer.cryo) (Phase 6a in `instance.cryo`), invoked after type resolution and trait-bound validation but before function-body type checking. The follow-on [`compiler/src/compiler/passes/specialization.cryo`](../compiler/src/compiler/passes/specialization.cryo) walks already-typed bodies and rewrites generic call sites to point at the monomorphised callees.

---

## 13. Implement Blocks

`implement` adds methods to a type without modifying its original declaration. There are three forms.

### 13.1 Inherent Implementation

Adds methods to a type. For a struct, an inherent block is interchangeable with declaring the methods inline:

```cryo
implement struct Point {
    distance_to(&this, other: Point) -> f64 {
        const dx: f64 = (this.x - other.x) as f64;
        const dy: f64 = (this.y - other.y) as f64;
        return sqrt(dx * dx + dy * dy);
    }
}
```

Inherent blocks are the **only** way to add methods to enums:

```cryo
implement enum Color {
    to_string(&this) -> string {
        match (this) {
            Color::Red   => { return "red";   }
            Color::Green => { return "green"; }
            Color::Blue  => { return "blue";  }
        }
    }
}
```

### 13.2 Trait Implementation

Provides the method bodies a trait requires for a specific type. Use this to make `T` participate in generic code that has a `where T: SomeTrait` bound.

```cryo
implement trait Eq for i32 {
    equals(&this, other: &i32) -> boolean {
        return this == *other;
    }
}
```

### 13.3 Implement Blocks on Primitives

Implement blocks may target primitive types, which is how the standard library hangs methods off `boolean`, the integer types, and `string`:

```cryo
implement boolean {
    to_i32(&this) -> i32 {
        if (this) { return 1; }
        return 0;
    }

    static default() -> boolean {
        return false;
    }
}
```

After the block is loaded, `my_bool.to_i32()` resolves like any other method call. Resolution is at compile time; no dynamic dispatch.

---

## 14. Modules and Imports

Cryo organises code into modules using a hierarchical namespace system. Every source file declares its namespace, and files reference each other through `import`.

### 14.1 Namespaces

Every Cryo source file begins with a `namespace` declaration that establishes its position in the module hierarchy.

```cryo
namespace MyApp;
namespace MyApp::Utils;
namespace std::collections::array;
```

The namespace serves as the file's identity within the project. The compiler uses it to resolve imports and to mangle symbol names so that two unrelated functions named `parse` from different modules do not collide at link time.

### 14.2 Module Aggregators

A directory of related files uses a `_module.cryo` aggregator to declare which submodules exist and which are public, analogous to Rust's `mod.rs`.

```cryo
// stdlib/collections/_module.cryo
namespace std::collections;

public module collections::raw_buffer;
public module collections::array;
public module collections::str;
public module collections::string;
public module collections::hashmap;
public module collections::hashset;
```

When code imports `std::collections`, only the modules declared `public` in the aggregator are visible.

### 14.3 Imports

```cryo
import Math::Vector;                  // import the module
import Math::Vector::*;               // wildcard: everything public
import Math::Vector as V;             // aliased
import Math::Vector::{ Vec2, Vec3 };  // selective import (brace list)
```

Each `import` declaration imports from a single path. To bring two items from the same module into scope, use the selective brace form (`import M::{A, B};`) or write two separate `import` statements.

Wildcard imports are convenient but can cause name collisions; prefer the brace form or using the module name directly.

### 14.4 Visibility

| Modifier            | Meaning                                                                                  |
| ------------------- | ---------------------------------------------------------------------------------------- |
| *(none)* / `public` | Accessible to any module that imports this one. This is the default for top-level items. |
| `private`           | Accessible only within the same module.                                                  |
| `protected`         | Class-only; accessible to the class and its subclasses.                                  |

Top-level items are **public by default**; mark an item `private` to confine it to its own module. For top-level types (`struct` / `class` / `enum`), `private` is enforced across modules: naming a `private` type from another module — in a type annotation, a struct literal, or a function signature — is rejected with `E0503`. A `private` type remains fully usable within its own module.

This is the mechanism behind hiding iterator engines: a cursor struct can be `private` while its producer returns [`implement Iterator<…>`](#211-opaque-types-implement-trait), so callers consume the sequence through the trait and can never name the concrete type.

```cryo
// engine.cryo
namespace app::engine;
private type struct Cursor { /* … */ }          // hidden outside app::engine
public function scan(...) -> implement Iterator<i32> { return Cursor { ... }; }

// main.cryo
namespace app;
import app::engine;
for (x in scan(...)) { ... }                     // fine — never names Cursor
mut c: Cursor = ...;                             // E0503: `Cursor` is private to `app::engine`
```

---

## 15. Pointers and Memory

Cryo is a systems language. There is no garbage collector. Memory is managed manually through explicit allocation and deallocation, or, preferably, through RAII patterns: a type that owns memory implements `Drop`, and its destructor releases what its constructor acquired.

### 15.1 Address-of and Dereference

The `&` operator takes the address of a value. The `*` operator follows a pointer to its target.

```cryo
mut x: int   = 42;
const ptr: int* = &x;       // ptr holds the address of x
const val: int  = *ptr;     // val is 42
```

Pointer indexing is supported: `ptr[0]` is `*ptr`, and `ptr[n]` accesses the n-th element from the pointer's base.

### 15.2 Heap Allocation

**Low level (`malloc` / `free`)** for raw byte buffers and FFI-shaped allocations:

```cryo
const buf: u8* = malloc(1024) as u8*;
buf[0] = 0xFF;
free(buf);
```

**Class instances (`new` / `delete`):**

```cryo
const p: Person* = new Person("Alice", 30);
delete p;
```

**Array allocation (`new T[n]`):**

```cryo
const arr: int* = new int[100];
```

`new T[n]` allocates room for `n` contiguous `T` and yields a `T*`. The memory is **uninitialized** and no constructors run - it is the typed equivalent of `malloc(n * sizeof(T))`. Free it with `free(arr as void*)`. For constructed, growable, bounds-checked storage prefer `Array<T>`.

**Higher level (`Box<T>` and the collections).** Prefer `Box<T>` over raw `malloc` for owning a single heap value, and prefer `Array<T>` / `String` / `HashMap<K, V>` over manual allocation for collections. They handle reservation, growth, and cleanup, and they implement `Drop`.

### 15.3 Null

`null` is the null pointer literal, valid in any pointer context.

```cryo
const p: int* = null;
if (p == null) { println("null"); }
```

Dereferencing a null pointer is undefined behaviour. For "may be absent" semantics on a value, use `Option<T>` rather than a nullable pointer; the compiler then forces you to handle the absent case at every use.

### 15.4 NonNull

`core::ptr::NonNull<T>` is a thin wrapper around `T*` that statically guarantees non-nullness. Containers and smart pointers in the standard library use `NonNull<T>` internally so they never have to defensively null-check.

```cryo
import core::ptr::NonNull;

const non_null: NonNull<u8> = NonNull::new(buf).unwrap();
```

---

## 16. Ownership, Copy, and Drop

Cryo implements a static ownership model that is enforced at compile time. The model is **deliberately weaker than Rust's**: it is built around three notions (`Copy`, `Drop`, and a flow-sensitive move check). It has no borrow checker, no lifetimes, and does not track aliasing of raw pointers — but the move check is a *hard error*, not a warning: using a value after it has been moved is rejected at compile time (`E0452`, see [§ 16.3](#163-move-checking)).

### 16.1 The `Copy` Trait

A type is `Copy` if it can be duplicated by a bitwise copy of its bytes. The compiler infers `Copy` for:

- All primitive types (integers, floats, `boolean`, `char`, `string` (the raw `*u8` view), pointers, references, function pointers).
- Struct, class, enum, and tuple aggregates **iff** every field is `Copy` **and** the type does not implement `Drop`.
- Generic parameters are conservatively non-`Copy` unless bound by `T: Copy`.

`Copy` is a marker trait declared in `stdlib/core/marker.cryo`; you do not implement it explicitly. Implementing `Drop` automatically makes a type non-`Copy`.

### 16.2 The `Drop` Trait

A type implements `Drop` to attach a destructor:

```cryo
type trait Drop {
    drop(mut &this) -> void;
}

implement trait Drop for Buffer {
    drop(mut &this) -> void {
        free(this.data);
    }
}
```

Implementing `Drop` declares "I own resources that must be released." The compiler **automatically synthesises drop calls at scope exit** for non-`Copy` `const`/`mut` bindings - the analyzer + synthesizer run unconditionally between `MoveCheck` and `TypeLowering`. Drops fire in reverse declaration order at every scope-exit point (block end, early `return`, `break`, `continue`). Manual `binding.drop()` remains valid and idiomatic: the analyzer treats the call as a move, the synthesizer skips bindings already consumed, and a **second** `binding.drop()` (or any other use of the binding after `.drop()`) is rejected as use-after-move (`E0452`).

Auto-drop covers `const x: T = ...` and `mut x: T = ...` declarations. It does **not** yet cover pattern bindings (`match` arms) or members reached by field/index access - explicit `.drop()` is still required in those positions.

### 16.3 Move Checking

Non-`Copy` bindings are tracked by a flow-sensitive analysis ([`passes/move_check.cryo`](../compiler/src/compiler/passes/move_check.cryo)). A *storage-duplicating* move transfers ownership; using the original afterward is a hard error (`E0452`). Move sites are:

- binding a non-`Copy` value to a new name (`const b: T = a;`),
- assigning a non-`Copy` value into a slot (`x = a;`),
- placing a non-`Copy` value into an aggregate literal,
- passing a non-`Copy` value to a function or method parameter that is **not** a reference type (`fn f(t: T)` moves; `fn f(t: &T)` borrows), and
- calling `binding.drop()` or any method whose receiver consumes (`mut this` or `![sink]`).

References (`&T` / `mut &T`) and raw pointers borrow; passing `&x` keeps `x` usable. Reassigning a binding re-initialises it, so `consume(x); x = make();` is legal and `x` is live again after the assignment.

```cryo
const a: Buffer = make_buffer(1024);
const b: Buffer = a;   // moves a -> b
use(a);                // error E0452: use of moved value 'a'
```

Two move/ownership hazards that are unambiguous memory errors, called out as their own hard-error classes for clearer diagnostics, are:

- **Loop-carried move** (`E0452`) - a value moved inside a loop and re-read on the next iteration would be freed twice.
- **Returning the address of a local** (`E0455`) - `return &local;` hands back a pointer into the stack frame that is freed when the function returns. (`return &this` / `return &param` is fine - those are caller-backed.)

Cryo has **no borrow checker**. References and raw pointers are unchecked: aliasing, validity, and lifetimes are the programmer's responsibility, as in C++ (see [§ 2](#2-type-system) and [§ 15](#15-pointers-and-memory)). Move tracking enforces the moved-set above; it is not a full Rust-style soundness boundary.

---

## 17. Directives and Attributes

Directives are compile-time annotations attached to declarations using `![...]` syntax. They modify how the compiler treats the declaration without changing its semantic meaning at the call site.

```cryo
![inline]
function hot_path(x: int) -> int {
    return x * 2;
}

![repr(packed)]
type struct PackedHeader {
    magic:   u32;
    version: u8;
    flags:   u8;
}

![align(16)]
type struct AlignedData {
    data: f64;
}
```

### 17.1 Recognised Attributes

| Directive                                                 | Target                    | Description                                                                                                                                                                                                                                                                                                                                                                                                                 |
| --------------------------------------------------------- | ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `![inline]`                                               | function                  | Inlining hint. *Parsed and validated; not yet emitted as an LLVM attribute (no effect at the default `-O0`).*                                                                                                                                                                                                                                                                                                               |
| `![noinline]`                                             | function                  | Anti-inlining hint. *Parsed and validated; not yet emitted as an LLVM attribute.*                                                                                                                                                                                                                                                                                                                                           |
| `![deprecated]` / `![deprecated("msg")]`                  | any decl                  | Marks a declaration as deprecated. *Parsed and validated; use-site warnings are not yet emitted.*                                                                                                                                                                                                                                                                                                                           |
| `![symbol("name")]`                                       | extern fn / method        | Override the linker symbol used to resolve an extern function or body-less method: Cryo callers use the declared name while the symbol resolved at link time is `name`. Used by the C++ binding generator to pin a decl to its Itanium-mangled symbol.                                                                                                                                                                       |
| `![allow(name)]` / `![warn(name)]` / `![deny(name)]`      | any decl                  | Intended to adjust a named lint's level. *Parsed and validated; lint-level adjustment is not yet implemented.*                                                                                                                                                                                                                                                                                                              |
| `![derive(Trait, …)]`                                     | struct / class / enum     | Auto-derive one or more traits.                                                                                                                                                                                                                                                                                                                                                                                             |
| `![sink]`                                                 | method                    | Marks a method as consuming its receiver, even when the receiver is syntactically `&this` or `mut &this`. Useful for methods that semantically take ownership but want the borrow-style call ergonomics.                                                                                                                                                                                                                    |
| `![implicit]`                                             | generic function / method | Lets a call omit its generic type arguments: the compiler recovers them by unifying the declared return type against the call's *expected* type (the type of the `const x: T = …` it initialises). Every type parameter must appear in the return type. A call with no expected type reports `E0307`; write the arguments explicitly (`f<T>(…)`) there. Used by `VaArgs::next` so `const n: i64 = va.next()` reads cleanly. |
| `![config(<atom>)]` / `![target(<atom>)]` / `![<atom>]`   | any decl                  | Platform / build-flavor gate. `<atom>` is `windows`, `linux`, `macos`, `unix`, or `not(<atom>)`. The bare-atom form (`![windows]`) is sugar for `![config(windows)]`. The decl is stripped from the AST when the gate doesn't match.                                                                                                                                                                                        |
| `![repr(C)]` / `![repr(packed)]` / `![repr(transparent)]` | struct / class / enum     | Memory layout control. See [§ 17.3](#173-memory-layout).                                                                                                                                                                                                                                                                                                                                                                    |
| `![align(N)]`                                             | struct / class / variable | Minimum alignment in bytes; N must be a power of two. See [§ 17.3](#173-memory-layout).                                                                                                                                                                                                                                                                                                                                     |
| `![arch(<arch>, <dialect>)]`                              | asm block                 | Required directly above an `asm { ... }` block (see [§ 6.13](#613-inline-assembly)). Names the target architecture — which *gates* the block, like `![config]` — and the assembly dialect (`intel` or `att`).                                                                                                                                                                                    |
| `![clobber(...)]`                                         | asm block                 | Lists registers, `flags`, or `memory` that an `asm` block overwrites but does not bind as operands.                                                                                                                                                                                                                                                                                                       |

The test-mode directives (`![config(testing)]`, `![test]`, `![ignore]`, `![should_panic]`) are documented in the next subsection.

Other names that appear with `![…]` syntax are accepted by the parser and recorded so user tooling can observe them, but the compiler emits a warning for any non-builtin name and applies no semantics. A list of reserved-but-unimplemented directive names is in [§ 21](#21-reserved-syntax).

### 17.2 Test Directives

The test framework introduces a small set of directives recognised by the compiler ([§ 20](#20-testing) covers usage):

| Directive            | Description                                                        |
| -------------------- | ------------------------------------------------------------------ |
| `![config(testing)]` | Marks a file as a test source. May appear next to its `namespace`. |
| `![test]`            | Marks a function as a discoverable test.                           |
| `![ignore]`          | Skips the test unless `cryo test --ignored` is passed.             |
| `![should_panic]`    | Asserts the test panics; passing the test means observing a panic. |

### 17.3 Memory Layout

Cryo is a systems language and gives programmers explicit control over the in-memory layout of user-defined types. Layout matters for FFI (matching a `struct` declared in a C header), for binary protocols (network packets, on-disk records, hardware register maps), and for memory-conscious data structures.

#### 17.3.1 The Default Layout (`repr(Cryo)`)

Without any explicit directive, a struct or class has the **default Cryo layout**:

- Fields are laid out in source order.
- Each field is placed at the lowest offset that satisfies its natural alignment.
- The size of the type is rounded up to the type's alignment (the maximum alignment among its fields, or 1 for empty types).
- Padding bytes inserted to satisfy alignment have undefined content.

The default layout currently matches the platform's C ABI for primitive-typed fields. The language reserves the right to optimise the default layout in future versions (e.g. reordering fields). **Do not rely on the default layout for FFI or binary serialisation; use `![repr(C)]` for those cases.**

A class with virtual methods carries an 8-byte vtable pointer at offset 0; non-virtual classes have no vtable. Inheritance is flattened in root-to-derived order so an upcast pointer is a no-op.

#### 17.3.2 `![repr(C)]`

```cryo
![repr(C)]
type struct timespec {
    tv_sec:  i64;
    tv_nsec: i64;
}
```

`![repr(C)]` guarantees that the type's layout matches the platform's C ABI:

- Fields are laid out in source order.
- Padding follows the C rules: each field starts at the lowest offset that is a multiple of its alignment.
- The size of the type is rounded up to the type's alignment.
- The compiler will not reorder fields under any circumstances.

`![repr(C)]` is the correct choice for any type that is exchanged with C code, mapped to a C header, or used as a binary record. Combine with `![align(N)]` to over-align such a struct.

#### 17.3.3 `![repr(packed)]`

```cryo
![repr(packed)]
type struct PacketHeader {
    version: u8;
    flags:   u8;
    length:  u32;     // no padding before this field
    crc:     u32;
}
```

`![repr(packed)]` removes all inter-field padding:

- Fields are laid out in source order.
- Each field is placed at the next byte offset, regardless of its natural alignment.
- The type's alignment is 1.
- The size of the type equals the sum of its fields' sizes.

A `repr(packed)` type may contain misaligned fields. Taking a Cryo reference (`&` or `mut &`) to a field of a `repr(packed)` type is a compile error: the reference would not be naturally aligned for its referent, which is undefined behaviour. Read or write the field by value instead; the compiler emits the unaligned load/store at the use site.

`![repr(packed)]` and `![align(N)]` are mutually exclusive on the same type.

#### 17.3.4 `![repr(transparent)]`

```cryo
![repr(transparent)]
type struct Pid {
    inner: i32;
}
```

`![repr(transparent)]` declares a wrapper type whose layout is identical to a single contained field:

- The type must have exactly one field whose size is non-zero.
- The wrapper has the same size, alignment, and ABI as that inner field.
- A `Pid` and an `i32` are interchangeable at the ABI level - passing `Pid` to an `extern "C"` function is identical to passing `i32`.

This is the recommended idiom for type-safe wrappers around primitive FFI types (file descriptors, error codes, opaque handles) where the wrapper exists purely for type discipline at the source level.

#### 17.3.5 `![align(N)]`

```cryo
![align(64)]
type struct CacheLine {
    payload: u8[64];
}
```

`![align(N)]` sets the minimum alignment of the type to `N` bytes:

- `N` must be a power of two between 1 and 65536.
- The actual alignment of the type is `max(natural_alignment, N)`.
- The size of the type is rounded up to its alignment.

`![align(N)]` is compatible with `![repr(C)]` and `![repr(transparent)]`; it is mutually exclusive with `![repr(packed)]`.

`![align(N)]` on a variable raises that variable's alignment for the lifetime of its storage; this is useful for stack buffers that must satisfy SIMD or hardware-register alignment requirements.

#### 17.3.6 Enums

A unit enum (no variant payloads) has a discriminant whose type defaults to `i32`. The discriminant type can be set explicitly with the type-annotation syntax (not a directive):

```cryo
type enum Color : u8 {
    Red    = 0;
    Green  = 1;
    Blue   = 2;
}
```

An ADT enum (variants with payloads) is laid out as a tag (`i32`) followed by a payload area sized to the largest variant, with padding so the payload area is naturally aligned. `![repr(C)]` on an ADT enum is reserved syntax and currently produces the same layout as the default.

#### 17.3.7 Inspecting Layout: `sizeof(T)` and `alignof(T)`

`sizeof(T)` and `alignof(T)` return compile-time `u64` constants reflecting the type's chosen layout - including any `![repr]` or `![align]` directives applied to it. They are the recommended way to verify FFI struct layout against a C header in a test:

```cryo
![test]
function timespec_matches_c() -> void {
    expect_eq(sizeof(timespec),  16);
    expect_eq(alignof(timespec), 8);
}
```

---

## 18. Foreign Function Interface

Cryo follows the platform C ABI and uses an LLVM backend, so calling C from Cryo and Cryo from C is straightforward.

### 18.1 Extern Blocks

Declare C function signatures with Cryo syntax inside an `extern "C"` block. The compiler trusts the declarations and the linker resolves the symbols.

```cryo
extern "C" {
    function puts(s: string) -> int;
    function atoi(s: string) -> int;
}
```

A standalone extern is also valid:

```cryo
extern function exit(code: int) -> void;
```

It is the programmer's responsibility to ensure the Cryo signature matches the C signature; the compiler cannot verify this across the language boundary.

### 18.2 C Header Import

For larger C libraries, transcribing every signature by hand is error-prone. Cryo can import a C header directly. The compiler drives **libclang** (Clang's stable C API), which parses the header and generates Cryo declarations for the functions **and the structs, unions, enums, and typedefs** it finds.

```cryo
extern module c := "C" {
    #include <stdio.h>       // angle: system include search path
    #include <stdlib.h>
    #include "./my_header.h" // quoted: relative to this source file
}

function main() -> int {
    c::printf("Value: %d\n", 42);
    const buf: void* = c::malloc(256);
    c::free(buf);
    // Imported C types are reached through the alias too:
    const p: c::Point = c::Point { x: 1 as i32, y: 2 as i32 };
    return 0;
}
```

Each `#include` takes either an angle-bracketed name (`<stdio.h>`, resolved on the C preprocessor's system include search path) or a quoted path (`"./my_header.h"`, resolved relative to the importing file) — exactly as in C. The identifier after `extern module` (`c` here) introduces a namespace into which the imported declarations are placed; access both functions and types with `::` (`c::printf`, `c::Point`) to prevent collisions between C and Cryo names. Only declarations from the named header(s) are imported — types pulled in transitively from system headers (`size_t`, `int32_t`, …) are resolved to their Cryo primitive directly rather than re-emitted.

Type mapping: a C `struct`/`union` becomes a `![repr(c)]` `type struct` (a union is a layout-faithful opaque storage blob — no field access); a named `enum` becomes a `type enum` with its explicit discriminant values; an **anonymous** `enum` (which has no nameable type — its constants are plain integers in C) contributes one alias-namespaced `const` per constant (`enum { LO = 1, HI = 2 }` → `c::LO`, `c::HI`); a `typedef` becomes a `type alias`; function-pointer parameters/fields map to Cryo `(Args) -> Ret`. Imported types and constants are namespaced under the alias only (`c::Point`, `c::LO`), never the global namespace.

Some C constructs have no first-class Cryo equivalent, so they are translated to **layout-faithful opaque storage** — a struct of that type round-trips by value over the FFI boundary (correct size and alignment, verifiable with `static_assert`; see [§18.5](#185-compile-time-layout-assertions-static_assert)) but offers no member access to that part: a **bitfield run** (adjacent `unsigned x : 3` fields share a storage unit) collapses to one blob field; a C11 **anonymous struct/union member** (`union { ... };` with no field name) becomes an aligned blob field named `_anon0`, `_anon1`, …; and a field whose type is an inline **anonymous struct/union** likewise maps to a blob rather than a pointer. Each such approximation is reported (see below).

Object-like `#define` constants whose body is a single literal are imported too, each as an alias-namespaced `const` with an inferred type: a **numeric** literal (`#define MAX_LEN 256` → `c::MAX_LEN: i32`; hex, octal, negative, and floating-point are supported, with integer width inferred from the value and `u`/`l` suffixes); a **string** literal (`#define NAME "cryo"` → `c::NAME: string`, with C escape sequences decoded); and a **character** literal (`#define TAB '\t'` → `c::TAB: char` carrying the code point). Macros that can't be bound — **function-like** macros (`#define SQUARE(x) ...`), valueless guards (`#define HEADER_H`), and compound expressions (`#define AREA (W * H)`) — are skipped rather than silently dropped. Only macros defined in the named header itself are considered (the compiler's predefined macros are excluded).

Every construct that is skipped or approximated is recorded and printed as a per-header **translation report** under `--debug` (`[skip]` for an unbound construct, `[approx]` for a layout-only binding), so nothing is lost silently.

To import only a header's function prototypes — suppressing all struct/enum/typedef emission — apply the **`![functions_only]`** directive to the extern-module block. It is valid only on a C-import extern module:

```cryo
![functions_only]
extern module c := "C" {
    #include "./api.h"   // import c::do_thing(...) etc.; skip the header's types
}
```

An aliased import block holds **only** `#include` directives, never Cryo declarations; conversely, a plain `extern "C"` block (§18.1) holds **only** hand-written Cryo signatures, never `#include`. Don't also hand-declare a symbol that an imported header already defines (e.g. `puts` from `<stdio.h>`) — reach it through the alias (`c::puts`) instead.

### 18.3 Calling Cryo from C

Cryo emits each declaration under its [mangled symbol name](cryo-mangling-spec.md). To call a Cryo function from C, write a thin wrapper inside an `extern "C"` block on the Cryo side that forwards to the real function, and declare the wrapper in your C header using the wrapper's mangled symbol.

A `![no_mangle]` / `![export]` directive that suppresses mangling for a single Cryo function is on the post-1.0 roadmap; until then, the wrapper-plus-mangled-symbol approach is the supported path.

### 18.4 Function-pointer callbacks

Many C APIs take a function pointer (`qsort`, event loops, AST visitors). Declare the parameter with Cryo's function-pointer type `(Args) -> Ret` and pass a **named Cryo function by its bare name** — taking the address is implicit, no `&`:

```cryo
extern "C" {
    function qsort(base: void*, nmemb: u64, size: u64,
                   compar: (const void*, const void*) -> i32) -> void;
}

function cmp_i32(a: const void*, b: const void*) -> i32 {
    const pa: i32* = a as i32*;
    const pb: i32* = b as i32*;
    return *pa - *pb;
}

// qsort(&arr[0] as void*, n, 4, cmp_i32);   // sorts in place via the callback
```

The callback must be a **bare function pointer** — a named function or a non-capturing lambda. A capturing closure is *not* C-compatible: a C function pointer has no environment slot. Thread per-call state through an explicit `void*` client-data parameter instead (the standard C idiom, e.g. libclang's `clang_visitChildren(cursor, visitor, client_data)`):

```cryo
extern "C" {
    function with_each(items: void*, n: u64,
                       visit: (void*, void*) -> i32, client: void*) -> void;
}
```

Where a C API documents an *optional* callback, a typed `null` is a valid value: pass `null` and guard the call site with `f == null`.

### 18.5 Compile-time layout assertions (`static_assert`)

When binding a C library, a Cryo struct must match the C struct's layout exactly. `static_assert` checks a constant condition at compile time and fails the build (E0237) if it is false — so a binding can verify its own layout against the numbers the C side guarantees, rather than miscompiling silently.

```cryo
type struct Color { r: u8; g: u8; b: u8; a: u8; }

static_assert(sizeof(Color) == 4);
static_assert(alignof(Color) == 1);
static_assert(sizeof(Color) == 4, "Color must be 4 bytes to match the C ABI");
```

`static_assert` is a module-scope declaration: `static_assert(cond)` or `static_assert(cond, "message")`. The condition is folded after layouts are computed and may use integer/boolean literals, `sizeof(T)`, `alignof(T)`, and the arithmetic, comparison, logical, and bitwise operators. A condition that is false — or that is not a compile-time constant — is a compile error. (It is a general feature, not FFI-only, but layout verification is its primary use.)

---

## 19. The Standard Library

The standard library is written entirely in Cryo and ships with the compiler as a single static library plus its sources. It is organised into a small `core` (no allocation, no I/O), a heap-allocation layer (`alloc`), a collection layer (`collections`), and a set of domain-specific modules. The complete top-level manifest with one-line descriptions lives in [`stdlib/lib.cryo`](../stdlib/lib.cryo).

### 19.1 The Prelude

The prelude is auto-imported into every Cryo source file. Currently:

| Module               | What it brings in                                                                                                     |
| -------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `core::panic`        | `panic(message: string, file: string, line: u32) -> void` (does not return; aborts the process)                       |
| `core::option`       | `Option<T>` (`Some` / `None`) and its methods                                                                         |
| `core::result`       | `Result<T, E>` (`Ok` / `Err`) and its methods                                                                         |
| `core::primitives`   | Methods on built-in types (`i32::max_value`, `char::is_digit`, …)                                                     |
| `core::intrinsics`   | Compiler intrinsics including `printf`, `malloc`, `free`, `memcpy` (the `print`/`println` family lives in `std::fmt`) |
| `core::varargs`      | `VaArgs`, the compiler-assigned type of a function's `args...` bucket                                                 |
| `collections::array` | `Array<T>`, needed because `T[]` desugars to `Array<T>`                                                               |
| `core::slice`        | `Slice<T>`, backing the for-in lowering over fixed-size arrays (`for (x in arr)` where `arr: T[N]`)                   |
| `core::ops`          | `Range` / `RangeInclusive`, backing range literals (`a..b` desugars to `Range::new`)                                  |
| `core::iter`         | `Iterator`, the trait the for-in sugar drives the scrutinee through                                                   |
| `alloc::box`         | `Box<T>`                                                                                                              |
| `alloc::rc`          | `Rc<T>`                                                                                                               |

The prelude is deliberately small. Anything else is an explicit `import` - notably, the `print` / `println` / `eprint` / `eprintln` family lives in `std::fmt` and is **not** auto-imported. Examples in this document that use `println` assume `import std::fmt;` is in scope.

### 19.2 Module Map

| Module            | Highlights                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **`core`**        | The language foundations. `Option<T>`, `Result<T, E>`, `Slice<T>`, `NonNull<T>`, `Range<T>`, `RangeInclusive<T>`, `Ordering`. Traits: `Copy`, `Drop`, `Clone`, `Default`, `Eq`, `Ord`, `Hash`, `Iterator<Item>`, `IntoIterator`, `From`/`Into`/`TryFrom`/`TryInto`, `Step`. Memory utilities (`copy`, `zero`, `swap`, `transmute`, `align_up`/`align_down`). Hashing (`Hasher`, `DefaultHasher`, an FNV-1a implementation).                                                                                                                                                                            |
| **`alloc`**       | `Layout`, `Allocator` trait, `GlobalAlloc`, `Box<T>`, `Arena` (bump allocator with reset), `Pool` (fixed-slot slab), `Rc<T>` (single-threaded reference counting), `Arc<T>` (atomic reference counting for cross-thread sharing).                                                                                                                                                                                                                                                                                                                                                                      |
| **`collections`** | `Array<T, A>` (growable contiguous), `Str` (borrowed length-typed UTF-8 view), `String<A>` (owned UTF-8), `HashMap<K, V, A>` (separate-chaining), `HashSet<T, A>`, `Pair<A, B>` (owned two-element tuple). All allocator-generic with `GlobalAlloc` default.                                                                                                                                                                                                                                                                                                                                           |
| **`io`**          | `Read` / `Write` traits with rich defaults (`read_byte`, `read_until`, `read_to_end`, `read_to_string`, `write_all`, `write_str`, `write_line`). `Stdin`, `Stdout`, `Stderr` handles with `is_tty()`/`as_fd()`. `BufWriter<W>` / `LineWriter<W>` / `BufReader<R>`. POSIX flag constants and an `IoError` / `IoErrorKind` mapping `errno`.                                                                                                                                                                                                                                                              |
| **`fmt`**         | `Display` and `Debug` traits, `Formatter<W>` borrowing its sink, `FmtWrite`, `print` / `println` / `eprint` / `eprintln`, `format_to_string`, `format_debug_to_string`. Heap-free integer and float writers (`write_u64_decimal`, `write_f64`).                                                                                                                                                                                                                                                                                                                                                        |
| **`json`**        | RFC 8259 parser and serialiser. `JsonValue`, `JsonNumber`, `JsonObject` (insertion-ordered). `parse`, `stringify`, `stringify_pretty`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| **`fs`**          | `Path` (borrowed) and `PathBuf` (owned). `OpenOptions` builder, `File` (`Read + Write`), convenience `read(path)` / `write(path, bytes)` / `read_to_string(path)` / `copy(from, to)`. Whole-path operations: `remove_file`, `rename`, `create_dir` / `create_dir_all`, `remove_dir` / `remove_dir_all`, `read_dir` (a `ReadDir` iterator of `DirEntry`), `canonicalize`. Metadata: `metadata` / `symlink_metadata` (typed `Metadata` via `stat` / `lstat`), `exists`, `is_file`, `is_dir`. `O_*` and `SEEK_*` constants.                                                                               |
| **`ffi`**         | The C ABI boundary. `libc` is the single home for every `extern "C"` the stdlib needs (POSIX I/O, sockets, math) and the named POSIX constants. `cstr` provides `CStr` (borrowed) and `CString` (owned), with a `NulError` for interior-NUL conversion failures.                                                                                                                                                                                                                                                                                                                                       |
| **`env`**         | `args() -> Array<String>`, `var(name) -> Option<String>`, `set_var`, `remove_var`, `process_exit(code: i32) -> void`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| **`math`**        | Thin libm wrappers: `sqrt`, `cbrt`, `pow`, `exp` / `exp2`, `ln`, `log2` / `log10`, `sin` / `cos` / `tan` (plus the `a*` inverses and `*h` hyperbolics), `floor`, `ceil`, `round`, `trunc`, `fabs`, integer `abs_i32` / `abs_i64`, `min` / `max` / `clamp`, `f32` variants (`sqrt_f32`, `fabs_f32`, …), and the constants `PI`, `TAU`, `E`.                                                                                                                                                                                                                                                             |
| **`time`**        | `Duration` (normalized seconds + sub-second nanoseconds; `from_secs`/`from_millis`/`from_micros`/`from_nanos`, `as_secs`/`as_millis`/`as_micros`/`as_nanos`/`subsec_*`, `add`/`saturating_sub`/`is_zero`, `Eq` + `Ord`). `Instant` (monotonic `CLOCK_MONOTONIC`; `now`, `elapsed`, `duration_since`). `SystemTime` (wall `CLOCK_REALTIME`; `now`, `unix_epoch`, `duration_since_epoch`, `duration_since`). `sleep(Duration)` (EINTR-restarting). All clock differences saturate at zero.                                                                                                               |
| **`random`**      | `Rng`, a fast non-cryptographic xoshiro256** generator seeded via `from_seed(u64)` (reproducible) or `from_os()`: `next_u64`/`next_u32`/`next_bool`/`next_f64`, unbiased `below(bound)` / `range_u64(lo, hi)` (rejection-sampled), `fill_bytes`. `secure_bytes(buf, len)` fills from the kernel CSPRNG (`getrandom`); use it for keys/tokens/nonces - `Rng` is not cryptographic.                                                                                                                                                                                                                      |
| **`net`**         | `IpV4Addr`, `IpV6Addr`, `IpAddr`, `SocketAddr`, `TcpStream` (`Read + Write`), `TcpListener`. **HTTP/1.1 layer (`net::http`):** `Method`, `StatusCode`, `Headers`, `Request`, `Response`, `Router`, `HttpServer` with keep-alive + `Connection: close` opt-out + per-connection read timeouts, `Client::get`/`post` with `send(addr, req)`. **TLS** (`net::tls`, OpenSSL-backed `TlsStream`), **UDP** (`UdpSocket`), **HTTP/2** (`net::http2`, HPACK + single-stream framing), and **WebSocket** (`net::ws`, RFC 6455) all ship in 1.0. IPv6 addressing is parsed and represented but not yet dialable. |
| **`process`**     | POSIX subprocess spawning (`fork + execve`). `Command` builder (`arg`, `env`, `stdin`/`stdout`/`stderr`, `current_dir`), `Stdio` (`Inherit`, `Null`, `Piped`, `Fd`), `Child`, `ExitStatus`, `ChildStdin`/`ChildStdout`/`ChildStderr`, `Signal`. Windows is not yet supported.                                                                                                                                                                                                                                                                                                                          |
| **`sync`**        | A generic `Atomic<T>` (`T` = `u8` / `u32` / `u64` / `i32` / `i64` / `boolean`, dispatched at compile time via `static match`; `load` / `store` / `swap` / `fetch_add` / `fetch_sub` / `fetch_and` / `fetch_or` / `fetch_xor` / `compare_exchange`), `MemoryOrder`, `fence`, `compiler_fence`, `Mutex<T, A>`, `RwLock<T, A>`, `CondVar`, `Once`, `Barrier`. RAII guards (`MutexGuard`, `RwLockReadGuard`, `RwLockWriteGuard`) are `!Send`. `Send` / `Sync` auto-derive with call-site enforcement.                                                                                                      |
| **`thread`**      | `ThreadLocal<T>` lazy per-thread storage via `pthread_key`. `thread::spawn` / `try_spawn` / `JoinHandle<T>` (returning the body's value on `join`), `spawn_with_attr`, scoped threads (`thread::Scope`), `thread::current` / `yield_now` / `sleep` / `sleep_ms`, plus `sync::mpsc` channels (`channel`, `Sender`, `Receiver`) all ship in 1.0, built on `pthread`. The `sync` primitives and `Arc<T>` ship alongside. `Builder` (`Builder::new().stack_size(bytes).name(str).spawn`/`try_spawn`) configures the stack size and OS thread name for a spawn.                                             |
| **`test`**        | Built-in unit-test framework. Tests live in `<project>/tests/`, are marked `![test]`, and are discovered and run fork-per-test by `cryo test`. `expect`, `expect_eq`, `expect_ne`, `bail`, `bail_other`. See [§ 20](#20-testing).                                                                                                                                                                                                                                                                                                                                                                      |

### 19.3 Naming Conventions in the Standard Library

The standard library follows a small set of conventions that user code is encouraged to mirror.

- **No NUL-terminated strings outside `ffi/`.** Every other module works in `Str` and `String`. The translation between Cryo strings and C strings happens at the boundary in `ffi::cstr`.
- **`Result` for fallible operations, `panic` for broken invariants.** A function returns `Result<T, E>` whenever failure is part of its contract; it panics only when the contract truly cannot be preserved (e.g., an out-of-bounds index on a function whose contract excludes it).
- **Explicit resource management.** Every owning type exposes a `drop(mut &this)` method, and its docstring identifies the ownership obligation. In practice the compiler synthesises these drops at scope exit (see [§ 16.2](#162-the-drop-trait)); manual `.drop()` remains valid for early release.
- **Allocator-generic containers.** `Array<T>`, `HashMap<K, V>`, `String`, etc. accept any `Allocator` implementation, defaulting to `GlobalAlloc`.

---

## 20. Testing

Cryo ships a built-in unit-test framework. Tests live in `<project>/tests/` files whose namespace declaration carries the `![config(testing)]` directive. Inside such a file, every function marked `![test]` is auto-discovered by the compiler and run fork-per-test by `cryo test`.

```cryo
![config(testing)]
namespace MyApp::Tests;

import std::test::assert::{ expect_eq, expect, bail };

![test]
function addition_is_commutative() -> Result<(), TestError> {
    return expect_eq(1 + 2, 2 + 1);
}

![test]
![ignore]
function expensive_integration_test() -> Result<(), TestError> {
    // Run only when the user passes --ignored.
    return expect(setup_real_environment(), "environment ready");
}

![test]
![should_panic]
function unwrap_none_panics() -> Result<(), TestError> {
    const empty: Option<int> = Option::None;
    const _value: int = empty.unwrap();   // panics
    return bail("unwrap should have panicked");
}
```

A test function returns `Result<(), TestError>`. `Ok(())` is success; `Err(...)` is failure with a message. The `assert` module provides:

- `expect(condition, message)`: fails if the condition is false.
- `expect_eq(a, b)`: fails if `a != b` (requires `T: Eq + Display`).
- `expect_ne(a, b)`: fails if `a == b`.
- `bail(message)`: unconditional failure.
- `bail_other(message)`: non-assertion failure (treated as an error rather than a test fail).

The runner forks a child process per test, captures its output, and applies the `![should_panic]` inversion if requested. Run with:

```bash
cryo test                          # run every discoverable test
cryo test some_filter              # run tests whose name contains "some_filter"
cryo test --list                   # discover only; print and exit
cryo test --ignored                # also run ![ignore]-marked tests
cryo test --exact                  # treat the filter as an exact match
cryo test -q, --quiet              # suppress per-test ok / ignored lines
```

### Output format

The runner ships three output formats; `--format=<mode>` picks per-run:

| Format    | Layout                                                                                                                                                  |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `plain`   | Cargo-style `test NAME ... ok` lines; CI-friendly. **Default.**                                                                                         |
| `pretty`  | Tests grouped under their namespace, indented leaves, `[PASS]` / `[FAIL]` / `[skip]` chips. Colored on a TTY.                                           |
| `compact` | One line per namespace with a dot-stream of results (`.` pass, `F` fail, `s` skip, `P` did-not-panic, `E` runner error) and a trailing per-group tally. |

Color follows `--color=<auto|always|never>`. `auto` (default) emits ANSI escapes only when stdout is a terminal; the `NO_COLOR` environment variable forces it off per <https://no-color.org/>.

Configuration precedence is **CLI flag > environment variable > `cryoconfig` > built-in default**:

```bash
cryo test --format=pretty --color=always
CRYO_TEST_FORMAT=compact CRYO_TEST_COLOR=never cryo test
```

```ini
# cryoconfig
[test]
format = "pretty"   # one of: plain | pretty | compact
color  = "auto"     # one of: auto  | always | never
```

`cryo test` forwards `[test] format = "..."` to the spawned test binary as `--format=...`, so the cryoconfig defaults travel without needing exported environment variables.

---

## 21. Reserved Syntax

The lexer and grammar reserve the following forms because the language plans to use them. The compiler does not yet lower them; using them today either errors at parse time or is silently ignored by later passes. Treat this list as a roadmap, not as features.

| Reserved                                      | Status                                                                                                                                                                                            |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Raw strings `r"..."`                          | Reserved. Will treat backslashes literally (no escape processing). Today an `r` prefix lexes as a separate identifier followed by an ordinary string literal.                                     |
| Escapes `\a` `\b` `\f` `\v`                   | Reserved. The lexer does not yet recognise them; they pass through as a literal backslash plus the following character. The implemented escapes are `\n` `\t` `\r` `\0` `\\` `\'` `\"` `\xHH`.    |
| `async` / `await`                             | Lexer recognises them; parser will accept `await expr`, but the type system has no `Future` / `Promise` and codegen does not implement coroutines.                                                |
| `yield`                                       | Parser accepts a `yield` expression; no generator semantics exist.                                                                                                                                |
| Optional chaining `?.`                        | Token reserved; not consumed by the parser.                                                                                                                                                       |
| Spread `...` in calls / literals              | The token exists for variadic parameter declarations only.                                                                                                                                        |
| Pure-virtual class method (e.g. `= 0` syntax) | Not implemented. Use a `virtual` method without a body to declare an interface point.                                                                                                             |
| Struct field defaults (`field: T = expr`)     | The `= expr` default syntax parses, but defaults are not applied at construction: every field must be supplied in a literal, and omitting one is `E0355`. See [§ 8.2](#82-fields-and-visibility). |
| Macros                                        | No macro system exists, and `macro` is **not** currently a reserved word (it lexes as an ordinary identifier). A future hygienic macro system may introduce one.                                  |
| `![pure]`                                     | Reserved. Will assert the function has no observable side effects (enabling aggressive folding). Parsed as an unknown directive today (warning + no semantics).                                   |
| `![const]`                                    | Reserved. Will mark a function as evaluable at compile time.                                                                                                                                      |
| `![noreturn]`                                 | Reserved. Will declare a function that never returns normally.                                                                                                                                    |
| `![export]` / `![no_mangle]`                  | Reserved. Will suppress Cryo name mangling so the function ships under its declared identifier and can be linked from C without a wrapper. See [§ 18.3](#183-calling-cryo-from-c).                |
| `![section("name")]`                          | Reserved. Will place the symbol in a specific object-file section.                                                                                                                                |
| `![weak]`                                     | Reserved. Will declare weak linkage.                                                                                                                                                              |
| `![constructor]` / `![destructor]`            | Reserved. Will register functions that run before `main` / after `main` returns.                                                                                                                  |
| `![repr(<int_type>)]` on enums                | Reserved. Use `type enum Foo : u8 { … }` syntax for now; future versions may accept `![repr(u8)]` as an equivalent spelling.                                                                      |

When any of these moves out of "reserved" and into "implemented," it will be added to the relevant section of this document and removed from this table.

---

## 22. Grammar Summary

The complete formal grammar is in [`docs/grammar.md`](grammar.md), written in EBNF following ISO/IEC 14977. What follows is a condensed overview of the major productions.

### 22.1 Program Structure

```
program         = { directive } [ namespace_decl ] { top_level_item }
top_level_item  = import_decl | module_decl | var_declaration
                | function_declaration | extern_function_decl | extern_block
                | c_header_import | intrinsic_decl
                | struct_declaration | class_declaration
                | enum_declaration | trait_declaration
                | type_alias_declaration | implementation_block
```

### 22.2 Statements

```
statement       = var_declaration | function_declaration | struct_declaration
                | class_declaration | enum_declaration | trait_declaration
                | type_alias_declaration | implementation_block
                | if_statement | while_statement | for_statement
                | loop_statement | do_while_statement
                | match_statement | switch_statement
                | break_statement | continue_statement | return_statement
                | unsafe_block | block | expression_statement
```

### 22.3 Expression Hierarchy

Expressions are stratified by precedence; each level delegates to the next-higher-precedence level.

```
expression          = assignment_expr
assignment_expr     = coalesce_expr [ assignment_op assignment_expr ]
coalesce_expr       = pipe_expr [ "??" coalesce_expr ]
pipe_expr           = conditional_expr { ( "|>" | "<|" ) conditional_expr }
conditional_expr    = logical_or_expr [ "?" expression ":" conditional_expr ]
logical_or_expr     = logical_and_expr { "||" logical_and_expr }
logical_and_expr    = bitwise_or_expr { "&&" bitwise_or_expr }
bitwise_or_expr     = bitwise_xor_expr { "|" bitwise_xor_expr }
bitwise_xor_expr    = bitwise_and_expr { "^" bitwise_and_expr }
bitwise_and_expr    = equality_expr { "&" equality_expr }
equality_expr       = relational_expr { ( "==" | "!=" ) relational_expr }
relational_expr     = shift_expr { ( "<" | ">" | "<=" | ">=" | "<=>" ) shift_expr }
shift_expr          = additive_expr { ( "<<" | ">>" ) additive_expr }
additive_expr       = multiplicative_expr { ( "+" | "-" ) multiplicative_expr }
multiplicative_expr = cast_expr { ( "*" | "/" | "%" ) cast_expr }
cast_expr           = unary_expr { "as" type }
unary_expr          = unary_op unary_expr | postfix_expr
postfix_expr        = primary_expr { postfix_op }
```

For the full grammar, including type and pattern productions, see [`grammar.md`](grammar.md).

---

## 23. Project Configuration (cryoconfig)

A project is a directory containing a `cryoconfig` file. `cryo build`, `cryo run`, and `cryo test` search upward from the working directory for it, then build the project it describes. Scaffold a starter file with `cryo init`.

`cryoconfig` is an INI-like file: `[section]` headers, `key = value` lines, `#` comments. List values use TOML-style arrays (`["a", "b"]`). Unknown keys are ignored with a warning; keys removed in 1.0 are a hard error that names the replacement.

### 23.1 `[project]`

Project identity and source layout.

```ini
[project]
project_name  = "my-app"            # display name
target_type   = "executable"        # executable | library | stdlib
entry_point   = "src/main.cryo"     # main file (executables only)
source_dir    = "src"               # source root (libraries / stdlib)
output_dir    = "build"             # where build artifacts are written
source_paths  = ["../shared/src"]   # extra source roots to scan for modules
target_triple = ""                  # cross-compile triple; empty => build for host
stdlib_root   = ""                  # project-pinned stdlib root (see §24.3)
```

`target_triple` (e.g. `"x86_64-pc-windows-gnu"`) cross-compiles: it is threaded into LLVM and selects the target ABI and toolchain. For `x86_64-pc-windows-gnu` the build drives a full mingw-w64 link to a `.exe`; for triples without a known toolchain (aarch64, riscv, windows-msvc, …) the host link step is skipped and the object files are left for a manual link. The CLI `--target=TRIPLE` overrides it.

### 23.2 `[compiler]`

Code-generation knobs.

```ini
[compiler]
debug     = false                   # verbose compiler logging
optimize  = "O2"                    # O0 | O1 | O2 | O3 (default O2)
emit_llvm = false                   # also write LLVM IR (.ll) beside the object
no_std    = false                   # build without linking the standard library
```

**Build profiles.** A build runs under a named *profile* that supplies a
default optimization level and debug-info setting and names the per-profile
cache subtree. Two are built in:

| Profile             | Optimization | Debug info   |
| ------------------- | ------------ | ------------ |
| `release` (default) | `O2`         | off          |
| `debug`             | `O0`         | DWARF (`-g`) |

Set the default in cryoconfig, or pick one per build with `--release` / `--dev`
(`--dev` = the `debug` profile) / `--profile=NAME`:

```ini
[profile]
default = "release"                 # release | debug
```

An explicit `[compiler] optimize` overrides the profile's level; `--opt-level=N`
overrides everything; `-g` forces debug info on regardless of profile.

**Build directory layout.** The final artifact is **hoisted** to the root of
`output_dir` so it runs as `build/<name>` regardless of profile; everything
else lives under a visible, per-profile cache (`target/<profile>/`) grouped by
**package origin** — the standard library (`std/`), the local project
(`local/`), and one subtree per third-party dependency (`<depname>/`):

```
build/
├── <name>                          # hoisted final executable  (cryo run / [[bin]])
├── lib<name>.a                     # hoisted final library     ([lib] target)
└── target/
    └── <profile>/                  # release | debug
        ├── <name>                  # per-profile build (the hoist source)
        ├── <name>.ll               # combined LLVM IR          (when emit_llvm)
        ├── build-manifest.json     # per-profile metadata + fingerprint
        ├── std/                    # standard library package
        │   ├── deps/  *.o          #   per-module objects
        │   └── ir/    *.ll         #   per-module IR (when emit_llvm)
        ├── local/                  # the local project package
        │   ├── deps/  *.o
        │   ├── ir/    *.ll
        │   └── incremental/        #   rebuild fingerprint
        └── <depname>/              # one subtree per dependency
            ├── deps/  *.o
            └── ir/    *.ll
```

`cryo build` is incremental: if no input changed (sources, the resolved knobs,
the compiler binary, or the linked stdlib) and the artifact still exists, the
build is skipped (`<name> is up to date`). Pass `--no-incremental` to force a
full rebuild and refresh the manifest.

### 23.3 `[link]`

Native libraries to link, named by *intent* rather than by raw linker flag — so a project never has to juggle two similar lists.

```ini
[link]
system = ["ssl", "crypto"]          # system libraries        -> -l<name>
search = ["/usr/lib/llvm-20/lib"]   # extra -L dirs to resolve `system` libs
static = ["helpers/libabihelpers.a"] # local archives, passed to the linker by path
```

| Key      | Role                                                                                        | Linker form        |
| -------- | ------------------------------------------------------------------------------------------- | ------------------ |
| `system` | A library the linker finds on its default search path.                                      | `-l<name>`         |
| `search` | Extra directories to search — only needed for `system` libs that live off the default path. | `-L<dir>`          |
| `static` | A local archive in your project; linked by its path.                                        | the path, verbatim |

> Migrating from pre-1.0: `link_libs` → `[link] system`, `link_paths` → `[link] search` (or `[link] static` for a local archive). The old `[compiler] args = ["--emit-llvm"]` flag-smuggling is gone — set `emit_llvm = true`. `[compiler] include_paths` → `[project] source_paths`. `[project] target` → `[project] target_triple`.

### 23.4 Multi-target: `[lib]` and `[[bin]]`

A project can build a library and one or more executables from one source tree. `[lib]` declares the library; each `[[bin]]` (array-of-tables) declares an executable. When present these take precedence over the single-target `[project] target_type` / `entry_point`.

```ini
[lib]
name       = "mylib"
source_dir = "src"

[[bin]]
name        = "mytool"
entry_point = "src/main.cryo"
```

### 23.5 `[dependencies]`

Each entry is an inline table — a local path dependency or a git dependency. `cryo fetch` resolves them and writes `cryoconfig.lock`.

```ini
[dependencies]
mylib    = { path = "../mylib", alias = "MyLib" }
remote   = { git = "https://example.com/lib.git", tag = "v0.1.0", alias = "Remote" }
```

A git dependency requires exactly one of `version`, `tag`, `branch`, or `rev`, and may set `subdir` (the path to the dependency's `cryoconfig` inside the repo). `alias` is the top-level namespace the consumer imports.

### 23.6 `[test]`

Defaults for the test runner; forwarded to the spawned test binary unless overridden on the CLI.

```ini
[test]
format = "pretty"                   # plain | pretty | compact
color  = "auto"                     # auto | always | never
```

---

## 24. Command-Line Interface

`cryo` is a single binary: the compiler, package manager, test runner, and dependency resolver. Run `cryo` (or `cryo --help`) for a command overview, `cryo help <command>` for one command, and `cryo help <topic>` for the topics below.

### 24.1 Commands

| Command                  | Purpose                                                                                    |
| ------------------------ | ------------------------------------------------------------------------------------------ |
| `cryo build [file\|dir]` | Build a single `.cryo` file, or the project whose cryoconfig lives in `dir` (default `.`). |
| `cryo run`               | Build the project and run the resulting executable.                                        |
| `cryo test [pattern]`    | Build and run the project's `![test]` functions.                                           |
| `cryo check <file>`      | Front-end + semantic analysis only; no codegen.                                            |
| `cryo init [dir]`        | Scaffold a new project (`cryoconfig` + `main.cryo`).                                       |
| `cryo raw <file>`        | Compile without the stdlib or prelude.                                                     |
| `cryo demangle <sym>`    | Decode a mangled symbol to its source form.                                                |
| `cryo fetch`             | Fetch dependencies and write `cryoconfig.lock`.                                            |
| `cryo update`            | Re-resolve every dependency, ignoring the lockfile.                                        |
| `cryo version`           | Print the compiler version.                                                                |

Running `cryo <file.cryo>` with no command compiles that file directly.

### 24.2 Build flags

Accepted by `build` / `run` / `test` / `check` as noted; run `cryo help flags` or `cryo help <flag>` for detail.

| Flag                  | Effect                                                                                                                                  |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `--debug`             | Verbose compiler logging.                                                                                                               |
| `--ast`               | Dump the AST after parsing.                                                                                                             |
| `--emit-llvm`         | Emit LLVM IR (`.ll`) beside the object output.                                                                                          |
| `--build-dir=PATH`    | Override `[project] output_dir`.                                                                                                        |
| `--stdlib=PATH`       | Standard-library root for this run (highest priority; see §24.3).                                                                       |
| `--target=TRIPLE`     | Cross-compile for an LLVM target triple. `x86_64-pc-windows-gnu` links to a `.exe` via mingw-w64; other triples emit object files only. |
| `--opt-level=N`       | Optimization level `0`..`3`; overrides the profile and `[compiler] optimize`.                                                           |
| `-g`, `--debug-info`  | Emit DWARF debug info.                                                                                                                  |
| `--release`           | Build with the `release` profile (O2, no debug info).                                                                                   |
| `--dev`               | Build with the `debug` profile (O0 + DWARF).                                                                                            |
| `--profile=NAME`      | Build with a named profile (cache under `build/target/NAME/`).                                                                          |
| `--no-incremental`    | Force a full build: skip the up-to-date short-circuit and the manifest/fingerprint write.                                               |
| `-o`, `--output PATH` | Redirect output for single-file builds; the output kind is inferred from the extension (`.o`, `.s`, `.ll`, or an executable).           |

Test-only flags: `--ignored`, `--list`, `--exact`, `-q` / `--quiet`.

### 24.3 Standard-library lookup

The compiler resolves the stdlib root from the first source that matches:

1. `--stdlib=PATH` — one-off override (this run only)
2. `$CRYO_STDLIB` — explicit stdlib pointer (environment)
3. `$CRYO_HOME/stdlib` — install-root pointer (set by `install.sh`)
4. `<bindir>/../stdlib` — binary-relative auto-detection
5. `[project] stdlib_root` — project-pinned in cryoconfig (relative to the project root when not absolute)
6. `<project_root>/../stdlib` — in-tree fallback

Most installs need none of these: the binary-relative default (4) finds the stdlib shipped alongside `cryo`. Projects living outside the Cryo repo pin a specific stdlib with `[project] stdlib_root`.
