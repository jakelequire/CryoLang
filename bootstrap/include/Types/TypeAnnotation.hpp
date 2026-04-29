#pragma once
/******************************************************************************
 * @file TypeAnnotation.hpp
 * @brief Type annotation structure for representing types before resolution
 *
 * TypeAnnotation represents a type as written in source code, before it is
 * resolved to a TypeRef. The parser creates TypeAnnotations, and the
 * TypeResolver converts them to TypeRefs.
 ******************************************************************************/

#include "Lexer/lexer.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Cryo
{
    /**************************************************************************
     * @brief Type annotation kinds (from parsing)
     **************************************************************************/
    enum class TypeAnnotationKind
    {
        Primitive,     // i32, bool, void, etc.
        Named,         // MyStruct, Option, etc.
        Qualified,     // std::core::Option
        Pointer,       // T*
        Reference,     // &T or &mut T
        Array,         // T[] or T[N]
        Function,      // (params) -> return_type
        Tuple,         // (T1, T2, ...)
        Optional,      // T? (sugar for Option<T>)
        Generic,       // Array<T>, Option<int>
    };

    /**************************************************************************
     * @brief Type annotation from parsing
     *
     * Represents a type as written in source code, before resolution.
     * The TypeResolver converts these into TypeRefs.
     **************************************************************************/
    struct TypeAnnotation
    {
        TypeAnnotationKind kind;
        std::string name;                        // For Primitive, Named
        std::vector<std::string> qualified_path; // For Qualified (std::core::Option)
        SourceLocation location;

        // For compound types
        std::unique_ptr<TypeAnnotation> inner;   // Pointer, Reference, Array, Optional
        std::vector<TypeAnnotation> elements;    // Tuple, Function params, Generic args
        std::unique_ptr<TypeAnnotation> return_type; // Function

        // For arrays
        std::optional<size_t> array_size;

        // For references
        bool is_mutable = false;

        // For functions
        bool is_variadic = false;

        TypeAnnotation() : kind(TypeAnnotationKind::Named) {}

        // Copy constructor (deep copy)
        TypeAnnotation(const TypeAnnotation &other);

        // Move constructor
        TypeAnnotation(TypeAnnotation &&other) noexcept = default;

        // Copy assignment
        TypeAnnotation &operator=(const TypeAnnotation &other);

        // Move assignment
        TypeAnnotation &operator=(TypeAnnotation &&other) noexcept = default;

        // Factory methods for creating annotations
        static TypeAnnotation primitive(const std::string &name, SourceLocation loc);
        static TypeAnnotation named(const std::string &name, SourceLocation loc);
        static TypeAnnotation qualified(std::vector<std::string> path, SourceLocation loc);
        static TypeAnnotation pointer(TypeAnnotation inner, SourceLocation loc);
        static TypeAnnotation reference(TypeAnnotation inner, bool is_mut, SourceLocation loc);
        static TypeAnnotation array(TypeAnnotation elem, std::optional<size_t> size, SourceLocation loc);
        static TypeAnnotation function(std::vector<TypeAnnotation> params,
                                       TypeAnnotation ret, bool variadic, SourceLocation loc);
        static TypeAnnotation tuple(std::vector<TypeAnnotation> elems, SourceLocation loc);
        static TypeAnnotation optional(TypeAnnotation inner, SourceLocation loc);
        static TypeAnnotation generic(TypeAnnotation base,
                                      std::vector<TypeAnnotation> args, SourceLocation loc);

        // Get a display string for debugging
        std::string to_string() const;
    };

} // namespace Cryo
