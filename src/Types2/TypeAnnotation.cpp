/******************************************************************************
 * @file TypeAnnotation.cpp
 * @brief Implementation of TypeAnnotation structure
 ******************************************************************************/

#include "Types2/TypeAnnotation.hpp"
#include <sstream>

namespace Cryo
{
    // ========================================================================
    // Copy constructor (deep copy)
    // ========================================================================
    TypeAnnotation::TypeAnnotation(const TypeAnnotation &other)
        : kind(other.kind),
          name(other.name),
          qualified_path(other.qualified_path),
          location(other.location),
          elements(other.elements),
          array_size(other.array_size),
          is_mutable(other.is_mutable),
          is_variadic(other.is_variadic)
    {
        if (other.inner)
        {
            inner = std::make_unique<TypeAnnotation>(*other.inner);
        }
        if (other.return_type)
        {
            return_type = std::make_unique<TypeAnnotation>(*other.return_type);
        }
    }

    // ========================================================================
    // Copy assignment
    // ========================================================================
    TypeAnnotation &TypeAnnotation::operator=(const TypeAnnotation &other)
    {
        if (this != &other)
        {
            kind = other.kind;
            name = other.name;
            qualified_path = other.qualified_path;
            location = other.location;
            elements = other.elements;
            array_size = other.array_size;
            is_mutable = other.is_mutable;
            is_variadic = other.is_variadic;

            if (other.inner)
            {
                inner = std::make_unique<TypeAnnotation>(*other.inner);
            }
            else
            {
                inner.reset();
            }

            if (other.return_type)
            {
                return_type = std::make_unique<TypeAnnotation>(*other.return_type);
            }
            else
            {
                return_type.reset();
            }
        }
        return *this;
    }

    // ========================================================================
    // Factory methods
    // ========================================================================

    TypeAnnotation TypeAnnotation::primitive(const std::string &name, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Primitive;
        ann.name = name;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::named(const std::string &name, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Named;
        ann.name = name;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::qualified(std::vector<std::string> path, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Qualified;
        ann.qualified_path = std::move(path);
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::pointer(TypeAnnotation inner, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Pointer;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::reference(TypeAnnotation inner, bool is_mut, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Reference;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.is_mutable = is_mut;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::array(TypeAnnotation elem, std::optional<size_t> size, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Array;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(elem));
        ann.array_size = size;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::function(std::vector<TypeAnnotation> params,
                                            TypeAnnotation ret, bool variadic, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Function;
        ann.elements = std::move(params);
        ann.return_type = std::make_unique<TypeAnnotation>(std::move(ret));
        ann.is_variadic = variadic;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::tuple(std::vector<TypeAnnotation> elems, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Tuple;
        ann.elements = std::move(elems);
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::optional(TypeAnnotation inner, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Optional;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::generic(TypeAnnotation base,
                                           std::vector<TypeAnnotation> args, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Generic;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(base));
        ann.elements = std::move(args);
        ann.location = loc;
        return ann;
    }

    // ========================================================================
    // to_string for debugging
    // ========================================================================
    std::string TypeAnnotation::to_string() const
    {
        std::ostringstream oss;

        switch (kind)
        {
        case TypeAnnotationKind::Primitive:
        case TypeAnnotationKind::Named:
            oss << name;
            break;

        case TypeAnnotationKind::Qualified:
            for (size_t i = 0; i < qualified_path.size(); ++i)
            {
                if (i > 0)
                    oss << "::";
                oss << qualified_path[i];
            }
            break;

        case TypeAnnotationKind::Pointer:
            if (inner)
                oss << inner->to_string() << "*";
            break;

        case TypeAnnotationKind::Reference:
            oss << "&";
            if (is_mutable)
                oss << "mut ";
            if (inner)
                oss << inner->to_string();
            break;

        case TypeAnnotationKind::Array:
            if (inner)
            {
                oss << inner->to_string() << "[";
                if (array_size)
                    oss << *array_size;
                oss << "]";
            }
            break;

        case TypeAnnotationKind::Function:
            oss << "(";
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << elements[i].to_string();
            }
            if (is_variadic)
                oss << ", ...";
            oss << ") -> ";
            if (return_type)
                oss << return_type->to_string();
            break;

        case TypeAnnotationKind::Tuple:
            oss << "(";
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << elements[i].to_string();
            }
            oss << ")";
            break;

        case TypeAnnotationKind::Optional:
            if (inner)
                oss << inner->to_string() << "?";
            break;

        case TypeAnnotationKind::Generic:
            if (inner)
                oss << inner->to_string();
            oss << "<";
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << elements[i].to_string();
            }
            oss << ">";
            break;
        }

        return oss.str();
    }

} // namespace Cryo
