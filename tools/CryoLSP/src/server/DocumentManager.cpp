#include "../../include/DocumentManager.hpp"
#include "../../include/Logger.hpp"
#include "../../include/analyzer/CryoAnalyzer.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

namespace Cryo {
namespace LSP {

DocumentManager::DocumentManager() {
    analyzer_ = std::make_unique<CryoLSP::CryoAnalyzer>();
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "DocumentManager initialized with CryoAnalyzer");
}

DocumentManager::~DocumentManager() = default;

void DocumentManager::didOpen(const std::string& uri, const std::string& languageId, int version, const std::string& text) {
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "Document opened: " + uri);
    
    TextDocumentItem doc;
    doc.uri = uri;
    doc.languageId = languageId;
    doc.version = version;
    doc.text = text;
    
    documents_[uri] = doc;
    
    // Parse the file with CryoAnalyzer
    if (languageId == "cryo") {
        std::string file_path = uriToFilePath(uri);
        if (analyzer_->parseFile(file_path, text)) {
            logger.debug("DocumentManager", "Successfully parsed file: " + file_path);
        } else {
            logger.error("DocumentManager", "Failed to parse file: " + file_path);
        }
    }
}

void DocumentManager::didChange(const std::string& uri, int version, const std::string& text) {
    Logger& logger = Logger::instance();
    logger.info("DocumentManager", "Document changed: " + uri);
    
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second.version = version;
        it->second.text = text;
        
        // Update analyzer with new content
        if (it->second.languageId == "cryo") {
            std::string file_path = uriToFilePath(uri);
            analyzer_->updateFileContent(file_path, text);
        }
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
    Logger& logger = Logger::instance();
    auto doc = getDocument(uri);
    if (doc.has_value()) {
        logger.debug("DocumentManager", "Retrieved document text for " + uri + ", length: " + std::to_string(doc->text.length()));
        return doc->text;
    }
    logger.debug("DocumentManager", "No document found for URI: " + uri);
    return std::nullopt;
}

std::optional<HoverResult> DocumentManager::getHoverInfo(const std::string& uri, const Position& position) {
    Logger& logger = Logger::instance();
    logger.debug("DocumentManager", "Hover request for " + uri + " at line " + std::to_string(position.line) + ", char " + std::to_string(position.character));
    
    auto text = getDocumentText(uri);
    if (!text.has_value()) {
        logger.debug("DocumentManager", "No document found for hover request");
        
        // Provide a basic hover response even without document text
        logger.info("DocumentManager", "Providing basic hover info without document text");
        std::string basicHover = "```cryo\n// Cryo Language Symbol\n// Document not loaded - showing basic info\n```\n\n**Position**: Line " + 
                                std::to_string(position.line) + ", Character " + std::to_string(position.character) +
                                "\n\n**File**: " + uri;
        
        Position start = {position.line, position.character};
        Position end = {position.line, position.character + 1};
        
        return HoverResult(basicHover, start, end);
    }
    
    // Try to get hover info from CryoAnalyzer first
    auto doc = getDocument(uri);
    if (doc && doc->languageId == "cryo") {
        std::string file_path = uriToFilePath(uri);
        
        CryoLSP::Position analyzer_pos;
        analyzer_pos.line = position.line;
        analyzer_pos.character = position.character;
        
        // Use retry mechanism for better reliability
        auto hover_info = analyzer_->getHoverInfoWithRetry(file_path, analyzer_pos, 2);
        if (hover_info) {
            // Clean, simple formatting showing just the signature
            std::string formatted_content = "```cryo\n" + hover_info->signature + "\n```";
            
            // Add documentation if available
            if (!hover_info->documentation.empty()) {
                formatted_content += "\n\n" + hover_info->documentation;
            }
            
            Position start, end;
            
            // Use specific range if provided by the analyzer (e.g., for literals)
            if (hover_info->start_pos.line != -1 && hover_info->end_pos.line != -1) {
                start = hover_info->start_pos;
                end = hover_info->end_pos;
                logger.debug("DocumentManager", "Using specific range from analyzer: {}:{} to {}:{}", 
                           start.line, start.character, end.line, end.character);
            } else {
                // Fall back to word boundaries for regular symbols
                start = findWordStart(text.value(), position);
                end = findWordEnd(text.value(), position);
                logger.debug("DocumentManager", "Using word boundary range: {}:{} to {}:{}", 
                           start.line, start.character, end.line, end.character);
            }
            
            logger.debug("DocumentManager", "Found enhanced symbol info: " + hover_info->name + " (" + hover_info->type + ") in scope: " + hover_info->scope);
            return HoverResult(formatted_content, start, end);
        }
    }
    
    // Fallback to simple word-based hover
    std::string word = getWordAtPosition(text.value(), position);
    if (word.empty()) {
        logger.debug("DocumentManager", "No word found at position");
        return std::nullopt;
    }
    
    logger.debug("DocumentManager", "Word at position: '" + word + "'");
    
    // Check if this is our debugging fallback word
    if (word.find("hover_fallback_") == 0) {
        std::string debugContent = "```cryo\n// Debug Hover Information\n```\n\n";
        debugContent += "**Position**: Line " + std::to_string(position.line) + ", Character " + std::to_string(position.character) + "\n\n";
        debugContent += "**Issue**: Line parsing failed - document has content but line splitting isn't working correctly.\n\n";
        debugContent += "**Word**: " + word;
        
        Position start = {position.line, position.character};
        Position end = {position.line, position.character + 1};
        
        return HoverResult(debugContent, start, end);
    }
    
    std::string hoverContent = getHoverContentForSymbol(word, uri);
    if (hoverContent.empty()) {
        return std::nullopt;
    }
    
    Position start = findWordStart(text.value(), position);
    Position end = findWordEnd(text.value(), position);
    
    return HoverResult(hoverContent, start, end);
}

