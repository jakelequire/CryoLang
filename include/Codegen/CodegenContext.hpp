#pragma once

#include "Codegen/LLVMContext.hpp"
#include "Codegen/TypeMapper.hpp"
#include "Codegen/ValueContext.hpp"
#include "Codegen/Intrinsics.hpp"
#include "Codegen/FunctionRegistry.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/TemplateRegistry.hpp"
#include "GDM/DiagnosticBuilders.hpp"
#include "Utils/SymbolResolutionManager.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <stack>

namespace Cryo::Codegen
{
    // Forward declarations
    class CodegenVisitor;

    /**
     * @brief Information about a variable that needs a destructor call
     */
    struct DestructorInfo
    {
        std::string variable_name;
        llvm::Value *variable_value;
        std::string type_name;
        bool is_heap_allocated;

        DestructorInfo(const std::string &name, llvm::Value *value,
                       const std::string &type, bool is_heap)
            : variable_name(name), variable_value(value),
              type_name(type), is_heap_allocated(is_heap) {}
    };

    /**
     * @brief Scope context for nested declarations/statements
     */
    struct ScopeContext
    {
        llvm::BasicBlock *entry_block;
        llvm::BasicBlock *exit_block;
        std::unordered_map<std::string, llvm::Value *> local_values;
        std::unordered_map<std::string, llvm::AllocaInst *> local_allocas;
        std::vector<DestructorInfo> destructors;

        ScopeContext(llvm::BasicBlock *entry, llvm::BasicBlock *exit = nullptr)
            : entry_block(entry), exit_block(exit) {}
    };

    /**
     * @brief Function generation context
     */
    struct FunctionContext
    {
        llvm::Function *function;
        Cryo::FunctionDeclarationNode *ast_node;
        llvm::BasicBlock *entry_block;
        llvm::BasicBlock *return_block;
        llvm::AllocaInst *return_value_alloca;
        std::vector<ScopeContext> scope_stack;

        FunctionContext(llvm::Function *fn, Cryo::FunctionDeclarationNode *node)
            : function(fn), ast_node(node), entry_block(nullptr),
              return_block(nullptr), return_value_alloca(nullptr) {}
    };

    /**
     * @brief Breakable context for break/continue (loops and switch statements)
     */
    struct BreakableContext
    {
        enum Type
        {
            Loop,
            Switch
        };

        Type context_type;
        llvm::BasicBlock *condition_block;
        llvm::BasicBlock *body_block;
        llvm::BasicBlock *continue_block;
        llvm::BasicBlock *break_block;

        // Constructor for loops
        BreakableContext(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                         llvm::BasicBlock *cont, llvm::BasicBlock *brk)
            : context_type(Loop), condition_block(cond), body_block(body),
              continue_block(cont), break_block(brk) {}

        // Constructor for switch statements
        explicit BreakableContext(llvm::BasicBlock *brk)
            : context_type(Switch), condition_block(nullptr), body_block(nullptr),
              continue_block(nullptr), break_block(brk) {}
    };

    /**
     * @brief Unified context for all codegen components
     *
     * This class provides centralized access to all codegen infrastructure,
     * eliminating the need for each component to hold references to multiple
     * managers. All specialized codegen components receive a single context.
     *
     * This is the cornerstone of the refactored codegen architecture.
     */
    class CodegenContext
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        CodegenContext(
            LLVMContextManager &llvm_ctx,
            Cryo::SymbolTable &symbols,
            Cryo::DiagnosticManager *diagnostics);

        /**
         * @brief Set the visitor for AST traversal dispatch
         * @param visitor The CodegenVisitor instance
         */
        void set_visitor(CodegenVisitor *visitor) { _visitor = visitor; }

        /**
         * @brief Get the visitor for AST traversal
         * @return CodegenVisitor pointer (may be null during construction)
         */
        CodegenVisitor *visitor() { return _visitor; }

        ~CodegenContext() = default;

        // Non-copyable, non-movable
        CodegenContext(const CodegenContext &) = delete;
        CodegenContext &operator=(const CodegenContext &) = delete;
        CodegenContext(CodegenContext &&) = delete;
        CodegenContext &operator=(CodegenContext &&) = delete;

        //===================================================================
        // LLVM Access
        //===================================================================

        /** @brief Get LLVM context */
        llvm::LLVMContext &llvm_context() { return _llvm.get_context(); }

