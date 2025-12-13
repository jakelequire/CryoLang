#pragma once

#include "LSPTypes.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/TypeChecker.hpp"
#include "AST/Type.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>

namespace CryoLSP
{

    struct SymbolInfo
    {
        std::string name;
        std::string kind; // "function", "variable", "type", "namespace", etc.
        std::string type_name;
        std::string signature; // For functions
        std::string documentation;
        Location definition_location;
        std::vector<Location> references;
        std::string scope; // "global", "local", etc.
    };

    struct DiagnosticInfo
    {
        Range range;
        std::string message;
        int severity; // 1=error, 2=warning, 3=info, 4=hint
        std::string code;
        std::string source; // "cryo-compiler"
    };

    struct DocumentSymbol
    {
        std::string name;
        std::string kind;
        Range range;
        Range selection_range; // The range to select when navigating to this symbol
        std::string detail;    // Additional info like type signature
        std::vector<DocumentSymbol> children;
    };

    /**
     * LSPAnalyzer integrates with the Cryo compiler to provide language server features.
     * It maintains document state, parses code, and provides symbol/type analysis.
     */
    class LSPAnalyzer
    {
    private:
        std::unique_ptr<Cryo::CompilerInstance> compiler_;
        std::unordered_map<std::string, std::string> document_cache_;                    // URI -> content
        std::unordered_map<std::string, std::shared_ptr<Cryo::ASTNode>> ast_cache_;      // URI -> AST
        std::unordered_map<std::string, std::vector<DiagnosticInfo>> diagnostics_cache_; // URI -> diagnostics

    public:
        LSPAnalyzer();
        ~LSPAnalyzer();

        // Document management
        void open_document(const std::string &uri, const std::string &content);
        void update_document(const std::string &uri, const std::string &content);
        void close_document(const std::string &uri);

        // Core LSP features
        std::optional<SymbolInfo> get_hover_info(const std::string &uri, const Position &position);
        std::vector<CompletionItem> get_completions(const std::string &uri, const Position &position);
        std::optional<Location> get_definition(const std::string &uri, const Position &position);
        std::vector<Location> get_references(const std::string &uri, const Position &position);
        std::vector<DocumentSymbol> get_document_symbols(const std::string &uri);
        std::vector<DiagnosticInfo> get_diagnostics(const std::string &uri);

        // Utility functions
        std::string position_to_string(const Position &pos) const;
        Position source_location_to_position(const Cryo::SourceLocation &loc) const;
        Range source_range_to_range(const Cryo::SourceLocation &start, const Cryo::SourceLocation &end) const;

    private:
        // Internal analysis methods
        std::shared_ptr<Cryo::ASTNode> parse_document_safe(const std::string &uri, const std::string &content);
        Cryo::ASTNode *find_node_at_position(Cryo::ASTNode *root, const Position &position);

        SymbolInfo extract_symbol_info(Cryo::ASTNode *node, const std::string &uri);
        std::string get_type_string(Cryo::Type *type) const;

        std::string get_literal_type_string(Cryo::TokenKind literal_kind) const;

        // Symbol resolution helpers
        std::vector<CompletionItem> get_scope_completions(const std::string &uri, const Position &position);
        std::vector<CompletionItem> get_builtin_completions() const;

        // Diagnostic helpers
        std::vector<DiagnosticInfo> extract_diagnostics_from_compiler(const std::string &uri);
        void add_diagnostic_error(const std::string &uri, const std::string &message);
        void add_diagnostic_warning(const std::string &uri, const std::string &message);
        void clear_diagnostics(const std::string &uri);

        // Helper to convert URI to file path
        std::string uri_to_file_path(const std::string &uri) const;
        std::string file_path_to_uri(const std::string &file_path) const;

        // Helper to extract word at cursor position
        std::string extract_word_at_position(const std::string &content, const Position &position);

        // Real symbol resolution using compiler APIs
        std::optional<SymbolInfo> resolve_symbol_with_compiler(const std::string &symbol_name,
                                                               const Position &position,
                                                               Cryo::ASTNode *ast_root);

        // Helper methods for documentation
        std::string get_keyword_documentation(const std::string &keyword);
        std::string get_type_documentation(const std::string &type_name);
    };

} // namespace CryoLSP