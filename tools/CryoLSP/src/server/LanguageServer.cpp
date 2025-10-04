#include "../../include/LanguageServer.hpp"
#include <thread>
#include <chrono>

namespace Cryo {
namespace LSP {

LanguageServer::LanguageServer(std::unique_ptr<Transport> transport)
    : transport(std::move(transport))
    , protocolHandler(std::make_unique<ProtocolHandler>())
    , documentManager(std::make_unique<DocumentManager>())
    , logger(Logger::instance())
    , state(ServerState::Uninitialized) {
    
    // Initialize the transport
    if (!this->transport->initialize()) {
        logger.error("LSP", "Failed to initialize transport");
        throw std::runtime_error("Transport initialization failed");
    }
    
    setupHandlers();
    logger.info("LSP", "CryoLSP Language Server initialized");
}

void LanguageServer::setupHandlers() {
    // Register basic handlers
    protocolHandler->registerRequestHandler("initialize", 
        [this](const std::string& id, const std::string& method, const JsonValue& params) {
            // Basic initialize response
            JsonObject caps;
            caps["textDocumentSync"] = JsonValue(1); // Full sync
            caps["hoverProvider"] = JsonValue(true);
            
            JsonObject result;
            result["capabilities"] = JsonValue(caps);
            
            state = ServerState::Initialized;
            return JsonValue(result);
        });
    
    protocolHandler->registerRequestHandler("shutdown", 
        [this](const std::string& id, const std::string& method, const JsonValue& params) {
            state = ServerState::Shutdown;
            return JsonValue(); // null result
        });
    
    protocolHandler->registerNotificationHandler("initialized", 
        [this](const std::string& method, const JsonValue& params) {
            logger.info("LSP", "Server initialized");
        });
    
    protocolHandler->registerNotificationHandler("exit", 
        [this](const std::string& method, const JsonValue& params) {
            state = ServerState::Shutdown;
        });
    
    // Add textDocument/hover handler
    protocolHandler->registerRequestHandler("textDocument/hover", 
        [this](const std::string& id, const std::string& method, const JsonValue& params) {
            return handleHover(params);
        });
    
    // Add document lifecycle handlers
    protocolHandler->registerNotificationHandler("textDocument/didOpen", 
        [this](const std::string& method, const JsonValue& params) {
            handleDidOpen(params);
        });
    
    protocolHandler->registerNotificationHandler("textDocument/didChange", 
        [this](const std::string& method, const JsonValue& params) {
            handleDidChange(params);
        });
    
    protocolHandler->registerNotificationHandler("textDocument/didClose", 
        [this](const std::string& method, const JsonValue& params) {
            handleDidClose(params);
        });
}

bool LanguageServer::run() {
    logger.info("LSP", "Starting language server main loop");
    
    while (state != ServerState::Shutdown) {
        try {
            logger.debug("LSP", "Main loop iteration - checking transport active: " + std::string(transport->is_active() ? "true" : "false"));
            
            // Read message from transport
            logger.debug("LSP", "Attempting to read message from transport...");
            auto message = transport->read_message();
            
            if (!message.has_value()) {
                logger.debug("LSP", "No message received from transport");
                // No message or transport closed
                if (transport->is_active()) {
                    logger.debug("LSP", "Transport still active, continuing with delay");
                    // Add a small delay to prevent busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue; // Try again
                } else {
                    logger.info("LSP", "Transport disconnected, shutting down");
                    break;
                }
            }
            
            logger.info("LSP", "Received message, processing...");
            logger.debug("LSP", "Message preview: " + message->substr(0, std::min(200, static_cast<int>(message->length()))));
            processMessage(message.value());
            
        } catch (const std::exception& e) {
            logger.error("LSP", "Exception in main loop: " + std::string(e.what()));
            // Continue running unless it's a critical error
        }
    }
    
    logger.info("LSP", "Language server shutting down");
    return true;
}

void LanguageServer::processMessage(const std::string& message) {
    logger.debug("LSP", "Processing message: " + message.substr(0, 100) + (message.length() > 100 ? "..." : ""));
    logger.debug("LSP", "Full message length: " + std::to_string(message.length()));
    logger.debug("LSP", "Current server state: " + std::to_string(static_cast<int>(state)));
    
    try {
        auto response = protocolHandler->processMessage(message);
        logger.debug("LSP", "Protocol handler returned response");
        
        if (response.has_value()) {
            logger.debug("LSP", "Sending response: " + response->substr(0, 100) + (response->length() > 100 ? "..." : ""));
            // Send response back
            if (!transport->write_message(response.value())) {
                logger.error("LSP", "Failed to send response message");
            } else {
                logger.debug("LSP", "Response sent successfully");
            }
        } else {
            logger.debug("LSP", "No response to send (notification or void request)");
        }
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception in processMessage: " + std::string(e.what()));
        logger.error("LSP", "Problematic message was: " + message);
    }
}

JsonValue LanguageServer::handleHover(const JsonValue& params) {
    logger.debug("LSP", "=== HOVER REQUEST RECEIVED ===");
    try {
        if (!params.isObject()) {
            logger.error("LSP", "Hover params is not an object");
            return JsonValue(); // null
        }
        
        auto obj = params.asObject();
        
        // Extract textDocument URI
        if (!obj.count("textDocument") || !obj.at("textDocument").isObject()) {
            logger.error("LSP", "Missing textDocument in hover params");
            return JsonValue(); // null
        }
        
        auto textDoc = obj.at("textDocument").asObject();
        if (!textDoc.count("uri") || !textDoc.at("uri").isString()) {
            logger.error("LSP", "Missing URI in textDocument");
            return JsonValue(); // null
        }
        
        std::string uri = textDoc.at("uri").asString();
        
        // Extract position
        if (!obj.count("position") || !obj.at("position").isObject()) {
            logger.error("LSP", "Missing position in hover params");
            return JsonValue(); // null
        }
        
        auto posObj = obj.at("position").asObject();
        if (!posObj.count("line") || !posObj.count("character")) {
            logger.error("LSP", "Missing line/character in position");
            return JsonValue(); // null
        }
        
        Position position(
            static_cast<int>(posObj.at("line").asNumber()),
            static_cast<int>(posObj.at("character").asNumber())
        );
        
        logger.info("LSP", "Hover request for " + uri + " at " + std::to_string(position.line) + ":" + std::to_string(position.character));
        
        // Get hover information
        auto hoverResult = documentManager->getHoverInfo(uri, position);
        if (!hoverResult.has_value()) {
            logger.debug("LSP", "No hover information available");
            return JsonValue(); // null
        }
        
        // Build hover response
        JsonObject result;
        
        // Contents (as markdown string)
        result["contents"] = JsonValue(hoverResult->contents);
        
        // Range (optional)
        JsonObject range;
        JsonObject start;
        start["line"] = JsonValue(hoverResult->start.line);
        start["character"] = JsonValue(hoverResult->start.character);
        
        JsonObject end;
        end["line"] = JsonValue(hoverResult->end.line);
        end["character"] = JsonValue(hoverResult->end.character);
        
        range["start"] = JsonValue(start);
        range["end"] = JsonValue(end);
        result["range"] = JsonValue(range);
        
        logger.info("LSP", "Returning hover info: " + hoverResult->contents);
        return JsonValue(result);
        
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception in handleHover: " + std::string(e.what()));
        return JsonValue(); // null
    }
}

void LanguageServer::handleDidOpen(const JsonValue& params) {
    try {
        if (!params.isObject()) {
            logger.error("LSP", "didOpen params is not an object");
            return;
        }
        
        auto obj = params.asObject();
        if (!obj.count("textDocument") || !obj.at("textDocument").isObject()) {
            logger.error("LSP", "Missing textDocument in didOpen params");
            return;
        }
        
        auto textDoc = obj.at("textDocument").asObject();
        
        std::string uri = textDoc.at("uri").asString();
        std::string languageId = textDoc.at("languageId").asString();
        int version = static_cast<int>(textDoc.at("version").asNumber());
        std::string text = textDoc.at("text").asString();
        
        documentManager->didOpen(uri, languageId, version, text);
        
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception in handleDidOpen: " + std::string(e.what()));
    }
}

void LanguageServer::handleDidChange(const JsonValue& params) {
    try {
        if (!params.isObject()) {
            logger.error("LSP", "didChange params is not an object");
            return;
        }
        
        auto obj = params.asObject();
        if (!obj.count("textDocument") || !obj.at("textDocument").isObject()) {
            logger.error("LSP", "Missing textDocument in didChange params");
            return;
        }
        
        auto textDoc = obj.at("textDocument").asObject();
        std::string uri = textDoc.at("uri").asString();
        int version = static_cast<int>(textDoc.at("version").asNumber());
        
        // For full text sync, get the full document content
        if (obj.count("contentChanges") && obj.at("contentChanges").isArray()) {
            auto changes = obj.at("contentChanges").asArray();
            if (!changes.empty() && changes[0].isObject()) {
                auto change = changes[0].asObject();
                if (change.count("text")) {
                    std::string text = change.at("text").asString();
                    documentManager->didChange(uri, version, text);
                }
            }
        }
        
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception in handleDidChange: " + std::string(e.what()));
    }
}

void LanguageServer::handleDidClose(const JsonValue& params) {
    try {
        if (!params.isObject()) {
            logger.error("LSP", "didClose params is not an object");
            return;
        }
        
        auto obj = params.asObject();
        if (!obj.count("textDocument") || !obj.at("textDocument").isObject()) {
            logger.error("LSP", "Missing textDocument in didClose params");
            return;
        }
        
        auto textDoc = obj.at("textDocument").asObject();
        std::string uri = textDoc.at("uri").asString();
        
        documentManager->didClose(uri);
        
    } catch (const std::exception& e) {
        logger.error("LSP", "Exception in handleDidClose: " + std::string(e.what()));
    }
}

} // namespace LSP
} // namespace Cryo