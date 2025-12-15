#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>

// Include specific cjson headers to ensure class definitions are available
#include <cjson/json.hpp>
#include <cjson/parser.hpp>
#include <cjson/writer.hpp>
#include <cjson/utils.hpp>

namespace CryoLSP
{

    using json = cjson::JsonValue;
    // Import cjson types into our namespace for easier use
    using cjson::JsonArray;
    using cjson::JsonObject;
    using cjson::JsonValue;

    // ================================================================
    // Core LSP Types
    // ================================================================

    struct Position
    {
        int line = 0;      // 0-based
        int character = 0; // 0-based

        json to_json() const
        {
            return json::object({{"line", json(line)}, {"character", json(character)}});
        }

        static Position from_json(const json &j)
        {
            return {j["line"].get_int(), j["character"].get_int()};
        }
    };

    struct Range
    {
        Position start;
        Position end;

        json to_json() const
        {
            return json::object({{"start", start.to_json()}, {"end", end.to_json()}});
        }

        static Range from_json(const json &j)
        {
            return {Position::from_json(j["start"]), Position::from_json(j["end"])};
        }
    };

    struct Location
    {
        std::string uri;
        Range range;

        json to_json() const
        {
            return json::object({{"uri", json(uri)}, {"range", range.to_json()}});
        }
    };

    struct TextDocumentIdentifier
    {
        std::string uri;

        static TextDocumentIdentifier from_json(const json &j)
        {
            return {j["uri"].get_string()};
        }
    };

    struct VersionedTextDocumentIdentifier : TextDocumentIdentifier
    {
        int version;

        static VersionedTextDocumentIdentifier from_json(const json &j)
        {
            VersionedTextDocumentIdentifier result;
            result.uri = j["uri"].get_string();
            result.version = j["version"].get_int();
            return result;
        }
    };

    struct TextDocumentItem
    {
        std::string uri;
        std::string language_id;
        int version;
        std::string text;

        static TextDocumentItem from_json(const json &j)
        {
            TextDocumentItem result;
            result.uri = j["uri"].get_string();
            result.language_id = j["languageId"].get_string();
            result.version = j["version"].get_int();
            result.text = j["text"].get_string();
            return result;
        }
    };

    struct TextDocumentContentChangeEvent
    {
        std::optional<Range> range;
        std::optional<int> range_length;
        std::string text;

        static TextDocumentContentChangeEvent from_json(const json &j)
        {
            TextDocumentContentChangeEvent event;
            event.text = j["text"].get_string();
            // Check if object has range field (simplified check)
            if (j.is_object())
            {
                try
                {
                    auto range_val = j["range"];
                    if (!range_val.is_null())
                    {
                        event.range = Range::from_json(range_val);
                    }
                }
                catch (...)
                {
                    // Range not present, that's OK
                }
                try
                {
                    auto range_len = j["rangeLength"];
                    if (!range_len.is_null())
                    {
                        event.range_length = range_len.get_int();
                    }
                }
                catch (...)
                {
                    // Range length not present, that's OK
                }
            }
            return event;
        }
    };

    struct TextDocumentPositionParams
    {
        TextDocumentIdentifier text_document;
        Position position;

        static TextDocumentPositionParams from_json(const json &j)
        {
            return {TextDocumentIdentifier::from_json(j.as_object().at("textDocument")),
                    Position::from_json(j.as_object().at("position"))};
        }
    };

    // ================================================================
    // Diagnostic Types
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
        DiagnosticSeverity severity;
        std::optional<std::string> code;
        std::optional<std::string> code_description;
        std::string source;
        std::string message;
        std::vector<json> related_information; // DiagnosticRelatedInformation
        std::vector<json> tags;                // DiagnosticTag

