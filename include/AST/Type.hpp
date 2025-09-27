#pragma once
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>

namespace Cryo
{
    // Forward declarations
    class Type;
    class TypeContext;

    // Type system enums and constants
    enum class TypeKind
    {
        // Primitive types
        Void,
        Boolean,
        Integer,
        Float,
        Char,
        String,

        // Compound types
        Array,
        Pointer,
        Reference,
        Function,

        // User-defined types
        Struct,
        Class,
        Interface,
        Trait,
        Enum,
        Union,

        // Advanced types
        Generic,
        Optional,
        Tuple,
        TypeAlias,

        // Special types
        Auto,    // For type inference
        Unknown, // Error recovery
        Never,   // Bottom type (functions that never return)
        Null,    // Null pointer type
        Variadic // Variadic parameter type (...)
    };

    // Integer type specifications
    enum class IntegerKind
    {
        I8,
        I16,
        I32,
        I64,
        I128,
        U8,
        U16,
        U32,
        U64,
        U128,
        Int, // Default signed integer
        UInt // Default unsigned integer
    };

    // Float type specifications
    enum class FloatKind
    {
        F32,
        F64,
        Float // Default float type
    };

    // Type qualifiers and modifiers
    struct TypeQualifiers
    {
        bool is_const = false;
        bool is_mutable = false;
        bool is_volatile = false;
        bool is_static = false;
        bool is_readonly = false;
    };

    // Base Type class - all types inherit from this
    class Type
    {
    protected:
        TypeKind _kind;
        std::string _name;
        TypeQualifiers _qualifiers;
        mutable std::string _cached_string; // For toString() caching

    public:
        Type(TypeKind kind, const std::string &name = "")
            : _kind(kind), _name(name) {}

        virtual ~Type() = default;

        // Core type information
        TypeKind kind() const { return _kind; }
        const std::string &name() const { return _name; }
        const TypeQualifiers &qualifiers() const { return _qualifiers; }

        // Type comparison and compatibility
        virtual bool equals(const Type &other) const;
        virtual bool is_assignable_from(const Type &other) const;
        virtual bool is_convertible_to(const Type &other) const;

        // Type properties
        virtual bool is_primitive() const { return false; }
        virtual bool is_numeric() const { return false; }
        virtual bool is_integral() const { return false; }
        virtual bool is_floating_point() const { return false; }
        virtual bool is_signed() const { return false; }
        virtual bool is_unsigned() const { return false; }
        virtual bool is_void() const { return _kind == TypeKind::Void; }
        virtual bool is_reference_type() const { return false; }
        virtual bool is_value_type() const { return true; }
        virtual bool is_nullable() const { return false; }

        // Type conversion safety checking
        enum class ConversionSafety {
            Safe,           // No warnings, implicit conversion allowed
            Warning,        // Emit compiler warning, explicit conversion required
            Unsafe,         // Require explicit cast or try_into()
            Impossible      // No conversion possible
        };
        
        virtual ConversionSafety get_conversion_safety(const Type& target) const;
        virtual bool allows_implicit_conversion_to(const Type& target) const;
        virtual bool allows_explicit_conversion_to(const Type& target) const;

        // Size and alignment (for code generation)
        virtual size_t size_bytes() const = 0;
        virtual size_t alignment() const = 0;

        // String representation
        virtual std::string to_string() const;
        virtual std::string mangle() const; // For name mangling

        // Qualifier manipulation
        void set_qualifiers(const TypeQualifiers &quals) { _qualifiers = quals; }
        Type *with_qualifiers(const TypeQualifiers &quals) const;

        // Type system utilities
        static bool are_compatible(const Type &lhs, const Type &rhs);
        static std::shared_ptr<Type> get_common_type(const Type &lhs, const Type &rhs);
    };

    // Void type
    class VoidType : public Type
    {
    public:
        VoidType() : Type(TypeKind::Void, "void") {}

