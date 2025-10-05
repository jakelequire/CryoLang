#pragma once

#include "ProtocolHandler.hpp"
#include "Transport.hpp"
#include "Logger.hpp"
#include "DocumentManager.hpp"
#include <memory>
#include <string>
#include <map>
#include <vector>

// Forward declarations
namespace CryoLSP {
    struct LSPDiagnostic;
}

namespace Cryo {
namespace LSP {

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
    std::unique_ptr<DocumentManager> documentManager;
    Logger& logger;
    
    ServerState state;
    
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
    
    // LSP request handlers
    JsonValue handleHover(const JsonValue& params);
    
    // LSP notification handlers
    void handleDidOpen(const JsonValue& params);
    void handleDidChange(const JsonValue& params);
    void handleDidClose(const JsonValue& params);
    
    // Diagnostic support
    void publishDiagnostics(const std::string& uri);
    void clearDiagnostics(const std::string& uri);
    JsonValue createDiagnosticNotification(const std::string& uri, const std::vector<CryoLSP::LSPDiagnostic>& diagnostics);
};

} // namespace LSP
} // namespace Cryo