std::string DocumentManager::getWordAtPosition(const std::string& text, const Position& position) {
    Logger& logger = Logger::instance();
    
    // Debug: Check first 100 characters to see line endings
    std::string sample = text.substr(0, std::min(100, static_cast<int>(text.length())));
    logger.debug("DocumentManager", "Text sample (first 100 chars): '" + sample + "'");
    
    // Debug: Show character codes for the first few characters
    std::string charCodes = "";
    for (size_t i = 0; i < std::min(20, static_cast<int>(text.length())); ++i) {
        charCodes += std::to_string((unsigned char)text[i]) + " ";
    }
    logger.debug("DocumentManager", "First 20 character codes: " + charCodes);
    
    // Count line endings to debug
    int cr_count = 0, lf_count = 0, crlf_count = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\r') {
            cr_count++;
            // Check if this is part of \r\n
            if (i + 1 < text.length() && text[i + 1] == '\n') {
                crlf_count++;
            }
        }
        if (c == '\n') {
            lf_count++;
        }
    }
    logger.debug("DocumentManager", "Line ending analysis: CR=" + std::to_string(cr_count) + 
                 ", LF=" + std::to_string(lf_count) + ", CRLF=" + std::to_string(crlf_count));
    
    // Split text into lines - handle both \r\n and \n line endings
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string currentLine;
    
    // Use getline which properly handles both \n and \r\n
    while (std::getline(stream, currentLine)) {
        lines.push_back(currentLine);
    }
    
    // If text ends with a newline, add empty line
    if (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        lines.push_back("");
    }
    
    logger.debug("DocumentManager", "Word detection: position " + std::to_string(position.line) + ":" + std::to_string(position.character) + 
                 ", total lines: " + std::to_string(lines.size()) + ", text length: " + std::to_string(text.length()));
    
    // Check bounds
    if (position.line < 0 || position.line >= static_cast<int>(lines.size())) {
        logger.debug("DocumentManager", "Line out of bounds: " + std::to_string(position.line) + " >= " + std::to_string(lines.size()));
        
        // For debugging: provide a basic response even when line is out of bounds
        logger.info("DocumentManager", "Providing fallback hover for line " + std::to_string(position.line));
        return "hover_fallback_line_" + std::to_string(position.line) + "_char_" + std::to_string(position.character);
    }
    
    const std::string& line = lines[position.line];
    if (position.character < 0 || position.character >= static_cast<int>(line.length())) {
        logger.debug("DocumentManager", "Character out of bounds: " + std::to_string(position.character) + " >= " + std::to_string(line.length()));
        return "";
    }
    
    logger.debug("DocumentManager", "Line content preview: '" + line.substr(0, std::min(50, static_cast<int>(line.length()))) + "'");
    
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
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\n') {
            lines.push_back(current_line);
            current_line.clear();
        } else if (c == '\r') {
            // Skip \r characters
            continue;
        } else {
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
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\n') {
            lines.push_back(current_line);
            current_line.clear();
        } else if (c == '\r') {
            // Skip \r characters
            continue;
        } else {
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
    if (symbol == "boolean") {
        return "**boolean** - Boolean type (true/false)\n\nExample: `let flag: bool = true;`";
    }
    if (symbol == "float") {
        return "**float** - 64-bit floating point type\n\nExample: `let pi: float = 3.14159;`";
    }
    if (symbol == "void") {
        return "**void** - No value type\n\nUsed for functions that don't return a value";
    }
    
    // Keywords
    if (symbol == "function") {
        return "**function** - Function declaration keyword\n\nExample: `function myFunction() -> int { return 42; }`";
    }
    if (symbol == "mut") {
        return "**mut** - Mutable variable declaration keyword\n\nExample: `mut x: int = 10;`";
    }
    if (symbol == "struct") {
        return "**struct** - Structure declaration keyword\n\nExample: `type struct Point { x: int, y: int }`";
    }
    if (symbol == "class") {
        return "**class** - Class declaration keyword\n\nExample: `type class MyClass { ... }`";
    }
    if (symbol == "type") {
        return "**type** - Type alias or definition keyword\n\nExample: `type MyInt = int;\n\ntype struct MyStruct { a: int, b: string };`";
    }
    if (symbol == "if") {
        return "**if** - Conditional statement keyword\n\nExample: `if (condition) { ... }`";
    }
    if (symbol == "return") {
        return "**return** - Return statement keyword\n\nExample: `return value;`";
    }
    if (symbol == "import") {
        return "**import** - Module import keyword\n\nExample: `import \"mymodule.cryo\";`";
    }
    if (symbol == "namespace") {
        return "**namespace** - Namespace declaration keyword\n\nExample: `namespace MyNamespace { ... }`";
    }
    if (symbol == "null") {
        return "**null** - Null literal\n\nRepresents a null or absent value.";
    }

    // Generic symbol information
    return "**" + symbol + "** - CryoLang symbol\n\n*Hover over built-in types and keywords for more information*";
}

