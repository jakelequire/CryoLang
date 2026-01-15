/******************************************************************************
 * @file SymbolTable.cpp
 * @brief Implementation of SymbolTable for Cryo's new type system
 ******************************************************************************/

#include "Types/SymbolTable.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace Cryo
{
    // ========================================================================
    // Scope Implementation
    // ========================================================================

    bool Scope::declare(const std::string &name, const Symbol &symbol)
    {
        if (_symbols.find(name) != _symbols.end())
        {
            return false; // Already declared
        }
        _symbols[name] = symbol;
        return true;
    }

    Symbol *Scope::lookup(const std::string &name)
    {
        // First check local
        auto it = _symbols.find(name);
        if (it != _symbols.end())
        {
            return &it->second;
        }

        // Then check parent
        if (_parent)
        {
            return _parent->lookup(name);
        }

        return nullptr;
    }

    const Symbol *Scope::lookup(const std::string &name) const
    {
        auto it = _symbols.find(name);
        if (it != _symbols.end())
        {
            return &it->second;
        }

        if (_parent)
        {
            return _parent->lookup(name);
        }

        return nullptr;
    }

    Symbol *Scope::lookup_local(const std::string &name)
    {
        auto it = _symbols.find(name);
        return (it != _symbols.end()) ? &it->second : nullptr;
    }

    const Symbol *Scope::lookup_local(const std::string &name) const
    {
        auto it = _symbols.find(name);
        return (it != _symbols.end()) ? &it->second : nullptr;
    }

    bool Scope::has_symbol(const std::string &name) const
    {
        return lookup(name) != nullptr;
    }

    bool Scope::has_symbol_local(const std::string &name) const
    {
        return _symbols.find(name) != _symbols.end();
    }

    // ========================================================================
    // ScopeGuard Implementation
    // ========================================================================

    ScopeGuard::ScopeGuard(SymbolTable &table)
        : _table(table), _active(true)
    {
        _table.enter_scope();
    }

    ScopeGuard::~ScopeGuard()
    {
        if (_active)
        {
            _table.exit_scope();
        }
    }

    // ========================================================================
    // SymbolTable Implementation
    // ========================================================================

    SymbolTable::SymbolTable(TypeArena &arena, ModuleTypeRegistry &modules)
        : _arena(arena),
          _modules(modules),
          _current_scope(nullptr),
          _current_module(ModuleID::invalid())
    {
        ensure_global_scope();
    }

    void SymbolTable::ensure_global_scope()
    {
        if (_scopes.empty())
        {
            _scopes.push_back(std::make_unique<Scope>(nullptr, "global", _current_module));
            _current_scope = _scopes.back().get();
        }
    }

    void SymbolTable::add_import(const ImportDecl &import)
    {
        _active_imports.push_back(import);
    }

    // ========================================================================
    // Scope Management
    // ========================================================================

    void SymbolTable::enter_scope(const std::string &name)
    {
        ensure_global_scope();

        auto new_scope = std::make_unique<Scope>(_current_scope, name, _current_module);
        _current_scope = new_scope.get();
        _scopes.push_back(std::move(new_scope));
    }

    void SymbolTable::exit_scope()
    {
        if (_scopes.size() <= 1)
        {
            // Don't exit global scope
            return;
        }

        _scopes.pop_back();
        _current_scope = _scopes.empty() ? nullptr : _scopes.back().get();
    }

    ScopeGuard SymbolTable::scope_guard()
    {
        return ScopeGuard(*this);
    }

    // ========================================================================
    // Symbol Declaration
    // ========================================================================

    bool SymbolTable::declare(const Symbol &symbol)
    {
        ensure_global_scope();
        return _current_scope->declare(symbol.name, symbol);
    }

    bool SymbolTable::declare_variable(const std::string &name,
                                         TypeRef type,
                                         SourceLocation loc,
                                         bool is_mutable)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = SymbolKind::Variable;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.is_mutable = is_mutable;
        sym.visibility = SymbolVisibility::Private;

        return declare(sym);
    }

    bool SymbolTable::declare_function(const std::string &name,
                                         TypeRef type,
                                         SourceLocation loc)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = SymbolKind::Function;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.visibility = SymbolVisibility::Public;

        return declare(sym);
    }

    bool SymbolTable::declare_type(const std::string &name,
                                     TypeRef type,
                                     SourceLocation loc)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = SymbolKind::Type;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.visibility = SymbolVisibility::Public;

        return declare(sym);
    }

    bool SymbolTable::declare_constant(const std::string &name,
                                         TypeRef type,
                                         SourceLocation loc)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = SymbolKind::Constant;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.is_mutable = false;
        sym.visibility = SymbolVisibility::Private;

        return declare(sym);
    }

    bool SymbolTable::declare_intrinsic(const std::string &name,
                                         TypeRef type,
                                         SourceLocation loc)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = SymbolKind::Intrinsic;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.visibility = SymbolVisibility::Public;

        return declare(sym);
    }

    bool SymbolTable::declare_symbol(const std::string &name,
                                      SymbolKind kind,
                                      SourceLocation loc,
                                      TypeRef type,
                                      const std::string &scope_name)
    {
        Symbol sym;
        sym.name = name;
        sym.kind = kind;
        sym.type = type;
        sym.module = _current_module;
        sym.location = loc;
        sym.scope = scope_name;
        sym.visibility = SymbolVisibility::Public;

        return declare(sym);
    }

    void SymbolTable::register_namespace(const std::string &namespace_name,
                                          const std::unordered_map<std::string, Symbol> &symbols)
    {
        ensure_global_scope();

        // Register each symbol with the namespace prefix
        for (const auto &[name, symbol] : symbols)
        {
            // Register with qualified name for qualified access (e.g., IO::println)
            std::string qualified_name = namespace_name + "::" + name;
            Symbol qualified_sym = symbol;
            qualified_sym.name = qualified_name;
            _current_scope->declare(qualified_name, qualified_sym);
        }
    }

    // ========================================================================
    // Symbol Lookup
    // ========================================================================

    Symbol *SymbolTable::lookup(const std::string &name)
    {
        if (!_current_scope)
            return nullptr;
        return _current_scope->lookup(name);
    }

    const Symbol *SymbolTable::lookup(const std::string &name) const
    {
        if (!_current_scope)
            return nullptr;
        return _current_scope->lookup(name);
    }

    Symbol *SymbolTable::lookup_local(const std::string &name)
    {
        if (!_current_scope)
            return nullptr;
        return _current_scope->lookup_local(name);
    }

    const Symbol *SymbolTable::lookup_local(const std::string &name) const
    {
        if (!_current_scope)
            return nullptr;
        return _current_scope->lookup_local(name);
    }

    Symbol *SymbolTable::lookup_qualified(const std::vector<std::string> &path)
    {
        if (path.empty())
            return nullptr;

        // For single-element path, do normal lookup
        if (path.size() == 1)
        {
            return lookup(path[0]);
        }

        // For multi-element path, build qualified name and look up in modules
        // This would integrate with ModuleTypeRegistry
        // For now, just try to find it in current scope
        std::ostringstream oss;
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (i > 0)
                oss << "::";
            oss << path[i];
        }

        return lookup(oss.str());
    }

    const Symbol *SymbolTable::lookup_qualified(const std::vector<std::string> &path) const
    {
        return const_cast<SymbolTable *>(this)->lookup_qualified(path);
    }

    Symbol *SymbolTable::lookup_with_imports(const std::string &name)
    {
        // First try local lookup
        Symbol *sym = lookup(name);
        if (sym)
            return sym;

        // Then try imported namespaces
        for (const auto &import : _active_imports)
        {
            // If specific items imported, check if name is in list
            if (!import.items.empty())
            {
                bool found = false;
                for (const auto &item : import.items)
                {
                    if (item == name)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    continue;
            }

            // Try to find in the imported module via ModuleTypeRegistry
            auto type = _modules.lookup_visible(_current_module,
                                                 import.source_module,
                                                 name);
            if (type)
            {
                // Create a temporary symbol for the type
                // In a real implementation, we'd have a symbol cache
                return nullptr; // Would need proper symbol caching
            }
        }

        return nullptr;
    }

    const Symbol *SymbolTable::lookup_with_imports(const std::string &name) const
    {
        return const_cast<SymbolTable *>(this)->lookup_with_imports(name);
    }

    bool SymbolTable::has_symbol(const std::string &name) const
    {
        return lookup(name) != nullptr;
    }

    bool SymbolTable::has_symbol_local(const std::string &name) const
    {
        return lookup_local(name) != nullptr;
    }

    // ========================================================================
    // Type Resolution
    // ========================================================================

    std::optional<TypeRef> SymbolTable::get_type(const std::string &name) const
    {
        const Symbol *sym = lookup(name);
        if (sym && sym->type.is_valid())
        {
            return sym->type;
        }
        return std::nullopt;
    }

    std::optional<TypeRef> SymbolTable::resolve_type(const std::string &name) const
    {
        // First check symbol table for type symbols
        const Symbol *sym = lookup(name);
        if (sym && sym->kind == SymbolKind::Type)
        {
            return sym->type;
        }

        // Then check module registry
        auto type = _modules.resolve_with_imports(name, _current_module, _active_imports);
        return type;
    }

    TypeRef SymbolTable::lookup_struct_type(const std::string &name) const
    {
        auto type_opt = resolve_type(name);
        if (type_opt && type_opt->is_valid() && type_opt->get()->kind() == TypeKind::Struct)
        {
            return *type_opt;
        }
        return TypeRef{}; // Invalid TypeRef
    }

    TypeRef SymbolTable::lookup_class_type(const std::string &name) const
    {
        auto type_opt = resolve_type(name);
        if (type_opt && type_opt->is_valid() && type_opt->get()->kind() == TypeKind::Class)
        {
            return *type_opt;
        }
        return TypeRef{}; // Invalid TypeRef
    }

    TypeRef SymbolTable::lookup_enum_type(const std::string &name) const
    {
        auto type_opt = resolve_type(name);
        if (type_opt && type_opt->is_valid() && type_opt->get()->kind() == TypeKind::Enum)
        {
            return *type_opt;
        }
        return TypeRef{}; // Invalid TypeRef
    }

    // ========================================================================
    // Iteration
    // ========================================================================

    void SymbolTable::for_each_symbol(std::function<void(const Symbol &)> callback) const
    {
        // Iterate through all scopes from current to global
        for (const auto &scope : _scopes)
        {
            for (const auto &[name, symbol] : scope->symbols())
            {
                callback(symbol);
            }
        }
    }

    void SymbolTable::for_each_local(std::function<void(const Symbol &)> callback) const
    {
        if (_current_scope)
        {
            for (const auto &[name, symbol] : _current_scope->symbols())
            {
                callback(symbol);
            }
        }
    }

    std::vector<const Symbol *> SymbolTable::get_overloads(const std::string &name) const
    {
        std::vector<const Symbol *> result;

        for (const auto &scope : _scopes)
        {
            const Symbol *sym = scope->lookup_local(name);
            if (sym && sym->kind == SymbolKind::Function)
            {
                result.push_back(sym);
            }
        }

        return result;
    }

    // ========================================================================
    // Debug
    // ========================================================================

    void SymbolTable::dump(std::ostream &os) const
    {
        os << "=== Symbol Table ===\n";
        os << "Module: " << _current_module.id << "\n";
        os << "Scopes: " << _scopes.size() << "\n\n";

        int scope_num = 0;
        for (const auto &scope : _scopes)
        {
            os << "Scope " << scope_num++ << " (" << scope->name() << "):\n";
            os << std::setw(20) << std::left << "Name"
               << std::setw(12) << "Kind"
               << std::setw(20) << "Type"
               << "Location\n";
            os << std::string(60, '-') << "\n";

            for (const auto &[name, symbol] : scope->symbols())
            {
                os << std::setw(20) << std::left << name
                   << std::setw(12) << symbol_kind_to_string(symbol.kind)
                   << std::setw(20) << (symbol.type.is_valid()
                                             ? symbol.type.get()->display_name()
                                             : "<no type>")
                   << symbol.location.line() << ":" << symbol.location.column()
                   << "\n";
            }
            os << "\n";
        }
    }

    // ========================================================================
    // Utility Functions
    // ========================================================================

    std::string symbol_kind_to_string(SymbolKind kind)
    {
        switch (kind)
        {
        case SymbolKind::Variable:
            return "Variable";
        case SymbolKind::Parameter:
            return "Parameter";
        case SymbolKind::Function:
            return "Function";
        case SymbolKind::Method:
            return "Method";
        case SymbolKind::Type:
            return "Type";
        case SymbolKind::TypeAlias:
            return "TypeAlias";
        case SymbolKind::Constant:
            return "Constant";
        case SymbolKind::Field:
            return "Field";
        case SymbolKind::EnumVariant:
            return "EnumVariant";
        case SymbolKind::GenericParam:
            return "GenericParam";
        case SymbolKind::Namespace:
            return "Namespace";
        case SymbolKind::Import:
            return "Import";
        case SymbolKind::Intrinsic:
            return "Intrinsic";
        default:
            return "Unknown";
        }
    }

    // ========================================================================
    // Backward Compatibility Methods
    // ========================================================================

    Symbol *SymbolTable::lookup_namespaced_symbol(const std::string &ns, const std::string &name)
    {
        // Build qualified name and look up
        std::string qualified_name = ns.empty() ? name : ns + "::" + name;
        Symbol *sym = lookup(qualified_name);
        if (sym)
            return sym;

        // Try just the name
        return lookup(name);
    }

    const Symbol *SymbolTable::lookup_namespaced_symbol(const std::string &ns, const std::string &name) const
    {
        return const_cast<SymbolTable *>(this)->lookup_namespaced_symbol(ns, name);
    }

    std::vector<Symbol> SymbolTable::get_all_symbols_for_lsp() const
    {
        std::vector<Symbol> result;
        for_each_symbol([&result](const Symbol &sym)
                        { result.push_back(sym); });
        return result;
    }

    std::string visibility_to_string(SymbolVisibility vis)
    {
        switch (vis)
        {
        case SymbolVisibility::Private:
            return "private";
        case SymbolVisibility::Internal:
            return "internal";
        case SymbolVisibility::Public:
            return "public";
        default:
            return "unknown";
        }
    }

} // namespace Cryo
