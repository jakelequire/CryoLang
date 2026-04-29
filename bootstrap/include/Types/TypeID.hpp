#pragma once
/******************************************************************************
 * @file TypeID.hpp
 * @brief Core type identity system for Cryo's new type system
 *
 * This file defines TypeID (unique type identifier) and TypeRef (lightweight
 * handle to types). These form the foundation of the new type system where
 * type equality is determined by ID comparison rather than string names.
 *
 * Key design principles:
 * - TypeID is a simple 64-bit identifier, enabling O(1) equality checks
 * - TypeRef is a lightweight handle that can be passed by value
 * - All types are owned by TypeArena; TypeRef just references them
 * - Invalid/null types are explicitly represented, not via raw pointers
 ******************************************************************************/

#include <cstdint>
#include <functional>

namespace Cryo
{
    // Forward declarations
    class Type;
    class TypeArena;

    /**************************************************************************
     * @brief Unique identifier for a type in the type system
     *
     * Each type created in the TypeArena gets a unique TypeID. Type equality
     * is determined by comparing TypeIDs, making it an O(1) operation.
     *
     * TypeID 0 is reserved as the invalid/null ID.
     **************************************************************************/
    struct TypeID
    {
        uint64_t id;

        // Comparison operators
        bool operator==(const TypeID &other) const { return id == other.id; }
        bool operator!=(const TypeID &other) const { return id != other.id; }
        bool operator<(const TypeID &other) const { return id < other.id; }

        // Factory for invalid ID
        static TypeID invalid() { return TypeID{0}; }

        // Validity check
        bool is_valid() const { return id != 0; }

        // Hash support for use in containers
        struct Hash
        {
            size_t operator()(const TypeID &tid) const
            {
                return std::hash<uint64_t>{}(tid.id);
            }
        };
    };

    /**************************************************************************
     * @brief Lightweight handle to a type in the TypeArena
     *
     * TypeRef is the primary way to reference types throughout the compiler.
     * It's designed to be:
     * - Small and cheap to copy (just an ID + pointer)
     * - Safe (explicit validity checking, no dangling pointers)
     * - Fast for comparison (compares IDs, not strings)
     *
     * Usage:
     *   TypeRef int_type = arena.get_i32();
     *   TypeRef ptr_type = arena.get_pointer_to(int_type);
     *
     *   if (expr_type == int_type) { ... }  // O(1) comparison
     *
     * The TypeArena owns all Type objects. TypeRef just holds an ID and a
     * pointer back to the arena for dereferencing when needed.
     **************************************************************************/
    class TypeRef
    {
    private:
        TypeID _id;
        TypeArena *_arena;

    public:
        // Default constructor creates an invalid TypeRef
        TypeRef() : _id(TypeID::invalid()), _arena(nullptr) {}

        // Construct from ID and arena
        TypeRef(TypeID id, TypeArena *arena) : _id(id), _arena(arena) {}

        // Copy/move constructors and assignment (all defaulted - TypeRef is trivially copyable)
        TypeRef(const TypeRef &) = default;
        TypeRef(TypeRef &&) = default;
        TypeRef &operator=(const TypeRef &) = default;
        TypeRef &operator=(TypeRef &&) = default;

        /***********************************************************************
         * Equality comparison (based on TypeID only)
         *
         * Two TypeRefs are equal if they have the same TypeID.
         * This is an O(1) operation - the core improvement over string-based
         * type identity in the old system.
         ***********************************************************************/
        bool operator==(const TypeRef &other) const { return _id == other._id; }
        bool operator!=(const TypeRef &other) const { return _id != other._id; }
        bool operator<(const TypeRef &other) const { return _id < other._id; }

        /***********************************************************************
         * Access the actual Type object
         *
         * These methods dereference through the arena to get the Type*.
         * Returns nullptr if the TypeRef is invalid.
         ***********************************************************************/
        const Type *get() const;
        const Type *operator->() const { return get(); }
        const Type &operator*() const { return *get(); }

        /***********************************************************************
         * Validity and type checking
         ***********************************************************************/

        // Check if this TypeRef points to a valid type
        bool is_valid() const { return _id.is_valid() && _arena != nullptr; }

        // Check if this is an error type
        bool is_error() const;

        // Explicit bool conversion for use in if statements
        explicit operator bool() const { return is_valid(); }

        /***********************************************************************
         * ID access
         ***********************************************************************/
        TypeID id() const { return _id; }

        // Get raw ID value (useful for debugging)
        uint64_t raw_id() const { return _id.id; }

        /***********************************************************************
         * Hash support for use in containers
         ***********************************************************************/
        struct Hash
        {
            size_t operator()(const TypeRef &ref) const
            {
                return TypeID::Hash{}(ref._id);
            }
        };
    };

} // namespace Cryo
