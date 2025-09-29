# CryoLang

<div align="center">
  <img src="./assets/cryo-logo.svg" alt="CryoLang Logo" width="200"/>
  
  **A Modern Systems Programming Language**
  
  Fast • Safe • Expressive
</div>

---

## Overview

CryoLang is a statically-typed, compiled systems programming language designed for performance, safety, and developer productivity. With a modern syntax, comprehensive type system, and zero-cost abstractions, CryoLang enables developers to write efficient, maintainable code for system-level applications.

## Key Features

**Memory Safety**: References, pointers, and automatic memory management with compile-time safety guarantees.

**Generic Programming**: Template-like generics for code reuse without runtime overhead.

**Pattern Matching**: Advanced pattern matching with destructuring for expressive control flow.

**Trait System**: Interface-based programming with polymorphic behavior and composition.

**LLVM Backend**: Optimizing compiler backend producing efficient native code.

**Rich Type System**: Strong static typing with type inference, algebraic data types, and comprehensive error handling.

## Language Syntax

### Variable Declarations

CryoLang supports both mutable and immutable variable declarations with explicit type annotations:

```cryo
const name: string = "CryoLang";        // Immutable variable
mut counter: int = 0;                   // Mutable variable
const pi: float = 3.14159;              // Floating point
const is_ready: boolean = true;         // Boolean type
const letter: char = 'A';               // Character type
```

### Functions

Functions are first-class citizens with support for multiple parameters, return types, and generic programming:

```cryo
// Simple function
function add(a: int, b: int) -> int {
    return a + b;
}

// Function with multiple parameter types
function process_data(id: int, name: string, active: boolean) -> void {
    std::IO::println("Processing user data");
    std::IO::print_int(id);
    std::IO::println(name);
    std::IO::print_bool(active);
}

// Generic function
function swap<T>(a: T, b: T) -> void {
    // Implementation details
}
```

### Control Flow

CryoLang provides comprehensive control flow constructs including loops, conditionals, and pattern matching:

```cryo
// Conditional statements
const value: int = 42;
if (value > 0) {
    std::IO::println("Positive number");
} else if (value < 0) {
    std::IO::println("Negative number");
} else {
    std::IO::println("Zero");
}

// Loops
for (mut i: int = 0; i < 10; i++) {
    if (i == 5) {
        continue;
    }
    if (i == 8) {
        break;
    }
    std::IO::print_int(i);
}

mut counter: int = 0;
while (counter < 5) {
    std::IO::print_int(counter);
    counter++;
}

// Ternary operator
const result: int = value > 10 ? value : 10;
```

### Data Structures

#### Structs

Define custom data types with methods and constructors:

```cryo
type struct Point {
    x: int;
    y: int;
    
    Point(x: int, y: int);
    distance_from_origin() -> float;
    move(dx: int, dy: int) -> void;
}

implement struct Point {
    Point(_x: int, _y: int) {
        this.x = _x;
        this.y = _y;
    }
    
    distance_from_origin() -> float {
        const dx: float = this.x as float;
        const dy: float = this.y as float;
        return std::Math::sqrt(dx * dx + dy * dy);
    }
    
    move(dx: int, dy: int) -> void {
        this.x = this.x + dx;
        this.y = this.y + dy;
    }
}

// Usage
const point: Point = new Point(10, 20);
point.move(5, -3);
```

#### Classes

Object-oriented programming with encapsulation and inheritance:

```cryo
type class Circle {
public:
    center: Point;
    radius: float;

    Circle(center: Point, radius: float);
    area() -> float;
    circumference() -> float;

private:
    validate_radius() -> boolean;
}

implement class Circle {
    Circle(_center: Point, _radius: float) {
        this.center = _center;
        this.radius = _radius;
    }
    
    area() -> float {
        return 3.14159 * this.radius * this.radius;
    }
    
    circumference() -> float {
        return 2.0 * 3.14159 * this.radius;
    }
}
```

#### Enums

Support for both simple and complex algebraic data types:

```cryo
// Simple enum
enum Status {
    PENDING,
    PROCESSING,
    COMPLETED,
    FAILED
}

// Complex enum with associated data
enum Shape {
    Circle(float),
    Rectangle(float, float),
    Triangle(float, float, float)
}

// Pattern matching with enums
const shape: Shape = Shape::Circle(5.0);
match shape {
    Shape::Circle(radius) => {
        const area: float = 3.14159 * radius * radius;
        std::IO::print_float(area);
    },
    Shape::Rectangle(width, height) => {
        const area: float = width * height;
        std::IO::print_float(area);
    },
    Shape::Triangle(a, b, c) => {
        const s: float = (a + b + c) / 2.0;
        std::IO::print_float(s);
    }
}
```