        /** @brief Get IR builder */
        llvm::IRBuilder<> &builder() { return _llvm.get_builder(); }

        /** @brief Get current module */
        llvm::Module *module() { return _llvm.get_module(); }

        /** @brief Get main module */
        llvm::Module *main_module() { return _llvm.get_main_module(); }

        /** @brief Get the LLVM context manager */
        LLVMContextManager &llvm_manager() { return _llvm; }

        //===================================================================
        // Component Access
        //===================================================================

        /** @brief Get type mapper */
        TypeMapper &types() { return *_type_mapper; }

        /** @brief Get value context */
        ValueContext &values() { return *_value_context; }

        /** @brief Get function registry */
        FunctionRegistry &functions() { return *_function_registry; }

        /** @brief Get intrinsics handler */
        Intrinsics &intrinsics() { return *_intrinsics; }

        /** @brief Get symbol table */
        Cryo::SymbolTable &symbols() { return _symbols; }

        /** @brief Set template registry for cross-module type resolution */
        void set_template_registry(Cryo::TemplateRegistry *registry) { _template_registry = registry; }

        /** @brief Get template registry (may be null) */
        Cryo::TemplateRegistry *template_registry() { return _template_registry; }

        //===================================================================
        // Symbol Resolution
        //===================================================================

        /** @brief Get symbol resolution manager */
        Cryo::SRM::SymbolResolutionManager &srm() { return *_srm_manager; }

        /** @brief Get symbol resolution context */
        Cryo::SRM::SymbolResolutionContext &srm_context() { return *_srm_context; }

        //===================================================================
        // Error Reporting
        //===================================================================

        /** @brief Get diagnostic manager (may be null) */
        Cryo::DiagnosticManager *diagnostics() { return _diagnostics; }

        /** @brief Get diagnostic builder (may be null) */
        CodegenDiagnosticBuilder *diagnostic_builder() { return _diagnostic_builder.get(); }

        /** @brief Report an error with node context */
        void report_error(ErrorCode code, Cryo::ASTNode *node, const std::string &msg);

        /** @brief Report an error without node context */
        void report_error(ErrorCode code, const std::string &msg);

        /** @brief Check if errors have occurred */
        bool has_errors() const { return _has_errors; }

        /** @brief Clear error state */
        void clear_errors();

        //===================================================================
        // Current State Management
        //===================================================================

        /** @brief Get current function context */
        FunctionContext *current_function() { return _current_function.get(); }

        /** @brief Set current function context */
        void set_current_function(std::unique_ptr<FunctionContext> fn);

        /** @brief Clear current function context */
        void clear_current_function();

        /** @brief Release ownership of current function context (for save/restore) */
        std::unique_ptr<FunctionContext> release_current_function() { return std::move(_current_function); }

        /** @brief Get current AST node being processed */
        Cryo::ASTNode *current_node() { return _current_node; }

        /** @brief Set current AST node (for error reporting) */
        void set_current_node(Cryo::ASTNode *node) { _current_node = node; }

        //===================================================================
        // Value Registration (centralizes register_value calls)
        //===================================================================

        /** @brief Register a generated LLVM value for an AST node */
        void register_value(Cryo::ASTNode *node, llvm::Value *value);

        /** @brief Get previously registered value for an AST node */
        llvm::Value *get_value(Cryo::ASTNode *node);

        /** @brief Check if a value is registered for an AST node */
        bool has_value(Cryo::ASTNode *node);

        //===================================================================
        // Type/Function Registration
        //===================================================================

        /** @brief Register a named type */
        void register_type(const std::string &name, llvm::Type *type);

        /** @brief Get a registered type by name */
        llvm::Type *get_type(const std::string &name);

        /** @brief Register a named function */
        void register_function(const std::string &name, llvm::Function *fn);

        /** @brief Get a registered function by name */
        llvm::Function *get_function(const std::string &name);

        //===================================================================
        // Current Expression Result
        //===================================================================

        /** @brief Set the result of the current expression */
        void set_result(llvm::Value *value) { _current_result = value; }

        /** @brief Get the result of the current expression */
        llvm::Value *get_result() { return _current_result; }

        //===================================================================
        // Cached Mappings Access
        //===================================================================

