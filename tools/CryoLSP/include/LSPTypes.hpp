#pragma once

#include <string>
#include <vector>

namespace CryoLSP
{

    struct Position
    {
        int line;      // 0-based
        int character; // 0-based
    };

    struct Range
    {
        Position start;
        Position end;
    };

    struct Location
    {
        std::string uri;
        Range range;
    };

    struct CompletionItem
    {
        std::string label;
        int kind; // LSP CompletionItemKind
        std::string detail;
        std::string documentation;
        std::string insertText;
    };

    struct SymbolInformation
    {
        std::string name;
        int kind; // LSP SymbolKind
        Location location;
        std::string containerName;
    };

    struct Hover
    {
        std::string contents;
        Range range;
    };

} // namespace CryoLSP