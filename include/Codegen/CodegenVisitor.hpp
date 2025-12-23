#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "Codegen/LLVMContext.hpp"
#include "Codegen/CodegenContext.hpp"
#include "GDM/DiagnosticBuilders.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Cryo
{
    class TypeChecker;
}

namespace Cryo::Codegen
{
    // Forward declarations for components
    class MemoryCodegen;
    class ScopeManager;
    class OperatorCodegen;
    class CallCodegen;
    class ControlFlowCodegen;
    class StatementCodegen;
    class DeclarationCodegen;
    class TypeCodegen;
    class GenericCodegen;
    class ExpressionCodegen;
    class CastCodegen;

    /**
     * @brief AST Visitor for LLVM IR Code Generation
     *
     * Thin dispatcher that delegates to specialized codegen components.
     * Each component handles a specific aspect of code generation:
     * - MemoryCodegen: alloca, load, store operations
     * - OperatorCodegen: binary and unary operators
     * - CallCodegen: function/method/constructor calls
     * - ControlFlowCodegen: if, while, for, match, switch
     * - StatementCodegen: blocks, returns, variable declarations
     * - DeclarationCodegen: functions, methods
     * - TypeCodegen: structs, classes, enums
     * - GenericCodegen: generic instantiation
     * - ExpressionCodegen: literals, identifiers, member access
     * - CastCodegen: type casting operations
     */
    class CodegenVisitor : public Cryo::ASTVisitor
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        CodegenVisitor(
            LLVMContextManager &context_manager,
            Cryo::SymbolTable &symbol_table,
            Cryo::DiagnosticManager *gdm = nullptr);

        ~CodegenVisitor();

        // Non-copyable, non-movable
        CodegenVisitor(const CodegenVisitor &) = delete;
        CodegenVisitor &operator=(const CodegenVisitor &) = delete;
        CodegenVisitor(CodegenVisitor &&) = delete;
        CodegenVisitor &operator=(CodegenVisitor &&) = delete;

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
         */
        void set_source_info(const std::string &source_file, const std::string &namespace_context = "");

        /**
         * @brief Set stdlib compilation mode
         */
        void set_stdlib_compilation_mode(bool enable);

        /**
         * @brief Set imported ASTs for dynamic enum variant extraction
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
        void visit(Cryo::DirectiveNode &node) override;

        //===================================================================
        // AST Visitor Implementation - Statements
        //===================================================================

        void visit(Cryo::StatementNode &node) override;
        void visit(Cryo::BlockStatementNode &node) override;
        void visit(Cryo::UnsafeBlockStatementNode &node) override;
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
        void visit(Cryo::CastExpressionNode &node) override;
        void visit(Cryo::StructLiteralNode &node) override;
        void visit(Cryo::ArrayLiteralNode &node) override;
        void visit(Cryo::ArrayAccessNode &node) override;
        void visit(Cryo::MemberAccessNode &node) override;
        void visit(Cryo::ScopeResolutionNode &node) override;

        //===================================================================
        // Error Handling
        //===================================================================

        bool has_errors() const { return _has_errors; }
        const std::string &get_last_error() const { return _last_error; }
        const std::vector<std::string> &get_errors() const { return _errors; }
        void clear_errors();

        //===================================================================
        // Public Accessors (for compatibility)
        //===================================================================

        void pre_register_functions_from_symbol_table();
        void import_specialized_methods(const Cryo::TypeChecker &type_checker);
        void import_namespace_aliases(const Cryo::TypeChecker &type_checker);
        void process_global_variables_recursively(ASTNode *node);
        void generate_global_constructors();
        void set_pre_registration_mode(bool enabled);

        TypeMapper *get_type_mapper() const;
        Cryo::ASTNode *get_current_node() const;
        Cryo::SRM::SymbolResolutionManager *get_srm_manager() const;
        Cryo::SRM::SymbolResolutionContext *get_srm_context() const;

        /**
         * @brief Get the codegen context (for component access)
         */
        CodegenContext &context() { return *_ctx; }

    private:
        //===================================================================
        // Core Context
        //===================================================================

        std::unique_ptr<CodegenContext> _ctx;

        //===================================================================
        // Specialized Components
        //===================================================================

        std::unique_ptr<MemoryCodegen> _memory;
        std::unique_ptr<ScopeManager> _scope;
        std::unique_ptr<OperatorCodegen> _operators;
        std::unique_ptr<CallCodegen> _calls;
        std::unique_ptr<ControlFlowCodegen> _control_flow;
        std::unique_ptr<StatementCodegen> _statements;
        std::unique_ptr<DeclarationCodegen> _declarations;
        std::unique_ptr<TypeCodegen> _types;
        std::unique_ptr<GenericCodegen> _generics;
        std::unique_ptr<ExpressionCodegen> _expressions;
        std::unique_ptr<CastCodegen> _casts;

        //===================================================================
        // Error State
        //===================================================================

        bool _has_errors = false;
        std::string _last_error;
        std::vector<std::string> _errors;

        //===================================================================
        // Control Flags
        //===================================================================

        bool _stdlib_compilation_mode = false;
        bool _pre_registration_mode = false;

        //===================================================================
        // Helper Methods
        //===================================================================

        void initialize_components();
        void wire_component_dependencies();
        void report_error(const std::string &message);
        void report_error(const std::string &message, Cryo::ASTNode *node);
    };

} // namespace Cryo::Codegen
