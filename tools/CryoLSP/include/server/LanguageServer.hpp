#pragma once

#include "ProtocolHandler.hpp"
#include "Transport.hpp"
#include "Logger.hpp"
#include "analyzer/CryoAnalyzer.hpp"
#include "protocol/LSPMessage.hpp"
#include <memory>
#include <string>
#include <map>

// Forward declarations
namespace CryoLSP {
    class CryoAnalyzer;
}

namespace cryo {
namespace lsp {

enum class ServerState {
    Uninitialized,
    Initializing,
    Initialized,
    Shutdown
};

class LanguageServer {
private:
    std::unique_ptr<Transport> transport;
    std::unique_ptr<ProtocolHandler> protocolHandler;
    std::unique_ptr<CryoLSP::CryoAnalyzer> analyzer;
    Logger& logger;
    
    ServerState state;
    std::map<std::string, std::string> documents; // URI -> content
    
public:
    LanguageServer(std::unique_ptr<Transport> transport);
    ~LanguageServer() = default;
    
    // Main server loop
    bool run();
    
    // Server state
    ServerState getState() const { return state; }
    
private:
    // Initialize protocol handlers
    void setupHandlers();
    
    // Helper methods
    void processMessage(const std::string& message);
    
    // LSP message handlers
    JsonValue handleInitialize(const std::string& id, const std::string& method, const JsonValue& params);
    JsonValue handleShutdown(const std::string& id, const std::string& method, const JsonValue& params);
    JsonValue handleHover(const std::string& id, const std::string& method, const JsonValue& params);
    
    void handleInitialized(const std::string& method, const JsonValue& params);
    void handleExit(const std::string& method, const JsonValue& params);
    void handleDidOpenTextDocument(const std::string& method, const JsonValue& params);
    void handleDidChangeTextDocument(const std::string& method, const JsonValue& params);
    void handleDidCloseTextDocument(const std::string& method, const JsonValue& params);
    
    // Enhanced LSP support
    void publishDiagnostics(const std::string& uri);
    void clearDiagnostics(const std::string& uri);
    std::string convertUriToPath(const std::string& uri);
    
    // Helper methods
    ServerCapabilities createServerCapabilities();
    std::string getDocumentContent(const std::string& uri);
    void updateDocumentContent(const std::string& uri, const std::string& content);
};

} // namespace lsp
} // namespace cryo