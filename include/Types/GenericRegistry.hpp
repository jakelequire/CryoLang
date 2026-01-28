#pragma once
/******************************************************************************
 * @file GenericRegistry.hpp
 * @brief Generic template and instantiation registry for Cryo's new type system
 *
 * GenericRegistry manages:
 * - Registration of generic type templates (struct Array<T>, class Box<T>)
 * - Caching of instantiated types (Array<int>, Array<string>)
 * - Type substitution during instantiation
 *
 * This replaces the scattered template handling in the old TemplateRegistry
 * with a unified, type-system-aware approach.
 ******************************************************************************/

#include "Types/TypeID.hpp"
#include "Types/Type.hpp"
#include "Types/GenericTypes.hpp"
#include "Lexer/lexer.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>

namespace Cryo
{
    // Forward declarations
    class TypeArena;
    class ASTNode;

    /**************************************************************************
     * @brief Generic template information
     *
     * Stores metadata about a generic type template, including:
     * - The generic type itself (e.g., the uninstantiated Array<T>)
     * - Type parameters (T, U, etc.)
     * - AST node for monomorphization
     **************************************************************************/
    struct GenericTemplate
    {
        TypeRef generic_type;                    // The generic type
        std::vector<GenericParam> params;        // Type parameters [T, U, ...]
        ModuleID source_module;                  // Module where defined
        ASTNode *ast_node = nullptr;             // AST node for monomorphization
        std::string name;                        // Template name

        GenericTemplate() = default;
        GenericTemplate(TypeRef type, std::vector<GenericParam> ps,
                        ModuleID mod, ASTNode *ast = nullptr, std::string n = "")
            : generic_type(type), params(std::move(ps)),
              source_module(mod), ast_node(ast), name(std::move(n)) {}

        size_t param_count() const { return params.size(); }

