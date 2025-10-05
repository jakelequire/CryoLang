#pragma once

#include "../LSPMessage.hpp"
#include "../Logger.hpp"

// Include Cryo compiler frontend components
#include "Compiler/CompilerInstance.hpp"
#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/TypeChecker.hpp"
#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/file.hpp"
#include "GDM/GDM.hpp" // For diagnostic support

#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <map>
#include <chrono>
#include <regex>

namespace CryoLSP
{

    using namespace Cryo::LSP;

    struct HoverInfo
    {
        std::string name;
        std::string type;
        std::string kind;
        std::string signature;
        std::string documentation;
        std::string scope;          // Full scope path (e.g., "std::IO")
        std::string qualified_name; // Full qualified name (e.g., "std::IO::println")
        Position definition_location;
        Position start_pos;         // Start position for range highlighting
        Position end_pos;           // End position for range highlighting
        bool has_errors = false;    // Whether symbol has compilation errors
    };

    struct FunctionSignature
    {
        std::string name;
        std::string return_type;
        std::vector<std::pair<std::string, std::string>> parameters; // name, type
        std::string documentation;
        std::string scope;
        std::string qualified_name;
        Position definition_location;
    };

    // LSP Diagnostic compatible with VS Code
    struct LSPDiagnostic
    {
        Position start;
        Position end;
        std::string message;
        std::string severity; // "error", "warning", "info", "hint"
        std::string source;   // "cryo-lsp"
        std::string code;     // Error code if available
    };

    // File analysis results with full compiler integration
    struct FileAnalysis
    {
        Cryo::ProgramNode *ast; // Raw pointer to AST owned by compiler
        std::unique_ptr<Cryo::CompilerInstance> compiler;
        std::string content;
        bool parsed_successfully;
        std::vector<LSPDiagnostic> diagnostics; // Compilation errors/warnings
        std::chrono::steady_clock::time_point last_analyzed;

        FileAnalysis() : ast(nullptr), parsed_successfully(false), last_analyzed(std::chrono::steady_clock::now()) {}
    };

    class CryoAnalyzer
    {
    public:
        CryoAnalyzer();
        ~CryoAnalyzer();

        // Parse a file using direct compiler integration
        bool parseFile(const std::string &file_path, const std::string &content);

        // Get hover information for a position in a file
        std::optional<HoverInfo> getHoverInfo(const std::string &file_path, const Position &position);

        // Get hover information with retry mechanism
        std::optional<HoverInfo> getHoverInfoWithRetry(const std::string &file_path, const Position &position, int max_retries = 2);

        // Force reprocess a document with fresh compiler instance
        bool forceReprocessDocument(const std::string &file_path);

        // Get symbol at a specific position
        std::optional<std::string> getSymbolAtPosition(const std::string &file_path, const Position &position);

        // Update file content (re-parse)
        void updateFileContent(const std::string &file_path, const std::string &content);

        // Enhanced LSP features using direct compiler access
        std::optional<FunctionSignature> getFunctionSignature(const std::string &file_path, const Position &position);
        std::vector<HoverInfo> getStructMembers(const std::string &file_path, const std::string &struct_name);
        std::optional<Position> getDefinitionLocation(const std::string &file_path, const Position &position);

        // Diagnostic support
        std::vector<LSPDiagnostic> getDiagnostics(const std::string &file_path);
        void clearDiagnostics(const std::string &file_path);

        // Scope-aware symbol resolution
        std::optional<HoverInfo> getQualifiedSymbolInfo(const std::string &file_path, const std::string &qualified_name);
        std::vector<std::string> getAvailableNamespaces(const std::string &file_path);
        std::vector<HoverInfo> getAllSymbolsInScope(const std::string &file_path, const std::string &scope = "");

    private:
        // File analysis results per file (with full compiler instances)
        std::unordered_map<std::string, FileAnalysis> analyzed_files_;

        // File content cache for fallback analysis
        std::unordered_map<std::string, std::string> file_contents_;

