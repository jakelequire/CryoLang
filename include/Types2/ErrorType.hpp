#pragma once
/******************************************************************************
 * @file ErrorType.hpp
 * @brief Error type for Cryo's new type system
 *
 * Defines ErrorType which replaces the old "Unknown" type. Instead of silently
 * masking type resolution failures, ErrorType explicitly tracks:
 * - What went wrong (reason)
 * - Where it happened (location)
 * - Additional context (notes)
 *
 * This enables better error messages and prevents compilation from continuing
 * with invalid types.
 ******************************************************************************/

#include "Types2/Type.hpp"
#include "Types2/TypeKind.hpp"
#include "Lexer/lexer.hpp"

#include <string>
#include <vector>

namespace Cryo
{
    /**************************************************************************
     * @brief Error type - captures type resolution failures
     *
     * ErrorType is created when type resolution fails. Unlike the old Unknown
     * type which silently allowed compilation to continue, ErrorType:
     *
     * 1. Tracks WHY the error occurred (reason)
     * 2. Tracks WHERE the error occurred (location)
     * 3. Can accumulate additional context (notes)
     * 4. Propagates through compound types (pointer-to-error is still an error)
     *
     * Usage:
     *   TypeRef err = arena.create_error("undefined type 'Foo'", loc);
     *   if (some_type.is_error()) {
     *       // Handle error - don't continue with invalid type
     *   }
     **************************************************************************/
    class ErrorType : public Type
    {
    private:
        std::string _reason;
        SourceLocation _location;
        std::vector<std::string> _notes;

    public:
        ErrorType(TypeID id, std::string reason, SourceLocation location)
            : Type(id, TypeKind::Error),
              _reason(std::move(reason)),
              _location(location) {}

        ErrorType(TypeID id, std::string reason, SourceLocation location,
                  std::vector<std::string> notes)
            : Type(id, TypeKind::Error),
              _reason(std::move(reason)),
              _location(location),
              _notes(std::move(notes)) {}

        // Error information
        const std::string &reason() const { return _reason; }
        const SourceLocation &location() const { return _location; }
        const std::vector<std::string> &notes() const { return _notes; }

        // Add additional context
        void add_note(std::string note) { _notes.push_back(std::move(note)); }

        // Type properties
        bool is_error() const override { return true; }
        bool is_resolved() const override { return false; }  // Errors are never "resolved"

        // Errors have no valid size (compilation should fail)
        size_t size_bytes() const override { return 0; }
        size_t alignment() const override { return 1; }

        std::string display_name() const override
        {
            return "<error: " + _reason + ">";
        }

        std::string mangled_name() const override
        {
            return "E_error";
        }

        std::string debug_string() const override
        {
            std::string result = "ErrorType { reason: \"" + _reason + "\", location: " +
                                 std::to_string(_location.line()) + ":" +
                                 std::to_string(_location.column());
            if (!_notes.empty())
            {
                result += ", notes: [";
                for (size_t i = 0; i < _notes.size(); ++i)
                {
                    if (i > 0)
                        result += ", ";
                    result += "\"" + _notes[i] + "\"";
                }
                result += "]";
            }
            result += " }";
            return result;
        }
    };

    /**************************************************************************
     * @brief Error kind enumeration for categorizing type errors
     **************************************************************************/
    enum class TypeErrorKind
    {
        UndefinedType,        // Type name not found
        UndefinedMember,      // Field/method not found on type
        TypeMismatch,         // Expected X, got Y
        InvalidInstantiation, // Wrong number of type args, invalid args
        CyclicType,           // Type definition is cyclic
        IncompleteType,       // Type used before fully defined
        AmbiguousType,        // Multiple types match
        InternalError,        // Bug in the compiler
    };

    /**************************************************************************
     * @brief Convert error kind to string
     **************************************************************************/
    inline std::string type_error_kind_to_string(TypeErrorKind kind)
    {
        switch (kind)
        {
        case TypeErrorKind::UndefinedType:
            return "undefined type";
        case TypeErrorKind::UndefinedMember:
            return "undefined member";
        case TypeErrorKind::TypeMismatch:
            return "type mismatch";
        case TypeErrorKind::InvalidInstantiation:
            return "invalid generic instantiation";
        case TypeErrorKind::CyclicType:
            return "cyclic type definition";
        case TypeErrorKind::IncompleteType:
            return "incomplete type";
        case TypeErrorKind::AmbiguousType:
            return "ambiguous type";
        case TypeErrorKind::InternalError:
            return "internal compiler error";
        default:
            return "unknown error";
        }
    }

    /**************************************************************************
     * @brief Helper to create common error types
     **************************************************************************/
    struct TypeErrors
    {
        // Create an "undefined type" error
        static ErrorType *undefined_type(TypeID id, const std::string &name, SourceLocation loc)
        {
            return new ErrorType(id, "undefined type '" + name + "'", loc);
        }

        // Create a "type mismatch" error
        static ErrorType *type_mismatch(TypeID id, const std::string &expected,
                                        const std::string &got, SourceLocation loc)
        {
            return new ErrorType(id, "expected '" + expected + "', got '" + got + "'", loc);
        }

        // Create an "invalid generic instantiation" error
        static ErrorType *invalid_instantiation(TypeID id, const std::string &base_type,
                                                size_t expected_args, size_t got_args,
                                                SourceLocation loc)
        {
            return new ErrorType(id,
                                 "'" + base_type + "' expects " + std::to_string(expected_args) +
                                     " type argument(s), got " + std::to_string(got_args),
                                 loc);
        }

        // Create an "undefined member" error
        static ErrorType *undefined_member(TypeID id, const std::string &type_name,
                                           const std::string &member_name, SourceLocation loc)
        {
            return new ErrorType(id,
                                 "type '" + type_name + "' has no member '" + member_name + "'",
                                 loc);
        }

        // Create a "cyclic type" error
        static ErrorType *cyclic_type(TypeID id, const std::string &type_name, SourceLocation loc)
        {
            return new ErrorType(id, "cyclic type definition for '" + type_name + "'", loc);
        }

        // Create an internal error
        static ErrorType *internal(TypeID id, const std::string &message, SourceLocation loc)
        {
            return new ErrorType(id, "internal error: " + message, loc);
        }
    };

} // namespace Cryo
