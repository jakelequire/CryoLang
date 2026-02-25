#pragma once
/******************************************************************************
 * @file UserDefinedTypes.hpp
 * @brief User-defined type classes for Cryo's new type system
 *
 * Defines concrete type classes for user-defined types:
 * - StructType: struct Foo { ... }
 * - ClassType: class Foo { ... } (with optional inheritance)
 * - EnumType: enum Foo { ... }
 * - TraitType: trait Foo { ... }
 * - TypeAliasType: type Foo = Bar
 ******************************************************************************/

#include "Types/Type.hpp"
#include "Types/TypeID.hpp"
#include "Types/TypeKind.hpp"

#include <vector>
#include <optional>
#include <string>
#include <sstream>

namespace Cryo
{
    /**************************************************************************
     * @brief Field descriptor for struct/class types
     **************************************************************************/
    struct FieldInfo
    {
        std::string name;
        TypeRef type;
        size_t offset;           // Byte offset within the struct
        bool is_public = true;   // Visibility
        bool is_mutable = true;  // Can be modified (const fields are immutable)

        FieldInfo(std::string n, TypeRef t, size_t off = 0, bool pub = true, bool mut = true)
            : name(std::move(n)), type(t), offset(off), is_public(pub), is_mutable(mut) {}
    };

    /**************************************************************************
     * @brief Method descriptor for struct/class/trait types
     **************************************************************************/
    struct MethodInfo
    {
        std::string name;
        TypeRef function_type;   // FunctionType
        bool is_public = true;
        bool is_static = false;
        bool is_virtual = false; // For class inheritance
        bool is_override = false;

        MethodInfo(std::string n, TypeRef ft, bool pub = true, bool stat = false)
            : name(std::move(n)), function_type(ft), is_public(pub), is_static(stat) {}
    };

    /**************************************************************************
     * @brief Struct type - product type with named fields
     *
     * Structs are value types with fields. They can have methods.
     *
     * Note: Fields can be set after construction to handle:
     * - Forward declarations
     * - Recursive types (struct Node { value: int; next: Node*; })
     **************************************************************************/
    class StructType : public Type
    {
    private:
        QualifiedTypeName _qualified_name;
        std::vector<FieldInfo> _fields;
        std::vector<MethodInfo> _methods;
        bool _is_complete = false;  // True once fields have been set
        size_t _computed_size = 0;
        size_t _computed_alignment = 1;

    public:
        StructType(TypeID id, QualifiedTypeName name)
            : Type(id, TypeKind::Struct), _qualified_name(std::move(name)) {}

        // Name accessors
        const QualifiedTypeName &qualified_name() const { return _qualified_name; }
        const std::string &name() const { return _qualified_name.name; }
        ModuleID module() const { return _qualified_name.module; }

        // Completion state
        bool is_complete() const { return _is_complete; }

        // Set fields (completes the type)
        void set_fields(std::vector<FieldInfo> fields);

        // Add methods
        void add_method(MethodInfo method) { _methods.push_back(std::move(method)); }
        void set_methods(std::vector<MethodInfo> methods) { _methods = std::move(methods); }

        // Field access
        const std::vector<FieldInfo> &fields() const { return _fields; }
        size_t field_count() const { return _fields.size(); }

        std::optional<size_t> field_index(const std::string &name) const
        {
            for (size_t i = 0; i < _fields.size(); ++i)
            {
                if (_fields[i].name == name)
                    return i;
            }
            return std::nullopt;
        }

        std::optional<TypeRef> field_type(const std::string &name) const
        {
            auto idx = field_index(name);
            if (idx)
                return _fields[*idx].type;
            return std::nullopt;
        }

        const FieldInfo *get_field(const std::string &name) const
        {
            auto idx = field_index(name);
            if (idx)
                return &_fields[*idx];
            return nullptr;
        }

        // Method access
        const std::vector<MethodInfo> &methods() const { return _methods; }

        const MethodInfo *get_method(const std::string &name) const
        {
            for (const auto &m : _methods)
            {
                if (m.name == name)
                    return &m;
            }
            return nullptr;
        }

        // Type properties
        bool is_user_defined() const override { return true; }
        bool is_resolved() const override { return _is_complete; }

