#pragma once

#include <string>
#include <functional>
#include <optional>
#include <memory>

namespace Cryo::LSP {

/**
 * @brief Abstract transport interface for LSP communication
 * 
 * This interface abstracts the underlying communication mechanism,
 * allowing the LSP server to work with different transports:
 * - Standard I/O (stdin/stdout) - most common
 * - TCP sockets - for debugging
 * - Named pipes - for Windows integration
 * - WebSockets - for browser-based clients
 */
class Transport {
public:
    virtual ~Transport() = default;
    
    /**
     * @brief Initialize the transport
     * @return true if initialization succeeded
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Read a complete LSP message
     * @return The message content if available, nullopt if no message or error
     */
    virtual std::optional<std::string> read_message() = 0;
    
    /**
     * @brief Write an LSP message
     * @param message The complete message to send
     * @return true if the message was sent successfully
     */
    virtual bool write_message(const std::string& message) = 0;
    
    /**
     * @brief Check if the transport is still active
     * @return true if the transport can still send/receive messages
     */
    virtual bool is_active() const = 0;
    
    /**
     * @brief Shutdown the transport gracefully
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Get a human-readable name for this transport
     */
    virtual std::string get_name() const = 0;
    
    // Event callbacks (optional)
    std::function<void(const std::string&)> on_message_received;
    std::function<void(const std::string&)> on_error;
    std::function<void()> on_disconnected;
};

/**
 * @brief Standard I/O transport for LSP communication
 * 
 * This implements the standard LSP transport using stdin/stdout.
 * Messages follow the LSP protocol format:
 * 
 * Content-Length: <length>\r\n
 * \r\n
 * <JSON content>
 */
class StdioTransport : public Transport {
public:
    StdioTransport();
    ~StdioTransport() override;
    
    bool initialize() override;
    std::optional<std::string> read_message() override;
    bool write_message(const std::string& message) override;
    bool is_active() const override;
    void shutdown() override;
    std::string get_name() const override { return "stdio"; }

private:
    bool read_headers(size_t& content_length);
    std::optional<std::string> read_content(size_t length);
    bool write_response(const std::string& content);
    
    bool active_;
    bool initialized_;
};

/**
 * @brief TCP Socket transport for LSP communication (for debugging)
 * 
 * This transport listens on a TCP port and accepts connections.
 * Useful for debugging LSP communication with tools like telnet or netcat.
 */
class SocketTransport : public Transport {
public:
    explicit SocketTransport(int port);
    ~SocketTransport() override;
    
    bool initialize() override;
    std::optional<std::string> read_message() override;
    bool write_message(const std::string& message) override;
    bool is_active() const override;
    void shutdown() override;
    std::string get_name() const override;

private:
    bool setup_server_socket();
    bool accept_client_connection();
    void cleanup_sockets();
    
    int port_;
    int server_socket_;
    int client_socket_;
    bool active_;
    bool initialized_;
};

/**
 * @brief Factory for creating transport instances
 */
class TransportFactory {
public:
    static std::unique_ptr<Transport> create_stdio_transport();
    static std::unique_ptr<Transport> create_socket_transport(int port);
    
    // Helper to determine transport type from command line arguments
    struct TransportConfig {
        enum Type { Stdio, Socket } type;
        int socket_port = 7777;
    };
    
    static TransportConfig parse_config(int argc, char* argv[]);
    static std::unique_ptr<Transport> create_from_config(const TransportConfig& config);
};

} // namespace Cryo::LSP