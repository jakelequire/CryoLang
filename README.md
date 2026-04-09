<div align="center">
  <img src="./assets/cryo-logo.svg" alt="Cryo" width="180"/>

  <h1>The Cryo Programming Language</h1>
  <h4><i>Under Development</i></h4>

  <p>A statically-typed, compiled systems language with monomorphic generics, class inheritance, pattern matching, algebraic data types, and an LLVM backend.</p>
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
- [Tooling](#tooling)
- [Building from Source](#building-from-source)
- [Architecture](#architecture)
- [License](#license)

## Features

| | |
|---|---|
| **Strong static typing** | Explicit type annotations with no implicit conversions |
| **Monomorphic generics** | Compile-time specialization with zero runtime overhead |
| **Class inheritance** | Single inheritance, virtual methods, and polymorphic dispatch |
| **Algebraic data types** | Enums with payloads and exhaustive pattern matching |
| **Module system** | Hierarchical namespaces with visibility control and a prelude |
| **LLVM 20 backend** | Optimizing compilation to native x86-64, ARM64, and WebAssembly |
| **Rich standard library** | `Option<T>`, `Result<T, E>`, `Array<T>`, `String`, allocators, I/O, and more |
| **Self-hosting** | *(In development)* Cryo written in itself bootstrapped from C++, located in [`./cryoc`](./cryoc/) |
| **Integrated tooling** | LSP server, VS Code extension, code formatter, and a tiered test suite |

## Getting Started

### Prerequisites

| Dependency | Version |
|---|---|
| LLVM | 20 |
| Clang | 20 |
| GNU Make | 4.0+ |
| C++ compiler | C++23 support required |
| Python | 3.x (for test runner) |

### Install

```bash
git clone https://github.com/jakelequire/CryoLang.git
cd CryoLang
make all
```

The compiler binary is placed at `./bin/cryo`.

### Hello World

Create `hello.cryo`:

```cryo
namespace Hello;

function main() -> int {
    println("Hello, world!");
    return 0;
}
```

Compile and run:

```bash
./bin/cryo hello.cryo -o hello
./hello
```

### Project-Based Builds

For multi-file projects, create a `cryoconfig` file by running `cryo init`:

```bash
cryo init <project_name>
```

```toml
[project]
project_name = "myapp"
output_dir = "build"
target_type = "executable"
entry_point = "main.cryo"

[compiler]
debug = false
optimize = true
```

Then build and run:

```bash
cd myapp
cryo build
cryo run
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

// ternary operators
const abs: int = x >= 0 ? x : -x;
```

### Structs

Structs define value types with fields and methods. Methods use `&this` for immutable access and `mut &this` for mutation.

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

Classes are heap-allocated reference types that support single inheritance, constructor chaining, virtual methods, and polymorphic dispatch.

#### Defining a Class

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

function main() -> i32 {
    const a: Animal* = new Animal("Cat");
    a.describe();   // Animal: Cat
    return 0;
}
```

#### Inheritance & Constructor Chaining

Derived classes use `: BaseClass(args)` syntax to chain constructors. Fields and methods from the base class are inherited automatically.

```cryo
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

const buddy: Dog* = new Dog("Buddy");
buddy.describe();   // Animal: Dog       (inherited from Animal)
buddy.bark();       // Buddy says: Woof!
```

#### Virtual Methods & Polymorphism

Mark base class methods as `virtual` and override them in derived classes with `override`. Calls through a base class pointer dispatch to the correct implementation at runtime.

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

// Polymorphic dispatch through a base class pointer
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

#### Deep Inheritance

Inheritance chains can extend to any depth. Each level chains to its parent's constructor.

```cryo
type class Animal {
public:
    species: string;
    Animal(_species: string) { this.species = _species; }
}

type class Dog : Animal {
public:
    breed: string;
    Dog(_breed: string) : Animal("Dog") { this.breed = _breed; }
}

type class GoldenRetriever : Dog {
public:
    name: string;
    GoldenRetriever(_name: string) : Dog("GoldenRetriever") { this.name = _name; }

    introduce(&this) -> void {
        println("%s: %s (%s, %s)", this.breed, this.name, this.breed, this.species);
    }
}
```

#### Structs vs. Classes

| | Struct | Class |
|---|---|---|
| **Allocation** | Stack (value type) | Heap via `new` (reference type) |
| **Inheritance** | No | Single inheritance |
| **Virtual dispatch** | No | `virtual` / `override` |
| **Receivers** | `&this` / `mut &this` ( ** ) | `&this` / `mut &this` ( ** ) |
| **Use when** | Plain data, small types, generics | Polymorphism, object hierarchies |

** Non-static methods on any object *(struct or class)* by default receive an immutable reference `&this`. Use `mut &this` if mutation is needed.

### Enums & Pattern Matching

Enums support unit variants and variants with payloads. Pattern matching is exhaustive.

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

Cryo uses monomorphization — generic code is specialized at compile time for each concrete type used, producing zero-overhead abstractions.

#### Generic Structs

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

// Each instantiation generates specialized code
const ints: Pair<int> = Pair<int>::new(1, 2);
const strs: Pair<string> = Pair<string>::new("hello", "world");
```

#### Generic Enums

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

#### Generic Functions

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

#### Generic Implement Blocks

```cryo
implement enum Option<T> {
    is_some(&this) -> boolean {
        return match (this) {
            Option::Some(_) => { true }
            Option::None =>    { false }
        };
    }

    unwrap_or(&this, default_value: T) -> T {
        return match (this) {
            Option::Some(value) => { value }
            Option::None =>        { default_value }
        };
    }
}
```

#### Type Aliases

```cryo
type StringResult<T> = Result<T, string>;
```

### Modules

Every file declares a namespace. Modules are organized using `_module.cryo` files that re-export submodules — similar to Rust's `mod.rs`.

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

Items are private by default. Use `public` to export them.

### Pointers & Memory

Cryo provides explicit pointer operations for systems-level control.

```cryo
function example() -> void {
    mut x: int = 42;
    const ptr: int* = &x;          // address-of
    println("%d", *ptr);          // dereference

    // Heap allocation
    const buf: int* = malloc(sizeof(int) * 10);
    buf[0] = 100;
    free(buf);
}
```

## FFI (Foreign Function Interface)
Cryo can call C functions and be called from C. Use `extern "C"` blocks to declare foreign functions. Or, use `name := extern "C" { ... }` to import C header files directly, which generates bindings automatically under the given namespace.

```cryo
namespace FFI;

// Declare an external C function, using Cryo syntax.
extern "C" {
    function puts(s: string) -> int;
}

// Import C functions from a header file.
// The `c` namespace (any identifier) is used to access imported C functions.
c := extern "C" {
    #include <stdio.h>
    #include "./my_header.h" // void foo(int);
}

function main() -> int {
    const message: string = "Hello from C!";
    puts(message);  // Calls the C function `puts`

    c::foo(42);    // Calls the C function `foo` imported from my_header.h
    c::printf("Value: %d\n", 123); // Calls C's printf via the c namespace

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
| `f32` `f64` | Floating-point numbers |
| `boolean` | `true` or `false` |
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

The standard library is written entirely in Cryo and compiled as a static library. A **prelude** automatically imports the most common types and functions into every file.

### Prelude (auto-imported)

The prelude provides `Option<T>`, `Result<T, E>`, `Array<T>`, `String`, `print`, `println`, `assert`, `assert_eq`, `panic`, `min`, `max`, `clamp`, `swap`, `identity`, and more.


### Core Modules
- `/stdlib/alloc` — Arena, heap, stack, pool allocators
- `/stdlib/collections` — Array, String, HashMap, Deque, BTreeMap
- `/stdlib/core` — Option, Result, primitives, intrinsics
- `/stdlib/env` — Environment variables, args
- `/stdlib/ffi` — Foreign function interface (C interop)
- `/stdlib/fmt` — Formatting
- `/stdlib/fs` — File system, paths
- `/stdlib/io` — stdio, file, reader, writer
- `/stdlib/math` — math functions and constants
- `/stdlib/os` — OS abstractions, threads, synchronization
- `/stdlib/process` — Process spawning, exit codes
- `/stdlib/time` — Time, duration, sleep



## Tooling

### Compiler CLI

```
Usage: cryo <command> [options]

Commands:
  compile <file>     Compile a source file (default)
  build              Build project from cryoconfig
  run                Build and execute
  check              Type-check without compiling
  init               Initialize a new project
  ast                Display the AST
  tokens             Display lexer output
  symbols            Display symbol table
  info               Show compiler information
  version            Show version

Options:
  -o, --output       Output file name
  -c, --compile-only Compile only (no linking)
  -d, --debug        Enable debug output
  -v, --verbose      Verbose output
  --emit-llvm        Emit LLVM bitcode
  --emit-wasm        Compile to WebAssembly (experimental)
  --ir               Display LLVM IR
  --ast              Display AST
  --no-std           Compile without standard library
  --log-component    Filter logs (CODEGEN, PARSER, LEXER, etc.)
```

### Language Server (LSP)

A full LSP implementation provides real-time feedback in your editor:

- Syntax highlighting and diagnostics
- Code completion
- Go to definition
- Hover documentation
- Symbol search

Build the LSP server:

```bash
make lsp
```

### VS Code Extension

The official extension **cryo-language-support** integrates the LSP server with VS Code for a complete development experience.

### Test Suite

The E2E test suite is organized into tiers:

```bash
make test            # Run all tests
make test-tier1      # Core language (variables, control flow, functions)
make test-tier2      # Type system (structs, enums, pointers, match)
make test-tier3      # Generics (structs, enums, cross-module)
make test-tier4      # Modules (imports, submodules, visibility)
make test-tier5      # Classes (inheritance, virtual dispatch, polymorphism)
make test-tier6      # FFI (C interop, unsafe code)
make test-tier7      # Standard library (collections, allocators, I/O)
make test-negative   # Expected compilation failures
```

## Building from Source

### Quick Build

```bash
make all        # Build compiler, stdlib, and run tests
```

### Individual Targets

```bash
make build      # Compiler only
make stdlib     # Standard library
make lsp        # Language Server
make rebuild    # Clean rebuild
make clean      # Remove all build artifacts
```

### Platform Notes

| Platform | Toolchain | Notes |
|---|---|---|
| **Windows** | MSYS2 MinGW64 + Clang 20 | Primary development platform |
| **Linux** | Clang 20 | x86-64 and ARM64 |
| **macOS** | Clang (Xcode) | x86-64 and ARM64 |
| **WebAssembly** | Emscripten | Experimental; uses `--emit-wasm` flag |

### Project Structure

```
CryoLang/
├── src/                    # Compiler implementation (C++23)
│   ├── CLI/                #   Command-line interface
│   ├── Compiler/           #   Multi-pass compilation pipeline
│   ├── Codegen/            #   LLVM IR generation
│   ├── Lexer/              #   Tokenization
│   ├── Parser/             #   Syntax analysis
│   ├── AST/                #   Abstract syntax tree
│   ├── Types/              #   Type system (TypeID, TypeRef, TypeArena)
│   ├── Linker/             #   Object linking
│   └── Utils/              #   Platform abstractions, logging
├── include/                # C++ headers
├── stdlib/                 # Standard library (written in Cryo)
│   ├── prelude.cryo        #   Auto-imported essentials
│   ├── core/               #   Option, Result, primitives, intrinsics
│   ├── collections/        #   Array, String, HashMap, Deque, BTreeMap
│   ├── alloc/              #   Arena, heap, stack, pool allocators
│   ├── io/                 #   stdio, file, reader, writer
│   ├── fs/                 #   File system, paths
│   └── ...
├── cryoc/                  # Self-hosted compiler (in Cryo)
├── tests/                  # E2E test suite (tiered)
├── tools/
│   ├── CryoLSP/           #   Language Server Protocol server
│   ├── CryoFormat/        #   Code formatter
│   └── CryoAnalyzer/      #   VS Code extension
└── makefile                # Build system
```

## Architecture

The compiler uses a 9-stage multi-pass pipeline:

```
Source → Frontend → Module Resolution → Declaration Collection → Type Resolution
     → Semantic Analysis → Specialization → Codegen Preparation → IR Generation
     → Optimization → Native Binary
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
| **Optimization** | LLVM optimization passes and linking |


## License

Licensed under the [Apache License 2.0](LICENSE).

Copyright 2025 Jacob LeQuire.
