#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/BasicBlock.h>
#include <vector>

namespace Cryo::Codegen
{
    // Forward declarations
    class ControlFlowCodegen;
    class ExpressionCodegen;

    /**
     * @brief Handles statement code generation
     *
     * This class centralizes generation for:
     * - Block statements (compound statements)
     * - Expression statements
     * - Return statements
     * - Variable declaration statements
     * - Empty statements
     *
     * Works in conjunction with ControlFlowCodegen for
     * control flow statements (if, while, for, switch).
     */
    class StatementCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit StatementCodegen(CodegenContext &ctx);
        ~StatementCodegen() = default;

        /**
         * @brief Set the control flow codegen component
         */
        void set_control_flow(ControlFlowCodegen *control_flow) { _control_flow = control_flow; }

        /**
         * @brief Set the expression codegen component
         */
        void set_expression_codegen(ExpressionCodegen *expressions) { _expressions = expressions; }

        //===================================================================
        // Main Entry Point
        //===================================================================

        /**
         * @brief Generate code for any statement
         * @param stmt Statement node
         */
        void generate(Cryo::StatementNode *stmt);

        //===================================================================
        // Block Statements
        //===================================================================

        /**
         * @brief Generate code for a block statement (compound statement)
         * @param node Block statement node
         */
        void generate_block(Cryo::BlockStatementNode *node);

        /**
         * @brief Generate code for a list of statements
         * @param statements Vector of statement nodes
         */
        void generate_statement_list(const std::vector<std::unique_ptr<Cryo::StatementNode>> &statements);

        //===================================================================
        // Expression Statements
        //===================================================================

        /**
         * @brief Generate code for an expression statement
         * @param node Expression statement node
         *
         * An expression statement is an expression evaluated for its
         * side effects, with the result discarded.
         */
        void generate_expression_statement(Cryo::ExpressionStatementNode *node);

        //===================================================================
        // Return Statements
        //===================================================================

        /**
         * @brief Generate code for a return statement
         * @param node Return statement node
         */
        void generate_return(Cryo::ReturnStatementNode *node);

        /**
         * @brief Generate implicit return for void functions
         */
        void generate_implicit_return();

        //===================================================================
        // Variable Declarations
        //===================================================================

        /**
         * @brief Generate code for a variable declaration statement
         * @param node Variable declaration node
         */
        void generate_var_declaration(Cryo::VariableDeclarationNode *node);

        /**
         * @brief Generate code for a constant declaration
         * @param node Constant declaration node
         */
        // void generate_const_declaration(Cryo::ConstDeclarationNode *node);

        /**
         * @brief Generate code for multiple variable declarations
         * @param node Multiple declaration node
         */
        // void generate_multi_declaration(Cryo::MultiVarDeclNode *node);

        //===================================================================
        // Special Statements
        //===================================================================

        /**
         * @brief Generate code for an empty statement (no-op)
         */
        void generate_empty_statement();

        /**
         * @brief Generate code for a defer statement
         * @param node Defer statement node
         *
         * Registers cleanup to run at scope exit.
         */
        // void generate_defer(Cryo::DeferStatementNode *node);

        /**
         * @brief Generate code for an assert statement
         * @param node Assert statement node
         */
        // void generate_assert(Cryo::AssertStatementNode *node);

        //===================================================================
        // Helpers
        //===================================================================

        /**
         * @brief Check if statement is a terminator (return, break, continue)
         * @param stmt Statement to check
         * @return true if statement terminates control flow
         */
        bool is_terminator(Cryo::StatementNode *stmt) const;

        /**
         * @brief Check if current block needs a terminator
         * @return true if block has no terminator
         */
        bool needs_terminator() const;

    private:
        ControlFlowCodegen *_control_flow = nullptr;
        ExpressionCodegen *_expressions = nullptr;

        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Generate expression and return value
         * @param expr Expression to generate
         * @return Generated value
         */
        llvm::Value *generate_expression(Cryo::ExpressionNode *expr);

        /**
         * @brief Generate variable initializer
         * @param init_expr Initializer expression
         * @param var_type Target variable type
         * @return Initialized value
         */
        llvm::Value *generate_initializer(Cryo::ExpressionNode *init_expr, llvm::Type *var_type);

        /**
         * @brief Get default value for a type
         * @param type LLVM type
         * @return Zero/null constant for the type
         */
        llvm::Constant *get_default_value(llvm::Type *type);
    };

} // namespace Cryo::Codegen
