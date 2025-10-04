#pragma once

#include "../LSPMessage.hpp"
#include "../Logger.hpp"

#include <string>
#include <optional>
#include <memory>
#include <unordered_map>

// Forward declarations to avoid heavy includes for now
namespace Cryo {
    class Symbol;
    class SourceLocation;
    class Type;
}

namespace CryoLSP {

using namespace Cryo::LSP;

struct HoverInfo {
    std::string name;
    std::string type;
    std::string kind;
    std::string signature;
    std::string documentation;
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

private:
    // Helper methods for simple pattern matching
    std::string getWordAtPosition(const std::string& content, const Position& position);
    HoverInfo analyzeSimpleSymbol(const std::string& word, const std::string& content, const Position& position);
    bool isVariableDeclaration(const std::string& word, const std::string& content, const Position& position);
    std::string getVariableType(const std::string& word, const std::string& content, const Position& position);
    
    // Store file contents for simple analysis
    std::unordered_map<std::string, std::string> file_contents_;
};

} // namespace CryoLSP