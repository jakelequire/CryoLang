#pragma once
/******************************************************************************
 * @file GenericTypes.hpp
 * @brief Generic type classes for Cryo's new type system
 *
 * Defines type classes for generic/parameterized types:
 * - GenericParamType: Type parameter (T, U, E)
 * - BoundedParamType: Type parameter with constraints (T: Clone)
 * - InstantiatedType: Concrete instantiation (Array<int>, Option<string>)
 ******************************************************************************/

#include "Types2/Type.hpp"
#include "Types2/TypeID.hpp"
#include "Types2/TypeKind.hpp"

#include <vector>
#include <string>
#include <sstream>

namespace Cryo
{
    /**************************************************************************
     * @brief Generic parameter descriptor
     *
     * Used to define type parameters in generic declarations.
     **************************************************************************/
    struct GenericParam
    {
        std::string name;              // "T", "U", "E"
        size_t index;                  // Position in parameter list
        TypeRef type;                  // The GenericParamType
        std::vector<TypeRef> bounds;   // Trait bounds

        GenericParam(std::string n, size_t idx, TypeRef t, std::vector<TypeRef> b = {})
            : name(std::move(n)), index(idx), type(t), bounds(std::move(b)) {}
    };

    /**************************************************************************
     * @brief Generic type parameter - T, U, E, etc.
     *
     * Represents an uninstantiated type parameter in a generic definition.
     *
     * Example: In `struct Container<T> { value: T; }`, the `T` is a
     * GenericParamType. It has no concrete size until instantiated.
     *
     * Type parameters are identified by their index in the parameter list,
     * not their name. This allows different generic definitions to use
     * "T" without collision.
     **************************************************************************/
    class GenericParamType : public Type
    {
    private:
        std::string _name;    // "T", "U", "E" - for display purposes
        size_t _index;        // Position in the type parameter list

    public:
        GenericParamType(TypeID id, std::string name, size_t index)
            : Type(id, TypeKind::GenericParam),
              _name(std::move(name)),
              _index(index) {}

        const std::string &param_name() const { return _name; }
        size_t param_index() const { return _index; }

        // Type properties
        bool is_generic_param() const override { return true; }
        bool is_resolved() const override { return false; }  // Not resolved until instantiated

        // Generic params have no concrete size
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override { return _name; }
        std::string mangled_name() const override
        {
            return "G" + std::to_string(_index) + "_" + _name;
        }
    };

    /**************************************************************************
     * @brief Bounded type parameter - T: Trait or T: Trait1 + Trait2
     *
     * A generic type parameter with trait constraints.
     *
     * Example: In `fn foo<T: Clone + Debug>(x: T)`, the T is a BoundedParamType
     * with bounds [Clone, Debug].
     **************************************************************************/
    class BoundedParamType : public Type
    {
    private:
        std::string _name;
        size_t _index;
        std::vector<TypeRef> _bounds;  // Trait type refs

    public:
        BoundedParamType(TypeID id, std::string name, size_t index, std::vector<TypeRef> bounds)
            : Type(id, TypeKind::BoundedParam),
              _name(std::move(name)),
              _index(index),
              _bounds(std::move(bounds)) {}

        const std::string &param_name() const { return _name; }
        size_t param_index() const { return _index; }
        const std::vector<TypeRef> &bounds() const { return _bounds; }

        // Type properties
        bool is_generic_param() const override { return true; }
        bool is_resolved() const override { return false; }

        // Generic params have no concrete size
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override
        {
            if (_bounds.empty())
                return _name;

            std::ostringstream oss;
            oss << _name << ": ";
            for (size_t i = 0; i < _bounds.size(); ++i)
            {
                if (i > 0)
                    oss << " + ";
                oss << (_bounds[i].is_valid() ? _bounds[i]->display_name() : "<invalid>");
            }
            return oss.str();
        }

        std::string mangled_name() const override
        {
            std::ostringstream oss;
            oss << "B" << _index << "_" << _name;
            for (const auto &bound : _bounds)
            {
                oss << "_" << (bound.is_valid() ? bound->mangled_name() : "X");
            }
            return oss.str();
        }
    };

