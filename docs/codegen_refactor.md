# CryoLang Codegen Phase Refactoring Plan

## Executive Summary

The `CodegenVisitor.cpp` file has grown to **19,820 lines**, representing 74.7% of all codegen code. This document provides a technical plan to decompose this monolith into a modular, production-grade architecture with clean APIs that eliminate code duplication.

---

## 1. Current State Analysis

### 1.1 File Size Distribution

| File | Lines | % of Total |
|------|-------|------------|
| CodegenVisitor.cpp | 19,820 | 74.7% |
| TypeMapper.cpp | 2,684 | 10.1% |
| Intrinsics.cpp | 1,904 | 7.2% |
| FunctionRegistry.cpp | 565 | 2.1% |
| LLVMContext.cpp | 531 | 2.0% |
| CodeGenerator.cpp | 502 | 1.9% |
| TargetConfig.cpp | 349 | 1.3% |
| ValueContext.cpp | 186 | 0.7% |
| **Total** | **26,541** | 100% |

### 1.2 Critical Problem Areas in CodegenVisitor.cpp

| Method/Section | Approx. Lines | Issue |
|----------------|---------------|-------|
| `generate_binary_operation()` | ~2,300 | Massive switch/if-else chain for all operators |
| `generate_function_call()` | ~3,100 | Handles constructors, intrinsics, methods, runtime calls |
| `generate_function_body()` | ~2,200 | Complex control flow, parameter handling |
| Assignment handling in binary ops | ~500 | Duplicated across identifier, member, array contexts |
| Array access codegen | ~900 | `Array<T>` vs raw array logic intermingled |
| Enum handling | ~700+ | Scattered across multiple methods |

### 1.3 Existing Good Patterns to Preserve

1. **`Intrinsics` class** - Clean dispatch pattern with dedicated methods per intrinsic
2. **`TypeMapper`** - Centralized type conversion with caching
3. **`FunctionRegistry`** - Function metadata classification
4. **`ValueContext`** - Scope-aware value tracking
5. **`NodeTracker` RAII helper** - AST node tracking for error reporting

---

## 2. Proposed Architecture

### 2.1 High-Level Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          CodegenVisitor                                  │
│  (Thin dispatcher - ~500 lines, delegates to specialized generators)    │
└─────────────────────────────┬───────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│ Declaration   │    │  Statement    │    │  Expression   │
│   Codegen     │    │   Codegen     │    │   Codegen     │
│  (~2000 lines)│    │ (~1500 lines) │    │ (~3000 lines) │
└───────┬───────┘    └───────┬───────┘    └───────┬───────┘
        │                    │                    │
        ▼                    ▼                    ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│  TypeCodegen  │    │ ControlFlow   │    │  Operators    │
│ struct/class/ │    │   Codegen     │    │   Codegen     │
│    enum       │    │ if/for/match  │    │  binary/unary │
└───────────────┘    └───────────────┘    └───────────────┘
                                                  │
                              ┌───────────────────┼───────────────────┐
                              ▼                   ▼                   ▼
                     ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
                     │  CallCodegen  │   │ MemoryCodegen │   │ CastCodegen   │
                     │  func/method/ │   │ alloca/load/  │   │ type casts    │
                     │  constructor  │   │    store      │   │               │
                     └───────────────┘   └───────────────┘   └───────────────┘
```

### 2.2 Shared Infrastructure Layer

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        CodegenContext (NEW)                              │
│  Unified access to: LLVMContextManager, TypeMapper, ValueContext,        │
│  FunctionRegistry, Intrinsics, SymbolTable, DiagnosticManager            │
└─────────────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────┴─────────────────────────────┐
│                     CodegenHelpers (NEW)                   │
│  Common operations: create_load, create_store,             │
│  create_entry_block_alloca, ensure_valid_insertion_point   │
└────────────────────────────────────────────────────────────┘
```

---

## 3. New File Structure

### 3.1 Directory Layout

