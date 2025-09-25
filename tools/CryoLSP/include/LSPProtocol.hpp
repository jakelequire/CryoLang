#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <variant>
#include <cstdint>

namespace Cryo::LSP
{
    // ================================================================
    // LSP Protocol Structures
    // ================================================================

    struct Position
    {
        uint32_t line = 0;       // 0-based line number
        uint32_t character = 0;  // 0-based character offset

        Position() = default;
        Position(uint32_t line, uint32_t character) : line(line), character(character) {}
    };

    struct Range
    {
        Position start;
        Position end;

        Range() = default;
        Range(Position start, Position end) : start(start), end(end) {}
        Range(uint32_t startLine, uint32_t startChar, uint32_t endLine, uint32_t endChar)
            : start(startLine, startChar), end(endLine, endChar) {}
    };

    struct Location
    {
        std::string uri;
        Range range;
    };

    struct TextDocumentIdentifier
    {
        std::string uri;
    };

    struct VersionedTextDocumentIdentifier : TextDocumentIdentifier
    {
        std::optional<int> version;
    };

    struct TextDocumentItem
    {
        std::string uri;
        std::string language_id;
        int version;
        std::string text;
    };

    struct TextDocumentPositionParams
    {
        TextDocumentIdentifier text_document;
        Position position;
    };

    // ================================================================
    // Diagnostic Structures
    // ================================================================

    enum class DiagnosticSeverity
    {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    struct Diagnostic
    {
        Range range;
        std::optional<DiagnosticSeverity> severity;
        std::optional<std::string> code;
        std::optional<std::string> source;
        std::string message;
        std::vector<std::string> related_information;

        Diagnostic() = default;
        Diagnostic(Range range, const std::string& message, DiagnosticSeverity severity = DiagnosticSeverity::Error)
            : range(range), message(message), severity(severity) {}
    };

    struct PublishDiagnosticsParams
    {
        std::string uri;
        std::vector<Diagnostic> diagnostics;
    };

    // ================================================================
    // Completion Structures
    // ================================================================

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
        TypeParameter = 25
    };

    struct CompletionItem
    {
        std::string label;
        std::optional<CompletionItemKind> kind;
        std::optional<std::string> detail;
        std::optional<std::string> documentation;
        std::optional<std::string> sort_text;
        std::optional<std::string> filter_text;
        std::optional<std::string> insert_text;
    };

    struct CompletionList
    {
        bool is_incomplete = false;
        std::vector<CompletionItem> items;
    };

    // ================================================================
    // Hover Structures
    // ================================================================

    struct MarkupContent
    {
        std::string kind;  // "plaintext" or "markdown"
        std::string value;
    };

    struct Hover
    {
        MarkupContent contents;
        std::optional<Range> range;
    };

    // ================================================================
    // Server Capabilities
    // ================================================================

    struct CompletionOptions
    {
        std::optional<bool> resolve_provider;
        std::vector<std::string> trigger_characters;
    };

    struct TextDocumentSyncOptions
    {
        std::optional<bool> open_close;
        std::optional<int> change;  // 0=None, 1=Full, 2=Incremental
        std::optional<bool> will_save;
        std::optional<bool> will_save_wait_until;
        std::optional<bool> save;
    };

    struct ServerCapabilities
    {
        std::optional<TextDocumentSyncOptions> text_document_sync;
        std::optional<bool> hover_provider;
        std::optional<CompletionOptions> completion_provider;
        std::optional<bool> definition_provider;
        std::optional<bool> references_provider;
        std::optional<bool> document_highlight_provider;
        std::optional<bool> document_symbol_provider;
        std::optional<bool> workspace_symbol_provider;
        std::optional<bool> code_action_provider;
        std::optional<bool> code_lens_provider;
        std::optional<bool> document_formatting_provider;
        std::optional<bool> document_range_formatting_provider;
        std::optional<bool> rename_provider;
        std::optional<bool> folding_range_provider;
        std::optional<bool> selection_range_provider;
    };

    struct InitializeResult
    {
        ServerCapabilities capabilities;
    };

    // ================================================================
    // LSP Message Types
    // ================================================================

    enum class MessageType
    {
        Request,
        Response,
        Notification
    };

    struct LSPMessage
    {
        MessageType type;
        std::optional<std::string> id;  // Only for requests/responses
        std::string method;
        std::optional<std::string> params_json;  // Raw JSON parameters
        std::optional<std::string> result_json;  // Raw JSON result (for responses)
        std::optional<std::string> error_message; // Error message (for error responses)
    };

} // namespace Cryo::LSP
