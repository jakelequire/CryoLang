#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

#include "Lexer/lexer.hpp"
#include "AST/Type.hpp" // Add Type include

namespace Cryo
{
    enum class SymbolKind
    {
        Variable,
        Function,
        Intrinsic,
        Type
    };

    struct Symbol
    {
        std::string name;
        SymbolKind kind;
        SourceLocation declaration_location;
        Type *data_type;   // Changed from string to Type*
        std::string scope; // Scope information (e.g., "Global", "main", "test_fn")

        // Default constructor
        Symbol() : name(""), kind(SymbolKind::Variable), declaration_location(SourceLocation()), data_type(nullptr), scope("Global") {}

        // Parameterized constructor
        Symbol(const std::string &n, SymbolKind k, SourceLocation loc, Type *type = nullptr, const std::string &sc = "Global")
            : name(n), kind(k), declaration_location(loc), data_type(type), scope(sc) {}
    };

    class SymbolTable
    {
    private:
        std::unordered_map<std::string, Symbol> symbols_;
        std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> namespaces_; // namespace_name -> symbols
        std::unique_ptr<SymbolTable> parent_scope_;
        TypeContext *type_context_; // Add TypeContext for creating function types

    public:
        SymbolTable(std::unique_ptr<SymbolTable> parent = nullptr, TypeContext *type_context = nullptr)
            : parent_scope_(std::move(parent)), type_context_(type_context) {}

        // Symbol management
        bool declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, Type *data_type = nullptr, const std::string &scope = "Global");
        Symbol *lookup_symbol(const std::string &name) const;
        Symbol *lookup_namespaced_symbol(const std::string &namespace_name, const std::string &symbol_name) const;

        // Namespace management for imports
        void register_namespace(const std::string &namespace_name, const std::unordered_map<std::string, Symbol> &symbols);
        bool has_namespace(const std::string &namespace_name) const;

        // Access symbols for copying to other symbol tables
        const std::unordered_map<std::string, Symbol> &get_symbols() const { return symbols_; }

        // Built-in function registration
        bool declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context);
        bool declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context, const std::string &namespace_scope);
        bool declare_builtin_type(const std::string &name, const std::string &description = "");

        // Type context management
        void set_type_context(TypeContext *type_context) { type_context_ = type_context; }

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

        // Function signature parsing for built-ins
        Type *parse_function_signature(const std::string &signature, TypeContext &type_context);
        Type *convert_string_to_type(const std::string &type_str, TypeContext &type_context);
        std::string trim(const std::string &str);
    };
}