```
src/Codegen/
├── CodegenVisitor.cpp          # Reduced to ~500 lines (dispatcher only)
├── CodegenContext.cpp          # NEW: Shared context management
├── CodegenHelpers.cpp          # NEW: Common utility functions
│
├── Declarations/               # NEW DIRECTORY
│   ├── DeclarationCodegen.cpp  # Function, variable declarations
│   ├── TypeCodegen.cpp         # Struct, class, enum generation
│   └── GenericCodegen.cpp      # Generic type instantiation
│
├── Statements/                 # NEW DIRECTORY
│   ├── StatementCodegen.cpp    # Block, return, expression statements
│   └── ControlFlowCodegen.cpp  # If, while, for, match, switch
│
├── Expressions/                # NEW DIRECTORY
│   ├── ExpressionCodegen.cpp   # Literals, identifiers, member access
│   ├── OperatorCodegen.cpp     # Binary and unary operators
│   ├── CallCodegen.cpp         # Function/method/constructor calls
│   └── CastCodegen.cpp         # Type casting operations
│
├── Memory/                     # NEW DIRECTORY
│   ├── MemoryCodegen.cpp       # Alloca, load, store operations
│   └── ScopeManager.cpp        # Scope enter/exit, destructor tracking
│
├── TypeMapper.cpp              # Existing (minor refactoring)
├── Intrinsics.cpp              # Existing (no changes needed)
├── FunctionRegistry.cpp        # Existing (no changes needed)
├── LLVMContext.cpp             # Existing (no changes needed)
├── ValueContext.cpp            # Existing (minor refactoring)
├── CodeGenerator.cpp           # Existing (no changes needed)
└── TargetConfig.cpp            # Existing (no changes needed)
```

### 3.2 Header Structure

```
include/Codegen/
├── CodegenVisitor.hpp          # Simplified interface
├── CodegenContext.hpp          # NEW: Shared context
├── CodegenHelpers.hpp          # NEW: Common utilities
├── ICodegenComponent.hpp       # NEW: Base interface for all generators
│
├── Declarations/
│   ├── DeclarationCodegen.hpp
│   ├── TypeCodegen.hpp
│   └── GenericCodegen.hpp
│
├── Statements/
│   ├── StatementCodegen.hpp
│   └── ControlFlowCodegen.hpp
│
├── Expressions/
│   ├── ExpressionCodegen.hpp
│   ├── OperatorCodegen.hpp
│   ├── CallCodegen.hpp
│   └── CastCodegen.hpp
│
├── Memory/
│   ├── MemoryCodegen.hpp
│   └── ScopeManager.hpp
│
└── (existing headers remain)
```

---

## 4. Core API Design

### 4.1 CodegenContext - The Shared State Container

```cpp
namespace Cryo::Codegen {

/**
 * @brief Unified context for all codegen components
 *
 * This replaces the need for each component to hold references
 * to multiple managers. All components receive a single context.
 */
class CodegenContext {
public:
    CodegenContext(
        LLVMContextManager& llvm_ctx,
        SymbolTable& symbols,
        DiagnosticManager* diagnostics
    );

    // LLVM Access
    llvm::LLVMContext& llvm_context();
    llvm::IRBuilder<>& builder();
    llvm::Module* module();

    // Component Access
    TypeMapper& types();
    ValueContext& values();
    FunctionRegistry& functions();
    Intrinsics& intrinsics();

    // Symbol Resolution
    SRM::SymbolResolutionManager& srm();
    SRM::SymbolResolutionContext& srm_context();

    // Error Reporting
    DiagnosticManager* diagnostics();
    void report_error(ErrorCode code, ASTNode* node, const std::string& msg);

    // Current State
    FunctionContext* current_function();
    void set_current_function(FunctionContext* fn);

    ASTNode* current_node();
    void set_current_node(ASTNode* node);

    // Value Registration (centralizes the 109+ register_value calls)
    void register_value(ASTNode* node, llvm::Value* value);
    llvm::Value* get_value(ASTNode* node);

    // Current Value (for expression results)
    void set_result(llvm::Value* value);
    llvm::Value* get_result();

private:
    LLVMContextManager& _llvm;
    SymbolTable& _symbols;
    // ... other members
};

} // namespace Cryo::Codegen
```

