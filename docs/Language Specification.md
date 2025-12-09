# The Cryo Programming Language: A Formal Specification

**Version 1.0**

*A Comprehensive Academic Specification for a Modern Systems Programming Language*

---

## Abstract

This document presents a formal specification for Cryo, a statically-typed, compiled systems programming language designed for performance, safety, and developer productivity. Cryo combines the expressiveness of modern high-level languages with the control and efficiency required for systems programming. The language features a comprehensive type system with generic programming support, memory safety guarantees through references and explicit pointer management, trait-based polymorphism, pattern matching with algebraic data types, and an LLVM-based compilation backend. This specification provides a rigorous mathematical foundation for the language's syntax, semantics, type system, and operational behavior, establishing Cryo as a principled approach to systems programming that bridges the gap between safety and performance.

**Keywords:** Programming Languages, Type Systems, Systems Programming, Formal Semantics, Memory Safety, Generic Programming, LLVM

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Lexical Structure](#2-lexical-structure)
3. [Syntactic Structure](#3-syntactic-structure)
4. [Type System](#4-type-system)
5. [Semantic Domains](#5-semantic-domains)
6. [Operational Semantics](#6-operational-semantics)
7. [Memory Model](#7-memory-model)
8. [Module System](#8-module-system)
9. [Generic Programming Model](#9-generic-programming-model)
10. [Trait System](#10-trait-system)
11. [Error Handling](#11-error-handling)
12. [Runtime System](#12-runtime-system)
13. [Standard Library](#13-standard-library)
14. [Compilation Model](#14-compilation-model)
15. [Formal Properties](#15-formal-properties)
16. [Conclusion](#16-conclusion)

---

## 1. Introduction

### 1.1 Motivation and Design Philosophy

Cryo emerges from the recognition that contemporary systems programming requires a language that reconciles the fundamental tension between safety and performance. Traditional systems programming languages prioritize performance and control at the expense of safety, while modern high-level languages achieve safety through mechanisms that often compromise performance. Cryo addresses this dichotomy through a principled design that provides compile-time safety guarantees without sacrificing runtime performance.

The design philosophy of Cryo is anchored in several key principles:

**Static Safety**: All memory safety violations, type errors, and undefined behavior are detected at compile time through a sophisticated static analysis system that includes lifetime tracking, ownership analysis, and effect typing.

**Zero-Cost Abstractions**: High-level constructs are designed to compile to efficient machine code without runtime overhead. Generic programming, trait dispatch, and pattern matching all resolve to optimal code during compilation.

**Explicit Control**: While providing safety guarantees, Cryo maintains explicit control over memory layout, allocation strategies, and performance-critical operations through carefully designed escape hatches and low-level primitives.

**Compositional Design**: Language features are designed to compose naturally, allowing complex behaviors to emerge from simple, orthogonal primitives.

### 1.2 Theoretical Foundations

Cryo's type system is grounded in System F with type constructors, extended with linear types for memory management, and refined with effect types for tracking computational effects. The semantic model builds upon operational semantics in the style of Wright and Felleisen, with additional structures for memory management and concurrency.

The language's formal foundation can be characterized as:

- **Type Theory**: System F<sub>ω</sub> with linear types and effect annotations
- **Semantics**: Small-step operational semantics with explicit memory modeling
- **Safety Properties**: Type safety, memory safety, and data race freedom through static analysis

### 1.3 Language Overview

Cryo is a statically-typed, compiled programming language that targets system-level programming while providing modern language constructs. The language supports:

- **Algebraic Data Types** with pattern matching for expressive data modeling
- **Generic Programming** through parametric polymorphism with type constraints
- **Trait-Based Polymorphism** for interface-oriented programming
- **Memory Management** combining automatic memory safety with explicit control
- **Concurrent Programming** with built-in primitives for safe concurrency
- **Interoperability** with C and other systems languages through foreign function interfaces

---

## 2. Lexical Structure

### 2.1 Formal Lexical Grammar

The lexical structure of Cryo is defined through a formal grammar that specifies the transformation from character sequences to token sequences. Let Σ be the alphabet of Unicode characters, and let Σ* be the set of all finite strings over Σ.

**Definition 2.1** (Lexical Categories): The lexical categories of Cryo are defined as follows:

```bnf
Token ::= Keyword | Identifier | Literal | Operator | Separator | Comment

Keyword ::= 'const' | 'mut' | 'function' | 'type' | 'struct' | 'class' | 'enum' 
          | 'trait' | 'implement' | 'namespace' | 'import' | 'export'
          | 'if' | 'else' | 'while' | 'for' | 'do' | 'break' | 'continue' 
          | 'return' | 'match' | 'switch' | 'case' | 'default'
          | 'public' | 'private' | 'protected' | 'static' | 'extern' 
          | 'inline' | 'virtual' | 'override' | 'abstract' | 'final'
          | 'this' | 'true' | 'false' | 'null' | 'sizeof' | 'new' | 'intrinsic'

Identifier ::= IdentifierStart IdentifierContinue*
IdentifierStart ::= Letter | '_'
IdentifierContinue ::= Letter | Digit | '_'

Literal ::= IntegerLiteral | FloatingPointLiteral | CharacterLiteral 
          | StringLiteral | BooleanLiteral

IntegerLiteral ::= DecimalInteger | HexadecimalInteger | BinaryInteger | OctalInteger
DecimalInteger ::= Digit+
HexadecimalInteger ::= '0' ('x' | 'X') HexDigit+
BinaryInteger ::= '0' ('b' | 'B') BinaryDigit+
OctalInteger ::= '0' ('o' | 'O') OctalDigit+

FloatingPointLiteral ::= Digit+ '.' Digit+ (ExponentPart)?
                       | Digit+ ExponentPart
ExponentPart ::= ('e' | 'E') ('+' | '-')? Digit+

CharacterLiteral ::= "'" CharacterContent "'"
StringLiteral ::= '"' StringContent* '"'

BooleanLiteral ::= 'true' | 'false'

Operator ::= '+' | '-' | '*' | '/' | '%' | '==' | '!=' | '<' | '>' 
           | '<=' | '>=' | '&&' | '||' | '!' | '&' | '|' | '^' 
           | '<<' | '>>' | '+=' | '-=' | '*=' | '/=' | '++' | '--'
           | '=' | '->' | '::' | '.' | '?'

Separator ::= '(' | ')' | '{' | '}' | '[' | ']' | ';' | ',' | ':'

Comment ::= LineComment | BlockComment | DocComment
LineComment ::= '//' (!'\n' Character)* '\n'?
BlockComment ::= '/*' (!'*/' Character)* '*/'
DocComment ::= '/**' (!'**/' Character)* '**/' | '///' (!'\n' Character)* '\n'?
```

### 2.2 Lexical Analysis Rules

**Definition 2.2** (Maximal Munch Principle): The lexical analyzer applies the maximal munch principle, where the longest possible sequence of characters that forms a valid token is selected.

**Definition 2.3** (Reserved Keywords): All keywords are reserved and cannot be used as identifiers in any context. The set of reserved keywords is fixed and cannot be extended through user definitions.

**Definition 2.4** (Unicode Support): Cryo supports the full Unicode character set for string literals and comments. Identifiers are restricted to ASCII characters for portability and tooling compatibility.

### 2.3 Numeric Literal Semantics

**Definition 2.5** (Integer Literal Typing): Integer literals are typed according to the following rules:

1. Decimal literals without suffix are typed as the smallest type in {i32, i64} that can represent the value
2. Literals with explicit type suffixes (e.g., 42u64, 100i16) have the specified type
3. Literals that exceed the range of i64 cause a compile-time error unless explicitly typed

**Definition 2.6** (Floating-Point Literal Typing): Floating-point literals are typed as f64 by default, or according to their explicit suffix (f32, f64).

### 2.4 String and Character Literal Semantics

**Definition 2.7** (String Literal Encoding): String literals are encoded as UTF-8 sequences and have type `string`, which is equivalent to `Array<u8>` with additional type-level guarantees of valid UTF-8 encoding.

**Definition 2.8** (Character Literal Encoding): Character literals represent Unicode scalar values and have type `char`, which is equivalent to `u32` with additional type-level constraints ensuring valid Unicode code points.

**Definition 2.9** (Escape Sequences): The following escape sequences are recognized in string and character literals:

- `\n` (newline, U+000A)
- `\r` (carriage return, U+000D)  
- `\t` (horizontal tab, U+0009)
- `\0` (null character, U+0000)
- `\\` (backslash, U+005C)
- `\"` (quotation mark, U+0022)
- `\'` (apostrophe, U+0027)
- `\xNN` (8-bit character code)
- `\u{NNNNNN}` (Unicode code point)

---

## 3. Syntactic Structure

### 3.1 Context-Free Grammar

The syntactic structure of Cryo is defined by a context-free grammar G = (N, Σ, P, S) where:
- N is the set of non-terminal symbols
- Σ is the set of terminal symbols (tokens from the lexical analysis)
- P is the set of production rules
- S is the start symbol (Program)

**Definition 3.1** (Core Grammar Productions): The core syntactic structure is defined by the following productions:

```bnf
Program ::= Item*

Item ::= VariableDeclaration
       | FunctionDeclaration
       | TypeDeclaration
       | ImplementationBlock
       | NamespaceDeclaration
       | ImportDeclaration
       | ExportDeclaration

VariableDeclaration ::= (const | mut) Identifier : Type [= Expression] ;

FunctionDeclaration ::= function Identifier <<GenericParameters?>> 
                       ( ParameterList? ) [-> Type] Block

TypeDeclaration ::= StructDeclaration
                  | ClassDeclaration
                  | EnumDeclaration
                  | TraitDeclaration
                  | TypeAliasDeclaration

StructDeclaration ::= [type] struct Identifier <GenericParameters?> 
                     { StructMember* }

ClassDeclaration ::= [type] class Identifier <GenericParameters?> [: Type] 
                    { ClassMember* }

EnumDeclaration ::= [type] enum Identifier <GenericParameters?> 
                   { EnumVariant* }

TraitDeclaration ::= trait Identifier <GenericParameters?> [: TraitBound] 
                    { TraitItem* }

TypeAliasDeclaration ::= type Identifier <GenericParameters?> = Type ;

ImplementationBlock ::= implement (struct | class | enum | trait) Type 
                       [for Type] { ImplementationMember* }
```

### 3.2 Expression Grammar

**Definition 3.2** (Expression Grammar): The expression grammar follows a precedence hierarchy that eliminates ambiguity:

```bnf
Expression ::= AssignmentExpression

AssignmentExpression ::= ConditionalExpression
                       | Identifier = AssignmentExpression
                       | Identifier AssignmentOperator AssignmentExpression

ConditionalExpression ::= LogicalOrExpression
                        | LogicalOrExpression ? Expression : ConditionalExpression

LogicalOrExpression ::= LogicalAndExpression
                      | LogicalOrExpression || LogicalAndExpression

LogicalAndExpression ::= EqualityExpression
                       | LogicalAndExpression && EqualityExpression

EqualityExpression ::= RelationalExpression
                     | EqualityExpression (== | !=) RelationalExpression

RelationalExpression ::= AdditiveExpression
                       | RelationalExpression (< | > | <= | >=) AdditiveExpression

AdditiveExpression ::= MultiplicativeExpression
                     | AdditiveExpression (+ | -) MultiplicativeExpression

MultiplicativeExpression ::= UnaryExpression
                           | MultiplicativeExpression (* | / | %) UnaryExpression

UnaryExpression ::= PostfixExpression
                  | UnaryOperator UnaryExpression
                  | sizeof ( Type )

PostfixExpression ::= PrimaryExpression
                    | PostfixExpression [ Expression ]
                    | PostfixExpression ( ArgumentList? )
                    | PostfixExpression . Identifier
                    | PostfixExpression ++
                    | PostfixExpression --

PrimaryExpression ::= Identifier
                    | Literal
                    | ( Expression )
                    | new Type ( ArgumentList? )
                    | StructLiteral
                    | ArrayLiteral
```

### 3.3 Type Grammar

**Definition 3.3** (Type Grammar): The type system supports primitive types, user-defined types, generic types, and type constructors:

```bnf
Type ::= PrimitiveType
       | TypeIdentifier
       | Type [ ]
       | Type *
       | & Type
       | & mut Type
       | ( Type )
       | TypeIdentifier < TypeArgumentList >

PrimitiveType ::= i8 | i16 | i32 | i64 | i128
                | u8 | u16 | u32 | u64 | u128
                | int | uint
                | f32 | f64 | float | double
                | boolean | char | string | void

TypeArgumentList ::= Type (, Type)*

GenericParameters ::= < GenericParameter (, GenericParameter)* >

GenericParameter ::= Identifier [: TypeBound]

TypeBound ::= Type (+ Type)*
```

### 3.4 Pattern Grammar

**Definition 3.4** (Pattern Grammar): Pattern matching supports destructuring of data types:

```bnf
Pattern ::= IdentifierPattern
          | LiteralPattern  
          | StructPattern
          | EnumPattern
          | ArrayPattern
          | WildcardPattern
          | ( Pattern )

IdentifierPattern ::= [mut] Identifier

LiteralPattern ::= Literal

StructPattern ::= TypeIdentifier { FieldPattern (, FieldPattern)* }

EnumPattern ::= TypeIdentifier :: Identifier [( Pattern (, Pattern)* )]

ArrayPattern ::= [ Pattern (, Pattern)* ]

WildcardPattern ::= _

FieldPattern ::= Identifier [: Pattern]
```

### 3.5 Syntactic Precedence and Associativity

**Definition 3.5** (Operator Precedence): The operator precedence is defined in ascending order of binding strength:

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `=`, `+=`, `-=`, `*=`, `/=`, `%=` | Right |
| 2 | `?:` (ternary) | Right |
| 3 | `\|\|` | Left |
| 4 | `&&` | Left |
| 5 | `\|` | Left |
| 6 | `^` | Left |
| 7 | `&` | Left |
| 8 | `==`, `!=` | Left |
| 9 | `<`, `>`, `<=`, `>=` | Left |
| 10 | `<<`, `>>` | Left |
| 11 | `+`, `-` | Left |
| 12 | `*`, `/`, `%` | Left |
| 13 | `!`, `-`, `++`, `--` (unary) | Right |
| 14 | `[]`, `()`, `.` | Left |

### 3.6 Ambiguity Resolution

**Definition 3.6** (Disambiguation Rules): Several syntactic ambiguities are resolved through the following rules:

1. **Generic vs. Comparison**: `Type<T>` is parsed as a generic type instantiation rather than less-than comparison when `Type` is in scope as a type constructor.

2. **Struct vs. Block**: `Type { ... }` is parsed as struct literal when `Type` is a struct type and the contents match field initialization syntax.

3. **Expression vs. Type**: In generic parameter positions, identifiers are interpreted as types when possible, falling back to constant expressions for non-type template parameters.

---

## 4. Type System

### 4.1 Type Theory Foundation

The Cryo type system is formalized as an extension of System F<sub>ω</sub> (System F with type constructors) augmented with linear types for memory management and effect types for computational effects. This provides a sound theoretical foundation for both safety and performance guarantees.

**Definition 4.1** (Kind System): Types are classified by kinds, which describe the arity and structure of type constructors:

```bnf
κ ::= *                    (kind of types)
    | κ₁ → κ₂              (kind of type constructors)
    | Lin                  (kind of linear types)
    | Eff                  (kind of effects)

τ ::= α                    (type variable)
    | T                    (type constant)
    | τ₁ τ₂                (type application)
    | λα:κ.τ               (type abstraction)
    | ∀α:κ.τ               (universal quantification)
    | τ₁ ⊸ τ₂              (linear function type)
    | τ₁ →^ε τ₂            (function type with effect ε)
    | Ref^ρ τ              (reference type with region ρ)
    | Own τ                (owned type)
```

### 4.2 Primitive Types

**Definition 4.2** (Primitive Type Hierarchy): Cryo's primitive types form a hierarchy based on their semantic properties:

**Integer Types**: The integer types are defined with explicit bit-widths and signedness:

- **Signed integers**: `i8`, `i16`, `i32`, `i64`, `i128` represent two's complement integers
- **Unsigned integers**: `u8`, `u16`, `u32`, `u64`, `u128` represent natural numbers modulo 2ⁿ
- **Default integers**: `int` (alias for `i32`) and `uint` (alias for `u32`)

**Floating-Point Types**: Floating-point types follow IEEE 754 standard:

- `f32`: Single-precision floating point (binary32)
- `f64`: Double-precision floating point (binary64)  
- `float`: Alias for `f32`
- `double`: Alias for `f64`

**Other Primitive Types**:

- `boolean`: Two-valued logic type with values `true` and `false`
- `char`: Unicode scalar value (21-bit subset of `u32`)
- `string`: UTF-8 encoded string (equivalent to `Array<u8>` with UTF-8 invariant)
- `void`: Unit type with a single value (used for functions with no return value)

**Definition 4.3** (Type Equivalence): Two types τ₁ and τ₂ are equivalent (τ₁ ≡ τ₂) if they are structurally equal after normalization of type aliases and α-renaming of bound variables.

### 4.3 Composite Types

**Definition 4.4** (Array Types): Array types `T[]` represent homogeneous sequences with the following properties:

- **Element Type**: All elements have type `T`
- **Bounds Checking**: Array access is bounds-checked at runtime
- **Memory Layout**: Elements are stored contiguously in memory
- **Size Information**: Arrays carry their length as runtime information

**Definition 4.5** (Reference Types): Reference types provide safe access to memory locations:

- **Immutable References**: `&T` allows read-only access to a value of type `T`
- **Mutable References**: `&mut T` allows read-write access to a value of type `T`
- **Exclusivity**: Mutable references are exclusive (no aliasing during mutation)
- **Lifetime Tracking**: References have associated lifetime parameters (implicit in syntax)

**Definition 4.6** (Pointer Types): Pointer types `T*` provide low-level memory access:

- **Raw Pointers**: Direct memory addresses without safety guarantees
- **Null Pointers**: Pointers may be null and must be checked before dereferencing
- **Arithmetic**: Pointer arithmetic is supported with appropriate safety warnings

### 4.4 Algebraic Data Types

**Definition 4.7** (Struct Types): Struct types are product types that combine multiple values:

```cryo
struct Point {
    x: f64;
    y: f64;
}
```

The type `Point` is equivalent to the product type `f64 × f64`. Access to fields is through projection functions that are well-typed only when the field exists.

**Definition 4.8** (Enum Types): Enum types are sum types that represent choices between alternatives:

```cryo
enum Option<T> {
    Some(T),
    None
}
```

The type `Option<T>` is equivalent to the sum type `T + Unit`. Pattern matching provides the elimination form for sum types.

**Definition 4.9** (Class Types): Class types extend struct types with inheritance and dynamic dispatch:

- **Single Inheritance**: Classes may inherit from at most one base class
- **Virtual Methods**: Methods may be virtual, enabling dynamic dispatch
- **Access Control**: Members have visibility modifiers (`public`, `private`, `protected`)

### 4.5 Generic Types

**Definition 4.10** (Type Parameters): Generic types are parameterized by type variables with optional bounds:

```cryo
struct Container<T> where T: Clone {
    value: T;
}
```

The type parameter `T` is universally quantified with the constraint that `T` implements the `Clone` trait.

**Definition 4.11** (Type Parameter Constraints): Type parameters may be constrained by trait bounds:

- **Single Bounds**: `T: Trait` requires `T` to implement `Trait`
- **Multiple Bounds**: `T: Trait₁ + Trait₂` requires `T` to implement both traits
- **Lifetime Bounds**: `T: 'a` requires all references in `T` to outlive lifetime `'a`

**Definition 4.12** (Monomorphization): Generic types are instantiated through monomorphization, where each concrete instantiation generates specialized code. This ensures zero runtime overhead for generic abstractions.

### 4.6 Type Inference

**Definition 4.13** (Type Inference Algorithm): Cryo employs a bidirectional type inference algorithm based on Hindley-Milner with extensions for subtyping and effects:

1. **Constraint Generation**: Generate type constraints from syntax
2. **Constraint Solving**: Solve constraints using unification with occurs check
3. **Defaulting**: Apply default types to ambiguous numeric literals
4. **Coherence Check**: Ensure trait implementations are coherent

**Definition 4.14** (Principal Types): Every well-typed expression has a principal type that is the most general type assignable to that expression. The type inference algorithm computes principal types when they exist.

### 4.7 Subtyping Relations

**Definition 4.15** (Subtyping): Cryo supports limited subtyping for ergonomic purposes:

- **Lifetime Subtyping**: Longer lifetimes are subtypes of shorter lifetimes
- **Reference Weakening**: `&mut T` is a subtype of `&T` (when safe)
- **Trait Object Coercion**: Implementing types coerce to trait objects

The subtyping relation `<:` is reflexive, transitive, and antisymmetric (up to type equivalence).

### 4.8 Effect System

**Definition 4.16** (Effect Types): Functions are annotated with effect types that track computational effects:

- **Pure Effects**: Functions with no observable effects
- **Memory Effects**: Functions that allocate, deallocate, or mutate memory
- **IO Effects**: Functions that perform input/output operations
- **Divergence Effects**: Functions that may not terminate

Effects compose using a semilattice structure, where the least upper bound represents effect combination.

---

## 5. Semantic Domains

### 5.1 Value Domains

**Definition 5.1** (Value Domains): The semantic domains define the mathematical structures that model runtime values:

```bnf
Values v ∈ V ::= n ∈ ℤ                    (integers)
                | r ∈ ℚ                    (rationals)  
                | b ∈ 𝔹                    (booleans)
                | c ∈ Unicode              (characters)
                | s ∈ String               (strings)
                | ⟨v₁, v₂, ..., vₙ⟩        (tuples)
                | inₗ(v) | inᵣ(v)          (tagged unions)
                | [v₁, v₂, ..., vₙ]        (arrays)
                | λx.e                     (function closures)
                | ⟨f₁ = v₁, ..., fₙ = vₙ⟩  (records)
                | ℓ ∈ L                    (memory locations)
                | null                     (null pointer)
                | ⊥                        (undefined value)
```

**Definition 5.2** (Memory Locations): The location domain L represents memory addresses with associated metadata:

```bnf
Locations ℓ ∈ L ::= ⟨addr, size, type, ownership⟩

where:
  addr ∈ ℕ           (physical address)
  size ∈ ℕ           (size in bytes)  
  type ∈ Type        (type of stored value)
  ownership ∈ {own, ref, raw}  (ownership status)
```

### 5.2 Environment Domains

**Definition 5.3** (Environment Structure): Environments map identifiers to values and types:

```bnf
Type Environments   Γ : Identifier → Type
Value Environments  ρ : Identifier → V
Store              σ : L → V
Heap               h : L → {allocated, free}
```

**Definition 5.4** (Environment Operations): Standard environment operations include:

- **Extension**: Γ[x ↦ τ] extends environment Γ with binding x : τ
- **Lookup**: Γ(x) retrieves the type of identifier x
- **Domain**: dom(Γ) returns the set of bound identifiers
- **Compatibility**: Γ₁ ⊕ Γ₂ merges compatible environments

### 5.3 Continuation Domains

**Definition 5.5** (Continuation Structure): Continuations model the rest of the computation:

```bnf
Continuations κ ∈ K ::= halt
                      | let x = □ in e; κ
                      | □ op v; κ  
                      | v op □; κ
                      | if □ then e₁ else e₂; κ
                      | □[e]; κ
                      | v[□]; κ
                      | □.f; κ
                      | call □ (v₁, ..., vₙ); κ
                      | call f (v₁, ..., vᵢ₋₁, □, eᵢ₊₁, ..., eₙ); κ
```

### 5.4 Effect Domains

**Definition 5.6** (Effect Structure): Effects model observable computational actions:

```bnf
Effects ε ∈ E ::= pure                    (no effects)
                | read(ℓ)                 (memory read)
                | write(ℓ, v)             (memory write)  
                | alloc(n)                (allocation)
                | free(ℓ)                 (deallocation)
                | io(op, data)            (I/O operation)
                | diverge                 (non-termination)
                | ε₁ ; ε₂                 (effect sequence)
                | ε₁ ⊕ ε₂                 (effect choice)
```

**Definition 5.7** (Effect Ordering): Effects form a partial order under the relation ⊑:

- pure ⊑ ε for all ε
- ε₁ ⊑ ε₁ ⊕ ε₂ and ε₂ ⊑ ε₁ ⊕ ε₂
- If ε₁ ⊑ ε₃ and ε₂ ⊑ ε₄, then ε₁ ; ε₂ ⊑ ε₃ ; ε₄

---

## 6. Operational Semantics

### 6.1 Formal Semantic Relations

**Definition 6.1** (Configuration Space): A configuration represents the current state of computation:

```bnf
Configuration ⟨e, ρ, σ, h, κ⟩ where:
  e ∈ Expression    (current expression)
  ρ ∈ Environment   (variable bindings)
  σ ∈ Store         (memory contents)  
  h ∈ Heap          (allocation status)
  κ ∈ Continuation  (rest of computation)
```

**Definition 6.2** (Reduction Relations): The small-step operational semantics is defined by reduction relations:

```bnf
⟨e, ρ, σ, h, κ⟩ → ⟨e', ρ', σ', h', κ'⟩    (single step)
⟨e, ρ, σ, h, κ⟩ →* ⟨e', ρ', σ', h', κ'⟩   (multi-step)
⟨e, ρ, σ, h, κ⟩ ↑                          (divergence)
```

### 6.2 Expression Evaluation Rules

**Definition 6.3** (Arithmetic Operations): Arithmetic expressions evaluate according to mathematical semantics with overflow checking:

```bnf
(E-Add)
n₁ + n₂ = n₃ ∧ inBounds(n₃, τ)
─────────────────────────────────────
⟨n₁ + n₂, ρ, σ, h, κ⟩ → ⟨n₃, ρ, σ, h, κ⟩

(E-Add-Overflow)  
n₁ + n₂ = n₃ ∧ ¬inBounds(n₃, τ)
─────────────────────────────────────
⟨n₁ + n₂, ρ, σ, h, κ⟩ → ⟨panic("overflow"), ρ, σ, h, κ⟩
```

**Definition 6.4** (Variable Access): Variable lookup follows scoping rules with error handling:

```bnf
(E-Var)
ρ(x) = v
─────────────────────────
⟨x, ρ, σ, h, κ⟩ → ⟨v, ρ, σ, h, κ⟩

(E-Var-Unbound)
x ∉ dom(ρ)  
─────────────────────────────────────
⟨x, ρ, σ, h, κ⟩ → ⟨error("unbound variable"), ρ, σ, h, κ⟩
```

**Definition 6.5** (Function Application): Function calls follow call-by-value semantics with stack management:

```bnf
(E-App)
ρ(f) = λ(x₁: τ₁, ..., xₙ: τₙ).e
ρ' = ρ[x₁ ↦ v₁, ..., xₙ ↦ vₙ]
─────────────────────────────────────
⟨f(v₁, ..., vₙ), ρ, σ, h, κ⟩ → ⟨e, ρ', σ, h, κ⟩
```

### 6.3 Memory Operations

**Definition 6.6** (Memory Allocation): Heap allocation creates new memory locations:

```bnf
(E-Alloc)
ℓ ∉ dom(σ) ∧ size(τ) = n
h' = h[ℓ ↦ allocated]
σ' = σ[ℓ ↦ uninit]
─────────────────────────────────────
⟨alloc(τ), ρ, σ, h, κ⟩ → ⟨ℓ, ρ, σ', h', κ⟩
```

**Definition 6.7** (Memory Deallocation): Memory deallocation marks locations as free:

```bnf
(E-Free)  
σ(ℓ) = v ∧ h(ℓ) = allocated
h' = h[ℓ ↦ free]
σ' = σ[ℓ ↦ ⊥]
─────────────────────────────────────
⟨free(ℓ), ρ, σ, h, κ⟩ → ⟨(), ρ, σ', h', κ⟩

(E-Double-Free)
h(ℓ) = free
─────────────────────────────────────
⟨free(ℓ), ρ, σ, h, κ⟩ → ⟨panic("double free"), ρ, σ, h, κ⟩
```

**Definition 6.8** (Memory Access): Memory reads and writes check allocation status:

```bnf
(E-Deref)
σ(ℓ) = v ∧ h(ℓ) = allocated  
─────────────────────────────────────
⟨*ℓ, ρ, σ, h, κ⟩ → ⟨v, ρ, σ, h, κ⟩

(E-Use-After-Free)
h(ℓ) = free
─────────────────────────────────────
⟨*ℓ, ρ, σ, h, κ⟩ → ⟨panic("use after free"), ρ, σ, h, κ⟩
```

### 6.4 Control Flow Semantics

**Definition 6.9** (Conditional Evaluation): Conditionals evaluate based on boolean values:

```bnf
(E-If-True)
─────────────────────────────────────
⟨if true then e₁ else e₂, ρ, σ, h, κ⟩ → ⟨e₁, ρ, σ, h, κ⟩

(E-If-False)  
─────────────────────────────────────
⟨if false then e₁ else e₂, ρ, σ, h, κ⟩ → ⟨e₂, ρ, σ, h, κ⟩
```

**Definition 6.10** (Loop Semantics): While loops are desugared to recursive function calls:

```bnf
(E-While)
─────────────────────────────────────
⟨while e₁ do e₂, ρ, σ, h, κ⟩ → ⟨if e₁ then (e₂; while e₁ do e₂) else (), ρ, σ, h, κ⟩
```

**Definition 6.11** (Pattern Matching): Pattern matching follows exhaustiveness and coverage rules:

```bnf
(E-Match-Success)
match(v, p) = ρ'
─────────────────────────────────────
⟨match v { p → e | ... }, ρ, σ, h, κ⟩ → ⟨e, ρ ⊕ ρ', σ, h, κ⟩

(E-Match-Fail)
∀i. ¬match(v, pᵢ)
─────────────────────────────────────
⟨match v { p₁ → e₁ | ... | pₙ → eₙ }, ρ, σ, h, κ⟩ → ⟨panic("match failed"), ρ, σ, h, κ⟩
```

### 6.5 Exception and Error Handling

**Definition 6.12** (Panic Propagation): Panics propagate through the call stack until handled:

```bnf
(E-Panic-Prop)
─────────────────────────────────────
⟨panic(msg), ρ, σ, h, let x = □ in e; κ⟩ → ⟨panic(msg), ρ, σ, h, κ⟩

(E-Panic-Halt)
─────────────────────────────────────
⟨panic(msg), ρ, σ, h, halt⟩ → abort(msg)
```

---

## 7. Memory Model

### 7.1 Abstract Memory Model

**Definition 7.1** (Memory Regions): Memory is partitioned into distinct regions with different allocation and lifetime semantics:

```bnf
Memory Regions R ::= Stack(thread_id)      (stack memory per thread)
                   | Heap(allocator_id)    (heap memory per allocator)  
                   | Static                 (program static data)
                   | Code                   (executable code)

Memory Layout M : R → P(Address × Size × Type)
```

**Definition 7.2** (Memory Safety Invariants): The memory model maintains the following invariants:

1. **Spatial Safety**: All memory accesses are within allocated bounds
2. **Temporal Safety**: No access to deallocated memory
3. **Type Safety**: Memory contents match their declared types
4. **Initialization Safety**: No access to uninitialized memory

**Definition 7.3** (Ownership Model): Memory ownership follows an affine type system:

```bnf
Ownership ::= Owned(T)          (exclusive ownership)
            | Borrowed(&T, ℓ)   (shared immutable reference)  
            | BorrowedMut(&mut T, ℓ)  (exclusive mutable reference)
            | Raw(*T)           (unsafe raw pointer)
```

### 7.2 Lifetime Analysis

**Definition 7.4** (Lifetime Parameters): Lifetimes represent the scope during which references are valid:

```bnf
Lifetimes ℓ ::= 'a                      (named lifetime parameter)
              | 'static                 (program lifetime)
              | '_                      (inferred lifetime)
              | ℓ₁ ∩ ℓ₂                 (lifetime intersection)
              | ℓ₁ ∪ ℓ₂                 (lifetime union)
```

**Definition 7.5** (Lifetime Ordering): Lifetimes form a partial order ⊑ where ℓ₁ ⊑ ℓ₂ means ℓ₁ outlives ℓ₂:

- 'static ⊑ ℓ for all ℓ
- If ℓ₁ ⊑ ℓ₃ and ℓ₂ ⊑ ℓ₃, then ℓ₁ ∩ ℓ₂ ⊑ ℓ₃
- ℓ₁ ⊑ ℓ₁ ∪ ℓ₂ and ℓ₂ ⊑ ℓ₁ ∪ ℓ₂

**Definition 7.6** (Borrow Checking): The borrow checker enforces aliasing restrictions:

1. **Unique Mutable References**: At most one mutable reference to any location
2. **No Mixed References**: No simultaneous mutable and immutable references
3. **Lifetime Constraints**: References must not outlive their referents

### 7.3 Stack Allocation

**Definition 7.7** (Stack Frame Structure): Stack frames contain local variables and control information:

```bnf
StackFrame ::= ⟨locals: Identifier ⇀ Value,
                return_addr: Address,  
                saved_regs: Register ⇀ Value,
                frame_ptr: Address⟩
```

**Definition 7.8** (Stack Allocation Rules): Stack allocation follows LIFO semantics with automatic cleanup:

- Variables are allocated upon scope entry
- Variables are deallocated upon scope exit (in reverse order)
- Nested scopes create new stack levels
- Exception unwinding deallocates intermediate frames

### 7.4 Heap Allocation

**Definition 7.9** (Heap Allocation Strategy): The heap allocator uses a segregated free list strategy:

- **Size Classes**: Objects are grouped by size for efficient allocation
- **Free Lists**: Each size class maintains a list of free blocks
- **Coalescing**: Adjacent free blocks are merged to reduce fragmentation
- **Garbage Collection**: Unreachable objects are automatically reclaimed

**Definition 7.10** (Allocation Metadata): Each allocation carries metadata for safety checking:

```bnf
AllocationHeader ::= ⟨size: Size,
                      type: Type,
                      checksum: u32,
                      gc_mark: boolean⟩
```

### 7.5 Reference Semantics

**Definition 7.11** (Reference Operations): References support safe aliasing with compile-time checking:

```bnf
Reference Operations:
- &x          (create immutable reference)
- &mut x      (create mutable reference)  
- *r          (dereference reference)
- r.f         (field access through reference)
```

**Definition 7.12** (Reference Safety Rules): References must satisfy temporal and spatial safety:

1. References cannot outlive their referents
2. Reference targets must be initialized
3. Mutable references require exclusive access
4. Reference arithmetic is prohibited

---

## 8. Module System

### 8.1 Module Structure

**Definition 8.1** (Module Definition): A module is a collection of items with associated visibility and dependency information:

```bnf
Module M ::= ⟨name: ModuleName,
             items: Item*,
             imports: ImportDecl*,
             exports: ExportDecl*,
             visibility: Visibility⟩

ModuleName ::= Identifier (:: Identifier)*

Item ::= TypeDecl | FunctionDecl | ConstDecl | ModuleDecl
```

**Definition 8.2** (Namespace Hierarchy): Modules form a hierarchical namespace with qualified names:

- **Root Namespace**: All modules exist within a global namespace
- **Nested Modules**: Modules can contain sub-modules
- **Qualified Names**: Items are referenced by their fully qualified path
- **Name Resolution**: Unqualified names are resolved according to scope rules

### 8.2 Import and Export Semantics

**Definition 8.3** (Import Declaration): Imports bring external items into the current module's scope:

```bnf
ImportDecl ::= import ModulePath                    (import all public items)
             | import ModulePath :: ItemList         (import specific items)
             | import ItemName from ModulePath       (import with renaming)
             | import ModulePath as Alias            (import with alias)

ModulePath ::= Identifier (:: Identifier)*
ItemList ::= ItemName (, ItemName)*
```

**Definition 8.4** (Export Declaration): Exports control the visibility of items to external modules:

```bnf
ExportDecl ::= export Item                          (re-export item)
             | export ModulePath :: ItemList         (re-export from module)
             | export * from ModulePath              (re-export all)

Visibility ::= public | private | protected
```

**Definition 8.5** (Visibility Rules): Item visibility determines accessibility across module boundaries:

- **Public**: Accessible from any module
- **Private**: Accessible only within the declaring module
- **Protected**: Accessible within the module hierarchy
- **Module-Private**: Default visibility for unexported items

### 8.3 Dependency Resolution

**Definition 8.6** (Dependency Graph): Module dependencies form a directed acyclic graph:

```bnf
Dependencies D : Module → P(Module)
Dependency Order: topological sort of D
```

**Definition 8.7** (Circular Dependency Detection): The module system rejects circular dependencies:

- **Static Analysis**: Dependency cycles are detected at compile time
- **Forward Declarations**: Allowed for mutually recursive types
- **Interface Separation**: Circular dependencies resolved through traits

### 8.4 Namespace Resolution

**Definition 8.8** (Name Resolution Algorithm): Names are resolved according to a priority hierarchy:

1. **Local Scope**: Variables and parameters in current function
2. **Module Scope**: Items declared in current module  
3. **Imported Items**: Items brought in through import declarations
4. **Standard Library**: Items from the standard library
5. **Qualified Lookup**: Explicit qualified names

**Definition 8.9** (Ambiguity Resolution): Naming conflicts are resolved through disambiguation rules:

- **Explicit Qualification**: Use fully qualified names to resolve ambiguity
- **Import Precedence**: More specific imports take precedence
- **Error Reporting**: Ambiguous names generate compile-time errors

### 8.5 Module Compilation

**Definition 8.10** (Compilation Units): Each module compiles to an independent compilation unit:

- **Interface Files**: Contain public type and signature information
- **Implementation Files**: Contain compiled code and private details
- **Dependency Information**: Metadata about required modules

**Definition 8.11** (Separate Compilation): Modules can be compiled independently when interfaces are stable:

- **Interface Stability**: Changes to public interfaces require recompilation of dependents
- **Implementation Changes**: Private implementation changes do not affect dependents
- **Incremental Compilation**: Only modified modules and their dependents are recompiled

---

## 9. Generic Programming Model

### 9.1 Parametric Polymorphism

**Definition 9.1** (Type Parameters): Generic types and functions are parameterized by type variables:

```bnf
GenericDecl ::= ⟨name: Identifier,
                 params: TypeParam*,
                 constraints: Constraint*,
                 body: Item⟩

TypeParam ::= ⟨name: Identifier,
              kind: Kind,
              default: Type?⟩

Constraint ::= TraitBound | LifetimeBound | TypeEquality
```

**Definition 9.2** (Type Parameter Constraints): Type parameters can be constrained to ensure required capabilities:

- **Trait Bounds**: `T: Trait` requires implementation of trait
- **Lifetime Bounds**: `T: 'a` requires references in T to outlive 'a
- **Type Equality**: `T: Clone = Self` specifies associated types
- **Higher-Kinded Types**: `F: * → *` for type constructors

### 9.2 Monomorphization

**Definition 9.3** (Monomorphization Process): Generic code is specialized for each concrete type instantiation:

1. **Instantiation Collection**: Collect all generic instantiations used in the program
2. **Specialization Generation**: Generate specialized versions for each instantiation
3. **Code Size Optimization**: Share implementations where possible
4. **Dead Code Elimination**: Remove unused specializations

**Definition 9.4** (Specialization Conditions): Code sharing is possible under certain conditions:

- **Type Erasure**: Types with identical representation can share code
- **Trait Object Dispatch**: Dynamic dispatch can replace monomorphization
- **Compile-Time Constants**: Constant parameters enable additional specialization

### 9.3 Type Inference for Generics

**Definition 9.5** (Generic Type Inference): Type inference for generics follows bidirectional typing:

```bnf
Inference Rules:
⊢ e : ∀α.τ    α ∉ FV(Γ)
─────────────────────────    (Instantiation)
⊢ e : [β/α]τ

Γ, α ⊢ e : τ    α ∉ FV(Γ)  
─────────────────────────    (Generalization)  
Γ ⊢ e : ∀α.τ
```

**Definition 9.6** (Constraint Solving): Generic constraints are solved using a constraint-based approach:

- **Constraint Generation**: Generate constraints from generic instantiations
- **Unification**: Solve type equality constraints through unification
- **Trait Resolution**: Resolve trait bounds through instance search
- **Error Reporting**: Report unsatisfiable constraints with helpful messages

### 9.4 Associated Types

**Definition 9.7** (Associated Type Definition): Traits can define associated types that are determined by implementations:

```bnf
trait Iterator {
    type Item;
    next(&mut self) -> Option<Self::Item>;
}

impl Iterator for Vec<T> {
    type Item = T;
    next(&mut self) -> Option<T> { ... }
}
```

**Definition 9.8** (Associated Type Projection): Associated types are accessed through projection syntax:

- **Qualified Syntax**: `<T as Iterator>::Item`
- **Unqualified Syntax**: `T::Item` (when unambiguous)
- **Higher-Ranked Types**: `for<T> T::Item where T: Iterator`

### 9.5 Variance and Subtyping

**Definition 9.9** (Type Parameter Variance): Generic types have variance properties for their parameters:

- **Covariant**: `F<A>` is subtype of `F<B>` when `A <: B`
- **Contravariant**: `F<A>` is subtype of `F<B>` when `B <: A`  
- **Invariant**: No subtyping relationship regardless of parameter relationship

**Definition 9.10** (Variance Inference): Variance is automatically inferred from type parameter usage:

- **Positive Position**: Covariant (function return types, struct fields)
- **Negative Position**: Contravariant (function parameters)
- **Mixed Position**: Invariant (mutable references, function parameters and returns)

---

## 10. Trait System

### 10.1 Trait Definition and Implementation

**Definition 10.1** (Trait Declaration): Traits define interfaces that types can implement:

```bnf
TraitDecl ::= trait TraitName GenericParams? SuperTraits? {
                TraitItem*
              }

TraitItem ::= FunctionSignature
            | AssociatedType  
            | AssociatedConst
            | DefaultImpl

SuperTraits ::= : TraitBound (, TraitBound)*
```

**Definition 10.2** (Trait Implementation): Types implement traits through implementation blocks:

```bnf
ImplDecl ::= implement trait TraitName GenericParams? for Type {
               ImplItem*
             }

ImplItem ::= FunctionDef
           | TypeAlias
           | ConstDef
```

**Definition 10.3** (Implementation Coherence): Trait implementations must satisfy coherence conditions:

- **Orphan Rule**: Can only implement trait T for type S if you define either T or S
- **Overlap Prevention**: No two implementations can overlap
- **Completeness**: All trait items must be implemented

### 10.2 Trait Bounds and Where Clauses

**Definition 10.4** (Trait Bounds): Generic parameters can be constrained by trait requirements:

```bnf
TraitBound ::= TypeParam : Trait
             | TypeParam : Trait + Trait  
             | TypeParam : for<'a> Trait<'a>

WhereClause ::= where TraitBound (, TraitBound)*
```

**Definition 10.5** (Bound Satisfaction): Type parameter bounds are satisfied when implementations exist:

```bnf
Satisfaction Relation ⊨:
Γ ⊨ T : Trait  iff  ∃ impl Trait for T ∈ Γ

Higher-Ranked Bounds:
Γ ⊨ T : for<'a> Trait<'a>  iff  ∀'a. Γ ⊨ T : Trait<'a>
```

### 10.3 Dynamic Dispatch

**Definition 10.6** (Trait Objects): Traits can be used as types for dynamic dispatch:

```bnf
TraitObject ::= dyn Trait
              | dyn Trait + Trait
              | dyn Trait + 'lifetime

Object Safety Conditions:
- No static methods
- No generic methods  
- Self appears only in receiver position
- No associated constants
```

**Definition 10.7** (Virtual Table Layout): Trait objects use virtual tables for method dispatch:

```bnf
VTable ::= ⟨type_info: TypeInfo,
           destructor: (*mut ()),
           size: usize,
           align: usize,
           methods: [(args) -> ret]*⟩
```

### 10.4 Associated Items

**Definition 10.8** (Associated Types): Traits can declare associated types determined by implementations:

```bnf
Associated Type Declaration:
trait Trait {
    type AssocType: Bound;
}

Associated Type Implementation:  
impl Trait for Type {
    type AssocType = ConcreteType;
}
```

**Definition 10.9** (Associated Constants): Traits can declare associated constants:

```bnf
trait Trait {
    const CONSTANT: Type;
}

impl Trait for Type {
    const CONSTANT: Type = value;
}
```

### 10.5 Trait Resolution

**Definition 10.10** (Trait Resolution Algorithm): Trait method calls are resolved through a priority system:

1. **Inherent Methods**: Methods defined directly on the type
2. **Trait Methods**: Methods from traits in scope
3. **Prelude Traits**: Methods from standard library traits
4. **Disambiguation**: Explicit syntax for ambiguous cases

**Definition 10.11** (Coherence Rules): Trait implementations must be coherent and non-overlapping:

- **Local Coherence**: Within a crate, implementations cannot overlap
- **Global Coherence**: Across crates, orphan rules prevent conflicts  
- **Negative Reasoning**: Absence of implementations can be reasoned about

---

## 11. Error Handling

### 11.1 Error Types and Semantics

**Definition 11.1** (Error Classification): Cryo distinguishes between different categories of errors:

```bnf
Error Categories:
- RecoverableError: Errors that can be handled by user code
- UnrecoverableError: Errors that cause program termination  
- CompileTimeError: Errors detected during compilation
- RuntimeError: Errors detected during execution
```

**Definition 11.2** (Result Type): The Result type encapsulates operations that may fail:

```cryo
enum Result<T, E> {
    Ok(T),
    Err(E)
}

// Error Propagation Operator:
expression? ≡ match expression {
    Ok(value) => value,
    Err(error) => return Err(error)
}
```

**Definition 11.3** (Option Type): The Option type represents optional values:

```cryo
enum Option<T> {
    Some(T),
    None
}

// Null Pointer Alternative:
Option<T> replaces nullable pointers for safe null handling
```

### 11.2 Panic and Recovery

**Definition 11.4** (Panic Semantics): Panics represent unrecoverable errors:

```bnf
Panic Behavior:
1. Immediate termination of current thread
2. Stack unwinding with destructor execution
3. Panic hook execution for cleanup
4. Process termination or thread abort
```

**Definition 11.5** (Panic Safety): Operations must be panic-safe to maintain program invariants:

- **Exception Safety**: Data structures remain in valid states after panic
- **Resource Safety**: Resources are properly released during unwinding
- **Atomicity**: Operations are atomic with respect to panics

### 11.3 Error Propagation

**Definition 11.6** (Error Propagation Rules): Errors propagate through the call stack according to:

1. **Automatic Propagation**: `?` operator propagates compatible errors
2. **Type Conversion**: Errors are converted using `From` trait implementations
3. **Early Return**: Error propagation causes early function return
4. **Composition**: Error contexts can be composed and nested

**Definition 11.7** (Error Context): Errors can carry additional context information:

```cryo
trait Context<T> {
    context(self, msg: &str) -> Result<T, ContextError>;
    with_context<F>(self, f: F) -> Result<T, ContextError>
        where F: FnOnce() -> String;
}
```

### 11.4 Compile-Time Error Detection

**Definition 11.8** (Static Analysis): The compiler performs static analysis to detect potential errors:

- **Type Checking**: Ensures type safety at all program points
- **Borrow Checking**: Prevents memory safety violations
- **Exhaustiveness Checking**: Ensures pattern matches are complete
- **Reachability Analysis**: Detects unreachable code paths

**Definition 11.9** (Error Recovery**: The compiler attempts error recovery for better diagnostics:

- **Syntax Error Recovery**: Continue parsing after syntax errors
- **Type Error Recovery**: Infer reasonable types after type errors
- **Suggestion Generation**: Provide fix suggestions where possible

---

## 12. Runtime System

### 12.1 Runtime Architecture

**Definition 12.1** (Runtime Components): The Cryo runtime consists of several interconnected subsystems:

```bnf
Runtime System ::= ⟨memory_manager: MemoryManager,
                    gc_system: GarbageCollector,
                    type_system: TypeSystem,
                    thread_scheduler: ThreadScheduler,
                    panic_handler: PanicHandler,
                    ffi_bridge: FFIBridge⟩
```

**Definition 12.2** (Runtime State Machine): The runtime follows a state machine model for lifecycle management:

```bnf
RuntimeState ::= Uninitialized
               | Initializing  
               | Running
               | ShuttingDown
               | Terminated

State Transitions:
Uninitialized → Initializing → Running → ShuttingDown → Terminated
```

### 12.2 Memory Management Subsystem

**Definition 12.3** (Heap Manager): The heap manager implements segregated free lists with coalescing:

```bnf
HeapManager ::= ⟨free_lists: SizeClass → FreeList,
                 large_objects: Set<LargeObject>,
                 allocation_stats: AllocationStats⟩

Block Management:
- Size classes from 32 bytes to 4KB
- Large objects (>4KB) handled separately  
- Free blocks coalesced to reduce fragmentation
- Block headers contain metadata for safety
```

**Definition 12.4** (Stack Management): Stack frames are managed automatically with overflow detection:

```bnf
StackFrame ::= ⟨locals: LocalVariables,
               return_address: Address,
               frame_pointer: Address,
               stack_guard: GuardPage⟩

Stack Safety:
- Guard pages detect stack overflow
- Stack probes prevent silent corruption
- Automatic unwinding on exceptions
```

### 12.3 Type System Runtime

**Definition 12.5** (Runtime Type Information): Types carry runtime information for safety checking:

```bnf
TypeInfo ::= ⟨type_id: TypeId,
             size: Size,
             alignment: Alignment,  
             drop_fn: Option<DropFn>,
             vtable: Option<VTable>⟩

Type Checking:
- Dynamic type checks for trait objects
- Cast safety verification
- Size and alignment validation
```

**Definition 12.6** (Generic Monomorphization): Generic functions are monomorphized at compile time:

- Each generic instantiation generates specialized code
- Type parameters are erased at runtime
- Trait objects provide dynamic dispatch when needed
- Dead code elimination removes unused specializations

### 12.4 Concurrency Runtime

**Definition 12.7** (Thread Model**: Cryo uses a 1:1 threading model with OS threads:

```bnf
Thread ::= ⟨thread_id: ThreadId,
           stack: Stack,
           local_heap: LocalHeap,
           state: ThreadState⟩

ThreadState ::= Running | Blocked | Terminated
```

**Definition 12.8** (Synchronization Primitives): The runtime provides safe synchronization:

- **Atomic Operations**: Lock-free primitives for basic operations
- **Mutexes**: Exclusive access to shared data
- **Condition Variables**: Thread coordination and signaling
- **Channels**: Message passing between threads

### 12.5 Panic Handling

**Definition 12.9** (Panic Propagation): Panics unwind the stack while executing destructors:

```bnf
Panic Handler:
1. Capture panic location and message
2. Begin stack unwinding
3. Execute destructors for each frame
4. Call panic hooks for cleanup
5. Terminate thread or process
```

**Definition 12.10** (Panic Safety Guarantees**: The runtime maintains safety during panics:

- Memory safety is preserved during unwinding
- Resources are properly released
- Data structure invariants are maintained
- No memory leaks occur due to panics

---

## 13. Standard Library

### 13.1 Core Module Organization

**Definition 13.1** (Standard Library Structure): The standard library is organized into logical modules:

```bnf
std Library Structure:
├── core/           (Core types and traits)
│   ├── types       (Fundamental types)  
│   ├── intrinsics  (Compiler intrinsics)
│   ├── memory      (Memory management)
│   └── syscall     (System calls)
├── collections/    (Data structures)
├── io/            (Input/output)
├── string/        (String processing)
├── thread/        (Concurrency)  
├── fs/            (File system)
├── net/           (Networking)
└── sys/           (System interfaces)
```

**Definition 13.2** (Module Dependencies): Standard library modules form a dependency hierarchy:

- **Core modules** have no dependencies
- **Primitive modules** depend only on core  
- **Higher-level modules** build on primitive modules
- **Circular dependencies** are eliminated through careful design

### 13.2 Core Types and Traits

**Definition 13.3** (Fundamental Traits): Core traits provide basic capabilities:

```cryo
trait Clone {
    clone(&self) -> Self;
}

trait Copy: Clone {}  // Marker trait for trivial copying

trait Drop {
    drop(&mut self);  // Custom cleanup logic
}

trait Default {
    default() -> Self;  // Default value construction
}

trait Debug {
    fmt(&self, f: &mut Formatter) -> Result;
}

trait Display {
    fmt(&self, f: &mut Formatter) -> Result;  
}
```

**Definition 13.4** (Comparison Traits): Traits for ordering and equality:

```cryo
trait PartialEq<Rhs = Self> {
    eq(&self, other: &Rhs) -> bool;
    ne(&self, other: &Rhs) -> bool { !self.eq(other) }
}

trait Eq: PartialEq<Self> {}

trait PartialOrd<Rhs = Self>: PartialEq<Rhs> {
    partial_cmp(&self, other: &Rhs) -> Option<Ordering>;
}

trait Ord: Eq + PartialOrd<Self> {
    cmp(&self, other: &Self) -> Ordering;
}
```

### 13.3 Memory Management

**Definition 13.5** (Allocation Interface): Standard allocator interface:

```
trait Allocator {
    allocate(&self, layout: Layout) -> Result<*mut u8, AllocError>;
    deallocate(&self, ptr: *mut u8, layout: Layout);
    realloc(&self, ptr: *mut u8, old: Layout, new: Layout) 
        -> Result<*mut u8, AllocError>;
}

Global Allocator:
- Default system allocator
- Configurable per-thread allocators  
- Custom allocator implementations
- Zero-overhead allocation abstractions
```

**Definition 13.6** (Smart Pointers): Automatic memory management through smart pointers:

```cryo
struct Box<T> {          // Owned heap allocation
    ptr: *mut T,
    _marker: PhantomData<T>
}

struct Rc<T> {           // Reference counted
    ptr: *mut RcBox<T>  
}

struct Arc<T> {          // Atomic reference counted  
    ptr: *mut ArcInner<T>
}
```

### 13.4 Collections Framework

**Definition 13.7** (Collection Traits): Generic interfaces for data structures:

```cryo
trait Iterator {
    type Item;
    next(&mut self) -> Option<Self::Item>;
    
    // Default implementations for common operations
    collect<C>(self) -> C where C: FromIterator<Self::Item>;
    map<B, F>(self, f: F) -> Map<Self, F> where F: FnMut(Self::Item) -> B;
    filter<P>(self, predicate: P) -> Filter<Self, P>;
}

trait FromIterator<A> {
    from_iter<T>(iter: T) -> Self where T: IntoIterator<Item = A>;
}
```

**Definition 13.8** (Container Types): Standard data structures with performance guarantees:

- **Array\<T\>**: Dynamic array with O(1) indexing, O(n) insertion
- **LinkedList\<T\>**: Doubly-linked list with O(1) insertion/removal  
- **HashMap\<K,V\>**: Hash table with O(1) average access
- **BTreeMap\<K,V\>**: B-tree with O(log n) ordered operations
- **HashSet\<T\>**: Hash set for uniqueness constraints
- **BinaryHeap\<T\>**: Priority queue with O(log n) operations

### 13.5 Input/Output Framework

**Definition 13.9** (IO Traits): Abstract interfaces for input/output operations:

```
trait Read {
    read(&mut self, buf: &mut [u8]) -> Result<usize>;
    read_to_end(&mut self, buf: &mut Vec<u8>) -> Result<usize>;
    read_exact(&mut self, buf: &mut [u8]) -> Result<()>;
}

trait Write {  
    write(&mut self, buf: &[u8]) -> Result<usize>;
    flush(&mut self) -> Result<()>;
    write_all(&mut self, buf: &[u8]) -> Result<()>;
}

trait Seek {
    seek(&mut self, pos: SeekFrom) -> Result<u64>;
}
```

**Definition 13.10** (Buffered IO): Efficient buffered operations:

- **BufReader\<R\>**: Buffered reading with lookahead
- **BufWriter\<W\>**: Buffered writing with batching
- **LineWriter\<W\>**: Line-buffered output for terminals
- **Cursor\<T\>**: In-memory buffer with seek capability

---

## 14. Compilation Model

### 14.1 Compilation Pipeline

**Definition 14.1** (Compilation Phases): The Cryo compiler follows a multi-phase compilation model:

```
Compilation Pipeline:
Source Code → Lexical Analysis → Syntax Analysis → Semantic Analysis 
→ Type Checking → Borrow Checking → MIR Generation → Optimization 
→ Monomorphization → LLVM IR Generation → Code Generation → Linking
```

**Definition 14.2** (Intermediate Representations): The compiler uses multiple IRs for optimization:

- **AST**: Abstract syntax tree preserving source structure
- **HIR**: High-level IR after desugaring and name resolution  
- **MIR**: Mid-level IR suitable for analysis and optimization
- **LLVM IR**: Low-level IR for code generation

### 14.2 Type Checking and Inference

**Definition 14.3** (Type Checking Algorithm): Type checking follows a constraint-based approach:

1. **Constraint Generation**: Generate type constraints from syntax
2. **Constraint Solving**: Solve constraints using unification
3. **Trait Resolution**: Resolve trait bounds and associated types
4. **Coherence Checking**: Ensure trait implementations are coherent

**Definition 14.4** (Borrow Checking**: Memory safety is ensured through static analysis:

- **Lifetime Inference**: Infer lifetime parameters for references
- **Ownership Analysis**: Track ownership transfer and borrowing
- **Aliasing Detection**: Prevent dangerous aliasing patterns
- **Move Semantics**: Enforce move semantics for linear types

### 14.3 Optimization Framework

**Definition 14.5** (Optimization Passes**: The compiler applies various optimization transformations:

**High-Level Optimizations**:
- Constant folding and propagation
- Dead code elimination  
- Inlining of small functions
- Loop optimization and vectorization

**Mid-Level Optimizations**:
- Escape analysis for stack allocation
- Devirtualization of trait calls
- Specialization of generic functions
- Effect analysis for optimization opportunities

**Low-Level Optimizations**:
- Register allocation and spilling
- Instruction scheduling and selection  
- Peephole optimizations
- Link-time optimization

**Definition 14.6** (Optimization Safety**: All optimizations preserve program semantics:

- **Correctness Preservation**: Optimizations maintain program meaning
- **Safety Preservation**: Memory and type safety are maintained
- **Debug Information**: Debug info remains accurate through optimization
- **Reproducible Builds**: Optimization results are deterministic

### 14.4 Code Generation

**Definition 14.7** (LLVM Backend**: Code generation targets LLVM IR for portability:

- **Platform Independence**: LLVM handles target-specific details
- **Optimization Pipeline**: Leverage LLVM's optimization passes
- **Debug Support**: Generate DWARF debug information
- **Link-Time Optimization**: Cross-module optimization

**Definition 14.8** (ABI Compliance**: Generated code follows platform ABIs:

- **Calling Conventions**: Proper function call protocols
- **Data Layout**: Compatible struct and union layouts
- **Exception Handling**: Platform exception mechanisms
- **Foreign Function Interface**: C-compatible interfaces

### 14.5 Incremental Compilation

**Definition 14.9** (Dependency Tracking**: The compiler tracks fine-grained dependencies:

- **Query-Based Architecture**: Compilation as cached queries
- **Incremental Analysis**: Only reprocess changed components
- **Parallel Compilation**: Independent modules compile concurrently
- **Cache Management**: Intelligent cache invalidation

**Definition 14.10** (Separate Compilation**: Modules compile independently when possible:

- **Interface Stability**: Public interfaces determine recompilation needs
- **Link-Time Checks**: Verify compatibility at link time
- **Metadata Preservation**: Store necessary information for separate compilation

---

## 15. Formal Properties

### 15.1 Type Safety

**Definition 15.1** (Type Safety Theorem): Cryo satisfies the fundamental type safety property:

**Theorem 15.1** (Type Safety): If ⊢ e : τ and ⟨e, ∅, ∅, ∅, halt⟩ →* ⟨v, ρ, σ, h, halt⟩, then ⊢ v : τ.

*Proof Sketch*: By induction on the operational semantics, showing that each reduction step preserves typing. The proof relies on:
- **Preservation**: Type preservation under reduction
- **Progress**: Well-typed expressions either reduce or are values
- **Canonical Forms**: Values have canonical forms for their types

**Theorem 15.2** (Memory Safety): Well-typed programs do not exhibit memory safety violations:

1. **Spatial Safety**: All memory accesses are within allocated bounds
2. **Temporal Safety**: No access to deallocated memory  
3. **Type Safety**: Memory contents match their declared types

### 15.2 Soundness Properties

**Definition 15.2** (Soundness of Type System): The static type system correctly predicts runtime behavior:

**Theorem 15.3** (Soundness): If the type checker accepts a program, then the program will not exhibit runtime type errors.

**Theorem 15.4** (Completeness of Borrow Checker): The borrow checker accepts all memory-safe programs that follow the ownership discipline:

- **Precision**: The analysis is precise enough to accept safe programs
- **Conservatism**: The analysis rejects potentially unsafe programs
- **Decidability**: The analysis terminates for all input programs

### 15.3 Semantic Properties

**Definition 15.3** (Determinism): Cryo programs have deterministic sequential semantics:

**Theorem 15.5** (Sequential Determinism): For any expression e, if ⟨e, ρ, σ, h, κ⟩ →* ⟨v₁, ρ₁, σ₁, h₁, κ₁⟩ and ⟨e, ρ, σ, h, κ⟩ →* ⟨v₂, ρ₂, σ₂, h₂, κ₂⟩, then v₁ = v₂ (when both computations terminate).

**Definition 15.4** (Confluence): Reduction strategies do not affect final results:

**Theorem 15.6** (Confluence): Different evaluation orders produce equivalent results for terminating computations.

### 15.4 Complexity Properties

**Definition 15.5** (Compilation Complexity): The compilation process has polynomial complexity:

**Theorem 15.7** (Type Checking Complexity): Type checking is decidable in polynomial time for programs without higher-ranked types.

**Theorem 15.8** (Borrow Checking Complexity): Borrow checking has linear complexity in the size of the program text.

### 15.5 Abstraction Properties

**Definition 15.6** (Zero-Cost Abstractions): High-level abstractions compile to efficient code:

**Theorem 15.9** (Generic Specialization): Monomorphized generic code has the same performance as hand-specialized code.

**Theorem 15.10** (Trait Dispatch Optimization): Static trait dispatch compiles to direct function calls when possible.

---

## 16. Conclusion

### 16.1 Summary of Contributions

This formal specification establishes Cryo as a principled systems programming language that successfully reconciles safety and performance through careful language design. The key contributions of this specification include:

**Theoretical Foundation**: A rigorous type-theoretic foundation based on System F<sub>ω</sub> with linear types and effects, providing formal guarantees of memory safety and type safety.

**Practical Safety**: A comprehensive approach to memory safety that eliminates undefined behavior while maintaining explicit control over performance-critical operations.

**Zero-Cost Abstractions**: A design that ensures high-level language constructs compile to efficient machine code without runtime overhead.

**Compositional Design**: A systematic approach where language features interact predictably and compose naturally to enable complex programming patterns.

### 16.2 Language Design Principles

The development of Cryo demonstrates several important principles for systems language design:

**Static Safety**: Moving error detection to compile time through sophisticated static analysis eliminates entire classes of runtime errors while improving performance.

**Explicit Resource Management**: Providing explicit control over memory allocation and layout while maintaining safety through static analysis.

**Gradual Complexity**: Supporting both simple patterns for common cases and sophisticated patterns for complex requirements.

**Tool-Friendly Design**: Designing language semantics to support sophisticated development tools and IDE integration.

### 16.3 Performance Characteristics

Cryo achieves performance comparable to C and C++ through several design decisions:

**Minimal Runtime**: The runtime system provides only essential services, avoiding unnecessary overhead for applications that don't need them.

**Compile-Time Resolution**: Generic programming, trait dispatch, and pattern matching all resolve at compile time, eliminating runtime overhead.

**Memory Layout Control**: Programmers can control memory layout and allocation strategies while maintaining safety guarantees.

**Optimization Opportunities**: The type system provides information that enables aggressive optimization by both the compiler and LLVM backend.

### 16.4 Safety Guarantees

The formal properties established in this specification provide strong safety guarantees:

**Memory Safety**: The combination of ownership types, lifetime analysis, and borrow checking eliminates memory safety violations at compile time.

**Type Safety**: The static type system ensures that runtime values always match their declared types.

**Concurrency Safety**: The type system prevents data races and other concurrency bugs through static analysis.

**Exception Safety**: The panic system ensures that resources are properly cleaned up even when errors occur.

### 16.5 Future Extensions

The formal foundation established in this specification supports several future extensions:

**Async/Await**: The effect system provides a foundation for adding structured concurrency primitives.

**Dependent Types**: The kind system could be extended to support limited dependent types for array bounds and protocol specifications.

**Linear Types**: The ownership system could be extended with full linear types for resource management.

**Higher-Kinded Types**: The generic system could support higher-kinded types for advanced abstractions.

### 16.6 Comparison with Related Work

Cryo builds on insights from several research areas:

**Rust**: Shares the ownership and borrowing model but with different syntax and some semantic variations.

**C++**: Provides similar performance characteristics but with compile-time safety guarantees.

**ML Family**: Adopts the algebraic data type and pattern matching concepts with systems programming adaptations.

**Linear Logic**: Incorporates linear type theory for resource management in a practical programming context.

### 16.7 Verification and Implementation

This specification serves as the foundation for both formal verification and practical implementation:

**Formal Verification**: The mathematical semantics enable formal proofs of program properties using tools like Coq or Lean.

**Reference Implementation**: The operational semantics guide the development of interpreters and compilers.

**Testing Framework**: The type system rules enable automated generation of test cases for compiler validation.

**Standardization**: This specification provides the foundation for potential language standardization efforts.

### 16.8 Conclusion

Cryo represents a significant step forward in systems programming language design by demonstrating that safety and performance are not fundamentally at odds. Through careful application of type theory, static analysis, and compiler technology, it is possible to create languages that provide the safety guarantees of high-level languages while maintaining the performance characteristics essential for systems programming.

The formal specification presented here establishes Cryo as a well-founded programming language with clear semantics and strong safety properties. It provides both the theoretical foundation necessary for reasoning about program correctness and the practical guidance needed for implementing robust, efficient compilers.

As systems programming continues to evolve, languages like Cryo point the way toward a future where safety and performance are complementary rather than competing concerns. This specification represents not just the definition of a particular language, but a demonstration of the principles and techniques necessary for achieving this synthesis.

---

## References

[1] Wright, A. K., & Felleisen, M. (1994). A syntactic approach to type soundness. *Information and Computation*, 115(1), 38-94.

[2] Pierce, B. C. (2002). *Types and programming languages*. MIT press.

[3] Harper, R. (2016). *Practical foundations for programming languages*. Cambridge University Press.

[4] Girard, J. Y. (1972). Interprétation fonctionelle et élimination des coupures de l'arithmétique d'ordre supérieur. *Thèse de doctorat d'État*, Université Paris VII.

[5] Reynolds, J. C. (1974). Towards a theory of type structure. In *Programming Symposium* (pp. 408-425). Springer.

[6] Wadler, P. (1990). Linear types can change the world. In *Programming concepts and methods* (Vol. 2, No. 3, pp. 347-359).

[7] Ahmed, A., Fluet, M., & Morrisett, G. (2007). L3: A linear language with locations. *Fundamenta Informaticae*, 77(4), 397-449.

[8] Klabnik, S., & Nichols, C. (2019). *The Rust Programming Language*. No Starch Press.

[9] Stroustrup, B. (2013). *The C++ programming language*. Pearson Education.

[10] Milner, R., Tofte, M., Harper, R., & MacQueen, D. (1997). *The definition of Standard ML: revised*. MIT press.

---

## Appendices

### Appendix A: Complete Grammar

[The complete BNF grammar would be included here, building on the fragments presented throughout the specification]

### Appendix B: Type System Rules

[Complete typing rules for all language constructs would be presented here in formal notation]

### Appendix C: Operational Semantics Rules

[Complete operational semantics rules would be provided here]

### Appendix D: Standard Library Interface

[Complete interface specifications for the standard library would be documented here]

### Appendix E: LLVM IR Generation

[Detailed mapping from Cryo constructs to LLVM IR would be specified here]

---

*This specification represents Version 1.0 of the Cryo language definition. Future versions will extend and refine these definitions based on implementation experience and formal verification efforts.*