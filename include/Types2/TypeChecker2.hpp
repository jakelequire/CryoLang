#pragma once
/******************************************************************************
 * @file TypeChecker2.hpp
 * @brief Type checking infrastructure using the new Types2 system
 *
 * TypeChecker2 provides type checking, inference, and compatibility analysis
 * using the new TypeRef-based type system. Key improvements over the old
 * TypeChecker:
 *
 * - Uses TypeRef handles instead of raw Type* pointers
 * - Integrates with TypeResolver for single-path type resolution
 * - Uses TypeArena for type management
 * - Clean error handling through ErrorType
 * - Module-aware type checking
 ******************************************************************************/

#include "Types2/TypeID.hpp"
#include "Types2/Type.hpp"
#include "Types2/TypeArena.hpp"
#include "Types2/TypeResolver.hpp"
#include "Types2/ModuleTypeRegistry.hpp"
#include "Types2/GenericRegistry.hpp"
#include "Types2/PrimitiveTypes.hpp"
#include "Types2/CompoundTypes.hpp"
#include "Types2/UserDefinedTypes.hpp"
#include "Types2/ErrorType.hpp"

#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace Cryo
{
    // Forward declarations
    class DiagnosticManager;

    /**************************************************************************
     * @brief Type compatibility result
     **************************************************************************/
    enum class TypeCompatibility
    {
        Identical,        // Exact same type
        Compatible,       // Compatible without conversion
        ImplicitConvert,  // Needs implicit conversion
        ExplicitCast,     // Needs explicit cast
        Incompatible,     // Not compatible
    };

    /**************************************************************************
     * @brief Result of a type check operation
     **************************************************************************/
    struct TypeCheckResult
    {
        TypeRef result_type;
        bool success;
        std::string error_message;
        std::vector<std::string> notes;

        TypeCheckResult() : success(false) {}

        static TypeCheckResult ok(TypeRef type)
        {
            TypeCheckResult r;
            r.result_type = type;
            r.success = true;
            return r;
        }

        static TypeCheckResult error(const std::string &msg, TypeRef err_type = TypeRef{})
        {
            TypeCheckResult r;
            r.result_type = err_type;
            r.success = false;
            r.error_message = msg;
            return r;
        }

        void add_note(const std::string &note) { notes.push_back(note); }
        bool is_ok() const { return success && !result_type.is_error(); }
    };

    /**************************************************************************
     * @brief Conversion information
     **************************************************************************/
    struct ConversionInfo
    {
        TypeRef from_type;
        TypeRef to_type;
        TypeCompatibility compatibility;
        bool is_narrowing;      // Potential data loss
        bool is_sign_change;    // Sign conversion
        std::string warning_message;

        bool requires_cast() const
        {
            return compatibility == TypeCompatibility::ExplicitCast;
        }

        bool may_lose_data() const
        {
            return is_narrowing || is_sign_change;
        }
    };

    /**************************************************************************
     * @brief Type checker using the new Types2 system
     *
     * Provides type checking operations including:
     * - Type compatibility checking
     * - Type inference for expressions
     * - Conversion validation
     * - Binary/unary operation type resolution
     *
     * Usage:
     *   TypeChecker2 checker(arena, resolver, modules, generics);
     *
     *   TypeRef int_type = arena.get_i32();
     *   TypeRef float_type = arena.get_f32();
     *
     *   auto compat = checker.check_compatibility(int_type, float_type);
     *   // compat == TypeCompatibility::ImplicitConvert
     **************************************************************************/
    class TypeChecker2
    {
    private:
        TypeArena &_arena;
        TypeResolver &_resolver;
        ModuleTypeRegistry &_modules;
        GenericRegistry &_generics;
        DiagnosticManager *_diagnostics;

    public:
        // ====================================================================
        // Construction
        // ====================================================================

        TypeChecker2(TypeArena &arena,
                     TypeResolver &resolver,
                     ModuleTypeRegistry &modules,
                     GenericRegistry &generics,
                     DiagnosticManager *diag = nullptr);

        ~TypeChecker2() = default;

        // ====================================================================
        // Type Compatibility
        // ====================================================================

        /**
         * @brief Check compatibility between two types
         */
        TypeCompatibility check_compatibility(TypeRef from, TypeRef to);

        /**
         * @brief Check if from can be assigned to to
         */
        bool can_assign(TypeRef from, TypeRef to);

        /**
         * @brief Check if from can be implicitly converted to to
         */
        bool can_implicit_convert(TypeRef from, TypeRef to);

        /**
         * @brief Check if explicit cast from from to to is valid
         */
        bool can_cast(TypeRef from, TypeRef to);

        /**
         * @brief Get detailed conversion information
         */
        ConversionInfo get_conversion_info(TypeRef from, TypeRef to);

        // ====================================================================
        // Type Identity
        // ====================================================================

        /**
         * @brief Check if two types are identical
         */
        bool are_identical(TypeRef a, TypeRef b);

        /**
         * @brief Check if two types are structurally equivalent
         */
        bool are_equivalent(TypeRef a, TypeRef b);

        /**
         * @brief Check if sub is a subtype of super
         */
        bool is_subtype(TypeRef sub, TypeRef super);

        // ====================================================================
        // Binary Operations
        // ====================================================================

        /**
         * @brief Get result type of binary operation
         * @param op Operator token kind
         * @param lhs Left operand type
         * @param rhs Right operand type
         * @return Result type or error type
         */
        TypeCheckResult check_binary_op(int op, TypeRef lhs, TypeRef rhs);

        /**
         * @brief Get the common type for two operands
         */
        TypeRef get_common_type(TypeRef a, TypeRef b);

        // ====================================================================
        // Unary Operations
        // ====================================================================

        /**
         * @brief Get result type of unary operation
         */
        TypeCheckResult check_unary_op(int op, TypeRef operand);

        // ====================================================================
        // Function Calls
        // ====================================================================

        /**
         * @brief Check function call compatibility
         * @param func_type Function type being called
         * @param arg_types Argument types provided
         * @return Result with return type on success
         */
        TypeCheckResult check_function_call(TypeRef func_type,
                                             const std::vector<TypeRef> &arg_types);

        /**
         * @brief Check method call on a type
         */
        TypeCheckResult check_method_call(TypeRef receiver_type,
                                           const std::string &method_name,
                                           const std::vector<TypeRef> &arg_types);

        // ====================================================================
        // Member Access
        // ====================================================================

        /**
         * @brief Get type of field access
         */
        TypeCheckResult check_field_access(TypeRef base_type,
                                            const std::string &field_name);

        /**
         * @brief Get type of array/index access
         */
        TypeCheckResult check_index_access(TypeRef base_type,
                                            TypeRef index_type);

        // ====================================================================
        // Type Queries
        // ====================================================================

        /**
         * @brief Check if type is numeric (integer or float)
         */
        bool is_numeric(TypeRef type);

        /**
         * @brief Check if type is an integer type
         */
        bool is_integer(TypeRef type);

        /**
         * @brief Check if type is a floating point type
         */
        bool is_floating_point(TypeRef type);

        /**
         * @brief Check if type is a signed integer
         */
        bool is_signed_integer(TypeRef type);

        /**
         * @brief Check if type is an unsigned integer
         */
        bool is_unsigned_integer(TypeRef type);

        /**
         * @brief Check if type is boolean
         */
        bool is_boolean(TypeRef type);

        /**
         * @brief Check if type can be used in boolean context
         */
        bool is_truthy(TypeRef type);

        /**
         * @brief Check if type is a pointer type
         */
        bool is_pointer(TypeRef type);

        /**
         * @brief Check if type is a reference type
         */
        bool is_reference(TypeRef type);

        /**
         * @brief Check if type is nullable (pointer, optional, reference)
         */
        bool is_nullable(TypeRef type);

        /**
         * @brief Check if type is a user-defined type (struct, class, enum)
         */
        bool is_user_defined(TypeRef type);

        /**
         * @brief Check if type is a generic/parameterized type
         */
        bool is_generic(TypeRef type);

        // ====================================================================
        // Integer Type Utilities
        // ====================================================================

        /**
         * @brief Get bit width of integer type
         * @return Bit width or 0 if not integer
         */
        unsigned get_integer_width(TypeRef type);

        /**
         * @brief Compare integer type sizes
         * @return true if a is narrower than b
         */
        bool is_narrower_than(TypeRef a, TypeRef b);

        // ====================================================================
        // Error Handling
        // ====================================================================

        /**
         * @brief Create an error type with context
         */
        TypeRef make_error(const std::string &reason,
                           const SourceLocation &loc = SourceLocation{});

        // ====================================================================
        // Context Access
        // ====================================================================

        TypeArena &arena() { return _arena; }
        TypeResolver &resolver() { return _resolver; }
        ModuleTypeRegistry &modules() { return _modules; }
        GenericRegistry &generics() { return _generics; }

    private:
        // ====================================================================
        // Internal Helpers
        // ====================================================================

        /**
         * @brief Check numeric type compatibility
         */
        TypeCompatibility check_numeric_compatibility(TypeRef from, TypeRef to);

        /**
         * @brief Get arithmetic operation result type
         */
        TypeRef get_arithmetic_result(TypeRef lhs, TypeRef rhs);

        /**
         * @brief Get comparison operation result type
         */
        TypeRef get_comparison_result(TypeRef lhs, TypeRef rhs);

        /**
         * @brief Get logical operation result type
         */
        TypeRef get_logical_result(TypeRef lhs, TypeRef rhs);

        /**
         * @brief Get bitwise operation result type
         */
        TypeRef get_bitwise_result(TypeRef lhs, TypeRef rhs);

        /**
         * @brief Report diagnostic error
         */
        void report_error(const std::string &message, const SourceLocation &loc);

        /**
         * @brief Report diagnostic warning
         */
        void report_warning(const std::string &message, const SourceLocation &loc);
    };

    /**************************************************************************
     * @brief Type inference utilities using Types2
     **************************************************************************/
    class TypeInference2
    {
    private:
        TypeArena &_arena;
        TypeResolver &_resolver;

    public:
        TypeInference2(TypeArena &arena, TypeResolver &resolver)
            : _arena(arena), _resolver(resolver) {}

        /**
         * @brief Infer type of literal value
         */
        TypeRef infer_literal_type(int literal_kind, const std::string &value);

        /**
         * @brief Get the promoted type for arithmetic
         */
        TypeRef promote_for_arithmetic(TypeRef a, TypeRef b);

        /**
         * @brief Widen integer type to fit value
         */
        TypeRef widen_for_value(TypeRef type, int64_t value);
    };

    /**************************************************************************
     * @brief Type formatting utilities
     **************************************************************************/
    class TypeFormatter
    {
    public:
        /**
         * @brief Format type for display
         */
        static std::string format(TypeRef type);

        /**
         * @brief Format function signature
         */
        static std::string format_function(TypeRef func_type);

        /**
         * @brief Format type list
         */
        static std::string format_list(const std::vector<TypeRef> &types);

        /**
         * @brief Format type mismatch error
         */
        static std::string format_mismatch(TypeRef expected, TypeRef actual,
                                           const std::string &context);
    };

} // namespace Cryo
