#pragma once
/******************************************************************************
 * @file ModuleTypeRegistry.hpp
 * @brief Cross-module type tracking for Cryo's new type system
 *
 * ModuleTypeRegistry tracks which types belong to which modules and handles
 * cross-module type lookup. This enables:
 * - Distinguishing types with same name in different modules
 * - Resolving imports and type visibility
 * - Module-qualified type lookup
 ******************************************************************************/

#include "Types2/TypeID.hpp"
#include "Types2/Type.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace Cryo
{
    // Forward declarations
    class TypeArena;

    /**************************************************************************
     * @brief Import declaration for module imports
     **************************************************************************/
    struct ImportDecl
    {
        ModuleID source_module;              // Module being imported from
        std::vector<std::string> items;      // Specific items (empty = import all)
        std::optional<std::string> alias;    // Namespace alias (import foo as bar)
        bool is_wildcard;                    // import foo::* (all public items)

        ImportDecl(ModuleID src, std::vector<std::string> itms = {},
                   std::optional<std::string> als = std::nullopt, bool wild = false)
            : source_module(src), items(std::move(itms)), alias(als), is_wildcard(wild) {}

        // Import all from module
        static ImportDecl all(ModuleID src)
        {
            return ImportDecl(src, {}, std::nullopt, true);
        }

        // Import specific items
        static ImportDecl specific(ModuleID src, std::vector<std::string> items)
        {
            return ImportDecl(src, std::move(items));
        }

        // Import with alias
        static ImportDecl with_alias(ModuleID src, std::string alias)
        {
            return ImportDecl(src, {}, std::move(alias), true);
        }
    };

    /**************************************************************************
     * @brief Module information for tracking module metadata
     **************************************************************************/
    struct ModuleInfo
    {
        ModuleID id;
        std::string name;                    // e.g., "std::core::types"
        std::string file_path;               // Source file path
        std::vector<ImportDecl> imports;     // This module's imports
        bool is_loaded = false;

        ModuleInfo() : id(ModuleID::invalid()) {}
        ModuleInfo(ModuleID mid, std::string n, std::string path = "")
            : id(mid), name(std::move(n)), file_path(std::move(path)), is_loaded(false) {}
    };

    /**************************************************************************
     * @brief Type visibility levels
     **************************************************************************/
    enum class TypeVisibility
    {
        Public,     // Visible to all modules
        Private,    // Only visible within declaring module
        Internal,   // Visible within module and submodules
    };

    /**************************************************************************
     * @brief Registry entry for a type in a module
     **************************************************************************/
    struct TypeRegistryEntry
    {
        TypeRef type;
        TypeVisibility visibility;
        std::string local_name;              // Name within the module

        TypeRegistryEntry() : visibility(TypeVisibility::Public) {}
        TypeRegistryEntry(TypeRef t, TypeVisibility vis, std::string name)
            : type(t), visibility(vis), local_name(std::move(name)) {}
    };

    /**************************************************************************
     * @brief Cross-module type registry
     *
     * Tracks types declared in each module and handles resolution across
     * module boundaries. This replaces ad-hoc string-based lookups with
     * structured module-aware resolution.
     *
     * Usage:
     *   ModuleTypeRegistry registry;
     *
     *   // Register a type in a module
     *   registry.register_type(module_id, "MyStruct", struct_type);
     *
     *   // Look up a type
     *   auto type = registry.lookup({module_id, "MyStruct"});
     *
     *   // Look up with imports
     *   auto type = registry.resolve_with_imports("MyStruct", current_module, imports);
     **************************************************************************/
    class ModuleTypeRegistry
    {
    private:
        // Module metadata
        std::unordered_map<ModuleID, ModuleInfo, ModuleID::Hash> _modules;

        // Types per module: module_id -> (type_name -> entry)
        std::unordered_map<ModuleID,
                           std::unordered_map<std::string, TypeRegistryEntry>,
                           ModuleID::Hash> _module_types;

        // Type aliases per module: module_id -> (alias_name -> target_type)
        std::unordered_map<ModuleID,
                           std::unordered_map<std::string, TypeRef>,
                           ModuleID::Hash> _type_aliases;

        // Next module ID to assign
        uint32_t _next_module_id = 2; // 0 = invalid, 1 = builtin

        // Builtin module for primitives
        ModuleID _builtin_module;

    public:
        ModuleTypeRegistry();

        // ====================================================================
        // Module management
        // ====================================================================

        // Create or get a module by name
        ModuleID get_or_create_module(const std::string &name,
                                      const std::string &file_path = "");

        // Get module info
        std::optional<ModuleInfo> get_module_info(ModuleID id) const;

        // Get module by name
        std::optional<ModuleID> get_module_by_name(const std::string &name) const;

        // Mark module as loaded
        void set_module_loaded(ModuleID id, bool loaded = true);

        // Add import to a module
        void add_import(ModuleID module, ImportDecl import);

        // Get builtin module ID
        ModuleID builtin_module() const { return _builtin_module; }

        // ====================================================================
        // Type registration
        // ====================================================================

        // Register a type in a module
        void register_type(ModuleID module,
                           const std::string &name,
                           TypeRef type,
                           TypeVisibility visibility = TypeVisibility::Public);

        // Register a type alias
        void register_alias(ModuleID module,
                            const std::string &alias,
                            TypeRef target);

        // Unregister a type (rarely needed)
        void unregister_type(ModuleID module, const std::string &name);

        // ====================================================================
        // Type lookup
        // ====================================================================

        // Look up type by qualified name
        std::optional<TypeRef> lookup(const QualifiedTypeName &name) const;

        // Look up type in specific module
        std::optional<TypeRef> lookup_in_module(ModuleID module,
                                                 const std::string &name) const;

        // Look up type with visibility check
        std::optional<TypeRef> lookup_visible(ModuleID from_module,
                                               ModuleID in_module,
                                               const std::string &name) const;

        // Resolve type name using imports
        std::optional<TypeRef> resolve_with_imports(
            const std::string &name,
            ModuleID current_module,
            const std::vector<ImportDecl> &imports) const;

        // Resolve qualified name (e.g., "std::core::Option")
        std::optional<TypeRef> resolve_qualified(
            const std::vector<std::string> &path,
            ModuleID current_module) const;

        // ====================================================================
        // Queries
        // ====================================================================

        // Get all types in a module
        std::vector<TypeRegistryEntry> get_module_types(ModuleID module) const;

        // Get all public types in a module
        std::vector<TypeRegistryEntry> get_public_types(ModuleID module) const;

        // Check if a type exists
        bool has_type(const QualifiedTypeName &name) const;
        bool has_type(ModuleID module, const std::string &name) const;

        // Get total number of registered types
        size_t type_count() const;

        // Get number of modules
        size_t module_count() const { return _modules.size(); }

    private:
        // Check if a type is visible from one module to another
        bool is_visible(ModuleID from_module,
                        ModuleID in_module,
                        TypeVisibility visibility) const;

        // Check if one module is a submodule of another
        bool is_submodule(ModuleID potential_sub, ModuleID parent) const;
    };

} // namespace Cryo
