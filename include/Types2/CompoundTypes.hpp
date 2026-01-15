#pragma once
/******************************************************************************
 * @file CompoundTypes.hpp
 * @brief Compound type classes for Cryo's new type system
 *
 * Defines concrete type classes for compound types:
 * - PointerType: T* - raw pointers
 * - ReferenceType: &T or &mut T - references
 * - ArrayType: T[] or T[N] - dynamic or fixed-size arrays
 * - FunctionType: (params) -> return_type
 * - TupleType: (T1, T2, ...)
 * - OptionalType: Option<T>
 ******************************************************************************/

#include "Types2/Type.hpp"
#include "Types2/TypeID.hpp"
#include "Types2/TypeKind.hpp"

#include <vector>
#include <optional>
#include <sstream>

namespace Cryo
{
    /**************************************************************************
     * @brief Pointer type - T* (raw pointer)
     *
     * Represents raw pointer types. Can be null.
     **************************************************************************/
    class PointerType : public Type
    {
    private:
        TypeRef _pointee;

    public:
        PointerType(TypeID id, TypeRef pointee)
            : Type(id, TypeKind::Pointer), _pointee(pointee) {}

        TypeRef pointee() const { return _pointee; }

        bool is_pointer() const override { return true; }
        bool is_nullable() const override { return true; }

        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }

        std::string display_name() const override
        {
            if (!_pointee.is_valid())
                return "<invalid>*";
            return _pointee->display_name() + "*";
        }