        size_t size_bytes() const override { return _computed_size; }
        size_t alignment() const override { return _computed_alignment; }

        std::string display_name() const override { return _qualified_name.name; }
        std::string mangled_name() const override;

    private:
        void compute_layout();
    };

    /**************************************************************************
     * @brief Class type - struct with inheritance support
     *
     * Classes extend structs with:
     * - Single inheritance (one base class)
     * - Virtual method dispatch
     * - Constructors and destructors
     **************************************************************************/
    class ClassType : public Type
    {
    private:
        QualifiedTypeName _qualified_name;
        TypeRef _base_class;    // Optional base class (invalid if no inheritance)
        std::vector<FieldInfo> _fields;
        std::vector<MethodInfo> _methods;
        bool _is_complete = false;
        size_t _computed_size = 0;
        size_t _computed_alignment = 1;
        bool _has_virtual_methods = false;
        bool _is_abstract = false;

    public:
        ClassType(TypeID id, QualifiedTypeName name)
            : Type(id, TypeKind::Class), _qualified_name(std::move(name)) {}

        // Name accessors
        const QualifiedTypeName &qualified_name() const { return _qualified_name; }
        const std::string &name() const { return _qualified_name.name; }
        ModuleID module() const { return _qualified_name.module; }

        // Inheritance
        void set_base_class(TypeRef base) { _base_class = base; }
        TypeRef base_class() const { return _base_class; }
        bool has_base_class() const { return _base_class.is_valid(); }

        // Completion state
        bool is_complete() const { return _is_complete; }

        // Set fields (completes the type)
        void set_fields(std::vector<FieldInfo> fields);

        // Add methods
        void add_method(MethodInfo method);
        void set_methods(std::vector<MethodInfo> methods);

        // Field access
        const std::vector<FieldInfo> &fields() const { return _fields; }
        size_t field_count() const { return _fields.size(); }
        std::optional<size_t> field_index(const std::string &name) const;
        std::optional<TypeRef> field_type(const std::string &name) const;
        const FieldInfo *get_field(const std::string &name) const;

        // Method access
        const std::vector<MethodInfo> &methods() const { return _methods; }
        const MethodInfo *get_method(const std::string &name) const;
        bool has_virtual_methods() const { return _has_virtual_methods; }
        bool is_abstract() const { return _is_abstract; }
        void set_abstract(bool v) { _is_abstract = v; }

        // Build the ordered vtable method list (base class methods first, overrides replace)
        std::vector<MethodInfo> build_vtable() const;
        // Get the vtable index for a given method name, or -1 if not found
        int vtable_index(const std::string &method_name) const;

        // Type properties
        bool is_user_defined() const override { return true; }
        bool is_resolved() const override { return _is_complete; }

        size_t size_bytes() const override { return _computed_size; }
        size_t alignment() const override { return _computed_alignment; }

        std::string display_name() const override { return _qualified_name.name; }
        std::string mangled_name() const override;

    private:
        void compute_layout();
    };

    /**************************************************************************
     * @brief Enum variant descriptor
     **************************************************************************/
    struct EnumVariant
    {
        std::string name;
        std::vector<TypeRef> payload_types;  // Empty for unit variants
        size_t tag_value;

        EnumVariant(std::string n, std::vector<TypeRef> payload = {}, size_t tag = 0)
            : name(std::move(n)), payload_types(std::move(payload)), tag_value(tag) {}

        bool has_payload() const { return !payload_types.empty(); }
    };

    /**************************************************************************
     * @brief Enum type - sum type with named variants
     *
     * Enums are algebraic data types (tagged unions).
     * Each variant can optionally carry data.
     **************************************************************************/
    class EnumType : public Type
    {
    private:
        QualifiedTypeName _qualified_name;
        std::vector<EnumVariant> _variants;
        std::vector<MethodInfo> _methods;
        bool _is_complete = false;
        size_t _computed_size = 0;
        size_t _computed_alignment = 1;

    public:
        EnumType(TypeID id, QualifiedTypeName name)
            : Type(id, TypeKind::Enum), _qualified_name(std::move(name)) {}

