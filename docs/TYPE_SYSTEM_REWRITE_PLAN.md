# Cryo Type System Rewrite Plan

**Author:** Claude (AI Assistant)
**Date:** January 2025
**Status:** Design Document

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current System Analysis](#2-current-system-analysis)
3. [Design Principles](#3-design-principles)
4. [New Type System Architecture](#4-new-type-system-architecture)
5. [Type Identity System](#5-type-identity-system)
6. [Type Registry and Ownership](#6-type-registry-and-ownership)
7. [Type Hierarchy](#7-type-hierarchy)
8. [Type Resolution](#8-type-resolution)
9. [Generic Types and Monomorphization](#9-generic-types-and-monomorphization)
10. [Cross-Module Type Sharing](#10-cross-module-type-sharing)
11. [Symbol Integration](#11-symbol-integration)
12. [Error Handling](#12-error-handling)
13. [Migration Strategy](#13-migration-strategy)
14. [Implementation Phases](#14-implementation-phases)
15. [File Structure](#15-file-structure)
16. [Testing Strategy](#16-testing-strategy)

---

## 1. Executive Summary

### 1.1 Why Rewrite?

The current Cryo type system has accumulated significant technical debt over a year of development:

- **String-based type identity** causes fragile comparisons and subtle bugs
- **50+ hardcoded string comparisons** scattered throughout the codebase
- **Raw `Type*` pointers** with no ownership semantics leading to null pointer issues
- **Unknown type as silent error recovery** masks real type errors
- **1,474-line type resolution function** with 20+ fallback paths
- **Type corruption defensive coding** (try/catch with "CORRUPTED_TYPE" fallbacks)

These issues compound in multi-module compilation like the stdlib, where cross-module type resolution becomes unreliable.

### 1.2 Goals

1. **Strong Type Identity** - Types identified by unique TypeID, not string names
2. **Clear Ownership** - TypeArena owns all types; handles are lightweight references
3. **Single Resolution Path** - One well-defined type resolution algorithm
4. **Proper Error Handling** - ErrorType tracks failures, no silent Unknown fallbacks
5. **Module-Aware** - Types know their home module for cross-module resolution
6. **Generic-First** - First-class support for parameterized types
7. **Performance** - Efficient type comparisons via ID, not string

### 1.3 Scope

This rewrite encompasses:
- Type representation (`Type.hpp`, `Type.cpp`)
- Type checking (`TypeChecker.cpp`)
- Monomorphization (`MonomorphizationPass.cpp`)
- Symbol type storage (`SymbolTable`)
- Cross-module type resolution (`ModuleLoader`)
- Codegen type mapping (`TypeMapper`)

---

## 2. Current System Analysis

### 2.1 Critical Issues

| Issue | Severity | Impact |
|-------|----------|--------|
| String-based type identity | Critical | Type equality fails across modules |
| Raw Type* pointers | Critical | Null pointer crashes, use-after-free |
| Unknown type recovery | High | Real errors masked, silent failures |
| 1400+ line resolution function | High | Unmaintainable, debugging impossible |
| Hardcoded type strings | Medium | Adding types requires multi-file edits |
| Dual API (old/new) | Medium | Confusion, inconsistent usage |

### 2.2 Current Type Identity

```cpp
// Current: String-based comparison
bool Type::equals(const Type &other) const {
    if (_kind == other._kind && _name == other._name)
        return true;
    // ... special cases for Generic/Struct equality
}
```

**Problems:**
- Two `Array<int>` from different modules may have different `_name` strings
- Generic types use name "T" which conflicts across different generics
- No distinction between `Option` in module A vs `Option` in module B

### 2.3 Current Ownership Model

```cpp
// Symbol stores raw pointer - no ownership
struct Symbol {
    Type *data_type;  // Who owns this? When is it freed?
};

// AST nodes store raw pointer
class ExpressionNode {
    Cryo::Type *_resolved_type = nullptr;  // Dangling pointer risk
};
```

### 2.4 Current Resolution Complexity

The `resolve_type_with_generic_context()` function spans 1,474 lines with these fallback paths:

1. Check generic parameters
2. Check module-qualified types
3. Check tuple types
4. Check reference types
5. Check parameterized types
6. Check array types
7. Check generic syntax again
8. Check symbol table
9. Try token-based parsing
10. Try primitive types
11. Try user-defined types
12. Cross-module lookup via SRM
13. Return nullptr

Each step masks the previous failure, making debugging nearly impossible.

---

## 3. Design Principles

### 3.1 Core Principles

1. **Types are values, TypeRefs are handles**
   - Type objects live in TypeArena
   - Code passes around TypeRef (lightweight handle)
   - No raw pointers to types

2. **Identity by ID, not name**
   - Each type gets a unique TypeID at creation
   - Equality check is `id1 == id2` (O(1))
   - Names are for display only

3. **Fail explicitly, not silently**
   - No Unknown type fallback
   - ErrorType captures what went wrong
   - Compilation fails fast with good diagnostics

4. **Module-aware from the start**
   - Types know their declaring module
   - Cross-module types resolved via ModuleTypeRegistry
   - No namespace string parsing at runtime

5. **Generics are first-class**
   - GenericTypeParam, GenericTypeArg as explicit types
   - Instantiation creates new type with new ID
   - Substitution is explicit operation

### 3.2 Non-Goals

- Runtime type information (RTTI) - this is compile-time only
- Dependent types - out of scope for this rewrite
- Type inference algorithm changes - focusing on representation

---

## 4. New Type System Architecture

### 4.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        TypeArena                                 │
│  (Owns all Type objects, provides TypeRef handles)              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Primitives  │  │ Compounds   │  │ User-Defined            │  │
│  │ ─────────── │  │ ─────────── │  │ ───────────────────     │  │
│  │ VoidType    │  │ ArrayType   │  │ StructType              │  │
│  │ BoolType    │  │ PointerType │  │ ClassType               │  │
│  │ IntType     │  │ RefType     │  │ EnumType                │  │
│  │ FloatType   │  │ FunctionType│  │ TraitType               │  │
│  │ CharType    │  │ TupleType   │  │ GenericStructType       │  │
│  │ StringType  │  │ OptionalType│  │ GenericClassType        │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │ Generic Types                                                │ │
│  │ ─────────────────────────────────────────────────────────── │ │
│  │ GenericParam     - Type parameter (T, U, E)                 │ │
│  │ BoundedParam     - Type parameter with constraints          │ │
│  │ InstantiatedType - Concrete instantiation (Array<int>)      │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │ Error Types                                                  │ │
│  │ ─────────────────────────────────────────────────────────── │ │
│  │ ErrorType - Tracks resolution failure with reason           │ │
│  │ NeverType - Bottom type (functions that don't return)       │ │
│  └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    ModuleTypeRegistry                            │
│  (Tracks which types belong to which modules)                   │
├─────────────────────────────────────────────────────────────────┤
│  module_id -> { type_name -> TypeRef }                          │
│  Handles cross-module type lookup                               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    GenericRegistry                               │
│  (Manages generic type templates and instantiations)            │
├─────────────────────────────────────────────────────────────────┤
│  template_id -> GenericTemplate (AST node, params)              │
│  instantiation_key -> TypeRef (cached instantiations)           │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 Component Interactions

```
Parser                  TypeChecker              Codegen
  │                         │                       │
  │  AST with type         │                       │
  │  annotations           │                       │
  └────────────────────────►                       │
                            │                       │
          TypeResolver      │                       │
              │             │                       │
              ▼             │                       │
         TypeArena ◄────────┘                       │
              │                                     │
              │  TypeRef                            │
              └────────────────────────────────────►
                                                    │
                                               TypeMapper
                                                    │
                                                    ▼
                                               LLVM Types
```

---

## 5. Type Identity System

### 5.1 TypeID

```cpp
// Unique identifier for each type
struct TypeID {
    uint64_t id;

    bool operator==(const TypeID& other) const { return id == other.id; }
    bool operator!=(const TypeID& other) const { return id != other.id; }

    // Hash support for use in containers
    struct Hash {
        size_t operator()(const TypeID& tid) const { return std::hash<uint64_t>{}(tid.id); }
    };

    static TypeID invalid() { return TypeID{0}; }
    bool is_valid() const { return id != 0; }
};
```

### 5.2 TypeRef

```cpp
// Lightweight handle to a type in the arena
class TypeRef {
private:
    TypeID _id;
    TypeArena* _arena;  // Back-pointer for dereferencing

public:
    TypeRef() : _id(TypeID::invalid()), _arena(nullptr) {}
    TypeRef(TypeID id, TypeArena* arena) : _id(id), _arena(arena) {}

    // Equality based on ID only
    bool operator==(const TypeRef& other) const { return _id == other._id; }
    bool operator!=(const TypeRef& other) const { return _id != other._id; }

    // Access the actual type
    const Type* get() const;
    const Type* operator->() const { return get(); }
    const Type& operator*() const { return *get(); }

    // Validity checking
    bool is_valid() const { return _id.is_valid() && _arena != nullptr; }
    bool is_error() const;  // Check if this is an ErrorType

    // Type ID access
    TypeID id() const { return _id; }

    // Hash support
    struct Hash {
        size_t operator()(const TypeRef& ref) const { return TypeID::Hash{}(ref._id); }
    };
};
```

### 5.3 Type Equality

```cpp
// Old way (BAD):
if (type1->name() == type2->name() && type1->kind() == type2->kind())

// New way (GOOD):
if (type_ref1 == type_ref2)  // Compares TypeID only
```

### 5.4 Module-Qualified Identity

For user-defined types, identity includes module:

```cpp
struct QualifiedTypeName {
    ModuleID module;
    std::string name;

    bool operator==(const QualifiedTypeName& other) const {
        return module == other.module && name == other.name;
    }
};
```

This ensures that `std::core::Option` and `mylib::Option` are different types.

---

## 6. Type Registry and Ownership

### 6.1 TypeArena

```cpp
class TypeArena {
private:
    // All types owned by the arena
    std::vector<std::unique_ptr<Type>> _types;

    // ID -> index mapping for fast lookup
    std::unordered_map<TypeID, size_t, TypeID::Hash> _id_to_index;

    // Next ID to assign
    std::atomic<uint64_t> _next_id{1};

    // Canonical type caching
    struct PrimitiveCache {
        TypeRef void_type;
        TypeRef bool_type;
        TypeRef i8_type, i16_type, i32_type, i64_type, i128_type;
        TypeRef u8_type, u16_type, u32_type, u64_type, u128_type;
        TypeRef f32_type, f64_type;
        TypeRef char_type;
        TypeRef string_type;
        TypeRef never_type;
    } _primitives;

    // Compound type deduplication
    std::unordered_map<TypeRef, TypeRef, TypeRef::Hash> _pointer_types;    // pointee -> ptr
    std::unordered_map<TypeRef, TypeRef, TypeRef::Hash> _reference_types;  // referent -> ref
    std::unordered_map<TypeRef, TypeRef, TypeRef::Hash> _optional_types;   // wrapped -> optional
    std::unordered_map<std::string, TypeRef> _array_types;                 // key = element_id + size
    std::unordered_map<std::string, TypeRef> _function_types;              // key = signature hash

public:
    TypeArena();

    // Primitive type accessors (always return same instance)
    TypeRef get_void() const { return _primitives.void_type; }
    TypeRef get_bool() const { return _primitives.bool_type; }
    TypeRef get_i32() const { return _primitives.i32_type; }
    // ... etc for all primitives

    // Compound type creation (deduplicated)
    TypeRef get_pointer_to(TypeRef pointee);
    TypeRef get_reference_to(TypeRef referent, bool is_mutable = false);
    TypeRef get_optional_of(TypeRef wrapped);
    TypeRef get_array_of(TypeRef element, std::optional<size_t> size = std::nullopt);
    TypeRef get_function(TypeRef return_type, std::vector<TypeRef> params, bool is_variadic = false);
    TypeRef get_tuple(std::vector<TypeRef> elements);

    // User-defined type creation
    TypeRef create_struct(const QualifiedTypeName& name);
    TypeRef create_class(const QualifiedTypeName& name);
    TypeRef create_enum(const QualifiedTypeName& name, std::vector<EnumVariant> variants);
    TypeRef create_trait(const QualifiedTypeName& name);
    TypeRef create_type_alias(const QualifiedTypeName& name, TypeRef target);

    // Generic type support
    TypeRef create_generic_param(const std::string& name, size_t index);
    TypeRef create_bounded_param(const std::string& name, size_t index, std::vector<TypeRef> bounds);
    TypeRef create_generic_struct(const QualifiedTypeName& name, std::vector<GenericParam> params);
    TypeRef create_generic_class(const QualifiedTypeName& name, std::vector<GenericParam> params);
    TypeRef instantiate_generic(TypeRef generic_type, std::vector<TypeRef> type_args);

    // Error type creation
    TypeRef create_error(const std::string& reason, SourceLocation location);

    // Lookup by ID
    const Type* lookup(TypeID id) const;

private:
    TypeID allocate_id() { return TypeID{_next_id++}; }
    TypeRef register_type(std::unique_ptr<Type> type);
};
```

### 6.2 Type Lifetime

**Rule:** Types are **never** freed during compilation.

The TypeArena owns all types for the entire compilation session. This eliminates:
- Use-after-free bugs
- Dangling pointer issues
- Complex lifetime tracking

Memory is reclaimed when compilation finishes and TypeArena is destroyed.

### 6.3 Type Deduplication

For compound types, we ensure only one instance exists:

```cpp
TypeRef TypeArena::get_pointer_to(TypeRef pointee) {
    auto it = _pointer_types.find(pointee);
    if (it != _pointer_types.end()) {
        return it->second;  // Return existing
    }

    // Create new pointer type
    auto ptr_type = std::make_unique<PointerType>(pointee);
    TypeRef ref = register_type(std::move(ptr_type));
    _pointer_types[pointee] = ref;
    return ref;
}
```

This ensures `int*` created anywhere in compilation is the same TypeRef.

---

## 7. Type Hierarchy

### 7.1 Base Type Class

```cpp
class Type {
protected:
    TypeID _id;
    TypeKind _kind;

public:
    Type(TypeID id, TypeKind kind) : _id(id), _kind(kind) {}
    virtual ~Type() = default;

    // Identity
    TypeID id() const { return _id; }
    TypeKind kind() const { return _kind; }

    // Type properties (virtual)
    virtual bool is_primitive() const { return false; }
    virtual bool is_numeric() const { return false; }
    virtual bool is_integral() const { return false; }
    virtual bool is_floating_point() const { return false; }
    virtual bool is_signed() const { return false; }
    virtual bool is_reference_type() const { return false; }
    virtual bool is_nullable() const { return false; }
    virtual bool is_generic() const { return false; }
    virtual bool is_error() const { return false; }

    // Size and alignment (for codegen)
    virtual size_t size_bytes() const = 0;
    virtual size_t alignment() const = 0;

    // Display
    virtual std::string display_name() const = 0;
    virtual std::string mangled_name() const = 0;
};
```

### 7.2 TypeKind Enum

```cpp
enum class TypeKind : uint8_t {
    // Primitives
    Void,
    Bool,
    Int,
    Float,
    Char,
    String,

    // Compounds
    Pointer,
    Reference,
    Array,
    Function,
    Tuple,
    Optional,

    // User-defined
    Struct,
    Class,
    Enum,
    Trait,
    TypeAlias,

    // Generics
    GenericParam,      // T in fn foo<T>()
    BoundedParam,      // T: Trait
    InstantiatedType,  // Array<int>

    // Special
    Error,
    Never,
};
```

### 7.3 Primitive Types

```cpp
class IntType : public Type {
private:
    IntegerKind _int_kind;  // I8, I16, I32, I64, I128, U8, U16, U32, U64, U128

public:
    IntType(TypeID id, IntegerKind kind);

    IntegerKind integer_kind() const { return _int_kind; }
    bool is_primitive() const override { return true; }
    bool is_numeric() const override { return true; }
    bool is_integral() const override { return true; }
    bool is_signed() const override;

    size_t size_bytes() const override;
    size_t alignment() const override;
    std::string display_name() const override;
    std::string mangled_name() const override;
};

// Similar for FloatType, BoolType, CharType, etc.
```

### 7.4 Compound Types

```cpp
class PointerType : public Type {
private:
    TypeRef _pointee;

public:
    PointerType(TypeID id, TypeRef pointee);

    TypeRef pointee() const { return _pointee; }
    bool is_nullable() const override { return true; }

    size_t size_bytes() const override { return sizeof(void*); }
    size_t alignment() const override { return sizeof(void*); }
    std::string display_name() const override;  // "int*"
    std::string mangled_name() const override;  // "Pi" or similar
};

class FunctionType : public Type {
private:
    TypeRef _return_type;
    std::vector<TypeRef> _param_types;
    bool _is_variadic;

public:
    FunctionType(TypeID id, TypeRef return_type,
                 std::vector<TypeRef> param_types, bool is_variadic);

    TypeRef return_type() const { return _return_type; }
    const std::vector<TypeRef>& param_types() const { return _param_types; }
    bool is_variadic() const { return _is_variadic; }

    // ... size/alignment/display methods
};
```

### 7.5 User-Defined Types

```cpp
class StructType : public Type {
private:
    QualifiedTypeName _qualified_name;
    std::vector<StructField> _fields;  // Populated after declaration processing
    bool _is_complete;  // False until fields are set

public:
    StructType(TypeID id, QualifiedTypeName name);

    const QualifiedTypeName& qualified_name() const { return _qualified_name; }
    const std::string& name() const { return _qualified_name.name; }
    ModuleID module() const { return _qualified_name.module; }

    bool is_complete() const { return _is_complete; }
    void set_fields(std::vector<StructField> fields);
    const std::vector<StructField>& fields() const { return _fields; }

    std::optional<size_t> field_index(const std::string& name) const;
    std::optional<TypeRef> field_type(const std::string& name) const;

    size_t size_bytes() const override;
    size_t alignment() const override;
    std::string display_name() const override;
    std::string mangled_name() const override;
};

struct StructField {
    std::string name;
    TypeRef type;
    size_t offset;  // Computed during layout
};
```

### 7.6 Generic Types

```cpp
class GenericParamType : public Type {
private:
    std::string _name;     // "T"
    size_t _index;         // Position in parameter list
    std::vector<TypeRef> _bounds;  // Trait constraints

public:
    GenericParamType(TypeID id, std::string name, size_t index,
                     std::vector<TypeRef> bounds = {});

    const std::string& param_name() const { return _name; }
    size_t param_index() const { return _index; }
    const std::vector<TypeRef>& bounds() const { return _bounds; }

    bool is_generic() const override { return true; }

    // Size unknown until instantiation
    size_t size_bytes() const override { return 0; }
    size_t alignment() const override { return 1; }
    std::string display_name() const override { return _name; }
};

class InstantiatedType : public Type {
private:
    TypeRef _generic_base;           // The generic type (Array<T>)
    std::vector<TypeRef> _type_args; // Concrete arguments [int]
    TypeRef _resolved_type;          // The concrete result type

public:
    InstantiatedType(TypeID id, TypeRef generic_base,
                     std::vector<TypeRef> type_args, TypeRef resolved);

    TypeRef generic_base() const { return _generic_base; }
    const std::vector<TypeRef>& type_args() const { return _type_args; }
    TypeRef resolved_type() const { return _resolved_type; }

    // Delegate to resolved type
    size_t size_bytes() const override;
    size_t alignment() const override;
    std::string display_name() const override;  // "Array<int>"
};
```

### 7.7 Error Type

```cpp
class ErrorType : public Type {
private:
    std::string _reason;
    SourceLocation _location;
    std::vector<std::string> _notes;

public:
    ErrorType(TypeID id, std::string reason, SourceLocation location);

    const std::string& reason() const { return _reason; }
    const SourceLocation& location() const { return _location; }
    void add_note(std::string note) { _notes.push_back(std::move(note)); }

    bool is_error() const override { return true; }

    // Errors have no size (compilation should fail)
    size_t size_bytes() const override { return 0; }
    size_t alignment() const override { return 1; }
    std::string display_name() const override { return "<error: " + _reason + ">"; }
};
```

---

## 8. Type Resolution

### 8.1 TypeResolver Class

Replace the monolithic `resolve_type_with_generic_context()` with a focused resolver:

```cpp
class TypeResolver {
private:
    TypeArena& _arena;
    ModuleTypeRegistry& _module_registry;
    GenericRegistry& _generic_registry;
    DiagnosticManager& _diagnostics;

    // Current resolution context
    struct ResolutionContext {
        ModuleID current_module;
        std::vector<GenericBinding> generic_bindings;  // T -> int mappings
        std::unordered_set<TypeRef, TypeRef::Hash> in_progress;  // Cycle detection
    };

public:
    TypeResolver(TypeArena& arena, ModuleTypeRegistry& modules,
                 GenericRegistry& generics, DiagnosticManager& diag);

    // Main entry point
    TypeRef resolve(const TypeAnnotation& annotation, ResolutionContext& ctx);

private:
    // Specific resolution methods (no fallback chains!)
    TypeRef resolve_primitive(const std::string& name);
    TypeRef resolve_user_defined(const QualifiedTypeName& name, ResolutionContext& ctx);
    TypeRef resolve_pointer(TypeRef pointee);
    TypeRef resolve_reference(TypeRef referent, bool is_mutable);
    TypeRef resolve_array(TypeRef element, std::optional<size_t> size);
    TypeRef resolve_function(TypeRef return_type, std::vector<TypeRef> params, bool variadic);
    TypeRef resolve_generic_instantiation(TypeRef base, std::vector<TypeRef> args, ResolutionContext& ctx);
    TypeRef resolve_generic_param(const std::string& name, ResolutionContext& ctx);

    // Error creation
    TypeRef make_error(const std::string& reason, SourceLocation loc);
};
```

### 8.2 Resolution Algorithm

```cpp
TypeRef TypeResolver::resolve(const TypeAnnotation& annotation, ResolutionContext& ctx) {
    switch (annotation.kind()) {
        case TypeAnnotationKind::Primitive:
            return resolve_primitive(annotation.name());

        case TypeAnnotationKind::Named: {
            // First check if it's a generic parameter in scope
            if (auto bound = find_generic_binding(annotation.name(), ctx)) {
                return *bound;
            }

            // Otherwise resolve as user-defined type
            QualifiedTypeName qname = qualify_name(annotation.name(), ctx.current_module);
            return resolve_user_defined(qname, ctx);
        }

        case TypeAnnotationKind::Pointer:
            return resolve_pointer(resolve(annotation.inner(), ctx));

        case TypeAnnotationKind::Reference:
            return resolve_reference(resolve(annotation.inner(), ctx), annotation.is_mutable());

        case TypeAnnotationKind::Array:
            return resolve_array(resolve(annotation.element(), ctx), annotation.size());

        case TypeAnnotationKind::Function:
            return resolve_function_type(annotation, ctx);

        case TypeAnnotationKind::Generic: {
            TypeRef base = resolve(annotation.base(), ctx);
            std::vector<TypeRef> args;
            for (const auto& arg : annotation.type_args()) {
                args.push_back(resolve(arg, ctx));
            }
            return resolve_generic_instantiation(base, std::move(args), ctx);
        }

        default:
            return make_error("Unknown type annotation kind", annotation.location());
    }
}
```

### 8.3 No Fallbacks - Fail Fast

The key difference from the current system:

```cpp
// OLD (bad): Try everything, return Unknown if all fail
Type* old_resolve(const std::string& type_str) {
    if (auto t = try_primitive(type_str)) return t;
    if (auto t = try_struct(type_str)) return t;
    if (auto t = try_class(type_str)) return t;
    // ... 15 more fallbacks ...
    return get_unknown_type();  // Silent failure!
}

// NEW (good): Single path, explicit error
TypeRef TypeResolver::resolve_user_defined(const QualifiedTypeName& name,
                                            ResolutionContext& ctx) {
    // Check module registry
    if (auto type = _module_registry.lookup(name)) {
        return *type;
    }

    // Not found - this IS an error
    return make_error(
        fmt::format("Unknown type '{}' in module '{}'", name.name, name.module.name()),
        ctx.current_location
    );
}
```

---

## 9. Generic Types and Monomorphization

### 9.1 GenericRegistry

```cpp
class GenericRegistry {
private:
    struct GenericTemplate {
        TypeRef type;                          // The generic type (GenericStructType, etc.)
        std::vector<GenericParam> params;      // [T, U, E]
        ASTNode* ast_node;                     // For monomorphization
        ModuleID source_module;
    };

    std::unordered_map<TypeID, GenericTemplate, TypeID::Hash> _templates;

    // Cache of instantiations: (base_id, [arg_ids]) -> instantiated_type
    struct InstantiationKey {
        TypeID base;
        std::vector<TypeID> args;

        bool operator==(const InstantiationKey& other) const;
        struct Hash { size_t operator()(const InstantiationKey&) const; };
    };
    std::unordered_map<InstantiationKey, TypeRef, InstantiationKey::Hash> _instantiations;

public:
    void register_template(TypeRef generic_type, std::vector<GenericParam> params,
                          ASTNode* ast_node, ModuleID module);

    std::optional<GenericTemplate> get_template(TypeRef type);

    TypeRef instantiate(TypeRef generic_type, std::vector<TypeRef> type_args,
                        TypeArena& arena);

    std::optional<TypeRef> get_cached_instantiation(TypeRef base,
                                                     const std::vector<TypeRef>& args);
};
```

### 9.2 Generic Instantiation

```cpp
TypeRef GenericRegistry::instantiate(TypeRef generic_type,
                                      std::vector<TypeRef> type_args,
                                      TypeArena& arena) {
    // Check cache first
    if (auto cached = get_cached_instantiation(generic_type, type_args)) {
        return *cached;
    }

    // Get template info
    auto template_opt = get_template(generic_type);
    if (!template_opt) {
        return arena.create_error("Not a generic type", SourceLocation{});
    }
    auto& tmpl = *template_opt;

    // Validate argument count
    if (type_args.size() != tmpl.params.size()) {
        return arena.create_error(
            fmt::format("Expected {} type arguments, got {}",
                       tmpl.params.size(), type_args.size()),
            SourceLocation{}
        );
    }

    // Create substitution map
    std::unordered_map<TypeID, TypeRef, TypeID::Hash> substitutions;
    for (size_t i = 0; i < type_args.size(); ++i) {
        substitutions[tmpl.params[i].type.id()] = type_args[i];
    }

    // Create instantiated type
    TypeRef result = arena.instantiate_generic(generic_type, type_args);

    // Cache and return
    cache_instantiation(generic_type, type_args, result);
    return result;
}
```

### 9.3 Monomorphization Pass

```cpp
class MonomorphizationPass {
private:
    TypeArena& _arena;
    GenericRegistry& _generic_registry;
    std::vector<InstantiationRequest> _pending_instantiations;

public:
    void collect_instantiations(const ProgramNode& ast);
    void generate_instantiations(ProgramNode& ast);

private:
    // Visitor that finds all generic type usages
    void visit_type_usage(TypeRef type, const SourceLocation& loc);

    // Creates concrete AST node for an instantiation
    std::unique_ptr<ASTNode> create_instantiated_ast(
        const GenericRegistry::GenericTemplate& tmpl,
        const std::vector<TypeRef>& type_args);
};
```

---

## 10. Cross-Module Type Sharing

### 10.1 ModuleTypeRegistry

```cpp
class ModuleTypeRegistry {
private:
    struct ModuleTypes {
        std::unordered_map<std::string, TypeRef> types;
        std::unordered_map<std::string, TypeRef> type_aliases;
    };

    std::unordered_map<ModuleID, ModuleTypes, ModuleID::Hash> _modules;

public:
    // Register type in module
    void register_type(ModuleID module, const std::string& name, TypeRef type);
    void register_alias(ModuleID module, const std::string& alias, TypeRef target);

    // Lookup type by qualified name
    std::optional<TypeRef> lookup(const QualifiedTypeName& name);

    // Lookup in specific module
    std::optional<TypeRef> lookup_in_module(ModuleID module, const std::string& name);

    // Resolve type with imports
    std::optional<TypeRef> resolve_with_imports(
        const std::string& name,
        ModuleID current_module,
        const std::vector<Import>& imports);
};
```

### 10.2 Module Loading Integration

```cpp
// In ModuleLoader
void ModuleLoader::load_module(ModuleID id, const std::string& path) {
    // Parse and type-check module
    auto ast = parse_module(path);

    // Register all types from this module
    for (const auto& decl : ast->declarations()) {
        if (auto struct_decl = dynamic_cast<StructDeclaration*>(decl.get())) {
            TypeRef type = _type_arena.create_struct({id, struct_decl->name()});
            _module_registry.register_type(id, struct_decl->name(), type);
        }
        // ... handle other declaration types
    }

    // Process imports from this module
    for (const auto& import : ast->imports()) {
        ModuleID imported = resolve_import(import);
        load_module_if_needed(imported);
    }
}
```

### 10.3 Type Export/Import

Types are visible across modules based on visibility modifiers:

```cpp
struct TypeVisibility {
    enum class Level { Public, Private, Internal };
    Level level;
};

// When loading module, only export public types
void register_exports(ModuleID module, const ModuleAST& ast) {
    for (const auto& decl : ast.type_declarations()) {
        if (decl.visibility() == Visibility::Public) {
            TypeRef type = resolve_type(decl);
            _module_registry.register_type(module, decl.name(), type);
        }
    }
}
```

---

## 11. Symbol Integration

### 11.1 Updated Symbol Structure

```cpp
struct Symbol {
    std::string name;
    SymbolKind kind;
    SourceLocation declaration_location;
    TypeRef type;  // TypeRef instead of Type*
    ModuleID home_module;
    Visibility visibility;

    // For functions
    std::optional<FunctionSignature> function_signature;

    // For variables
    bool is_mutable = false;

    bool has_type() const { return type.is_valid(); }
    bool is_error() const { return type.is_valid() && type.is_error(); }
};
```

### 11.2 SymbolTable Updates

```cpp
class SymbolTable {
private:
    std::unordered_map<std::string, Symbol> _symbols;
    std::unique_ptr<SymbolTable> _parent;
    ModuleID _current_module;

public:
    void register_symbol(Symbol symbol);

    std::optional<Symbol> lookup(const std::string& name) const;
    std::optional<Symbol> lookup_qualified(const QualifiedName& name) const;

    // Type-specific lookups
    std::optional<TypeRef> lookup_type(const std::string& name) const;
    std::optional<TypeRef> lookup_type_qualified(const QualifiedTypeName& name) const;
};
```

### 11.3 AST Node Type Storage

```cpp
class ExpressionNode : public ASTNode {
private:
    TypeRef _type;  // TypeRef instead of Type*

public:
    TypeRef get_type() const { return _type; }
    void set_type(TypeRef type) { _type = type; }
    bool has_type() const { return _type.is_valid(); }
    bool has_error_type() const { return _type.is_valid() && _type.is_error(); }
};
```

---

## 12. Error Handling

### 12.1 No More Unknown Type

The `UnknownType` is **removed**. Instead:

```cpp
// When type resolution fails:
TypeRef TypeResolver::resolve_user_defined(...) {
    if (auto type = _module_registry.lookup(name)) {
        return *type;
    }

    // Create an ErrorType with diagnostic information
    auto error = _arena.create_error(
        fmt::format("Undefined type '{}'", name.name),
        annotation.location()
    );

    // Report to diagnostic system
    _diagnostics.report(Diagnostic::Error,
        annotation.location(),
        fmt::format("use of undefined type '{}'", name.name));

    return error;
}
```

### 12.2 Error Propagation

```cpp
// Errors propagate through compound types
TypeRef TypeArena::get_pointer_to(TypeRef pointee) {
    // If pointee is an error, return error (don't create pointer-to-error)
    if (pointee.is_error()) {
        return pointee;  // Propagate the original error
    }

    // Normal case
    return create_pointer(pointee);
}
```

### 12.3 Error Collection

```cpp
class TypeCheckResult {
private:
    std::vector<TypeRef> _errors;
    bool _has_fatal_errors = false;

public:
    void add_error(TypeRef error_type) {
        _errors.push_back(error_type);
    }

    bool has_errors() const { return !_errors.empty(); }

    void report_all(DiagnosticManager& diag) {
        for (const auto& err : _errors) {
            const ErrorType* error = static_cast<const ErrorType*>(err.get());
            diag.report(Diagnostic::Error, error->location(), error->reason());
            for (const auto& note : error->notes()) {
                diag.note(error->location(), note);
            }
        }
    }
};
```

---

## 13. Migration Strategy

### 13.1 Strangler Fig Pattern

We'll implement the new type system alongside the old one, then migrate incrementally:

```
Phase 1: New types exist alongside old (types2/)
Phase 2: Codegen uses new types
Phase 3: TypeChecker uses new types
Phase 4: Parser uses new types
Phase 5: Remove old type system
```

### 13.2 Adapter Layer

During migration, an adapter bridges old and new:

```cpp
class TypeSystemAdapter {
private:
    TypeArena& _new_arena;
    TypeContext& _old_context;

    // Mapping between old and new
    std::unordered_map<Type*, TypeRef> _old_to_new;
    std::unordered_map<TypeID, Type*, TypeID::Hash> _new_to_old;

public:
    // Convert old Type* to new TypeRef
    TypeRef convert_to_new(Type* old_type);

    // Convert new TypeRef to old Type* (for gradual migration)
    Type* convert_to_old(TypeRef new_type);
};
```

### 13.3 Component Migration Order

1. **TypeArena + Core Types** - Foundation
2. **TypeMapper (Codegen)** - Low risk, validates the design
3. **SymbolTable** - Update Symbol to use TypeRef
4. **TypeChecker** - Largest change, use TypeResolver
5. **MonomorphizationPass** - Update to use GenericRegistry
6. **Parser** - Create TypeAnnotation nodes
7. **ModuleLoader** - Update type registration
8. **Remove old TypeContext** - Final cleanup

---

## 14. Implementation Phases

### Phase 1: Foundation (Week 1-2)

**Goal:** Create new type system infrastructure

- [ ] Create `include/Types2/TypeID.hpp` - TypeID and TypeRef
- [ ] Create `include/Types2/Type.hpp` - Base Type class and hierarchy
- [ ] Create `include/Types2/TypeArena.hpp` - Type ownership
- [ ] Create `src/Types2/TypeArena.cpp` - Implementation
- [ ] Create `include/Types2/TypeKind.hpp` - TypeKind enum
- [ ] Unit tests for TypeArena and basic types

### Phase 2: Type Hierarchy (Week 2-3)

**Goal:** Implement all type classes

- [ ] Primitive types (Int, Float, Bool, Char, String, Void)
- [ ] Compound types (Pointer, Reference, Array, Function, Tuple, Optional)
- [ ] User-defined types (Struct, Class, Enum, Trait)
- [ ] Generic types (GenericParam, InstantiatedType)
- [ ] Error type (ErrorType, NeverType)
- [ ] Unit tests for each type class

### Phase 3: Resolution Infrastructure (Week 3-4)

**Goal:** Type resolution without fallback chains

- [ ] Create `include/Types2/TypeResolver.hpp`
- [ ] Create `include/Types2/ModuleTypeRegistry.hpp`
- [ ] Create `include/Types2/GenericRegistry.hpp`
- [ ] Implement TypeResolver with single resolution path
- [ ] Integration tests for type resolution

### Phase 4: Codegen Migration (Week 4-5)

**Goal:** TypeMapper uses new types

- [ ] Create TypeSystemAdapter
- [ ] Update TypeMapper to accept TypeRef
- [ ] Map TypeRef to LLVM types
- [ ] Test codegen with new types
- [ ] Validate against existing test suite

### Phase 5: TypeChecker Migration (Week 5-7)

**Goal:** TypeChecker uses new TypeResolver

- [ ] Update SymbolTable to use TypeRef
- [ ] Replace `resolve_type_with_generic_context` with TypeResolver
- [ ] Update AST nodes to use TypeRef
- [ ] Update type checking logic
- [ ] Test against stdlib compilation

### Phase 6: Monomorphization Update (Week 7-8)

**Goal:** Generic instantiation with new system

- [ ] Update MonomorphizationPass for GenericRegistry
- [ ] Update template registration
- [ ] Test generic struct/class/function instantiation
- [ ] Test cross-module generics

### Phase 7: Cleanup (Week 8-9)

**Goal:** Remove old type system

- [ ] Remove TypeContext
- [ ] Remove old Type.hpp/Type.cpp
- [ ] Remove adapter layer
- [ ] Update all imports
- [ ] Final test pass

---

## 15. File Structure

```
include/
├── Types2/                          # New type system
│   ├── TypeID.hpp                   # TypeID, TypeRef
│   ├── TypeKind.hpp                 # TypeKind enum
│   ├── Type.hpp                     # Base Type class
│   ├── PrimitiveTypes.hpp           # Int, Float, Bool, etc.
│   ├── CompoundTypes.hpp            # Pointer, Array, Function, etc.
│   ├── UserDefinedTypes.hpp         # Struct, Class, Enum, Trait
│   ├── GenericTypes.hpp             # GenericParam, InstantiatedType
│   ├── ErrorType.hpp                # ErrorType, NeverType
│   ├── TypeArena.hpp                # Type ownership and creation
│   ├── TypeResolver.hpp             # Type resolution
│   ├── ModuleTypeRegistry.hpp       # Cross-module type tracking
│   ├── GenericRegistry.hpp          # Generic template registry
│   └── TypeSystemAdapter.hpp        # Migration adapter (temporary)
│
├── AST/
│   ├── ASTNode.hpp                  # Update to use TypeRef
│   ├── SymbolTable.hpp              # Update Symbol to use TypeRef
│   └── ...
│
└── Codegen/
    └── TypeMapper.hpp               # Update to use TypeRef

src/
├── Types2/
│   ├── TypeArena.cpp
│   ├── PrimitiveTypes.cpp
│   ├── CompoundTypes.cpp
│   ├── UserDefinedTypes.cpp
│   ├── GenericTypes.cpp
│   ├── TypeResolver.cpp
│   ├── ModuleTypeRegistry.cpp
│   ├── GenericRegistry.cpp
│   └── TypeSystemAdapter.cpp
│
└── ...
```

---

## 16. Testing Strategy

### 16.1 Unit Tests

```cpp
// Test TypeArena
TEST(TypeArenaTest, PrimitivesAreSingletons) {
    TypeArena arena;
    EXPECT_EQ(arena.get_i32(), arena.get_i32());
    EXPECT_NE(arena.get_i32(), arena.get_i64());
}

TEST(TypeArenaTest, PointerDeduplication) {
    TypeArena arena;
    TypeRef int_ptr1 = arena.get_pointer_to(arena.get_i32());
    TypeRef int_ptr2 = arena.get_pointer_to(arena.get_i32());
    EXPECT_EQ(int_ptr1, int_ptr2);
}

TEST(TypeArenaTest, ErrorTypePropagation) {
    TypeArena arena;
    TypeRef error = arena.create_error("test error", SourceLocation{});
    TypeRef ptr_to_error = arena.get_pointer_to(error);
    EXPECT_TRUE(ptr_to_error.is_error());
}
```

### 16.2 Integration Tests

```cpp
// Test cross-module type resolution
TEST(TypeResolverTest, CrossModuleStruct) {
    TypeArena arena;
    ModuleTypeRegistry registry;

    ModuleID mod_a = create_module("module_a");
    TypeRef struct_a = arena.create_struct({mod_a, "MyStruct"});
    registry.register_type(mod_a, "MyStruct", struct_a);

    ModuleID mod_b = create_module("module_b");

    // Resolve from module B with import
    Import import{mod_a, {"MyStruct"}};
    auto resolved = registry.resolve_with_imports("MyStruct", mod_b, {import});

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, struct_a);
}
```

### 16.3 Regression Tests

Run the existing test suite after each phase to ensure no regressions:

```bash
# After each phase
./build/cryo test tests/
./build/cryo build stdlib/  # Critical for stdlib compilation
```

---

## Appendix A: Comparison with Current System

| Aspect | Current System | New System |
|--------|---------------|------------|
| Type Identity | String name | TypeID (uint64_t) |
| Ownership | Raw Type* pointers | TypeArena owns, TypeRef handles |
| Equality | String comparison | ID comparison (O(1)) |
| Error Handling | UnknownType fallback | ErrorType with diagnostics |
| Resolution | 20+ fallback paths | Single deterministic path |
| Generic Params | Name "T" collision | Index-based identity |
| Cross-Module | Fragile string lookup | ModuleTypeRegistry |
| Memory Safety | Null pointer risks | Always valid TypeRef |

---

## Appendix B: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Subtle behavior changes | Medium | High | Extensive testing, run stdlib |
| Performance regression | Low | Medium | Benchmark type operations |
| Long migration period | Medium | Medium | Adapter layer for gradual migration |
| Breaking existing code | Low | High | Keep old system working during migration |

---

## Appendix C: Open Questions

1. **Should TypeRef be copyable or move-only?**
   - Recommendation: Copyable (like shared_ptr, but cheaper)

2. **How to handle recursive types (e.g., linked list)?**
   - Use forward declarations, set fields after creation

3. **Thread safety for TypeArena?**
   - Single-threaded for now; can add mutex later if needed

4. **Should we support type interning for complex types?**
   - Yes, for Array, Function, Pointer to ensure uniqueness

---

*End of Design Document*
