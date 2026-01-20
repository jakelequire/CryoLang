#include "Codegen/Expressions/ExpressionCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Statements/ControlFlowCodegen.hpp"
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

    llvm::Value *ExpressionCodegen::generate_integer_literal(int64_t value, TypeRef type)
    {
        // Determine bit width and signedness
        unsigned bits = 32; // Default to i32
        bool is_signed = true;
        if (type)
        {
            std::string type_name = type.get()->display_name();
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

    llvm::Value *ExpressionCodegen::generate_unsigned_integer_literal(uint64_t value, TypeRef type)
    {
        // Determine bit width for unsigned types
        unsigned bits = 64; // Default to u64 for large unsigned values
        if (type)
        {
            std::string type_name = type.get()->display_name();
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

    llvm::Value *ExpressionCodegen::generate_null_literal(TypeRef type)
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
                // Check for hexadecimal, binary, or octal literals first - these are always integers
                if (value_str.starts_with("0x") || value_str.starts_with("0X") ||
                    value_str.starts_with("0b") || value_str.starts_with("0B") ||
                    value_str.starts_with("0o") || value_str.starts_with("0O"))
                {
                    // Parse as integer - check if type is unsigned
                    TypeRef resolved_type = node->get_resolved_type();
                    bool is_unsigned = false;
                    if (resolved_type)
                    {
                        std::string type_name = resolved_type.get()->display_name();
                        is_unsigned = (type_name.length() > 0 && type_name[0] == 'u');
                    }

                    if (is_unsigned)
                    {
                        uint64_t uint_value = std::stoull(value_str, nullptr, 0);
                        return generate_unsigned_integer_literal(uint_value, resolved_type);
                    }
                    else
                    {
                        int64_t int_value = std::stoll(value_str, nullptr, 0);
                        return generate_integer_literal(int_value, resolved_type);
                    }
                }
                // Check if it contains a decimal point or scientific notation 'e'/'E' (but not hex 'E')
                else if (value_str.find('.') != std::string::npos ||
                        (value_str.find('e') != std::string::npos && !value_str.starts_with("0x") && !value_str.starts_with("0X")) ||
                        (value_str.find('E') != std::string::npos && !value_str.starts_with("0x") && !value_str.starts_with("0X")))
                {
                    // Parse as float
                    double float_value = std::stod(value_str);
                    bool is_double = true;
                    if (node->get_resolved_type())
                    {
                        std::string type_name = node->get_resolved_type().get()->display_name();
                        is_double = (type_name != "f32");
                    }
                    return generate_float_literal(float_value, is_double);
                }
                else
                {
                    // Parse as integer - check if type is unsigned
                    TypeRef resolved_type = node->get_resolved_type();
                    bool is_unsigned = false;
                    if (resolved_type)
                    {
                        std::string type_name = resolved_type.get()->display_name();
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
                char char_value;
                // Check for escape sequences (e.g., '\n', '\0', '\t')
                if (value_str[1] == '\\' && value_str.length() >= 4)
                {
                    char escape_char = value_str[2];
                    switch (escape_char)
                    {
                    case 'n':
                        char_value = '\n';
                        break;
                    case 't':
                        char_value = '\t';
                        break;
                    case 'r':
                        char_value = '\r';
                        break;
                    case '\\':
                        char_value = '\\';
                        break;
                    case '\'':
                        char_value = '\'';
                        break;
                    case '"':
                        char_value = '"';
                        break;
                    case '0':
                        char_value = '\0';
                        break;
                    case 'a':
                        char_value = '\a';
                        break;
                    case 'b':
                        char_value = '\b';
                        break;
                    case 'f':
                        char_value = '\f';
                        break;
                    case 'v':
                        char_value = '\v';
                        break;
                    default:
                        // Unknown escape, use the character literally
                        char_value = escape_char;
                        break;
                    }
                }
                else
                {
                    // Simple case - no escape sequence
                    char_value = value_str[1];
                }
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

        // If we don't have a valid insertion point (basic block), we're likely in a global initialization context
        // In this case, don't generate loads - work with constant values
        bool in_global_context = !builder().GetInsertBlock();

        // If it's an alloca, load the value (only if we're in a function context)
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
        {
            if (in_global_context)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, 
                         "ExpressionCodegen: Attempted to reference local variable '{}' in global context", name);
                return nullptr;
            }
            llvm::Type *loaded_type = alloca->getAllocatedType();
            return create_load(alloca, loaded_type, name + ".load");
        }

        // If it's a global variable, handle based on context
        if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value))
        {
            if (in_global_context)
            {
                // In global context, check if the global has an initializer we can use as a constant
                if (global->hasInitializer())
                {
                    return global->getInitializer();
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::CODEGEN, 
                             "ExpressionCodegen: Referenced uninitialized global '{}' in global context", name);
                    return global; // Return the global itself as a constant
                }
            }
            else
            {
                // In function context, load the value
                llvm::Type *loaded_type = global->getValueType();
                return create_load(global, loaded_type, name + ".load");
            }
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Looking for '{}'", name);

        // Try alloca first
        if (llvm::AllocaInst *alloca = values().get_alloca(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' as alloca", name);
            return alloca;
        }

        // Try value context (which also checks global values via search_scopes)
        if (llvm::Value *value = values().get_value(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' in value context", name);
            return value;
        }

        // Try global value context directly
        if (llvm::Value *global_val = values().get_global_value(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' in global value context", name);
            return global_val;
        }

        // Try global variable from module
        if (llvm::GlobalVariable *global = module()->getGlobalVariable(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' as module global", name);
            return global;
        }

        // Use SRM to generate all possible name candidates
        // This handles: current namespace, imported namespaces, aliases, global scope
        auto candidates = generate_lookup_candidates(name, Cryo::SymbolKind::Variable);

        for (const auto &candidate : candidates)
        {
            // Try global variable with this candidate name
            if (llvm::GlobalVariable *global = module()->getGlobalVariable(candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "lookup_variable: Found '{}' as '{}'", name, candidate);
                return global;
            }

            // Try global value context with this candidate name
            if (llvm::Value *global_val = values().get_global_value(candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "lookup_variable: Found '{}' in global values as '{}'", name, candidate);
                return global_val;
            }
        }

        // Try function
        if (llvm::Function *func = module()->getFunction(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' as function", name);
            return func;
        }

        // Check enum variants map for constants (lower priority than functions)
        auto &enum_variants = ctx().enum_variants_map();
        if (enum_variants.find(name) != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "lookup_variable: Found '{}' as enum variant", name);
            return enum_variants[name];
        }

        // Not found - log at DEBUG level instead of ERROR to avoid premature diagnostics
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "lookup_variable: '{}' not found in immediate lookup, may be forward-declared", name);

        // Log availability check at DEBUG level for troubleshooting
        if (values().has_global_value(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "lookup_variable: '{}' exists in global values but failed regular lookup", name);
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "lookup_variable: '{}' does NOT exist in global values", name);
        }

        // Check module globals at DEBUG level
        bool found_in_module = false;
        for (auto &gv : module()->globals())
        {
            if (gv.getName() == name)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "lookup_variable: '{}' exists in module globals but getGlobalVariable failed", name);
                found_in_module = true;
                break;
            }
        }
        if (!found_in_module)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "lookup_variable: '{}' does NOT exist in module globals", name);
        }

        // Return nullptr to allow deferred resolution in later compilation phases

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

        // First resolve the member info to get struct type and field index
        llvm::StructType *struct_type = nullptr;
        unsigned field_idx = 0;
        if (!resolve_member_info(node->object(), member_name, struct_type, field_idx))
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Cannot resolve member: " + member_name);
            return nullptr;
        }

        // Generate the object value
        llvm::Value *object = generate(node->object());
        if (!object)
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Failed to generate member access object");
            return nullptr;
        }

        // Check if the object is a struct value or a pointer to a struct
        if (object->getType()->isPointerTy())
        {
            // Object is a pointer to a struct - use GEP + load (original path)
            llvm::Value *member_ptr = generate_member_address(node);
            if (!member_ptr)
            {
                return nullptr;
            }

            // Load the field value using the resolved type information
            llvm::Type *field_type = struct_type->getElementType(field_idx);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Loading member '{}' from pointer with field type ID {}",
                      member_name, field_type->getTypeID());
            return create_load(member_ptr, field_type, member_name + ".load");
        }
        else if (llvm::isa<llvm::StructType>(object->getType()))
        {
            // Object is a struct value - use extractvalue directly
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Extracting member '{}' from struct value at index {}",
                      member_name, field_idx);
            return builder().CreateExtractValue(object, {field_idx}, member_name + ".extract");
        }
        else
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Member access requires struct value or pointer to struct");
            return nullptr;
        }
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

        // For array access, we need to handle stack-allocated arrays differently
        llvm::Value *array_ptr = nullptr;
        llvm::Type *element_type = nullptr;

        // Check if this is a simple identifier (stack-allocated array or Array<T>)
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->array()))
        {
            std::string array_name = identifier->name();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: *** Processing array access for identifier: '{}'", array_name);

            // Check if this is an Array<T> type first
            TypeRef resolved_type = identifier->get_resolved_type();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Identifier resolved type: {}",
                      resolved_type ? resolved_type.get()->display_name() : "null");

            // Fallback: if identifier doesn't have resolved type, check variable_types_map
            if (!resolved_type)
            {
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find(array_name);
                if (it != var_types.end())
                {
                    resolved_type = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "ExpressionCodegen: Found type in variable_types_map for '{}': {}",
                              array_name, resolved_type.get()->display_name());
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "ExpressionCodegen: Variable '{}' not found in variable_types_map. Available variables:",
                              array_name);
                    for (const auto &pair : var_types)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "  - {}: {}", pair.first, pair.second.get()->display_name());
                    }
                }
            }

            if (resolved_type && resolved_type->kind() == TypeKind::Array)
            {
                auto *cryo_array_type = static_cast<const Cryo::ArrayType *>(resolved_type.get());
                std::string element_type_name = cryo_array_type->element()->display_name();

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Detected Array<{}> access for variable: {}", element_type_name, array_name);

                // Get the element type from the Cryo array type
                element_type = get_llvm_type(cryo_array_type->element());
                if (!element_type)
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Could not determine element type for Array<T>");
                    return nullptr;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> element type determined: {} (LLVM type: {})",
                          element_type_name, element_type->getTypeID());

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

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> index value generated successfully");

                // Check if this ArrayType maps to a struct (Array<T> class) or native LLVM array
                llvm::Type *array_llvm_type = get_llvm_type(resolved_type);

                // Debug: Log what type we got
                if (array_llvm_type)
                {
                    std::string llvm_type_str;
                    llvm::raw_string_ostream rso(llvm_type_str);
                    array_llvm_type->print(rso);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "*** Array<T> LLVM type: {}, isStructTy: {}, isArrayTy: {}, isPointerTy: {}",
                              llvm_type_str,
                              array_llvm_type->isStructTy() ? "true" : "false",
                              array_llvm_type->isArrayTy() ? "true" : "false",
                              array_llvm_type->isPointerTy() ? "true" : "false");
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> LLVM type is NULL - using direct struct access");

                    // Even if get_llvm_type fails, we know this is an Array<T> type
                    // Try to get the alloca and use the known Array<T> struct layout
                    llvm::AllocaInst *array_alloca = values().get_alloca(array_name);
                    if (array_alloca)
                    {
                        llvm::Type *alloca_type = array_alloca->getAllocatedType();
                        if (alloca_type && alloca_type->isStructTy())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Using alloca type for Array<T> struct access");
                            llvm::StructType *struct_type = llvm::cast<llvm::StructType>(alloca_type);

                            llvm::Value *elements_ptr = builder().CreateStructGEP(
                                struct_type, array_alloca, 0, array_name + ".elements.ptr");

                            llvm::Value *elements_array = create_load(elements_ptr,
                                                                      llvm::PointerType::get(element_type, 0),
                                                                      array_name + ".elements.load");

                            llvm::Value *element_ptr = builder().CreateGEP(
                                element_type, elements_array, index_val, "elem.ptr");

                            llvm::Value *result = create_load(element_ptr, element_type, "elem.load");
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> element loaded via alloca type");
                            return result;
                        }
                    }
                }

                if (array_llvm_type && array_llvm_type->isStructTy())
                {
                    // This is an Array<T> class (struct with elements field)
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> is a struct type - accessing elements field");

                    // Get the alloca for the Array<T> struct - we need a pointer for GEP, not a loaded value
                    llvm::AllocaInst *array_alloca = values().get_alloca(array_name);
                    if (!array_alloca)
                    {
                        report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                     "Failed to find alloca for Array<T> struct");
                        return nullptr;
                    }

                    llvm::Value *elements_ptr = builder().CreateStructGEP(
                        llvm::cast<llvm::StructType>(array_llvm_type),
                        array_alloca,
                        0, // 'elements' is the first field
                        array_name + ".elements.ptr");

                    // Load the elements pointer (T*)
                    llvm::Value *elements_array = create_load(elements_ptr,
                                                              llvm::PointerType::get(element_type, 0),
                                                              array_name + ".elements.load");

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> elements pointer loaded successfully");

                    // Now do pointer arithmetic: elements_array[index]
                    llvm::Value *element_ptr = builder().CreateGEP(
                        element_type,
                        elements_array,
                        index_val,
                        "elem.ptr");

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> element pointer created successfully");

                    // Load and return the element value
                    llvm::Value *result = create_load(element_ptr, element_type, "elem.load");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Array<T> element value loaded successfully, returning element");
                    return result;
                }
                else if (array_llvm_type && array_llvm_type->isArrayTy())
                {
                    // This is a native LLVM array type (like [5 x i64])
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** ArrayType is native LLVM array - using direct array access");

                    // Get the alloca for the array
                    llvm::AllocaInst *array_alloca = values().get_alloca(array_name);
                    if (!array_alloca)
                    {
                        report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                     "Failed to find alloca for native array");
                        return nullptr;
                    }

                    // For native arrays, use GEP with [0, index] to access element
                    std::vector<llvm::Value *> indices = {
                        llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0),
                        index_val};

                    llvm::Value *element_ptr = builder().CreateInBoundsGEP(
                        array_llvm_type,
                        array_alloca,
                        indices,
                        "elem.ptr");

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Native array element pointer created successfully");

                    // Load and return the element value
                    llvm::Value *result = create_load(element_ptr, element_type, "elem.load");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Native array element value loaded successfully");
                    return result;
                }
                else if (array_llvm_type && array_llvm_type->isPointerTy())
                {
                    // This is a dynamic array (pointer type like i64*)
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** ArrayType is pointer (dynamic array) - using pointer access");

                    // Generate the array pointer value
                    llvm::Value *array_ptr_val = generate(node->array());
                    if (!array_ptr_val)
                    {
                        report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                     "Failed to generate array pointer");
                        return nullptr;
                    }

                    // For pointer arrays, use GEP with single index
                    llvm::Value *element_ptr = builder().CreateGEP(
                        element_type,
                        array_ptr_val,
                        index_val,
                        "elem.ptr");

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Dynamic array element pointer created successfully");

                    // Load and return the element value
                    llvm::Value *result = create_load(element_ptr, element_type, "elem.load");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Dynamic array element value loaded successfully");
                    return result;
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "*** ArrayType has unexpected LLVM type, falling through to generic handling");
                }
            }

            // Handle Array<T> type class (when kind is Struct/Generic, not Array)
            // This handles cases where the resolved type is "Array<u64>" from the type class
            if (resolved_type && resolved_type->kind() != TypeKind::Array)
            {
                std::string type_name = resolved_type.get()->display_name();
                if (type_name.find("Array<") == 0 || type_name.find("[]") != std::string::npos)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "*** Detected Array<T> type class (kind: {}) for variable: '{}', type_name: '{}'",
                              static_cast<int>(resolved_type->kind()), array_name, type_name);

                    // Get the alloca - it should be the Array<T> struct type
                    llvm::AllocaInst *array_alloca = values().get_alloca(array_name);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "*** Array<T> type class: get_alloca('{}') returned: {}",
                              array_name, array_alloca ? "valid" : "NULL");

                    if (array_alloca)
                    {
                        llvm::Type *alloca_type = array_alloca->getAllocatedType();
                        std::string alloca_type_str = "NULL";
                        if (alloca_type)
                        {
                            llvm::raw_string_ostream rso(alloca_type_str);
                            alloca_type->print(rso);
                        }
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "*** Array<T> type class: alloca type: {}, isStructTy: {}",
                                  alloca_type_str,
                                  (alloca_type && alloca_type->isStructTy()) ? "true" : "false");

                        if (alloca_type && alloca_type->isStructTy())
                        {
                            llvm::StructType *struct_type = llvm::cast<llvm::StructType>(alloca_type);

                            // Try to determine element type from the type name
                            // For "Array<u64>", extract "u64" and map it
                            llvm::Type *elem_type = nullptr;
                            size_t start = type_name.find('<');
                            size_t end = type_name.rfind('>');
                            if (start != std::string::npos && end != std::string::npos && end > start)
                            {
                                std::string elem_type_name = type_name.substr(start + 1, end - start - 1);
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Extracted element type name: '{}'", elem_type_name);

                                // Map common type names to LLVM types
                                if (elem_type_name == "u64" || elem_type_name == "i64" || elem_type_name == "long")
                                    elem_type = llvm::Type::getInt64Ty(llvm_ctx());
                                else if (elem_type_name == "u32" || elem_type_name == "i32" || elem_type_name == "int")
                                    elem_type = llvm::Type::getInt32Ty(llvm_ctx());
                                else if (elem_type_name == "u16" || elem_type_name == "i16" || elem_type_name == "short")
                                    elem_type = llvm::Type::getInt16Ty(llvm_ctx());
                                else if (elem_type_name == "u8" || elem_type_name == "i8" || elem_type_name == "byte")
                                    elem_type = llvm::Type::getInt8Ty(llvm_ctx());
                                else if (elem_type_name == "f64" || elem_type_name == "double")
                                    elem_type = llvm::Type::getDoubleTy(llvm_ctx());
                                else if (elem_type_name == "f32" || elem_type_name == "float")
                                    elem_type = llvm::Type::getFloatTy(llvm_ctx());
                                else if (elem_type_name == "bool" || elem_type_name == "boolean")
                                    elem_type = llvm::Type::getInt1Ty(llvm_ctx());
                                else if (elem_type_name == "string")
                                    elem_type = llvm::PointerType::get(llvm_ctx(), 0); // Opaque pointer for strings
                                else
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Unknown element type name: '{}'", elem_type_name);
                            }

                            if (elem_type)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Element type mapped successfully, generating index");
                                // Generate index expression
                                llvm::Value *index_val = generate(node->index());
                                if (index_val)
                                {
                                    if (!index_val->getType()->isIntegerTy())
                                        index_val = cast_if_needed(index_val, llvm::Type::getInt64Ty(llvm_ctx()));

                                    // Access elements field (field 0) of Array<T> struct
                                    llvm::Value *elements_ptr = builder().CreateStructGEP(
                                        struct_type, array_alloca, 0, array_name + ".elements.ptr");

                                    llvm::Value *elements_array = create_load(elements_ptr,
                                                                              llvm::PointerType::get(elem_type, 0),
                                                                              array_name + ".elements.load");

                                    llvm::Value *element_ptr = builder().CreateGEP(
                                        elem_type, elements_array, index_val, "elem.ptr");

                                    llvm::Value *result = create_load(element_ptr, elem_type, "elem.load");
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "*** Array<T> type class element loaded successfully");
                                    return result;
                                }
                            }
                            else
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "*** Failed to map element type");
                            }
                        }
                    }
                }
            }

            // Additional logging for debugging
            if (!resolved_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "*** No resolved type found for '{}', falling back to traditional array access",
                          array_name);
            }

            // Try to get the alloca directly for traditional stack-allocated arrays
            if (llvm::AllocaInst *alloca = values().get_alloca(array_name))
            {
                llvm::Type *allocated_type = alloca->getAllocatedType();

                // Check if it's an array type
                if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(allocated_type))
                {
                    array_ptr = alloca;
                    element_type = arr_type->getElementType();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Found stack-allocated traditional array: {}, element type determined from alloca", array_name);
                }
            }
        }

        // Check if this is a member access expression (e.g., this.octets[0] or other.octets[0])
        // For array fields accessed via member access, we need to get the address
        // of the array member, not load its value, so we can do proper GEP
        if (!array_ptr)
        {
            if (auto *member_access = dynamic_cast<Cryo::MemberAccessNode *>(node->array()))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Processing array access on member: '{}'",
                          member_access->member());

                // First, try to get the address of the member directly
                // This works for references (like 'this') but may fail for by-value parameters
                llvm::Value *member_ptr = generate_member_address(member_access);

                // If generate_member_address failed, it might be because the object is a
                // by-value parameter (stored in an alloca but generate() loads the value).
                // In this case, check if the object is an identifier with an alloca.
                if (!member_ptr)
                {
                    if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(member_access->object()))
                    {
                        std::string obj_name = identifier->name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "ExpressionCodegen: generate_member_address failed, trying alloca for '{}'",
                                  obj_name);

                        // Check if this identifier has an alloca (by-value parameter case)
                        if (llvm::AllocaInst *alloca = values().get_alloca(obj_name))
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "ExpressionCodegen: Found alloca for '{}', using it for member access",
                                      obj_name);

                            // Resolve struct type and field index
                            llvm::StructType *struct_type = nullptr;
                            unsigned field_idx = 0;
                            if (resolve_member_info(member_access->object(), member_access->member(),
                                                    struct_type, field_idx))
                            {
                                // Create GEP to get field pointer from the alloca
                                member_ptr = create_struct_gep(struct_type, alloca, field_idx,
                                                               member_access->member() + ".ptr");
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "ExpressionCodegen: Created struct GEP for field '{}' at index {}",
                                          member_access->member(), field_idx);
                            }
                        }
                    }
                }

                if (member_ptr)
                {
                    // Resolve struct type and field info to get the field's LLVM type
                    llvm::StructType *struct_type = nullptr;
                    unsigned field_idx = 0;
                    if (resolve_member_info(member_access->object(), member_access->member(),
                                            struct_type, field_idx))
                    {
                        llvm::Type *field_type = struct_type->getElementType(field_idx);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "ExpressionCodegen: Member field type ID: {}, isArrayTy: {}",
                                  field_type->getTypeID(),
                                  field_type->isArrayTy() ? "true" : "false");

                        // Check if the field is an array type (like [4 x i8])
                        if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(field_type))
                        {
                            element_type = arr_type->getElementType();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "ExpressionCodegen: Array field element type ID: {}",
                                      element_type->getTypeID());

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

                            // For array fields, we need [0, index] indices to access element
                            std::vector<llvm::Value *> indices = {
                                llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0),
                                index_val};

                            llvm::Value *element_ptr = builder().CreateInBoundsGEP(
                                field_type,
                                member_ptr,
                                indices,
                                member_access->member() + ".elem.ptr");

                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "ExpressionCodegen: Created GEP for array member element access");

                            // Load and return the element value
                            llvm::Value *result = create_load(element_ptr, element_type,
                                                              member_access->member() + ".elem.load");
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "ExpressionCodegen: Loaded array element, type ID: {}",
                                      result->getType()->getTypeID());
                            return result;
                        }
                    }
                }
            }
        }

        // If we didn't find a stack array, use the standard path
        if (!array_ptr)
        {
            // Generate array expression
            llvm::Value *array_val = generate(node->array());
            if (!array_val)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array expression");
                return nullptr;
            }

            array_ptr = array_val;

            // Determine element type from array type
            if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(array_val->getType()))
            {
                element_type = arr_type->getElementType();
            }
            else if (array_val->getType()->isPointerTy())
            {
                // For pointer types (strings, etc.), try to get from resolved type
                TypeRef cryo_array_type = node->array()->get_resolved_type();
                if (cryo_array_type.is_valid())
                {
                    if (cryo_array_type->kind() == TypeKind::Array)
                    {
                        auto *arr = static_cast<const Cryo::ArrayType *>(cryo_array_type.get());
                        element_type = get_llvm_type(arr->element());
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Determined element type from resolved Cryo array type for non-stack array");
                    }
                    else if (cryo_array_type->kind() == TypeKind::String)
                    {
                        element_type = llvm::Type::getInt8Ty(llvm_ctx());
                    }
                    else if (cryo_array_type->kind() == TypeKind::Pointer)
                    {
                        auto *ptr = static_cast<const Cryo::PointerType *>(cryo_array_type.get());
                        element_type = get_llvm_type(ptr->pointee());
                    }
                }
                // Fallback to i8 for string-like access only if we still don't have an element type
                if (!element_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Falling back to i8 element type for array access");
                    element_type = llvm::Type::getInt8Ty(llvm_ctx());
                }
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

        // For stack-allocated arrays, we need to use InBoundsGEP with proper indices
        llvm::Value *element_ptr = nullptr;
        if (llvm::isa<llvm::AllocaInst>(array_ptr) &&
            llvm::isa<llvm::ArrayType>(static_cast<llvm::AllocaInst *>(array_ptr)->getAllocatedType()))
        {
            // For stack arrays, we need [0, index] indices
            std::vector<llvm::Value *> indices = {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                index_val};
            element_ptr = builder().CreateInBoundsGEP(
                static_cast<llvm::AllocaInst *>(array_ptr)->getAllocatedType(),
                array_ptr,
                indices,
                "elem.ptr");
        }
        else
        {
            // For pointer arrays, use single index
            element_ptr = create_array_gep(element_type, array_ptr, index_val, "elem.ptr");
        }

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

        // For array access, we need to handle stack-allocated arrays differently
        llvm::Value *array_ptr = nullptr;
        llvm::Type *element_type = nullptr;

        // Check if this is a simple identifier (stack-allocated array or Array<T>)
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->array()))
        {
            std::string array_name = identifier->name();

            // Check if this is an Array<T> type first
            TypeRef resolved_type = identifier->get_resolved_type();

            // Fallback: if identifier doesn't have resolved type, check variable_types_map
            if (!resolved_type)
            {
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find(array_name);
                if (it != var_types.end())
                {
                    resolved_type = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "ExpressionCodegen: Found Array<T> type in variable_types_map for address '{}': {}",
                              array_name, resolved_type.get()->display_name());
                }
            }

            if (resolved_type && resolved_type->kind() == TypeKind::Array)
            {
                auto *cryo_array_type = static_cast<const Cryo::ArrayType *>(resolved_type.get());
                std::string element_type_name = cryo_array_type->element()->display_name();

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Detected Array<{}> address access for variable: {}", element_type_name, array_name);

                // This is an Array<T> type (like u64[] which is Array<u64>)
                // We need to access the 'elements' field first: arr[index] becomes arr.elements[index]

                // Generate the Array<T> instance
                llvm::Value *array_instance = generate(node->array());
                if (!array_instance)
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Failed to generate Array<T> instance for address");
                    return nullptr;
                }

                // Get the element type from the Cryo array type
                element_type = get_llvm_type(cryo_array_type->element());
                if (!element_type)
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Could not determine element type for Array<T> address");
                    return nullptr;
                }

                // Generate index expression
                llvm::Value *index_val = generate(node->index());
                if (!index_val)
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Failed to generate index expression for address");
                    return nullptr;
                }

                // Ensure index is integer type
                if (!index_val->getType()->isIntegerTy())
                {
                    index_val = cast_if_needed(index_val, llvm::Type::getInt64Ty(llvm_ctx()));
                }

                // Access the 'elements' field (first field in Array<T> struct)
                // Get the Array<T> struct type from the Cryo type system
                llvm::Type *array_struct_type = get_llvm_type(resolved_type);
                if (!array_struct_type || !array_struct_type->isStructTy())
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Array<T> does not map to a struct type for address");
                    return nullptr;
                }

                llvm::Value *elements_ptr = builder().CreateStructGEP(
                    llvm::cast<llvm::StructType>(array_struct_type),
                    array_instance,
                    0, // 'elements' is the first field
                    array_name + ".elements.ptr");

                // Load the elements pointer (T*)
                llvm::Value *elements_array = create_load(elements_ptr,
                                                          llvm::PointerType::get(element_type, 0),
                                                          array_name + ".elements.load");

                // Return address of elements_array[index]
                return builder().CreateGEP(
                    element_type,
                    elements_array,
                    index_val,
                    "elem.ptr");
            }

            // Try to get the alloca directly for traditional stack-allocated arrays
            if (llvm::AllocaInst *alloca = values().get_alloca(array_name))
            {
                llvm::Type *allocated_type = alloca->getAllocatedType();

                // Check if it's an array type
                if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(allocated_type))
                {
                    array_ptr = alloca;
                    element_type = arr_type->getElementType();
                }
            }
        }

        // If we didn't find a stack array, use the standard path
        if (!array_ptr)
        {
            // Generate array expression
            array_ptr = generate(node->array());
            if (!array_ptr)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array expression");
                return nullptr;
            }

            // Get element type
            if (auto *arr_type = llvm::dyn_cast<llvm::ArrayType>(array_ptr->getType()))
            {
                element_type = arr_type->getElementType();
            }
            else
            {
                // Assume pointer to elements
                element_type = llvm::Type::getInt8Ty(llvm_ctx()); // Default
            }
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

        // For stack-allocated arrays, we need to use InBoundsGEP with proper indices
        if (llvm::isa<llvm::AllocaInst>(array_ptr) &&
            llvm::isa<llvm::ArrayType>(static_cast<llvm::AllocaInst *>(array_ptr)->getAllocatedType()))
        {
            // For stack arrays, we need [0, index] indices
            std::vector<llvm::Value *> indices = {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                index_val};
            return builder().CreateInBoundsGEP(
                static_cast<llvm::AllocaInst *>(array_ptr)->getAllocatedType(),
                array_ptr,
                indices,
                "elem.ptr");
        }
        else
        {
            // For pointer arrays, use single index
            return create_array_gep(element_type, array_ptr, index_val, "elem.ptr");
        }
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

        // Get the target type - prefer resolved type, fall back to annotation
        llvm::Type *llvm_target = nullptr;
        std::string target_type_name;

        if (node->has_resolved_target_type())
        {
            TypeRef resolved = node->get_resolved_target_type();
            llvm_target = get_llvm_type(resolved);
            target_type_name = resolved->display_name();
        }
        else if (node->target_type_annotation())
        {
            target_type_name = node->target_type_annotation()->to_string();
            llvm_target = types().get_type(target_type_name);
        }

        if (!llvm_target)
        {
            report_error(ErrorCode::E0626_CAST_OPERATION_ERROR, node, "Unknown cast target type: " + target_type_name);
            return nullptr;
        }

        // Cast using LLVM type
        return cast_if_needed(value, llvm_target);
    }

    llvm::Value *ExpressionCodegen::generate_cast(llvm::Value *value, TypeRef target_type)
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

        // Check if type is sized before computing size
        if (!llvm_type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: sizeof called on unsized type '{}', returning 0",
                     node->type_name());
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0);
        }

        // Generate sizeof using LLVM type
        auto &context = llvm_ctx();
        auto &data_layout = module()->getDataLayout();
        uint64_t size = data_layout.getTypeAllocSize(llvm_type);
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), size);
    }

    llvm::Value *ExpressionCodegen::generate_sizeof(TypeRef type)
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

        // Check if type is sized before computing size
        if (!llvm_type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: sizeof called on unsized type '{}', returning 0",
                     type.get()->display_name());
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0);
        }

        const llvm::DataLayout &dl = module()->getDataLayout();
        uint64_t size = dl.getTypeAllocSize(llvm_type);

        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
    }

    llvm::Value *ExpressionCodegen::generate_alignof(Cryo::AlignofExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null alignof node");
            return nullptr;
        }

        // TODO: Get proper Cryo::Type* from node instead of string lookup
        llvm::Type *llvm_type = types().get_type(node->type_name());
        if (!llvm_type)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Unknown type for alignof: " + node->type_name());
            return nullptr;
        }

        // Check if type is sized before computing alignment
        if (!llvm_type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: alignof called on unsized type '{}', returning default alignment",
                     node->type_name());
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 8);
        }

        // Generate alignof using LLVM type
        auto &context = llvm_ctx();
        auto &data_layout = module()->getDataLayout();
        uint64_t align = data_layout.getABITypeAlign(llvm_type).value();
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), align);
    }

    llvm::Value *ExpressionCodegen::generate_alignof(TypeRef type)
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

        // Check if type is sized before computing alignment
        if (!llvm_type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: alignof called on unsized type '{}', returning default alignment",
                     type.get()->display_name());
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 8);
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

    llvm::Value *ExpressionCodegen::generate_dereference(llvm::Value *operand, TypeRef pointee_type)
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
        llvm::BasicBlock *ternary_block = builder().GetInsertBlock();
        if (!ternary_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for ternary expression");
            return nullptr;
        }

        llvm::Function *fn = ternary_block->getParent();
        if (!fn)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for ternary expression");
            return nullptr;
        }
        
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
    // If Expression
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_if_expression(Cryo::IfExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null if-expression node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating if-expression");

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
                             "If-expression condition must be boolean");
                return nullptr;
            }
        }

        // Create basic blocks
        llvm::BasicBlock *if_expr_block = builder().GetInsertBlock();
        if (!if_expr_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for if expression");
            return nullptr;
        }

        llvm::Function *fn = if_expr_block->getParent();
        if (!fn)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for if expression");
            return nullptr;
        }
        
        llvm::BasicBlock *then_block = create_block("ifexpr.then", fn);
        llvm::BasicBlock *else_block = create_block("ifexpr.else", fn);
        llvm::BasicBlock *merge_block = create_block("ifexpr.merge", fn);

        // Branch on condition
        builder().CreateCondBr(condition, then_block, else_block);

        // Generate then value
        builder().SetInsertPoint(then_block);
        llvm::Value *then_val = generate(node->then_expression());
        if (!then_val)
        {
            return nullptr;
        }
        llvm::BasicBlock *then_end = builder().GetInsertBlock();
        builder().CreateBr(merge_block);

        // Generate else value
        builder().SetInsertPoint(else_block);
        llvm::Value *else_val = generate(node->else_expression());
        if (!else_val)
        {
            return nullptr;
        }
        llvm::BasicBlock *else_end = builder().GetInsertBlock();
        builder().CreateBr(merge_block);

        // Create phi node
        builder().SetInsertPoint(merge_block);
        llvm::PHINode *phi = builder().CreatePHI(then_val->getType(), 2, "ifexpr.result");
        phi->addIncoming(then_val, then_end);
        phi->addIncoming(cast_if_needed(else_val, then_val->getType()), else_end);

        return phi;
    }

    //===================================================================
    // Match Expression
    //===================================================================

    llvm::Value *ExpressionCodegen::generate_match_expression(Cryo::MatchExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null match-expression node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating match-expression");

        // Check that control flow codegen is available
        if (!_control_flow)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "ControlFlowCodegen not available for match expression");
            return nullptr;
        }

        // Get current context
        llvm::BasicBlock *match_block = builder().GetInsertBlock();
        if (!match_block)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current basic block for match expression");
            return nullptr;
        }

        llvm::Function *fn = match_block->getParent();
        if (!fn)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "No current function for match expression");
            return nullptr;
        }

        // Generate the match expression value
        llvm::Value *match_value = generate(node->expression());
        if (!match_value)
        {
            report_error(ErrorCode::E0613_CONTROL_FLOW_ERROR, node, "Failed to generate match expression value");
            return nullptr;
        }

        // Create merge block for collecting results
        llvm::BasicBlock *merge_block = create_block("matchexpr.merge", fn);

        // We need to track arm values and their source blocks for the PHI node
        std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> arm_results;

        // Generate arms as a chain of comparisons
        llvm::BasicBlock *current_test_block = builder().GetInsertBlock();

        const auto &arms = node->arms();
        for (size_t i = 0; i < arms.size(); ++i)
        {
            auto *arm = arms[i].get();
            if (!arm)
                continue;

            bool is_last = (i == arms.size() - 1);

            // Create blocks for this arm
            llvm::BasicBlock *arm_block = create_block("matchexpr.arm." + std::to_string(i), fn);
            llvm::BasicBlock *next_test_block = is_last
                                                    ? merge_block
                                                    : create_block("matchexpr.test." + std::to_string(i + 1), fn);

            builder().SetInsertPoint(current_test_block);

            // Generate pattern match
            Cryo::PatternNode *pattern = arm->pattern();
            if (pattern)
            {
                // Use control flow codegen to generate pattern match
                llvm::Value *matches = _control_flow->generate_pattern_match(match_value, pattern);
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

            // Generate arm body and get result value
            builder().SetInsertPoint(arm_block);
            values().enter_scope("matchexpr.arm." + std::to_string(i));

            // Bind pattern variables before generating body
            if (pattern)
            {
                _control_flow->bind_enum_pattern_variables(match_value, pattern);
            }

            llvm::Value *arm_value = nullptr;
            if (arm->body())
            {
                // For match expressions, the arm body should be an expression that returns a value
                // The body is a StatementNode, typically a BlockStatement containing an expression
                Cryo::Codegen::CodegenVisitor *visitor = ctx().visitor();
                arm->body()->accept(*visitor);
                arm_value = get_result();
            }

            values().exit_scope();

            // If we got a value, branch to merge and record it
            llvm::BasicBlock *arm_end = builder().GetInsertBlock();
            if (arm_end && !arm_end->getTerminator())
            {
                builder().CreateBr(merge_block);
                if (arm_value)
                {
                    arm_results.push_back({arm_value, arm_end});
                }
            }

            current_test_block = next_test_block;
        }

        // Create PHI node to merge results
        builder().SetInsertPoint(merge_block);

        if (arm_results.empty())
        {
            // No arms produced values - return void
            return llvm::Constant::getNullValue(llvm::Type::getInt8Ty(llvm_ctx()));
        }

        // Determine result type from first arm
        llvm::Type *result_type = arm_results[0].first->getType();
        llvm::PHINode *phi = builder().CreatePHI(result_type, arm_results.size(), "matchexpr.result");

        for (const auto &[value, block] : arm_results)
        {
            llvm::Value *casted_value = cast_if_needed(value, result_type);
            phi->addIncoming(casted_value, block);
        }

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

        // Special handling for 'this' - prefer current_type_name for generic instantiations
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(object))
        {
            if (identifier->name() == "this")
            {
                // First try current_type_name (set during generic method instantiation)
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

                // Fallback: Try variable_types_map
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find("this");
                if (it != var_types.end() && it->second)
                {
                    TypeRef this_type = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_member_info: Found 'this' in variable_types_map: {}",
                              this_type.get()->display_name());

                    // Get the type name (handle pointer types)
                    std::string this_type_name = this_type.get()->display_name();
                    if (this_type->kind() == Cryo::TypeKind::Pointer)
                    {
                        auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(this_type.get());
                        if (ptr_type && ptr_type->pointee())
                        {
                            this_type_name = ptr_type->pointee().get()->display_name();
                        }
                    }
                    else if (!this_type_name.empty() && this_type_name.back() == '*')
                    {
                        this_type_name.pop_back();
                    }

                    // Look up the struct type
                    out_struct_type = llvm::StructType::getTypeByName(llvm_ctx(), this_type_name);
                    if (!out_struct_type)
                    {
                        if (llvm::Type *type = ctx().get_type(this_type_name))
                        {
                            out_struct_type = llvm::dyn_cast<llvm::StructType>(type);
                        }
                    }

                    if (out_struct_type)
                    {
                        int field_idx = ctx().get_struct_field_index(this_type_name, member_name);
                        if (field_idx >= 0)
                        {
                            out_field_idx = static_cast<unsigned>(field_idx);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: Resolved this.{} to index {} via variable_types_map",
                                      member_name, out_field_idx);
                            return true;
                        }
                    }
                }
            }
        }

        // Get the resolved type of the object expression
        TypeRef obj_type = object->get_resolved_type();
        if (!obj_type)
        {
            // Fallback: If object is an identifier, try looking up its type from variable_types_map
            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(object))
            {
                const std::string &var_name = identifier->name();
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find(var_name);
                if (it != var_types.end() && it->second.is_valid())
                {
                    obj_type = it->second;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_member_info: Found type for '{}' in variable_types_map: {}",
                              var_name, obj_type->display_name());
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
        std::string type_name = obj_type.get()->display_name();

        // Handle pointer types - get the pointee type name
        if (obj_type->kind() == Cryo::TypeKind::Pointer)
        {
            auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
            if (ptr_type && ptr_type->pointee())
            {
                type_name = ptr_type->pointee().get()->display_name();
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
            // Fallback: try TemplateRegistry for cross-module struct field lookup
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Field '{}' not in local registry for '{}', trying TemplateRegistry",
                      member_name, type_name);

            if (auto *template_reg = ctx().template_registry())
            {
                // Try various name candidates (qualified and unqualified)
                std::vector<std::string> candidates = {type_name};
                if (!ctx().namespace_context().empty())
                {
                    candidates.push_back(ctx().namespace_context() + "::" + type_name);
                }

                for (const auto &candidate : candidates)
                {
                    const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(candidate);
                    if (field_info && !field_info->field_names.empty())
                    {
                        // Find the field index by name
                        for (size_t i = 0; i < field_info->field_names.size(); ++i)
                        {
                            if (field_info->field_names[i] == member_name)
                            {
                                field_idx = static_cast<int>(i);
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "resolve_member_info: Found field '{}' at index {} via TemplateRegistry ({})",
                                          member_name, field_idx, candidate);

                                // Register the field indices for future lookups
                                ctx().register_struct_fields(type_name, field_info->field_names);
                                break;
                            }
                        }
                        if (field_idx >= 0)
                            break;
                    }
                }
            }

            if (field_idx < 0)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_member_info: Field '{}' not found in struct '{}' (also checked TemplateRegistry)",
                          member_name, type_name);
                return false;
            }
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

        // Check if this array literal is being used to initialize an Array<T> type
        // by looking at the context or expected type
        TypeRef resolved_type = node->get_resolved_type();
        if (resolved_type)
        {
            std::string type_name = resolved_type.get()->display_name();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Array literal resolved type: {}", type_name);

            // Check if this is an Array<T> type (syntactic sugar for u64[], i32[], etc.)
            if (type_name.find("Array<") == 0 || type_name.find("[]") != std::string::npos)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Detected Array<T> type, generating Array constructor call");

                // Get the correct element type from the Cryo Array type, not from the generated elements
                // This ensures u64[] uses i64, not i32 (which integer literals default to)
                if (resolved_type->kind() == TypeKind::Array)
                {
                    auto *arr_type = static_cast<const Cryo::ArrayType *>(resolved_type.get());
                    llvm::Type *cryo_elem_type = get_llvm_type(arr_type->element());
                    if (cryo_elem_type)
                    {
                        elem_type = cryo_elem_type;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Using element type from Cryo Array type");
                    }
                }

                return generate_array_constructor_call(node, elements, elem_type);
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Array literal has NO resolved type - may need to infer from context");
        }

        // Fallback to traditional C-style array for non-Array<T> types
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

    llvm::Value *ExpressionCodegen::generate_array_constructor_call(Cryo::ArrayLiteralNode *node,
                                                                    const std::vector<std::unique_ptr<Cryo::ExpressionNode>> &elements,
                                                                    llvm::Type *elem_type)
    {
        // Add basic debug output that will definitely show up
        std::string type_str;
        llvm::raw_string_ostream rso(type_str);
        elem_type->print(rso);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating Array<T> constructor call");

        // First, create a traditional C-style array to hold the elements
        llvm::ArrayType *raw_array_type = llvm::ArrayType::get(elem_type, elements.size());
        llvm::AllocaInst *elements_array = create_entry_alloca(raw_array_type, "array.elements");

        // Store each element in the raw array
        for (size_t i = 0; i < elements.size(); ++i)
        {
            llvm::Value *elem_val = generate(elements[i].get());
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
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(raw_array_type, elements_array, indices,
                                                                "elem." + std::to_string(i) + ".ptr");
            builder().CreateStore(elem_val, elem_ptr);
        }

        // Get Array<T> struct type
        TypeRef resolved_type = node->get_resolved_type();
        if (!resolved_type)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node, "No resolved Array<T> type");
            return nullptr;
        }

        llvm::Type *array_struct_type = get_llvm_type(resolved_type);
        if (!array_struct_type)
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node, "Failed to get LLVM Array<T> struct type");
            return nullptr;
        }

        // Create Array<T> instance
        llvm::AllocaInst *array_instance = create_entry_alloca(array_struct_type, "array.instance");

        // Get pointer to the raw array data (elements field)
        llvm::Value *array_data_indices[] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0)};
        llvm::Value *array_data_ptr = builder().CreateInBoundsGEP(raw_array_type, elements_array, array_data_indices, "array.data.ptr");

        // Set elements field (field 0)
        if (llvm::StructType *struct_type = llvm::dyn_cast<llvm::StructType>(array_struct_type))
        {
            llvm::Value *elements_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 0, "elements.ptr");
            builder().CreateStore(array_data_ptr, elements_field_ptr);

            // Set length field (field 1)
            llvm::Value *length_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 1, "length.ptr");
            llvm::Value *length_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), elements.size());
            builder().CreateStore(length_val, length_field_ptr);

            // Set capacity field (field 2) - same as length for array literals
            llvm::Value *capacity_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 2, "capacity.ptr");
            builder().CreateStore(length_val, capacity_field_ptr);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Created Array<T> instance with {} elements", elements.size());

            // Load and return the struct value, not the alloca pointer
            // This is necessary because the caller will store this value into another alloca
            return create_load(array_instance, struct_type, "array.value");
        }
        else
        {
            report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node, "Array<T> type is not a struct type");
            return nullptr;
        }
    }

    llvm::Value *ExpressionCodegen::generate_struct_literal(Cryo::StructLiteralNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, "Null struct literal node");
            return nullptr;
        }

        std::string type_name = node->struct_type();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating struct literal for {}",
                  type_name);

        // Look up struct type
        llvm::Type *struct_type = ctx().get_type(type_name);
        if (!struct_type || !struct_type->isStructTy())
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Unknown struct type: " + type_name);
            return nullptr;
        }

        auto *st = llvm::cast<llvm::StructType>(struct_type);

        // CRITICAL: Check if struct is opaque (unsized) before proceeding
        // This can happen if the struct body hasn't been set yet due to order-of-operations issues
        if (st->isOpaque())
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: ERROR - Struct '{}' is OPAQUE (unsized), cannot create struct literal. "
                     "This likely means the struct body was not set before method body generation.",
                     type_name);

            // Try to look up the struct type by name directly from LLVM context
            // The type might have been registered under a different name (e.g., qualified name)
            std::string module_namespace = ctx().namespace_context();
            std::string qualified_name = module_namespace.empty() ? type_name : module_namespace + "::" + type_name;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "Attempting to find non-opaque struct under qualified name: '{}'", qualified_name);

            llvm::StructType *qualified_type = llvm::StructType::getTypeByName(llvm_ctx(), qualified_name);
            if (qualified_type && !qualified_type->isOpaque())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                         "Found non-opaque struct type under qualified name: '{}'", qualified_name);
                st = qualified_type;
                struct_type = qualified_type;
            }
            else
            {
                // Last resort: try to complete from TypeMapper registry
                llvm::StructType *completed = types().lookup_struct(type_name);
                if (completed && !completed->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                             "Completed struct '{}' from TypeMapper registry", type_name);
                    st = completed;
                    struct_type = completed;
                }
                else
                {
                    report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                                 "Cannot create struct literal for opaque (unsized) struct: " + type_name);
                    return nullptr;
                }
            }
        }

        // Check if we're in a constructor - constructors should allocate on heap
        bool is_constructor = false;
        llvm::Function *current_fn = builder().GetInsertBlock()
                                         ? builder().GetInsertBlock()->getParent()
                                         : nullptr;
        if (current_fn)
        {
            std::string fn_name = current_fn->getName().str();
            // Simple heuristic: if function returns a pointer and the name contains the struct type
            // it's likely a constructor
            llvm::Type *ret_type = current_fn->getReturnType();
            is_constructor = ret_type && ret_type->isPointerTy() && 
                           fn_name.find(type_name) != std::string::npos;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, 
                     "Function '{}' constructor check: {} (return type: {}, contains '{}': {})", 
                     fn_name, is_constructor ? "yes" : "no", 
                     ret_type->isPointerTy() ? "pointer" : "non-pointer",
                     type_name, fn_name.find(type_name) != std::string::npos ? "yes" : "no");
        }

        llvm::Value *struct_storage = nullptr;
        
        if (is_constructor)
        {
            // For constructors: allocate on heap using malloc
            const llvm::DataLayout &DL = module()->getDataLayout();
            llvm::Value *size = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvm_ctx()), 
                DL.getTypeAllocSize(struct_type));
            
            // Get or declare malloc function
            llvm::Function *malloc_fn = module()->getFunction("malloc");
            if (!malloc_fn)
            {
                llvm::Type *ptr_type = llvm::Type::getInt8Ty(llvm_ctx())->getPointerTo();
                llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
                llvm::FunctionType *fn_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
                malloc_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "malloc", module());
            }
            
            llvm::Value *raw_ptr = builder().CreateCall(malloc_fn, {size}, "heap.alloc");
            // Cast void* to struct*
            struct_storage = builder().CreateBitCast(raw_ptr, 
                                                     struct_type->getPointerTo(),
                                                     type_name + ".heap.ptr");
            
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, 
                     "Constructor: allocated {} on heap", type_name);
        }
        else
        {
            // For non-constructors: allocate on stack
            struct_storage = create_entry_alloca(struct_type, type_name + ".literal");
        }

        // Initialize fields from field initializers
        // IMPORTANT: Use field name lookup to get correct index, NOT positional index
        // Fields may be provided in any order (e.g., Point { y: 5, x: 10 })
        const auto &field_initializers = node->field_initializers();
        for (size_t i = 0; i < field_initializers.size(); ++i)
        {
            const auto &initializer = field_initializers[i];
            if (!initializer || !initializer->value())
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Null field initializer at index {}", i);
                continue;
            }

            std::string field_name = initializer->field_name();

            // Look up the actual field index by name - this handles fields in any order
            int field_idx = ctx().get_struct_field_index(type_name, field_name);
            if (field_idx < 0)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Field '{}' not found in struct '{}', skipping", field_name, type_name);
                continue;
            }

            // Verify field index is valid
            if (static_cast<unsigned>(field_idx) >= st->getNumElements())
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Field index {} out of bounds for struct '{}' with {} elements",
                         field_idx, type_name, st->getNumElements());
                continue;
            }

            llvm::Value *field_val = generate(initializer->value());
            if (!field_val)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to generate struct field {}",
                         field_name);
                continue;
            }

            // Cast if needed - use the correct field type based on looked-up index
            field_val = cast_if_needed(field_val, st->getElementType(field_idx));

            // Get pointer to field using correct index
            llvm::Value *field_ptr = create_struct_gep(struct_type, struct_storage,
                                                       static_cast<unsigned>(field_idx),
                                                       field_name + ".ptr");
            create_store(field_val, field_ptr);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "Initialized struct field '{}' at index {} (type: {})",
                     field_name, field_idx, type_name);
        }

        if (is_constructor)
        {
            // For constructors: return pointer to heap-allocated struct
            return struct_storage;
        }
        else
        {
            // For non-constructors: load and return the struct value, not the alloca pointer
            // This is necessary because the caller expects a struct value, not a pointer
            return create_load(struct_storage, struct_type, type_name + ".value");
        }
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

        llvm::Type *alloc_type = nullptr;
        std::string type_name_for_alloc = node->type_name();

        // Check if this is a generic type instantiation
        if (!node->generic_args().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generic new expression with {} type args",
                      node->generic_args().size());

            // Get GenericCodegen via visitor
            auto *generics = ctx().visitor() ? ctx().visitor()->get_generics() : nullptr;
            if (!generics)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                             "GenericCodegen not available for generic type instantiation");
                return nullptr;
            }

            // Convert string type arguments to Cryo::TypeRef
            std::vector<TypeRef> type_args;
            for (const auto &type_arg_str : node->generic_args())
            {
                TypeRef arg_type = symbols().lookup_struct_type(type_arg_str);
                if (!arg_type.is_valid())
                {
                    arg_type = symbols().lookup_class_type(type_arg_str);
                }
                if (!arg_type.is_valid())
                {
                    // Try common primitive types
                    if (type_arg_str == "int" || type_arg_str == "i32")
                        arg_type = symbols().arena().get_i32();
                    else if (type_arg_str == "i64" || type_arg_str == "long")
                        arg_type = symbols().arena().get_i64();
                    else if (type_arg_str == "f32" || type_arg_str == "float")
                        arg_type = symbols().arena().get_f32();
                    else if (type_arg_str == "f64" || type_arg_str == "double")
                        arg_type = symbols().arena().get_f64();
                    else if (type_arg_str == "string")
                        arg_type = symbols().arena().get_string();
                    else if (type_arg_str == "bool" || type_arg_str == "boolean")
                        arg_type = symbols().arena().get_bool();
                }

                if (arg_type.is_valid())
                {
                    type_args.push_back(arg_type);
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::CODEGEN,
                             "ExpressionCodegen: Failed to resolve type argument: {}", type_arg_str);
                }
            }

            // Instantiate the generic type
            llvm::StructType *instantiated_type = generics->instantiate_struct(node->type_name(), type_args);
            if (!instantiated_type)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                             "Failed to instantiate generic type: " + node->type_name());
                return nullptr;
            }

            alloc_type = instantiated_type;
            type_name_for_alloc = instantiated_type->getName().str();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Instantiated generic type: {}", type_name_for_alloc);
        }
        else
        {
            // Get the type to allocate (non-generic)
            alloc_type = ctx().get_type(node->type_name());
        }

        if (!alloc_type)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                         "Unknown type for new: " + node->type_name());
            return nullptr;
        }

        // Calculate size (with safety check for unsized types)
        const llvm::DataLayout &dl = module()->getDataLayout();
        uint64_t size;
        if (alloc_type->isSized())
        {
            size = dl.getTypeAllocSize(alloc_type);
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: new expression for unsized type '{}', using default size",
                     node->type_name());
            size = 8; // Default to pointer size for unsized types
        }

        // Call cryo_alloc (runtime heap allocator)
        llvm::Function *cryo_alloc_fn = module()->getFunction("std::Runtime::cryo_alloc");
        if (!cryo_alloc_fn)
        {
            llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
            llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
            llvm::FunctionType *cryo_alloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
            cryo_alloc_fn = llvm::Function::Create(cryo_alloc_type, llvm::Function::ExternalLinkage,
                                                   "std::Runtime::cryo_alloc", module());
        }

        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
        llvm::Value *ptr = builder().CreateCall(cryo_alloc_fn, {size_val}, "new." + type_name_for_alloc);

        // If there are constructor arguments, call constructor
        if (!node->arguments().empty())
        {
            // Look up the constructor using multiple patterns (consistent with CallCodegen)
            llvm::Function *ctor_fn = nullptr;

            // Try various constructor name patterns
            std::vector<std::string> ctor_patterns = {
                type_name_for_alloc + "::" + node->type_name(),  // Point::Point
                type_name_for_alloc + "::init",                   // Point::init
                type_name_for_alloc + "::new",                    // Point::new
                node->type_name() + "::" + node->type_name(),    // Also try simple name
            };

            for (const auto &ctor_name : ctor_patterns)
            {
                ctor_fn = module()->getFunction(ctor_name);
                if (ctor_fn)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_new: Found constructor '{}' for type {}",
                              ctor_name, type_name_for_alloc);
                    break;
                }
                // Also try context's function registry
                ctor_fn = ctx().get_function(ctor_name);
                if (ctor_fn)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_new: Found constructor '{}' in registry for type {}",
                              ctor_name, type_name_for_alloc);
                    break;
                }
            }

            if (ctor_fn)
            {
                std::vector<llvm::Value *> ctor_args;
                ctor_args.push_back(ptr); // 'this' pointer

                // Generate constructor arguments
                for (const auto &arg : node->arguments())
                {
                    llvm::Value *arg_val = generate(arg.get());
                    if (arg_val)
                        ctor_args.push_back(arg_val);
                }

                builder().CreateCall(ctor_fn, ctor_args);
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "generate_new: No constructor found for type '{}', memory will be uninitialized",
                         type_name_for_alloc);
            }
        }

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

        // Helper lambda to try finding a qualified name
        auto try_find = [this](const std::string &name) -> llvm::Value *
        {
            // Try as function
            if (llvm::Function *fn = module()->getFunction(name))
                return fn;

            // Try as global variable
            if (llvm::GlobalVariable *global = module()->getGlobalVariable(name))
                return global;

            // Try context function registry
            if (llvm::Function *fn = ctx().get_function(name))
                return fn;

            // Try as enum variant
            auto &enum_variants = ctx().enum_variants_map();
            auto it = enum_variants.find(name);
            if (it != enum_variants.end())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Found enum variant: {}", name);
                return it->second;
            }

            return nullptr;
        };

        // First try exact match
        if (llvm::Value *result = try_find(qualified_name))
        {
            return result;
        }

        // Use SRM to generate lookup candidates for scope resolution
        // This handles: current namespace prefix, imported namespaces, etc.
        auto candidates = generate_lookup_candidates(qualified_name, Cryo::SymbolKind::Type);
        for (const auto &candidate : candidates)
        {
            if (llvm::Value *result = try_find(candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Found '{}' as '{}'", qualified_name, candidate);
                return result;
            }
        }

        // Also try with just the member name in case scope is current namespace
        auto member_candidates = generate_lookup_candidates(node->member_name(), Cryo::SymbolKind::Type);
        for (const auto &candidate : member_candidates)
        {
            // Build candidate with original scope
            std::string scoped_candidate = node->scope_name() + "::" + candidate;
            if (llvm::Value *result = try_find(scoped_candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Found '{}' as '{}'", qualified_name, scoped_candidate);
                return result;
            }
        }

        // Log failure for debugging
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Failed to find '{}'", qualified_name);
        auto &enum_variants = ctx().enum_variants_map();
        if (!enum_variants.empty() && enum_variants.size() <= 20)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Available enum variants ({} total):", enum_variants.size());
            for (const auto &[name, val] : enum_variants)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "  - {}", name);
            }
        }

        // In generic contexts, create a placeholder value instead of failing completely
        // This prevents segmentation faults when generic enum variants can't be resolved
        if (qualified_name.find("Option::None") != std::string::npos ||
            qualified_name.find("Result::Err") != std::string::npos ||
            qualified_name.find("Result::Ok") != std::string::npos)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Creating placeholder for unresolved generic enum variant: {}", qualified_name);
            // Return a null pointer as placeholder - this will be handled by the calling code
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx(), 0));
        }

        report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, node,
                     "Cannot resolve: " + qualified_name);
        return nullptr;
    }

} // namespace Cryo::Codegen
