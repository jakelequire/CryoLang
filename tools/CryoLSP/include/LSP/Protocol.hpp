#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cjson/json.hpp>

namespace CryoLSP
{

    // ============================================================================
    // LSP Protocol Types
    // ============================================================================

    struct Position
    {
        int line = 0;   // 0-based
        int character = 0; // 0-based
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

    struct TextEdit
    {
        Range range;
        std::string newText;
    };

    // ============================================================================
    // Diagnostic Types
    // ============================================================================

    enum class DiagnosticSeverity
    {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4,
    };

    struct DiagnosticRelatedInformation
    {
        Location location;
        std::string message;
    };

    struct Diagnostic
    {
        Range range;
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        std::string code;
        std::string source = "cryo";
        std::string message;
        std::vector<DiagnosticRelatedInformation> relatedInformation;
    };

    // ============================================================================
    // Hover Types
    // ============================================================================

    struct MarkupContent
    {
        std::string kind = "markdown"; // "plaintext" or "markdown"
        std::string value;
    };

    struct Hover
    {
        MarkupContent contents;
        std::optional<Range> range;
    };

    // ============================================================================
    // Completion Types
    // ============================================================================

    enum class CompletionItemKind
    {
        Text = 1,
        Method = 2,
        Function = 3,
        Constructor = 4,
        Field = 5,
        Variable = 6,
        Class = 7,
        Interface = 8,
        Module = 9,
        Property = 10,
        Unit = 11,
        Value = 12,
        Enum = 13,
        Keyword = 14,
        Snippet = 15,
        Color = 16,
        File = 17,
        Reference = 18,
        Folder = 19,
        EnumMember = 20,
        Constant = 21,
        Struct = 22,
        Event = 23,
        Operator = 24,
        TypeParameter = 25,
    };

    struct CompletionItem
    {
        std::string label;
        CompletionItemKind kind = CompletionItemKind::Text;
        std::string detail;
        std::string documentation;
        std::string insertText;
    };

    struct CompletionList
    {
        bool isIncomplete = false;
        std::vector<CompletionItem> items;
    };

    // ============================================================================
    // Document Symbol Types
    // ============================================================================

    enum class SymbolKind
    {
        File = 1,
        Module = 2,
        Namespace = 3,
        Package = 4,
        Class = 5,
        Method = 6,
        Property = 7,
        Field = 8,
        Constructor = 9,
        Enum = 10,
        Interface = 11,
        Function = 12,
        Variable = 13,
        Constant = 14,
        String = 15,
        Number = 16,
        Boolean = 17,
        Array = 18,
        Object = 19,
        Key = 20,
        Null = 21,
        EnumMember = 22,
        Struct = 23,
        Event = 24,
        Operator = 25,
        TypeParameter = 26,
    };

    struct DocumentSymbol
    {
        std::string name;
        std::string detail;
        SymbolKind kind = SymbolKind::Variable;
        Range range;
        Range selectionRange;
        std::vector<DocumentSymbol> children;
    };

    // ============================================================================
    // Signature Help Types
    // ============================================================================

    struct ParameterInformation
    {
        std::string label;
        std::string documentation;
    };

    struct SignatureInformation
    {
        std::string label;
        std::string documentation;
        std::vector<ParameterInformation> parameters;
    };

    struct SignatureHelp
    {
        std::vector<SignatureInformation> signatures;
        int activeSignature = 0;
        int activeParameter = 0;
    };

    // ============================================================================
    // Semantic Tokens Types
    // ============================================================================

    struct SemanticTokensLegend
    {
        std::vector<std::string> tokenTypes;
        std::vector<std::string> tokenModifiers;
    };

    struct SemanticTokens
    {
        std::vector<int> data; // delta-encoded
    };

    // ============================================================================
    // Text Document Types
    // ============================================================================

    struct TextDocumentIdentifier
    {
        std::string uri;
    };

    struct TextDocumentItem
    {
        std::string uri;
        std::string languageId;
        int version = 0;
        std::string text;
    };

    struct VersionedTextDocumentIdentifier
    {
        std::string uri;
        int version = 0;
    };

    struct TextDocumentContentChangeEvent
    {
        std::string text; // Full content (we use full sync)
    };

    struct TextDocumentPositionParams
    {
        TextDocumentIdentifier textDocument;
        Position position;
    };

    // ============================================================================
    // JSON Conversion Functions
    // ============================================================================

    // From JSON
    Position position_from_json(const cjson::JsonValue &json);
    Range range_from_json(const cjson::JsonValue &json);
    TextDocumentIdentifier text_document_id_from_json(const cjson::JsonValue &json);
    TextDocumentItem text_document_item_from_json(const cjson::JsonValue &json);
    TextDocumentPositionParams text_document_position_from_json(const cjson::JsonValue &json);

    // To JSON
    cjson::JsonValue position_to_json(const Position &pos);
    cjson::JsonValue range_to_json(const Range &range);
    cjson::JsonValue location_to_json(const Location &loc);
    cjson::JsonValue diagnostic_to_json(const Diagnostic &diag);
    cjson::JsonValue hover_to_json(const Hover &hover);
    cjson::JsonValue completion_item_to_json(const CompletionItem &item);
    cjson::JsonValue completion_list_to_json(const CompletionList &list);
    cjson::JsonValue document_symbol_to_json(const DocumentSymbol &sym);
    cjson::JsonValue signature_help_to_json(const SignatureHelp &help);
    cjson::JsonValue semantic_tokens_to_json(const SemanticTokens &tokens);

    // Utility
    std::string uri_to_path(const std::string &uri);
    std::string path_to_uri(const std::string &path);

} // namespace CryoLSP
