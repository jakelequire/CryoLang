#pragma once

#include "Codegen/ICodegenComponent.hpp"
#include "Codegen/CodegenContext.hpp"
#include "Codegen/Memory/ScopeManager.hpp"
#include "AST/ASTNode.hpp"

#include <llvm/IR/Value.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <stack>

namespace Cryo::Codegen
{
    /**
     * @brief Handles all control flow statement generation
     *
     * This class extracts control flow generation from CodegenVisitor,
     * providing clean, specialized handling for:
     * - If/else statements
     * - While loops
     * - For loops
     * - Switch/case statements
     * - Break/continue statements
     * - Return statements
     *
     * Key features:
     * - Proper basic block management
     * - RAII-based scope tracking
     * - Break/continue context stack
     * - Condition normalization to i1
     */
    class ControlFlowCodegen : public ICodegenComponent
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        explicit ControlFlowCodegen(CodegenContext &ctx);
        ~ControlFlowCodegen() = default;

        /**
         * @brief Set the scope manager for RAII scope handling
         */
        void set_scope_manager(ScopeManager *scope_mgr) { _scope_manager = scope_mgr; }

        /**
         * @brief Set the statement codegen for block generation
         */
        void set_statement_codegen(class StatementCodegen *stmt) { _statements = stmt; }

        //===================================================================
        // If/Else Statements
        //===================================================================

        /**
         * @brief Generate code for an if statement
         * @param node If statement AST node
         */
        void generate_if(Cryo::IfStatementNode *node);

        /**
         * @brief Generate code for an if-else if-else chain
         * @param nodes Chain of if/else-if nodes
         */
        void generate_if_chain(const std::vector<Cryo::IfStatementNode *> &nodes);

        //===================================================================
        // Loop Statements
        //===================================================================

        /**
         * @brief Generate code for a while loop
         * @param node While statement AST node
         */
        void generate_while(Cryo::WhileStatementNode *node);

        /**
         * @brief Generate code for a for loop
         * @param node For statement AST node
         */
        void generate_for(Cryo::ForStatementNode *node);

        /**
         * @brief Generate code for a do-while loop
         * @param node Do-while statement AST node
         */
        void generate_do_while(Cryo::DoWhileStatementNode *node);

        //===================================================================
        // Switch Statements
        //===================================================================

        /**
         * @brief Generate code for a switch statement
         * @param node Switch statement AST node
         */
        void generate_switch(Cryo::SwitchStatementNode *node);

        /**
         * @brief Generate code for a match statement (pattern matching)
         * @param node Match statement AST node
         */
        void generate_match(Cryo::MatchStatementNode *node);

        /**
         * @brief Generate pattern match comparison
         * @param value Value to match against
         * @param pattern Pattern to match
         * @return Boolean comparison result, or nullptr for wildcard patterns
         */
        llvm::Value *generate_pattern_match(llvm::Value *value, Cryo::PatternNode *pattern);

        /**
         * @brief Bind pattern variables for an enum pattern match
         * @param value The matched enum value
         * @param pattern The enum pattern containing bound variables
         * @param match_expr Optional: the match expression node to get resolved type from
         */
        void bind_enum_pattern_variables(llvm::Value *value, Cryo::PatternNode *pattern,
                                         Cryo::ExpressionNode *match_expr = nullptr);

        //===================================================================
        // Jump Statements
        //===================================================================

        /**
         * @brief Generate code for a break statement
         * @param node Break statement AST node (optional, for location info)
         */
        void generate_break(Cryo::ASTNode *node = nullptr);

        /**
         * @brief Generate code for a continue statement
         * @param node Continue statement AST node (optional, for location info)
         */
        void generate_continue(Cryo::ASTNode *node = nullptr);

        /**
         * @brief Generate code for a return statement
         * @param node Return statement AST node
         */
        void generate_return(Cryo::ReturnStatementNode *node);

        //===================================================================
        // Block Management
        //===================================================================