### 4.2 ICodegenComponent - Base Interface

```cpp
namespace Cryo::Codegen {

/**
 * @brief Base interface for all codegen component classes
 *
 * Provides common functionality and ensures consistent
 * access patterns across all generators.
 */
class ICodegenComponent {
public:
    explicit ICodegenComponent(CodegenContext& ctx) : _ctx(ctx) {}
    virtual ~ICodegenComponent() = default;

protected:
    CodegenContext& ctx() { return _ctx; }

    // Convenience accessors
    llvm::IRBuilder<>& builder() { return _ctx.builder(); }
    llvm::LLVMContext& llvm_ctx() { return _ctx.llvm_context(); }
    TypeMapper& types() { return _ctx.types(); }

    // Common helpers available to all components
    llvm::AllocaInst* create_alloca(llvm::Type* type, const std::string& name);
    llvm::Value* create_load(llvm::Value* ptr, llvm::Type* type, const std::string& name = "");
    void create_store(llvm::Value* value, llvm::Value* ptr);

private:
    CodegenContext& _ctx;
};

} // namespace Cryo::Codegen
```

### 4.3 OperatorCodegen - Example Specialized Component

```cpp
namespace Cryo::Codegen {

/**
 * @brief Handles all binary and unary operator code generation
 *
 * Replaces the 2300+ line generate_binary_operation method
 * with a clean, dispatch-based architecture.
 */
class OperatorCodegen : public ICodegenComponent {
public:
    explicit OperatorCodegen(CodegenContext& ctx);

    // Main entry points
    llvm::Value* generate_binary(BinaryExpressionNode* node);
    llvm::Value* generate_unary(UnaryExpressionNode* node);

private:
    // Assignment operations (extracted from binary)
    llvm::Value* generate_assignment(BinaryExpressionNode* node);
    llvm::Value* generate_identifier_assignment(IdentifierNode* target, ExpressionNode* value);
    llvm::Value* generate_member_assignment(MemberAccessNode* target, ExpressionNode* value);
    llvm::Value* generate_array_assignment(ArrayAccessNode* target, ExpressionNode* value);
    llvm::Value* generate_deref_assignment(UnaryExpressionNode* target, ExpressionNode* value);

    // Arithmetic operations
    llvm::Value* generate_arithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, Type* result_type);
    llvm::Value* generate_integer_arithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, bool is_signed);
    llvm::Value* generate_float_arithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs);

    // Comparison operations
    llvm::Value* generate_comparison(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, Type* operand_type);
    llvm::Value* generate_integer_comparison(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, bool is_signed);
    llvm::Value* generate_float_comparison(TokenKind op, llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* generate_pointer_comparison(TokenKind op, llvm::Value* lhs, llvm::Value* rhs);

    // Logical operations
    llvm::Value* generate_logical_and(BinaryExpressionNode* node); // Short-circuit
    llvm::Value* generate_logical_or(BinaryExpressionNode* node);  // Short-circuit

    // Bitwise operations
    llvm::Value* generate_bitwise(TokenKind op, llvm::Value* lhs, llvm::Value* rhs);

    // String operations (consolidates 3 separate methods)
    llvm::Value* generate_string_concat(llvm::Value* lhs, llvm::Value* rhs);

    // Unary operations
    llvm::Value* generate_negation(llvm::Value* operand, Type* type);
    llvm::Value* generate_logical_not(llvm::Value* operand);
    llvm::Value* generate_bitwise_not(llvm::Value* operand);
    llvm::Value* generate_address_of(ExpressionNode* operand);
    llvm::Value* generate_dereference(llvm::Value* ptr, Type* pointee_type);
    llvm::Value* generate_increment(ExpressionNode* operand, bool prefix);
    llvm::Value* generate_decrement(ExpressionNode* operand, bool prefix);

    // Dispatch table for binary operators (replaces if-else chain)
    using BinaryOpHandler = llvm::Value* (OperatorCodegen::*)(llvm::Value*, llvm::Value*, Type*);
    static const std::unordered_map<TokenKind, BinaryOpHandler> _binary_dispatch;
};

} // namespace Cryo::Codegen
```

