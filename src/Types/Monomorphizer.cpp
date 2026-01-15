/******************************************************************************
 * @file Monomorphizer.cpp
 * @brief Implementation of Monomorphizer for Cryo's new type system
 ******************************************************************************/

#include "Types/Monomorphizer.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/ErrorType.hpp"

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

        // Validate the instantiation
        std::string error_msg;
        if (!_generics.validate_type_args(generic_type, type_args, &error_msg))
        {
            return MonomorphResult::error(error_msg);
        }

        // Create the instantiated type via GenericRegistry
        TypeRef instantiated = _generics.instantiate(generic_type, type_args, _arena);

        if (instantiated.is_error())
        {
            auto *err = static_cast<const ErrorType *>(instantiated.get());
            return MonomorphResult::error(err->reason());
        }

        // If we have an AST specializer, invoke it
        ASTNode *specialized_ast = nullptr;
        if (_ast_specializer)
        {
            auto tmpl = _generics.get_template(generic_type);
            if (tmpl)
            {
                TypeSubstitution subst = create_substitution(generic_type, type_args);
                std::string specialized_name = generate_specialized_name(generic_type, type_args);

                specialized_ast = _ast_specializer(*tmpl, subst, specialized_name);
            }
        }

        // Mark as monomorphized in the registry
        _generics.mark_monomorphized(generic_type, type_args);

        return MonomorphResult::ok(instantiated, specialized_ast);
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

    // ========================================================================
    // Utility Functions
    // ========================================================================

    std::vector<MonomorphRequest> collect_instantiation_requests(
        const ProgramNode &program,
        const GenericRegistry &generics)
    {
        std::vector<MonomorphRequest> requests;

        // Collect pending from registry
        for (const auto &pending : generics.pending_instantiations())
        {
            MonomorphRequest req;
            req.generic_type = pending.generic_type;
            req.type_args = pending.type_args;
            req.location = pending.location;
            req.source_module = ModuleID::invalid();

            requests.push_back(std::move(req));
        }

        // TODO: Also scan AST for instantiation sites
        // This would involve traversing the AST and finding:
        // - NewExpressionNode with generic types
        // - Function calls with generic parameters
        // - Variable declarations with generic types

        (void)program; // Will be used when AST scanning is implemented

        return requests;
    }

} // namespace Cryo
