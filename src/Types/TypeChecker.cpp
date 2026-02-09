/******************************************************************************
 * @file TypeChecker.cpp
 * @brief Implementation of TypeChecker for Cryo's new type system
 ******************************************************************************/

#include "Types/TypeChecker.hpp"
#include "Types/GenericTypes.hpp"

#include <sstream>
#include <algorithm>

namespace Cryo
{
    // ========================================================================
    // TypeChecker Construction
    // ========================================================================

    TypeChecker::TypeChecker(TypeArena &arena,
                               TypeResolver &resolver,
                               ModuleTypeRegistry &modules,
                               GenericRegistry &generics,
                               DiagEmitter *diag)
        : _arena(arena),
          _resolver(resolver),
          _modules(modules),
          _generics(generics),
          _diagnostics(diag)
    {
    }

    // ========================================================================
    // Type Compatibility
    // ========================================================================

    TypeCompatibility TypeChecker::check_compatibility(TypeRef from, TypeRef to)
    {
        if (!from.is_valid() || !to.is_valid())
        {
            return TypeCompatibility::Incompatible;
        }

        // Same type
        if (are_identical(from, to))
        {
            return TypeCompatibility::Identical;
        }

        const Type *from_t = from.get();
        const Type *to_t = to.get();

        // Error types propagate
        if (from_t->is_error() || to_t->is_error())
        {
            return TypeCompatibility::Incompatible;
        }

        TypeKind from_kind = from_t->kind();
        TypeKind to_kind = to_t->kind();

        // Numeric conversions
        if ((from_kind == TypeKind::Int || from_kind == TypeKind::Float) &&
            (to_kind == TypeKind::Int || to_kind == TypeKind::Float))
        {
            return check_numeric_compatibility(from, to);
        }

        // Pointer conversions
        if (from_kind == TypeKind::Pointer && to_kind == TypeKind::Pointer)
        {
            // All pointers are compatible (opaque pointers in LLVM 15+)
            return TypeCompatibility::Compatible;
        }

        // Reference to pointer
        if (from_kind == TypeKind::Reference && to_kind == TypeKind::Pointer)
        {
            return TypeCompatibility::ImplicitConvert;
        }

        // Pointer to reference (requires explicit)
        if (from_kind == TypeKind::Pointer && to_kind == TypeKind::Reference)
        {
            return TypeCompatibility::ExplicitCast;
        }

        // Optional unwrapping
        if (from_kind == TypeKind::Optional)
        {
            auto *opt = static_cast<const OptionalType *>(from_t);
            if (are_identical(opt->wrapped(), to))
            {
                return TypeCompatibility::ExplicitCast; // Needs unwrap
            }
        }

        // Wrapping in optional
        if (to_kind == TypeKind::Optional)
        {
            auto *opt = static_cast<const OptionalType *>(to_t);
            if (are_identical(from, opt->wrapped()))
            {
                return TypeCompatibility::ImplicitConvert;
            }
        }

        // Type alias resolution
        if (from_kind == TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const TypeAliasType *>(from_t);
            return check_compatibility(alias->target(), to);
        }
        if (to_kind == TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const TypeAliasType *>(to_t);
            return check_compatibility(from, alias->target());
        }

        // Unwrap InstantiatedType to its resolved concrete type
        if (from_kind == TypeKind::InstantiatedType)
        {
            auto *inst = static_cast<const InstantiatedType *>(from_t);
            if (inst->has_resolved_type())
            {
                return check_compatibility(inst->resolved_type(), to);
            }
        }
        if (to_kind == TypeKind::InstantiatedType)
        {
            auto *inst = static_cast<const InstantiatedType *>(to_t);
            if (inst->has_resolved_type())
            {
                return check_compatibility(from, inst->resolved_type());
            }
        }

        // Instantiated type to base type (both unresolved)
        if (from_kind == TypeKind::InstantiatedType && to_kind == TypeKind::InstantiatedType)
        {
            auto *from_inst = static_cast<const InstantiatedType *>(from_t);
            auto *to_inst = static_cast<const InstantiatedType *>(to_t);

            if (are_identical(from_inst->generic_base(), to_inst->generic_base()))
            {
                // Same base, check type arguments
                const auto &from_args = from_inst->type_args();
                const auto &to_args = to_inst->type_args();

                if (from_args.size() == to_args.size())
                {
                    bool all_identical = true;
                    for (size_t i = 0; i < from_args.size(); ++i)
                    {
                        if (!are_identical(from_args[i], to_args[i]))
                        {
                            all_identical = false;
                            break;
                        }
                    }
                    if (all_identical)
                    {
                        return TypeCompatibility::Identical;
                    }
                }
            }
        }

        return TypeCompatibility::Incompatible;
    }

