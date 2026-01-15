#pragma once
/******************************************************************************
 * @file TypeArena.hpp
 * @brief Type ownership and creation for Cryo's new type system
 *
 * TypeArena owns all Type objects in the compiler. It provides:
 * - Singleton primitive types (i32, bool, etc.)
 * - Deduplicated compound types (int* always returns same TypeRef)
 * - Factory methods for creating user-defined types
 * - TypeRef handles for safe type references
 *
 * Key design principles:
 * - Types are NEVER freed during compilation (arena owns everything)
 * - Primitive types are created once and reused
 * - Compound types are deduplicated for identity comparison
 * - TypeRef is the external interface; raw Type* stays internal
 ******************************************************************************/

#include "Types2/TypeID.hpp"
#include "Types2/TypeKind.hpp"
#include "Types2/Type.hpp"
#include "Lexer/lexer.hpp"

#include <memory>
#include <vector>
#include <unordered_map>
#include <optional>
#include <atomic>
#include <string>

namespace Cryo
{
    // Forward declarations for type classes
    class VoidType;
    class BoolType;
    class IntType;
    class FloatType;
    class CharType;
    class StringType;
    class PointerType;
    class ReferenceType;
    class ArrayType;
    class FunctionType;
    class TupleType;
    class OptionalType;
    class StructType;
    class ClassType;
    class EnumType;
    class TraitType;
    class TypeAliasType;
    class GenericParamType;
    class BoundedParamType;
    class InstantiatedType;
    class ErrorType;
    class NeverType;

    /**************************************************************************
     * @brief Central ownership and factory for all types in the compiler
     *
     * TypeArena is the single source of truth for type objects. All types
     * are created through the arena and referenced via TypeRef handles.
     *
     * Usage:
     *   TypeArena arena;
     *   TypeRef int_type = arena.get_i32();
     *   TypeRef ptr_type = arena.get_pointer_to(int_type);
     *   TypeRef struct_type = arena.create_struct({"mymodule", "MyStruct"});
     *
     * The arena guarantees:
     * - Primitive types return the same TypeRef every time
     * - Compound types with same components return the same TypeRef
     * - Types are never freed during compilation
     **************************************************************************/
    class TypeArena
    {
    private:
        // ====================================================================
        // Type storage
        // ====================================================================

        // All types owned by the arena
        std::vector<std::unique_ptr<Type>> _types;

        // ID -> index mapping for fast lookup
        std::unordered_map<TypeID, size_t, TypeID::Hash> _id_to_index;

        // Next ID to assign (starts at 1; 0 is invalid)
        std::atomic<uint64_t> _next_id{1};

        // ====================================================================
        // Primitive type cache (singleton instances)
        // ====================================================================

        struct PrimitiveCache
        {
            TypeRef void_type;
            TypeRef bool_type;
            TypeRef char_type;
            TypeRef string_type;
            TypeRef never_type;

            // Integer types
            TypeRef i8_type;
            TypeRef i16_type;
            TypeRef i32_type;
            TypeRef i64_type;
            TypeRef i128_type;
            TypeRef u8_type;
            TypeRef u16_type;
            TypeRef u32_type;
            TypeRef u64_type;
            TypeRef u128_type;

            // Float types
            TypeRef f32_type;
            TypeRef f64_type;
        };
        PrimitiveCache _primitives;
        bool _primitives_initialized = false;

        // ====================================================================
        // Compound type deduplication caches
        // ====================================================================

        // Pointer types: pointee TypeID -> pointer TypeRef
        std::unordered_map<TypeID, TypeRef, TypeID::Hash> _pointer_types;

        // Reference types: (referent TypeID, mutability) -> reference TypeRef
        struct RefKey
        {
            TypeID referent;
            RefMutability mutability;

            bool operator==(const RefKey &other) const
            {
                return referent == other.referent && mutability == other.mutability;
            }

            struct Hash
            {
                size_t operator()(const RefKey &key) const
                {
                    return TypeID::Hash{}(key.referent) ^
                           (static_cast<size_t>(key.mutability) << 1);
                }
            };
        };
        std::unordered_map<RefKey, TypeRef, RefKey::Hash> _reference_types;

        // Array types: (element TypeID, size) -> array TypeRef
        struct ArrayKey
        {
            TypeID element;
            std::optional<size_t> size; // nullopt for dynamic arrays

            bool operator==(const ArrayKey &other) const
            {
                return element == other.element && size == other.size;
            }

            struct Hash
            {
                size_t operator()(const ArrayKey &key) const
                {
                    size_t h = TypeID::Hash{}(key.element);
                    if (key.size)
                    {
                        h ^= std::hash<size_t>{}(*key.size) << 1;
                    }
                    return h;
                }
            };
        };
        std::unordered_map<ArrayKey, TypeRef, ArrayKey::Hash> _array_types;

        // Optional types: wrapped TypeID -> optional TypeRef
        std::unordered_map<TypeID, TypeRef, TypeID::Hash> _optional_types;

        // Function types: signature hash -> function TypeRef
        std::unordered_map<std::string, TypeRef> _function_types;

        // Tuple types: element hash -> tuple TypeRef
        std::unordered_map<std::string, TypeRef> _tuple_types;

