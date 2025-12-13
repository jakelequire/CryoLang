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
        Type *data_type;              // Changed from string to Type*
        std::string scope;            // Scope information (e.g., "Global", "main", "test_fn")
        std::string enhanced_display; // Optional enhanced display string for complex types like generics

        // Default constructor
        Symbol() : name(""), kind(SymbolKind::Variable), declaration_location(SourceLocation()), data_type(nullptr), scope("Global"), enhanced_display("") {}

        // Parameterized constructor
        Symbol(const std::string &n, SymbolKind k, SourceLocation loc, Type *type = nullptr, const std::string &sc = "Global", const std::string &enhanced = "")
            : name(n), kind(k), declaration_location(loc), data_type(type), scope(sc), enhanced_display(enhanced) {}
    };

    // Forward declarations for SRM integration
    namespace SRM
    {
        class SymbolResolutionContext;
        class SymbolResolutionManager;
        class SymbolIdentifier;
    }

    class SymbolTable
    {
    private:
        std::unordered_map<std::string, Symbol> symbols_;
        std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> namespaces_; // namespace_name -> symbols
        std::unique_ptr<SymbolTable> parent_scope_;
        TypeContext *type_context_; // Add TypeContext for creating function types

        // SRM integration - lazy initialization to maintain compatibility
        mutable std::unique_ptr<SRM::SymbolResolutionContext> srm_context_;
        mutable std::unique_ptr<SRM::SymbolResolutionManager> srm_manager_;

    public:
        // Constructor and destructor declarations - implementations in .cpp file to handle forward declarations
        SymbolTable(std::unique_ptr<SymbolTable> parent = nullptr, TypeContext *type_context = nullptr);
        ~SymbolTable();

        // Symbol management
        bool declare_symbol(const std::string &name, SymbolKind kind, SourceLocation loc, Type *data_type = nullptr, const std::string &scope = "Global", const std::string &enhanced_display = "");
        Symbol *lookup_symbol(const std::string &name) const;
        Symbol *lookup_namespaced_symbol(const std::string &namespace_name, const std::string &symbol_name) const;
        Symbol *lookup_namespaced_symbol_with_context(const std::string &namespace_name, const std::string &symbol_name, const std::string &current_namespace) const;
        Symbol *lookup_symbol_in_any_namespace(const std::string &symbol_name) const;

        // Enhanced import resolution
        Symbol *lookup_symbol_with_import_resolution(const std::string &symbol_name, const std::vector<std::string> &imported_namespaces) const;
        Symbol *lookup_qualified_symbol_with_import_shortcuts(const std::string &qualified_name, const std::vector<std::string> &imported_namespaces) const;

        // Namespace management for imports
        void register_namespace(const std::string &namespace_name, const std::unordered_map<std::string, Symbol> &symbols);
        bool has_namespace(const std::string &namespace_name) const;

        // Access symbols for copying to other symbol tables
        const std::unordered_map<std::string, Symbol> &get_symbols() const { return symbols_; }
        const std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> &get_namespaces() const { return namespaces_; }

        // Built-in function registration
        bool declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context);
        bool declare_builtin_function(const std::string &name, const std::string &signature, TypeContext &type_context, const std::string &namespace_scope);
        bool declare_builtin_type(const std::string &name, const std::string &description = "");

        // Type context management
        void set_type_context(TypeContext *type_context) { type_context_ = type_context; }
        TypeContext *get_type_context() const { return type_context_; }

        // Scope management
        std::unique_ptr<SymbolTable> enter_scope();
        std::unique_ptr<SymbolTable> exit_scope();

        // SRM integration methods
        SRM::SymbolResolutionContext *get_srm_context() const;
        SRM::SymbolResolutionManager *get_srm_manager() const;
        void initialize_srm() const;

        // Enhanced symbol lookup using SRM
        Symbol *lookup_symbol_srm(const SRM::SymbolIdentifier &identifier) const;
        std::vector<Symbol *> find_symbol_overloads(const std::string &base_name, const std::vector<Type *> &arg_types) const;
        Symbol *find_best_function_overload(const std::string &base_name, const std::vector<Type *> &arg_types) const;

        // SRM-based symbol registration
        bool declare_symbol_srm(std::unique_ptr<SRM::SymbolIdentifier> identifier,
                                SymbolKind kind,
                                SourceLocation loc,
                                Type *data_type = nullptr,
                                const std::string &scope = "Global");

        // SRM configuration
        void configure_srm(bool enable_implicit_std = true, bool enable_namespace_fallback = true) const;
        void add_srm_namespace_alias(const std::string &alias, const std::string &full_namespace) const;
        void add_srm_imported_namespace(const std::string &namespace_name) const;

        // Debug/Pretty printing
        void dump(std::ostream &os = std::cout, int indent_level = 0) const;
        void print_pretty(std::ostream &os = std::cout) const;
        void dump_srm_state(std::ostream &os = std::cout) const;

        // LSP-specific API
        struct LSPSymbolInfo
        {
            std::string name;
            std::string type_name;
            std::string kind; // "function", "variable", "class", "struct", "enum", etc.
            std::string scope;
            std::string namespace_name;
            size_t definition_line;
            size_t definition_column;
            std::string definition_file;
            std::string documentation;
            bool is_generic;
            std::vector<std::string> generic_parameters;
        };

        std::vector<LSPSymbolInfo> get_all_symbols_for_lsp() const;
        std::optional<LSPSymbolInfo> find_symbol_at_position(const std::string &filename,
                                                             size_t line, size_t column) const;
        std::vector<LSPSymbolInfo> find_symbols_by_name(const std::string &name) const;
        std::vector<LSPSymbolInfo> get_completions_for_scope(const std::string &scope_name) const;

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