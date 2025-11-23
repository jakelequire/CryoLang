# Symbol Resolution and Naming Convention System

## Overview

The Cryo compiler currently suffers from inconsistent and fragmented naming conventions across different phases of compilation. This document outlines a comprehensive standardized naming system called the **Symbol Resolution Manager (SRM)** that will provide unified symbol name generation, resolution, and management throughout the compiler pipeline.

## Current Problems

### Identified Issues

1. **Duplicated Name Construction Logic**: The same naming patterns are constructed manually throughout the codebase:
   ```cpp
   // Pattern repeated everywhere:
   if (!target_namespace.empty()) {
       constructor_name = target_namespace + "::" + full_type_name + "::" + base_type_name;
   } else {
       constructor_name = full_type_name + "::" + base_type_name;
   }
   ```

2. **Inconsistent Qualified Name Formats**: Different parts of the compiler use different qualification schemes:
   - `namespace::type::method`
   - `type::method`
   - `namespace::type::method(param_signature)`
   - `type::method(param_signature)`

3. **Ad-hoc Function Lookup**: Function resolution uses multiple fallback strategies without a unified approach:
   - Simple name lookup
   - Signature-based lookup
   - Namespace-qualified lookup
   - Alternative signature attempts

4. **Type Name Inconsistencies**: Type names are handled differently across phases:
   - Sometimes use `Type::to_string()`
   - Sometimes manually construct type names
   - Inconsistent handling of generic type parameters

### Impact

These issues cause:
- Code duplication and maintenance burden
- Potential naming conflicts and resolution failures
- Difficulty in adding new features that require symbol resolution
- Inconsistent behavior between different language constructs

## Proposed Solution: Symbol Resolution Manager (SRM)

### Architecture

The SRM will be a centralized utility module that provides standardized naming and resolution services to all compiler phases. It will be implemented as a header-only library for efficiency and will integrate with the existing TypeContext and SymbolTable systems.

### Core Components

#### 1. Symbol Identifier Classes

```cpp
namespace Cryo::SRM {
    
    // Base class for all symbol identifiers
    class SymbolIdentifier {
    public:
        virtual ~SymbolIdentifier() = default;
        virtual std::string to_string() const = 0;
        virtual std::string to_mangled_string() const = 0;
        virtual std::string to_debug_string() const = 0;
        virtual SymbolKind get_symbol_kind() const = 0;
    };
    
    // Qualified symbol name with namespace context
    class QualifiedIdentifier : public SymbolIdentifier {
    private:
        std::vector<std::string> namespace_parts_;
        std::string symbol_name_;
        SymbolKind symbol_kind_;
        
    public:
        QualifiedIdentifier(const std::vector<std::string>& namespaces, 
                          const std::string& name, 
                          SymbolKind kind);
        
        std::string to_string() const override;
        std::string to_mangled_string() const override;
        std::string get_namespace_path() const;
        std::string get_symbol_name() const;
    };
    
    // Function signature identifier with parameter types
    class FunctionIdentifier : public QualifiedIdentifier {
    private:
        std::vector<Type*> parameter_types_;
        Type* return_type_;
        bool is_constructor_;
        bool is_static_;
        
    public:
        FunctionIdentifier(const std::vector<std::string>& namespaces,
                         const std::string& name,
                         const std::vector<Type*>& params,
                         Type* return_type = nullptr,
                         bool is_constructor = false);
        
        std::string to_signature_string() const;
        std::string to_llvm_name() const;
        std::string to_overload_key() const;
    };
    
    // Type identifier for structs, classes, enums
    class TypeIdentifier : public QualifiedIdentifier {
    private:
        std::vector<Type*> template_parameters_;
        
    public:
        TypeIdentifier(const std::vector<std::string>& namespaces,
                     const std::string& name,
                     const std::vector<Type*>& template_params = {});
        
        std::string to_canonical_name() const;
        std::string to_constructor_name() const;
        std::string to_destructor_name() const;
    };
}
```

#### 2. Symbol Resolution Context

