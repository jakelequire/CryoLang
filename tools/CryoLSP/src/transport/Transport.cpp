#include "Transport.hpp"
#include "Logger.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <conio.h>
#include <fcntl.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/select.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace Cryo
{
    namespace LSP
    {

        // Simple, working stdin/stdout transport
        StdioTransport::StdioTransport() : active_(false), initialized_(false) {}

        StdioTransport::~StdioTransport()
        {
            shutdown();
        }

        bool StdioTransport::initialize()
        {
            if (initialized_)
            {
                return true;
            }

            // Use text mode for LSP communication - headers are text, content can contain text
            // Binary mode causes issues with getline() and mixed text/binary reading
            std::ios_base::sync_with_stdio(false);

            // DO NOT set binary mode - use text mode for proper line reading
            // LSP protocol works fine with text mode on Windows

            active_ = true;
            initialized_ = true;

            return true;
        }

        std::optional<std::string> StdioTransport::read_message()
        {
            if (!is_active())
            {
                Logger::instance().debug("Transport", "Transport not active, returning null");
                return std::nullopt;
            }

            Logger::instance().debug("Transport", "Starting to read message from stdin...");

            // Debug: Check if stdin has any data available
            if (std::cin.eof())
            {
                Logger::instance().debug("Transport", "stdin is at EOF - marking transport as inactive");
                active_ = false;
                return std::nullopt;
            }

            if (std::cin.fail())
            {
                Logger::instance().debug("Transport", "stdin is in fail state - clearing and continuing");
                std::cin.clear();
            }

            // Debug: Write a debug marker to show we're actively trying to read
            static int attempt_count = 0;
            attempt_count++;
            
            // Debug file logging disabled to reduce log spam

            // Check stdin state before reading
            if (std::cin.eof())
            {
                Logger::instance().debug("Transport", "stdin is at EOF - marking transport as inactive");
                active_ = false;
                return std::nullopt;
            }

            if (std::cin.fail())
            {
                Logger::instance().debug("Transport", "stdin is in fail state - clearing and continuing");
                std::cin.clear();
            }

            // Force flush any pending output before reading
            std::cout.flush();
            
            // Read headers using getline with explicit error checking
            std::string line;
            size_t content_length = 0;
            bool found_content_length = false;
            int header_count = 0;

            // VS Code compatibility: Check if data is available before attempting to read
            // VS Code may not send the 'initialized' notification, so we need to timeout gracefully
            
#ifdef _WIN32
            // Windows: Check if data is available on stdin
            HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
            DWORD events = 0;
            if (!GetNumberOfConsoleInputEvents(hStdin, &events) || events == 0)
            {
                // No input available, wait then timeout (reduced logging)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                // Check again after brief wait
                if (!GetNumberOfConsoleInputEvents(hStdin, &events) || events == 0)
                {
                    // Don't log every timeout to prevent spam
                    return std::nullopt;
                }
            }
#else
            // Unix: Use select() to check if data is available
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms
            
            int result = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);
            if (result <= 0)
            {
                Logger::instance().debug("Transport", "No input available on Unix stdin - timing out for VS Code compatibility");
                return std::nullopt;
            }
#endif

            while (true)
            {
                Logger::instance().debug("Transport", "=== STARTING GETLINE ATTEMPT #" + std::to_string(header_count + 1) + " ===");
                
                // Check stream state before each getline
                if (std::cin.eof())
                {
                    Logger::instance().debug("Transport", "EOF reached while reading headers");
                    break;
                }
                
                if (std::cin.fail())
                {
                    Logger::instance().debug("Transport", "Stream fail state while reading headers");
                    std::cin.clear();
                    break;
                }

                Logger::instance().debug("Transport", "About to call std::getline...");
                
                // Try to read a line
                bool getline_success = static_cast<bool>(std::getline(std::cin, line));
                
                Logger::instance().debug("Transport", "std::getline completed. Success: " + std::to_string(getline_success));
                Logger::instance().debug("Transport", "stdin state after getline: eof=" + std::to_string(std::cin.eof()) + 
                                        ", fail=" + std::to_string(std::cin.fail()) + 
                                        ", bad=" + std::to_string(std::cin.bad()) + 
                                        ", good=" + std::to_string(std::cin.good()));
                
                if (!getline_success)
                {
                    Logger::instance().debug("Transport", "getline failed - stream state: eof=" + 
                                            std::to_string(std::cin.eof()) + ", fail=" + std::to_string(std::cin.fail()));
                    Logger::instance().debug("Transport", "Breaking from header reading loop due to getline failure");
                    break;
                }

                header_count++;
                Logger::instance().debug("Transport", "Header #" + std::to_string(header_count) + ": '" + line + "'");
                Logger::instance().debug("Transport", "Line length: " + std::to_string(line.length()));

                // Remove carriage return if present (Windows line endings)
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                    Logger::instance().debug("Transport", "Removed \\r, line now: '" + line + "'");
                }

                // Empty line signals end of headers
                if (line.empty())
                {
                    Logger::instance().debug("Transport", "Found empty line - end of headers");
                    break;
                }

                // Parse Content-Length header
                if (line.find("Content-Length:") == 0)
                {
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos)
                    {
                        std::string length_str = line.substr(colon_pos + 1);
                        
                        // Trim whitespace
                        size_t start = length_str.find_first_not_of(" \t\r\n");
                        size_t end = length_str.find_last_not_of(" \t\r\n");
                        
                        if (start != std::string::npos)
                        {
                            length_str = length_str.substr(start, end - start + 1);
                            
                            try {
                                content_length = std::stoull(length_str);
                                found_content_length = true;
                                Logger::instance().debug("Transport", "Content-Length: " + std::to_string(content_length));
                            }
                            catch (const std::exception& e) {
                                Logger::instance().debug("Transport", "Failed to parse Content-Length: " + std::string(e.what()));
                                return std::nullopt;
                            }
                        }
                    }
                }

                // Safety check to prevent infinite loops
                if (header_count > 20)
                {
                    Logger::instance().debug("Transport", "Too many headers - possible protocol error");
                    return std::nullopt;
                }
            }

            Logger::instance().debug("Transport", "Finished reading headers. Found " + std::to_string(header_count) + " header lines");

            if (!found_content_length || content_length == 0)
            {
                Logger::instance().debug("Transport", "No valid Content-Length found");
                return std::nullopt;
            }

            Logger::instance().debug("Transport", "Reading content of " + std::to_string(content_length) + " bytes...");

            // Read the content with improved error checking
            std::vector<char> buffer(content_length);
            std::cin.read(buffer.data(), content_length);
            
            std::streamsize bytes_read = std::cin.gcount();
            Logger::instance().debug("Transport", "Attempted to read " + std::to_string(content_length) + 
                                    " bytes, actually read " + std::to_string(bytes_read) + " bytes");
            
            if (bytes_read != static_cast<std::streamsize>(content_length))
            {
                Logger::instance().debug("Transport", "Content read mismatch - expected: " + 
                                        std::to_string(content_length) + ", got: " + std::to_string(bytes_read));
                return std::nullopt;
            }

            // Convert to string
            std::string content(buffer.begin(), buffer.end());
            
            size_t preview_length = std::min<size_t>(content.length(), 100);
            Logger::instance().debug("Transport", "Successfully read message: " + content.substr(0, preview_length));

            return content;
        }

        bool StdioTransport::write_message(const std::string &message)
        {
            if (!is_active())
            {
                Logger::instance().debug("Transport", "Transport not active, cannot write message");
                return false;
            }

            // Validate message content before sending
            if (message.empty())
            {
                Logger::instance().error("Transport", "Attempted to send empty message");
                return false;
            }

            // Basic JSON validation
            if (message.front() != '{' || message.back() != '}')
            {
                Logger::instance().error("Transport", "Message does not appear to be valid JSON: " + message.substr(0, 100));
                return false;
            }

            // Calculate content length in bytes (UTF-8)
            size_t content_length = message.length();

            // Debug: Log before writing (to log file, not stdout)
            Logger::instance().debug("Transport", "Writing message of length: " + std::to_string(message.length()));
            Logger::instance().debug("Transport", "Message content: " + message);
            Logger::instance().debug("Transport", "Writing header: Content-Length: " + std::to_string(content_length));

            // Create LSP message with proper binary output
            // LSP spec requires EXACTLY: Content-Length: <number>\r\n\r\n<content>

            // Build the header with explicit \r\n sequences
            std::string header = "Content-Length: " + std::to_string(content_length) + "\r\n\r\n";
            
            // Use the simplest possible approach - no binary mode, no buffer manipulation
            // Just pure cout with the message
            std::string complete_message = header + message;
            
            // Simplest possible output - let the system handle everything
            std::cout << complete_message << std::flush;
            
            Logger::instance().debug("Transport", "Complete LSP message written using simple cout and flushed successfully");

            return true;
        }

        bool StdioTransport::is_active() const
        {
            return active_ && initialized_;
        }

        void StdioTransport::shutdown()
        {
            active_ = false;
        }

        // ============================================================================
        // TCP Socket Transport Implementation
        // ============================================================================

        SocketTransport::SocketTransport(int port) 
            : port_(port), server_socket_(-1), client_socket_(-1), active_(false), initialized_(false)
        {
            Logger::instance().info("SocketTransport", "Configured for port: " + std::to_string(port_));
        }

        SocketTransport::~SocketTransport()
        {
            shutdown();
        }

        bool SocketTransport::initialize()
        {
            if (initialized_)
            {
                return true;
            }

            Logger::instance().info("SocketTransport", "Initializing TCP socket transport on port " + std::to_string(port_));

            if (!setup_server_socket())
            {
                Logger::instance().error("SocketTransport", "Failed to setup server socket");
                return false;
            }

            Logger::instance().info("SocketTransport", "Server socket listening on port " + std::to_string(port_));
            Logger::instance().info("SocketTransport", "Waiting for client connection...");

            if (!accept_client_connection())
            {
                Logger::instance().error("SocketTransport", "Failed to accept client connection");
                cleanup_sockets();
                return false;
            }

            Logger::instance().info("SocketTransport", "Client connected successfully");
            initialized_ = true;
            active_ = true;
            return true;
        }

        std::optional<std::string> SocketTransport::read_message()
        {
            if (!is_active())
            {
                return std::nullopt;
            }

            Logger::instance().debug("SocketTransport", "Reading message from TCP socket...");

            // Read headers first
            size_t content_length = 0;
            if (!read_headers(content_length))
            {
                Logger::instance().debug("SocketTransport", "Failed to read headers or no data available");
                return std::nullopt;
            }

            Logger::instance().debug("SocketTransport", "Headers read, content length: " + std::to_string(content_length));

            // Read the JSON content
            auto content = read_content(content_length);
            if (content.has_value())
            {
                Logger::instance().debug("SocketTransport", "Successfully read message of " + std::to_string(content_length) + " bytes");
                return content;
            }

            Logger::instance().debug("SocketTransport", "Failed to read message content");
            return std::nullopt;
        }

        bool SocketTransport::write_message(const std::string& message)
        {
            if (!is_active())
            {
                Logger::instance().error("SocketTransport", "Cannot write message - transport not active");
                return false;
            }

            return write_response(message);
        }

        bool SocketTransport::is_active() const
        {
            return active_ && initialized_ && client_socket_ != -1;
        }

        void SocketTransport::shutdown()
        {
            Logger::instance().info("SocketTransport", "Shutting down socket transport");
            active_ = false;
            cleanup_sockets();
        }

        std::string SocketTransport::get_name() const
        {
            return "socket:" + std::to_string(port_);
        }

        bool SocketTransport::setup_server_socket()
        {
#ifdef _WIN32
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0)
            {
                Logger::instance().error("SocketTransport", "WSAStartup failed: " + std::to_string(result));
                return false;
            }
