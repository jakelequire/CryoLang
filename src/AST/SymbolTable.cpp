#include "AST/SymbolTable.hpp"

namespace Cryo
{
    bool SymbolTable::declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc)
    {
        // Check if symbol already exists in current scope
        if (symbols_.find(name) != symbols_.end())
        {
            return false; // Symbol already declared
        }

        // Add the symbol to current scope
        symbols_[name] = Symbol{name, kind, loc};
        return true;
    }

    Symbol *SymbolTable::lookup_symbol(const std::string &name)
    {
        // Check current scope first
        auto it = symbols_.find(name);
        if (it != symbols_.end())
        {
            return &it->second;
        }

        // Check parent scopes
        if (parent_scope_)
        {
            return parent_scope_->lookup_symbol(name);
        }

        return nullptr; // Symbol not found
    }

    std::unique_ptr<SymbolTable> SymbolTable::enter_scope()
    {
        // Create new child scope with this as parent
        return std::make_unique<SymbolTable>(std::move(parent_scope_));
    }

    std::unique_ptr<SymbolTable> SymbolTable::exit_scope()
    {
        // Return parent scope
        return std::move(parent_scope_);
    }
}
