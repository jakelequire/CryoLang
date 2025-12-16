#include "LSPServer.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace CryoLSP
{

    LSPServer::LSPServer() : _config({}), _server_socket(0)
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    LSPServer::LSPServer(const Config &config) : _config(config), _server_socket(0)
    {
        std::cerr << "[LSPServer] Constructor starting..." << std::endl;
        std::cout << "[LSPServer] Constructor starting..." << std::endl;
        std::cerr.flush();
        std::cout.flush();
        
        try {
#ifdef _WIN32
            std::cerr << "[LSPServer] Setting up WSA..." << std::endl;
            std::cout << "[LSPServer] Setting up WSA..." << std::endl;
            std::cerr.flush();
            std::cout.flush();
            
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0)
            {
                log_error("WSAStartup failed with error: " + std::to_string(result));
            }
            else
            {
                log_debug("WSAStartup successful");
            }
#endif
            std::cerr << "[LSPServer] Constructor completed successfully" << std::endl;
            std::cout << "[LSPServer] Constructor completed successfully" << std::endl;
            std::cerr.flush();
            std::cout.flush();
            
            log_debug("LSPServer constructor completed");
        } catch (const std::exception& e) {
            std::cerr << "[LSPServer] Exception in constructor: " << e.what() << std::endl;
            std::cout << "[LSPServer] Exception in constructor: " << e.what() << std::endl;
            std::cerr.flush();
            std::cout.flush();
            throw;
        } catch (...) {
            std::cerr << "[LSPServer] Unknown exception in constructor" << std::endl;
            std::cout << "[LSPServer] Unknown exception in constructor" << std::endl;
            std::cerr.flush();
            std::cout.flush();
            throw;
        }
    }

    LSPServer::~LSPServer()
    {
        shutdown();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool LSPServer::initialize()
    {
        log_info("Initializing CryoLang LSP Server v1.0.0");
        log_debug("Server config - Host: " + _config.host + ", Port: " + std::to_string(_config.port));
        log_debug("Max connections: " + std::to_string(_config.max_connections));

        // Initialize compiler
        std::cerr << "[LSPServer] About to create compiler instance..." << std::endl;
        std::cout << "[LSPServer] About to create compiler instance..." << std::endl;
        std::cerr.flush();
        std::cout.flush();
        
        log_debug("Creating compiler instance...");
        try
        {
            std::cerr << "[LSPServer] Calling Cryo::create_compiler_instance()..." << std::endl;
            std::cout << "[LSPServer] Calling Cryo::create_compiler_instance()..." << std::endl;
            std::cerr.flush();
            std::cout.flush();
            
            _compiler = Cryo::create_compiler_instance();
            
            std::cerr << "[LSPServer] create_compiler_instance() returned: " << (_compiler ? "valid pointer" : "NULL") << std::endl;
            std::cout << "[LSPServer] create_compiler_instance() returned: " << (_compiler ? "valid pointer" : "NULL") << std::endl;
            std::cerr.flush();
            std::cout.flush();
        }
        catch (const std::exception &e)
        {
            std::cerr << "[LSPServer] Exception creating compiler instance: " << e.what() << std::endl;
            std::cout << "[LSPServer] Exception creating compiler instance: " << e.what() << std::endl;
            std::cerr.flush();
            std::cout.flush();
            log_error("Exception creating compiler instance: " + std::string(e.what()));
            return false;
        }
        catch (...)
        {
            std::cerr << "[LSPServer] Unknown exception creating compiler instance" << std::endl;
            std::cout << "[LSPServer] Unknown exception creating compiler instance" << std::endl;
            std::cerr.flush();
            std::cout.flush();
            log_error("Unknown exception creating compiler instance");
            return false;
        }

        if (!_compiler)
        {
            std::cerr << "[LSPServer] Compiler instance is NULL!" << std::endl;
            std::cout << "[LSPServer] Compiler instance is NULL!" << std::endl;
            std::cerr.flush();
            std::cout.flush();
            log_error("Failed to create compiler instance - returned null");
            return false;
        }
        
        std::cerr << "[LSPServer] Compiler instance created successfully" << std::endl;
        std::cout << "[LSPServer] Compiler instance created successfully" << std::endl;
        std::cerr.flush();
        std::cout.flush();
        log_debug("Compiler instance created successfully");

        _compiler->set_debug_mode(_config.enable_debug_logging);

        // Initialize LSP providers
        _document_manager = std::make_unique<DocumentManager>();
        _symbol_provider = std::make_unique<SymbolProvider>(_compiler.get());
        _diagnostics_provider = std::make_unique<DiagnosticsProvider>(_compiler.get());
        _hover_provider = std::make_unique<HoverProvider>(_compiler.get(), _symbol_provider.get());
        _completion_provider = std::make_unique<CompletionProvider>(_compiler.get(), _symbol_provider.get());

        // Setup message handlers
        setup_message_handlers();

        log_info("LSP Server initialized successfully");
        return true;
    }

    bool LSPServer::start()
    {
        if (_running.load())
        {
            log_error("Server is already running");
            return false;
        }

        if (!setup_socket())
        {
            return false;
        }

        _running.store(true);
        _shutdown_requested.store(false);

        log_info("Starting LSP server on " + _config.host + ":" + std::to_string(_config.port));

        // Start server thread
        _server_thread = std::thread(&LSPServer::server_loop, this);

        // Output the exact message the VS Code extension is looking for
        std::cout << "Ready to accept client connections" << std::endl;
        std::cout.flush(); // Ensure immediate output
        log_info("Ready to accept client connections");

        return true;
    }

    void LSPServer::stop()
    {
        log_info("Stopping LSP server...");

        _running.store(false);

        // Close server socket to break accept loop
        if (_server_socket != 0)
        {
#ifdef _WIN32
            closesocket(_server_socket);
#else
            close(_server_socket);
#endif
            _server_socket = 0;
        }

        // Wait for server thread
        if (_server_thread.joinable())
        {
            _server_thread.join();
        }

        // Wait for client threads
        std::lock_guard<std::mutex> lock(_clients_mutex);
        for (auto &thread : _client_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        _client_threads.clear();

        log_info("LSP server stopped");
    }

    void LSPServer::shutdown()
    {
        if (_running.load())
        {
            _shutdown_requested.store(true);
            stop();
        }

        cleanup_socket();

        // Clear providers
        _completion_provider.reset();
        _hover_provider.reset();
        _diagnostics_provider.reset();
        _symbol_provider.reset();
        _document_manager.reset();
        _compiler.reset();
    }

    bool LSPServer::setup_socket()
    {
        log_debug("Creating socket...");
        _server_socket = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
        if (_server_socket == INVALID_SOCKET)
        {
            int error = WSAGetLastError();
            log_error("Failed to create socket, WSA Error: " + std::to_string(error));
            return false;
        }
#else
        if (_server_socket < 0)
        {
            log_error("Failed to create socket, errno: " + std::to_string(errno));
            return false;
        }
#endif
        log_debug("Socket created successfully");

        // Set socket options
        int opt = 1;
#ifdef _WIN32
        setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
#else
        setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind socket
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        
        // Use configured host address instead of INADDR_ANY
        if (_config.host == "localhost" || _config.host == "127.0.0.1") {
            address.sin_addr.s_addr = inet_addr("127.0.0.1");
        } else if (_config.host == "0.0.0.0") {
            address.sin_addr.s_addr = INADDR_ANY;
        } else {
            address.sin_addr.s_addr = inet_addr(_config.host.c_str());
        }
        
        address.sin_port = htons(_config.port);

        log_debug("Binding socket to " + _config.host + ":" + std::to_string(_config.port) + "...");
        std::cout << "Attempting to bind to " << _config.host << ":" << _config.port << std::endl;
        std::cout.flush();
        
        if (bind(_server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
#ifdef _WIN32
            int error = WSAGetLastError();
            std::cout << "BIND FAILED: WSA Error " << error << std::endl;
            if (error == WSAEADDRINUSE) {
                std::cout << "ERROR: Port " << _config.port << " is already in use!" << std::endl;
            } else if (error == WSAEACCES) {
                std::cout << "ERROR: Permission denied to bind to port " << _config.port << std::endl;
            }
            log_error("Failed to bind socket to port " + std::to_string(_config.port) + ", WSA Error: " + std::to_string(error));
#else
            std::cout << "BIND FAILED: errno " << errno << std::endl;
            if (errno == EADDRINUSE) {
                std::cout << "ERROR: Port " << _config.port << " is already in use!" << std::endl;
            } else if (errno == EACCES) {
                std::cout << "ERROR: Permission denied to bind to port " << _config.port << std::endl;
            }
            log_error("Failed to bind socket to port " + std::to_string(_config.port) + ", errno: " + std::to_string(errno));
#endif
            std::cout.flush();
            cleanup_socket();
            return false;
        }
        std::cout << "Socket bound successfully to " << _config.host << ":" << _config.port << std::endl;
        std::cout.flush();
        log_debug("Socket bound successfully");

        // Listen for connections
        log_debug("Setting up socket to listen...");
        std::cout << "Setting up socket to listen (max connections: " << _config.max_connections << ")..." << std::endl;
        std::cout.flush();
        
        if (listen(_server_socket, _config.max_connections) < 0)
        {
#ifdef _WIN32
            int error = WSAGetLastError();
            std::cout << "LISTEN FAILED: WSA Error " << error << std::endl;
            log_error("Failed to listen on socket, WSA Error: " + std::to_string(error));
#else
            std::cout << "LISTEN FAILED: errno " << errno << std::endl;
            log_error("Failed to listen on socket, errno: " + std::to_string(errno));
#endif
            std::cout.flush();
            cleanup_socket();
            return false;
        }
        std::cout << "Socket is now listening for connections" << std::endl;
        std::cout.flush();
        log_debug("Socket listening successfully");

        return true;
    }

    void LSPServer::cleanup_socket()
    {
        if (_server_socket != 0)
        {
#ifdef _WIN32
            closesocket(_server_socket);
#else
            close(_server_socket);
#endif
            _server_socket = 0;
        }
    }

    void LSPServer::server_loop()
    {
        log_info("Server loop started, listening for connections...");

        while (_running.load() && !_shutdown_requested.load())
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            socket_t client_socket = accept(_server_socket, (struct sockaddr *)&client_addr, &client_len);

#ifdef _WIN32
            if (client_socket == INVALID_SOCKET)
            {
                if (_running.load())
                {
                    int error = WSAGetLastError();
                    log_error("Failed to accept client connection, WSA Error: " + std::to_string(error));
                }
                continue;
            }
#else
            if (client_socket < 0)
            {
                if (_running.load())
                {
                    log_error("Failed to accept client connection, errno: " + std::to_string(errno));
                }
                continue;
            }
#endif

            log_info("New client connected");

            // Start client handler thread
            std::lock_guard<std::mutex> lock(_clients_mutex);
            _client_threads.emplace_back(&LSPServer::handle_client, this, client_socket);
        }

        log_info("Server loop ended");
    }

    void LSPServer::handle_client(socket_t client_socket)
    {
        log_debug("Client handler started");

        while (_running.load() && !_shutdown_requested.load())
        {
            try
            {
                std::string raw_message = read_message(client_socket);
                if (raw_message.empty())
                {
                    break; // Client disconnected
                }

                log_debug("Received message: " + raw_message.substr(0, 200) + (raw_message.length() > 200 ? "..." : ""));

                LSPMessage message = parse_message(raw_message);

                if (message.is_request())
                {
                    LSPRequest request = LSPRequest::from_message(message);

                    // Find and call appropriate handler
                    auto handler_it = _message_handlers.find(request.method);
                    if (handler_it != _message_handlers.end())
                    {
                        LSPResponse response = handler_it->second(request, client_socket);
                        std::string response_str = serialize_response(response);
                        send_message(client_socket, response_str);
                    }
                    else
                    {
                        log_error("Unknown method: " + request.method);
                        LSPResponse error_response;
                        error_response.id = request.id;
                        error_response.error = JsonValue::object({{"code", JsonValue(-32601)},
                                                                  {"message", JsonValue("Method not found")}});
                        std::string response_str = serialize_response(error_response);
                        send_message(client_socket, response_str);
                    }
                }
                else if (message.is_notification())
                {
                    // Handle notification
                    LSPRequest notification;
                    notification.method = *message.method;
                    notification.params = message.params.value_or(json::object());

                    auto handler_it = _message_handlers.find(notification.method);
                    if (handler_it != _message_handlers.end())
                    {
                        handler_it->second(notification, client_socket);
                    }
                }
            }
            catch (const std::exception &e)
            {
                log_error("Error handling client message: " + std::string(e.what()));
                break;
            }
        }

#ifdef _WIN32
        closesocket(client_socket);
#else
        close(client_socket);
#endif

        log_debug("Client handler ended");
    }

    std::string LSPServer::read_message(socket_t socket)
    {
        // Read Content-Length header
        std::string header;
        char c;

        while (true)
        {
            int result = recv(socket, &c, 1, 0);
            if (result <= 0)
                return "";

            header += c;
            if (header.length() >= 4 && header.substr(header.length() - 4) == "\r\n\r\n")
            {
                break;
            }
        }

        // Parse Content-Length
        size_t content_length = 0;
        std::istringstream header_stream(header);
        std::string line;

        while (std::getline(header_stream, line))
        {
            if (line.substr(0, 14) == "Content-Length")
            {
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos)
                {
                    content_length = std::stoul(line.substr(colon_pos + 1));
                }
            }
        }

        if (content_length == 0)
        {
            return "";
        }

        // Read message content
        std::string content(content_length, '\0');
        size_t bytes_read = 0;

        while (bytes_read < content_length)
        {
            int result = recv(socket, &content[bytes_read], content_length - bytes_read, 0);
            if (result <= 0)
                return "";
            bytes_read += result;
        }

        return content;
    }

    bool LSPServer::send_message(socket_t socket, const std::string &message)
    {
        std::string header = "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n";
        std::string full_message = header + message;

        size_t bytes_sent = 0;
        while (bytes_sent < full_message.length())
        {
            int result = send(socket, full_message.c_str() + bytes_sent, full_message.length() - bytes_sent, 0);
            if (result <= 0)
            {
                return false;
            }
            bytes_sent += result;
        }

        log_debug("Sent response: " + message.substr(0, 200) + (message.length() > 200 ? "..." : ""));
        return true;
    }

    LSPMessage LSPServer::parse_message(const std::string &raw_message)
    {
        LSPMessage message;

        try
        {
            json j = JsonValue::parse(raw_message);

            // Get jsonrpc field, default to "2.0"
            if (j.is_object() && j.as_object().contains("jsonrpc"))
            {
                message.jsonrpc = j.as_object().at("jsonrpc").as_string();
            }
            else
            {
                message.jsonrpc = "2.0";
            }

            if (j.is_object() && j.as_object().contains("id"))
            {
                message.id = j.as_object().at("id");
            }

            if (j.is_object() && j.as_object().contains("method"))
            {
                message.method = j.as_object().at("method").as_string();
            }

            if (j.is_object() && j.as_object().contains("params"))
            {
                message.params = j.as_object().at("params");
            }

            if (j.is_object() && j.as_object().contains("result"))
            {
                message.result = j.as_object().at("result");
            }

            if (j.is_object() && j.as_object().contains("error"))
            {
                message.error = j.as_object().at("error");
            }
        }
        catch (const std::exception &e)
        {
            log_error("Failed to parse message: " + std::string(e.what()));
        }

        return message;
    }

    std::string LSPServer::serialize_response(const LSPResponse &response)
    {
        return response.to_json().dump();
    }

    void LSPServer::setup_message_handlers()
    {
        _message_handlers["initialize"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_initialize(req, socket);
        };

        _message_handlers["initialized"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_initialized(req, socket);
        };

        _message_handlers["shutdown"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_shutdown(req, socket);
        };

        _message_handlers["exit"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_exit(req, socket);
        };

        _message_handlers["textDocument/didOpen"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_did_open(req, socket);
        };

        _message_handlers["textDocument/didChange"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_did_change(req, socket);
        };

        _message_handlers["textDocument/didSave"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_did_save(req, socket);
        };

        _message_handlers["textDocument/didClose"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_did_close(req, socket);
        };

        _message_handlers["textDocument/hover"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_hover(req, socket);
        };

        _message_handlers["textDocument/completion"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_completion(req, socket);
        };

        _message_handlers["textDocument/definition"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_definition(req, socket);
        };

        _message_handlers["textDocument/references"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_references(req, socket);
        };

        _message_handlers["textDocument/documentSymbol"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_text_document_document_symbol(req, socket);
        };

        _message_handlers["workspace/symbol"] = [this](const LSPRequest &req, socket_t socket)
        {
            return handle_workspace_symbol(req, socket);
        };
    }

    // Message handler implementations will continue in the next part...
    void LSPServer::log_debug(const std::string &message)
    {
        if (_config.enable_debug_logging)
        {
            std::cout << "[DEBUG] " << message << std::endl;
        }
    }

    void LSPServer::log_info(const std::string &message)
    {
        std::cout << "[INFO] " << message << std::endl;
    }

    void LSPServer::log_error(const std::string &message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
    }

    std::string LSPServer::uri_to_path(const std::string &uri)
    {
        if (uri.substr(0, 7) == "file://")
        {
            std::string path = uri.substr(7);
#ifdef _WIN32
            // Convert /C:/path to C:/path on Windows
            if (path.length() > 2 && path[0] == '/' && path[2] == ':')
            {
                path = path.substr(1);
            }
            // Replace forward slashes with backslashes
            std::replace(path.begin(), path.end(), '/', '\\');
#endif
            return path;
        }
        return uri;
    }

    std::string LSPServer::path_to_uri(const std::string &path)
    {
        std::string uri_path = path;
#ifdef _WIN32
        // Replace backslashes with forward slashes
        std::replace(uri_path.begin(), uri_path.end(), '\\', '/');
        // Add leading slash for Windows absolute paths
        if (uri_path.length() > 1 && uri_path[1] == ':')
        {
            uri_path = "/" + uri_path;
        }
#endif
        return "file://" + uri_path;
    }

} // namespace CryoLSP
