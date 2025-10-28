#include "AST/Type.hpp"
#include "AST/TemplateRegistry.hpp" // For TemplateRegistry in instantiate_generic
#include "Lexer/lexer.hpp"          // For TokenKind enum
#include "Utils/Logger.hpp"
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
        // Exact match check
        if (_kind == other._kind && _name == other._name)
            return true;

        // Special case: Generic and Parameterized types with the same name should be considered equal
        // This handles cases where T (Generic) should equal T (Parameterized)
        if ((_kind == TypeKind::Generic || _kind == TypeKind::Parameterized) &&
            (other._kind == TypeKind::Generic || other._kind == TypeKind::Parameterized) &&
            _name == other._name)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Allowing generic type equality: {} ({} ) == {} ({})",
                      _name, TypeKindToString(_kind),
                      other._name, TypeKindToString(other._kind));
            return true;
        }

        // Special case: Generic types vs Struct types with the same name should be considered equal
        // This handles cases where T (Generic) should equal T (Struct) representing the same generic parameter
        if (((_kind == TypeKind::Generic && other._kind == TypeKind::Struct) ||
             (_kind == TypeKind::Struct && other._kind == TypeKind::Generic)) &&
            _name == other._name)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Allowing generic-struct type equality for type: ({} == {})",
                      TypeKindToString(_kind), TypeKindToString(other._kind));
            return true;
        }

        // Debug logging for failed type equality involving T
        if (_name == "T" || other._name == "T")
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Type equality check failed between types: {} ({}) and {} ({})",
                      _name, TypeKindToString(_kind),
                      other._name, TypeKindToString(other._kind));
        }

        return false;
    }

    bool Type::is_assignable_from(const Type &other) const
    {
        // Same type is always assignable
        if (equals(other))
            return true;

        // Special case: Generic type parameters with the same name should be compatible
        // This handles cases where T (Generic) should be assignable from T (Parameterized) or vice versa
        if ((_kind == TypeKind::Generic || _kind == TypeKind::Parameterized) &&
            (other._kind == TypeKind::Generic || other._kind == TypeKind::Parameterized) &&
            _name == other._name)
        {
            return true;
        }

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

    bool ArrayType::is_assignable_from(const Type &other) const
    {
        // First check base implementation (handles same type, unknown type, etc.)
        if (Type::is_assignable_from(other))
            return true;

        // Array types are assignable if their element types are assignable and sizes match
        if (other.kind() == TypeKind::Array)
        {
            const auto &other_array = static_cast<const ArrayType &>(other);

            // Size must match (both dynamic or both same fixed size)
            if (_size != other_array._size)
                return false;

            // Element types must be assignable
            return _element_type->is_assignable_from(*other_array._element_type);
        }

        return false;
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

    bool PointerType::is_assignable_from(const Type &other) const
    {
        // Call base implementation first (handles same type, null, etc.)
        if (Type::is_assignable_from(other))
            return true;

        // Allow any pointer type to be assigned to void*
        if (_pointee_type->kind() == TypeKind::Void && other.kind() == TypeKind::Pointer)
            return true;

        // Allow void* to be assigned to any specific pointer type (common C pattern)
        if (other.kind() == TypeKind::Pointer)
        {
            const auto &other_ptr = static_cast<const PointerType &>(other);
            if (other_ptr._pointee_type->kind() == TypeKind::Void)
                return true;
        }

        // Allow conversion between compatible pointer types
        if (other.kind() == TypeKind::Pointer)
        {
            const auto &other_ptr = static_cast<const PointerType &>(other);
            // Allow assignment if pointee types are assignable
            return _pointee_type->is_assignable_from(*other_ptr._pointee_type);
        }

        return false;
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

            // Try to safely get the parameter type name
            if (_parameter_types[i])
            {
                try
                {
                    std::string param_name = _parameter_types[i]->name();
                    oss << param_name;
                }
                catch (...)
                {
                    // Only fall back to void if there's an actual exception
                    LOG_WARN(Cryo::LogComponent::AST, "FunctionType: Exception getting parameter type at index {}, using 'void'", i);
                    oss << "void";
                }
            }
            else
            {
                oss << "void";
            }
        }
        if (_is_variadic)
        {
            if (!_parameter_types.empty())
                oss << ", ";
            oss << "...";
        }

        // Guard against null return type to prevent crashes
        if (_return_type == nullptr)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "FunctionType: Attempted to build function name with null return type!");
            oss << ") -> <null>";
        }
        else
        {
            try
            {
                // Get the raw pointer and check if it's valid
                Type *raw_ptr = _return_type.get();
                if (raw_ptr == nullptr)
                {
                    LOG_ERROR(Cryo::LogComponent::AST, "FunctionType: shared_ptr is not null but get() returns null!");
                    oss << ") -> <bad_ptr>";
                }
                else
                {
                    // Additional safety check - try to read the virtual function table
                    // by checking if we can safely cast to a void*
                    void *vptr_test = static_cast<void *>(raw_ptr);
                    if (vptr_test == nullptr)
                    {
                        LOG_ERROR(Cryo::LogComponent::AST, "FunctionType: Object pointer cast failed!");
                        oss << ") -> <cast_fail>";
                    }
                    else
                    {
                        // Try to safely get the return type name
                        try
                        {
                            std::string return_type_str = raw_ptr->name();
                            oss << ") -> " << return_type_str;
                        }
                        catch (...)
                        {
                            // Only fall back to void if there's an actual exception
                            LOG_WARN(Cryo::LogComponent::AST, "FunctionType: Exception getting return type name, using 'void'");
                            oss << ") -> void";
                        }
                    }
                }
            }
            catch (...)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "FunctionType: Exception in return type handling! Using fallback.");
                oss << ") -> <exception>";
            }
        }
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
    // TupleType Implementation
    //===----------------------------------------------------------------------===//

    size_t TupleType::size_bytes() const
    {
        size_t total_size = 0;
        for (const auto &element : _element_types)
        {
            total_size += element->size_bytes();
        }
        return total_size;
    }

    size_t TupleType::alignment() const
    {
        size_t max_alignment = 1;
        for (const auto &element : _element_types)
        {
            max_alignment = std::max(max_alignment, element->alignment());
        }
        return max_alignment;
    }

    bool TupleType::equals(const Type &other) const
    {
        if (other.kind() != TypeKind::Tuple)
            return false;

        const auto &other_tuple = static_cast<const TupleType &>(other);
        if (_element_types.size() != other_tuple._element_types.size())
            return false;

        for (size_t i = 0; i < _element_types.size(); ++i)
        {
            if (!_element_types[i]->equals(*other_tuple._element_types[i]))
                return false;
        }
        return true;
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

            register_parameterized_enum_type(TypeKind::OptionType, {"T"}, std::move(option_layout));
        }

        // Register Result<T,E> enum
        {
            auto result_layout = std::make_unique<EnumLayout>();
            result_layout->pattern = EnumLayout::LayoutPattern::ResultType;
            result_layout->uses_tag = true;
            result_layout->tag_size = 1; // bool
            result_layout->alignment = sizeof(void *);

            register_parameterized_enum_type(TypeKind::ResultType, {"T", "E"}, std::move(result_layout));
        }
    }

    Type *TypeContext::get_integer_type(IntegerKind kind, bool is_signed)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_integer_type() called with kind={} is_signed={}", IntegerKindToString(kind), is_signed);

        // Validate the IntegerKind input to prevent corruption
        int kind_int = static_cast<int>(kind);
        if (kind_int < 0 || kind_int > static_cast<int>(IntegerKind::UInt))
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - invalid IntegerKind: {}", kind_int);
            // Default to a safe integer type
            kind = IntegerKind::I32;
            kind_int = static_cast<int>(kind);
        }

        // Create a hash key for the integer type
        int key = kind_int * 2 + (is_signed ? 0 : 1);
        LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - calculated cache key={} (int_kind={}, signed={})", key, IntegerKindToString(kind), is_signed);
        LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - current cache size={}", _integer_types.size());

        auto it = _integer_types.find(key);
        if (it != _integer_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - found cached type, pointer={}", static_cast<void *>(it->second.get()));
            // Validate cached type before returning
            try
            {
                auto cached_type = it->second.get();
                if (cached_type && cached_type->kind() == TypeKind::Integer)
                {
                    return cached_type;
                }
                else
                {
                    LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - cached type is corrupted, recreating");
                    _integer_types.erase(it);
                }
            }
            catch (...)
            {
                LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - cached type validation failed, recreating");
                _integer_types.erase(it);
            }
        }

        // Create new integer type with additional validation
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - creating new IntegerType");

        // Validate construction parameters one more time
        if (kind_int < 0 || kind_int > static_cast<int>(IntegerKind::UInt))
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - refusing to create type with invalid kind");
            return nullptr;
        }

        auto int_type = std::make_unique<IntegerType>(kind, is_signed);
        Type *result = int_type.get();
        LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - created IntegerType, pointer={}", static_cast<void *>(result));
        LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - about to test kind() method on new type");

        // Test the new type before storing it
        try
        {
            auto test_kind = result->kind();
            LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - kind() test passed, kind={} ({})", TypeKindToString(test_kind), static_cast<int>(test_kind));

            // Additional validation - ensure the name was properly set
            auto test_name = result->name();
            if (test_name.empty())
            {
                LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - type name is empty, possible constructor issue");
                return nullptr;
            }
            LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - name validation passed: '{}'", test_name);
        }
        catch (...)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - type validation failed!");
            return nullptr;
        }

        _integer_types[key] = std::move(int_type);
        LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_integer_type() - stored in cache with key={}, cache size now={}", key, _integer_types.size());

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
        if (!element_type)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::create_array_type() - null element type");
            return nullptr;
        }

        // Generate a unique key for array type caching
        std::string array_key = element_type->name() + "[]";
        if (size.has_value())
        {
            array_key = element_type->name() + "[" + std::to_string(*size) + "]";
        }

        // Check if we already have this array type (simple name-based check)
        for (const auto &existing_type : _complex_types)
        {
            if (existing_type->name() == array_key)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::create_array_type() - found cached array type: {}", array_key);
                return existing_type.get();
            }
        }

        // Create a non-owning shared_ptr since TypeContext manages the lifetime
        // This is safe because element_type is guaranteed to live as long as TypeContext
        std::shared_ptr<Type> element_shared(element_type, [](Type *)
                                             {
                                                 // Non-deleting deleter - TypeContext owns the element type
                                             });

        auto array_type = std::make_unique<ArrayType>(element_shared, size);

        Type *result = array_type.get();
        _complex_types.push_back(std::move(array_type));

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::create_array_type() - created new array type: {}", array_key);
        return result;
    }

    Type *TypeContext::create_pointer_type(Type *pointee_type)
    {
        if (!pointee_type)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::create_pointer_type() - null pointee type");
            return nullptr;
        }

        // Generate the pointer type name (automatically set by PointerType constructor)
        std::string pointer_key = pointee_type->name() + "*";

        // Check if we already have this pointer type (simple name-based check)
        for (const auto &existing_type : _complex_types)
        {
            if (existing_type->name() == pointer_key)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::create_pointer_type() - found cached pointer type: {}", pointer_key);
                return existing_type.get();
            }
        }

        // Create a non-owning shared_ptr since TypeContext manages the lifetime
        // This is safe because pointee_type is guaranteed to live as long as TypeContext
        std::shared_ptr<Type> pointee_shared(pointee_type, [](Type *)
                                             {
                                                 // Non-deleting deleter - TypeContext owns the pointee type
                                             });

        auto pointer_type = std::make_unique<PointerType>(pointee_shared);

        Type *result = pointer_type.get();
        _complex_types.push_back(std::move(pointer_type));

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::create_pointer_type() - created new pointer type: {}", pointer_key);
        return result;
    }

    Type *TypeContext::create_reference_type(Type *referent_type)
    {
        if (!referent_type)
        {
            LOG_ERROR(Cryo::LogComponent::AST, "TypeContext::create_reference_type() - null referent type");
            return nullptr;
        }

        // Generate the reference type name (automatically set by ReferenceType constructor)
        std::string reference_key = "&" + referent_type->name();

        // Check if we already have this reference type (simple name-based check)
        for (const auto &existing_type : _complex_types)
        {
            if (existing_type->name() == reference_key)
            {
                LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::create_reference_type() - found cached reference type: {}", reference_key);
                return existing_type.get();
            }
        }

        // Create a non-owning shared_ptr since TypeContext manages the lifetime
        // This is safe because referent_type is guaranteed to live as long as TypeContext
        std::shared_ptr<Type> referent_shared(referent_type, [](Type *)
                                              {
                                                  // Non-deleting deleter - TypeContext owns the referent type
                                              });

        auto reference_type = std::make_unique<ReferenceType>(referent_shared);

        Type *result = reference_type.get();
        _complex_types.push_back(std::move(reference_type));

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::create_reference_type() - created new reference type: {}", reference_key);
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

    Type *TypeContext::create_tuple_type(const std::vector<Type *> &element_types)
    {
        std::vector<std::shared_ptr<Type>> shared_elements;
        for (Type *element : element_types)
        {
            shared_elements.push_back(std::shared_ptr<Type>(element, [](Type *) {}));
        }

        auto tuple_type = std::make_unique<TupleType>(std::move(shared_elements));
        Type *result = tuple_type.get();
        _complex_types.push_back(std::move(tuple_type));

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

    //===----------------------------------------------------------------------===//
    // Token-based Type Parsing (Primary pathway)
    //===----------------------------------------------------------------------===//

    Type *TypeContext::parse_primitive_type_from_token(TokenKind token_kind)
    {
        switch (token_kind)
        {
        case TokenKind::TK_KW_VOID:
            return get_void_type();
        case TokenKind::TK_KW_BOOLEAN:
            return get_boolean_type();
        case TokenKind::TK_KW_CHAR:
            return get_char_type();
        case TokenKind::TK_KW_STRING:
            return get_string_type();

        // Integer types
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

        // Unsigned integer types
        case TokenKind::TK_KW_UINT8:
            return get_u8_type();
        case TokenKind::TK_KW_UINT16:
            return get_u16_type();
        case TokenKind::TK_KW_UINT32:
            return get_u32_type();
        case TokenKind::TK_KW_UINT64:
            return get_u64_type();
        case TokenKind::TK_KW_UINT:
            return get_integer_type(IntegerKind::UInt, false);

        // Float types
        case TokenKind::TK_KW_F32:
            return get_f32_type();
        case TokenKind::TK_KW_F64:
            return get_f64_type();
        case TokenKind::TK_KW_FLOAT:
            return get_default_float_type();
        case TokenKind::TK_KW_DOUBLE:
            return get_f64_type(); // double is alias for f64

        // Special types
        case TokenKind::TK_KW_AUTO:
            return get_auto_type();
        case TokenKind::TK_KW_NULL:
            return get_null_type();
        case TokenKind::TK_ELLIPSIS:
            return get_variadic_type();

        default:
            return nullptr; // Not a primitive type token
        }
    }

    Type *TypeContext::parse_pointer_type_from_tokens(const std::vector<Token> &tokens, size_t &index)
    {
        // Expect: <base_type> '*'
        if (index >= tokens.size())
            return nullptr;

        // Parse the base type first
        Type *base_type = parse_type_from_token_stream(tokens, index);
        if (!base_type)
            return nullptr;

        // Check for '*' token
        if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
        {
            ++index; // consume '*'
            return create_pointer_type(base_type);
        }

        // Not a pointer type, return the base type
        return base_type;
    }

    Type *TypeContext::parse_array_type_from_tokens(const std::vector<Token> &tokens, size_t &index)
    {
        // Expect: <element_type> '[' [size] ']'
        if (index >= tokens.size())
            return nullptr;

        // Parse the element type first
        Type *element_type = parse_type_from_token_stream(tokens, index);
        if (!element_type)
            return nullptr;

        // Check for '[' token
        if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_L_SQUARE)
        {
            ++index; // consume '['

            std::optional<size_t> array_size;

            // Check for size (optional)
            if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_NUMERIC_CONSTANT)
            {
                // Parse the numeric constant for array size
                std::string size_str = std::string(tokens[index].text());
                array_size = std::stoull(size_str);
                ++index; // consume size
            }

            // Expect ']'
            if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_R_SQUARE)
            {
                ++index; // consume ']'
                return create_array_type(element_type, array_size);
            }

            return nullptr; // Malformed array type
        }

        // Not an array type, return the element type
        return element_type;
    }

    Type *TypeContext::parse_type_from_token_stream(const std::vector<Token> &tokens, size_t &index)
    {
        if (index >= tokens.size())
            return nullptr;

        const Token &current_token = tokens[index];

        // Handle reference types first (&Type)
        if (current_token.kind() == TokenKind::TK_AMP)
        {
            ++index; // consume '&'

            // Parse the referent type
            Type *referent_type = parse_type_from_token_stream(tokens, index);
            if (referent_type)
            {
                return create_reference_type(referent_type);
            }
            else
            {
                return nullptr; // Failed to parse referent type
            }
        }

        // Try primitive types first
        Type *primitive_type = parse_primitive_type_from_token(current_token.kind());
        if (primitive_type)
        {
            ++index; // consume the primitive type token

            // Handle pointer modifiers directly here
            Type *result_type = primitive_type;
            while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
            {
                ++index; // consume '*'
                result_type = create_pointer_type(result_type);
            }

            // Handle array modifiers
            while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_L_SQUARE)
            {
                ++index; // consume '['

                std::optional<size_t> array_size;
                if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_NUMERIC_CONSTANT)
                {
                    std::string size_str = std::string(tokens[index].text());
                    array_size = std::stoull(size_str);
                    ++index; // consume size
                }

                // Expect ']'
                if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_R_SQUARE)
                {
                    ++index; // consume ']'
                    result_type = create_array_type(result_type, array_size);
                }
                else
                {
                    return nullptr; // Malformed array type
                }
            }

            return result_type;
        }

        // Handle identifiers (user-defined types, generic parameters)
        if (current_token.kind() == TokenKind::TK_IDENTIFIER)
        {
            std::string type_name = std::string(current_token.text());
            ++index; // consume identifier

            // Check for generic instantiation: Type<T, U>
            if (index < tokens.size() && tokens[index].kind() == TokenKind::TK_L_ANGLE)
            {
                return parse_generic_type_from_tokens(tokens, index);
            }

            // First check if it's a primitive type (to avoid creating incorrect ClassType)
            if (type_name == "u8")
                return get_u8_type();
            if (type_name == "u16")
                return get_u16_type();
            if (type_name == "u32")
                return get_u32_type();
            if (type_name == "u64")
                return get_u64_type();
            if (type_name == "i8")
                return get_i8_type();
            if (type_name == "i16")
                return get_i16_type();
            if (type_name == "i32")
                return get_i32_type();
            if (type_name == "i64")
                return get_i64_type();
            if (type_name == "f32")
                return get_f32_type();
            if (type_name == "f64")
                return get_f64_type();
            if (type_name == "boolean")
                return get_boolean_type();
            if (type_name == "string")
                return get_string_type();
            if (type_name == "void")
                return get_void_type();

            // Simple user-defined type
            // Try struct, class, enum, trait types
            Type *user_type = get_struct_type(type_name);
            if (user_type && user_type->kind() != TypeKind::Unknown)
            {
                // Handle pointer modifiers for user-defined types
                Type *result_type = user_type;
                while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
                {
                    ++index; // consume '*'
                    result_type = create_pointer_type(result_type);
                }
                return result_type;
            }

            user_type = get_class_type(type_name);
            if (user_type && user_type->kind() != TypeKind::Unknown)
            {
                // Handle pointer modifiers for user-defined types
                Type *result_type = user_type;
                while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
                {
                    ++index; // consume '*'
                    result_type = create_pointer_type(result_type);
                }
                return result_type;
            }

            user_type = lookup_enum_type(type_name);
            if (user_type)
            {
                // Handle pointer modifiers for user-defined types
                Type *result_type = user_type;
                while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
                {
                    ++index; // consume '*'
                    result_type = create_pointer_type(result_type);
                }
                return result_type;
            }

            user_type = get_trait_type(type_name);
            if (user_type && user_type->kind() != TypeKind::Unknown)
            {
                // Handle pointer modifiers for user-defined types
                Type *result_type = user_type;
                while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
                {
                    ++index; // consume '*'
                    result_type = create_pointer_type(result_type);
                }
                return result_type;
            }

            // Generic parameter - also handle pointers
            Type *generic_type = get_generic_type(type_name);
            Type *result_type = generic_type;
            while (index < tokens.size() && tokens[index].kind() == TokenKind::TK_STAR)
            {
                ++index; // consume '*'
                result_type = create_pointer_type(result_type);
            }
            return result_type;
        }

        return nullptr; // Unable to parse type
    }

    Type *TypeContext::parse_type_from_tokens(Lexer &lexer)
    {
        // Tokenize the current type expression from lexer
        std::vector<Token> tokens;
        Token current_token = lexer.next_token();

        // Collect tokens until we hit a delimiter or EOF
        while (!current_token.is_eof() &&
               current_token.kind() != TokenKind::TK_SEMICOLON &&
               current_token.kind() != TokenKind::TK_COMMA &&
               current_token.kind() != TokenKind::TK_R_PAREN &&
               current_token.kind() != TokenKind::TK_R_BRACE)
        {
            tokens.push_back(current_token);
            current_token = lexer.next_token();
        }

        // Parse the collected tokens
        size_t index = 0;
        return parse_type_from_token_stream(tokens, index);
    }

    Type *TypeContext::parse_type_from_string_via_tokens(const std::string &type_str)
    {
        // Create a spot lexer for the type string
        Lexer type_lexer(type_str);
        return parse_type_from_tokens(type_lexer);
    }

    Type *TypeContext::parse_generic_type_from_tokens(const std::vector<Token> &tokens, size_t &index)
    {
        // This is a simplified implementation
        // For now, fall back to string parsing for complex generic types
        // TODO: Implement full token-based generic parsing
        return nullptr;
    }

    //===----------------------------------------------------------------------===//
    // Legacy String-based Type Parsing (Deprecated)
    //===----------------------------------------------------------------------===//

    Type *TypeContext::parse_type_from_string(const std::string &type_str)
    {
        // Normalize the type string first to handle spacing inconsistencies
        std::string normalized_type_str = normalize_generic_type_string(type_str);

        // Check for function types first (before basic types)
        if (normalized_type_str.find("->") != std::string::npos)
        {
            return parse_function_type_from_string(normalized_type_str);
        }

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
            Type *element_type = parse_type_from_string_via_tokens(element_type_str);
            if (element_type)
            {
                return create_array_type(element_type);
            }
        }

        // Pointer types (basic parsing for "type*")
        if (normalized_type_str.length() > 1 && normalized_type_str.back() == '*')
        {
            std::string pointee_type_str = normalized_type_str.substr(0, normalized_type_str.length() - 1);
            Type *pointee_type = parse_type_from_string_via_tokens(pointee_type_str);
            if (pointee_type)
            {
                return create_pointer_type(pointee_type);
            }
        }

        // Reference types (basic parsing for "&type")
        if (normalized_type_str.length() > 1 && normalized_type_str.front() == '&')
        {
            std::string referent_type_str = normalized_type_str.substr(1);
            Type *referent_type = parse_type_from_string_via_tokens(referent_type_str);
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
                Type *pointee_type = parse_type_from_string_via_tokens(params_str);
                if (pointee_type)
                {
                    return create_pointer_type(pointee_type);
                }
            }
            else if (base_name == "const_ptr")
            {
                // const_ptr<T> = const T* - for now, treat same as ptr<T>
                Type *pointee_type = parse_type_from_string_via_tokens(params_str);
                if (pointee_type)
                {
                    return create_pointer_type(pointee_type);
                }
            }

            // This looks like a generic instantiation - use TypeRegistry if available
            if (_type_registry)
            {
                // Try to instantiate using the TypeRegistry
                ParameterizedType *instantiated_type = _type_registry->parse_and_instantiate(normalized_type_str);
                if (instantiated_type)
                {
                    return instantiated_type;
                }
            }

            // Fallback: For unknown generic types during parsing (before TypeRegistry is set up),
            // return an unknown type instead of creating a struct type with the full generic name.
            // This allows parsing to continue and type checking will resolve it later.
            return get_unknown_type();
        }

        // Check for user-defined enum types FIRST (before struct types)
        auto enum_it = _enum_types.find(normalized_type_str);
        if (enum_it != _enum_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST, "parse_type_from_string: Found '{}' in _enum_types", normalized_type_str);
            return enum_it->second.get();
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

    Type *TypeContext::parse_function_type_from_string(const std::string &type_str)
    {
        // Parse function type string like "() -> void" or "<T>(arg: T) -> T" or "(int, string) -> bool"
        std::string normalized = type_str;

        // Check if it's a generic function type
        bool is_generic = false;
        std::string generics_part;
        std::string function_part;

        if (normalized.front() == '<')
        {
            // Generic function type: <T>(params) -> return_type
            size_t close_angle = normalized.find('>');
            if (close_angle != std::string::npos)
            {
                is_generic = true;
                generics_part = normalized.substr(1, close_angle - 1); // Extract T
                function_part = normalized.substr(close_angle + 1);    // Extract (params) -> return_type
            }
            else
            {
                return get_unknown_type();
            }
        }
        else
        {
            // Non-generic function type: (params) -> return_type
            function_part = normalized;
        }

        // Parse the function part: (params) -> return_type
        size_t arrow_pos = function_part.find("->");
        if (arrow_pos == std::string::npos)
        {
            return get_unknown_type();
        }

        std::string params_part = function_part.substr(0, arrow_pos);
        std::string return_part = function_part.substr(arrow_pos + 2);

        // Trim whitespace
        params_part.erase(params_part.find_last_not_of(" \t") + 1);
        params_part.erase(0, params_part.find_first_not_of(" \t"));
        return_part.erase(return_part.find_last_not_of(" \t") + 1);
        return_part.erase(0, return_part.find_first_not_of(" \t"));

        // Parse return type
        Type *return_type = parse_type_from_string_via_tokens(return_part);
        if (!return_type)
        {
            return get_unknown_type();
        }

        // Parse parameter types
        std::vector<Type *> param_types;

        if (params_part.size() >= 2 && params_part.front() == '(' && params_part.back() == ')')
        {
            std::string params_inner = params_part.substr(1, params_part.size() - 2);

            if (!params_inner.empty())
            {
                // Split parameters by comma
                std::vector<std::string> param_strings;
                std::string current_param;
                int paren_depth = 0;

                for (char c : params_inner)
                {
                    if (c == '(')
                    {
                        paren_depth++;
                        current_param += c;
                    }
                    else if (c == ')')
                    {
                        paren_depth--;
                        current_param += c;
                    }
                    else if (c == ',' && paren_depth == 0)
                    {
                        // Trim and add parameter
                        current_param.erase(current_param.find_last_not_of(" \t") + 1);
                        current_param.erase(0, current_param.find_first_not_of(" \t"));
                        if (!current_param.empty())
                        {
                            param_strings.push_back(current_param);
                        }
                        current_param.clear();
                    }
                    else
                    {
                        current_param += c;
                    }
                }

                // Add the last parameter
                current_param.erase(current_param.find_last_not_of(" \t") + 1);
                current_param.erase(0, current_param.find_first_not_of(" \t"));
                if (!current_param.empty())
                {
                    param_strings.push_back(current_param);
                }

                // Parse each parameter type
                for (const std::string &param_str : param_strings)
                {
                    // Handle named parameters like "arg: T" - extract just the type part
                    std::string type_part = param_str;
                    size_t colon_pos = param_str.find(':');
                    if (colon_pos != std::string::npos)
                    {
                        type_part = param_str.substr(colon_pos + 1);
                        type_part.erase(type_part.find_last_not_of(" \t") + 1);
                        type_part.erase(0, type_part.find_first_not_of(" \t"));
                    }

                    Type *param_type = parse_type_from_string_via_tokens(type_part);
                    if (!param_type)
                    {
                        return get_unknown_type();
                    }
                    param_types.push_back(param_type);
                }
            }
        }
        else
        {
            return get_unknown_type();
        }

        // Create function type
        return create_function_type(return_type, param_types);
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

        // Check if an enum type with this name already exists - if so, return the enum type
        // This prevents creating struct types for names that should be enum types
        Type *existing_enum = lookup_enum_type(name);
        if (existing_enum)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Attempted to create struct type for '{}' but enum type already exists - returning enum type instead", name);
            return existing_enum;
        }

        // Prevent creating struct types for built-in type names
        // This prevents accidental creation of struct types for primitive types like "u64"
        if (name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "int" ||
            name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
            name == "f32" || name == "f64" || name == "float" || name == "double" ||
            name == "boolean" || name == "char" || name == "string" || name == "void")
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Attempted to create struct type for built-in type name: {}", name);
            LOG_DEBUG(Cryo::LogComponent::AST, "Returning unknown type instead of creating struct");
            return get_unknown_type();
        }

        // Create new struct type
        auto struct_type = std::make_unique<StructType>(name);
        Type *result = struct_type.get();
        _struct_types[name] = std::move(struct_type);

        return result;
    }

    Type *TypeContext::lookup_struct_type(const std::string &name)
    {
        auto it = _struct_types.find(name);
        if (it != _struct_types.end())
        {
            return it->second.get();
        }

        // Check if an enum type with this name exists
        Type *existing_enum = lookup_enum_type(name);
        if (existing_enum)
        {
            return existing_enum;
        }

        // Return null if not found - don't create
        return nullptr;
    }

    Type *TypeContext::get_class_type(const std::string &name)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_class_type() called with name='{}'", name);

        auto it = _class_types.find(name);
        if (it != _class_types.end())
        {
            LOG_TRACE(Cryo::LogComponent::AST, "TypeContext::get_class_type() - found existing class type for '{}'", name);
            return it->second.get();
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_class_type() - creating new class type for '{}'", name);

        // Create new class type
        auto class_type = std::make_unique<ClassType>(name);
        Type *result = class_type.get();
        _class_types[name] = std::move(class_type);

        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_class_type() - successfully created class type for '{}'", name);
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
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::get_enum_type called with name={} is_simple={}", name, is_simple);
        LOG_DEBUG(Cryo::LogComponent::AST, "Call stack trace (simplified): TypeContext::get_enum_type");

        auto it = _enum_types.find(name);
        if (it != _enum_types.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found existing enum type for {} existing is_simple={}", name, it->second->is_simple_enum());
            return it->second.get();
        }

        // If there's a conflicting struct type with the same name, remove it
        // This handles cases where struct types were created before enum types were available
        auto struct_it = _struct_types.find(name);
        if (struct_it != _struct_types.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Removing conflicting struct type '{}' to make way for enum type", name);
            _struct_types.erase(struct_it);
        }

        // Create new enum type
        LOG_DEBUG(Cryo::LogComponent::AST, "Creating new EnumType with is_simple={}", is_simple);
        auto enum_type = std::make_unique<EnumType>(name, std::move(variants), is_simple);
        LOG_DEBUG(Cryo::LogComponent::AST, "Created EnumType, result is_simple_enum()={}", enum_type->is_simple_enum());
        Type *result = enum_type.get();
        _enum_types[name] = std::move(enum_type);

        return result;
    }

    Type *TypeContext::lookup_enum_type(const std::string &name)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::lookup_enum_type called with name={}", name);

        auto it = _enum_types.find(name);
        if (it != _enum_types.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Found existing enum type for {} is_simple={}", name, it->second->is_simple_enum());
            return it->second.get();
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "No existing enum type found for {}", name);
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

    void TypeContext::register_parameterized_enum_type(TypeKind enum_type_kind,
                                                       const std::vector<std::string> &type_params,
                                                       std::unique_ptr<EnumLayout> layout_template)
    {
        std::string base_name = ParameterizedType::get_base_name_from_kind(enum_type_kind);

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

    // Helper function to convert string base names to TypeKind
    TypeKind TypeContext::get_parameterized_type_kind(const std::string &base_name)
    {
        if (base_name == "Option")
            return TypeKind::OptionType;
        if (base_name == "Result")
            return TypeKind::ResultType;
        if (base_name == "Array")
            return TypeKind::ArrayType;
        if (base_name == "Vector")
            return TypeKind::VectorType;
        if (base_name == "ptr")
            return TypeKind::PtrType;
        if (base_name == "const_ptr")
            return TypeKind::ConstPtrType;
        // Default to generic parameterized type
        return TypeKind::Parameterized;
    }

    ParameterizedType *TypeContext::create_parameterized_type(const std::string &base_name,
                                                              const std::vector<std::string> &param_names)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeContext::create_parameterized_type('{}', {} params)", base_name, param_names.size());

        // Create a unique key for deduplication
        std::string key = base_name + "<";
        for (size_t i = 0; i < param_names.size(); ++i)
        {
            if (i > 0)
                key += ",";
            key += param_names[i];
        }
        key += ">";

        // Check if we already have a deferred type for this signature
        static std::unordered_map<std::string, std::unique_ptr<ParameterizedType>> deferred_types;
        auto it = deferred_types.find(key);
        if (it != deferred_types.end())
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Reusing existing deferred ParameterizedType for '{}'", key);
            return it->second.get();
        }

        // Create a deferred parameterized type that can be resolved later
        // when the template is registered in the TypeRegistry
        TypeKind param_kind = get_parameterized_type_kind(base_name);

        // For deferred types, we'll create a ParameterizedType with the base name
        // and parameter names, but not instantiated yet
        LOG_DEBUG(Cryo::LogComponent::AST, "About to call ParameterizedType constructor with base_name='{}'", base_name);
        auto param_type = std::make_unique<ParameterizedType>(base_name, param_names);
        auto *result = param_type.get();
        LOG_DEBUG(Cryo::LogComponent::AST, "Created ParameterizedType, result->name()='{}', result->base_name()='{}'", result->name(), result->base_name());

        // Store it with deduplication
        deferred_types[key] = std::move(param_type);

        LOG_DEBUG(Cryo::LogComponent::AST, "Created new deferred ParameterizedType for '{}' -> '{}'", key, base_name);
        return result;
    }

    bool TypeContext::is_enum_pattern_type(TypeKind kind)
    {
        return kind == TypeKind::OptionType || kind == TypeKind::ResultType;
    }

    ParameterizedType *TypeContext::instantiate_generic(const std::string &base_name,
                                                        const std::vector<Type *> &args)
    {
        // Convert base name to TypeKind
        TypeKind param_kind = get_parameterized_type_kind(base_name);

        // Convert args to shared_ptr
        std::vector<std::shared_ptr<Type>> shared_args;
        for (auto *arg : args)
        {
            shared_args.push_back(std::shared_ptr<Type>(arg, [](Type *) {}));
        }

        // Create appropriate specialized type based on pattern
        std::unique_ptr<ParameterizedType> instantiation;

        if (is_enum_pattern_type(param_kind))
        {
            // Create enum-specific type
            auto enum_template = get_parameterized_enum_template(base_name);
            instantiation = std::make_unique<ParameterizedEnumType>(param_kind, shared_args, enum_template);
        }
        else
        {
            // Create class-specific type
            instantiation = std::make_unique<ParameterizedClassType>(param_kind, shared_args);
        }

        ParameterizedType *result = instantiation.get();
        _complex_types.push_back(std::move(instantiation));

        return result;
    }

    Type *TypeContext::resolve_scoped_type(const std::string &scope, const std::string &type_name)
    {
        // Handle scoped type resolution like "Std::Runtime::MemoryBlock"
        std::string full_name = scope + "::" + type_name;

        // First try to find it as a struct type
        Type *struct_type = get_struct_type(full_name);
        if (struct_type && struct_type->kind() != TypeKind::Unknown)
        {
            return struct_type;
        }

        // Try as a class type
        Type *class_type = get_class_type(full_name);
        if (class_type && class_type->kind() != TypeKind::Unknown)
        {
            return class_type;
        }

        // Try as an enum type
        auto it = _enum_types.find(full_name);
        if (it != _enum_types.end())
        {
            return it->second.get();
        }

        // Try as a trait type
        Type *trait_type = get_trait_type(full_name);
        if (trait_type && trait_type->kind() != TypeKind::Unknown)
        {
            return trait_type;
        }

        // Try as a generic type parameter
        Type *generic_type = get_generic_type(full_name);
        if (generic_type && generic_type->kind() != TypeKind::Unknown)
        {
            return generic_type;
        }

        // If not found, return unknown type
        return get_unknown_type();
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
    // ParameterizedType Base Implementation
    //===----------------------------------------------------------------------===//

    std::string ParameterizedType::get_instantiated_name() const
    {
        if (_cached_instantiated_name.empty())
        {
            if (is_template())
            {
                // For templates like Array<T>, return the template name
                _cached_instantiated_name = name() + "<";
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
                _cached_instantiated_name = name() + "<";
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
                _cached_instantiated_name = name();
            }
        }
        return _cached_instantiated_name;
    }

    bool ParameterizedType::is_assignable_from(const Type &other) const
    {
        // Special case: pointer types (ptr<T>) can accept null
        if (other.kind() == TypeKind::Null && (_param_type_kind == TypeKind::PtrType || _param_type_kind == TypeKind::ConstPtrType))
        {
            return true;
        }

        if (other.kind() != TypeKind::Parameterized)
            return false;

        const ParameterizedType &other_param = static_cast<const ParameterizedType &>(other);

        // Must have same param type kind
        if (_param_type_kind != other_param._param_type_kind)
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

        // Default implementation for basic parameterized types
        // Specialized classes will override this for specific behavior
        if (_param_type_kind == TypeKind::ArrayType)
        {
            // Array<T> has: T* elements, u64 length, u64 capacity
            return sizeof(void *) + sizeof(uint64_t) + sizeof(uint64_t);
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

    std::string ParameterizedType::get_mangled_name() const
    {
        if (is_template())
        {
            // Template itself doesn't have a mangled name, return base name
            return name();
        }

        // For instantiated types, create mangled name like "Array_int" instead of "Array<int>"
        std::string mangled = name();
        for (const auto &param : _type_params)
        {
            mangled += "_" + param->to_string();
        }

        // Replace any remaining angle brackets or special characters that might cause issues
        std::replace(mangled.begin(), mangled.end(), '<', '_');
        std::replace(mangled.begin(), mangled.end(), '>', '_');
        std::replace(mangled.begin(), mangled.end(), ',', '_');
        std::replace(mangled.begin(), mangled.end(), ' ', '_');

        return mangled;
    }

    std::shared_ptr<ParameterizedType> ParameterizedType::substitute(const std::unordered_map<std::string, std::shared_ptr<Type>> &substitutions) const
    {
        if (!is_template())
        {
            // For instantiated types, substitute within the type parameters
            std::vector<std::shared_ptr<Type>> new_params;
            bool changed = false;

            for (const auto &param : _type_params)
            {
                std::shared_ptr<Type> substituted_param = param;

                // If the parameter is itself a generic type that needs substitution
                if (param->kind() == TypeKind::Generic)
                {
                    auto it = substitutions.find(param->name());
                    if (it != substitutions.end())
                    {
                        substituted_param = it->second;
                        changed = true;
                    }
                }
                // If the parameter is a parameterized type, recursively substitute
                else if (param->kind() == TypeKind::Parameterized)
                {
                    auto param_type = std::static_pointer_cast<ParameterizedType>(param);
                    auto substituted_nested = param_type->substitute(substitutions);
                    if (substituted_nested)
                    {
                        substituted_param = substituted_nested;
                        changed = true;
                    }
                }

                new_params.push_back(substituted_param);
            }

            if (changed)
            {
                // For now, create base ParameterizedType - this could be improved with virtual factory method
                return std::make_shared<ParameterizedType>(_param_type_kind, new_params);
            }
            return nullptr; // No substitution needed
        }

        // For template types, create instantiation if all parameters can be substituted
        std::vector<std::shared_ptr<Type>> concrete_types;
        for (const auto &param_name : _param_names)
        {
            auto it = substitutions.find(param_name);
            if (it != substitutions.end())
            {
                concrete_types.push_back(it->second);
            }
            else
            {
                // Cannot fully substitute - return nullptr
                return nullptr;
            }
        }

        return std::make_shared<ParameterizedType>(_param_type_kind, concrete_types);
    }

    bool ParameterizedType::has_type_parameter(const std::string &param_name) const
    {
        // Check if this parameterized type has the given parameter name
        if (is_template())
        {
            return std::find(_param_names.begin(), _param_names.end(), param_name) != _param_names.end();
        }

        // For instantiated types, check recursively in type parameters
        for (const auto &param : _type_params)
        {
            if (param->kind() == TypeKind::Generic && param->name() == param_name)
            {
                return true;
            }
            else if (param->kind() == TypeKind::Parameterized)
            {
                auto param_type = std::static_pointer_cast<ParameterizedType>(param);
                if (param_type->has_type_parameter(param_name))
                {
                    return true;
                }
            }
        }

        return false;
    }

    //===----------------------------------------------------------------------===//
    // ParameterizedEnumType Implementation
    //===----------------------------------------------------------------------===//

    std::shared_ptr<ParameterizedType> ParameterizedEnumType::instantiate(const std::vector<std::shared_ptr<Type>> &concrete_types) const
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

        // Use the base name from this template instead of TypeKind mapping to preserve custom enum names
        LOG_DEBUG(Cryo::LogComponent::AST, "ParameterizedEnumType::instantiate - template base_name(): '{}'", base_name());
        auto instantiation = std::make_shared<ParameterizedEnumType>(base_name(), concrete_types, _base_enum_type);
        LOG_DEBUG(Cryo::LogComponent::AST, "ParameterizedEnumType::instantiate - created instantiation with base_name(): '{}', type_parameters.size(): {}", instantiation->base_name(), instantiation->type_parameters().size());
        return instantiation;
    }

    std::vector<ParameterizedEnumType::FieldLayout> ParameterizedEnumType::get_enum_field_layout() const
    {
        std::vector<FieldLayout> layout;

        if (!_base_enum_type)
        {
            return layout; // Empty layout if no base enum
        }

        // Implementation depends on the specific enum variant structure
        // This is a simplified version - real implementation would analyze enum variants
        if (is_optional_like())
        {
            // Option<T> layout: discriminant (1 byte) + value (T bytes, if Some)
            layout.push_back({"discriminant", std::make_shared<IntegerType>(IntegerKind::U8, false), 0, 1});

            if (!_type_params.empty())
            {
                size_t value_offset = 1; // After discriminant
                auto value_type = _type_params[0];
                layout.push_back({"value", value_type, value_offset, value_type->size_bytes()});
            }
        }
        else if (is_result_like())
        {
            // Result<T,E> layout: discriminant + union of T and E
            layout.push_back({"discriminant", std::make_shared<IntegerType>(IntegerKind::U8, false), 0, 1});

            if (_type_params.size() >= 2)
            {
                size_t value_offset = 1;
                size_t max_size = std::max(_type_params[0]->size_bytes(), _type_params[1]->size_bytes());
                layout.push_back({"ok_value", _type_params[0], value_offset, max_size});
                layout.push_back({"err_value", _type_params[1], value_offset, max_size});
            }
        }

        return layout;
    }

    bool ParameterizedEnumType::has_discriminant() const
    {
        return _base_enum_type && _base_enum_type->uses_discriminant_tag();
    }

    size_t ParameterizedEnumType::get_discriminant_size() const
    {
        if (!has_discriminant())
            return 0;

        return _base_enum_type->get_discriminant_size();
    }

    std::vector<std::shared_ptr<Type>> ParameterizedEnumType::get_data_field_types() const
    {
        std::vector<std::shared_ptr<Type>> field_types;

        for (const auto &param : _type_params)
        {
            field_types.push_back(param);
        }

        return field_types;
    }

    size_t ParameterizedEnumType::get_data_offset() const
    {
        return has_discriminant() ? get_discriminant_size() : 0;
    }

    size_t ParameterizedEnumType::size_bytes() const
    {
        if (is_template())
        {
            return 0; // Template has no concrete size
        }

        size_t total_size = 0;

        // Add discriminant size
        if (has_discriminant())
        {
            total_size += get_discriminant_size();
        }

        // Add data size based on enum pattern
        if (is_optional_like() && !_type_params.empty())
        {
            // Option<T>: discriminant + T (when Some)
            total_size += _type_params[0]->size_bytes();
        }
        else if (is_result_like() && _type_params.size() >= 2)
        {
            // Result<T,E>: discriminant + max(T, E)
            total_size += std::max(_type_params[0]->size_bytes(), _type_params[1]->size_bytes());
        }
        else
        {
            // Generic enum: discriminant + largest variant
            size_t max_variant_size = 0;
            for (const auto &param : _type_params)
            {
                max_variant_size = std::max(max_variant_size, param->size_bytes());
            }
            total_size += max_variant_size;
        }

        return total_size;
    }

    size_t ParameterizedEnumType::alignment() const
    {
        if (is_template())
            return 1;

        size_t max_align = get_discriminant_size(); // Discriminant alignment

        for (const auto &param : _type_params)
        {
            max_align = std::max(max_align, param->alignment());
        }

        return max_align;
    }

    //===----------------------------------------------------------------------===//
    // ParameterizedClassType Implementation
    //===----------------------------------------------------------------------===//

    std::shared_ptr<ParameterizedType> ParameterizedClassType::instantiate(const std::vector<std::shared_ptr<Type>> &concrete_types) const
    {
        if (!is_template())
        {
            return nullptr; // Already instantiated or not a template
        }

        if (concrete_types.size() != _param_names.size())
        {
            return nullptr; // Mismatched parameter count
        }

        auto instantiation = std::make_shared<ParameterizedClassType>(_param_type_kind, concrete_types);
        return instantiation;
    }

    //===----------------------------------------------------------------------===//
    // TypeRegistry Implementation
    //===----------------------------------------------------------------------===//

    void TypeRegistry::register_template(const std::string &base_name,
                                         const std::vector<std::string> &param_names)
    {
        auto template_type = std::make_shared<ParameterizedType>(base_name, param_names);
        _templates[base_name] = template_type;
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeRegistry::register_template - stored '{}' in templates map, size now: {}", base_name, _templates.size());
    }

    void TypeRegistry::register_enum_template(const std::string &base_name,
                                              const std::vector<std::string> &param_names,
                                              std::shared_ptr<EnumType> base_enum)
    {
        // Create a ParameterizedEnumType for enum templates instead of basic ParameterizedType
        auto template_type = std::make_shared<ParameterizedEnumType>(base_name, param_names, base_enum);
        _templates[base_name] = template_type;
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeRegistry::register_enum_template - stored enum '{}' in templates map, size now: {}", base_name, _templates.size());
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
            LOG_DEBUG(Cryo::LogComponent::AST, "TypeRegistry::instantiate created type with name='{}', base_name='{}'", instantiation->name(), instantiation->base_name());
            _instantiations[inst_name] = instantiation;
            return instantiation.get();
        }

        return nullptr;
    }

    ParameterizedType *TypeRegistry::parse_and_instantiate(const std::string &type_string)
    {
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeRegistry::parse_and_instantiate called with: '{}'", type_string);

        auto [base_name, param_strs] = parse_generic_syntax(type_string);

        LOG_DEBUG(Cryo::LogComponent::AST, "Parsed base_name: '{}', param_strs.size(): {}", base_name, param_strs.size());

        if (param_strs.empty() || !has_template(base_name))
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "parse_and_instantiate failed: param_strs.empty()={}, has_template('{}')={}",
                      param_strs.empty(), base_name, has_template(base_name));
            return nullptr;
        }

        // Convert parameter strings to types
        std::vector<Type *> concrete_types;
        for (const auto &param_str : param_strs)
        {
            LOG_DEBUG(Cryo::LogComponent::AST, "Converting parameter string: '{}'", param_str);
            // Use token-based type parsing
            Type *param_type = _type_context->parse_type_from_string_via_tokens(param_str);
            if (!param_type)
            {
                LOG_DEBUG(Cryo::LogComponent::AST, "Failed to parse parameter type: '{}'", param_str);
                return nullptr;
            }
            LOG_DEBUG(Cryo::LogComponent::AST, "Successfully parsed parameter type: '{}' -> {}", param_str, param_type->name());
            concrete_types.push_back(param_type);
        }

        LOG_DEBUG(Cryo::LogComponent::AST, "Calling instantiate('{}', {} types)", base_name, concrete_types.size());
        return instantiate(base_name, concrete_types);
    }

    ParameterizedType *TypeRegistry::get_instantiation(const std::string &instantiated_name)
    {
        auto it = _instantiations.find(instantiated_name);
        return (it != _instantiations.end()) ? it->second.get() : nullptr;
    }

    bool TypeRegistry::has_template(const std::string &base_name) const
    {
        bool found = _templates.find(base_name) != _templates.end();
        LOG_DEBUG(Cryo::LogComponent::AST, "TypeRegistry::has_template('{}') - result: {}, templates size: {}", base_name, found, _templates.size());
        return found;
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
        // Trim whitespace from base name
        base_name.erase(0, base_name.find_first_not_of(" \t"));
        base_name.erase(base_name.find_last_not_of(" \t") + 1);

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
    // TypeContext Enhanced Implementation
    //===----------------------------------------------------------------------===//

    // FIXME: Temporarily disabled - method signature changed
    /*
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
    */

    std::shared_ptr<EnumType> TypeContext::get_parameterized_enum_template(const std::string &base_name)
    {
        auto it = _parameterized_enum_templates.find(base_name);
        return (it != _parameterized_enum_templates.end()) ? it->second : nullptr;
    }

    // FIXME: Temporarily disabled - return type changed
    /*
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
    */

    // FIXME: Temporarily disabled - method not in header
    /*
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
    */

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