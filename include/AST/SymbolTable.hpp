#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

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
        std::string data_type; // Data type information (e.g., "int", "string", "void", etc.)
        std::string scope;     // Scope information (e.g., "Global", "main", "test_fn")

        // Default constructor
        Symbol() : name(""), kind(SymbolKind::Variable), declaration_location(SourceLocation()), data_type("unknown"), scope("Global") {}

        // Parameterized constructor
        Symbol(const std::string &n, SymbolKind k, SourceLocation loc, const std::string &type = "unknown", const std::string &sc = "Global")
            : name(n), kind(k), declaration_location(loc), data_type(type), scope(sc) {}
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
        bool declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, const std::string &data_type = "unknown", const std::string &scope = "Global");
        Symbol *lookup_symbol(const std::string &name);

        // Scope management
        std::unique_ptr<SymbolTable> enter_scope();
        std::unique_ptr<SymbolTable> exit_scope();

        // Debug/Pretty printing
        void dump(std::ostream &os = std::cout, int indent_level = 0) const;
        void print_pretty(std::ostream &os = std::cout) const;

    private:
        std::string get_symbol_kind_string(SymbolKind kind) const;
        void print_symbols_table(std::ostream &os, int scope_level) const;
        std::string format_field(const std::string &text, int width) const;
    };
}