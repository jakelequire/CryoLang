#pragma once
/******************************************************************************
 * @file TypeResolver.hpp
 * @brief Single-path type resolution for Cryo's new type system
 *
 * TypeResolver replaces the old 1,474-line resolve_type_with_generic_context()
 * function with a clean, single-path resolver. Key improvements:
 *
 * - No fallback chains: Each resolution has one clear path
 * - Explicit errors: Failed resolution returns ErrorType, not Unknown
 * - Module-aware: Proper cross-module type lookup
 * - Generic-aware: Clean handling of type parameters and instantiation
 ******************************************************************************/

#include "Types/TypeID.hpp"
#include "Types/Type.hpp"
#include "Types/TypeArena.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Types/ModuleTypeRegistry.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/ErrorType.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>

namespace Cryo
{
    // Forward declarations
    class DiagEmitter;

    /**************************************************************************
     * @brief Resolution context for tracking state during type resolution
     **************************************************************************/
    struct ResolutionContext
    {
        ModuleID current_module;                 // Module we're resolving in
        std::vector<ImportDecl> imports;         // Active imports

        // Generic context: maps param names to their types
        std::unordered_map<std::string, TypeRef> generic_bindings;

        // Cycle detection: types currently being resolved
        std::unordered_set<std::string> in_progress;

        // Current source location for error reporting
        SourceLocation current_location;

        ResolutionContext() : current_module(ModuleID::invalid()) {}

        explicit ResolutionContext(ModuleID mod) : current_module(mod) {}

        // Add a generic binding
        void bind_generic(const std::string &name, TypeRef type)
        {
            generic_bindings[name] = type;
        }

        // Look up a generic binding
        std::optional<TypeRef> lookup_generic(const std::string &name) const
        {
            auto it = generic_bindings.find(name);
            if (it != generic_bindings.end())
                return it->second;
            return std::nullopt;
        }

        // Check if we're in a cycle
        bool is_in_progress(const std::string &name) const
        {
            return in_progress.count(name) > 0;
        }

        // Create a child context (for nested scopes)
        ResolutionContext child() const
        {
            ResolutionContext ctx;
            ctx.current_module = current_module;
            ctx.imports = imports;
            ctx.generic_bindings = generic_bindings;
            // Don't copy in_progress - start fresh for nested resolution
            return ctx;
        }
    };

    /**************************************************************************
     * @brief Result of type resolution
     **************************************************************************/
    struct ResolutionResult
    {
        TypeRef type;
        bool success;
        std::string error_message;
        std::vector<std::string> notes;

        ResolutionResult() : success(false) {}

        static ResolutionResult ok(TypeRef t)
        {
            ResolutionResult r;
            r.type = t;
            r.success = true;
            return r;
        }

        static ResolutionResult error(const std::string &msg, TypeRef err_type = TypeRef{})
        {
            ResolutionResult r;
            r.type = err_type;
            r.success = false;
            r.error_message = msg;
            return r;
        }

        void add_note(const std::string &note) { notes.push_back(note); }
    };

    /**************************************************************************
     * @brief Single-path type resolver
     *
     * Resolves type annotations to TypeRefs using a clean, deterministic
     * algorithm. No fallback chains - each type kind has exactly one
     * resolution path.
     *
     * Usage:
     *   TypeResolver resolver(arena, module_registry, generic_registry);
     *
     *   ResolutionContext ctx(current_module);
     *   ctx.imports = module_imports;
     *
     *   TypeRef result = resolver.resolve(type_annotation, ctx);
     *   if (result.is_error()) {
     *       // Handle error
     *   }
     **************************************************************************/
    class TypeResolver
    {
    private:
        TypeArena &_arena;
        ModuleTypeRegistry &_module_registry;
        GenericRegistry &_generic_registry;
        DiagEmitter *_diagnostics; // Optional diagnostic reporting

    public:
        TypeResolver(TypeArena &arena,
                     ModuleTypeRegistry &modules,
                     GenericRegistry &generics,
                     DiagEmitter *diag = nullptr);

        // ====================================================================
        // Main resolution interface
        // ====================================================================

        // Resolve a type annotation to a TypeRef
        TypeRef resolve(const TypeAnnotation &annotation, ResolutionContext &ctx);

        // Resolve multiple annotations
        std::vector<TypeRef> resolve_all(const std::vector<TypeAnnotation> &annotations,
                                          ResolutionContext &ctx);

        // Resolve a type string (convenience method)
        TypeRef resolve_string(const std::string &type_str,
                               ResolutionContext &ctx);

        // ====================================================================
        // Specific resolution methods (no fallbacks!)
        // ====================================================================

        // Resolve a primitive type name
        TypeRef resolve_primitive(const std::string &name);

        // Resolve a named type (struct, class, enum, etc.)
        TypeRef resolve_named(const std::string &name, ResolutionContext &ctx);

        // Resolve a qualified type path (std::core::Option)
        TypeRef resolve_qualified(const std::vector<std::string> &path,
                                  ResolutionContext &ctx);

        // Resolve a pointer type
        TypeRef resolve_pointer(const TypeAnnotation &inner, ResolutionContext &ctx);

        // Resolve a reference type
        TypeRef resolve_reference(const TypeAnnotation &inner,
                                  bool is_mutable, ResolutionContext &ctx);

        // Resolve an array type
        TypeRef resolve_array(const TypeAnnotation &element,
                              std::optional<size_t> size, ResolutionContext &ctx);

        // Resolve a function type
        TypeRef resolve_function(const std::vector<TypeAnnotation> &params,
                                 const TypeAnnotation &return_type,
                                 bool is_variadic, ResolutionContext &ctx);

        // Resolve a tuple type
        TypeRef resolve_tuple(const std::vector<TypeAnnotation> &elements,
                              ResolutionContext &ctx);

        // Resolve an optional type (T? -> Option<T>)
        TypeRef resolve_optional(const TypeAnnotation &inner, ResolutionContext &ctx);

        // Resolve a generic instantiation (Array<int>)
        TypeRef resolve_generic(const TypeAnnotation &base,
                                const std::vector<TypeAnnotation> &args,
                                ResolutionContext &ctx);

        // ====================================================================
        // Utility methods
        // ====================================================================

        // Check if a name is a primitive type
        bool is_primitive_name(const std::string &name) const;

        // Check if a name is a generic parameter in context
        bool is_generic_param(const std::string &name, const ResolutionContext &ctx) const;

        // Get the arena
        TypeArena &arena() { return _arena; }
        const TypeArena &arena() const { return _arena; }

    private:
        // Create an error type with diagnostics
        TypeRef make_error(const std::string &reason,
                           const SourceLocation &location,
                           const std::vector<std::string> &notes = {});

        // Report a diagnostic
        void report_error(const std::string &message, const SourceLocation &location);
        void report_note(const std::string &message, const SourceLocation &location);

        // Primitive type mapping
        static const std::unordered_map<std::string, std::function<TypeRef(TypeArena &)>> _primitive_map;
    };

} // namespace Cryo
