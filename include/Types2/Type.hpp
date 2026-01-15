#pragma once
/******************************************************************************
 * @file Type.hpp
 * @brief Base Type class for Cryo's new type system
 *
 * Defines the abstract base class for all types in the system. Each concrete
 * type (IntType, StructType, etc.) derives from this class.
 *
 * Key design principles:
 * - Types are immutable after creation
 * - Type identity is via TypeID, not object identity
 * - Virtual methods enable polymorphic operations
 * - Types know their size/alignment for codegen
 ******************************************************************************/

#include "Types2/TypeID.hpp"
#include "Types2/TypeKind.hpp"
#include <string>
#include <cstddef>

namespace Cryo
{
    // Forward declarations
    class TypeArena;

    /**************************************************************************
     * @brief Module identifier for tracking type provenance
     *
     * Used to distinguish types with the same name in different modules.
     * For example, std::core::Option vs mylib::Option are different types.
     **************************************************************************/
    struct ModuleID
    {
        uint32_t id;

        bool operator==(const ModuleID &other) const { return id == other.id; }
        bool operator!=(const ModuleID &other) const { return id != other.id; }

        static ModuleID invalid() { return ModuleID{0}; }
        static ModuleID builtin() { return ModuleID{1}; }

        bool is_valid() const { return id != 0; }

        struct Hash
        {
            size_t operator()(const ModuleID &mid) const
            {
                return std::hash<uint32_t>{}(mid.id);
            }
        };
    };

    /**************************************************************************
     * @brief Qualified type name (module + local name)
     *
     * Uniquely identifies a user-defined type across modules.
     **************************************************************************/
    struct QualifiedTypeName
    {
        ModuleID module;
        std::string name;

        bool operator==(const QualifiedTypeName &other) const
        {
            return module == other.module && name == other.name;
        }

        bool operator!=(const QualifiedTypeName &other) const
        {
            return !(*this == other);
        }

        // Full qualified string representation
        std::string to_string() const;

        struct Hash
        {
            size_t operator()(const QualifiedTypeName &qtn) const
            {
                size_t h1 = ModuleID::Hash{}(qtn.module);
                size_t h2 = std::hash<std::string>{}(qtn.name);
                return h1 ^ (h2 << 1);
            }
        };
    };

    /**************************************************************************
     * @brief Abstract base class for all types in the Cryo type system
     *
     * Type objects are owned by TypeArena and should never be created directly.
     * Use TypeArena methods to create types and TypeRef to reference them.
     *
     * Types are immutable after creation - their properties cannot be changed.
     * (Exception: StructType/ClassType may have fields populated after creation
     * to handle forward declarations and recursive types.)
     **************************************************************************/
    class Type
    {
    protected:
        TypeID _id;
        TypeKind _kind;

        // Protected constructor - only TypeArena and derived classes can create types
        Type(TypeID id, TypeKind kind) : _id(id), _kind(kind) {}

    public:
        // Virtual destructor for proper cleanup of derived types
        virtual ~Type() = default;

        // Prevent copying - types should only exist in TypeArena
        Type(const Type &) = delete;
        Type &operator=(const Type &) = delete;

        // Allow moving for construction
        Type(Type &&) = default;
        Type &operator=(Type &&) = default;

        /***********************************************************************
         * Identity
         ***********************************************************************/

        // Get the unique type ID
        TypeID id() const { return _id; }

        // Get the type kind
        TypeKind kind() const { return _kind; }

        /***********************************************************************
         * Type properties - virtual methods for derived classes to override
         ***********************************************************************/

        // Is this a primitive type (void, bool, int, float, char, string)?
        virtual bool is_primitive() const { return false; }

        // Is this a numeric type (int or float)?
        virtual bool is_numeric() const { return false; }

        // Is this an integral type (any integer)?
        virtual bool is_integral() const { return false; }

        // Is this a floating-point type?
        virtual bool is_floating_point() const { return false; }

        // Is this a signed numeric type?
        virtual bool is_signed() const { return false; }

        // Is this a reference type (&T or &mut T)?
        virtual bool is_reference() const { return false; }

        // Is this a pointer type (T*)?
        virtual bool is_pointer() const { return false; }

        // Can this type be null? (pointers, optionals)
        virtual bool is_nullable() const { return false; }

        // Is this a generic type parameter (T, U, etc.)?
        virtual bool is_generic_param() const { return false; }

        // Is this an instantiated generic type (Array<int>)?
        virtual bool is_instantiated() const { return false; }

        // Is this an error type?
        virtual bool is_error() const { return false; }

        // Is this a user-defined type (struct, class, enum, trait)?
        virtual bool is_user_defined() const { return false; }

        // Is this type fully resolved? (generics may not be until instantiated)
        virtual bool is_resolved() const { return true; }

        // Is this a void type?
        bool is_void() const { return _kind == TypeKind::Void; }

        // Is this a boolean type?
        bool is_bool() const { return _kind == TypeKind::Bool; }

        // Is this an array type?
        bool is_array() const { return _kind == TypeKind::Array; }

        // Is this a function type?
        bool is_function() const { return _kind == TypeKind::Function; }

        // Is this a struct type?
        bool is_struct() const { return _kind == TypeKind::Struct; }

        // Is this a class type?
        bool is_class() const { return _kind == TypeKind::Class; }

        // Is this an enum type?
        bool is_enum() const { return _kind == TypeKind::Enum; }

        // Is this a trait type?
        bool is_trait() const { return _kind == TypeKind::Trait; }

        /***********************************************************************
         * Size and alignment - for codegen and memory layout
         ***********************************************************************/

        // Size of this type in bytes (0 for unsized types like generic params)
        virtual size_t size_bytes() const = 0;

        // Alignment requirement in bytes
        virtual size_t alignment() const = 0;

        /***********************************************************************
         * Display and debugging
         ***********************************************************************/

        // Human-readable type name (e.g., "i32", "Array<string>", "MyStruct")
        virtual std::string display_name() const = 0;

        // Mangled name for codegen (unique, valid identifier)
        virtual std::string mangled_name() const = 0;

        // Debug string with full details
        virtual std::string debug_string() const;
    };

} // namespace Cryo