        json to_json() const
        {
            auto obj = JsonValue::object({{"range", range.to_json()},
                                          {"severity", static_cast<int>(severity)},
                                          {"source", JsonValue(source)},
                                          {"message", JsonValue(message)}});

            if (code)
            {
                obj.as_object()["code"] = JsonValue(*code);
            }
            if (code_description)
            {
                obj.as_object()["codeDescription"] = JsonValue(*code_description);
            }
            if (!related_information.empty())
            {
                auto arr = JsonValue::array();
                for (const auto &item : related_information)
                {
                    arr.as_array().push_back(item);
                }
                obj.as_object()["relatedInformation"] = arr;
            }
            if (!tags.empty())
            {
                auto arr = JsonValue::array();
                for (const auto &tag : tags)
                {
                    arr.as_array().push_back(tag);
                }
                obj.as_object()["tags"] = arr;
            }
            return obj;
        }
    };

    // ================================================================
    // Completion Types
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
        CompletionItemKind kind;
        std::optional<std::string> detail;
        std::optional<std::string> documentation;
        std::optional<std::string> sort_text;
        std::optional<std::string> filter_text;
        std::optional<std::string> insert_text;
        std::optional<json> text_edit;

        json to_json() const
        {
            auto obj = JsonValue::object({{"label", JsonValue(label)},
                                          {"kind", JsonValue(static_cast<int>(kind))}});

            if (detail)
            {
                obj.as_object()["detail"] = JsonValue(*detail);
            }
            if (documentation)
            {
                obj.as_object()["documentation"] = JsonValue(*documentation);
            }
            if (sort_text)
            {
                obj.as_object()["sortText"] = JsonValue(*sort_text);
            }
            if (filter_text)
            {
                obj.as_object()["filterText"] = JsonValue(*filter_text);
            }
            if (insert_text)
            {
                obj.as_object()["insertText"] = JsonValue(*insert_text);
            }
            if (text_edit)
            {
                obj.as_object()["textEdit"] = *text_edit;
            }
            return obj;
        }
    };

    struct CompletionList
    {
        bool is_incomplete;
        std::vector<CompletionItem> items;

        json to_json() const
        {
            auto items_json = JsonValue::array();
            for (const auto &item : items)
            {
                items_json.as_array().push_back(item.to_json());
            }
            return JsonValue::object({{"isIncomplete", JsonValue(is_incomplete)},
                                      {"items", items_json}});
        }
    };

    // ================================================================
    // Hover Types
    // ================================================================

    struct MarkupContent
    {
        std::string kind; // "plaintext" or "markdown"
        std::string value;

        json to_json() const
        {
            return JsonValue::object({{"kind", JsonValue(kind)},
                                      {"value", JsonValue(value)}});
        }
    };

    struct Hover
    {
        MarkupContent contents;
        std::optional<Range> range;

        json to_json() const
        {
            auto obj = JsonValue::object({{"contents", contents.to_json()}});
            if (range)
            {
                obj.as_object()["range"] = range->to_json();
            }
            return obj;
        }
    };

    // ================================================================
    // Symbol Types
    // ================================================================

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
        TypeParameter = 26
    };

    struct DocumentSymbol
    {
        std::string name;
        std::optional<std::string> detail;
        SymbolKind kind;
        std::optional<bool> deprecated;
        Range range;
        Range selection_range;
        std::vector<DocumentSymbol> children;

        json to_json() const
        {
            json result = json::object({{"name", json(name)},
                                        {"kind", json(static_cast<int>(kind))},
                                        {"range", range.to_json()},
                                        {"selectionRange", selection_range.to_json()}});

            if (detail.has_value())
            {
                result["detail"] = json(detail.value());
            }

            if (deprecated.has_value())
            {
                result["deprecated"] = json(deprecated.value());
            }

            if (!children.empty())
            {
                json children_array = json::array();
                for (const auto &child : children)
                {
                    children_array.as_array().push_back(child.to_json());
                }
                result["children"] = children_array;
            }

            return result;
        }
    };

    // ================================================================
    // Message Types
    // ================================================================

    struct LSPMessage
    {
        std::string jsonrpc = "2.0";
        std::optional<json> id;
        std::optional<std::string> method;
        std::optional<json> params;
        std::optional<json> result;
        std::optional<json> error;

        bool is_request() const { return method.has_value() && id.has_value(); }
        bool is_notification() const { return method.has_value() && !id.has_value(); }
        bool is_response() const { return !method.has_value(); }
    };

    struct LSPRequest
    {
        std::string jsonrpc = "2.0";
        json id;
        std::string method;
        json params;

        static LSPRequest from_message(const LSPMessage &msg)
        {
            return {msg.jsonrpc, *msg.id, *msg.method, msg.params.value_or(JsonValue::object())};
        }
    };

    struct LSPResponse
    {
        std::string jsonrpc = "2.0";
        json id;
        std::optional<json> result;
        std::optional<json> error;

        json to_json() const
        {
            auto obj = JsonValue::object({{"jsonrpc", JsonValue(jsonrpc)},
                                          {"id", id}});
            if (result)
            {
                obj.as_object()["result"] = *result;
            }
            if (error)
            {
                obj.as_object()["error"] = *error;
            }
            return obj;
        }
    };

    struct LSPNotification
    {
        std::string jsonrpc = "2.0";
        std::string method;
        json params;

        json to_json() const
        {
            return JsonValue::object({{"jsonrpc", JsonValue(jsonrpc)},
                                      {"method", JsonValue(method)},
                                      {"params", params}});
        }
    };

    // ================================================================
    // Server Capabilities
    // ================================================================

    struct ServerCapabilities
    {
        json to_json() const
        {
            // Create trigger characters array
            auto trigger_chars = JsonValue::array();
            auto &arr = trigger_chars.as_array();
            arr.push_back(JsonValue("."));
            arr.push_back(JsonValue("::"));
            arr.push_back(JsonValue("<"));
            arr.push_back(JsonValue("("));
            arr.push_back(JsonValue("["));
            arr.push_back(JsonValue(","));
            arr.push_back(JsonValue(" "));

            return JsonValue::object({{"textDocumentSync", JsonValue::object({{"openClose", JsonValue(true)},
                                                                              {"change", JsonValue(2)}, // Incremental
                                                                              {"save", JsonValue::object({{"includeText", JsonValue(false)}})}})},
                                      {"hoverProvider", JsonValue(true)},
                                      {"completionProvider", JsonValue::object({{"resolveProvider", JsonValue(false)},
                                                                                {"triggerCharacters", trigger_chars}})},
                                      {"definitionProvider", JsonValue(true)},
                                      {"referencesProvider", JsonValue(true)},
                                      {"documentSymbolProvider", JsonValue(true)},
                                      {"workspaceSymbolProvider", JsonValue(true)},
                                      {"codeActionProvider", JsonValue(false)},
                                      {"codeLensProvider", JsonValue(false)},
                                      {"documentFormattingProvider", JsonValue(false)},
                                      {"documentRangeFormattingProvider", JsonValue(false)},
                                      {"renameProvider", JsonValue(false)},
                                      {"foldingRangeProvider", JsonValue(false)},
                                      {"selectionRangeProvider", JsonValue(false)}});
        }
    };

} // namespace CryoLSP
