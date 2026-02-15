#include "LSP/Server.hpp"
#include "LSP/Protocol.hpp"
#include "LSP/Transport.hpp"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace CryoLSP
{

    Server::Server()
        : _diagnosticsProvider(_engine, _transport),
          _hoverProvider(_engine, _documents),
          _definitionProvider(_engine),
          _completionProvider(_engine, _documents),
          _symbolProvider(_engine),
          _referencesProvider(_engine),
          _signatureHelpProvider(_engine),
          _semanticTokensProvider(_engine, _documents)
    {
        registerHandlers();
    }

    void Server::registerHandlers()
    {
        // Lifecycle
        _handlers["initialize"] = [this](const cjson::JsonValue &p)
        { return handleInitialize(p); };
        _handlers["shutdown"] = [this](const cjson::JsonValue &p)
        { return handleShutdown(p); };

        // Feature request handlers
        _handlers["textDocument/hover"] = [this](const cjson::JsonValue &p)
        { return handleHover(p); };
        _handlers["textDocument/definition"] = [this](const cjson::JsonValue &p)
        { return handleDefinition(p); };
        _handlers["textDocument/completion"] = [this](const cjson::JsonValue &p)
        { return handleCompletion(p); };
        _handlers["textDocument/documentSymbol"] = [this](const cjson::JsonValue &p)
        { return handleDocumentSymbol(p); };
        _handlers["textDocument/references"] = [this](const cjson::JsonValue &p)
        { return handleReferences(p); };
        _handlers["textDocument/signatureHelp"] = [this](const cjson::JsonValue &p)
        { return handleSignatureHelp(p); };
        _handlers["textDocument/semanticTokens/full"] = [this](const cjson::JsonValue &p)
        { return handleSemanticTokensFull(p); };

        // Notification handlers
        _notification_handlers["initialized"] = [this](const cjson::JsonValue &p)
        { handleInitialized(p); };
        _notification_handlers["exit"] = [this](const cjson::JsonValue &p)
        { handleExit(p); };
        _notification_handlers["textDocument/didOpen"] = [this](const cjson::JsonValue &p)
        { handleDidOpen(p); };
        _notification_handlers["textDocument/didChange"] = [this](const cjson::JsonValue &p)
        { handleDidChange(p); };
        _notification_handlers["textDocument/didClose"] = [this](const cjson::JsonValue &p)
        { handleDidClose(p); };
        _notification_handlers["textDocument/didSave"] = [this](const cjson::JsonValue &p)
        { handleDidSave(p); };
    }

    int Server::run()
    {
#ifdef _WIN32
        // Set stdin/stdout to binary mode on Windows
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif

        Transport::log("CryoLSP server starting...");

        while (true)
        {
            auto msg = _transport.readMessage();
            if (!msg.has_value())
            {
                Transport::log("EOF on stdin, exiting");
                break;
            }

            try
            {
                dispatch(msg.value());
            }
            catch (const std::exception &e)
            {
                Transport::log(std::string("Dispatch error: ") + e.what());
            }
            catch (...)
            {
                Transport::log("Dispatch error: unknown exception (non-std::exception)");
            }
        }

        return _shutdown ? 0 : 1;
    }

    void Server::dispatch(const cjson::JsonValue &message)
    {
        if (!message.is_object())
        {
            Transport::log("Received non-object message");
            return;
        }

        const auto &obj = message.as_object();

        // Check if it has an id (request) or not (notification)
        bool has_id = obj.contains("id");
        std::string method;

        if (obj.contains("method") && message["method"].is_string())
        {
            method = message["method"].get_string();
        }
        else
        {
            if (has_id)
            {
                _transport.sendError(message["id"], ErrorCodes::InvalidRequest, "Missing method");
            }
            return;
        }

        // Get params (may be null/missing)
        cjson::JsonValue params;
        if (obj.contains("params"))
        {
            params = message["params"];
        }

        if (has_id)
        {
            // Request - needs response
            std::string id_str = message["id"].dump();
            Transport::log("Request: " + method + " (id=" + id_str + ")");

            auto handler_it = _handlers.find(method);
            if (handler_it != _handlers.end())
            {
                // Check initialization state
                if (!_initialized && method != "initialize")
                {
                    _transport.sendError(message["id"], ErrorCodes::ServerNotInitialized,
                                          "Server not initialized");
                    return;
                }

                try
                {
                    auto result = handler_it->second(params);
                    _transport.sendResponse(message["id"], result);
                }
                catch (const std::exception &e)
                {
                    Transport::log(std::string("Handler error for ") + method + ": " + e.what());
                    _transport.sendError(message["id"], ErrorCodes::InternalError, e.what());
                }
            }
            else
            {
                Transport::log("Unhandled request: " + method);
                _transport.sendError(message["id"], ErrorCodes::MethodNotFound,
                                      "Method not found: " + method);
            }
        }
        else
        {
            // Notification - no response
            Transport::log("Notification: " + method);

            auto handler_it = _notification_handlers.find(method);
            if (handler_it != _notification_handlers.end())
            {
                try
                {
                    handler_it->second(params);
                }
                catch (const std::exception &e)
                {
                    Transport::log(std::string("Notification handler error for ") + method + ": " + e.what());
                }
            }
            // Unknown notifications are silently ignored per LSP spec
        }
    }

    // ============================================================================
    // Lifecycle Handlers
    // ============================================================================

    cjson::JsonValue Server::handleInitialize(const cjson::JsonValue &params)
    {
        Transport::log("Handling initialize request");

        // Extract workspace root
        if (params.is_object())
        {
            const auto &obj = params.as_object();

            // Try rootUri first (preferred), then rootPath
            if (obj.contains("rootUri") && params["rootUri"].is_string())
            {
                _workspace_root = uri_to_path(params["rootUri"].get_string());
            }
            else if (obj.contains("rootPath") && params["rootPath"].is_string())
            {
                _workspace_root = params["rootPath"].get_string();
            }
        }

        if (!_workspace_root.empty())
        {
            Transport::log("Workspace root: " + _workspace_root);
            _engine.setWorkspaceRoot(_workspace_root);
        }

        _initialized = true;

        // Build result
        cjson::JsonObject result;
        result["capabilities"] = buildCapabilities();

        cjson::JsonObject serverInfo;
        serverInfo["name"] = cjson::JsonValue("CryoLSP");
        serverInfo["version"] = cjson::JsonValue("1.0.0");
        result["serverInfo"] = cjson::JsonValue(std::move(serverInfo));

        return cjson::JsonValue(std::move(result));
    }

    cjson::JsonValue Server::handleShutdown(const cjson::JsonValue &)
    {
        Transport::log("Handling shutdown request");
        _shutdown = true;
        return cjson::JsonValue(nullptr); // null response
    }

    void Server::handleInitialized(const cjson::JsonValue &)
    {
        Transport::log("Client initialized notification received");
    }

    void Server::handleExit(const cjson::JsonValue &)
    {
        Transport::log("Exit notification received");
        std::exit(_shutdown ? 0 : 1);
    }

    // ============================================================================
    // Document Sync Handlers
    // ============================================================================

    void Server::handleDidOpen(const cjson::JsonValue &params)
    {
        if (!params.is_object())
            return;

        auto item = text_document_item_from_json(params["textDocument"]);
        _documents.open(item.uri, item.text, item.version);

        Transport::log("Document opened: " + item.uri);

        // Trigger analysis
        _diagnosticsProvider.publishDiagnostics(item.uri, item.text);
    }

    void Server::handleDidChange(const cjson::JsonValue &params)
    {
        if (!params.is_object())
            return;

        auto td = text_document_id_from_json(params["textDocument"]);
        int version = 0;
        if (params["textDocument"].is_object() && params["textDocument"].as_object().contains("version"))
        {
            version = params["textDocument"]["version"].get_int();
        }

        // Full sync: contentChanges[0].text has the full content
        std::string content;
        if (params.as_object().contains("contentChanges") && params["contentChanges"].is_array())
        {
            const auto &changes = params["contentChanges"].as_array();
            if (!changes.empty() && changes[0].is_object())
            {
                content = changes[0]["text"].get_string();
            }
        }

        _documents.update(td.uri, content, version);
    }

    void Server::handleDidClose(const cjson::JsonValue &params)
    {
        if (!params.is_object())
            return;

        auto td = text_document_id_from_json(params["textDocument"]);
        _documents.close(td.uri);

        // Clear diagnostics
        _diagnosticsProvider.clearDiagnostics(td.uri);

        Transport::log("Document closed: " + td.uri);
    }

    void Server::handleDidSave(const cjson::JsonValue &params)
    {
        if (!params.is_object())
            return;

        auto td = text_document_id_from_json(params["textDocument"]);

        // Re-analyze with stored content
        auto content = _documents.getContent(td.uri);
        if (content.has_value())
        {
            _diagnosticsProvider.publishDiagnostics(td.uri, content.value());
        }
    }

    // ============================================================================
    // Feature Handlers
    // ============================================================================

    cjson::JsonValue Server::handleHover(const cjson::JsonValue &params)
    {
        auto tdp = text_document_position_from_json(params);
        auto result = _hoverProvider.getHover(tdp.textDocument.uri, tdp.position);

        if (result.has_value())
            return hover_to_json(result.value());
        return cjson::JsonValue(nullptr);
    }

    cjson::JsonValue Server::handleDefinition(const cjson::JsonValue &params)
    {
        auto tdp = text_document_position_from_json(params);
        auto result = _definitionProvider.getDefinition(tdp.textDocument.uri, tdp.position);

        if (result.has_value())
            return location_to_json(result.value());
        return cjson::JsonValue(nullptr);
    }

    cjson::JsonValue Server::handleCompletion(const cjson::JsonValue &params)
    {
        auto tdp = text_document_position_from_json(params);

        // Extract trigger character from completion context
        std::string triggerCharacter;
        if (params.is_object() && params.as_object().contains("context") &&
            params["context"].is_object() && params["context"].as_object().contains("triggerCharacter") &&
            params["context"]["triggerCharacter"].is_string())
        {
            triggerCharacter = params["context"]["triggerCharacter"].get_string();
        }

        auto result = _completionProvider.getCompletions(tdp.textDocument.uri, tdp.position, triggerCharacter);
        return completion_list_to_json(result);
    }

    cjson::JsonValue Server::handleDocumentSymbol(const cjson::JsonValue &params)
    {
        auto td = text_document_id_from_json(params["textDocument"]);
        auto result = _symbolProvider.getDocumentSymbols(td.uri);

        cjson::JsonArray arr;
        for (const auto &sym : result)
        {
            arr.push_back(document_symbol_to_json(sym));
        }
        return cjson::JsonValue(std::move(arr));
    }

    cjson::JsonValue Server::handleReferences(const cjson::JsonValue &params)
    {
        auto tdp = text_document_position_from_json(params);
        bool includeDecl = true;
        if (params.is_object() && params.as_object().contains("context") &&
            params["context"].is_object() && params["context"].as_object().contains("includeDeclaration"))
        {
            includeDecl = params["context"]["includeDeclaration"].get_bool();
        }

        auto result = _referencesProvider.getReferences(tdp.textDocument.uri, tdp.position, includeDecl);

        cjson::JsonArray arr;
        for (const auto &loc : result)
        {
            arr.push_back(location_to_json(loc));
        }
        return cjson::JsonValue(std::move(arr));
    }

    cjson::JsonValue Server::handleSignatureHelp(const cjson::JsonValue &params)
    {
        auto tdp = text_document_position_from_json(params);
        auto result = _signatureHelpProvider.getSignatureHelp(tdp.textDocument.uri, tdp.position);

        if (result.has_value())
            return signature_help_to_json(result.value());
        return cjson::JsonValue(nullptr);
    }

    cjson::JsonValue Server::handleSemanticTokensFull(const cjson::JsonValue &params)
    {
        auto td = text_document_id_from_json(params["textDocument"]);
        auto result = _semanticTokensProvider.getSemanticTokens(td.uri);
        return semantic_tokens_to_json(result);
    }

    // ============================================================================
    // Capabilities
    // ============================================================================

    cjson::JsonValue Server::buildCapabilities()
    {
        cjson::JsonObject caps;

        // Text document sync
        cjson::JsonObject textDocSync;
        textDocSync["openClose"] = cjson::JsonValue(true);
        textDocSync["change"] = cjson::JsonValue(1); // Full sync
        cjson::JsonObject saveOptions;
        saveOptions["includeText"] = cjson::JsonValue(false);
        textDocSync["save"] = cjson::JsonValue(std::move(saveOptions));
        caps["textDocumentSync"] = cjson::JsonValue(std::move(textDocSync));

        // Hover
        caps["hoverProvider"] = cjson::JsonValue(true);

        // Definition
        caps["definitionProvider"] = cjson::JsonValue(true);

        // Completion
        cjson::JsonObject completionProvider;
        cjson::JsonArray triggerChars;
        triggerChars.push_back(cjson::JsonValue("."));
        triggerChars.push_back(cjson::JsonValue(":"));
        triggerChars.push_back(cjson::JsonValue("<"));
        completionProvider["triggerCharacters"] = cjson::JsonValue(std::move(triggerChars));
        completionProvider["resolveProvider"] = cjson::JsonValue(false);
        caps["completionProvider"] = cjson::JsonValue(std::move(completionProvider));

        // Document symbols
        caps["documentSymbolProvider"] = cjson::JsonValue(true);

        // References
        caps["referencesProvider"] = cjson::JsonValue(true);

        // Signature help
        cjson::JsonObject sigHelpProvider;
        cjson::JsonArray sigTriggers;
        sigTriggers.push_back(cjson::JsonValue("("));
        sigTriggers.push_back(cjson::JsonValue(","));
        sigHelpProvider["triggerCharacters"] = cjson::JsonValue(std::move(sigTriggers));
        caps["signatureHelpProvider"] = cjson::JsonValue(std::move(sigHelpProvider));

        // Semantic tokens
        auto legend = SemanticTokensProvider::getLegend();
        cjson::JsonObject semTokens;
        cjson::JsonArray tokenTypes;
        for (const auto &t : legend.tokenTypes)
            tokenTypes.push_back(cjson::JsonValue(t));
        cjson::JsonArray tokenModifiers;
        for (const auto &m : legend.tokenModifiers)
            tokenModifiers.push_back(cjson::JsonValue(m));
        cjson::JsonObject semLegend;
        semLegend["tokenTypes"] = cjson::JsonValue(std::move(tokenTypes));
        semLegend["tokenModifiers"] = cjson::JsonValue(std::move(tokenModifiers));
        semTokens["legend"] = cjson::JsonValue(std::move(semLegend));
        semTokens["full"] = cjson::JsonValue(true);
        caps["semanticTokensProvider"] = cjson::JsonValue(std::move(semTokens));

        return cjson::JsonValue(std::move(caps));
    }

} // namespace CryoLSP
