#include "AST/Type.hpp"
#include "Lexer/lexer.hpp" // For TokenKind enum
#include <sstream>
#include <algorithm>

namespace Cryo
{
    //===----------------------------------------------------------------------===//
    // Base Type Implementation
    //===----------------------------------------------------------------------===//

    bool Type::equals(const Type &other) const
    {
        return _kind == other._kind && _name == other._name;
    }

    bool Type::is_assignable_from(const Type &other) const
    {
        // Same type is always assignable
        if (equals(other))
            return true;

        // Unknown type is assignable from anything (for error recovery)
        if (_kind == TypeKind::Unknown || other._kind == TypeKind::Unknown)
            return true;

        // Auto type needs special handling
        if (_kind == TypeKind::Auto)
            return true;

        // Numeric promotions
        if (is_numeric() && other.is_numeric())
        {
            // Allow promotion from smaller to larger types
            if (is_floating_point() && other.is_integral())
                return true; // int -> float

            if (is_integral() && other.is_integral())
            {
                // Allow promotion between integral types of same signedness
                if (is_signed() == other.is_signed())
                    return size_bytes() >= other.size_bytes();
            }
        }

        // No implicit conversion allowed
        return false;
    }

    bool Type::is_convertible_to(const Type &other) const
    {
        // Default implementation - check assignability
        return other.is_assignable_from(*this);
    }

    std::string Type::to_string() const
    {
        if (_cached_string.empty())
        {
            _cached_string = _name;
        }
        return _cached_string;
    }

    std::string Type::mangle() const
    {
        // Simple name mangling scheme
        std::string result = _name;
        std::replace(result.begin(), result.end(), ' ', '_');
        return result;
    }

    Type *Type::with_qualifiers(const TypeQualifiers &quals) const
    {
        // This is a simplified implementation - in practice, you'd want to
        // create a new type instance with the qualifiers
        // For now, we'll modify in place (not ideal but functional)
        const_cast<Type *>(this)->_qualifiers = quals;
        return const_cast<Type *>(this);
    }

    bool Type::are_compatible(const Type &lhs, const Type &rhs)
    {
        return lhs.is_assignable_from(rhs) || rhs.is_assignable_from(lhs);
    }

    std::shared_ptr<Type> Type::get_common_type(const Type &lhs, const Type &rhs)
    {
        // Simplified implementation - return the "larger" type
        if (lhs.equals(rhs))
        {
            // Return a copy of the same type
            // This is simplified - you'd want proper type construction
            return nullptr; // TODO: Implement proper type copying
        }

        // For numeric types, promote to larger type
        if (lhs.is_numeric() && rhs.is_numeric())
        {
            // Simplified numeric promotion
            if (lhs.is_floating_point() || rhs.is_floating_point())
            {
                // Promote to float
                return nullptr; // TODO: Return float type
            }
            // Both integral - return the larger one
            return nullptr; // TODO: Implement size comparison
        }

        return nullptr; // No common type
    }

    //===----------------------------------------------------------------------===//
    // Integer Type Implementation
    //===----------------------------------------------------------------------===//

    size_t IntegerType::size_bytes() const
    {
        switch (_int_kind)
        {
        case IntegerKind::I8:
        case IntegerKind::U8:
            return 1;
        case IntegerKind::I16:
        case IntegerKind::U16:
            return 2;
        case IntegerKind::I32:
        case IntegerKind::U32:
        case IntegerKind::Int: // Assuming 32-bit default
        case IntegerKind::UInt:
            return 4;
        case IntegerKind::I64:
        case IntegerKind::U64:
            return 8;
        case IntegerKind::I128:
        case IntegerKind::U128:
            return 16;
        default:
            return 4; // Default
        }
    }

    size_t IntegerType::alignment() const
    {
        return size_bytes(); // Usually alignment == size for integers
    }

