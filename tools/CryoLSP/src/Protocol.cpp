#include "LSP/Protocol.hpp"
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace CryoLSP
{

    // ============================================================================
    // From JSON
    // ============================================================================

    Position position_from_json(const cjson::JsonValue &json)
    {
        Position pos;
        if (json.is_object())
        {
            pos.line = json["line"].get_int();
            pos.character = json["character"].get_int();
        }
        return pos;
    }

    Range range_from_json(const cjson::JsonValue &json)
    {
        Range range;
        if (json.is_object())
        {
            range.start = position_from_json(json["start"]);
            range.end = position_from_json(json["end"]);
        }
        return range;
    }

    TextDocumentIdentifier text_document_id_from_json(const cjson::JsonValue &json)
    {
        TextDocumentIdentifier id;
        if (json.is_object())
        {
            id.uri = json["uri"].get_string();
        }
        return id;
    }

    TextDocumentItem text_document_item_from_json(const cjson::JsonValue &json)
    {
        TextDocumentItem item;
        if (json.is_object())
        {
            item.uri = json["uri"].get_string();
            item.languageId = json["languageId"].get_string();
            item.version = json["version"].get_int();
            item.text = json["text"].get_string();
        }
        return item;
    }

    TextDocumentPositionParams text_document_position_from_json(const cjson::JsonValue &json)
    {
        TextDocumentPositionParams params;
        if (json.is_object())
        {
            params.textDocument = text_document_id_from_json(json["textDocument"]);
            params.position = position_from_json(json["position"]);
        }
        return params;
    }

    // ============================================================================
    // To JSON
    // ============================================================================

    cjson::JsonValue position_to_json(const Position &pos)
    {
        cjson::JsonObject obj;
        obj["line"] = cjson::JsonValue(pos.line);
        obj["character"] = cjson::JsonValue(pos.character);
        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue range_to_json(const Range &range)
    {
        cjson::JsonObject obj;
        obj["start"] = position_to_json(range.start);
        obj["end"] = position_to_json(range.end);
        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue location_to_json(const Location &loc)
    {
        cjson::JsonObject obj;
        obj["uri"] = cjson::JsonValue(loc.uri);
        obj["range"] = range_to_json(loc.range);
        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue diagnostic_to_json(const Diagnostic &diag)
    {
        cjson::JsonObject obj;
        obj["range"] = range_to_json(diag.range);
        obj["severity"] = cjson::JsonValue(static_cast<int>(diag.severity));
        if (!diag.code.empty())
            obj["code"] = cjson::JsonValue(diag.code);
        obj["source"] = cjson::JsonValue(diag.source);
        obj["message"] = cjson::JsonValue(diag.message);

        if (!diag.relatedInformation.empty())
        {
            cjson::JsonArray related;
            for (const auto &rel : diag.relatedInformation)
            {
                cjson::JsonObject r;
                r["location"] = location_to_json(rel.location);
                r["message"] = cjson::JsonValue(rel.message);
                related.push_back(cjson::JsonValue(std::move(r)));
            }
            obj["relatedInformation"] = cjson::JsonValue(std::move(related));
        }

        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue hover_to_json(const Hover &hover)
    {
        cjson::JsonObject obj;

        cjson::JsonObject contents;
        contents["kind"] = cjson::JsonValue(hover.contents.kind);
        contents["value"] = cjson::JsonValue(hover.contents.value);
        obj["contents"] = cjson::JsonValue(std::move(contents));

        if (hover.range.has_value())
        {
            obj["range"] = range_to_json(hover.range.value());
        }

        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue completion_item_to_json(const CompletionItem &item)
    {
        cjson::JsonObject obj;
        obj["label"] = cjson::JsonValue(item.label);
        obj["kind"] = cjson::JsonValue(static_cast<int>(item.kind));
        if (!item.detail.empty())
            obj["detail"] = cjson::JsonValue(item.detail);
        if (!item.documentation.empty())
        {
            cjson::JsonObject doc;
            doc["kind"] = cjson::JsonValue("markdown");
            doc["value"] = cjson::JsonValue(item.documentation);
            obj["documentation"] = cjson::JsonValue(std::move(doc));
        }
        if (!item.insertText.empty())
            obj["insertText"] = cjson::JsonValue(item.insertText);
        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue completion_list_to_json(const CompletionList &list)
    {
        cjson::JsonObject obj;
        obj["isIncomplete"] = cjson::JsonValue(list.isIncomplete);

        cjson::JsonArray items;
        for (const auto &item : list.items)
        {
            items.push_back(completion_item_to_json(item));
        }
        obj["items"] = cjson::JsonValue(std::move(items));

        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue document_symbol_to_json(const DocumentSymbol &sym)
    {
        cjson::JsonObject obj;
        obj["name"] = cjson::JsonValue(sym.name);
        if (!sym.detail.empty())
            obj["detail"] = cjson::JsonValue(sym.detail);
        obj["kind"] = cjson::JsonValue(static_cast<int>(sym.kind));
        obj["range"] = range_to_json(sym.range);
        obj["selectionRange"] = range_to_json(sym.selectionRange);

        if (!sym.children.empty())
        {
            cjson::JsonArray children;
            for (const auto &child : sym.children)
            {
                children.push_back(document_symbol_to_json(child));
            }
            obj["children"] = cjson::JsonValue(std::move(children));
        }

        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue signature_help_to_json(const SignatureHelp &help)
    {
        cjson::JsonObject obj;
        obj["activeSignature"] = cjson::JsonValue(help.activeSignature);
        obj["activeParameter"] = cjson::JsonValue(help.activeParameter);

        cjson::JsonArray sigs;
        for (const auto &sig : help.signatures)
        {
            cjson::JsonObject s;
            s["label"] = cjson::JsonValue(sig.label);
            if (!sig.documentation.empty())
            {
                cjson::JsonObject doc;
                doc["kind"] = cjson::JsonValue("markdown");
                doc["value"] = cjson::JsonValue(sig.documentation);
                s["documentation"] = cjson::JsonValue(std::move(doc));
            }

            cjson::JsonArray params;
            for (const auto &p : sig.parameters)
            {
                cjson::JsonObject param;
                param["label"] = cjson::JsonValue(p.label);
                if (!p.documentation.empty())
                    param["documentation"] = cjson::JsonValue(p.documentation);
                params.push_back(cjson::JsonValue(std::move(param)));
            }
            s["parameters"] = cjson::JsonValue(std::move(params));

            sigs.push_back(cjson::JsonValue(std::move(s)));
        }
        obj["signatures"] = cjson::JsonValue(std::move(sigs));

        return cjson::JsonValue(std::move(obj));
    }

    cjson::JsonValue semantic_tokens_to_json(const SemanticTokens &tokens)
    {
        cjson::JsonObject obj;
        cjson::JsonArray data;
        for (int val : tokens.data)
        {
            data.push_back(cjson::JsonValue(val));
        }
        obj["data"] = cjson::JsonValue(std::move(data));
        return cjson::JsonValue(std::move(obj));
    }

    // ============================================================================
    // URI utilities
    // ============================================================================

    std::string uri_to_path(const std::string &uri)
    {
        std::string path = uri;

        // Remove file:// or file:/// prefix
        if (path.substr(0, 8) == "file:///")
        {
#ifdef _WIN32
            // On Windows: file:///C:/path -> C:/path
            path = path.substr(8);
#else
            // On Linux: file:///path -> /path
            path = path.substr(7);
#endif
        }
        else if (path.substr(0, 7) == "file://")
        {
            path = path.substr(7);
        }

        // Decode percent-encoded characters
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (path[i] == '%' && i + 2 < path.size())
            {
                std::string hex = path.substr(i + 1, 2);
                char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
                decoded += ch;
                i += 2;
            }
            else
            {
                decoded += path[i];
            }
        }

        // Normalize path separators on Windows
#ifdef _WIN32
        std::replace(decoded.begin(), decoded.end(), '/', '\\');
#endif

        return decoded;
    }

    std::string path_to_uri(const std::string &path)
    {
        std::string normalized = path;

        // Normalize to forward slashes
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        // Percent-encode special characters (simplified: spaces and common chars)
        std::string encoded;
        for (char c : normalized)
        {
            if (c == ' ')
                encoded += "%20";
            else if (c == '#')
                encoded += "%23";
            else if (c == '?')
                encoded += "%3F";
            else
                encoded += c;
        }

#ifdef _WIN32
        // Windows: C:/path -> file:///C:/path
        return "file:///" + encoded;
#else
        // Linux: /path -> file:///path
        return "file://" + encoded;
#endif
    }

} // namespace CryoLSP
