#include "Transport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace Cryo::LSP {

// ================================================================
// StdioTransport Implementation
// ================================================================

StdioTransport::StdioTransport() 
    : active_(false), initialized_(false) {
}

StdioTransport::~StdioTransport() {
    shutdown();
}

bool StdioTransport::initialize() {
    if (initialized_) {
        return true;
    }
    
    LOG_INFO("StdioTransport", "Initializing stdio transport...");
    
    // Configure stdio for LSP communication
    // Disable buffering to ensure immediate message delivery
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);
    
    // Set binary mode for stdin/stdout (important on Windows)
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    
    // Test that we can read/write
    if (!std::cin.good() || !std::cout.good()) {
        LOG_ERROR("StdioTransport", "stdin or stdout is not available");
        return false;
    }
    
    active_ = true;
    initialized_ = true;
    
    LOG_INFO("StdioTransport", "Stdio transport initialized successfully");
    return true;
}

std::optional<std::string> StdioTransport::read_message() {
    if (!is_active()) {
        LOG_DEBUG("StdioTransport", "Transport not active, returning nullopt");
        return std::nullopt;
    }
    
    LOG_DEBUG("StdioTransport", "Reading LSP message...");
    
    // Read headers
    size_t content_length = 0;
    if (!read_headers(content_length)) {
        LOG_ERROR("StdioTransport", "Failed to read message headers");
        return std::nullopt;
    }
    
    LOG_DEBUG("StdioTransport", "Message content length: {}", content_length);
    
    // Read content
    auto content = read_content(content_length);
    if (!content) {
        LOG_ERROR("StdioTransport", "Failed to read message content");
        return std::nullopt;
    }
    
    LOG_DEBUG("StdioTransport", "Successfully read message: {} bytes", content->length());
    return content;
}

bool StdioTransport::write_message(const std::string& message) {
    if (!is_active()) {
        LOG_ERROR("StdioTransport", "Cannot write - transport not active");
        return false;
    }
    
    return write_response(message);
}

bool StdioTransport::is_active() const {
    // Only check our internal state, not stdio state
    // stdio state will be checked when we actually try to read/write
    return active_ && initialized_;
}

void StdioTransport::shutdown() {
    if (active_) {
        LOG_INFO("StdioTransport", "Shutting down stdio transport");
        active_ = false;
    }
}

bool StdioTransport::read_headers(size_t& content_length) {
    content_length = 0;
    std::string line;
    
    LOG_DEBUG("StdioTransport", "Starting to read headers...");
    
    while (true) {
        // Check if input is available before trying to read
        if (!std::cin.good()) {
            LOG_ERROR("StdioTransport", "stdin is not in good state");
            active_ = false;
            return false;
        }
        
        // Simple blocking approach - just use getline
        LOG_DEBUG("StdioTransport", "Calling std::getline...");
        
        if (!std::getline(std::cin, line)) {
            if (std::cin.eof()) {
                LOG_INFO("StdioTransport", "EOF reached - client disconnected");
                active_ = false;
                return false;
            }
            LOG_ERROR("StdioTransport", "Error reading from stdin");
            active_ = false;
            return false;
        }
        
        LOG_DEBUG("StdioTransport", "Successfully read line: '{}'", line);
        
        // Remove ALL trailing whitespace including \r\n
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        
        LOG_DEBUG("StdioTransport", "Header line after cleanup: '{}'", line);
        
        // Empty line indicates end of headers
        if (line.empty()) {
            if (content_length > 0) {
                LOG_DEBUG("StdioTransport", "Headers complete, content length: {}", content_length);
                return true;
            } else {
                LOG_ERROR("StdioTransport", "No Content-Length header found");
                return false;
            }
        }
        
        // Parse Content-Length header
        const std::string content_length_prefix = "Content-Length:";
        if (line.substr(0, content_length_prefix.length()) == content_length_prefix) {
            std::string length_str = line.substr(content_length_prefix.length());
            
            // Trim whitespace
            length_str.erase(0, length_str.find_first_not_of(" \t"));
            length_str.erase(length_str.find_last_not_of(" \t\r\n") + 1);
            
            try {
                content_length = std::stoull(length_str);
                LOG_DEBUG("StdioTransport", "Parsed Content-Length: {}", content_length);
            } catch (const std::exception& e) {
                LOG_ERROR("StdioTransport", "Invalid Content-Length: '{}', error: {}", length_str, e.what());
                return false;
            }
        }
        // Ignore other headers (Content-Type, etc.)
    }
}

