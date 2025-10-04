#pragma once

#include "ProtocolHandler.hpp"
#include "Transport.hpp"
#include "Logger.hpp"
#include <memory>
#include <string>
#include <map>

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
};

} // namespace lsp
} // namespace cryo