        // Name accessors
        const QualifiedTypeName &qualified_name() const { return _qualified_name; }
        const std::string &name() const { return _qualified_name.name; }
        ModuleID module() const { return _qualified_name.module; }

        // Completion state
        bool is_complete() const { return _is_complete; }

        // Set variants (completes the type)
        void set_variants(std::vector<EnumVariant> variants);

        // Add methods
        void add_method(MethodInfo method) { _methods.push_back(std::move(method)); }

        // Variant access
        const std::vector<EnumVariant> &variants() const { return _variants; }
        size_t variant_count() const { return _variants.size(); }

        std::optional<size_t> variant_index(const std::string &name) const
        {
            for (size_t i = 0; i < _variants.size(); ++i)
            {
                if (_variants[i].name == name)
                    return i;
            }
            return std::nullopt;
        }

        const EnumVariant *get_variant(const std::string &name) const
        {
            auto idx = variant_index(name);
            if (idx)
                return &_variants[*idx];
            return nullptr;
        }

        // Check if this is a simple enum (no payloads)
        bool is_simple_enum() const
        {
            for (const auto &v : _variants)
            {
                if (v.has_payload())
                    return false;
            }
            return true;
        }

        // Method access
        const std::vector<MethodInfo> &methods() const { return _methods; }

        // Type properties
        bool is_user_defined() const override { return true; }
        bool is_resolved() const override { return _is_complete; }

        size_t size_bytes() const override { return _computed_size; }
        size_t alignment() const override { return _computed_alignment; }

        std::string display_name() const override { return _qualified_name.name; }
        std::string mangled_name() const override;

    private:
        void compute_layout();
        size_t tag_size() const;  // Size needed for discriminant
    };

    /**************************************************************************
     * @brief Trait type - interface definition
     *
     * Traits define interfaces that types can implement.
     * They contain method signatures and associated types.
     **************************************************************************/
    class TraitType : public Type
    {
    private:
        QualifiedTypeName _qualified_name;
        std::vector<MethodInfo> _required_methods;
        std::vector<TypeRef> _super_traits;  // Traits this trait extends

    public:
        TraitType(TypeID id, QualifiedTypeName name)
            : Type(id, TypeKind::Trait), _qualified_name(std::move(name)) {}

        // Name accessors
        const QualifiedTypeName &qualified_name() const { return _qualified_name; }
        const std::string &name() const { return _qualified_name.name; }
        ModuleID module() const { return _qualified_name.module; }

        // Super traits
        void add_super_trait(TypeRef trait) { _super_traits.push_back(trait); }
        const std::vector<TypeRef> &super_traits() const { return _super_traits; }

        // Required methods
        void add_required_method(MethodInfo method) { _required_methods.push_back(std::move(method)); }
        const std::vector<MethodInfo> &required_methods() const { return _required_methods; }

        // Type properties
        bool is_user_defined() const override { return true; }

        // Traits have no runtime representation by themselves
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override { return _qualified_name.name; }
        std::string mangled_name() const override;
    };

    /**************************************************************************
     * @brief Type alias - type Foo = Bar
     *
     * Type aliases provide alternative names for existing types.
     * They are transparent - the alias is equal to the target type.
     **************************************************************************/
    class TypeAliasType : public Type
    {
    private:
        QualifiedTypeName _qualified_name;
        TypeRef _target;

    public:
        TypeAliasType(TypeID id, QualifiedTypeName name, TypeRef target)
            : Type(id, TypeKind::TypeAlias), _qualified_name(std::move(name)), _target(target) {}

        // Name accessors
        const QualifiedTypeName &qualified_name() const { return _qualified_name; }
        const std::string &name() const { return _qualified_name.name; }
        ModuleID module() const { return _qualified_name.module; }

        // Target type
        TypeRef target() const { return _target; }

        // Type aliases delegate to target
        bool is_user_defined() const override { return true; }

        size_t size_bytes() const override
        {
            return _target.is_valid() ? _target->size_bytes() : 0;
        }

        size_t alignment() const override
        {
            return _target.is_valid() ? _target->alignment() : 1;
        }

        std::string display_name() const override { return _qualified_name.name; }
        std::string mangled_name() const override;
    };

} // namespace Cryo
