#pragma once

#include "LSP/Transport.hpp"
#include "LSP/DocumentStore.hpp"
#include "LSP/AnalysisEngine.hpp"
#include "LSP/Providers/DiagnosticsProvider.hpp"
#include "LSP/Providers/HoverProvider.hpp"
#include "LSP/Providers/DefinitionProvider.hpp"
#include "LSP/Providers/CompletionProvider.hpp"
#include "LSP/Providers/SymbolProvider.hpp"
#include "LSP/Providers/ReferencesProvider.hpp"
#include "LSP/Providers/SignatureHelpProvider.hpp"
#include "LSP/Providers/SemanticTokensProvider.hpp"

#include <string>
#include <functional>
#include <unordered_map>
#include <cjson/json.hpp>

namespace CryoLSP
{

    /**
     * @brief Main LSP server class
     *
     * Handles the JSON-RPC message loop, dispatches to handlers,
     * manages lifecycle (initialize, shutdown, exit).
     */
    class Server
    {
    public:
        Server();
        ~Server() = default;

        // Run the server (blocks until exit)
        int run();

    private:
        // Components
        Transport _transport;
        DocumentStore _documents;
        AnalysisEngine _engine;

        // Providers
        DiagnosticsProvider _diagnosticsProvider;
        HoverProvider _hoverProvider;
        DefinitionProvider _definitionProvider;
        CompletionProvider _completionProvider;
        SymbolProvider _symbolProvider;
        ReferencesProvider _referencesProvider;
        SignatureHelpProvider _signatureHelpProvider;
        SemanticTokensProvider _semanticTokensProvider;

        // State
        bool _initialized = false;
        bool _shutdown = false;
        std::string _workspace_root;

        // Handler type
        using Handler = std::function<cjson::JsonValue(const cjson::JsonValue &params)>;
        using NotificationHandler = std::function<void(const cjson::JsonValue &params)>;

        std::unordered_map<std::string, Handler> _handlers;
        std::unordered_map<std::string, NotificationHandler> _notification_handlers;

        // Register all handlers
        void registerHandlers();

        // Dispatch a message
        void dispatch(const cjson::JsonValue &message);

        // ============================================
        // Lifecycle handlers
        // ============================================
        cjson::JsonValue handleInitialize(const cjson::JsonValue &params);
        cjson::JsonValue handleShutdown(const cjson::JsonValue &params);

        // ============================================
        // Notification handlers
        // ============================================
        void handleInitialized(const cjson::JsonValue &params);
        void handleExit(const cjson::JsonValue &params);
        void handleDidOpen(const cjson::JsonValue &params);
        void handleDidChange(const cjson::JsonValue &params);
        void handleDidClose(const cjson::JsonValue &params);
        void handleDidSave(const cjson::JsonValue &params);

        // ============================================
        // Feature handlers
        // ============================================
        cjson::JsonValue handleHover(const cjson::JsonValue &params);
        cjson::JsonValue handleDefinition(const cjson::JsonValue &params);
        cjson::JsonValue handleCompletion(const cjson::JsonValue &params);
        cjson::JsonValue handleDocumentSymbol(const cjson::JsonValue &params);
        cjson::JsonValue handleReferences(const cjson::JsonValue &params);
        cjson::JsonValue handleSignatureHelp(const cjson::JsonValue &params);
        cjson::JsonValue handleSemanticTokensFull(const cjson::JsonValue &params);

        // ============================================
        // Helpers
        // ============================================
        cjson::JsonValue buildCapabilities();
    };

} // namespace CryoLSP
