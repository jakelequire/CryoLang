# Cryo

Cryo is a statically typed, OOP, manual memory managed, systems programming language. Designed to allow for low level control while offering high level constructs for performative and type safe code.

# Table of Contents
- [Data Types](#data-types)
- [Basic Variables](#basic-variables)
- [Basic Functions](#basic-functions)

## Data Types

In the Cryo stdlib, there is a file called `types.cryo` which shows all of the primitive types defined as well as more complex fundamental types such as `Array<T>`. 

### Primitives

|             |              |              |         |
|-------------|--------------|--------------|---------|
| i8          | u8           | f32 (float)  |         |
| i16         | u16          | f64 (double) |         |
| i32 (int)   | u32 (uint)   | boolean      |         |
| i64         | u64          | string       |         | 
| i128        |              |              |         |
|             |              |              |         |


## Basic Variables

In Cryo, there are two types of variables; `const` variables and `mut` variables. When declaring a variable, either `const` or `mut` is required when defining a variable. Here is a basic example:

```cryo
const a: int = 32;
const b: string = "Hello, world!";
mut c: boolean = true;
mut d: float = 3.14;
mut e: char = 'A';
```

Variable Declarations follow this grammar rule:
```
<var-declaration> ::= ("const" | "mut") <identifier> ":" <type> ["=" <expression>] ";"
```

---

## Basic Functions

Functions in Cryo are defined using the `function` keyword followed by the function name, parameters, return type, and body. The 

```cryo
function add(a: int, b: int) -> int {
    return a + b;
}
```

Function Declarations follow this grammar rule:
```
<function-declaration>  ::=  "function" <identifier> "(" [<param-list>] ")" ["->" <type>] <block>
```




