#include "AST/SymbolTable.hpp"

namespace Cryo
{
    bool SymbolTable::declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, Type *data_type, const std::string &scope)
    {
        // Check if symbol already exists in current scope
        if (symbols_.find(name) != symbols_.end())
        {
            return false; // Symbol already declared
        }

        // Add the symbol to current scope
        symbols_[name] = Symbol{name, kind, loc, data_type, scope};
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

    std::string SymbolTable::get_symbol_kind_string(SymbolKind kind) const
    {
        switch (kind)
        {
        case SymbolKind::Variable:
            return "Variable";
        case SymbolKind::Function:
            return "Function";
        case SymbolKind::Type:
            return "Type";
        default:
            return "Unknown";
        }
    }

    void SymbolTable::dump(std::ostream &os, int indent_level) const
    {
        std::string indent(indent_level * 2, ' ');

        if (symbols_.empty())
        {
            os << indent << "(empty scope)" << std::endl;
        }
        else
        {
            for (const auto &[name, symbol] : symbols_)
            {
                std::string type_str = symbol.data_type ? symbol.data_type->to_string() : "unknown";
                os << indent << "|-- " << symbol.name
                   << " [" << get_symbol_kind_string(symbol.kind) << "]"
                   << " (" << type_str << ")"
                   << " at line " << symbol.declaration_location.line()
                   << ", col " << symbol.declaration_location.column()
                   << std::endl;
            }
        }

        // If there's a parent scope, show it with increased indentation
        if (parent_scope_)
        {
            os << indent << "`-- Parent Scope:" << std::endl;
            parent_scope_->dump(os, indent_level + 1);
        }
    }

    void SymbolTable::print_pretty(std::ostream &os) const
    {
        os << "+=========================================================================================+" << std::endl;
        os << "|                                      Symbol Table                                       |" << std::endl;
        os << "+=========================================================================================+" << std::endl;

        if (symbols_.empty() && !parent_scope_)
        {
            os << "|  No symbols found                                                                       |" << std::endl;
            os << "+=========================================================================================+" << std::endl;
            return;
        }

        // Count total symbols across all scopes
        int total_symbols = 0;
        const SymbolTable *current = this;
        while (current)
        {
            total_symbols += current->symbols_.size();
            current = current->parent_scope_.get();
        }

        os << "|  Total symbols: " << total_symbols;
        // Pad to align right side of box
        for (int i = std::to_string(total_symbols).length(); i < 72; i++)
        {
            os << " ";
        }
        os << "|" << std::endl;
        os << "+-----------------------------------------------------------------------------------------+" << std::endl;

        // Header for symbol details - properly aligned with wider columns (Scope and Location swapped)
        os << "| Type     | Name           | Data Type                      | Scope           | Location |" << std::endl;
        os << "+----------+----------------+--------------------------------+-----------------+----------+" << std::endl;

        // Display symbols from current and parent scopes
        print_symbols_table(os, 0);

        os << "+=========================================================================================+" << std::endl;
    }

    void SymbolTable::print_symbols_table(std::ostream &os, int scope_level) const
    {
        // Print symbols in current scope
        for (const auto &[name, symbol] : symbols_)
        {
            std::string kind_str = get_symbol_kind_string(symbol.kind);
            std::string type_str = symbol.data_type ? symbol.data_type->to_string() : "unknown";
            std::string location = std::to_string(symbol.declaration_location.line()) +
                                   ":" + std::to_string(symbol.declaration_location.column());

            os << "| " << format_field(kind_str, 8)
               << " | " << format_field(symbol.name, 14)
               << " | " << format_field(type_str, 30)
               << " | " << format_field(symbol.scope, 15)
               << " | " << format_field(location, 8)
               << " |" << std::endl;
        }

        // Print symbols from parent scopes
        if (parent_scope_)
        {
            parent_scope_->print_symbols_table(os, scope_level + 1);
        }
    }

    std::string SymbolTable::format_field(const std::string &text, int width) const
    {
        if (text.length() >= width)
        {
            return text.substr(0, width);
        }
        else
        {
            std::string result = text;
            result.resize(width, ' ');
            return result;
        }
    }

}