### 4.4 CallCodegen - Function Call Specialization

```cpp
namespace Cryo::Codegen {

/**
 * @brief Handles all function/method/constructor call generation
 *
 * Replaces the 3100+ line generate_function_call method
 * with specialized handlers for each call type.
 */
class CallCodegen : public ICodegenComponent {
public:
    explicit CallCodegen(CodegenContext& ctx);

    // Main entry point
    llvm::Value* generate(CallExpressionNode* node);

private:
    // Call type determination
    enum class CallKind {
        PrimitiveConstructor,  // i32(value), f64(value)
        StructConstructor,     // MyStruct(args...)
        ClassConstructor,      // new MyClass(args...)
        EnumVariant,           // Option::Some(value)
        Intrinsic,             // __malloc__(size)
        RuntimeFunction,       // cryo_alloc(...)
        StaticMethod,          // Type::method(args)
        InstanceMethod,        // obj.method(args)
        FreeFunction,          // function(args)
        GenericInstantiation   // Array<int>::new()
    };

    CallKind classify_call(CallExpressionNode* node);

    // Specialized generators
    llvm::Value* generate_primitive_constructor(CallExpressionNode* node, const std::string& type);
    llvm::Value* generate_struct_constructor(CallExpressionNode* node, const std::string& type);
    llvm::Value* generate_class_constructor(CallExpressionNode* node, const std::string& type);
    llvm::Value* generate_enum_variant(CallExpressionNode* node, const std::string& enum_name, const std::string& variant);
    llvm::Value* generate_intrinsic(CallExpressionNode* node, const std::string& intrinsic);
    llvm::Value* generate_runtime_call(CallExpressionNode* node, const std::string& function);
    llvm::Value* generate_static_method(CallExpressionNode* node, const std::string& type, const std::string& method);
    llvm::Value* generate_instance_method(CallExpressionNode* node, llvm::Value* receiver, const std::string& method);
    llvm::Value* generate_free_function(CallExpressionNode* node, llvm::Function* function);

    // Argument processing
    std::vector<llvm::Value*> generate_arguments(const std::vector<std::unique_ptr<ExpressionNode>>& args);
    llvm::Value* prepare_argument(llvm::Value* arg, llvm::Type* expected_type);

    // Function resolution
    llvm::Function* resolve_function(const std::string& name);
    llvm::Function* resolve_method(const std::string& type, const std::string& method);

    // Helpers
    bool is_primitive_constructor(const std::string& name) const;
    bool is_runtime_function(const std::string& name) const;
};

} // namespace Cryo::Codegen
```

### 4.5 MemoryCodegen - Centralized Memory Operations