#endif

            // Create socket
            server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_socket_ == -1)
            {
                Logger::instance().error("SocketTransport", "Failed to create socket");
#ifdef _WIN32
                WSACleanup();
#endif
                return false;
            }

            // Set socket options
            int opt = 1;
#ifdef _WIN32
            if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR)
#else
            if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
#endif
            {
                Logger::instance().warn("SocketTransport", "Failed to set SO_REUSEADDR");
            }

            // Bind socket
            struct sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port_);

            if (bind(server_socket_, (struct sockaddr*)&address, sizeof(address)) < 0)
            {
                Logger::instance().error("SocketTransport", "Failed to bind socket to port " + std::to_string(port_));
                cleanup_sockets();
                return false;
            }

            // Listen for connections
            if (listen(server_socket_, 1) < 0)
            {
                Logger::instance().error("SocketTransport", "Failed to listen on socket");
                cleanup_sockets();
                return false;
            }

            return true;
        }

        bool SocketTransport::accept_client_connection()
        {
            struct sockaddr_in client_addr;
#ifdef _WIN32
            int client_addr_len = sizeof(client_addr);
#else
            socklen_t client_addr_len = sizeof(client_addr);
#endif

            client_socket_ = accept(server_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_socket_ == -1)
            {
                Logger::instance().error("SocketTransport", "Failed to accept client connection");
                return false;
            }

#ifdef _WIN32
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
#else
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
#endif
            Logger::instance().info("SocketTransport", "Client connected from " + std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port)));

            return true;
        }

        void SocketTransport::cleanup_sockets()
        {
            if (client_socket_ != -1)
            {
#ifdef _WIN32
                closesocket(client_socket_);
#else
                close(client_socket_);
#endif
                client_socket_ = -1;
            }

            if (server_socket_ != -1)
            {
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

        bool SocketTransport::read_headers(size_t& content_length)
        {
            content_length = 0;
            bool found_content_length = false;
            int header_count = 0;

            while (is_active())
            {
                std::string line;
                
                // Read line from socket
                char c;
                while (true)
                {
#ifdef _WIN32
                    int result = recv(client_socket_, &c, 1, 0);
                    if (result == SOCKET_ERROR || result == 0)
                    {
                        if (WSAGetLastError() == WSAEWOULDBLOCK)
                        {
                            // No data available yet
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }
                        Logger::instance().debug("SocketTransport", "Socket read error or connection closed");
                        return false;
                    }
#else
                    ssize_t result = recv(client_socket_, &c, 1, 0);
                    if (result <= 0)
                    {
                        if (result == 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                        {
                            Logger::instance().debug("SocketTransport", "Socket read error or connection closed");
                            return false;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
#endif
                    
                    if (c == '\n')
                        break;
                    if (c != '\r')  // Skip carriage returns
                        line += c;
                }

                header_count++;
                Logger::instance().debug("SocketTransport", "Header #" + std::to_string(header_count) + ": '" + line + "'");

                // Empty line signals end of headers
                if (line.empty())
                {
                    Logger::instance().debug("SocketTransport", "Found empty line - end of headers");
                    break;
                }

                // Parse Content-Length header
                if (line.find("Content-Length:") == 0)
                {
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos)
                    {
                        std::string length_str = line.substr(colon_pos + 1);
                        
                        // Trim whitespace
                        size_t start = length_str.find_first_not_of(" \t\r\n");
                        size_t end = length_str.find_last_not_of(" \t\r\n");
                        
                        if (start != std::string::npos)
                        {
                            length_str = length_str.substr(start, end - start + 1);
                            
                            try {
                                content_length = std::stoull(length_str);
                                found_content_length = true;
                                Logger::instance().debug("SocketTransport", "Content-Length: " + std::to_string(content_length));
                            }
                            catch (const std::exception& e) {
                                Logger::instance().error("SocketTransport", "Failed to parse Content-Length: " + std::string(e.what()));
                                return false;
                            }
                        }
                    }
                }

                // Safety check to prevent infinite loops
                if (header_count > 20)
                {
                    Logger::instance().error("SocketTransport", "Too many headers - possible protocol error");
                    return false;
                }
            }

            return found_content_length && content_length > 0;
        }

        std::optional<std::string> SocketTransport::read_content(size_t length)
        {
            if (!is_active() || length == 0)
            {
                return std::nullopt;
            }

            Logger::instance().debug("SocketTransport", "Reading content of " + std::to_string(length) + " bytes...");

            std::vector<char> buffer(length);
            size_t total_read = 0;

            while (total_read < length && is_active())
            {
                size_t remaining = length - total_read;
#ifdef _WIN32
                int result = recv(client_socket_, buffer.data() + total_read, static_cast<int>(remaining), 0);
                if (result == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    Logger::instance().error("SocketTransport", "Socket read error during content read");
                    return std::nullopt;
                }
                else if (result == 0)
                {
                    Logger::instance().error("SocketTransport", "Connection closed during content read");
                    return std::nullopt;
                }
                total_read += result;
#else
                ssize_t result = recv(client_socket_, buffer.data() + total_read, remaining, 0);
                if (result < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    Logger::instance().error("SocketTransport", "Socket read error during content read");
                    return std::nullopt;
                }
                else if (result == 0)
                {
                    Logger::instance().error("SocketTransport", "Connection closed during content read");
                    return std::nullopt;
                }
                total_read += result;
#endif
            }

            if (total_read != length)
            {
                Logger::instance().error("SocketTransport", "Content read mismatch - expected: " + std::to_string(length) + ", got: " + std::to_string(total_read));
                return std::nullopt;
            }

            std::string content(buffer.begin(), buffer.end());
            size_t preview_length = std::min<size_t>(content.length(), 100);
            Logger::instance().debug("SocketTransport", "Successfully read message: " + content.substr(0, preview_length));

            return content;
        }

        bool SocketTransport::write_response(const std::string& message)
        {
            if (!is_active() || message.empty())
            {
                return false;
            }

            // Validate message content
            if (message.front() != '{' || message.back() != '}')
            {
                Logger::instance().error("SocketTransport", "Message does not appear to be valid JSON");
                return false;
            }

            size_t content_length = message.length();
            std::string header = "Content-Length: " + std::to_string(content_length) + "\r\n\r\n";
            std::string complete_message = header + message;

            Logger::instance().debug("SocketTransport", "Writing message of length: " + std::to_string(message.length()));

            size_t total_sent = 0;
            const char* data = complete_message.c_str();
            size_t data_length = complete_message.length();

            while (total_sent < data_length && is_active())
            {
                size_t remaining = data_length - total_sent;
#ifdef _WIN32
                int result = send(client_socket_, data + total_sent, static_cast<int>(remaining), 0);
                if (result == SOCKET_ERROR)
                {
                    Logger::instance().error("SocketTransport", "Socket write error");
                    return false;
                }
                total_sent += result;
#else
                ssize_t result = send(client_socket_, data + total_sent, remaining, 0);
                if (result < 0)
                {
                    Logger::instance().error("SocketTransport", "Socket write error");
                    return false;
                }
                total_sent += result;
#endif
            }

            Logger::instance().debug("SocketTransport", "Successfully sent " + std::to_string(total_sent) + " bytes");
            return total_sent == data_length;
        }

        // ============================================================================
        // Transport Factory Implementation
        // ============================================================================

        std::unique_ptr<Transport> TransportFactory::create_stdio_transport()
        {
            return std::make_unique<StdioTransport>();
        }

        std::unique_ptr<Transport> TransportFactory::create_socket_transport(int port)
        {
            return std::make_unique<SocketTransport>(port);
        }

        TransportFactory::TransportConfig TransportFactory::parse_config(int argc, char* argv[])
        {
            TransportConfig config;
            config.type = TransportConfig::Stdio; // Default to stdio

            for (int i = 1; i < argc; ++i)
            {
                std::string arg = argv[i];
                
                if (arg == "--socket" || arg == "-s")
                {
                    config.type = TransportConfig::Socket;
                    config.socket_port = 7777; // Default port
                }
                else if (arg.starts_with("--port="))
                {
                    std::string port_str = arg.substr(7);
                    try {
                        config.socket_port = std::stoi(port_str);
                    } catch (const std::exception& e) {
                        Logger::instance().warn("TransportFactory", "Invalid port '" + port_str + "', using default 7777");
                    }
                }
                else if (arg == "--port" && i + 1 < argc)
                {
                    try {
                        config.socket_port = std::stoi(argv[++i]);
                    } catch (const std::exception& e) {
                        Logger::instance().warn("TransportFactory", "Invalid port '" + std::string(argv[i]) + "', using default 7777");
                    }
                }
            }

            return config;
        }

        std::unique_ptr<Transport> TransportFactory::create_from_config(const TransportConfig& config)
        {
            switch (config.type)
            {
                case TransportConfig::Socket:
                    Logger::instance().info("TransportFactory", "Creating socket transport on port " + std::to_string(config.socket_port));
                    return create_socket_transport(config.socket_port);
                    
                case TransportConfig::Stdio:
                default:
                    Logger::instance().info("TransportFactory", "Creating stdio transport");
                    return create_stdio_transport();
            }
        }

    } // namespace LSP
} // namespace Cryo