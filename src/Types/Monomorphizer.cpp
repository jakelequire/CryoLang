/******************************************************************************
 * @file Monomorphizer.cpp
 * @brief Implementation of Monomorphizer for Cryo's new type system
 ******************************************************************************/

#include "Types/Monomorphizer.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/ErrorType.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/Logger.hpp"
#include "AST/ASTNode.hpp"

#include <sstream>
#include <algorithm>

namespace Cryo
{
    // ========================================================================
    // MonomorphRequest Implementation
    // ========================================================================

    std::string MonomorphRequest::key() const
    {
        std::ostringstream oss;
        if (generic_type.is_valid())
        {
            oss << generic_type.id().id;
        }
        else
        {
            oss << "invalid";
        }
        oss << "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            if (type_args[i].is_valid())
            {
                oss << type_args[i].id().id;
            }
            else
            {
                oss << "?";
            }
        }
        oss << ">";
        return oss.str();
    }

    // ========================================================================
    // Monomorphizer Implementation
    // ========================================================================

    Monomorphizer::Monomorphizer(TypeArena &arena,
                                   GenericRegistry &generics,
                                   ModuleTypeRegistry &modules)
        : _arena(arena),
          _generics(generics),
          _modules(modules)
    {
    }

    // ========================================================================
    // Request Management
    // ========================================================================

    void Monomorphizer::add_request(TypeRef generic_type,
                                      std::vector<TypeRef> type_args,
                                      SourceLocation location,
                                      ModuleID source_module)
    {
        // Check if already specialized
        std::string key = generate_key(generic_type, type_args);
        if (has_specialization(key))
        {
            return; // Already done
        }

        // Check if already pending
        for (const auto &req : _pending_requests)
        {
            if (req.key() == key)
            {
                return; // Already pending
            }
        }

        _pending_requests.emplace_back(generic_type, std::move(type_args),
                                        location, source_module);
    }

    void Monomorphizer::add_request(const InstantiationRequest &request)
    {
        add_request(request.generic_type, request.type_args,
                    request.location, ModuleID::invalid());
    }

    void Monomorphizer::import_pending_from_registry()
    {
        for (const auto &request : _generics.pending_instantiations())
        {
            add_request(request);
        }
    }

    void Monomorphizer::import_cached_instantiations()
    {
        auto all_instantiations = _generics.get_all_instantiations();

        for (const auto &[base_type, type_args] : all_instantiations)
        {
            // Skip if already monomorphized
            if (_generics.is_monomorphized(base_type, type_args))
            {
                continue;
            }

            // Add to pending requests
            add_request(base_type, type_args, SourceLocation{}, ModuleID::invalid());
        }
    }

    // ========================================================================
    // Processing
    // ========================================================================

    bool Monomorphizer::process_all()
    {
        bool all_success = true;

        // Process until no more pending requests
        while (!_pending_requests.empty())
        {
            // Take the first request
            MonomorphRequest request = std::move(_pending_requests.front());
            _pending_requests.erase(_pending_requests.begin());

            auto result = process_request(request);
            if (!result.is_ok())
            {
                all_success = false;
                // Could log error here
            }
        }

        return all_success;
    }

    MonomorphResult Monomorphizer::process_request(const MonomorphRequest &request)
    {
        std::string key = request.key();

        // Check if already specialized
        auto cached = get_specialization(key);
        if (cached)
        {
            return MonomorphResult::ok(cached->specialized_type, cached->ast_node);
        }

        // Check for circular instantiation
        if (is_circular(key))
        {
            return MonomorphResult::error("circular instantiation detected: " + key);
        }

        // Mark as in progress
        _in_progress.insert(key);

        // Perform specialization
        auto result = specialize(request.generic_type, request.type_args);

        // Remove from in progress
        _in_progress.erase(key);

        if (result.is_ok())
        {
            cache_specialization(key, result.specialized_type, result.specialized_ast);
        }

        return result;
    }

    MonomorphResult Monomorphizer::specialize(TypeRef generic_type,
                                                 const std::vector<TypeRef> &type_args)
    {
        if (!generic_type.is_valid())
        {
            return MonomorphResult::error("invalid generic type");
        }

        // Get the template by TypeID - do NOT fall back to name-based lookup
        // as that masks TypeID inconsistencies that should be fixed properly
        TypeRef template_type = generic_type;
        auto tmpl = _generics.get_template(generic_type);
        if (!tmpl)
        {
            // Template not found by TypeID - this is a bug in the type system.
            std::string type_name = generic_type->display_name();
            std::string type_id_str = std::to_string(generic_type.id().id);

            LOG_ERROR(LogComponent::GENERAL,
                      "Monomorphizer::specialize: Template '{}' (TypeID={}) not found. "
                      "This indicates a TypeID mismatch - the type was likely registered with a different TypeID.",
                      type_name, type_id_str);

            diag_emitter().emit(
                Diag::error(ErrorCode::E0301_GENERIC_TYPE_RESOLUTION_FAILED,
                            "cannot monomorphize '" + type_name + "': generic template not found (TypeID mismatch)")
                    .with_note("TypeID=" + type_id_str + " is not registered as a generic template.")
                    .help("Ensure the generic type is registered consistently across modules."));

            return MonomorphResult::error("template '" + type_name + "' not found (TypeID=" + type_id_str + ")");
        }

        // Validate the instantiation
        std::string error_msg;
        if (!_generics.validate_type_args(template_type, type_args, &error_msg))
        {
            return MonomorphResult::error(error_msg);
        }

        // Create the instantiated type via GenericRegistry
        TypeRef instantiated = _generics.instantiate(template_type, type_args, _arena);

        if (instantiated.is_error())
        {
            auto *err = static_cast<const ErrorType *>(instantiated.get());
            return MonomorphResult::error(err->reason());
        }

        // Create type substitution for resolving generic params
        TypeSubstitution subst = create_substitution(template_type, type_args);
        std::string specialized_name = generate_specialized_name(template_type, type_args);

        // Create concrete resolved type based on the base type's kind
        TypeRef resolved_type;
        const Type *base_type = template_type.get();

        LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::specialize: {} base_type kind={}, specialized_name={}",
                  template_type->display_name(), static_cast<int>(base_type->kind()), specialized_name);

        if (base_type->kind() == TypeKind::Enum)
        {
            // Create concrete enum with substituted variants
            // Use AST node to get variant info since EnumType may not have variants set for generics
            auto *enum_type = static_cast<const EnumType *>(base_type);
            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::specialize: Enum {} has {} variants, is_complete={}",
                      enum_type->name(), enum_type->variant_count(), enum_type->is_complete());

            ASTNode *ast_node = tmpl ? tmpl->ast_node : nullptr;
            resolved_type = create_concrete_enum(base_type, subst, specialized_name, type_args, ast_node);
        }
        else if (base_type->kind() == TypeKind::Struct)
        {
            // Create concrete struct with substituted fields
            // Use AST node to get field info since StructType may not have fields set for generics
            auto *struct_type = static_cast<const StructType *>(base_type);
            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::specialize: Struct {} has {} fields, is_complete={}",
                      struct_type->name(), struct_type->field_count(), struct_type->is_complete());
            ASTNode *ast_node = tmpl ? tmpl->ast_node : nullptr;
            resolved_type = create_concrete_struct(base_type, subst, specialized_name, type_args, ast_node);
        }

        // Set the resolved type on the InstantiatedType
        if (resolved_type.is_valid())
        {
            auto *inst_type = const_cast<InstantiatedType *>(
                static_cast<const InstantiatedType *>(instantiated.get()));
            inst_type->set_resolved_type(resolved_type);
        }

        // If we have an AST specializer, invoke it
        ASTNode *specialized_ast = nullptr;
        if (_ast_specializer && tmpl)
        {
            specialized_ast = _ast_specializer(*tmpl, subst, specialized_name);
        }

        // Mark as monomorphized in the registry
        _generics.mark_monomorphized(template_type, type_args);

        return MonomorphResult::ok(instantiated, specialized_ast);
    }

    TypeRef Monomorphizer::create_concrete_enum(const Type *base_enum_type,
                                                 const TypeSubstitution &subst,
                                                 const std::string &specialized_name,
                                                 const std::vector<TypeRef> &type_args,
                                                 ASTNode *ast_node)
    {
        auto *enum_type = static_cast<const EnumType *>(base_enum_type);

        // Create a new qualified name for the concrete enum
        QualifiedTypeName concrete_name;
        concrete_name.name = specialized_name;
        concrete_name.module = enum_type->module();

        // Create the concrete enum type
        TypeRef concrete_enum = _arena.create_enum(concrete_name);
        if (!concrete_enum.is_valid())
        {
            return TypeRef{};
        }

        // Get the concrete enum type to set its variants
        auto *concrete = const_cast<EnumType *>(
            static_cast<const EnumType *>(concrete_enum.get()));

        // Create substituted variants from AST if available (generic enums don't have variants in type system)
        std::vector<EnumVariant> concrete_variants;

        auto *enum_decl = ast_node ? dynamic_cast<EnumDeclarationNode *>(ast_node) : nullptr;
        if (enum_decl)
        {
            // Get type parameters from the enum declaration
            std::vector<std::string> type_params;
            for (const auto &param : enum_decl->generic_parameters())
            {
                type_params.push_back(param->name());
            }

            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_enum: {} with {} variants from AST",
                      specialized_name, enum_decl->variants().size());

            size_t tag = 0;
            for (const auto &variant : enum_decl->variants())
            {
                std::vector<TypeRef> substituted_payload;

                // Handle variant payload types (associated_types are strings like "T", "E", "i32")
                for (const auto &type_name : variant->associated_types())
                {
                    TypeRef payload_type;

                    // Check if it's a type parameter that needs substitution
                    for (size_t i = 0; i < type_params.size(); ++i)
                    {
                        if (type_name == type_params[i] && i < type_args.size())
                        {
                            payload_type = type_args[i];
                            break;
                        }
                    }

                    // If not a type param, try to look it up as a named type
                    if (!payload_type.is_valid())
                    {
                        payload_type = _arena.lookup_type_by_name(type_name);
                    }

                    if (payload_type.is_valid())
                    {
                        // Apply substitution in case it's a compound type with generic params
                        TypeRef substituted = subst.apply(payload_type, _arena);
                        substituted_payload.push_back(substituted);
                    }
                    else
                    {
                        LOG_WARN(LogComponent::GENERAL,
                                 "Monomorphizer::create_concrete_enum: Could not resolve payload type '{}' for variant {}",
                                 type_name, variant->name());
                    }
                }

                EnumVariant concrete_variant(variant->name(), std::move(substituted_payload), tag++);
                concrete_variants.push_back(std::move(concrete_variant));

                LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_enum: Added variant {} with {} payload types",
                          variant->name(), concrete_variants.back().payload_types.size());
            }
        }
        else if (enum_type->variant_count() > 0)
        {
            // Fallback to type system variants if available
            for (const auto &orig_variant : enum_type->variants())
            {
                std::vector<TypeRef> substituted_payload;
                for (const auto &payload_type : orig_variant.payload_types)
                {
                    TypeRef substituted = subst.apply(payload_type, _arena);
                    substituted_payload.push_back(substituted);
                }

                EnumVariant concrete_variant(orig_variant.name, std::move(substituted_payload), orig_variant.tag_value);
                concrete_variants.push_back(std::move(concrete_variant));
            }
        }

        // Set the variants to complete the enum type
        if (!concrete_variants.empty())
        {
            concrete->set_variants(std::move(concrete_variants));
            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_enum: Completed {} with {} variants",
                      specialized_name, concrete->variant_count());
        }

        return concrete_enum;
    }

    TypeRef Monomorphizer::create_concrete_struct(const Type *base_struct_type,
                                                   const TypeSubstitution &subst,
                                                   const std::string &specialized_name,
                                                   const std::vector<TypeRef> &type_args,
                                                   ASTNode *ast_node)
    {
        auto *struct_type = static_cast<const StructType *>(base_struct_type);

        // Create a new qualified name for the concrete struct
        QualifiedTypeName concrete_name;
        concrete_name.name = specialized_name;
        concrete_name.module = struct_type->module();

        // Create the concrete struct type
        TypeRef concrete_struct = _arena.create_struct(concrete_name);
        if (!concrete_struct.is_valid())
        {
            return TypeRef{};
        }

        // Get the concrete struct type to set its fields
        auto *concrete = const_cast<StructType *>(
            static_cast<const StructType *>(concrete_struct.get()));

        // Create substituted fields from AST if available (generic structs don't have fields in type system)
        std::vector<FieldInfo> concrete_fields;

        auto *struct_decl = ast_node ? dynamic_cast<StructDeclarationNode *>(ast_node) : nullptr;
        if (struct_decl)
        {
            // Get type parameters from the struct declaration
            std::vector<std::string> type_params;
            for (const auto &param : struct_decl->generic_parameters())
            {
                type_params.push_back(param->name());
            }

            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_struct: {} with {} fields from AST",
                      specialized_name, struct_decl->fields().size());

            for (const auto &field : struct_decl->fields())
            {
                TypeRef field_type;

                // First try to get the resolved type from the field
                if (field->has_resolved_type())
                {
                    field_type = field->get_resolved_type();
                }
                // Otherwise, try to resolve from type annotation
                else if (field->has_type_annotation())
                {
                    std::string type_name = field->type_annotation()->name;

                    // Check if it's a type parameter that needs substitution
                    for (size_t i = 0; i < type_params.size(); ++i)
                    {
                        if (type_name == type_params[i] && i < type_args.size())
                        {
                            field_type = type_args[i];
                            break;
                        }
                    }

                    // If not a type param, try to look it up as a named type
                    if (!field_type.is_valid())
                    {
                        field_type = _arena.lookup_type_by_name(type_name);
                    }
                }

                if (field_type.is_valid())
                {
                    // Apply substitution in case it's a compound type with generic params
                    TypeRef substituted = subst.apply(field_type, _arena);

                    FieldInfo concrete_field(
                        field->name(),
                        substituted,
                        0, // Offset will be recomputed by set_fields
                        field->visibility() == Visibility::Public,
                        true); // is_mutable
                    concrete_fields.push_back(std::move(concrete_field));

                    LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_struct: Added field {} with type {}",
                              field->name(), substituted->display_name());
                }
                else
                {
                    LOG_WARN(LogComponent::GENERAL,
                             "Monomorphizer::create_concrete_struct: Could not resolve field type for {} in {}",
                             field->name(), specialized_name);
                }
            }
        }
        else if (struct_type->field_count() > 0)
        {
            // Fallback to type system fields if available
            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_struct: {} with {} fields from type system",
                      specialized_name, struct_type->field_count());

            for (const auto &orig_field : struct_type->fields())
            {
                TypeRef substituted_type = subst.apply(orig_field.type, _arena);

                FieldInfo concrete_field(
                    orig_field.name,
                    substituted_type,
                    0, // Offset will be recomputed by set_fields
                    orig_field.is_public,
                    orig_field.is_mutable);
                concrete_fields.push_back(std::move(concrete_field));
            }
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL,
                     "Monomorphizer::create_concrete_struct: No fields found for {} (no AST, no type system fields)",
                     specialized_name);
        }

        // Set the fields to complete the struct type
        if (!concrete_fields.empty())
        {
            concrete->set_fields(std::move(concrete_fields));
            LOG_DEBUG(LogComponent::GENERAL, "Monomorphizer::create_concrete_struct: Completed {} with {} fields",
                      specialized_name, concrete->field_count());
        }

        return concrete_struct;
    }

    // ========================================================================
    // Specialization Cache
    // ========================================================================

    bool Monomorphizer::has_specialization(const std::string &key) const
    {
        return _specializations.find(key) != _specializations.end();
    }

    std::optional<SpecializationEntry> Monomorphizer::get_specialization(
        const std::string &key) const
    {
        auto it = _specializations.find(key);
        if (it != _specializations.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    void Monomorphizer::cache_specialization(const std::string &key,
                                               TypeRef type,
                                               ASTNode *ast)
    {
        SpecializationEntry entry;
        entry.specialized_type = type;
        entry.ast_node = ast;
        entry.code_generated = false;

        _specializations[key] = entry;
    }

    void Monomorphizer::mark_generated(const std::string &key)
    {
        auto it = _specializations.find(key);
        if (it != _specializations.end())
        {
            it->second.code_generated = true;
        }
    }

    size_t Monomorphizer::generated_count() const
    {
        size_t count = 0;
        for (const auto &[key, entry] : _specializations)
        {
            if (entry.code_generated)
            {
                ++count;
            }
        }
        return count;
    }

    // ========================================================================
    // Type Substitution
    // ========================================================================

    TypeSubstitution Monomorphizer::create_substitution(
        TypeRef generic_type,
        const std::vector<TypeRef> &type_args)
    {
        return _generics.create_substitution(generic_type, type_args);
    }

    TypeRef Monomorphizer::apply_substitution(TypeRef type,
                                                 const TypeSubstitution &subst)
    {
        return _generics.substitute(type, subst, _arena);
    }

    // ========================================================================
    // Name Generation
    // ========================================================================

    std::string Monomorphizer::generate_specialized_name(
        TypeRef base,
        const std::vector<TypeRef> &args)
    {
        if (!base.is_valid())
        {
            return "invalid";
        }

        std::ostringstream oss;
        oss << base.get()->display_name();

        if (!args.empty())
        {
            oss << "_";
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i > 0)
                    oss << "_";
                if (args[i].is_valid())
                {
                    // Use a simplified name for mangling
                    std::string arg_name = args[i].get()->display_name();

                    // Replace special characters
                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ':', '_');

                    oss << arg_name;
                }
                else
                {
                    oss << "unknown";
                }
            }
        }

        return oss.str();
    }

    std::string Monomorphizer::generate_key(TypeRef base,
                                               const std::vector<TypeRef> &args)
    {
        std::ostringstream oss;
        if (base.is_valid())
        {
            oss << base.id().id;
        }
        else
        {
            oss << "0";
        }

        oss << "<";
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            if (args[i].is_valid())
            {
                oss << args[i].id().id;
            }
            else
            {
                oss << "0";
            }
        }
        oss << ">";

        return oss.str();
    }

} // namespace Cryo
