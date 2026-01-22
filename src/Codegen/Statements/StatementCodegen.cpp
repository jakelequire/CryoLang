#include "Codegen/Statements/StatementCodegen.hpp"
#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/Expressions/ExpressionCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    StatementCodegen::StatementCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Main Entry Point
    //===================================================================

    void StatementCodegen::generate(Cryo::StatementNode *stmt)
    {
        if (!stmt)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating statement of kind {}",
                  NodeKindToString(stmt->kind()));

        // Dispatch based on statement kind
        switch (stmt->kind())
        {
        case NodeKind::BlockStatement:
            generate_block(static_cast<BlockStatementNode *>(stmt));
            break;

        case NodeKind::ExpressionStatement:
            generate_expression_statement(static_cast<ExpressionStatementNode *>(stmt));
            break;

        case NodeKind::ReturnStatement:
            generate_return(static_cast<ReturnStatementNode *>(stmt));
            break;

        case NodeKind::IfStatement:
            if (_control_flow)
            {
                _control_flow->generate_if(static_cast<IfStatementNode *>(stmt));
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        case NodeKind::WhileStatement:
            if (_control_flow)
            {
                _control_flow->generate_while(static_cast<WhileStatementNode *>(stmt));
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        case NodeKind::ForStatement:
            if (_control_flow)
            {
                _control_flow->generate_for(static_cast<ForStatementNode *>(stmt));
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        case NodeKind::SwitchStatement:
            if (_control_flow)
            {
                _control_flow->generate_switch(static_cast<SwitchStatementNode *>(stmt));
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        case NodeKind::BreakStatement:
            if (_control_flow)
            {
                _control_flow->generate_break(stmt);
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        case NodeKind::ContinueStatement:
            if (_control_flow)
            {
                _control_flow->generate_continue(stmt);
            }
            else
            {
                CodegenVisitor *visitor = ctx().visitor();
                stmt->accept(*visitor);
            }
            break;

        default:
            // Fallback to visitor for unhandled statements
            CodegenVisitor *visitor = ctx().visitor();
            stmt->accept(*visitor);
            break;
        }
    }

    //===================================================================
    // Block Statements
    //===================================================================

    void StatementCodegen::generate_block(Cryo::BlockStatementNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating block with {} statements",
                  node->statements().size());

        // Enter a new scope for this block - variables declared here should be isolated
        values().enter_scope("block");

        // Generate each statement in the block
        for (const auto &stmt : node->statements())
        {
            if (!stmt)
                continue;

            generate(stmt.get());

            // Stop generating if we hit a terminator
            if (is_terminator(stmt.get()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Hit terminator, stopping block generation");
                break;
            }
        }

        // Exit block scope - cleans up local variables
        values().exit_scope();
    }

    void StatementCodegen::generate_statement_list(
        const std::vector<std::unique_ptr<Cryo::StatementNode>> &statements)
    {
        for (const auto &stmt : statements)
        {
            if (!stmt)
                continue;

            generate(stmt.get());

            // Stop generating if we hit a terminator
            if (is_terminator(stmt.get()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Hit terminator, stopping block generation");
                break;
            }
        }
    }

    //===================================================================
    // Expression Statements
    //===================================================================

    void StatementCodegen::generate_expression_statement(Cryo::ExpressionStatementNode *node)
    {
        if (!node || !node->expression())
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating expression statement");

        // Generate the expression for side effects, discard result
        generate_expression(node->expression());
    }

    //===================================================================
    // Return Statements
    //===================================================================

    void StatementCodegen::generate_return(Cryo::ReturnStatementNode *node)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating return statement");

        // If control flow codegen is available, use it (handles destructor cleanup)
        if (_control_flow)
        {
            _control_flow->generate_return(node);
            return;
        }

        // Fallback implementation
        if (node && node->expression())
        {
            llvm::Value *ret_val = generate_expression(node->expression());
            if (ret_val)
            {
                builder().CreateRet(ret_val);
            }
            else
            {
                builder().CreateRetVoid();
            }
        }
        else
        {
            builder().CreateRetVoid();
        }
    }

    void StatementCodegen::generate_implicit_return()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating implicit return");

        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            llvm::Function *fn = current_block->getParent();
            if (fn && fn->getReturnType()->isVoidTy())
            {
                builder().CreateRetVoid();
            }
            else if (fn)
            {
                // Return default value for non-void functions
                llvm::Value *default_val = get_default_value(fn->getReturnType());
                builder().CreateRet(default_val);
            }
        }
    }

    //===================================================================
    // Variable Declarations
    //===================================================================

    void StatementCodegen::generate_var_declaration(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating variable declaration: {}", name);

        // Get variable type from resolved type
        llvm::Type *var_type = nullptr;
        if (node->has_resolved_type())
        {
            var_type = types().map(node->get_resolved_type());
        }
        if (!var_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for variable: " + name);
            return;
        }

        // Create alloca in entry block
        llvm::AllocaInst *alloca = create_entry_alloca(var_type, name);
        if (!alloca)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Failed to allocate variable: " + name);
            return;
        }

        // Generate initializer if present
        if (node->initializer())
        {
            llvm::Value *init_val = generate_initializer(node->initializer(), var_type);
            if (init_val)
            {
                create_store(init_val, alloca);
            }
        }
        else
        {
            // Initialize to default value
            llvm::Constant *default_val = get_default_value(var_type);
            if (default_val)
            {
                create_store(default_val, alloca);
            }
        }

        // Register in value context with the alloca type for proper type tracking
        // The 4th parameter (alloca_type) is needed for correct store/load operations
        values().set_value(name, nullptr, alloca, var_type);
    }

    //===================================================================
    // Special Statements
    //===================================================================

    void StatementCodegen::generate_empty_statement()
    {
        // No-op - nothing to generate
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Empty statement (no-op)");
    }

    //===================================================================
    // Helpers
    //===================================================================

    bool StatementCodegen::is_terminator(Cryo::StatementNode *stmt) const
    {
        if (!stmt)
            return false;

        switch (stmt->kind())
        {
        case NodeKind::ReturnStatement:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
            return true;
        default:
            return false;
        }
    }

    bool StatementCodegen::needs_terminator() const
    {
        // Cast away const for this query operation
        auto *non_const_this = const_cast<StatementCodegen *>(this);
        llvm::BasicBlock *current_block = non_const_this->builder().GetInsertBlock();
        return current_block && !current_block->getTerminator();
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    llvm::Value *StatementCodegen::generate_expression(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Use visitor pattern through context
        CodegenVisitor *visitor = ctx().visitor();
        expr->accept(*visitor);
        return get_result();
    }

    llvm::Value *StatementCodegen::generate_initializer(Cryo::ExpressionNode *init_expr,
                                                        llvm::Type *var_type)
    {
        if (!init_expr)
            return nullptr;

        llvm::Value *init_val = generate_expression(init_expr);
        if (!init_val)
            return nullptr;

        // Cast to target type if needed
        return cast_if_needed(init_val, var_type);
    }

    llvm::Constant *StatementCodegen::get_default_value(llvm::Type *type)
    {
        if (!type)
            return nullptr;

        return llvm::Constant::getNullValue(type);
    }

} // namespace Cryo::Codegen