        std::string mangled_name() const override
        {
            if (!_pointee.is_valid())
                return "PX";
            return "P" + _pointee->mangled_name();
        }
    };

    /**************************************************************************
     * @brief Reference type - &T or &mut T
     *
     * References are non-null pointers with borrow semantics.
     * - Immutable references (&T) can be shared
     * - Mutable references (&mut T) are exclusive
     **************************************************************************/
    class ReferenceType : public Type
    {
    private:
        TypeRef _referent;
        RefMutability _mutability;

    public:
        ReferenceType(TypeID id, TypeRef referent, RefMutability mutability)
            : Type(id, TypeKind::Reference), _referent(referent), _mutability(mutability) {}

        TypeRef referent() const { return _referent; }
        RefMutability mutability() const { return _mutability; }
        bool is_mutable() const { return _mutability == RefMutability::Mutable; }

        bool is_reference() const override { return true; }

        // References cannot be null (unlike pointers)
        bool is_nullable() const override { return false; }

        // References are pointer-sized
        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }

        std::string display_name() const override
        {
            std::string base = _referent.is_valid() ? _referent->display_name() : "<invalid>";
            if (_mutability == RefMutability::Mutable)
            {
                return "&mut " + base;
            }
            return "&" + base;
        }

        std::string mangled_name() const override
        {
            std::string prefix = (_mutability == RefMutability::Mutable) ? "Rm" : "R";
            if (!_referent.is_valid())
                return prefix + "X";
            return prefix + _referent->mangled_name();
        }
    };

    /**************************************************************************
     * @brief Array type - T[] or T[N]
     *
     * - Dynamic arrays (T[]) have runtime-determined size
     * - Fixed-size arrays (T[N]) have compile-time-known size
     **************************************************************************/
    class ArrayType : public Type
    {
    private:
        TypeRef _element;
        std::optional<size_t> _size; // nullopt for dynamic arrays

    public:
        ArrayType(TypeID id, TypeRef element, std::optional<size_t> size = std::nullopt)
            : Type(id, TypeKind::Array), _element(element), _size(size) {}

        TypeRef element() const { return _element; }
        std::optional<size_t> size() const { return _size; }
        bool is_fixed_size() const { return _size.has_value(); }
        bool is_dynamic() const { return !_size.has_value(); }

        size_t size_bytes() const override
        {
            if (!_element.is_valid())
                return 0;

            if (_size.has_value())
            {
                // Fixed-size array: element_size * count
                return _element->size_bytes() * (*_size);
            }
            else
            {
                // Dynamic array: fat pointer (ptr + length)
                return sizeof(void *) * 2;
            }
        }

        size_t alignment() const override
        {
            if (!_element.is_valid())
                return 1;

            if (_size.has_value())
            {
                // Fixed-size: alignment of element
                return _element->alignment();
            }
            else
            {
                // Dynamic: pointer alignment
                return sizeof(void *);
            }
        }

        std::string display_name() const override
        {
            std::string base = _element.is_valid() ? _element->display_name() : "<invalid>";
            if (_size.has_value())
            {
                return base + "[" + std::to_string(*_size) + "]";
            }
            return base + "[]";
        }

        std::string mangled_name() const override
        {
            std::string elem = _element.is_valid() ? _element->mangled_name() : "X";
            if (_size.has_value())
            {
                return "A" + std::to_string(*_size) + "_" + elem;
            }
            return "A_" + elem;
        }
    };

    /**************************************************************************
     * @brief Function type - (params) -> return_type
     *
     * Represents the type of a function, including parameter and return types.
     **************************************************************************/
    class FunctionType : public Type
    {
    private:
        TypeRef _return_type;
        std::vector<TypeRef> _param_types;
        bool _is_variadic;

    public:
        FunctionType(TypeID id,
                     TypeRef return_type,
                     std::vector<TypeRef> param_types,
                     bool is_variadic = false)
            : Type(id, TypeKind::Function),
              _return_type(return_type),
              _param_types(std::move(param_types)),
              _is_variadic(is_variadic) {}

        TypeRef return_type() const { return _return_type; }
        const std::vector<TypeRef> &param_types() const { return _param_types; }
        size_t param_count() const { return _param_types.size(); }
        bool is_variadic() const { return _is_variadic; }

        // Function types are pointer-sized (function pointer)
        size_t size_bytes() const override { return sizeof(void *); }
        size_t alignment() const override { return sizeof(void *); }

        std::string display_name() const override
        {
            std::ostringstream oss;
            oss << "(";
            for (size_t i = 0; i < _param_types.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << (_param_types[i].is_valid() ? _param_types[i]->display_name() : "<invalid>");
            }
            if (_is_variadic)
            {
                if (!_param_types.empty())
                    oss << ", ";
                oss << "...";
            }
            oss << ") -> ";
            oss << (_return_type.is_valid() ? _return_type->display_name() : "<invalid>");
            return oss.str();
        }

        std::string mangled_name() const override
        {
            std::ostringstream oss;
            oss << "F";
            for (const auto &param : _param_types)
            {
                oss << (param.is_valid() ? param->mangled_name() : "X");
            }
            if (_is_variadic)
            {
                oss << "V";
            }
            oss << "_";
            oss << (_return_type.is_valid() ? _return_type->mangled_name() : "X");
            return oss.str();
        }
    };

    /**************************************************************************
     * @brief Tuple type - (T1, T2, ...)
     *
     * Product type combining multiple values.
     **************************************************************************/
    class TupleType : public Type
    {
    private:
        std::vector<TypeRef> _elements;

    public:
        TupleType(TypeID id, std::vector<TypeRef> elements)
            : Type(id, TypeKind::Tuple), _elements(std::move(elements)) {}

        const std::vector<TypeRef> &elements() const { return _elements; }
        size_t element_count() const { return _elements.size(); }

        TypeRef element_at(size_t index) const
        {
            if (index < _elements.size())
                return _elements[index];
            return TypeRef{}; // Invalid
        }

        size_t size_bytes() const override
        {
            // Sum of element sizes with alignment padding
            size_t total = 0;
            size_t max_align = 1;
            for (const auto &elem : _elements)
            {
                if (!elem.is_valid())
                    continue;
                size_t elem_align = elem->alignment();
                // Align for this element
                total = (total + elem_align - 1) / elem_align * elem_align;
                total += elem->size_bytes();
                max_align = std::max(max_align, elem_align);
            }
            // Final alignment for the struct
            return (total + max_align - 1) / max_align * max_align;
        }

        size_t alignment() const override
        {
            size_t max_align = 1;
            for (const auto &elem : _elements)
            {
                if (elem.is_valid())
                {
                    max_align = std::max(max_align, elem->alignment());
                }
            }
            return max_align;
        }

        std::string display_name() const override
        {
            std::ostringstream oss;
            oss << "(";
            for (size_t i = 0; i < _elements.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << (_elements[i].is_valid() ? _elements[i]->display_name() : "<invalid>");
            }
            oss << ")";
            return oss.str();
        }

        std::string mangled_name() const override
        {
            std::ostringstream oss;
            oss << "T" << _elements.size();
            for (const auto &elem : _elements)
            {
                oss << "_" << (elem.is_valid() ? elem->mangled_name() : "X");
            }
            return oss.str();
        }
    };

    /**************************************************************************
     * @brief Optional type - Option<T>
     *
     * Represents a value that may or may not be present.
     * Equivalent to Rust's Option<T> or Haskell's Maybe.
     **************************************************************************/
    class OptionalType : public Type
    {
    private:
        TypeRef _wrapped;

    public:
        OptionalType(TypeID id, TypeRef wrapped)
            : Type(id, TypeKind::Optional), _wrapped(wrapped) {}

        TypeRef wrapped() const { return _wrapped; }

        bool is_nullable() const override { return true; }

        size_t size_bytes() const override
        {
            if (!_wrapped.is_valid())
                return 1; // Just the discriminant

            // Option needs space for discriminant + value
            // Typically 1 byte discriminant + padding + wrapped type
            size_t wrapped_size = _wrapped->size_bytes();
            size_t wrapped_align = _wrapped->alignment();

            // Discriminant is at the beginning, then aligned wrapped value
            size_t disc_padded = (1 + wrapped_align - 1) / wrapped_align * wrapped_align;
            return disc_padded + wrapped_size;
        }

        size_t alignment() const override
        {
            if (!_wrapped.is_valid())
                return 1;
            // Alignment is the max of discriminant (1) and wrapped type
            return std::max(size_t(1), _wrapped->alignment());
        }

        std::string display_name() const override
        {
            std::string inner = _wrapped.is_valid() ? _wrapped->display_name() : "<invalid>";
            return "Option<" + inner + ">";
        }

        std::string mangled_name() const override
        {
            std::string inner = _wrapped.is_valid() ? _wrapped->mangled_name() : "X";
            return "O" + inner;
        }
    };

} // namespace Cryo