```cpp
namespace Cryo::SRM {
    
    class SymbolResolutionContext {
    private:
        std::vector<std::string> current_namespace_stack_;
        std::unordered_map<std::string, std::string> namespace_aliases_;
        std::unordered_set<std::string> imported_namespaces_;
        SymbolTable* symbol_table_;
        TypeContext* type_context_;
        
    public:
        SymbolResolutionContext(SymbolTable* symbol_table, TypeContext* type_context);
        
        // Namespace management
        void push_namespace(const std::string& namespace_name);
        void pop_namespace();
        std::vector<std::string> get_current_namespace_stack() const;
        
        // Alias management
        void register_namespace_alias(const std::string& alias, const std::string& full_namespace);
        std::string resolve_namespace_alias(const std::string& alias) const;
        
        // Import management
        void add_imported_namespace(const std::string& namespace_name);
        bool is_namespace_imported(const std::string& namespace_name) const;
        
        // Symbol creation helpers
        std::unique_ptr<FunctionIdentifier> create_function_identifier(
            const std::string& name,
            const std::vector<Type*>& params,
            Type* return_type = nullptr,
            bool is_constructor = false) const;
            
        std::unique_ptr<TypeIdentifier> create_type_identifier(
            const std::string& name,
            const std::vector<Type*>& template_params = {}) const;
    };
}
```

#### 3. Symbol Resolution Manager

```cpp
namespace Cryo::SRM {
    
    class SymbolResolutionManager {
    private:
        SymbolResolutionContext* context_;
        
        // Resolution strategies
        std::vector<std::function<Symbol*(const SymbolIdentifier&)>> resolution_strategies_;
        
    public:
        SymbolResolutionManager(SymbolResolutionContext* context);
        
        // Primary resolution methods
        Symbol* resolve_symbol(const SymbolIdentifier& identifier) const;
        Symbol* resolve_function(const FunctionIdentifier& identifier) const;
        Symbol* resolve_type(const TypeIdentifier& identifier) const;
        
        // Function overload resolution
        Symbol* resolve_function_overload(
            const std::string& base_name,
            const std::vector<Type*>& argument_types) const;
            
        // Constructor resolution
        Symbol* resolve_constructor(
            const TypeIdentifier& type_id,
            const std::vector<Type*>& argument_types) const;
            
        // Batch resolution for imported symbols
        std::vector<Symbol*> resolve_all_in_namespace(const std::string& namespace_name) const;
        
        // Registration methods for codegen
        void register_generated_function(const FunctionIdentifier& identifier, llvm::Function* function);
        void register_generated_type(const TypeIdentifier& identifier, llvm::Type* type);
        
        // Query methods
        bool symbol_exists(const SymbolIdentifier& identifier) const;
        std::vector<Symbol*> find_similar_symbols(const std::string& partial_name) const;
        
    private:
        // Resolution strategy implementations
        Symbol* try_exact_match(const SymbolIdentifier& identifier) const;
        Symbol* try_namespace_qualified(const SymbolIdentifier& identifier) const;
        Symbol* try_imported_namespaces(const SymbolIdentifier& identifier) const;
        Symbol* try_parent_scopes(const SymbolIdentifier& identifier) const;
    };
}
```

### Integration Points

#### 1. AST Node Integration

Each AST node type that deals with symbols will have standardized methods:

```cpp
// In ASTNode base class or specific node types
namespace Cryo {
    class FunctionDeclarationNode {
    private:
        mutable std::unique_ptr<SRM::FunctionIdentifier> cached_identifier_;
        
    public:
        SRM::FunctionIdentifier get_symbol_identifier(SRM::SymbolResolutionContext* context) const;
        std::string get_canonical_name(SRM::SymbolResolutionContext* context) const;
        std::string get_llvm_name(SRM::SymbolResolutionContext* context) const;
    };
    
    class StructDeclarationNode {
    private:
        mutable std::unique_ptr<SRM::TypeIdentifier> cached_identifier_;
        
    public:
        SRM::TypeIdentifier get_symbol_identifier(SRM::SymbolResolutionContext* context) const;
        SRM::FunctionIdentifier get_constructor_identifier(
            SRM::SymbolResolutionContext* context,
            const std::vector<Type*>& param_types) const;
    };
}
```

#### 2. CodegenVisitor Integration

The CodegenVisitor will use SRM for all naming operations:

```cpp
class CodegenVisitor {
private:
    std::unique_ptr<SRM::SymbolResolutionContext> srm_context_;
    std::unique_ptr<SRM::SymbolResolutionManager> srm_manager_;
    
public:
    // Replace manual name construction with SRM calls
    llvm::Function* create_function_declaration(FunctionDeclarationNode* node) {
        auto func_id = node->get_symbol_identifier(srm_context_.get());
        std::string llvm_name = func_id->to_llvm_name();
        
        // Create LLVM function
        auto* function = llvm::Function::Create(/*...*/);
        
        // Register with SRM for future lookups
        srm_manager_->register_generated_function(*func_id, function);
        
        return function;
    }
    
    llvm::Function* resolve_function_call(CallExpressionNode* node) {
        // Use SRM to resolve function instead of manual lookup
        auto func_id = create_function_identifier_from_call(node);
        auto* symbol = srm_manager_->resolve_function(*func_id);
        
        if (!symbol) {
            // Try overload resolution
            symbol = srm_manager_->resolve_function_overload(
                func_id->get_symbol_name(),
                get_argument_types(node));
        }
        
        return symbol ? get_llvm_function(symbol) : nullptr;
    }
};
```

