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

        // Map specialized_name to key for lookup by name later
        std::string key = generate_key(template_type, type_args);
        _name_to_key[specialized_name] = key;

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

                        // Request instantiation for nested generic types
                        request_nested_instantiation(substituted);

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

                    // Request instantiation for nested generic types
                    request_nested_instantiation(substituted);

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

                // Try annotation-based substitution first for generic type params,
                // since resolved types may have different TypeIDs than the template params
                if (field->has_type_annotation())
                {
                    std::string type_name = field->type_annotation()->name;

                    LOG_DEBUG(LogComponent::GENERAL,
                              "Monomorphizer::create_concrete_struct: field '{}' annotation name='{}', kind={}, type_params=[{}]",
                              field->name(), type_name, static_cast<int>(field->type_annotation()->kind),
                              [&]() { std::string s; for (size_t j = 0; j < type_params.size(); ++j) { if (j) s += ", "; s += type_params[j]; } return s; }());

                    // Check if it's a type parameter that needs substitution
                    for (size_t i = 0; i < type_params.size(); ++i)
                    {
                        if (type_name == type_params[i] && i < type_args.size())
                        {
                            field_type = type_args[i];
                            break;
                        }
                    }

                    // If not a type param, try annotation-based substitution for
                    // complex generic types (e.g., "HashMapEntry<K,V>*")
                    if (!field_type.is_valid() && type_name.find('<') != std::string::npos)
                    {
                        // Substitute type params in the annotation string
                        std::string substituted_name = type_name;
                        for (size_t i = 0; i < type_params.size() && i < type_args.size(); ++i)
                        {
                            std::string param = type_params[i];
                            std::string concrete = type_args[i]->display_name();
                            // Replace occurrences of the type param in the annotation
                            size_t pos = 0;
                            while ((pos = substituted_name.find(param, pos)) != std::string::npos)
                            {
                                // Make sure it's a whole token (not part of a longer identifier)
                                bool at_start = (pos == 0 || !std::isalnum(static_cast<unsigned char>(substituted_name[pos - 1])) && substituted_name[pos - 1] != '_');
                                bool at_end = (pos + param.size() >= substituted_name.size() ||
                                              (!std::isalnum(static_cast<unsigned char>(substituted_name[pos + param.size()])) && substituted_name[pos + param.size()] != '_'));
                                if (at_start && at_end)
                                {
                                    substituted_name.replace(pos, param.size(), concrete);
                                    pos += concrete.size();
                                }
                                else
                                {
                                    pos += param.size();
                                }
                            }
                        }

                        LOG_DEBUG(LogComponent::GENERAL,
                                  "Monomorphizer::create_concrete_struct: field '{}' substituted annotation '{}' -> '{}'",
                                  field->name(), type_name, substituted_name);

                        // Strip pointer suffix and resolve
                        int ptr_depth = 0;
                        std::string base_name = substituted_name;
                        while (!base_name.empty() && base_name.back() == '*')
                        {
                            base_name.pop_back();
                            ptr_depth++;
                        }

                        // Strip array suffix
                        int arr_depth = 0;
                        while (base_name.size() >= 2 && base_name.substr(base_name.size() - 2) == "[]")
                        {
                            base_name.erase(base_name.size() - 2);
                            arr_depth++;
                        }

                        // Try to resolve the base type (may be an instantiated generic)
                        if (base_name.find('<') != std::string::npos)
                        {
                            // Parse out the generic base and args
                            size_t open = base_name.find('<');
                            size_t close = base_name.rfind('>');
                            if (open != std::string::npos && close != std::string::npos)
                            {
                                std::string gen_base = base_name.substr(0, open);
                                std::string args_str = base_name.substr(open + 1, close - open - 1);

                                // Build mangled name: Base_Arg1_Arg2
                                std::string mangled = gen_base;
                                // Split args by comma (respecting nested generics)
                                std::vector<std::string> arg_strs;
                                int depth = 0;
                                size_t arg_start = 0;
                                for (size_t ci = 0; ci <= args_str.size(); ++ci)
                                {
                                    if (ci == args_str.size() || (args_str[ci] == ',' && depth == 0))
                                    {
                                        std::string a = args_str.substr(arg_start, ci - arg_start);
                                        while (!a.empty() && a.front() == ' ') a.erase(0, 1);
                                        while (!a.empty() && a.back() == ' ') a.pop_back();
                                        if (!a.empty()) arg_strs.push_back(a);
                                        arg_start = ci + 1;
                                    }
                                    else if (args_str[ci] == '<') depth++;
                                    else if (args_str[ci] == '>') depth--;
                                }
                                for (const auto &a : arg_strs)
                                {
                                    mangled += "_" + a;
                                }
                                // Convert X[] to Array<X> ([] is built-in Array<T>)
                                while (mangled.find("[]") != std::string::npos)
                                {
                                    size_t bracket_pos = mangled.find("[]");
                                    size_t name_start = bracket_pos;
                                    while (name_start > 0 && mangled[name_start - 1] != '_' &&
                                           mangled[name_start - 1] != '<' && mangled[name_start - 1] != ',')
                                    {
                                        name_start--;
                                    }
                                    std::string before = mangled.substr(0, name_start);
                                    std::string type_name_part = mangled.substr(name_start, bracket_pos - name_start);
                                    std::string after = mangled.substr(bracket_pos + 2);
                                    mangled = before + "Array<" + type_name_part + ">" + after;
                                }

                                // Protect Array<...> brackets from sanitization
                                const char kOpen = '\x01';
                                const char kClose = '\x02';
                                {
                                    size_t pos = 0;
                                    while ((pos = mangled.find("Array<", pos)) != std::string::npos)
                                    {
                                        mangled[pos + 5] = kOpen;
                                        int depth = 1;
                                        for (size_t ci = pos + 6; ci < mangled.size() && depth > 0; ++ci)
                                        {
                                            if (mangled[ci] == '<') depth++;
                                            else if (mangled[ci] == '>') { depth--; if (depth == 0) mangled[ci] = kClose; }
                                        }
                                        pos += 6;
                                    }
                                }

                                // Sanitize mangled name (Array<> brackets are protected)
                                std::replace(mangled.begin(), mangled.end(), '<', '_');
                                std::replace(mangled.begin(), mangled.end(), '>', '_');
                                std::replace(mangled.begin(), mangled.end(), ',', '_');
                                std::replace(mangled.begin(), mangled.end(), ' ', '_');
                                std::replace(mangled.begin(), mangled.end(), '*', 'p');
                                std::replace(mangled.begin(), mangled.end(), '&', 'r');
                                std::replace(mangled.begin(), mangled.end(), '[', '_');
                                std::replace(mangled.begin(), mangled.end(), ']', '_');

                                // Restore Array<> brackets
                                std::replace(mangled.begin(), mangled.end(), kOpen, '<');
                                std::replace(mangled.begin(), mangled.end(), kClose, '>');

                                // Remove trailing underscores
                                while (!mangled.empty() && mangled.back() == '_') mangled.pop_back();
                                // Remove consecutive underscores
                                std::string cleaned;
                                for (size_t ci = 0; ci < mangled.size(); ++ci)
                                {
                                    if (mangled[ci] == '_' && ci + 1 < mangled.size() && mangled[ci + 1] == '_')
                                        continue;
                                    cleaned += mangled[ci];
                                }
                                mangled = cleaned;

                                field_type = _arena.lookup_type_by_name(mangled);
                                if (!field_type.is_valid())
                                {
                                    // Also try looking up via the generic registry
                                    field_type = _arena.lookup_type_by_name(gen_base + "<" + args_str + ">");
                                }

                                // If still not found, try to create the instantiation
                                // The nested generic type may not have been instantiated yet
                                if (!field_type.is_valid())
                                {
                                    // Look up the base generic type
                                    TypeRef base_generic = _arena.lookup_type_by_name(gen_base);
                                    if (base_generic.is_valid())
                                    {
                                        // Resolve each arg string to a TypeRef
                                        std::vector<TypeRef> nested_args;
                                        bool all_resolved = true;
                                        for (const auto &a : arg_strs)
                                        {
                                            // Strip trailing [] and * modifiers
                                            std::string base_a = a;
                                            int arr_depth = 0;
                                            int ptr_depth = 0;
                                            while (base_a.size() >= 2 && base_a.substr(base_a.size() - 2) == "[]")
                                            {
                                                base_a.erase(base_a.size() - 2);
                                                arr_depth++;
                                            }
                                            while (!base_a.empty() && base_a.back() == '*')
                                            {
                                                base_a.pop_back();
                                                ptr_depth++;
                                            }

                                            TypeRef arg_ref = _arena.lookup_type_by_name(base_a);
                                            if (!arg_ref.is_valid())
                                            {
                                                // Try primitives
                                                if (base_a == "string") arg_ref = _arena.get_string();
                                                else if (base_a == "i32" || base_a == "int") arg_ref = _arena.get_i32();
                                                else if (base_a == "i64") arg_ref = _arena.get_i64();
                                                else if (base_a == "u32") arg_ref = _arena.get_u32();
                                                else if (base_a == "u64") arg_ref = _arena.get_u64();
                                                else if (base_a == "u8") arg_ref = _arena.get_u8();
                                                else if (base_a == "boolean") arg_ref = _arena.get_bool();
                                                else if (base_a == "f32") arg_ref = _arena.get_f32();
                                                else if (base_a == "f64") arg_ref = _arena.get_f64();
                                                else if (base_a == "void") arg_ref = _arena.get_void();
                                            }
                                            // Re-wrap with pointer/array modifiers
                                            if (arg_ref.is_valid())
                                            {
                                                for (int pi = 0; pi < ptr_depth; ++pi)
                                                    arg_ref = _arena.get_pointer_to(arg_ref);
                                                for (int ai = 0; ai < arr_depth; ++ai)
                                                    arg_ref = _arena.get_array_of(arg_ref, std::nullopt);
                                            }
                                            if (arg_ref.is_valid())
                                            {
                                                nested_args.push_back(arg_ref);
                                            }
                                            else
                                            {
                                                all_resolved = false;
                                                break;
                                            }
                                        }

                                        if (all_resolved && !nested_args.empty())
                                        {
                                            // Synchronously specialize the nested type so it exists
                                            // before we continue building the parent struct
                                            specialize(base_generic, nested_args);

                                            // Now look up the concrete type by mangled name
                                            field_type = _arena.lookup_type_by_name(mangled);
                                            if (!field_type.is_valid())
                                            {
                                                // Also try the instantiation type
                                                field_type = _arena.create_instantiation(base_generic, nested_args);
                                                _arena.register_instantiated_by_name(field_type);
                                            }

                                            LOG_DEBUG(LogComponent::GENERAL,
                                                      "Monomorphizer::create_concrete_struct: created instantiation for '{}' -> valid={}",
                                                      base_name, field_type.is_valid());
                                        }
                                    }
                                }

                                LOG_DEBUG(LogComponent::GENERAL,
                                          "Monomorphizer::create_concrete_struct: resolved generic '{}' (mangled='{}') -> valid={}",
                                          base_name, mangled, field_type.is_valid());
                            }
                        }
                        else
                        {
                            field_type = _arena.lookup_type_by_name(base_name);
                        }

                        // Apply pointer wrapping
                        if (field_type.is_valid())
                        {
                            for (int p = 0; p < arr_depth; ++p)
                                field_type = _arena.get_array_of(field_type, std::nullopt);
                            for (int p = 0; p < ptr_depth; ++p)
                                field_type = _arena.get_pointer_to(field_type);
                        }
                    }

                    // If not a type param, try resolved type or name lookup
                    if (!field_type.is_valid())
                    {
                        if (field->has_resolved_type())
                        {
                            field_type = field->get_resolved_type();
                        }
                        else
                        {
                            field_type = _arena.lookup_type_by_name(type_name);
                        }
                    }
                }
                // Fallback to resolved type if no annotation
                else if (field->has_resolved_type())
                {
                    field_type = field->get_resolved_type();

                    // If the resolved type is a GenericParam, substitute by name
                    // (TypeID-based substitution may fail due to ID mismatch between passes)
                    if (field_type->kind() == TypeKind::GenericParam)
                    {
                        auto *gp = static_cast<const GenericParamType *>(field_type.get());
                        for (size_t i = 0; i < type_params.size(); ++i)
                        {
                            if (gp->param_name() == type_params[i] && i < type_args.size())
                            {
                                field_type = type_args[i];
                                break;
                            }
                        }
                    }
                }

                if (field_type.is_valid())
                {
                    // Handle InstantiatedType fields (e.g., Inner<T>) where generic param
                    // TypeIDs may differ between templates. The T in Inner<T> resolved to
                    // Inner's own GenericParam TypeID, but the substitution map uses Outer's
                    // TypeID. Substitute by name before applying the TypeID-based substitution.
                    if (field_type->kind() == TypeKind::InstantiatedType)
                    {
                        auto *inst = static_cast<const InstantiatedType *>(field_type.get());
                        std::vector<TypeRef> new_args;
                        bool args_changed = false;

                        for (const auto &arg : inst->type_args())
                        {
                            if (arg->kind() == TypeKind::GenericParam)
                            {
                                auto *gp = static_cast<const GenericParamType *>(arg.get());
                                bool found = false;
                                for (size_t j = 0; j < type_params.size(); ++j)
                                {
                                    if (gp->param_name() == type_params[j] && j < type_args.size())
                                    {
                                        new_args.push_back(type_args[j]);
                                        args_changed = true;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                    new_args.push_back(arg);
                            }
                            else
                            {
                                new_args.push_back(arg);
                            }
                        }

                        if (args_changed)
                        {
                            field_type = _arena.create_instantiation(inst->generic_base(), std::move(new_args));
                            _arena.register_instantiated_by_name(field_type);
                        }
                    }

                    // Apply substitution in case it's a compound type with generic params
                    TypeRef substituted = subst.apply(field_type, _arena);

                    // Request instantiation for nested generic types
                    // e.g., when HashSet<string> has field entries: HashSetEntry<T>*
                    // we need to monomorphize HashSetEntry<string> as well
                    request_nested_instantiation(substituted);

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

                // Request instantiation for nested generic types
                request_nested_instantiation(substituted_type);

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

    std::optional<SpecializationEntry> Monomorphizer::get_specialization_by_name(
        const std::string &specialized_name) const
    {
        // Look up the key by specialized_name
        auto name_it = _name_to_key.find(specialized_name);
        if (name_it == _name_to_key.end())
        {
            return std::nullopt;
        }

        // Then get the specialization by key
        return get_specialization(name_it->second);
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
    // Nested Type Instantiation
    // ========================================================================

    void Monomorphizer::request_nested_instantiation(TypeRef type)
    {
        if (!type.is_valid())
            return;

        const Type *t = type.get();

        // Direct InstantiatedType - request monomorphization if not already done
        if (t->kind() == TypeKind::InstantiatedType)
        {
            auto *inst = static_cast<const InstantiatedType *>(t);
            TypeRef base = inst->generic_base();

            if (base.is_valid() && !inst->type_args().empty())
            {
                // Check if already monomorphized
                if (!_generics.is_monomorphized(base, inst->type_args()))
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "Monomorphizer: Requesting nested instantiation of {}",
                              inst->display_name());

                    // Add to pending requests
                    add_request(base, inst->type_args(), SourceLocation{}, ModuleID::invalid());
                }
            }

            // Recursively check type arguments for nested instantiated types
            for (const auto &arg : inst->type_args())
            {
                request_nested_instantiation(arg);
            }
            return;
        }

        // PointerType - check pointee
        if (t->kind() == TypeKind::Pointer)
        {
            auto *ptr = static_cast<const PointerType *>(t);
            request_nested_instantiation(ptr->pointee());
            return;
        }

        // ReferenceType - check referent
        if (t->kind() == TypeKind::Reference)
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            request_nested_instantiation(ref->referent());
            return;
        }

        // ArrayType - check element
        if (t->kind() == TypeKind::Array)
        {
            auto *arr = static_cast<const ArrayType *>(t);
            request_nested_instantiation(arr->element());
            return;
        }

        // OptionalType - check wrapped
        if (t->kind() == TypeKind::Optional)
        {
            auto *opt = static_cast<const OptionalType *>(t);
            request_nested_instantiation(opt->wrapped());
            return;
        }

        // FunctionType - check return type and parameters
        if (t->kind() == TypeKind::Function)
        {
            auto *fn = static_cast<const FunctionType *>(t);
            request_nested_instantiation(fn->return_type());
            for (const auto &param : fn->param_types())
            {
                request_nested_instantiation(param);
            }
            return;
        }

        // TupleType - check all elements
        if (t->kind() == TypeKind::Tuple)
        {
            auto *tup = static_cast<const TupleType *>(t);
            for (const auto &elem : tup->elements())
            {
                request_nested_instantiation(elem);
            }
            return;
        }
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

                    // Convert X[] to Array<X> ([] is the built-in Array<T> type)
                    while (arg_name.size() >= 2 &&
                           arg_name.substr(arg_name.size() - 2) == "[]")
                    {
                        arg_name = "Array<" + arg_name.substr(0, arg_name.size() - 2) + ">";
                    }

                    // Protect Array<...> angle brackets from sanitization
                    // by temporarily replacing them with placeholders
                    const char kOpen = '\x01';
                    const char kClose = '\x02';
                    {
                        size_t pos = 0;
                        while ((pos = arg_name.find("Array<", pos)) != std::string::npos)
                        {
                            arg_name[pos + 5] = kOpen; // replace < after "Array"
                            // Find the matching >
                            int depth = 1;
                            for (size_t ci = pos + 6; ci < arg_name.size() && depth > 0; ++ci)
                            {
                                if (arg_name[ci] == '<') depth++;
                                else if (arg_name[ci] == '>') { depth--; if (depth == 0) arg_name[ci] = kClose; }
                            }
                            pos += 6;
                        }
                    }

                    // Replace special characters (Array<> brackets are protected)
                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ':', '_');
                    std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                    std::replace(arg_name.begin(), arg_name.end(), '&', 'r');
                    std::replace(arg_name.begin(), arg_name.end(), '[', '_');
                    std::replace(arg_name.begin(), arg_name.end(), ']', '_');

                    // Restore Array<> brackets
                    std::replace(arg_name.begin(), arg_name.end(), kOpen, '<');
                    std::replace(arg_name.begin(), arg_name.end(), kClose, '>');

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
