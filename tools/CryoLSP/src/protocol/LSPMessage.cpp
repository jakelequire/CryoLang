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
    
    // Preserve the original ID type and value
    req.id = obj.at("id");
    
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