    /**************************************************************************
     * @brief Instantiated generic type - Array<int>, Option<string>, etc.
     *
     * Represents a concrete instantiation of a generic type with specific
     * type arguments.
     *
     * Example: Array<int> is an InstantiatedType with:
     * - _generic_base: the generic Array<T> type
     * - _type_args: [int]
     *
     * The InstantiatedType may also hold a reference to the fully resolved
     * concrete type (e.g., a StructType with int fields instead of T fields).
     **************************************************************************/
    class InstantiatedType : public Type
    {
    private:
        TypeRef _generic_base;            // The generic type (Array<T>)
        std::vector<TypeRef> _type_args;  // Concrete type arguments [int]
        TypeRef _resolved_type;           // The monomorphized concrete type (optional)

    public:
        InstantiatedType(TypeID id,
                         TypeRef generic_base,
                         std::vector<TypeRef> type_args,
                         TypeRef resolved = TypeRef{})
            : Type(id, TypeKind::InstantiatedType),
              _generic_base(generic_base),
              _type_args(std::move(type_args)),
              _resolved_type(resolved) {}

        TypeRef generic_base() const { return _generic_base; }
        const std::vector<TypeRef> &type_args() const { return _type_args; }
        TypeRef resolved_type() const { return _resolved_type; }

        // Set the resolved type after monomorphization
        void set_resolved_type(TypeRef resolved) { _resolved_type = resolved; }
        bool has_resolved_type() const { return _resolved_type.is_valid(); }

        // Type properties
        bool is_instantiated() const override { return true; }
        bool is_resolved() const override { return _resolved_type.is_valid(); }

        // Size delegates to resolved type if available
        size_t size_bytes() const override
        {
            if (_resolved_type.is_valid())
                return _resolved_type->size_bytes();
            return 0;  // Unknown until resolved
        }

        size_t alignment() const override
        {
            if (_resolved_type.is_valid())
                return _resolved_type->alignment();
            return 1;
        }

        std::string display_name() const override
        {
            std::ostringstream oss;

            // Get base name
            if (_generic_base.is_valid())
            {
                oss << _generic_base->display_name();
            }
            else
            {
                oss << "<invalid>";
            }

            // Add type arguments
            oss << "<";
            for (size_t i = 0; i < _type_args.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << (_type_args[i].is_valid() ? _type_args[i]->display_name() : "<invalid>");
            }
            oss << ">";

            return oss.str();
        }

        std::string mangled_name() const override
        {
            std::ostringstream oss;
            oss << "I";

            // Base type mangled name
            if (_generic_base.is_valid())
            {
                oss << _generic_base->mangled_name();
            }
            else
            {
                oss << "X";
            }

            // Type arguments
            oss << "_" << _type_args.size();
            for (const auto &arg : _type_args)
            {
                oss << "_" << (arg.is_valid() ? arg->mangled_name() : "X");
            }

            return oss.str();
        }
    };

    /**************************************************************************
     * @brief Helper to check if a type contains any unresolved generic params
     **************************************************************************/
    inline bool contains_generic_params(TypeRef type)
    {
        if (!type.is_valid())
            return false;

        const Type *t = type.get();

        // Direct generic param
        if (t->is_generic_param())
            return true;

        // Check based on type kind
        switch (t->kind())
        {
        case TypeKind::Pointer:
        {
            auto *ptr = static_cast<const PointerType *>(t);
            return contains_generic_params(ptr->pointee());
        }
        case TypeKind::Reference:
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            return contains_generic_params(ref->referent());
        }
        case TypeKind::Array:
        {
            auto *arr = static_cast<const ArrayType *>(t);
            return contains_generic_params(arr->element());
        }
        case TypeKind::Optional:
        {
            auto *opt = static_cast<const OptionalType *>(t);
            return contains_generic_params(opt->wrapped());
        }
        case TypeKind::Function:
        {
            auto *fn = static_cast<const FunctionType *>(t);
            if (contains_generic_params(fn->return_type()))
                return true;
            for (const auto &param : fn->param_types())
            {
                if (contains_generic_params(param))
                    return true;
            }
            return false;
        }
        case TypeKind::Tuple:
        {
            auto *tup = static_cast<const TupleType *>(t);
            for (const auto &elem : tup->elements())
            {
                if (contains_generic_params(elem))
                    return true;
            }
            return false;
        }
        case TypeKind::InstantiatedType:
        {
            auto *inst = static_cast<const InstantiatedType *>(t);
            for (const auto &arg : inst->type_args())
            {
                if (contains_generic_params(arg))
                    return true;
            }
            return false;
        }
        default:
            return false;
        }
    }

    // Forward declarations needed for contains_generic_params
    class PointerType;
    class ReferenceType;
    class ArrayType;
    class OptionalType;
    class FunctionType;
    class TupleType;

} // namespace Cryo