std::string DocumentManager::uriToFilePath(const std::string& uri) {
    // Convert file:// URI to local file path
    if (uri.substr(0, 7) == "file://") {
        std::string path = uri.substr(7);
        
        // URL decode
        std::string decoded;
        for (size_t i = 0; i < path.length(); ++i) {
            if (path[i] == '%' && i + 2 < path.length()) {
                // Convert hex to char
                int hex = std::stoi(path.substr(i + 1, 2), nullptr, 16);
                decoded += static_cast<char>(hex);
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        
        // Convert forward slashes to backslashes on Windows
        #ifdef _WIN32
        std::replace(decoded.begin(), decoded.end(), '/', '\\');
        #endif
        
        return decoded;
    }
    
    return uri; // Fallback
}

// ========================================
// Diagnostic Support
// ========================================

std::vector<CryoLSP::LSPDiagnostic> DocumentManager::getDiagnostics(const std::string& uri) {
    Logger& logger = Logger::instance();
    logger.debug("DocumentManager", "Getting diagnostics for: " + uri);
    
    std::string file_path = uriToFilePath(uri);
    return analyzer_->getDiagnostics(file_path);
}

void DocumentManager::clearDiagnostics(const std::string& uri) {
    Logger& logger = Logger::instance();
    logger.debug("DocumentManager", "Clearing diagnostics for: " + uri);
    
    std::string file_path = uriToFilePath(uri);
    analyzer_->clearDiagnostics(file_path);
}

} // namespace LSP
} // namespace Cryo