### Arrays and Collections

```cryo
// Array declarations
const numbers: int[] = [1, 2, 3, 4, 5];
const names: string[] = ["Alice", "Bob", "Charlie"];
const matrix: int[][] = [[1, 2], [3, 4], [5, 6]];

// Array access
const first: int = numbers[0];
const element: int = matrix[1][0];  // Access 2D array

// Iteration
for (mut i: int = 0; i < 5; i++) {
    std::IO::print_int(numbers[i]);
}
```

### Memory Management

CryoLang provides both references and pointers for fine-grained memory control:

```cryo
function memory_example() -> void {
    mut x: int = 42;
    mut y: int = 24;
    
    // References (safe, automatic dereferencing)
    mut ref_x: &int = &x;
    mut ref_y: &int = &y;
    
    // Pointers (explicit dereferencing required)
    mut ptr_x: int* = &x;
    mut ptr_y: int* = &y;
    
    // Modify through pointer
    *ptr_x = *ptr_x + 10;  // x is now 52
    
    // Safe reference usage
    x = *ref_x + 5;        // x is now 57
}
```

### Generic Programming

Write reusable code with type parameters:

```cryo
type struct Container<T> {
    value: T;
    
    Container(val: T) {
        this.value = val;
    }
    
    get() -> T {
        return this.value;
    }
    
    set(new_value: T) -> void {
        this.value = new_value;
    }
}

// Multiple type parameters
type struct Pair<T, U> {
    first: T;
    second: U;
}

// Usage
const int_container: Container<int> = new Container<int>(42);
const pair: Pair<int, string> = Pair<int, string>({first: 1, second: "one"});
```

### Trait System

Define interfaces and implement them for different types:

```cryo
trait Drawable {
    draw(self: &Drawable) -> void;
    area(self: &Drawable) -> float;
}

trait Printable {
    print(self: &Printable) -> void;
}

// Trait inheritance
trait UIComponent : Drawable, Printable {
    handle_click(self: &UIComponent) -> void;
}
```

### Error Handling

CryoLang includes comprehensive error handling with Result and Option types:

```cryo
enum Result<T, E> {
    Ok(T),
    Err(E)
}

enum Option<T> {
    Some(T),
    None
}

// Functions can return Results for error handling
function divide(a: int, b: int) -> Result<float, string> {
    if (b == 0) {
        return Result::Err("Division by zero");
    }
    return Result::Ok(a as float / b as float);
}
```

## Type System

### Primitive Types

| Type | Description | Size |
|------|-------------|------|
| `i8`, `i16`, `i32`, `i64` | Signed integers | 1, 2, 4, 8 bytes |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers | 1, 2, 4, 8 bytes |
| `int` | Default integer (i64) | 8 bytes |
| `uint` | Default unsigned integer (u64) | 8 bytes |
| `f32`, `f64` | Floating point | 4, 8 bytes |
| `float` | Default float (f32) | 4 bytes |
| `double` | Double precision float (f64) | 8 bytes |
| `boolean` | Boolean value | 1 byte |
| `char` | Unicode character | 4 bytes |
| `string` | UTF-8 string | Variable |

### Type Aliases

Create custom type names for better code clarity:

```cryo
type UserId = u64;
type Temperature = f32;
type ptr<T> = T*;
type const_ptr<T> = const T*;
```

## Standard Library

### Core Utilities (`std::core::Types`)

```cryo
import <core/types>;

// Option type for nullable values
const maybe_value: Option<int> = Option::Some(42);

// Result type for error handling
const operation_result: Result<int, string> = Result::Ok(100);

// Error types
const error: Error = Error(404, "Not Found");
```

### Input/Output (`std::IO`)

```cryo
import <io/stdio>;

// Print functions
std::IO::println("Hello, World!");
std::IO::print_int(42);
std::IO::print_float(3.14);
std::IO::print_bool(true);
```

### String Operations (`std::String`)

```cryo
import <strings/strings>;

const text: string = "Hello";
const length: u64 = std::String::_strlen(text);
const number_str: string = std::String::_int_to_string(42);
const float_str: string = std::String::_float_to_string(3.14);
```

