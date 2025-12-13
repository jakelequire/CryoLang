#include "Server.hpp"
#include "JsonUtils.hpp"
#include "LSPAnalyzer.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "GDM/GDM.hpp"
#include "AST/SymbolTable.hpp"
#include "Utils/Logger.hpp"

#include <iostream>
#include <sstream>
#include <regex>
#include <fstream>
#include <cerrno>

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

namespace CryoLSP
{

    Server::Server() : compiler(nullptr), initialized(false), shutdown_requested(false),
                       tcp_mode(false), tcp_port(8080), server_socket(-1), client_socket(-1), debug_mode(false)
    {
        // Constructor - server will be configured via set_compiler_instance
        analyzer_ = std::make_unique<LSPAnalyzer>();
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Server initialized with LSPAnalyzer");

#ifdef _WIN32
        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            std::cerr << "WSAStartup failed with error: " << result << std::endl;
        }
#endif
    }

    Server::~Server()
    {
        // Close sockets if they're open
        if (client_socket != -1)
        {
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
        }
        if (server_socket != -1)
        {
#ifdef _WIN32
            closesocket(server_socket);
#else
            close(server_socket);
#endif
        }

#ifdef _WIN32
        // Cleanup Winsock
        WSACleanup();
#endif
    }

    void Server::set_compiler_instance(Cryo::CompilerInstance *compiler_instance)
    {
        compiler = compiler_instance;
    }

    void Server::set_tcp_mode(bool enable_tcp, int port)
    {
        tcp_mode = enable_tcp;
        tcp_port = port;
    }

    void Server::set_debug_mode(bool enable_debug)
    {
        debug_mode = enable_debug;
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Server debug mode set to: " + std::string(enable_debug ? "enabled" : "disabled"));
    }

    void Server::run()
    {
        if (!compiler)
        {
            std::string error_msg = "Error: No compiler instance configured";
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return;
        }

        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Starting LSP server run loop");

        // Setup TCP server if in TCP mode
        if (tcp_mode)
        {
            if (!setup_tcp_server())
            {
                std::string error_msg = "Failed to setup TCP server";
                Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
                std::cerr << error_msg << std::endl;
                return;
            }
        }
        else
        {
            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Running in stdio mode");
        }

        // Main server loop
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Entering main server loop");
        while (!shutdown_requested)
        {
            try
            {
                if (debug_mode)
                {
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Waiting for LSP message...");
                }

                std::string message_content = read_message();
                if (message_content.empty())
                {
                    Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Empty message received, connection likely closed");
                    break; // EOF or connection closed
                }

                if (debug_mode)
                {
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Received message (" + std::to_string(message_content.length()) + " bytes): " + message_content.substr(0, 200) + (message_content.length() > 200 ? "..." : ""));
                }

                JsonRpcMessage message = parse_message(message_content);
                process_message(message);
            }
            catch (const std::exception &e)
            {
                std::string error_msg = "Error processing message: " + std::string(e.what());
                Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
                // Continue processing other messages
            }
        }

        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Server loop ended, shutdown_requested=" + std::string(shutdown_requested ? "true" : "false"));
    }

    std::string Server::read_message()
    {
        if (tcp_mode)
        {
            // TCP mode - read from socket
            return read_message_tcp();
        }
        else
        {
            // stdio mode - read from stdin
            return read_message_stdio();
        }
    }

    std::string Server::read_message_stdio()
    {
        std::string line;
        int content_length = 0;

        // Read headers
        while (std::getline(std::cin, line))
        {
            // Remove \r if present
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            // Empty line indicates end of headers
            if (line.empty())
            {
                break;
            }

            if (line.substr(0, 16) == "Content-Length: ")
            {
                try
                {
                    content_length = std::stoi(line.substr(16));
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
                    return "";
                }
            }
        }

        // Check if we reached EOF during header reading
        if (std::cin.eof())
        {
            return ""; // Connection closed
        }

        if (content_length == 0)
        {
            std::cerr << "No Content-Length header found" << std::endl;
            return ""; // No valid message
        }

        // Read the JSON content
        std::string content;
        content.resize(content_length);

        if (!std::cin.read(&content[0], content_length))
        {
            if (std::cin.eof())
            {
                return ""; // Connection closed
            }
            std::cerr << "Error reading message content" << std::endl;
            return "";
        }

        return content;
    }

    std::string Server::read_message_tcp()
    {
        // Read headers from TCP socket
        std::string headers;
        char buffer[1024];
        int content_length = 0;

        // Read until we find \r\n\r\n
        while (headers.find("\r\n\r\n") == std::string::npos)
        {
            int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0)
            {
                return ""; // Connection closed or error
            }
            buffer[bytes_received] = '\0';
            headers += buffer;
        }

        // Parse Content-Length from headers
        size_t content_length_pos = headers.find("Content-Length: ");
        if (content_length_pos != std::string::npos)
        {
            size_t start = content_length_pos + 16;
            size_t end = headers.find("\r\n", start);
            if (end != std::string::npos)
            {
                try
                {
                    content_length = std::stoi(headers.substr(start, end - start));
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
                    return "";
                }
            }
        }

        if (content_length == 0)
        {
            std::cerr << "No Content-Length header found" << std::endl;
            return "";
        }

        // Check if we already have some content after headers
        size_t header_end = headers.find("\r\n\r\n") + 4;
        std::string content = headers.substr(header_end);

        // Read remaining content if needed
        while (content.length() < content_length)
        {
            int bytes_needed = content_length - content.length();
            int bytes_to_read = std::min(bytes_needed, (int)sizeof(buffer) - 1);
            int bytes_received = recv(client_socket, buffer, bytes_to_read, 0);
            if (bytes_received <= 0)
            {
                return ""; // Connection closed or error
            }
            buffer[bytes_received] = '\0';
            content += std::string(buffer, bytes_received);
        }

        // Trim to exact content length
        content = content.substr(0, content_length);

        return content;
    }

    bool Server::setup_tcp_server()
    {
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Setting up TCP server on port " + std::to_string(tcp_port));

        // Create socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0)
        {
            std::string error_msg = "Error creating socket: errno " + std::to_string(errno);
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return false;
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Socket created successfully");

        // Allow socket reuse
        int opt = 1;
#ifdef _WIN32
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
        {
#else
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
#endif
            std::string error_msg = "Error setting socket options: errno " + std::to_string(errno);
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return false;
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Socket options set successfully");

        // Bind to address
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(tcp_port);

        if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            std::string error_msg = "Error binding socket to port " + std::to_string(tcp_port) + ": errno " + std::to_string(errno);
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return false;
        }

        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Socket bound to port " + std::to_string(tcp_port));

        // Start listening
        if (listen(server_socket, 1) < 0)
        {
            std::string error_msg = "Error listening on socket: errno " + std::to_string(errno);
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return false;
        }

        // Log to both stdout (for VS Code extension) and logger
        std::string ready_msg = "LSP server listening on port " + std::to_string(tcp_port);
        std::cout << ready_msg << std::endl;
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, ready_msg);

        // Accept connection
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Waiting for client connection...");
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket < 0)
        {
            std::string error_msg = "Error accepting client connection: errno " + std::to_string(errno);
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, error_msg);
            std::cerr << error_msg << std::endl;
            return false;
        }

        // Log successful connection
        std::string client_connected_msg = "Client connected successfully";
        std::cout << client_connected_msg << std::endl;
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, client_connected_msg);
        return true;
    }

    void Server::write_message(const std::string &message)
    {
        if (tcp_mode)
        {
            write_message_tcp(message);
        }
        else
        {
            write_message_stdio(message);
        }

        // Debug logging
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Sent LSP response (" + std::to_string(message.length()) + " bytes): " + message);
    }

    void Server::write_message_stdio(const std::string &message)
    {
        // Send exactly Content-Length: XX\r\n\r\nJSON with no extra newlines
        std::cout << "Content-Length: " << message.length() << "\r\n\r\n"
                  << message << std::flush;
    }

    void Server::write_message_tcp(const std::string &message)
    {
        // Create HTTP-like response
        std::string response = "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n" + message;

        // Send via TCP socket
        int bytes_sent = 0;
        int total_bytes = response.length();

        while (bytes_sent < total_bytes)
        {
            int result = send(client_socket, response.c_str() + bytes_sent, total_bytes - bytes_sent, 0);
            if (result < 0)
            {
                std::cerr << "Error sending message via TCP" << std::endl;
                return;
            }
            bytes_sent += result;
        }
    }

    JsonRpcMessage Server::parse_message(const std::string &content)
    {
        JsonRpcMessage message;

        // Extract basic JSON-RPC fields using our simple JSON parser
        message.jsonrpc = JsonUtils::extract_json_value(content, "jsonrpc");
        message.method = JsonUtils::extract_json_value(content, "method");
        message.id = JsonUtils::extract_json_value(content, "id");

        // Determine message type
        if (!message.method.empty() && !message.id.empty())
        {
            message.is_request = true;
        }
        else if (!message.method.empty())
        {
            message.is_notification = true;
        }
        else if (!message.id.empty())
        {
            message.is_response = true;
        }

        // Extract params - find the params field and extract its full JSON value
        size_t params_pos = content.find("\"params\":");
        if (params_pos != std::string::npos)
        {
            size_t start_pos = content.find_first_of("{[", params_pos);
            if (start_pos != std::string::npos)
            {
                char open_char = content[start_pos];
                char close_char = (open_char == '{') ? '}' : ']';

                int bracket_count = 1;
                size_t end_pos = start_pos + 1;

                // Find matching closing bracket, handling nested brackets
                while (end_pos < content.length() && bracket_count > 0)
                {
                    if (content[end_pos] == open_char)
                    {
                        bracket_count++;
                    }
                    else if (content[end_pos] == close_char)
                    {
                        bracket_count--;
                    }
                    end_pos++;
                }

                if (bracket_count == 0)
                {
                    message.params = content.substr(start_pos, end_pos - start_pos);
                }
            }
        }

        return message;
    }

    std::string Server::serialize_response(const JsonRpcMessage &response)
    {
        std::stringstream ss;
        ss << "{\"jsonrpc\":\"" << response.jsonrpc << "\"";

        if (!response.id.empty())
        {
            // ID should be a number, not a string
            ss << ",\"id\":" << response.id;
        }

        if (!response.result.empty())
        {
            ss << ",\"result\":" << response.result;
        }
        else if (!response.error.empty())
        {
            ss << ",\"error\":" << response.error;
        }

        ss << "}";
        return ss.str();
    }

    void Server::process_message(const JsonRpcMessage &message)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing message - Method: '" + message.method + "', ID: '" + message.id + "', Type: " +
                                                                    (message.is_request ? "request" : message.is_notification ? "notification"
                                                                                                                              : "response"));

        try
        {
            if (message.is_request)
            {
                // Handle requests
                if (message.method == "initialize")
                {
                    handle_initialize(message);
                }
                else if (message.method == "shutdown")
                {
                    handle_shutdown(message);
                }
                else if (message.method == "textDocument/completion")
                {
                    handle_text_document_completion(message);
                }
                else if (message.method == "textDocument/hover")
                {
                    handle_text_document_hover(message);
                }
                else if (message.method == "textDocument/definition")
                {
                    handle_text_document_definition(message);
                }
                else if (message.method == "workspace/symbol")
                {
                    handle_workspace_symbol(message);
                }
                else if (message.method == "textDocument/references")
                {
                    handle_text_document_references(message);
                }
                else if (message.method == "textDocument/documentSymbol")
                {
                    handle_text_document_document_symbol(message);
                }
                else if (message.method == "textDocument/diagnostic")
                {
                    handle_text_document_diagnostic(message);
                }
                else if (message.method == "$/cancelRequest")
                {
                    handle_cancel_request(message);
                }
                else
                {
                    // Unknown request method
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Unknown request method: " + message.method);
                    JsonRpcMessage error_response;
                    error_response.id = message.id;
                    error_response.error = "{\"code\":-32601,\"message\":\"Method not found: " + message.method + "\"}";
                    write_message(serialize_response(error_response));
                }
            }
            else if (message.is_notification)
            {
                // Handle notifications
                if (message.method == "initialized")
                {
                    handle_initialized(message);
                }
                else if (message.method == "exit")
                {
                    handle_exit(message);
                }
                else if (message.method == "textDocument/didOpen")
                {
                    handle_text_document_did_open(message);
                }
                else if (message.method == "textDocument/didChange")
                {
                    handle_text_document_did_change(message);
                }
                else if (message.method == "textDocument/didClose")
                {
                    handle_text_document_did_close(message);
                }
                else if (message.method == "textDocument/didSave")
                {
                    handle_text_document_did_save(message);
                }
                else if (message.method == "workspace/didChangeConfiguration")
                {
                    handle_workspace_did_change_configuration(message);
                }
                else if (message.method == "$/setTrace")
                {
                    handle_set_trace(message);
                }
                else
                {
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Unknown notification method: " + message.method);
                }
                // Notifications don't require responses
            }
        }
        catch (const std::exception &e)
        {
            if (message.is_request)
            {
                // Send error response
                JsonRpcMessage error_response;
                error_response.id = message.id;
                error_response.error = "{\"code\":-32603,\"message\":\"Internal error: " + std::string(e.what()) + "\"}";
                write_message(serialize_response(error_response));
            }
        }
    }

    void Server::handle_initialize(const JsonRpcMessage &request)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing initialize request with ID: " + request.id);

        // Comprehensive LSP capabilities
        std::string result = R"({
            "capabilities": {
                "textDocumentSync": {
                    "openClose": true,
                    "change": 2,
                    "save": {
                        "includeText": true
                    }
                },
                "hoverProvider": true,
                "completionProvider": {
                    "triggerCharacters": [".", "::", "->"]
                },
                "definitionProvider": true,
                "referencesProvider": true,
                "workspaceSymbolProvider": true,
                "documentSymbolProvider": true,
                "diagnosticProvider": {
                    "interFileDependencies": true,
                    "workspaceDiagnostics": true
                }
            },
            "serverInfo": {
                "name": "CryoLSP",
                "version": "0.1.0"
            }
        })";

        JsonRpcMessage response;
        response.jsonrpc = "2.0";
        response.id = request.id;
        response.result = result;

        std::string serialized = serialize_response(response);
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Sending initialize response with full capabilities");
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Initialize response: " + serialized);
        write_message(serialized);
    }

    void Server::handle_initialized(const JsonRpcMessage &notification)
    {
        initialized = true;
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "LSP server initialized successfully - ready to receive requests");
    }

    void Server::handle_shutdown(const JsonRpcMessage &request)
    {
        shutdown_requested = true;

        JsonRpcMessage response;
        response.id = request.id;
        response.result = "null";

        write_message(serialize_response(response));
    }

    void Server::handle_exit(const JsonRpcMessage &notification)
    {
        std::exit(shutdown_requested ? 0 : 1);
    }

    void Server::handle_text_document_did_open(const JsonRpcMessage &notification)
    {
        try
        {
            // Extract URI and text with better error handling
            std::string uri = JsonUtils::extract_nested_value(notification.params, "textDocument.uri");
            std::string text = JsonUtils::extract_nested_value(notification.params, "textDocument.text");

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                           "Document opened: " + uri + " (" + std::to_string(text.length()) + " chars)");

            // If text extraction failed, try direct JSON parsing
            if (text.empty() && notification.params.find("textDocument") != std::string::npos)
            {
                // Look for text field more robustly
                std::regex text_regex("\"text\"\\s*:\\s*\"([^\"\\\\]*(\\\\.[^\"\\\\]*)*)\"");
                std::smatch text_match;
                if (std::regex_search(notification.params, text_match, text_regex))
                {
                    text = text_match[1].str();
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Extracted text via regex: " + std::to_string(text.length()) + " chars");
                }
            }

            if (!uri.empty())
            {
                document_cache[uri] = text;

                // Update the analyzer with the new document - but safely
                try
                {
                    analyzer_->open_document(uri, text);
                    Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Document processing completed safely: " + uri);
                }
                catch (const std::exception &e)
                {
                    Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error in analyzer->open_document: " + std::string(e.what()));
                }

                send_diagnostics(uri);
            }
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error in handle_text_document_did_open: " + std::string(e.what()));
        }
    }

    void Server::handle_text_document_did_change(const JsonRpcMessage &notification)
    {
        std::string uri = JsonUtils::extract_nested_value(notification.params, "textDocument.uri");

        // For full document sync (textDocumentSync: 1), we get the full text
        std::string text = JsonUtils::extract_nested_value(notification.params, "contentChanges.0.text");

        if (!uri.empty() && !text.empty())
        {
            document_cache[uri] = text;
            send_diagnostics(uri);
        }
    }

    void Server::handle_text_document_did_close(const JsonRpcMessage &notification)
    {
        std::string uri = JsonUtils::extract_nested_value(notification.params, "textDocument.uri");

        if (!uri.empty())
        {
            document_cache.erase(uri);
        }
    }

    void Server::handle_text_document_completion(const JsonRpcMessage &request)
    {
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Processing completion request");

        try
        {
            std::string uri = JsonUtils::extract_nested_value(request.params, "textDocument.uri");

            // Extract position with error handling
            Position position;
            std::string line_str = JsonUtils::extract_nested_value(request.params, "position.line");
            std::string char_str = JsonUtils::extract_nested_value(request.params, "position.character");

            if (line_str.empty() || char_str.empty())
            {
                throw std::runtime_error("Failed to extract position from completion request");
            }

            position.line = std::stoi(line_str);
            position.character = std::stoi(char_str);

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                           "Completion request for " + uri + " at " + std::to_string(position.line) + ":" + std::to_string(position.character));

            // Use analyzer to get completions safely
            auto completions = analyzer_->get_completions(uri, position);

            JsonRpcMessage response;
            response.jsonrpc = "2.0";
            response.id = request.id;
            response.result = JsonUtils::serialize_completions_array(completions);

            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Sending completion response with " + std::to_string(completions.size()) + " items");
            write_message(serialize_response(response));
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error in completion handler: " + std::string(e.what()));

            JsonRpcMessage error_response;
            error_response.jsonrpc = "2.0";
            error_response.id = request.id;

            std::string error_msg = R"({"code": -32603, "message": "Internal error: )" + std::string(e.what()) + R"("})";
            error_response.error = error_msg;

            write_message(serialize_response(error_response));
        }
    }

    void Server::handle_text_document_hover(const JsonRpcMessage &request)
    {
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Processing hover request");

        try
        {
            // Parse the JSON manually for better control
            std::string uri;
            int line = -1, character = -1;

            // Extract URI using improved method
            std::regex uri_regex("\"textDocument\"\\s*:\\s*\\{[^}]*\"uri\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch uri_match;
            if (std::regex_search(request.params, uri_match, uri_regex))
            {
                uri = uri_match[1].str();
            }

            // Extract position using improved method
            std::regex line_regex("\"position\"\\s*:\\s*\\{[^}]*\"line\"\\s*:\\s*(\\d+)");
            std::regex char_regex("\"position\"\\s*:\\s*\\{[^}]*\"character\"\\s*:\\s*(\\d+)");
            std::smatch line_match, char_match;

            if (std::regex_search(request.params, line_match, line_regex))
            {
                line = std::stoi(line_match[1].str());
            }
            if (std::regex_search(request.params, char_match, char_regex))
            {
                character = std::stoi(char_match[1].str());
            }

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Parsed URI: " + uri);
            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Parsed position: " + std::to_string(line) + "," + std::to_string(character));

            if (uri.empty() || line == -1 || character == -1)
            {
                throw std::runtime_error("Failed to parse request parameters");
            }

            // Use the analyzer to get symbol information
            Position pos = {line, character};
            auto symbol_info = analyzer_->get_hover_info(uri, pos);

            std::string result;
            if (symbol_info)
            {
                // Create rich hover content - build markdown value properly escaped
                std::string markdown_content = "**" + symbol_info->name + "**\\n\\n" +
                                               "**Type:** `" + symbol_info->type_name + "`\\n\\n" +
                                               "**Kind:** " + symbol_info->kind + "\\n\\n";

                if (!symbol_info->signature.empty())
                {
                    markdown_content += "**Signature:** `" + symbol_info->signature + "`\\n\\n";
                }

                if (!symbol_info->documentation.empty())
                {
                    markdown_content += symbol_info->documentation + "\\n\\n";
                }

                markdown_content += "**Scope:** " + symbol_info->scope;

                result = R"({
                    "contents": {
                        "kind": "markdown",
                        "value": ")" +
                         markdown_content + R"("
                    }
                })";
            }
            else
            {
                // Fallback to basic position info
                std::string fallback_content = "**CryoLang**\\n\\nPosition: Line " +
                                               std::to_string(line + 1) + ", Column " + std::to_string(character + 1) +
                                               "\\n\\n*No symbol information available at this position*";

                result = R"({
                    "contents": {
                        "kind": "markdown",
                        "value": ")" +
                         fallback_content + R"("
                    }
                })";
            }

            JsonRpcMessage response;
            response.jsonrpc = "2.0";
            response.id = request.id;
            response.result = result;

            Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Sending hover response");
            write_message(serialize_response(response));
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP, "Error in hover handler: " + std::string(e.what()));

            JsonRpcMessage error_response;
            error_response.jsonrpc = "2.0";
            error_response.id = request.id;

            std::string error_msg = R"({"code": -32603, "message": "Internal error: )" + std::string(e.what()) + R"("})";
            error_response.error = error_msg;

            write_message(serialize_response(error_response));
        }
    }

    void Server::handle_text_document_definition(const JsonRpcMessage &request)
    {
        std::string uri = JsonUtils::extract_nested_value(request.params, "textDocument.uri");

        Position position;
        position.line = std::stoi(JsonUtils::extract_nested_value(request.params, "position.line"));
        position.character = std::stoi(JsonUtils::extract_nested_value(request.params, "position.character"));

        auto definitions = get_definition(uri, position);

        JsonRpcMessage response;
        response.id = request.id;
        response.result = JsonUtils::serialize_locations_array(definitions);

        write_message(serialize_response(response));
    }

    void Server::handle_workspace_symbol(const JsonRpcMessage &request)
    {
        std::string query = JsonUtils::extract_json_value(request.params, "query");

        auto symbols = get_workspace_symbols(query);

        JsonRpcMessage response;
        response.id = request.id;
        response.result = JsonUtils::serialize_symbols_array(symbols);

        write_message(serialize_response(response));
    }

    void Server::handle_text_document_references(const JsonRpcMessage &request)
    {
        std::string uri = JsonUtils::extract_nested_value(request.params, "textDocument.uri");

        Position position;
        position.line = std::stoi(JsonUtils::extract_nested_value(request.params, "position.line"));
        position.character = std::stoi(JsonUtils::extract_nested_value(request.params, "position.character"));

        auto references = get_references(uri, position);

        JsonRpcMessage response;
        response.id = request.id;
        response.result = JsonUtils::serialize_locations_array(references);

        write_message(serialize_response(response));
    }

    void Server::send_diagnostics(const std::string &uri)
    {
        try
        {
            // Get diagnostics from the analyzer safely
            auto diagnostics_info = analyzer_->get_diagnostics(uri);

            // Convert DiagnosticInfo to LSP diagnostic format inline
            std::stringstream diagnostics_json;
            diagnostics_json << "[";
            for (size_t i = 0; i < diagnostics_info.size(); ++i)
            {
                if (i > 0)
                    diagnostics_json << ",";

                const auto &diag = diagnostics_info[i];
                diagnostics_json << "{";
                diagnostics_json << "\"range\":{";
                diagnostics_json << "\"start\":{\"line\":" << diag.range.start.line << ",\"character\":" << diag.range.start.character << "},";
                diagnostics_json << "\"end\":{\"line\":" << diag.range.end.line << ",\"character\":" << diag.range.end.character << "}";
                diagnostics_json << "},";
                diagnostics_json << "\"severity\":" << diag.severity << ",";
                diagnostics_json << "\"message\":\"" << diag.message << "\",";
                diagnostics_json << "\"source\":\"" << diag.source << "\"";
                if (!diag.code.empty())
                {
                    diagnostics_json << ",\"code\":\"" << diag.code << "\"";
                }
                diagnostics_json << "}";
            }
            diagnostics_json << "]";

            std::string params = "{\"uri\":\"" + uri + "\",\"diagnostics\":" + diagnostics_json.str() + "}";

            std::string notification = JsonUtils::create_notification("textDocument/publishDiagnostics", params);

            Cryo::Logger::instance().debug(Cryo::LogComponent::LSP,
                                           "Sending " + std::to_string(diagnostics_info.size()) + " diagnostics for " + uri);

            write_message(notification);
        }
        catch (const std::exception &e)
        {
            Cryo::Logger::instance().error(Cryo::LogComponent::LSP,
                                           "Error sending diagnostics for " + uri + ": " + std::string(e.what()));
        }
    }

    void Server::handle_text_document_document_symbol(const JsonRpcMessage &request)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing document symbol request");

        JsonRpcMessage response;
        response.jsonrpc = "2.0";
        response.id = request.id;
        response.result = "[]";

        write_message(serialize_response(response));
    }

    void Server::handle_text_document_diagnostic(const JsonRpcMessage &request)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing diagnostic request");

        JsonRpcMessage response;
        response.jsonrpc = "2.0";
        response.id = request.id;
        response.result = "{\"kind\": \"full\", \"items\": []}";

        write_message(serialize_response(response));
    }

    void Server::handle_text_document_did_save(const JsonRpcMessage &notification)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing didSave notification");
        std::string uri = JsonUtils::extract_nested_value(notification.params, "textDocument.uri");
        Cryo::Logger::instance().info(Cryo::LogComponent::LSP, "Document saved: " + uri);
    }

    void Server::handle_workspace_did_change_configuration(const JsonRpcMessage &notification)
    {
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Processing workspace configuration change");
    }

    void Server::handle_set_trace(const JsonRpcMessage &notification)
    {
        std::string trace_value = JsonUtils::extract_json_value(notification.params, "value");
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Trace level set to: " + trace_value);
    }

    void Server::handle_cancel_request(const JsonRpcMessage &request)
    {
        std::string cancel_id = JsonUtils::extract_nested_value(request.params, "id");
        Cryo::Logger::instance().debug(Cryo::LogComponent::LSP, "Cancelling request with ID: " + cancel_id);

        // For now, just acknowledge the cancellation
        // In a full implementation, we'd track pending requests and cancel them
        JsonRpcMessage response;
        response.jsonrpc = "2.0";
        response.id = request.id;
        response.result = "null";

        write_message(serialize_response(response));
    }

    // Implementation continues in next part due to length...
} // namespace CryoLSP