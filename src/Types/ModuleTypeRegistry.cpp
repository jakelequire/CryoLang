/******************************************************************************
 * @file ModuleTypeRegistry.cpp
 * @brief Implementation of ModuleTypeRegistry for Cryo's new type system
 ******************************************************************************/

#include "Types/ModuleTypeRegistry.hpp"
#include "Types/TypeArena.hpp"

#include <algorithm>
#include <sstream>

namespace Cryo
{
    // ========================================================================
    // Constructor
    // ========================================================================

    ModuleTypeRegistry::ModuleTypeRegistry()
    {
        // Create builtin module for primitives
        _builtin_module = ModuleID::builtin();
        _modules[_builtin_module] = ModuleInfo(_builtin_module, "<builtin>", "");
        _modules[_builtin_module].is_loaded = true;
        _module_types[_builtin_module] = {};
    }

    // ========================================================================
    // Module management
    // ========================================================================

    ModuleID ModuleTypeRegistry::get_or_create_module(const std::string &name,
                                                       const std::string &file_path)
    {
        // Check if module already exists
        for (const auto &[id, info] : _modules)
        {
            if (info.name == name)
            {
                return id;
            }
        }

        // Create new module
        ModuleID new_id{_next_module_id++};
        _modules[new_id] = ModuleInfo(new_id, name, file_path);
        _module_types[new_id] = {};

        return new_id;
    }

