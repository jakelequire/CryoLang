#pragma once
/******************************************************************************
 * @file Monomorphizer.hpp
 * @brief Monomorphization infrastructure using the new Types system
 *
 * Monomorphizer handles generic type instantiation and specialization
 * using TypeRef-based types. Key improvements:
 *
 * - Uses TypeRef and TypeSubstitution for clean type manipulation
 * - Integrates with GenericRegistry for template management
 * - Proper error handling through ErrorType
 * - No string-based type substitution
 ******************************************************************************/

#include "Types/TypeID.hpp"
#include "Types/Type.hpp"
#include "Types/TypeArena.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/ModuleTypeRegistry.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>

namespace Cryo
{
    // Forward declarations
    class ASTNode;
    class ProgramNode;
    class StructDeclarationNode;
    class ClassDeclarationNode;
    class EnumDeclarationNode;
    class FunctionDeclarationNode;

    /**************************************************************************
     * @brief Monomorphization request for a specific instantiation
     **************************************************************************/
    struct MonomorphRequest
    {
        TypeRef generic_type;            // Base generic type
        std::vector<TypeRef> type_args;  // Concrete type arguments
        SourceLocation location;         // Where instantiation was requested
        ModuleID source_module;          // Module requesting instantiation

        MonomorphRequest() = default;
        MonomorphRequest(TypeRef base, std::vector<TypeRef> args,
                         SourceLocation loc, ModuleID mod)
            : generic_type(base), type_args(std::move(args)),
              location(loc), source_module(mod) {}

        // Generate unique key for deduplication
        std::string key() const;
    };

    /**************************************************************************
     * @brief Result of a monomorphization operation
     **************************************************************************/
    struct MonomorphResult
    {
        TypeRef specialized_type;        // The specialized type
        ASTNode *specialized_ast;        // Specialized AST node (if applicable)
        bool success;
        std::string error_message;
        std::vector<std::string> notes;

        MonomorphResult() : specialized_ast(nullptr), success(false) {}

        static MonomorphResult ok(TypeRef type, ASTNode *ast = nullptr)
        {
            MonomorphResult r;
            r.specialized_type = type;
            r.specialized_ast = ast;
            r.success = true;
            return r;
        }

        static MonomorphResult error(const std::string &msg)
        {
            MonomorphResult r;
            r.success = false;
            r.error_message = msg;
            return r;
        }

        bool is_ok() const { return success; }
    };

    /**************************************************************************
     * @brief Specialization cache entry
     **************************************************************************/
    struct SpecializationEntry
    {
        TypeRef specialized_type;
        ASTNode *ast_node;
        bool code_generated;

        SpecializationEntry()
            : ast_node(nullptr), code_generated(false) {}
    };

    /**************************************************************************
     * @brief Monomorphizer using Types system
     *
     * Handles:
     * - Type substitution using TypeSubstitution
     * - Specialization caching
     * - Demand-driven monomorphization
     *
     * Usage:
     *   Monomorphizer mono(arena, generics, modules);
     *
     *   // Add instantiation requests
     *   mono.add_request(array_type, {int_type}, loc, mod);
     *
     *   // Process all pending requests
     *   mono.process_all();
     **************************************************************************/
    class Monomorphizer
    {
    private:
        TypeArena &_arena;
        GenericRegistry &_generics;
        ModuleTypeRegistry &_modules;

        // Pending monomorphization requests
        std::vector<MonomorphRequest> _pending_requests;

        // Processed specializations: key -> entry
        std::unordered_map<std::string, SpecializationEntry> _specializations;

        // Track which requests are in progress (cycle detection)
        std::unordered_set<std::string> _in_progress;

        // Callback for AST specialization (set by compiler)
        using ASTSpecializer = std::function<ASTNode *(
            const GenericTemplate &tmpl,
            const TypeSubstitution &subst,
            const std::string &specialized_name)>;

        ASTSpecializer _ast_specializer;

    public:
        // ====================================================================
        // Construction
        // ====================================================================

        Monomorphizer(TypeArena &arena,
                       GenericRegistry &generics,
                       ModuleTypeRegistry &modules);

        ~Monomorphizer() = default;

        // Non-copyable
        Monomorphizer(const Monomorphizer &) = delete;
        Monomorphizer &operator=(const Monomorphizer &) = delete;

        // ====================================================================
        // Configuration
        // ====================================================================

        /**
         * @brief Set the AST specializer callback
         *
         * This callback is invoked when a generic type needs AST-level
         * specialization (e.g., creating specialized struct/class nodes).
         */
        void set_ast_specializer(ASTSpecializer specializer)
        {
            _ast_specializer = std::move(specializer);
        }

