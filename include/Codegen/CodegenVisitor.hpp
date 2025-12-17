#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "Codegen/LLVMContext.hpp"
#include "Codegen/ValueContext.hpp"
#include "Codegen/TypeMapper.hpp"
#include "Codegen/Intrinsics.hpp"
#include "Codegen/FunctionRegistry.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "GDM/DiagnosticBuilders.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/ASTNodeSRMExtensions.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <memory>
#include <set>
#include <stack>
#include <unordered_map>

namespace Cryo
{
    class TypeChecker; // Forward declaration
}

namespace Cryo::Codegen
{
    /**
     * @brief AST Visitor for LLVM IR Code Generation
     *
     * Implements the visitor pattern to traverse CryoLang AST and generate
     * corresponding LLVM IR. Handles all language constructs including:
     * - Functions and methods
     * - Classes and structs
     * - Enums and pattern matching
     * - Generics instantiation
     * - Memory management
     * - Control flow
     * - Expression evaluation
     *
     * The visitor maintains contextual state for nested scopes, function
     * calls, and type instantiations.
     */
    class CodegenVisitor : public Cryo::ASTVisitor
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct codegen visitor
         * @param context_manager LLVM context manager
         * @param symbol_table Symbol table from frontend
         * @param gdm Optional diagnostic manager for reporting errors
         */
        CodegenVisitor(
            LLVMContextManager &context_manager,
            Cryo::SymbolTable &symbol_table,
            Cryo::DiagnosticManager *gdm = nullptr);

        // Prevent copying and moving to avoid unique_ptr issues
        CodegenVisitor(const CodegenVisitor &) = delete;
        CodegenVisitor &operator=(const CodegenVisitor &) = delete;
        CodegenVisitor(CodegenVisitor &&) = delete;
        CodegenVisitor &operator=(CodegenVisitor &&) = delete;

        ~CodegenVisitor() = default;

        //===================================================================
        // Main Generation Interface
        //===================================================================

        /**
         * @brief Generate IR for entire program
         * @param program Root program node
         * @return true if generation successful
         */
        bool generate_program(Cryo::ProgramNode *program);

        /**
         * @brief Get generated LLVM value for given AST node
         * @param node AST node to lookup
         * @return LLVM value or nullptr if not found
         */
        llvm::Value *get_generated_value(Cryo::ASTNode *node);

        /**
         * @brief Set source file information for module naming
         * @param source_file Full path to the source file
         * @param namespace_context Current namespace context
         */
        void set_source_info(const std::string &source_file, const std::string &namespace_context = "");

        /**
         * @brief Set stdlib compilation mode (generates full implementations for imports)
         */
        void set_stdlib_compilation_mode(bool enable) { _stdlib_compilation_mode = enable; }

        /**
         * @brief Set imported ASTs for dynamic enum variant extraction
         * @param imported_asts Pointer to map of imported AST nodes
         */
        void set_imported_asts(const std::unordered_map<std::string, std::unique_ptr<Cryo::ProgramNode>> *imported_asts);

