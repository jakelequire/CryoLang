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
    class EnumType;
    class ParameterizedType;
    class TemplateRegistry;

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
        Parameterized, // For types like Array<T>, Option<T>
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
        enum class ConversionSafety
        {
            Safe,      // No warnings, implicit conversion allowed
            Warning,   // Emit compiler warning, explicit conversion required
            Unsafe,    // Require explicit cast or try_into()
            Impossible // No conversion possible
        };

        virtual ConversionSafety get_conversion_safety(const Type &target) const;
        virtual bool allows_implicit_conversion_to(const Type &target) const;
        virtual bool allows_explicit_conversion_to(const Type &target) const;

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
        ConversionSafety get_conversion_safety(const Type &target) const override;

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
        bool is_assignable_from(const Type &other) const override;
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

    // Tuple type for multiple values
    class TupleType : public Type
    {
    private:
        std::vector<std::shared_ptr<Type>> _element_types;

    public:
        TupleType(std::vector<std::shared_ptr<Type>> element_types)
            : Type(TypeKind::Tuple), _element_types(std::move(element_types))
        {
            // Build name like (int, string, bool)
            _name = "(";
            for (size_t i = 0; i < _element_types.size(); ++i)
            {
                if (i > 0)
                    _name += ", ";
                _name += _element_types[i]->name();
            }
            _name += ")";
        }

        const std::vector<std::shared_ptr<Type>> &element_types() const { return _element_types; }
        size_t element_count() const { return _element_types.size(); }
        std::shared_ptr<Type> element_type(size_t index) const
        {
            return index < _element_types.size() ? _element_types[index] : nullptr;
        }

        bool is_value_type() const override { return true; }
        size_t size_bytes() const override;
        size_t alignment() const override;
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

        size_t size_bytes() const override { return sizeof(void *); } // Pointer size
        size_t alignment() const override { return sizeof(void *); }
        std::string to_string() const override { return "null"; }
    };

    // Variadic parameter type (...)
    class VariadicType : public Type
    {
    public:
        VariadicType() : Type(TypeKind::Variadic, "...") {}

        // Variadic types represent a parameter pack, so they have special handling
        bool is_assignable_from(const Type &other) const override { return false; } // No direct assignment
        bool is_convertible_to(const Type &other) const override { return false; }  // No direct conversion

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

    // Parameterized type (e.g., Array<T>, Option<T>)
    class ParameterizedType : public Type
    {
    private:
        std::string _base_name;                          // "Array", "Option", etc.
        std::vector<std::shared_ptr<Type>> _type_params; // [T, U, ...]
        std::vector<std::string> _param_names;           // ["T", "U", ...] for template params
        mutable std::string _cached_instantiated_name;   // "Array<int>", "Option<string>"
        std::shared_ptr<EnumType> _base_enum_type;       // If this is an enum parameterization

    public:
        ParameterizedType(const std::string &base_name,
                          const std::vector<std::string> &param_names)
            : Type(TypeKind::Parameterized, base_name),
              _base_name(base_name), _param_names(param_names) {}

        ParameterizedType(const std::string &base_name,
                          const std::vector<std::shared_ptr<Type>> &type_params)
            : Type(TypeKind::Parameterized, base_name),
              _base_name(base_name), _type_params(type_params) {}

        // Accessors
        const std::string &base_name() const { return _base_name; }
        const std::vector<std::shared_ptr<Type>> &type_parameters() const { return _type_params; }
        const std::vector<std::string> &parameter_names() const { return _param_names; }
        size_t parameter_count() const { return std::max(_type_params.size(), _param_names.size()); }

        // Check if this is a template (has parameter names) vs instantiation (has concrete types)
        bool is_template() const { return !_param_names.empty() && _type_params.empty(); }
        bool is_instantiation() const { return !_type_params.empty(); }

        // Enum-specific methods
        void set_base_enum_type(std::shared_ptr<EnumType> enum_type) { _base_enum_type = enum_type; }
        std::shared_ptr<EnumType> get_base_enum_type() const { return _base_enum_type; }
        bool is_enum_instantiation() const { return _base_enum_type != nullptr; }

        // Layout information for enum instantiations
        struct FieldLayout
        {
            std::string name;
            std::shared_ptr<Type> type;
            size_t offset;
            size_t size;
        };

        // Get field layout for instantiated enum types
        std::vector<FieldLayout> get_enum_field_layout() const;

        // Query specific layout patterns
        bool is_optional_like() const;
        bool is_result_like() const;

        // Get discriminant information for enum types
        bool has_discriminant() const;
        size_t get_discriminant_size() const;
        size_t get_discriminant_offset() const { return 0; } // Usually at offset 0

        // Get data field information
        std::vector<std::shared_ptr<Type>> get_data_field_types() const;
        size_t get_data_offset() const; // Offset where data starts after discriminant

        // Get instantiated name like "Array<int>"
        std::string get_instantiated_name() const;

        // Create an instantiation of this parameterized type
        std::shared_ptr<ParameterizedType> instantiate(const std::vector<std::shared_ptr<Type>> &concrete_types) const;

        // New methods for migration from string-based operations
        std::string get_mangled_name() const;
        std::shared_ptr<ParameterizedType> substitute(const std::unordered_map<std::string, std::shared_ptr<Type>> &substitutions) const;
        bool has_type_parameter(const std::string &param_name) const;

        // Type compatibility - only compatible with other parameterized types with same base
        bool is_assignable_from(const Type &other) const override;
        bool is_convertible_to(const Type &other) const override;

        // Size is only known for instantiated types
        size_t size_bytes() const override;
        size_t alignment() const override;
        std::string to_string() const override;
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

    // Enum variant metadata for complex enums
    struct EnumVariant
    {
        std::string name;
        std::vector<std::shared_ptr<Type>> field_types; // Types of the fields in this variant
        std::vector<std::string> field_names;           // Names of the fields (optional)
        bool has_data = false;                          // Whether this variant carries data

        EnumVariant(const std::string &variant_name)
            : name(variant_name), has_data(false) {}

        EnumVariant(const std::string &variant_name,
                    const std::vector<std::shared_ptr<Type>> &types,
                    const std::vector<std::string> &names = {})
            : name(variant_name), field_types(types), field_names(names), has_data(!types.empty()) {}
    };

    // Enum layout specification for parameterized enums
    struct EnumLayout
    {
        bool uses_tag = true;                                       // Whether enum uses discriminant tag
        size_t tag_size = sizeof(int);                              // Size of discriminant tag
        std::vector<std::shared_ptr<Type>> common_fields;           // Fields present in all variants
        std::unordered_map<std::string, size_t> variant_data_sizes; // Max data size per variant
        size_t total_size = 0;                                      // Total enum size
        size_t alignment = sizeof(void *);                          // Enum alignment

        // Layout patterns for common enum types
        enum class LayoutPattern
        {
            SimpleEnum,   // C-style enum (just discriminant)
            TaggedUnion,  // Rust-style enum (tag + union)
            OptionalType, // Option<T> pattern (bool + T)
            ResultType,   // Result<T,E> pattern (bool + union)
            Custom        // User-defined layout
        };
        LayoutPattern pattern = LayoutPattern::Custom;
    };

    // Enum type (user-defined)
    class EnumType : public Type
    {
    private:
        std::vector<std::string> _variants;         // Simple variant names (legacy)
        std::vector<EnumVariant> _variant_metadata; // Rich variant information
        bool _is_simple_enum;                       // true for C-style, false for Rust-style
        bool _is_parameterized;                     // true if this enum takes type parameters
        std::vector<std::string> _type_parameters;  // Parameter names like ["T", "E"]
        std::unique_ptr<EnumLayout> _layout;        // Layout specification

    public:
        // Legacy constructor for simple enums
        EnumType(const std::string &name, std::vector<std::string> variants, bool is_simple = true)
            : Type(TypeKind::Enum, name), _variants(std::move(variants)), _is_simple_enum(is_simple),
              _is_parameterized(false), _layout(std::make_unique<EnumLayout>())
        {
            _layout->pattern = is_simple ? EnumLayout::LayoutPattern::SimpleEnum : EnumLayout::LayoutPattern::TaggedUnion;
        }

        // Enhanced constructor for complex/parameterized enums
        EnumType(const std::string &name,
                 std::vector<EnumVariant> variants,
                 const std::vector<std::string> &type_params = {},
                 std::unique_ptr<EnumLayout> layout = nullptr)
            : Type(TypeKind::Enum, name), _variant_metadata(std::move(variants)),
              _is_simple_enum(false), _is_parameterized(!type_params.empty()),
              _type_parameters(type_params), _layout(std::move(layout))
        {
            if (!_layout)
            {
                _layout = std::make_unique<EnumLayout>();
                _layout->pattern = _is_parameterized ? EnumLayout::LayoutPattern::Custom : EnumLayout::LayoutPattern::TaggedUnion;
            }

            // Build simple variant names for compatibility
            for (const auto &variant : _variant_metadata)
            {
                _variants.push_back(variant.name);
            }
        }

        bool is_primitive() const override { return _is_simple_enum; }
        bool is_value_type() const override { return true; }
        bool is_parameterized() const { return _is_parameterized; }

        size_t size_bytes() const override
        {
            if (_layout && _layout->total_size > 0)
            {
                return _layout->total_size;
            }
            return _is_simple_enum ? sizeof(int) : sizeof(void *);
        }

        size_t alignment() const override
        {
            if (_layout)
            {
                return _layout->alignment;
            }
            return _is_simple_enum ? sizeof(int) : sizeof(void *);
        }

        std::string to_string() const override { return _name; }

        // Legacy interface
        const std::vector<std::string> &variants() const { return _variants; }
        bool is_simple_enum() const { return _is_simple_enum; }
        bool has_variant(const std::string &variant_name) const
        {
            return std::find(_variants.begin(), _variants.end(), variant_name) != _variants.end();
        }

        // Enhanced interface
        const std::vector<EnumVariant> &get_variant_metadata() const { return _variant_metadata; }
        const std::vector<std::string> &get_type_parameters() const { return _type_parameters; }
        const EnumLayout *get_layout() const { return _layout.get(); }

        // Variant query methods
        const EnumVariant *get_variant_info(const std::string &variant_name) const
        {
            auto it = std::find_if(_variant_metadata.begin(), _variant_metadata.end(),
                                   [&variant_name](const EnumVariant &v)
                                   { return v.name == variant_name; });
            return it != _variant_metadata.end() ? &(*it) : nullptr;
        }

        bool variant_has_data(const std::string &variant_name) const
        {
            const auto *variant = get_variant_info(variant_name);
            return variant && variant->has_data;
        }

        std::vector<std::shared_ptr<Type>> get_variant_field_types(const std::string &variant_name) const
        {
            const auto *variant = get_variant_info(variant_name);
            return variant ? variant->field_types : std::vector<std::shared_ptr<Type>>{};
        }

        // Layout pattern queries
        bool is_optional_pattern() const
        {
            return _layout && _layout->pattern == EnumLayout::LayoutPattern::OptionalType;
        }

        bool is_result_pattern() const
        {
            return _layout && _layout->pattern == EnumLayout::LayoutPattern::ResultType;
        }

        bool uses_discriminant_tag() const
        {
            return _layout && _layout->uses_tag;
        }

        size_t get_discriminant_size() const
        {
            return _layout ? _layout->tag_size : sizeof(int);
        }

        // Create layout information for parameterized enum instantiation
        std::unique_ptr<EnumLayout> create_instantiated_layout(const std::vector<std::shared_ptr<Type>> &concrete_types) const;
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
        bool is_nullable() const override { return _target_type ? _target_type->is_nullable() : false; }
        bool is_assignable_from(const Type &other) const override { return _target_type ? _target_type->is_assignable_from(other) : false; }
        bool is_convertible_to(const Type &other) const override { return _target_type ? _target_type->is_convertible_to(other) : false; }
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

        // Parameterized enum registry
        std::unordered_map<std::string, std::shared_ptr<EnumType>> _parameterized_enum_templates;
        std::unordered_map<std::string, std::unique_ptr<EnumLayout>> _enum_layout_templates;

        // Global template registry access for AST-based analysis
        TemplateRegistry *_global_template_registry = nullptr;

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

        // Get unsigned integer types
        Type *get_u8_type() { return get_integer_type(IntegerKind::U8, false); }
        Type *get_u16_type() { return get_integer_type(IntegerKind::U16, false); }
        Type *get_u32_type() { return get_integer_type(IntegerKind::U32, false); }
        Type *get_u64_type() { return get_integer_type(IntegerKind::U64, false); }

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
        Type *create_tuple_type(const std::vector<Type *> &element_types);
        Type *create_function_type(Type *return_type, std::vector<Type *> param_types, bool is_variadic = false);

        // Create user-defined types
        Type *get_struct_type(const std::string &name);
        Type *get_class_type(const std::string &name);
        Type *get_trait_type(const std::string &name);
        Type *get_enum_type(const std::string &name, std::vector<std::string> variants, bool is_simple);
        Type *lookup_enum_type(const std::string &name); // Lookup existing enum type only
        Type *get_generic_type(const std::string &name);

        // Parameterized types
        ParameterizedType *create_parameterized_type(const std::string &base_name,
                                                     const std::vector<std::string> &param_names);
        ParameterizedType *instantiate_parameterized_type(const std::string &base_name,
                                                          const std::vector<Type *> &concrete_types);

        // Create type aliases
        Type *create_type_alias(const std::string &alias_name, Type *target_type);

        // Enum metadata registry
        void register_parameterized_enum(const std::string &base_name,
                                         const std::vector<std::string> &type_params,
                                         std::unique_ptr<EnumLayout> layout_template);

        std::shared_ptr<EnumType> get_parameterized_enum_template(const std::string &base_name);

        // Template registry access for AST-based analysis
        void set_global_template_registry(TemplateRegistry *registry) { _global_template_registry = registry; }
        TemplateRegistry *get_global_template_registry() const { return _global_template_registry; }

        std::shared_ptr<ParameterizedType> instantiate_parameterized_enum(
            const std::string &base_name,
            const std::vector<Type *> &concrete_types);

        // Query enum layout patterns without hard-coding names
        bool is_optional_pattern_enum(const std::string &base_name) const;
        bool is_result_pattern_enum(const std::string &base_name) const;

        // Get enum field layout generically
        std::vector<std::shared_ptr<Type>> get_enum_field_types(
            const std::string &base_name,
            const std::vector<Type *> &concrete_types) const;

        // Type parsing utilities
        std::string normalize_generic_type_string(const std::string &type_str);
        Type *parse_type_from_string(const std::string &type_str);
        Type *parse_function_type_from_string(const std::string &type_str);
        Type *resolve_type_from_token_kind(int token_kind); // From your TokenKind enum

        // Type alias resolution
        Type *lookup_type_alias(const std::string &alias_name);
        std::string resolve_parameterized_type_alias(const std::string &base_name,
                                                     const std::string &type_args_str);

        // Type compatibility and conversion
        bool are_types_compatible(Type *lhs, Type *rhs);
        Type *get_common_type(Type *lhs, Type *rhs);

        // New methods for migration from string-based operations
        ParameterizedType *instantiate_generic(const std::string &base_name,
                                               const std::vector<Type *> &args);
        Type *resolve_scoped_type(const std::string &scope, const std::string &type_name);

    private:
        // Helper to register built-in parameterized enums
        void register_builtin_parameterized_enums();
    };

    // Type Registry for managing parameterized types and instantiations
    class TypeRegistry
    {
    private:
        // Template definitions: "Array" -> ParameterizedType with param names ["T"]
        std::unordered_map<std::string, std::shared_ptr<ParameterizedType>> _templates;

        // Instantiations: "Array<int>" -> concrete ParameterizedType with [IntType]
        std::unordered_map<std::string, std::shared_ptr<ParameterizedType>> _instantiations;

        // Type context for creating concrete types
        TypeContext *_type_context;

    public:
        TypeRegistry(TypeContext *context) : _type_context(context) {}

        // Register a parameterized type template (e.g., Array<T>)
        void register_template(const std::string &base_name,
                               const std::vector<std::string> &param_names);

        // Get a template by base name
        ParameterizedType *get_template(const std::string &base_name);

        // Create an instantiation (Array<int>) from template (Array<T>)
        ParameterizedType *instantiate(const std::string &base_name,
                                       const std::vector<Type *> &concrete_types);

        // Parse type string like "Array<int>" and return instantiation
        ParameterizedType *parse_and_instantiate(const std::string &type_string);

        // Get existing instantiation if it exists
        ParameterizedType *get_instantiation(const std::string &instantiated_name);

        // Check if a base name has a registered template
        bool has_template(const std::string &base_name) const;

        // Utility to parse generic type syntax: "Array<int, string>" -> ("Array", ["int", "string"])
        std::pair<std::string, std::vector<std::string>> parse_generic_syntax(const std::string &type_string);
    };
}