    bool TypeChecker::can_assign(TypeRef from, TypeRef to)
    {
        auto compat = check_compatibility(from, to);
        return compat == TypeCompatibility::Identical ||
               compat == TypeCompatibility::Compatible ||
               compat == TypeCompatibility::ImplicitConvert;
    }

    bool TypeChecker::can_implicit_convert(TypeRef from, TypeRef to)
    {
        auto compat = check_compatibility(from, to);
        return compat != TypeCompatibility::Incompatible &&
               compat != TypeCompatibility::ExplicitCast;
    }

    bool TypeChecker::can_cast(TypeRef from, TypeRef to)
    {
        auto compat = check_compatibility(from, to);
        return compat != TypeCompatibility::Incompatible;
    }

    ConversionInfo TypeChecker::get_conversion_info(TypeRef from, TypeRef to)
    {
        ConversionInfo info;
        info.from_type = from;
        info.to_type = to;
        info.compatibility = check_compatibility(from, to);
        info.is_narrowing = false;
        info.is_sign_change = false;

        if (!from.is_valid() || !to.is_valid())
        {
            return info;
        }

        const Type *from_t = from.get();
        const Type *to_t = to.get();

        // Check for narrowing conversions
        if (from_t->kind() == TypeKind::Int && to_t->kind() == TypeKind::Int)
        {
            auto *from_int = static_cast<const IntType *>(from_t);
            auto *to_int = static_cast<const IntType *>(to_t);

            // Check bit width
            unsigned from_bits = from_int->bit_width();
            unsigned to_bits = to_int->bit_width();

            if (from_bits > to_bits)
            {
                info.is_narrowing = true;
                info.warning_message = "potential data loss in narrowing conversion";
            }

            // Check sign change
            if (from_int->is_signed() != to_int->is_signed())
            {
                info.is_sign_change = true;
                if (info.warning_message.empty())
                {
                    info.warning_message = "sign conversion";
                }
                else
                {
                    info.warning_message += " with sign conversion";
                }
            }
        }

        // Float to int is always narrowing
        if (from_t->kind() == TypeKind::Float && to_t->kind() == TypeKind::Int)
        {
            info.is_narrowing = true;
            info.warning_message = "conversion from floating point to integer";
        }

        return info;
    }

    // ========================================================================
    // Type Identity
    // ========================================================================

    bool TypeChecker::are_identical(TypeRef a, TypeRef b)
    {
        // Fast path: same TypeID
        if (a.id() == b.id())
        {
            return true;
        }

        if (!a.is_valid() || !b.is_valid())
        {
            return false;
        }

        return false; // Different IDs means different types
    }

    bool TypeChecker::are_equivalent(TypeRef a, TypeRef b)
    {
        if (are_identical(a, b))
        {
            return true;
        }

        if (!a.is_valid() || !b.is_valid())
        {
            return false;
        }

        const Type *a_t = a.get();
        const Type *b_t = b.get();

        // Handle type aliases
        if (a_t->kind() == TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const TypeAliasType *>(a_t);
            return are_equivalent(alias->target(), b);
        }
        if (b_t->kind() == TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const TypeAliasType *>(b_t);
            return are_equivalent(a, alias->target());
        }

        return false;
    }

