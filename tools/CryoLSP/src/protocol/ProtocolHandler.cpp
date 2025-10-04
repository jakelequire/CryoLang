#include "../../include/ProtocolHandler.hpp"
#include "../../include/Logger.hpp"

namespace Cryo {
namespace LSP {

std::optional<std::string> ProtocolHandler::processMessage(const std::string& messageJson) {
    Logger& logger = Logger::instance();
    
    try {
        // Parse the JSON
        auto json = JsonParser::parse(messageJson);
        if (!json.has_value()) {
            logger.error("LSP", "Failed to parse JSON message: " + messageJson);
            return createErrorResponse("", LSPErrorCode::ParseError, "Parse error");
        }
        
        auto jsonValue = json.value();
        if (!jsonValue.isObject()) {
            logger.error("LSP", "Message is not a JSON object");
            return createErrorResponse("", LSPErrorCode::InvalidRequest, "Invalid request");
        }
        
        auto obj = jsonValue.asObject();
        
        // Debug: Print all keys in the object
        logger.debug("LSP", "JSON object contents:");
        for (const auto& pair : obj) {
            logger.debug("LSP", "  Key: '" + pair.first + "', Value type: " + 
                       (pair.second.isString() ? "string" : 
                        pair.second.isNumber() ? "number" : 
                        pair.second.isObject() ? "object" : "other"));
        }
        
        logger.debug("LSP", "Message object keys: id=" + std::string(obj.count("id") ? "present" : "absent") + 
                           ", method=" + std::string(obj.count("method") ? "present" : "absent"));
        
        // Check if it's a request (has 'id' field)
        if (obj.count("id")) {
            logger.debug("LSP", "Attempting to parse as request...");
            auto request = Request::fromJson(jsonValue);
            if (request.has_value()) {
                logger.info("LSP", "Processing request: " + request->method + " (id: " + request->id + ")");
                return handleRequest(request.value());
            } else {
                logger.error("LSP", "Invalid request format - failed Request::fromJson");
                return createErrorResponse("", LSPErrorCode::InvalidRequest, "Invalid request");
            }
        }
        // Check if it's a notification (no 'id' field but has 'method')
        else if (obj.count("method")) {
            auto notification = Notification::fromJson(jsonValue);
            if (notification.has_value()) {
                logger.info("LSP", "Processing notification: " + notification->method);
                handleNotification(notification.value());
                return std::nullopt; // Notifications don't require responses
            } else {
                logger.error("LSP", "Invalid notification format");
                return std::nullopt; // Don't respond to invalid notifications
            }
        }
        // Response message (we typically don't handle responses from client)
        else {
            logger.error("LSP", "Received response message (unexpected)");
            return std::nullopt;
        }
        
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception while processing message: " + std::string(e.what()));
        return createErrorResponse("", LSPErrorCode::InternalError, "Internal error");
    }
}

std::string ProtocolHandler::handleRequest(const Request& request) {
    Logger& logger = Logger::instance();
    
    // Look for registered handler
    auto it = requestHandlers.find(request.method);
    if (it != requestHandlers.end()) {
        try {
            auto result = it->second(request.id, request.method, request.params);
            return createSuccessResponse(request.id, result);
        } catch (const std::exception& e) {
            logger.error("LSP", "Handler for '" + request.method + "' threw exception: " + e.what());
            return createErrorResponse(request.id, LSPErrorCode::InternalError, "Handler error: " + std::string(e.what()));
        }
    } else {
        logger.error("LSP", "No handler for request method: " + request.method);
        return createErrorResponse(request.id, LSPErrorCode::MethodNotFound, "Method not found: " + request.method);
    }
}

void ProtocolHandler::handleNotification(const Notification& notification) {
    Logger& logger = Logger::instance();
    
    // Look for registered handler
    auto it = notificationHandlers.find(notification.method);
    if (it != notificationHandlers.end()) {
        try {
            it->second(notification.method, notification.params);
        } catch (const std::exception& e) {
            logger.error("LSP", "Handler for notification '" + notification.method + "' threw exception: " + e.what());
        }
    } else {
        logger.error("LSP", "No handler for notification method: " + notification.method);
    }
}

std::string ProtocolHandler::createSuccessResponse(const std::string& id, const JsonValue& result) {
    Response response(id, result);
    return response.toJson().toString();
}

std::string ProtocolHandler::createErrorResponse(const std::string& id, LSPErrorCode code, const std::string& message) {
    LSPError error(code, message);
    Response response(id, error.toJson(), true);
    return response.toJson().toString();
}

std::string ProtocolHandler::createNotification(const std::string& method, const JsonValue& params) {
    Notification notification(method, params);
    return notification.toJson().toString();
}

JsonValue ProtocolHandler::createErrorObject(LSPErrorCode code, const std::string& message) {
    LSPError error(code, message);
    return error.toJson();
}

} // namespace LSP
} // namespace Cryo