        // Resource management constants
        static constexpr size_t MAX_CACHED_FILES = 50; // Limit cached files to prevent unbounded growth

        // Rate limiting for hover requests
        std::chrono::steady_clock::time_point last_hover_time_;
        static constexpr std::chrono::milliseconds MIN_HOVER_INTERVAL{50}; // Minimum 50ms between hover requests

        // Resource management
        void cleanupOldAnalysisIfNeeded();

        // Direct compiler-based analysis
        HoverInfo analyzeWithCompiler(const std::string &file_path, const Position &position);

        // Enhanced symbol table search with scope support
        std::optional<Cryo::Symbol *> findSymbolInSymbolTable(const Cryo::SymbolTable &symbol_table, const std::string &symbol_name);
        std::optional<Cryo::Symbol *> findQualifiedSymbol(const Cryo::SymbolTable &symbol_table, const std::string &qualified_name);
        std::optional<Cryo::Symbol *> findSymbolWithImports(const Cryo::SymbolTable &symbol_table, const std::string &symbol_name, const std::vector<std::string> &imported_namespaces);

        // Diagnostic extraction from compiler
        std::vector<LSPDiagnostic> extractDiagnosticsFromCompiler(const Cryo::CompilerInstance &compiler);
        LSPDiagnostic convertCryoDiagnosticToLSP(const Cryo::Diagnostic &diagnostic);
        Position convertSourceLocationToPosition(const Cryo::SourceLocation &loc);

        // Enhanced hover info construction
        // Enhanced hover info building with compiler context
        HoverInfo buildEnhancedHoverInfo(const Cryo::Symbol &symbol, const std::string &word, const std::string &file_path, Cryo::CompilerInstance *compiler = nullptr);
        
        // Function signature extraction from source text
        std::string extractFunctionSignatureFromSource(const std::string &file_path, const std::string &function_name);
        std::string extractParameterNamesFromAST(Cryo::FunctionDeclarationNode *function_node);
        std::string extractParameterNamesFromAST(Cryo::FunctionDeclarationNode *function_node, const std::string &qualified_name);
        std::string buildQualifiedSignature(const Cryo::Symbol &symbol, const std::string &file_path, Cryo::CompilerInstance *compiler = nullptr);
        std::string getSymbolDocumentation(const Cryo::Symbol &symbol, const std::string &symbol_name);
        
        // Find function AST nodes in imported modules
        Cryo::FunctionDeclarationNode* findFunctionInImportedModules(Cryo::CompilerInstance *compiler, const std::string &word, const std::string &qualified_symbol);
        
        // Helper method to recursively search for function declarations in an AST
        Cryo::FunctionDeclarationNode* findFunctionInAST(Cryo::ASTNode *node, const std::string &word, const std::string &qualified_symbol, const std::string &module_name);

        // Additional methods that should be declared
        std::optional<FunctionSignature> extractFunctionFromAST(const Cryo::ProgramNode *ast, const Position &position);
        HoverInfo analyzeSimplePattern(const std::string &content, const Position &position);
        std::string getWordAtPosition(const std::string &content, const Position &position);
        std::string getLineAtPosition(const std::string &content, const Position &position);
        std::string getPrimitiveTypeDocumentation(const std::string &type_name);
        std::string getKeywordDocumentation(const std::string &keyword);
        std::string getLiteralDocumentation(const std::string &literal, const std::string &content, const Position &position);
        std::string getBuiltinFunctionDocumentation(const std::string &function_name);
        HoverInfo analyzeSimpleSymbol(const std::string &word, const std::string &content, const Position &position);
        bool isVariableDeclaration(const std::string &word, const std::string &content, const Position &position);
        std::string getVariableType(const std::string &word, const std::string &content, const Position &position);
        bool findVariableMutability(const std::string &file_path, const std::string &variable_name);
        bool findVariableInAST(Cryo::ASTNode *node, const std::string &variable_name);
        std::string extractQualifiedSymbolAtPosition(const std::string &line, const Position &position);
    };

} // namespace CryoLSP