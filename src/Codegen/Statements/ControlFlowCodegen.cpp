#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "AST/Type.hpp"
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

        llvm::Function *function = builder().GetInsertBlock()->getParent();
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

            // Generate pattern match
            Cryo::PatternNode *pattern = arm->pattern();
            if (pattern)
            {
                // Generate comparison based on pattern type
                llvm::Value *matches = generate_pattern_match(match_value, pattern);
                if (matches)
                {
                    builder().CreateCondBr(matches, arm_block, next_test_block);
                }
                else
                {
                    // Wildcard/default pattern - always matches
                    builder().CreateBr(arm_block);
                }
            }
            else
            {
                // No pattern - always matches (wildcard)
                builder().CreateBr(arm_block);
            }

            // Generate arm body
            builder().SetInsertPoint(arm_block);
            enter_scope(arm_block);

            // Bind pattern variables if this is an enum pattern
            if (pattern && pattern->pattern_type() == Cryo::PatternNode::PatternType::Enum)
            {
                bind_enum_pattern_variables(match_value, pattern);
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
            // Cast to EnumPatternNode to get variant info
            auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(pattern);
            if (!enum_pattern)
                return nullptr;

            std::string enum_name = enum_pattern->enum_name();
            std::string variant_name = enum_pattern->variant_name();
            std::string qualified_variant = enum_name + "::" + variant_name;

            // Look up the discriminant value for this variant
            auto &enum_variants = ctx().enum_variants_map();
            auto it = enum_variants.find(qualified_variant);
            if (it == enum_variants.end())
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Unknown enum variant in pattern: {}", qualified_variant);
                return nullptr;
            }

            llvm::Value *expected_discriminant = it->second;

            // Check if value is a tagged union struct or simple enum
            if (value->getType()->isStructTy())
            {
                // Tagged union - extract discriminant from first field
                llvm::Value *discriminant = builder().CreateExtractValue(value, 0, "discriminant");
                return builder().CreateICmpEQ(discriminant, expected_discriminant, "pattern.match");
            }
            else if (value->getType()->isIntegerTy())
            {
                // Simple enum - compare directly
                return builder().CreateICmpEQ(value, expected_discriminant, "pattern.match");
            }

            return nullptr;
        }
        }

        // Default: wildcard behavior
        return nullptr;
    }

    void ControlFlowCodegen::bind_enum_pattern_variables(llvm::Value *value, Cryo::PatternNode *pattern)
    {
        auto *enum_pattern = dynamic_cast<Cryo::EnumPatternNode *>(pattern);
        if (!enum_pattern)
            return;

        const auto &bound_vars = enum_pattern->bound_variables();
        if (bound_vars.empty())
            return;

        std::string enum_name = enum_pattern->enum_name();
        std::string variant_name = enum_pattern->variant_name();

        // Get the enum type information from the type context
        Cryo::Type *cryo_enum_type = symbols().get_type_context()->lookup_enum_type(enum_name);
        if (!cryo_enum_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "Cannot find enum type '{}' for pattern binding", enum_name);
            return;
        }

        auto *enum_type = dynamic_cast<Cryo::EnumType *>(cryo_enum_type);
        if (!enum_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "Type '{}' is not an enum type", enum_name);
            return;
        }

        // Get variant metadata to find field types
        const Cryo::EnumVariant *variant_info = enum_type->get_variant_info(variant_name);
        if (!variant_info)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "Cannot find variant info for {}", variant_name);
            return;
        }

        // The value should be a tagged union struct
        if (!value->getType()->isStructTy())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "Expected struct type for enum pattern binding");
            return;
        }

        // Extract the payload (field 1 of the struct)
        llvm::Value *payload = builder().CreateExtractValue(value, 1, "payload");

        // Bind each variable by extracting from the payload
        size_t offset = 0;
        for (size_t i = 0; i < bound_vars.size() && i < variant_info->field_types.size(); ++i)
        {
            const std::string &var_name = bound_vars[i];
            Cryo::Type *field_cryo_type = variant_info->field_types[i].get();

            // Get the LLVM type for this field
            llvm::Type *field_type = types().map(field_cryo_type);
            if (!field_type)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Unknown type for pattern variable '{}'", var_name);
                continue;
            }

            // The payload is [N x i8], we need to extract from the right offset
            // First get a pointer to the payload array
            llvm::Type *struct_type = value->getType();
            llvm::Value *value_alloca = builder().CreateAlloca(struct_type, nullptr, "enum_tmp");
            builder().CreateStore(value, value_alloca);

            llvm::Value *payload_ptr = builder().CreateStructGEP(struct_type, value_alloca, 1, "payload_ptr");

            // Calculate field pointer with offset
            llvm::Value *field_ptr = builder().CreateConstGEP1_32(
                llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, offset, "field_ptr");

            // Cast to field type pointer and load
            field_ptr = builder().CreateBitCast(field_ptr, llvm::PointerType::get(field_type, 0));
            llvm::Value *field_value = builder().CreateLoad(field_type, field_ptr, var_name);

            // Create alloca for the variable and store the value
            llvm::AllocaInst *var_alloca = builder().CreateAlloca(field_type, nullptr, var_name + "_alloca");
            builder().CreateStore(field_value, var_alloca);

            // Register the variable in the current scope
            values().set_value(var_name, field_value, var_alloca, field_type);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Bound pattern variable '{}' from enum variant {}", var_name, variant_name);

            // Advance offset
            offset += module()->getDataLayout().getTypeAllocSize(field_type);
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
                    ret_val = cast_if_needed(ret_val, expected_ret_type);
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
