#pragma once
/******************************************************************************
 * @file TypeKind.hpp
 * @brief Type kind enumeration for Cryo's new type system
 *
 * Defines the TypeKind enum that categorizes all types in the system.
 * This enum replaces string-based type discrimination with efficient
 * enum-based switching.
 ******************************************************************************/

#include <cstdint>
#include <string>

namespace Cryo
{
    /**************************************************************************
     * @brief Enumeration of all type kinds in the Cryo type system
     *
     * Each type in the system has exactly one TypeKind. This enables efficient
     * type discrimination without string comparisons.
     **************************************************************************/
    enum class TypeKind : uint8_t
    {
        // ====================================================================
        // Primitive Types
        // ====================================================================
        Void,   // void - no value
        Bool,   // boolean - true/false
        Int,    // integer types (i8, i16, i32, i64, i128, u8, u16, u32, u64, u128)
        Float,  // floating point types (f32, f64)
        Char,   // Unicode character (u32 with Unicode constraint)
        String, // UTF-8 string

        // ====================================================================
        // Compound Types
        // ====================================================================
        Pointer,   // T* - raw pointer
        Reference, // &T or &mut T - reference
        Array,     // T[] or T[N] - dynamic or fixed-size array
        Function,  // (params) -> return_type
        Tuple,     // (T1, T2, ...)
        Optional,  // Option<T> - nullable type

        // ====================================================================
        // User-Defined Types
        // ====================================================================
        Struct,    // struct Foo { ... }
        Class,     // class Foo { ... }
        Enum,      // enum Foo { ... }
        Trait,     // trait Foo { ... }
        TypeAlias, // type Foo = Bar

        // ====================================================================
        // Generic Types
        // ====================================================================
        GenericParam,     // Type parameter (T, U, E) - uninstantiated
        BoundedParam,     // Type parameter with constraints (T: Clone)
        InstantiatedType, // Concrete instantiation (Array<int>, Option<string>)

        // ====================================================================
        // Special Types
        // ====================================================================
        Error, // Error type - captures type resolution failures
        Never, // Bottom type - functions that don't return (panic, infinite loop)
    };

    /**************************************************************************
     * @brief Integer type variants (signed/unsigned, bit widths)
     **************************************************************************/
    enum class IntegerKind : uint8_t
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
    };

    /**************************************************************************
     * @brief Floating point type variants
     **************************************************************************/
    enum class FloatKind : uint8_t
    {
        F32, // 32-bit float (single precision)
        F64, // 64-bit float (double precision)
    };

    /**************************************************************************
     * @brief Reference mutability
     **************************************************************************/
    enum class RefMutability : uint8_t
    {
        Immutable, // &T - shared, read-only reference
        Mutable,   // &mut T - exclusive, read-write reference
    };

    /**************************************************************************
     * @brief Convert TypeKind to string for debugging/display
     **************************************************************************/
    inline std::string type_kind_to_string(TypeKind kind)
    {
        switch (kind)
        {
        case TypeKind::Void:
            return "Void";
        case TypeKind::Bool:
            return "Bool";
        case TypeKind::Int:
            return "Int";
        case TypeKind::Float:
            return "Float";
        case TypeKind::Char:
            return "Char";
        case TypeKind::String:
            return "String";
        case TypeKind::Pointer:
            return "Pointer";
        case TypeKind::Reference:
            return "Reference";
        case TypeKind::Array:
            return "Array";
        case TypeKind::Function:
            return "Function";
        case TypeKind::Tuple:
            return "Tuple";
        case TypeKind::Optional:
            return "Optional";
        case TypeKind::Struct:
            return "Struct";
        case TypeKind::Class:
            return "Class";
        case TypeKind::Enum:
            return "Enum";
        case TypeKind::Trait:
            return "Trait";
        case TypeKind::TypeAlias:
            return "TypeAlias";
        case TypeKind::GenericParam:
            return "GenericParam";
        case TypeKind::BoundedParam:
            return "BoundedParam";
        case TypeKind::InstantiatedType:
            return "InstantiatedType";
        case TypeKind::Error:
            return "Error";
        case TypeKind::Never:
            return "Never";
        default:
            return "Unknown";
        }
    }

    /**************************************************************************
     * @brief Convert IntegerKind to string for debugging/display
     **************************************************************************/
    inline std::string integer_kind_to_string(IntegerKind kind)
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
        default:
            return "unknown_int";
        }
    }

    /**************************************************************************
     * @brief Convert FloatKind to string for debugging/display
     **************************************************************************/
    inline std::string float_kind_to_string(FloatKind kind)
    {
        switch (kind)
        {
        case FloatKind::F32:
            return "f32";
        case FloatKind::F64:
            return "f64";
        default:
            return "unknown_float";
        }
    }

    /**************************************************************************
     * @brief Get the size in bytes for an integer type
     **************************************************************************/
    inline size_t integer_kind_size(IntegerKind kind)
    {
        switch (kind)
        {
        case IntegerKind::I8:
        case IntegerKind::U8:
            return 1;
        case IntegerKind::I16:
        case IntegerKind::U16:
            return 2;
        case IntegerKind::I32:
        case IntegerKind::U32:
            return 4;
        case IntegerKind::I64:
        case IntegerKind::U64:
            return 8;
        case IntegerKind::I128:
        case IntegerKind::U128:
            return 16;
        default:
            return 0;
        }
    }

    /**************************************************************************
     * @brief Check if an integer kind is signed
     **************************************************************************/
    inline bool integer_kind_is_signed(IntegerKind kind)
    {
        switch (kind)
        {
        case IntegerKind::I8:
        case IntegerKind::I16:
        case IntegerKind::I32:
        case IntegerKind::I64:
        case IntegerKind::I128:
            return true;
        case IntegerKind::U8:
        case IntegerKind::U16:
        case IntegerKind::U32:
        case IntegerKind::U64:
        case IntegerKind::U128:
            return false;
        default:
            return false;
        }
    }

    /**************************************************************************
     * @brief Get the size in bytes for a float type
     **************************************************************************/
    inline size_t float_kind_size(FloatKind kind)
    {
        switch (kind)
        {
        case FloatKind::F32:
            return 4;
        case FloatKind::F64:
            return 8;
        default:
            return 0;
        }
    }

} // namespace Cryo