        // Find parameter by name
        std::optional<size_t> find_param(const std::string &param_name) const
        {
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (params[i].name == param_name)
                    return i;
            }
            return std::nullopt;
        }
    };

    /**************************************************************************
     * @brief Key for caching instantiated generic types
     *
     * An instantiation is uniquely identified by:
     * - The base generic type
     * - The type arguments
     **************************************************************************/
    struct InstantiationKey
    {
        TypeID base_type;
        std::vector<TypeID> type_args;

        bool operator==(const InstantiationKey &other) const
        {
            if (base_type != other.base_type)
                return false;
            if (type_args.size() != other.type_args.size())
                return false;
            for (size_t i = 0; i < type_args.size(); ++i)
            {
                if (type_args[i] != other.type_args[i])
                    return false;
            }
            return true;
        }

        struct Hash
        {
            size_t operator()(const InstantiationKey &key) const
            {
                size_t hash = TypeID::Hash{}(key.base_type);
                for (const auto &arg : key.type_args)
                {
                    hash ^= TypeID::Hash{}(arg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                }
                return hash;
            }
        };
    };

    /**************************************************************************
     * @brief Pending instantiation request
     *
     * Records a request to instantiate a generic type. Used during
     * type checking to collect all instantiations before monomorphization.
     **************************************************************************/
    struct InstantiationRequest
    {
        TypeRef generic_type;
        std::vector<TypeRef> type_args;
        SourceLocation location;
        bool is_processed = false;

        InstantiationRequest() = default;
        InstantiationRequest(TypeRef gen, std::vector<TypeRef> args, SourceLocation loc)
            : generic_type(gen), type_args(std::move(args)), location(loc) {}
    };

    /**************************************************************************
     * @brief Type substitution map for generic instantiation
     *
     * Maps type parameters to concrete types during instantiation.
     **************************************************************************/
    class TypeSubstitution
    {
    private:
        std::unordered_map<TypeID, TypeRef, TypeID::Hash> _substitutions;

    public:
        TypeSubstitution() = default;

        // Build substitution from template and arguments
        TypeSubstitution(const GenericTemplate &tmpl, const std::vector<TypeRef> &args);

        // Add a substitution
        void add(TypeRef param, TypeRef replacement);

        // Get substitution for a type parameter
        std::optional<TypeRef> get(TypeRef param) const;
        std::optional<TypeRef> get(TypeID param_id) const;

        // Check if a type parameter has a substitution
        bool has(TypeRef param) const;
        bool has(TypeID param_id) const;

        // Apply substitution to a type (recursively)
        TypeRef apply(TypeRef type, TypeArena &arena) const;

        // Get all substitutions
        const std::unordered_map<TypeID, TypeRef, TypeID::Hash> &substitutions() const
        {
            return _substitutions;
        }

        size_t size() const { return _substitutions.size(); }
        bool empty() const { return _substitutions.empty(); }
    };

    /**************************************************************************
     * @brief Generic type template and instantiation registry
     *
     * Manages generic types throughout the compilation process:
     *
     * 1. Template Registration: Store generic type definitions
     * 2. Instantiation Caching: Reuse instantiated types (Array<int>)
     * 3. Type Substitution: Replace type params with concrete types
     *
     * Usage:
     *   GenericRegistry registry;
     *
     *   // Register a generic template
     *   registry.register_template(array_type, {T_param}, module_id, ast_node);
     *
     *   // Instantiate with concrete types
     *   TypeRef array_int = registry.instantiate(array_type, {int_type}, arena);
     **************************************************************************/
    class GenericRegistry
    {
    private:
        // Templates indexed by generic type ID
        std::unordered_map<TypeID, GenericTemplate, TypeID::Hash> _templates;

        // Templates indexed by name (for lookup during parsing)
        std::unordered_map<std::string, TypeID> _templates_by_name;

        // Cached instantiations
        std::unordered_map<InstantiationKey, TypeRef, InstantiationKey::Hash> _instantiations;

        // Pending instantiation requests (collected during type checking)
        std::vector<InstantiationRequest> _pending_instantiations;

        // Track which instantiations have been monomorphized
        std::unordered_set<InstantiationKey, InstantiationKey::Hash> _monomorphized;

    public:
        GenericRegistry() = default;

        // ====================================================================
        // Template registration
        // ====================================================================

        // Register a generic type template
        void register_template(TypeRef generic_type,
                               std::vector<GenericParam> params,
                               ModuleID source_module,
                               ASTNode *ast_node = nullptr,
                               const std::string &name = "");

        // Check if a type is a registered template
        bool is_template(TypeRef type) const;
        bool is_template(TypeID type_id) const;

        // Get template info
        std::optional<GenericTemplate> get_template(TypeRef type) const;
        std::optional<GenericTemplate> get_template(TypeID type_id) const;
        std::optional<GenericTemplate> get_template_by_name(const std::string &name) const;

        // Get all registered templates
        std::vector<GenericTemplate> get_all_templates() const;

        // ====================================================================
        // Instantiation
        // ====================================================================

        // Instantiate a generic type with concrete type arguments
        // Returns the instantiated type, creating it if necessary
        TypeRef instantiate(TypeRef generic_type,
                            std::vector<TypeRef> type_args,
                            TypeArena &arena);

        // Check if an instantiation already exists
        std::optional<TypeRef> get_cached_instantiation(TypeRef base,
                                                         const std::vector<TypeRef> &args) const;

        // Record a pending instantiation request
        void request_instantiation(TypeRef generic_type,
                                   std::vector<TypeRef> type_args,
                                   SourceLocation location);

        // Get pending instantiation requests
        const std::vector<InstantiationRequest> &pending_instantiations() const
        {
            return _pending_instantiations;
        }

        // Clear pending instantiations
        void clear_pending_instantiations() { _pending_instantiations.clear(); }

        // Mark an instantiation as monomorphized
        void mark_monomorphized(TypeRef base, const std::vector<TypeRef> &args);

        // Check if an instantiation has been monomorphized
        bool is_monomorphized(TypeRef base, const std::vector<TypeRef> &args) const;

        // Get all cached instantiations for monomorphization
        std::vector<std::pair<TypeRef, std::vector<TypeRef>>> get_all_instantiations() const;

        // ====================================================================
        // Type substitution
        // ====================================================================

        // Create a substitution map for an instantiation
        TypeSubstitution create_substitution(TypeRef generic_type,
                                              const std::vector<TypeRef> &type_args) const;

        // Apply substitution to a type
        TypeRef substitute(TypeRef type,
                           const TypeSubstitution &subst,
                           TypeArena &arena) const;

        // ====================================================================
        // Validation
        // ====================================================================

        // Validate type arguments against template parameters
        bool validate_type_args(TypeRef generic_type,
                                const std::vector<TypeRef> &type_args,
                                std::string *error_msg = nullptr) const;

        // Check if type arguments satisfy bounds
        bool check_bounds(const GenericTemplate &tmpl,
                          const std::vector<TypeRef> &type_args) const;

        // ====================================================================
        // Statistics
        // ====================================================================

        size_t template_count() const { return _templates.size(); }
        size_t instantiation_count() const { return _instantiations.size(); }
        size_t pending_count() const { return _pending_instantiations.size(); }

        // Clear per-module state (instantiation caches) while preserving template definitions.
        // Call between module compilations to prevent specializations leaking across modules.
        void clear_module_state()
        {
            _instantiations.clear();
            _pending_instantiations.clear();
            _monomorphized.clear();
        }

    private:
        // Create instantiation key
        InstantiationKey make_key(TypeRef base, const std::vector<TypeRef> &args) const;

        // Cache an instantiation
        void cache_instantiation(const InstantiationKey &key, TypeRef instantiated);
    };

} // namespace Cryo