        // ====================================================================
        // Request Management
        // ====================================================================

        /**
         * @brief Add a monomorphization request
         */
        void add_request(TypeRef generic_type,
                         std::vector<TypeRef> type_args,
                         SourceLocation location,
                         ModuleID source_module);

        /**
         * @brief Add a request from the GenericRegistry pending list
         */
        void add_request(const InstantiationRequest &request);

        /**
         * @brief Import pending requests from GenericRegistry
         */
        void import_pending_from_registry();

        /**
         * @brief Import all cached instantiations from GenericRegistry
         *
         * This imports ALL instantiated types that haven't been monomorphized yet,
         * not just pending requests. This is the primary way to collect work
         * when TypeResolver eagerly instantiates types during resolution.
         */
        void import_cached_instantiations();

        /**
         * @brief Get pending request count
         */
        size_t pending_count() const { return _pending_requests.size(); }

        /**
         * @brief Check if there are pending requests
         */
        bool has_pending() const { return !_pending_requests.empty(); }

        // ====================================================================
        // Processing
        // ====================================================================

        /**
         * @brief Process all pending monomorphization requests
         * @return true if all succeeded
         */
        bool process_all();

        /**
         * @brief Process a single request
         */
        MonomorphResult process_request(const MonomorphRequest &request);

        /**
         * @brief Specialize a generic type with given arguments
         */
        MonomorphResult specialize(TypeRef generic_type,
                                    const std::vector<TypeRef> &type_args);

        // ====================================================================
        // Specialization Cache
        // ====================================================================

        /**
         * @brief Check if a specialization already exists
         */
        bool has_specialization(const std::string &key) const;

        /**
         * @brief Get a cached specialization
         */
        std::optional<SpecializationEntry> get_specialization(const std::string &key) const;

        /**
         * @brief Get all specializations
         */
        const std::unordered_map<std::string, SpecializationEntry> &
        specializations() const { return _specializations; }

        /**
         * @brief Mark a specialization as code-generated
         */
        void mark_generated(const std::string &key);

        // ====================================================================
        // Type Substitution
        // ====================================================================

        /**
         * @brief Create a substitution map for a template
         */
        TypeSubstitution create_substitution(TypeRef generic_type,
                                              const std::vector<TypeRef> &type_args);

        /**
         * @brief Apply substitution to a type
         */
        TypeRef apply_substitution(TypeRef type, const TypeSubstitution &subst);

        // ====================================================================
        // Name Generation
        // ====================================================================

        /**
         * @brief Generate a mangled name for a specialization
         */
        std::string generate_specialized_name(TypeRef base,
                                               const std::vector<TypeRef> &args);

        /**
         * @brief Generate a key for specialization lookup
         */
        std::string generate_key(TypeRef base, const std::vector<TypeRef> &args);

        // ====================================================================
        // Context Access
        // ====================================================================

        TypeArena &arena() { return _arena; }
        GenericRegistry &generics() { return _generics; }
        ModuleTypeRegistry &modules() { return _modules; }

        // ====================================================================
        // Statistics
        // ====================================================================

        size_t specialization_count() const { return _specializations.size(); }
        size_t generated_count() const;

    private:
        /**
         * @brief Cache a specialization
         */
        void cache_specialization(const std::string &key,
                                   TypeRef type,
                                   ASTNode *ast = nullptr);

        /**
         * @brief Check for circular instantiation
         */
        bool is_circular(const std::string &key) const
        {
            return _in_progress.find(key) != _in_progress.end();
        }

        /**
         * @brief Create a concrete enum type with substituted variant types
         * @param base_enum_type The generic enum type
         * @param subst Type substitution map
         * @param specialized_name Mangled name for the concrete type
         * @param type_args Concrete type arguments
         * @param ast_node AST node for the enum declaration (contains variants)
         */
        TypeRef create_concrete_enum(const Type *base_enum_type,
                                      const TypeSubstitution &subst,
                                      const std::string &specialized_name,
                                      const std::vector<TypeRef> &type_args,
                                      ASTNode *ast_node);

        /**
         * @brief Create a concrete struct type with substituted field types
         * @param base_struct_type The generic struct type
         * @param subst Type substitution map
         * @param specialized_name Mangled name for the concrete type
         * @param type_args Concrete type arguments
         * @param ast_node AST node for the struct declaration (contains fields)
         */
        TypeRef create_concrete_struct(const Type *base_struct_type,
                                        const TypeSubstitution &subst,
                                        const std::string &specialized_name,
                                        const std::vector<TypeRef> &type_args,
                                        ASTNode *ast_node);
    };

} // namespace Cryo