    std::string IntegerType::get_integer_name(IntegerKind kind, bool is_signed)
    {
        switch (kind)
        {
        case IntegerKind::I8:
            return "i8";
        case IntegerKind::I16:
            return "i16";
        case IntegerKind::I32:
            return "i32";
        case IntegerKind::I64:
            return "i64";
        case IntegerKind::I128:
            return "i128";
        case IntegerKind::U8:
            return "u8";
        case IntegerKind::U16:
            return "u16";
        case IntegerKind::U32:
            return "u32";
        case IntegerKind::U64:
            return "u64";
        case IntegerKind::U128:
            return "u128";
        case IntegerKind::Int:
            return is_signed ? "int" : "uint";
        case IntegerKind::UInt:
            return "uint";
        default:
            return "int";
        }
    }

    //===----------------------------------------------------------------------===//
    // Float Type Implementation
    //===----------------------------------------------------------------------===//

    size_t FloatType::size_bytes() const
    {
        switch (_float_kind)
        {
        case FloatKind::F32:
        case FloatKind::Float: // Assuming f32 default
            return 4;
        case FloatKind::F64:
            return 8;
        default:
            return 4;
        }
    }

    size_t FloatType::alignment() const
    {
        return size_bytes();
    }

    std::string FloatType::get_float_name(FloatKind kind)
    {
        switch (kind)
        {
        case FloatKind::F32:
            return "f32";
        case FloatKind::F64:
            return "f64";
        case FloatKind::Float:
            return "float";
        default:
            return "float";
        }
    }

    //===----------------------------------------------------------------------===//
    // Array Type Implementation
    //===----------------------------------------------------------------------===//

    size_t ArrayType::size_bytes() const
    {
        if (is_dynamic())
        {
            return sizeof(void *); // Pointer to heap data
        }
        return _element_type->size_bytes() * _size.value_or(0);
    }

    std::string ArrayType::to_string() const
    {
        std::ostringstream oss;
        oss << _element_type->to_string() << "[";
        if (_size.has_value())
        {
            oss << _size.value();
        }
        oss << "]";
        return oss.str();
    }

