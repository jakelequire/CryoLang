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

#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <map>

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
    };

    struct FunctionSignature
    {
        std::string name;
        std::string return_type;
        std::vector<std::pair<std::string, std::string>> parameters; // name, type
        std::string documentation;
        Position definition_location;
    };

    // File analysis results with full compiler integration
    struct FileAnalysis
    {
        Cryo::ProgramNode *ast; // Raw pointer to AST owned by compiler
        std::unique_ptr<Cryo::CompilerInstance> compiler;
        std::string content;
        bool parsed_successfully;

        FileAnalysis() : ast(nullptr), parsed_successfully(false) {}
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

        // Get symbol at a specific position
        std::optional<std::string> getSymbolAtPosition(const std::string &file_path, const Position &position);

        // Update file content (re-parse)
        void updateFileContent(const std::string &file_path, const std::string &content);

        // Enhanced LSP features using direct compiler access
        std::optional<FunctionSignature> getFunctionSignature(const std::string &file_path, const Position &position);
        std::vector<HoverInfo> getStructMembers(const std::string &file_path, const std::string &struct_name);
        std::optional<Position> getDefinitionLocation(const std::string &file_path, const Position &position);

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

        // Extract symbol information from symbol table
        std::optional<Cryo::Symbol *> findSymbolInSymbolTable(const Cryo::SymbolTable &symbol_table, const std::string &symbol_name);

        // Extract function signature from AST
        std::optional<FunctionSignature> extractFunctionFromAST(const Cryo::ProgramNode *ast, const Position &position);

        // Fallback simple analysis when compiler analysis fails
        HoverInfo analyzeSimplePattern(const std::string &content, const Position &position);

        // Simple analysis helpers
        std::string getWordAtPosition(const std::string &content, const Position &position);
        HoverInfo analyzeSimpleSymbol(const std::string &word, const std::string &content, const Position &position);
        bool isVariableDeclaration(const std::string &word, const std::string &content, const Position &position);
        std::string getVariableType(const std::string &word, const std::string &content, const Position &position);
    };

} // namespace CryoLSP