#pragma once

#include <string>
#include <optional>
#include <variant>
#include <vector>
#include "JsonParser.hpp"

namespace Cryo {
namespace LSP {

// JSON-RPC Message Types
enum class MessageType {
    Request,
    Response,
    Notification
};

// LSP Message Base
struct LSPMessage {
    std::string jsonrpc = "2.0";
    
    virtual ~LSPMessage() = default;
    virtual JsonValue toJson() const = 0;
    virtual MessageType getType() const = 0;
};

// JSON-RPC Request
struct Request : public LSPMessage {
    std::string id;
    std::string method;
    JsonValue params;
    
    Request() = default;
    Request(const std::string& id, const std::string& method, const JsonValue& params = JsonValue{})
        : id(id), method(method), params(params) {}
    
    MessageType getType() const override { return MessageType::Request; }
    
    JsonValue toJson() const override {
        JsonObject obj;
        obj["jsonrpc"] = JsonValue(jsonrpc);
        obj["id"] = JsonValue(id);
        obj["method"] = JsonValue(method);
        if (!params.isNull()) {
            obj["params"] = params;
        }
        return JsonValue(obj);
    }
    
    static std::optional<Request> fromJson(const JsonValue& json);
};

// JSON-RPC Response
struct Response : public LSPMessage {
    std::string id;
    std::optional<JsonValue> result;
    std::optional<JsonValue> error;
    
    Response() = default;
    Response(const std::string& id, const JsonValue& result)
        : id(id), result(result) {}
    Response(const std::string& id, const JsonValue& error, bool)
        : id(id), error(error) {}
    
    MessageType getType() const override { return MessageType::Response; }
    
    JsonValue toJson() const override {
        JsonObject obj;
        obj["jsonrpc"] = JsonValue(jsonrpc);
        obj["id"] = JsonValue(id);
        
        if (result.has_value()) {
            obj["result"] = result.value();
        }
        if (error.has_value()) {
            obj["error"] = error.value();
        }
        
        return JsonValue(obj);
    }
    
    static std::optional<Response> fromJson(const JsonValue& json) {
        if (!json.isObject()) return std::nullopt;
        
        auto obj = json.asObject();
        if (!obj.count("jsonrpc") || !obj.count("id")) {
            return std::nullopt;
        }
        
        Response resp;
        resp.jsonrpc = obj.at("jsonrpc").asString();
        resp.id = obj.at("id").asString();
        
        if (obj.count("result")) {
            resp.result = obj.at("result");
        }
        if (obj.count("error")) {
            resp.error = obj.at("error");
        }
        
        return resp;
    }
};

// JSON-RPC Notification
struct Notification : public LSPMessage {
    std::string method;
    JsonValue params;
    
    Notification() = default;
    Notification(const std::string& method, const JsonValue& params = JsonValue{})
        : method(method), params(params) {}
    
    MessageType getType() const override { return MessageType::Notification; }
    
    JsonValue toJson() const override {
        JsonObject obj;
        obj["jsonrpc"] = JsonValue(jsonrpc);
        obj["method"] = JsonValue(method);
        if (!params.isNull()) {
            obj["params"] = params;
        }
        return JsonValue(obj);
    }
    
    static std::optional<Notification> fromJson(const JsonValue& json);
};

// LSP Error Codes
enum class LSPErrorCode {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
    RequestCancelled = -32800,
    ContentModified = -32801
};

// LSP Error
struct LSPError {
    LSPErrorCode code;
    std::string message;
    std::optional<JsonValue> data;
    
    LSPError(LSPErrorCode code, const std::string& message, const std::optional<JsonValue>& data = std::nullopt)
        : code(code), message(message), data(data) {}
    
    JsonValue toJson() const {
        JsonObject obj;
        obj["code"] = JsonValue(static_cast<int>(code));
        obj["message"] = JsonValue(message);
        if (data.has_value()) {
            obj["data"] = data.value();
        }
        return JsonValue(obj);
    }
};

// Common LSP Types
struct Position {
    int line;
    int character;
    
    Position(int line = 0, int character = 0) : line(line), character(character) {}
    
    JsonValue toJson() const {
        JsonObject obj;
        obj["line"] = JsonValue(line);
        obj["character"] = JsonValue(character);
        return JsonValue(obj);
    }
    
    static std::optional<Position> fromJson(const JsonValue& json) {
        if (!json.isObject()) return std::nullopt;
        
        auto obj = json.asObject();
        if (!obj.count("line") || !obj.count("character")) {
            return std::nullopt;
        }
        
        return Position(
            static_cast<int>(obj.at("line").asNumber()),
            static_cast<int>(obj.at("character").asNumber())
        );
    }
};

struct Range {
    Position start;
    Position end;
    
    Range() = default;
    Range(const Position& start, const Position& end) : start(start), end(end) {}
    
    JsonValue toJson() const {
        JsonObject obj;
        obj["start"] = start.toJson();
        obj["end"] = end.toJson();
        return JsonValue(obj);
    }
    
    static std::optional<Range> fromJson(const JsonValue& json) {
        if (!json.isObject()) return std::nullopt;
        
        auto obj = json.asObject();
        if (!obj.count("start") || !obj.count("end")) {
            return std::nullopt;
        }
        
        auto start = Position::fromJson(obj.at("start"));
        auto end = Position::fromJson(obj.at("end"));
        
        if (!start || !end) return std::nullopt;
        
        return Range(*start, *end);
    }
};

struct TextDocumentIdentifier {
    std::string uri;
    
    TextDocumentIdentifier() = default;
    TextDocumentIdentifier(const std::string& uri) : uri(uri) {}
    
    JsonValue toJson() const {
        JsonObject obj;
        obj["uri"] = JsonValue(uri);
        return JsonValue(obj);
    }
    
    static std::optional<TextDocumentIdentifier> fromJson(const JsonValue& json) {
        if (!json.isObject()) return std::nullopt;
        
        auto obj = json.asObject();
        if (!obj.count("uri")) return std::nullopt;
        
        return TextDocumentIdentifier(obj.at("uri").asString());
    }
};

struct VersionedTextDocumentIdentifier : public TextDocumentIdentifier {
    int version;
    
    VersionedTextDocumentIdentifier() = default;
    VersionedTextDocumentIdentifier(const std::string& uri, int version) 
        : TextDocumentIdentifier(uri), version(version) {}
    
    JsonValue toJson() const {
        JsonObject obj;
        obj["uri"] = JsonValue(uri);
        obj["version"] = JsonValue(version);
        return JsonValue(obj);
    }
    
    static std::optional<VersionedTextDocumentIdentifier> fromJson(const JsonValue& json) {
        if (!json.isObject()) return std::nullopt;
        
        auto obj = json.asObject();
        if (!obj.count("uri") || !obj.count("version")) return std::nullopt;
        
        return VersionedTextDocumentIdentifier(
            obj.at("uri").asString(),
            static_cast<int>(obj.at("version").asNumber())
        );
    }
};

} // namespace LSP
} // namespace Cryo