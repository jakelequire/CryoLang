/******************************************************************************
 * @file TypeArena.cpp
 * @brief Implementation of TypeArena for Cryo's new type system
 ******************************************************************************/

#include "Types/TypeArena.hpp"
#include "Types/PrimitiveTypes.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/ErrorType.hpp"

#include <sstream>
#include <cassert>

namespace Cryo
{
    // ========================================================================
    // Constructor
    // ========================================================================

    TypeArena::TypeArena()
    {
        // Reserve some initial capacity
        _types.reserve(256);

        // Initialize primitive types
        initialize_primitives();
    }

    // ========================================================================
    // Type registration
    // ========================================================================

    TypeRef TypeArena::register_type(std::unique_ptr<Type> type)
    {
        TypeID id = type->id();
        size_t index = _types.size();

        _types.push_back(std::move(type));
        _id_to_index[id] = index;

        return TypeRef(id, this);
    }

    const Type *TypeArena::lookup(TypeID id) const
    {
        auto it = _id_to_index.find(id);
        if (it == _id_to_index.end())
            return nullptr;

        return _types[it->second].get();
    }

    TypeRef TypeArena::lookup_type_by_name(const std::string &name) const
    {
        // Try struct types
        auto struct_it = _struct_types.find(name);
        if (struct_it != _struct_types.end())
            return struct_it->second;

        // Try enum types
        auto enum_it = _enum_types.find(name);
        if (enum_it != _enum_types.end())
            return enum_it->second;

        // Try class types
        auto class_it = _class_types.find(name);
        if (class_it != _class_types.end())
            return class_it->second;

        // Try trait types
        auto trait_it = _trait_types.find(name);
        if (trait_it != _trait_types.end())
            return trait_it->second;

        return TypeRef{}; // Invalid/not found
    }

    // ========================================================================
    // Primitive type initialization
    // ========================================================================

    void TypeArena::initialize_primitives()
    {
        if (_primitives_initialized)
            return;

        // Void
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<VoidType>(id);
            _primitives.void_type = register_type(std::move(type));
        }

