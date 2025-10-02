#include "AST/Type.hpp"
#include "Lexer/lexer.hpp" // For TokenKind enum
#include <sstream>
#include <algorithm>
#include <iostream>

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

        // Nullable types can accept null
        if (other._kind == TypeKind::Null && is_nullable())
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

    Type::ConversionSafety Type::get_conversion_safety(const Type &target) const
    {
        // Base implementation - most conversions are impossible unless overridden
        if (equals(target))
            return ConversionSafety::Safe;

        // Check if assignable (for basic type promotion)
        if (target.is_assignable_from(*this))
            return ConversionSafety::Safe;

        return ConversionSafety::Impossible;
    }

    bool Type::allows_implicit_conversion_to(const Type &target) const
    {
        return get_conversion_safety(target) == ConversionSafety::Safe;
    }

    bool Type::allows_explicit_conversion_to(const Type &target) const
    {
        ConversionSafety safety = get_conversion_safety(target);
        return safety == ConversionSafety::Safe ||
               safety == ConversionSafety::Warning ||
               safety == ConversionSafety::Unsafe;
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

    Type::ConversionSafety IntegerType::get_conversion_safety(const Type &target) const
    {
        // First check base class behavior
        if (equals(target))
            return ConversionSafety::Safe;

        // Only handle integer-to-integer conversions here
        if (target.kind() != TypeKind::Integer)
        {
            // Integer to float is generally safe (may lose precision but no overflow)
            if (target.is_floating_point())
                return ConversionSafety::Safe;

            // Use base class for other type conversions
            return Type::get_conversion_safety(target);
        }

        const IntegerType &target_int = static_cast<const IntegerType &>(target);

        // Same type is always safe
        if (_int_kind == target_int._int_kind && _is_signed == target_int._is_signed)
            return ConversionSafety::Safe;

        size_t source_size = size_bytes();
        size_t target_size = target_int.size_bytes();
        bool source_signed = _is_signed;
        bool target_signed = target_int._is_signed;

        // Safe widening conversions (same signedness, larger size)
        if (source_signed == target_signed && target_size > source_size)
            return ConversionSafety::Safe;

        // Narrowing conversions (potential data loss)
        if (target_size < source_size)
            return ConversionSafety::Warning;

        // Sign conversions (same size, different signedness)
        if (source_signed != target_signed && target_size == source_size)
            return ConversionSafety::Warning;

        // Mixed sign and size changes are more dangerous
        if (source_signed != target_signed)
            return ConversionSafety::Unsafe;

        // Default to requiring explicit conversion
        return ConversionSafety::Warning;
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
        _variadic_type = std::make_unique<VariadicType>();

        // Register built-in parameterized enums
        register_builtin_parameterized_enums();
    }

    void TypeContext::register_builtin_parameterized_enums()
    {
        // Register Option<T> enum
        {
            auto option_layout = std::make_unique<EnumLayout>();
            option_layout->pattern = EnumLayout::LayoutPattern::OptionalType;
            option_layout->uses_tag = true;
            option_layout->tag_size = 1; // bool
            option_layout->alignment = sizeof(void *);

            register_parameterized_enum("Option", {"T"}, std::move(option_layout));
        }

        // Register Result<T,E> enum
        {
            auto result_layout = std::make_unique<EnumLayout>();
            result_layout->pattern = EnumLayout::LayoutPattern::ResultType;
            result_layout->uses_tag = true;
            result_layout->tag_size = 1; // bool
            result_layout->alignment = sizeof(void *);

            register_parameterized_enum("Result", {"T", "E"}, std::move(result_layout));
        }
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

    Type *TypeContext::create_type_alias(const std::string &alias_name, Type *target_type)
    {
        auto alias_type = std::make_unique<TypeAlias>(alias_name, target_type);
        Type *result = alias_type.get();
        _complex_types.push_back(std::move(alias_type));

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
            pos += 1;                 // Move past the comma
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
        if (normalized_type_str == "...")
            return get_variadic_type();

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
        if (normalized_type_str == "double")
            return get_f64_type(); // double is alias for f64

        // Unsigned integer types
        if (normalized_type_str == "u8" || normalized_type_str == "uint8" || normalized_type_str == "unsigned i8")
            return get_u8_type();
        if (normalized_type_str == "u16" || normalized_type_str == "uint16" || normalized_type_str == "unsigned i16")
            return get_u16_type();
        if (normalized_type_str == "u32" || normalized_type_str == "uint32" || normalized_type_str == "unsigned i32")
            return get_u32_type();
        if (normalized_type_str == "u64" || normalized_type_str == "uint64" || normalized_type_str == "unsigned i64")
            return get_u64_type();

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

        // Check for generic instantiation syntax FIRST (before struct types)
        size_t angle_pos = normalized_type_str.find('<');
        if (angle_pos != std::string::npos && normalized_type_str.back() == '>')
        {
            std::string base_name = normalized_type_str.substr(0, angle_pos);
            std::string params_str = normalized_type_str.substr(angle_pos + 1, normalized_type_str.length() - angle_pos - 2);

            // Handle known generic type aliases
            if (base_name == "ptr")
            {
                // ptr<T> = T* - create a pointer type to T
                Type *pointee_type = parse_type_from_string(params_str);
                if (pointee_type)
                {
                    return create_pointer_type(pointee_type);
                }
            }
            else if (base_name == "const_ptr")
            {
                // const_ptr<T> = const T* - for now, treat same as ptr<T>
                Type *pointee_type = parse_type_from_string(params_str);
                if (pointee_type)
                {
                    return create_pointer_type(pointee_type);
                }
            }

            // This looks like a generic instantiation - create struct type for it
            return get_struct_type(normalized_type_str);
        }

        // Check for user-defined struct types (including generic instantiation)
        auto struct_it = _struct_types.find(normalized_type_str);
        if (struct_it != _struct_types.end())
        {
            return struct_it->second.get();
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

    Type *TypeContext::get_trait_type(const std::string &name)
    {
        auto it = _trait_types.find(name);
        if (it != _trait_types.end())
        {
            return it->second.get();
        }

        // Create new trait type
        auto trait_type = std::make_unique<TraitType>(name);
        Type *result = trait_type.get();
        _trait_types[name] = std::move(trait_type);

        return result;
    }

    Type *TypeContext::get_enum_type(const std::string &name, std::vector<std::string> variants, bool is_simple)
    {
        std::cout << "[DEBUG] TypeContext::get_enum_type called with name=" << name
                  << " is_simple=" << is_simple << std::endl;
        std::cout << "[DEBUG] Call stack trace (simplified): TypeContext::get_enum_type" << std::endl;

        auto it = _enum_types.find(name);
        if (it != _enum_types.end())
        {
            std::cout << "[DEBUG] Found existing enum type for " << name
                      << " existing is_simple=" << it->second->is_simple_enum() << std::endl;
            return it->second.get();
        }

        // Create new enum type
        std::cout << "[DEBUG] Creating new EnumType with is_simple=" << is_simple << std::endl;
        auto enum_type = std::make_unique<EnumType>(name, std::move(variants), is_simple);
        std::cout << "[DEBUG] Created EnumType, result is_simple_enum()=" << enum_type->is_simple_enum() << std::endl;
        Type *result = enum_type.get();
        _enum_types[name] = std::move(enum_type);

        return result;
    }

    Type *TypeContext::lookup_enum_type(const std::string &name)
    {
        std::cout << "[DEBUG] TypeContext::lookup_enum_type called with name=" << name << std::endl;

        auto it = _enum_types.find(name);
        if (it != _enum_types.end())
        {
            std::cout << "[DEBUG] Found existing enum type for " << name
                      << " is_simple=" << it->second->is_simple_enum() << std::endl;
            return it->second.get();
        }

        std::cout << "[DEBUG] No existing enum type found for " << name << std::endl;
        return nullptr;
    }

    Type *TypeContext::get_generic_type(const std::string &name)
    {
        // Check if we already have this generic type
        auto it = _generic_types.find(name);
        if (it != _generic_types.end())
        {
            return it->second.get();
        }

        // Create new generic type
        auto generic_type = std::make_unique<GenericType>(name);
        Type *type_ptr = generic_type.get();
        _generic_types[name] = std::move(generic_type);
        return type_ptr;
    }

    //===----------------------------------------------------------------------===//
    // StructType Size Calculation
    //===----------------------------------------------------------------------===//

    size_t StructType::size_bytes() const
    {
        if (_cached_size)
        {
            return *_cached_size;
        }

        // For now, return a placeholder size based on typical struct overhead
        // TODO: This should calculate actual size based on fields when field info is available
        // For structs with unknown field layout, we use a reasonable default
        size_t estimated_size = 8; // Base struct size

        _cached_size = estimated_size;
        return estimated_size;
    }

    //===----------------------------------------------------------------------===//
    // ClassType Size Calculation
    //===----------------------------------------------------------------------===//

    size_t ClassType::size_bytes() const
    {
        if (_cached_size)
        {
            return *_cached_size;
        }

        // Classes are typically stored as pointers/references
        // The actual object size would include vtable pointer + fields
        size_t estimated_size = sizeof(void *) * 2; // vtable + typical field overhead

        _cached_size = estimated_size;
        return estimated_size;
    }

    //===----------------------------------------------------------------------===//
    // ParameterizedType Implementation
    //===----------------------------------------------------------------------===//

    std::string ParameterizedType::get_instantiated_name() const
    {
        if (_cached_instantiated_name.empty())
        {
            if (is_template())
            {
                // For templates like Array<T>, return the template name
                _cached_instantiated_name = _base_name + "<";
                for (size_t i = 0; i < _param_names.size(); ++i)
                {
                    if (i > 0)
                        _cached_instantiated_name += ", ";
                    _cached_instantiated_name += _param_names[i];
                }
                _cached_instantiated_name += ">";
            }
            else if (is_instantiation())
            {
                // For instantiations like Array<int>, return the concrete name
                _cached_instantiated_name = _base_name + "<";
                for (size_t i = 0; i < _type_params.size(); ++i)
                {
                    if (i > 0)
                        _cached_instantiated_name += ", ";
                    _cached_instantiated_name += _type_params[i]->to_string();
                }
                _cached_instantiated_name += ">";
            }
            else
            {
                _cached_instantiated_name = _base_name;
            }
        }
        return _cached_instantiated_name;
    }

    std::shared_ptr<ParameterizedType> ParameterizedType::instantiate(const std::vector<std::shared_ptr<Type>> &concrete_types) const
    {
        if (!is_template())
        {
            // Already instantiated or not a template
            return nullptr;
        }

        if (concrete_types.size() != _param_names.size())
        {
            // Mismatched parameter count
            return nullptr;
        }

        auto instantiation = std::make_shared<ParameterizedType>(_base_name, concrete_types);
        return instantiation;
    }

    bool ParameterizedType::is_assignable_from(const Type &other) const
    {
        // Special case: pointer types (ptr<T>) can accept null
        if (other.kind() == TypeKind::Null && (_base_name == "ptr" || _base_name == "const_ptr"))
        {
            return true;
        }

        if (other.kind() != TypeKind::Parameterized)
            return false;

        const ParameterizedType &other_param = static_cast<const ParameterizedType &>(other);

        // Must have same base name
        if (_base_name != other_param._base_name)
            return false;

        // If both are instantiations, check type parameter compatibility
        if (is_instantiation() && other_param.is_instantiation())
        {
            if (_type_params.size() != other_param._type_params.size())
                return false;

            for (size_t i = 0; i < _type_params.size(); ++i)
            {
                if (!_type_params[i]->is_assignable_from(*other_param._type_params[i]))
                    return false;
            }
            return true;
        }

        // Templates are compatible with any instantiation of the same base
        return true;
    }

    bool ParameterizedType::is_convertible_to(const Type &other) const
    {
        return other.is_assignable_from(*this);
    }

    size_t ParameterizedType::size_bytes() const
    {
        if (is_template())
        {
            // Templates have no concrete size until instantiated
            return 0;
        }

        // For instantiations, calculate size based on the concrete type structure
        // This is a simplified implementation - in practice you'd need to know
        // the actual struct layout for each parameterized type
        if (_base_name == "Array")
        {
            // Array<T> has: T* elements, u64 length, u64 capacity
            return sizeof(void *) + sizeof(uint64_t) + sizeof(uint64_t);
        }
        else if (_base_name == "Option")
        {
            // Option<T> might be implemented as: bool has_value, T value
            if (!_type_params.empty())
            {
                return sizeof(bool) + _type_params[0]->size_bytes();
            }
        }

        // Default fallback
        return sizeof(void *);
    }

    size_t ParameterizedType::alignment() const
    {
        if (is_template())
            return 1;

        // For instantiated types, use the largest alignment of contained types
        size_t max_align = sizeof(void *); // Default pointer alignment

        for (const auto &param : _type_params)
        {
            max_align = std::max(max_align, param->alignment());
        }

        return max_align;
    }

    std::string ParameterizedType::to_string() const
    {
        return get_instantiated_name();
    }

    //===----------------------------------------------------------------------===//
    // TypeRegistry Implementation
    //===----------------------------------------------------------------------===//

    void TypeRegistry::register_template(const std::string &base_name,
                                         const std::vector<std::string> &param_names)
    {
        auto template_type = std::make_shared<ParameterizedType>(base_name, param_names);
        _templates[base_name] = template_type;
    }

    ParameterizedType *TypeRegistry::get_template(const std::string &base_name)
    {
        auto it = _templates.find(base_name);
        return (it != _templates.end()) ? it->second.get() : nullptr;
    }

    ParameterizedType *TypeRegistry::instantiate(const std::string &base_name,
                                                 const std::vector<Type *> &concrete_types)
    {
        // Get the template
        auto template_type = get_template(base_name);
        if (!template_type)
            return nullptr;

        // Create instantiation name
        std::string inst_name = base_name + "<";
        for (size_t i = 0; i < concrete_types.size(); ++i)
        {
            if (i > 0)
                inst_name += ", ";
            inst_name += concrete_types[i]->to_string();
        }
        inst_name += ">";

        // Check if already instantiated
        auto existing = _instantiations.find(inst_name);
        if (existing != _instantiations.end())
        {
            return existing->second.get();
        }

        // Create new instantiation
        std::vector<std::shared_ptr<Type>> shared_types;
        for (auto *type : concrete_types)
        {
            // This is a simplification - in practice you'd need proper shared_ptr management
            shared_types.push_back(std::shared_ptr<Type>(type, [](Type *) {})); // Non-owning shared_ptr
        }

        auto instantiation = template_type->instantiate(shared_types);
        if (instantiation)
        {
            _instantiations[inst_name] = instantiation;
            return instantiation.get();
        }

        return nullptr;
    }

    ParameterizedType *TypeRegistry::parse_and_instantiate(const std::string &type_string)
    {
        auto [base_name, param_strs] = parse_generic_syntax(type_string);

        if (param_strs.empty() || !has_template(base_name))
            return nullptr;

        // Convert parameter strings to types
        std::vector<Type *> concrete_types;
        for (const auto &param_str : param_strs)
        {
            // This is simplified - you'd need proper type parsing here
            Type *param_type = _type_context->parse_type_from_string(param_str);
            if (!param_type)
                return nullptr;
            concrete_types.push_back(param_type);
        }

        return instantiate(base_name, concrete_types);
    }

    ParameterizedType *TypeRegistry::get_instantiation(const std::string &instantiated_name)
    {
        auto it = _instantiations.find(instantiated_name);
        return (it != _instantiations.end()) ? it->second.get() : nullptr;
    }

    bool TypeRegistry::has_template(const std::string &base_name) const
    {
        return _templates.find(base_name) != _templates.end();
    }

    std::pair<std::string, std::vector<std::string>> TypeRegistry::parse_generic_syntax(const std::string &type_string)
    {
        std::string base_name = type_string;
        std::vector<std::string> param_strs;

        size_t angle_pos = type_string.find('<');
        if (angle_pos == std::string::npos)
        {
            return {base_name, param_strs};
        }

        base_name = type_string.substr(0, angle_pos);

        size_t close_angle = type_string.find('>', angle_pos);
        if (close_angle == std::string::npos)
        {
            return {base_name, param_strs}; // Malformed
        }

        std::string params_str = type_string.substr(angle_pos + 1, close_angle - angle_pos - 1);

        // Simple parameter parsing (splits on commas)
        std::stringstream ss(params_str);
        std::string param;
        while (std::getline(ss, param, ','))
        {
            // Trim whitespace
            param.erase(0, param.find_first_not_of(" \t"));
            param.erase(param.find_last_not_of(" \t") + 1);
            if (!param.empty())
            {
                param_strs.push_back(param);
            }
        }

        return {base_name, param_strs};
    }

    //===----------------------------------------------------------------------===//
    // EnumType Enhanced Implementation
    //===----------------------------------------------------------------------===//

    std::unique_ptr<EnumLayout> EnumType::create_instantiated_layout(const std::vector<std::shared_ptr<Type>> &concrete_types) const
    {
        if (!_is_parameterized || !_layout)
        {
            return nullptr;
        }

        auto instantiated_layout = std::make_unique<EnumLayout>(*_layout);

        // Update layout with concrete types
        if (_layout->pattern == EnumLayout::LayoutPattern::OptionalType && concrete_types.size() >= 1)
        {
            // Option<T> layout: { bool has_value, T value }
            instantiated_layout->common_fields.clear();
            instantiated_layout->common_fields.push_back(std::make_shared<BooleanType>());
            instantiated_layout->common_fields.push_back(concrete_types[0]);

            size_t bool_size = 1;
            size_t value_size = concrete_types[0]->size_bytes();
            size_t value_align = concrete_types[0]->alignment();

            instantiated_layout->total_size = bool_size + value_size;
            instantiated_layout->alignment = std::max(bool_size, value_align);
        }
        else if (_layout->pattern == EnumLayout::LayoutPattern::ResultType && concrete_types.size() >= 2)
        {
            // Result<T,E> layout: { bool is_ok, union { T ok_value, E err_value } }
            instantiated_layout->common_fields.clear();
            instantiated_layout->common_fields.push_back(std::make_shared<BooleanType>());

            size_t ok_size = concrete_types[0]->size_bytes();
            size_t err_size = concrete_types[1]->size_bytes();
            size_t union_size = std::max(ok_size, err_size);

            // Use the larger type for the union field
            instantiated_layout->common_fields.push_back(ok_size >= err_size ? concrete_types[0] : concrete_types[1]);

            instantiated_layout->total_size = 1 + union_size; // bool + union
            instantiated_layout->alignment = std::max(concrete_types[0]->alignment(), concrete_types[1]->alignment());
        }

        return instantiated_layout;
    }

    //===----------------------------------------------------------------------===//
    // ParameterizedType Enhanced Implementation
    //===----------------------------------------------------------------------===//

    std::vector<ParameterizedType::FieldLayout> ParameterizedType::get_enum_field_layout() const
    {
        std::vector<FieldLayout> fields;

        if (!is_enum_instantiation() || !_base_enum_type)
        {
            return fields;
        }

        const auto *layout = _base_enum_type->get_layout();
        if (!layout || layout->common_fields.empty())
        {
            return fields;
        }

        size_t current_offset = 0;
        for (size_t i = 0; i < layout->common_fields.size(); ++i)
        {
            FieldLayout field;
            field.type = layout->common_fields[i];
            field.size = field.type->size_bytes();
            field.offset = current_offset;

            if (i == 0 && layout->uses_tag)
            {
                field.name = "discriminant";
            }
            else
            {
                field.name = "field_" + std::to_string(i);
            }

            fields.push_back(field);
            current_offset += field.size;
        }

        return fields;
    }

    bool ParameterizedType::is_optional_like() const
    {
        return is_enum_instantiation() && _base_enum_type && _base_enum_type->is_optional_pattern();
    }

    bool ParameterizedType::is_result_like() const
    {
        return is_enum_instantiation() && _base_enum_type && _base_enum_type->is_result_pattern();
    }

    bool ParameterizedType::has_discriminant() const
    {
        return is_enum_instantiation() && _base_enum_type && _base_enum_type->uses_discriminant_tag();
    }

    size_t ParameterizedType::get_discriminant_size() const
    {
        if (!is_enum_instantiation() || !_base_enum_type)
        {
            return 0;
        }
        return _base_enum_type->get_discriminant_size();
    }

    std::vector<std::shared_ptr<Type>> ParameterizedType::get_data_field_types() const
    {
        if (!is_enum_instantiation() || !_base_enum_type)
        {
            return {};
        }

        const auto *layout = _base_enum_type->get_layout();
        if (!layout || layout->common_fields.empty())
        {
            return {};
        }

        // Skip discriminant field if present
        std::vector<std::shared_ptr<Type>> data_fields;
        size_t start_idx = layout->uses_tag ? 1 : 0;

        for (size_t i = start_idx; i < layout->common_fields.size(); ++i)
        {
            data_fields.push_back(layout->common_fields[i]);
        }

        return data_fields;
    }

    size_t ParameterizedType::get_data_offset() const
    {
        if (!has_discriminant())
        {
            return 0;
        }
        return get_discriminant_size();
    }

    //===----------------------------------------------------------------------===//
    // TypeContext Enhanced Implementation
    //===----------------------------------------------------------------------===//

    void TypeContext::register_parameterized_enum(const std::string &base_name,
                                                  const std::vector<std::string> &type_params,
                                                  std::unique_ptr<EnumLayout> layout_template)
    {
        // Create enum variants based on layout pattern
        std::vector<EnumVariant> variants;

        if (layout_template->pattern == EnumLayout::LayoutPattern::OptionalType)
        {
            variants.emplace_back("None");
            variants.emplace_back("Some", std::vector<std::shared_ptr<Type>>{std::make_shared<GenericType>(type_params[0])});
        }
        else if (layout_template->pattern == EnumLayout::LayoutPattern::ResultType)
        {
            variants.emplace_back("Ok", std::vector<std::shared_ptr<Type>>{std::make_shared<GenericType>(type_params[0])});
            variants.emplace_back("Err", std::vector<std::shared_ptr<Type>>{std::make_shared<GenericType>(type_params[1])});
        }

        auto enum_template = std::make_shared<EnumType>(base_name, std::move(variants), type_params, std::move(layout_template));
        _parameterized_enum_templates[base_name] = enum_template;
    }

    std::shared_ptr<EnumType> TypeContext::get_parameterized_enum_template(const std::string &base_name)
    {
        auto it = _parameterized_enum_templates.find(base_name);
        return (it != _parameterized_enum_templates.end()) ? it->second : nullptr;
    }

    std::shared_ptr<ParameterizedType> TypeContext::instantiate_parameterized_enum(
        const std::string &base_name,
        const std::vector<Type *> &concrete_types)
    {
        auto enum_template = get_parameterized_enum_template(base_name);
        if (!enum_template)
        {
            return nullptr;
        }

        // Convert Type* to shared_ptr<Type>
        std::vector<std::shared_ptr<Type>> shared_types;
        for (Type *type : concrete_types)
        {
            // This is a bit unsafe - ideally we'd have shared_ptr management throughout
            // For now, create a new instance (this may need refinement based on your memory management)
            shared_types.push_back(std::shared_ptr<Type>(type, [](Type *) {})); // No-op deleter since we don't own
        }

        auto parameterized = std::make_shared<ParameterizedType>(base_name, shared_types);
        parameterized->set_base_enum_type(enum_template);

        return parameterized;
    }

    bool TypeContext::is_optional_pattern_enum(const std::string &base_name) const
    {
        auto it = _parameterized_enum_templates.find(base_name);
        return it != _parameterized_enum_templates.end() && it->second->is_optional_pattern();
    }

    bool TypeContext::is_result_pattern_enum(const std::string &base_name) const
    {
        auto it = _parameterized_enum_templates.find(base_name);
        return it != _parameterized_enum_templates.end() && it->second->is_result_pattern();
    }

    std::vector<std::shared_ptr<Type>> TypeContext::get_enum_field_types(
        const std::string &base_name,
        const std::vector<Type *> &concrete_types) const
    {
        auto it = _parameterized_enum_templates.find(base_name);
        if (it == _parameterized_enum_templates.end())
        {
            return {};
        }

        auto enum_template = it->second;

        // Convert concrete types to shared_ptr - create actual shared ownership
        std::vector<std::shared_ptr<Type>> shared_types;
        for (Type *type : concrete_types)
        {
            if (!type)
            {
                // Skip null types
                continue;
            }

            // Create a copy of the type for safe shared_ptr management
            // For now, we'll create a simple wrapper - in production this should be a proper clone
            shared_types.push_back(std::shared_ptr<Type>(type, [](Type *) {})); // No-op deleter for now
        }

        if (shared_types.empty())
        {
            return {};
        }

        auto instantiated_layout = enum_template->create_instantiated_layout(shared_types);
        if (!instantiated_layout)
        {
            return {};
        }

        return instantiated_layout->common_fields;
    }

    Type *TypeContext::lookup_type_alias(const std::string &alias_name)
    {
        // Search through complex types for type aliases
        for (const auto &type : _complex_types)
        {
            if (type->kind() == TypeKind::TypeAlias)
            {
                std::string type_name = type->name();
                // Check for exact match first
                if (type_name == alias_name)
                {
                    return type.get();
                }
                // Check for generic type alias pattern: "BaseName<param1, param2, ...>"
                // where we're looking for "BaseName"
                size_t angle_pos = type_name.find('<');
                if (angle_pos != std::string::npos)
                {
                    std::string base_name = type_name.substr(0, angle_pos);
                    if (base_name == alias_name)
                    {
                        return type.get();
                    }
                }
            }
        }
        return nullptr;
    }

    std::string TypeContext::resolve_parameterized_type_alias(const std::string &base_name,
                                                              const std::string &type_args_str)
    {
        // Look up the type alias
        Type *alias_type = lookup_type_alias(base_name);
        if (!alias_type || alias_type->kind() != TypeKind::TypeAlias)
        {
            // Not an alias, return original
            return base_name + "<" + type_args_str + ">";
        }

        auto *type_alias = static_cast<TypeAlias *>(alias_type);
        Type *target_type = type_alias->target_type();
        if (!target_type)
        {
            // Invalid alias, return original
            return base_name + "<" + type_args_str + ">";
        }

        std::string target_type_str = target_type->to_string();

        // If the target type contains template parameters, substitute them
        // This is a simple substitution - for a more robust implementation,
        // we'd need to parse the generic parameters and map them properly
        if (target_type_str.find('<') != std::string::npos)
        {
            // For parameterized aliases like AllocResult<T> = Result<T*, AllocError>
            // We need to substitute T with the actual type arguments

            // Simple case: if we have one type argument, replace T with it
            if (type_args_str.find(',') == std::string::npos) // Single type argument
            {
                // Replace T with the type argument
                size_t pos = 0;
                std::string result = target_type_str;
                while ((pos = result.find('T', pos)) != std::string::npos)
                {
                    // Check if this T is a standalone type parameter (not part of another word)
                    bool is_standalone = (pos == 0 || !std::isalnum(result[pos - 1])) &&
                                         (pos + 1 >= result.length() || !std::isalnum(result[pos + 1]));

                    if (is_standalone)
                    {
                        result.replace(pos, 1, type_args_str);
                        pos += type_args_str.length();
                    }
                    else
                    {
                        pos++;
                    }
                }
                return result;
            }
        }

        // If no substitution needed, return the target type as-is
        return target_type_str;
    }
}