    std::optional<ModuleInfo> ModuleTypeRegistry::get_module_info(ModuleID id) const
    {
        auto it = _modules.find(id);
        if (it != _modules.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<ModuleID> ModuleTypeRegistry::get_module_by_name(const std::string &name) const
    {
        for (const auto &[id, info] : _modules)
        {
            if (info.name == name)
            {
                return id;
            }
        }
        return std::nullopt;
    }

    void ModuleTypeRegistry::set_module_loaded(ModuleID id, bool loaded)
    {
        auto it = _modules.find(id);
        if (it != _modules.end())
        {
            it->second.is_loaded = loaded;
        }
    }

    void ModuleTypeRegistry::add_import(ModuleID module, ImportDecl import)
    {
        auto it = _modules.find(module);
        if (it != _modules.end())
        {
            it->second.imports.push_back(std::move(import));
        }
    }

    // ========================================================================
    // Type registration
    // ========================================================================

    void ModuleTypeRegistry::register_type(ModuleID module,
                                            const std::string &name,
                                            TypeRef type,
                                            TypeVisibility visibility)
    {
        // Ensure module exists
        if (_module_types.find(module) == _module_types.end())
        {
            _module_types[module] = {};
        }

        // Register the type
        _module_types[module][name] = TypeRegistryEntry(type, visibility, name);
    }

    void ModuleTypeRegistry::register_alias(ModuleID module,
                                             const std::string &alias,
                                             TypeRef target)
    {
        // Ensure module exists
        if (_type_aliases.find(module) == _type_aliases.end())
        {
            _type_aliases[module] = {};
        }

        _type_aliases[module][alias] = target;
    }

    void ModuleTypeRegistry::unregister_type(ModuleID module, const std::string &name)
    {
        auto it = _module_types.find(module);
        if (it != _module_types.end())
        {
            it->second.erase(name);
        }
    }

    // ========================================================================
    // Type lookup
    // ========================================================================

    std::optional<TypeRef> ModuleTypeRegistry::lookup(const QualifiedTypeName &name) const
    {
        return lookup_in_module(name.module, name.name);
    }

    std::optional<TypeRef> ModuleTypeRegistry::lookup_in_module(ModuleID module,
                                                                  const std::string &name) const
    {
        // Check type registry
        auto module_it = _module_types.find(module);
        if (module_it != _module_types.end())
        {
            auto type_it = module_it->second.find(name);
            if (type_it != module_it->second.end())
            {
                return type_it->second.type;
            }
        }

        // Check aliases
        auto alias_module_it = _type_aliases.find(module);
        if (alias_module_it != _type_aliases.end())
        {
            auto alias_it = alias_module_it->second.find(name);
            if (alias_it != alias_module_it->second.end())
            {
                return alias_it->second;
            }
        }

        return std::nullopt;
    }

    std::optional<TypeRef> ModuleTypeRegistry::lookup_visible(ModuleID from_module,
                                                               ModuleID in_module,
                                                               const std::string &name) const
    {
        auto module_it = _module_types.find(in_module);
        if (module_it == _module_types.end())
        {
            return std::nullopt;
        }

        auto type_it = module_it->second.find(name);
        if (type_it == module_it->second.end())
        {
            return std::nullopt;
        }

        // Check visibility
        if (is_visible(from_module, in_module, type_it->second.visibility))
        {
            return type_it->second.type;
        }

        return std::nullopt;
    }

    std::optional<TypeRef> ModuleTypeRegistry::resolve_with_imports(
        const std::string &name,
        ModuleID current_module,
        const std::vector<ImportDecl> &imports) const
    {
        // First, check current module
        auto local = lookup_in_module(current_module, name);
        if (local)
        {
            return local;
        }

        // Check imports
        for (const auto &import : imports)
        {
            // If specific items, check if name is in the list
            if (!import.items.empty())
            {
                bool found = std::find(import.items.begin(), import.items.end(), name)
                             != import.items.end();
                if (!found)
                    continue;
            }

            // Check if type exists in source module and is visible
            auto result = lookup_visible(current_module, import.source_module, name);
            if (result)
            {
                return result;
            }
        }

        // Check builtin module
        auto builtin = lookup_in_module(_builtin_module, name);
        if (builtin)
        {
            return builtin;
        }

        return std::nullopt;
    }

    std::optional<TypeRef> ModuleTypeRegistry::resolve_qualified(
        const std::vector<std::string> &path,
        ModuleID current_module) const
    {
        if (path.empty())
        {
            return std::nullopt;
        }

        // If single component, try as local name
        if (path.size() == 1)
        {
            return lookup_in_module(current_module, path[0]);
        }

        // Build module name from path (all but last component)
        std::ostringstream module_name;
        for (size_t i = 0; i < path.size() - 1; ++i)
        {
            if (i > 0)
                module_name << "::";
            module_name << path[i];
        }

        // Get the type name (last component)
        const std::string &type_name = path.back();

        // Find the module
        auto module_id = get_module_by_name(module_name.str());
        if (!module_id)
        {
            return std::nullopt;
        }

        // Look up the type in that module
        return lookup_visible(current_module, *module_id, type_name);
    }

    // ========================================================================
    // Queries
    // ========================================================================

    std::vector<TypeRegistryEntry> ModuleTypeRegistry::get_module_types(ModuleID module) const
    {
        std::vector<TypeRegistryEntry> result;

        auto it = _module_types.find(module);
        if (it != _module_types.end())
        {
            for (const auto &[name, entry] : it->second)
            {
                result.push_back(entry);
            }
        }

        return result;
    }

    std::vector<TypeRegistryEntry> ModuleTypeRegistry::get_public_types(ModuleID module) const
    {
        std::vector<TypeRegistryEntry> result;

        auto it = _module_types.find(module);
        if (it != _module_types.end())
        {
            for (const auto &[name, entry] : it->second)
            {
                if (entry.visibility == TypeVisibility::Public)
                {
                    result.push_back(entry);
                }
            }
        }

        return result;
    }

    bool ModuleTypeRegistry::has_type(const QualifiedTypeName &name) const
    {
        return lookup(name).has_value();
    }

    bool ModuleTypeRegistry::has_type(ModuleID module, const std::string &name) const
    {
        return lookup_in_module(module, name).has_value();
    }

    size_t ModuleTypeRegistry::type_count() const
    {
        size_t count = 0;
        for (const auto &[module, types] : _module_types)
        {
            count += types.size();
        }
        return count;
    }

    // ========================================================================
    // Private helpers
    // ========================================================================

    bool ModuleTypeRegistry::is_visible(ModuleID from_module,
                                         ModuleID in_module,
                                         TypeVisibility visibility) const
    {
        switch (visibility)
        {
        case TypeVisibility::Public:
            return true;

        case TypeVisibility::Private:
            return from_module == in_module;

        case TypeVisibility::Internal:
            // Visible if same module or submodule
            return from_module == in_module || is_submodule(from_module, in_module);

        default:
            return false;
        }
    }

    bool ModuleTypeRegistry::is_submodule(ModuleID potential_sub, ModuleID parent) const
    {
        // Get module names
        auto sub_info = get_module_info(potential_sub);
        auto parent_info = get_module_info(parent);

        if (!sub_info || !parent_info)
        {
            return false;
        }

        // Check if sub's name starts with parent's name followed by ::
        const std::string &sub_name = sub_info->name;
        const std::string &parent_name = parent_info->name;

        if (sub_name.size() <= parent_name.size())
        {
            return false;
        }

        return sub_name.compare(0, parent_name.size(), parent_name) == 0 &&
               sub_name.compare(parent_name.size(), 2, "::") == 0;
    }

} // namespace Cryo
