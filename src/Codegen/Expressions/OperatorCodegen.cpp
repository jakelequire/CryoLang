#include "Codegen/Expressions/OperatorCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    OperatorCodegen::OperatorCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Main Entry Points
    //===================================================================

    llvm::Value *OperatorCodegen::generate_binary(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, "Null binary expression node");
            return nullptr;
        }

        TokenKind op = node->operator_token().kind();

        // Assignment is special - doesn't evaluate LHS as a value
        if (op == TokenKind::TK_EQUAL)
        {
            return generate_assignment(node);
        }

        // TODO: Handle compound assignments (+=, -=, etc.) here

        // Generate operand values
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Failed to generate left operand");
            return nullptr;
        }

        // For logical operators, use short-circuit evaluation
        if (op == TokenKind::TK_AMPERSAND_AMPERSAND)
        {
            return generate_logical_and(node);
        }
        if (op == TokenKind::TK_PIPE_PIPE)
        {
            return generate_logical_or(node);
        }

        llvm::Value *rhs = generate_operand(node->right());
        if (!rhs)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Failed to generate right operand");
            return nullptr;
        }

        // Classify and dispatch
        BinaryOpClass op_class = classify_binary_op(op, lhs->getType());

        switch (op_class)
        {
        case BinaryOpClass::Arithmetic:
            return generate_arithmetic(op, lhs, rhs, node->resolved_type());

        case BinaryOpClass::Comparison:
            return generate_comparison(op, lhs, rhs, node->left()->resolved_type());

        case BinaryOpClass::Bitwise:
            return generate_bitwise(op, lhs, rhs);

        case BinaryOpClass::StringConcat:
            return generate_string_concat(lhs, rhs);

        default:
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Unsupported binary operator: " + node->operator_token().value());
            return nullptr;
        }
    }

    llvm::Value *OperatorCodegen::generate_unary(Cryo::UnaryExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, "Null unary expression node");
            return nullptr;
        }

        TokenKind op = node->operator_token().kind();
        std::string op_str = node->operator_token().value();

        // Handle increment/decrement specially (they modify lvalues)
        if (op == TokenKind::TK_PLUS_PLUS)
        {
            return generate_increment(node->operand(), node->is_prefix());
        }
        if (op == TokenKind::TK_MINUS_MINUS)
        {
            return generate_decrement(node->operand(), node->is_prefix());
        }

        // Handle address-of specially (doesn't evaluate operand)
        if (op == TokenKind::TK_AMPERSAND)
        {
            return generate_address_of(node->operand());
        }

        // Generate operand value for other operators
        llvm::Value *operand = generate_operand(node->operand());
        if (!operand)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, node,
                         "Failed to generate unary operand");
            return nullptr;
        }

        // Dispatch based on operator
        if (op == TokenKind::TK_MINUS)
        {
            return generate_negation(operand, node->operand()->resolved_type());
        }
        if (op == TokenKind::TK_BANG)
        {
            return generate_logical_not(operand);
        }
        if (op == TokenKind::TK_TILDE)
        {
            return generate_bitwise_not(operand);
        }
        if (op == TokenKind::TK_STAR)
        {
            return generate_dereference(operand, node->operand()->resolved_type());
        }

        report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, node,
                     "Unsupported unary operator: " + op_str);
        return nullptr;
    }

    //===================================================================
    // Operator Classification
    //===================================================================

    OperatorCodegen::BinaryOpClass OperatorCodegen::classify_binary_op(TokenKind op, llvm::Type *left_type)
    {
        switch (op)
        {
        // Assignment
        case TokenKind::TK_EQUAL:
        case TokenKind::TK_PLUS_EQUAL:
        case TokenKind::TK_MINUS_EQUAL:
        case TokenKind::TK_STAR_EQUAL:
        case TokenKind::TK_SLASH_EQUAL:
        case TokenKind::TK_PERCENT_EQUAL:
            return BinaryOpClass::Assignment;

        // Arithmetic
        case TokenKind::TK_PLUS:
            // Special case: + can be string concatenation
            if (left_type && left_type->isPointerTy())
            {
                // Could be string concatenation - caller needs to verify
                return BinaryOpClass::StringConcat;
            }
            return BinaryOpClass::Arithmetic;

        case TokenKind::TK_MINUS:
        case TokenKind::TK_STAR:
        case TokenKind::TK_SLASH:
        case TokenKind::TK_PERCENT:
            return BinaryOpClass::Arithmetic;

        // Comparison
        case TokenKind::TK_EQUAL_EQUAL:
        case TokenKind::TK_BANG_EQUAL:
        case TokenKind::TK_LESS_THAN:
        case TokenKind::TK_GREATER_THAN:
        case TokenKind::TK_LESS_THAN_EQUAL:
        case TokenKind::TK_GREATER_THAN_EQUAL:
            return BinaryOpClass::Comparison;

        // Logical
        case TokenKind::TK_AMPERSAND_AMPERSAND:
        case TokenKind::TK_PIPE_PIPE:
            return BinaryOpClass::Logical;

        // Bitwise
        case TokenKind::TK_AMPERSAND:
        case TokenKind::TK_PIPE:
        case TokenKind::TK_CARET:
        case TokenKind::TK_LESS_LESS:
        case TokenKind::TK_GREATER_GREATER:
            return BinaryOpClass::Bitwise;

        default:
            return BinaryOpClass::Unknown;
        }
    }

    //===================================================================
    // Assignment Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_assignment(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        Cryo::ExpressionNode *lhs = node->left();
        Cryo::ExpressionNode *rhs = node->right();

        // Dispatch based on LHS type
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(lhs))
        {
            return generate_identifier_assignment(identifier, rhs);
        }
        if (auto *member = dynamic_cast<Cryo::MemberAccessNode *>(lhs))
        {
            return generate_member_assignment(member, rhs);
        }
        if (auto *array_access = dynamic_cast<Cryo::ArrayAccessNode *>(lhs))
        {
            return generate_array_assignment(array_access, rhs);
        }
        if (auto *unary = dynamic_cast<Cryo::UnaryExpressionNode *>(lhs))
        {
            if (unary->operator_token().kind() == TokenKind::TK_STAR)
            {
                return generate_deref_assignment(unary, rhs);
            }
        }

        report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                     "Invalid left-hand side of assignment");
        return nullptr;
    }

    llvm::Value *OperatorCodegen::generate_identifier_assignment(Cryo::IdentifierNode *target,
                                                                   Cryo::ExpressionNode *value_node)
    {
        if (!target || !value_node)
            return nullptr;

        std::string var_name = target->name();

        // Find the variable's storage location
        llvm::Value *var_ptr = values().get_alloca(var_name);
        if (!var_ptr)
        {
            // Try global
            auto &globals = ctx().globals_map();
            auto it = globals.find(var_name);
            if (it != globals.end())
            {
                var_ptr = it->second;
            }
        }

        if (!var_ptr)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Undefined variable: " + var_name);
            return nullptr;
        }

        // Generate the value to assign
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Store the value
        if (_memory)
        {
            _memory->create_store(value, var_ptr);
        }
        else
        {
            builder().CreateStore(value, var_ptr);
        }

        return value;
    }

    llvm::Value *OperatorCodegen::generate_member_assignment(Cryo::MemberAccessNode *target,
                                                               Cryo::ExpressionNode *value_node)
    {
        if (!target || !value_node)
            return nullptr;

        // Get the address of the member field
        llvm::Value *member_ptr = get_lvalue_address(target);
        if (!member_ptr)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Failed to get member address for assignment");
            return nullptr;
        }

        // Generate the value to assign
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Store the value
        if (_memory)
        {
            _memory->create_store(value, member_ptr);
        }
        else
        {
            builder().CreateStore(value, member_ptr);
        }

        return value;
    }

    llvm::Value *OperatorCodegen::generate_array_assignment(Cryo::ArrayAccessNode *target,
                                                              Cryo::ExpressionNode *value_node)
    {
        if (!target || !value_node)
            return nullptr;

        // Get the address of the array element
        llvm::Value *element_ptr = get_lvalue_address(target);
        if (!element_ptr)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Failed to get array element address for assignment");
            return nullptr;
        }

        // Generate the value to assign
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Store the value
        if (_memory)
        {
            _memory->create_store(value, element_ptr);
        }
        else
        {
            builder().CreateStore(value, element_ptr);
        }

        return value;
    }

    llvm::Value *OperatorCodegen::generate_deref_assignment(Cryo::UnaryExpressionNode *target,
                                                              Cryo::ExpressionNode *value_node)
    {
        if (!target || !value_node)
            return nullptr;

        // Generate the pointer value
        llvm::Value *ptr = generate_operand(target->operand());
        if (!ptr)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Failed to generate pointer for dereference assignment");
            return nullptr;
        }

        if (!ptr->getType()->isPointerTy())
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Cannot dereference non-pointer type");
            return nullptr;
        }

        // Generate the value to assign
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Store the value through the pointer
        if (_memory)
        {
            _memory->create_store(value, ptr);
        }
        else
        {
            builder().CreateStore(value, ptr);
        }

        return value;
    }

    //===================================================================
    // Arithmetic Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_arithmetic(TokenKind op,
                                                        llvm::Value *lhs,
                                                        llvm::Value *rhs,
                                                        Cryo::Type *result_type)
    {
        if (!lhs || !rhs)
            return nullptr;

        // Ensure compatible types
        ensure_compatible_types(lhs, rhs);

        llvm::Type *type = lhs->getType();

        if (type->isIntegerTy())
        {
            bool is_signed = result_type ? is_signed_integer_type(result_type) : true;
            return generate_integer_arithmetic(op, lhs, rhs, is_signed);
        }

        if (type->isFloatingPointTy())
        {
            return generate_float_arithmetic(op, lhs, rhs);
        }

        report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                     "Arithmetic operations not supported for this type");
        return nullptr;
    }

    llvm::Value *OperatorCodegen::generate_integer_arithmetic(TokenKind op,
                                                                 llvm::Value *lhs,
                                                                 llvm::Value *rhs,
                                                                 bool is_signed)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_PLUS:
            return b.CreateAdd(lhs, rhs, "add");

        case TokenKind::TK_MINUS:
            return b.CreateSub(lhs, rhs, "sub");

        case TokenKind::TK_STAR:
            return b.CreateMul(lhs, rhs, "mul");

        case TokenKind::TK_SLASH:
            if (is_signed)
                return b.CreateSDiv(lhs, rhs, "sdiv");
            else
                return b.CreateUDiv(lhs, rhs, "udiv");

        case TokenKind::TK_PERCENT:
            if (is_signed)
                return b.CreateSRem(lhs, rhs, "srem");
            else
                return b.CreateURem(lhs, rhs, "urem");

        default:
            return nullptr;
        }
    }

    llvm::Value *OperatorCodegen::generate_float_arithmetic(TokenKind op,
                                                              llvm::Value *lhs,
                                                              llvm::Value *rhs)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_PLUS:
            return b.CreateFAdd(lhs, rhs, "fadd");

        case TokenKind::TK_MINUS:
            return b.CreateFSub(lhs, rhs, "fsub");

        case TokenKind::TK_STAR:
            return b.CreateFMul(lhs, rhs, "fmul");

        case TokenKind::TK_SLASH:
            return b.CreateFDiv(lhs, rhs, "fdiv");

        case TokenKind::TK_PERCENT:
            return b.CreateFRem(lhs, rhs, "frem");

        default:
            return nullptr;
        }
    }

    //===================================================================
    // Comparison Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_comparison(TokenKind op,
                                                        llvm::Value *lhs,
                                                        llvm::Value *rhs,
                                                        Cryo::Type *operand_type)
    {
        if (!lhs || !rhs)
            return nullptr;

        // Ensure compatible types
        ensure_compatible_types(lhs, rhs);

        llvm::Type *type = lhs->getType();

        if (type->isIntegerTy())
        {
            bool is_signed = operand_type ? is_signed_integer_type(operand_type) : true;
            return generate_integer_comparison(op, lhs, rhs, is_signed);
        }

        if (type->isFloatingPointTy())
        {
            return generate_float_comparison(op, lhs, rhs);
        }

        if (type->isPointerTy())
        {
            return generate_pointer_comparison(op, lhs, rhs);
        }

        report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                     "Comparison not supported for this type");
        return nullptr;
    }

    llvm::Value *OperatorCodegen::generate_integer_comparison(TokenKind op,
                                                                 llvm::Value *lhs,
                                                                 llvm::Value *rhs,
                                                                 bool is_signed)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_EQUAL_EQUAL:
            return b.CreateICmpEQ(lhs, rhs, "eq");

        case TokenKind::TK_BANG_EQUAL:
            return b.CreateICmpNE(lhs, rhs, "ne");

        case TokenKind::TK_LESS_THAN:
            return is_signed ? b.CreateICmpSLT(lhs, rhs, "slt")
                             : b.CreateICmpULT(lhs, rhs, "ult");

        case TokenKind::TK_GREATER_THAN:
            return is_signed ? b.CreateICmpSGT(lhs, rhs, "sgt")
                             : b.CreateICmpUGT(lhs, rhs, "ugt");

        case TokenKind::TK_LESS_THAN_EQUAL:
            return is_signed ? b.CreateICmpSLE(lhs, rhs, "sle")
                             : b.CreateICmpULE(lhs, rhs, "ule");

        case TokenKind::TK_GREATER_THAN_EQUAL:
            return is_signed ? b.CreateICmpSGE(lhs, rhs, "sge")
                             : b.CreateICmpUGE(lhs, rhs, "uge");

        default:
            return nullptr;
        }
    }

    llvm::Value *OperatorCodegen::generate_float_comparison(TokenKind op,
                                                              llvm::Value *lhs,
                                                              llvm::Value *rhs)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_EQUAL_EQUAL:
            return b.CreateFCmpOEQ(lhs, rhs, "feq");

        case TokenKind::TK_BANG_EQUAL:
            return b.CreateFCmpONE(lhs, rhs, "fne");

        case TokenKind::TK_LESS_THAN:
            return b.CreateFCmpOLT(lhs, rhs, "flt");

        case TokenKind::TK_GREATER_THAN:
            return b.CreateFCmpOGT(lhs, rhs, "fgt");

        case TokenKind::TK_LESS_THAN_EQUAL:
            return b.CreateFCmpOLE(lhs, rhs, "fle");

        case TokenKind::TK_GREATER_THAN_EQUAL:
            return b.CreateFCmpOGE(lhs, rhs, "fge");

        default:
            return nullptr;
        }
    }

    llvm::Value *OperatorCodegen::generate_pointer_comparison(TokenKind op,
                                                                 llvm::Value *lhs,
                                                                 llvm::Value *rhs)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_EQUAL_EQUAL:
            return b.CreateICmpEQ(lhs, rhs, "ptr_eq");

        case TokenKind::TK_BANG_EQUAL:
            return b.CreateICmpNE(lhs, rhs, "ptr_ne");

        default:
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "Only equality comparisons supported for pointers");
            return nullptr;
        }
    }

    //===================================================================
    // Logical Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_logical_and(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        llvm::IRBuilder<> &b = builder();
        llvm::Function *fn = b.GetInsertBlock()->getParent();

        // Create blocks for short-circuit evaluation
        llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(llvm_ctx(), "and.rhs", fn);
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(llvm_ctx(), "and.merge", fn);

        // Evaluate LHS
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
            return nullptr;

        // Convert to bool if needed
        if (!lhs->getType()->isIntegerTy(1))
        {
            lhs = b.CreateICmpNE(lhs, llvm::Constant::getNullValue(lhs->getType()), "tobool");
        }

        llvm::BasicBlock *lhs_block = b.GetInsertBlock();

        // Short-circuit: if LHS is false, result is false
        b.CreateCondBr(lhs, rhs_block, merge_block);

        // Evaluate RHS
        b.SetInsertPoint(rhs_block);
        llvm::Value *rhs = generate_operand(node->right());
        if (!rhs)
            return nullptr;

        if (!rhs->getType()->isIntegerTy(1))
        {
            rhs = b.CreateICmpNE(rhs, llvm::Constant::getNullValue(rhs->getType()), "tobool");
        }

        llvm::BasicBlock *rhs_end_block = b.GetInsertBlock();
        b.CreateBr(merge_block);

        // Merge
        b.SetInsertPoint(merge_block);
        llvm::PHINode *phi = b.CreatePHI(llvm::Type::getInt1Ty(llvm_ctx()), 2, "and.result");
        phi->addIncoming(b.getFalse(), lhs_block);
        phi->addIncoming(rhs, rhs_end_block);

        return phi;
    }

    llvm::Value *OperatorCodegen::generate_logical_or(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        llvm::IRBuilder<> &b = builder();
        llvm::Function *fn = b.GetInsertBlock()->getParent();

        // Create blocks for short-circuit evaluation
        llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(llvm_ctx(), "or.rhs", fn);
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(llvm_ctx(), "or.merge", fn);

        // Evaluate LHS
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
            return nullptr;

        // Convert to bool if needed
        if (!lhs->getType()->isIntegerTy(1))
        {
            lhs = b.CreateICmpNE(lhs, llvm::Constant::getNullValue(lhs->getType()), "tobool");
        }

        llvm::BasicBlock *lhs_block = b.GetInsertBlock();

        // Short-circuit: if LHS is true, result is true
        b.CreateCondBr(lhs, merge_block, rhs_block);

        // Evaluate RHS
        b.SetInsertPoint(rhs_block);
        llvm::Value *rhs = generate_operand(node->right());
        if (!rhs)
            return nullptr;

        if (!rhs->getType()->isIntegerTy(1))
        {
            rhs = b.CreateICmpNE(rhs, llvm::Constant::getNullValue(rhs->getType()), "tobool");
        }

        llvm::BasicBlock *rhs_end_block = b.GetInsertBlock();
        b.CreateBr(merge_block);

        // Merge
        b.SetInsertPoint(merge_block);
        llvm::PHINode *phi = b.CreatePHI(llvm::Type::getInt1Ty(llvm_ctx()), 2, "or.result");
        phi->addIncoming(b.getTrue(), lhs_block);
        phi->addIncoming(rhs, rhs_end_block);

        return phi;
    }

    //===================================================================
    // Bitwise Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_bitwise(TokenKind op,
                                                     llvm::Value *lhs,
                                                     llvm::Value *rhs)
    {
        if (!lhs || !rhs)
            return nullptr;

        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_AMPERSAND:
            return b.CreateAnd(lhs, rhs, "and");

        case TokenKind::TK_PIPE:
            return b.CreateOr(lhs, rhs, "or");

        case TokenKind::TK_CARET:
            return b.CreateXor(lhs, rhs, "xor");

        case TokenKind::TK_LESS_LESS:
            return b.CreateShl(lhs, rhs, "shl");

        case TokenKind::TK_GREATER_GREATER:
            // Default to arithmetic shift (preserve sign)
            return b.CreateAShr(lhs, rhs, "ashr");

        default:
            return nullptr;
        }
    }

    //===================================================================
    // String Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_string_concat(llvm::Value *lhs, llvm::Value *rhs)
    {
        if (!lhs || !rhs)
            return nullptr;

        llvm::Function *concat_fn = get_string_concat_fn();
        if (!concat_fn)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "Could not get string concatenation function");
            return nullptr;
        }

        return builder().CreateCall(concat_fn, {lhs, rhs}, "strcat");
    }

    llvm::Value *OperatorCodegen::generate_string_char_concat(llvm::Value *str, llvm::Value *chr)
    {
        if (!str || !chr)
            return nullptr;

        llvm::Function *concat_fn = get_string_char_concat_fn();
        if (!concat_fn)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "Could not get string-char concatenation function");
            return nullptr;
        }

        return builder().CreateCall(concat_fn, {str, chr}, "strcat");
    }

    llvm::Value *OperatorCodegen::generate_char_string_concat(llvm::Value *chr, llvm::Value *str)
    {
        if (!chr || !str)
            return nullptr;

        // Similar to string_char but with swapped semantics
        // This would call a different runtime function
        LOG_WARN(Cryo::LogComponent::CODEGEN, "char-string concatenation not fully implemented");
        return generate_string_char_concat(str, chr); // Fallback
    }

    //===================================================================
    // Unary Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_negation(llvm::Value *operand, Cryo::Type *type)
    {
        if (!operand)
            return nullptr;

        llvm::IRBuilder<> &b = builder();

        if (operand->getType()->isIntegerTy())
        {
            return b.CreateNeg(operand, "neg");
        }

        if (operand->getType()->isFloatingPointTy())
        {
            return b.CreateFNeg(operand, "fneg");
        }

        report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                     "Negation not supported for this type");
        return nullptr;
    }

    llvm::Value *OperatorCodegen::generate_logical_not(llvm::Value *operand)
    {
        if (!operand)
            return nullptr;

        llvm::IRBuilder<> &b = builder();

        // Convert to bool first if needed
        if (!operand->getType()->isIntegerTy(1))
        {
            operand = b.CreateICmpNE(operand,
                                      llvm::Constant::getNullValue(operand->getType()),
                                      "tobool");
        }

        return b.CreateNot(operand, "not");
    }

    llvm::Value *OperatorCodegen::generate_bitwise_not(llvm::Value *operand)
    {
        if (!operand)
            return nullptr;

        if (!operand->getType()->isIntegerTy())
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                         "Bitwise NOT only supported for integer types");
            return nullptr;
        }

        return builder().CreateNot(operand, "bitnot");
    }

    llvm::Value *OperatorCodegen::generate_address_of(Cryo::ExpressionNode *operand)
    {
        if (!operand)
            return nullptr;

        llvm::Value *addr = get_lvalue_address(operand);
        if (!addr)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, operand,
                         "Cannot take address of non-lvalue");
            return nullptr;
        }

        return addr;
    }

    llvm::Value *OperatorCodegen::generate_dereference(llvm::Value *ptr, Cryo::Type *pointee_type)
    {
        if (!ptr)
            return nullptr;

        if (!ptr->getType()->isPointerTy())
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                         "Cannot dereference non-pointer type");
            return nullptr;
        }

        // Get the type to load
        llvm::Type *load_type = nullptr;
        if (pointee_type)
        {
            load_type = types().map_type(pointee_type);
        }

        // Fallback to i32 if we can't determine type
        if (!load_type)
        {
            load_type = llvm::Type::getInt32Ty(llvm_ctx());
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Dereference type unknown, defaulting to i32");
        }

        if (_memory)
        {
            return _memory->create_load(ptr, load_type, "deref");
        }
        return builder().CreateLoad(load_type, ptr, "deref");
    }

    llvm::Value *OperatorCodegen::generate_increment(Cryo::ExpressionNode *operand, bool is_prefix)
    {
        if (!operand)
            return nullptr;

        llvm::Value *ptr = get_lvalue_address(operand);
        if (!ptr)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, operand,
                         "Cannot increment non-lvalue");
            return nullptr;
        }

        // Load current value
        llvm::Value *current = generate_operand(operand);
        if (!current)
            return nullptr;

        // Get increment value
        llvm::Value *one = get_increment_value(current->getType());
        if (!one)
            return nullptr;

        // Create new value
        llvm::Value *new_val;
        if (current->getType()->isIntegerTy())
        {
            new_val = builder().CreateAdd(current, one, "inc");
        }
        else if (current->getType()->isFloatingPointTy())
        {
            new_val = builder().CreateFAdd(current, one, "finc");
        }
        else
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                         "Increment not supported for this type");
            return nullptr;
        }

        // Store new value
        if (_memory)
        {
            _memory->create_store(new_val, ptr);
        }
        else
        {
            builder().CreateStore(new_val, ptr);
        }

        // Return old value for postfix, new value for prefix
        return is_prefix ? new_val : current;
    }

    llvm::Value *OperatorCodegen::generate_decrement(Cryo::ExpressionNode *operand, bool is_prefix)
    {
        if (!operand)
            return nullptr;

        llvm::Value *ptr = get_lvalue_address(operand);
        if (!ptr)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, operand,
                         "Cannot decrement non-lvalue");
            return nullptr;
        }

        // Load current value
        llvm::Value *current = generate_operand(operand);
        if (!current)
            return nullptr;

        // Get decrement value
        llvm::Value *one = get_increment_value(current->getType());
        if (!one)
            return nullptr;

        // Create new value
        llvm::Value *new_val;
        if (current->getType()->isIntegerTy())
        {
            new_val = builder().CreateSub(current, one, "dec");
        }
        else if (current->getType()->isFloatingPointTy())
        {
            new_val = builder().CreateFSub(current, one, "fdec");
        }
        else
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                         "Decrement not supported for this type");
            return nullptr;
        }

        // Store new value
        if (_memory)
        {
            _memory->create_store(new_val, ptr);
        }
        else
        {
            builder().CreateStore(new_val, ptr);
        }

        // Return old value for postfix, new value for prefix
        return is_prefix ? new_val : current;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    llvm::Value *OperatorCodegen::generate_operand(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Visit the expression and get its value
        // Note: This relies on the expression visitor being set up
        // For now, we use a simple approach
        expr->accept(*static_cast<ASTVisitor *>(&ctx().symbols())); // This won't work directly

        // TODO: Need a way to generate expression values
        // For now, return the value from context if registered
        return ctx().get_value(expr);
    }

    llvm::Value *OperatorCodegen::get_lvalue_address(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Handle different lvalue types
        if (auto *ident = dynamic_cast<Cryo::IdentifierNode *>(expr))
        {
            std::string name = ident->name();

            // Try local first
            llvm::AllocaInst *alloca = values().get_alloca(name);
            if (alloca)
                return alloca;

            // Try global
            auto &globals = ctx().globals_map();
            auto it = globals.find(name);
            if (it != globals.end())
                return it->second;

            return nullptr;
        }

        // TODO: Handle member access, array access
        // These require more complex logic to compute the address

        return nullptr;
    }

    bool OperatorCodegen::is_lvalue(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return false;

        // Identifiers are lvalues
        if (dynamic_cast<Cryo::IdentifierNode *>(expr))
            return true;

        // Member access is an lvalue
        if (dynamic_cast<Cryo::MemberAccessNode *>(expr))
            return true;

        // Array access is an lvalue
        if (dynamic_cast<Cryo::ArrayAccessNode *>(expr))
            return true;

        // Dereference is an lvalue
        if (auto *unary = dynamic_cast<Cryo::UnaryExpressionNode *>(expr))
        {
            if (unary->operator_token().kind() == TokenKind::TK_STAR)
                return true;
        }

        return false;
    }

    bool OperatorCodegen::is_signed_integer_type(Cryo::Type *type)
    {
        if (!type)
            return true; // Default to signed

        // Check if it's an unsigned type
        std::string type_str = type->to_string();
        return type_str.find('u') != 0; // Not starting with 'u' means signed
    }

    bool OperatorCodegen::is_float_type(Cryo::Type *type)
    {
        if (!type)
            return false;

        TypeKind kind = type->kind();
        return kind == TypeKind::Float;
    }

    bool OperatorCodegen::is_string_type(Cryo::Type *type)
    {
        if (!type)
            return false;

        TypeKind kind = type->kind();
        return kind == TypeKind::String;
    }

    bool OperatorCodegen::ensure_compatible_types(llvm::Value *&lhs, llvm::Value *&rhs)
    {
        if (!lhs || !rhs)
            return false;

        llvm::Type *lhs_type = lhs->getType();
        llvm::Type *rhs_type = rhs->getType();

        if (lhs_type == rhs_type)
            return true;

        // Integer type promotion
        if (lhs_type->isIntegerTy() && rhs_type->isIntegerTy())
        {
            unsigned lhs_bits = lhs_type->getIntegerBitWidth();
            unsigned rhs_bits = rhs_type->getIntegerBitWidth();

            if (lhs_bits < rhs_bits)
            {
                lhs = builder().CreateSExt(lhs, rhs_type, "sext");
            }
            else if (rhs_bits < lhs_bits)
            {
                rhs = builder().CreateSExt(rhs, lhs_type, "sext");
            }
            return true;
        }

        // Float promotion
        if (lhs_type->isFloatingPointTy() && rhs_type->isFloatingPointTy())
        {
            if (lhs_type->isFloatTy() && rhs_type->isDoubleTy())
            {
                lhs = builder().CreateFPExt(lhs, rhs_type, "fpext");
            }
            else if (rhs_type->isFloatTy() && lhs_type->isDoubleTy())
            {
                rhs = builder().CreateFPExt(rhs, lhs_type, "fpext");
            }
            return true;
        }

        return false;
    }

    llvm::Function *OperatorCodegen::get_string_concat_fn()
    {
        if (_string_concat_fn)
            return _string_concat_fn;

        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        // Look for existing function
        _string_concat_fn = mod->getFunction("std::Runtime::cryo_strcat");
        if (_string_concat_fn)
            return _string_concat_fn;

        // Create declaration
        llvm::Type *char_ptr = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *fn_type = llvm::FunctionType::get(char_ptr, {char_ptr, char_ptr}, false);

        _string_concat_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                    "std::Runtime::cryo_strcat", mod);
        return _string_concat_fn;
    }

    llvm::Function *OperatorCodegen::get_string_char_concat_fn()
    {
        if (_string_char_concat_fn)
            return _string_char_concat_fn;

        llvm::Module *mod = module();
        if (!mod)
            return nullptr;

        // Look for existing function
        _string_char_concat_fn = mod->getFunction("std::Runtime::cryo_strcat_char");
        if (_string_char_concat_fn)
            return _string_char_concat_fn;

        // Create declaration
        llvm::Type *char_ptr = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *i8 = llvm::Type::getInt8Ty(llvm_ctx());
        llvm::FunctionType *fn_type = llvm::FunctionType::get(char_ptr, {char_ptr, i8}, false);

        _string_char_concat_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                         "std::Runtime::cryo_strcat_char", mod);
        return _string_char_concat_fn;
    }

    llvm::Value *OperatorCodegen::get_increment_value(llvm::Type *type)
    {
        if (!type)
            return nullptr;

        if (type->isIntegerTy())
        {
            return llvm::ConstantInt::get(type, 1);
        }

        if (type->isFloatTy())
        {
            return llvm::ConstantFP::get(type, 1.0);
        }

        if (type->isDoubleTy())
        {
            return llvm::ConstantFP::get(type, 1.0);
        }

        return nullptr;
    }

} // namespace Cryo::Codegen
