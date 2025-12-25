#include "Codegen/Expressions/OperatorCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "AST/ASTVisitor.hpp"
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

        // Handle compound assignments (+=, -=, etc.)
        if (op == TokenKind::TK_PLUSEQUAL || op == TokenKind::TK_MINUSEQUAL ||
            op == TokenKind::TK_STAREQUAL || op == TokenKind::TK_SLASHEQUAL ||
            op == TokenKind::TK_PERCENTEQUAL)
        {
            return generate_compound_assignment(node);
        }

        // Generate operand values
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Failed to generate left operand");
            return nullptr;
        }

        // For logical operators, use short-circuit evaluation
        if (op == TokenKind::TK_AMPAMP)
        {
            return generate_logical_and(node);
        }
        if (op == TokenKind::TK_PIPEPIPE)
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
            return generate_arithmetic(op, lhs, rhs, node->get_resolved_type());

        case BinaryOpClass::Comparison:
            return generate_comparison(op, lhs, rhs, node->left()->get_resolved_type());

        case BinaryOpClass::Bitwise:
            return generate_bitwise(op, lhs, rhs);

        case BinaryOpClass::StringConcat:
            // Check if rhs is a char (i8) or other integer, use char concat
            if (rhs->getType()->isIntegerTy() && !rhs->getType()->isIntegerTy(1))
            {
                return generate_string_char_concat(lhs, rhs);
            }
            return generate_string_concat(lhs, rhs);

        default:
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Unsupported binary operator: " + std::string(node->operator_token().text()));
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
        std::string op_str = std::string(node->operator_token().text());

        // Handle increment/decrement specially (they modify lvalues)
        if (op == TokenKind::TK_PLUSPLUS)
        {
            return generate_increment(node->operand(), true); // TODO: determine prefix/postfix
        }
        if (op == TokenKind::TK_MINUSMINUS)
        {
            return generate_decrement(node->operand(), true); // TODO: determine prefix/postfix
        }

        // Handle address-of specially (doesn't evaluate operand)
        if (op == TokenKind::TK_AMP)
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
            return generate_negation(operand, node->operand()->get_resolved_type());
        }
        if (op == TokenKind::TK_EXCLAIM)
        {
            return generate_logical_not(operand);
        }
        if (op == TokenKind::TK_TILDE)
        {
            return generate_bitwise_not(operand);
        }
        if (op == TokenKind::TK_STAR)
        {
            return generate_dereference(operand, node->operand()->get_resolved_type());
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
        case TokenKind::TK_PLUSEQUAL:
        case TokenKind::TK_MINUSEQUAL:
        case TokenKind::TK_STAREQUAL:
        case TokenKind::TK_SLASHEQUAL:
        case TokenKind::TK_PERCENTEQUAL:
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
        case TokenKind::TK_EQUALEQUAL:
        case TokenKind::TK_EXCLAIMEQUAL:
        case TokenKind::TK_L_ANGLE:
        case TokenKind::TK_R_ANGLE:
        case TokenKind::TK_LESSEQUAL:
        case TokenKind::TK_GREATEREQUAL:
            return BinaryOpClass::Comparison;

        // Logical
        case TokenKind::TK_AMPAMP:
        case TokenKind::TK_PIPEPIPE:
            return BinaryOpClass::Logical;

        // Bitwise
        case TokenKind::TK_AMP:
        case TokenKind::TK_PIPE:
        case TokenKind::TK_CARET:
        case TokenKind::TK_LESSLESS:
        case TokenKind::TK_GREATERGREATER:
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

    llvm::Value *OperatorCodegen::generate_compound_assignment(Cryo::BinaryExpressionNode *node)
    {
        if (!node)
            return nullptr;

        Cryo::ExpressionNode *lhs_expr = node->left();
        Cryo::ExpressionNode *rhs_expr = node->right();
        TokenKind op = node->operator_token().kind();

        // Get the lvalue address
        llvm::Value *lhs_ptr = get_lvalue_address(lhs_expr);
        if (!lhs_ptr)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                         "Left side of compound assignment must be an lvalue");
            return nullptr;
        }

        // Load the current value from the lvalue
        llvm::Value *current_value = generate_operand(lhs_expr);
        if (!current_value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                         "Failed to load current value for compound assignment");
            return nullptr;
        }

        // Generate the right-hand side value
        llvm::Value *rhs_value = generate_operand(rhs_expr);
        if (!rhs_value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                         "Failed to generate right operand for compound assignment");
            return nullptr;
        }

        // Ensure compatible types
        ensure_compatible_types(current_value, rhs_value);

        // Determine the underlying arithmetic operation
        TokenKind arith_op;
        switch (op)
        {
        case TokenKind::TK_PLUSEQUAL:
            arith_op = TokenKind::TK_PLUS;
            break;
        case TokenKind::TK_MINUSEQUAL:
            arith_op = TokenKind::TK_MINUS;
            break;
        case TokenKind::TK_STAREQUAL:
            arith_op = TokenKind::TK_STAR;
            break;
        case TokenKind::TK_SLASHEQUAL:
            arith_op = TokenKind::TK_SLASH;
            break;
        case TokenKind::TK_PERCENTEQUAL:
            arith_op = TokenKind::TK_PERCENT;
            break;
        default:
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                         "Unsupported compound assignment operator");
            return nullptr;
        }

        // Perform the arithmetic operation
        llvm::Value *result = generate_arithmetic(arith_op, current_value, rhs_value,
                                                  lhs_expr->get_resolved_type());
        if (!result)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, node,
                         "Failed to compute compound assignment result");
            return nullptr;
        }

        // Store the result back to the lvalue
        if (_memory)
        {
            _memory->create_store(result, lhs_ptr);
        }
        else
        {
            builder().CreateStore(result, lhs_ptr);
        }

        return result;
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

        // Log type information for debugging
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_comparison: lhs type = {}, rhs type = {}",
                  lhs->getType()->getTypeID(), rhs->getType()->getTypeID());

        // Ensure compatible types
        if (!ensure_compatible_types(lhs, rhs))
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "generate_comparison: Types are incompatible and cannot be converted. "
                      "lhs type ID = {}, rhs type ID = {}",
                      lhs->getType()->getTypeID(), rhs->getType()->getTypeID());
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "Cannot compare values of incompatible types");
            return nullptr;
        }

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
        case TokenKind::TK_EQUALEQUAL:
            return b.CreateICmpEQ(lhs, rhs, "eq");

        case TokenKind::TK_EXCLAIMEQUAL:
            return b.CreateICmpNE(lhs, rhs, "ne");

        case TokenKind::TK_L_ANGLE:
            return is_signed ? b.CreateICmpSLT(lhs, rhs, "slt")
                             : b.CreateICmpULT(lhs, rhs, "ult");

        case TokenKind::TK_R_ANGLE:
            return is_signed ? b.CreateICmpSGT(lhs, rhs, "sgt")
                             : b.CreateICmpUGT(lhs, rhs, "ugt");

        case TokenKind::TK_LESSEQUAL:
            return is_signed ? b.CreateICmpSLE(lhs, rhs, "sle")
                             : b.CreateICmpULE(lhs, rhs, "ule");

        case TokenKind::TK_GREATEREQUAL:
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
        case TokenKind::TK_EQUALEQUAL:
            return b.CreateFCmpOEQ(lhs, rhs, "feq");

        case TokenKind::TK_EXCLAIMEQUAL:
            return b.CreateFCmpONE(lhs, rhs, "fne");

        case TokenKind::TK_L_ANGLE:
            return b.CreateFCmpOLT(lhs, rhs, "flt");

        case TokenKind::TK_R_ANGLE:
            return b.CreateFCmpOGT(lhs, rhs, "fgt");

        case TokenKind::TK_LESSEQUAL:
            return b.CreateFCmpOLE(lhs, rhs, "fle");

        case TokenKind::TK_GREATEREQUAL:
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
        case TokenKind::TK_EQUALEQUAL:
            return b.CreateICmpEQ(lhs, rhs, "ptr_eq");

        case TokenKind::TK_EXCLAIMEQUAL:
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
        case TokenKind::TK_AMP:
            return b.CreateAnd(lhs, rhs, "and");

        case TokenKind::TK_PIPE:
            return b.CreateOr(lhs, rhs, "or");

        case TokenKind::TK_CARET:
            return b.CreateXor(lhs, rhs, "xor");

        case TokenKind::TK_LESSLESS:
            return b.CreateShl(lhs, rhs, "shl");

        case TokenKind::TK_GREATERGREATER:
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

        // Cast the character value to i8 if needed
        llvm::Type *i8_type = llvm::Type::getInt8Ty(llvm_ctx());
        if (chr->getType() != i8_type && chr->getType()->isIntegerTy())
        {
            chr = builder().CreateTrunc(chr, i8_type, "tochar");
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

        // Use visitor pattern through context
        CodegenVisitor *visitor = ctx().visitor();
        if (!visitor)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "CodegenVisitor not available for expression generation");
            return nullptr;
        }

        // Visit the expression to generate its value
        expr->accept(*visitor);

        // Get the result from the visitor/context
        return get_result();
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

        // Handle member access (e.g., this.field, obj.field)
        if (auto *member = dynamic_cast<Cryo::MemberAccessNode *>(expr))
        {
            std::string member_name = member->member();

            // Generate the object expression to get the base pointer
            llvm::Value *object = generate_operand(member->object());
            if (!object)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Failed to generate member access object");
                return nullptr;
            }

            // Ensure we have a pointer to the struct
            if (!object->getType()->isPointerTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Member access object is not a pointer type");
                return nullptr;
            }

            // Resolve struct type and field index
            llvm::StructType *struct_type = nullptr;
            unsigned field_idx = 0;

            // Get the resolved type of the object expression
            Cryo::Type *obj_type = member->object()->get_resolved_type();
            std::string type_name;

            if (obj_type)
            {
                type_name = obj_type->to_string();
                // Handle pointer types - get the pointee type name
                if (obj_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<Cryo::PointerType *>(obj_type);
                    if (ptr_type && ptr_type->pointee_type())
                    {
                        type_name = ptr_type->pointee_type()->to_string();
                    }
                }
                else if (!type_name.empty() && type_name.back() == '*')
                {
                    type_name.pop_back();
                }
            }
            else
            {
                // Fallback: Check if this is the 'this' identifier
                if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(member->object()))
                {
                    if (identifier->name() == "this")
                    {
                        // Try to get 'this' type from variable_types_map
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find("this");
                        if (it != var_types.end() && it->second)
                        {
                            type_name = it->second->to_string();
                            if (!type_name.empty() && type_name.back() == '*')
                            {
                                type_name.pop_back();
                            }
                        }
                        else
                        {
                            // Fallback to current_type_name if available
                            type_name = ctx().current_type_name();
                        }
                    }
                }
            }

            if (type_name.empty())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Could not determine type for member access");
                return nullptr;
            }

            // Look up struct type in LLVM context
            struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
            if (!struct_type)
            {
                // Try context registry
                if (llvm::Type *type = ctx().get_type(type_name))
                {
                    struct_type = llvm::dyn_cast<llvm::StructType>(type);
                }
            }

            if (!struct_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Struct type '{}' not found", type_name);
                return nullptr;
            }

            // Look up field index
            int field_idx_signed = ctx().get_struct_field_index(type_name, member_name);
            if (field_idx_signed < 0)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Field '{}' not found in struct '{}'",
                          member_name, type_name);
                return nullptr;
            }
            field_idx = static_cast<unsigned>(field_idx_signed);

            // Create GEP for field access
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "get_lvalue_address: Creating GEP for {}.{} at index {}",
                      type_name, member_name, field_idx);
            return create_struct_gep(struct_type, object, field_idx, member_name + ".ptr");
        }

        // Handle array access (e.g., arr[i])
        if (auto *array_access = dynamic_cast<Cryo::ArrayAccessNode *>(expr))
        {
            // Generate array pointer
            llvm::Value *array_val = generate_operand(array_access->array());
            if (!array_val)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Failed to generate array expression");
                return nullptr;
            }

            // Generate index
            llvm::Value *index_val = generate_operand(array_access->index());
            if (!index_val)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Failed to generate array index");
                return nullptr;
            }

            // Ensure array is a pointer
            if (!array_val->getType()->isPointerTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Array expression is not a pointer type");
                return nullptr;
            }

            // Determine element type
            llvm::Type *element_type = nullptr;
            Cryo::Type *array_type = array_access->array()->get_resolved_type();
            if (array_type)
            {
                if (array_type->kind() == Cryo::TypeKind::Array)
                {
                    auto *arr_type = dynamic_cast<Cryo::ArrayType *>(array_type);
                    if (arr_type && arr_type->element_type())
                    {
                        element_type = get_llvm_type(arr_type->element_type().get());
                    }
                }
                else if (array_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<Cryo::PointerType *>(array_type);
                    if (ptr_type && ptr_type->pointee_type())
                    {
                        element_type = get_llvm_type(ptr_type->pointee_type().get());
                    }
                }
            }

            if (!element_type)
            {
                // Fallback to i8 as a generic byte type
                element_type = llvm::Type::getInt8Ty(llvm_ctx());
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Could not determine element type, using i8");
            }

            // Create GEP for array element access
            return create_array_gep(element_type, array_val, index_val, "array.elem.ptr");
        }

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

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "ensure_compatible_types: lhs type ID = {}, rhs type ID = {}",
                  lhs_type->getTypeID(), rhs_type->getTypeID());

        if (lhs_type == rhs_type)
            return true;

        // Integer type promotion
        if (lhs_type->isIntegerTy() && rhs_type->isIntegerTy())
        {
            unsigned lhs_bits = lhs_type->getIntegerBitWidth();
            unsigned rhs_bits = rhs_type->getIntegerBitWidth();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ensure_compatible_types: Integer promotion - lhs bits = {}, rhs bits = {}",
                      lhs_bits, rhs_bits);

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

        // Pointer comparison (both should be pointers)
        if (lhs_type->isPointerTy() && rhs_type->isPointerTy())
        {
            // Opaque pointers in LLVM 15+ are compatible
            return true;
        }

        // Log detailed error for debugging
        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                  "ensure_compatible_types: Cannot convert between incompatible types. "
                  "lhs is integer: {}, lhs is pointer: {}, lhs is float: {}; "
                  "rhs is integer: {}, rhs is pointer: {}, rhs is float: {}",
                  lhs_type->isIntegerTy(), lhs_type->isPointerTy(), lhs_type->isFloatingPointTy(),
                  rhs_type->isIntegerTy(), rhs_type->isPointerTy(), rhs_type->isFloatingPointTy());

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
