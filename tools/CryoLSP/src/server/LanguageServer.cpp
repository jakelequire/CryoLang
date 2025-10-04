#include "../../include/LanguageServer.hpp"
#include <thread>
#include <chrono>

namespace Cryo
{
    namespace LSP
    {

        LanguageServer::LanguageServer(std::unique_ptr<Transport> transport)
            : transport(std::move(transport)), protocolHandler(std::make_unique<ProtocolHandler>()), documentManager(std::make_unique<DocumentManager>()), logger(Logger::instance()), state(ServerState::Uninitialized)
        {

            // Initialize the transport
            if (!this->transport->initialize())
            {
                logger.error("LSP", "Failed to initialize transport");
                throw std::runtime_error("Transport initialization failed");
            }

            setupHandlers();
            logger.info("LSP", "CryoLSP Language Server initialized");
        }

        void LanguageServer::setupHandlers()
        {
            // Register basic handlers
            protocolHandler->registerRequestHandler("initialize",
                                                    [this](const JsonValue &id, const std::string &method, const JsonValue &params)
                                                    {
                                                        logger.info("LSP", "=== PROCESSING INITIALIZE REQUEST ===");
                                                        logger.debug("LSP", "Initialize params received (large payload)");

                                                        // Basic initialize response
                                                        JsonObject caps;
                                                        caps["textDocumentSync"] = JsonValue(1); // Full sync
                                                        caps["hoverProvider"] = JsonValue(true);

                                                        JsonObject result;
                                                        result["capabilities"] = JsonValue(caps);

                                                        state = ServerState::Initialized;
                                                        logger.info("LSP", "Server initialized and ready for requests (no initialized notification required)");
                                                        logger.info("LSP", "Capabilities configured: textDocumentSync=1, hoverProvider=true");
                                                        return JsonValue(result);
                                                    });

            protocolHandler->registerRequestHandler("shutdown",
                                                    [this](const JsonValue &id, const std::string &method, const JsonValue &params)
                                                    {
                                                        state = ServerState::Shutdown;
                                                        return JsonValue(); // null result
                                                    });

            protocolHandler->registerNotificationHandler("initialized",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             logger.info("LSP", "=== INITIALIZED NOTIFICATION RECEIVED ===");
                                                             logger.info("LSP", "Server is now fully ready for all requests");
                                                             logger.info("LSP", "LSP handshake completed successfully");
                                                         });

            protocolHandler->registerNotificationHandler("exit",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             state = ServerState::Shutdown;
                                                         });

            // Add textDocument/hover handler
            protocolHandler->registerRequestHandler("textDocument/hover",
                                                    [this](const JsonValue &id, const std::string &method, const JsonValue &params)
                                                    {
                                                        logger.info("LSP", "=== HOVER REQUEST RECEIVED (bypassing initialized check) ===");
                                                        return handleHover(params);
                                                    });

