#pragma once
/******************************************************************************
 * @file PrimitiveTypes.hpp
 * @brief Primitive type classes for Cryo's new type system
 *
 * Defines concrete type classes for primitive types:
 * - VoidType: no value
 * - BoolType: boolean true/false
 * - IntType: integer types (i8, i16, i32, i64, i128, u8, u16, u32, u64, u128)
 * - FloatType: floating point (f32, f64)
 * - CharType: Unicode character
 * - StringType: UTF-8 string
 * - NeverType: bottom type (functions that don't return)
 ******************************************************************************/

#include "Types/Type.hpp"
#include "Types/TypeKind.hpp"

namespace Cryo
{
    /**************************************************************************
     * @brief Void type - represents no value
     *
     * Used for functions that don't return a value.
     **************************************************************************/
    class VoidType : public Type
    {
    public:
        explicit VoidType(TypeID id) : Type(id, TypeKind::Void) {}

        bool is_primitive() const override { return true; }

        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override { return "void"; }
        std::string mangled_name() const override { return "v"; }
    };

    /**************************************************************************
     * @brief Boolean type - true/false
     **************************************************************************/
    class BoolType : public Type
    {
    public:
        explicit BoolType(TypeID id) : Type(id, TypeKind::Bool) {}

        bool is_primitive() const override { return true; }

        size_t size_bytes() const override { return 1; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override { return "boolean"; }
        std::string mangled_name() const override { return "b"; }
    };

    /**************************************************************************
     * @brief Integer type - signed and unsigned integers of various widths
     *
     * Encompasses i8, i16, i32, i64, i128, u8, u16, u32, u64, u128.
     **************************************************************************/
    class IntType : public Type
    {
    private:
        IntegerKind _int_kind;

    public:
        IntType(TypeID id, IntegerKind kind)
            : Type(id, TypeKind::Int), _int_kind(kind) {}

        IntegerKind integer_kind() const { return _int_kind; }

        bool is_primitive() const override { return true; }
        bool is_numeric() const override { return true; }
        bool is_integral() const override { return true; }
        bool is_signed() const override { return integer_kind_is_signed(_int_kind); }

        size_t size_bytes() const override { return integer_kind_size(_int_kind); }
        size_t alignment() const override { return integer_kind_size(_int_kind); }

        std::string display_name() const override { return integer_kind_to_string(_int_kind); }

        std::string mangled_name() const override
        {
            // Use standard mangling: signed = i, unsigned = u, followed by bits
            switch (_int_kind)
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
                return "iX";
            }
        }

        // Helper to get bit width
        size_t bit_width() const { return size_bytes() * 8; }

        // Helper to get min/max values
        int64_t min_value() const;
        uint64_t max_value() const;
    };

    /**************************************************************************
     * @brief Floating-point type - f32 (single) and f64 (double)
     **************************************************************************/
    class FloatType : public Type
    {
    private:
        FloatKind _float_kind;

    public:
        FloatType(TypeID id, FloatKind kind)
            : Type(id, TypeKind::Float), _float_kind(kind) {}

        FloatKind float_kind() const { return _float_kind; }

        bool is_primitive() const override { return true; }
        bool is_numeric() const override { return true; }
        bool is_floating_point() const override { return true; }
        bool is_signed() const override { return true; } // Floats are always signed

        size_t size_bytes() const override { return float_kind_size(_float_kind); }
        size_t alignment() const override { return float_kind_size(_float_kind); }

        std::string display_name() const override { return float_kind_to_string(_float_kind); }

        std::string mangled_name() const override
        {
            switch (_float_kind)
            {
            case FloatKind::F32:
                return "f32";
            case FloatKind::F64:
                return "f64";
            default:
                return "fX";
            }
        }

        // Helper to get bit width
        size_t bit_width() const { return size_bytes() * 8; }
    };

    /**************************************************************************
     * @brief Character type - Unicode scalar value (21-bit, stored as u32)
     **************************************************************************/
    class CharType : public Type
    {
    public:
        explicit CharType(TypeID id) : Type(id, TypeKind::Char) {}

        bool is_primitive() const override { return true; }

        // Stored as u32 to hold any Unicode code point
        size_t size_bytes() const override { return 4; }
        size_t alignment() const override { return 4; }

        std::string display_name() const override { return "char"; }
        std::string mangled_name() const override { return "c"; }
    };

    /**************************************************************************
     * @brief String type - UTF-8 encoded string
     *
     * Strings are dynamically sized. The size_bytes represents the size of
     * the string descriptor/fat pointer, not the string content.
     **************************************************************************/
    class StringType : public Type
    {
    public:
        explicit StringType(TypeID id) : Type(id, TypeKind::String) {}

        bool is_primitive() const override { return true; }

        // String descriptor size (pointer + length)
        size_t size_bytes() const override { return sizeof(void *) * 2; }
        size_t alignment() const override { return sizeof(void *); }

        std::string display_name() const override { return "string"; }
        std::string mangled_name() const override { return "s"; }
    };

    /**************************************************************************
     * @brief Never type - bottom type for functions that don't return
     *
     * Used for:
     * - Functions that always panic
     * - Infinite loops
     * - The type of `return` in a noreturn context
     *
     * Never is a subtype of all types (can be used anywhere).
     **************************************************************************/
    class NeverType : public Type
    {
    public:
        explicit NeverType(TypeID id) : Type(id, TypeKind::Never) {}

        // Never is uninhabited - no values exist
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override { return "never"; }
        std::string mangled_name() const override { return "!"; }
    };

} // namespace Cryo