std::optional<std::string> StdioTransport::read_content(size_t length) {
    if (length == 0) {
        return std::string{};
    }
    
    LOG_DEBUG("StdioTransport", "Reading content: {} bytes", length);
    
    std::string content;
    content.reserve(length);
    
    // Simple approach - read all content at once
    std::vector<char> buffer(length);
    
    LOG_DEBUG("StdioTransport", "Reading {} bytes of content...", length);
    
    if (!std::cin.read(buffer.data(), length)) {
        if (std::cin.eof()) {
            LOG_ERROR("StdioTransport", "EOF while reading content");
            active_ = false;
            return std::nullopt;
        }
        LOG_ERROR("StdioTransport", "Error reading content");
        return std::nullopt;
    }
    
    content.assign(buffer.begin(), buffer.end());
    LOG_DEBUG("StdioTransport", "Successfully read {} bytes of content", content.length());
    return content;
}

bool StdioTransport::write_response(const std::string& content) {
    // Write LSP message format: headers + content
    std::string response = "Content-Length: " + std::to_string(content.length()) + "\r\n";
    response += "\r\n";
    response += content;
    
    LOG_DEBUG("StdioTransport", "Writing response: {} bytes total", response.length());
    
    std::cout << response;
    std::cout.flush();
    
    if (!std::cout.good()) {
        LOG_ERROR("StdioTransport", "Failed to write response - stdout error");
        active_ = false;
        return false;
    }
    
    LOG_DEBUG("StdioTransport", "Response written successfully");
    return true;
}

// ================================================================
// SocketTransport Implementation
// ================================================================

SocketTransport::SocketTransport(int port)
    : port_(port), server_socket_(-1), client_socket_(-1), 
      active_(false), initialized_(false) {
}

SocketTransport::~SocketTransport() {
    shutdown();
}

bool SocketTransport::initialize() {
    if (initialized_) {
        return true;
    }
    
    LOG_INFO("SocketTransport", "Initializing socket transport on port {}", port_);
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("SocketTransport", "Failed to initialize Winsock");
        return false;
    }
#endif
    
    if (!setup_server_socket()) {
        cleanup_sockets();
        return false;
    }
    
    LOG_INFO("SocketTransport", "Waiting for client connection on port {}", port_);
    
    if (!accept_client_connection()) {
        cleanup_sockets();
        return false;
    }
    
    active_ = true;
    initialized_ = true;
    
    LOG_INFO("SocketTransport", "Socket transport initialized successfully");
    return true;
}

std::optional<std::string> SocketTransport::read_message() {
    if (!is_active()) {
        return std::nullopt;
    }
    
    // For socket transport, we need to implement LSP message reading over TCP
    // This is a simplified implementation - in production you'd want more robust buffering
    
    // Read headers line by line
    std::string headers;
    char buffer[1024];
    
    while (true) {
        int bytes_received = recv(client_socket_, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            LOG_ERROR("SocketTransport", "Failed to receive data from client");
            active_ = false;
            return std::nullopt;
        }
        
        buffer[bytes_received] = '\0';
        headers += buffer;
        
        // Look for the end of headers (\r\n\r\n)
        size_t header_end = headers.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // We have complete headers, now read the content
            // Parse Content-Length from headers
            size_t content_length = 0;
            std::istringstream header_stream(headers);
            std::string line;
            
            while (std::getline(header_stream, line)) {
                if (line.substr(0, 15) == "Content-Length:") {
                    std::string length_str = line.substr(15);
                    length_str.erase(0, length_str.find_first_not_of(" \t"));
                    length_str.erase(length_str.find_last_not_of(" \t\r\n") + 1);
                    content_length = std::stoull(length_str);
                    break;
                }
            }
            
            if (content_length == 0) {
                LOG_ERROR("SocketTransport", "No Content-Length found in headers");
                return std::nullopt;
            }
            
            // Read the JSON content
            std::string content;
            content.reserve(content_length);
            
            // Check if we already have some content after headers
            size_t content_start = header_end + 4;
            if (headers.length() > content_start) {
                content = headers.substr(content_start);
            }
            
            // Read remaining content
            while (content.length() < content_length) {
                bytes_received = recv(client_socket_, buffer, 
                    std::min(sizeof(buffer) - 1, content_length - content.length()), 0);
                if (bytes_received <= 0) {
                    LOG_ERROR("SocketTransport", "Failed to receive content from client");
                    active_ = false;
                    return std::nullopt;
                }
                buffer[bytes_received] = '\0';
                content += buffer;
            }
            
            LOG_DEBUG("SocketTransport", "Received message: {} bytes", content.length());
            return content;
        }
        
        // Prevent infinite header accumulation
        if (headers.length() > 8192) {
            LOG_ERROR("SocketTransport", "Headers too large");
            return std::nullopt;
        }
    }
}

