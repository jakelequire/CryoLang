# Cryo Language Reference

> **Version:** 0.1.0 (In Development)
> **Specification Date:** March 2026

Cryo is a statically-typed, compiled systems programming language with monomorphic generics, single-inheritance classes, algebraic data types, exhaustive pattern matching, and an LLVM backend. It is designed to give programmers explicit, low-level control over memory and data layout while providing modern high-level constructs  generics, pattern matching, algebraic enums  that compile down to efficient native code with no runtime overhead.

Two principles shape the language:

1. **Explicitness over convenience.** Every variable must carry a type annotation. Mutability must be opted into. There is no implicit conversion between numeric types. The goal is that reading Cryo code tells you exactly what the machine will do, with no hidden costs.

2. **Pay only for what you use.** Generics are monomorphized (specialized at compile time), not boxed. Structs live on the stack. Classes live on the heap only when you ask for inheritance and virtual dispatch. The programmer chooses the abstraction, and the compiler ensures it costs nothing more than necessary.

---

## Table of Contents

- [Cryo Language Reference](#cryo-language-reference)
  - [Table of Contents](#table-of-contents)
  - [1. Lexical Structure](#1-lexical-structure)
    - [1.1 Identifiers](#11-identifiers)
    - [1.2 Keywords](#12-keywords)
    - [1.3 Comments](#13-comments)
    - [1.4 Literals](#14-literals)
      - [Numeric Literals](#numeric-literals)
      - [String Literals](#string-literals)
      - [Character Literals](#character-literals)
      - [Boolean Literals](#boolean-literals)
      - [Null Literal](#null-literal)
  - [2. Type System](#2-type-system)
    - [2.1 Primitive Types](#21-primitive-types)
    - [2.2 Pointer Types](#22-pointer-types)
    - [2.3 Reference Types](#23-reference-types)
    - [2.4 Array Types](#24-array-types)
    - [2.5 Function Types](#25-function-types)
    - [2.6 Tuple Types](#26-tuple-types)
    - [2.7 The Unit Type](#27-the-unit-type)
    - [2.8 Type Aliases](#28-type-aliases)
    - [2.9 Type Casting](#29-type-casting)
  - [3. Variables and Constants](#3-variables-and-constants)
  - [4. Functions](#4-functions)
    - [4.1 Function Declarations](#41-function-declarations)
    - [4.2 Generic Functions](#42-generic-functions)
    - [4.3 Variadic Functions](#43-variadic-functions)
    - [4.4 Extern Functions](#44-extern-functions)
    - [4.5 Intrinsic Functions](#45-intrinsic-functions)
  - [5. Operators](#5-operators)
    - [5.1 Arithmetic Operators](#51-arithmetic-operators)
    - [5.2 Comparison Operators](#52-comparison-operators)
    - [5.3 Logical Operators](#53-logical-operators)
    - [5.4 Bitwise Operators](#54-bitwise-operators)
    - [5.5 Assignment Operators](#55-assignment-operators)
    - [5.6 Other Operators](#56-other-operators)
    - [5.7 Operator Precedence](#57-operator-precedence)
  - [6. Control Flow](#6-control-flow)
    - [6.1 If / Else](#61-if--else)
    - [6.2 If Expressions](#62-if-expressions)
    - [6.3 While Loops](#63-while-loops)
    - [6.4 For Loops](#64-for-loops)
    - [6.5 Loop](#65-loop)
    - [6.6 Break and Continue](#66-break-and-continue)
    - [6.7 Match Statements](#67-match-statements)
    - [6.8 Match Expressions](#68-match-expressions)
    - [6.9 Switch Statements](#69-switch-statements)
    - [6.10 Ternary Operator](#610-ternary-operator)
    - [6.11 Return](#611-return)
    - [6.12 Unsafe Blocks](#612-unsafe-blocks)
  - [7. Structs](#7-structs)
    - [7.1 Struct Declaration](#71-struct-declaration)
    - [7.2 Fields and Visibility](#72-fields-and-visibility)
    - [7.3 Methods](#73-methods)
    - [7.4 Static Methods](#74-static-methods)
    - [7.5 Struct Literals](#75-struct-literals)
    - [7.6 Generic Structs](#76-generic-structs)
  - [8. Classes](#8-classes)
    - [8.1 Class Declaration](#81-class-declaration)
    - [8.2 Constructors and Destructors](#82-constructors-and-destructors)
    - [8.3 Inheritance](#83-inheritance)
    - [8.4 Virtual Methods and Override](#84-virtual-methods-and-override)
    - [8.5 Polymorphic Dispatch](#85-polymorphic-dispatch)
    - [8.6 Structs vs. Classes](#86-structs-vs-classes)
  - [9. Enums](#9-enums)
    - [9.1 Unit Enums](#91-unit-enums)
    - [9.2 Data Enums (Algebraic Data Types)](#92-data-enums-algebraic-data-types)
    - [9.3 Generic Enums](#93-generic-enums)
    - [9.4 Implement Blocks on Enums](#94-implement-blocks-on-enums)
  - [10. Pattern Matching](#10-pattern-matching)
    - [10.1 Patterns](#101-patterns)
    - [10.2 Enum Destructuring](#102-enum-destructuring)
    - [10.3 Wildcard Pattern](#103-wildcard-pattern)
    - [10.4 Range Patterns](#104-range-patterns)
  - [11. Generics](#11-generics)
    - [11.1 Generic Type Parameters](#111-generic-type-parameters)
    - [11.2 Generic Structs](#112-generic-structs)
    - [11.3 Generic Enums](#113-generic-enums)
    - [11.4 Generic Functions](#114-generic-functions)
    - [11.5 Generic Implement Blocks](#115-generic-implement-blocks)
    - [11.6 Where Clauses](#116-where-clauses)
    - [11.7 Monomorphization](#117-monomorphization)
  - [12. Implement Blocks](#12-implement-blocks)
  - [13. Modules and Imports](#13-modules-and-imports)
    - [13.1 Namespaces](#131-namespaces)
    - [13.2 Imports](#132-imports)
    - [13.3 Module Declarations](#133-module-declarations)
    - [13.4 Visibility](#134-visibility)
  - [14. Pointers and Memory](#14-pointers-and-memory)
    - [14.1 Address-of and Dereference](#141-address-of-and-dereference)
    - [14.2 Heap Allocation](#142-heap-allocation)
    - [14.3 Null](#143-null)
  - [15. Directives](#15-directives)
  - [16. Foreign Function Interface (FFI)](#16-foreign-function-interface-ffi)
    - [16.1 Extern Blocks](#161-extern-blocks)
    - [16.2 C Header Import](#162-c-header-import)
  - [17. Standard Library](#17-standard-library)
    - [17.1 Prelude](#171-prelude)
    - [17.2 Core Modules](#172-core-modules)
  - [18. Appendix: Grammar Summary](#18-appendix-grammar-summary)
    - [Program Structure](#program-structure)
    - [Statements](#statements)
    - [Expression Hierarchy](#expression-hierarchy)

---

## 1. Lexical Structure

This section describes the lowest-level building blocks of a Cryo program: the characters and tokens the compiler recognizes before any semantic meaning is assigned.

### 1.1 Identifiers

Identifiers are the names you give to variables, functions, types, and modules. An identifier begins with a letter (`a`-`z`, `A`-`Z`) or an underscore (`_`), followed by any combination of letters, digits, and underscores.

```
identifier = letter { letter | digit | "_" }
```

Cryo does not enforce naming conventions at the compiler level, but the standard library and ecosystem follow these conventions consistently:

- **`snake_case`** for variables, functions, and methods  `my_variable`, `parse_input`.
- **`PascalCase`** for types (structs, classes, enums, type aliases)  `HashMap`, `FileReader`.
- **`SCREAMING_SNAKE_CASE`** for compile-time constants  `MAX_BUFFER_SIZE`, `VERSION`.

These conventions also affect syntax highlighting: the TextMate grammar treats `PascalCase` identifiers as types and `SCREAMING_SNAKE_CASE` identifiers as constants, so following the conventions gives you better editor support out of the box.

### 1.2 Keywords

Keywords are reserved identifiers that have special meaning to the compiler. They cannot be used as variable or function names.

**Control flow:**
`if` `else` `elif` `switch` `case` `default` `match` `while` `for` `loop` `do` `break` `continue` `return` `yield`

**Declarations:**
`function` `class` `struct` `enum` `trait` `type` `namespace` `module` `import` `export` `from` `as` `implement` `intrinsic` `where` `extern`

**Modifiers:**
`const` `mut` `static` `public` `private` `protected` `virtual` `override` `inline` `async` `await` `unsafe` `mutable`

**Operator keywords:**
`new` `delete` `sizeof` `alignof` `typeof` `in` `as`

**Literals:**
`true` `false` `null` `this`

Note that some keywords like `trait`, `yield`, `async`, and `await` are reserved for future use and are not yet implemented in the current compiler.

### 1.3 Comments

Cryo supports four styles of comments. The first two are familiar from C-family languages, while the doc-comment forms are inspired by Rust and Javadoc.

```cryo
// Line comment  everything from // to end of line is ignored.

/* Block comment  can span multiple lines.
   Useful for temporarily disabling chunks of code. */

/// Documentation comment (line form).
/// Attaches to the declaration that follows it.
/// Multiple consecutive /// lines are joined together.

/** Documentation comment (block form).
    Also attaches to the following declaration. */
```

Line comments (`//`) are the default choice for inline explanations. Block comments (`/* */`) are useful when you need to comment out a region of code or write a multi-line aside. Documentation comments (`///` and `/** */`) are semantically meaningful: they are associated with the declaration that immediately follows them and are used by tooling (the LSP, documentation generators) to provide hover information and API docs.

### 1.4 Literals

Literals are values written directly in source code. Cryo provides literal syntax for integers, floats, strings, characters, booleans, and the null pointer.

#### Numeric Literals

Integer literals support four bases. Underscores can appear between digits as visual separators  the compiler ignores them entirely, so `1_000_000` is identical to `1000000`. An optional type suffix pins the literal to a specific integer type; without a suffix, the compiler infers the type from context (defaulting to `int`/`i32` for integers and `f64` for floats).

```cryo
42                // decimal integer
1_000_000         // digit separators  purely visual, ignored by compiler
0xFF              // hexadecimal (prefix 0x or 0X)
0b1010            // binary (prefix 0b or 0B)
0o77              // octal (prefix 0o or 0O)
42u64             // typed: unsigned 64-bit integer
42i8              // typed: signed 8-bit integer
```

Float literals require either a decimal point or an exponent (or both) to distinguish them from integers. They accept an optional `f32` or `f64` suffix.

```cryo
3.14              // float (defaults to f64)
3.14f32           // explicit 32-bit float
1.0e10            // scientific notation
2.5e-3f64         // scientific notation with explicit type
```

**Available type suffixes:** `u8` `u16` `u32` `u64` `i8` `i16` `i32` `i64` `f32` `f64` `usize` `isize`

The suffix is attached directly to the digits with no space. This is particularly useful when working with APIs that expect a specific width, such as `malloc(64u64)` or loop counters like `0u32`.

#### String Literals

Strings are enclosed in double quotes. Inside a regular string, backslash escape sequences are interpreted. Raw strings, prefixed with `r`, treat backslashes as literal characters  useful for file paths and regular expressions.

```cryo
"Hello, world!"           // regular string
"line one\nline two"      // escape: \n becomes a newline character
r"C:\Users\name\file"     // raw string: backslashes are literal
```

**Escape sequences:** `\n` (newline) `\t` (tab) `\r` (carriage return) `\a` (bell) `\b` (backspace) `\f` (form feed) `\v` (vertical tab) `\0` (null) `\\` (literal backslash) `\'` (single quote) `\"` (double quote) `\xHH` (hex byte) `\NNN` (octal byte)

#### Character Literals

Character literals are enclosed in single quotes and represent a single byte value. They support the same escape sequences as strings.

```cryo
'A'               // ASCII letter
'\n'              // newline character
'\x41'            // hex escape  same as 'A'
```

#### Boolean Literals

The two boolean values. There is no implicit conversion between booleans and integers  `if (1)` is a type error.

```cryo
true
false
```

#### Null Literal

`null` represents the null pointer. It is only valid in pointer contexts.

```cryo
null
```

---

## 2. Type System

Cryo's type system is built around a core design decision: **every value has a known type at compile time, and the programmer must state that type explicitly.** There is no type inference on variable declarations. This is a deliberate trade-off  it makes code more verbose, but also more readable. When you see `const x: i32 = compute()`, you know immediately what `x` is without needing to chase through function return types.

The type system has no implicit conversions. An `i32` does not silently promote to `i64`, and a `boolean` is not secretly an integer. When you need a conversion, you write it with `as`. This eliminates an entire class of subtle bugs common in C, where an unsigned-to-signed conversion or a narrowing cast can silently corrupt data.

### 2.1 Primitive Types

These are the built-in scalar types provided by the language. They map directly to machine types and have a fixed size on all platforms (except `usize`/`isize`, which match the pointer width).

| Type                          | Description                                                                   | Size             |
| ----------------------------- | ----------------------------------------------------------------------------- | ---------------- |
| `void`                        | No value; used as a return type for side-effecting functions                  | 0                |
| `boolean`                     | `true` or `false`; not interchangeable with integers                          | 1 byte           |
| `char`                        | 8-bit character (byte)                                                        | 1 byte           |
| `string`                      | Null-terminated string, equivalent to `char*` under the hood                  | pointer          |
| `int`                         | Default signed integer, alias for `i32`                                       | 4 bytes          |
| `i8` `i16` `i32` `i64` `i128` | Signed integers of explicit width                                             | 1/2/4/8/16 bytes |
| `uint`                        | Default unsigned integer, alias for `u32`                                     | 4 bytes          |
| `u8` `u16` `u32` `u64` `u128` | Unsigned integers of explicit width                                           | 1/2/4/8/16 bytes |
| `float`                       | Default float, alias for `f32`                                                | 4 bytes          |
| `f32` `f64`                   | IEEE 754 floating-point numbers                                               | 4/8 bytes        |
| `double`                      | Alias for `f64`, provided for C familiarity                                   | 8 bytes          |
| `usize` `isize`               | Pointer-sized unsigned/signed integers; 8 bytes on 64-bit platforms           | platform         |
| `never`                       | The bottom type; a function returning `never` does not return (e.g., `panic`) | N/A              |

The shorthand types `int`, `uint`, `float`, and `double` exist for convenience and readability. In performance-critical or cross-platform code, prefer the explicit-width types (`i32`, `u64`, `f64`) so the data layout is unambiguous.

### 2.2 Pointer Types

A pointer holds the memory address of a value. Pointer types are written by placing `*` after the pointee type, which mirrors the C convention.

```cryo
const p: int* = &x;           // pointer to int
const pp: int** = &p;         // pointer to pointer to int
const vp: void* = malloc(64); // void pointer (type-erased)
```

Pointers are the primary mechanism for heap allocation, FFI with C libraries, and building data structures like linked lists and trees. Unlike references (`&this`), raw pointers carry no compiler-enforced guarantees about validity or aliasing  it is the programmer's responsibility to ensure they point to valid memory. See [Section 14](#14-pointers-and-memory) for a deeper discussion.

### 2.3 Reference Types

References use the `&` prefix. In the current language, references appear primarily as method receivers (`&this` and `mut &this`) rather than as general-purpose types. They indicate that a method borrows the value it operates on rather than taking ownership of it.

```cryo
&int              // immutable reference to int
&mut int          // mutable reference to int
```

The distinction between `&this` and `mut &this` is central to how Cryo methods work. A method that takes `&this` promises not to modify the struct's fields; a method that takes `mut &this` may modify them. This makes the mutability contract part of the function signature, visible to both the caller and the compiler.

### 2.4 Array Types

Array types represent a contiguous sequence of values of the same type. The size may be specified as a compile-time constant (fixed-size array) or omitted (dynamically-sized).

```cryo
const arr: int[10];            // fixed-size array of 10 ints
const matrix: f64[3][3];      // 3x3 matrix (array of arrays)
const dynamic: int[];          // dynamically-sized array
```

For most purposes, the standard library's `Array<T>` (a heap-backed, growable vector) is preferred over raw array types. Raw arrays are useful for stack-allocated buffers and FFI.

### 2.5 Function Types

Cryo treats functions as first-class values. A function type describes its parameter types and return type, using the same `->` arrow syntax as function declarations.

```cryo
(int, int) -> int             // takes two ints, returns int
(T) -> U                      // generic function type
() -> void                    // no parameters, no return value
```

Function types are most commonly used as parameter types, enabling higher-order programming patterns like `map`, `filter`, and callbacks:

```cryo
function apply(f: (int) -> int, x: int) -> int {
    return f(x);
}
```

This is how the standard library's `Option::map` and `Result::and_then` work under the hood  they accept a function and apply it to the contained value.

### 2.6 Tuple Types

Tuple types group a fixed number of heterogeneous values into a single compound type. They are useful for returning multiple values from a function without declaring a named struct.

```cryo
(int, string)                 // a pair of int and string
(int, int, int)               // a triple of ints
```

### 2.7 The Unit Type

The unit type `()` represents "no meaningful value." It is distinct from `void`: `void` means "this function produces no value at all," while `()` means "this function produces a value, but it carries no information." In practice, `void` is used for function return types; `()` appears in generic contexts where a type parameter must be supplied but no data is needed, for example `Result<(), Error>` to represent an operation that either succeeds (with nothing to return) or fails with an error.

### 2.8 Type Aliases

Type aliases introduce a new name for an existing type. They do not create a distinct type  the alias and the original are fully interchangeable. Aliases are useful for shortening long generic types and giving domain-specific names to primitive types.

```cryo
type Byte = u8;
type StringResult<T> = Result<T, string>;
type Callback = (int) -> void;
```

The standard library uses this extensively in its FFI module, where C types are aliased to their Cryo equivalents:

```cryo
type c_int = i32;
type c_char = i8;
type c_size_t = u64;
```

### 2.9 Type Casting

Cryo has no implicit type conversions. When you need to convert between types, you use the `as` keyword to make the conversion explicit and visible:

```cryo
const a: i64 = 42;
const b: i32 = a as i32;             // narrowing cast  programmer takes responsibility
const p: u8* = some_string as u8*;   // reinterpret pointer type
```

The `as` keyword handles numeric widening, narrowing, and pointer reinterpretation. The compiler does not insert safety checks for narrowing casts (where data could be lost), so the programmer must ensure the value fits in the target type. This is a conscious design choice: in systems code, you often know the value range from context and don't want to pay for a runtime check on every cast.

---

## 3. Variables and Constants

Every variable declaration in Cryo has three parts: a mutability qualifier (`const` or `mut`), a name with a type annotation, and an optional initializer.

```cryo
const name: string = "Cryo";       // immutable binding
mut counter: int = 0;              // mutable binding
counter = counter + 1;             // reassignment (only allowed on mut)
```

**Immutable by default.** `const` is the default and the most common qualifier. An immutable binding cannot be reassigned after initialization. This is not just a stylistic preference  it makes code easier to reason about because you can see a `const` binding and know its value will never change for the rest of its scope.

**Explicit mutability.** When you do need a value to change, you mark it `mut`. This makes mutation visible at the declaration site. Anyone reading the code can immediately see which variables are "live wires" that might change and which are fixed.

Variables can also be declared without an initializer:

```cryo
mut y: int;                        // declared, will be assigned later
```

**Global variables** follow the same rules. Global `const` declarations are true constants; global `mut` declarations create mutable global state (use sparingly):

```cryo
const VERSION: string = "0.1.0";
mut g_counter: u64 = 0;
```

> **Important:** The type annotation is always required. Writing `const x = 10;` without a type is a syntax error. This is intentional  Cryo favors explicitness so that the type of every binding is immediately visible without any inference.

---

## 4. Functions

Functions are the primary unit of code organization in Cryo. Every Cryo program starts with a `main` function.

### 4.1 Function Declarations

A function is declared with the `function` keyword, a name, a parameter list in parentheses, an optional return type after `->`, and a body block. If no return type is specified, the function returns `void`.

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

Each parameter requires a name and a type annotation, separated by `:`. This is consistent with variable declarations and makes function signatures self-documenting  you can read the parameter list and understand what the function expects without looking at its body.

Functions can be recursive. There is no forward-declaration requirement: the compiler's multi-pass pipeline collects all function signatures before type-checking any bodies, so functions can call each other regardless of source order.

### 4.2 Generic Functions

When you need a function that works across multiple types, you add generic type parameters in angle brackets after the function name. The type parameter `T` is then available as a type throughout the function's signature and body.

```cryo
function identity<T>(x: T) -> T {
    return x;
}

function min<T>(a: T, b: T) -> T {
    if (a < b) {
        return a;
    }
    return b;
}

function swap<T>(a: T*, b: T*) -> void {
    const temp: T = *a;
    *a = *b;
    *b = temp;
}
```

At the call site, you supply the concrete type in angle brackets. The compiler generates a fully specialized version of the function for each distinct type it is called with (see [Section 11.7: Monomorphization](#117-monomorphization)).

```cryo
const n: int = identity<int>(42);
const s: string = identity<string>("hello");
```

Each call above produces a different machine-code function  one that operates on `int` and one that operates on `string`. There is no boxing, no vtable, and no runtime type information involved.

### 4.3 Variadic Functions

Some functions (particularly those bridging to C) accept a variable number of arguments. The last parameter uses the `...` suffix to indicate variadic arguments.

```cryo
function printf(format: string, args: ...) -> i32;
```

Variadic functions are primarily used for FFI interop with C's `printf`, `sprintf`, and similar APIs. User-defined variadic functions follow the same convention.

### 4.4 Extern Functions

Extern functions declare the signature of a function defined elsewhere  typically in a C library. They have no body; the linker resolves them.

```cryo
extern function puts(s: string) -> int;
```

This tells the compiler: "a function called `puts` exists, takes a `string`, and returns an `int`." The actual implementation comes from libc (or whatever library you link against). See [Section 16](#16-foreign-function-interface-ffi) for more on foreign function interop.

### 4.5 Intrinsic Functions

Intrinsic functions are provided by the compiler itself. They look like regular function declarations but are prefixed with `intrinsic`. The compiler knows how to lower them directly to LLVM IR rather than generating a normal function call.

```cryo
intrinsic function malloc(size: u64) -> void*;
intrinsic function free(ptr: void*) -> void;
intrinsic function memcpy(dest: void*, src: void*, count: u64) -> void*;
intrinsic function strlen(str: string) -> u64;
```

The compiler also provides intrinsic constants that are replaced at compile time:

```cryo
intrinsic const FILE: string;   // expands to the current source file path
intrinsic const LINE: u32;      // expands to the current line number
```

These are used by the standard library's `panic` and `assert` functions to report the location of errors without requiring the caller to pass file/line information manually.

---

## 5. Operators

Cryo provides a rich set of operators that will be familiar to C, C++, and Rust programmers. This section catalogs them and defines their precedence.

### 5.1 Arithmetic Operators

These operate on numeric types. Integer division truncates toward zero. The modulo operator `%` returns the remainder.

| Operator | Description                              |
| -------- | ---------------------------------------- |
| `+`      | Addition                                 |
| `-`      | Subtraction (binary) or negation (unary) |
| `*`      | Multiplication                           |
| `/`      | Division                                 |
| `%`      | Modulo (remainder)                       |
| `++`     | Increment by one (prefix and postfix)    |
| `--`     | Decrement by one (prefix and postfix)    |

The prefix forms (`++x`, `--x`) modify the value and evaluate to the new value. The postfix forms (`x++`, `x--`) modify the value and evaluate to the old value, matching C semantics.

### 5.2 Comparison Operators

Comparison operators return a `boolean`. They work on numeric types and pointers.

| Operator | Description                                                           |
| -------- | --------------------------------------------------------------------- |
| `==`     | Equal                                                                 |
| `!=`     | Not equal                                                             |
| `<`      | Less than                                                             |
| `>`      | Greater than                                                          |
| `<=`     | Less than or equal                                                    |
| `>=`     | Greater than or equal                                                 |
| `<=>`    | Three-way comparison (spaceship); returns negative, zero, or positive |

The spaceship operator `<=>` is borrowed from C++20. It performs a single comparison and returns a value that indicates whether the left operand is less than, equal to, or greater than the right operand, which is useful for implementing custom sort orders.

### 5.3 Logical Operators

Logical operators work on `boolean` values. `&&` and `||` use short-circuit evaluation: if the left operand determines the result, the right operand is not evaluated.

| Operator | Description                                              |
| -------- | -------------------------------------------------------- |
| `&&`     | Logical AND  evaluates right side only if left is `true` |
| `\|\|`   | Logical OR  evaluates right side only if left is `false` |
| `!`      | Logical NOT (unary)                                      |

### 5.4 Bitwise Operators

Bitwise operators work on integer types at the bit level. They are essential for low-level systems programming: flag manipulation, protocol encoding, and hardware register access.

| Operator | Description                                                     |
| -------- | --------------------------------------------------------------- |
| `&`      | Bitwise AND                                                     |
| `\|`     | Bitwise OR                                                      |
| `^`      | Bitwise XOR                                                     |
| `~`      | Bitwise NOT (unary; flips all bits)                             |
| `<<`     | Left shift                                                      |
| `>>`     | Right shift (arithmetic for signed types, logical for unsigned) |

### 5.5 Assignment Operators

The simple assignment `=` stores a value. Compound assignment operators combine an arithmetic or bitwise operation with assignment, so `x += 1` is equivalent to `x = x + 1`. Only `mut` bindings can be assigned to.

| Operator                    | Description                    |
| --------------------------- | ------------------------------ |
| `=`                         | Simple assignment              |
| `+=` `-=` `*=` `/=` `%=`    | Compound arithmetic assignment |
| `&=` `\|=` `^=` `<<=` `>>=` | Compound bitwise assignment    |

### 5.6 Other Operators

These operators serve specialized purposes unique to Cryo's syntax:

| Operator     | Description                                                                  |
| ------------ | ---------------------------------------------------------------------------- |
| `->`         | Return type annotation in function signatures; also pointer member access    |
| `=>`         | Separates pattern from body in a `match` arm                                 |
| `::`         | Scope resolution  accesses static methods, enum variants, and module members |
| `\|>`        | Pipe  passes the left-hand value as an argument to the right-hand function   |
| `??`         | Null coalescing  returns the left side if non-null, otherwise the right side |
| `?.`         | Optional chaining  accesses a member only if the receiver is non-null        |
| `..`         | Range  creates a range value between two bounds                              |
| `...`        | Spread / variadic marker                                                     |
| `as`         | Type cast  explicit type conversion (see [Section 2.9](#29-type-casting))    |
| `?` `:`      | Ternary conditional  `condition ? if_true : if_false`                        |
| `.`          | Member access  field or method on a value                                    |
| `&`          | Address-of (unary)  takes a pointer to a value                               |
| `*`          | Dereference (unary)  follows a pointer to its target value                   |
| `sizeof(T)`  | Evaluates to the size of type `T` in bytes at compile time                   |
| `alignof(T)` | Evaluates to the alignment requirement of type `T` in bytes                  |

### 5.7 Operator Precedence

When multiple operators appear in a single expression, precedence determines the order of evaluation. Higher precedence binds tighter. Parentheses override precedence.

From **lowest** to **highest** precedence:

| Level | Operators                                    | Associativity |
| ----- | -------------------------------------------- | ------------- |
| 1     | `=` `+=` `-=` `*=` `/=` `&=` `\|=`           | Right         |
| 2     | `? :` (ternary)                              | Right         |
| 3     | `\|\|`                                       | Left          |
| 4     | `&&`                                         | Left          |
| 5     | `\|`                                         | Left          |
| 6     | `^`                                          | Left          |
| 7     | `&`                                          | Left          |
| 8     | `==` `!=`                                    | Left          |
| 9     | `<` `>` `<=` `>=` `<=>`                      | Left          |
| 10    | `<<` `>>`                                    | Left          |
| 11    | `+` `-`                                      | Left          |
| 12    | `*` `/` `%`                                  | Left          |
| 13    | `as`                                         | Left          |
| 14    | `-` `!` `&` `*` `~` `++` `--` (unary prefix) | Right         |
| 15    | `()` `[]` `.` `->` `?.` `++` `--` (postfix)  | Left          |

This precedence table follows C conventions, so expressions like `a + b * c` work as expected (`*` binds tighter than `+`). The `as` cast operator sits between multiplicative and unary, so `x * y as i64` casts `y`, not the product  use parentheses if you mean `(x * y) as i64`.

---

## 6. Control Flow

Cryo provides both statement-oriented and expression-oriented control flow. Most constructs (`if`, `while`, `for`, `loop`) will be immediately familiar to C and Rust programmers. The `match` construct, inspired by ML-family languages and Rust, provides exhaustive pattern matching on enums and other values.

### 6.1 If / Else

The `if` statement evaluates a condition and executes the corresponding block. Conditions must be enclosed in parentheses and the body must be a block (braces are required, even for single statements). This prevents the dangling-else ambiguity that plagues C.

```cryo
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}
```

You can chain as many `else if` branches as needed. The `else` branch is optional.

### 6.2 If Expressions

`if` can also be used as an expression that returns a value. When used this way, both the `if` and `else` branches are required, and they must evaluate to the same type. The last expression in each block becomes the branch's value.

```cryo
const is_even: boolean = if (n % 2 == 0) { true } else { false };
```

This is useful for initializing `const` bindings conditionally without needing a mutable variable, keeping the immutability guarantee intact.

### 6.3 While Loops

`while` loops execute their body as long as the condition is true. The condition is checked before each iteration, so the body may execute zero times.

```cryo
mut i: int = 0;
while (i < 10) {
    println("%d", i);
    i = i + 1;
}
```

### 6.4 For Loops

Cryo uses C-style `for` loops with three components: an initializer that declares a loop variable, a condition checked before each iteration, and an update expression evaluated after each iteration.

```cryo
for (mut i: int = 0; i < 10; i++) {
    println("%d", i);
}
```

The loop variable is scoped to the loop body  it does not leak into the surrounding scope. Note that the initializer requires a type annotation, consistent with Cryo's "no inference on variable declarations" rule.

### 6.5 Loop

The `loop` keyword creates an unconditional infinite loop. You exit it with `break`. This is preferred over `while (true)` because it communicates intent more clearly: "this loop runs until explicitly stopped."

```cryo
mut count: int = 0;
loop {
    if (count >= 5) {
        break;
    }
    println("%d", count);
    count = count + 1;
}
```

`loop` is particularly common in event loops, parsers, and any situation where the termination condition is complex or checked at multiple points within the body.

### 6.6 Break and Continue

`break` exits the innermost enclosing loop immediately. `continue` skips the rest of the current iteration and jumps to the next one.

```cryo
for (mut i: int = 0; i < 20; i++) {
    if (i % 2 == 0) {
        continue;           // skip even numbers
    }
    if (i > 10) {
        break;              // stop at 11
    }
    println("%d", i);       // prints: 1, 3, 5, 7, 9
}
```

### 6.7 Match Statements

`match` is Cryo's primary mechanism for branching on the structure of a value. It is most commonly used with enums, where it destructures variants and extracts their payloads. The compiler checks that all possible variants are covered, so forgetting a case is a compile-time error.

```cryo
match (color) {
    Color::Red   => { println("red"); }
    Color::Green => { println("green"); }
    Color::Blue  => { println("blue"); }
}
```

Each arm consists of a pattern, the `=>` separator, and a block. Arms are separated by commas. Unlike C's `switch`, there is no fallthrough between arms. See [Section 10](#10-pattern-matching) for the full pattern syntax.

### 6.8 Match Expressions

When used as an expression, `match` evaluates to the value of the matching arm. All arms must produce the same type. This is invaluable for initializing `const` bindings based on a discriminated value:

```cryo
const name: string = match (n) {
    1 => { "one" }
    2 => { "two" }
    _ => { "other" }
};
```

### 6.9 Switch Statements

For programmers who prefer traditional `switch`/`case` syntax, Cryo provides it as well. `switch` works with integer and enum values and uses `case`/`default` labels.

```cryo
switch (value) {
    case 1: {
        println("one");
    }
    case 2: {
        println("two");
    }
    default: {
        println("other");
    }
}
```

In most idiomatic Cryo code, `match` is preferred because it supports richer patterns (destructuring, ranges, wildcards) and enforces exhaustiveness. `switch` exists primarily for familiarity and for simple integer dispatching.

### 6.10 Ternary Operator

The ternary operator `? :` provides a concise inline conditional expression:

```cryo
const abs: int = x >= 0 ? x : -x;
```

It is right-associative, so `a ? b : c ? d : e` parses as `a ? b : (c ? d : e)`. For complex conditions, prefer `if`/`else` expressions for readability.

### 6.11 Return

`return` exits the current function, optionally with a value. If the function has a non-void return type, the `return` must include a value of that type.

```cryo
function add(a: int, b: int) -> int {
    return a + b;
}
```

Functions may have multiple return points. Early returns are common for error checking and guard clauses.

### 6.12 Unsafe Blocks

The `unsafe` keyword wraps a block of code in which the compiler relaxes certain safety checks. This is intended for low-level operations like raw pointer manipulation, FFI calls, or direct memory access that the compiler cannot statically verify.

```cryo
unsafe {
    const raw: void* = malloc(64);
    // raw pointer operations here
}
```

`unsafe` does not disable all checking  it specifically permits operations that would otherwise be rejected by the type system or the safety analysis. The intent is to quarantine dangerous code into clearly-marked blocks so that the rest of the codebase can be reasoned about with stronger guarantees.

---

## 7. Structs

Structs are Cryo's primary mechanism for defining composite value types. A struct groups related fields together into a single unit that lives on the stack, is passed by value, and can have methods attached to it.

Structs are the right choice when you need a simple data container, a lightweight abstraction over a few related values, or a generic type parameter. They are fast (no heap allocation, no indirection) and predictable (value semantics mean copies are independent).

### 7.1 Struct Declaration

Structs are declared with `type struct` followed by a name and a brace-enclosed list of fields. Each field has a name and a type.

```cryo
type struct Point {
    x: int;
    y: int;
}
```

The `type` prefix distinguishes type declarations from variable declarations and makes them visually scannable in source code. It also groups `type struct`, `type class`, and `type enum` into a consistent declaration family.

### 7.2 Fields and Visibility

Fields are private by default, meaning only code within the same module can access them directly. Use `public`, `private`, or `protected` to control access.

```cryo
type struct Rect {
    public width: int;
    public height: int;
}
```

Fields may have default values, which are used when the field is not explicitly set in a struct literal:

```cryo
type struct Config {
    debug: boolean = false;
    verbose: boolean = false;
}
```

### 7.3 Methods

Methods are functions defined inside a struct body. They receive the struct instance through a special first parameter that declares how the method borrows the instance:

- **`&this`**  the method receives an immutable reference. It can read fields but not modify them. This is the default for "getter" methods, computations, and queries.
- **`mut &this`**  the method receives a mutable reference. It can read and write fields. This is required for methods that change the struct's state.

```cryo
type struct Rect {
    width: int;
    height: int;

    area(&this) -> int {
        return this.width * this.height;
    }

    scale(mut &this, factor: int) -> void {
        this.width = this.width * factor;
        this.height = this.height * factor;
    }
}
```

The `&this` / `mut &this` distinction is visible at the call site in the method signature. A caller can look at the signature and know immediately whether a method will modify the struct without reading its body. This is a key part of Cryo's explicitness philosophy.

### 7.4 Static Methods

Static methods belong to the type itself rather than to a specific instance. They have no `&this` or `mut &this` parameter and are called using the `::` scope resolution operator.

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

The `static new(...)` pattern is Cryo's idiomatic constructor for structs. Since structs are value types (not heap-allocated), there is no `new` keyword involved  the static method simply returns a struct literal. This gives the programmer full control over initialization logic.

### 7.5 Struct Literals

Struct literals create a value by specifying each field by name inside braces. This syntax is used both in standalone expressions and inside `static new` methods.

```cryo
const p: Point = Point { x: 10, y: 20 };
```

Every field must be specified (unless it has a default value). The field order does not need to match the declaration order.

### 7.6 Generic Structs

Structs can be parameterized over types using angle-bracket syntax. The type parameter `T` is available as a type for fields, method parameters, and return types.

```cryo
type struct Pair<T> {
    first: T;
    second: T;

    static new(a: T, b: T) -> Pair<T> {
        return Pair { first: a, second: b };
    }

    swap(mut &this) -> void {
        const temp: T = this.first;
        this.first = this.second;
        this.second = temp;
    }
}
```

Each instantiation with a different concrete type creates a distinct, specialized type at compile time:

```cryo
const ints: Pair<int> = Pair<int>::new(1, 2);
const strs: Pair<string> = Pair<string>::new("hello", "world");
```

`Pair<int>` and `Pair<string>` are completely independent types with no runtime relationship  the compiler generates separate code for each. This is the monomorphization model described in [Section 11.7](#117-monomorphization).

---

## 8. Classes

While structs are stack-allocated value types, classes are **heap-allocated reference types** designed for situations that require single inheritance, virtual method dispatch, and polymorphism. If you come from C++ or Java, Cryo's class system will feel familiar.

Classes exist because some problems genuinely benefit from object hierarchies and runtime polymorphism  GUI frameworks, interpreters with heterogeneous AST nodes, plugin systems. Cryo provides classes for these cases while keeping structs as the default for everything else.

### 8.1 Class Declaration

A class is declared with `type class` followed by a name. Fields and methods are grouped under visibility labels (`public:`, `private:`, `protected:`) in a style reminiscent of C++.

```cryo
type class Person {
public:
    name: string;
    age: i32;

    Person(_name: string, _age: i32) {
        this.name = _name;
        this.age = _age;
    }

    greet(&this) -> void {
        println("Hi, I'm %s, age %d", this.name, this.age);
    }
}
```

Class instances are always allocated on the heap via the `new` keyword and accessed through pointers. This is a deliberate difference from structs: if you're using a class, you've opted into reference semantics and heap allocation.

```cryo
const p: Person* = new Person("Alice", 30);
p.greet();
```

### 8.2 Constructors and Destructors

**Constructors** share the class name and are responsible for initializing the object's fields. Unlike static `new` methods on structs (which are just a convention), class constructors are actual language constructs that the `new` keyword calls.

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

**Destructors** are prefixed with `~` and take no parameters. They are called when the object is deallocated and should release any resources the constructor acquired (memory, file handles, etc.). This is the RAII (Resource Acquisition Is Initialization) pattern familiar from C++.

### 8.3 Inheritance

Cryo supports **single inheritance**: a class may extend exactly one base class. The derived class inherits all fields and methods from its parent. The derived constructor must chain to the parent constructor using `: ParentClass(args)` syntax.

```cryo
type class Animal {
public:
    kind: string;

    Animal(_kind: string) {
        this.kind = _kind;
    }

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

Constructor chaining ensures the base class is always properly initialized before the derived constructor body runs. The chain can extend to any depth:

```cryo
const buddy: Dog* = new Dog("Buddy");
buddy.describe();   // "Animal: Dog"  (inherited from Animal)
buddy.bark();       // "Buddy says: Woof!"
```

Cryo deliberately limits inheritance to a single parent. This avoids the "diamond problem" and the complexity of multiple inheritance while still providing the core benefits of code reuse and polymorphism.

### 8.4 Virtual Methods and Override

To enable **runtime polymorphism**, a base class marks methods as `virtual`. Derived classes then provide their own implementations using `override`. The compiler builds a vtable (virtual function table) behind the scenes, so the correct method is called based on the actual type of the object, not the type of the pointer.

```cryo
type class Shape {
public:
    Shape() {}
    virtual area(&this) -> f64;        // pure virtual  no body, must be overridden
    virtual name(&this) -> string {    // virtual with default implementation
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

A `virtual` method without a body is a **pure virtual** method  any concrete derived class must override it. A `virtual` method with a body provides a default that derived classes may optionally override. The `override` keyword is required on the derived class (not optional as in C++) to make the programmer's intent explicit and to catch typos that would silently create a new method instead of overriding an existing one.

### 8.5 Polymorphic Dispatch

The power of virtual methods is that you can write code that operates on a base class pointer and have it automatically dispatch to the correct derived implementation at runtime:

```cryo
function print_area(shape: Shape*) -> void {
    println("%s: area = %f", shape.name(), shape.area());
}

function main() -> i32 {
    const c: Circle* = new Circle(10.0);
    print_area(c);   // dispatches to Circle::area() and Circle::name()
    return 0;
}
```

This enables powerful design patterns like the Visitor pattern, plugin architectures, and any situation where you have a collection of objects with different concrete types but a shared interface:

```cryo
type class Dog : Animal {
public:
    Dog() : Animal("Dog") {}
    override speak(&this) -> void { println("Woof!"); }
}

type class Cat : Animal {
public:
    Cat() : Animal("Cat") {}
    override speak(&this) -> void { println("Meow!"); }
}

function make_speak(animal: Animal*) -> void {
    animal.speak();  // calls Dog::speak() or Cat::speak() depending on actual type
}
```

### 8.6 Structs vs. Classes

Understanding when to use each is important:

|                      | Struct                            | Class                            |
| -------------------- | --------------------------------- | -------------------------------- |
| **Allocation**       | Stack (value type)                | Heap via `new` (reference type)  |
| **Inheritance**      | No                                | Single inheritance               |
| **Virtual dispatch** | No                                | `virtual` / `override`           |
| **Receivers**        | `&this` / `mut &this`             | `&this` / `mut &this`            |
| **Use when**         | Plain data, small types, generics | Polymorphism, object hierarchies |

**Default to structs.** They are simpler, faster (no heap allocation or pointer indirection), and work with generics and monomorphization. Only reach for classes when you genuinely need inheritance and virtual dispatch.

---

## 9. Enums

Enums in Cryo go far beyond simple integer constants. They are **algebraic data types**  each variant can carry its own data payload, making them ideal for representing values that can be "one of several things, each with different associated data."

If you're familiar with Rust's `enum` or Haskell's algebraic data types, Cryo's enums work the same way. If you're coming from C or Java, think of them as a type-safe tagged union with compiler-enforced exhaustive handling.

### 9.1 Unit Enums

The simplest form: variants with no data. This is equivalent to a C enum.

```cryo
enum Color {
    Red;
    Green;
    Blue;
}

const c: Color = Color::Red;
```

Variants are accessed through the enum name using `::`. This prevents name collisions  `Color::Red` and `TrafficLight::Red` are distinct values.

Variants may have explicit integer values for FFI compatibility or protocol encoding:

```cryo
enum ErrorCode {
    Ok = 0;
    NotFound = 404;
    Internal = 500;
}
```

### 9.2 Data Enums (Algebraic Data Types)

Variants can carry payloads of one or more values. This is what makes Cryo enums truly powerful: a single type can represent multiple shapes of data.

```cryo
enum Shape {
    Circle(f64);              // carries a radius
    Rectangle(f64, f64);     // carries width and height
    Point;                    // carries nothing
}

const s: Shape = Shape::Circle(5.0);
```

A `Shape` value is always exactly one of these three variants, and the compiler enforces that you handle all of them when pattern matching. You cannot accidentally treat a `Circle` as a `Rectangle` because the type system distinguishes them.

This pattern is used throughout the standard library. The compiler's own AST uses data enums extensively to represent different expression and statement types.

### 9.3 Generic Enums

Enums can be parameterized over types, which is how the standard library defines its two most important types:

```cryo
enum Option<T> {
    Some(T);
    None;
}

enum Result<T, E> {
    Ok(T);
    Err(E);
}
```

`Option<T>` represents a value that may or may not be present  it replaces null in most contexts. `Result<T, E>` represents an operation that may succeed with a value of type `T` or fail with an error of type `E`.

```cryo
const val: Option<int> = Option::Some(42);
const res: Result<string, int> = Result::Ok("success");
```

Because these are monomorphized, `Option<int>` and `Option<string>` are fully independent types at the machine code level.

### 9.4 Implement Blocks on Enums

Since enums in Cryo cannot contain methods inline (unlike structs), you add methods to them using `implement` blocks. This is how `Option` and `Result` get their rich API:

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
            Option::None => {
                panic("called unwrap() on None", FILE, LINE);
            }
        }
    }

    unwrap_or(&this, default_value: T) -> T {
        match (this) {
            Option::Some(value) => { return value; }
            Option::None        => { return default_value; }
        }
    }

    map<U>(&this, f: (T) -> U) -> Option<U> {
        match (this) {
            Option::Some(value) => { return Option::Some(f(value)); }
            Option::None        => { return Option::None; }
        }
    }
}
```

Implement blocks are covered in more detail in [Section 12](#12-implement-blocks).

---

## 10. Pattern Matching

Pattern matching is one of Cryo's most powerful features. It lets you branch on the structure of a value, decompose compound values into their parts, and ensure at compile time that all cases are handled. If `match` is the mechanism, patterns are the language you use to describe what you're looking for.

### 10.1 Patterns

A pattern describes the shape of a value. When a value matches a pattern, any variables in the pattern are bound to the corresponding parts of the value.

| Pattern Kind     | Syntax                  | What It Matches                        |
| ---------------- | ----------------------- | -------------------------------------- |
| Literal          | `42`, `"hello"`, `true` | Exactly that value                     |
| Identifier       | `x`                     | Any value; binds it to the name `x`    |
| Wildcard         | `_`                     | Any value; discards it (no binding)    |
| Enum variant     | `Color::Red`            | That specific variant (no payload)     |
| Enum destructure | `Shape::Circle(r)`      | That variant; binds the payload to `r` |
| Range            | `1..10`                 | Any value in the range                 |

### 10.2 Enum Destructuring

The real power of patterns shows when you match on enums with data. The pattern names the variant and introduces variables for each payload field:

```cryo
function describe(s: Shape) -> void {
    match (s) {
        Shape::Circle(r) => {
            println("Circle with radius %f", r);
        }
        Shape::Rectangle(w, h) => {
            println("Rectangle %f x %f", w, h);
        }
        Shape::Point => {
            println("A point");
        }
    }
}
```

In the `Circle` arm, the variable `r` is bound to the `f64` payload. In the `Rectangle` arm, `w` and `h` are bound to the two `f64` payloads. The compiler checks that the number and types of pattern variables match the variant's declaration.

If you don't need a payload value, use `_` to discard it: `Option::Some(_) => { ... }`.

### 10.3 Wildcard Pattern

The wildcard pattern `_` matches any value without binding it. It is used as a catch-all arm to handle "everything else":

```cryo
match (n) {
    1 => { println("one"); }
    2 => { println("two"); }
    _ => { println("other"); }
}
```

Without the wildcard, this match would be incomplete (not all integers are covered), and the compiler would report an error. The wildcard acts as the default case.

### 10.4 Range Patterns

Range patterns match values within an inclusive range. They are useful for classifying characters or grouping numeric ranges:

```cryo
match (ch) {
    '0'..'9' => { println("digit"); }
    'a'..'z' => { println("lowercase"); }
    _         => { println("other"); }
}
```

---

## 11. Generics

Generics let you write code that works across multiple types without sacrificing type safety. Instead of duplicating a `Pair_int` and a `Pair_string`, you write `Pair<T>` once, and the compiler generates specialized versions for each type you actually use.

Cryo's generics model is **monomorphization**  the same approach used by Rust and C++ templates. At compile time, each generic instantiation produces a dedicated copy of the code, specialized for the concrete types. This means generics have **zero runtime overhead**: there are no vtables, no boxing, and no type erasure.

### 11.1 Generic Type Parameters

Generic parameters are declared in angle brackets after the name of a type or function. They act as placeholders that are filled in with concrete types at each use site.

```
<T>              // single parameter
<T, U>           // multiple parameters
<T: Comparable>  // bounded parameter (must satisfy a trait constraint)
```

By convention, type parameters use single uppercase letters: `T` for a general type, `E` for an error type, `K` and `V` for key-value pairs.

### 11.2 Generic Structs

A generic struct works exactly like a regular struct, except that one or more field types are parameterized. The standard library's `Box<T>` is a canonical example  a heap-allocated container for a single value:

```cryo
type struct Box<T> {
    ptr: T*;

    static new(value: T) -> Box<T> {
        const p: T* = malloc(sizeof(T));
        *p = value;
        return Box { ptr: p };
    }

    deref(&this) -> T {
        return *this.ptr;
    }

    drop(&this) -> void {
        free(this.ptr);
    }
}
```

When you write `Box<int>`, the compiler generates a version of this struct where every `T` is replaced with `int`. `Box<string>` generates a different, independent version.

### 11.3 Generic Enums

Enums with type parameters are how the standard library models optional values and error handling:

```cryo
enum Result<T, E> {
    Ok(T);
    Err(E);
}
```

This single definition creates an infinite family of concrete types: `Result<int, string>`, `Result<User, DatabaseError>`, and so on. Each is a distinct type at compile time.

### 11.4 Generic Functions

Functions can also be parameterized:

```cryo
function identity<T>(x: T) -> T {
    return x;
}

function min<T>(a: T, b: T) -> T {
    if (a < b) {
        return a;
    }
    return b;
}
```

At the call site, you provide the concrete type:

```cryo
const n: int = identity<int>(42);
```

### 11.5 Generic Implement Blocks

When you add methods to a generic type via an `implement` block, the block itself is generic. The methods can introduce additional type parameters of their own:

```cryo
implement enum Result<T, E> {
    is_ok(&this) -> boolean {
        match (this) {
            Result::Ok(_)  => { return true; }
            Result::Err(_) => { return false; }
        }
    }

    map<U>(&this, op: (T) -> U) -> Result<U, E> {
        match (this) {
            Result::Ok(value)  => { return Result::Ok(op(value)); }
            Result::Err(err)   => { return Result::Err(err); }
        }
    }

    and_then<U>(&this, op: (T) -> Result<U, E>) -> Result<U, E> {
        match (this) {
            Result::Ok(value) => { return op(value); }
            Result::Err(err)  => { return Result::Err(err); }
        }
    }
}
```

Here, the implement block is parameterized over `<T, E>` (from the `Result` type), and methods like `map` introduce an additional parameter `<U>` for the transformed type.

### 11.6 Where Clauses

Where clauses constrain generic parameters to types that satisfy certain trait bounds. This ensures that operations used inside the generic code (like comparison) are actually available for the type:

```cryo
function sort<T>(arr: Array<T>) -> Array<T>
    where T: Comparable {
    // The compiler knows T supports <, >, == because of the bound
}
```

Without the `where T: Comparable` clause, the compiler would reject `a < b` inside a generic function because it cannot prove that an arbitrary type `T` supports comparison.

### 11.7 Monomorphization

Monomorphization is the compilation strategy that makes generics zero-cost. When the compiler encounters:

```cryo
const a: Pair<int> = Pair<int>::new(1, 2);
const b: Pair<string> = Pair<string>::new("x", "y");
```

It generates two completely independent types and function bodies  conceptually equivalent to:

```
// Compiler-generated (not actual source code):
Pair_int { first: int, second: int }
Pair_int::new(a: int, b: int) -> Pair_int { ... }

Pair_string { first: string, second: string }
Pair_string::new(a: string, b: string) -> Pair_string { ... }
```

The trade-off: monomorphization can increase binary size because each instantiation produces its own code. But it eliminates all runtime overhead  no vtables, no type checks, no boxing allocations. For a systems language where performance predictability matters, this is the right default.

---

## 12. Implement Blocks

Implement blocks let you add methods to a type without modifying its original declaration. They are Cryo's way of extending types after the fact, and they are the only way to add methods to enums (since enums do not support inline method definitions).

```cryo
implement enum Color {
    to_string(&this) -> string {
        match (this) {
            Color::Red   => { return "red"; }
            Color::Green => { return "green"; }
            Color::Blue  => { return "blue"; }
        }
    }
}
```

You can use implement blocks on structs as well, which is useful for separating a type's core definition from extended functionality:

```cryo
implement struct Point {
    distance_to(&this, other: Point) -> f64 {
        const dx: f64 = (this.x - other.x) as f64;
        const dy: f64 = (this.y - other.y) as f64;
        return sqrt(dx * dx + dy * dy);
    }
}
```

Perhaps most surprisingly, implement blocks can extend **primitive types**. This is how the standard library adds methods to `boolean`, integer types, and other built-in types:

```cryo
implement boolean {
    to_i32(&this) -> i32 {
        if (&this) {
            return 1;
        }
        return 0;
    }

    static default() -> boolean {
        return false;
    }
}
```

This means you can call `my_bool.to_i32()` on any boolean value. The method is resolved at compile time and inlined  there is no dynamic dispatch.

---

## 13. Modules and Imports

Cryo organizes code into modules using a namespace system. Every source file declares its namespace, and files reference each other through imports. The system is hierarchical (names are separated by `::`) and supports visibility control.

### 13.1 Namespaces

Every Cryo source file begins with a `namespace` declaration that establishes the file's position in the module hierarchy.

```cryo
namespace MyApp;                    // top-level namespace
namespace MyApp::Utils;             // nested namespace
namespace std::collections::array;  // standard library convention
```

The namespace serves as the file's identity within the project. The compiler uses it to resolve imports, generate mangled symbol names, and prevent name collisions. A file's namespace does not need to match its filesystem path, but by convention they correspond.

### 13.2 Imports

The `import` statement brings names from other modules into the current file's scope. Cryo supports several import forms for different use cases:

```cryo
import Math::Vector;                           // import the module itself
import Math::Vector::Vec2, Math::Vector::Vec3; // import specific items
import * from Math::Vector;                    // wildcard: import everything public
import Math::Vector as V;                      // aliased: use V instead of Math::Vector
import Math::Vector::{ Vec2, Vec3 };           // destructured: selective import
```

Wildcard imports (`import * from ...`) are convenient but can cause name collisions. In general, prefer importing specific items or using the module name directly.

### 13.3 Module Declarations

Multi-file projects use `_module.cryo` aggregator files to define the public interface of a directory. This is similar to Rust's `mod.rs` pattern. The aggregator declares which submodules are part of the module and which are public:

```cryo
// collections/_module.cryo
namespace std::collections;

public module collections::array;
public module collections::string;
public module collections::hashmap;
public module collections::deque;
```

When another file imports `std::collections`, it sees only the modules declared `public` in this aggregator. This provides clean API boundaries between modules.

### 13.4 Visibility

Visibility modifiers control what is accessible outside a module:

| Modifier    | Meaning                                                           |
| ----------- | ----------------------------------------------------------------- |
| *(none)*    | Private  only accessible within the same module                   |
| `public`    | Accessible to any module that imports this one                    |
| `private`   | Explicitly private (same as the default)                          |
| `protected` | Accessible to the current class and its subclasses (classes only) |

The default of private-by-default encourages good API design: only the things you explicitly choose to expose become part of your module's public interface.

---

## 14. Pointers and Memory

Cryo is a systems language, and that means giving programmers direct control over memory. There is no garbage collector. Memory is managed manually through explicit allocation and deallocation, or through RAII patterns using class destructors.

This gives you predictable performance (no GC pauses) and fine-grained control (you decide what lives on the stack, what goes on the heap, and when it's freed). The trade-off is responsibility: if you forget to free memory, it leaks; if you use memory after freeing it, you get undefined behavior.

### 14.1 Address-of and Dereference

The `&` operator takes the address of a value, producing a pointer. The `*` operator dereferences a pointer, accessing the value it points to.

```cryo
mut x: int = 42;
const ptr: int* = &x;        // ptr holds the address of x
const val: int = *ptr;       // val is 42 (read through the pointer)
```

Pointers support array indexing, so `ptr[0]` is equivalent to `*ptr`, and `ptr[n]` accesses the n-th element from the pointer's base address.

### 14.2 Heap Allocation

For dynamically-sized data or data that must outlive the current scope, you allocate on the heap.

**Low-level allocation** with `malloc`/`free` (from the intrinsics):

```cryo
const buf: u8* = malloc(1024);   // allocate 1024 bytes
buf[0] = 0xFF;                   // use the memory
free(buf);                       // release it when done
```

**Object allocation** with `new` (for classes):

```cryo
const p: Person* = new Person("Alice", 30);
// p is a heap-allocated Person*, freed when you delete it
```

**Array allocation** with `new`:

```cryo
const arr: int* = new int[100];  // allocate space for 100 ints
```

The standard library's `Box<T>`, `Array<T>`, and `String` types wrap these primitives with safer, higher-level APIs that handle allocation and deallocation internally.

### 14.3 Null

`null` is the null pointer literal. It represents "points to nothing" and is valid for any pointer type.

```cryo
const p: int* = null;
if (p == null) {
    println("null pointer");
}
```

Dereferencing a null pointer is undefined behavior. For optional values, prefer `Option<T>` over nullable pointers  it forces explicit handling of the "absent" case at compile time.

---

## 15. Directives

Directives are compile-time annotations that modify how the compiler treats a declaration. They use `#[...]` syntax, placed before the declaration they apply to.

```cryo
#[deprecated]
function old_api() -> void { }

#[inline]
function hot_path(x: int) -> int {
    return x * 2;
}

#[packed]
type struct PackedData {
    a: u8;
    b: u32;
}

#[aligned(16)]
type struct AlignedData {
    data: f64;
}
```

**Available directives:**

| Directive       | Description                                                                |
| --------------- | -------------------------------------------------------------------------- |
| `deprecated`    | Marks a declaration as deprecated; the compiler emits a warning on use     |
| `inline`        | Suggests the compiler inline the function at call sites                    |
| `noinline`      | Prevents inlining even if the optimizer would normally inline it           |
| `pure`          | Asserts the function has no side effects (enables aggressive optimization) |
| `const`         | Marks a function as evaluable at compile time                              |
| `noreturn`      | Indicates the function never returns normally (e.g., `panic`, `exit`)      |
| `packed`        | Removes padding between struct fields (useful for binary protocols, FFI)   |
| `aligned(N)`    | Sets the minimum alignment of a type to N bytes                            |
| `section(name)` | Places the symbol in a specific object-file section                        |
| `weak`          | Declares weak linkage (can be overridden by a strong symbol)               |
| `constructor`   | Runs the function automatically before `main`                              |
| `destructor`    | Runs the function automatically after `main` returns                       |

Directives are metadata that guide the compiler and linker. They do not change the semantics of the code itself  `#[inline]` does not guarantee inlining, it only hints at it. The compiler is free to ignore hints when they would not improve performance.

---

## 16. Foreign Function Interface (FFI)

Cryo is designed to interoperate seamlessly with C. Since the compiler uses an LLVM backend and follows C calling conventions, calling C functions from Cryo (and vice versa) is straightforward.

### 16.1 Extern Blocks

The simplest FFI mechanism: declare C function signatures using Cryo syntax inside an `extern "C"` block. The compiler trusts these declarations and the linker resolves the actual function from the C library.

```cryo
extern "C" {
    function puts(s: string) -> int;
    function atoi(s: string) -> int;
}
```

Standalone extern declarations are also supported:

```cryo
extern function exit(code: int) -> void;
```

These declarations tell the compiler the function's type signature without providing a body. It is the programmer's responsibility to ensure the Cryo signature matches the actual C signature  the compiler cannot verify this across the language boundary.

### 16.2 C Header Import

For larger C libraries, manually transcribing every function signature is tedious and error-prone. Cryo's `extern "CImport"` blocks automate this by importing C header files directly. The compiler parses the headers and generates bindings automatically.

```cryo
extern "CImport" c {
    #include <stdio.h>
    #include <stdlib.h>
    #include "./my_header.h"
}

function main() -> int {
    c::printf("Value: %d\n", 42);
    return 0;
}
```

The identifier after `"CImport"` (here, `c`) creates a namespace for the imported functions. You access them with `::` scope resolution: `c::printf(...)`, `c::malloc(...)`. This prevents name collisions between C functions and Cryo functions.

---

## 17. Standard Library

The standard library is written entirely in Cryo and compiled as a static library (`libcryo.a`). It provides the core types, collections, I/O facilities, and utility functions that most programs need.

### 17.1 Prelude

The **prelude** is a special module that is automatically imported into every Cryo source file. You don't need to write any `import` statement to use these types and functions:

**Types:** `Option<T>`, `Result<T, E>`, `Array<T>`, `String`, `Box<T>`

**Functions:** `print`, `println`, `panic`, `assert`, `assert_eq`, `min`, `max`, `clamp`, `swap`, `identity`

The prelude is deliberately small  it includes only the types and functions that are so universally useful that requiring an import would be noise. Everything else lives in explicit modules that you import as needed.

### 17.2 Core Modules

The standard library is organized into domain-specific modules. Each module focuses on a single concern and can be imported independently.

| Module                      | Contents                                                                                                             |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `core::option`              | `Option<T>` with methods: `is_some`, `is_none`, `unwrap`, `unwrap_or`, `map`, `and_then`, `filter`, `take`           |
| `core::result`              | `Result<T, E>` with methods: `is_ok`, `is_err`, `unwrap`, `map`, `map_err`, `and_then`                               |
| `core::primitives`          | Methods on built-in types (`boolean.to_i32()`, etc.)                                                                 |
| `core::intrinsics`          | Compiler intrinsics: `malloc`, `free`, `memcpy`, `strlen`, `printf`, and more                                        |
| `core::mem`                 | Memory utilities: `size_of`, `align_of`, `zeroed`                                                                    |
| `core::ptr`                 | `NonNull<T>` smart pointer, pointer utilities                                                                        |
| `core::ops`                 | `Range<T>`, `RangeInclusive<T>`                                                                                      |
| `core::convert`             | Type conversion functions between numeric types                                                                      |
| `alloc::heap`               | `Box<T>` (single-value heap allocation), `RawVec<T>`, `HeapArray<T>`                                                 |
| `alloc::arena`              | Arena allocator for bulk allocation with single-point deallocation                                                   |
| `collections::array`        | `Array<T>`  growable, heap-backed dynamic array with `push`, `pop`, `insert`, `remove`, `get`, `set`, `len`, `clear` |
| `collections::string`       | `String`  heap-allocated, growable UTF-8 string with `push_char`, `append`, `len`, `substr`, `split`                 |
| `collections::hashmap`      | `HashMap<K, V>`  hash table with `insert`, `get`, `remove`, `contains_key`, `len`                                    |
| `collections::hashset`      | `HashSet<T>`  set backed by a hash table                                                                             |
| `collections::deque`        | `Deque<T>`  double-ended queue                                                                                       |
| `collections::btree`        | `BTreeMap<K, V>`  ordered map backed by a B-tree                                                                     |
| `collections::linkedlist`   | `LinkedList<T>`  doubly-linked list                                                                                  |
| `collections::pair`         | `Pair<T, U>`  a simple two-element tuple struct                                                                      |
| `io::stdio`                 | `stdin`, `stdout`, `stderr` handles; `print`, `println` functions                                                    |
| `io::file`                  | File I/O: open, read, write, close                                                                                   |
| `io::reader` / `io::writer` | Buffered I/O abstractions                                                                                            |
| `fs`                        | File system operations: paths, metadata, directory listing, `FileType` enum                                          |
| `math`                      | Mathematical functions and constants (`sqrt`, `sin`, `cos`, `PI`, `E`)                                               |
| `fmt`                       | String formatting utilities                                                                                          |
| `ffi`                       | C type aliases (`c_int`, `c_char`, `c_size_t`), `CString`                                                            |
| `env`                       | Environment variables, command-line arguments                                                                        |
| `process`                   | Process spawning, exit codes                                                                                         |
| `time`                      | Time measurement, `Duration`, `sleep`                                                                                |
| `os`                        | OS abstractions                                                                                                      |
| `sync`                      | Synchronization primitives (mutexes, etc.)                                                                           |
| `thread`                    | Threading support                                                                                                    |

---

## 18. Appendix: Grammar Summary

The complete formal grammar for the Cryo language is specified in [grammar.md](grammar.md) using Extended Backus-Naur Form (EBNF) notation following ISO/IEC 14977. What follows is a condensed overview of the key productions.

### Program Structure

```
program         = { directive } [ namespace_decl ] { top_level_item }
top_level_item  = import_decl | module_decl | var_declaration
                | function_declaration | extern_function_decl | extern_block
                | intrinsic_decl | struct_declaration | class_declaration
                | enum_declaration | type_alias_declaration
                | implementation_block
```

### Statements

```
statement       = var_declaration | function_declaration | struct_declaration
                | class_declaration | enum_declaration | type_alias_declaration
                | implementation_block | if_statement | while_statement
                | for_statement | loop_statement | match_statement
                | switch_statement | break_statement | continue_statement
                | return_statement | unsafe_block | block
                | expression_statement
```

### Expression Hierarchy

Expressions are organized into a precedence hierarchy where each level delegates to the next-higher-precedence level:

```
expression          = assignment_expr
assignment_expr     = conditional_expr [ assignment_op assignment_expr ]
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

For the full grammar including all type annotations, literal definitions, and lexical elements, see [grammar.md](grammar.md).
