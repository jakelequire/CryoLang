<div align="center">
  <img src="./assets/cryo-logo.svg" alt="Cryo" width="180"/>

  <h1>The Cryo Programming Language</h1>

  <p>A statically-typed, compiled systems language with monomorphic generics, algebraic data types, and an LLVM backend.</p>

  [![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
  [![LLVM](https://img.shields.io/badge/LLVM-20-orange.svg)](https://llvm.org/)
  [![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20WASM-lightgrey.svg)]()
</div>

---

Cryo is a systems programming language designed for performance, clarity, and zero-cost abstractions. It combines a familiar C-style syntax with modern language features — pattern matching, generic types, a module system, and a comprehensive standard library — all compiled through LLVM 20 to produce efficient native code.

```cryo
namespace HelloWorld;

function main() -> int {
    printf("Hello, world!\n");
    return 0;
}
```

## Table of Contents

- [Features](#features)
- [Getting Started](#getting-started)
- [Language Overview](#language-overview)
  - [Variables](#variables)
  - [Functions](#functions)
  - [Structs](#structs)
  - [Enums & Pattern Matching](#enums--pattern-matching)
  - [Generics](#generics)
  - [Modules](#modules)
  - [Pointers & Memory](#pointers--memory)
- [Type System](#type-system)
- [Standard Library](#standard-library)
- [Tooling](#tooling)
- [Building from Source](#building-from-source)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Strong static typing** — explicit type annotations with no implicit conversions
- **Monomorphic generics** — compile-time specialization with zero runtime overhead
- **Algebraic data types** — enums with payloads and exhaustive pattern matching
- **Module system** — hierarchical namespaces with visibility control and a prelude
- **LLVM 20 backend** — optimizing compilation to native x86-64, ARM64, and WebAssembly
- **Self-hosting in progress** — the compiler is being rewritten in Cryo itself
- **Rich standard library** — `Option<T>`, `Result<T, E>`, `Array<T>`, `String`, allocators, I/O, and more — written entirely in Cryo
- **Integrated tooling** — LSP server, VS Code extension, code formatter, and a tiered test suite

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
    printf("Hello, world!\n");
    return 0;
}
```

Compile and run:

```bash
./bin/cryo hello.cryo -o hello
./hello
```

### Project-Based Builds

For multi-file projects, create a `cryoconfig` file:

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
    printf("Hello, %s!\n", name);
}
```

### Structs

Structs define data types with fields and methods. Methods use `&this` for immutable access and `mut &this` for mutation.

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
    printf("Area: %d\n", r.area());    // 50
    r.scale(2);
    printf("Area: %d\n", r.area());    // 200
    return 0;
}
```

### Enums & Pattern Matching

Enums support unit variants and variants with payloads. Pattern matching is exhaustive.

```cryo
enum Shape {
    Circle(f64);
    Rectangle(f64, f64);
    Point;
}

function describe(s: Shape) -> void {
    match (s) {
        Shape::Circle(r) => {
            printf("Circle with radius %f\n", r);
        },
        Shape::Rectangle(w, h) => {
            printf("Rectangle %f x %f\n", w, h);
        },
        Shape::Point => {
            printf("A point\n");
        },
    }
}
```

Enums can be extended with methods via `implement` blocks:

```cryo
implement enum Shape {
    is_circle(&this) -> boolean {
        match (&this) {
            Shape::Circle(_) => { return true; }
            _ => { return false; }
        }
    }
}
```

### Generics

Cryo uses monomorphization — generic code is specialized at compile time for each concrete type used, producing zero-overhead abstractions.

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

Generic enums power the standard library's core types:

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

Generic functions:

```cryo
function min<T>(a: T, b: T) -> T {
    if (a < b) {
        return a;
    }
    return b;
}
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

public type struct Vec2 {
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

import Math;

function main() -> int {
    const v: Math::Vector::Vec2 = Math::Vector::Vec2::new(1.0, 2.0);
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
    printf("%d\n", *ptr);          // dereference

    // Heap allocation
    const buf: int* = malloc(sizeof(int) * 10);
    buf[0] = 100;
    free(buf);
}
```

### Control Flow

```cryo
// if / else
if (x > 0) {
    printf("positive\n");
} else if (x < 0) {
    printf("negative\n");
} else {
    printf("zero\n");
}

// for loop
for (mut i: int = 0; i < 10; i++) {
    printf("%d\n", i);
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
    1 => { printf("one\n"); },
    2 => { printf("two\n"); },
    _ => { printf("other\n"); },
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

### Type Casting

```cryo
const a: i64 = 42;
const b: i32 = a as i32;
```

### Type Aliases

```cryo
type StringResult<T> = Result<T, string>;
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

### Module Overview

| Module | Contents |
|---|---|
| `core::option` | `Option<T>` — `Some(T)` / `None` with `unwrap`, `map`, `and_then`, `unwrap_or` |
| `core::result` | `Result<T, E>` — `Ok(T)` / `Err(E)` with `unwrap`, `map`, `and_then`, `is_ok` |
| `core::primitives` | Methods on built-in types: `to_i64()`, `abs()`, `min_value()`, `max_value()` |
| `core::intrinsics` | `malloc`, `free`, `memcpy`, `memset`, `printf`, `sizeof` |
| `core::mem` | `sizeof<T>()`, `default<T>()`, `take`, `replace` |
| `collections::array` | `Array<T>` — growable array with `push`, `pop`, `len`, `capacity`, `filled` |
| `collections::string` | `String` — heap-allocated UTF-8 string with `from_cstr`, `push_char`, `len` |
| `collections::hashmap` | `HashMap<K, V>` — hash table |
| `collections::deque` | `Deque<T>` — double-ended queue |
| `collections::btree` | `BTreeMap<K, V>` — ordered map |
| `alloc` | Arena, heap, stack, and pool allocators |
| `io::stdio` | `print`, `println`, `printf` |
| `io::file` | File reading and writing |
| `thread` | Threads, mutexes, condition variables |
| `sync` | Atomics, channels, semaphores |
| `fs` | File system operations, paths |
| `time` | Instant, Duration, Timer |
| `math` | Mathematical functions |

### Example: Option and Result

```cryo
function find(arr: Array<int>, target: int) -> Option<u64> {
    mut i: u64 = 0;
    while (i < arr.len()) {
        if (arr[i] == target) {
            return Option::Some(i);
        }
        i = i + 1;
    }
    return Option::None;
}

function divide(a: f64, b: f64) -> Result<f64, string> {
    if (b == 0.0) {
        return Result::Err("division by zero");
    }
    return Result::Ok(a / b);
}
```

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
  --emit-wasm        Compile to WebAssembly
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
│   ├── net/                #   TCP, HTTP, sockets
│   ├── fs/                 #   File system, paths
│   ├── thread/             #   Threading primitives
│   ├── sync/               #   Atomics, channels, semaphores
│   └── ...
├── cryoc/                  # Self-hosted compiler (in Cryo)
├── tests/                  # E2E test suite (tiered)
├── tools/
│   ├── CryoLSP/           #   Language Server Protocol server
│   ├── CryoFormat/        #   Code formatter
│   └── CryoAnalyzer/      #   VS Code extension
├── makefile                # Build system
└── cryoconfig              # Project configuration
```

### Compiler Architecture

The compiler uses a 9-stage multi-pass pipeline:

1. **Frontend** — Lexing and parsing into AST
2. **Module Resolution** — Resolve imports and module dependencies
3. **Declaration Collection** — Gather all type and function declarations
4. **Type Resolution** — Resolve all type references and annotations
5. **Semantic Analysis** — Validate correctness, scope checking
6. **Specialization** — Monomorphize generic instantiations
7. **Codegen Preparation** — Multi-pass type and declaration ordering
8. **IR Generation** — Emit LLVM IR via the LLVM 20 C++ API
9. **Optimization** — LLVM optimization passes and linking

## Contributing

Cryo is under active development. Contributions are welcome across all areas:

- **Language design** — syntax, semantics, new features
- **Compiler** — passes, optimizations, diagnostics
- **Standard library** — new modules, more methods on existing types
- **Tooling** — LSP improvements, formatter, debugger integration
- **Documentation** — guides, examples, specification

## License

Licensed under the [Apache License 2.0](LICENSE).

Copyright 2025 Jacob LeQuire.
