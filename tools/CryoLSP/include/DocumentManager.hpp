#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include "LSPMessage.hpp"

namespace Cryo {
namespace LSP {

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version;
    std::string text;
};

struct HoverResult {
    std::string contents;
    Position start;
    Position end;
    
    HoverResult(const std::string& content) : contents(content) {}
    HoverResult(const std::string& content, Position s, Position e) 
        : contents(content), start(s), end(e) {}
};

class DocumentManager {
private:
    std::unordered_map<std::string, TextDocumentItem> documents_;

public:
    // Document lifecycle
    void didOpen(const std::string& uri, const std::string& languageId, int version, const std::string& text);
    void didChange(const std::string& uri, int version, const std::string& text);
    void didClose(const std::string& uri);
    
    // Document access
    std::optional<TextDocumentItem> getDocument(const std::string& uri) const;
    std::optional<std::string> getDocumentText(const std::string& uri) const;
    
    // Hover functionality
    std::optional<HoverResult> getHoverInfo(const std::string& uri, const Position& position);
    
private:
    // Helper methods
    std::string getWordAtPosition(const std::string& text, const Position& position);
    Position findWordStart(const std::string& text, const Position& position);
    Position findWordEnd(const std::string& text, const Position& position);
    std::string getHoverContentForSymbol(const std::string& symbol, const std::string& uri);
};

} // namespace LSP
} // namespace Cryo