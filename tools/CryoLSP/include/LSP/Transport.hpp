#pragma once

#include <string>
#include <optional>
#include <fstream>
#include <mutex>
#include <cjson/json.hpp>

namespace CryoLSP
{

    /**
     * @brief JSON-RPC over stdio transport layer
     *
     * Handles reading/writing LSP messages with Content-Length framing.
     * All JSON-RPC output goes to stdout.
     * All logging goes to stderr + log file.
     */
    class Transport
    {
    public:
        Transport() = default;
        ~Transport() = default;

        // Read a JSON-RPC message from stdin
        // Returns nullopt on EOF or read error
        std::optional<cjson::JsonValue> readMessage();

        // Send a JSON-RPC response (has id)
        void sendResponse(const cjson::JsonValue &id, const cjson::JsonValue &result);

        // Send a JSON-RPC error response
        void sendError(const cjson::JsonValue &id, int code, const std::string &message);

        // Send a JSON-RPC notification (no id)
        void sendNotification(const std::string &method, const cjson::JsonValue &params);

        // ================================================================
        // Logging
        // ================================================================

        // Initialize the log file (truncates existing). Call once at startup.
        static void initLogFile(const std::string &path);

        // Log a message to stderr + log file (if open)
        static void log(const std::string &message);

    private:
        // Write a raw JSON-RPC message to stdout with Content-Length header
        void writeMessage(const cjson::JsonValue &msg);

        // Read exactly n bytes from stdin
        std::string readBytes(size_t count);

        // Read a line from stdin (up to \n)
        std::string readLine();

        // Log file state
        static std::ofstream _log_file;
        static std::mutex _log_mutex;
    };

    // JSON-RPC error codes
    namespace ErrorCodes
    {
        constexpr int ParseError = -32700;
        constexpr int InvalidRequest = -32600;
        constexpr int MethodNotFound = -32601;
        constexpr int InvalidParams = -32602;
        constexpr int InternalError = -32603;
        constexpr int ServerNotInitialized = -32002;
        constexpr int RequestCancelled = -32800;
    }

} // namespace CryoLSP
