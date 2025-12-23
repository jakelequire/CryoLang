#include "Codegen/Statements/StatementCodegen.hpp"
#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/Expressions/ExpressionCodegen.hpp"
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
                  static_cast<int>(stmt->kind()));

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

        case NodeKind::VarDeclaration:
            generate_var_declaration(static_cast<VarDeclarationNode *>(stmt));
            break;

        case NodeKind::ConstDeclaration:
            generate_const_declaration(static_cast<ConstDeclarationNode *>(stmt));
            break;

        case NodeKind::IfStatement:
            if (_control_flow)
            {
                _control_flow->generate_if(static_cast<IfStatementNode *>(stmt));
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        case NodeKind::WhileStatement:
            if (_control_flow)
            {
                _control_flow->generate_while(static_cast<WhileStatementNode *>(stmt));
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        case NodeKind::ForStatement:
            if (_control_flow)
            {
                _control_flow->generate_for(static_cast<ForStatementNode *>(stmt));
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        case NodeKind::SwitchStatement:
            if (_control_flow)
            {
                _control_flow->generate_switch(static_cast<SwitchStatementNode *>(stmt));
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        case NodeKind::BreakStatement:
            if (_control_flow)
            {
                _control_flow->generate_break(stmt);
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        case NodeKind::ContinueStatement:
            if (_control_flow)
            {
                _control_flow->generate_continue(stmt);
            }
            else
            {
                stmt->accept(ctx().visitor());
            }
            break;

        default:
            // Fallback to visitor for unhandled statements
            stmt->accept(ctx().visitor());
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
                  node->body().size());

        // Generate each statement in the block
        for (const auto &stmt : node->body())
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

    void StatementCodegen::generate_var_declaration(Cryo::VarDeclarationNode *node)
    {
        if (!node)
            return;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating variable declaration: {}", name);

        // Get variable type
        llvm::Type *var_type = get_llvm_type(node->declared_type());
        if (!var_type)
        {
            report_error(ErrorCode::E0611_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for variable: " + name);
            return;
        }

        // Create alloca in entry block
        llvm::AllocaInst *alloca = create_entry_alloca(var_type, name);
        if (!alloca)
        {
            report_error(ErrorCode::E0611_VARIABLE_INITIALIZATION_ERROR, node,
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

        // Register in value context
        values().set_alloca(name, alloca);
    }

    void StatementCodegen::generate_const_declaration(Cryo::ConstDeclarationNode *node)
    {
        if (!node)
            return;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating constant declaration: {}", name);

        // Get constant type
        llvm::Type *const_type = get_llvm_type(node->declared_type());
        if (!const_type)
        {
            report_error(ErrorCode::E0611_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for constant: " + name);
            return;
        }

        // Constants must have initializers
        if (!node->initializer())
        {
            report_error(ErrorCode::E0611_VARIABLE_INITIALIZATION_ERROR, node,
                         "Constant must have initializer: " + name);
            return;
        }

        // Generate initializer
        llvm::Value *init_val = generate_initializer(node->initializer(), const_type);

        // For compile-time constants, try to use the value directly
        if (auto *constant = llvm::dyn_cast<llvm::Constant>(init_val))
        {
            // Store as constant in value context
            values().set_value(name, constant);
        }
        else
        {
            // Runtime constant - use alloca like a variable but mark as const
            llvm::AllocaInst *alloca = create_entry_alloca(const_type, name);
            if (alloca && init_val)
            {
                create_store(init_val, alloca);
                values().set_alloca(name, alloca);
            }
        }
    }

    void StatementCodegen::generate_multi_declaration(Cryo::MultiVarDeclNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating multi-declaration");

        for (const auto &decl : node->declarations())
        {
            if (auto *var_decl = dynamic_cast<VarDeclarationNode *>(decl.get()))
            {
                generate_var_declaration(var_decl);
            }
            else if (auto *const_decl = dynamic_cast<ConstDeclarationNode *>(decl.get()))
            {
                generate_const_declaration(const_decl);
            }
        }
    }

    //===================================================================
    // Special Statements
    //===================================================================

    void StatementCodegen::generate_empty_statement()
    {
        // No-op - nothing to generate
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Empty statement (no-op)");
    }

    void StatementCodegen::generate_defer(Cryo::DeferStatementNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Registering defer statement");

        // Defer statements register cleanup to run at scope exit
        // This would integrate with ScopeManager
        // For now, delegate to visitor
        node->accept(ctx().visitor());
    }

    void StatementCodegen::generate_assert(Cryo::AssertStatementNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "StatementCodegen: Generating assert statement");

        // Generate condition
        llvm::Value *condition = nullptr;
        if (node->condition())
        {
            condition = generate_expression(node->condition());
        }

        if (!condition)
        {
            report_error(ErrorCode::E0614_EXPRESSION_GENERATION_ERROR, node,
                         "Failed to generate assert condition");
            return;
        }

        // Convert to i1 if needed
        if (!condition->getType()->isIntegerTy(1))
        {
            if (condition->getType()->isIntegerTy())
            {
                condition = builder().CreateICmpNE(
                    condition,
                    llvm::ConstantInt::get(condition->getType(), 0),
                    "assert.cond");
            }
        }

        // Create blocks for assert pass/fail
        llvm::Function *fn = builder().GetInsertBlock()->getParent();
        llvm::BasicBlock *pass_block = create_block("assert.pass", fn);
        llvm::BasicBlock *fail_block = create_block("assert.fail", fn);

        builder().CreateCondBr(condition, pass_block, fail_block);

        // Generate fail block - call panic or abort
        builder().SetInsertPoint(fail_block);

        // Try to get panic function
        llvm::Function *panic_fn = module()->getFunction("__panic__");
        if (!panic_fn)
        {
            panic_fn = module()->getFunction("abort");
        }
        if (!panic_fn)
        {
            // Create abort declaration
            llvm::FunctionType *abort_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(llvm_ctx()), false);
            panic_fn = llvm::Function::Create(abort_type, llvm::Function::ExternalLinkage,
                                               "abort", module());
        }

        builder().CreateCall(panic_fn);
        builder().CreateUnreachable();

        // Continue with pass block
        builder().SetInsertPoint(pass_block);
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
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
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
        expr->accept(ctx().visitor());
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