        /**
         * @brief Check if currently inside a loop or switch
         * @return true if in a breakable context
         */
        bool in_breakable_context() const { return !_breakable_stack.empty(); }

        /**
         * @brief Get current loop condition block (for continue)
         * @return Condition block or nullptr if not in loop
         */
        llvm::BasicBlock *get_continue_target() const;

        /**
         * @brief Get current exit block (for break)
         * @return Exit block or nullptr if not in breakable context
         */
        llvm::BasicBlock *get_break_target() const;

    private:
        //===================================================================
        // Breakable Context Management
        //===================================================================

        /**
         * @brief Context for breakable statements (loops, switch)
         */
        struct BreakableContext
        {
            llvm::BasicBlock *condition_block; // Loop condition (for continue)
            llvm::BasicBlock *body_block;      // Loop/switch body
            llvm::BasicBlock *increment_block; // For loop increment (optional)
            llvm::BasicBlock *exit_block;      // After loop/switch (for break)
            bool is_switch;                    // true for switch, false for loop

            BreakableContext(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                             llvm::BasicBlock *inc, llvm::BasicBlock *exit,
                             bool is_switch_ctx = false)
                : condition_block(cond), body_block(body), increment_block(inc),
                  exit_block(exit), is_switch(is_switch_ctx) {}
        };

        std::stack<BreakableContext> _breakable_stack;
        ScopeManager *_scope_manager = nullptr;
        StatementCodegen *_statements = nullptr;

        //===================================================================
        // Internal Helpers
        //===================================================================

        /**
         * @brief Push a new breakable context
         */
        void push_breakable(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                            llvm::BasicBlock *inc, llvm::BasicBlock *exit,
                            bool is_switch = false);

        /**
         * @brief Pop the current breakable context
         */
        void pop_breakable();

        /**
         * @brief Generate condition expression and convert to i1 if needed
         * @param condition Condition expression
         * @param node Parent node for error reporting
         * @return Condition value as i1, or nullptr on error
         */
        llvm::Value *generate_condition(Cryo::ExpressionNode *condition, Cryo::ASTNode *node);

        /**
         * @brief Ensure two LLVM values have compatible types for comparison
         * @param lhs Left-hand side value (modified in place if needed)
         * @param rhs Right-hand side value (modified in place if needed)
         * @return true if types are compatible or successfully coerced
         */
        bool ensure_compatible_types(llvm::Value *&lhs, llvm::Value *&rhs);

        /**
         * @brief Ensure a block has a terminator, adding a branch if needed
         * @param block Block to check
         * @param target Target for branch if no terminator
         */
        void ensure_terminator(llvm::BasicBlock *block, llvm::BasicBlock *target);

        /**
         * @brief Generate code for a statement block
         * @param stmt Statement to generate
         * @param block Block to generate in
         */
        void generate_statement_in_block(Cryo::StatementNode *stmt, llvm::BasicBlock *block);

        /**
         * @brief Generate switch for integer types
         * @param node Switch statement node
         * @param switch_value Value being switched on
         * @param end_block Exit block
         */
        void generate_integer_switch(Cryo::SwitchStatementNode *node,
                                     llvm::Value *switch_value,
                                     llvm::BasicBlock *end_block);

        /**
         * @brief Generate switch for string types (uses if-else chain)
         * @param node Switch statement node
         * @param switch_value String value being switched on
         * @param end_block Exit block
         */
        void generate_string_switch(Cryo::SwitchStatementNode *node,
                                    llvm::Value *switch_value,
                                    llvm::BasicBlock *end_block);

        /**
         * @brief Generate expression and get result
         * @param expr Expression to generate
         * @return Generated value
         */
        llvm::Value *generate_expression(Cryo::ExpressionNode *expr);

        /**
         * @brief Enter a new scope (if scope manager available)
         * @param block Associated basic block
         */
        void enter_scope(llvm::BasicBlock *block);

        /**
         * @brief Exit the current scope
         */
        void exit_scope();
    };

} // namespace Cryo::Codegen
