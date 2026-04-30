<div align="center">
  <img src="./assets/cryo-logo-1.png" alt="Cryo" width="180"/>
    <br/>
  <h1>The Cryo Programming Language</h1>
  <h4><i>pre-0.1.0 — under heavy development</i></h4>

  <p>A statically-typed, compiled systems language with monomorphic generics, class inheritance, pattern matching, algebraic data types, and an LLVM 20 backend.</p>
</div>

---

```cryo
namespace HelloWorld;

function main() -> int {
    println("Hello, world!");
    return 0;
}
```

## Table of Contents

- [Features](#features)
- [Getting Started](#getting-started)
- [Language Overview](#language-overview)
  - [Variables](#variables)
  - [Functions](#functions)
  - [Control Flow](#control-flow)
  - [Structs](#structs)
  - [Classes & Inheritance](#classes--inheritance)
  - [Enums & Pattern Matching](#enums--pattern-matching)
  - [Generics](#generics)
  - [Modules](#modules)
  - [Pointers & Memory](#pointers--memory)
- [FFI (Foreign Function Interface)](#ffi-foreign-function-interface)
- [Type System](#type-system)
- [Standard Library](#standard-library)
- [Building from Source](#building-from-source)
- [Architecture](#architecture)
- [Project Layout](#project-layout)
- [License](#license)

## Features

| | |
|---|---|
| **Strong static typing** | Explicit type annotations with no implicit conversions |
| **Monomorphic generics** | Compile-time specialization with zero runtime overhead |
| **Class inheritance** | Single inheritance, virtual methods, polymorphic dispatch |
| **Algebraic data types** | Enums with payloads and exhaustive pattern matching |
| **Module system** | Hierarchical namespaces with visibility control and a prelude |
| **LLVM 20 backend** | Optimizing compilation to native x86-64 / ARM64 |
| **Self-hosting** | The compiler is written in Cryo. The C++ bootstrap is retained for emergency rebuilds in [`./legacy/bootstrap/`](./legacy/bootstrap/). |
| **Rich standard library** | `Option<T>`, `Result<T, E>`, `Array<T>`, `String`, allocators, I/O, more |

## Getting Started

### Prerequisites

| Dependency | Version |
|---|---|
| LLVM | 20 |
| Clang | 20 |
| GNU Make | 4.0+ |
| Python | 3.8+ (used by `make selfhost-check`) |

### Install

```bash
git clone https://github.com/jakelequire/CryoLang.git
cd CryoLang
./install.sh
```

`install.sh` runs `make cryo` (about 5 minutes on a cold cache) to build
the self-hosted compiler in place at `compiler/build/bin/cryo`, then
appends a `PATH` export to your shell rc so you can run `cryo` from
anywhere.

> **In-tree limitation (read this).** This release locates the standard
> library via a relative path (`<project_root>/../stdlib`). Until that's
> generalized, your projects must live as a sibling of `stdlib/` — the
> simplest pattern is to put projects inside the repo, e.g.
> `CryoLang/sandbox/myapp/`. System-wide install is not yet supported.

### Hello World

Create `sandbox/hello/cryoconfig`:

```toml
[project]
project_name = "hello"
output_dir = "build"
target_type = "executable"
entry_point = "main.cryo"
```

Create `sandbox/hello/main.cryo`:

```cryo
namespace Hello;

function main() -> int {
    println("Hello, world!");
    return 0;
}
```

Build and run:

```bash
cd sandbox/hello
cryo build
./build/bin/hello
```

## Language Overview

### Variables

All variables require explicit type annotations. Bindings are immutable by default.

```cryo
const name: string = "Cryo";       // immutable
mut counter: int = 0;              // mutable
counter = counter + 1;
```

### Functions

```cryo
function add(a: int, b: int) -> int {
    return a + b;
}

function greet(name: string) -> void {
    println("Hello, %s!", name);
}
```

### Control Flow

```cryo
// if / else
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}

// if expressions can return values
const is_even: boolean = if (n % 2 == 0) { true } else { false };

// for loop
for (mut i: int = 0; i < 10; i++) {
    println("%d", i);
}

// while loop
while (condition) {
    // ...
}

// infinite loop
loop {
    if (done) { break; }
}

// match (integers, enums)
match (n) {
    1 => { println("one"); }
    2 => { println("two"); }
    _ => { println("other"); }
}

// match expressions can return values
const parity: string = match (n % 2) {
    0 => { "even" }
    1 => { "odd" }
    _ => { "unknown" }
};

// ternary
const abs: int = x >= 0 ? x : -x;
```

### Structs

Structs define value types with fields and methods. Methods use `&this`
for immutable access and `mut &this` for mutation.

```cryo
type struct Rect {
    width: int;
    height: int;

    static new(w: int, h: int) -> Rect {
        return Rect { width: w, height: h };
    }

    area(&this) -> int {
        return this.width * this.height;
    }

    scale(mut &this, factor: int) -> void {
        this.width = this.width * factor;
        this.height = this.height * factor;
    }
}

function main() -> int {
    mut r: Rect = Rect::new(5, 10);
    println("Area: %d", r.area());    // 50
    r.scale(2);
    println("Area: %d", r.area());    // 200
    return 0;
}
```

### Classes & Inheritance

Classes are heap-allocated reference types that support single inheritance,
constructor chaining, virtual methods, and polymorphic dispatch.

```cryo
type class Animal {
public:
    name: string;

    Animal(_name: string) {
        this.name = _name;
    }

    virtual speak() -> void;
}

type class Dog : Animal {
public:
    Dog() : Animal("Dog") {}

    override speak(&this) -> void {
        println("%s speaks: Woof!", this.name);
    }
}

type class Cat : Animal {
public:
    Cat() : Animal("Cat") {}

    override speak(&this) -> void {
        println("%s speaks: Meow!", this.name);
    }
}

function make_speak(animal: Animal*) -> void {
    animal.speak();
}

function main() -> i32 {
    const dog: Dog* = new Dog();
    const cat: Cat* = new Cat();
    make_speak(dog);    // Dog speaks: Woof!
    make_speak(cat);    // Cat speaks: Meow!
    return 0;
}
```

#### Structs vs. Classes

| | Struct | Class |
|---|---|---|
| **Allocation** | Stack (value type) | Heap via `new` (reference type) |
| **Inheritance** | No | Single inheritance |
| **Virtual dispatch** | No | `virtual` / `override` |
| **Receivers** | `&this` / `mut &this` | `&this` / `mut &this` |
| **Use when** | Plain data, small types, generics | Polymorphism, object hierarchies |

### Enums & Pattern Matching

Enums support unit variants and variants with payloads. Pattern matching
is exhaustive.

```cryo
type enum Shape {
    Circle(f64);
    Rectangle(f64, f64);
    Point;
}

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

Enums can be extended with methods via `implement` blocks:

```cryo
implement enum Shape {
    is_circle(&this) -> boolean {
        return match (this) {
            Shape::Circle(_) => { true }
            _ =>                { false }
        }
    }
}
```

### Generics

Cryo uses monomorphization — generic code is specialized at compile time
for each concrete type used, producing zero-overhead abstractions.

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

const ints: Pair<int> = Pair<int>::new(1, 2);
const strs: Pair<string> = Pair<string>::new("hello", "world");
```

Generic enums power the standard library's core types:

```cryo
type enum Option<T> {
    Some(T);
    None;
}

type enum Result<T, E> {
    Ok(T);
    Err(E);
}
```

Generic functions:

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

### Modules

Every file declares a namespace. Modules are organized using
`_module.cryo` files that re-export submodules — similar to Rust's `mod.rs`.

```cryo
// math/_module.cryo
namespace Math;

public module vector;
public module matrix;
```

```cryo
// math/vector.cryo
namespace Math::Vector;

type struct Vec2 {
    x: f64;
    y: f64;

    static new(x: f64, y: f64) -> Vec2 {
        return Vec2 { x: x, y: y };
    }
}
```

```cryo
// main.cryo
namespace Main;

import Math::Vector;

function main() -> int {
    const v: Vec2 = Vec2::new(1.0, 2.0);
    return 0;
}
```

Items are private by default; use `public` to export them.

### Pointers & Memory

Cryo provides explicit pointer operations for systems-level control.

```cryo
function example() -> void {
    mut x: int = 42;
    const ptr: int* = &x;          // address-of
    println("%d", *ptr);           // dereference

    // Heap allocation
    const buf: int* = malloc(sizeof(int) * 10);
    buf[0] = 100;
    free(buf);
}
```

## FFI (Foreign Function Interface)

Cryo can call C functions and be called from C. Use `extern "C"` blocks
to declare foreign functions, or `name := extern "C" { ... }` to import C
header files directly under a chosen namespace.

```cryo
namespace FFI;

// Declare a C function manually
extern "C" {
    function puts(s: string) -> int;
}

// Import C functions from a header file under the `c` namespace
c := extern "C" {
    #include <stdio.h>
    #include "./my_header.h"        // void foo(int);
}

function main() -> int {
    puts("Hello from C!");
    c::foo(42);
    c::printf("Value: %d\n", 123);
    return 0;
}
```

## Type System

### Primitive Types

| Type | Description |
|---|---|
| `i8` `i16` `i32` `i64` | Signed integers |
| `u8` `u16` `u32` `u64` | Unsigned integers |
| `int` | Platform integer (i32) |
| `f32` `f64` | Floating-point |
| `boolean` | `true` / `false` |
| `char` | 8-bit character |
| `string` | Null-terminated string (`char*`) |
| `void` | No value |
| `()` | Unit type |

### Type Casting

```cryo
const a: i64 = 42;
const b: i32 = a as i32;
```

### Operators

| Category | Operators |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `<<` `>>` |
| Assignment | `=` `+=` `-=` `*=` `/=` `++` `--` |

## Standard Library

The standard library lives at [`./stdlib/`](./stdlib/) and is written
entirely in Cryo. A **prelude** automatically imports the most common
types and functions into every file.

### Prelude (auto-imported)

`Option<T>`, `Result<T, E>`, `Array<T>`, `String`, `print`, `println`,
`assert`, `assert_eq`, `panic`, `min`, `max`, `clamp`, `swap`, `identity`,
and more.

### Modules

| Path | Contents |
|---|---|
| `stdlib/alloc/` | Arena, heap, stack, pool allocators |
| `stdlib/collections/` | Array, String, HashMap, Deque, BTreeMap |
| `stdlib/core/` | Option, Result, primitives, intrinsics |
| `stdlib/env/` | Environment variables, args |
| `stdlib/ffi/` | Foreign function interface (C interop) |
| `stdlib/fmt/` | Formatting |
| `stdlib/fs/` | File system, paths |
| `stdlib/io/` | stdio, file, reader, writer |
| `stdlib/math/` | Math functions and constants |
| `stdlib/os/` | OS abstractions, threads, synchronization |
| `stdlib/process/` | Process spawning, exit codes |
| `stdlib/time/` | Time, duration, sleep |

A second-generation rewrite is parked at
[`experimental/stdlib-next/`](./experimental/stdlib-next/). It is **not
built or shipped**; it will replace `stdlib/` once the compiler grows
the features it depends on.

## Building from Source

The top-level `Makefile` orchestrates the full chain.

| Target | Time | Output |
|---|---|---|
| `make bootstrap` | ~3 min cold | `legacy/bootstrap/bin/cryo` |
| `make stdlib` | ~30 s | `stdlib/.bin/libcryo.a` |
| `make cryo` | ~5 min cold | `compiler/build/bin/cryo` (the working compiler) |
| `make selfhost-check` | ~3-10 min | Verifies stage-4 / stage-5 IR byte-identity |
| `make clean` | instant | Wipes compiler + stdlib outputs |
| `make distclean` | instant | Also cleans bootstrap |

Daily flow: `make cryo`. Pre-tag / pre-merge gate: `make selfhost-check`.

## Architecture

The compiler runs a multi-pass pipeline:

```
Source → Frontend → Module Resolution → Declaration Collection → Type Resolution
     → Semantic Analysis → Specialization → Codegen Preparation → IR Generation
     → Linking → Native Binary
```

| Stage | Purpose |
|---|---|
| **Frontend** | Lexing and parsing into AST |
| **Module Resolution** | Resolve imports and module dependencies |
| **Declaration Collection** | Gather all type and function declarations |
| **Type Resolution** | Resolve all type references and annotations |
| **Semantic Analysis** | Validate correctness and scope checking |
| **Specialization** | Monomorphize generic instantiations |
| **Codegen Preparation** | Multi-pass type and declaration ordering |
| **IR Generation** | Emit LLVM IR via the LLVM 20 C++ API |
| **Linking** | Link object files + libcryo.a → executable |

## Project Layout

```
CryoLang/
├── compiler/              The self-hosted Cryo compiler (active)
│   ├── src/
│   ├── cryoconfig
│   └── llvm_bindings.h
├── stdlib/                The current standard library (active, ~25k LOC, 53 modules)
├── legacy/
│   └── bootstrap/         Frozen C++23 bootstrap; builds via `make bootstrap`
├── experimental/
│   └── stdlib-next/       Parked stdlib rewrite; not built or shipped
├── tools/                 LSP / formatter / analyzer (not maintained for 0.1)
├── docs/                  Language reference, grammar, mangling spec
├── examples/              Standalone Cryo programs
├── scripts/               Build helpers
├── assets/
├── .github/
├── Makefile               Top-level build orchestration
├── install.sh             PATH-wrapper installer
├── CHANGELOG.md
├── CONTRIBUTING.md
└── LICENSE
```

## License

Licensed under the [Apache License 2.0](LICENSE).

Copyright 2025 Jacob LeQuire.
