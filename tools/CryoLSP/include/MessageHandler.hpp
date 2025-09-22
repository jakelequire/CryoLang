#pragma once
#include "LSPProtocol.hpp"
#include <iostream>
#include <string>
#include <optional>
#include <memory>

namespace Cryo::LSP
{
    /**
     * @brief Handles JSON-RPC message parsing and sending over stdin/stdout
     * 
     * This class manages the low-level LSP protocol communication:
     * - Reads Content-Length headers and JSON messages from stdin
     * - Parses JSON-RPC 2.0 messages into LSPMessage structures
     * - Sends responses and notifications back via stdout
     * - Handles protocol errors and malformed messages
     */
    class MessageHandler
    {
    private:
        std::istream& _input;
        std::ostream& _output;
        bool _shutdown_requested = false;
        
        // Message parsing helpers
        std::optional<std::string> read_message();
        std::optional<LSPMessage> parse_message(const std::string& json_content);
        std::string create_response(const std::string& id, const std::string& result_json);
        std::string create_error_response(const std::string& id, int error_code, const std::string& error_message);
        std::string create_notification(const std::string& method, const std::string& params_json);
        
        // JSON helpers (we'll use a simple JSON parser for now)
        std::optional<std::string> extract_json_string(const std::string& json, const std::string& key);
        std::optional<std::string> extract_json_object(const std::string& json, const std::string& key);
        std::optional<int> extract_json_int(const std::string& json, const std::string& key);
        bool has_json_key(const std::string& json, const std::string& key);

    public:
        explicit MessageHandler(std::istream& input = std::cin, std::ostream& output = std::cout);
        ~MessageHandler() = default;

        /**
         * @brief Read and parse the next LSP message from input
         * @return Parsed message, or nullopt if no more messages or error
         */
        std::optional<LSPMessage> receive_message();

        /**
         * @brief Send a response to a request
         * @param request_id The ID from the original request
         * @param result_json JSON string containing the result
         */
        void send_response(const std::string& request_id, const std::string& result_json);

        /**
         * @brief Send an error response to a request
         * @param request_id The ID from the original request
         * @param error_code LSP error code
         * @param error_message Human-readable error description
         */
        void send_error(const std::string& request_id, int error_code, const std::string& error_message);

        /**
         * @brief Send a notification (no response expected)
         * @param method LSP method name (e.g., "textDocument/publishDiagnostics")
         * @param params_json JSON string containing parameters
         */
        void send_notification(const std::string& method, const std::string& params_json);

        /**
         * @brief Check if shutdown has been requested
         */
        bool is_shutdown_requested() const { return _shutdown_requested; }

        /**
         * @brief Request shutdown of the message handler
         */
        void request_shutdown() { _shutdown_requested = true; }

        // Convenience methods for common LSP messages
        void send_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);
        void send_log_message(const std::string& message, int type = 3); // 3 = Info
        void send_show_message(const std::string& message, int type = 3);
    };

} // namespace Cryo::LSP