    bool TypeChecker::is_subtype(TypeRef sub, TypeRef super)
    {
        if (are_identical(sub, super))
        {
            return true;
        }

        // TODO: Implement trait-based subtyping when trait system is complete
        return false;
    }

    // ========================================================================
    // Binary Operations
    // ========================================================================

    TypeCheckResult TypeChecker::check_binary_op(int op, TypeRef lhs, TypeRef rhs)
    {
        if (!lhs.is_valid() || !rhs.is_valid())
        {
            return TypeCheckResult::error("invalid operand types");
        }

        if (lhs.is_error())
        {
            return TypeCheckResult::ok(lhs);
        }
        if (rhs.is_error())
        {
            return TypeCheckResult::ok(rhs);
        }

        // Determine operation category based on operator
        // This is a simplified check - actual implementation would use TokenKind
        // For now, categorize as arithmetic, comparison, logical, or bitwise

        // Try arithmetic result
        TypeRef result = get_arithmetic_result(lhs, rhs);
        if (result.is_valid() && !result.is_error())
        {
            return TypeCheckResult::ok(result);
        }

        // Try comparison result
        result = get_comparison_result(lhs, rhs);
        if (result.is_valid() && !result.is_error())
        {
            return TypeCheckResult::ok(result);
        }

        // Try logical result
        result = get_logical_result(lhs, rhs);
        if (result.is_valid() && !result.is_error())
        {
            return TypeCheckResult::ok(result);
        }

        // Try bitwise result
        result = get_bitwise_result(lhs, rhs);
        if (result.is_valid() && !result.is_error())
        {
            return TypeCheckResult::ok(result);
        }

        return TypeCheckResult::error(
            "incompatible operand types: " + lhs.get()->display_name() +
            " and " + rhs.get()->display_name());
    }

    TypeRef TypeChecker::get_common_type(TypeRef a, TypeRef b)
    {
        if (!a.is_valid())
            return b;
        if (!b.is_valid())
            return a;

        if (are_identical(a, b))
        {
            return a;
        }

        const Type *a_t = a.get();
        const Type *b_t = b.get();

        // Numeric promotion
        if (is_numeric(a) && is_numeric(b))
        {
            // Float wins over int
            if (a_t->kind() == TypeKind::Float)
            {
                if (b_t->kind() == TypeKind::Float)
                {
                    auto *a_f = static_cast<const FloatType *>(a_t);
                    auto *b_f = static_cast<const FloatType *>(b_t);
                    // Return wider float
                    return a_f->bit_width() >= b_f->bit_width() ? a : b;
                }
                return a;
            }
            if (b_t->kind() == TypeKind::Float)
            {
                return b;
            }

            // Both integers - return wider
            if (a_t->kind() == TypeKind::Int && b_t->kind() == TypeKind::Int)
            {
                auto *a_i = static_cast<const IntType *>(a_t);
                auto *b_i = static_cast<const IntType *>(b_t);

                if (a_i->bit_width() > b_i->bit_width())
                {
                    return a;
                }
                if (b_i->bit_width() > a_i->bit_width())
                {
                    return b;
                }

                // Same width - prefer unsigned
                if (!a_i->is_signed())
                    return a;
                if (!b_i->is_signed())
                    return b;

                return a;
            }
        }

        // No common type
        return _arena.create_error("no common type", SourceLocation{});
    }

    // ========================================================================
    // Unary Operations
    // ========================================================================

    TypeCheckResult TypeChecker::check_unary_op(int op, TypeRef operand)
    {
        if (!operand.is_valid())
        {
            return TypeCheckResult::error("invalid operand type");
        }

        if (operand.is_error())
        {
            return TypeCheckResult::ok(operand);
        }

        const Type *t = operand.get();

        // Negation
        if (is_numeric(operand))
        {
            return TypeCheckResult::ok(operand);
        }

        // Logical not
        if (is_boolean(operand))
        {
            return TypeCheckResult::ok(operand);
        }

        // Pointer dereference
        if (t->kind() == TypeKind::Pointer)
        {
            auto *ptr = static_cast<const PointerType *>(t);
            return TypeCheckResult::ok(ptr->pointee());
        }

        // Reference dereference
        if (t->kind() == TypeKind::Reference)
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            return TypeCheckResult::ok(ref->referent());
        }

