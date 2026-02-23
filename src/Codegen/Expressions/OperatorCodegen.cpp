#include "Codegen/Expressions/OperatorCodegen.hpp"
#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "AST/ASTVisitor.hpp"
#include "AST/TemplateRegistry.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/CompoundTypes.hpp"
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

        // For logical operators, use short-circuit evaluation.
        // These must be checked BEFORE evaluating the LHS, because the logical
        // handlers evaluate the LHS themselves (to correctly set up short-circuit
        // branching). Evaluating LHS here first would cause a double evaluation,
        // which is incorrect for expressions with side effects.
        if (op == TokenKind::TK_AMPAMP)
        {
            return generate_logical_and(node);
        }
        if (op == TokenKind::TK_PIPEPIPE)
        {
            return generate_logical_or(node);
        }

        // Generate operand values
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Failed to generate left operand, generate_operand returned nullptr.");
            return nullptr;
        }

        llvm::Value *rhs = generate_operand(node->right());
        if (!rhs)
        {
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                         "Failed to generate right operand, generate_operand returned nullptr");
            return nullptr;
        }

        // Check for string concatenation BEFORE classifying based on single operand type
        // This handles cases like: char + string, string + char, string + string
        // IMPORTANT: We must check the AST's resolved type, not just LLVM pointer types,
        // because dereferenced pointers to integers are also pointer types at LLVM level
        if (op == TokenKind::TK_PLUS)
        {
            // Check semantic types from AST to determine if operands are strings
            auto is_string_type = [](TypeRef type) -> bool
            {
                if (!type)
                    return false;
                // Check for string type
                if (type->kind() == Cryo::TypeKind::String)
                    return true;
                // Check for pointer to char (C-style string)
                if (type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(type.get());
                    if (ptr_type && ptr_type->pointee())
                    {
                        return ptr_type->pointee()->kind() == Cryo::TypeKind::Char;
                    }
                }
                return false;
            };

            auto is_char_type = [](TypeRef type) -> bool
            {
                if (!type)
                    return false;
                return type->kind() == Cryo::TypeKind::Char;
            };

            TypeRef lhs_type = node->left()->get_resolved_type();
            TypeRef rhs_type = node->right()->get_resolved_type();

            bool lhs_is_string = is_string_type(lhs_type);
            bool rhs_is_string = is_string_type(rhs_type);
            bool lhs_is_char = is_char_type(lhs_type);
            bool rhs_is_char = is_char_type(rhs_type);

            // string + string
            if (lhs_is_string && rhs_is_string)
            {
                return generate_string_concat(lhs, rhs);
            }
            // string + char
            if (lhs_is_string && rhs_is_char)
            {
                return generate_string_char_concat(lhs, rhs);
            }
            // char + string
            if (lhs_is_char && rhs_is_string)
            {
                return generate_char_string_concat(lhs, rhs);
            }
        }

        // Classify and dispatch
        BinaryOpClass op_class = classify_binary_op(op, lhs->getType());

        switch (op_class)
        {
        case BinaryOpClass::Arithmetic:
            return generate_arithmetic(op, lhs, rhs, node->get_resolved_type());

        case BinaryOpClass::Comparison:
        {
            TypeRef left_type = node->left()->get_resolved_type();
            TypeRef right_type = node->right()->get_resolved_type();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Comparison dispatch: left_type valid={}, right_type valid={}, "
                      "left_is_enum={}, right_is_enum={}, "
                      "lhs llvm_type={}, rhs llvm_type={}",
                      (bool)left_type, (bool)right_type,
                      (left_type ? left_type->is_enum() : false),
                      (right_type ? right_type->is_enum() : false),
                      lhs->getType()->getTypeID(), rhs->getType()->getTypeID());

            if (left_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Comparison left_type kind={}", (int)left_type->kind());
            }
            if (right_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Comparison right_type kind={}", (int)right_type->kind());
            }

            // Extract enum discriminants before comparison
            if (left_type && left_type->is_enum())
                lhs = extract_enum_discriminant(lhs, left_type);
            if (right_type && right_type->is_enum())
                rhs = extract_enum_discriminant(rhs, right_type);

            return generate_comparison(op, lhs, rhs, left_type, node);
        }

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
            // Get the Cryo type of the operand to determine the correct pointee type
            TypeRef operand_type = node->operand()->get_resolved_type();

            // If the operand expression doesn't have a resolved type, look it up
            if (!operand_type.is_valid())
            {
                if (auto *ident = dynamic_cast<Cryo::IdentifierNode *>(node->operand()))
                {
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find(ident->name());
                    if (it != var_types.end())
                        operand_type = it->second;
                }
            }

            // Extract the actual pointee type from the pointer type
            TypeRef pointee_type;
            if (operand_type.is_valid() && operand_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(operand_type.get());
                if (ptr_type)
                    pointee_type = ptr_type->pointee();
            }

            llvm::Value *result = generate_dereference(operand, pointee_type);

            // Set the resolved type on this node so parent dereferences can use it
            if (pointee_type.is_valid() && !node->has_resolved_type())
            {
                node->set_resolved_type(pointee_type);
            }

            return result;
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
            // For now, always treat pointer + as arithmetic (pointer arithmetic)
            // String concatenation should be handled through explicit string operations
            // TODO: Implement proper type analysis to distinguish string vs pointer arithmetic
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "=== ASSIGNMENT DEBUG: Starting assignment generation ===");

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
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "Assignment: Null target or value_node: target={}, value_node={}",
                      (void *)target, (void *)value_node);
            report_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                         "Assignment generation received null target or value node");
            return nullptr;
        }

        std::string var_name = target->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Assignment: Processing assignment to variable '{}'", var_name);

        // Find the variable's storage location
        llvm::Value *var_ptr = values().get_alloca(var_name);
        if (!var_ptr)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Assignment: Variable '{}' not found in alloca, trying globals", var_name);
            // Try global from ValueContext first
            var_ptr = values().get_global_value(var_name);
            if (var_ptr)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Assignment: Found variable '{}' in ValueContext globals", var_name);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Assignment: Variable '{}' not found in ValueContext globals, trying CodegenContext", var_name);
                // Try global from CodegenContext as fallback
                auto &globals = ctx().globals_map();
                auto it = globals.find(var_name);
                if (it != globals.end())
                {
                    var_ptr = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Assignment: Found variable '{}' in CodegenContext globals", var_name);
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Assignment: Variable '{}' not found in CodegenContext globals either", var_name);
                }
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Assignment: Found variable '{}' in alloca", var_name);
        }

        if (!var_ptr)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "Assignment: Variable '{}' lookup FAILED - undefined variable", var_name);
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Undefined variable: " + var_name);
            return nullptr;
        }

        // Check if this is a class constructor call being assigned to a value-type variable
        // In this case, we need to construct in-place rather than heap-allocating
        if (auto *call_node = dynamic_cast<Cryo::CallExpressionNode *>(value_node))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Assignment: Processing call expression for variable '{}'", var_name);

            // Get the function name being called
            std::string callee_name;
            if (auto *callee_id = dynamic_cast<Cryo::IdentifierNode *>(call_node->callee()))
            {
                callee_name = callee_id->name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Assignment: Callee name is '{}'", callee_name);
            }

            // Check if this is a class type constructor (only if callee_name is non-empty)
            bool is_class = (!callee_name.empty() && _calls) ? _calls->is_class_type(callee_name) : false;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Assignment: Callee '{}' is_class_type={}, _calls={}",
                      callee_name, is_class, (void *)_calls);

            if (!callee_name.empty() && _calls)
            {
                // Get the target variable's type
                llvm::Type *var_type = nullptr;
                if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(var_ptr))
                {
                    var_type = alloca->getAllocatedType();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Assignment: Variable '{}' is AllocaInst, type={}",
                              var_name, var_type ? "present" : "null");
                }
                else if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(var_ptr))
                {
                    var_type = global->getValueType();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Assignment: Variable '{}' is GlobalVariable, type={}",
                              var_name, var_type ? "present" : "null");
                }

                // If the target is a struct type (value type), try constructor-based assignment
                bool is_struct = var_type ? var_type->isStructTy() : false;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Assignment: Variable '{}' type isStructTy={}", var_name, is_struct);

                if (var_type && is_struct)
                {
                    // Check if class type or try constructor anyway
                    bool try_constructor = is_class || call_node->arguments().empty();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Assignment: Try constructor for '{}': is_class={}, try_constructor={}",
                              callee_name, is_class, try_constructor);

                    if (try_constructor)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Assignment: Starting in-place construction for '{}' of type '{}'",
                                  var_name, callee_name);

                        // Generate constructor arguments
                        std::vector<llvm::Value *> args;
                        for (const auto &arg : call_node->arguments())
                        {
                            if (arg)
                            {
                                llvm::Value *arg_val = generate_operand(arg.get());
                                if (arg_val)
                                {
                                    args.push_back(arg_val);
                                }
                            }
                        }

                        // Look for the constructor function
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Assignment: Calling resolve_constructor for '{}'", callee_name);
                        llvm::Function *ctor = _calls->resolve_constructor(callee_name);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Assignment: Constructor resolved: {}", ctor ? "SUCCESS" : "FAILED");

                        if (ctor)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Assignment: Calling constructor '{}' with {} args",
                                      ctor->getName().str(), args.size());
                            // Call constructor with var_ptr as 'this'
                            std::vector<llvm::Value *> ctor_args;
                            ctor_args.push_back(var_ptr);
                            ctor_args.insert(ctor_args.end(), args.begin(), args.end());
                            builder().CreateCall(ctor, ctor_args);
                            return var_ptr;
                        }
                        else
                        {
                            // No constructor found - try direct field initialization if there are args
                            if (!args.empty())
                            {
                                auto *st = llvm::cast<llvm::StructType>(var_type);
                                for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
                                {
                                    llvm::Value *field_ptr = builder().CreateStructGEP(var_type, var_ptr, i,
                                                                                       "field." + std::to_string(i));
                                    llvm::Value *arg = args[i];
                                    if (arg->getType() != st->getElementType(i))
                                    {
                                        arg = cast_if_needed(arg, st->getElementType(i));
                                    }
                                    builder().CreateStore(arg, field_ptr);
                                }
                                return var_ptr;
                            }
                        }
                    }
                }
            }
        }

        // Generate the value to assign (standard path)
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Check if we need to store through a pointer.
        // When the variable is a pointer type (e.g., Expr*) but the value is a struct/aggregate
        // type (e.g., %Expr from an enum constructor), we must load the pointer first and store
        // the value through it into the heap-allocated memory, rather than overwriting the
        // pointer alloca itself.
        llvm::Type *alloc_type = nullptr;
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(var_ptr))
            alloc_type = alloca->getAllocatedType();
        else if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(var_ptr))
            alloc_type = global->getValueType();

        if (alloc_type && alloc_type->isPointerTy() && value->getType()->isStructTy())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Assignment: Storing struct value through pointer for variable '{}'", var_name);
            llvm::Value *ptr_val = builder().CreateLoad(alloc_type, var_ptr, "ptr.deref");
            builder().CreateStore(value, ptr_val);
        }
        else
        {
            // Store the value directly
            if (_memory)
            {
                _memory->create_store(value, var_ptr);
            }
            else
            {
                builder().CreateStore(value, var_ptr);
            }
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
            std::string member = target->member();
            std::string obj_name;
            if (auto *id = dynamic_cast<Cryo::IdentifierNode *>(target->object()))
                obj_name = id->name();
            else
                obj_name = "<expr>";

            std::string msg = "cannot get address of member '" + member + "' on '" + obj_name + "' for assignment";
            auto diag = Diag::error(ErrorCode::E0614_ASSIGNMENT_ERROR, msg);
            diag.at(target);
            if (target->object() && target->object()->get_resolved_type())
                diag.with_note("object type is '" + target->object()->get_resolved_type()->display_name() + "'");
            emit_diagnostic(std::move(diag));
            return nullptr;
        }

        // Check if this is a class/struct constructor call being assigned to a member field
        // In this case, we need to construct in-place rather than heap-allocating
        if (auto *call_node = dynamic_cast<Cryo::CallExpressionNode *>(value_node))
        {
            std::string callee_name;
            if (auto *callee_id = dynamic_cast<Cryo::IdentifierNode *>(call_node->callee()))
            {
                callee_name = callee_id->name();
            }

            if (!callee_name.empty() && _calls)
            {
                // Get the member field's type to determine if it's a struct type
                llvm::Type *member_field_type = nullptr;
                if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(member_ptr))
                {
                    member_field_type = alloca_inst->getAllocatedType();
                }
                else if (auto *gep_inst = llvm::dyn_cast<llvm::GetElementPtrInst>(member_ptr))
                {
                    member_field_type = gep_inst->getResultElementType();
                }

                bool is_class = _calls->is_class_type(callee_name);
                bool is_struct_field = member_field_type && member_field_type->isStructTy();

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Member assignment: callee='{}', is_class={}, is_struct_field={}",
                          callee_name, is_class, is_struct_field);

                // If the target field is a struct type and we're calling a class/struct constructor,
                // construct in-place instead of heap-allocating
                if (is_struct_field && (is_class || _calls->is_struct_type(callee_name)))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Member assignment: In-place construction for '{}' into struct field",
                              callee_name);

                    // Generate constructor arguments
                    std::vector<llvm::Value *> args;
                    for (const auto &arg : call_node->arguments())
                    {
                        if (arg)
                        {
                            llvm::Value *arg_val = generate_operand(arg.get());
                            if (arg_val)
                            {
                                args.push_back(arg_val);
                            }
                        }
                    }

                    // Look for the constructor function
                    llvm::Function *ctor = _calls->resolve_constructor(callee_name);
                    if (ctor)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Member assignment: Calling constructor '{}' in-place with {} args",
                                  ctor->getName().str(), args.size());

                        // Call constructor with member_ptr as 'this' pointer
                        std::vector<llvm::Value *> ctor_args;
                        ctor_args.push_back(member_ptr);
                        ctor_args.insert(ctor_args.end(), args.begin(), args.end());
                        builder().CreateCall(ctor, ctor_args);
                        return member_ptr;
                    }
                    else
                    {
                        // No constructor found - initialize fields directly if there are args
                        if (!args.empty() && member_field_type)
                        {
                            auto *st = llvm::dyn_cast<llvm::StructType>(member_field_type);
                            if (st)
                            {
                                for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
                                {
                                    llvm::Value *field_ptr = builder().CreateStructGEP(member_field_type, member_ptr, i,
                                                                                       "field." + std::to_string(i));
                                    llvm::Value *arg = args[i];
                                    if (arg->getType() != st->getElementType(i))
                                    {
                                        arg = cast_if_needed(arg, st->getElementType(i));
                                    }
                                    builder().CreateStore(arg, field_ptr);
                                }
                                return member_ptr;
                            }
                        }
                    }
                }
            }
        }

        // Generate the value to assign (standard path)
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Get the types involved
        llvm::Type *member_ptr_type = member_ptr->getType();
        llvm::Type *value_type = value->getType();

        if (!member_ptr_type->isPointerTy())
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, target,
                         "Member address is not a pointer type");
            return nullptr;
        }

        // For LLVM 20+, we need to get the pointed-to type differently
        // We'll check if it's an AllocaInst or GEP and extract the type accordingly
        llvm::Type *member_field_type = nullptr;
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(member_ptr))
        {
            member_field_type = alloca_inst->getAllocatedType();
        }
        else if (auto *gep_inst = llvm::dyn_cast<llvm::GetElementPtrInst>(member_ptr))
        {
            member_field_type = gep_inst->getResultElementType();
        }
        else
        {
            // For other cases, we can't easily determine the pointed-to type in LLVM 20+
            // Fall back to regular store
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

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Member assignment: member_field_type={}, value_type={}",
                  member_field_type->getTypeID(),
                  value_type->getTypeID());

        // Check if we're assigning a struct value to a struct field
        if (member_field_type->isStructTy() && value_type->isPointerTy())
        {
            // For LLVM 20+, we need to determine the pointed-to type differently
            llvm::Type *value_pointed_type = nullptr;
            if (auto *value_alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
            {
                value_pointed_type = value_alloca->getAllocatedType();
            }
            else if (auto *value_gep = llvm::dyn_cast<llvm::GetElementPtrInst>(value))
            {
                value_pointed_type = value_gep->getResultElementType();
            }
            else
            {
                // Can't determine type, fall back to regular store
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

            // If value points to the same struct type as the field expects
            if (value_pointed_type == member_field_type)
            {
                // Check if the struct type is fully defined (sized) before attempting memcpy
                if (!member_field_type->isSized())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Member assignment: Struct type is not sized, falling back to regular store");

                    // Fall back to regular store for unsized struct types
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

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Member assignment: Struct value assignment detected, using memcpy");

                // Use memcpy to copy the struct value instead of storing pointer
                auto &dl = module()->getDataLayout();
                uint64_t struct_size = dl.getTypeAllocSize(member_field_type);

                // Create memcpy call
                llvm::Type *i8_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(llvm_ctx()), 0);
                llvm::Type *size_type = dl.getIntPtrType(llvm_ctx());

                // Cast pointers to i8* for memcpy
                llvm::Value *dest = builder().CreateBitCast(member_ptr, i8_ptr_type, "dest.cast");
                llvm::Value *src = builder().CreateBitCast(value, i8_ptr_type, "src.cast");
                llvm::Value *size_val = llvm::ConstantInt::get(size_type, struct_size);

                // Call memcpy intrinsic
                llvm::Function *memcpy_fn = llvm::Intrinsic::getDeclaration(
                    module(),
                    llvm::Intrinsic::memcpy,
                    {i8_ptr_type, i8_ptr_type, size_type});

                builder().CreateCall(memcpy_fn, {dest, src, size_val, llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_ctx()), false)});

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Member assignment: Memcpy complete for struct of size {} bytes", struct_size);

                return value;
            }
        }

        // Regular assignment for non-struct types or when types don't match
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

        // Convert value to match element type if needed (e.g., i32 -> i8 for u8 arrays)
        // Try multiple approaches to determine the target element type:

        llvm::Type *target_store_type = nullptr;

        // Approach 1: Try to get element type from GEP instruction (most reliable)
        if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(element_ptr))
        {
            target_store_type = gep->getSourceElementType();
        }

        // Approach 2: Try from array access node's resolved type
        if (!target_store_type)
        {
            TypeRef element_type_ref = target->get_resolved_type();
            if (element_type_ref)
            {
                target_store_type = types().map_type(element_type_ref);
            }
        }

        // Approach 3: Try from array expression's pointer/array type
        if (!target_store_type && target->array())
        {
            TypeRef array_type = target->array()->get_resolved_type();
            if (array_type && array_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(array_type.get());
                if (ptr_type && ptr_type->pointee().is_valid())
                {
                    target_store_type = types().map_type(ptr_type->pointee());
                }
            }
            else if (array_type && array_type->kind() == Cryo::TypeKind::Array)
            {
                auto *arr_type = dynamic_cast<const Cryo::ArrayType *>(array_type.get());
                if (arr_type && arr_type->element().is_valid())
                {
                    target_store_type = types().map_type(arr_type->element());
                }
            }
        }

        // Apply truncation/extension if needed
        if (target_store_type && target_store_type != value->getType())
        {
            if (target_store_type->isIntegerTy() && value->getType()->isIntegerTy())
            {
                unsigned target_bits = target_store_type->getIntegerBitWidth();
                unsigned val_bits = value->getType()->getIntegerBitWidth();
                if (val_bits > target_bits)
                {
                    value = builder().CreateTrunc(value, target_store_type, "trunc");
                }
                else if (val_bits < target_bits)
                {
                    value = builder().CreateZExt(value, target_store_type, "zext");
                }
            }
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

        // Generate the pointer value (this is where we'll store to)
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

        // Check if this is a struct constructor call being assigned through a pointer
        // In this case, we need to construct in-place rather than on the stack
        if (auto *call_node = dynamic_cast<Cryo::CallExpressionNode *>(value_node))
        {
            std::string callee_name;
            if (auto *callee_ident = dynamic_cast<Cryo::IdentifierNode *>(call_node->callee()))
            {
                callee_name = callee_ident->name();
            }

            if (!callee_name.empty())
            {
                // Get the pointed-to type from the AST
                TypeRef target_resolved_type = target->operand()->get_resolved_type();
                llvm::Type *pointee_type = nullptr;

                if (target_resolved_type && target_resolved_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(target_resolved_type.get());
                    if (ptr_type && ptr_type->pointee())
                    {
                        // Try to resolve the pointee type to LLVM type
                        pointee_type = resolve_type_by_name(ptr_type->pointee().get()->display_name());
                    }
                }

                // Check if callee_name resolves to a struct type
                llvm::Type *struct_type = resolve_type_by_name(callee_name);
                if (struct_type && struct_type->isStructTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Deref assignment: detected struct constructor call for '{}', constructing in-place",
                              callee_name);

                    // Generate constructor arguments
                    std::vector<llvm::Value *> args;
                    for (const auto &arg : call_node->arguments())
                    {
                        if (arg)
                        {
                            llvm::Value *arg_val = generate_operand(arg.get());
                            if (arg_val)
                            {
                                args.push_back(arg_val);
                            }
                        }
                    }

                    // Look for the constructor function
                    llvm::Function *ctor = _calls->resolve_constructor(callee_name);
                    if (ctor)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Deref assignment: Calling constructor '{}' in-place with {} args",
                                  ctor->getName().str(), args.size());

                        // Call constructor with ptr as 'this' pointer (in-place construction)
                        std::vector<llvm::Value *> ctor_args;
                        ctor_args.push_back(ptr);
                        ctor_args.insert(ctor_args.end(), args.begin(), args.end());
                        builder().CreateCall(ctor, ctor_args);
                        return ptr;
                    }
                    else
                    {
                        // No constructor found - initialize fields directly
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Deref assignment: No constructor for '{}', initializing fields directly",
                                  callee_name);

                        auto *st = llvm::cast<llvm::StructType>(struct_type);
                        for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
                        {
                            llvm::Value *field_ptr = builder().CreateStructGEP(struct_type, ptr, i,
                                                                               "field." + std::to_string(i));
                            llvm::Value *arg = args[i];
                            if (arg->getType() != st->getElementType(i))
                            {
                                arg = cast_if_needed(arg, st->getElementType(i));
                            }
                            builder().CreateStore(arg, field_ptr);
                        }
                        return ptr;
                    }
                }
            }
        }

        // Generate the value to assign (standard path for non-struct-constructor cases)
        llvm::Value *value = generate_operand(value_node);
        if (!value)
        {
            report_error(ErrorCode::E0614_ASSIGNMENT_ERROR, value_node,
                         "Failed to generate assignment value");
            return nullptr;
        }

        // Check if we're assigning a struct value - need to memcpy instead of simple store
        if (value->getType()->isPointerTy())
        {
            // The value might be a pointer to a struct (e.g., from a previous constructor)
            // Check if we should do a memcpy
            TypeRef value_resolved_type = value_node->get_resolved_type();
            if (value_resolved_type &&
                (value_resolved_type->kind() == Cryo::TypeKind::Struct ||
                 value_resolved_type->kind() == Cryo::TypeKind::Class))
            {
                // Get the struct type to determine size
                llvm::Type *struct_type = resolve_type_by_name(value_resolved_type.get()->display_name());
                if (struct_type && struct_type->isStructTy() && struct_type->isSized())
                {
                    auto &data_layout = ctx().module()->getDataLayout();
                    uint64_t size = data_layout.getTypeAllocSize(struct_type);

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Deref assignment: memcpy struct of size {} bytes", size);

                    // Use memcpy to copy struct contents
                    builder().CreateMemCpy(
                        ptr,                                                        // dest
                        llvm::MaybeAlign(data_layout.getABITypeAlign(struct_type)), // dest align
                        value,                                                      // src
                        llvm::MaybeAlign(data_layout.getABITypeAlign(struct_type)), // src align
                        size                                                        // size
                    );
                    return ptr;
                }
                // If struct is opaque/unsized, fall through to standard store path
            }
        }

        // Store the value through the pointer (standard path for primitives)
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
                                                      TypeRef result_type)
    {
        if (!lhs || !rhs)
            return nullptr;

        llvm::Type *lhs_type = lhs->getType();
        llvm::Type *rhs_type = rhs->getType();

        // Handle pointer arithmetic
        if (lhs_type->isPointerTy() && rhs_type->isIntegerTy())
        {
            // Get the element type from the Cryo type system
            llvm::Type *element_type = nullptr;
            if (result_type && result_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(result_type.get());
                if (ptr_type && ptr_type->pointee())
                {
                    element_type = resolve_type_by_name(ptr_type->pointee().get()->display_name());
                }
            }
            return generate_pointer_arithmetic(op, lhs, rhs, element_type);
        }

        // Extract enum discriminant if operand is a tagged union struct
        if (lhs->getType()->isStructTy())
        {
            auto *st = llvm::cast<llvm::StructType>(lhs->getType());
            if (!st->isOpaque() && st->getNumElements() >= 1 &&
                st->getElementType(0)->isIntegerTy())
            {
                lhs = builder().CreateExtractValue(lhs, 0, "enum.disc");
            }
        }
        if (rhs->getType()->isStructTy())
        {
            auto *st = llvm::cast<llvm::StructType>(rhs->getType());
            if (!st->isOpaque() && st->getNumElements() >= 1 &&
                st->getElementType(0)->isIntegerTy())
            {
                rhs = builder().CreateExtractValue(rhs, 0, "enum.disc");
            }
        }

        // Ensure compatible types for non-pointer arithmetic
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

        std::string type_name = type->isStructTy() ? type->getStructName().str() : "<unknown>";

        report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, 
                     "Arithmetic operations not supported for type: " + type_name);
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

    llvm::Value *OperatorCodegen::generate_pointer_arithmetic(TokenKind op,
                                                              llvm::Value *ptr,
                                                              llvm::Value *offset,
                                                              llvm::Type *element_type)
    {
        llvm::IRBuilder<> &b = builder();
        llvm::LLVMContext &ctx = llvm_ctx();

        // If we have a known element type, use proper scaled pointer arithmetic
        // This ensures ptr + 1 advances by sizeof(element_type), not by 1 byte
        if (element_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Pointer arithmetic with element type (size matters)");

            switch (op)
            {
            case TokenKind::TK_PLUS:
                // Use GEP with the actual element type for proper scaling
                return b.CreateGEP(element_type, ptr, offset, "ptr.add");

            case TokenKind::TK_MINUS:
            {
                llvm::Value *neg_offset = b.CreateNeg(offset, "neg.offset");
                return b.CreateGEP(element_type, ptr, neg_offset, "ptr.sub");
            }

            default:
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                             "Unsupported pointer arithmetic operation");
                return nullptr;
            }
        }

        // Fallback: byte-level arithmetic when element type is unknown
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Pointer arithmetic fallback to byte-level (no element type)");

        switch (op)
        {
        case TokenKind::TK_PLUS:
            // For pointer + offset, use CreateGEP with byte-level arithmetic
            {
                llvm::Type *i8_type = llvm::Type::getInt8Ty(ctx);
                llvm::Value *result_ptr = b.CreateGEP(i8_type, ptr, offset, "ptr.add.bytes");
                return result_ptr;
            }

        case TokenKind::TK_MINUS:
            // For pointer - offset, use negative offset
            {
                llvm::Value *neg_offset = b.CreateNeg(offset, "neg.offset");
                llvm::Type *i8_type_sub = llvm::Type::getInt8Ty(ctx);
                llvm::Value *result_ptr_sub = b.CreateGEP(i8_type_sub, ptr, neg_offset, "ptr.sub.bytes");
                return result_ptr_sub;
            }

        default:
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                         "Unsupported pointer arithmetic operation");
            return nullptr;
        }
    }

    //===================================================================
    // Comparison Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_comparison(TokenKind op,
                                                      llvm::Value *lhs,
                                                      llvm::Value *rhs,
                                                      TypeRef operand_type,
                                                      Cryo::BinaryExpressionNode *node)
    {
        if (!lhs || !rhs)
            return nullptr;

        // Extract enum discriminants from tagged union structs ({ i32, [N x i8] })
        // so that enum values can be compared by their discriminant integer.
        // This mirrors the approach used in pattern matching (ControlFlowCodegen.cpp).
        if (lhs->getType()->isStructTy())
        {
            auto *st = llvm::cast<llvm::StructType>(lhs->getType());
            if (!st->isOpaque() && st->getNumElements() >= 1 &&
                st->getElementType(0)->isIntegerTy())
            {
                lhs = builder().CreateExtractValue(lhs, 0, "enum.disc");
            }
        }
        if (rhs->getType()->isStructTy())
        {
            auto *st = llvm::cast<llvm::StructType>(rhs->getType());
            if (!st->isOpaque() && st->getNumElements() >= 1 &&
                st->getElementType(0)->isIntegerTy())
            {
                rhs = builder().CreateExtractValue(rhs, 0, "enum.disc");
            }
        }

        // Log type information for debugging
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_comparison: lhs type = {}, rhs type = {}",
                  lhs->getType()->getTypeID(), rhs->getType()->getTypeID());

        // Get type names from AST for better error messages, with LLVM fallbacks
        std::string lhs_type_name = "unknown";
        std::string rhs_type_name = "unknown";
        if (node)
        {
            if (node->left() && node->left()->get_resolved_type())
                lhs_type_name = node->left()->get_resolved_type()->display_name();
            if (node->right() && node->right()->get_resolved_type())
                rhs_type_name = node->right()->get_resolved_type()->display_name();
        }
        // LLVM struct name fallback when AST resolved type is missing
        if (lhs_type_name == "unknown")
        {
            if (auto *st = llvm::dyn_cast<llvm::StructType>(lhs->getType()))
            {
                if (st->hasName()) lhs_type_name = st->getName().str();
            }
            else if (!ctx().current_type_display_name().empty())
                lhs_type_name = ctx().current_type_display_name();
        }
        if (rhs_type_name == "unknown")
        {
            if (auto *st = llvm::dyn_cast<llvm::StructType>(rhs->getType()))
            {
                if (st->hasName()) rhs_type_name = st->getName().str();
            }
        }

        // Ensure compatible types
        if (!ensure_compatible_types(lhs, rhs))
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "generate_comparison: Types are incompatible and cannot be converted. "
                      "lhs type ID = {}, rhs type ID = {}",
                      lhs->getType()->getTypeID(), rhs->getType()->getTypeID());

            std::string e = "Cannot compare `" + lhs_type_name + "` with `" + rhs_type_name + "`";

            if (node)
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node, e);
            else
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, e.c_str());
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
            return generate_pointer_comparison(op, lhs, rhs, node);
        }

        // Handle struct comparison using memcmp
        // This is needed for generic types where T is a struct (e.g., Option<String>::contains)
        if (type->isStructTy() && rhs->getType()->isStructTy())
        {
            // Only support equality/inequality for structs
            if (op != TokenKind::TK_EQUALEQUAL && op != TokenKind::TK_EXCLAIMEQUAL)
            {
                {
                    auto diag = Diag::error(ErrorCode::E0615_BINARY_OPERATION_ERROR,
                                            "binary operation cannot be applied to type '" + lhs_type_name + "'");
                    if (node) diag.at(node);
                    diag.with_note("struct types do not support ordering comparisons (<, >, <=, >=)");
                    emit_diagnostic(std::move(diag));
                }
                return nullptr;
            }

            // Get struct size using DataLayout
            const llvm::DataLayout &dl = module()->getDataLayout();
            uint64_t struct_size = dl.getTypeAllocSize(type);

            // Need to store the struct values to memory to get pointers for memcmp
            llvm::IRBuilder<> &b = builder();
            llvm::Function *fn = b.GetInsertBlock()->getParent();

            // Create allocas for both values
            llvm::AllocaInst *lhs_alloca = b.CreateAlloca(type, nullptr, "cmp.lhs");
            llvm::AllocaInst *rhs_alloca = b.CreateAlloca(rhs->getType(), nullptr, "cmp.rhs");
            b.CreateStore(lhs, lhs_alloca);
            b.CreateStore(rhs, rhs_alloca);

            // Get or declare memcmp
            llvm::FunctionCallee memcmp_fn = module()->getOrInsertFunction(
                "memcmp",
                llvm::FunctionType::get(
                    llvm::Type::getInt32Ty(llvm_ctx()),
                    {llvm::PointerType::get(llvm_ctx(), 0),
                     llvm::PointerType::get(llvm_ctx(), 0),
                     llvm::Type::getInt64Ty(llvm_ctx())},
                    false));

            // Call memcmp(lhs_ptr, rhs_ptr, size)
            llvm::Value *cmp_result = b.CreateCall(
                memcmp_fn,
                {lhs_alloca, rhs_alloca, llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), struct_size)},
                "memcmp.result");

            // memcmp returns 0 if equal
            if (op == TokenKind::TK_EQUALEQUAL)
            {
                return b.CreateICmpEQ(cmp_result, llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0), "eq");
            }
            else // TK_EXCLAIMEQUAL
            {
                return b.CreateICmpNE(cmp_result, llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0), "ne");
            }
        }

        std::string e = "Comparison not supported for type `" + lhs_type_name + "`";
        if (lhs_type_name != rhs_type_name)
            e += " with `" + rhs_type_name + "`";

        if (node)
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node, e);
        else
            report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, e.c_str());
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
                                                              llvm::Value *rhs,
                                                              Cryo::BinaryExpressionNode *node)
    {
        llvm::IRBuilder<> &b = builder();

        switch (op)
        {
        case TokenKind::TK_EQUALEQUAL:
            return b.CreateICmpEQ(lhs, rhs, "ptr_eq");

        case TokenKind::TK_EXCLAIMEQUAL:
            return b.CreateICmpNE(lhs, rhs, "ptr_ne");

        case TokenKind::TK_L_ANGLE:
            return b.CreateICmpULT(lhs, rhs, "ptr_lt");

        case TokenKind::TK_R_ANGLE:
            return b.CreateICmpUGT(lhs, rhs, "ptr_gt");

        case TokenKind::TK_LESSEQUAL:
            return b.CreateICmpULE(lhs, rhs, "ptr_le");

        case TokenKind::TK_GREATEREQUAL:
            return b.CreateICmpUGE(lhs, rhs, "ptr_ge");

        default:
        {
            std::string op_str = node ? std::string(node->operator_token().text()) : "comparison";
            std::string e = "Only equality comparisons (== and !=) are supported for pointers, not `" + op_str + "`";
            if (node)
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node, e);
            else
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, e.c_str());
            return nullptr;
        }
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

        llvm::BasicBlock *and_block = b.GetInsertBlock();
        if (!and_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for logical AND");
            return nullptr;
        }

        llvm::Function *fn = and_block->getParent();
        if (!fn)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for logical AND");
            return nullptr;
        }

        // Create blocks for short-circuit evaluation
        llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(llvm_ctx(), "and.rhs", fn);
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(llvm_ctx(), "and.merge", fn);

        // Evaluate LHS
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
            return nullptr;

        // Convert to bool if needed — guard against struct types which can't be compared with ICmp
        if (!lhs->getType()->isIntegerTy(1))
        {
            if (!lhs->getType()->isIntegerTy() && !lhs->getType()->isPointerTy() && !lhs->getType()->isFloatingPointTy())
            {
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                             "Cannot convert struct/aggregate type to boolean for logical AND");
                return nullptr;
            }
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
            if (!rhs->getType()->isIntegerTy() && !rhs->getType()->isPointerTy() && !rhs->getType()->isFloatingPointTy())
            {
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                             "Cannot convert struct/aggregate type to boolean for logical AND");
                return nullptr;
            }
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

        llvm::BasicBlock *or_block = b.GetInsertBlock();
        if (!or_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for logical OR");
            return nullptr;
        }

        llvm::Function *fn = or_block->getParent();
        if (!fn)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for logical OR");
            return nullptr;
        }

        // Create blocks for short-circuit evaluation
        llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(llvm_ctx(), "or.rhs", fn);
        llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(llvm_ctx(), "or.merge", fn);

        // Evaluate LHS
        llvm::Value *lhs = generate_operand(node->left());
        if (!lhs)
            return nullptr;

        // Convert to bool if needed — guard against struct types which can't be compared with ICmp
        if (!lhs->getType()->isIntegerTy(1))
        {
            if (!lhs->getType()->isIntegerTy() && !lhs->getType()->isPointerTy() && !lhs->getType()->isFloatingPointTy())
            {
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                             "Cannot convert struct/aggregate type to boolean for logical OR");
                return nullptr;
            }
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
            if (!rhs->getType()->isIntegerTy() && !rhs->getType()->isPointerTy() && !rhs->getType()->isFloatingPointTy())
            {
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                             "Cannot convert struct/aggregate type to boolean for logical OR");
                return nullptr;
            }
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

        // For bitwise operations, ensure both operands have the same type
        // This is especially important for shifts where LLVM requires matching types
        llvm::Type *lhs_type = lhs->getType();
        llvm::Type *rhs_type = rhs->getType();

        if (lhs_type != rhs_type && lhs_type->isIntegerTy() && rhs_type->isIntegerTy())
        {
            unsigned lhs_bits = lhs_type->getIntegerBitWidth();
            unsigned rhs_bits = rhs_type->getIntegerBitWidth();

            // Extend the smaller type to match the larger one
            if (lhs_bits > rhs_bits)
            {
                rhs = b.CreateZExt(rhs, lhs_type, "rhs.zext");
            }
            else if (rhs_bits > lhs_bits)
            {
                lhs = b.CreateZExt(lhs, rhs_type, "lhs.zext");
            }
        }

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

        llvm::IRBuilder<> &b = builder();
        llvm::LLVMContext &ctx = llvm_ctx();
        llvm::Module *mod = module();

        // Types we'll need
        llvm::Type *i8_type = llvm::Type::getInt8Ty(ctx);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(ctx);
        llvm::Type *ptr_type = llvm::PointerType::get(ctx, 0);

        // Get or declare strlen
        llvm::Function *strlen_fn = mod->getFunction("strlen");
        if (!strlen_fn)
        {
            llvm::FunctionType *strlen_type = llvm::FunctionType::get(i64_type, {ptr_type}, false);
            strlen_fn = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", mod);
        }

        // Get or declare malloc
        llvm::Function *malloc_fn = mod->getFunction("malloc");
        if (!malloc_fn)
        {
            llvm::FunctionType *malloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
        }

        // Get or declare memcpy
        llvm::Function *memcpy_fn = mod->getFunction("memcpy");
        if (!memcpy_fn)
        {
            llvm::FunctionType *memcpy_type = llvm::FunctionType::get(ptr_type, {ptr_type, ptr_type, i64_type}, false);
            memcpy_fn = llvm::Function::Create(memcpy_type, llvm::Function::ExternalLinkage, "memcpy", mod);
        }

        // 1. Get the length of both strings
        llvm::Value *lhs_len = b.CreateCall(strlen_fn, {lhs}, "lhs.len");
        llvm::Value *rhs_len = b.CreateCall(strlen_fn, {rhs}, "rhs.len");

        // 2. Allocate new memory: len1 + len2 + 1 (for null terminator)
        llvm::Value *total_len = b.CreateAdd(lhs_len, rhs_len, "total.len");
        llvm::Value *alloc_size = b.CreateAdd(total_len, llvm::ConstantInt::get(i64_type, 1), "alloc.size");
        llvm::Value *new_str = b.CreateCall(malloc_fn, {alloc_size}, "new.str");

        // 3. Copy the first string to the new buffer
        b.CreateCall(memcpy_fn, {new_str, lhs, lhs_len});

        // 4. Copy the second string after the first
        llvm::Value *rhs_pos = b.CreateGEP(i8_type, new_str, lhs_len, "rhs.pos");
        b.CreateCall(memcpy_fn, {rhs_pos, rhs, rhs_len});

        // 5. Add null terminator at position total_len
        llvm::Value *null_pos = b.CreateGEP(i8_type, new_str, total_len, "null.pos");
        b.CreateStore(llvm::ConstantInt::get(i8_type, 0), null_pos);

        // 6. Return the new string
        return new_str;
    }

    llvm::Value *OperatorCodegen::generate_string_char_concat(llvm::Value *str, llvm::Value *chr)
    {
        if (!str || !chr)
            return nullptr;

        llvm::IRBuilder<> &b = builder();
        llvm::LLVMContext &ctx = llvm_ctx();
        llvm::Module *mod = module();

        // Types we'll need
        llvm::Type *i8_type = llvm::Type::getInt8Ty(ctx);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(ctx);
        llvm::Type *ptr_type = llvm::PointerType::get(ctx, 0);

        // Cast the character value to i8 if needed
        if (chr->getType() != i8_type && chr->getType()->isIntegerTy())
        {
            chr = b.CreateTrunc(chr, i8_type, "tochar");
        }

        // Get or declare strlen
        llvm::Function *strlen_fn = mod->getFunction("strlen");
        if (!strlen_fn)
        {
            llvm::FunctionType *strlen_type = llvm::FunctionType::get(i64_type, {ptr_type}, false);
            strlen_fn = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", mod);
        }

        // Get or declare malloc
        llvm::Function *malloc_fn = mod->getFunction("malloc");
        if (!malloc_fn)
        {
            llvm::FunctionType *malloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
        }

        // Get or declare memcpy
        llvm::Function *memcpy_fn = mod->getFunction("memcpy");
        if (!memcpy_fn)
        {
            llvm::FunctionType *memcpy_type = llvm::FunctionType::get(ptr_type, {ptr_type, ptr_type, i64_type}, false);
            memcpy_fn = llvm::Function::Create(memcpy_type, llvm::Function::ExternalLinkage, "memcpy", mod);
        }

        // 1. Get the length of the original string
        llvm::Value *str_len = b.CreateCall(strlen_fn, {str}, "str.len");

        // 2. Allocate new memory: original_len + 1 (for char) + 1 (for null terminator)
        llvm::Value *new_len = b.CreateAdd(str_len, llvm::ConstantInt::get(i64_type, 2), "new.len");
        llvm::Value *new_str = b.CreateCall(malloc_fn, {new_len}, "new.str");

        // 3. Copy the original string to the new buffer
        b.CreateCall(memcpy_fn, {new_str, str, str_len});

        // 4. Append the character at position str_len
        llvm::Value *char_pos = b.CreateGEP(i8_type, new_str, str_len, "char.pos");
        b.CreateStore(chr, char_pos);

        // 5. Add null terminator at position str_len + 1
        llvm::Value *null_pos = b.CreateGEP(i8_type, new_str,
                                            b.CreateAdd(str_len, llvm::ConstantInt::get(i64_type, 1)),
                                            "null.pos");
        b.CreateStore(llvm::ConstantInt::get(i8_type, 0), null_pos);

        // 6. Return the new string
        return new_str;
    }

    llvm::Value *OperatorCodegen::generate_char_string_concat(llvm::Value *chr, llvm::Value *str)
    {
        if (!chr || !str)
            return nullptr;

        llvm::IRBuilder<> &b = builder();
        llvm::LLVMContext &ctx = llvm_ctx();
        llvm::Module *mod = module();

        // Types we'll need
        llvm::Type *i8_type = llvm::Type::getInt8Ty(ctx);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(ctx);
        llvm::Type *ptr_type = llvm::PointerType::get(ctx, 0);

        // Cast the character value to i8 if needed
        if (chr->getType() != i8_type && chr->getType()->isIntegerTy())
        {
            chr = b.CreateTrunc(chr, i8_type, "tochar");
        }

        // Get or declare strlen
        llvm::Function *strlen_fn = mod->getFunction("strlen");
        if (!strlen_fn)
        {
            llvm::FunctionType *strlen_type = llvm::FunctionType::get(i64_type, {ptr_type}, false);
            strlen_fn = llvm::Function::Create(strlen_type, llvm::Function::ExternalLinkage, "strlen", mod);
        }

        // Get or declare malloc
        llvm::Function *malloc_fn = mod->getFunction("malloc");
        if (!malloc_fn)
        {
            llvm::FunctionType *malloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
        }

        // Get or declare memcpy
        llvm::Function *memcpy_fn = mod->getFunction("memcpy");
        if (!memcpy_fn)
        {
            llvm::FunctionType *memcpy_type = llvm::FunctionType::get(ptr_type, {ptr_type, ptr_type, i64_type}, false);
            memcpy_fn = llvm::Function::Create(memcpy_type, llvm::Function::ExternalLinkage, "memcpy", mod);
        }

        // 1. Get the length of the original string
        llvm::Value *str_len = b.CreateCall(strlen_fn, {str}, "str.len");

        // 2. Allocate new memory: 1 (for char) + original_len + 1 (for null terminator)
        llvm::Value *new_len = b.CreateAdd(str_len, llvm::ConstantInt::get(i64_type, 2), "new.len");
        llvm::Value *new_str = b.CreateCall(malloc_fn, {new_len}, "new.str");

        // 3. Store the character at position 0
        b.CreateStore(chr, new_str);

        // 4. Copy the original string starting at position 1
        llvm::Value *str_pos = b.CreateGEP(i8_type, new_str, llvm::ConstantInt::get(i64_type, 1), "str.pos");
        b.CreateCall(memcpy_fn, {str_pos, str, str_len});

        // 5. Add null terminator at position str_len + 1
        llvm::Value *null_pos = b.CreateGEP(i8_type, new_str,
                                            b.CreateAdd(str_len, llvm::ConstantInt::get(i64_type, 1)),
                                            "null.pos");
        b.CreateStore(llvm::ConstantInt::get(i8_type, 0), null_pos);

        // 6. Return the new string
        return new_str;
    }

    //===================================================================
    // Unary Operations
    //===================================================================

    llvm::Value *OperatorCodegen::generate_negation(llvm::Value *operand, TypeRef type)
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

    llvm::Value *OperatorCodegen::generate_dereference(llvm::Value *ptr, TypeRef pointee_type)
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
            // Cannot dereference void* — return the pointer as-is
            if (pointee_type->kind() == Cryo::TypeKind::Void)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Dereference of void* — returning pointer as-is");
                return ptr;
            }
            load_type = types().map_type(pointee_type);
        }

        // Fallback to i32 if we can't determine type
        if (!load_type || load_type->isVoidTy())
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

        // Create new value
        llvm::Value *new_val;
        if (current->getType()->isPointerTy())
        {
            // Pointer increment: advance by one element (byte-level GEP)
            new_val = builder().CreateGEP(
                llvm::Type::getInt8Ty(builder().getContext()),
                current,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder().getContext()), 1),
                "ptr.inc");
        }
        else if (current->getType()->isIntegerTy())
        {
            llvm::Value *one = get_increment_value(current->getType());
            if (!one)
                return nullptr;
            new_val = builder().CreateAdd(current, one, "inc");
        }
        else if (current->getType()->isFloatingPointTy())
        {
            llvm::Value *one = get_increment_value(current->getType());
            if (!one)
                return nullptr;
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

        // Create new value
        llvm::Value *new_val;
        if (current->getType()->isPointerTy())
        {
            // Pointer decrement: move back by one element (byte-level GEP with -1)
            new_val = builder().CreateGEP(
                llvm::Type::getInt8Ty(builder().getContext()),
                current,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder().getContext()), -1ULL),
                "ptr.dec");
        }
        else if (current->getType()->isIntegerTy())
        {
            llvm::Value *one = get_increment_value(current->getType());
            if (!one)
                return nullptr;
            new_val = builder().CreateSub(current, one, "dec");
        }
        else if (current->getType()->isFloatingPointTy())
        {
            llvm::Value *one = get_increment_value(current->getType());
            if (!one)
                return nullptr;
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

        // Clear stale result before visiting so that if the expression generation
        // fails (returns nullptr without calling set_result), we return nullptr
        // instead of a stale value from a previous expression.
        ctx().set_result(nullptr);

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

            // Try global from context map
            auto &globals = ctx().globals_map();
            auto it = globals.find(name);
            if (it != globals.end())
                return it->second;

            // Try LLVM module's global variables with various name patterns
            std::vector<std::string> candidates = {name};
            if (!ctx().namespace_context().empty())
            {
                candidates.push_back(ctx().namespace_context() + "::" + name);
            }

            for (const auto &candidate : candidates)
            {
                if (llvm::GlobalVariable *global = module()->getGlobalVariable(candidate))
                {
                    return global;
                }
            }

            return nullptr;
        }

        // Handle member access (e.g., this.field, obj.field)
        if (auto *member = dynamic_cast<Cryo::MemberAccessNode *>(expr))
        {
            std::string member_name = member->member();

            // Get a pointer to the struct object
            // For identifiers, we need special handling:
            // - If the identifier holds a pointer (like 'this'), load to get the pointer value
            // - If the identifier holds a by-value struct, use the alloca address directly
            llvm::Value *object = nullptr;

            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(member->object()))
            {
                std::string var_name = identifier->name();

                // Get the alloca for the identifier
                llvm::AllocaInst *alloca = values().get_alloca(var_name);
                if (alloca)
                {
                    // Check what type the alloca holds
                    llvm::Type *alloca_type = alloca->getAllocatedType();

                    if (alloca_type->isPointerTy())
                    {
                        // The alloca stores a pointer (like 'this') - load to get the pointer value
                        object = create_load(alloca, alloca_type, var_name + ".load");
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Loaded pointer from alloca for '{}'", var_name);
                    }
                    else if (alloca_type->isStructTy())
                    {
                        // The alloca stores a by-value struct - use alloca address directly
                        object = alloca;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Using alloca address for by-value struct '{}'", var_name);
                    }
                    else
                    {
                        // Other types - load the value
                        object = create_load(alloca, alloca_type, var_name + ".load");
                    }
                }
                else
                {
                    // Fallback to generate_operand for globals etc.
                    object = generate_operand(member->object());
                }
            }
            else if (auto *nested_member = dynamic_cast<Cryo::MemberAccessNode *>(member->object()))
            {
                // For nested member access (obj.a.b), get the address of the inner member
                object = get_lvalue_address(nested_member);
            }
            else if (auto *array_access = dynamic_cast<Cryo::ArrayAccessNode *>(member->object()))
            {
                // For array element member access (e.g., entries[i].state = ...), get the element address
                object = get_lvalue_address(array_access);
            }
            else
            {
                // For other expressions, generate the value and hope it's a pointer
                object = generate_operand(member->object());
            }

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
                          "get_lvalue_address: Member access object is not a pointer type (type: {})",
                          object->getType()->isStructTy() ? "struct" : "other");
                return nullptr;
            }

            // Resolve struct type and field index
            llvm::StructType *struct_type = nullptr;
            unsigned field_idx = 0;

            // Get the resolved type of the object expression
            TypeRef obj_type = member->object()->get_resolved_type();
            std::string type_name;

            if (obj_type.is_valid())
            {
                type_name = obj_type.get()->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: obj_type is valid, display_name='{}', kind={}",
                          type_name, static_cast<int>(obj_type->kind()));
                // Handle pointer types - get the pointee type name
                if (obj_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
                    if (ptr_type && ptr_type->pointee().is_valid())
                    {
                        type_name = ptr_type->pointee().get()->display_name();
                    }
                }
                else if (!type_name.empty() && type_name.back() == '*')
                {
                    type_name.pop_back();
                }
            }

            // If type_name is still empty, try fallback methods
            if (type_name.empty())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: type_name empty, trying fallbacks");
                // Fallback: If object is an identifier, look up its type from variable_types_map
                if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(member->object()))
                {
                    const std::string &var_name = identifier->name();
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find(var_name);
                    if (it != var_types.end() && it->second.is_valid())
                    {
                        TypeRef obj_type = it->second;
                        type_name = obj_type.get()->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Found type for '{}' in variable_types_map: {}",
                                  var_name, type_name);
                        // Handle pointer types - get the pointee type name
                        if (obj_type->kind() == Cryo::TypeKind::Pointer)
                        {
                            auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
                            if (ptr_type && ptr_type->pointee().is_valid())
                            {
                                type_name = ptr_type->pointee().get()->display_name();
                            }
                        }
                        else if (!type_name.empty() && type_name.back() == '*')
                        {
                            type_name.pop_back();
                        }
                    }
                    else if (var_name == "this")
                    {
                        // Special case: Fallback to current_type_name for 'this' if not in map
                        type_name = ctx().current_type_name();
                    }
                }
                // Handle nested member access (e.g., obj.field.subfield or this.current_chunk.next)
                else if (auto *nested_member = dynamic_cast<Cryo::MemberAccessNode *>(member->object()))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "get_lvalue_address: Handling nested member access, nested_member='{}', outer_member='{}'",
                              nested_member->member(), member_name);
                    // Recursively resolve the type of the nested member access
                    // First, get the base type (e.g., for obj.field, get type of 'obj')
                    std::string base_type_name;
                    if (auto *base_ident = dynamic_cast<Cryo::IdentifierNode *>(nested_member->object()))
                    {
                        const std::string &var_name = base_ident->name();
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find(var_name);
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            TypeRef base_obj_type = it->second;
                            base_type_name = base_obj_type.get()->display_name();
                            // Handle pointer types - get the pointee type name
                            if (base_obj_type->kind() == Cryo::TypeKind::Pointer)
                            {
                                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(base_obj_type.get());
                                if (ptr_type && ptr_type->pointee().is_valid())
                                {
                                    base_type_name = ptr_type->pointee().get()->display_name();
                                }
                            }
                            else if (!base_type_name.empty() && base_type_name.back() == '*')
                            {
                                base_type_name.pop_back();
                            }
                        }
                        else if (var_name == "this")
                        {
                            // Special case: Fallback to current_type_name for 'this' if not in map
                            base_type_name = ctx().current_type_name();
                        }
                    }

                    // Now look up the nested member's type (e.g., current_chunk in Arena -> ArenaChunk*)
                    if (!base_type_name.empty())
                    {
                        std::string nested_field = nested_member->member();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Looking up field '{}' in struct '{}'",
                                  nested_field, base_type_name);

                        // Use TemplateRegistry to get field type (it stores struct field types)
                        bool found_field = false;
                        if (auto *template_reg = ctx().template_registry())
                        {
                            // Try both base name and qualified name
                            const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(base_type_name);
                            if (field_info)
                            {
                                // Find the field in field_names and get its type from field_types
                                for (size_t i = 0; i < field_info->field_names.size(); ++i)
                                {
                                    if (field_info->field_names[i] == nested_field && i < field_info->field_types.size())
                                    {
                                        TypeRef field_type = field_info->field_types[i];
                                        if (field_type.is_valid())
                                        {
                                            type_name = field_type.get()->display_name();
                                            // If it's a pointer, get the pointee type name
                                            if (field_type->kind() == Cryo::TypeKind::Pointer)
                                            {
                                                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                                if (ptr_type && ptr_type->pointee().is_valid())
                                                {
                                                    type_name = ptr_type->pointee().get()->display_name();
                                                }
                                            }
                                            else if (!type_name.empty() && type_name.back() == '*')
                                            {
                                                type_name.pop_back();
                                            }
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "get_lvalue_address: Resolved nested member {} in {} to type {} (via TemplateRegistry)",
                                                      nested_field, base_type_name, type_name);
                                            found_field = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        if (!found_field)
                        {
                            // Fallback: try StructType::field_type (in case fields were populated there)
                            TypeRef struct_type_ref = ctx().symbols().lookup_struct_type(base_type_name);
                            if (struct_type_ref.is_valid() && struct_type_ref->kind() == Cryo::TypeKind::Struct)
                            {
                                auto *struct_ty = static_cast<const Cryo::StructType *>(struct_type_ref.get());
                                auto field_type_opt = struct_ty->field_type(nested_field);
                                if (field_type_opt.has_value())
                                {
                                    TypeRef field_type = field_type_opt.value();
                                    type_name = field_type.get()->display_name();
                                    // If it's a pointer, get the pointee type name
                                    if (field_type->kind() == Cryo::TypeKind::Pointer)
                                    {
                                        auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                        if (ptr_type && ptr_type->pointee().is_valid())
                                        {
                                            type_name = ptr_type->pointee().get()->display_name();
                                        }
                                    }
                                    else if (!type_name.empty() && type_name.back() == '*')
                                    {
                                        type_name.pop_back();
                                    }
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "get_lvalue_address: Resolved nested member {} in {} to type {} (via StructType)",
                                              nested_field, base_type_name, type_name);
                                    found_field = true;
                                }
                            }
                        }

                        if (!found_field)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "get_lvalue_address: field_type not found for '{}' in struct '{}' (checked TemplateRegistry and StructType)",
                                      nested_field, base_type_name);
                        }
                    }
                }
            }

            // Handle ArrayAccessNode object (e.g., entries[i].state = ...)
            if (type_name.empty())
            {
                if (auto *array_access = dynamic_cast<Cryo::ArrayAccessNode *>(member->object()))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "get_lvalue_address: Handling ArrayAccessNode object for member access");

                    // Get the type of the array expression (e.g., entries in entries[i])
                    TypeRef array_type = array_access->array()->get_resolved_type();
                    if (!array_type)
                    {
                        // Try from variable_types_map if array is an identifier
                        if (auto *arr_id = dynamic_cast<Cryo::IdentifierNode *>(array_access->array()))
                        {
                            auto &var_types = ctx().variable_types_map();
                            auto it = var_types.find(arr_id->name());
                            if (it != var_types.end() && it->second.is_valid())
                                array_type = it->second;
                        }
                        // Try if array is a member access (e.g., this.entries)
                        else if (auto *arr_member = dynamic_cast<Cryo::MemberAccessNode *>(array_access->array()))
                        {
                            std::string arr_type_name;
                            // Use variable_types_map first (works correctly in monomorphized code)
                            if (auto *base_ident = dynamic_cast<Cryo::IdentifierNode *>(arr_member->object()))
                            {
                                auto &var_types = ctx().variable_types_map();
                                auto vit = var_types.find(base_ident->name());
                                if (vit != var_types.end() && vit->second.is_valid())
                                {
                                    TypeRef base_type = vit->second;
                                    arr_type_name = base_type->display_name();
                                    if (base_type->kind() == Cryo::TypeKind::Pointer)
                                    {
                                        auto *ptr = dynamic_cast<const Cryo::PointerType *>(base_type.get());
                                        if (ptr && ptr->pointee())
                                            arr_type_name = ptr->pointee()->display_name();
                                    }
                                    else if (!arr_type_name.empty() && arr_type_name.back() == '*')
                                        arr_type_name.pop_back();
                                }
                                else if (base_ident->name() == "this")
                                {
                                    arr_type_name = ctx().current_type_name();
                                }
                            }
                            // Fallback: try AST resolved type
                            if (arr_type_name.empty())
                            {
                                TypeRef arr_obj_type = arr_member->object()->get_resolved_type();
                                if (arr_obj_type)
                                {
                                    arr_type_name = arr_obj_type->display_name();
                                    if (arr_obj_type->kind() == Cryo::TypeKind::Pointer)
                                    {
                                        auto *ptr = dynamic_cast<const Cryo::PointerType *>(arr_obj_type.get());
                                        if (ptr && ptr->pointee())
                                            arr_type_name = ptr->pointee()->display_name();
                                    }
                                }
                            }
                            if (!arr_type_name.empty())
                            {
                                if (auto *template_reg = ctx().template_registry())
                                {
                                    std::vector<std::string> candidates = {arr_type_name};
                                    if (!ctx().namespace_context().empty())
                                        candidates.push_back(ctx().namespace_context() + "::" + arr_type_name);

                                    // Look up the field by name in the TemplateRegistry
                                    std::string field_name = arr_member->member();
                                    for (const auto &candidate : candidates)
                                    {
                                        const auto *field_info = template_reg->get_struct_field_types(candidate);
                                        if (field_info)
                                        {
                                            for (size_t fi = 0; fi < field_info->field_names.size(); ++fi)
                                            {
                                                if (field_info->field_names[fi] == field_name && fi < field_info->field_types.size())
                                                {
                                                    array_type = field_info->field_types[fi];
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "get_lvalue_address: Resolved array field '{}' type to '{}'",
                                                              field_name, array_type ? array_type->display_name() : "<null>");
                                                    break;
                                                }
                                            }
                                            if (array_type)
                                                break;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Extract element type from array/pointer type
                    if (array_type)
                    {
                        if (array_type->kind() == Cryo::TypeKind::Pointer)
                        {
                            auto *ptr = dynamic_cast<const Cryo::PointerType *>(array_type.get());
                            if (ptr && ptr->pointee())
                                type_name = ptr->pointee()->display_name();
                        }
                        else if (array_type->kind() == Cryo::TypeKind::Array)
                        {
                            auto *arr = dynamic_cast<const Cryo::ArrayType *>(array_type.get());
                            if (arr && arr->element())
                                type_name = arr->element()->display_name();
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

            // Check if we're inside a generic instantiation context and need to redirect
            // the type name (e.g., "HashSetEntry<T>" -> "HashSetEntry_string")
            {
                auto *visitor = ctx().visitor();
                if (visitor)
                {
                    auto *generics = visitor->get_generics();
                    if (generics && generics->in_type_param_scope())
                    {
                        // First try exact base name match
                        std::string redirected = generics->get_instantiated_scope_name(type_name);
                        if (!redirected.empty())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "get_lvalue_address: Redirecting type {} -> {} in generic context",
                                      type_name, redirected);
                            type_name = redirected;
                        }
                        else
                        {
                            // Try substituting type parameters in the annotation
                            redirected = generics->substitute_type_annotation(type_name);
                            if (!redirected.empty())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "get_lvalue_address: Substituted type annotation {} -> {} in generic context",
                                          type_name, redirected);
                                type_name = redirected;
                            }
                            else if (type_name.find('<') != std::string::npos)
                            {
                                // Manual fallback: parse "HashSetEntry<T>" and resolve T individually
                                size_t angle = type_name.find('<');
                                std::string base = type_name.substr(0, angle);
                                std::string args = type_name.substr(angle + 1);
                                if (!args.empty() && args.back() == '>')
                                    args.pop_back();

                                TypeRef resolved_param = generics->resolve_type_param(args);
                                if (resolved_param.is_valid())
                                {
                                    std::string mangled = base + "_" + resolved_param->display_name();
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "get_lvalue_address: Manual fallback resolved {} -> {} in generic context",
                                              type_name, mangled);
                                    type_name = mangled;
                                }
                            }
                        }
                    }
                }
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
                // Fallback: try TemplateRegistry for cross-module struct field lookup
                if (auto *template_reg = ctx().template_registry())
                {
                    std::vector<std::string> candidates = {type_name};

                    // Add LLVM struct type name as candidate
                    if (struct_type && struct_type->hasName())
                    {
                        std::string llvm_name = struct_type->getName().str();
                        if (llvm_name != type_name)
                            candidates.push_back(llvm_name);
                    }

                    // Add namespace prefix
                    if (!ctx().namespace_context().empty())
                        candidates.push_back(ctx().namespace_context() + "::" + type_name);

                    // Add type_namespace_map lookup
                    std::string type_ns = ctx().get_type_namespace(type_name);
                    if (!type_ns.empty())
                        candidates.push_back(type_ns + "::" + type_name);

                    // Try base template name (e.g., "HashSetEntry" from "HashSetEntry_string")
                    size_t underscore_pos = type_name.find('_');
                    if (underscore_pos != std::string::npos)
                        candidates.push_back(type_name.substr(0, underscore_pos));

                    for (const auto &candidate : candidates)
                    {
                        const TemplateRegistry::StructFieldInfo *field_info =
                            template_reg->get_struct_field_types(candidate);
                        if (field_info && !field_info->field_names.empty())
                        {
                            for (size_t i = 0; i < field_info->field_names.size(); ++i)
                            {
                                if (field_info->field_names[i] == member_name)
                                {
                                    field_idx_signed = static_cast<int>(i);
                                    ctx().register_struct_fields(type_name, field_info->field_names);
                                    break;
                                }
                            }
                            if (field_idx_signed >= 0)
                                break;
                        }
                    }
                }

                if (field_idx_signed < 0)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "get_lvalue_address: Field '{}' not found in struct '{}'",
                              member_name, type_name);
                    return nullptr;
                }
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
            // For array access, we need a pointer to the array, not the array value
            // Handle identifier arrays specially - get the alloca address
            llvm::Value *array_val = nullptr;
            bool is_member_access_array = false;

            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(array_access->array()))
            {
                std::string array_name = identifier->name();

                // Try to get the alloca for the array - this gives us a pointer to the array
                llvm::AllocaInst *alloca = values().get_alloca(array_name);
                if (alloca)
                {
                    llvm::Type *alloca_type = alloca->getAllocatedType();

                    // If the alloca holds an array type, use the alloca directly as the base pointer
                    if (alloca_type->isArrayTy())
                    {
                        array_val = alloca;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Using alloca for array '{}'", array_name);
                    }
                    // If the alloca holds a pointer (e.g., T*), load it to get the pointer value
                    else if (alloca_type->isPointerTy())
                    {
                        array_val = create_load(alloca, alloca_type, array_name + ".load");
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "get_lvalue_address: Loaded pointer from alloca for '{}'", array_name);
                    }
                    else
                    {
                        // Other types - try generate_operand as fallback
                        array_val = generate_operand(array_access->array());
                    }
                }
            }
            else if (auto *member_access = dynamic_cast<Cryo::MemberAccessNode *>(array_access->array()))
            {
                // For member access arrays (e.g., this.data[i]), get the ADDRESS of the member
                // field rather than its value. This gives us a pointer to the array field in
                // the struct, which we can then GEP into for element access.
                array_val = get_lvalue_address(member_access);
                is_member_access_array = true;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Got member address for array field '{}'",
                          member_access->member());
            }

            // Fallback to generate_operand for other expressions or if above methods failed
            if (!array_val)
            {
                array_val = generate_operand(array_access->array());
            }

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
                          "get_lvalue_address: Array expression is not a pointer type (type ID: {})",
                          array_val->getType()->getTypeID());
                return nullptr;
            }

            // Determine element type
            llvm::Type *element_type = nullptr;
            TypeRef array_type = array_access->array()->get_resolved_type();

            // If no resolved type, check variable_types_map (like in ExpressionCodegen)
            if (!array_type)
            {
                if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(array_access->array()))
                {
                    std::string array_name = identifier->name();
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find(array_name);
                    if (it != var_types.end())
                    {
                        array_type = it->second;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Found in variable_types_map: {}", array_type.get()->display_name());
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: '{}' not found in variable_types_map", array_name);
                    }
                }
            }

            // If no resolved type and array is a member access (e.g., this.cells),
            // look up the field type from TemplateRegistry
            if (!array_type)
            {
                if (auto *member_access = dynamic_cast<Cryo::MemberAccessNode *>(array_access->array()))
                {
                    std::string base_type_name;
                    if (auto *base_ident = dynamic_cast<Cryo::IdentifierNode *>(member_access->object()))
                    {
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find(base_ident->name());
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            TypeRef base_type = it->second;
                            base_type_name = base_type->display_name();
                            if (base_type->kind() == Cryo::TypeKind::Pointer)
                            {
                                auto *ptr = dynamic_cast<const Cryo::PointerType *>(base_type.get());
                                if (ptr && ptr->pointee())
                                    base_type_name = ptr->pointee()->display_name();
                            }
                            else if (!base_type_name.empty() && base_type_name.back() == '*')
                                base_type_name.pop_back();
                        }
                        else if (base_ident->name() == "this")
                        {
                            base_type_name = ctx().current_type_name();
                        }
                    }

                    if (!base_type_name.empty())
                    {
                        if (auto *template_reg = ctx().template_registry())
                        {
                            std::vector<std::string> candidates = {base_type_name};
                            if (!ctx().namespace_context().empty())
                                candidates.push_back(ctx().namespace_context() + "::" + base_type_name);

                            for (const auto &candidate : candidates)
                            {
                                const auto *field_info = template_reg->get_struct_field_types(candidate);
                                if (field_info)
                                {
                                    for (size_t fi = 0; fi < field_info->field_names.size(); ++fi)
                                    {
                                        if (field_info->field_names[fi] == member_access->member() &&
                                            fi < field_info->field_types.size())
                                        {
                                            array_type = field_info->field_types[fi];
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "get_lvalue_address: Resolved member field '{}' type from TemplateRegistry: {}",
                                                      member_access->member(),
                                                      array_type ? array_type->display_name() : "<null>");
                                            break;
                                        }
                                    }
                                    if (array_type)
                                        break;
                                }
                            }
                        }
                    }
                }
            }

            // Also check if the array identifier is pool_sizes specifically
            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(array_access->array()))
            {
                std::string array_name = identifier->name();
                if (array_name == "pool_sizes")
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FOUND pool_sizes array access. Type: {}",
                              array_type ? array_type.get()->display_name() : "NULL");
                }
            }

            // Add debug output for the resolved type
            if (array_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Array resolved type: {} (kind: {})",
                          array_type.get()->display_name(), static_cast<int>(array_type->kind()));
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: No resolved array type");
            }

            if (array_type)
            {
                if (array_type->kind() == Cryo::TypeKind::Array)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Detected TypeKind::Array");
                    auto *arr_type = dynamic_cast<const Cryo::ArrayType *>(array_type.get());
                    if (arr_type && arr_type->element().is_valid())
                    {
                        element_type = get_llvm_type(arr_type->element());
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Got element type from ArrayType");
                    }
                }
                else if (array_type->kind() == Cryo::TypeKind::Pointer)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Detected TypeKind::Pointer");
                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(array_type.get());
                    if (ptr_type && ptr_type->pointee().is_valid())
                    {
                        element_type = get_llvm_type(ptr_type->pointee());

                        // If get_llvm_type failed (e.g., generic pointee like HashSetEntry<T>),
                        // try generic substitution to resolve to the concrete type
                        if (!element_type)
                        {
                            std::string pointee_name = ptr_type->pointee()->display_name();
                            auto *visitor = ctx().visitor();
                            if (visitor)
                            {
                                auto *generics = visitor->get_generics();
                                if (generics && generics->in_type_param_scope())
                                {
                                    std::string substituted = generics->substitute_type_annotation(pointee_name);
                                    if (substituted.empty() && pointee_name.find('<') != std::string::npos)
                                    {
                                        // Manual fallback: parse "HashSetEntry<T>" and resolve T
                                        size_t angle = pointee_name.find('<');
                                        std::string base = pointee_name.substr(0, angle);
                                        std::string args = pointee_name.substr(angle + 1);
                                        if (!args.empty() && args.back() == '>')
                                            args.pop_back();
                                        TypeRef resolved_param = generics->resolve_type_param(args);
                                        if (resolved_param.is_valid())
                                            substituted = base + "_" + resolved_param->display_name();
                                    }
                                    if (!substituted.empty())
                                    {
                                        llvm::StructType *st = llvm::StructType::getTypeByName(llvm_ctx(), substituted);
                                        if (!st)
                                        {
                                            if (llvm::Type *t = ctx().get_type(substituted))
                                                st = llvm::dyn_cast<llvm::StructType>(t);
                                        }
                                        if (st)
                                        {
                                            element_type = st;
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "get_lvalue_address: Resolved generic pointer element {} -> {}",
                                                      pointee_name, substituted);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else if (array_type->kind() == Cryo::TypeKind::String)
                {
                    // String is a char* (i8*) in LLVM - elements are i8
                    element_type = llvm::Type::getInt8Ty(llvm_ctx());
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Detected TypeKind::String, element type = i8");
                }
                else if (array_type->kind() == Cryo::TypeKind::Reference)
                {
                    // Unwrap Reference to check if it wraps a String (e.g., &string in implement blocks)
                    auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(array_type.get());
                    if (ref_type && ref_type->referent() && ref_type->referent()->kind() == Cryo::TypeKind::String)
                    {
                        element_type = llvm::Type::getInt8Ty(llvm_ctx());
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Detected Reference<String>, element type = i8");
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Type kind {} not handled, checking for Array<T> patterns",
                              static_cast<int>(array_type->kind()));

                    // Check if this is an Array<T> type (Parameterized or Class)
                    std::string type_str = array_type.get()->display_name();
                    if (type_str.find("Array<") == 0)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Detected Array<T> pattern, attempting to extract element type");
                        // Try to extract element type from Array<T>
                        // For Array<u64>, we want u64
                        size_t start = type_str.find('<') + 1;
                        size_t end = type_str.find('>', start);
                        if (start != std::string::npos && end != std::string::npos)
                        {
                            std::string element_name = type_str.substr(start, end - start);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Extracted element type name: {}", element_name);

                            // Map element type name to LLVM type
                            if (element_name == "u64" || element_name == "i64")
                                element_type = llvm::Type::getInt64Ty(llvm_ctx());
                            else if (element_name == "u32" || element_name == "i32")
                                element_type = llvm::Type::getInt32Ty(llvm_ctx());
                            else if (element_name == "u16" || element_name == "i16")
                                element_type = llvm::Type::getInt16Ty(llvm_ctx());
                            else if (element_name == "u8" || element_name == "i8")
                                element_type = llvm::Type::getInt8Ty(llvm_ctx());
                            else if (element_name == "f64")
                                element_type = llvm::Type::getDoubleTy(llvm_ctx());
                            else if (element_name == "f32")
                                element_type = llvm::Type::getFloatTy(llvm_ctx());
                            else if (element_name == "string")
                                element_type = llvm::PointerType::get(llvm_ctx(), 0);
                            else
                            {
                                // Try to look up as struct/class type
                                // First build candidate names with namespace prefixes
                                std::string current_ns = ctx().srm_context().get_current_namespace_path();
                                std::vector<std::string> candidates;

                                // Add namespace-qualified name first (most likely)
                                if (!current_ns.empty())
                                    candidates.push_back(current_ns + "::" + element_name);
                                candidates.push_back(element_name);

                                for (const auto &candidate : candidates)
                                {
                                    llvm::StructType *struct_type = llvm::StructType::getTypeByName(llvm_ctx(), candidate);
                                    if (struct_type)
                                    {
                                        element_type = struct_type;
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "get_lvalue_address: Found struct type '{}' for element '{}'",
                                                  candidate, element_name);
                                        break;
                                    }
                                }
                            }

                            if (element_type)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_lvalue_address: Successfully mapped {} to LLVM type", element_name);
                            }
                        }
                    }
                }
            }

            // If we still don't have an element type, try to infer from the current function context
            // This handles monomorphized generic methods like Array_Token::push where
            // the resolved type might not be set correctly on AST nodes after monomorphization
            if (!element_type)
            {
                llvm::Function *current_fn = builder().GetInsertBlock()->getParent();
                if (current_fn)
                {
                    std::string fn_name = current_fn->getName().str();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "get_lvalue_address: Trying to infer element type from function: {}", fn_name);

                    // Look for Array_<ElementType> pattern in the function name
                    // e.g., "CryoInterpreter::Array_Token::push" -> extract "Token"
                    size_t array_pos = fn_name.find("Array_");
                    if (array_pos != std::string::npos)
                    {
                        size_t elem_start = array_pos + 6; // Skip "Array_"
                        size_t elem_end = fn_name.find("::", elem_start);
                        if (elem_end != std::string::npos)
                        {
                            std::string elem_name = fn_name.substr(elem_start, elem_end - elem_start);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "get_lvalue_address: Extracted element type '{}' from function name", elem_name);

                            // Map primitive types
                            if (elem_name == "u64" || elem_name == "i64")
                                element_type = llvm::Type::getInt64Ty(llvm_ctx());
                            else if (elem_name == "u32" || elem_name == "i32")
                                element_type = llvm::Type::getInt32Ty(llvm_ctx());
                            else if (elem_name == "u16" || elem_name == "i16")
                                element_type = llvm::Type::getInt16Ty(llvm_ctx());
                            else if (elem_name == "u8" || elem_name == "i8")
                                element_type = llvm::Type::getInt8Ty(llvm_ctx());
                            else if (elem_name == "f64")
                                element_type = llvm::Type::getDoubleTy(llvm_ctx());
                            else if (elem_name == "f32")
                                element_type = llvm::Type::getFloatTy(llvm_ctx());
                            else if (elem_name == "string")
                                element_type = llvm::PointerType::get(llvm_ctx(), 0);
                            else
                            {
                                // Try to find struct type - extract namespace from function name
                                size_t last_sep = fn_name.rfind("::");
                                std::string ns_prefix;
                                if (last_sep != std::string::npos)
                                {
                                    // Go back one more :: to get the namespace
                                    size_t second_last = fn_name.rfind("::", last_sep - 1);
                                    if (second_last != std::string::npos)
                                    {
                                        ns_prefix = fn_name.substr(0, second_last);
                                    }
                                }

                                std::vector<std::string> candidates;
                                if (!ns_prefix.empty())
                                    candidates.push_back(ns_prefix + "::" + elem_name);
                                candidates.push_back(elem_name);

                                for (const auto &candidate : candidates)
                                {
                                    llvm::StructType *struct_type = llvm::StructType::getTypeByName(llvm_ctx(), candidate);
                                    if (struct_type)
                                    {
                                        element_type = struct_type;
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "get_lvalue_address: Found struct type '{}' for element", candidate);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!element_type)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR,
                             "Could not determine element type for array lvalue address");
                return nullptr;
            }

            // If the array came from a member access and the field is a pointer type
            // (not an array type), we need to load the pointer value first.
            // get_lvalue_address gives us the address of the pointer field; we must
            // dereference it to get the actual array base pointer before indexing.
            if (is_member_access_array && array_type && array_type->kind() == Cryo::TypeKind::Pointer)
            {
                array_val = builder().CreateLoad(builder().getPtrTy(), array_val, "ptr.load");
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_lvalue_address: Loaded pointer field before array indexing");
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

    bool OperatorCodegen::is_signed_integer_type(TypeRef type)
    {
        if (!type)
            return true; // Default to signed

        // Check if it's an unsigned type
        std::string type_str = type.get()->display_name();
        return type_str.find('u') != 0; // Not starting with 'u' means signed
    }

    bool OperatorCodegen::is_float_type(TypeRef type)
    {
        if (!type)
            return false;

        TypeKind kind = type->kind();
        return kind == TypeKind::Float;
    }

    bool OperatorCodegen::is_string_type(TypeRef type)
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

        // Integer to float conversion (when one operand is float and the other is integer)
        if (lhs_type->isFloatingPointTy() && rhs_type->isIntegerTy())
        {
            // Convert integer to float type
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ensure_compatible_types: Converting rhs integer to float");
            rhs = builder().CreateSIToFP(rhs, lhs_type, "sitofp");
            return true;
        }
        if (lhs_type->isIntegerTy() && rhs_type->isFloatingPointTy())
        {
            // Convert integer to float type
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ensure_compatible_types: Converting lhs integer to float");
            lhs = builder().CreateSIToFP(lhs, rhs_type, "sitofp");
            return true;
        }

        // Pointer comparison (both should be pointers)
        if (lhs_type->isPointerTy() && rhs_type->isPointerTy())
        {
            // Opaque pointers in LLVM 15+ are compatible
            return true;
        }

        // Integer-to-pointer comparison (e.g., ptr == null where null is integer 0)
        if (lhs_type->isIntegerTy() && rhs_type->isPointerTy())
        {
            lhs = builder().CreateIntToPtr(lhs, rhs_type, "int2ptr");
            return true;
        }
        if (lhs_type->isPointerTy() && rhs_type->isIntegerTy())
        {
            rhs = builder().CreateIntToPtr(rhs, lhs_type, "int2ptr");
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

    llvm::Value *OperatorCodegen::extract_enum_discriminant(llvm::Value *val, TypeRef enum_type)
    {
        if (!val || !enum_type || !enum_type->is_enum())
            return val;

        llvm::Type *llvm_ty = val->getType();

        // Already an integer (simple enum or already-extracted discriminant)
        if (llvm_ty->isIntegerTy())
            return val;

        // By-value struct: extract element 0 (the discriminant)
        if (llvm_ty->isStructTy())
        {
            return builder().CreateExtractValue(val, 0, "enum.disc");
        }

        // Pointer to struct: GEP + load
        if (llvm_ty->isPointerTy())
        {
            auto *et = dynamic_cast<const Cryo::EnumType *>(enum_type.get());
            if (!et)
                return val;

            std::string type_name = et->name();
            llvm::Type *resolved = ctx().get_type(type_name);
            if (!resolved || !resolved->isStructTy())
                return val;

            llvm::StructType *st = llvm::cast<llvm::StructType>(resolved);
            llvm::Value *gep = builder().CreateStructGEP(st, val, 0, "disc.ptr");
            return builder().CreateLoad(builder().getInt32Ty(), gep, "enum.disc");
        }

        return val;
    }

} // namespace Cryo::Codegen