        bool is_void() const override { return true; }
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "void"; }
    };

    // Boolean type
    class BooleanType : public Type
    {
    public:
        BooleanType() : Type(TypeKind::Boolean, "boolean") {}

        bool is_primitive() const override { return true; }
        size_t size_bytes() const override { return 1; }
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "boolean"; }
    };

    // Integer types
    class IntegerType : public Type
    {
    private:
        IntegerKind _int_kind;
        bool _is_signed;

    public:
        IntegerType(IntegerKind kind, bool is_signed = true)
            : Type(TypeKind::Integer), _int_kind(kind), _is_signed(is_signed)
        {
            _name = get_integer_name(kind, is_signed);
        }

        bool is_primitive() const override { return true; }
        bool is_numeric() const override { return true; }
        bool is_integral() const override { return true; }
        bool is_signed() const override { return _is_signed; }
        bool is_unsigned() const override { return !_is_signed; }

        IntegerKind integer_kind() const { return _int_kind; }

        // Override conversion safety for intelligent integer conversion
        ConversionSafety get_conversion_safety(const Type& target) const override;

        size_t size_bytes() const override;
        size_t alignment() const override;
        std::string to_string() const override { return _name; }

    private:
        static std::string get_integer_name(IntegerKind kind, bool is_signed);
    };

    // Float types
    class FloatType : public Type
    {
    private:
        FloatKind _float_kind;

    public:
        FloatType(FloatKind kind) : Type(TypeKind::Float), _float_kind(kind)
        {
            _name = get_float_name(kind);
        }

        bool is_primitive() const override { return true; }
        bool is_numeric() const override { return true; }
        bool is_floating_point() const override { return true; }
        bool is_signed() const override { return true; }

        FloatKind float_kind() const { return _float_kind; }

        size_t size_bytes() const override;
        size_t alignment() const override;
        std::string to_string() const override { return _name; }

    private:
        static std::string get_float_name(FloatKind kind);
    };

    // Character type
    class CharType : public Type
    {
    public:
        CharType() : Type(TypeKind::Char, "char") {}

        bool is_primitive() const override { return true; }
        bool is_integral() const override { return true; }
        bool is_unsigned() const override { return true; }
        size_t size_bytes() const override { return 1; }
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "char"; }
    };

    // String type
    class StringType : public Type
    {
    public:
        StringType() : Type(TypeKind::String, "string") {}

        bool is_reference_type() const override { return true; }
        bool is_value_type() const override { return false; }
        size_t size_bytes() const override { return sizeof(void *); } // Pointer size
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override { return "string"; }
    };

    // Array type
    class ArrayType : public Type
    {
    private:
        std::shared_ptr<Type> _element_type;
        std::optional<size_t> _size; // None for dynamic arrays

    public:
        ArrayType(std::shared_ptr<Type> element_type, std::optional<size_t> size = std::nullopt)
            : Type(TypeKind::Array), _element_type(element_type), _size(size)
        {
            _name = element_type->name() + "[]";
        }

        std::shared_ptr<Type> element_type() const { return _element_type; }
        std::optional<size_t> array_size() const { return _size; }
        bool is_dynamic() const { return !_size.has_value(); }

        bool is_reference_type() const override { return is_dynamic(); }
        bool is_value_type() const override { return !is_dynamic(); }

        size_t size_bytes() const override;
        size_t alignment() const override { return _element_type->alignment(); }
        std::string to_string() const override;

        bool equals(const Type &other) const override;
    };

    // Reference type
    class ReferenceType : public Type
    {
    private:
        std::shared_ptr<Type> _referent_type;

    public:
        ReferenceType(std::shared_ptr<Type> referent_type)
            : Type(TypeKind::Reference), _referent_type(referent_type)
        {
            _name = "&" + referent_type->name();
        }

        std::shared_ptr<Type> referent_type() const { return _referent_type; }

        bool is_reference_type() const override { return true; }
        bool is_value_type() const override { return false; }
        bool is_nullable() const override { return false; } // References cannot be null

        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override;

        bool equals(const Type &other) const override;
        bool is_assignable_from(const Type &other) const override;
    };

    // Pointer type
    class PointerType : public Type
    {
    private:
        std::shared_ptr<Type> _pointee_type;

    public:
        PointerType(std::shared_ptr<Type> pointee_type)
            : Type(TypeKind::Pointer), _pointee_type(pointee_type)
        {
            _name = pointee_type->name() + "*";
        }

        std::shared_ptr<Type> pointee_type() const { return _pointee_type; }

        bool is_reference_type() const override { return true; }
        bool is_value_type() const override { return false; }
        bool is_nullable() const override { return true; }

        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override;

        bool equals(const Type &other) const override;
    };

    // Function type
    class FunctionType : public Type
    {
    private:
        std::shared_ptr<Type> _return_type;
        std::vector<std::shared_ptr<Type>> _parameter_types;
        bool _is_variadic = false;

    public:
        FunctionType(std::shared_ptr<Type> return_type,
                     std::vector<std::shared_ptr<Type>> parameter_types,
                     bool is_variadic = false)
            : Type(TypeKind::Function), _return_type(return_type),
              _parameter_types(parameter_types), _is_variadic(is_variadic)
        {
            _name = build_function_name();
        }

        std::shared_ptr<Type> return_type() const { return _return_type; }
        const std::vector<std::shared_ptr<Type>> &parameter_types() const { return _parameter_types; }
        bool is_variadic() const { return _is_variadic; }

        size_t parameter_count() const { return _parameter_types.size(); }

        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override { return _name; }

        bool equals(const Type &other) const override;

    private:
        std::string build_function_name();
    };

    // Optional type (nullable)
    class OptionalType : public Type
    {
    private:
        std::shared_ptr<Type> _wrapped_type;

    public:
        OptionalType(std::shared_ptr<Type> wrapped_type)
            : Type(TypeKind::Optional), _wrapped_type(wrapped_type)
        {
            _name = wrapped_type->name() + "?";
        }

        std::shared_ptr<Type> wrapped_type() const { return _wrapped_type; }

        bool is_nullable() const override { return true; }
        bool is_reference_type() const override { return true; }
        bool is_value_type() const override { return false; }

        size_t size_bytes() const override;
        size_t alignment() const override { return _wrapped_type->alignment(); }
        std::string to_string() const override { return _name; }

        bool equals(const Type &other) const override;
    };

    // Auto type (for type inference)
    class AutoType : public Type
    {
    public:
        AutoType() : Type(TypeKind::Auto, "auto") {}

        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "auto"; }
    };

    // Unknown type (error recovery)
    class UnknownType : public Type
    {
    public:
        UnknownType() : Type(TypeKind::Unknown, "unknown") {}

        bool is_assignable_from(const Type &other) const override { return true; }
        bool is_convertible_to(const Type &other) const override { return true; }

        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "unknown"; }
    };

    // Null type (null pointer literal)
    class NullType : public Type
    {
    public:
        NullType() : Type(TypeKind::Null, "null") {}

        bool is_nullable() const override { return true; }
        bool is_assignable_from(const Type &other) const override { return other.is_nullable(); }
        bool is_convertible_to(const Type &other) const override { return other.is_nullable(); }

        size_t size_bytes() const override { return sizeof(void*); } // Pointer size
        size_t alignment() const override { return sizeof(void*); }
        std::string to_string() const override { return "null"; }
    };

    // Variadic parameter type (...)
    class VariadicType : public Type
    {
    public:
        VariadicType() : Type(TypeKind::Variadic, "...") {}

        // Variadic types represent a parameter pack, so they have special handling
        bool is_assignable_from(const Type &other) const override { return false; } // No direct assignment
        bool is_convertible_to(const Type &other) const override { return false; } // No direct conversion

        size_t size_bytes() const override { return 0; } // No size as it's not a concrete type
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return "..."; }
    };

    // Generic type parameter
    class GenericType : public Type
    {
    public:
        GenericType(const std::string &name) : Type(TypeKind::Generic, name) {}

        // Generic types are placeholders, so they act like unknown types for compatibility
        bool is_assignable_from(const Type &other) const override { return true; }
        bool is_convertible_to(const Type &other) const override { return true; }

        size_t size_bytes() const override { return 0; } // Size unknown until instantiation
        size_t alignment() const override { return 1; }  // Alignment unknown until instantiation
        std::string to_string() const override { return name(); }
    };

    // Struct type (user-defined)
    class StructType : public Type
    {
    private:
        mutable std::optional<size_t> _cached_size;

    public:
        StructType(const std::string &name) : Type(TypeKind::Struct, name) {}

        bool is_primitive() const override { return false; }
        bool is_value_type() const override { return true; }
        size_t size_bytes() const override;
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override { return _name; }
    };

    // Class type (user-defined)
    class ClassType : public Type
    {
    private:
        mutable std::optional<size_t> _cached_size;

    public:
        ClassType(const std::string &name) : Type(TypeKind::Class, name) {}

        bool is_primitive() const override { return false; }
        bool is_reference_type() const override { return true; }
        size_t size_bytes() const override;
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override { return _name; }
    };

    // Trait type (user-defined)
    class TraitType : public Type
    {
    public:
        TraitType(const std::string &name) : Type(TypeKind::Trait, name) {}

        bool is_primitive() const override { return false; }
        bool is_reference_type() const override { return false; }
        size_t size_bytes() const override { return 0; } // Traits have no runtime representation
        size_t alignment() const override { return 1; }
        std::string to_string() const override { return _name; }
    };

    // Enum type (user-defined)
    class EnumType : public Type
    {
    private:
        std::vector<std::string> _variants;
        bool _is_simple_enum; // true for C-style, false for Rust-style

    public:
        EnumType(const std::string &name, std::vector<std::string> variants, bool is_simple = true)
            : Type(TypeKind::Enum, name), _variants(std::move(variants)), _is_simple_enum(is_simple) {}

        bool is_primitive() const override { return _is_simple_enum; }
        bool is_value_type() const override { return true; }
        size_t size_bytes() const override
        {
            return _is_simple_enum ? sizeof(int) : sizeof(void *); // Simple enums are ints, complex ones are tagged unions
        }
        size_t alignment() const override { return _is_simple_enum ? sizeof(int) : sizeof(void *); }
        std::string to_string() const override { return _name; }

        const std::vector<std::string> &variants() const { return _variants; }
        bool is_simple_enum() const { return _is_simple_enum; }

        bool has_variant(const std::string &variant_name) const
        {
            return std::find(_variants.begin(), _variants.end(), variant_name) != _variants.end();
        }
    };

    // Type alias (user-defined aliases for existing types)
    class TypeAlias : public Type
    {
    private:
        Type *_target_type; // The actual type this alias refers to

    public:
        TypeAlias(const std::string &alias_name, Type *target_type)
            : Type(TypeKind::TypeAlias, alias_name), _target_type(target_type) {}

        bool is_primitive() const override { return _target_type ? _target_type->is_primitive() : false; }
        bool is_value_type() const override { return _target_type ? _target_type->is_value_type() : false; }
        bool is_reference_type() const override { return _target_type ? _target_type->is_reference_type() : false; }
        size_t size_bytes() const override { return _target_type ? _target_type->size_bytes() : 0; }
        size_t alignment() const override { return _target_type ? _target_type->alignment() : 1; }
        std::string to_string() const override { return _name; } // Return alias name, not target

        Type *target_type() const { return _target_type; }
        void set_target_type(Type *target) { _target_type = target; } // For forward declarations
    };

    // Type factory and context for managing types
    class TypeContext
    {
    private:
        // Built-in type instances (singletons)
        std::unique_ptr<VoidType> _void_type;
        std::unique_ptr<BooleanType> _boolean_type;
        std::unique_ptr<CharType> _char_type;
        std::unique_ptr<StringType> _string_type;
        std::unique_ptr<AutoType> _auto_type;
        std::unique_ptr<UnknownType> _unknown_type;
        std::unique_ptr<NullType> _null_type;
        std::unique_ptr<VariadicType> _variadic_type;

        // Integer type cache
        std::unordered_map<int, std::unique_ptr<IntegerType>> _integer_types;
        std::unordered_map<int, std::unique_ptr<FloatType>> _float_types;

        // User-defined type cache
        std::unordered_map<std::string, std::unique_ptr<StructType>> _struct_types;
        std::unordered_map<std::string, std::unique_ptr<ClassType>> _class_types;
        std::unordered_map<std::string, std::unique_ptr<TraitType>> _trait_types;
        std::unordered_map<std::string, std::unique_ptr<EnumType>> _enum_types;
        std::unordered_map<std::string, std::unique_ptr<Type>> _generic_types;

        // Complex type cache
        std::vector<std::unique_ptr<Type>> _complex_types;

    public:
        TypeContext();
        ~TypeContext() = default;

        // Get built-in types
        Type *get_void_type() { return _void_type.get(); }
        Type *get_boolean_type() { return _boolean_type.get(); }
        Type *get_char_type() { return _char_type.get(); }
        Type *get_string_type() { return _string_type.get(); }
        Type *get_auto_type() { return _auto_type.get(); }
        Type *get_unknown_type() { return _unknown_type.get(); }
        Type *get_null_type() { return _null_type.get(); }
        Type *get_variadic_type() { return _variadic_type.get(); }

        // Get integer types
        Type *get_integer_type(IntegerKind kind, bool is_signed = true);
        Type *get_i8_type() { return get_integer_type(IntegerKind::I8); }
        Type *get_i16_type() { return get_integer_type(IntegerKind::I16); }
        Type *get_i32_type() { return get_integer_type(IntegerKind::I32); }
        Type *get_i64_type() { return get_integer_type(IntegerKind::I64); }
        Type *get_int_type() { return get_integer_type(IntegerKind::Int); }

        // Get float types
        Type *get_float_type(FloatKind kind);
        Type *get_f32_type() { return get_float_type(FloatKind::F32); }
        Type *get_f64_type() { return get_float_type(FloatKind::F64); }
        Type *get_default_float_type() { return get_float_type(FloatKind::Float); }

        // Create complex types
        Type *create_array_type(Type *element_type, std::optional<size_t> size = std::nullopt);
        Type *create_pointer_type(Type *pointee_type);
        Type *create_reference_type(Type *referent_type);
        Type *create_optional_type(Type *wrapped_type);
        Type *create_function_type(Type *return_type, std::vector<Type *> param_types, bool is_variadic = false);

        // Create user-defined types
        Type *get_struct_type(const std::string &name);
        Type *get_class_type(const std::string &name);
        Type *get_trait_type(const std::string &name);
        Type *get_enum_type(const std::string &name, std::vector<std::string> variants = {}, bool is_simple = true);
        Type *get_generic_type(const std::string &name);
        
        // Create type aliases
        Type *create_type_alias(const std::string &alias_name, Type *target_type);

        // Type parsing utilities
        std::string normalize_generic_type_string(const std::string &type_str);
        Type *parse_type_from_string(const std::string &type_str);
        Type *resolve_type_from_token_kind(int token_kind); // From your TokenKind enum

        // Type compatibility and conversion
        bool are_types_compatible(Type *lhs, Type *rhs);
        Type *get_common_type(Type *lhs, Type *rhs);
    };
}