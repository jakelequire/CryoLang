#include "../../include/LSPMessage.hpp"

namespace Cryo {
namespace LSP {

std::optional<Request> Request::fromJson(const JsonValue& json) {
    if (!json.isObject()) return std::nullopt;
    
    auto obj = json.asObject();
    if (!obj.count("jsonrpc") || !obj.count("id") || !obj.count("method")) {
        return std::nullopt;
    }
    
    Request req;
    req.jsonrpc = obj.at("jsonrpc").asString();
    
    // Handle both string and numeric IDs
    auto idValue = obj.at("id");
    if (idValue.isString()) {
        req.id = idValue.asString();
    } else if (idValue.isNumber()) {
        req.id = std::to_string(static_cast<int>(idValue.asNumber()));
    } else {
        return std::nullopt; // Invalid ID type
    }
    
    req.method = obj.at("method").asString();
    
    if (obj.count("params")) {
        req.params = obj.at("params");
    }
    
    return req;
}

std::optional<Notification> Notification::fromJson(const JsonValue& json) {
    if (!json.isObject()) return std::nullopt;
    
    auto obj = json.asObject();
    if (!obj.count("jsonrpc") || !obj.count("method")) {
        return std::nullopt;
    }
    
    Notification notif;
    notif.jsonrpc = obj.at("jsonrpc").asString();
    notif.method = obj.at("method").asString();
    
    if (obj.count("params")) {
        notif.params = obj.at("params");
    }
    
    return notif;
}

} // namespace LSP
} // namespace Cryo