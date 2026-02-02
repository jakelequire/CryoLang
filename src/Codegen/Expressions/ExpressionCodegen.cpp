#include "Codegen/Expressions/ExpressionCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Statements/ControlFlowCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Lexer/lexer.hpp"
#include "AST/ASTNode.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/CompoundTypes.hpp"
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
                        // Try signed first, fall back to unsigned for large hex values
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

        case TokenKind::TK_KW_VOID:
        {
            // Unit literal () - return the unit type value (empty struct)
            // This is distinct from void which means "no return value"
            // The unit type can be passed as a value, stored, and used as a type argument
            llvm::StructType *unit_ty = types().unit_type();
            return llvm::ConstantStruct::get(unit_ty, {});
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

        // Handle built-in pseudo-macros FILE and LINE
        if (name == "FILE")
        {
            // Return the current source file as a string constant
            std::string filename = module()->getSourceFileName();
            if (filename.empty())
            {
                filename = "<unknown>";
            }
            return builder().CreateGlobalStringPtr(filename, "FILE.str");
        }
        else if (name == "LINE")
        {
            // Return the current line number
            // Get line number from the node's location if available
            int line = 0;
            if (node->location().line() > 0)
            {
                line = node->location().line();
            }
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), line);
        }

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
            llvm::Value *loaded_value = create_load(alloca, loaded_type, name + ".load");

            // Auto-dereference 'this' for primitive implement blocks.
            // When a method has '&this' parameter on a primitive type (i8, i32, etc.),
            // 'this' is stored as a pointer to the primitive. We need to load through
            // that pointer to get the actual primitive value for use in expressions.
            if (name == "this" && loaded_value && loaded_value->getType()->isPointerTy())
            {
                // Check if the pointee type is a primitive (integer or float)
                // We use variable_types_map to get the semantic type information
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find("this");
                if (it != var_types.end() && it->second)
                {
                    TypeRef this_type = it->second;
                    // Check if it's a pointer/reference to a primitive
                    if (this_type->kind() == Cryo::TypeKind::Pointer ||
                        this_type->kind() == Cryo::TypeKind::Reference)
                    {
                        TypeRef pointee_type;
                        if (auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(this_type.get()))
                        {
                            pointee_type = ptr_type->pointee();
                        }
                        else if (auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(this_type.get()))
                        {
                            pointee_type = ref_type->referent();
                        }

                        // If pointee is a primitive type, auto-dereference
                        if (pointee_type.is_valid() && pointee_type->is_primitive())
                        {
                            llvm::Type *pointee_llvm_type = get_llvm_type(pointee_type);
                            if (pointee_llvm_type)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "ExpressionCodegen: Auto-dereferencing 'this' for primitive type {}",
                                          pointee_type->display_name());
                                loaded_value = create_load(loaded_value, pointee_llvm_type, "this.deref");
                            }
                        }
                    }
                }
            }

            return loaded_value;
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

        // Get a pointer to the struct object
        // For identifiers, we need special handling:
        // - If the identifier holds a pointer (like 'this'), load to get the pointer value
        // - If the identifier holds a by-value struct, use the alloca address directly
        llvm::Value *object = nullptr;

        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->object()))
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
                              "generate_member_address: Loaded pointer from alloca for '{}'", var_name);
                }
                else if (alloca_type->isStructTy())
                {
                    // The alloca stores a by-value struct - use alloca address directly
                    object = alloca;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_member_address: Using alloca address for by-value struct '{}'", var_name);
                }
                else
                {
                    // Other types - load the value
                    object = create_load(alloca, alloca_type, var_name + ".load");
                }
            }
            else
            {
                // Fallback to generate for globals etc.
                object = generate(node->object());
            }
        }
        else if (auto *nested_member = dynamic_cast<Cryo::MemberAccessNode *>(node->object()))
        {
            // For nested member access (obj.a.b), get the address of the inner member
            object = generate_member_address(nested_member);
        }
        else
        {
            // For other expressions, generate the value and hope it's a pointer
            object = generate(node->object());
        }

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

        // Get type name and check for generic context redirection
        std::string type_name = node->type_name();
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
                              "generate_sizeof: Redirecting type {} -> {} in generic context",
                              type_name, redirected);
                    type_name = redirected;
                }
                else
                {
                    // Try substituting type parameters in the annotation (e.g., "HashSetEntry<T>" -> "HashSetEntry_string")
                    redirected = generics->substitute_type_annotation(type_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_sizeof: Substituted type annotation {} -> {} in generic context",
                                  type_name, redirected);
                        type_name = redirected;
                    }
                }
            }
        }

        // TODO: Get proper Cryo::Type* from node instead of string lookup
        llvm::Type *llvm_type = types().get_type(type_name);
        if (!llvm_type)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Unknown type for sizeof: " + type_name);
            return nullptr;
        }

        // Check if type is sized before computing size
        if (!llvm_type->isSized())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "ExpressionCodegen: sizeof called on unsized type '{}', returning 0",
                     type_name);
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

                    llvm::Value *matches = _control_flow->generate_pattern_match(match_value, pattern.get());
                    if (matches)
                    {
                        if (combined_match)
                        {
                            // OR with previous matches
                            combined_match = builder().CreateOr(combined_match, matches, "matchexpr.or");
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

            // Generate arm body and get result value
            builder().SetInsertPoint(arm_block);
            values().enter_scope("matchexpr.arm." + std::to_string(i));

            // Bind pattern variables before generating body (use first pattern for bindings)
            Cryo::PatternNode *first_pattern = arm->pattern();
            if (first_pattern)
            {
                _control_flow->bind_enum_pattern_variables(match_value, first_pattern);
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
        ctx().set_result(nullptr);
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
                        // Safety check: ensure struct is not opaque (body is set)
                        // This can happen during nested instantiation when struct A's fields
                        // trigger instantiation of struct B, and B's methods reference A
                        if (out_struct_type->isOpaque())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: Struct '{}' is opaque (being instantiated), skipping",
                                      current_type);
                            out_struct_type = nullptr;
                            // Continue to fallback instead of returning
                        }
                        else
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
                        // Safety check: ensure struct is not opaque
                        if (out_struct_type->isOpaque())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: Struct '{}' is opaque via variable_types_map, skipping",
                                      this_type_name);
                            out_struct_type = nullptr;
                        }
                        else
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
            // Fallback: If object is a MemberAccessNode, recursively resolve its type
            else if (auto *member_access = dynamic_cast<Cryo::MemberAccessNode *>(object))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_member_info: Object is MemberAccessNode, resolving nested member '{}'",
                          member_access->member());

                // Recursively resolve the outer object's type
                llvm::StructType *outer_struct = nullptr;
                unsigned outer_field_idx = 0;
                if (resolve_member_info(member_access->object(), member_access->member(), outer_struct, outer_field_idx))
                {
                    // Get the outer object type to look up field types in TemplateRegistry
                    TypeRef outer_obj_type = member_access->object()->get_resolved_type();
                    std::string outer_type_name;

                    // Try to get the outer type name
                    if (outer_obj_type)
                    {
                        outer_type_name = outer_obj_type->display_name();
                        if (outer_obj_type->kind() == Cryo::TypeKind::Pointer)
                        {
                            auto *ptr = dynamic_cast<const Cryo::PointerType *>(outer_obj_type.get());
                            if (ptr && ptr->pointee())
                            {
                                outer_type_name = ptr->pointee()->display_name();
                            }
                        }
                        else if (!outer_type_name.empty() && outer_type_name.back() == '*')
                        {
                            outer_type_name.pop_back();
                        }
                    }
                    else if (auto *id = dynamic_cast<Cryo::IdentifierNode *>(member_access->object()))
                    {
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find(id->name());
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            outer_type_name = it->second->display_name();
                            if (it->second->kind() == Cryo::TypeKind::Pointer)
                            {
                                auto *ptr = dynamic_cast<const Cryo::PointerType *>(it->second.get());
                                if (ptr && ptr->pointee())
                                {
                                    outer_type_name = ptr->pointee()->display_name();
                                }
                            }
                            else if (!outer_type_name.empty() && outer_type_name.back() == '*')
                            {
                                outer_type_name.pop_back();
                            }
                        }
                    }

                    // Look up the field type from TemplateRegistry
                    if (!outer_type_name.empty())
                    {
                        if (auto *template_reg = ctx().template_registry())
                        {
                            // Try various name candidates
                            std::vector<std::string> candidates = {outer_type_name};
                            if (!ctx().namespace_context().empty())
                            {
                                candidates.push_back(ctx().namespace_context() + "::" + outer_type_name);
                            }

                            for (const auto &candidate : candidates)
                            {
                                const auto *field_info = template_reg->get_struct_field_types(candidate);
                                if (field_info && outer_field_idx < field_info->field_types.size())
                                {
                                    obj_type = field_info->field_types[outer_field_idx];
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_member_info: Resolved nested member '{}' type to '{}' via TemplateRegistry",
                                              member_access->member(), obj_type ? obj_type->display_name() : "<null>");
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            // Fallback: If object is an ArrayAccessNode (e.g., entries[i].state), resolve the element type
            else if (auto *array_access = dynamic_cast<Cryo::ArrayAccessNode *>(object))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_member_info: Object is ArrayAccessNode, resolving array element type");

                // Get the type of the array expression (e.g., entries in entries[i])
                TypeRef array_type = array_access->array()->get_resolved_type();
                if (!array_type)
                {
                    // Try to get from variable_types_map if array is an identifier
                    if (auto *arr_id = dynamic_cast<Cryo::IdentifierNode *>(array_access->array()))
                    {
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find(arr_id->name());
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            array_type = it->second;
                        }
                    }
                    // Try to resolve if array is a member access (e.g., this.entries)
                    else if (auto *arr_member = dynamic_cast<Cryo::MemberAccessNode *>(array_access->array()))
                    {
                        llvm::StructType *arr_struct = nullptr;
                        unsigned arr_field_idx = 0;
                        if (resolve_member_info(arr_member->object(), arr_member->member(), arr_struct, arr_field_idx))
                        {
                            // Get the field type from TemplateRegistry
                            TypeRef arr_obj_type = arr_member->object()->get_resolved_type();
                            std::string arr_type_name;

                            if (arr_obj_type)
                            {
                                arr_type_name = arr_obj_type->display_name();
                                if (arr_obj_type->kind() == Cryo::TypeKind::Reference)
                                {
                                    auto *ref = dynamic_cast<const Cryo::ReferenceType *>(arr_obj_type.get());
                                    if (ref && ref->referent())
                                    {
                                        arr_type_name = ref->referent()->display_name();
                                    }
                                }
                                else if (arr_obj_type->kind() == Cryo::TypeKind::Pointer)
                                {
                                    auto *ptr = dynamic_cast<const Cryo::PointerType *>(arr_obj_type.get());
                                    if (ptr && ptr->pointee())
                                    {
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
                                    {
                                        candidates.push_back(ctx().namespace_context() + "::" + arr_type_name);
                                    }

                                    for (const auto &candidate : candidates)
                                    {
                                        const auto *field_info = template_reg->get_struct_field_types(candidate);
                                        if (field_info && arr_field_idx < field_info->field_types.size())
                                        {
                                            array_type = field_info->field_types[arr_field_idx];
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "resolve_member_info: Resolved array field '{}' type to '{}'",
                                                      arr_member->member(), array_type ? array_type->display_name() : "<null>");
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Now extract the element type from the array/pointer type
                if (array_type)
                {
                    if (array_type->kind() == Cryo::TypeKind::Pointer)
                    {
                        auto *ptr = dynamic_cast<const Cryo::PointerType *>(array_type.get());
                        if (ptr && ptr->pointee())
                        {
                            obj_type = ptr->pointee();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: ArrayAccessNode - extracted pointee type: {}",
                                      obj_type->display_name());
                        }
                    }
                    else if (array_type->kind() == Cryo::TypeKind::Array)
                    {
                        auto *arr = dynamic_cast<const Cryo::ArrayType *>(array_type.get());
                        if (arr && arr->element())
                        {
                            obj_type = arr->element();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_member_info: ArrayAccessNode - extracted array element type: {}",
                                      obj_type->display_name());
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

        // Check if we're inside a generic instantiation context and need to redirect
        // the type name. For example, if we're generating code for HashSet<string> methods
        // and the AST says "HashSet", we need to look for "HashSet_string".
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
                              "resolve_member_info: Redirecting type {} -> {} in generic context",
                              type_name, redirected);
                    type_name = redirected;
                }
                else
                {
                    // Try substituting type parameters in the annotation (e.g., "HashSetEntry<T>" -> "HashSetEntry_string")
                    redirected = generics->substitute_type_annotation(type_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_member_info: Substituted type annotation {} -> {} in generic context",
                                  type_name, redirected);
                        type_name = redirected;
                    }
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_member_info: Looking up type '{}' for member '{}'",
                  type_name, member_name);

        // Convert generic type names like "Array<Token>" to mangled form "Array_Token"
        // This is needed because LLVM struct types are stored with mangled names
        std::string mangled_type_name = type_name;
        if (type_name.find('<') != std::string::npos)
        {
            // Parse generic type: "Array<Token>" -> base="Array", args=["Token"]
            size_t angle_pos = type_name.find('<');
            std::string base_name = type_name.substr(0, angle_pos);
            std::string args_str = type_name.substr(angle_pos + 1);
            if (!args_str.empty() && args_str.back() == '>')
            {
                args_str.pop_back();
            }

            // Build mangled name: "Array_Token"
            mangled_type_name = base_name + "_";
            // Replace problematic characters in type arguments
            std::replace(args_str.begin(), args_str.end(), '<', '_');
            std::replace(args_str.begin(), args_str.end(), '>', '_');
            std::replace(args_str.begin(), args_str.end(), ',', '_');
            std::replace(args_str.begin(), args_str.end(), ' ', '_');
            std::replace(args_str.begin(), args_str.end(), '*', 'p');
            mangled_type_name += args_str;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Converted generic type '{}' to mangled name '{}'",
                      type_name, mangled_type_name);
        }

        // Look up struct type in LLVM context (try mangled name first, then original)
        out_struct_type = llvm::StructType::getTypeByName(llvm_ctx(), mangled_type_name);
        if (!out_struct_type && mangled_type_name != type_name)
        {
            // Try original type name
            out_struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
        }
        if (!out_struct_type)
        {
            // Try context registry with both names
            if (llvm::Type *type = ctx().get_type(mangled_type_name))
            {
                out_struct_type = llvm::dyn_cast<llvm::StructType>(type);
            }
            else if (mangled_type_name != type_name)
            {
                if (llvm::Type *type = ctx().get_type(type_name))
                {
                    out_struct_type = llvm::dyn_cast<llvm::StructType>(type);
                }
            }
        }

        if (!out_struct_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Struct type '{}' (mangled: '{}') not found",
                      type_name, mangled_type_name);
            return false;
        }

        // Safety check: ensure struct is not opaque (body is set)
        if (out_struct_type->isOpaque())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Struct type '{}' is opaque (being instantiated), cannot access members",
                      type_name);
            return false;
        }

        // Look up field index using the new CodegenContext API
        // Try mangled name first, then original
        int field_idx = ctx().get_struct_field_index(mangled_type_name, member_name);
        if (field_idx < 0 && mangled_type_name != type_name)
        {
            field_idx = ctx().get_struct_field_index(type_name, member_name);
        }
        if (field_idx < 0)
        {
            // Fallback: try TemplateRegistry for cross-module struct field lookup
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_member_info: Field '{}' not in local registry for '{}', trying TemplateRegistry",
                      member_name, mangled_type_name);

            if (auto *template_reg = ctx().template_registry())
            {
                // Try various name candidates (qualified and unqualified)
                std::vector<std::string> candidates = {mangled_type_name};
                if (mangled_type_name != type_name)
                {
                    candidates.push_back(type_name);
                }

                // Add LLVM struct type name as a candidate
                if (out_struct_type && out_struct_type->hasName())
                {
                    std::string llvm_name = out_struct_type->getName().str();
                    if (llvm_name != mangled_type_name && llvm_name != type_name)
                    {
                        candidates.push_back(llvm_name);
                    }
                }

                // Add current namespace prefix
                if (!ctx().namespace_context().empty())
                {
                    candidates.push_back(ctx().namespace_context() + "::" + mangled_type_name);
                    if (mangled_type_name != type_name)
                    {
                        candidates.push_back(ctx().namespace_context() + "::" + type_name);
                    }
                }

                // Try type_namespace_map for the registered namespace
                std::string type_ns = ctx().get_type_namespace(type_name);
                if (!type_ns.empty())
                {
                    candidates.push_back(type_ns + "::" + type_name);
                }

                // Also try unqualified name if type_name looks qualified
                size_t last_colon = type_name.rfind("::");
                if (last_colon != std::string::npos)
                {
                    std::string simple_name = type_name.substr(last_colon + 2);
                    candidates.push_back(simple_name);
                }

                // Try base template name (e.g., "HashSetEntry" from "HashSetEntry_string")
                size_t underscore_pos = type_name.find('_');
                if (underscore_pos != std::string::npos)
                {
                    std::string base_name = type_name.substr(0, underscore_pos);
                    candidates.push_back(base_name);
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

        // Check for [value; count] repeat syntax
        const bool is_repeat = node->is_repeat_syntax();
        const bool is_dynamic = node->has_dynamic_count();
        const size_t array_size = node->size(); // Uses repeat_count if repeat syntax (0 for dynamic)

        if (is_repeat)
        {
            if (is_dynamic)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Array literal uses dynamic repeat syntax [value; expr]");
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Array literal uses repeat syntax [value; {}]", array_size);
            }
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
        if (is_dynamic)
        {
            // Dynamic [value; count_expr] syntax - allocate variable-length array
            llvm::Value *count_val = generate(node->repeat_count_expr());
            if (!count_val)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array repeat count expression");
                return nullptr;
            }

            // Convert count to i64 for alloca
            llvm::Value *count_i64 = builder().CreateZExtOrTrunc(count_val,
                                                                  llvm::Type::getInt64Ty(llvm_ctx()),
                                                                  "array.count");

            // Allocate variable-length array on stack
            llvm::AllocaInst *array_alloca = builder().CreateAlloca(elem_type, count_i64, "array.vla");

            // Generate loop to fill the array
            llvm::Value *elem_val = cast_if_needed(first_elem, elem_type);

            // Create loop blocks
            llvm::Function *func = builder().GetInsertBlock()->getParent();
            llvm::BasicBlock *loop_header = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.header", func);
            llvm::BasicBlock *loop_body = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.body", func);
            llvm::BasicBlock *loop_exit = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.exit", func);

            // Branch to loop header
            builder().CreateBr(loop_header);

            // Loop header: i = phi(0, i+1)
            builder().SetInsertPoint(loop_header);
            llvm::PHINode *index_phi = builder().CreatePHI(llvm::Type::getInt64Ty(llvm_ctx()), 2, "array.idx");
            index_phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0),
                                   array_alloca->getParent());

            // Check if i < count
            llvm::Value *cond = builder().CreateICmpULT(index_phi, count_i64, "array.fill.cond");
            builder().CreateCondBr(cond, loop_body, loop_exit);

            // Loop body: store element
            builder().SetInsertPoint(loop_body);
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(elem_type, array_alloca, index_phi, "elem.ptr");
            builder().CreateStore(elem_val, elem_ptr);

            // Increment index
            llvm::Value *next_idx = builder().CreateAdd(index_phi,
                                                         llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 1),
                                                         "array.idx.next");
            index_phi->addIncoming(next_idx, loop_body);
            builder().CreateBr(loop_header);

            // Loop exit
            builder().SetInsertPoint(loop_exit);

            return array_alloca;
        }

        llvm::ArrayType *array_type = llvm::ArrayType::get(elem_type, array_size);

        // Allocate array on stack
        llvm::AllocaInst *array_alloca = create_entry_alloca(array_type, "array.literal");

        // Store elements
        if (is_repeat)
        {
            // [value; count] syntax - store the same value into all slots
            llvm::Value *elem_val = cast_if_needed(first_elem, elem_type);
            for (size_t i = 0; i < array_size; ++i)
            {
                llvm::Value *indices[] = {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), i)};
                llvm::Value *elem_ptr = builder().CreateInBoundsGEP(array_type, array_alloca, indices,
                                                                    "elem." + std::to_string(i) + ".ptr");
                builder().CreateStore(elem_val, elem_ptr);
            }
        }
        else
        {
            // Normal [a, b, c] syntax - store each element
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

        // Check for [value; count] repeat syntax
        const bool is_repeat = node->is_repeat_syntax();
        const bool is_dynamic = node->has_dynamic_count();
        const size_t array_size = node->size(); // Uses repeat_count if repeat syntax (0 for dynamic)

        llvm::Value *elements_array = nullptr;
        llvm::Value *length_val = nullptr;

        if (is_dynamic)
        {
            // Dynamic [value; count_expr] syntax
            llvm::Value *count_val = generate(node->repeat_count_expr());
            if (!count_val)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array repeat count expression");
                return nullptr;
            }

            // Convert count to i64
            length_val = builder().CreateZExtOrTrunc(count_val,
                                                      llvm::Type::getInt64Ty(llvm_ctx()),
                                                      "array.count");

            // Allocate variable-length array on stack
            llvm::AllocaInst *vla = builder().CreateAlloca(elem_type, length_val, "array.elements.vla");
            elements_array = vla;

            // Generate the fill value
            llvm::Value *elem_val = generate(elements[0].get());
            if (!elem_val)
            {
                report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                             "Failed to generate array repeat element");
                return nullptr;
            }
            elem_val = cast_if_needed(elem_val, elem_type);

            // Generate loop to fill the array
            llvm::Function *func = builder().GetInsertBlock()->getParent();
            llvm::BasicBlock *loop_header = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.header", func);
            llvm::BasicBlock *loop_body = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.body", func);
            llvm::BasicBlock *loop_exit = llvm::BasicBlock::Create(llvm_ctx(), "array.fill.exit", func);

            llvm::BasicBlock *entry_block = builder().GetInsertBlock();
            builder().CreateBr(loop_header);

            // Loop header
            builder().SetInsertPoint(loop_header);
            llvm::PHINode *index_phi = builder().CreatePHI(llvm::Type::getInt64Ty(llvm_ctx()), 2, "array.idx");
            index_phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 0), entry_block);

            llvm::Value *cond = builder().CreateICmpULT(index_phi, length_val, "array.fill.cond");
            builder().CreateCondBr(cond, loop_body, loop_exit);

            // Loop body
            builder().SetInsertPoint(loop_body);
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(elem_type, vla, index_phi, "elem.ptr");
            builder().CreateStore(elem_val, elem_ptr);

            llvm::Value *next_idx = builder().CreateAdd(index_phi,
                                                         llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 1),
                                                         "array.idx.next");
            index_phi->addIncoming(next_idx, loop_body);
            builder().CreateBr(loop_header);

            // Loop exit
            builder().SetInsertPoint(loop_exit);
        }
        else
        {
            // Static size - use fixed-size array type
            llvm::ArrayType *raw_array_type = llvm::ArrayType::get(elem_type, array_size);
            llvm::AllocaInst *static_array = create_entry_alloca(raw_array_type, "array.elements");
            elements_array = static_array;
            length_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), array_size);

            // Store elements in the raw array
            if (is_repeat)
            {
                // [value; count] syntax - generate value once and store into all slots
                llvm::Value *elem_val = generate(elements[0].get());
                if (!elem_val)
                {
                    report_error(ErrorCode::E0621_ARRAY_OPERATION_ERROR, node,
                                 "Failed to generate array repeat element");
                    return nullptr;
                }
                elem_val = cast_if_needed(elem_val, elem_type);

                for (size_t i = 0; i < array_size; ++i)
                {
                    llvm::Value *indices[] = {
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), i)};
                    llvm::Value *elem_ptr = builder().CreateInBoundsGEP(raw_array_type, static_array, indices,
                                                                        "elem." + std::to_string(i) + ".ptr");
                    builder().CreateStore(elem_val, elem_ptr);
                }
            }
            else
            {
                // Normal [a, b, c] syntax - store each element
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
                    llvm::Value *elem_ptr = builder().CreateInBoundsGEP(raw_array_type, static_array, indices,
                                                                        "elem." + std::to_string(i) + ".ptr");
                    builder().CreateStore(elem_val, elem_ptr);
                }
            }
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
        // For dynamic arrays, elements_array is already a pointer to elem_type
        // For static arrays, we need to GEP to get pointer to first element
        llvm::Value *array_data_ptr = elements_array;
        if (!is_dynamic)
        {
            llvm::Value *array_data_indices[] = {
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0),
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), 0)};
            llvm::ArrayType *raw_array_type = llvm::ArrayType::get(elem_type, array_size);
            array_data_ptr = builder().CreateInBoundsGEP(raw_array_type, elements_array, array_data_indices, "array.data.ptr");
        }

        // Set elements field (field 0)
        if (llvm::StructType *struct_type = llvm::dyn_cast<llvm::StructType>(array_struct_type))
        {
            llvm::Value *elements_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 0, "elements.ptr");
            builder().CreateStore(array_data_ptr, elements_field_ptr);

            // Set length field (field 1)
            llvm::Value *length_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 1, "length.ptr");
            builder().CreateStore(length_val, length_field_ptr);

            // Set capacity field (field 2) - same as length for array literals
            llvm::Value *capacity_field_ptr = builder().CreateStructGEP(struct_type, array_instance, 2, "capacity.ptr");
            builder().CreateStore(length_val, capacity_field_ptr);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Created Array<T> instance with {} elements", array_size);

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

    llvm::Value *ExpressionCodegen::generate_tuple_literal(Cryo::TupleLiteralNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null tuple literal node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating tuple literal with {} elements",
                  node->size());

        const auto &elements = node->elements();
        if (elements.empty())
        {
            // Empty tuple () is the unit type - return unit value (empty struct)
            llvm::StructType *unit_ty = types().unit_type();
            return llvm::ConstantStruct::get(unit_ty, {});
        }

        // Generate each element and collect their types
        std::vector<llvm::Value *> element_values;
        std::vector<llvm::Type *> element_types;

        for (size_t i = 0; i < elements.size(); ++i)
        {
            llvm::Value *elem_val = generate(elements[i].get());
            if (!elem_val)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                             "Failed to generate tuple element at index " + std::to_string(i));
                return nullptr;
            }
            element_values.push_back(elem_val);
            element_types.push_back(elem_val->getType());
        }

        // Create an anonymous struct type for this tuple
        llvm::StructType *tuple_type = llvm::StructType::get(llvm_ctx(), element_types, true);

        // Allocate the tuple on the stack
        llvm::AllocaInst *tuple_alloca = create_entry_alloca(tuple_type, "tuple.literal");

        // Store each element
        for (size_t i = 0; i < element_values.size(); ++i)
        {
            llvm::Value *elem_ptr = builder().CreateStructGEP(tuple_type, tuple_alloca, i,
                                                              "tuple.elem." + std::to_string(i) + ".ptr");
            builder().CreateStore(element_values[i], elem_ptr);
        }

        // Load and return the tuple value
        return create_load(tuple_alloca, tuple_type, "tuple.value");
    }

    llvm::Value *ExpressionCodegen::generate_lambda(Cryo::LambdaExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, "Null lambda expression node");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Generating lambda expression");

        // Build parameter types
        std::vector<llvm::Type *> param_types;
        for (const auto &[name, param_type] : node->parameters())
        {
            if (param_type)
            {
                llvm::Type *llvm_param_type = get_llvm_type(param_type);
                if (llvm_param_type)
                {
                    param_types.push_back(llvm_param_type);
                }
                else
                {
                    // Default to pointer type
                    param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
                }
            }
            else
            {
                // No type - default to pointer
                param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
            }
        }

        // Build return type
        llvm::Type *return_type = llvm::Type::getVoidTy(llvm_ctx());
        TypeRef ret_type = node->return_type();
        if (ret_type)
        {
            llvm::Type *llvm_ret_type = get_llvm_type(ret_type);
            if (llvm_ret_type)
            {
                return_type = llvm_ret_type;
            }
        }

        // Create function type
        llvm::FunctionType *func_type = llvm::FunctionType::get(return_type, param_types, false);

        // Generate unique name for the lambda
        static int lambda_counter = 0;
        std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);

        // Create the lambda function
        llvm::Function *lambda_func = llvm::Function::Create(
            func_type,
            llvm::Function::InternalLinkage,
            lambda_name,
            module());

        // Save current insertion point
        llvm::BasicBlock *saved_block = builder().GetInsertBlock();
        llvm::BasicBlock::iterator saved_point = builder().GetInsertPoint();

        // Create entry block for lambda
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(llvm_ctx(), "entry", lambda_func);
        builder().SetInsertPoint(entry_block);

        // Create allocas for parameters and bind them
        size_t param_idx = 0;
        for (auto &arg : lambda_func->args())
        {
            if (param_idx < node->parameters().size())
            {
                const auto &[name, param_type] = node->parameters()[param_idx];
                arg.setName(name);

                // Create alloca for parameter
                llvm::AllocaInst *param_alloca = create_entry_alloca(lambda_func, arg.getType(), name);
                builder().CreateStore(&arg, param_alloca);

                // Register in value context
                values().set_value(name, param_alloca, param_alloca, arg.getType());
            }
            param_idx++;
        }

        // Generate lambda body
        if (node->body())
        {
            Cryo::Codegen::CodegenVisitor *visitor = ctx().visitor();
            node->body()->accept(*visitor);
        }

        // Add terminator if needed
        if (!builder().GetInsertBlock()->getTerminator())
        {
            if (return_type->isVoidTy())
            {
                builder().CreateRetVoid();
            }
            else
            {
                // Return default value
                builder().CreateRet(llvm::Constant::getNullValue(return_type));
            }
        }

        // Restore insertion point
        if (saved_block)
        {
            builder().SetInsertPoint(saved_block, saved_point);
        }

        // Return pointer to the lambda function
        return lambda_func;
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

        // First, check if the node has a resolved type we can use directly
        // This is especially important for struct literals inside generic methods where
        // the literal type (e.g., "ArraySplit") is different from the containing generic type (e.g., "Array")
        // The resolved type may be an InstantiatedType like "ArraySplit<T>" that we can substitute
        TypeRef resolved_type = node->get_resolved_type();
        if (resolved_type.is_valid())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_struct_literal: Node has resolved type: {} (kind={})",
                      resolved_type->display_name(), static_cast<int>(resolved_type->kind()));

            // If the resolved type is an InstantiatedType, try to get the mangled name
            if (resolved_type->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst_type = static_cast<const Cryo::InstantiatedType *>(resolved_type.get());

                // First check if the InstantiatedType has an already-resolved concrete type
                // This is set during monomorphization (e.g., CheckedResult<i32> -> CheckedResult_i32)
                if (inst_type->has_resolved_type())
                {
                    TypeRef concrete_type = inst_type->resolved_type();
                    if (concrete_type.is_valid())
                    {
                        type_name = concrete_type->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_struct_literal: Using InstantiatedType's resolved concrete type: {}",
                                  type_name);
                    }
                }
                else
                {
                    // No concrete resolved type yet - try to substitute type parameters if in generic scope
                    auto *visitor = ctx().visitor();
                    if (visitor)
                    {
                        auto *generics = visitor->get_generics();
                        if (generics && generics->in_type_param_scope())
                        {
                            // The resolved type's display_name includes type params (e.g., "ArraySplit<T>")
                            // Use substitute_type_annotation to get the mangled name
                            std::string resolved_display = resolved_type->display_name();
                            std::string substituted = generics->substitute_type_annotation(resolved_display);
                            if (!substituted.empty())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_struct_literal: Using resolved type substitution {} -> {}",
                                          resolved_display, substituted);
                                type_name = substituted;
                            }
                        }
                    }
                }
            }
            else if (resolved_type->kind() == Cryo::TypeKind::Struct)
            {
                // Resolved to a concrete struct type, use its name directly
                type_name = resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_struct_literal: Using concrete resolved type name: {}",
                          type_name);
            }
        }

        // Check if we're inside a generic instantiation context and need to redirect
        // the type name. For example, if we're generating code for HashSet<string> methods
        // and the AST says "HashSet", we need to look for "HashSet_string".
        // Also handle types with type parameters like "HashSetEntry<T>" -> "HashSetEntry_string".
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
                              "generate_struct_literal: Redirecting type {} -> {} in generic context",
                              type_name, redirected);
                    type_name = redirected;
                }
                else if (type_name.find('_') == std::string::npos)
                {
                    // Only try substitution if we haven't already mangled the name
                    // (mangled names contain underscores like "ArraySplit_string")
                    // Try substituting type parameters in the annotation (e.g., "HashSetEntry<T>" -> "HashSetEntry_string")
                    redirected = generics->substitute_type_annotation(type_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_struct_literal: Substituted type annotation {} -> {} in generic context",
                                  type_name, redirected);
                        type_name = redirected;
                    }
                }
            }
        }

        // Look up struct type
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Looking up type '{}'", type_name);
        llvm::Type *struct_type = ctx().get_type(type_name);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: ctx().get_type returned {}",
                  struct_type ? "non-null" : "null");

        if (!struct_type || !struct_type->isStructTy())
        {
            report_error(ErrorCode::E0622_MEMBER_ACCESS_ERROR, node,
                         "Unknown struct type: " + type_name);
            return nullptr;
        }

        auto *st = llvm::cast<llvm::StructType>(struct_type);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Struct '{}' has {} elements, isOpaque={}",
                  type_name, st->getNumElements(), st->isOpaque());

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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Checking constructor status");
        bool is_constructor = false;
        llvm::BasicBlock *insert_block = builder().GetInsertBlock();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Insert block is {}",
                  insert_block ? "non-null" : "null");
        llvm::Function *current_fn = insert_block ? insert_block->getParent() : nullptr;
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Current function is {}",
                  current_fn ? "non-null" : "null");

        if (current_fn)
        {
            std::string fn_name = current_fn->getName().str();
            // Simple heuristic: if function returns a pointer and the name contains the struct type
            // it's likely a constructor
            llvm::Type *ret_type = current_fn->getReturnType();
            if (ret_type)
            {
                is_constructor = ret_type->isPointerTy() &&
                               fn_name.find(type_name) != std::string::npos;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                         "Function '{}' constructor check: {} (return type: {}, contains '{}': {})",
                         fn_name, is_constructor ? "yes" : "no",
                         ret_type->isPointerTy() ? "pointer" : "non-pointer",
                         type_name, fn_name.find(type_name) != std::string::npos ? "yes" : "no");
            }
        }

        llvm::Value *struct_storage = nullptr;

        if (is_constructor)
        {
            // For constructors: allocate on heap using malloc
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Constructor path - calling getDataLayout");
            const llvm::DataLayout &DL = module()->getDataLayout();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Calling getTypeAllocSize");
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
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: Non-constructor path - calling create_entry_alloca");
            struct_storage = create_entry_alloca(struct_type, type_name + ".literal");
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "generate_struct_literal: create_entry_alloca returned {}",
                      struct_storage ? "non-null" : "null");
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
                report_error(ErrorCode::E0638_INVALID_STRUCT_INITIALIZATION, node,
                            "Null field initializer at index " + std::to_string(i) + " in struct '" + type_name + "'");
                continue;
            }

            std::string field_name = initializer->field_name();

            // Look up the actual field index by name - this handles fields in any order
            int field_idx = ctx().get_struct_field_index(type_name, field_name);
            if (field_idx < 0)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Field '{}' not found in struct '{}', skipping", field_name, type_name);
                report_error(ErrorCode::E0638_INVALID_STRUCT_INITIALIZATION, node,
                            "Field '" + field_name + "' not found in struct '" + type_name + "'");
                continue;
            }

            // Verify field index is valid
            if (static_cast<unsigned>(field_idx) >= st->getNumElements())
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Field index {} out of bounds for struct '{}' with {} elements",
                         field_idx, type_name, st->getNumElements());
                report_error(ErrorCode::E0638_INVALID_STRUCT_INITIALIZATION, node,
                            "Field index " + std::to_string(field_idx) + " out of bounds for struct '" +
                            type_name + "' with " + std::to_string(st->getNumElements()) + " elements");
                continue;
            }

            llvm::Value *field_val = generate(initializer->value());
            if (!field_val)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to generate struct field {}",
                         field_name);
                report_error(ErrorCode::E0638_INVALID_STRUCT_INITIALIZATION, node,
                            "Failed to generate value for field '" + field_name + "' in struct '" + type_name + "'");
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

        // Handle array allocation: new Type[size]
        if (node->is_array_allocation())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "ExpressionCodegen: Array allocation for type {}",
                      node->type_name());

            // Get the element type
            llvm::Type *element_type = ctx().get_type(node->type_name());
            if (!element_type)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                             "Unknown type for array allocation: " + node->type_name());
                return nullptr;
            }

            // Generate the size expression
            llvm::Value *count = generate(node->array_size());
            if (!count)
            {
                report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                             "Failed to generate array size expression");
                return nullptr;
            }

            // Ensure count is i64
            if (count->getType() != llvm::Type::getInt64Ty(llvm_ctx()))
            {
                count = builder().CreateIntCast(count, llvm::Type::getInt64Ty(llvm_ctx()), false, "array.size.i64");
            }

            // Calculate element size
            const llvm::DataLayout &dl = module()->getDataLayout();
            uint64_t element_size = element_type->isSized() ? dl.getTypeAllocSize(element_type) : 1;
            llvm::Value *elem_size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), element_size);

            // Calculate total allocation size
            llvm::Value *total_size = builder().CreateMul(count, elem_size_val, "array.total.size");

            // Call cryo_alloc
            llvm::Function *cryo_alloc_fn = module()->getFunction("std::Runtime::cryo_alloc");
            if (!cryo_alloc_fn)
            {
                llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
                llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                llvm::FunctionType *cryo_alloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
                cryo_alloc_fn = llvm::Function::Create(cryo_alloc_type, llvm::Function::ExternalLinkage,
                                                       "std::Runtime::cryo_alloc", module());
            }

            llvm::Value *ptr = builder().CreateCall(cryo_alloc_fn, {total_size}, "new.array." + node->type_name());
            return ptr;
        }

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
                llvm::FunctionType *fn_type = ctor_fn->getFunctionType();

                // Check if this is a static factory method (returns struct type, not void)
                // vs a regular constructor (returns void, first param is pointer for 'this')
                bool is_static_factory = !fn_type->getReturnType()->isVoidTy() &&
                                         fn_type->getReturnType()->isStructTy();

                if (is_static_factory)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_new: '{}' is a static factory method, calling without 'this'",
                              ctor_fn->getName().str());

                    // Static factory method - call without 'this' pointer
                    std::vector<llvm::Value *> factory_args;
                    for (const auto &arg : node->arguments())
                    {
                        llvm::Value *arg_val = generate(arg.get());
                        if (arg_val)
                            factory_args.push_back(arg_val);
                    }

                    // Coerce arguments to match function signature
                    std::vector<llvm::Value *> coerced_args;
                    for (size_t i = 0; i < factory_args.size() && i < fn_type->getNumParams(); ++i)
                    {
                        llvm::Value *arg = factory_args[i];
                        llvm::Type *param_type = fn_type->getParamType(i);
                        if (arg->getType() != param_type)
                        {
                            if (arg->getType()->isIntegerTy() && param_type->isIntegerTy())
                            {
                                arg = builder().CreateIntCast(arg, param_type, true, "arg.cast");
                            }
                        }
                        coerced_args.push_back(arg);
                    }

                    // Call the factory and store result in allocated memory
                    llvm::Value *result = builder().CreateCall(ctor_fn, coerced_args, "factory.result");
                    builder().CreateStore(result, ptr);
                }
                else
                {
                    // Regular constructor - call with 'this' pointer
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

        // Check if the node has a resolved type from the GenericExpressionResolutionPass
        // This handles generic enum variants like Option::None when the concrete type is known
        if (node->has_resolved_type())
        {
            TypeRef resolved_type = node->get_resolved_type();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: ScopeResolution has resolved type: {}",
                      resolved_type->display_name());

            // Try to generate the enum variant using the resolved type information
            llvm::Value *result = generate_enum_variant_from_resolved_type(
                node, resolved_type, node->scope_name(), node->member_name());
            if (result)
            {
                return result;
            }
            // Fall through to normal lookup if resolved type didn't help
        }

        // Check if we're inside a generic instantiation context and need to redirect
        // the scope name. For example, if we're generating code for Option<void*> methods
        // and the AST says "Option::None", we need to look for "Option_voidp::None".
        std::string effective_scope_name = node->scope_name();
        auto *visitor = ctx().visitor();
        if (visitor)
        {
            auto *generics = visitor->get_generics();
            if (generics && generics->in_type_param_scope())
            {
                std::string redirected = generics->get_instantiated_scope_name(effective_scope_name);
                if (!redirected.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "ExpressionCodegen: Redirecting scope {} -> {} in generic context",
                              effective_scope_name, redirected);
                    effective_scope_name = redirected;
                }
                else
                {
                    // Cross-type generic enum resolution: when referencing a generic enum OTHER
                    // than the one currently being instantiated (e.g., Option::None inside a
                    // Result<T,E> method body). We resolve the template's type params using the
                    // current scope's bindings, compute the mangled name, and ensure the enum
                    // is instantiated so its variants are registered.
                    auto *template_registry = ctx().template_registry();
                    if (template_registry)
                    {
                        const auto *tmpl_info = template_registry->find_template(effective_scope_name);
                        if (tmpl_info && tmpl_info->enum_template)
                        {
                            const auto &generic_params = tmpl_info->enum_template->generic_parameters();
                            std::vector<TypeRef> resolved_args;
                            bool all_resolved = true;

                            for (const auto &param : generic_params)
                            {
                                TypeRef resolved = generics->resolve_type_param(param->name());
                                if (resolved.is_valid())
                                {
                                    resolved_args.push_back(resolved);
                                }
                                else
                                {
                                    all_resolved = false;
                                    break;
                                }
                            }

                            if (all_resolved && !resolved_args.empty())
                            {
                                // Build display name and mangle it (e.g., "Option<void*>" -> "Option_voidp")
                                std::string generic_display = effective_scope_name + "<";
                                for (size_t i = 0; i < resolved_args.size(); ++i)
                                {
                                    if (i > 0)
                                        generic_display += ", ";
                                    generic_display += resolved_args[i]->display_name();
                                }
                                generic_display += ">";
                                std::string mangled = mangle_generic_type_name(generic_display);

                                // Ensure the enum is instantiated (creates LLVM type + registers variants)
                                generics->instantiate_enum(effective_scope_name, resolved_args);

                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "ExpressionCodegen: Cross-type generic resolution {} -> {} in generic context",
                                          effective_scope_name, mangled);
                                effective_scope_name = mangled;
                            }
                        }
                    }
                }
            }
        }

        // Build fully qualified name
        std::string qualified_name = effective_scope_name + "::" + node->member_name();

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
            // Build candidate with effective scope (may be redirected in generic context)
            std::string scoped_candidate = effective_scope_name + "::" + candidate;
            if (llvm::Value *result = try_find(scoped_candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Found '{}' as '{}'", qualified_name, scoped_candidate);
                return result;
            }
        }

        // Try generating candidates for just the enum name (effective_scope_name) and append the variant
        // This handles cross-module enum lookups where Ordering is from std::sync::atomic
        auto scope_candidates = generate_lookup_candidates(effective_scope_name, Cryo::SymbolKind::Type);
        for (const auto &scope_candidate : scope_candidates)
        {
            // Build full qualified enum variant name: namespace::EnumName::Variant
            std::string full_variant_name = scope_candidate + "::" + node->member_name();
            if (llvm::Value *result = try_find(full_variant_name))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Found '{}' via scope lookup as '{}'", qualified_name, full_variant_name);
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

        // Report error for unresolved enum variants (no placeholders - fail early and clearly)
        report_error(ErrorCode::E0607_VARIABLE_GENERATION_ERROR, node,
                     "Cannot resolve enum variant: " + qualified_name +
                     " (generic enum variants must be instantiated before use)");
        return nullptr;
    }

    llvm::Value *ExpressionCodegen::generate_enum_variant_from_resolved_type(
        Cryo::ScopeResolutionNode *node,
        TypeRef resolved_type,
        const std::string &scope_name,
        const std::string &member_name)
    {
        if (!resolved_type.is_valid())
            return nullptr;

        // Get the display name of the resolved type for variant lookup
        std::string type_name = resolved_type->display_name();

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "ExpressionCodegen: Trying to resolve {}::{} with type '{}'",
                  scope_name, member_name, type_name);

        // For InstantiatedType, try to get the resolved (monomorphized) type
        if (resolved_type->kind() == TypeKind::InstantiatedType)
        {
            auto *inst_type = static_cast<const InstantiatedType *>(resolved_type.get());
            if (inst_type->has_resolved_type())
            {
                resolved_type = inst_type->resolved_type();
                type_name = resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Using monomorphized type '{}'", type_name);
            }
        }

        // Check if the resolved type is an enum
        if (resolved_type->kind() != TypeKind::Enum)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Resolved type '{}' is not an enum (kind={})",
                      type_name, static_cast<int>(resolved_type->kind()));
            return nullptr;
        }

        auto *enum_type = static_cast<const EnumType *>(resolved_type.get());

        // Verify the variant exists
        auto variant_idx = enum_type->variant_index(member_name);
        if (!variant_idx.has_value())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Variant '{}' not found in enum '{}'",
                      member_name, type_name);
            return nullptr;
        }

        // Build the fully qualified variant name for lookup
        // The enum variant should be registered as "TypeName::VariantName"
        std::string qualified_variant = type_name + "::" + member_name;

        // Pre-compute the LLVM type for this enum so we can wrap raw discriminants
        // when the enum is a tagged union struct (e.g., Option<T> = { i32, [N x i8] })
        llvm::Type *llvm_enum_type_for_wrap = types().map(resolved_type);
        llvm::StructType *enum_struct_type = nullptr;
        if (llvm_enum_type_for_wrap && llvm_enum_type_for_wrap->isStructTy())
            enum_struct_type = llvm::cast<llvm::StructType>(llvm_enum_type_for_wrap);

        // Try to find the variant in the enum_variants_map
        auto &enum_variants = ctx().enum_variants_map();
        auto it = enum_variants.find(qualified_variant);
        if (it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Found enum variant '{}'", qualified_variant);
            if (enum_struct_type && it->second->getType()->isIntegerTy())
                return wrap_discriminant_in_tagged_union(it->second, enum_struct_type, member_name);
            return it->second;
        }

        // Also try without any template parameters in the name
        // e.g., "Option<Duration>::None" might be stored as "Option::None"
        std::string base_variant = scope_name + "::" + member_name;
        it = enum_variants.find(base_variant);
        if (it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Found enum variant via base name '{}'", base_variant);
            if (enum_struct_type && it->second->getType()->isIntegerTy())
                return wrap_discriminant_in_tagged_union(it->second, enum_struct_type, member_name);
            return it->second;
        }

        // Try looking up in various namespace forms
        auto candidates = generate_lookup_candidates(qualified_variant, Cryo::SymbolKind::Type);
        for (const auto &candidate : candidates)
        {
            it = enum_variants.find(candidate);
            if (it != enum_variants.end())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Found enum variant via candidate '{}'", candidate);
                if (enum_struct_type && it->second->getType()->isIntegerTy())
                    return wrap_discriminant_in_tagged_union(it->second, enum_struct_type, member_name);
                return it->second;
            }
        }

        // If the variant is a simple tag (no payload), we can generate the discriminant value directly
        const auto &variant = enum_type->variants()[*variant_idx];
        if (!variant.has_payload())
        {
            // Simple variant - just return the discriminant value
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "ExpressionCodegen: Generating discriminant {} for simple variant '{}'",
                      *variant_idx, member_name);

            // Get or create the LLVM type for this enum
            llvm::Type *llvm_enum_type = types().map(resolved_type);
            if (!llvm_enum_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "ExpressionCodegen: Failed to map enum type '{}'", type_name);
                return nullptr;
            }

            // For simple enums, return the discriminant value
            // The discriminant is typically an integer (i32)
            llvm::Value *discriminant = llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(llvm_ctx()), *variant_idx);

            // If the enum type is more complex (struct with discriminant + payload),
            // we need to create a struct with the discriminant and zeroed payload
            if (llvm_enum_type->isStructTy())
            {
                auto *struct_type = llvm::cast<llvm::StructType>(llvm_enum_type);

                // Create alloca for the struct
                llvm::Value *alloca = builder().CreateAlloca(struct_type, nullptr, member_name + ".tmp");

                // Store the discriminant (first field)
                llvm::Value *disc_gep = builder().CreateStructGEP(struct_type, alloca, 0, "disc.ptr");
                builder().CreateStore(discriminant, disc_gep);

                // Zero-initialize the payload area if it exists
                if (struct_type->getNumElements() > 1)
                {
                    llvm::Value *payload_gep = builder().CreateStructGEP(struct_type, alloca, 1, "payload.ptr");
                    llvm::Type *payload_type = struct_type->getElementType(1);
                    llvm::Constant *zero_payload = llvm::Constant::getNullValue(payload_type);
                    builder().CreateStore(zero_payload, payload_gep);
                }

                // Load and return the struct
                return builder().CreateLoad(struct_type, alloca, member_name + ".val");
            }

            // For simple integer-based enums, just return the discriminant
            return discriminant;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "ExpressionCodegen: Cannot resolve variant '{}' with payload - needs constructor call",
                  member_name);
        return nullptr;
    }

    llvm::Value *ExpressionCodegen::wrap_discriminant_in_tagged_union(
        llvm::Value *discriminant, llvm::StructType *struct_type, const std::string &name)
    {
        if (!discriminant || !struct_type)
            return discriminant;

        // Create alloca for the struct
        llvm::Value *alloca = builder().CreateAlloca(struct_type, nullptr, name + ".tmp");

        // Store the discriminant (first field)
        llvm::Value *disc_gep = builder().CreateStructGEP(struct_type, alloca, 0, "disc.ptr");

        // Cast discriminant to the struct's first element type if needed
        llvm::Type *disc_field_type = struct_type->getElementType(0);
        llvm::Value *cast_disc = discriminant;
        if (discriminant->getType() != disc_field_type)
        {
            if (discriminant->getType()->isIntegerTy() && disc_field_type->isIntegerTy())
                cast_disc = builder().CreateIntCast(discriminant, disc_field_type, true, "disc.cast");
            else
                return discriminant; // Can't cast — return original
        }
        builder().CreateStore(cast_disc, disc_gep);

        // Zero-initialize the payload area if it exists
        if (struct_type->getNumElements() > 1)
        {
            llvm::Value *payload_gep = builder().CreateStructGEP(struct_type, alloca, 1, "payload.ptr");
            llvm::Type *payload_type = struct_type->getElementType(1);
            llvm::Constant *zero_payload = llvm::Constant::getNullValue(payload_type);
            builder().CreateStore(zero_payload, payload_gep);
        }

        // Load and return the struct
        return builder().CreateLoad(struct_type, alloca, name + ".val");
    }

} // namespace Cryo::Codegen