        /** @brief Get functions map */
        std::unordered_map<std::string, llvm::Function *> &functions_map() { return _functions; }

        /** @brief Get types map */
        std::unordered_map<std::string, llvm::Type *> &types_map() { return _types; }

        /** @brief Get globals map */
        std::unordered_map<std::string, llvm::GlobalVariable *> &globals_map() { return _globals; }

        /** @brief Get global types map */
        std::unordered_map<std::string, llvm::Type *> &global_types_map() { return _global_types; }

        /** @brief Get enum variants map */
        std::unordered_map<std::string, llvm::Value *> &enum_variants_map() { return _enum_variants; }

        /** @brief Get variable types map (Cryo types) */
        std::unordered_map<std::string, Cryo::Type *> &variable_types_map() { return _variable_types; }

        //===================================================================
        // Source Context
        //===================================================================

        /** @brief Set source file info */
        void set_source_info(const std::string &source_file, const std::string &namespace_ctx = "");

        /** @brief Get source file */
        const std::string &source_file() const { return _source_file; }

        /** @brief Get namespace context */
        const std::string &namespace_context() const { return _namespace_context; }

        /** @brief Get current type being processed (for methods) */
        const std::string &current_type_name() const { return _current_type_name; }

        /** @brief Set current type being processed */
        void set_current_type_name(const std::string &name) { _current_type_name = name; }

        /** @brief Register an enum variant constant */
        void register_enum_variant(const std::string &name, llvm::Value *value) { _enum_variants[name] = value; }

        /** @brief Register enum variant field types (for pattern matching) */
        void register_enum_variant_fields(const std::string &variant_name, const std::vector<std::string> &field_types)
        {
            _enum_variant_fields[variant_name] = field_types;
        }

        /** @brief Get enum variant field types */
        const std::vector<std::string> *get_enum_variant_fields(const std::string &variant_name) const
        {
            auto it = _enum_variant_fields.find(variant_name);
            return it != _enum_variant_fields.end() ? &it->second : nullptr;
        }

        //===================================================================
        // Breakable Context Stack (loops, switch)
        //===================================================================

        /** @brief Push a breakable context */
        void push_breakable(BreakableContext ctx);

        /** @brief Pop breakable context */
        void pop_breakable();

        /** @brief Get current breakable context (may be null) */
        BreakableContext *current_breakable();

        /** @brief Check if we're inside a breakable context */
        bool in_breakable_context() const { return !_breakable_stack.empty(); }

    private:
        //===================================================================
        // Core References
        //===================================================================

        LLVMContextManager &_llvm;
        Cryo::SymbolTable &_symbols;
        Cryo::DiagnosticManager *_diagnostics;
        CodegenVisitor *_visitor = nullptr;
        Cryo::TemplateRegistry *_template_registry = nullptr;

        //===================================================================
        // Owned Components
        //===================================================================

        std::unique_ptr<TypeMapper> _type_mapper;
        std::unique_ptr<ValueContext> _value_context;
        std::unique_ptr<FunctionRegistry> _function_registry;
        std::unique_ptr<Intrinsics> _intrinsics;
        std::unique_ptr<CodegenDiagnosticBuilder> _diagnostic_builder;

        // Symbol Resolution
        std::unique_ptr<Cryo::SRM::SymbolResolutionContext> _srm_context;
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _srm_manager;

        //===================================================================
        // State
        //===================================================================

        std::unique_ptr<FunctionContext> _current_function;
        std::stack<BreakableContext> _breakable_stack;
        Cryo::ASTNode *_current_node;
        llvm::Value *_current_result;
        bool _has_errors;
        std::string _last_error;

        // Source context
        std::string _source_file;
        std::string _namespace_context;
        std::string _current_type_name;

        //===================================================================
        // Cached Mappings
        //===================================================================

        std::unordered_map<Cryo::ASTNode *, llvm::Value *> _node_values;
        std::unordered_map<std::string, llvm::Function *> _functions;
        std::unordered_map<std::string, llvm::Type *> _types;
        std::unordered_map<std::string, llvm::GlobalVariable *> _globals;
        std::unordered_map<std::string, llvm::Type *> _global_types;
        std::unordered_map<std::string, llvm::Value *> _enum_variants;
        std::unordered_map<std::string, std::vector<std::string>> _enum_variant_fields;
        std::unordered_map<std::string, Cryo::Type *> _variable_types;