```cpp
namespace Cryo::Codegen {

/**
 * @brief Centralizes all memory-related IR generation
 *
 * Consolidates the 28+ alloca checks and scattered
 * load/store operations into a single, consistent API.
 */
class MemoryCodegen : public ICodegenComponent {
public:
    explicit MemoryCodegen(CodegenContext& ctx);

    // Allocation
    llvm::AllocaInst* create_entry_alloca(llvm::Function* fn, llvm::Type* type, const std::string& name);
    llvm::AllocaInst* create_stack_alloca(llvm::Type* type, const std::string& name);
    llvm::Value* create_heap_alloc(llvm::Type* type, const std::string& name);

    // Load operations with type safety
    llvm::Value* create_load(llvm::Value* ptr, const std::string& name = "");
    llvm::Value* create_load(llvm::Value* ptr, llvm::Type* expected_type, const std::string& name = "");
    llvm::Value* create_volatile_load(llvm::Value* ptr, llvm::Type* type, const std::string& name = "");

    // Store operations
    void create_store(llvm::Value* value, llvm::Value* ptr);
    void create_volatile_store(llvm::Value* value, llvm::Value* ptr);

    // GEP operations
    llvm::Value* create_struct_gep(llvm::Type* struct_type, llvm::Value* ptr, unsigned idx, const std::string& name = "");
    llvm::Value* create_array_gep(llvm::Type* element_type, llvm::Value* ptr, llvm::Value* idx, const std::string& name = "");
    llvm::Value* create_inbounds_gep(llvm::Type* type, llvm::Value* ptr, llvm::ArrayRef<llvm::Value*> indices, const std::string& name = "");

    // Memcpy/memset wrappers
    void create_memcpy(llvm::Value* dest, llvm::Value* src, uint64_t size);
    void create_memset(llvm::Value* dest, uint8_t value, uint64_t size);

    // Type queries
    bool is_alloca(llvm::Value* value) const;
    llvm::Type* get_allocated_type(llvm::Value* alloca) const;

private:
    // Cache of intrinsic declarations
    llvm::Function* _memcpy_fn = nullptr;
    llvm::Function* _memset_fn = nullptr;

    llvm::Function* get_memcpy_intrinsic();
    llvm::Function* get_memset_intrinsic();
};

} // namespace Cryo::Codegen
```

### 4.6 ScopeManager - RAII Scope Handling

```cpp
namespace Cryo::Codegen {

/**
 * @brief Manages scope entry/exit with automatic cleanup
 *
 * Replaces manual scope stack management with RAII pattern.
 */
class ScopeManager : public ICodegenComponent {
public:
    explicit ScopeManager(CodegenContext& ctx);

    // RAII scope guard
    class ScopeGuard {
    public:
        ScopeGuard(ScopeManager& mgr, llvm::BasicBlock* entry = nullptr);
        ~ScopeGuard(); // Automatically calls exit_scope and runs destructors

        // Disable copy
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        ScopeManager& _mgr;
    };

    // Manual scope management (for complex cases)
    void enter_scope(llvm::BasicBlock* entry = nullptr);
    void exit_scope();

    // Variable tracking within scope
    void register_local(const std::string& name, llvm::Value* value, llvm::AllocaInst* alloca = nullptr);
    llvm::Value* lookup_local(const std::string& name);
    llvm::AllocaInst* lookup_alloca(const std::string& name);

    // Destructor management
    void register_destructor(const std::string& var_name, llvm::Value* value,
                             const std::string& type_name, bool is_heap);
    void run_scope_destructors();

    // Breakable context (loops, switch)
    void push_breakable(BreakableContext ctx);
    void pop_breakable();
    BreakableContext* current_breakable();

private:
    std::vector<ScopeContext> _scope_stack;
    std::stack<BreakableContext> _breakable_stack;
};

// Usage example:
// {
//     ScopeManager::ScopeGuard guard(scope_mgr, entry_block);
//     // ... generate code ...
// } // Destructor automatically handles cleanup

} // namespace Cryo::Codegen
```

---

## 5. Refactoring Phases

### Phase 1: Infrastructure (Foundation)
**Goal:** Create shared infrastructure without modifying existing code

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `CodegenContext` class | CodegenContext.hpp/cpp | ~300 |
| Create `ICodegenComponent` base | ICodegenComponent.hpp | ~100 |
| Create `CodegenHelpers` utilities | CodegenHelpers.hpp/cpp | ~200 |
| Create `ScopeManager` with RAII | Memory/ScopeManager.hpp/cpp | ~400 |

**Validation:** All existing tests pass (no behavior change)

