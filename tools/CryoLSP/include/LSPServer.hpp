#pragma once

#include "Compiler/CompilerInstance.hpp"
#include "Types/TypeChecker.hpp"
#include "GDM/GDM.hpp"
#include "Utils/Logger.hpp"
#include "LSPTypes.hpp"
#include "DocumentManager.hpp"
#include "SymbolProvider.hpp"
#include "DiagnosticsProvider.hpp"
#include "HoverProvider.hpp"
#include "CompletionProvider.hpp"

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace CryoLSP
{

    /**
     * @brief High-performance LSP Server for CryoLang
     *
     * Production-quality Language Server Protocol implementation with:
     * - TCP-based communication for reliability
     * - Full integration with CryoLang compiler pipeline
     * - Advanced symbol resolution and type information
     * - Real-time diagnostics and error reporting
     * - Professional hover information with syntax highlighting
     * - Intelligent code completion
     * - Multi-document workspace support
     */
    class LSPServer
    {
    public:
        struct Config
        {
            int port = 7777;
            std::string host = "127.0.0.1";
            bool enable_debug_logging = false;
            bool enable_trace_logging = false;
            size_t max_connections = 10;
            int socket_timeout_ms = 30000;
            bool enable_diagnostics = true;
            bool enable_hover = true;
            bool enable_completion = true;
            bool enable_goto_definition = true;
        };

        LSPServer();
        LSPServer(const Config &config);
        ~LSPServer();

        // Server lifecycle
        bool initialize();
        bool start();
        void stop();
        void shutdown();
        bool is_running() const { return _running.load(); }

        // Configuration
        void set_config(const Config &config) { _config = config; }
        const Config &get_config() const { return _config; }

    private:
        // Core components
        Config _config;
        std::atomic<bool> _running{false};
        std::atomic<bool> _shutdown_requested{false};

        // Networking
        socket_t _server_socket;
        std::thread _server_thread;
        std::vector<std::thread> _client_threads;
        std::mutex _clients_mutex;

        // LSP providers
        std::unique_ptr<DocumentManager> _document_manager;
        std::unique_ptr<SymbolProvider> _symbol_provider;
        std::unique_ptr<DiagnosticsProvider> _diagnostics_provider;
        std::unique_ptr<HoverProvider> _hover_provider;
        std::unique_ptr<CompletionProvider> _completion_provider;

        // Compiler integration
        std::unique_ptr<Cryo::CompilerInstance> _compiler;
        std::mutex _compiler_mutex;

        // Message handling
        using MessageHandler = std::function<LSPResponse(const LSPRequest &, socket_t)>;
        std::unordered_map<std::string, MessageHandler> _message_handlers;

        // Network operations
        bool setup_socket();
        void cleanup_socket();
        void server_loop();
        void handle_client(socket_t client_socket);

        // Message processing
        void setup_message_handlers();
        std::string read_message(socket_t socket);
        bool send_message(socket_t socket, const std::string &message);
        LSPMessage parse_message(const std::string &raw_message);
        std::string serialize_response(const LSPResponse &response);

        // LSP method handlers
        LSPResponse handle_initialize(const LSPRequest &request, socket_t socket);
        LSPResponse handle_initialized(const LSPRequest &request, socket_t socket);
        LSPResponse handle_shutdown(const LSPRequest &request, socket_t socket);
        LSPResponse handle_exit(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_did_open(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_did_change(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_did_save(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_did_close(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_hover(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_completion(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_definition(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_references(const LSPRequest &request, socket_t socket);
        LSPResponse handle_text_document_document_symbol(const LSPRequest &request, socket_t socket);
        LSPResponse handle_workspace_symbol(const LSPRequest &request, socket_t socket);

        // Compiler integration
        bool compile_document(const std::string &uri, const std::string &content);
        void publish_diagnostics(const std::string &uri, socket_t socket);

        // Utility
        void log_debug(const std::string &message);
        void log_info(const std::string &message);
        void log_error(const std::string &message);
        std::string uri_to_path(const std::string &uri);
        std::string path_to_uri(const std::string &path);
    };

} // namespace CryoLSP
