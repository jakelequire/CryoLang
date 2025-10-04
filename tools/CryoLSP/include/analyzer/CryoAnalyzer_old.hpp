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

namespace CryoLSP {

using namespace Cryo::LSP;

struct HoverInfo {
    std::string name;
    std::string type;
    std::string kind;
    std::string signature;
    std::string documentation;
};

struct FunctionSignature {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> parameters; // name, type
    std::string documentation;
    Position definition_location;
};

// Symbol information from compiler analysis
struct SymbolInfo {
    std::string name;
    std::string type;
    std::string kind; // "function", "variable", "struct", "class", etc.
    Position location;
    std::string documentation;
};

// Struct/class information from compiler analysis
struct StructInfo {
    std::string name;
    std::vector<std::pair<std::string, std::string>> members; // name, type
    std::vector<FunctionSignature> methods;
    Position location;
};

// Compiler analysis results for a file
struct CompilerAnalysis {
    std::vector<FunctionSignature> functions;
    std::vector<StructInfo> structs;
    std::vector<SymbolInfo> symbols;
    std::map<int, std::vector<SymbolInfo>> symbolsByLine; // line -> symbols on that line
    bool analysis_successful = false;
    std::string error_message;
    
    CompilerAnalysis() = default;
};

class CryoAnalyzer {
public:
    CryoAnalyzer();
    ~CryoAnalyzer();
    
    // Parse a file and build AST/symbol information
    bool parseFile(const std::string& file_path, const std::string& content);
    
    // Get hover information for a position in a file
    std::optional<HoverInfo> getHoverInfo(const std::string& file_path, const Position& position);
    
    // Get symbol at a specific position
    std::optional<std::string> getSymbolAtPosition(const std::string& file_path, const Position& position);
    
    // Update file content (re-parse)
    void updateFileContent(const std::string& file_path, const std::string& content);
    
    // Enhanced LSP features using AST
    std::optional<FunctionSignature> getFunctionSignature(const std::string& file_path, const Position& position);
    std::vector<HoverInfo> getStructMembers(const std::string& file_path, const std::string& struct_name);
    std::optional<Position> getDefinitionLocation(const std::string& file_path, const Position& position);

private:
    // Compiler analysis results per file
    std::unordered_map<std::string, CompilerAnalysis> analyzed_files_;
    
    // File content cache for fallback analysis
    std::unordered_map<std::string, std::string> file_contents_;
    
    // Run Cryo compiler to analyze file and extract semantic information
    CompilerAnalysis runCompilerAnalysis(const std::string& file_path, const std::string& content);
    
    // Parse compiler output to extract symbols, functions, structs, etc.
    CompilerAnalysis parseCompilerOutput(const std::string& compiler_output);
    
    // Fallback simple analysis when compiler analysis fails
    HoverInfo analyzeSimplePattern(const std::string& content, const Position& position);
    
    // Helper to find symbol at position from compiler analysis
    std::optional<SymbolInfo> findSymbolAtPosition(const std::string& file_path, const Position& position);
    
    // Helper to extract function signature from analysis
    std::optional<FunctionSignature> extractFunctionAtPosition(const std::string& file_path, const Position& position);
    
    // Simple analysis helpers
    std::string getWordAtPosition(const std::string& content, const Position& position);
    HoverInfo analyzeSimpleSymbol(const std::string& word, const std::string& content, const Position& position);
    bool isVariableDeclaration(const std::string& word, const std::string& content, const Position& position);
    std::string getVariableType(const std::string& word, const std::string& content, const Position& position);
};

} // namespace CryoLSP