### System Calls (`std::Syscall`)

```cryo
import <core/syscall>;

// File operations
const fd: int = std::Syscall::IO::open("file.txt", 0, 0644);
const bytes_written: i64 = std::Syscall::IO::write(fd, "Hello", 5);
std::Syscall::IO::close(fd);
```

### Intrinsic Functions (`std::Intrinsics`)

Low-level system operations and compiler intrinsics:

```cryo
import <core/intrinsics>;

// Memory management
const ptr: void* = std::Intrinsics::__malloc__(1024);
std::Intrinsics::__free__(ptr);

// Math functions
const sqrt_value: f64 = std::Intrinsics::__sqrt__(25.0);
const power: f64 = std::Intrinsics::__pow__(2.0, 8.0);

// String operations
const str_len: u64 = std::Intrinsics::__strlen__("Hello");
```

## Operators

### Arithmetic Operators
```cryo
const a: int = 10 + 5;      // Addition
const b: int = 10 - 5;      // Subtraction  
const c: int = 10 * 5;      // Multiplication
const d: int = 10 / 5;      // Division
const e: int = 10 % 3;      // Modulus
```

### Comparison Operators
```cryo
const equal: boolean = a == b;      // Equality
const not_equal: boolean = a != b;  // Inequality
const less: boolean = a < b;        // Less than
const greater: boolean = a > b;     // Greater than
const less_eq: boolean = a <= b;    // Less than or equal
const greater_eq: boolean = a >= b; // Greater than or equal
```

### Logical Operators
```cryo
const and_result: boolean = true && false;  // Logical AND
const or_result: boolean = true || false;   // Logical OR
const not_result: boolean = !true;          // Logical NOT
```

### Bitwise Operators
```cryo
const and_bits: int = 5 & 3;    // Bitwise AND
const or_bits: int = 5 | 3;     // Bitwise OR
const xor_bits: int = 5 ^ 3;    // Bitwise XOR
const shift_left: int = 5 << 2; // Left shift
const shift_right: int = 5 >> 1; // Right shift
```

### Assignment Operators
```cryo
mut value: int = 10;
value += 5;     // Add and assign
value -= 3;     // Subtract and assign
value *= 2;     // Multiply and assign
value /= 4;     // Divide and assign
value++;        // Increment
value--;        // Decrement
```

## Keywords

### Declarations
`const` `mut` `function` `type` `struct` `class` `enum` `trait` `implement` `namespace` `import` `export`

### Control Flow
`if` `else` `elif` `while` `for` `do` `break` `continue` `return` `match` `switch` `case` `default`

### Types
`int` `i8` `i16` `i32` `i64` `uint` `u8` `u16` `u32` `u64` `float` `f32` `f64` `double` `boolean` `char` `string` `void`

### Modifiers
`public` `private` `protected` `static` `extern` `inline` `virtual` `override` `abstract` `final`

### Special
`this` `true` `false` `null` `sizeof` `new` `ref` `move` `copy` `intrinsic`

## Development Tools

### Compiler Usage

```bash
# Compile source file to executable
cryo source.cryo -o output

# Compile only (no linking)
cryo source.cryo -c

# Show AST
cryo source.cryo --ast

# Show LLVM IR
cryo source.cryo --ir

# Display help
cryo --help
```

### Language Server

CryoLang includes a full Language Server Protocol (LSP) implementation providing:
- Syntax highlighting
- Error diagnostics  
- Code completion
- Go to definition
- Symbol search
- Real-time compilation feedback

### VS Code Extension

The official VS Code extension (`cryo-language-support`) provides comprehensive IDE support with syntax highlighting, error reporting, and integrated development features.

## Getting Started

### Prerequisites

- LLVM 14+ development libraries
- C++23 compatible compiler (GCC 11+, Clang 14+)
- CMake 3.20+

### Building from Source

```bash
git clone https://github.com/jakelequire/CryoLang.git
cd CryoLang
make clean
make all
```

### Hello World

Create a file named `hello.cryo`:

```cryo
namespace Main;

import <io/stdio>;

function main() -> int {
    std::IO::println("Hello, CryoLang!");
    return 0;
}
```

Compile and run:

```bash
./bin/cryo hello.cryo -o hello
./hello
```

## Contributing

CryoLang is actively developed and welcomes contributions. Whether you're interested in language design, compiler implementation, standard library development, or tooling improvements, there are opportunities to get involved.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

---
