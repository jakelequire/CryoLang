#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    ControlFlowCodegen::ControlFlowCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // If/Else Statements
    //===================================================================

    void ControlFlowCodegen::generate_if(Cryo::IfStatementNode *node)
    {
        if (!node)
            return;

        llvm::Function *function = builder().GetInsertBlock()->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for if statement");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating if statement");

        // Generate condition expression FIRST
        // (Don't create then/else/merge blocks yet - condition might create its own control flow)
        llvm::Value *condition_val = generate_condition(node->condition(), node);
        if (!condition_val)
        {
            return;
        }

        // After condition evaluation, we might be in a block created by short-circuit evaluation
        llvm::BasicBlock *condition_block = builder().GetInsertBlock();

        // NOW create the control flow blocks (after condition is evaluated)
        llvm::BasicBlock *then_block = create_block("if.then", function);
        llvm::BasicBlock *else_block = nullptr;
        llvm::BasicBlock *merge_block = create_block("if.end", function);

        // Create else block if there's an else clause
        if (node->else_statement())
        {
            else_block = create_block("if.else", function);
        }

        // Check if condition_block already has a terminator
        if (condition_block->getTerminator())
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                         "Condition block already has terminator");
            return;
        }

        // Add the conditional branch
        if (else_block)
        {
            builder().CreateCondBr(condition_val, then_block, else_block);
        }
        else
        {
            builder().CreateCondBr(condition_val, then_block, merge_block);
        }

        // Generate then block
        builder().SetInsertPoint(then_block);
        enter_scope(then_block);
        node->then_statement()->accept(ctx().visitor());
        exit_scope();

        // Ensure current block ends with a branch to merge
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            builder().CreateBr(merge_block);
        }

        // Also ensure then_block itself has terminator
        if (then_block && !then_block->getTerminator())
        {
            llvm::BasicBlock *saved_block = builder().GetInsertBlock();
            builder().SetInsertPoint(then_block);
            builder().CreateBr(merge_block);
            builder().SetInsertPoint(saved_block);
        }

        // Generate else block if present
        if (else_block && node->else_statement())
        {
            builder().SetInsertPoint(else_block);
            enter_scope(else_block);
            node->else_statement()->accept(ctx().visitor());
            exit_scope();

            // Ensure else ends with branch to merge
            llvm::BasicBlock *else_current = builder().GetInsertBlock();
            if (else_current && !else_current->getTerminator())
            {
                builder().CreateBr(merge_block);
            }
        }

        // Continue with merge block
        builder().SetInsertPoint(merge_block);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: If statement complete");
    }

    void ControlFlowCodegen::generate_if_chain(const std::vector<Cryo::IfStatementNode *> &nodes)
    {
        // For simplicity, generate as nested if-else
        for (auto *node : nodes)
        {
            generate_if(node);
        }
    }

    //===================================================================
    // Loop Statements
    //===================================================================

    void ControlFlowCodegen::generate_while(Cryo::WhileStatementNode *node)
    {
        if (!node)
            return;

        llvm::Function *function = builder().GetInsertBlock()->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for while loop");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating while loop");

        // Create basic blocks for the loop
        llvm::BasicBlock *condition_block = create_block("while.cond", function);
        llvm::BasicBlock *body_block = create_block("while.body", function);
        llvm::BasicBlock *exit_block = create_block("while.end", function);

        // Push breakable context for break/continue
        push_breakable(condition_block, body_block, condition_block, exit_block);

        // Jump to condition block
        builder().CreateBr(condition_block);

        // Generate condition block
        builder().SetInsertPoint(condition_block);
        llvm::Value *condition_val = generate_condition(node->condition(), node);
        if (!condition_val)
        {
            pop_breakable();
            return;
        }

        // Conditional branch: if true go to body, else exit
        builder().CreateCondBr(condition_val, body_block, exit_block);

        // Generate body block
        builder().SetInsertPoint(body_block);
        enter_scope(body_block);
        node->body()->accept(ctx().visitor());
        exit_scope();

        // Ensure current block ends with branch back to condition
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            builder().CreateBr(condition_block);
        }

        // Pop breakable context
        pop_breakable();

        // Continue with exit block
        builder().SetInsertPoint(exit_block);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: While loop complete");
    }

    void ControlFlowCodegen::generate_for(Cryo::ForStatementNode *node)
    {
        if (!node)
            return;

        llvm::Function *function = builder().GetInsertBlock()->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for for loop");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating for loop");

        // Create basic blocks for the for loop
        llvm::BasicBlock *condition_block = create_block("for.cond", function);
        llvm::BasicBlock *body_block = create_block("for.body", function);
        llvm::BasicBlock *increment_block = create_block("for.inc", function);
        llvm::BasicBlock *exit_block = create_block("for.end", function);

        // Push breakable context (continue goes to increment, not condition)
        push_breakable(condition_block, body_block, increment_block, exit_block);

        // Generate initialization statement
        if (node->init())
        {
            node->init()->accept(ctx().visitor());
        }

        // Branch to condition block
        builder().CreateBr(condition_block);

        // Generate condition block
        builder().SetInsertPoint(condition_block);
        if (node->condition())
        {
            llvm::Value *condition_val = generate_condition(node->condition(), node);
            if (!condition_val)
            {
                pop_breakable();
                return;
            }
            builder().CreateCondBr(condition_val, body_block, exit_block);
        }
        else
        {
            // No condition means always true
            builder().CreateBr(body_block);
        }

        // Generate body block
        builder().SetInsertPoint(body_block);
        enter_scope(body_block);
        if (node->body())
        {
            node->body()->accept(ctx().visitor());
        }
        exit_scope();

        // Ensure body ends with branch to increment
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            builder().CreateBr(increment_block);
        }

        // Generate increment block
        builder().SetInsertPoint(increment_block);
        if (node->update())
        {
            node->update()->accept(ctx().visitor());
        }
        // Branch back to condition
        builder().CreateBr(condition_block);

        // Pop breakable context
        pop_breakable();

        // Continue with exit block
        builder().SetInsertPoint(exit_block);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: For loop complete");
    }

    void ControlFlowCodegen::generate_do_while(Cryo::DoWhileStatementNode *node)
    {
        if (!node)
            return;

        llvm::Function *function = builder().GetInsertBlock()->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for do-while loop");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating do-while loop");

        // Create basic blocks
        llvm::BasicBlock *body_block = create_block("dowhile.body", function);
        llvm::BasicBlock *condition_block = create_block("dowhile.cond", function);
        llvm::BasicBlock *exit_block = create_block("dowhile.end", function);

        // Push breakable context
        push_breakable(condition_block, body_block, condition_block, exit_block);

        // Jump directly to body (do-while executes body first)
        builder().CreateBr(body_block);

        // Generate body block
        builder().SetInsertPoint(body_block);
        enter_scope(body_block);
        node->body()->accept(ctx().visitor());
        exit_scope();

        // Ensure body ends with branch to condition
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (current_block && !current_block->getTerminator())
        {
            builder().CreateBr(condition_block);
        }

        // Generate condition block
        builder().SetInsertPoint(condition_block);
        llvm::Value *condition_val = generate_condition(node->condition(), node);
        if (!condition_val)
        {
            pop_breakable();
            return;
        }

        // If condition true, loop back to body; else exit
        builder().CreateCondBr(condition_val, body_block, exit_block);

        // Pop breakable context
        pop_breakable();

        // Continue with exit block
        builder().SetInsertPoint(exit_block);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Do-while loop complete");
    }

    //===================================================================
    // Switch Statements
    //===================================================================

    void ControlFlowCodegen::generate_switch(Cryo::SwitchStatementNode *node)
    {
        if (!node)
            return;

        llvm::Function *function = builder().GetInsertBlock()->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for switch statement");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating switch statement");

        // Generate switch expression
        llvm::Value *switch_value = generate_expression(node->expression());
        if (!switch_value)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "Failed to generate switch expression");
            return;
        }

        // Create end block
        llvm::BasicBlock *end_block = create_block("switch.end", function);

        // Push switch context (break targets end_block)
        push_breakable(nullptr, nullptr, nullptr, end_block, true);

        // Determine switch type (integer vs string)
        if (switch_value->getType()->isIntegerTy())
        {
            generate_integer_switch(node, switch_value, end_block);
        }
        else if (switch_value->getType()->isPointerTy())
        {
            // Assume string (char*)
            generate_string_switch(node, switch_value, end_block);
        }
        else
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                         "Unsupported switch expression type");
        }

        // Pop switch context
        pop_breakable();

        // Continue with end block
        builder().SetInsertPoint(end_block);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Switch statement complete");
    }

    void ControlFlowCodegen::generate_integer_switch(Cryo::SwitchStatementNode *node,
                                                      llvm::Value *switch_value,
                                                      llvm::BasicBlock *end_block)
    {
        llvm::Function *function = builder().GetInsertBlock()->getParent();

        // Create default block
        llvm::BasicBlock *default_block = create_block("switch.default", function);

        // Create switch instruction
        size_t num_cases = node->cases().size();
        llvm::SwitchInst *switch_inst = builder().CreateSwitch(switch_value, default_block, num_cases);

        // Generate case blocks
        llvm::BasicBlock *prev_block = nullptr;
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto *case_node = node->cases()[i].get();
            if (!case_node)
                continue;

            llvm::BasicBlock *case_block = create_block("switch.case." + std::to_string(i), function);

            // Generate case value and add to switch
            if (case_node->case_value())
            {
                llvm::Value *case_val = generate_expression(case_node->case_value());
                if (case_val && llvm::isa<llvm::ConstantInt>(case_val))
                {
                    switch_inst->addCase(llvm::cast<llvm::ConstantInt>(case_val), case_block);
                }
            }

            // Generate case body
            builder().SetInsertPoint(case_block);
            enter_scope(case_block);
            if (case_node->body())
            {
                case_node->body()->accept(ctx().visitor());
            }
            exit_scope();

            // Fall through to next case if no break (handled by break statement)
            // Default: branch to end
            llvm::BasicBlock *current = builder().GetInsertBlock();
            if (current && !current->getTerminator())
            {
                // Check if there's a next case for fallthrough
                if (i + 1 < node->cases().size())
                {
                    // Create next case block reference for fallthrough
                    // Note: fallthrough is implicit if no break
                }
                builder().CreateBr(end_block);
            }

            prev_block = case_block;
        }

        // Generate default block
        builder().SetInsertPoint(default_block);
        if (node->default_case())
        {
            enter_scope(default_block);
            node->default_case()->accept(ctx().visitor());
            exit_scope();
        }

        // Ensure default ends
        llvm::BasicBlock *default_current = builder().GetInsertBlock();
        if (default_current && !default_current->getTerminator())
        {
            builder().CreateBr(end_block);
        }
    }

    void ControlFlowCodegen::generate_string_switch(Cryo::SwitchStatementNode *node,
                                                     llvm::Value *switch_value,
                                                     llvm::BasicBlock *end_block)
    {
        llvm::Function *function = builder().GetInsertBlock()->getParent();

        // String switch is implemented as if-else chain with strcmp
        // Get or create strcmp function
        llvm::Function *strcmp_fn = module()->getFunction("strcmp");
        if (!strcmp_fn)
        {
            llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
            llvm::Type *i32_type = llvm::Type::getInt32Ty(llvm_ctx());
            llvm::FunctionType *strcmp_type = llvm::FunctionType::get(i32_type, {ptr_type, ptr_type}, false);
            strcmp_fn = llvm::Function::Create(strcmp_type, llvm::Function::ExternalLinkage, "strcmp", module());
        }

        llvm::BasicBlock *default_block = create_block("switch.default", function);

        // Generate if-else chain for each case
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto *case_node = node->cases()[i].get();
            if (!case_node || !case_node->case_value())
                continue;

            // Generate case value (string)
            llvm::Value *case_val = generate_expression(case_node->case_value());
            if (!case_val)
                continue;

            // Compare strings
            llvm::Value *cmp_result = builder().CreateCall(strcmp_fn, {switch_value, case_val}, "strcmp.result");
            llvm::Value *is_equal = builder().CreateICmpEQ(cmp_result,
                                                            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                                                            "str.eq");

            // Create blocks for this case
            llvm::BasicBlock *case_block = create_block("switch.case." + std::to_string(i), function);
            llvm::BasicBlock *next_block = (i + 1 < node->cases().size())
                                               ? create_block("switch.next." + std::to_string(i), function)
                                               : default_block;

            builder().CreateCondBr(is_equal, case_block, next_block);

            // Generate case body
            builder().SetInsertPoint(case_block);
            enter_scope(case_block);
            if (case_node->body())
            {
                case_node->body()->accept(ctx().visitor());
            }
            exit_scope();

            // Branch to end
            llvm::BasicBlock *current = builder().GetInsertBlock();
            if (current && !current->getTerminator())
            {
                builder().CreateBr(end_block);
            }

            // Continue with next comparison
            if (next_block != default_block)
            {
                builder().SetInsertPoint(next_block);
            }
        }

        // Generate default block
        builder().SetInsertPoint(default_block);
        if (node->default_case())
        {
            enter_scope(default_block);
            node->default_case()->accept(ctx().visitor());
            exit_scope();
        }

        llvm::BasicBlock *default_current = builder().GetInsertBlock();
        if (default_current && !default_current->getTerminator())
        {
            builder().CreateBr(end_block);
        }
    }

    //===================================================================
    // Jump Statements
    //===================================================================

    void ControlFlowCodegen::generate_break(Cryo::ASTNode *node)
    {
        if (_breakable_stack.empty())
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                         "break statement outside of loop or switch");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating break");

        // Run destructors for current scope if scope manager available
        if (_scope_manager)
        {
            _scope_manager->generate_break_cleanup(builder());
        }

        // Branch to exit block
        llvm::BasicBlock *exit_block = _breakable_stack.top().exit_block;
        if (exit_block)
        {
            builder().CreateBr(exit_block);
        }
    }

    void ControlFlowCodegen::generate_continue(Cryo::ASTNode *node)
    {
        if (_breakable_stack.empty())
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                         "continue statement outside of loop");
            return;
        }

        const auto &ctx = _breakable_stack.top();
        if (ctx.is_switch)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                         "continue statement inside switch (not in loop)");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating continue");

        // Run destructors for current scope
        if (_scope_manager)
        {
            _scope_manager->generate_continue_cleanup(builder());
        }

        // Branch to continue target (increment for for-loop, condition for while)
        llvm::BasicBlock *target = ctx.increment_block ? ctx.increment_block : ctx.condition_block;
        if (target)
        {
            builder().CreateBr(target);
        }
    }

    void ControlFlowCodegen::generate_return(Cryo::ReturnStatementNode *node)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating return");

        // Run all destructors in scope stack
        if (_scope_manager)
        {
            _scope_manager->generate_return_cleanup(builder());
        }

        if (node && node->expression())
        {
            llvm::Value *ret_val = generate_expression(node->expression());
            if (ret_val)
            {
                builder().CreateRet(ret_val);
            }
            else
            {
                // Fall back to void return on error
                builder().CreateRetVoid();
            }
        }
        else
        {
            builder().CreateRetVoid();
        }
    }

    //===================================================================
    // Block Management
    //===================================================================

    llvm::BasicBlock *ControlFlowCodegen::get_continue_target() const
    {
        if (_breakable_stack.empty())
            return nullptr;

        const auto &ctx = _breakable_stack.top();
        if (ctx.is_switch)
            return nullptr;

        return ctx.increment_block ? ctx.increment_block : ctx.condition_block;
    }

    llvm::BasicBlock *ControlFlowCodegen::get_break_target() const
    {
        if (_breakable_stack.empty())
            return nullptr;

        return _breakable_stack.top().exit_block;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    void ControlFlowCodegen::push_breakable(llvm::BasicBlock *cond, llvm::BasicBlock *body,
                                             llvm::BasicBlock *inc, llvm::BasicBlock *exit,
                                             bool is_switch)
    {
        _breakable_stack.emplace(cond, body, inc, exit, is_switch);
    }

    void ControlFlowCodegen::pop_breakable()
    {
        if (!_breakable_stack.empty())
        {
            _breakable_stack.pop();
        }
    }

    llvm::Value *ControlFlowCodegen::generate_condition(Cryo::ExpressionNode *condition,
                                                         Cryo::ASTNode *node)
    {
        if (!condition)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "Null condition expression");
            return nullptr;
        }

        llvm::Value *condition_val = generate_expression(condition);
        if (!condition_val)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "Failed to generate condition");
            return nullptr;
        }

        // Convert to i1 if needed
        if (!condition_val->getType()->isIntegerTy(1))
        {
            if (condition_val->getType()->isIntegerTy())
            {
                condition_val = builder().CreateICmpNE(
                    condition_val,
                    llvm::ConstantInt::get(condition_val->getType(), 0),
                    "tobool");
            }
            else if (condition_val->getType()->isFloatingPointTy())
            {
                condition_val = builder().CreateFCmpUNE(
                    condition_val,
                    llvm::ConstantFP::get(condition_val->getType(), 0.0),
                    "tobool");
            }
            else if (condition_val->getType()->isPointerTy())
            {
                // Pointer is truthy if not null
                llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(condition_val->getType()));
                condition_val = builder().CreateICmpNE(condition_val, null_ptr, "tobool");
            }
            else
            {
                report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node,
                             "Invalid condition type");
                return nullptr;
            }
        }

        return condition_val;
    }

    void ControlFlowCodegen::ensure_terminator(llvm::BasicBlock *block, llvm::BasicBlock *target)
    {
        if (!block || block->getTerminator())
            return;

        llvm::BasicBlock *saved = builder().GetInsertBlock();
        builder().SetInsertPoint(block);
        builder().CreateBr(target);
        builder().SetInsertPoint(saved);
    }

    void ControlFlowCodegen::generate_statement_in_block(Cryo::StatementNode *stmt,
                                                          llvm::BasicBlock *block)
    {
        if (!stmt || !block)
            return;

        builder().SetInsertPoint(block);
        enter_scope(block);
        stmt->accept(ctx().visitor());
        exit_scope();
    }

    llvm::Value *ControlFlowCodegen::generate_expression(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Use visitor pattern through context
        expr->accept(ctx().visitor());
        return get_result();
    }

    void ControlFlowCodegen::enter_scope(llvm::BasicBlock *block)
    {
        if (_scope_manager)
        {
            _scope_manager->enter_scope(block);
        }
    }

    void ControlFlowCodegen::exit_scope()
    {
        if (_scope_manager)
        {
            _scope_manager->exit_scope(builder());
        }
    }

} // namespace Cryo::Codegen
