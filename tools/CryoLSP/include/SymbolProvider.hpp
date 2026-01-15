#pragma once

#include "LSPTypes.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Types/TypeChecker.hpp"
#include "AST/ASTNode.hpp"
#include <vector>
#include <optional>
#include <memory>

namespace CryoLSP
{

    struct SymbolInfo
    {
        std::string name;
        std::string qualified_name;
        SymbolKind kind;
        std::string type_info;
        std::string detail;
        Location location;
        Range selection_range;
        std::vector<Location> references;
        bool deprecated = false;

        DocumentSymbol to_document_symbol() const;
    };

    /**
     * @brief Provides symbol resolution and navigation services
     *
     * Leverages the CryoLang compiler's type system and AST to provide
     * accurate symbol information for IDE features.
     */
    class SymbolProvider
    {
    public:
        SymbolProvider(Cryo::CompilerInstance *compiler);
        ~SymbolProvider() = default;

        // Symbol resolution
        std::optional<SymbolInfo> resolve_symbol_at_position(const std::string &uri, const Position &position);
        std::vector<SymbolInfo> get_document_symbols(const std::string &uri);
        std::vector<SymbolInfo> get_workspace_symbols(const std::string &query);

        // Navigation
        std::vector<Location> find_definition(const std::string &uri, const Position &position);
        std::vector<Location> find_references(const std::string &uri, const Position &position, bool include_declaration = false);
        std::vector<Location> find_implementations(const std::string &uri, const Position &position);

        // Symbol information
        std::string get_symbol_signature(const std::string &symbol_name, const std::string &uri);
        std::string get_symbol_documentation(const std::string &symbol_name, const std::string &uri);
        SymbolKind determine_symbol_kind(Cryo::ASTNode *node);

        // Type information
        std::string get_type_at_position(const std::string &uri, const Position &position);
        std::string format_type_info(Cryo::Type *type);

    private:
        Cryo::CompilerInstance *_compiler;

        // AST navigation helpers
        Cryo::ASTNode *find_node_at_position(Cryo::ASTNode *root, const Position &position);
        std::vector<SymbolInfo> extract_symbols_from_ast(Cryo::ASTNode *root, const std::string &uri);
        Location ast_location_to_lsp_location(const Cryo::SourceLocation &source_loc, const std::string &uri);
        Position source_location_to_position(const Cryo::SourceLocation &source_loc);

        // Symbol extraction from specific AST nodes
        std::optional<SymbolInfo> extract_function_symbol(Cryo::FunctionDeclarationNode *func_node, const std::string &uri);
        std::optional<SymbolInfo> extract_struct_symbol(Cryo::StructDeclarationNode *struct_node, const std::string &uri);
        std::optional<SymbolInfo> extract_class_symbol(Cryo::ClassDeclarationNode *class_node, const std::string &uri);
        std::optional<SymbolInfo> extract_variable_symbol(Cryo::VariableDeclarationNode *var_node, const std::string &uri);
        std::optional<SymbolInfo> extract_enum_symbol(Cryo::EnumDeclarationNode *enum_node, const std::string &uri);

        // Type checker integration
        std::string get_hover_info_for_symbol(const std::string &symbol_name);
        std::string format_function_signature(Cryo::FunctionDeclarationNode *func_node);
        std::string format_struct_info(Cryo::StructDeclarationNode *struct_node);
        std::string format_class_info(Cryo::ClassDeclarationNode *class_node);
    };

} // namespace CryoLSP