### Phase 2: Memory Operations
**Goal:** Extract and consolidate memory operations

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `MemoryCodegen` class | Memory/MemoryCodegen.hpp/cpp | ~500 |
| Replace `create_load/store` calls in CodegenVisitor | CodegenVisitor.cpp | (reduction) |
| Replace `create_entry_block_alloca` calls | CodegenVisitor.cpp | (reduction) |

**Validation:** Memory operations work identically

### Phase 3: Operator Extraction
**Goal:** Extract the massive `generate_binary_operation` method

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `OperatorCodegen` class | Expressions/OperatorCodegen.hpp/cpp | ~1200 |
| Extract assignment handling | OperatorCodegen.cpp | ~500 |
| Extract arithmetic operations | OperatorCodegen.cpp | ~300 |
| Extract comparison operations | OperatorCodegen.cpp | ~200 |
| Extract logical/bitwise ops | OperatorCodegen.cpp | ~200 |
| Wire up from CodegenVisitor | CodegenVisitor.cpp | (reduction) |

**Validation:** All operator tests pass

### Phase 4: Call Expression Extraction
**Goal:** Extract the massive `generate_function_call` method

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `CallCodegen` class | Expressions/CallCodegen.hpp/cpp | ~1500 |
| Extract constructor call handling | CallCodegen.cpp | ~400 |
| Extract method call handling | CallCodegen.cpp | ~400 |
| Extract intrinsic dispatch | CallCodegen.cpp | ~200 |
| Wire up from CodegenVisitor | CodegenVisitor.cpp | (reduction) |

**Validation:** All function call tests pass

### Phase 5: Control Flow Extraction
**Goal:** Extract control flow generation

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `ControlFlowCodegen` class | Statements/ControlFlowCodegen.hpp/cpp | ~800 |
| Extract if/else generation | ControlFlowCodegen.cpp | ~150 |
| Extract loop generation | ControlFlowCodegen.cpp | ~300 |
| Extract match/switch generation | ControlFlowCodegen.cpp | ~350 |

**Validation:** All control flow tests pass

### Phase 6: Declaration Extraction
**Goal:** Extract declaration handling

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `DeclarationCodegen` class | Declarations/DeclarationCodegen.hpp/cpp | ~800 |
| Create `TypeCodegen` class | Declarations/TypeCodegen.hpp/cpp | ~1000 |
| Create `GenericCodegen` class | Declarations/GenericCodegen.hpp/cpp | ~600 |

**Validation:** All declaration tests pass

### Phase 7: Expression Extraction
**Goal:** Extract remaining expression handling

| Task | Files | Est. Lines |
|------|-------|------------|
| Create `ExpressionCodegen` class | Expressions/ExpressionCodegen.hpp/cpp | ~800 |
| Create `CastCodegen` class | Expressions/CastCodegen.hpp/cpp | ~400 |
| Extract literal generation | ExpressionCodegen.cpp | ~300 |
| Extract member access | ExpressionCodegen.cpp | ~500 |

**Validation:** All expression tests pass

### Phase 8: Final Cleanup
**Goal:** Reduce CodegenVisitor to dispatcher only

| Task | Files | Est. Lines |
|------|-------|------------|
| Convert visitor methods to dispatch calls | CodegenVisitor.cpp | ~500 final |
| Remove dead code | Various | (deletion) |
| Update documentation | Headers | ~200 |

---

## 6. API Usage Examples

### 6.1 Before (Current Code)

```cpp
// In CodegenVisitor::visit(BinaryExpressionNode&)
void CodegenVisitor::visit(Cryo::BinaryExpressionNode &node)
{
    NodeTracker tracker(this, &node);

    try {
        llvm::Value *result = generate_binary_operation(&node);
        if (result) {
            set_current_value(result);
            register_value(&node, result);
        }
    } catch (...) {
        // error handling
    }
}

// generate_binary_operation is 2300+ lines of if-else chains
```

### 6.2 After (Refactored)

