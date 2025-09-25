#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "Codegen/LLVMContext.hpp"
#include "Codegen/ValueContext.hpp"
#include "Codegen/TypeMapper.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <memory>
#include <stack>
#include <unordered_map>

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
         */
        CodegenVisitor(
            LLVMContextManager &context_manager,
            Cryo::SymbolTable &symbol_table);

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

        //===================================================================
        // AST Visitor Implementation - Declarations
        //===================================================================

        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::DeclarationNode &node) override;
        void visit(Cryo::FunctionDeclarationNode &node) override;
        void visit(Cryo::VariableDeclarationNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::EnumDeclarationNode &node) override;
        void visit(Cryo::EnumVariantNode &node) override;
        void visit(Cryo::TypeAliasDeclarationNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;
        void visit(Cryo::ExternBlockNode &node) override;
        void visit(Cryo::GenericParameterNode &node) override;
        void visit(Cryo::StructFieldNode &node) override;
        void visit(Cryo::StructMethodNode &node) override;

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

    private:
        //===================================================================
        // Context Management
        //===================================================================

        /**
         * @brief Scope context for nested declarations/statements
         */
        struct ScopeContext
        {
            llvm::BasicBlock *entry_block;
            llvm::BasicBlock *exit_block;
            std::unordered_map<std::string, llvm::Value *> local_values;
            std::unordered_map<std::string, llvm::AllocaInst *> local_allocas;

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
         * @brief Loop context for break/continue
         */
        struct LoopContext
        {
            llvm::BasicBlock *condition_block;
            llvm::BasicBlock *body_block;
            llvm::BasicBlock *continue_block;
            llvm::BasicBlock *break_block;

            LoopContext(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                        llvm::BasicBlock *cont, llvm::BasicBlock *brk)
                : condition_block(cond), body_block(body),
                  continue_block(cont), break_block(brk) {}
        };

        //===================================================================
        // Core Components
        //===================================================================

        LLVMContextManager &_context_manager;
        Cryo::SymbolTable &_symbol_table;

        std::unique_ptr<ValueContext> _value_context;
        std::unique_ptr<TypeMapper> _type_mapper;

        //===================================================================
        // Generation State
        //===================================================================

        // Current generation context
        std::unique_ptr<FunctionContext> _current_function;
        std::stack<LoopContext> _loop_stack;

        // Generated values mapping
        std::unordered_map<Cryo::ASTNode *, llvm::Value *> _node_values;
        std::unordered_map<std::string, llvm::Function *> _functions;
        std::unordered_map<std::string, llvm::Type *> _types;
        std::unordered_map<std::string, llvm::GlobalVariable *> _globals;
        std::unordered_map<std::string, llvm::Type *> _global_types;   // Track global variable element types
        std::unordered_map<std::string, llvm::Value *> _enum_variants; // Track enum variants for scope resolution
        std::unordered_map<std::string, std::string> _variable_types;  // Track variable name -> type annotation string

        // Current value being generated (for expressions)
        llvm::Value *_current_value;

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
        // Private Generation Methods
        //===================================================================

        // Function generation
        llvm::Function *generate_function_declaration(Cryo::FunctionDeclarationNode *node);
        llvm::Function *generate_method_declaration(Cryo::StructMethodNode *method, llvm::Type *struct_type);
        bool generate_function_body(Cryo::FunctionDeclarationNode *node, llvm::Function *function);

        // Generic type generation
        llvm::Function *generate_generic_constructor(const std::string &instantiated_type,
                                                     const std::string &base_type,
                                                     const std::vector<std::string> &type_args,
                                                     llvm::Type *struct_type);
        void generate_generic_methods(const std::string &instantiated_type,
                                      const std::string &base_type,
                                      const std::vector<std::string> &type_args,
                                      llvm::Type *struct_type);

        // Type generation
        llvm::Type *generate_struct_type(Cryo::StructDeclarationNode *node);
        llvm::Type *generate_class_type(Cryo::ClassDeclarationNode *node);
        llvm::Type *generate_enum_type(Cryo::EnumDeclarationNode *node);

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

        // Expression generation helpers
        llvm::Value *generate_binary_operation(Cryo::BinaryExpressionNode *node);
        llvm::Value *generate_unary_operation(Cryo::UnaryExpressionNode *node);
        llvm::Value *generate_function_call(Cryo::CallExpressionNode *node);
        llvm::Value *generate_array_access(Cryo::ArrayAccessNode *node);
        llvm::Value *generate_member_access(Cryo::MemberAccessNode *node);

        // Function call helpers
        std::string extract_function_name_from_member_access(Cryo::MemberAccessNode *node);
        std::string map_cryo_to_c_function(const std::string &cryo_name);
        llvm::Function *create_runtime_function_declaration(const std::string &c_name, Cryo::CallExpressionNode *call_node);

        // Control flow generation
        void generate_if_statement(Cryo::IfStatementNode *node);
        void generate_while_loop(Cryo::WhileStatementNode *node);
        void generate_for_loop(Cryo::ForStatementNode *node);
        void generate_match_statement(Cryo::MatchStatementNode *node);

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

        // Utility methods
        llvm::BasicBlock *create_basic_block(const std::string &name, llvm::Function *function = nullptr);
        llvm::Type *get_llvm_type(Cryo::Type *cryo_type);
        llvm::Value *cast_value(llvm::Value *value, llvm::Type *target_type);
        bool is_lvalue(Cryo::ExpressionNode *expr);

        // Error reporting
        void report_error(const std::string &message);
        void report_error(const std::string &message, Cryo::ASTNode *node);
    };

} // namespace Cryo::Codegen