    bool ArrayType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Array)
            return false;

        const auto &other_array = static_cast<const ArrayType &>(other);
        return _element_type->equals(*other_array._element_type) &&
               _size == other_array._size;
    }

    //===----------------------------------------------------------------------===//
    // Pointer Type Implementation
    //===----------------------------------------------------------------------===//

    std::string PointerType::to_string() const
    {
        return _pointee_type->to_string() + "*";
    }

    bool PointerType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Pointer)
            return false;

        const auto &other_ptr = static_cast<const PointerType &>(other);
        return _pointee_type->equals(*other_ptr._pointee_type);
    }

    //===----------------------------------------------------------------------===//
    // Reference Type Implementation
    //===----------------------------------------------------------------------===//

    std::string ReferenceType::to_string() const
    {
        return "&" + _referent_type->to_string();
    }

    bool ReferenceType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Reference)
            return false;

        const auto &other_ref = static_cast<const ReferenceType &>(other);
        return _referent_type->equals(*other_ref._referent_type);
    }

    bool ReferenceType::is_assignable_from(const Type &other) const
    {
        // Reference can be assigned from a pointer to the same type
        if (other.kind() == TypeKind::Pointer)
        {
            const auto &other_ptr = static_cast<const PointerType &>(other);
            return _referent_type->equals(*other_ptr.pointee_type());
        }

        // Reference can be assigned from another reference to the same type
        if (other.kind() == TypeKind::Reference)
        {
            const auto &other_ref = static_cast<const ReferenceType &>(other);
            return _referent_type->equals(*other_ref._referent_type);
        }

        // Default behavior
        return Type::is_assignable_from(other);
    }

    //===----------------------------------------------------------------------===//
    // Function Type Implementation
    //===----------------------------------------------------------------------===//

    std::string FunctionType::build_function_name()
    {
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < _parameter_types.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << _parameter_types[i]->to_string();
        }
        if (_is_variadic)
        {
            if (!_parameter_types.empty())
                oss << ", ";
            oss << "...";
        }
        oss << ") -> " << _return_type->to_string();
        return oss.str();
    }

    bool FunctionType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Function)
            return false;

        const auto &other_fn = static_cast<const FunctionType &>(other);

        if (!_return_type->equals(*other_fn._return_type) ||
            _parameter_types.size() != other_fn._parameter_types.size() ||
            _is_variadic != other_fn._is_variadic)
        {
            return false;
        }

        for (size_t i = 0; i < _parameter_types.size(); ++i)
        {
            if (!_parameter_types[i]->equals(*other_fn._parameter_types[i]))
            {
                return false;
            }
        }

        return true;
    }

    //===----------------------------------------------------------------------===//
    // Optional Type Implementation
    //===----------------------------------------------------------------------===//

    size_t OptionalType::size_bytes() const
    {
        // Optional types typically need extra space for the "has value" flag
        return _wrapped_type->size_bytes() + 1; // +1 byte for flag
    }

    bool OptionalType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Optional)
            return false;

        const auto &other_opt = static_cast<const OptionalType &>(other);
        return _wrapped_type->equals(*other_opt._wrapped_type);
    }

    //===----------------------------------------------------------------------===//
    // TypeContext Implementation
    //===----------------------------------------------------------------------===//

    TypeContext::TypeContext()
    {
        // Initialize built-in types
        _void_type = std::make_unique<VoidType>();
        _boolean_type = std::make_unique<BooleanType>();
        _char_type = std::make_unique<CharType>();
        _string_type = std::make_unique<StringType>();
        _auto_type = std::make_unique<AutoType>();
        _unknown_type = std::make_unique<UnknownType>();
        _null_type = std::make_unique<NullType>();
    }

    Type *TypeContext::get_integer_type(IntegerKind kind, bool is_signed)
    {
        // Create a hash key for the integer type
        int key = static_cast<int>(kind) * 2 + (is_signed ? 0 : 1);

        auto it = _integer_types.find(key);
        if (it != _integer_types.end())
        {
            return it->second.get();
        }

        // Create new integer type
        auto int_type = std::make_unique<IntegerType>(kind, is_signed);
        Type *result = int_type.get();
        _integer_types[key] = std::move(int_type);

        return result;
    }

    Type *TypeContext::get_float_type(FloatKind kind)
    {
        int key = static_cast<int>(kind);

        auto it = _float_types.find(key);
        if (it != _float_types.end())
        {
            return it->second.get();
        }

        // Create new float type
        auto float_type = std::make_unique<FloatType>(kind);
        Type *result = float_type.get();
        _float_types[key] = std::move(float_type);

        return result;
    }

    Type *TypeContext::create_array_type(Type *element_type, std::optional<size_t> size)
    {
        // In practice, you'd want to cache these too
        auto array_type = std::make_unique<ArrayType>(
            std::shared_ptr<Type>(element_type, [](Type *) {}), // Non-owning shared_ptr
            size);

        Type *result = array_type.get();
        _complex_types.push_back(std::move(array_type));

        return result;
    }

    Type *TypeContext::create_pointer_type(Type *pointee_type)
    {
        auto pointer_type = std::make_unique<PointerType>(
            std::shared_ptr<Type>(pointee_type, [](Type *) {}));

        Type *result = pointer_type.get();
        _complex_types.push_back(std::move(pointer_type));

        return result;
    }

    Type *TypeContext::create_reference_type(Type *referent_type)
    {
        auto reference_type = std::make_unique<ReferenceType>(
            std::shared_ptr<Type>(referent_type, [](Type *) {}));

        Type *result = reference_type.get();
        _complex_types.push_back(std::move(reference_type));

        return result;
    }

    Type *TypeContext::create_optional_type(Type *wrapped_type)
    {
        auto optional_type = std::make_unique<OptionalType>(
            std::shared_ptr<Type>(wrapped_type, [](Type *) {}));

        Type *result = optional_type.get();
        _complex_types.push_back(std::move(optional_type));

        return result;
    }

    Type *TypeContext::create_function_type(Type *return_type, std::vector<Type *> param_types, bool is_variadic)
    {
        std::vector<std::shared_ptr<Type>> shared_params;
        for (Type *param : param_types)
        {
            shared_params.push_back(std::shared_ptr<Type>(param, [](Type *) {}));
        }

        auto function_type = std::make_unique<FunctionType>(
            std::shared_ptr<Type>(return_type, [](Type *) {}),
            shared_params,
            is_variadic);

        Type *result = function_type.get();
        _complex_types.push_back(std::move(function_type));

        return result;
    }

    std::string TypeContext::normalize_generic_type_string(const std::string &type_str)
    {
        std::string result = type_str;
        
        // Remove spaces after commas in generic type arguments
        size_t pos = 0;
        while ((pos = result.find(", ", pos)) != std::string::npos)
        {
            result.erase(pos + 1, 1); // Remove the space after comma
            pos += 1; // Move past the comma
        }
        
        return result;
    }

    Type *TypeContext::parse_type_from_string(const std::string &type_str)
    {
        // Normalize the type string first to handle spacing inconsistencies
        std::string normalized_type_str = normalize_generic_type_string(type_str);
        
        // Basic type string parsing
        if (normalized_type_str == "void")
            return get_void_type();
        if (normalized_type_str == "boolean")
            return get_boolean_type();
        if (normalized_type_str == "char")
            return get_char_type();
        if (normalized_type_str == "string")
            return get_string_type();
        if (normalized_type_str == "auto")
            return get_auto_type();
        if (normalized_type_str == "null")
            return get_null_type();

        // Integer types
        if (normalized_type_str == "i8")
            return get_i8_type();
        if (normalized_type_str == "i16")
            return get_i16_type();
        if (normalized_type_str == "i32")
            return get_i32_type();
        if (normalized_type_str == "i64")
            return get_i64_type();
        if (normalized_type_str == "int")
            return get_int_type();

        // Float types
        if (normalized_type_str == "f32")
            return get_f32_type();
        if (normalized_type_str == "f64")
            return get_f64_type();
        if (normalized_type_str == "float")
            return get_default_float_type();

        // Array types (basic parsing for "type[]")
        if (normalized_type_str.length() > 2 && normalized_type_str.substr(normalized_type_str.length() - 2) == "[]")
        {
            std::string element_type_str = normalized_type_str.substr(0, normalized_type_str.length() - 2);
            Type *element_type = parse_type_from_string(element_type_str);
            if (element_type)
            {
                return create_array_type(element_type);
            }
        }

        // Pointer types (basic parsing for "type*")
        if (normalized_type_str.length() > 1 && normalized_type_str.back() == '*')
        {
            std::string pointee_type_str = normalized_type_str.substr(0, normalized_type_str.length() - 1);
            Type *pointee_type = parse_type_from_string(pointee_type_str);
            if (pointee_type)
            {
                return create_pointer_type(pointee_type);
            }
        }

        // Reference types (basic parsing for "&type")
        if (normalized_type_str.length() > 1 && normalized_type_str.front() == '&')
        {
            std::string referent_type_str = normalized_type_str.substr(1);
            Type *referent_type = parse_type_from_string(referent_type_str);
            if (referent_type)
            {
                return create_reference_type(referent_type);
            }
        }

        // Check for user-defined struct types (including generic instantiation)
        auto struct_it = _struct_types.find(normalized_type_str);
        if (struct_it != _struct_types.end())
        {
            return struct_it->second.get();
        }
        
        // Check for generic instantiation syntax (e.g., "SimpleGeneric<int>")
        size_t angle_pos = normalized_type_str.find('<');
        if (angle_pos != std::string::npos && normalized_type_str.back() == '>')
        {
            // This looks like a generic instantiation - create struct type for it
            return get_struct_type(normalized_type_str);
        }

        // Check for user-defined class types
        auto class_it = _class_types.find(normalized_type_str);
        if (class_it != _class_types.end())
        {
            return class_it->second.get();
        }

        // Check for generic type parameters
        auto generic_it = _generic_types.find(normalized_type_str);
        if (generic_it != _generic_types.end())
        {
            return generic_it->second.get();
        }

        return get_unknown_type();
    }

    Type *TypeContext::resolve_type_from_token_kind(int token_kind)
    {
        // Map your TokenKind enum values to types
        switch (static_cast<TokenKind>(token_kind))
        {
        case TokenKind::TK_KW_VOID:
            return get_void_type();
        case TokenKind::TK_KW_BOOLEAN:
            return get_boolean_type();
        case TokenKind::TK_KW_CHAR:
            return get_char_type();
        case TokenKind::TK_KW_STRING:
            return get_string_type();
        case TokenKind::TK_KW_I8:
            return get_i8_type();
        case TokenKind::TK_KW_I16:
            return get_i16_type();
        case TokenKind::TK_KW_I32:
            return get_i32_type();
        case TokenKind::TK_KW_I64:
            return get_i64_type();
        case TokenKind::TK_KW_INT:
            return get_int_type();
        case TokenKind::TK_KW_F32:
            return get_f32_type();
        case TokenKind::TK_KW_F64:
            return get_f64_type();
        case TokenKind::TK_KW_FLOAT:
            return get_default_float_type();
        case TokenKind::TK_KW_AUTO:
            return get_auto_type();
        case TokenKind::TK_KW_NULL:
            return get_null_type();
        default:
            return get_unknown_type();
        }
    }

    bool TypeContext::are_types_compatible(Type *lhs, Type *rhs)
    {
        if (!lhs || !rhs)
            return false;
        return Type::are_compatible(*lhs, *rhs);
    }

    Type *TypeContext::get_common_type(Type *lhs, Type *rhs)
    {
        if (!lhs || !rhs)
            return nullptr;

        // Simplified implementation
        if (lhs->equals(*rhs))
            return lhs;

        // Handle numeric promotions
        if (lhs->is_numeric() && rhs->is_numeric())
        {
            // Float promotion
            if (lhs->is_floating_point() || rhs->is_floating_point())
            {
                if (lhs->is_floating_point())
                    return lhs;
                return rhs;
            }

            // Integer promotion - return the larger type
            if (lhs->size_bytes() >= rhs->size_bytes())
                return lhs;
            return rhs;
        }

        return nullptr;
    }

    Type *TypeContext::get_struct_type(const std::string &name)
    {
        auto it = _struct_types.find(name);
        if (it != _struct_types.end())
        {
            return it->second.get();
        }

        // Create new struct type
        auto struct_type = std::make_unique<StructType>(name);
        Type *result = struct_type.get();
        _struct_types[name] = std::move(struct_type);

        return result;
    }

    Type *TypeContext::get_class_type(const std::string &name)
    {
        auto it = _class_types.find(name);
        if (it != _class_types.end())
        {
            return it->second.get();
        }

        // Create new class type
        auto class_type = std::make_unique<ClassType>(name);
        Type *result = class_type.get();
        _class_types[name] = std::move(class_type);

        return result;
    }

    Type *TypeContext::get_enum_type(const std::string &name, std::vector<std::string> variants, bool is_simple)
    {
        auto it = _enum_types.find(name);
        if (it != _enum_types.end())
        {
            return it->second.get();
        }

        // Create new enum type
        auto enum_type = std::make_unique<EnumType>(name, std::move(variants), is_simple);
        Type *result = enum_type.get();
        _enum_types[name] = std::move(enum_type);

        return result;
    }

    Type *TypeContext::get_generic_type(const std::string &name)
    {
        // Check if we already have this generic type
        auto it = _generic_types.find(name);
        if (it != _generic_types.end()) {
            return it->second.get();
        }
        
        // Create new generic type
        auto generic_type = std::make_unique<GenericType>(name);
        Type *type_ptr = generic_type.get();
        _generic_types[name] = std::move(generic_type);
        return type_ptr;
    }
}