        //===================================================================
        // AST Visitor Implementation - Declarations
        //===================================================================

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationNode &node) override;
        void visit(Cryo::FunctionDeclarationNode &node) override;
        void visit(Cryo::IntrinsicDeclarationNode &node) override;
        void visit(Cryo::IntrinsicConstDeclarationNode &node) override;
        void visit(Cryo::ImportDeclarationNode &node) override;
        void visit(Cryo::VariableDeclarationNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::EnumDeclarationNode &node) override;
        void visit(Cryo::EnumVariantNode &node) override;
        void visit(Cryo::TypeAliasDeclarationNode &node) override;
        void visit(Cryo::TraitDeclarationNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;
        void visit(Cryo::ExternBlockNode &node) override;
        void visit(Cryo::GenericParameterNode &node) override;
        void visit(Cryo::StructFieldNode &node) override;
        void visit(Cryo::StructMethodNode &node) override;

        // DirectiveNode - No codegen needed, directives are compile-time only
        void visit(Cryo::DirectiveNode &node) override;

        //===================================================================
        // AST Visitor Implementation - Statements
        //===================================================================

        void visit(Cryo::StatementNode &node) override;
        void visit(Cryo::BlockStatementNode &node) override;
        void visit(Cryo::ReturnStatementNode &node) override;
        void visit(Cryo::IfStatementNode &node) override;
        void visit(Cryo::WhileStatementNode &node) override;
        void visit(Cryo::ForStatementNode &node) override;
        void visit(Cryo::MatchStatementNode &node) override;
        void visit(Cryo::SwitchStatementNode &node) override;
        void visit(Cryo::CaseStatementNode &node) override;
        void visit(Cryo::MatchArmNode &node) override;
        void visit(Cryo::PatternNode &node) override;
        void visit(Cryo::EnumPatternNode &node) override;
        void visit(Cryo::ExpressionStatementNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::BreakStatementNode &node) override;
        void visit(Cryo::ContinueStatementNode &node) override;

        //===================================================================
        // AST Visitor Implementation - Expressions
        //===================================================================

        void visit(Cryo::ExpressionNode &node) override;
        void visit(Cryo::LiteralNode &node) override;
        void visit(Cryo::IdentifierNode &node) override;
        void visit(Cryo::BinaryExpressionNode &node) override;
        void visit(Cryo::UnaryExpressionNode &node) override;
        void visit(Cryo::TernaryExpressionNode &node) override;
        void visit(Cryo::CallExpressionNode &node) override;
        void visit(Cryo::NewExpressionNode &node) override;
        void visit(Cryo::SizeofExpressionNode &node) override;
        void visit(Cryo::StructLiteralNode &node) override;
        void visit(Cryo::ArrayLiteralNode &node) override;
        void visit(Cryo::ArrayAccessNode &node) override;
        void visit(Cryo::MemberAccessNode &node) override;
        void visit(Cryo::ScopeResolutionNode &node) override;

        //===================================================================
        // Error Handling
        //===================================================================

        /**
         * @brief Check if visitor encountered errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string &get_last_error() const { return _last_error; }

        /**
         * @brief Get all error messages
         */
        const std::vector<std::string> &get_errors() const { return _errors; }

        /**
         * @brief Clear error state
         */
        void clear_errors();

        /**
         * @brief Pre-register all functions from symbol table to prevent forward declaration conflicts
         */
        void pre_register_functions_from_symbol_table();



        /**
         * @brief Import specialized methods from TypeChecker after monomorphization
         * @param type_checker Reference to TypeChecker containing specialized methods
         */
        void import_specialized_methods(const Cryo::TypeChecker &type_checker);

        /**
         * @brief Import namespace aliases from TypeChecker after type checking
         * @param type_checker Reference to TypeChecker containing namespace aliases
         */
        void import_namespace_aliases(const Cryo::TypeChecker &type_checker);

        /**
         * @brief Get TypeMapper for manual AST node registration
         */
        TypeMapper *get_type_mapper() const { return _type_mapper.get(); }

        /**
         * @brief Get current AST node being processed (for error reporting)
         */
        Cryo::ASTNode *get_current_node() const { return _current_node; }

        /**
         * @brief Get Symbol Resolution Manager for identifier generation
         */
        Cryo::SRM::SymbolResolutionManager *get_srm_manager() const { return _srm_manager.get(); }

        /**
         * @brief Get Symbol Resolution Context for namespace management
         */
        Cryo::SRM::SymbolResolutionContext *get_srm_context() const { return _srm_context.get(); }

    private:


    private:
        //===================================================================
        // Context Management
        //===================================================================

        /**
         * @brief Information about a variable that needs a destructor call
         */
        struct DestructorInfo
        {
            std::string variable_name;
            llvm::Value *variable_value; // The variable's value (alloca or pointer)
            std::string type_name;       // Type name for destructor lookup
            bool is_heap_allocated;      // Whether this is a heap-allocated object (pointer)

            DestructorInfo(const std::string &name, llvm::Value *value, const std::string &type, bool is_heap)
                : variable_name(name), variable_value(value), type_name(type), is_heap_allocated(is_heap) {}
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
            std::vector<DestructorInfo> destructors; // Variables that need destructor calls

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
                : function(fn), ast_node(node), return_value_alloca(nullptr) {}
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
            llvm::BasicBlock *condition_block; // For loops only
            llvm::BasicBlock *body_block;      // For loops only
            llvm::BasicBlock *continue_block;  // For loops only
            llvm::BasicBlock *break_block;     // For both loops and switches

            // Constructor for loops
            BreakableContext(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                             llvm::BasicBlock *cont, llvm::BasicBlock *brk)
                : context_type(Loop), condition_block(cond), body_block(body),
                  continue_block(cont), break_block(brk) {}

            // Constructor for switch statements
            BreakableContext(llvm::BasicBlock *brk)
                : context_type(Switch), condition_block(nullptr), body_block(nullptr),
                  continue_block(nullptr), break_block(brk) {}
        };

        //===================================================================
        // Core Components
        //===================================================================

        LLVMContextManager &_context_manager;
        Cryo::SymbolTable &_symbol_table;
        Cryo::DiagnosticManager *_diagnostic_manager;
        std::unique_ptr<CodegenDiagnosticBuilder> _diagnostic_builder;

        std::unique_ptr<ValueContext> _value_context;
        std::unique_ptr<TypeMapper> _type_mapper;
        std::unique_ptr<Intrinsics> _intrinsics;
        std::unique_ptr<FunctionRegistry> _function_registry;

        // Symbol Resolution Manager for standardized naming
        std::unique_ptr<Cryo::SRM::SymbolResolutionContext> _srm_context;
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _srm_manager;

        //===================================================================
        // Generation State
        //===================================================================

        // Current generation context
        std::unique_ptr<FunctionContext> _current_function;
        std::stack<BreakableContext> _breakable_stack;

        // Generated values mapping
        std::unordered_map<Cryo::ASTNode *, llvm::Value *> _node_values;
        std::unordered_map<std::string, llvm::Function *> _functions;
        std::unordered_map<std::string, llvm::Type *> _types;
        std::unordered_map<std::string, llvm::GlobalVariable *> _globals;
        std::unordered_map<std::string, llvm::Type *> _global_types;   // Track global variable element types
        std::unordered_map<std::string, llvm::Value *> _enum_variants; // Track enum variants for scope resolution
        std::unordered_map<std::string, Cryo::Type *> _variable_types; // Track variable name -> resolved type object

        // Imported ASTs for cross-module enum value extraction
        const std::unordered_map<std::string, std::unique_ptr<Cryo::ProgramNode>> *_imported_asts;

        // Current value being generated (for expressions)
        llvm::Value *_current_value;

        // Current AST node being processed (for error reporting)
        Cryo::ASTNode *_current_node;

        // Control flags
        bool _stdlib_compilation_mode; // Generate full implementations for imports in stdlib mode

        // Primitive type context for method generation
        Cryo::Type *current_primitive_type = nullptr;                   // Track current primitive type being implemented
        std::string current_struct_type;                                // Track current struct type being implemented
        std::set<Cryo::StructMethodNode *> processed_primitive_methods; // Track already processed primitive methods

        // Loop context for break/continue
        llvm::BasicBlock *_current_loop_exit = nullptr;
        llvm::BasicBlock *_current_loop_continue = nullptr;

        //===================================================================
        // Error State
        //===================================================================

        bool _has_errors;
        std::string _last_error;
        std::vector<std::string> _errors;

        //===================================================================
        // Source File Context
        //===================================================================

        std::string _source_file;
        std::string _namespace_context;
        
        // Flag to defer function body generation until all globals are processed
        bool _defer_function_bodies;
        
        // Vector to store function nodes for deferred body generation
        std::vector<std::pair<Cryo::FunctionDeclarationNode*, llvm::Function*>> _deferred_function_bodies;

        //===================================================================
        // Private Generation Methods
        //===================================================================

        // Function generation
        llvm::Function *generate_function_declaration(Cryo::FunctionDeclarationNode *node);
        llvm::Function *generate_method_declaration(Cryo::StructMethodNode *method, llvm::Type *struct_type);
        bool generate_function_body(Cryo::FunctionDeclarationNode *node, llvm::Function *function);
        void generate_primitive_method(Cryo::StructMethodNode *node, Cryo::Type *primitive_type);
        void generate_enum_method(Cryo::StructMethodNode &node, const std::string &enum_type_name);

        // Generic type generation
        llvm::Function *generate_generic_constructor(const std::string &instantiated_type,
                                                     const std::string &base_type,
                                                     const std::vector<std::string> &type_args,
                                                     llvm::Type *struct_type);
        void generate_generic_methods(const std::string &instantiated_type,
                                      const std::string &base_type,
                                      const std::vector<std::string> &type_args,
                                      llvm::Type *struct_type);
        void generate_generic_struct_methods(const std::string &instantiated_type,
                                             const std::vector<std::string> &type_args,
                                             llvm::Type *struct_type,
                                             const std::unordered_map<std::string, std::string> &type_substitutions);

        /**
         * @brief Generate specialized method on-demand for core library types
         * @param method_name Fully qualified method name (e.g., "std::net::HTTP::Array<Header>::push")
         * @param type_name The specialized type name (e.g., "Array<Header>")
         * @param method_base_name The method name without type (e.g., "push")
         * @return true if method was successfully generated, false otherwise
         */
        bool generate_specialized_method_on_demand(const std::string &method_name,
                                                   const std::string &type_name,
                                                   const std::string &method_base_name);

        void generate_get_value_method(const std::string &instantiated_type,
                                       const std::vector<std::string> &type_args,
                                       llvm::Type *struct_type,
                                       const std::unordered_map<std::string, std::string> &type_substitutions);
        void generate_get_value_method_body(llvm::Function *method,
                                            llvm::Type *struct_type,
                                            const std::unordered_map<std::string, std::string> &type_substitutions);

        // Function interception for runtime function qualification
        llvm::Function *get_qualified_function(const std::string &function_name);
        bool is_runtime_function(const std::string &function_name) const;

        // Type generation
        llvm::Type *generate_struct_type(Cryo::StructDeclarationNode *node);
        llvm::Type *generate_class_type(Cryo::ClassDeclarationNode *node);
        llvm::Type *generate_enum_type(Cryo::EnumDeclarationNode *node);

        // Array method generation helper
        void generate_array_methods(const std::string &class_name, llvm::Type *class_type);

        // Type name conversion helper
        std::string generate_monomorphized_type_name(const std::string &parameterized_type);

        // Enum generation helpers
        void generate_simple_enum_constants(Cryo::EnumDeclarationNode *enum_decl, llvm::Type *enum_type);
        void generate_complex_enum_constructors(Cryo::EnumDeclarationNode *enum_decl, llvm::Type *enum_type);
        void generate_simple_variant_in_complex_enum(Cryo::EnumDeclarationNode *enum_decl,
                                                     Cryo::EnumVariantNode *variant,
                                                     int discriminant);
        void generate_complex_variant_constructor(Cryo::EnumDeclarationNode *enum_decl,
                                                  Cryo::EnumVariantNode *variant,
                                                  int discriminant);
        void register_enum_variant(const std::string &enum_name, const std::string &variant_name, llvm::Value *value);
        void ensure_enum_variants_available(const std::string &enum_name);

        // Global constant on-demand processing
        void ensure_global_constant_available(const std::string &constant_name);

        // Cross-module enum loading
        void load_enum_variants_from_namespace(const std::string &namespace_name);

        // Cross-module global constant loading
        void load_global_constants_from_namespace(const std::string &namespace_name);

        // Cross-module constructor declaration
        void declare_imported_constructors(const Cryo::ImportDeclarationNode &import_node);

        // Parameterized enum helpers
        void ensure_parameterized_enum_constructors(const std::string &instantiated_name,
                                                    const std::string &base_name,
                                                    const std::vector<std::string> &type_args);
        void generate_parameterized_enum_variant_constructor(const std::string &instantiated_name,
                                                             const std::string &variant_name,
                                                             const std::vector<std::string> &type_args,
                                                             llvm::Type *enum_type);

        // Enum variant analysis helpers
        struct VariantInfo
        {
            bool has_associated_data;
            std::vector<std::string> associated_types;
            bool is_success_variant; // true for Ok/Some, false for Err/None
        };
        VariantInfo analyze_enum_variant(const std::string &base_enum_name,
                                         const std::string &variant_name,
                                         const std::vector<std::string> &type_args);
        llvm::Type *resolve_type_argument_to_llvm_type(const std::string &type_arg);

        // Expression generation helpers
        llvm::Value *generate_binary_operation(Cryo::BinaryExpressionNode *node);
        llvm::Value *generate_unary_operation(Cryo::UnaryExpressionNode *node);
        llvm::Value *generate_function_call(Cryo::CallExpressionNode *node);
        llvm::Value *generate_array_access(Cryo::ArrayAccessNode *node);
        llvm::Value *generate_member_access(Cryo::MemberAccessNode *node);
        llvm::Value *generate_member_field_address(Cryo::MemberAccessNode *node);
        llvm::Value *generate_nested_member_field_ptr(llvm::Value *nested_field_ptr, const std::string &member_name, Cryo::MemberAccessNode *node);

        // Constructor helpers
        bool is_primitive_integer_constructor(const std::string &function_name) const;
        bool is_primitive_float_constructor(const std::string &function_name) const;
        bool is_primitive_constructor(const std::string &function_name) const;
        llvm::Value *generate_primitive_constructor_call(CallExpressionNode *node, const std::string &target_type);
        llvm::Value *generate_stack_constructor_call(CallExpressionNode *node, const std::string &type_name, Type *struct_type);

        // Array helpers
        void handle_array_struct_assignment(llvm::Value *array_ptr, llvm::AllocaInst *alloca, size_t array_size);
        llvm::Value *generate_integer_cast(llvm::Value *source_value, llvm::Type *source_type,
                                           llvm::Type *target_type, const std::string &target_type_name);
        llvm::Value *generate_float_cast(llvm::Value *source_value, llvm::Type *source_type,
                                         llvm::Type *target_type, const std::string &target_type_name);

        // String concatenation helpers
        llvm::Value *generate_string_concatenation(llvm::Value *left_str, llvm::Value *right_str);
        llvm::Value *generate_string_char_concatenation(llvm::Value *str_val, llvm::Value *char_val);
        llvm::Value *generate_char_string_concatenation(llvm::Value *char_val, llvm::Value *str_val);

        // Function call helpers
        std::string extract_function_name_from_member_access(Cryo::MemberAccessNode *node);
        std::string determine_member_type(const std::string &base_name, const std::string &member_name);
        llvm::Function *create_runtime_function_declaration(const std::string &c_name, Cryo::CallExpressionNode *call_node);

        // Intrinsic call generation
        llvm::Value *generate_intrinsic_call(Cryo::CallExpressionNode *node, const std::string &intrinsic_name);
        llvm::Value *generate_member_intrinsic_call(Cryo::CallExpressionNode *node, const std::string &intrinsic_name, llvm::Value *object_value);

        // Control flow generation
        void generate_if_statement(Cryo::IfStatementNode *node);
        void generate_while_loop(Cryo::WhileStatementNode *node);
        void generate_for_loop(Cryo::ForStatementNode *node);
        void generate_match_statement(Cryo::MatchStatementNode *node);
        void generate_switch_statement(Cryo::SwitchStatementNode *node);
        void generate_string_switch(Cryo::SwitchStatementNode *node, llvm::Value *switch_value, llvm::BasicBlock *end_block);
        void generate_integer_switch(Cryo::SwitchStatementNode *node, llvm::Value *switch_value, llvm::BasicBlock *end_block);
        void generate_case_statement(Cryo::CaseStatementNode *node, llvm::SwitchInst *switch_inst, llvm::BasicBlock *end_block);

        // Match statement helpers
        llvm::Value *extract_enum_discriminant(llvm::Value *enum_value);
        int get_pattern_discriminant(Cryo::PatternNode *pattern);
        void generate_match_arm(Cryo::MatchArmNode *arm, llvm::Value *match_value);
        void extract_pattern_bindings(Cryo::EnumPatternNode *pattern, llvm::Value *enum_value);

        // Memory management
        llvm::AllocaInst *create_entry_block_alloca(llvm::Function *function,
                                                    llvm::Type *type,
                                                    const std::string &name);
        llvm::Value *create_load(llvm::Value *ptr, llvm::Type *element_type = nullptr, const std::string &name = "");
        void create_store(llvm::Value *value, llvm::Value *ptr);

        // Scope management
        void enter_scope(llvm::BasicBlock *entry_block = nullptr, llvm::BasicBlock *exit_block = nullptr);
        void exit_scope();
        ScopeContext &current_scope();

        // Value management
        void set_current_value(llvm::Value *value) { _current_value = value; }
        llvm::Value *get_current_value() const { return _current_value; }
        void register_value(Cryo::ASTNode *node, llvm::Value *value);

        // AST node tracking for error reporting
        void set_current_node(Cryo::ASTNode *node) { _current_node = node; }

        // RAII helper for automatic AST node tracking
        class NodeTracker
        {
        private:
            CodegenVisitor *visitor;
            Cryo::ASTNode *previous_node;

        public:
            NodeTracker(CodegenVisitor *v, Cryo::ASTNode *node) : visitor(v), previous_node(v->get_current_node())
            {
                visitor->set_current_node(node);
            }
            ~NodeTracker()
            {
                visitor->set_current_node(previous_node);
            }
        };

        friend class NodeTracker;

        // Utility methods
        llvm::BasicBlock *create_basic_block(const std::string &name, llvm::Function *function = nullptr);
        llvm::Type *get_llvm_type(Cryo::Type *cryo_type);
        llvm::Value *cast_value(llvm::Value *value, llvm::Type *target_type);
        bool is_lvalue(Cryo::ExpressionNode *expr);
        bool is_primitive_type(const std::string &type_name);
        bool is_runtime_internal_function(const std::string &function_name);
        void ensure_valid_insertion_point();

        // SRM Helper methods for standardized naming
        std::string generate_function_name(const std::string &function_name, const std::vector<Cryo::Type *> &parameter_types = {});
        std::string generate_method_name(const std::string &type_name, const std::string &method_name, const std::vector<Cryo::Type *> &parameter_types = {});
        std::string generate_constructor_name(const std::string &type_name, const std::vector<Cryo::Type *> &parameter_types);
        std::string generate_qualified_name(const std::string &base_name, Cryo::SymbolKind symbol_kind = Cryo::SymbolKind::Function);
        std::vector<std::string> get_current_namespace_parts() const;

        // Destructor management
        bool has_destructor(const std::string &type_name);
        void register_variable_for_destruction(const std::string &variable_name, const std::string &type_name, llvm::Value *value, bool is_heap_allocated = false);

        // Intrinsic constant context tracking
        std::string get_current_filename();
        uint32_t get_current_line_number();
        std::string get_current_function_name();
        llvm::Value *generate_intrinsic_constant(const std::string &const_name);

        // Error reporting
        void report_error(const std::string &message);
        void report_error(const std::string &message, Cryo::ASTNode *node);
    };

} // namespace Cryo::Codegen