            // Add document lifecycle handlers
            protocolHandler->registerNotificationHandler("textDocument/didOpen",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             logger.info("LSP", "=== DID OPEN NOTIFICATION RECEIVED ===");
                                                             handleDidOpen(params);
                                                         });

            protocolHandler->registerNotificationHandler("textDocument/didChange",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             handleDidChange(params);
                                                         });

            protocolHandler->registerNotificationHandler("textDocument/didClose",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             handleDidClose(params);
                                                         });

            // Add cancelRequest handler to suppress error messages
            protocolHandler->registerNotificationHandler("$/cancelRequest",
                                                         [this](const std::string &method, const JsonValue &params)
                                                         {
                                                             logger.debug("LSP", "Received cancel request - ignoring");
                                                         });
        }

        bool LanguageServer::run()
        {
            logger.info("LSP", "Starting language server main loop");

            int iteration = 0;
            while (state != ServerState::Shutdown)
            {
                try
                {
                    iteration++;
                    logger.debug("LSP", "Main loop iteration #" + std::to_string(iteration) + " - state: " + std::to_string(static_cast<int>(state)) + ", transport active: " + (transport->is_active() ? "true" : "false"));

                    // Read message from transport
                    logger.debug("LSP", "Attempting to read message from transport...");
                    auto message = transport->read_message();

                    if (!message.has_value())
                    {
                        logger.debug("LSP", "No message received from transport");
                        // No message or transport closed
                        if (transport->is_active())
                        {
                            logger.debug("LSP", "Transport still active, continuing with delay");
                            // Add a small delay to prevent busy-waiting
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue; // Try again
                        }
                        else
                        {
                            logger.info("LSP", "Transport disconnected, shutting down");
                            break;
                        }
                    }

                    logger.info("LSP", "Received message #" + std::to_string(iteration) + ", processing...");
                    logger.debug("LSP", "Message preview: " + message->substr(0, std::min(200, static_cast<int>(message->length()))));
                    processMessage(message.value());
                }
                catch (const std::exception &e)
                {
                    logger.error("LSP", "Exception in main loop iteration #" + std::to_string(iteration) + ": " + std::string(e.what()));
                    // Continue running unless it's a critical error
                }
            }

            logger.info("LSP", "Language server shutting down after " + std::to_string(iteration) + " iterations");
            return true;
        }

        void LanguageServer::processMessage(const std::string &message)
        {
            logger.debug("LSP", "Processing message: " + message.substr(0, 100) + (message.length() > 100 ? "..." : ""));
            logger.debug("LSP", "Full message length: " + std::to_string(message.length()));
            logger.debug("LSP", "Current server state: " + std::to_string(static_cast<int>(state)));

            try
            {
                auto response = protocolHandler->processMessage(message);
                logger.debug("LSP", "Protocol handler returned response");

                if (response.has_value())
                {
                    logger.debug("LSP", "Sending response: " + response->substr(0, 100) + (response->length() > 100 ? "..." : ""));
                    // Send response back
                    if (!transport->write_message(response.value()))
                    {
                        logger.error("LSP", "Failed to send response message");
                    }
                    else
                    {
                        logger.debug("LSP", "Response sent successfully");
                    }
                }
                else
                {
                    logger.debug("LSP", "No response to send (notification or void request)");
                }
            }
            catch (const std::exception &e)
            {
                logger.error("LSP", "Exception in processMessage: " + std::string(e.what()));
                logger.error("LSP", "Problematic message was: " + message);
            }
        }

        JsonValue LanguageServer::handleHover(const JsonValue &params)
        {
            auto start_time = std::chrono::steady_clock::now();
            logger.debug("LSP", "=== HOVER REQUEST RECEIVED ===");
            try
            {
                if (!params.isObject())
                {
                    logger.error("LSP", "Hover params is not an object");
                    return JsonValue(); // null
                }

                auto obj = params.asObject();

                // Extract textDocument URI
                if (!obj.count("textDocument") || !obj.at("textDocument").isObject())
                {
                    logger.error("LSP", "Missing textDocument in hover params");
                    return JsonValue(); // null
                }

                auto textDoc = obj.at("textDocument").asObject();
                if (!textDoc.count("uri") || !textDoc.at("uri").isString())
                {
                    logger.error("LSP", "Missing URI in textDocument");
                    return JsonValue(); // null
                }

                std::string uri = textDoc.at("uri").asString();

                // Extract position
                if (!obj.count("position") || !obj.at("position").isObject())
                {
                    logger.error("LSP", "Missing position in hover params");
                    return JsonValue(); // null
                }

                auto posObj = obj.at("position").asObject();
                if (!posObj.count("line") || !posObj.count("character"))
                {
                    logger.error("LSP", "Missing line/character in position");
                    return JsonValue(); // null
                }

                Position position(
                    static_cast<int>(posObj.at("line").asNumber()),
                    static_cast<int>(posObj.at("character").asNumber()));

                logger.info("LSP", "Hover request for " + uri + " at " + std::to_string(position.line) + ":" + std::to_string(position.character));

                // TEMPORARY DEBUG: Return a minimal response using proper LSP Hover format
                JsonObject contents;
                contents["kind"] = JsonValue("plaintext");
                contents["value"] = JsonValue("Simple test");

                JsonObject result;
                result["contents"] = JsonValue(contents);

                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                logger.debug("LSP", "Hover request processed in " + std::to_string(duration.count()) + "ms");

                logger.info("LSP", "Returning minimal test response");
                return JsonValue(result);
            }
            catch (const std::exception &e)
            {
                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                logger.error("LSP", "Exception in handleHover after " + std::to_string(duration.count()) + "ms: " + std::string(e.what()));
                return JsonValue(); // null
            }
        }

        void LanguageServer::handleDidOpen(const JsonValue &params)
        {
            try
            {
                if (!params.isObject())
                {
                    logger.error("LSP", "didOpen params is not an object");
                    return;
                }

                auto obj = params.asObject();
                if (!obj.count("textDocument") || !obj.at("textDocument").isObject())
                {
                    logger.error("LSP", "Missing textDocument in didOpen params");
                    return;
                }

                auto textDoc = obj.at("textDocument").asObject();

                // Debug: Log all available fields
                logger.debug("LSP", "textDocument fields available:");
                for (const auto &pair : textDoc)
                {
                    logger.debug("LSP", "  Field: " + pair.first + " (type: " +
                                            (pair.second.isString() ? "string" : pair.second.isNumber() ? "number"
                                                                             : pair.second.isObject()   ? "object"
                                                                                                        : "other") +
                                            ")");
                }

                // Check for each field individually to see which one is missing
                bool hasUri = textDoc.count("uri");
                bool hasLanguageId = textDoc.count("languageId");
                bool hasVersion = textDoc.count("version");
                bool hasText = textDoc.count("text");

                logger.debug("LSP", std::string("Field check: uri=") + (hasUri ? "yes" : "no") +
                                        ", languageId=" + (hasLanguageId ? "yes" : "no") +
                                        ", version=" + (hasVersion ? "yes" : "no") +
                                        ", text=" + (hasText ? "yes" : "no"));

                // Use safer key checking to avoid unordered_map::at exceptions
                if (!hasUri || !hasLanguageId || !hasVersion)
                {
                    logger.error("LSP", "Missing required fields in textDocument (uri, languageId, version)");
                    return;
                }

                std::string uri = textDoc.at("uri").asString();
                std::string languageId = textDoc.at("languageId").asString();
                int version = static_cast<int>(textDoc.at("version").asNumber());

                // Get text if available, otherwise use empty string as fallback
                std::string text = "";
                if (hasText)
                {
                    text = textDoc.at("text").asString();
                }
                else
                {
                    logger.info("LSP", "Text field missing from didOpen, using empty string");
                }

                logger.info("LSP", "Document opened: " + uri + " (language: " + languageId +
                                       ", version: " + std::to_string(version) +
                                       ", text length: " + std::to_string(text.length()) + ")");
                documentManager->didOpen(uri, languageId, version, text);
            }
            catch (const std::exception &e)
            {
                logger.error("LSP", "Exception in handleDidOpen: " + std::string(e.what()));
            }
        }

        void LanguageServer::handleDidChange(const JsonValue &params)
        {
            try
            {
                if (!params.isObject())
                {
                    logger.error("LSP", "didChange params is not an object");
                    return;
                }

                auto obj = params.asObject();
                if (!obj.count("textDocument") || !obj.at("textDocument").isObject())
                {
                    logger.error("LSP", "Missing textDocument in didChange params");
                    return;
                }

                auto textDoc = obj.at("textDocument").asObject();
                std::string uri = textDoc.at("uri").asString();
                int version = static_cast<int>(textDoc.at("version").asNumber());

                // For full text sync, get the full document content
                if (obj.count("contentChanges") && obj.at("contentChanges").isArray())
                {
                    auto changes = obj.at("contentChanges").asArray();
                    if (!changes.empty() && changes[0].isObject())
                    {
                        auto change = changes[0].asObject();
                        if (change.count("text"))
                        {
                            std::string text = change.at("text").asString();
                            documentManager->didChange(uri, version, text);
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                logger.error("LSP", "Exception in handleDidChange: " + std::string(e.what()));
            }
        }

        void LanguageServer::handleDidClose(const JsonValue &params)
        {
            try
            {
                if (!params.isObject())
                {
                    logger.error("LSP", "didClose params is not an object");
                    return;
                }

                auto obj = params.asObject();
                if (!obj.count("textDocument") || !obj.at("textDocument").isObject())
                {
                    logger.error("LSP", "Missing textDocument in didClose params");
                    return;
                }

                auto textDoc = obj.at("textDocument").asObject();
                std::string uri = textDoc.at("uri").asString();

                documentManager->didClose(uri);
            }
            catch (const std::exception &e)
            {
                logger.error("LSP", "Exception in handleDidClose: " + std::string(e.what()));
            }
        }

    } // namespace LSP
} // namespace Cryo