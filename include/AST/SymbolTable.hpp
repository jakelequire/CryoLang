#pragma once
#include <string>
#include <unordered_map>
#include <memory>

#include "Lexer/lexer.hpp"

namespace Cryo
{
    enum class SymbolKind
    {
        Variable,
        Function,
        Type
    };

    struct Symbol
    {
        std::string name;
        SymbolKind kind;
        SourceLocation declaration_location;
        // Type information, etc.
    };

    class SymbolTable
    {
    private:
        std::unordered_map<std::string, Symbol> symbols_;
        std::unique_ptr<SymbolTable> parent_scope_;

    public:
        SymbolTable(std::unique_ptr<SymbolTable> parent = nullptr)
            : parent_scope_(std::move(parent)) {}

        // Symbol management
        bool declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc);
        Symbol *lookup_symbol(const std::string &name);

        // Scope management
        std::unique_ptr<SymbolTable> enter_scope();
        std::unique_ptr<SymbolTable> exit_scope();
    };
}