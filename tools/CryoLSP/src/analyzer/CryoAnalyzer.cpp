#include "../../include/analyzer/CryoAnalyzer.hpp"
#include "../../include/Logger.hpp"

#include <fstream>
#include <sstream>

using namespace CryoLSP;
using namespace Cryo::LSP;

namespace CryoLSP {

CryoAnalyzer::CryoAnalyzer() {
    Logger::instance().debug("CryoAnalyzer", "Initializing CryoLang analyzer...");
    Logger::instance().debug("CryoAnalyzer", "CryoLang analyzer initialized successfully");
}

CryoAnalyzer::~CryoAnalyzer() = default;

bool CryoAnalyzer::parseFile(const std::string& file_path, const std::string& content) {
    Logger::instance().debug("CryoAnalyzer", "Parsing file: {}", file_path);
    
    // For now, just store the content - we'll implement real parsing later
    file_contents_[file_path] = content;
    
    Logger::instance().debug("CryoAnalyzer", "Successfully stored file: {}", file_path);
    return true;
}

std::optional<HoverInfo> CryoAnalyzer::getHoverInfo(const std::string& file_path, const Position& position) {
    Logger::instance().debug("CryoAnalyzer", "Getting hover info for {}:{}:{}", file_path, position.line, position.character);
    
    // Get file content
    auto it = file_contents_.find(file_path);
    if (it == file_contents_.end()) {
        Logger::instance().debug("CryoAnalyzer", "File not found: {}", file_path);
        return std::nullopt;
    }
    
    const std::string& content = it->second;
    
    // Simple word extraction at position
    std::string word = getWordAtPosition(content, position);
    if (word.empty()) {
        return std::nullopt;
    }
    
    Logger::instance().debug("CryoAnalyzer", "Word at position: '{}'", word);
    
    // Simple pattern matching for CryoLang syntax
    HoverInfo hover = analyzeSimpleSymbol(word, content, position);
    
    if (!hover.name.empty()) {
        Logger::instance().debug("CryoAnalyzer", "Found symbol info: {} of type {}", hover.name, hover.type);
        return hover;
    }
    
    return std::nullopt;
}

std::optional<std::string> CryoAnalyzer::getSymbolAtPosition(const std::string& file_path, const Position& position) {
    // Placeholder - not implemented yet
    return std::nullopt;
}

void CryoAnalyzer::updateFileContent(const std::string& file_path, const std::string& content) {
    Logger::instance().debug("CryoAnalyzer", "Updating file content: {}", file_path);
    parseFile(file_path, content);
}

std::string CryoAnalyzer::getWordAtPosition(const std::string& content, const Position& position) {
    // Split content into lines
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    
    if (position.line >= static_cast<int>(lines.size())) {
        return "";
    }
    
    const std::string& target_line = lines[position.line];
    if (position.character >= static_cast<int>(target_line.length())) {
        return "";
    }
    
    // Find word boundaries
    int start = position.character;
    int end = position.character;
    
    // Go back to find start of word
    while (start > 0 && (std::isalnum(target_line[start - 1]) || target_line[start - 1] == '_')) {
        start--;
    }
    
    // Go forward to find end of word
    while (end < static_cast<int>(target_line.length()) && (std::isalnum(target_line[end]) || target_line[end] == '_')) {
        end++;
    }
    
    if (start >= end) {
        return "";
    }
    
    return target_line.substr(start, end - start);
}

HoverInfo CryoAnalyzer::analyzeSimpleSymbol(const std::string& word, const std::string& content, const Position& position) {
    HoverInfo hover;
    hover.name = word;
    
    // Check for CryoLang built-in types
    if (word == "int") {
        hover.type = "primitive";
        hover.kind = "type";
        hover.signature = "int";
        hover.documentation = "32-bit signed integer type";
        return hover;
    }
    
    if (word == "string") {
        hover.type = "primitive";
        hover.kind = "type";
        hover.signature = "string";
        hover.documentation = "UTF-8 string type";
        return hover;
    }
    
    if (word == "boolean") {
        hover.type = "primitive";
        hover.kind = "type";
        hover.signature = "boolean";
        hover.documentation = "Boolean type (true/false)";
        return hover;
    }
    
    if (word == "float") {
        hover.type = "primitive";
        hover.kind = "type";
        hover.signature = "float";
        hover.documentation = "64-bit floating point type";
        return hover;
    }
    
    // Check for keywords
    if (word == "function") {
        hover.type = "keyword";
        hover.kind = "keyword";
        hover.signature = "function";
        hover.documentation = "Function declaration keyword";
        return hover;
    }
    
    if (word == "mut" || word == "const") {
        hover.type = "keyword";
        hover.kind = "keyword";
        hover.signature = word;
        hover.documentation = "Variable declaration keyword";
        return hover;
    }
    
    // Simple variable pattern matching
    if (isVariableDeclaration(word, content, position)) {
        hover.type = getVariableType(word, content, position);
        hover.kind = "variable";
        hover.signature = "const " + word + ": " + hover.type;
        hover.documentation = "Local variable";
        return hover;
    }
    
    // Return empty hover if no match
    return HoverInfo{};
}

bool CryoAnalyzer::isVariableDeclaration(const std::string& word, const std::string& content, const Position& position) {
    // Simple pattern: look for "const word:" or "mut word:" in the content
    std::string pattern1 = "const " + word + ":";
    std::string pattern2 = "mut " + word + ":";
    
    return content.find(pattern1) != std::string::npos || content.find(pattern2) != std::string::npos;
}

std::string CryoAnalyzer::getVariableType(const std::string& word, const std::string& content, const Position& position) {
    // Simple pattern matching for type extraction
    std::string pattern1 = "const " + word + ":";
    std::string pattern2 = "mut " + word + ":";
    
    size_t pos = content.find(pattern1);
    if (pos == std::string::npos) {
        pos = content.find(pattern2);
        if (pos == std::string::npos) {
            return "unknown";
        }
        pos += pattern2.length();
    } else {
        pos += pattern1.length();
    }
    
    // Skip whitespace
    while (pos < content.length() && std::isspace(content[pos])) {
        pos++;
    }
    
    // Extract type name
    std::string type;
    while (pos < content.length() && (std::isalnum(content[pos]) || content[pos] == '_')) {
        type += content[pos];
        pos++;
    }
    
    return type.empty() ? "unknown" : type;
}

} // namespace CryoLSP