#include "Codegen/Expressions/ExpressionCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Lexer/lexer.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/Logger.hpp"

#include <stdexcept>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    ExpressionCodegen::ExpressionCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Literal Expressions
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_integer_literal(int64_t value, Cryo::Type *type)
    {
        // Determine bit width and signedness
        unsigned bits = 32; // Default to i32
        bool is_signed = true;
        if (type)
        {
            std::string type_name = type->to_string();
            if (type_name == "i8")
            {
                bits = 8;
                is_signed = true;
            }
            else if (type_name == "u8")
            {
                bits = 8;
                is_signed = false;
            }
            else if (type_name == "i16")
            {
                bits = 16;
                is_signed = true;
            }
            else if (type_name == "u16")
            {
                bits = 16;
                is_signed = false;
            }
            else if (type_name == "i32")
            {
                bits = 32;
                is_signed = true;
            }
            else if (type_name == "u32")
            {
                bits = 32;
                is_signed = false;
            }
            else if (type_name == "i64")
            {
                bits = 64;
                is_signed = true;
            }
            else if (type_name == "u64")
            {
                bits = 64;
                is_signed = false;
            }
            else if (type_name == "i128")
            {
                bits = 128;
                is_signed = true;
            }
            else if (type_name == "u128")
            {
                bits = 128;
                is_signed = false;
            }
        }

        return llvm::ConstantInt::get(get_int_type(bits), value, is_signed);
    }

    llvm::Value *ExpressionCodegen::generate_unsigned_integer_literal(uint64_t value, Cryo::Type *type)
    {
        // Determine bit width for unsigned types
        unsigned bits = 64; // Default to u64 for large unsigned values
        if (type)
        {
            std::string type_name = type->to_string();
            if (type_name == "u8")
                bits = 8;
            else if (type_name == "u16")
                bits = 16;
            else if (type_name == "u32")
                bits = 32;
            else if (type_name == "u64")
                bits = 64;
            else if (type_name == "u128")
                bits = 128;
        }

        return llvm::ConstantInt::get(get_int_type(bits), value, false);
    }

    llvm::Value *ExpressionCodegen::generate_float_literal(double value, bool is_double)
    {
        if (is_double)
        {
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(llvm_ctx()), value);
        }
        else
        {
            return llvm::ConstantFP::get(llvm::Type::getFloatTy(llvm_ctx()), static_cast<float>(value));
        }
    }

    llvm::Value *ExpressionCodegen::generate_bool_literal(bool value)
    {
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(llvm_ctx()), value ? 1 : 0);
    }

    llvm::Value *ExpressionCodegen::generate_string_literal(const std::string &value)
    {
        // Check cache first
        auto it = _string_cache.find(value);
        if (it != _string_cache.end())
        {
            return it->second;
        }

        // Create global string constant
        llvm::Constant *str_constant = llvm::ConstantDataArray::getString(llvm_ctx(), value, true);

        llvm::GlobalVariable *global = new llvm::GlobalVariable(
            *module(),
            str_constant->getType(),
            true, // isConstant
            llvm::GlobalValue::PrivateLinkage,
            str_constant,
            ".str");

        global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        global->setAlignment(llvm::Align(1));

        // Get pointer to first character
        llvm::Value *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0);
        llvm::Value *indices[] = {zero, zero};
        llvm::Value *ptr = builder().CreateInBoundsGEP(str_constant->getType(), global, indices, "str.ptr");

        // Cache the global (not the GEP, as GEP is instruction-specific)
        _string_cache[value] = global;

        return ptr;
    }

    llvm::Value *ExpressionCodegen::generate_char_literal(char value)
    {
        return llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx()), static_cast<uint8_t>(value));
    }

    llvm::Value *ExpressionCodegen::generate_null_literal(Cryo::Type *type)
    {
        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_type));
    }

    llvm::Value *ExpressionCodegen::generate_literal(Cryo::LiteralNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null literal node");
            return nullptr;
        }

        const std::string &value_str = node->value();
        TokenKind kind = node->literal_kind();

        // Parse based on literal kind
        switch (kind)
        {
        case TokenKind::TK_NUMERIC_CONSTANT:
        {
            // Try to parse as integer first, then float
            try
            {
                // Check if it contains a decimal point or 'e'/'E'
                if (value_str.find('.') != std::string::npos ||
                    value_str.find('e') != std::string::npos ||
                    value_str.find('E') != std::string::npos)
                {
                    // Parse as float
                    double float_value = std::stod(value_str);
                    bool is_double = true;
                    if (node->get_resolved_type())
                    {
                        std::string type_name = node->get_resolved_type()->to_string();
                        is_double = (type_name != "f32");
                    }
                    return generate_float_literal(float_value, is_double);
                }
                else
                {
                    // Parse as integer - check if type is unsigned
                    Cryo::Type *resolved_type = node->get_resolved_type();
                    bool is_unsigned = false;
                    if (resolved_type)
                    {
                        std::string type_name = resolved_type->to_string();
                        is_unsigned = (type_name.length() > 0 && type_name[0] == 'u');
                    }

                    if (is_unsigned)
                    {
                        // Parse as unsigned for u8, u16, u32, u64, u128
                        uint64_t uint_value = std::stoull(value_str, nullptr, 0);
                        return generate_unsigned_integer_literal(uint_value, resolved_type);
                    }
                    else
                    {
                        // Try signed first, fall back to unsigned for large values
                        try
                        {
                            int64_t int_value = std::stoll(value_str, nullptr, 0);
                            return generate_integer_literal(int_value, resolved_type);
                        }
                        catch (const std::out_of_range &)
                        {
                            // Value too large for signed int64_t, try as unsigned
                            uint64_t uint_value = std::stoull(value_str, nullptr, 0);
                            return generate_unsigned_integer_literal(uint_value, resolved_type);
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node, "Invalid numeric literal: " + value_str);
                return nullptr;
            }
        }

        case TokenKind::TK_BOOLEAN_LITERAL:
        {
            bool bool_value = (value_str == "true");
            return generate_bool_literal(bool_value);
        }

        case TokenKind::TK_KW_TRUE:
        {
            // Boolean keyword 'true'
            return generate_bool_literal(true);
        }

        case TokenKind::TK_KW_FALSE:
        {
            // Boolean keyword 'false'
            return generate_bool_literal(false);
        }

        case TokenKind::TK_CHAR_CONSTANT:
        {
            if (value_str.length() >= 3 && value_str.front() == '\'' && value_str.back() == '\'')
            {
                char char_value = value_str[1]; // Simple case, handle escapes later
                return generate_char_literal(char_value);
            }
            else
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node, "Invalid char literal: " + value_str);
                return nullptr;
            }
        }

        case TokenKind::TK_STRING_LITERAL:
        case TokenKind::TK_RAW_STRING_LITERAL:
        {
            // Remove quotes and handle as string
            std::string str_value = value_str;
            if (str_value.length() >= 2 && str_value.front() == '"' && str_value.back() == '"')
            {
                str_value = str_value.substr(1, str_value.length() - 2);
            }
            return generate_string_literal(str_value);
        }

        case TokenKind::TK_KW_NULL:
        case TokenKind::TK_KW_NIL:
        case TokenKind::TK_KW_NONE:
        {
            // Null/nil/none literals - return null pointer
            return generate_null_literal(node->get_resolved_type());
        }

        default:
            break;
        }

        report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node, "Unknown literal type");
        return nullptr;
    }

    //===================================================================
    // Identifier Expressions
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_identifier(Cryo::IdentifierNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, "Null identifier node");
            return nullptr;
        }

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating identifier: {}", name);

        llvm::Value *value = lookup_variable(name);
        if (!value)
        {
            report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, node,
                         "Unknown identifier: " + name);
            return nullptr;
        }

        // If it's an alloca, load the value
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
        {
            llvm::Type *loaded_type = alloca->getAllocatedType();
            return create_load(alloca, loaded_type, name + ".load");
        }

        // If it's a global variable, load the value
        if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value))
        {
            llvm::Type *loaded_type = global->getValueType();
            return create_load(global, loaded_type, name + ".load");
        }

        return value;
    }

    llvm::Value *ExpressionCodegen::generate_identifier_address(Cryo::IdentifierNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();

        // Try alloca first
        if (llvm::AllocaInst *alloca = values().get_alloca(name))
        {
            return alloca;
        }

        // Try value context
        if (llvm::Value *value = values().get_value(name))
        {
            // If it's a pointer, return it directly
            if (value->getType()->isPointerTy())
            {
                return value;
            }
        }

        // Try global variable
        if (llvm::GlobalVariable *global = module()->getGlobalVariable(name))
        {
            return global;
        }

        report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, node,
                     "Cannot get address of: " + name);
        return nullptr;
    }

    llvm::Value *ExpressionCodegen::lookup_variable(const std::string &name)
    {
        // Try alloca first
        if (llvm::AllocaInst *alloca = values().get_alloca(name))
        {
            return alloca;
        }

        // Try value context
        if (llvm::Value *value = values().get_value(name))
        {
            return value;
        }

        // Try global variable
        if (llvm::GlobalVariable *global = module()->getGlobalVariable(name))
        {
            return global;
        }

        // Try with current namespace prefix
        std::string current_ns = ctx().namespace_context();
        if (!current_ns.empty())
        {
            std::string qualified = current_ns + "::" + name;
            if (llvm::GlobalVariable *global = module()->getGlobalVariable(qualified))
            {
                return global;
            }
        }

        // Try common namespace prefixes (for constants from runtime, core, etc.)
        static const std::vector<std::string> common_namespaces = {
            "std::Runtime::", "std::core::", "std::IO::", "Runtime::"};

        for (const auto &ns : common_namespaces)
        {
            std::string ns_qualified = ns + name;
            if (llvm::GlobalVariable *global = module()->getGlobalVariable(ns_qualified))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "lookup_variable: Found '{}' in namespace -> '{}'", name, ns_qualified);
                return global;
            }
        }

        // Try function
        if (llvm::Function *func = module()->getFunction(name))
        {
            return func;
        }

        return nullptr;
    }

    //===================================================================
    // Member Access Expressions
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_member_access(Cryo::MemberAccessNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, "Null member access node");
            return nullptr;
        }

        std::string member_name = node->member();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating member access: {}", member_name);

        // Get member address first
        llvm::Value *member_ptr = generate_member_address(node);
        if (!member_ptr)
        {
            return nullptr;
        }

        // Determine the type to load
        llvm::StructType *struct_type = nullptr;
        unsigned field_idx = 0;
        if (resolve_member_info(node->object(), member_name, struct_type, field_idx))
        {
            llvm::Type *field_type = struct_type->getElementType(field_idx);
            return create_load(member_ptr, field_type, member_name + ".load");
        }

        // Fallback: assume pointer type
        return member_ptr;
    }

    llvm::Value *ExpressionCodegen::generate_member_address(Cryo::MemberAccessNode *node)
    {
        if (!node)
            return nullptr;

        std::string member_name = node->member();

        // Generate object expression
        llvm::Value *object = generate(node->object());
        if (!object)
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Failed to generate member access object");
            return nullptr;
        }

        // Ensure we have a pointer to the struct
        if (!object->getType()->isPointerTy())
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Member access requires pointer type");
            return nullptr;
        }

        // Resolve struct type and field index
        llvm::StructType *struct_type = nullptr;
        unsigned field_idx = 0;
        if (!resolve_member_info(node->object(), member_name, struct_type, field_idx))
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Cannot resolve member: " + member_name);
            return nullptr;
        }

        // Create GEP for field access
        return create_struct_gep(struct_type, object, field_idx, member_name + ".ptr");
    }

    int ExpressionCodegen::get_field_index(llvm::StructType *struct_type, const std::string &field_name)
    {
        if (!struct_type)
            return -1;

        // Look up field info from context
        // This requires the struct to have been registered with field names
        // For now, return -1 to indicate lookup is needed elsewhere

        return -1;
    }

    //===================================================================
    // Index Expressions
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_index(Cryo::ArrayAccessNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, "Null index node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating index expression");

        // Generate array expression to determine element type
        llvm::Value *array_val = generate(node->array());
        if (!array_val)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Failed to generate array expression");
            return nullptr;
        }

        // Determine element type from array type
        llvm::Type *element_type = nullptr;
        if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(array_val->getType()))
        {
            element_type = arr_type->getElementType();
        }
        else if (array_val->getType()->isPointerTy())
        {
            // For pointer types (strings, etc.), try to get from resolved type
            Cryo::Type *cryo_array_type = node->array()->get_resolved_type();
            if (cryo_array_type)
            {
                if (cryo_array_type->kind() == TypeKind::Array)
                {
                    auto *arr = static_cast<Cryo::ArrayType *>(cryo_array_type);
                    element_type = get_llvm_type(arr->element_type().get());
                }
                else if (cryo_array_type->kind() == TypeKind::String)
                {
                    element_type = llvm::Type::getInt8Ty(llvm_ctx());
                }
                else if (cryo_array_type->kind() == TypeKind::Pointer)
                {
                    auto *ptr = static_cast<Cryo::PointerType *>(cryo_array_type);
                    element_type = get_llvm_type(ptr->pointee_type().get());
                }
            }
            // Fallback to i8 for string-like access
            if (!element_type)
            {
                element_type = llvm::Type::getInt8Ty(llvm_ctx());
            }
        }

        if (!element_type)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Could not determine element type for array access");
            return nullptr;
        }

        // Generate index expression
        llvm::Value *index_val = generate(node->index());
        if (!index_val)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Failed to generate index expression");
            return nullptr;
        }

        // Ensure index is integer type
        if (!index_val->getType()->isIntegerTy())
        {
            index_val = cast_if_needed(index_val, llvm::Type::getInt64Ty(llvm_ctx()));
        }

        // Create GEP and load the element
        llvm::Value *element_ptr = create_array_gep(element_type, array_val, index_val, "elem.ptr");
        if (!element_ptr)
        {
            return nullptr;
        }

        // Load the element value
        return create_load(element_ptr, element_type, "elem.load");
    }

    llvm::Value *ExpressionCodegen::generate_index_address(Cryo::ArrayAccessNode *node)
    {
        if (!node)
            return nullptr;

        // Generate array expression
        llvm::Value *array_val = generate(node->array());
        if (!array_val)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Failed to generate array expression");
            return nullptr;
        }

        // Generate index expression
        llvm::Value *index_val = generate(node->index());
        if (!index_val)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Failed to generate index expression");
            return nullptr;
        }

        // Ensure index is integer type
        if (!index_val->getType()->isIntegerTy())
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Index must be integer type");
            return nullptr;
        }

        // Get element type
        llvm::Type *element_type = nullptr;
        if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(array_val->getType()))
        {
            element_type = arr_type->getElementType();
        }
        else
        {
            // Assume pointer to elements
            element_type = llvm::Type::getInt8Ty(llvm_ctx()); // Default
        }

        // Create GEP
        return create_array_gep(element_type, array_val, index_val, "elem.ptr");
    }

    //===================================================================
    // Cast Expressions
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_cast(Cryo::CastExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0626_CAST_OPERATION_ERROR, "Null cast node");
            return nullptr;
        }

        // Generate source expression
        llvm::Value *value = generate(node->expression());
        if (!value)
        {
            return nullptr;
        }

        // For now, handle string-based cast by looking up the type
        // TODO: Use proper Type* resolution instead of string lookup
        llvm::Type *llvm_target = types().get_type(node->target_type_name());
        if (!llvm_target)
        {
            report_error(ErrorCode::E0626_CAST_OPERATION_ERROR, node, "Unknown cast target type: " + node->target_type_name());
            return nullptr;
        }

        // For now, cast directly using LLVM type - this needs proper Cryo::Type* conversion
        return cast_if_needed(value, llvm_target);
    }

    llvm::Value *ExpressionCodegen::generate_cast(llvm::Value *value, Cryo::Type *target_type)
    {
        if (!value || !target_type)
            return value;

        llvm::Type *llvm_target = get_llvm_type(target_type);
        if (!llvm_target)
        {
            return value;
        }

        return cast_if_needed(value, llvm_target);
    }

    //===================================================================
    // Sizeof/Alignof
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_sizeof(Cryo::SizeofExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null sizeof node");
            return nullptr;
        }

        // TODO: Get proper Cryo::Type* from node instead of string lookup
        llvm::Type *llvm_type = types().get_type(node->type_name());
        if (!llvm_type)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Unknown type for sizeof: " + node->type_name());
            return nullptr;
        }

        // Generate sizeof using LLVM type
        auto &context = llvm_ctx();
        auto &data_layout = module()->getDataLayout();
        uint64_t size = data_layout.getTypeAllocSize(llvm_type);
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), size);
    }

    llvm::Value *ExpressionCodegen::generate_sizeof(Cryo::Type *type)
    {
        if (!type)
        {
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0);
        }

        llvm::Type *llvm_type = get_llvm_type(type);
        if (!llvm_type)
        {
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0);
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        uint64_t size = dl.getTypeAllocSize(llvm_type);

        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
    }

    llvm::Value *ExpressionCodegen::generate_alignof(Cryo::Type *type)
    {
        if (!type)
        {
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 1);
        }

        llvm::Type *llvm_type = get_llvm_type(type);
        if (!llvm_type)
        {
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 1);
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        uint64_t align = dl.getABITypeAlign(llvm_type).value();

        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), align);
    }

    //===================================================================
    // Address-of and Dereference
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_address_of(Cryo::ExpressionNode *operand)
    {
        if (!operand)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, "Null operand for address-of");
            return nullptr;
        }

        // Handle different expression types
        if (auto *id = dynamic_cast<IdentifierNode *>(operand))
        {
            return generate_identifier_address(id);
        }
        else if (auto *member = dynamic_cast<MemberAccessNode *>(operand))
        {
            return generate_member_address(member);
        }
        else if (auto *index = dynamic_cast<ArrayAccessNode *>(operand))
        {
            return generate_index_address(index);
        }

        report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, operand,
                     "Cannot take address of this expression");
        return nullptr;
    }

    llvm::Value *ExpressionCodegen::generate_dereference(llvm::Value *operand, Cryo::Type *pointee_type)
    {
        if (!operand)
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR, "Null operand for dereference");
            return nullptr;
        }

        if (!operand->getType()->isPointerTy())
        {
            report_error(ErrorCode::E0616_UNARY_OPERATION_ERROR,
                         "Cannot dereference non-pointer type");
            return nullptr;
        }

        llvm::Type *load_type = nullptr;
        if (pointee_type)
        {
            load_type = get_llvm_type(pointee_type);
        }

        if (!load_type)
        {
            // Default to i8
            load_type = llvm::Type::getInt8Ty(llvm_ctx());
        }

        return create_load(operand, load_type, "deref");
    }

    //===================================================================
    // Ternary Expression
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_ternary(Cryo::TernaryExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null ternary node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating ternary expression");

        // Generate condition
        llvm::Value *condition = generate(node->condition());
        if (!condition)
        {
            return nullptr;
        }

        // Convert to i1 if needed
        if (!condition->getType()->isIntegerTy(1))
        {
            if (condition->getType()->isIntegerTy())
            {
                condition = builder().CreateICmpNE(
                    condition,
                    llvm::ConstantInt::get(condition->getType(), 0),
                    "tobool");
            }
            else
            {
                report_error(ErrorCode::E0615_BINARY_OPERATION_ERROR, node,
                             "Ternary condition must be boolean");
                return nullptr;
            }
        }

        // Create basic blocks
        llvm::Function *fn = builder().GetInsertBlock()->getParent();
        llvm::BasicBlock *then_block = create_block("ternary.then", fn);
        llvm::BasicBlock *else_block = create_block("ternary.else", fn);
        llvm::BasicBlock *merge_block = create_block("ternary.merge", fn);

        // Branch on condition
        builder().CreateCondBr(condition, then_block, else_block);

        // Generate true value
        builder().SetInsertPoint(then_block);
        llvm::Value *then_val = generate(node->true_expression());
        if (!then_val)
        {
            return nullptr;
        }
        llvm::BasicBlock *then_end = builder().GetInsertBlock();
        builder().CreateBr(merge_block);

        // Generate false value
        builder().SetInsertPoint(else_block);
        llvm::Value *else_val = generate(node->false_expression());
        if (!else_val)
        {
            return nullptr;
        }
        llvm::BasicBlock *else_end = builder().GetInsertBlock();
        builder().CreateBr(merge_block);

        // Create phi node
        builder().SetInsertPoint(merge_block);
        llvm::PHINode *phi = builder().CreatePHI(then_val->getType(), 2, "ternary.result");
        phi->addIncoming(then_val, then_end);
        phi->addIncoming(cast_if_needed(else_val, then_val->getType()), else_end);

        return phi;
    }

    //===================================================================
    // Helpers
    //===================================================================

    bool ExpressionCodegen::is_lvalue(Cryo::ExpressionNode *expr) const
    {
        if (!expr)
            return false;

        // Identifiers are lvalues
        if (dynamic_cast<IdentifierNode *>(expr))
        {
            return true;
        }

        // Member access is lvalue
        if (dynamic_cast<MemberAccessNode *>(expr))
        {
            return true;
        }

        // Index expression is lvalue
        if (dynamic_cast<ArrayAccessNode *>(expr))
        {
            return true;
        }

        // Dereference is lvalue
        if (auto *unary = dynamic_cast<UnaryExpressionNode *>(expr))
        {
            if (unary->operator_token().kind() == TokenKind::TK_STAR)
            {
                return true;
            }
        }

        return false;
    }

    llvm::Value *ExpressionCodegen::generate(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Use visitor pattern through context
        CodegenVisitor *visitor = ctx().visitor();
        expr->accept(*visitor);
        return get_result();
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    llvm::IntegerType *ExpressionCodegen::get_int_type(unsigned bits)
    {
        return llvm::IntegerType::get(llvm_ctx(), bits);
    }

    llvm::Value *ExpressionCodegen::load_if_pointer(llvm::Value *value, llvm::Type *expected_type)
    {
        if (!value)
            return nullptr;

        if (value->getType()->isPointerTy() && expected_type && !expected_type->isPointerTy())
        {
            return create_load(value, expected_type, "load");
        }

        return value;
    }

    bool ExpressionCodegen::resolve_member_info(Cryo::ExpressionNode *object,
                                                const std::string &member_name,
                                                llvm::StructType *&out_struct_type,
                                                unsigned &out_field_idx)
    {
        if (!object)
            return false;

        // Get the resolved type of the object expression
        Cryo::Type *obj_type = object->get_resolved_type();
        if (!obj_type)
        {
            // Fallback: Check if this is the 'this' identifier
            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(object))
            {
                if (identifier->name() == "this")
                {
                    // Try to get 'this' type from variable_types_map
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find("this");
                    if (it != var_types.end() && it->second)
                    {
                        obj_type = it->second;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_member_info: Resolved 'this' type from variable_types_map: {}",
                                  obj_type->to_string());
                    }
                    else
                    {
                        // Fallback to current_type_name if available
                        const std::string &current_type = ctx().current_type_name();
                        if (!current_type.empty())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: Using current_type_name for 'this': {}",
                                      current_type);

                            // Look up the struct type directly
                            out_struct_type = llvm::StructType::getTypeByName(llvm_ctx(), current_type);
                            if (!out_struct_type)
                            {
                                if (llvm::Type *type = ctx().get_type(current_type))
                                {
                                    out_struct_type = llvm::dyn_cast<llvm::StructType>(type);
                                }
                            }

                            if (out_struct_type)
                            {
                                int field_idx = ctx().get_struct_field_index(current_type, member_name);
                                if (field_idx >= 0)
                                {
                                    out_field_idx = static_cast<unsigned>(field_idx);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_member_info: Resolved this.{} to index {} via current_type_name",
                                              member_name, out_field_idx);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }

            if (!obj_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_member_info: No resolved type for object");
                return false;
            }
        }

        // Get type name (handle pointer types)
        std::string type_name = obj_type->to_string();

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

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_member_info: Looking up type '{}' for member '{}'",
                  type_name, member_name);

        // Look up struct type in LLVM context
        out_struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
        if (!out_struct_type)
        {
            // Try context registry
            if (llvm::Type *type = ctx().get_type(type_name))
            {
                out_struct_type = llvm::dyn_cast<llvm::StructType>(type);
            }
        }

        if (!out_struct_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Struct type '{}' not found", type_name);
            return false;
        }

        // Look up field index using the new CodegenContext API
        int field_idx = ctx().get_struct_field_index(type_name, member_name);
        if (field_idx < 0)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Field '{}' not found in struct '{}'",
                      member_name, type_name);
            return false;
        }

        out_field_idx = static_cast<unsigned>(field_idx);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_member_info: Resolved {}.{} to index {}",
                  type_name, member_name, out_field_idx);
        return true;
    }

    //===================================================================
    // Missing Expression Generators
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_array_access(Cryo::ArrayAccessNode *node)
    {
        // Delegate to generate_index which handles ArrayAccessNode
        return generate_index(node);
    }

    llvm::Value *ExpressionCodegen::generate_array_literal(Cryo::ArrayLiteralNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, "Null array literal node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating array literal");

        const auto &elements = node->elements();
        if (elements.empty())
        {
            // Empty array - return null pointer for now
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx(), 0));
        }

        // Generate first element to determine type
        llvm::Value *first_elem = generate(elements[0].get());
        if (!first_elem)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                         "Failed to generate first array element");
            return nullptr;
        }

        llvm::Type *elem_type = first_elem->getType();
        llvm::ArrayType *array_type = llvm::ArrayType::get(elem_type, elements.size());

        // Allocate array on stack
        llvm::AllocaInst *array_alloca = create_entry_alloca(array_type, "array.literal");

        // Store each element
        for (size_t i = 0; i < elements.size(); ++i)
        {
            llvm::Value *elem_val = (i == 0) ? first_elem : generate(elements[i].get());
            if (!elem_val)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array element at index " + std::to_string(i));
                return nullptr;
            }

            // Cast if needed
            elem_val = cast_if_needed(elem_val, elem_type);

            // Get pointer to element
            llvm::Value *indices[] = {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), i)};
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(array_type, array_alloca, indices,
                                                                "elem." + std::to_string(i) + ".ptr");
            builder().CreateStore(elem_val, elem_ptr);
        }

        return array_alloca;
    }

    llvm::Value *ExpressionCodegen::generate_struct_literal(Cryo::StructLiteralNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, "Null struct literal node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating struct literal for {}",
                  node->struct_type());

        // Look up struct type
        llvm::Type *struct_type = ctx().get_type(node->struct_type());
        if (!struct_type || !struct_type->isStructTy())
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Unknown struct type: " + node->struct_type());
            return nullptr;
        }

        auto *st = llvm::cast<llvm::StructType>(struct_type);

        // Allocate struct on stack
        llvm::AllocaInst *struct_alloca = create_entry_alloca(struct_type, node->struct_type() + ".literal");

        // Initialize fields from field initializers
        const auto &field_initializers = node->field_initializers();
        for (size_t i = 0; i < field_initializers.size() && i < st->getNumElements(); ++i)
        {
            const auto &initializer = field_initializers[i];
            if (!initializer || !initializer->value())
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Null field initializer at index {}", i);
                continue;
            }

            llvm::Value *field_val = generate(initializer->value());
            if (!field_val)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to generate struct field {}",
                         initializer->field_name());
                continue;
            }

            // Cast if needed
            field_val = cast_if_needed(field_val, st->getElementType(i));

            // Get pointer to field
            llvm::Value *field_ptr = create_struct_gep(struct_type, struct_alloca, i,
                                                       initializer->field_name() + ".ptr");
            create_store(field_val, field_ptr);
        }

        return struct_alloca;
    }

    llvm::Value *ExpressionCodegen::generate_new(Cryo::NewExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null new expression node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating new expression for {}",
                  node->type_name());

        // Get the type to allocate
        llvm::Type *alloc_type = ctx().get_type(node->type_name());
        if (!alloc_type)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                         "Unknown type for new: " + node->type_name());
            return nullptr;
        }

        // Calculate size
        const llvm::DataLayout &dl = module()->getDataLayout();
        uint64_t size = dl.getTypeAllocSize(alloc_type);

        // Call malloc
        llvm::Function *malloc_fn = module()->getFunction("malloc");
        if (!malloc_fn)
        {
            llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
            llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
            llvm::FunctionType *malloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                               "malloc", module());
        }

        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
        llvm::Value *ptr = builder().CreateCall(malloc_fn, {size_val}, "new." + node->type_name());

        // If there are constructor arguments, call constructor
        // TODO: Handle constructor calls with arguments

        return ptr;
    }

    llvm::Value *ExpressionCodegen::generate_scope_resolution(Cryo::ScopeResolutionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, "Null scope resolution node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating scope resolution {}::{}",
                  node->scope_name(), node->member_name());

        // Build fully qualified name
        std::string qualified_name = node->scope_name() + "::" + node->member_name();

        // Try to find as a function
        if (llvm::Function *fn = module()->getFunction(qualified_name))
        {
            return fn;
        }

        // Try to find as a global variable
        if (llvm::GlobalVariable *global = module()->getGlobalVariable(qualified_name))
        {
            return global;
        }

        // Try context registry
        if (llvm::Function *fn = ctx().get_function(qualified_name))
        {
            return fn;
        }

        // Try as enum variant
        auto &enum_variants = ctx().enum_variants_map();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Looking for enum variant: {} (map has {} entries)",
                  qualified_name, enum_variants.size());
        auto it = enum_variants.find(qualified_name);
        if (it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Found enum variant: {}", qualified_name);
            return it->second;
        }

        // Log available variants for debugging
        if (!enum_variants.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Available enum variants:");
            for (const auto &[name, val] : enum_variants)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "  - {}", name);
            }
        }

        report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, node,
                     "Cannot resolve: " + qualified_name);
        return nullptr;
    }

} // namespace Cryo::Codegen