        // ====================================================================
        // Helper methods
        // ====================================================================

        // Allocate a new unique TypeID
        TypeID allocate_id() { return TypeID{_next_id++}; }

        // Register a type in the arena, returning its TypeRef
        TypeRef register_type(std::unique_ptr<Type> type);

        // Initialize primitive type cache
        void initialize_primitives();

        // Generate function type cache key
        std::string make_function_key(TypeRef return_type,
                                      const std::vector<TypeRef> &params,
                                      bool is_variadic) const;

        // Generate tuple type cache key
        std::string make_tuple_key(const std::vector<TypeRef> &elements) const;

    public:
        TypeArena();
        ~TypeArena() = default;

        // Prevent copying (arena owns unique resources)
        TypeArena(const TypeArena &) = delete;
        TypeArena &operator=(const TypeArena &) = delete;

        // Allow moving
        TypeArena(TypeArena &&) = default;
        TypeArena &operator=(TypeArena &&) = default;

        // ====================================================================
        // Type lookup
        // ====================================================================

        // Look up a type by its ID
        const Type *lookup(TypeID id) const;

        // Get the number of types in the arena
        size_t type_count() const { return _types.size(); }

        // ====================================================================
        // Primitive type accessors (always return same instance)
        // ====================================================================

        TypeRef get_void();
        TypeRef get_bool();
        TypeRef get_char();
        TypeRef get_string();
        TypeRef get_never();

        // Integer types
        TypeRef get_i8();
        TypeRef get_i16();
        TypeRef get_i32();
        TypeRef get_i64();
        TypeRef get_i128();
        TypeRef get_u8();
        TypeRef get_u16();
        TypeRef get_u32();
        TypeRef get_u64();
        TypeRef get_u128();

        // Float types
        TypeRef get_f32();
        TypeRef get_f64();

        // Generic integer/float by kind
        TypeRef get_integer(IntegerKind kind);
        TypeRef get_float(FloatKind kind);

        // Type aliases (return canonical types)
        TypeRef get_int() { return get_i32(); }    // int -> i32
        TypeRef get_uint() { return get_u32(); }   // uint -> u32
        TypeRef get_float_alias() { return get_f32(); }  // float -> f32
        TypeRef get_double() { return get_f64(); } // double -> f64

        // ====================================================================
        // Compound type creation (deduplicated)
        // ====================================================================

        // Pointer type: T*
        TypeRef get_pointer_to(TypeRef pointee);

        // Reference type: &T or &mut T
        TypeRef get_reference_to(TypeRef referent, RefMutability mutability = RefMutability::Immutable);
        TypeRef get_mut_reference_to(TypeRef referent) { return get_reference_to(referent, RefMutability::Mutable); }

        // Array type: T[] or T[N]
        TypeRef get_array_of(TypeRef element, std::optional<size_t> size = std::nullopt);
        TypeRef get_fixed_array_of(TypeRef element, size_t size) { return get_array_of(element, size); }

        // Optional type: Option<T>
        TypeRef get_optional_of(TypeRef wrapped);

        // Function type: (params) -> return_type
        TypeRef get_function(TypeRef return_type,
                             std::vector<TypeRef> params,
                             bool is_variadic = false);

        // Tuple type: (T1, T2, ...)
        TypeRef get_tuple(std::vector<TypeRef> elements);

        // ====================================================================
        // User-defined type creation
        // ====================================================================

        // Create a new struct type
        TypeRef create_struct(const QualifiedTypeName &name);

        // Create a new class type
        TypeRef create_class(const QualifiedTypeName &name);

        // Create a new enum type
        TypeRef create_enum(const QualifiedTypeName &name);

        // Create a new trait type
        TypeRef create_trait(const QualifiedTypeName &name);

        // Create a type alias
        TypeRef create_type_alias(const QualifiedTypeName &name, TypeRef target);

        // ====================================================================
        // Generic type support
        // ====================================================================

        // Create a generic type parameter (T, U, E, etc.)
        TypeRef create_generic_param(const std::string &name, size_t index);

        // Create a bounded type parameter (T: Clone, T: Clone + Debug)
        TypeRef create_bounded_param(const std::string &name,
                                     size_t index,
                                     std::vector<TypeRef> bounds);

        // Create an instantiated generic type (Array<int>, Option<string>)
        // This creates a new type that wraps the generic base with concrete args
        TypeRef create_instantiation(TypeRef generic_base,
                                     std::vector<TypeRef> type_args);

        // ====================================================================
        // Error type creation
        // ====================================================================

        // Create an error type with reason and location
        TypeRef create_error(const std::string &reason, SourceLocation location);

        // Create an error type with additional notes
        TypeRef create_error(const std::string &reason,
                             SourceLocation location,
                             std::vector<std::string> notes);
    };

    // ========================================================================
    // TypeRef implementation (needs TypeArena definition)
    // ========================================================================

    inline const Type *TypeRef::get() const
    {
        if (!is_valid())
            return nullptr;
        return _arena->lookup(_id);
    }

    inline bool TypeRef::is_error() const
    {
        if (!is_valid())
            return false;
        const Type *t = get();
        return t && t->is_error();
    }

} // namespace Cryo
