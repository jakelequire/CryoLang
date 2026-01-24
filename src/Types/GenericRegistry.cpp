/******************************************************************************
 * @file GenericRegistry.cpp
 * @brief Implementation of GenericRegistry for Cryo's new type system
 ******************************************************************************/

#include "Types/GenericRegistry.hpp"
#include "Types/TypeArena.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/Logger.hpp"

#include <sstream>

namespace Cryo
{
    // ========================================================================
    // TypeSubstitution implementation
    // ========================================================================

    TypeSubstitution::TypeSubstitution(const GenericTemplate &tmpl,
                                       const std::vector<TypeRef> &args)
    {
        // Build substitution map from template params to arguments
        for (size_t i = 0; i < tmpl.params.size() && i < args.size(); ++i)
        {
            _substitutions[tmpl.params[i].type.id()] = args[i];
        }
    }

    void TypeSubstitution::add(TypeRef param, TypeRef replacement)
    {
        _substitutions[param.id()] = replacement;
    }

    std::optional<TypeRef> TypeSubstitution::get(TypeRef param) const
    {
        return get(param.id());
    }

    std::optional<TypeRef> TypeSubstitution::get(TypeID param_id) const
    {
        auto it = _substitutions.find(param_id);
        if (it != _substitutions.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    bool TypeSubstitution::has(TypeRef param) const
    {
        return has(param.id());
    }

    bool TypeSubstitution::has(TypeID param_id) const
    {
        return _substitutions.find(param_id) != _substitutions.end();
    }

    TypeRef TypeSubstitution::apply(TypeRef type, TypeArena &arena) const
    {
        if (!type.is_valid())
        {
            return type;
        }

        const Type *t = type.get();

        // Check if this is a type parameter we should substitute
        if (t->is_generic_param())
        {
            auto sub = get(type.id());
            if (sub)
            {
                return *sub;
            }
            // No substitution found - return as-is
            return type;
        }

        // Recursively apply to compound types
        switch (t->kind())
        {
        case TypeKind::Pointer:
        {
            auto *ptr = static_cast<const PointerType *>(t);
            TypeRef new_pointee = apply(ptr->pointee(), arena);
            if (new_pointee == ptr->pointee())
            {
                return type; // No change
            }
            return arena.get_pointer_to(new_pointee);
        }

        case TypeKind::Reference:
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            TypeRef new_referent = apply(ref->referent(), arena);
            if (new_referent == ref->referent())
            {
                return type;
            }
            return arena.get_reference_to(new_referent, ref->mutability());
        }

        case TypeKind::Array:
        {
            auto *arr = static_cast<const ArrayType *>(t);
            TypeRef new_element = apply(arr->element(), arena);
            if (new_element == arr->element())
            {
                return type;
            }
            return arena.get_array_of(new_element, arr->size());
        }

        case TypeKind::Optional:
        {
            auto *opt = static_cast<const OptionalType *>(t);
            TypeRef new_wrapped = apply(opt->wrapped(), arena);
            if (new_wrapped == opt->wrapped())
            {
                return type;
            }
            return arena.get_optional_of(new_wrapped);
        }

        case TypeKind::Function:
        {
            auto *fn = static_cast<const FunctionType *>(t);
            TypeRef new_return = apply(fn->return_type(), arena);

            std::vector<TypeRef> new_params;
            bool changed = (new_return != fn->return_type());

            for (const auto &param : fn->param_types())
            {
                TypeRef new_param = apply(param, arena);
                new_params.push_back(new_param);
                if (new_param != param)
                {
                    changed = true;
                }
            }

            if (!changed)
            {
                return type;
            }
            return arena.get_function(new_return, std::move(new_params), fn->is_variadic());
        }

        case TypeKind::Tuple:
        {
            auto *tup = static_cast<const TupleType *>(t);
            std::vector<TypeRef> new_elements;
            bool changed = false;

            for (const auto &elem : tup->elements())
            {
                TypeRef new_elem = apply(elem, arena);
                new_elements.push_back(new_elem);
                if (new_elem != elem)
                {
                    changed = true;
                }
            }

            if (!changed)
            {
                return type;
            }
            return arena.get_tuple(std::move(new_elements));
        }

        case TypeKind::InstantiatedType:
        {
            auto *inst = static_cast<const InstantiatedType *>(t);
            std::vector<TypeRef> new_args;
            bool changed = false;

            for (const auto &arg : inst->type_args())
            {
                TypeRef new_arg = apply(arg, arena);
                new_args.push_back(new_arg);
                if (new_arg != arg)
                {
                    changed = true;
                }
            }

            if (!changed)
            {
                return type;
            }
            return arena.create_instantiation(inst->generic_base(), std::move(new_args));
        }

        default:
            // No substitution needed for primitive types, user-defined types, etc.
            return type;
        }
    }

    // ========================================================================
    // GenericRegistry implementation
    // ========================================================================

    void GenericRegistry::register_template(TypeRef generic_type,
                                            std::vector<GenericParam> params,
                                            ModuleID source_module,
                                            ASTNode *ast_node,
                                            const std::string &name)
    {
        GenericTemplate tmpl(generic_type, std::move(params), source_module, ast_node, name);
        _templates[generic_type.id()] = tmpl;

        if (!name.empty())
        {
            _templates_by_name[name] = generic_type.id();
        }
    }

    bool GenericRegistry::is_template(TypeRef type) const
    {
        return is_template(type.id());
    }

    bool GenericRegistry::is_template(TypeID type_id) const
    {
        return _templates.find(type_id) != _templates.end();
    }

    std::optional<GenericTemplate> GenericRegistry::get_template(TypeRef type) const
    {
        return get_template(type.id());
    }

    std::optional<GenericTemplate> GenericRegistry::get_template(TypeID type_id) const
    {
        auto it = _templates.find(type_id);
        if (it != _templates.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<GenericTemplate> GenericRegistry::get_template_by_name(const std::string &name) const
    {
        auto it = _templates_by_name.find(name);
        if (it != _templates_by_name.end())
        {
            return get_template(it->second);
        }
        return std::nullopt;
    }

    std::vector<GenericTemplate> GenericRegistry::get_all_templates() const
    {
        std::vector<GenericTemplate> result;
        result.reserve(_templates.size());
        for (const auto &[id, tmpl] : _templates)
        {
            result.push_back(tmpl);
        }
        return result;
    }

    TypeRef GenericRegistry::instantiate(TypeRef generic_type,
                                          std::vector<TypeRef> type_args,
                                          TypeArena &arena)
    {
        // Check cache first
        auto cached = get_cached_instantiation(generic_type, type_args);
        if (cached)
        {
            return *cached;
        }

        // Validate
        std::string error;
        if (!validate_type_args(generic_type, type_args, &error))
        {
            return arena.create_error(error, SourceLocation{});
        }

        // Create the instantiated type
        TypeRef instantiated = arena.create_instantiation(generic_type, type_args);

        // Cache it
        auto key = make_key(generic_type, type_args);
        cache_instantiation(key, instantiated);

        return instantiated;
    }

    std::optional<TypeRef> GenericRegistry::get_cached_instantiation(
        TypeRef base,
        const std::vector<TypeRef> &args) const
    {
        auto key = make_key(base, args);
        auto it = _instantiations.find(key);
        if (it != _instantiations.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    void GenericRegistry::request_instantiation(TypeRef generic_type,
                                                 std::vector<TypeRef> type_args,
                                                 SourceLocation location)
    {
        // Check if we already have this instantiation
        if (get_cached_instantiation(generic_type, type_args))
        {
            return;
        }

        // Add to pending list
        _pending_instantiations.emplace_back(generic_type, std::move(type_args), location);
    }

    void GenericRegistry::mark_monomorphized(TypeRef base, const std::vector<TypeRef> &args)
    {
        auto key = make_key(base, args);
        _monomorphized.insert(key);
    }

    bool GenericRegistry::is_monomorphized(TypeRef base, const std::vector<TypeRef> &args) const
    {
        auto key = make_key(base, args);
        return _monomorphized.find(key) != _monomorphized.end();
    }

    std::vector<std::pair<TypeRef, std::vector<TypeRef>>>
    GenericRegistry::get_all_instantiations() const
    {
        std::vector<std::pair<TypeRef, std::vector<TypeRef>>> result;
        result.reserve(_instantiations.size());

        for (const auto &[key, instantiated_type] : _instantiations)
        {
            if (instantiated_type.is_valid() &&
                instantiated_type->kind() == TypeKind::InstantiatedType)
            {
                auto *inst = static_cast<const InstantiatedType *>(instantiated_type.get());
                result.emplace_back(inst->generic_base(), inst->type_args());
            }
        }
        return result;
    }

    TypeSubstitution GenericRegistry::create_substitution(
        TypeRef generic_type,
        const std::vector<TypeRef> &type_args) const
    {
        auto tmpl = get_template(generic_type);
        if (!tmpl)
        {
            // Template not found by TypeID - this is a bug in the type system.
            // The type should have been registered with a consistent TypeID.
            std::string type_name = generic_type.is_valid() ? generic_type->display_name() : "<invalid>";
            std::string type_id_str = generic_type.is_valid() ? std::to_string(generic_type.id().id) : "invalid";

            LOG_ERROR(LogComponent::GENERAL,
                      "GenericRegistry::create_substitution: Template '{}' (TypeID={}) not found. "
                      "This indicates a TypeID mismatch - the type was likely registered with a different TypeID.",
                      type_name, type_id_str);

            diag_emitter().emit(
                Diag::error(ErrorCode::E0301_GENERIC_TYPE_RESOLUTION_FAILED,
                            "generic template '" + type_name + "' not found (TypeID mismatch)")
                    .with_note("The type was registered with a different TypeID than expected.")
                    .help("Ensure the generic type is registered consistently across modules."));

            return TypeSubstitution();
        }

        return TypeSubstitution(*tmpl, type_args);
    }

    TypeRef GenericRegistry::substitute(TypeRef type,
                                         const TypeSubstitution &subst,
                                         TypeArena &arena) const
    {
        return subst.apply(type, arena);
    }

    bool GenericRegistry::validate_type_args(TypeRef generic_type,
                                              const std::vector<TypeRef> &type_args,
                                              std::string *error_msg) const
    {
        auto tmpl = get_template(generic_type);
        if (!tmpl)
        {
            // Template not found by TypeID - this is a bug in the type system.
            // Do NOT fall back to name-based lookup as that masks TypeID inconsistencies.
            std::string type_name = generic_type.is_valid() ? generic_type->display_name() : "<invalid>";
            std::string type_id_str = generic_type.is_valid() ? std::to_string(generic_type.id().id) : "invalid";

            LOG_ERROR(LogComponent::GENERAL,
                      "GenericRegistry::validate_type_args: Template '{}' (TypeID={}) not found. "
                      "This indicates a TypeID mismatch - the type was likely registered with a different TypeID.",
                      type_name, type_id_str);

            if (error_msg)
            {
                *error_msg = "'" + type_name + "' is not a registered generic type (TypeID=" + type_id_str +
                             ") - possible TypeID mismatch across modules";
            }
            return false;
        }

        // Check argument count
        if (type_args.size() != tmpl->params.size())
        {
            if (error_msg)
            {
                std::ostringstream oss;
                oss << "expected " << tmpl->params.size() << " type argument(s), got "
                    << type_args.size();
                *error_msg = oss.str();
            }
            return false;
        }

        // Check for error types in arguments
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (type_args[i].is_error())
            {
                if (error_msg)
                {
                    *error_msg = "type argument " + std::to_string(i + 1) + " is an error type";
                }
                return false;
            }
        }

        // TODO: Check bounds (trait constraints)
        // For now, we accept any types
        // Traits are a future feature, and right now there is no way
        // to specify or check them. DO NOT IMPLEMENT THIS YET OR ANYTIME SOON.

        return true;
    }

    bool GenericRegistry::check_bounds(const GenericTemplate &tmpl,
                                        const std::vector<TypeRef> &type_args) const
    {
        // TODO: Implement bound checking when trait system is integrated
        // For now, all types satisfy all bounds
        return true;
    }

    InstantiationKey GenericRegistry::make_key(TypeRef base,
                                                const std::vector<TypeRef> &args) const
    {
        InstantiationKey key;
        key.base_type = base.id();
        key.type_args.reserve(args.size());
        for (const auto &arg : args)
        {
            key.type_args.push_back(arg.id());
        }
        return key;
    }

    void GenericRegistry::cache_instantiation(const InstantiationKey &key, TypeRef instantiated)
    {
        _instantiations[key] = instantiated;
    }

} // namespace Cryo