bool SocketTransport::write_message(const std::string& message) {
    if (!is_active()) {
        return false;
    }
    
    std::string response = "Content-Length: " + std::to_string(message.length()) + "\r\n";
    response += "\r\n";
    response += message;
    
    int bytes_sent = send(client_socket_, response.c_str(), response.length(), 0);
    if (bytes_sent != static_cast<int>(response.length())) {
        LOG_ERROR("SocketTransport", "Failed to send complete message");
        active_ = false;
        return false;
    }
    
    LOG_DEBUG("SocketTransport", "Sent message: {} bytes", response.length());
    return true;
}

bool SocketTransport::is_active() const {
    return active_ && initialized_ && client_socket_ != -1;
}

void SocketTransport::shutdown() {
    if (active_) {
        LOG_INFO("SocketTransport", "Shutting down socket transport");
        cleanup_sockets();
        active_ = false;
    }
}

std::string SocketTransport::get_name() const {
    return "socket:" + std::to_string(port_);
}

bool SocketTransport::setup_server_socket() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        LOG_ERROR("SocketTransport", "Failed to create server socket");
        return false;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&server_addr), 
             sizeof(server_addr)) < 0) {
        LOG_ERROR("SocketTransport", "Failed to bind socket to port {}", port_);
        return false;
    }
    
    if (listen(server_socket_, 1) < 0) {
        LOG_ERROR("SocketTransport", "Failed to listen on socket");
        return false;
    }
    
    return true;
}

bool SocketTransport::accept_client_connection() {
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    
    client_socket_ = accept(server_socket_, 
                           reinterpret_cast<struct sockaddr*>(&client_addr), 
                           &client_len);
    
    if (client_socket_ < 0) {
        LOG_ERROR("SocketTransport", "Failed to accept client connection");
        return false;
    }
    
    LOG_INFO("SocketTransport", "Client connected successfully");
    return true;
}

void SocketTransport::cleanup_sockets() {
    if (client_socket_ != -1) {
#ifdef _WIN32
        closesocket(client_socket_);
#else
        close(client_socket_);
#endif
        client_socket_ = -1;
    }
    
    if (server_socket_ != -1) {
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = -1;
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
}

// ================================================================
// TransportFactory Implementation
// ================================================================

std::unique_ptr<Transport> TransportFactory::create_stdio_transport() {
    return std::make_unique<StdioTransport>();
}

std::unique_ptr<Transport> TransportFactory::create_socket_transport(int port) {
    return std::make_unique<SocketTransport>(port);
}

TransportFactory::TransportConfig TransportFactory::parse_config(int argc, char* argv[]) {
    TransportConfig config;
    config.type = TransportConfig::Stdio; // Default
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--socket" && i + 1 < argc) {
            config.type = TransportConfig::Socket;
            config.socket_port = std::atoi(argv[i + 1]);
            break;
        } else if (arg.find("--socket=") == 0) {
            config.type = TransportConfig::Socket;
            config.socket_port = std::atoi(arg.substr(9).c_str());
            break;
        }
    }
    
    return config;
}

std::unique_ptr<Transport> TransportFactory::create_from_config(const TransportConfig& config) {
    switch (config.type) {
        case TransportConfig::Socket:
            return create_socket_transport(config.socket_port);
        case TransportConfig::Stdio:
        default:
            return create_stdio_transport();
    }
}

} // namespace Cryo::LSP