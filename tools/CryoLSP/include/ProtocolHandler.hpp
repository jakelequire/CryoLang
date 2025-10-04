#pragma once

#include "LSPMessage.hpp"
#include <memory>
#include <functional>
#include <map>

namespace Cryo {
namespace LSP {

// Message Handler Types
using RequestHandler = std::function<JsonValue(const std::string& id, const std::string& method, const JsonValue& params)>;
using NotificationHandler = std::function<void(const std::string& method, const JsonValue& params)>;

class ProtocolHandler {
private:
    std::map<std::string, RequestHandler> requestHandlers;
    std::map<std::string, NotificationHandler> notificationHandlers;
    
public:
    ProtocolHandler() = default;
    ~ProtocolHandler() = default;
    
    // Register handlers
    void registerRequestHandler(const std::string& method, RequestHandler handler) {
        requestHandlers[method] = std::move(handler);
    }
    
    void registerNotificationHandler(const std::string& method, NotificationHandler handler) {
        notificationHandlers[method] = std::move(handler);
    }
    
    // Process incoming messages
    std::optional<std::string> processMessage(const std::string& messageJson);
    
    // Helper methods for creating responses
    static std::string createSuccessResponse(const std::string& id, const JsonValue& result);
    static std::string createErrorResponse(const std::string& id, LSPErrorCode code, const std::string& message);
    static std::string createNotification(const std::string& method, const JsonValue& params = JsonValue{});
    
private:
    std::string handleRequest(const Request& request);
    void handleNotification(const Notification& notification);
    
    JsonValue createErrorObject(LSPErrorCode code, const std::string& message);
};

} // namespace LSP
} // namespace Cryo