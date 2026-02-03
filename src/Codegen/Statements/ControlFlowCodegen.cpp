#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
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

        // Check for valid insertion context
        llvm::BasicBlock *current_block = builder().GetInsertBlock();
        if (!current_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for if statement");
            return;
        }

        llvm::Function *function = current_block->getParent();
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
        CodegenVisitor *visitor = ctx().visitor();
        node->then_statement()->accept(*visitor);
        exit_scope();

        // Track if then branch has a path to merge block
        bool then_falls_through = false;
        llvm::BasicBlock *then_end_block = builder().GetInsertBlock();
        if (then_end_block && !then_end_block->getTerminator())
        {
            builder().CreateBr(merge_block);
            then_falls_through = true;
        }

        // Also ensure then_block itself has terminator (for nested control flow)
        if (then_block && !then_block->getTerminator())
        {
            llvm::BasicBlock *saved_block = builder().GetInsertBlock();
            builder().SetInsertPoint(then_block);
            builder().CreateBr(merge_block);
            then_falls_through = true;
            builder().SetInsertPoint(saved_block);
        }

        // Track if else branch has a path to merge block
        bool else_falls_through = false;

        // Generate else block if present
        if (else_block && node->else_statement())
        {
            builder().SetInsertPoint(else_block);
            enter_scope(else_block);
            CodegenVisitor *visitor = ctx().visitor();
            node->else_statement()->accept(*visitor);
            exit_scope();

            // Check if else ends with branch to merge
            llvm::BasicBlock *else_current = builder().GetInsertBlock();
            if (else_current && !else_current->getTerminator())
            {
                builder().CreateBr(merge_block);
                else_falls_through = true;
            }
        }
        else if (!else_block)
        {
            // No else clause means the false branch goes directly to merge
            else_falls_through = true;
        }

        // Only continue with merge block if at least one branch falls through to it
        // If both branches terminate (break/return/continue), merge block is unreachable
        if (then_falls_through || else_falls_through)
        {
            builder().SetInsertPoint(merge_block);
        }
        else
        {
            // Both branches terminated - merge block is dead code
            // Remove it from the function to avoid LLVM verification errors
            merge_block->eraseFromParent();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "ControlFlowCodegen: Removed unreachable merge block (both branches terminate)");
        }

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

        // Check for valid insertion context
        llvm::BasicBlock *while_block = builder().GetInsertBlock();
        if (!while_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for while loop");
            return;
        }

        llvm::Function *function = while_block->getParent();
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
        CodegenVisitor *visitor = ctx().visitor();
        node->body()->accept(*visitor);
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

        // Check for valid insertion context
        llvm::BasicBlock *for_block = builder().GetInsertBlock();
        if (!for_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for for loop");
            return;
        }

        llvm::Function *function = for_block->getParent();
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
            CodegenVisitor *visitor = ctx().visitor();
            node->init()->accept(*visitor);
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
            CodegenVisitor *visitor = ctx().visitor();
            node->body()->accept(*visitor);
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
            CodegenVisitor *visitor = ctx().visitor();
            node->update()->accept(*visitor);
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

        // Check for valid insertion context
        llvm::BasicBlock *do_while_block = builder().GetInsertBlock();
        if (!do_while_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for do-while loop");
            return;
        }

        llvm::Function *function = do_while_block->getParent();
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
        CodegenVisitor *visitor = ctx().visitor();
        node->body()->accept(*visitor);
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

        // Check for valid insertion context
        llvm::BasicBlock *switch_block = builder().GetInsertBlock();
        if (!switch_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for switch statement");
            return;
        }

        llvm::Function *function = switch_block->getParent();
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
        // Check for valid insertion context
        llvm::BasicBlock *int_switch_block = builder().GetInsertBlock();
        if (!int_switch_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for integer switch");
            return;
        }

        llvm::Function *function = int_switch_block->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for integer switch");
            return;
        }

        // Create default block
        llvm::BasicBlock *default_block = create_block("switch.default", function);

        // Create switch instruction
        size_t num_cases = node->cases().size();
        llvm::SwitchInst *switch_inst = builder().CreateSwitch(switch_value, default_block, num_cases);

        // Generate case blocks
        std::vector<llvm::BasicBlock *> case_blocks;
        
        // Pre-create all case blocks for fallthrough references
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto *case_node = node->cases()[i].get();
            if (case_node && !case_node->is_default())
            {
                llvm::BasicBlock *case_block = create_block("switch.case." + std::to_string(i), function);
                case_blocks.push_back(case_block);
            }
            else
            {
                case_blocks.push_back(nullptr); // Placeholder for default case
            }
        }
        
        // Generate case blocks
        for (size_t i = 0; i < node->cases().size(); ++i)
        {
            auto *case_node = node->cases()[i].get();
            if (!case_node || case_node->is_default())
                continue; // Skip default case, handled separately

            llvm::BasicBlock *case_block = case_blocks[i];
            if (!case_block)
                continue;

            // Generate case value and add to switch
            if (case_node->value())
            {
                llvm::Value *case_val = generate_expression(case_node->value());
                if (case_val && llvm::isa<llvm::ConstantInt>(case_val))
                {
                    switch_inst->addCase(llvm::cast<llvm::ConstantInt>(case_val), case_block);
                }
            }

            // Generate case body
            builder().SetInsertPoint(case_block);
            enter_scope(case_block);
            if (!case_node->statements().empty())
            {
                CodegenVisitor *visitor = ctx().visitor();
                for (const auto &stmt : case_node->statements())
                {
                    stmt->accept(*visitor);
                }
            }
            exit_scope();

            // Handle fallthrough: if no terminator exists, fall through to next case or end
            llvm::BasicBlock *current = builder().GetInsertBlock();
            if (current && !current->getTerminator())
            {
                // Find next non-default case for fallthrough
                llvm::BasicBlock *fallthrough_target = end_block;
                for (size_t j = i + 1; j < case_blocks.size(); ++j)
                {
                    if (case_blocks[j])
                    {
                        fallthrough_target = case_blocks[j];
                        break;
                    }
                }
                
                // If no more cases, fall through to default if it exists, otherwise end
                if (fallthrough_target == end_block)
                {
                    // Check if default case exists
                    for (const auto &case_stmt : node->cases())
                    {
                        if (case_stmt->is_default())
                        {
                            fallthrough_target = default_block;
                            break;
                        }
                    }
                }
                
                builder().CreateBr(fallthrough_target);
            }
        }

        // Generate default block
        builder().SetInsertPoint(default_block);
        CaseStatementNode *default_case = nullptr;
        for (const auto &case_stmt : node->cases())
        {
            if (case_stmt->is_default())
            {
                default_case = case_stmt.get();
                break;
            }
        }
        if (default_case)
        {
            enter_scope(default_block);
            CodegenVisitor *visitor = ctx().visitor();
            for (const auto &stmt : default_case->statements())
            {
                stmt->accept(*visitor);
            }
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
        // Check for valid insertion context
        llvm::BasicBlock *str_switch_block = builder().GetInsertBlock();
        if (!str_switch_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for string switch");
            return;
        }

        llvm::Function *function = str_switch_block->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for string switch");
            return;
        }

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
            if (!case_node || !case_node->value())
                continue;

            // Generate case value (string)
            llvm::Value *case_val = generate_expression(case_node->value());
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
            if (!case_node->statements().empty())
            {
                CodegenVisitor *visitor = ctx().visitor();
                for (const auto &stmt : case_node->statements())
                {
                    stmt->accept(*visitor);
                }
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
        CaseStatementNode *default_case = nullptr;
        for (const auto &case_stmt : node->cases())
        {
            if (case_stmt->is_default())
            {
                default_case = case_stmt.get();
                break;
            }
        }
        if (default_case)
        {
            enter_scope(default_block);
            CodegenVisitor *visitor = ctx().visitor();
            for (const auto &stmt : default_case->statements())
            {
                stmt->accept(*visitor);
            }
            exit_scope();
        }

        llvm::BasicBlock *default_current = builder().GetInsertBlock();
        if (default_current && !default_current->getTerminator())
        {
            builder().CreateBr(end_block);
        }
    }

    //===================================================================
    // Match Statement
    //===================================================================

    void ControlFlowCodegen::generate_match(Cryo::MatchStatementNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: Generating match statement");

        // Check for valid insertion context
        llvm::BasicBlock *match_block = builder().GetInsertBlock();
        if (!match_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for match statement");
            return;
        }

        llvm::Function *function = match_block->getParent();
        if (!function)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for match statement");
            return;
        }

        // Generate the match expression
        llvm::Value *match_value = generate_expression(node->expr());
        if (!match_value)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "Failed to generate match expression");
            return;
        }

        // Create end block
        llvm::BasicBlock *end_block = create_block("match.end", function);

        // Generate arms as a chain of comparisons
        llvm::BasicBlock *current_test_block = builder().GetInsertBlock();

        for (size_t i = 0; i < node->arms().size(); ++i)
        {
            auto *arm = node->arms()[i].get();
            if (!arm)
                continue;

            bool is_last = (i == node->arms().size() - 1);

            // Create blocks for this arm
            llvm::BasicBlock *arm_block = create_block("match.arm." + std::to_string(i), function);
            llvm::BasicBlock *next_test_block = is_last
                                                    ? end_block
                                                    : create_block("match.test." + std::to_string(i + 1), function);

            builder().SetInsertPoint(current_test_block);

            // Generate pattern match - supports multiple patterns with '|' syntax
            const auto &patterns = arm->patterns();
            if (!patterns.empty())
            {
                // Generate comparisons for all patterns and combine with OR
                llvm::Value *combined_match = nullptr;
                bool has_wildcard = false;

                for (const auto &pattern : patterns)
                {
                    if (!pattern)
                        continue;

                    // Check if this is a wildcard pattern
                    if (pattern->is_wildcard())
                    {
                        has_wildcard = true;
                        break; // Wildcard matches everything, no need to check other patterns
                    }

                    llvm::Value *matches = generate_pattern_match(match_value, pattern.get());
                    if (matches)
                    {
                        if (combined_match)
                        {
                            // OR with previous matches
                            combined_match = builder().CreateOr(combined_match, matches, "match.or");
                        }
                        else
                        {
                            combined_match = matches;
                        }
                    }
                    else
                    {
                        // Pattern match returned null (wildcard behavior)
                        has_wildcard = true;
                        break;
                    }
                }

                if (has_wildcard)
                {
                    // Wildcard/default pattern - always matches
                    builder().CreateBr(arm_block);
                }
                else if (combined_match)
                {
                    builder().CreateCondBr(combined_match, arm_block, next_test_block);
                }
                else
                {
                    // No valid pattern match generated - skip this arm
                    builder().CreateBr(next_test_block);
                }
            }
            else
            {
                // No patterns - always matches (wildcard)
                builder().CreateBr(arm_block);
            }

            // Generate arm body
            builder().SetInsertPoint(arm_block);
            enter_scope(arm_block);

            // Bind pattern variables before generating body (use first pattern for bindings)
            Cryo::PatternNode *first_pattern = arm->pattern();
            if (first_pattern)
            {
                bind_enum_pattern_variables(match_value, first_pattern, node->expr());
            }

            if (arm->body())
            {
                CodegenVisitor *visitor = ctx().visitor();
                arm->body()->accept(*visitor);
            }
            exit_scope();

            // Branch to end
            llvm::BasicBlock *arm_current = builder().GetInsertBlock();
            if (arm_current && !arm_current->getTerminator())
            {
                builder().CreateBr(end_block);
            }

            current_test_block = next_test_block;
        }

        // Continue at end block
        builder().SetInsertPoint(end_block);
    }

    llvm::Value *ControlFlowCodegen::generate_pattern_match(llvm::Value *value, Cryo::PatternNode *pattern)
    {
        if (!value || !pattern)
            return nullptr;

        // Handle different pattern types based on pattern_type()
        switch (pattern->pattern_type())
        {
        case Cryo::PatternNode::PatternType::Literal:
        {
            // Generate literal value
            if (Cryo::LiteralNode *lit = pattern->literal_value())
            {
                llvm::Value *pattern_val = generate_expression(lit);
                if (!pattern_val)
                    return nullptr;

                if (value->getType()->isIntegerTy() && pattern_val->getType()->isIntegerTy())
                {
                    // Ensure both operands have the same integer type for ICmp
                    llvm::Type *value_type = value->getType();
                    llvm::Type *pattern_type = pattern_val->getType();

                    if (value_type != pattern_type)
                    {
                        unsigned value_bits = value_type->getIntegerBitWidth();
                        unsigned pattern_bits = pattern_type->getIntegerBitWidth();

                        if (pattern_bits > value_bits)
                        {
                            // Truncate pattern to match value's smaller type
                            pattern_val = builder().CreateTrunc(pattern_val, value_type, "pattern.trunc");
                        }
                        else
                        {
                            // Extend pattern to match value's larger type
                            // Use ZExt for unsigned comparison (most common in match)
                            pattern_val = builder().CreateZExt(pattern_val, value_type, "pattern.zext");
                        }
                    }

                    return builder().CreateICmpEQ(value, pattern_val, "pattern.match");
                }
                else if (value->getType()->isFloatingPointTy() && pattern_val->getType()->isFloatingPointTy())
                {
                    return builder().CreateFCmpOEQ(value, pattern_val, "pattern.match");
                }
                else if (value->getType()->isPointerTy() && pattern_val->getType()->isPointerTy())
                {
                    return builder().CreateICmpEQ(value, pattern_val, "pattern.match");
                }
            }
            break;
        }
        case Cryo::PatternNode::PatternType::Identifier:
            // Identifier pattern binds the value - always matches
            // Could store value in named variable here
            return nullptr;

        case Cryo::PatternNode::PatternType::Wildcard:
            // Wildcard always matches
            return nullptr;

        case Cryo::PatternNode::PatternType::Enum:
        {
            // Enum pattern - compare discriminant
            auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(pattern);
            if (!enum_pattern)
                return nullptr;

            std::string enum_name = enum_pattern->enum_name();
            std::string variant_name = enum_pattern->variant_name();

            // Check if we're inside a generic instantiation context and need to redirect
            // the enum type name. For example, "Option" -> "Option_string"
            auto *visitor = ctx().visitor();
            if (visitor)
            {
                auto *generics = visitor->get_generics();
                if (generics && generics->in_type_param_scope())
                {
                    std::string redirected = generics->get_instantiated_scope_name(enum_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_pattern_match: Redirecting enum type {} -> {} in generic context",
                                  enum_name, redirected);
                        enum_name = redirected;
                    }
                }
            }

            // Build candidate variant names for lookup
            std::vector<std::string> variant_candidates = {
                enum_name + "::" + variant_name,
                variant_name,
                enum_pattern->enum_name() + "::" + variant_name};

            // Look up the expected discriminant value
            llvm::Value *expected_discriminant = nullptr;
            auto &variants_map = ctx().enum_variants_map();
            for (const auto &candidate : variant_candidates)
            {
                auto it = variants_map.find(candidate);
                if (it != variants_map.end())
                {
                    expected_discriminant = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_pattern_match: Found variant '{}' discriminant", candidate);
                    break;
                }
            }

            if (!expected_discriminant)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_pattern_match: Could not find discriminant for {}::{}",
                          enum_name, variant_name);
                return nullptr; // Fall back to wildcard
            }

            // Get the struct type and extract discriminant from the match value
            llvm::Type *value_type = value->getType();
            llvm::Value *discriminant_value = nullptr;

            if (value_type->isStructTy())
            {
                // Value is by-value struct, extract element 0
                discriminant_value = builder().CreateExtractValue(value, 0, "enum.discriminant");
            }
            else if (value_type->isPointerTy())
            {
                // Value is a pointer to struct, need to GEP and load
                llvm::Type *resolved_type = ctx().get_type(enum_name);
                if (!resolved_type || !resolved_type->isStructTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_pattern_match: Could not resolve struct type for '{}'", enum_name);
                    return nullptr;
                }

                llvm::StructType *struct_type = llvm::cast<llvm::StructType>(resolved_type);
                llvm::Value *discriminant_ptr = builder().CreateStructGEP(struct_type, value, 0, "discriminant.ptr");
                discriminant_value = builder().CreateLoad(builder().getInt32Ty(), discriminant_ptr, "enum.discriminant");
            }
            else if (value_type->isIntegerTy())
            {
                // Value is a plain integer (simple enum without associated data)
                // The value IS the discriminant
                discriminant_value = value;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_pattern_match: Simple integer enum, using value directly as discriminant");
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_pattern_match: Unexpected value type for enum match");
                return nullptr;
            }

            // Ensure both values have the same type before comparison
            // The expected_discriminant is always i32 (from TypeCodegen), but discriminant_value
            // might be a different integer size depending on how the field was loaded
            if (discriminant_value->getType() != expected_discriminant->getType())
            {
                llvm::Type *disc_type = discriminant_value->getType();
                llvm::Type *exp_type = expected_discriminant->getType();

                if (disc_type->isIntegerTy() && exp_type->isIntegerTy())
                {
                    unsigned disc_bits = disc_type->getIntegerBitWidth();
                    unsigned exp_bits = exp_type->getIntegerBitWidth();

                    // Cast to the larger type to avoid data loss
                    if (disc_bits < exp_bits)
                    {
                        discriminant_value = builder().CreateZExt(discriminant_value, exp_type, "disc.zext");
                    }
                    else if (disc_bits > exp_bits)
                    {
                        // Cast expected_discriminant to match discriminant_value's type
                        expected_discriminant = llvm::ConstantInt::get(
                            disc_type,
                            llvm::cast<llvm::ConstantInt>(expected_discriminant)->getZExtValue());
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_pattern_match: Type mismatch between discriminant ({}) and expected ({})",
                              disc_type->getTypeID(), exp_type->getTypeID());
                    return nullptr;
                }
            }

            // Compare discriminant with expected value
            return builder().CreateICmpEQ(discriminant_value, expected_discriminant, "enum.match");
        }

        case Cryo::PatternNode::PatternType::Range:
        {
            // Range pattern - check if value is within range [start, end]
            // e.g., 'a'..'z' matches any char from 'a' to 'z'
            Cryo::LiteralNode *start_lit = pattern->range_start();
            Cryo::LiteralNode *end_lit = pattern->range_end();

            if (!start_lit || !end_lit)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_pattern_match: Range pattern missing start or end literal");
                return nullptr;
            }

            llvm::Value *start_val = generate_expression(start_lit);
            llvm::Value *end_val = generate_expression(end_lit);

            if (!start_val || !end_val)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_pattern_match: Failed to generate range boundary values");
                return nullptr;
            }

            // Ensure types match for comparison
            llvm::Type *value_type = value->getType();

            if (value_type->isIntegerTy())
            {
                // Cast range boundaries to match value type if needed
                if (start_val->getType() != value_type)
                {
                    unsigned value_bits = value_type->getIntegerBitWidth();
                    unsigned start_bits = start_val->getType()->getIntegerBitWidth();
                    if (start_bits > value_bits)
                        start_val = builder().CreateTrunc(start_val, value_type, "range.start.trunc");
                    else if (start_bits < value_bits)
                        start_val = builder().CreateZExt(start_val, value_type, "range.start.zext");
                }

                if (end_val->getType() != value_type)
                {
                    unsigned value_bits = value_type->getIntegerBitWidth();
                    unsigned end_bits = end_val->getType()->getIntegerBitWidth();
                    if (end_bits > value_bits)
                        end_val = builder().CreateTrunc(end_val, value_type, "range.end.trunc");
                    else if (end_bits < value_bits)
                        end_val = builder().CreateZExt(end_val, value_type, "range.end.zext");
                }

                // Generate: value >= start && value <= end
                llvm::Value *ge_start = builder().CreateICmpSGE(value, start_val, "range.ge.start");
                llvm::Value *le_end = builder().CreateICmpSLE(value, end_val, "range.le.end");
                return builder().CreateAnd(ge_start, le_end, "range.match");
            }
            else if (value_type->isFloatingPointTy())
            {
                // For floating point ranges
                llvm::Value *ge_start = builder().CreateFCmpOGE(value, start_val, "range.ge.start");
                llvm::Value *le_end = builder().CreateFCmpOLE(value, end_val, "range.le.end");
                return builder().CreateAnd(ge_start, le_end, "range.match");
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_pattern_match: Unsupported type for range pattern");
            return nullptr;
        }
        }

        // Default: wildcard behavior
        return nullptr;
    }

    void ControlFlowCodegen::bind_enum_pattern_variables(llvm::Value *value, Cryo::PatternNode *pattern,
                                                         Cryo::ExpressionNode *match_expr)
    {
        if (!value || !pattern)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ControlFlowCodegen: bind_enum_pattern_variables called");

        // Handle identifier patterns (simple variable binding)
        if (pattern->pattern_type() == Cryo::PatternNode::PatternType::Identifier)
        {
            std::string var_name = pattern->identifier();
            if (!var_name.empty() && var_name != "_")
            {
                // The matched value becomes the bound variable
                // Create an alloca for the variable and store the value
                llvm::Type *value_type = value->getType();
                llvm::AllocaInst *alloca = create_entry_alloca(value_type, var_name);
                if (alloca)
                {
                    builder().CreateStore(value, alloca);
                    // Register in scope so it can be looked up later
                    values().set_value(var_name, alloca, alloca, value_type);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Bound identifier '{}' to value", var_name);
                }
            }
            return;
        }

        // Handle enum patterns with bindings (e.g., Result::Ok(d))
        auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(pattern);
        if (!enum_pattern)
            return;

        const auto &elements = enum_pattern->pattern_elements();
        if (elements.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "bind_enum_pattern_variables: No pattern elements in {}::{}",
                      enum_pattern->enum_name(), enum_pattern->variant_name());
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "bind_enum_pattern_variables: Processing {}::{} with {} pattern elements",
                  enum_pattern->enum_name(), enum_pattern->variant_name(), elements.size());

        // The match value should be an enum struct with layout: { i32 discriminant, [N x i8] payload }
        // We need to extract payload fields for each binding

        llvm::Type *value_type = value->getType();

        // If value is not a struct or not a pointer to struct, we can't extract fields
        llvm::StructType *struct_type = nullptr;
        llvm::Value *base_ptr = nullptr;

        if (value_type->isStructTy())
        {
            struct_type = llvm::cast<llvm::StructType>(value_type);
            // Value is by-value, we need to create an alloca to get a pointer for GEP
            base_ptr = create_entry_alloca(struct_type, "enum_match_temp");
            if (!base_ptr)
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "bind_enum_pattern_variables: Failed to create alloca for enum match temp (struct: {})",
                          struct_type->hasName() ? struct_type->getName().str() : "<unnamed>");
                return;
            }
            builder().CreateStore(value, base_ptr);
        }
        else if (value_type->isPointerTy())
        {
            // Value is already a pointer, use it directly
            base_ptr = value;
            // Try to get the struct type from the pointer
            // In opaque pointer mode, we need to look it up by name

            std::string type_name;

            // First, try to get the type name from the match expression's resolved type
            // This gives us the correct instantiated name like "Result_i8_ConversionError"
            if (match_expr)
            {
                TypeRef resolved_type_ref = match_expr->get_resolved_type();
                if (resolved_type_ref.is_valid())
                {
                    // Handle InstantiatedType - use the resolved (monomorphized) type
                    if (resolved_type_ref->kind() == Cryo::TypeKind::InstantiatedType)
                    {
                        auto *inst_type = static_cast<const Cryo::InstantiatedType *>(resolved_type_ref.get());
                        if (inst_type->has_resolved_type())
                        {
                            TypeRef concrete_type = inst_type->resolved_type();
                            if (concrete_type.is_valid())
                            {
                                type_name = concrete_type->display_name();
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "bind_enum_pattern_variables: Using InstantiatedType's resolved name: {}",
                                          type_name);
                            }
                        }
                        else
                        {
                            // No resolved_type, use display_name of InstantiatedType itself
                            type_name = resolved_type_ref->display_name();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "bind_enum_pattern_variables: Using InstantiatedType display name: {}",
                                      type_name);
                        }
                    }
                    else
                    {
                        type_name = resolved_type_ref->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "bind_enum_pattern_variables: Using resolved type display name: {}",
                                  type_name);
                    }
                }
            }

            // Fallback to pattern's enum name with generic redirection
            if (type_name.empty())
            {
                type_name = enum_pattern->enum_name();

                // Check if we're inside a generic instantiation context and need to redirect
                // the type name. For example, "Option" -> "Option_string"
                auto *visitor = ctx().visitor();
                if (visitor)
                {
                    auto *generics = visitor->get_generics();
                    if (generics && generics->in_type_param_scope())
                    {
                        std::string redirected = generics->get_instantiated_scope_name(type_name);
                        if (!redirected.empty())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "bind_enum_pattern_variables: Redirecting enum type {} -> {} in generic context",
                                      type_name, redirected);
                            type_name = redirected;
                        }
                    }
                }
            }

            // Convert type name format if needed (e.g., "Result<i8, ConversionError>" -> "Result_i8_ConversionError")
            // First try exact match
            llvm::Type *resolved_type = ctx().get_type(type_name);
            if (!resolved_type || !resolved_type->isStructTy())
            {
                // Try converting angle bracket format to underscore format
                std::string mangled_name = type_name;
                for (char &c : mangled_name)
                {
                    if (c == '<' || c == '>' || c == ',' || c == ' ')
                        c = '_';
                }
                // Remove trailing underscores
                while (!mangled_name.empty() && mangled_name.back() == '_')
                    mangled_name.pop_back();

                if (mangled_name != type_name)
                {
                    resolved_type = ctx().get_type(mangled_name);
                    if (resolved_type && resolved_type->isStructTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "bind_enum_pattern_variables: Found type via mangled name '{}' (original: '{}')",
                                  mangled_name, type_name);
                    }
                }
            }

            if (resolved_type && resolved_type->isStructTy())
            {
                struct_type = llvm::cast<llvm::StructType>(resolved_type);
            }
        }

        if (!struct_type || struct_type->getNumElements() < 2)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "bind_enum_pattern_variables: Cannot determine struct type for enum, trying fallback");
            // Fallback: just bind the value directly for single bindings
            if (elements.size() == 1 && elements[0].is_binding())
            {
                const std::string &var_name = elements[0].binding_name;
                if (!var_name.empty() && var_name != "_")
                {
                    llvm::AllocaInst *alloca = create_entry_alloca(value_type, var_name);
                    if (alloca)
                    {
                        builder().CreateStore(value, alloca);
                        values().set_value(var_name, alloca, alloca, value_type);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "bind_enum_pattern_variables: Fallback - bound '{}' to match value", var_name);
                    }
                }
            }
            return;
        }

        // Get pointer to payload (index 1 in the enum struct)
        llvm::Value *payload_ptr = builder().CreateStructGEP(struct_type, base_ptr, 1, "payload_ptr");

        // Look up the EnumType to get the variant's actual payload types
        std::vector<TypeRef> payload_types;
        std::string variant_name = enum_pattern->variant_name();

        // Use the LLVM struct type's name for lookup - this gives us the instantiated name
        // (e.g., "Option<(String,String)>" or "Option_u64") instead of just "Option"
        std::string enum_name = struct_type->getName().str();

        // Try lookup with the full instantiated name first
        TypeRef enum_type_ref = ctx().symbols().arena().lookup_type_by_name(enum_name);

        // If not found, the name might be in a different format (mangled vs display)
        // Try converting between formats
        if (!enum_type_ref.is_valid())
        {
            // Try mangling the name if it has angle brackets
            std::string mangled_name = enum_name;
            size_t angle_pos = mangled_name.find('<');
            if (angle_pos != std::string::npos)
            {
                // Convert "Option<T>" format to "Option_T" format
                for (char &c : mangled_name)
                {
                    if (c == '<' || c == '>' || c == ',' || c == ' ')
                        c = '_';
                }
                // Remove trailing underscores
                while (!mangled_name.empty() && mangled_name.back() == '_')
                    mangled_name.pop_back();
                enum_type_ref = ctx().symbols().arena().lookup_type_by_name(mangled_name);
                if (enum_type_ref.is_valid())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Found type via mangled name '{}' (original: '{}')",
                              mangled_name, enum_name);
                }
            }
        }

        // Last resort: try the pattern's enum name (base name like "Option")
        if (!enum_type_ref.is_valid())
        {
            std::string base_enum_name = enum_pattern->enum_name();
            enum_type_ref = ctx().symbols().arena().lookup_type_by_name(base_enum_name);
            if (enum_type_ref.is_valid())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "bind_enum_pattern_variables: Found type via base name '{}' (LLVM name was: '{}')",
                          base_enum_name, enum_name);
            }
        }

        // Handle non-generic enum types
        if (enum_type_ref.is_valid() && enum_type_ref->kind() == TypeKind::Enum)
        {
            auto *enum_type = static_cast<const EnumType *>(enum_type_ref.get());
            if (const EnumVariant *variant = enum_type->get_variant(variant_name))
            {
                // Check if we're in a generic context and need to substitute type parameters
                auto *visitor = ctx().visitor();
                auto *generics = visitor ? visitor->get_generics() : nullptr;

                if (generics && generics->in_type_param_scope())
                {
                    // Substitute generic params using the current instantiation context
                    for (const auto &pt : variant->payload_types)
                    {
                        TypeRef substituted = generics->substitute_type_params(pt);
                        payload_types.push_back(substituted.is_valid() ? substituted : pt);
                    }
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Found variant {}::{} with {} payload types (substituted in generic context)",
                              enum_name, variant_name, payload_types.size());
                }
                else
                {
                    payload_types = variant->payload_types;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Found variant {}::{} with {} payload types",
                              enum_name, variant_name, payload_types.size());
                }
            }
        }
        // Also check for instantiated enums like Option<Entry<String, i32>>
        else if (enum_type_ref.is_valid() && enum_type_ref->kind() == TypeKind::InstantiatedType)
        {
            auto *inst_type = static_cast<const InstantiatedType *>(enum_type_ref.get());
            TypeRef base = inst_type->generic_base();
            if (base.is_valid() && base->kind() == TypeKind::Enum)
            {
                auto *enum_type = static_cast<const EnumType *>(base.get());
                if (const EnumVariant *variant = enum_type->get_variant(variant_name))
                {
                    // Substitute generic params in payload types
                    const auto &type_args = inst_type->type_args();
                    for (const auto &pt : variant->payload_types)
                    {
                        TypeRef substituted = ctx().types().substitute_generic_param(pt, type_args);
                        payload_types.push_back(substituted);
                    }
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Found instantiated variant {}::{} with {} substituted payload types",
                              enum_name, variant_name, payload_types.size());
                }
            }
        }

        // Fallback: use variant field types registered by GenericCodegen during instantiation.
        // This handles generic enum variants when no InstantiatedType is in the TypeArena
        // and we're outside the generic type parameter scope (e.g., user code matching on Option<IoError>).
        if (payload_types.empty())
        {
            std::string qualified_variant = enum_name + "::" + variant_name;
            const auto *field_types = ctx().get_enum_variant_fields(qualified_variant);
            if (field_types && !field_types->empty())
            {
                auto &arena = ctx().symbols().arena();
                for (const auto &field_type_name : *field_types)
                {
                    // Try looking up as a user-defined type first
                    TypeRef field_ref = arena.lookup_type_by_name(field_type_name);
                    if (!field_ref.is_valid())
                    {
                        // Try common primitive types
                        if (field_type_name == "i8") field_ref = arena.get_i8();
                        else if (field_type_name == "i16") field_ref = arena.get_i16();
                        else if (field_type_name == "i32" || field_type_name == "int") field_ref = arena.get_i32();
                        else if (field_type_name == "i64") field_ref = arena.get_i64();
                        else if (field_type_name == "i128") field_ref = arena.get_i128();
                        else if (field_type_name == "u8") field_ref = arena.get_u8();
                        else if (field_type_name == "u16") field_ref = arena.get_u16();
                        else if (field_type_name == "u32") field_ref = arena.get_u32();
                        else if (field_type_name == "u64") field_ref = arena.get_u64();
                        else if (field_type_name == "u128") field_ref = arena.get_u128();
                        else if (field_type_name == "f32" || field_type_name == "float") field_ref = arena.get_f32();
                        else if (field_type_name == "f64" || field_type_name == "double") field_ref = arena.get_f64();
                        else if (field_type_name == "bool" || field_type_name == "boolean") field_ref = arena.get_bool();
                        else if (field_type_name == "string") field_ref = arena.get_string();
                        else if (field_type_name == "char") field_ref = arena.get_char();
                        else if (field_type_name == "void") field_ref = arena.get_void();
                        else if (field_type_name == "void*" || field_type_name == "voidp")
                            field_ref = arena.get_pointer_to(arena.get_void());
                        // Handle pointer types to user-defined types (e.g., "BinaryExpr*")
                        else if (field_type_name.size() > 1 && field_type_name.back() == '*')
                        {
                            std::string base_name = field_type_name.substr(0, field_type_name.size() - 1);
                            TypeRef base_type = arena.lookup_type_by_name(base_name);
                            if (base_type.is_valid())
                            {
                                field_ref = arena.get_pointer_to(base_type);
                            }
                            else
                            {
                                // Try with namespace prefix
                                std::string ns_context = ctx().namespace_context();
                                if (!ns_context.empty())
                                {
                                    base_type = arena.lookup_type_by_name(ns_context + "::" + base_name);
                                    if (base_type.is_valid())
                                    {
                                        field_ref = arena.get_pointer_to(base_type);
                                    }
                                }
                            }
                        }
                        // Handle reference types (e.g., "&BinaryExpr")
                        else if (field_type_name.size() > 1 && field_type_name.front() == '&')
                        {
                            std::string base_name = field_type_name.substr(1);
                            TypeRef base_type = arena.lookup_type_by_name(base_name);
                            if (base_type.is_valid())
                            {
                                field_ref = arena.get_reference_to(base_type, Cryo::RefMutability::Immutable);
                            }
                            else
                            {
                                // Try with namespace prefix
                                std::string ns_context = ctx().namespace_context();
                                if (!ns_context.empty())
                                {
                                    base_type = arena.lookup_type_by_name(ns_context + "::" + base_name);
                                    if (base_type.is_valid())
                                    {
                                        field_ref = arena.get_reference_to(base_type, Cryo::RefMutability::Immutable);
                                    }
                                }
                            }
                        }
                    }
                    if (field_ref.is_valid())
                    {
                        payload_types.push_back(field_ref);
                    }
                }
                if (!payload_types.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Found {} payload types for {}::{} via variant field registry",
                              payload_types.size(), enum_name, variant_name);
                }
            }
        }

        // Extract fields from payload for each binding
        // The payload is typically a byte array, and fields are at specific offsets

        size_t offset = 0;
        size_t binding_index = 0;
        size_t payload_index = 0;

        for (const auto &element : elements)
        {
            if (element.is_binding())
            {
                const std::string &var_name = element.binding_name;
                if (var_name.empty() || var_name == "_")
                {
                    // Skip wildcards
                    offset += 8; // Default pointer size
                    payload_index++;
                    continue;
                }

                // Get the payload type for this binding if available
                TypeRef payload_type_ref;
                llvm::Type *field_type = nullptr;

                if (payload_index < payload_types.size())
                {
                    payload_type_ref = payload_types[payload_index];
                    field_type = ctx().types().map(payload_type_ref);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Binding '{}' has type '{}'",
                              var_name, payload_type_ref.is_valid() ? payload_type_ref->display_name() : "unknown");
                }

                // Default to i64 if we couldn't determine the type
                if (!field_type)
                {
                    field_type = llvm::Type::getInt64Ty(llvm_ctx());
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Binding '{}' defaulting to i64 (no type info)",
                              var_name);
                }

                // Calculate pointer to this field in payload
                // Using i8* arithmetic for byte offsets
                llvm::Type *i8_type = llvm::Type::getInt8Ty(llvm_ctx());
                llvm::Value *field_ptr = builder().CreateConstGEP1_32(i8_type, payload_ptr, offset, var_name + "_ptr");

                // Load the value
                llvm::Value *field_value = builder().CreateLoad(field_type, field_ptr, var_name + "_val");

                // Create alloca and store
                llvm::AllocaInst *alloca = create_entry_alloca(field_type, var_name);
                if (alloca)
                {
                    builder().CreateStore(field_value, alloca);
                    values().set_value(var_name, alloca, alloca, field_type);

                    // Also register the TypeRef in variable_types_map for member resolution
                    if (payload_type_ref.is_valid())
                    {
                        ctx().variable_types_map()[var_name] = payload_type_ref;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "bind_enum_pattern_variables: Registered TypeRef for '{}': {}",
                                  var_name, payload_type_ref->display_name());
                    }

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "bind_enum_pattern_variables: Bound '{}' from payload at offset {}",
                              var_name, offset);
                }

                // Compute offset for next field based on field type size
                size_t field_size = field_type ? ctx().types().size_of(field_type) : 8;
                offset += field_size;
                payload_index++;
            }
            else if (element.is_wildcard())
            {
                // Wildcard - skip this position
                if (payload_index < payload_types.size())
                {
                    llvm::Type *field_type = ctx().types().map(payload_types[payload_index]);
                    offset += field_type ? ctx().types().size_of(field_type) : 8;
                }
                else
                {
                    offset += 8;
                }
                payload_index++;
            }
            else if (element.is_literal())
            {
                // Literal - skip (already matched in generate_pattern_match)
                offset += 8;
            }

            binding_index++;
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

        // The scope cleanup is handled automatically by scope guards

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

        // The scope cleanup is handled automatically by scope guards

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

        // Get the current function's return type
        llvm::Function *current_fn = builder().GetInsertBlock()
                                         ? builder().GetInsertBlock()->getParent()
                                         : nullptr;
        llvm::Type *expected_ret_type = current_fn ? current_fn->getReturnType() : nullptr;

        if (node && node->expression())
        {
            llvm::Value *ret_val = generate_expression(node->expression());
            if (ret_val)
            {
                // Cast return value to match function return type if needed
                if (expected_ret_type && ret_val->getType() != expected_ret_type)
                {
                    // Special case: if function expects void but we have a value, just return void
                    // This handles cases like Result<void>::unwrap() where the return type becomes void
                    // but the method body still tries to return the value
                    if (expected_ret_type->isVoidTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                "Return type is void but expression produced a value. Using void return.");
                        builder().CreateRetVoid();
                        return;
                    }

                    // Special case: if function expects struct but we have void, create null value
                    if (expected_ret_type->isStructTy() && ret_val->getType()->isVoidTy())
                    {
                        LOG_WARN(Cryo::LogComponent::CODEGEN,
                                "Return type mismatch: expected struct but got void. Using null struct.");
                        ret_val = llvm::Constant::getNullValue(expected_ret_type);
                    }
                    // Special case: if function expects struct but we have a pointer, load the struct
                    else if (expected_ret_type->isStructTy() && ret_val->getType()->isPointerTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                 "Return value is pointer but function expects struct by value. Loading struct.");
                        ret_val = builder().CreateLoad(expected_ret_type, ret_val, "ret.load");
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                 "Return type mismatch: expected {}, got {}. Attempting cast.",
                                 expected_ret_type->isVoidTy() ? "void" : "non-void",
                                 ret_val->getType()->isStructTy() ? "struct" : "non-struct");

                        ret_val = cast_if_needed(ret_val, expected_ret_type);

                        // If cast failed and returned null, use a default value instead
                        if (!ret_val)
                        {
                            LOG_WARN(Cryo::LogComponent::CODEGEN,
                                    "Cast failed in return statement, using default return value");
                            ret_val = llvm::Constant::getNullValue(expected_ret_type);
                        }
                    }
                }
                if (ret_val)
                {
                    builder().CreateRet(ret_val);
                }
                else
                {
                    // Cast failed, use default value
                    if (expected_ret_type && !expected_ret_type->isVoidTy())
                    {
                        builder().CreateRet(llvm::Constant::getNullValue(expected_ret_type));
                    }
                    else
                    {
                        builder().CreateRetVoid();
                    }
                }
            }
            else
            {
                // Expression generation failed - return default value
                if (expected_ret_type && !expected_ret_type->isVoidTy())
                {
                    builder().CreateRet(llvm::Constant::getNullValue(expected_ret_type));
                }
                else
                {
                    builder().CreateRetVoid();
                }
            }
        }
        else
        {
            // No expression - void return or default value
            if (expected_ret_type && !expected_ret_type->isVoidTy())
            {
                builder().CreateRet(llvm::Constant::getNullValue(expected_ret_type));
            }
            else
            {
                builder().CreateRetVoid();
            }
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
        CodegenVisitor *visitor = ctx().visitor();
        stmt->accept(*visitor);
        exit_scope();
    }

    llvm::Value *ControlFlowCodegen::generate_expression(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Use visitor pattern through context
        CodegenVisitor *visitor = ctx().visitor();
        ctx().set_result(nullptr);
        expr->accept(*visitor);
        return get_result();
    }

    void ControlFlowCodegen::enter_scope(llvm::BasicBlock *block)
    {
        // Enter both scope manager and value context for variable tracking
        if (_scope_manager)
        {
            _scope_manager->enter_scope(block);
        }
        values().enter_scope(block ? block->getName().str() : "scope");
    }

    void ControlFlowCodegen::exit_scope()
    {
        // Exit both scope manager and value context
        values().exit_scope();
        if (_scope_manager)
        {
            _scope_manager->exit_scope();
        }
    }

} // namespace Cryo::Codegen