        // Struct field index mapping: type_name -> (field_name -> field_index)
        std::unordered_map<std::string, std::unordered_map<std::string, unsigned>> _struct_field_indices;

        // Type namespace mapping: type_name -> namespace
        // Used for cross-module method resolution
        std::unordered_map<std::string, std::string> _type_namespace_map;

        // Method signature storage for cross-module extern declarations
        // Key: fully qualified method name (e.g., "std::core::option::Option::is_some")
        // Value: return type as Cryo::Type*
        std::unordered_map<std::string, Cryo::Type *> _method_return_types;

    public:
        //===================================================================
        // Struct Field Index API
        //===================================================================

        /** @brief Register a struct's field indices */
        void register_struct_fields(const std::string &type_name,
                                    const std::vector<std::string> &field_names)
        {
            auto &field_map = _struct_field_indices[type_name];
            for (unsigned i = 0; i < field_names.size(); ++i)
            {
                field_map[field_names[i]] = i;
            }
        }

        /** @brief Get field index for a struct field, returns -1 if not found */
        int get_struct_field_index(const std::string &type_name, const std::string &field_name) const
        {
            auto type_it = _struct_field_indices.find(type_name);
            if (type_it == _struct_field_indices.end())
                return -1;

            auto field_it = type_it->second.find(field_name);
            if (field_it == type_it->second.end())
                return -1;

            return static_cast<int>(field_it->second);
        }

        /** @brief Check if struct has a field */
        bool has_struct_field(const std::string &type_name, const std::string &field_name) const
        {
            return get_struct_field_index(type_name, field_name) >= 0;
        }

        //===================================================================
        // Type Namespace API
        //===================================================================

        /** @brief Register a type's source namespace for cross-module method resolution */
        void register_type_namespace(const std::string &type_name, const std::string &namespace_path)
        {
            _type_namespace_map[type_name] = namespace_path;
        }

        /** @brief Get the namespace for a type, returns empty string if not found */
        std::string get_type_namespace(const std::string &type_name) const
        {
            auto it = _type_namespace_map.find(type_name);
            return it != _type_namespace_map.end() ? it->second : "";
        }

        /** @brief Get the type namespace map for inspection/debugging */
        const std::unordered_map<std::string, std::string> &type_namespace_map() const
        {
            return _type_namespace_map;
        }

        //===================================================================
        // Method Signature Registry API
        //===================================================================

        /**
         * @brief Register a method's return type for cross-module extern declarations
         * @param qualified_method_name Fully qualified method name (e.g., "std::core::option::Option::is_some")
         * @param return_type The method's return type
         */
        void register_method_return_type(const std::string &qualified_method_name, Cryo::Type *return_type)
        {
            _method_return_types[qualified_method_name] = return_type;
        }

        /**
         * @brief Get a method's return type for extern declaration creation
         * @param qualified_method_name Fully qualified method name
         * @return The return type, or nullptr if not registered
         */
        Cryo::Type *get_method_return_type(const std::string &qualified_method_name) const
        {
            auto it = _method_return_types.find(qualified_method_name);
            return it != _method_return_types.end() ? it->second : nullptr;
        }

        /**
         * @brief Check if a method's return type is registered
         * @param qualified_method_name Fully qualified method name
         * @return true if registered
         */
        bool has_method_return_type(const std::string &qualified_method_name) const
        {
            return _method_return_types.find(qualified_method_name) != _method_return_types.end();
        }
    };

    //=======================================================================
    // RAII Helper for AST Node Tracking
    //=======================================================================

    /**
     * @brief RAII helper for automatic AST node tracking
     *
     * Automatically sets the current node on construction and
     * restores the previous node on destruction.
     */
    class NodeTracker
    {
    public:
        NodeTracker(CodegenContext &ctx, Cryo::ASTNode *node)
            : _ctx(ctx), _previous_node(ctx.current_node())
        {
            _ctx.set_current_node(node);
        }

        ~NodeTracker()
        {
            _ctx.set_current_node(_previous_node);
        }

        // Non-copyable
        NodeTracker(const NodeTracker &) = delete;
        NodeTracker &operator=(const NodeTracker &) = delete;

    private:
        CodegenContext &_ctx;
        Cryo::ASTNode *_previous_node;
    };

} // namespace Cryo::Codegen