```cpp
// In CodegenVisitor::visit(BinaryExpressionNode&)
void CodegenVisitor::visit(Cryo::BinaryExpressionNode &node)
{
    NodeTracker tracker(this, &node);

    llvm::Value* result = _operators->generate_binary(&node);
    if (result) {
        _ctx->set_result(result);
        _ctx->register_value(&node, result);
    }
}

// OperatorCodegen::generate_binary is clean, dispatch-based
llvm::Value* OperatorCodegen::generate_binary(BinaryExpressionNode* node)
{
    TokenKind op = node->operator_token().kind();

    // Assignment is special - doesn't evaluate LHS as value
    if (op == TokenKind::TK_EQUAL) {
        return generate_assignment(node);
    }

    // Generate operands
    llvm::Value* lhs = generate_operand(node->left());
    llvm::Value* rhs = generate_operand(node->right());

    if (!lhs || !rhs) return nullptr;

    // Dispatch to appropriate handler
    Type* result_type = node->resolved_type();

    switch (classify_operation(op)) {
        case OpClass::Arithmetic:
            return generate_arithmetic(op, lhs, rhs, result_type);
        case OpClass::Comparison:
            return generate_comparison(op, lhs, rhs, result_type);
        case OpClass::Logical:
            return generate_logical(op, node); // Short-circuit needs AST
        case OpClass::Bitwise:
            return generate_bitwise(op, lhs, rhs);
        case OpClass::StringConcat:
            return generate_string_concat(lhs, rhs);
    }

    ctx().report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                       "Unsupported binary operator");
    return nullptr;
}
```

### 6.3 Scope Management Before/After

```cpp
// BEFORE: Manual scope management
void CodegenVisitor::generate_function_body(...) {
    enter_scope(entry_block);
    // ... 2000 lines ...
    // Easy to forget exit_scope on error paths
    exit_scope();
}

// AFTER: RAII scope management
void FunctionCodegen::generate_body(...) {
    ScopeManager::ScopeGuard guard(_scope, entry_block);
    // ... cleaner code ...
} // Automatic cleanup, even on exceptions
```

---

## 7. Testing Strategy

### 7.1 Test Categories

1. **Unit Tests** - Each new component in isolation
2. **Integration Tests** - Components working together
3. **Regression Tests** - Existing test suite must pass
4. **Golden Tests** - Compare IR output before/after

### 7.2 Incremental Validation

After each phase:
1. Run full test suite
2. Compare IR output for sample programs
3. Verify no performance regression in generated code

---

## 8. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Breaking changes during refactor | Feature branches per phase, comprehensive tests |
| Performance regression | Profile compilation speed before/after |
| Hidden dependencies in monolith | Extract one method at a time, test immediately |
| Scope creep | Strict adherence to phase boundaries |
| Knowledge loss | Document extracted patterns as we go |

---

## 9. Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| CodegenVisitor.cpp lines | 19,820 | ~500 |
| Largest single method | ~3,100 | <300 |
| Files in Codegen/ | 8 | ~20 |
| Average file size | 3,300 | <800 |
| Code duplication | High | Minimal |
| Test coverage | ? | >80% |

---

## 10. Recommended Execution Order

1. **Start with Phase 1** (Infrastructure) - Zero risk, creates foundation
2. **Then Phase 2** (Memory) - Small, well-defined scope
3. **Then Phase 3** (Operators) - Biggest single-method extraction
4. **Then Phase 4** (Calls) - Second biggest extraction
5. **Remaining phases** can be parallelized after core is stable

---

## 11. Notes for Implementation

1. **Preserve the `Intrinsics` pattern** - It's already well-designed
2. **Keep `TypeMapper` mostly unchanged** - It works well
3. **The dispatch pattern** in `Intrinsics.cpp` should be replicated in `OperatorCodegen` and `CallCodegen`
4. **Use the existing `NodeTracker`** RAII pattern as a model for `ScopeGuard`
5. **Maintain backward compatibility** - The public interface of `CodegenVisitor` should not change

---

*This plan provides a production-grade path forward for the CryoLang compiler's codegen phase.*