        return TypeCheckResult::error(
            "invalid operand type for unary operator: " + t->display_name());
    }

    // ========================================================================
    // Function Calls
    // ========================================================================

    TypeCheckResult TypeChecker::check_function_call(TypeRef func_type,
                                                       const std::vector<TypeRef> &arg_types)
    {
        if (!func_type.is_valid())
        {
            return TypeCheckResult::error("invalid function type");
        }

        const Type *t = func_type.get();
        if (t->kind() != TypeKind::Function)
        {
            return TypeCheckResult::error(
                "'" + t->display_name() + "' is not a callable type");
        }

        auto *fn = static_cast<const FunctionType *>(t);
        const auto &param_types = fn->param_types();

        // Check argument count
        if (!fn->is_variadic())
        {
            if (arg_types.size() != param_types.size())
            {
                std::ostringstream oss;
                oss << "expected " << param_types.size() << " arguments, got "
                    << arg_types.size();
                return TypeCheckResult::error(oss.str());
            }
        }
        else
        {
            if (arg_types.size() < param_types.size())
            {
                std::ostringstream oss;
                oss << "expected at least " << param_types.size()
                    << " arguments, got " << arg_types.size();
                return TypeCheckResult::error(oss.str());
            }
        }

        // Check argument types
        for (size_t i = 0; i < param_types.size(); ++i)
        {
            if (!can_assign(arg_types[i], param_types[i]))
            {
                std::ostringstream oss;
                oss << "argument " << (i + 1) << " type mismatch: expected '"
                    << param_types[i].get()->display_name() << "', got '"
                    << arg_types[i].get()->display_name() << "'";
                return TypeCheckResult::error(oss.str());
            }
        }

        return TypeCheckResult::ok(fn->return_type());
    }

    TypeCheckResult TypeChecker::check_method_call(TypeRef receiver_type,
                                                     const std::string &method_name,
                                                     const std::vector<TypeRef> &arg_types)
    {
        if (!receiver_type.is_valid())
        {
            return TypeCheckResult::error("invalid receiver type");
        }

        const Type *t = receiver_type.get();

        // Get method from struct/class
        if (t->kind() == TypeKind::Struct)
        {
            auto *st = static_cast<const StructType *>(t);
            auto method = st->get_method(method_name);
            if (method)
            {
                return check_function_call(method->function_type, arg_types);
            }
        }
        else if (t->kind() == TypeKind::Class)
        {
            auto *cl = static_cast<const ClassType *>(t);
            auto method = cl->get_method(method_name);
            if (method)
            {
                return check_function_call(method->function_type, arg_types);
            }
        }

        return TypeCheckResult::error(
            "no method '" + method_name + "' on type '" + t->display_name() + "'");
    }

    // ========================================================================
    // Member Access
    // ========================================================================

    TypeCheckResult TypeChecker::check_field_access(TypeRef base_type,
                                                      const std::string &field_name)
    {
        if (!base_type.is_valid())
        {
            return TypeCheckResult::error("invalid base type");
        }

        const Type *t = base_type.get();

        // Handle pointer/reference auto-deref
        if (t->kind() == TypeKind::Pointer)
        {
            auto *ptr = static_cast<const PointerType *>(t);
            return check_field_access(ptr->pointee(), field_name);
        }
        if (t->kind() == TypeKind::Reference)
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            return check_field_access(ref->referent(), field_name);
        }

        // Get field from struct/class
        if (t->kind() == TypeKind::Struct)
        {
            auto *st = static_cast<const StructType *>(t);
            auto field = st->get_field(field_name);
            if (field)
            {
                return TypeCheckResult::ok(field->type);
            }
        }
        else if (t->kind() == TypeKind::Class)
        {
            auto *cl = static_cast<const ClassType *>(t);
            auto field = cl->get_field(field_name);
            if (field)
            {
                return TypeCheckResult::ok(field->type);
            }
        }
        else if (t->kind() == TypeKind::Tuple)
        {
            auto *tup = static_cast<const TupleType *>(t);
            // Try to parse field name as index
            try
            {
                size_t idx = std::stoul(field_name);
                const auto &elems = tup->elements();
                if (idx < elems.size())
                {
                    return TypeCheckResult::ok(elems[idx]);
                }
            }
            catch (...)
            {
                // Not a valid index
            }
        }

        return TypeCheckResult::error(
            "no field '" + field_name + "' on type '" + t->display_name() + "'");
    }

    TypeCheckResult TypeChecker::check_index_access(TypeRef base_type,
                                                      TypeRef index_type)
    {
        if (!base_type.is_valid())
        {
            return TypeCheckResult::error("invalid base type");
        }

        const Type *t = base_type.get();

        // Check index is integer
        if (!is_integer(index_type))
        {
            return TypeCheckResult::error(
                "array index must be an integer type, got '" +
                index_type.get()->display_name() + "'");
        }

        // Array access
        if (t->kind() == TypeKind::Array)
        {
            auto *arr = static_cast<const ArrayType *>(t);
            return TypeCheckResult::ok(arr->element());
        }

        // Pointer arithmetic
        if (t->kind() == TypeKind::Pointer)
        {
            auto *ptr = static_cast<const PointerType *>(t);
            return TypeCheckResult::ok(ptr->pointee());
        }

        return TypeCheckResult::error(
            "type '" + t->display_name() + "' is not indexable");
    }

    // ========================================================================
    // Type Queries
    // ========================================================================

    bool TypeChecker::is_numeric(TypeRef type)
    {
        return is_integer(type) || is_floating_point(type);
    }

    bool TypeChecker::is_integer(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        return type.get()->kind() == TypeKind::Int;
    }

    bool TypeChecker::is_floating_point(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        return type.get()->kind() == TypeKind::Float;
    }

    bool TypeChecker::is_signed_integer(TypeRef type)
    {
        if (!is_integer(type))
            return false;
        auto *int_type = static_cast<const IntType *>(type.get());
        return int_type->is_signed();
    }

    bool TypeChecker::is_unsigned_integer(TypeRef type)
    {
        if (!is_integer(type))
            return false;
        auto *int_type = static_cast<const IntType *>(type.get());
        return !int_type->is_signed();
    }

    bool TypeChecker::is_boolean(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        return type.get()->kind() == TypeKind::Bool;
    }

    bool TypeChecker::is_truthy(TypeRef type)
    {
        if (!type.is_valid())
            return false;

        TypeKind kind = type.get()->kind();
        return kind == TypeKind::Bool ||
               kind == TypeKind::Int ||
               kind == TypeKind::Pointer ||
               kind == TypeKind::Optional;
    }

    bool TypeChecker::is_pointer(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        return type.get()->kind() == TypeKind::Pointer;
    }

    bool TypeChecker::is_reference(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        return type.get()->kind() == TypeKind::Reference;
    }

    bool TypeChecker::is_nullable(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        TypeKind kind = type.get()->kind();
        return kind == TypeKind::Pointer ||
               kind == TypeKind::Optional ||
               kind == TypeKind::Reference;
    }

    bool TypeChecker::is_user_defined(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        TypeKind kind = type.get()->kind();
        return kind == TypeKind::Struct ||
               kind == TypeKind::Class ||
               kind == TypeKind::Enum ||
               kind == TypeKind::Trait;
    }

    bool TypeChecker::is_generic(TypeRef type)
    {
        if (!type.is_valid())
            return false;
        TypeKind kind = type.get()->kind();
        return kind == TypeKind::GenericParam ||
               kind == TypeKind::BoundedParam ||
               kind == TypeKind::InstantiatedType;
    }

    unsigned TypeChecker::get_integer_width(TypeRef type)
    {
        if (!is_integer(type))
            return 0;
        auto *int_type = static_cast<const IntType *>(type.get());
        return int_type->bit_width();
    }

    bool TypeChecker::is_narrower_than(TypeRef a, TypeRef b)
    {
        return get_integer_width(a) < get_integer_width(b);
    }

    // ========================================================================
    // Error Handling
    // ========================================================================

    TypeRef TypeChecker::make_error(const std::string &reason, const SourceLocation &loc)
    {
        return _arena.create_error(reason, loc);
    }

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    TypeCompatibility TypeChecker::check_numeric_compatibility(TypeRef from, TypeRef to)
    {
        const Type *from_t = from.get();
        const Type *to_t = to.get();

        bool from_is_float = from_t->kind() == TypeKind::Float;
        bool to_is_float = to_t->kind() == TypeKind::Float;

        // Int to float: always implicit
        if (!from_is_float && to_is_float)
        {
            return TypeCompatibility::ImplicitConvert;
        }

        // Float to int: needs cast
        if (from_is_float && !to_is_float)
        {
            return TypeCompatibility::ExplicitCast;
        }

        // Both floats
        if (from_is_float && to_is_float)
        {
            auto *from_f = static_cast<const FloatType *>(from_t);
            auto *to_f = static_cast<const FloatType *>(to_t);

            if (from_f->bit_width() <= to_f->bit_width())
            {
                return TypeCompatibility::ImplicitConvert;
            }
            return TypeCompatibility::ExplicitCast;
        }

        // Both integers
        auto *from_i = static_cast<const IntType *>(from_t);
        auto *to_i = static_cast<const IntType *>(to_t);

        unsigned from_bits = from_i->bit_width();
        unsigned to_bits = to_i->bit_width();

        // Widening is always implicit
        if (from_bits < to_bits)
        {
            return TypeCompatibility::ImplicitConvert;
        }

        // Same size, same signedness
        if (from_bits == to_bits && from_i->is_signed() == to_i->is_signed())
        {
            return TypeCompatibility::Compatible;
        }

        // Narrowing or sign change needs cast
        return TypeCompatibility::ExplicitCast;
    }

    TypeRef TypeChecker::get_arithmetic_result(TypeRef lhs, TypeRef rhs)
    {
        if (is_numeric(lhs) && is_numeric(rhs))
        {
            return get_common_type(lhs, rhs);
        }

        // Pointer arithmetic
        if (is_pointer(lhs) && is_integer(rhs))
        {
            return lhs;
        }
        if (is_integer(lhs) && is_pointer(rhs))
        {
            return rhs;
        }

        return TypeRef{};
    }

    TypeRef TypeChecker::get_comparison_result(TypeRef lhs, TypeRef rhs)
    {
        // Numeric comparison
        if (is_numeric(lhs) && is_numeric(rhs))
        {
            return _arena.get_bool();
        }

        // Pointer comparison
        if (is_pointer(lhs) && is_pointer(rhs))
        {
            return _arena.get_bool();
        }

        // Boolean comparison
        if (is_boolean(lhs) && is_boolean(rhs))
        {
            return _arena.get_bool();
        }

        // String comparison (if char pointers)
        if (lhs.get()->kind() == TypeKind::String && rhs.get()->kind() == TypeKind::String)
        {
            return _arena.get_bool();
        }

        return TypeRef{};
    }

    TypeRef TypeChecker::get_logical_result(TypeRef lhs, TypeRef rhs)
    {
        if (is_truthy(lhs) && is_truthy(rhs))
        {
            return _arena.get_bool();
        }
        return TypeRef{};
    }

    TypeRef TypeChecker::get_bitwise_result(TypeRef lhs, TypeRef rhs)
    {
        if (is_integer(lhs) && is_integer(rhs))
        {
            return get_common_type(lhs, rhs);
        }
        return TypeRef{};
    }

    void TypeChecker::report_error(const std::string &message, const SourceLocation &loc)
    {
        // TODO: Integrate with DiagEmitter when available
        (void)message;
        (void)loc;
    }

    void TypeChecker::report_warning(const std::string &message, const SourceLocation &loc)
    {
        // TODO: Integrate with DiagEmitter when available
        (void)message;
        (void)loc;
    }

    // ========================================================================
    // TypeInference Implementation
    // ========================================================================

    TypeRef TypeInference::infer_literal_type(int literal_kind, const std::string &value)
    {
        (void)value; // May be used later for value-dependent inference

        // This would map to LiteralType enum from the AST
        // For now, use simple heuristics
        switch (literal_kind)
        {
        case 0: // Integer
            return _arena.get_i32();
        case 1: // Float
            return _arena.get_f64();
        case 2: // String
            return _arena.get_string();
        case 3: // Char
            return _arena.get_char();
        case 4: // Boolean
            return _arena.get_bool();
        default:
            return _arena.create_error("unknown literal kind", SourceLocation{});
        }
    }

    TypeRef TypeInference::promote_for_arithmetic(TypeRef a, TypeRef b)
    {
        if (!a.is_valid())
            return b;
        if (!b.is_valid())
            return a;

        // Use the widest type
        const Type *a_t = a.get();
        const Type *b_t = b.get();

        // Float wins
        if (a_t->kind() == TypeKind::Float || b_t->kind() == TypeKind::Float)
        {
            if (a_t->kind() == TypeKind::Float && b_t->kind() == TypeKind::Float)
            {
                auto *a_f = static_cast<const FloatType *>(a_t);
                auto *b_f = static_cast<const FloatType *>(b_t);
                return a_f->bit_width() >= b_f->bit_width() ? a : b;
            }
            return a_t->kind() == TypeKind::Float ? a : b;
        }

        // Both integers
        if (a_t->kind() == TypeKind::Int && b_t->kind() == TypeKind::Int)
        {
            auto *a_i = static_cast<const IntType *>(a_t);
            auto *b_i = static_cast<const IntType *>(b_t);
            return a_i->bit_width() >= b_i->bit_width() ? a : b;
        }

        return a;
    }

    TypeRef TypeInference::widen_for_value(TypeRef type, int64_t value)
    {
        if (!type.is_valid() || type.get()->kind() != TypeKind::Int)
        {
            return type;
        }

        auto *int_type = static_cast<const IntType *>(type.get());
        unsigned bits = int_type->bit_width();

        // Check if value fits in current type
        if (int_type->is_signed())
        {
            int64_t min = -(1LL << (bits - 1));
            int64_t max = (1LL << (bits - 1)) - 1;
            if (value >= min && value <= max)
            {
                return type;
            }
        }
        else
        {
            uint64_t max = (1ULL << bits) - 1;
            if (value >= 0 && static_cast<uint64_t>(value) <= max)
            {
                return type;
            }
        }

        // Need to widen - return i64
        return _arena.get_i64();
    }

    // ========================================================================
    // TypeFormatter Implementation
    // ========================================================================

    std::string TypeFormatter::format(TypeRef type)
    {
        if (!type.is_valid())
        {
            return "<invalid>";
        }
        return type.get()->display_name();
    }

    std::string TypeFormatter::format_function(TypeRef func_type)
    {
        if (!func_type.is_valid() || func_type.get()->kind() != TypeKind::Function)
        {
            return "<not a function>";
        }

        auto *fn = static_cast<const FunctionType *>(func_type.get());
        std::ostringstream oss;

        oss << "(";
        const auto &params = fn->param_types();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << format(params[i]);
        }
        if (fn->is_variadic())
        {
            if (!params.empty())
                oss << ", ";
            oss << "...";
        }
        oss << ") -> " << format(fn->return_type());

        return oss.str();
    }

    std::string TypeFormatter::format_list(const std::vector<TypeRef> &types)
    {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < types.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << format(types[i]);
        }
        oss << "]";
        return oss.str();
    }

    std::string TypeFormatter::format_mismatch(TypeRef expected, TypeRef actual,
                                                const std::string &context)
    {
        std::ostringstream oss;
        oss << context << ": expected '" << format(expected) << "', got '"
            << format(actual) << "'";
        return oss.str();
    }

} // namespace Cryo