#### 3. SymbolTable Integration

The existing SymbolTable will be extended to work with SRM:

```cpp
class SymbolTable {
private:
    std::unique_ptr<SRM::SymbolResolutionContext> srm_context_;
    
public:
    // Enhanced symbol lookup using SRM
    Symbol* lookup_symbol_srm(const SRM::SymbolIdentifier& identifier) const;
    
    // Batch operations
    void register_namespace_symbols(const std::string& namespace_name, 
                                  const std::vector<Symbol>& symbols);
    
    // SRM accessor
    SRM::SymbolResolutionContext* get_srm_context() { return srm_context_.get(); }
};
```

### Naming Convention Standards

#### 1. Namespace Qualification

- **Format**: `namespace1::namespace2::symbol_name`
- **Constructor**: `namespace1::Type::Type`
- **Method**: `namespace1::Type::method_name`
- **Static Method**: `namespace1::Type::static_method_name`

#### 2. Function Signatures

- **Simple Function**: `function_name`
- **With Namespace**: `namespace::function_name`
- **With Signature**: `namespace::function_name(type1,type2)`
- **Constructor**: `namespace::Type::Type(param_types)`

#### 3. Type Names

- **Simple Type**: `TypeName`
- **Namespaced**: `namespace::TypeName`
- **Generic**: `TypeName<T1,T2>`
- **Specialized**: `TypeName<int,string>`

#### 4. LLVM IR Names

- **Functions**: Use full qualified name with signature for uniqueness
- **Types**: Use canonical name with namespace qualification
- **Variables**: Use scoped names to avoid conflicts

### Migration Strategy

#### Phase 1: Core Infrastructure (Week 1-2)
1. Implement SRM header files in `include/Utils/SymbolResolutionManager.hpp`
2. Create basic SymbolIdentifier classes
3. Implement SymbolResolutionContext
4. Add unit tests for core functionality

#### Phase 2: Integration Layer (Week 3-4)
1. Integrate SRM with existing SymbolTable
2. Add SRM methods to key AST nodes
3. Update TypeContext to work with SRM
4. Test integration with existing symbol resolution

#### Phase 3: CodegenVisitor Migration (Week 5-6)
1. Replace manual name construction in CodegenVisitor
2. Update function registration and lookup
3. Migrate constructor name generation
4. Update all `_functions` map usage

#### Phase 4: Other Phases (Week 7-8)
1. Update TypeChecker to use SRM
2. Migrate Parser symbol handling
3. Update LSP analyzer
4. Comprehensive integration testing

#### Phase 5: Cleanup and Optimization (Week 9-10)
1. Remove old naming code
2. Optimize performance-critical paths
3. Add comprehensive documentation
4. Final validation testing

### Implementation Files

The SRM will be implemented in the following files:

- `include/Utils/SymbolResolutionManager.hpp` - Main header with all classes
- `src/Utils/SymbolResolutionManager.cpp` - Implementation (if needed for complex methods)
- `tests/unit/SymbolResolutionManager_test.cpp` - Unit tests
- `docs/symbol-resolution-examples.md` - Usage examples and best practices

### Benefits

1. **Consistency**: All symbol names generated using the same rules
2. **Maintainability**: Single point of change for naming conventions
3. **Extensibility**: Easy to add new symbol types or naming schemes
4. **Performance**: Caching and optimized resolution strategies
5. **Debugging**: Standardized debug output and symbol tracking
6. **IDE Support**: Better symbol resolution for LSP and tooling

### Performance Considerations

- **Caching**: Symbol identifiers cached at AST node level
- **Lazy Evaluation**: Names generated only when needed
- **String Optimization**: Use string views where possible
- **Hash-based Lookup**: Fast symbol resolution using optimized hash tables
- **Memory Management**: RAII and smart pointers throughout

### Backward Compatibility

During migration, the SRM will coexist with existing naming code:

- New code should use SRM from day one
- Existing code will be migrated incrementally
- Fallback mechanisms ensure continued functionality
- Comprehensive testing validates equivalence

This standardized approach will significantly improve the Cryo compiler's maintainability, consistency, and extensibility while providing a solid foundation for future language features.