        // Bool
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<BoolType>(id);
            _primitives.bool_type = register_type(std::move(type));
        }

        // Char
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<CharType>(id);
            _primitives.char_type = register_type(std::move(type));
        }

        // String
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<StringType>(id);
            _primitives.string_type = register_type(std::move(type));
        }

        // Never
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<NeverType>(id);
            _primitives.never_type = register_type(std::move(type));
        }

        // Signed integers
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::I8);
            _primitives.i8_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::I16);
            _primitives.i16_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::I32);
            _primitives.i32_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::I64);
            _primitives.i64_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::I128);
            _primitives.i128_type = register_type(std::move(type));
        }

        // Unsigned integers
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::U8);
            _primitives.u8_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::U16);
            _primitives.u16_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::U32);
            _primitives.u32_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::U64);
            _primitives.u64_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<IntType>(id, IntegerKind::U128);
            _primitives.u128_type = register_type(std::move(type));
        }

        // Floats
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<FloatType>(id, FloatKind::F32);
            _primitives.f32_type = register_type(std::move(type));
        }
        {
            TypeID id = allocate_id();
            auto type = std::make_unique<FloatType>(id, FloatKind::F64);
            _primitives.f64_type = register_type(std::move(type));
        }

        _primitives_initialized = true;
    }

    // ========================================================================
    // Primitive type accessors
    // ========================================================================

    TypeRef TypeArena::get_void() { return _primitives.void_type; }
    TypeRef TypeArena::get_bool() { return _primitives.bool_type; }
    TypeRef TypeArena::get_char() { return _primitives.char_type; }
    TypeRef TypeArena::get_string() { return _primitives.string_type; }
    TypeRef TypeArena::get_never() { return _primitives.never_type; }

    TypeRef TypeArena::get_i8() { return _primitives.i8_type; }
    TypeRef TypeArena::get_i16() { return _primitives.i16_type; }
    TypeRef TypeArena::get_i32() { return _primitives.i32_type; }
    TypeRef TypeArena::get_i64() { return _primitives.i64_type; }
    TypeRef TypeArena::get_i128() { return _primitives.i128_type; }

    TypeRef TypeArena::get_u8() { return _primitives.u8_type; }
    TypeRef TypeArena::get_u16() { return _primitives.u16_type; }
    TypeRef TypeArena::get_u32() { return _primitives.u32_type; }
    TypeRef TypeArena::get_u64() { return _primitives.u64_type; }
    TypeRef TypeArena::get_u128() { return _primitives.u128_type; }

    TypeRef TypeArena::get_f32() { return _primitives.f32_type; }
    TypeRef TypeArena::get_f64() { return _primitives.f64_type; }

    TypeRef TypeArena::get_integer(IntegerKind kind)
    {
        switch (kind)
        {
        case IntegerKind::I8:
            return get_i8();
        case IntegerKind::I16:
            return get_i16();
        case IntegerKind::I32:
            return get_i32();
        case IntegerKind::I64:
            return get_i64();
        case IntegerKind::I128:
            return get_i128();
        case IntegerKind::U8:
            return get_u8();
        case IntegerKind::U16:
            return get_u16();
        case IntegerKind::U32:
            return get_u32();
        case IntegerKind::U64:
            return get_u64();
        case IntegerKind::U128:
            return get_u128();
        default:
            return TypeRef{}; // Invalid
        }
    }

    TypeRef TypeArena::get_float(FloatKind kind)
    {
        switch (kind)
        {
        case FloatKind::F32:
            return get_f32();
        case FloatKind::F64:
            return get_f64();
        default:
            return TypeRef{}; // Invalid
        }
    }

    // ========================================================================
    // Compound type creation (with deduplication)
    // ========================================================================

    TypeRef TypeArena::get_pointer_to(TypeRef pointee)
    {
        // If pointee is an error, propagate the error
        if (pointee.is_error())
        {
            return pointee;
        }

        // Check cache
        auto it = _pointer_types.find(pointee.id());
        if (it != _pointer_types.end())
        {
            return it->second;
        }

        // Create new pointer type
        TypeID id = allocate_id();
        auto type = std::make_unique<PointerType>(id, pointee);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _pointer_types[pointee.id()] = ref;
        return ref;
    }

    TypeRef TypeArena::get_reference_to(TypeRef referent, RefMutability mutability)
    {
        // If referent is an error, propagate the error
        if (referent.is_error())
        {
            return referent;
        }

        // Check cache
        RefKey key{referent.id(), mutability};
        auto it = _reference_types.find(key);
        if (it != _reference_types.end())
        {
            return it->second;
        }

        // Create new reference type
        TypeID id = allocate_id();
        auto type = std::make_unique<ReferenceType>(id, referent, mutability);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _reference_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::get_array_of(TypeRef element, std::optional<size_t> size)
    {
        // If element is an error, propagate the error
        if (element.is_error())
        {
            return element;
        }

        // Check cache
        ArrayKey key{element.id(), size};
        auto it = _array_types.find(key);
        if (it != _array_types.end())
        {
            return it->second;
        }

        // Create new array type
        TypeID id = allocate_id();
        auto type = std::make_unique<ArrayType>(id, element, size);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _array_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::get_optional_of(TypeRef wrapped)
    {
        // If wrapped is an error, propagate the error
        if (wrapped.is_error())
        {
            return wrapped;
        }

        // Check cache
        auto it = _optional_types.find(wrapped.id());
        if (it != _optional_types.end())
        {
            return it->second;
        }

        // Create new optional type
        TypeID id = allocate_id();
        auto type = std::make_unique<OptionalType>(id, wrapped);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _optional_types[wrapped.id()] = ref;
        return ref;
    }

    std::string TypeArena::make_function_key(TypeRef return_type,
                                              const std::vector<TypeRef> &params,
                                              bool is_variadic) const
    {
        std::ostringstream oss;
        oss << return_type.raw_id() << "(";
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            oss << params[i].raw_id();
        }
        if (is_variadic)
            oss << ",...";
        oss << ")";
        return oss.str();
    }

    TypeRef TypeArena::get_function(TypeRef return_type,
                                     std::vector<TypeRef> params,
                                     bool is_variadic)
    {
        // Check for errors in return type or params
        if (return_type.is_error())
        {
            return return_type;
        }
        for (const auto &param : params)
        {
            if (param.is_error())
            {
                return param;
            }
        }

        // Check cache
        std::string key = make_function_key(return_type, params, is_variadic);
        auto it = _function_types.find(key);
        if (it != _function_types.end())
        {
            return it->second;
        }

        // Create new function type
        TypeID id = allocate_id();
        auto type = std::make_unique<FunctionType>(id, return_type, std::move(params), is_variadic);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _function_types[key] = ref;
        return ref;
    }

    std::string TypeArena::make_tuple_key(const std::vector<TypeRef> &elements) const
    {
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < elements.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            oss << elements[i].raw_id();
        }
        oss << ")";
        return oss.str();
    }

    TypeRef TypeArena::get_tuple(std::vector<TypeRef> elements)
    {
        // Check for errors in elements
        for (const auto &elem : elements)
        {
            if (elem.is_error())
            {
                return elem;
            }
        }

        // Check cache
        std::string key = make_tuple_key(elements);
        auto it = _tuple_types.find(key);
        if (it != _tuple_types.end())
        {
            return it->second;
        }

        // Create new tuple type
        TypeID id = allocate_id();
        auto type = std::make_unique<TupleType>(id, std::move(elements));
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _tuple_types[key] = ref;
        return ref;
    }

    // ========================================================================
    // User-defined type creation (with deduplication)
    // ========================================================================

    std::string TypeArena::make_user_type_key(const QualifiedTypeName &name)
    {
        // Use module ID and name for uniqueness
        return std::to_string(name.module.id) + "::" + name.name;
    }

    TypeRef TypeArena::create_struct(const QualifiedTypeName &name)
    {
        // Check cache first for deduplication
        std::string key = make_user_type_key(name);
        auto it = _struct_types.find(key);
        if (it != _struct_types.end())
        {
            return it->second;
        }

        // Create new struct type
        TypeID id = allocate_id();
        auto type = std::make_unique<StructType>(id, name);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _struct_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::create_class(const QualifiedTypeName &name)
    {
        // Check cache first for deduplication
        std::string key = make_user_type_key(name);
        auto it = _class_types.find(key);
        if (it != _class_types.end())
        {
            return it->second;
        }

        // Create new class type
        TypeID id = allocate_id();
        auto type = std::make_unique<ClassType>(id, name);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _class_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::create_enum(const QualifiedTypeName &name)
    {
        // Check cache first for deduplication
        std::string key = make_user_type_key(name);
        auto it = _enum_types.find(key);
        if (it != _enum_types.end())
        {
            return it->second;
        }

        // Create new enum type
        TypeID id = allocate_id();
        auto type = std::make_unique<EnumType>(id, name);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _enum_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::create_trait(const QualifiedTypeName &name)
    {
        // Check cache first for deduplication
        std::string key = make_user_type_key(name);
        auto it = _trait_types.find(key);
        if (it != _trait_types.end())
        {
            return it->second;
        }

        // Create new trait type
        TypeID id = allocate_id();
        auto type = std::make_unique<TraitType>(id, name);
        TypeRef ref = register_type(std::move(type));

        // Cache and return
        _trait_types[key] = ref;
        return ref;
    }

    TypeRef TypeArena::create_type_alias(const QualifiedTypeName &name, TypeRef target)
    {
        TypeID id = allocate_id();
        auto type = std::make_unique<TypeAliasType>(id, name, target);
        return register_type(std::move(type));
    }

    // ========================================================================
    // Generic type support
    // ========================================================================

    TypeRef TypeArena::create_generic_param(const std::string &name, size_t index)
    {
        TypeID id = allocate_id();
        auto type = std::make_unique<GenericParamType>(id, name, index);
        return register_type(std::move(type));
    }

    TypeRef TypeArena::create_bounded_param(const std::string &name,
                                             size_t index,
                                             std::vector<TypeRef> bounds)
    {
        TypeID id = allocate_id();
        auto type = std::make_unique<BoundedParamType>(id, name, index, std::move(bounds));
        return register_type(std::move(type));
    }

    TypeRef TypeArena::create_instantiation(TypeRef generic_base,
                                             std::vector<TypeRef> type_args)
    {
        // Check for errors
        if (generic_base.is_error())
        {
            return generic_base;
        }
        for (const auto &arg : type_args)
        {
            if (arg.is_error())
            {
                return arg;
            }
        }

        TypeID id = allocate_id();
        auto type = std::make_unique<InstantiatedType>(id, generic_base, std::move(type_args));
        return register_type(std::move(type));
    }

    // ========================================================================
    // Error type creation
    // ========================================================================

    TypeRef TypeArena::create_error(const std::string &reason, SourceLocation location)
    {
        TypeID id = allocate_id();
        auto type = std::make_unique<ErrorType>(id, reason, location);
        return register_type(std::move(type));
    }

    TypeRef TypeArena::create_error(const std::string &reason,
                                     SourceLocation location,
                                     std::vector<std::string> notes)
    {
        TypeID id = allocate_id();
        auto type = std::make_unique<ErrorType>(id, reason, location, std::move(notes));
        return register_type(std::move(type));
    }

    // ========================================================================
    // Type base class implementations
    // ========================================================================

    std::string Type::debug_string() const
    {
        return "Type { id: " + std::to_string(_id.id) +
               ", kind: " + type_kind_to_string(_kind) +
               ", name: " + display_name() + " }";
    }

    std::string QualifiedTypeName::to_string() const
    {
        if (module.id == ModuleID::builtin().id)
        {
            return name;
        }
        return "module_" + std::to_string(module.id) + "::" + name;
    }

    // ========================================================================
    // IntType min/max value implementations
    // ========================================================================

    int64_t IntType::min_value() const
    {
        if (!is_signed())
            return 0;

        switch (_int_kind)
        {
        case IntegerKind::I8:
            return INT8_MIN;
        case IntegerKind::I16:
            return INT16_MIN;
        case IntegerKind::I32:
            return INT32_MIN;
        case IntegerKind::I64:
            return INT64_MIN;
        case IntegerKind::I128:
            return INT64_MIN; // Approximation for i128
        default:
            return 0;
        }
    }

    uint64_t IntType::max_value() const
    {
        switch (_int_kind)
        {
        case IntegerKind::I8:
            return INT8_MAX;
        case IntegerKind::I16:
            return INT16_MAX;
        case IntegerKind::I32:
            return INT32_MAX;
        case IntegerKind::I64:
            return INT64_MAX;
        case IntegerKind::I128:
            return INT64_MAX; // Approximation for i128
        case IntegerKind::U8:
            return UINT8_MAX;
        case IntegerKind::U16:
            return UINT16_MAX;
        case IntegerKind::U32:
            return UINT32_MAX;
        case IntegerKind::U64:
            return UINT64_MAX;
        case IntegerKind::U128:
            return UINT64_MAX; // Approximation for u128
        default:
            return 0;
        }
    }

    // ========================================================================
    // User-defined type implementations
    // ========================================================================

    void StructType::set_fields(std::vector<FieldInfo> fields)
    {
        _fields = std::move(fields);
        _is_complete = true;
        compute_layout();
    }

    void StructType::compute_layout()
    {
        size_t offset = 0;
        size_t max_align = 1;

        for (auto &field : _fields)
        {
            if (!field.type.is_valid())
                continue;

            size_t field_align = field.type->alignment();
            max_align = std::max(max_align, field_align);

            // Align offset for this field
            offset = (offset + field_align - 1) / field_align * field_align;
            field.offset = offset;
            offset += field.type->size_bytes();
        }

        // Final alignment for the struct
        _computed_alignment = max_align;
        _computed_size = (offset + max_align - 1) / max_align * max_align;
    }

    std::string StructType::mangled_name() const
    {
        return "S" + std::to_string(_qualified_name.module.id) + "_" + _qualified_name.name;
    }

    void ClassType::set_fields(std::vector<FieldInfo> fields)
    {
        _fields = std::move(fields);
        _is_complete = true;
        compute_layout();
    }

    void ClassType::add_method(MethodInfo method)
    {
        if (method.is_virtual)
        {
            _has_virtual_methods = true;
        }
        _methods.push_back(std::move(method));
    }

    void ClassType::set_methods(std::vector<MethodInfo> methods)
    {
        _methods = std::move(methods);
        for (const auto &m : _methods)
        {
            if (m.is_virtual)
            {
                _has_virtual_methods = true;
                break;
            }
        }
    }

    std::optional<size_t> ClassType::field_index(const std::string &name) const
    {
        for (size_t i = 0; i < _fields.size(); ++i)
        {
            if (_fields[i].name == name)
                return i;
        }
        return std::nullopt;
    }

    std::optional<TypeRef> ClassType::field_type(const std::string &name) const
    {
        auto idx = field_index(name);
        if (idx)
            return _fields[*idx].type;
        return std::nullopt;
    }

    const FieldInfo *ClassType::get_field(const std::string &name) const
    {
        auto idx = field_index(name);
        if (idx)
            return &_fields[*idx];
        return nullptr;
    }

    const MethodInfo *ClassType::get_method(const std::string &name) const
    {
        for (const auto &m : _methods)
        {
            if (m.name == name)
                return &m;
        }
        return nullptr;
    }

    void ClassType::compute_layout()
    {
        size_t offset = 0;
        size_t max_align = sizeof(void *); // At least pointer alignment

        // If has virtual methods, account for vtable pointer
        if (_has_virtual_methods || _base_class.is_valid())
        {
            offset = sizeof(void *);
        }

        // If has base class, include its size
        if (_base_class.is_valid())
        {
            offset = _base_class->size_bytes();
            max_align = std::max(max_align, _base_class->alignment());
        }

        for (auto &field : _fields)
        {
            if (!field.type.is_valid())
                continue;

            size_t field_align = field.type->alignment();
            max_align = std::max(max_align, field_align);

            // Align offset for this field
            offset = (offset + field_align - 1) / field_align * field_align;
            field.offset = offset;
            offset += field.type->size_bytes();
        }

        _computed_alignment = max_align;
        _computed_size = (offset + max_align - 1) / max_align * max_align;
    }

    std::string ClassType::mangled_name() const
    {
        return "C" + std::to_string(_qualified_name.module.id) + "_" + _qualified_name.name;
    }

    void EnumType::set_variants(std::vector<EnumVariant> variants)
    {
        _variants = std::move(variants);

        // Assign tag values
        for (size_t i = 0; i < _variants.size(); ++i)
        {
            _variants[i].tag_value = i;
        }

        _is_complete = true;
        compute_layout();
    }

    size_t EnumType::tag_size() const
    {
        size_t count = _variants.size();
        if (count <= 256)
            return 1;
        if (count <= 65536)
            return 2;
        return 4;
    }

    void EnumType::compute_layout()
    {
        if (is_simple_enum())
        {
            // Simple enums are just the tag
            _computed_size = tag_size();
            _computed_alignment = _computed_size;
            return;
        }

        // Find the largest variant payload
        size_t max_payload_size = 0;
        size_t max_payload_align = 1;

        for (const auto &variant : _variants)
        {
            size_t variant_size = 0;
            size_t variant_align = 1;

            for (const auto &payload : variant.payload_types)
            {
                if (!payload.is_valid())
                    continue;

                size_t align = payload->alignment();
                variant_align = std::max(variant_align, align);

                // Align and add
                variant_size = (variant_size + align - 1) / align * align;
                variant_size += payload->size_bytes();
            }

            max_payload_size = std::max(max_payload_size, variant_size);
            max_payload_align = std::max(max_payload_align, variant_align);
        }

        // Total size = tag + padding + max_payload_size
        size_t tag = tag_size();
        _computed_alignment = std::max(tag, max_payload_align);

        size_t offset = tag;
        offset = (offset + max_payload_align - 1) / max_payload_align * max_payload_align;
        offset += max_payload_size;

        _computed_size = (offset + _computed_alignment - 1) / _computed_alignment * _computed_alignment;
    }

    std::string EnumType::mangled_name() const
    {
        return "E" + std::to_string(_qualified_name.module.id) + "_" + _qualified_name.name;
    }

    std::string TraitType::mangled_name() const
    {
        return "T" + std::to_string(_qualified_name.module.id) + "_" + _qualified_name.name;
    }

    std::string TypeAliasType::mangled_name() const
    {
        // Type aliases delegate to their target
        if (_target.is_valid())
        {
            return _target->mangled_name();
        }
        return "A" + std::to_string(_qualified_name.module.id) + "_" + _qualified_name.name;
    }

    // ========================================================================
    // Interning (backward compatibility)
    // ========================================================================

    TypeRef TypeArena::intern(const Type *type)
    {
        if (!type)
        {
            return create_error("null type", SourceLocation{});
        }

        // Check if this exact type pointer is already in our arena
        for (size_t i = 0; i < _types.size(); ++i)
        {
            if (_types[i].get() == type)
            {
                return TypeRef(_types[i]->id(), this);
            }
        }

        // Type is not in our arena - we need to create an equivalent
        // This is a fallback for legacy code; ideally this shouldn't happen
        // Return an equivalent type based on kind
        TypeKind kind = type->kind();

        switch (kind)
        {
        case TypeKind::Void:
            return get_void();
        case TypeKind::Bool:
            return get_bool();
        case TypeKind::Char:
            return get_char();
        case TypeKind::String:
            return get_string();
        case TypeKind::Never:
            return get_never();
        case TypeKind::Int:
        {
            auto *int_type = static_cast<const IntType *>(type);
            return get_integer(int_type->integer_kind());
        }
        case TypeKind::Float:
        {
            auto *float_type = static_cast<const FloatType *>(type);
            return get_float(float_type->float_kind());
        }
        case TypeKind::Pointer:
        {
            auto *ptr_type = static_cast<const PointerType *>(type);
            TypeRef pointee = intern(ptr_type->pointee().get());
            return get_pointer_to(pointee);
        }
        case TypeKind::Reference:
        {
            auto *ref_type = static_cast<const ReferenceType *>(type);
            TypeRef referent = intern(ref_type->referent().get());
            return get_reference_to(referent, ref_type->mutability());
        }
        case TypeKind::Array:
        {
            auto *arr_type = static_cast<const ArrayType *>(type);
            TypeRef elem = intern(arr_type->element().get());
            return get_array_of(elem, arr_type->size());
        }
        case TypeKind::Optional:
        {
            auto *opt_type = static_cast<const OptionalType *>(type);
            TypeRef wrapped = intern(opt_type->wrapped().get());
            return get_optional_of(wrapped);
        }
        case TypeKind::Function:
        {
            auto *func_type = static_cast<const FunctionType *>(type);
            TypeRef ret = intern(func_type->return_type().get());
            std::vector<TypeRef> params;
            for (const auto &p : func_type->param_types())
            {
                params.push_back(intern(p.get()));
            }
            return get_function(ret, params, func_type->is_variadic());
        }
        case TypeKind::Tuple:
        {
            auto *tuple_type = static_cast<const TupleType *>(type);
            std::vector<TypeRef> elems;
            for (const auto &e : tuple_type->elements())
            {
                elems.push_back(intern(e.get()));
            }
            return get_tuple(elems);
        }
        case TypeKind::Struct:
        {
            auto *struct_type = static_cast<const StructType *>(type);
            return create_struct(struct_type->qualified_name());
        }
        case TypeKind::Class:
        {
            auto *class_type = static_cast<const ClassType *>(type);
            return create_class(class_type->qualified_name());
        }
        case TypeKind::Enum:
        {
            auto *enum_type = static_cast<const EnumType *>(type);
            return create_enum(enum_type->qualified_name());
        }
        case TypeKind::Trait:
        {
            auto *trait_type = static_cast<const TraitType *>(type);
            return create_trait(trait_type->qualified_name());
        }
        case TypeKind::TypeAlias:
        {
            auto *alias_type = static_cast<const TypeAliasType *>(type);
            TypeRef target = intern(alias_type->target().get());
            return create_type_alias(alias_type->qualified_name(), target);
        }
        case TypeKind::GenericParam:
        {
            auto *gen_type = static_cast<const GenericParamType *>(type);
            return create_generic_param(gen_type->param_name(), gen_type->param_index());
        }
        case TypeKind::Error:
        {
            auto *err_type = static_cast<const ErrorType *>(type);
            return create_error(err_type->reason(), err_type->location());
        }
        default:
            return create_error("unknown type kind in intern", SourceLocation{});
        }
    }

} // namespace Cryo
