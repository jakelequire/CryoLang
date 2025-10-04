#include "../../include/DocumentManager.hpp"
#include "../../include/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

namespace Cryo {
namespace LSP {

void DocumentManager::didOpen(const std::string& uri, const std::string& languageId, int version, const std::string& text) {
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "Document opened: " + uri);
    
    TextDocumentItem doc;
    doc.uri = uri;
    doc.languageId = languageId;
    doc.version = version;
    doc.text = text;
    
    documents_[uri] = doc;
}

void DocumentManager::didChange(const std::string& uri, int version, const std::string& text) {
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "Document changed: " + uri);
    
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second.version = version;
        it->second.text = text;
    }
}

void DocumentManager::didClose(const std::string& uri) {
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "Document closed: " + uri);
    
    documents_.erase(uri);
}

std::optional<TextDocumentItem> DocumentManager::getDocument(const std::string& uri) const {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> DocumentManager::getDocumentText(const std::string& uri) const {
    auto doc = getDocument(uri);
    if (doc.has_value()) {
        return doc->text;
    }
    return std::nullopt;
}

std::optional<HoverResult> DocumentManager::getHoverInfo(const std::string& uri, const Position& position) {
    Logger& logger = Logger::instance();
    logger.debug("DocumentManager", "Hover request for " + uri + " at line " + std::to_string(position.line) + ", char " + std::to_string(position.character));
    
    auto text = getDocumentText(uri);
    if (!text.has_value()) {
        logger.debug("DocumentManager", "No document found for hover request");
        return std::nullopt;
    }
    
    std::string word = getWordAtPosition(text.value(), position);
    if (word.empty()) {
        logger.debug("DocumentManager", "No word found at position");
        return std::nullopt;
    }
    
    logger.debug("DocumentManager", "Word at position: '" + word + "'");
    
    std::string hoverContent = getHoverContentForSymbol(word, uri);
    if (hoverContent.empty()) {
        return std::nullopt;
    }
    
    Position start = findWordStart(text.value(), position);
    Position end = findWordEnd(text.value(), position);
    
    return HoverResult(hoverContent, start, end);
}

std::string DocumentManager::getWordAtPosition(const std::string& text, const Position& position) {
    // Split text into lines
    std::vector<std::string> lines;
    std::string current_line;
    
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current_line);
            current_line.clear();
        } else if (c != '\r') {
            current_line += c;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    // Check bounds
    if (position.line < 0 || position.line >= static_cast<int>(lines.size())) {
        return "";
    }
    
    const std::string& line = lines[position.line];
    if (position.character < 0 || position.character >= static_cast<int>(line.length())) {
        return "";
    }
    
    // Find word boundaries
    int start = position.character;
    int end = position.character;
    
    // Move start backwards to find word start
    while (start > 0 && (std::isalnum(line[start - 1]) || line[start - 1] == '_')) {
        start--;
    }
    
    // Move end forwards to find word end
    while (end < static_cast<int>(line.length()) && (std::isalnum(line[end]) || line[end] == '_')) {
        end++;
    }
    
    if (start == end) {
        return "";
    }
    
    return line.substr(start, end - start);
}

Position DocumentManager::findWordStart(const std::string& text, const Position& position) {
    // Similar to getWordAtPosition but returns position
    std::vector<std::string> lines;
    std::string current_line;
    
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current_line);
            current_line.clear();
        } else if (c != '\r') {
            current_line += c;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    if (position.line < 0 || position.line >= static_cast<int>(lines.size())) {
        return position;
    }
    
    const std::string& line = lines[position.line];
    if (position.character < 0 || position.character >= static_cast<int>(line.length())) {
        return position;
    }
    
    int start = position.character;
    while (start > 0 && (std::isalnum(line[start - 1]) || line[start - 1] == '_')) {
        start--;
    }
    
    return Position(position.line, start);
}

Position DocumentManager::findWordEnd(const std::string& text, const Position& position) {
    // Similar to getWordAtPosition but returns end position
    std::vector<std::string> lines;
    std::string current_line;
    
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current_line);
            current_line.clear();
        } else if (c != '\r') {
            current_line += c;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    if (position.line < 0 || position.line >= static_cast<int>(lines.size())) {
        return position;
    }
    
    const std::string& line = lines[position.line];
    if (position.character < 0 || position.character >= static_cast<int>(line.length())) {
        return position;
    }
    
    int end = position.character;
    while (end < static_cast<int>(line.length()) && (std::isalnum(line[end]) || line[end] == '_')) {
        end++;
    }
    
    return Position(position.line, end);
}

std::string DocumentManager::getHoverContentForSymbol(const std::string& symbol, const std::string& uri) {
    // For now, provide basic hover information for common CryoLang symbols
    
    // Built-in types
    if (symbol == "int") {
        return "**int** - 32-bit signed integer type\n\nExample: `let x: int = 42;`";
    }
    if (symbol == "string") {
        return "**string** - UTF-8 string type\n\nExample: `let name: string = \"Hello\";`";
    }
    if (symbol == "bool") {
        return "**bool** - Boolean type (true/false)\n\nExample: `let flag: bool = true;`";
    }
    if (symbol == "float") {
        return "**float** - 64-bit floating point type\n\nExample: `let pi: float = 3.14159;`";
    }
    if (symbol == "void") {
        return "**void** - No value type\n\nUsed for functions that don't return a value";
    }
    
    // Keywords
    if (symbol == "fn") {
        return "**fn** - Function declaration keyword\n\nExample: `fn myFunction() -> int { return 42; }`";
    }
    if (symbol == "let") {
        return "**let** - Variable declaration keyword\n\nExample: `let x: int = 10;`";
    }
    if (symbol == "struct") {
        return "**struct** - Structure declaration keyword\n\nExample: `struct Point { x: int, y: int }`";
    }
    if (symbol == "class") {
        return "**class** - Class declaration keyword\n\nExample: `class MyClass { ... }`";
    }
    if (symbol == "if") {
        return "**if** - Conditional statement keyword\n\nExample: `if (condition) { ... }`";
    }
    if (symbol == "return") {
        return "**return** - Return statement keyword\n\nExample: `return value;`";
    }
    
    // Built-in functions
    if (symbol == "print") {
        return "**print** - Built-in function to output text\n\nExample: `print(\"Hello, World!\");`";
    }
    
    // Generic symbol information
    return "**" + symbol + "** - CryoLang symbol\n\n*Hover over built-in types and keywords for more information*";
}

} // namespace LSP
} // namespace Cryo