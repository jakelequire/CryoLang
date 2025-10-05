#pragma once

#include <string>
#include <optional>
#include <memory>
#include <functional>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace Cryo::LSP {

/**
 * @brief Simple TCP Socket Transport for LSP communication
 * 
 * This provides a reliable alternative to the broken stdio transport.
 * The server listens on a TCP port and accepts one client connection.
 */
class TcpTransport {
public:
    explicit TcpTransport(int port = 7777);
    ~TcpTransport();
    
    bool initialize();
    std::optional<std::string> read_message();
    bool write_message(const std::string& message);
    bool is_active() const;
    void shutdown();
    std::string get_name() const;

private:
    bool setup_server();
    bool accept_client();
    bool read_headers(size_t& content_length);
    std::optional<std::string> read_content(size_t length);
    void cleanup();
    
    int port_;
#ifdef _WIN32
    SOCKET server_socket_;
    SOCKET client_socket_;
#else
    int server_socket_;
    int client_socket_;
#endif
    bool active_;
    bool initialized_;
    bool wsa_initialized_;
};

} // namespace Cryo::LSP