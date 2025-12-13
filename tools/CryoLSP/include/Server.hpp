#ifndef CRYOLSP_SERVER_HPP
#define CRYOLSP_SERVER_HPP

#include "LSPTypes.hpp"

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <iostream>
#include <sstream>

// Include necessary headers
#include "Compiler/CompilerInstance.hpp"
#include "GDM/GDM.hpp"
#include "AST/SymbolTable.hpp"

// Forward declaration
namespace CryoLSP
{
    class LSPAnalyzer;
}

namespace CryoLSP
{
    struct Diagnostic
    {
        Range range;
        std::string message;
        int severity; // 1=Error, 2=Warning, 3=Information, 4=Hint
        std::string code;
        std::string source;
    };

    // JSON-RPC message structure
    struct JsonRpcMessage
    {
        std::string jsonrpc = "2.0";
        std::string method;
        std::string id;
        std::string params;
        std::string result;
        std::string error;
        bool is_request = false;
        bool is_response = false;
        bool is_notification = false;
    };

    // LSP Server class
    class Server
    {
    private:
        Cryo::CompilerInstance *compiler;
        std::unique_ptr<LSPAnalyzer> analyzer_;
        std::unordered_map<std::string, std::string> document_cache;
        bool initialized;
        bool shutdown_requested;

        // TCP mode support
        bool tcp_mode;
        int tcp_port;
        int server_socket;
        int client_socket;
        bool debug_mode;

        // JSON-RPC handling
        std::string read_message();
        void write_message(const std::string &message);
        JsonRpcMessage parse_message(const std::string &content);
        std::string serialize_response(const JsonRpcMessage &response);

        // TCP/stdio specific methods
        std::string read_message_stdio();
        std::string read_message_tcp();
        void write_message_stdio(const std::string &message);
        void write_message_tcp(const std::string &message);

        // TCP server setup
        bool setup_tcp_server();

        // LSP method handlers
        void handle_initialize(const JsonRpcMessage &request);
        void handle_initialized(const JsonRpcMessage &notification);
        void handle_shutdown(const JsonRpcMessage &request);
        void handle_exit(const JsonRpcMessage &notification);
        void handle_text_document_did_open(const JsonRpcMessage &notification);
        void handle_text_document_did_change(const JsonRpcMessage &notification);
        void handle_text_document_did_close(const JsonRpcMessage &notification);
        void handle_text_document_completion(const JsonRpcMessage &request);
        void handle_text_document_hover(const JsonRpcMessage &request);
        void handle_text_document_definition(const JsonRpcMessage &request);
        void handle_workspace_symbol(const JsonRpcMessage &request);
        void handle_text_document_references(const JsonRpcMessage &request);
        void handle_text_document_document_symbol(const JsonRpcMessage &request);
        void handle_text_document_diagnostic(const JsonRpcMessage &request);
        void handle_cancel_request(const JsonRpcMessage &request);
        void handle_text_document_did_save(const JsonRpcMessage &notification);
        void handle_workspace_did_change_configuration(const JsonRpcMessage &notification);
        void handle_set_trace(const JsonRpcMessage &notification);

        // Helper methods
        void send_diagnostics(const std::string &uri);
        std::vector<Diagnostic> get_diagnostics_for_document(const std::string &uri);
        std::vector<CompletionItem> get_completions(const std::string &uri, const Position &position);
        Hover get_hover_info(const std::string &uri, const Position &position);
        std::vector<Location> get_definition(const std::string &uri, const Position &position);
        std::vector<SymbolInformation> get_workspace_symbols(const std::string &query);
        std::vector<Location> get_references(const std::string &uri, const Position &position);

        // Utility methods
        std::string uri_to_path(const std::string &uri);
        std::string path_to_uri(const std::string &path);
        Position offset_to_position(const std::string &text, int offset);
        int position_to_offset(const std::string &text, const Position &position);

    public:
        Server();
        ~Server();

        void run(); // Main server loop
        void process_message(const JsonRpcMessage &message);

        // Configuration
        void set_compiler_instance(Cryo::CompilerInstance *compiler_instance);
        void set_tcp_mode(bool enable_tcp, int port = 8080);
        void set_debug_mode(bool enable_debug);
    };

} // namespace CryoLSP

#endif // CRYOLSP_SERVER_HPP